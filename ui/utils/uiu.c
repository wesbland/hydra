/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2008 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "hydra.h"
#include "hydra_utils.h"
#include "uiu.h"

void HYD_UIU_init_params(void)
{
    HYD_handle.base_path = NULL;
    HYD_handle.proxy_port = -1;
    HYD_handle.launch_mode = HYD_LAUNCH_UNSET;

    HYD_handle.bootstrap = NULL;
    HYD_handle.css = NULL;
    HYD_handle.rmk = NULL;
    HYD_handle.binding = NULL;
    HYD_handle.bindlib = NULL;
    HYD_handle.user_bind_map = NULL;

    HYD_handle.ckpointlib = NULL;
    HYD_handle.ckpoint_int = -1;
    HYD_handle.ckpoint_prefix = NULL;
    HYD_handle.ckpoint_restart = 0;

    HYD_handle.debug = -1;
    HYD_handle.print_rank_map = -1;
    HYD_handle.print_all_exitcodes = -1;
    HYD_handle.enablex = -1;
    HYD_handle.pm_env = -1;
    HYD_handle.wdir = NULL;
    HYD_handle.host_file = NULL;

    HYD_handle.ranks_per_proc = -1;
    HYD_handle.bootstrap_exec = NULL;

    HYD_handle.global_env.inherited = NULL;
    HYD_handle.global_env.system = NULL;
    HYD_handle.global_env.user = NULL;
    HYD_handle.global_env.prop = NULL;

    HYD_handle.stdin_cb = NULL;
    HYD_handle.stdout_cb = NULL;
    HYD_handle.stderr_cb = NULL;

    /* FIXME: Should the timers be initialized? */

    HYD_handle.global_core_count = 0;
    HYD_handle.exec_info_list = NULL;
    HYD_handle.proxy_list = NULL;

    HYD_handle.func_depth = 0;
    HYD_handle.stdin_buf_offset = 0;
    HYD_handle.stdin_buf_count = 0;
}


void HYD_UIU_free_params(void)
{
    if (HYD_handle.base_path)
        HYDU_FREE(HYD_handle.base_path);

    if (HYD_handle.bootstrap)
        HYDU_FREE(HYD_handle.bootstrap);

    if (HYD_handle.css)
        HYDU_FREE(HYD_handle.css);

    if (HYD_handle.rmk)
        HYDU_FREE(HYD_handle.rmk);

    if (HYD_handle.binding)
        HYDU_FREE(HYD_handle.binding);

    if (HYD_handle.bindlib)
        HYDU_FREE(HYD_handle.bindlib);

    if (HYD_handle.user_bind_map)
        HYDU_FREE(HYD_handle.user_bind_map);

    if (HYD_handle.ckpointlib)
        HYDU_FREE(HYD_handle.ckpointlib);

    if (HYD_handle.ckpoint_prefix)
        HYDU_FREE(HYD_handle.ckpoint_prefix);

    if (HYD_handle.wdir)
        HYDU_FREE(HYD_handle.wdir);

    if (HYD_handle.host_file)
        HYDU_FREE(HYD_handle.host_file);

    if (HYD_handle.bootstrap_exec)
        HYDU_FREE(HYD_handle.bootstrap_exec);

    if (HYD_handle.global_env.inherited)
        HYDU_env_free_list(HYD_handle.global_env.inherited);

    if (HYD_handle.global_env.system)
        HYDU_env_free_list(HYD_handle.global_env.system);

    if (HYD_handle.global_env.user)
        HYDU_env_free_list(HYD_handle.global_env.user);

    if (HYD_handle.global_env.prop)
        HYDU_FREE(HYD_handle.global_env.prop);

    if (HYD_handle.exec_info_list)
        HYDU_free_exec_info_list(HYD_handle.exec_info_list);

    if (HYD_handle.proxy_list)
        HYDU_free_proxy_list(HYD_handle.proxy_list);

    /* Re-initialize everything to default values */
    HYD_UIU_init_params();
}


HYD_Status HYD_UIU_get_current_exec_info(struct HYD_Exec_info **exec_info)
{
    HYD_Status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    if (HYD_handle.exec_info_list == NULL) {
        status = HYDU_alloc_exec_info(&HYD_handle.exec_info_list);
        HYDU_ERR_POP(status, "unable to allocate exec_info\n");
    }

    *exec_info = HYD_handle.exec_info_list;
    while ((*exec_info)->next)
        *exec_info = (*exec_info)->next;

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}


