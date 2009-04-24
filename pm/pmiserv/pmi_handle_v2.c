/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2008 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "hydra.h"
#include "hydra_utils.h"
#include "bsci.h"
#include "demux.h"
#include "pmi_handle.h"
#include "pmi_handle_v2.h"

HYD_Handle handle;
HYD_PMCD_pmi_pg_t *pg_list;

/* TODO: abort, create_kvs, destroy_kvs, getbyidx, spawn */
static struct HYD_PMCD_pmi_handle_fns pmi_v2_handle_fns_foo[] = {
    {"fullinit", HYD_PMCD_pmi_handle_v2_fullinit},
    {"job-getid", HYD_PMCD_pmi_handle_v2_job_getid},
    {"info-putnodeattr", HYD_PMCD_pmi_handle_v2_info_putnodeattr},
    {"info-getnodeattr", HYD_PMCD_pmi_handle_v2_info_getnodeattr},
    {"info-getjobattr", HYD_PMCD_pmi_handle_v2_info_getjobattr},
    {"kvs-put", HYD_PMCD_pmi_handle_v2_kvs_put},
    {"kvs-get", HYD_PMCD_pmi_handle_v2_kvs_get},
    {"kvs-fence", HYD_PMCD_pmi_handle_v2_kvs_fence},
    {"finalize", HYD_PMCD_pmi_handle_v2_finalize},
    {"\0", NULL}
};

static struct HYD_PMCD_pmi_handle pmi_v2_foo = { PMI_V2_DELIM, pmi_v2_handle_fns_foo };

struct HYD_PMCD_pmi_handle *HYD_PMCD_pmi_v2 = &pmi_v2_foo;

struct token {
    char *key;
    char *val;
};

static struct attr_reqs {
    int fd;
    char *thrid;
    char **req;
    struct attr_reqs *next;
} *outstanding_attr_reqs = NULL;

static void print_attr_reqs(void)
{
    int i;
    struct attr_reqs *areq;

    printf("Outstanding reqs: ");
    for (areq = outstanding_attr_reqs; areq; areq = areq->next) {
        printf("%d(", areq->fd);
        for (i = 0; areq->req[i]; i++) {
            printf("%s", areq->req[i]);
            if (areq->req[i+1])
                printf(",");
        }
        printf(") ");
    }
    printf("\n");
}

static HYD_Status send_command(int fd, char *cmd)
{
    char cmdlen[7];
    HYD_Status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    MPIU_Snprintf(cmdlen, 7, "%6d", strlen(cmd));
    status = HYDU_sock_write(fd, cmdlen, 6);
    HYDU_ERR_POP(status, "error writing PMI line\n");

    status = HYDU_sock_write(fd, cmd, strlen(cmd));
    HYDU_ERR_POP(status, "error writing PMI line\n");

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}


static HYD_Status args_to_tokens(char *args[], struct token **tokens, int *count)
{
    int i;
    char *arg;
    HYD_Status status = HYD_SUCCESS;

    for (i = 0; args[i]; i++);
    *count = i;
    HYDU_MALLOC(*tokens, struct token *, *count * sizeof(struct token), status);

    for (i = 0; args[i]; i++) {
        arg = HYDU_strdup(args[i]);
        (*tokens)[i].key = strtok(arg, "=");
        (*tokens)[i].val = strtok(NULL, "=");
    }

fn_exit:
    return status;

fn_fail:
    goto fn_exit;
}

static int progress_nest_count = 0;
static int req_complete = 0;

static void free_attr_req(struct attr_reqs *areq)
{
    HYDU_free_strlist(areq->req);
    HYDU_FREE(areq);
}

static HYD_Status poke_progress(void)
{
    struct attr_reqs *areq, *tmp;
    int i;
    HYD_Status status = HYD_SUCCESS;

    progress_nest_count++;

    if (outstanding_attr_reqs == NULL)
        goto fn_exit;

    for (areq = outstanding_attr_reqs; areq;) {
        req_complete = 0;

        status = HYD_PMCD_pmi_handle_v2_info_getnodeattr(areq->fd, areq->req);
        HYDU_ERR_POP(status, "getnodeattr returned error\n");

        tmp = areq->next;
        if (req_complete) {
            if (areq == outstanding_attr_reqs) {
                outstanding_attr_reqs = areq->next;
            }
            else {
                for (tmp = outstanding_attr_reqs; tmp->next != areq; tmp = tmp->next);
                tmp->next = areq->next;
            }
            tmp = areq->next;
            free_attr_req(areq);
        }

        areq = tmp;
    }

fn_exit:
    progress_nest_count--;
    return status;

fn_fail:
    goto fn_exit;
}


