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

char	   *backendstatenames[] =
{
	"", "idle", "active", "idltxn", "fast", "abort", "disabl", NULL
};

char	   *procstatenames[] =
{
	" other background task(s), ", " idle, ", " active, ", " idle txn, ",
	" fastpath, ", " aborted, ", " disabled, ", NULL
};

char		fmt_header_replication[] =
"    PID USERNAME APPLICATION          CLIENT STATE     PRIMARY    SENT       WRITE      FLUSH      REPLAY      SLAG  WLAG  FLAG  RLAG";

void
update_state(int *pgstate, char *state)
{
	/*
	 * pgstate is always cleared to 0 when the node is created, so it will be
	 * to STATE_UNDEFINED if there is no match when comparing the state
	 */
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
