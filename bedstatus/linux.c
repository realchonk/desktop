#include <sys/sysinfo.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "bedstatus.h"

#define NTIMES 9

struct cpu_times {
	uint64_t times[NTIMES];
};

static int ncpu;
static FILE *meminfo, *stat, **cpufreqs;

static bool parse_meminfo_line (char *line, const char *name, uint64_t *value)
{
	char *s;
	bool b;

	s = strchr (line, ':');
	if (s == NULL)
		return false;

	*s = '\0';
	b = strcmp (line, name) == 0;
	*s = ':';

	if (!b)
		return false;

	++s;

	while (isspace (*s))
		++s;

	*value = 0;

	while (isdigit (*s))
		*value = *value * 10 + (*s++ - '0');

	if (strcmp (s, " kB\n") == 0)
		*value *= 1024;

	return true;
}


static bool mem_usage (uint64_t *usage)
{
	uint64_t total, free, buffers, cached, sreclaim, shmem;
	char line[100];

	if (meminfo == NULL)
		return false;

	rewind (meminfo);

	while (fgets (line, sizeof (line), meminfo) != NULL) {
		if (parse_meminfo_line (line, "MemTotal", &total))
			continue;

		if (parse_meminfo_line (line, "MemFree", &free))
			continue;

		if (parse_meminfo_line (line, "Buffers", &buffers))
			continue;

		if (parse_meminfo_line (line, "Cached", &cached))
			continue;

		if (parse_meminfo_line (line, "SReclaimable", &sreclaim))
			continue;

		if (parse_meminfo_line (line, "Shmem", &shmem))
			continue;
	}

	*usage = total - free - buffers - cached - sreclaim + shmem;

	return true;
}

static bool cpu_usage (int *usage)
{
	static struct cpu_times old;
	struct cpu_times new, diff;
	uint64_t total;
	char line[100], *s;

	rewind (stat);

	if (fgets (line, sizeof (line), stat) == NULL)
		return false;

	if (memcmp (line, "cpu  ", 5) != 0)
		return false;
	
	memset (&new, 0, sizeof (new));
	s = line + 5;

	for (int i = 0; *s != '\0'; ++s) {
		if (isdigit (*s)) {
			new.times[i] = new.times[i] * 10 + (*s - '0');
		} else if (isspace (*s)) {
			++i;
		}
	}

	total = 0;
	for (int i = 0; i < NTIMES; ++i) {
		diff.times[i] = new.times[i] - old.times[i];
		total += diff.times[i];
	}

	*usage = 100 - 100 * diff.times[3] / total;
	old = new;
	return true;
}

static bool cpu_speed (int *speed)
{
	uint64_t total = 0;
	char line[100];
	int num = 0;
	FILE *file;

	for (int i = 0; i < ncpu; ++i) {
		file = cpufreqs[i];

		if (file == NULL)
			continue;

		rewind (file);

		if (fgets (line, sizeof (line), file) == NULL)
			continue;

		total += atoi (line);
		++num;
	}

	if (num == 0)
		return false;

	*speed = total / num / 1000;
	return true;
}

void init_backend (void)
{
	char path[256];

	meminfo = fopen ("/proc/meminfo", "r");
	stat = fopen ("/proc/stat", "r");
	ncpu = get_nprocs_conf ();
	cpufreqs = calloc (ncpu, sizeof (FILE *));

	for (int i = 0; i < ncpu; ++i) {
		snprintf (path, sizeof (path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", i);
		cpufreqs[i] = fopen (path, "r");
	}
}

void update_status (struct status *st)
{
	memset (st, 0, sizeof (*st));

	st->has_mem_usage = mem_usage (&st->mem_usage);
	st->has_cpu_usage = cpu_usage (&st->cpu_usage);
	st->has_cpu_speed = cpu_speed (&st->cpu_speed);
}