char *find_token_keyval(struct token *tokens, int count, char *key)
{
    int i;

    for (i = 0; i < count; i++) {
        if (!strcmp(tokens[i].key, key))
            return tokens[i].val;
    }

    return NULL;
}


HYD_Status HYD_PMCD_pmi_handle_v2_fullinit(int fd, char *args[])
{
    int rank, size, debug, i;
    char *tmp[HYD_NUM_TMP_STRINGS], *cmd, *rank_str;
    struct HYD_Partition *partition;
    struct HYD_Partition_exec *exec;
    HYD_PMCD_pmi_pg_t *run;
    struct token *tokens;
    int token_count;
    HYD_Status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = args_to_tokens(args, &tokens, &token_count);
    HYDU_ERR_POP(status, "unable to convert args to tokens\n");

    rank_str = find_token_keyval(tokens, token_count, "pmirank");
    HYDU_ERR_CHKANDJUMP(status, rank_str == NULL, HYD_INTERNAL_ERROR,
                        "unable to find pmirank token\n");

    rank = atoi(rank_str);

    size = 0;
    for (partition = handle.partition_list; partition && partition->exec_list;
         partition = partition->next)
        for (exec = partition->exec_list; exec; exec = exec->next)
            size += exec->proc_count;

    debug = handle.debug;

    i = 0;
    tmp[i++] = HYDU_strdup("cmd=fullinit-response;pmi-version=2;pmi-subversion=0;rank=");
    tmp[i++] = HYDU_int_to_str(rank);
    tmp[i++] = HYDU_strdup(";size=");
    tmp[i++] = HYDU_int_to_str(size);
    tmp[i++] = HYDU_strdup(";appnum=0;debugged=FALSE;pmiverbose=0;rc=0;");
    tmp[i++] = NULL;

    status = HYDU_str_alloc_and_join(tmp, &cmd);
    HYDU_ERR_POP(status, "error while joining strings\n");

    for (i = 0; tmp[i]; i++)
        HYDU_FREE(tmp[i]);

    status = send_command(fd, cmd);
    HYDU_ERR_POP(status, "send command failed\n");

    HYDU_FREE(cmd);

    run = pg_list;
    while (run->next)
        run = run->next;

    /* Add the process to the last PG */
    status = HYD_PMCD_pmi_add_process_to_pg(run, fd, rank);
    HYDU_ERR_POP(status, "unable to add process to pg\n");

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}


HYD_Status HYD_PMCD_pmi_handle_v2_job_getid(int fd, char *args[])
{
    char *tmp[HYD_NUM_TMP_STRINGS], *cmd, *thrid;
    int i;
    HYD_PMCD_pmi_process_t *process;
    struct token *tokens;
    int token_count;
    HYD_Status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = args_to_tokens(args, &tokens, &token_count);
    HYDU_ERR_POP(status, "unable to convert args to tokens\n");

    thrid = find_token_keyval(tokens, token_count, "thrid");

    /* Find the group id corresponding to this fd */
    process = HYD_PMCD_pmi_find_process(fd);
    if (process == NULL)        /* We didn't find the process */
        HYDU_ERR_SETANDJUMP1(status, HYD_INTERNAL_ERROR,
                             "unable to find process structure for fd %d\n", fd);

    i = 0;
    tmp[i++] = HYDU_strdup("cmd=job-getid-response;");
    if (thrid) {
        tmp[i++] = HYDU_strdup("thrid=");
        tmp[i++] = HYDU_strdup(thrid);
        tmp[i++] = HYDU_strdup(";");
    }
    tmp[i++] = HYDU_strdup("jobid=");
    tmp[i++] = HYDU_strdup(process->node->pg->kvs->kvs_name);
    tmp[i++] = HYDU_strdup(";rc=0;");
    tmp[i++] = NULL;

    status = HYDU_str_alloc_and_join(tmp, &cmd);
    HYDU_ERR_POP(status, "unable to join strings\n");

    HYDU_free_strlist(tmp);

    status = send_command(fd, cmd);
    HYDU_ERR_POP(status, "send command failed\n");

    HYDU_FREE(cmd);

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}


