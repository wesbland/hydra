/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2008 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "hydra.h"
#include "hydra_utils.h"
#include "pmci.h"
#include "bsci.h"
#include "debugger.h"
#include "pmiserv.h"
#include "pmiserv_utils.h"
#include "pmiserv_pmi.h"

HYD_status HYD_pmcd_pmiserv_pmi_listen_cb(int fd, HYD_event_t events, void *userp)
{
    int accept_fd, pgid;
    struct HYD_pg *pg;
    struct HYD_pmcd_pmi_pg_scratch *pg_scratch;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    /* We got a PMI connection; find the PGID */
    pgid = ((int) (size_t) userp);
    for (pg = &HYD_handle.pg_list; pg && pg->pgid != pgid; pg = pg->next);
    if (!pg)
        HYDU_ERR_SETANDJUMP1(status, HYD_INTERNAL_ERROR, "cannot find pg with id %d\n", pgid);

    pg_scratch = (struct HYD_pmcd_pmi_pg_scratch *) pg->pg_scratch;
    pg_scratch->pmi_listen_fd = fd;

    status = HYDU_sock_accept(fd, &accept_fd);
    HYDU_ERR_POP(status, "accept error\n");

    status = HYDT_dmx_register_fd(1, &accept_fd, HYD_POLLIN, userp, HYD_pmcd_pmiserv_pmi_cb);
    HYDU_ERR_POP(status, "unable to register fd\n");

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

static HYD_status handle_pmi_cmd(int fd, int pgid, int pid, char *buf, int pmi_version)
{
    char *args[HYD_NUM_TMP_STRINGS], *cmd = NULL;
    struct HYD_pmcd_pmi_handle *h;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    if (pmi_version == 1)
        HYD_pmcd_pmi_handle = HYD_pmcd_pmi_v1;
    else
        HYD_pmcd_pmi_handle = HYD_pmcd_pmi_v2;

    if (HYD_handle.user_global.debug)
        HYDU_dump(stdout, "[pgid: %d] got PMI command: %s\n", pgid, buf);

    status = HYD_pmcd_pmi_parse_pmi_cmd(buf, pmi_version, &cmd, args);
    HYDU_ERR_POP(status, "unable to parse PMI command\n");

    h = HYD_pmcd_pmi_handle;
    while (h->handler) {
        if (!strcmp(cmd, h->cmd)) {
            status = h->handler(fd, pid, pgid, args);
            HYDU_ERR_POP(status, "PMI handler returned error\n");
            break;
        }
        h++;
    }
    if (!h->handler) {
        /* We don't understand the command */
        HYDU_ERR_SETANDJUMP1(status, HYD_INTERNAL_ERROR,
                             "Unrecognized PMI command: %s | cleaning up processes\n", cmd);
    }

  fn_exit:
    if (cmd)
        HYDU_FREE(cmd);
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    /* Cleanup all the processes */
    status = HYD_pmcd_pmiserv_cleanup();
    HYDU_ERR_POP(status, "unable to cleanup processes\n");
    goto fn_exit;
}

HYD_status HYD_pmcd_pmiserv_pmi_cb(int fd, HYD_event_t events, void *userp)
{
    int pmi_version, pgid, closed;
    char *buf;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    /* We got a PMI command; find the PGID */
    pgid = ((int) (size_t) userp);

    status = HYD_pmcd_pmi_read_pmi_cmd(fd, &buf, &pmi_version, &closed);
    HYDU_ERR_POP(status, "unable to read PMI command\n");

    if (closed) {
        /* This is not a clean close. If a finalize was called, we
         * would have deregistered this socket. The application might
         * have aborted. Just cleanup all the processes */
        status = HYD_pmcd_pmiserv_cleanup();
        if (status != HYD_SUCCESS) {
            HYDU_warn_printf("bootstrap server returned error cleaning up processes\n");
            status = HYD_SUCCESS;
            goto fn_fail;
        }

        status = HYDT_dmx_deregister_fd(fd);
        if (status != HYD_SUCCESS) {
            HYDU_warn_printf("unable to deregister fd %d\n", fd);
            status = HYD_SUCCESS;
            goto fn_fail;
        }

        close(fd);
        goto fn_exit;
    }

    /* If we got the PMI command directly, just pass the PID as -1,
     * since the control fd is not shared for PMI communication */
    status = handle_pmi_cmd(fd, pgid, -1, buf, pmi_version);
    HYDU_ERR_POP(status, "unable to process PMI command\n");

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

static HYD_status handle_pid_list(int fd, struct HYD_proxy *proxy)
{
    int count;
    struct HYD_proxy *tproxy;
    struct HYD_pg *pg = proxy->pg;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    HYDU_MALLOC(proxy->pid, int *, proxy->proxy_process_count * sizeof(int), status);
    status = HYDU_sock_read(fd, (void *) proxy->pid,
                            proxy->proxy_process_count * sizeof(int),
                            &count, HYDU_SOCK_COMM_MSGWAIT);
    HYDU_ERR_POP(status, "unable to read status from proxy\n");

    if (pg->pgid) {
        /* We initialize the debugger code only for non-dynamically
         * spawned processes */
        goto fn_exit;
    }

    /* Check if all the PIDs have been received */
    for (tproxy = pg->proxy_list; tproxy; tproxy = tproxy->next)
        if (tproxy->pid == NULL)
            goto fn_exit;

    /* Call the debugger initialization */
    status = HYDT_dbg_setup_procdesc(pg);
    HYDU_ERR_POP(status, "debugger setup failed\n");

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

static HYD_status handle_exit_status(int fd, struct HYD_proxy *proxy)
{
    int count;
    struct HYD_proxy *tproxy;
    struct HYD_pg *pg = proxy->pg;
    struct HYD_pmcd_pmi_pg_scratch *pg_scratch;
    struct HYD_pmcd_pmi_proxy_scratch *proxy_scratch;
    struct HYD_pmcd_pmi_process *process, *tmp;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    HYDU_MALLOC(proxy->exit_status, int *, proxy->proxy_process_count * sizeof(int), status);
    status = HYDU_sock_read(fd, (void *) proxy->exit_status,
                            proxy->proxy_process_count * sizeof(int),
                            &count, HYDU_SOCK_COMM_MSGWAIT);
    HYDU_ERR_POP(status, "unable to read status from proxy\n");

    status = HYDT_dmx_deregister_fd(fd);
    HYDU_ERR_POP(status, "error deregistering fd\n");

    close(fd);

    for (tproxy = pg->proxy_list; tproxy; tproxy = tproxy->next) {
        if (tproxy->exit_status == NULL)
            goto fn_exit;
    }

    /* All proxies in this process group have terminated */
    pg_scratch = (struct HYD_pmcd_pmi_pg_scratch *) pg->pg_scratch;

    /* If the PMI listen fd has been initialized, deregister it */
    if (pg_scratch->pmi_listen_fd != -1) {
        status = HYDT_dmx_deregister_fd(pg_scratch->pmi_listen_fd);
        HYDU_ERR_POP(status, "unable to deregister PMI listen fd\n");
    }

    status = HYDT_dmx_deregister_fd(pg_scratch->control_listen_fd);
    HYDU_ERR_POP(status, "unable to deregister control listen fd\n");

    close(pg_scratch->pmi_listen_fd);
    close(pg_scratch->control_listen_fd);

    HYD_pmcd_pmi_free_pg_scratch(pg);

    for (tproxy = pg->proxy_list; tproxy; tproxy = tproxy->next) {
        proxy_scratch = (struct HYD_pmcd_pmi_proxy_scratch *) tproxy->proxy_scratch;

        if (proxy_scratch) {
            for (process = proxy_scratch->process_list; process;) {
                tmp = process->next;
                HYDU_FREE(process);
                process = tmp;
            }

            HYD_pmcd_free_pmi_kvs_list(proxy_scratch->kvs);
            HYDU_FREE(proxy_scratch);
            tproxy->proxy_scratch = NULL;
        }
    }

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

static HYD_status control_cb(int fd, HYD_event_t events, void *userp)
{
    int count;
    enum HYD_pmcd_pmi_cmd cmd;
    struct HYD_pmcd_pmi_cmd_hdr hdr;
    struct HYD_proxy *proxy;
    char *buf;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    proxy = (struct HYD_proxy *) userp;

    status = HYDU_sock_read(fd, &cmd, sizeof(cmd), &count, HYDU_SOCK_COMM_MSGWAIT);
    HYDU_ERR_POP(status, "unable to read command from proxy\n");

    if (cmd == PID_LIST) {      /* Got PIDs */
        status = handle_pid_list(fd, proxy);
        HYDU_ERR_POP(status, "unable to receive PID list\n");
    }
    else if (cmd == EXIT_STATUS) {
        status = handle_exit_status(fd, proxy);
        HYDU_ERR_POP(status, "unable to receive exit status\n");
    }
    else if (cmd == PMI_CMD) {
        status = HYDU_sock_read(fd, &hdr, sizeof(hdr), &count, HYDU_SOCK_COMM_MSGWAIT);
        HYDU_ERR_POP(status, "unable to read PMI header from proxy\n");

        HYDU_MALLOC(buf, char *, hdr.buflen + 1, status);

        status = HYDU_sock_read(fd, buf, hdr.buflen, &count, HYDU_SOCK_COMM_MSGWAIT);
        HYDU_ERR_POP(status, "unable to read PMI command from proxy\n");

        buf[hdr.buflen] = 0;

        status = handle_pmi_cmd(fd, proxy->pg->pgid, hdr.pid, buf, hdr.pmi_version);
        HYDU_ERR_POP(status, "unable to process PMI command\n");
    }

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

static HYD_status send_exec_info(struct HYD_proxy *proxy)
{
    enum HYD_pmcd_pmi_cmd cmd;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    cmd = PROC_INFO;
    status = HYDU_sock_write(proxy->control_fd, &cmd, sizeof(enum HYD_pmcd_pmi_cmd));
    HYDU_ERR_POP(status, "unable to write data to proxy\n");

    status = HYDU_send_strlist(proxy->control_fd, proxy->exec_launch_info);
    HYDU_ERR_POP(status, "error sending exec info\n");

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

HYD_status HYD_pmcd_pmiserv_control_listen_cb(int fd, HYD_event_t events, void *userp)
{
    int accept_fd, proxy_id, count, pgid;
    struct HYD_pg *pg;
    struct HYD_proxy *proxy;
    struct HYD_pmcd_pmi_pg_scratch *pg_scratch;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    /* We got a control socket connection */
    status = HYDU_sock_accept(fd, &accept_fd);
    HYDU_ERR_POP(status, "accept error\n");

    /* Get the PGID of the connection */
    pgid = ((int) (size_t) userp);

    /* Read the proxy ID */
    status = HYDU_sock_read(accept_fd, &proxy_id, sizeof(int), &count, HYDU_SOCK_COMM_MSGWAIT);
    HYDU_ERR_POP(status, "sock read returned error\n");

    /* Find the process group */
    for (pg = &HYD_handle.pg_list; pg; pg = pg->next)
        if (pg->pgid == pgid)
            break;
    if (!pg)
        HYDU_ERR_SETANDJUMP1(status, HYD_INTERNAL_ERROR, "could not find pg with ID %d\n",
                             pgid);

    /* Find the proxy */
    for (proxy = pg->proxy_list; proxy; proxy = proxy->next) {
        if (proxy->proxy_id == proxy_id)
            break;
    }
    HYDU_ERR_CHKANDJUMP1(status, proxy == NULL, HYD_INTERNAL_ERROR,
                         "cannot find proxy with ID %d\n", proxy_id);

    pg_scratch = (struct HYD_pmcd_pmi_pg_scratch *) pg->pg_scratch;
    pg_scratch->control_listen_fd = fd;

    /* This will be the control socket for this proxy */
    proxy->control_fd = accept_fd;

    /* Send out the executable information */
    status = send_exec_info(proxy);
    HYDU_ERR_POP(status, "unable to send exec info to proxy\n");

    status = HYDT_dmx_register_fd(1, &accept_fd, HYD_POLLIN, proxy, control_cb);
    HYDU_ERR_POP(status, "unable to register fd\n");

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

static int in_cleanup = 0;

HYD_status HYD_pmcd_pmiserv_cleanup(void)
{
    struct HYD_pg *pg;
    struct HYD_proxy *proxy;
    enum HYD_pmcd_pmi_cmd cmd;
    HYD_status status = HYD_SUCCESS, overall_status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    if (in_cleanup)
        goto fn_exit;

    in_cleanup = 1;

    /* FIXME: Instead of doing this from this process itself, fork a
     * bunch of processes to do this. */
    /* Connect to all proxies and send a KILL command */
    for (pg = &HYD_handle.pg_list; pg; pg = pg->next) {
        for (proxy = pg->proxy_list; proxy; proxy = proxy->next) {
            while (proxy->control_fd == -1) {
                /* Wait for the proxy to connect back */
                status = HYDT_bsci_wait_for_completion(-1);
                HYDU_ERR_POP(status,
                             "bootstrap server returned error waiting for completion\n");
            }

            if (proxy->exit_status) {
                /* Already got the exit status on this fd; skip it */
                continue;
            }

            cmd = KILL_JOB;
            status =
                HYDU_sock_trywrite(proxy->control_fd, &cmd, sizeof(enum HYD_pmcd_pmi_cmd));
            if (status != HYD_SUCCESS) {
                HYDU_warn_printf("unable to send data to the proxy on %s\n",
                                 proxy->node.hostname);
                overall_status = HYD_INTERNAL_ERROR;
            }
        }
    }

  fn_exit:
    HYDU_FUNC_EXIT();
    return overall_status;

  fn_fail:
    goto fn_exit;
}

static HYD_status ckpoint(void)
{
    struct HYD_pg *pg;
    struct HYD_proxy *proxy;
    enum HYD_pmcd_pmi_cmd cmd;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    /* FIXME: Instead of doing this from this process itself, fork a
     * bunch of processes to do this. */
    /* Connect to all proxies and send the checkpoint command */
    for (pg = &HYD_handle.pg_list; pg; pg = pg->next) {
        for (proxy = pg->proxy_list; proxy; proxy = proxy->next) {
            cmd = CKPOINT;
            status = HYDU_sock_write(proxy->control_fd, &cmd, sizeof(enum HYD_pmcd_pmi_cmd));
            HYDU_ERR_POP(status, "unable to send checkpoint message\n");
        }
    }

    HYDU_FUNC_EXIT();

  fn_exit:
    return status;

  fn_fail:
    goto fn_exit;
}

void HYD_pmcd_pmiserv_signal_cb(int sig)
{
    HYDU_FUNC_ENTER();

    if (sig == SIGINT || sig == SIGQUIT || sig == SIGTERM) {
        /* There's nothing we can do with the return value for now. */
        HYD_pmcd_pmiserv_cleanup();
    }
    else if (sig == SIGUSR1) {
        ckpoint();
    }
    /* Ignore other signals for now */

    HYDU_FUNC_EXIT();
    return;
}
