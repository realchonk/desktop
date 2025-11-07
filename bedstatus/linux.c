#include <sys/sysinfo.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include "bedstatus.h"

#define NTIMES 9
#define MAXBATS 4

struct bat {
	int dirfd;
};

struct cpu_times {
	uint64_t times[NTIMES];
};

static int ncpu, nbat;
static FILE *meminfo, *stat, **cpufreqs;
static struct bat bats[MAXBATS];

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

	if (total == 0)
		return false;

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

static char *bat_read (const struct bat *bat, const char *name)
{
	static char buf[100];
	FILE *file;
	char *s;
	int fd;

	fd = openat (bat->dirfd, name, O_RDONLY);
	if (fd < 0)
		return NULL;

	file = fdopen (fd, "r");
	if (file == NULL) {
		close (fd);
		return NULL;
	}

	s = fgets (buf, sizeof (buf), file);
	fclose (file);
	return s;
}

static long long bat_parse (const struct bat *bat, const char *name)
{
	char *s, *endp;
	long long x;

	s = bat_read (bat, name);
	if (s == NULL)
		return -1;

	x = strtoll (s, &endp, 10);
	if (*endp != '\n' && *endp != '\0')
		return -1;

	return x;
}

static void bat (struct status *st)
{
	unsigned long long now = 0;
	unsigned long long full = 0;

	st->power = 0;

	for (int i = 0; i < nbat; ++i) {
		const struct bat *bat = &bats[i];
		long long c, cf, v, cn, p, en, ef;
		char *s;

		s = bat_read (bat, "status");
		if (s != NULL) {
			st->has_bat_charging = true;
			st->bat_charging |= (strcmp (s, "Charging\n") == 0);
		}

		v = bat_parse (bat, "voltage_now");
		if (v < 0)
			continue;

		c = bat_parse (bat, "current_now");
		p = bat_parse (bat, "power_now");
		cn = bat_parse (bat, "charge_now");
		en = bat_parse (bat, "energy_now");
		cf = bat_parse (bat, "charge_full");
		ef = bat_parse (bat, "energy_full");

		if (en >= 0 && ef > 0) {
			st->has_bat_perc = true;
			full += ef / 1000;
			now += en / 1000;
		} else if (cn >= 0 && cf > 0) {
			st->has_bat_perc = true;
			full += cf / 1000 * v / 1000000;
			now += cn / 1000 * v / 1000000;
		}

		if (p >= 0) {
			st->has_power = true;
			st->power = p / 1000;
		} else if (c >= 0) {
			st->has_power = true;
			st->power += c / 1000 * v / 1000000;
		}
	}

	if (st->has_bat_perc)
		st->bat_perc = now * 100 / full;

	if (st->has_bat_perc && st->has_power) {
		st->has_bat_rem = true;
		st->bat_rem = now / st->power * 60;
	}

}

void init_backend (void)
{
	char path[256];

	meminfo = fopen ("/proc/meminfo", "r");
	stat = fopen ("/proc/stat", "r");
	ncpu = get_nprocs_conf ();
	cpufreqs = calloc (ncpu, sizeof (FILE *));

	// disable buffering
	setvbuf (meminfo, NULL, _IONBF, 0);
	setvbuf (stat, NULL, _IONBF, 0);

	for (int i = 0; i < ncpu; ++i) {
		snprintf (path, sizeof (path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", i);
		cpufreqs[i] = fopen (path, "r");
	}

	nbat = 0;
	for (int i = 0; i < MAXBATS; ++i) {
		struct bat *bat;
		char *path = NULL;
		
		bat = &bats[nbat];
		memset (bat, 0, sizeof (*bat));

		asprintf (&path, "/sys/class/power_supply/BAT%d", i);
		bat->dirfd = open (path, O_DIRECTORY | O_RDONLY);
		free (path);

		if (bat->dirfd > 0) {
			printf ("found battery BAT%d\n", i);
			++nbat;
		}
	}
}

void update_status (struct status *st)
{
	memset (st, 0, sizeof (*st));

	st->has_mem_usage = mem_usage (&st->mem_usage);
	st->has_cpu_usage = cpu_usage (&st->cpu_usage);
	st->has_cpu_speed = cpu_speed (&st->cpu_speed);
	bat (st);
}