HYD_Status HYD_PMCD_pmi_handle_v2_info_putnodeattr(int fd, char *args[])
{
    char *tmp[HYD_NUM_TMP_STRINGS], *cmd, *key_pair_str;
    char *key, *val, *thrid;
    int i, ret;
    HYD_PMCD_pmi_process_t *process;
    struct token *tokens;
    int token_count;
    HYD_Status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = args_to_tokens(args, &tokens, &token_count);
    HYDU_ERR_POP(status, "unable to convert args to tokens\n");

    key = find_token_keyval(tokens, token_count, "key");
    HYDU_ERR_CHKANDJUMP(status, key == NULL, HYD_INTERNAL_ERROR,
                        "unable to find key token\n");

    val = find_token_keyval(tokens, token_count, "value");
    HYDU_ERR_CHKANDJUMP(status, val == NULL, HYD_INTERNAL_ERROR,
                        "unable to find value token\n");

    thrid = find_token_keyval(tokens, token_count, "thrid");

    /* Find the group id corresponding to this fd */
    process = HYD_PMCD_pmi_find_process(fd);
    if (process == NULL)        /* We didn't find the process */
        HYDU_ERR_SETANDJUMP1(status, HYD_INTERNAL_ERROR,
                             "unable to find process structure for fd %d\n", fd);

    status = HYD_PMCD_pmi_add_kvs(key, val, process->node->kvs, &key_pair_str, &ret);
    HYDU_ERR_POP(status, "unable to put data into kvs\n");

    i = 0;
    tmp[i++] = HYDU_strdup("cmd=info-putnodeattr-response;");
    if (thrid) {
        tmp[i++] = HYDU_strdup("thrid=");
        tmp[i++] = HYDU_strdup(thrid);
        tmp[i++] = HYDU_strdup(";");
    }
    tmp[i++] = HYDU_strdup("rc=");
    tmp[i++] = HYDU_int_to_str(ret);
    tmp[i++] = HYDU_strdup(";");
    tmp[i++] = NULL;

    status = HYDU_str_alloc_and_join(tmp, &cmd);
    HYDU_ERR_POP(status, "unable to join strings\n");

    HYDU_free_strlist(tmp);

    status = send_command(fd, cmd);
    HYDU_ERR_POP(status, "send command failed\n");

    HYDU_FREE(cmd);

    /* Poke the progress engine before exiting */
    status = poke_progress();
    HYDU_ERR_POP(status, "poke progress error\n");

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}


