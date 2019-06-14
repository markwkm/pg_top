/*
 * machine/m_common.c
 *
 * Functionalities common to all the platforms.
 *
 * Copyright (c) 2013 VMware, Inc. All Rights Reserved.
 */
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <libpq-fe.h>

#include "machine.h"

char *backendstatenames[] =
{
	"", "idle", "active", "idltxn", "fast", "abort", "disabl", NULL
};

char *procstatenames[] =
{
	"", " idle, ", " active, ", " idle txn, ", " fastpath, ", " aborted, ",
	" disabled, ", NULL
};

void
update_state(int *pgstate, char *state)
{
	if (strcmp(state, "idle") == 0)
		*pgstate = STATE_IDLE;
	else if (strcmp(state, "active") == 0)
		*pgstate = STATE_RUNNING;
	else if (strcmp(state, "idle in transaction") == 0)
		*pgstate = STATE_IDLEINTRANSACTION;
	else if (strcmp(state, "fastpath function call") == 0)
		*pgstate = STATE_FASTPATH;
	else if (strcmp(state, "idle in transaction (aborted)") == 0)
		*pgstate = STATE_IDLEINTRANSACTION_ABORTED;
	else if (strcmp(state, "disabled") == 0)
		*pgstate = STATE_DISABLED;
	else
		*pgstate = STATE_UNDEFINED;
}

void
update_str(char **old, char *new)
{
	if (*old == NULL)
		*old = strdup(new);
	else if (strcmp(*old, new) != 0)
	{
		free(*old);
		*old = strdup(new);
	}
}
