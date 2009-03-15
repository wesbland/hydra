/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2008 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "hydra_base.h"
#include "bsci.h"

struct HYD_BSCI_fns HYD_BSCI_fns;

HYD_Status HYD_BSCI_launch_procs(void)
{
    HYD_Status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = HYD_BSCI_fns.launch_procs();

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}