HYD_Status HYD_PMCD_pmi_handle_v2_info_getnodeattr(int fd, char *args[])
{
    int i, found;
    HYD_PMCD_pmi_process_t *process;
    HYD_PMCD_pmi_kvs_pair_t *run;
    char *key, *wait, *thrid;
    char *tmp[HYD_NUM_TMP_STRINGS] = { 0 }, *cmd;
    struct token *tokens;
    int token_count;
    struct attr_reqs *attr_req, *a;
    HYD_Status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = args_to_tokens(args, &tokens, &token_count);
    HYDU_ERR_POP(status, "unable to convert args to tokens\n");

    key = find_token_keyval(tokens, token_count, "key");
    HYDU_ERR_CHKANDJUMP(status, key == NULL, HYD_INTERNAL_ERROR,
                        "unable to find key token\n");

    wait = find_token_keyval(tokens, token_count, "wait");
    thrid = find_token_keyval(tokens, token_count, "thrid");

    /* Find the group id corresponding to this fd */
    process = HYD_PMCD_pmi_find_process(fd);
    if (process == NULL)        /* We didn't find the process */
        HYDU_ERR_SETANDJUMP1(status, HYD_INTERNAL_ERROR,
                             "unable to find process structure for fd %d\n", fd);

    found = 0;
    for (run = process->node->kvs->key_pair; run; run = run->next) {
        if (!strcmp(run->key, key)) {
            found = 1;
            break;
        }
    }

    if (!found) { /* We need to decide whether to return not found or queue up */
        /* If we are already nested, get out of here */
        if (progress_nest_count)
            goto fn_exit;

        if (wait && !strcmp(wait, "TRUE")) {
            /* queue up */
            HYDU_MALLOC(attr_req, struct attr_reqs *, sizeof(struct attr_reqs), status);
            attr_req->fd = fd;
            attr_req->next = NULL;

            status = HYDU_strdup_list(args, &attr_req->req);
            HYDU_ERR_POP(status, "unable to dup args\n");

            if (outstanding_attr_reqs == NULL)
                outstanding_attr_reqs = attr_req;
            else {
                a = outstanding_attr_reqs;
                while (a->next)
                    a = a->next;
                a->next = attr_req;
            }
        }
        else {
            /* Tell the client that we can't find the attribute */
            tmp[i++] = HYDU_strdup("cmd=info-getnodeattr-response;");
            if (thrid) {
                tmp[i++] = HYDU_strdup("thrid=");
                tmp[i++] = HYDU_strdup(thrid);
                tmp[i++] = HYDU_strdup(";");
            }
            tmp[i++] = HYDU_strdup("found=FALSE;rc=0;");
            tmp[i++] = NULL;

            status = HYDU_str_alloc_and_join(tmp, &cmd);
            HYDU_ERR_POP(status, "unable to join strings\n");
            HYDU_free_strlist(tmp);

            status = send_command(fd, cmd);
            HYDU_ERR_POP(status, "send command failed\n");
            HYDU_FREE(cmd);
        }
    }
    else { /* We found the attribute */
        i = 0;
        tmp[i++] = HYDU_strdup("cmd=info-getnodeattr-response;");
        if (thrid) {
            tmp[i++] = HYDU_strdup("thrid=");
            tmp[i++] = HYDU_strdup(thrid);
            tmp[i++] = HYDU_strdup(";");
        }
        tmp[i++] = HYDU_strdup("found=TRUE;value=");
        tmp[i++] = HYDU_strdup(run->val);
        tmp[i++] = HYDU_strdup(";rc=0;");
        tmp[i++] = NULL;

        status = HYDU_str_alloc_and_join(tmp, &cmd);
        HYDU_ERR_POP(status, "unable to join strings\n");
        HYDU_free_strlist(tmp);

        status = send_command(fd, cmd);
        HYDU_ERR_POP(status, "send command failed\n");
        HYDU_FREE(cmd);

        if (progress_nest_count)
            req_complete = 1;
    }

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}


HYD_Status HYD_PMCD_pmi_handle_v2_info_getjobattr(int fd, char *args[])
{
    int i;
    HYD_PMCD_pmi_process_t *process;
    HYD_PMCD_pmi_kvs_pair_t *run;
    char *key, *thrid;
    char *tmp[HYD_NUM_TMP_STRINGS], *cmd;
    struct token *tokens;
    int token_count;
    HYD_Status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = args_to_tokens(args, &tokens, &token_count);
    HYDU_ERR_POP(status, "unable to convert args to tokens\n");

    key = find_token_keyval(tokens, token_count, "key");
    HYDU_ERR_CHKANDJUMP(status, key == NULL, HYD_INTERNAL_ERROR,
                        "unable to find key token\n");

    thrid = find_token_keyval(tokens, token_count, "thrid");

    /* Find the group id corresponding to this fd */
    process = HYD_PMCD_pmi_find_process(fd);
    if (process == NULL)        /* We didn't find the process */
        HYDU_ERR_SETANDJUMP1(status, HYD_INTERNAL_ERROR,
                             "unable to find process structure for fd %d\n", fd);

    i = 0;
    tmp[i++] = HYDU_strdup("cmd=info-getjobattr-response;");
    if (thrid) {
        tmp[i++] = HYDU_strdup("thrid=");
        tmp[i++] = HYDU_strdup(thrid);
        tmp[i++] = HYDU_strdup(";");
    }
    tmp[i++] = HYDU_strdup("found=");
    if (process->node->pg->kvs->key_pair == NULL) {
        tmp[i++] = HYDU_strdup("FALSE;rc=0;");
    }
    else {
        run = process->node->pg->kvs->key_pair;
        while (run) {
            if (!strcmp(run->key, key)) {
                tmp[i++] = HYDU_strdup("TRUE;value=");
                tmp[i++] = HYDU_strdup(run->val);
                tmp[i++] = HYDU_strdup(";rc=0;");
                break;
            }
            run = run->next;
        }
        if (run == NULL) {
            tmp[i++] = HYDU_strdup("FALSE;rc=0;");
        }
    }
    tmp[i++] = NULL;

    status = HYDU_str_alloc_and_join(tmp, &cmd);
    HYDU_ERR_POP(status, "unable to join strings\n");

    HYDU_free_strlist(tmp);

    status = send_command(fd, cmd);
    HYDU_ERR_POP(status, "send command failed\n");

    HYDU_FREE(cmd);

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}


