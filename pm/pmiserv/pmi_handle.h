/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2008 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#ifndef PMI_HANDLE_H_INCLUDED
#define PMI_HANDLE_H_INCLUDED

#include "hydra_base.h"

#define MAXKEYLEN    64 /* max length of key in keyval space */
#define MAXVALLEN   256 /* max length of value in keyval space */
#define MAXNAMELEN  256 /* max length of various names */
#define MAXKVSNAME  MAXNAMELEN  /* max length of a kvsname */

typedef struct HYD_PMCD_pmi_kvs_pair {
    char key[MAXKEYLEN];
    char val[MAXVALLEN];
    struct HYD_PMCD_pmi_kvs_pair *next;
} HYD_PMCD_pmi_kvs_pair_t;

typedef struct HYD_PMCD_pmi_kvs {
    char kvs_name[MAXNAMELEN];  /* Name of this kvs */
    HYD_PMCD_pmi_kvs_pair_t *key_pair;
} HYD_PMCD_pmi_kvs_t;

typedef struct HYD_PMCD_pmi_pg HYD_PMCD_pmi_pg_t;
typedef struct HYD_PMCD_pmi_process HYD_PMCD_pmi_process_t;

struct HYD_PMCD_pmi_process {
    /* This is a bad design if we need to tie in an FD to a PMI
     * process. This essentially kills any chance of PMI server
     * masquerading. However, PMI v1 requires this state to be
     * maintained. */
    int fd;
    struct HYD_PMCD_pmi_pg *pg;
    struct HYD_PMCD_pmi_process *next;
};

struct HYD_PMCD_pmi_pg {
    int id;

    int num_procs;              /* Number of processes in the group */
    int barrier_count;

    struct HYD_PMCD_pmi_process *process;
    HYD_PMCD_pmi_kvs_t *kvs;

    struct HYD_PMCD_pmi_pg *next;
};

HYD_Status HYD_PMCD_pmi_create_pg(void);
HYD_Status HYD_PMCD_pmi_init(int fd, char *args[]);
HYD_Status HYD_PMCD_pmi_finalize(void);

extern HYD_PMCD_pmi_pg_t *pg_list;

struct HYD_PMCD_pmi_handle {
    char *cmd;
     HYD_Status(*handler) (int fd, char *args[]);
};

extern struct HYD_PMCD_pmi_handle *HYD_PMCD_pmi_handle_list;

#endif /* PMI_HANDLE_H_INCLUDED */