/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2008 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#ifndef CKPOINT_BLCR_H_INCLUDED
#define CKPOINT_BLCR_H_INCLUDED

HYD_Status HYDU_ckpoint_blcr_init(void);
HYD_Status HYDU_ckpoint_blcr_suspend(const char *prefix);
HYD_Status HYDU_ckpoint_blcr_restart(const char *prefix, HYD_Env_t * envlist, int num_ranks,
                                     int ranks[], int *in, int *out, int *err);

#endif /* CKPOINT_BLCR_H_INCLUDED */
