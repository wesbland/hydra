/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2008 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#ifndef RMK_PBS_H_INCLUDED
#define RMK_PBS_H_INCLUDED

#include "hydra_base.h"

HYD_status HYD_rmkd_pbs_query_node_list(int *num_nodes, struct HYD_proxy **proxy_list);

#endif /* RMK_PBS_H_INCLUDED */