HYD_Status HYD_PMCD_pmi_handle_v2_kvs_put(int fd, char *args[])
{
    char *tmp[HYD_NUM_TMP_STRINGS], *cmd, *key_pair_str;
    char *key, *val, *thrid;
    int i, ret;
    HYD_PMCD_pmi_process_t *process;
    struct token *tokens;
    int token_count;
    HYD_Status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = args_to_tokens(args, &tokens, &token_count);
    HYDU_ERR_POP(status, "unable to convert args to tokens\n");

    key = find_token_keyval(tokens, token_count, "key");
    HYDU_ERR_CHKANDJUMP(status, key == NULL, HYD_INTERNAL_ERROR,
                        "unable to find key token\n");

    val = find_token_keyval(tokens, token_count, "value");
    HYDU_ERR_CHKANDJUMP(status, val == NULL, HYD_INTERNAL_ERROR,
                        "unable to find value token\n");

    thrid = find_token_keyval(tokens, token_count, "thrid");

    /* Find the group id corresponding to this fd */
    process = HYD_PMCD_pmi_find_process(fd);
    if (process == NULL)        /* We didn't find the process */
        HYDU_ERR_SETANDJUMP1(status, HYD_INTERNAL_ERROR,
                             "unable to find process structure for fd %d\n", fd);

    status = HYD_PMCD_pmi_add_kvs(key, val, process->node->pg->kvs, &key_pair_str, &ret);
    HYDU_ERR_POP(status, "unable to put data into kvs\n");

    i = 0;
    tmp[i++] = HYDU_strdup("cmd=kvs-put-response;");
    if (thrid) {
        tmp[i++] = HYDU_strdup("thrid=");
        tmp[i++] = HYDU_strdup(thrid);
        tmp[i++] = HYDU_strdup(";");
    }
    tmp[i++] = HYDU_strdup("rc=");
    tmp[i++] = HYDU_int_to_str(ret);
    tmp[i++] = HYDU_strdup(";");
    tmp[i++] = NULL;

    status = HYDU_str_alloc_and_join(tmp, &cmd);
    HYDU_ERR_POP(status, "unable to join strings\n");

    HYDU_free_strlist(tmp);

    status = send_command(fd, cmd);
    HYDU_ERR_POP(status, "send command failed\n");

    HYDU_FREE(cmd);

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}


HYD_Status HYD_PMCD_pmi_handle_v2_kvs_get(int fd, char *args[])
{
    int i;
    HYD_PMCD_pmi_process_t *process;
    HYD_PMCD_pmi_kvs_pair_t *run;
    char *key, *thrid;
    char *tmp[HYD_NUM_TMP_STRINGS], *cmd;
    struct token *tokens;
    int token_count;
    HYD_Status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = args_to_tokens(args, &tokens, &token_count);
    HYDU_ERR_POP(status, "unable to convert args to tokens\n");

    key = find_token_keyval(tokens, token_count, "key");
    HYDU_ERR_CHKANDJUMP(status, key == NULL, HYD_INTERNAL_ERROR,
                        "unable to find key token\n");

    thrid = find_token_keyval(tokens, token_count, "thrid");

    /* Find the group id corresponding to this fd */
    process = HYD_PMCD_pmi_find_process(fd);
    if (process == NULL)        /* We didn't find the process */
        HYDU_ERR_SETANDJUMP1(status, HYD_INTERNAL_ERROR,
                             "unable to find process structure for fd %d\n", fd);

    i = 0;
    tmp[i++] = HYDU_strdup("cmd=kvs-get-response;");
    if (thrid) {
        tmp[i++] = HYDU_strdup("thrid=");
        tmp[i++] = HYDU_strdup(thrid);
        tmp[i++] = HYDU_strdup(";");
    }
    tmp[i++] = HYDU_strdup("found=");
    if (process->node->pg->kvs->key_pair == NULL) {
        tmp[i++] = HYDU_strdup("FALSE;rc=0;");
    }
    else {
        for (run = process->node->pg->kvs->key_pair; run; run = run->next) {
            if (!strcmp(run->key, key)) {
                tmp[i++] = HYDU_strdup("TRUE;value=");
                tmp[i++] = HYDU_strdup(run->val);
                tmp[i++] = HYDU_strdup(";rc=0;");
                break;
            }
        }
        if (run == NULL) {
            tmp[i++] = HYDU_strdup("FALSE;rc=0;");
        }
    }
    tmp[i++] = NULL;

    status = HYDU_str_alloc_and_join(tmp, &cmd);
    HYDU_ERR_POP(status, "unable to join strings\n");
    HYDU_free_strlist(tmp);

    status = send_command(fd, cmd);
    HYDU_ERR_POP(status, "send command failed\n");

    HYDU_FREE(cmd);

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}