static HYD_Status add_exec_info_to_proxy(struct HYD_Exec_info *exec_info,
                                         struct HYD_Proxy *proxy,
                                         int num_procs)
{
    int i;
    struct HYD_Proxy_exec *exec;
    HYD_Status status = HYD_SUCCESS;

    if (proxy->exec_list == NULL) {
        status = HYDU_alloc_proxy_exec(&proxy->exec_list);
        HYDU_ERR_POP(status, "unable to allocate proxy exec\n");

        proxy->exec_list->pgid = 0; /* This is the COMM_WORLD exec */

        for (i = 0; exec_info->exec[i]; i++)
            proxy->exec_list->exec[i] = HYDU_strdup(exec_info->exec[i]);
        proxy->exec_list->exec[i] = NULL;

        proxy->exec_list->proc_count = num_procs;
        proxy->exec_list->env_prop = exec_info->env_prop ?
            HYDU_strdup(exec_info->env_prop) : NULL;
        proxy->exec_list->user_env = HYDU_env_list_dup(exec_info->user_env);
    }
    else {
        for (exec = proxy->exec_list; exec->next; exec = exec->next);
        status = HYDU_alloc_proxy_exec(&exec->next);
        HYDU_ERR_POP(status, "unable to allocate proxy exec\n");

        exec = exec->next;
        exec->pgid = 0; /* This is the COMM_WORLD exec */

        for (i = 0; exec_info->exec[i]; i++)
            exec->exec[i] = HYDU_strdup(exec_info->exec[i]);
        exec->exec[i] = NULL;

        exec->proc_count = num_procs;
        exec->env_prop = exec_info->env_prop ? HYDU_strdup(exec_info->env_prop) : NULL;
        exec->user_env = HYDU_env_list_dup(exec_info->user_env);
    }

  fn_exit:
    return status;

  fn_fail:
    goto fn_exit;
}


HYD_Status HYD_UIU_merge_exec_info_to_proxy(void)
{
    int proxy_rem_procs, exec_rem_procs;
    struct HYD_Proxy *proxy;
    struct HYD_Exec_info *exec_info;
    HYD_Status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    for (proxy = HYD_handle.proxy_list; proxy; proxy = proxy->next)
        HYD_handle.global_core_count += proxy->proxy_core_count;

    proxy = HYD_handle.proxy_list;
    exec_info = HYD_handle.exec_info_list;
    proxy_rem_procs = proxy->proxy_core_count;
    exec_rem_procs = exec_info ? exec_info->process_count : 0;
    while (exec_info) {
        if (exec_rem_procs <= proxy_rem_procs) {
            status = add_exec_info_to_proxy(exec_info, proxy, exec_rem_procs);
            HYDU_ERR_POP(status, "unable to add executable to proxy\n");

            proxy_rem_procs -= exec_rem_procs;
            if (proxy_rem_procs == 0) {
                proxy = proxy->next;
                if (proxy == NULL)
                    proxy = HYD_handle.proxy_list;
                proxy_rem_procs = proxy->proxy_core_count;
            }

            exec_info = exec_info->next;
            exec_rem_procs = exec_info ? exec_info->process_count : 0;
        }
        else {
            status = add_exec_info_to_proxy(exec_info, proxy, proxy_rem_procs);
            HYDU_ERR_POP(status, "unable to add executable to proxy\n");

            exec_rem_procs -= proxy_rem_procs;

            proxy = proxy->next;
            if (proxy == NULL)
                proxy = HYD_handle.proxy_list;
            proxy_rem_procs = proxy->proxy_core_count;
        }
    }

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}


