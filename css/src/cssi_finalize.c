/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2008 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "hydra_base.h"
#include "cssi.h"

struct HYD_CSSI_fns HYD_CSSI_fns;

HYD_Status HYD_CSSI_finalize(void)
{
    HYD_Status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = HYD_CSSI_fns.finalize();

    HYDU_FUNC_EXIT();

    return status;
}