HYD_Status HYD_PMCD_pmi_handle_v2_kvs_fence(int fd, char *args[])
{
    HYD_PMCD_pmi_process_t *process, *prun;
    HYD_PMCD_pmi_node_t *node;
    char *tmp[HYD_NUM_TMP_STRINGS], *cmd, *thrid;
    struct token *tokens;
    int token_count, i;
    HYD_Status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = args_to_tokens(args, &tokens, &token_count);
    HYDU_ERR_POP(status, "unable to convert args to tokens\n");

    thrid = find_token_keyval(tokens, token_count, "thrid");

    /* Find the group id corresponding to this fd */
    process = HYD_PMCD_pmi_find_process(fd);
    if (process == NULL)        /* We didn't find the process */
        HYDU_ERR_SETANDJUMP1(status, HYD_INTERNAL_ERROR,
                             "unable to find process structure for fd %d\n", fd);

    process->node->pg->barrier_count++;

    /* All the processes have arrived at the barrier; send a
     * barrier_out message to everyone. */
    if (process->node->pg->barrier_count == process->node->pg->num_procs) {
        i = 0;
        tmp[i++] = HYDU_strdup("cmd=kvs-fence-response;");
        if (thrid) {
            tmp[i++] = HYDU_strdup("thrid=");
            tmp[i++] = HYDU_strdup(thrid);
            tmp[i++] = HYDU_strdup(";");
        }
        tmp[i++] = HYDU_strdup("rc=0;");
        tmp[i++] = NULL;

        status = HYDU_str_alloc_and_join(tmp, &cmd);
        HYDU_ERR_POP(status, "unable to join strings\n");
        HYDU_free_strlist(tmp);

        for (node = process->node->pg->node_list; node; node = node->next) {
            for (prun = node->process_list; prun; prun = prun->next) {
                status = send_command(prun->fd, cmd);
                HYDU_ERR_POP(status, "send command failed\n");
            }
        }
        HYDU_FREE(cmd);
        process->node->pg->barrier_count = 0;
    }

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}


HYD_Status HYD_PMCD_pmi_handle_v2_finalize(int fd, char *args[])
{
    char *thrid;
    char *tmp[HYD_NUM_TMP_STRINGS], *cmd;
    struct token *tokens;
    int token_count, i;
    HYD_Status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = args_to_tokens(args, &tokens, &token_count);
    HYDU_ERR_POP(status, "unable to convert args to tokens\n");

    thrid = find_token_keyval(tokens, token_count, "thrid");

    i = 0;
    tmp[i++] = HYDU_strdup("cmd=finalize-response;");
    if (thrid) {
        tmp[i++] = HYDU_strdup("thrid=");
        tmp[i++] = HYDU_strdup(thrid);
        tmp[i++] = HYDU_strdup(";");
    }
    tmp[i++] = HYDU_strdup("rc=0;");
    tmp[i++] = NULL;

    status = HYDU_str_alloc_and_join(tmp, &cmd);
    HYDU_ERR_POP(status, "unable to join strings\n");
    HYDU_free_strlist(tmp);

    status = send_command(fd, cmd);
    HYDU_ERR_POP(status, "send command failed\n");
    HYDU_FREE(cmd);

    if (status == HYD_SUCCESS) {
        status = HYD_DMX_deregister_fd(fd);
        HYDU_ERR_POP(status, "unable to register fd\n");
        close(fd);
    }

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}