void HYD_UIU_print_params(void)
{
    HYD_Env_t *env;
    int i;
    struct HYD_Proxy *proxy;
    struct HYD_Proxy_segment *segment;
    struct HYD_Proxy_exec *exec;
    struct HYD_Exec_info *exec_info;

    HYDU_FUNC_ENTER();

    HYDU_dump(stdout, "\n");
    HYDU_dump(stdout, "=================================================");
    HYDU_dump(stdout, "=================================================");
    HYDU_dump(stdout, "\n");
    HYDU_dump(stdout, "mpiexec options:\n");
    HYDU_dump(stdout, "----------------\n");
    HYDU_dump(stdout, "  Base path: %s\n", HYD_handle.base_path);
    HYDU_dump(stdout, "  Proxy port: %d\n", HYD_handle.proxy_port);
    HYDU_dump(stdout, "  Bootstrap server: %s\n", HYD_handle.bootstrap);
    HYDU_dump(stdout, "  Debug level: %d\n", HYD_handle.debug);
    HYDU_dump(stdout, "  Enable X: %d\n", HYD_handle.enablex);
    HYDU_dump(stdout, "  Working dir: %s\n", HYD_handle.wdir);
    HYDU_dump(stdout, "  Host file: %s\n", HYD_handle.host_file);

    HYDU_dump(stdout, "\n");
    HYDU_dump(stdout, "  Global environment:\n");
    HYDU_dump(stdout, "  -------------------\n");
    for (env = HYD_handle.global_env.inherited; env; env = env->next)
        HYDU_dump(stdout, "    %s=%s\n", env->env_name, env->env_value);

    if (HYD_handle.global_env.system) {
        HYDU_dump(stdout, "\n");
        HYDU_dump(stdout, "  Hydra internal environment:\n");
        HYDU_dump(stdout, "  ---------------------------\n");
        for (env = HYD_handle.global_env.system; env; env = env->next)
            HYDU_dump(stdout, "    %s=%s\n", env->env_name, env->env_value);
    }

    if (HYD_handle.global_env.user) {
        HYDU_dump(stdout, "\n");
        HYDU_dump(stdout, "  User set environment:\n");
        HYDU_dump(stdout, "  ---------------------\n");
        for (env = HYD_handle.global_env.user; env; env = env->next)
            HYDU_dump(stdout, "    %s=%s\n", env->env_name, env->env_value);
    }

    HYDU_dump(stdout, "\n\n");

    HYDU_dump(stdout, "    Executable information:\n");
    HYDU_dump(stdout, "    **********************\n");
    i = 1;
    for (exec_info = HYD_handle.exec_info_list; exec_info; exec_info = exec_info->next) {
        HYDU_dump(stdout, "      Executable ID: %2d\n", i++);
        HYDU_dump(stdout, "      -----------------\n");
        HYDU_dump(stdout, "        Process count: %d\n", exec_info->process_count);
        HYDU_dump(stdout, "        Executable: ");
        HYDU_print_strlist(exec_info->exec);
        HYDU_dump(stdout, "\n");

        if (exec_info->user_env) {
            HYDU_dump(stdout, "\n");
            HYDU_dump(stdout, "        User set environment:\n");
            HYDU_dump(stdout, "        .....................\n");
            for (env = exec_info->user_env; env; env = env->next)
                HYDU_dump(stdout, "          %s=%s\n", env->env_name, env->env_value);
        }
    }

    HYDU_dump(stdout, "    Proxy information:\n");
    HYDU_dump(stdout, "    *********************\n");
    i = 1;
    for (proxy = HYD_handle.proxy_list; proxy; proxy = proxy->next) {
        HYDU_dump(stdout, "      Proxy ID: %2d\n", i++);
        HYDU_dump(stdout, "      -----------------\n");
        HYDU_dump(stdout, "        Proxy name: %s\n", proxy->hostname);
        HYDU_dump(stdout, "        Process count: %d\n", proxy->proxy_core_count);
        HYDU_dump(stdout, "\n");
        HYDU_dump(stdout, "        Proxy segment list:\n");
        HYDU_dump(stdout, "        .......................\n");
        for (segment = proxy->segment_list; segment; segment = segment->next)
            HYDU_dump(stdout, "          Start PID: %d; Process count: %d\n",
                      segment->start_pid, segment->proc_count);
        HYDU_dump(stdout, "\n");
        HYDU_dump(stdout, "        Proxy exec list:\n");
        HYDU_dump(stdout, "        ....................\n");
        for (exec = proxy->exec_list; exec; exec = exec->next)
            HYDU_dump(stdout, "          Exec: %s; Process count: %d\n", exec->exec[0],
                      exec->proc_count);
    }

    HYDU_dump(stdout, "\n");
    HYDU_dump(stdout, "=================================================");
    HYDU_dump(stdout, "=================================================");
    HYDU_dump(stdout, "\n\n");

    HYDU_FUNC_EXIT();

    return;
}
