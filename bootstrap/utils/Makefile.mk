# -*- Mode: Makefile; -*-
#
# (C) 2008 by Argonne National Laboratory.
#     See COPYRIGHT in top-level directory.
#

libhydra_a_SOURCES += $(top_srcdir)/bootstrap/utils/bscu_finalize.c \
	$(top_srcdir)/bootstrap/utils/bscu_query_node_list.c \
	$(top_srcdir)/bootstrap/utils/bscu_query_partition_id.c \
	$(top_srcdir)/bootstrap/utils/bscu_usize.c \
	$(top_srcdir)/bootstrap/utils/bscu_wait.c
