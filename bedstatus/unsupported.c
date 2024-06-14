#include <string.h>
#include "bedstatus.h"

void init_backend (void)
{
}

void update_status (struct status *st)
{
	memset (st, 0, sizeof (*st));
}
