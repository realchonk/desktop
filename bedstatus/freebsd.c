// This code is heavily borrowed from FreeBSD's top
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <string.h>
#include "bedstatus.h"

#define SYSCTL(name, x) (xsysctl (name, x, sizeof (*(x))))

struct cpu_times {
	long times[CPUSTATES];	
};

static long pagesize;
static int ncpu;
static struct cpu_times *old = NULL, *new, *diff;
static char **cpuspeed_sysctls = NULL;

static bool xsysctl (const char *name, void *ptr, size_t len)
{
	size_t len2 = len;

	return sysctlbyname (name, ptr, &len2, NULL, 0) >= 0 && len == len2;
}

static bool mem_usage (uint64_t *usage)
{
	int active;

	if (!SYSCTL ("vm.stats.vm.v_active_count", &active))
		return false;

	*usage = (uint64_t)active * pagesize;

	return true;
}

static bool cpu_usage (int *usage)
{
	int len;

	if (old == NULL)
		return false;


	return false;
}

static bool bat_perc (int *perc)
{
	return SYSCTL ("hw.acpi.battery.life", perc);
}

static bool bat_charging (bool *b)
{
	int x;

	if (!SYSCTL ("hw.acpi.battery.state", &x))
		return false;

	*b = (x & 2) == 2;

	return true;
}

static bool bat_rem (int *x)
{
	return SYSCTL ("hw.acpi.battery.time", x) && *x != -1;
}

static bool power (int *pwr)
{
	return SYSCTL ("hw.acpi.battery.rate", pwr);
}

static bool cpu_speed (int *mhz)
{
	int x, sum = 0, n = 0;

	for (int i = 0; i < ncpu; ++i) {
		if (!SYSCTL (cpuspeed_sysctls[i], &x))
			continue;
		sum += x;
		++n;
	}

	*mhz = sum / n;

	return n > 0;
}

void init_backend (void)
{
	pagesize = getpagesize ();
	if (SYSCTL ("kern.smp.maxcpus", &ncpu)) {
		old = calloc (ncpu, sizeof (struct cpu_times));
		new = calloc (ncpu, sizeof (struct cpu_times));
		diff = calloc (ncpu, sizeof (struct cpu_times));
	}

	cpuspeed_sysctls = calloc (ncpu, sizeof (char *));
	for (int i = 0; i < ncpu; ++i) {
		asprintf (&cpuspeed_sysctls[i], "dev.cpu.%d.freq", i);
	}
}

void update_status (struct status *st)
{
	memset (st, 0, sizeof (*st));
	st->has_mem_usage = mem_usage (&st->mem_usage);
	st->has_cpu_usage = cpu_usage (&st->cpu_usage);
	st->has_cpu_speed = cpu_speed (&st->cpu_speed);
	st->has_bat_perc = bat_perc (&st->bat_perc);
	st->has_bat_rem = bat_rem (&st->bat_rem);
	st->has_bat_charging = bat_charging (&st->bat_charging);
	st->has_power = power (&st->power);
}
