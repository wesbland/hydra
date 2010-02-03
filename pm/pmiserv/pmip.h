/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2008 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#ifndef PMIP_H_INCLUDED
#define PMIP_H_INCLUDED

#include "hydra_base.h"
#include "hydra_utils.h"
#include "pmi_common.h"

struct HYD_pmcd_pmip {
    struct HYD_user_global user_global;

    struct {
        int enable_stdin;
        int global_core_count;

        /* PMI */
        char *pmi_port;
        int pmi_id;             /* If this is -1, we auto-generate it */
        char *pmi_kvsname;
    } system_global;            /* Global system parameters */

    struct {
        /* Upstream server contact information */
        char *server_name;
        int server_port;
        int control;
    } upstream;

    /* Currently our downstream only consists of actual MPI
     * processes */
    struct {
        int *out;
        int *err;
        int in;

        int *pid;
        int *exit_status;

        int *pmi_id;
        int *pmi_fd;
    } downstream;

    /* Proxy details */
    struct {
        int id;
        int pgid;
        char *interface_env_name;
        char *hostname;
        char *local_binding;

        int proxy_core_count;
        int proxy_process_count;
    } local;

    /* Process segmentation information for this proxy */
    int start_pid;
    struct HYD_exec *exec_list;
};

extern struct HYD_pmcd_pmip HYD_pmcd_pmip;
extern struct HYD_arg_match_table HYD_pmcd_pmip_match_table[];

/* utils */
HYD_status HYD_pmcd_pmip_get_params(char **t_argv);
void HYD_pmcd_pmip_killjob(void);

/* callback */
HYD_status HYD_pmcd_pmip_control_cmd_cb(int fd, HYD_event_t events, void *userp);
HYD_status HYD_pmcd_pmip_stdout_cb(int fd, HYD_event_t events, void *userp);
HYD_status HYD_pmcd_pmip_stderr_cb(int fd, HYD_event_t events, void *userp);
HYD_status HYD_pmcd_pmip_stdin_cb(int fd, HYD_event_t events, void *userp);

#endif /* PMIP_H_INCLUDED */
