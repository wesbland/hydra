/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#ifndef MPLSTR_H_INCLUDED
#define MPLSTR_H_INCLUDED

#include "mplbase.h"

/* *INDENT-ON* */
#if defined(__cplusplus)
extern "C" {
#endif
/* *INDENT-OFF* */

/* Provide a fallback snprintf for systems that do not have one */
#ifdef HAVE_SNPRINTF
#define MPL_Snprintf snprintf
/* Sometimes systems don't provide prototypes for snprintf */
#ifdef NEEDS_SNPRINTF_DECL
extern int snprintf(char *, size_t, const char *, ...) ATTRIBUTE((format(printf,3,4)));
#endif
#else
int MPL_Snprintf(char *str, size_t size, const char *format, ...)
    ATTRIBUTE((format(printf,3,4)));
#endif /* HAVE_SNPRINTF */

#ifdef HAVE_STRDUP
/* Watch for the case where strdup is defined as a macro by a header include */
# if defined(NEEDS_STRDUP_DECL) && !defined(strdup)
extern char *strdup(const char *);
# endif
#define MPL_Strdup(a)    strdup(a)
#else
/* Don't define MPL_Strdup, provide it ourselves */
char *MPL_Strdup(const char *);
#endif /* HAVE_STRDUP */

/* *INDENT-ON* */
#if defined(__cplusplus)
}
#endif
/* *INDENT-OFF* */

#endif /* MPLSTR_H_INCLUDED */