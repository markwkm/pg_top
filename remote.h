/*
 * Copyright (c) 2008-2019, Mark Wong
 */

#ifndef _REMOTE_H_
#define _REMOTE_H_

#include "machine.h"

int machine_init_r(struct statics *, struct pg_conninfo_ctx *);
void get_system_info_r(struct system_info *, struct pg_conninfo_ctx *);
caddr_t get_process_info_r(struct system_info *, struct process_select *, int,
		struct pg_conninfo_ctx *, int);
char *format_header_r(char *);
char *format_next_io_r(caddr_t);
char *format_next_process_r(caddr_t);
char *format_next_replication_r(caddr_t);

extern char fmt_header_io_r[];
extern char fmt_header_replication_r[];

#endif /* _REMOTE_H_ */
