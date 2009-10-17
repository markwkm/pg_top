/*
 * Copyright (c) 2008-2009, Mark Wong
 */

#ifndef _REMOTE_H_
#define _REMOTE_H_

#include "machine.h"

int machine_init_r(struct statics *, char *);
void get_system_info_r(struct system_info *, char *);
caddr_t get_process_info_r(struct system_info *, struct process_select *, int,
		char *);
char *format_header_r(char *);
char *format_next_io_r(caddr_t);
char *format_next_process_r(caddr_t);

#endif /* _REMOTE_H_ */
