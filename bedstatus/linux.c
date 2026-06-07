#include <sys/sysinfo.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <unistd.h>
#include <dirent.h>
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
static FILE *cputemp;
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

static bool cpu_temp (int *temp)
{
	char line[32];

	if (cputemp == NULL)
		return false;

	rewind (cputemp);

	if (fgets (line, sizeof (line), cputemp) == NULL)
		return false;

	/* hwmon reports millidegrees Celsius */
	*temp = atoi (line) / 1000;
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

	if (st->has_bat_perc && st->has_power && st->power > 0) {
		st->has_bat_rem = true;
		st->bat_rem = now * 60 / st->power;
	}
}

#define NETDIR "/sys/class/net"

/* Read the first line of NETDIR/<iface>/<attr> into buf (newline stripped). */
static bool net_read_attr (const char *iface, const char *attr, char *buf, size_t len)
{
	char path[256], *s;
	FILE *file;

	snprintf (path, sizeof (path), NETDIR "/%.32s/%.64s", iface, attr);

	file = fopen (path, "r");
	if (file == NULL)
		return false;

	s = fgets (buf, len, file);
	fclose (file);

	if (s == NULL)
		return false;

	s = strchr (buf, '\n');
	if (s != NULL)
		*s = '\0';

	return true;
}

static bool net_has (const char *iface, const char *attr)
{
	char path[256];

	snprintf (path, sizeof (path), NETDIR "/%.32s/%.64s", iface, attr);
	return access (path, F_OK) == 0;
}

static bool net_is_wireless (const char *iface)
{
	return net_has (iface, "wireless") || net_has (iface, "phy80211");
}

/* Physical interfaces have a backing device; this filters out loopback,
 * bridges and other purely virtual interfaces. */
static bool net_is_physical (const char *iface)
{
	return net_has (iface, "device");
}

/* True if a default route (destination 0.0.0.0) goes through iface. */
static bool net_has_default_route (const char *iface)
{
	char line[256], name[64];
	unsigned long dest, gw, flags;
	bool found = false;
	FILE *file;

	file = fopen ("/proc/net/route", "r");
	if (file == NULL)
		return false;

	if (fgets (line, sizeof (line), file) == NULL) {	/* skip header */
		fclose (file);
		return false;
	}

	while (fgets (line, sizeof (line), file) != NULL) {
		if (sscanf (line, "%63s %lx %lx %lx", name, &dest, &gw, &flags) != 4)
			continue;

		if (dest == 0 && strcmp (name, iface) == 0) {
			found = true;
			break;
		}
	}

	fclose (file);
	return found;
}

/* Send a single ping to host; true if it replies within the timeout. */
static bool net_ping (const char *host)
{
	char cmd[128];
	FILE *p;

	/* one packet, 1s timeout, no DNS; output is irrelevant, only status */
	snprintf (cmd, sizeof (cmd),
		"ping -c1 -W1 -n -q %s >/dev/null 2>&1", host);

	p = popen (cmd, "r");
	if (p == NULL)
		return false;

	return pclose (p) == 0;
}

static enum net_state net_iface_state (const char *iface)
{
	char buf[32];
	bool up = false;

	if (net_read_attr (iface, "operstate", buf, sizeof (buf)))
		up = (strcmp (buf, "up") == 0);

	/* fall back to the carrier flag for drivers reporting "unknown" */
	if (!up && net_read_attr (iface, "carrier", buf, sizeof (buf)))
		up = (strcmp (buf, "1") == 0);

	if (!up)
		return NET_DOWN;

	if (!net_has_default_route (iface))
		return NET_ISOLATED;

	return NET_CONNECTED;
}

/* Aggregate the state of all physical interfaces of the requested kind,
 * keeping the best-connected one (the enum is ordered by connectivity). */
static enum net_state net_type_state (bool want_wireless)
{
	enum net_state best = NET_UNKNOWN;
	struct dirent *ent;
	DIR *dir;

	dir = opendir (NETDIR);
	if (dir == NULL)
		return NET_UNKNOWN;

	while ((ent = readdir (dir)) != NULL) {
		enum net_state s;

		if (ent->d_name[0] == '.')
			continue;

		if (!net_is_physical (ent->d_name))
			continue;

		if (net_is_wireless (ent->d_name) != want_wireless)
			continue;

		s = net_iface_state (ent->d_name);
		if (s > best)
			best = s;
	}

	closedir (dir);
	return best;
}

static void net (struct status *st)
{
	st->net_eth = net_type_state (false);
	st->net_wifi = net_type_state (true);
}

/* True if the VPN interface exists and is up, i.e. a VPN is configured. */
static bool vpn_iface_present (void)
{
	char buf[32];

	/* wireguard interfaces commonly report "unknown" while running */
	return net_read_attr (VPN_IFACE, "operstate", buf, sizeof (buf))
	    && (strcmp (buf, "up") == 0 || strcmp (buf, "unknown") == 0);
}

static void vpn (struct status *st)
{
	/* Avoid the ping (which blocks until timeout when unreachable) unless a
	 * VPN is actually configured. */
	if (!vpn_iface_present ()) {
		st->vpn = VPN_UNKNOWN;
		return;
	}

	st->vpn = net_ping (VPN_GATEWAY) ? VPN_UP : VPN_DOWN;
}

/* Locate the CPU temperature sensor under /sys/class/hwmon, preferring the
 * dedicated CPU drivers over the generic ACPI thermal zone. */
static void find_cputemp (void)
{
	static const char *const names[] = {
		"coretemp", "k10temp", "zenpower", "cpu_thermal", "acpitz",
	};

	for (size_t i = 0; i < sizeof (names) / sizeof (names[0]); ++i) {
		struct dirent *ent;
		DIR *dir;

		dir = opendir ("/sys/class/hwmon");
		if (dir == NULL)
			return;

		while ((ent = readdir (dir)) != NULL) {
			char path[256], name[32];
			FILE *f;

			if (ent->d_name[0] == '.')
				continue;

			snprintf (path, sizeof (path), "/sys/class/hwmon/%.16s/name", ent->d_name);
			f = fopen (path, "r");
			if (f == NULL)
				continue;
			if (fgets (name, sizeof (name), f) != NULL)
				name[strcspn (name, "\n")] = '\0';
			else
				name[0] = '\0';
			fclose (f);

			if (strcmp (name, names[i]) != 0)
				continue;

			snprintf (path, sizeof (path), "/sys/class/hwmon/%.16s/temp1_input", ent->d_name);
			cputemp = fopen (path, "r");
			if (cputemp != NULL)
				setvbuf (cputemp, NULL, _IONBF, 0);
			break;
		}

		closedir (dir);

		if (cputemp != NULL)
			return;
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

	find_cputemp ();

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
	st->has_cpu_temp = cpu_temp (&st->cpu_temp);
	bat (st);
	net (st);
	vpn (st);
}
