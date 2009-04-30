/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2008 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "hydra_base.h"
#include "bsci.h"
#include "rsh.h"

struct HYD_BSCI_fns HYD_BSCI_fns;

HYD_Status HYD_BSCI_rsh_init(void)
{
    HYD_Status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    HYD_BSCI_fns.launch_procs = HYD_BSCD_rsh_launch_procs;

    HYDU_FUNC_EXIT();

    return status;
}