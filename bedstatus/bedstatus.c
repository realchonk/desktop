#include <sys/types.h>
#include <machine/apmvar.h>
#include <sys/signal.h>
#include <sys/sched.h>
#include <sys/sysctl.h>
#include <sys/ioctl.h>
#include <sys/sensors.h>
#include <X11/Xlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <err.h>

#define MAXSENSORS 16
#define CPUSPEEDHISTLEN 3

static int power_sensors[MAXSENSORS];
static int cputemp_sensor = -1;
static int num_power_sensors = 0;

struct cpu_usage {
	struct cpustats new;
	struct cpustats old;
	struct cpustats diffs;
};

static int num_cpu (void)
{
	const int mib[] = { CTL_HW, HW_NCPU };
	int ncpu = 0;
	size_t size = sizeof ncpu;
	
	if (sysctl (mib, 2, &ncpu, &size, NULL, 0) < 0)
		return 1;

	return ncpu;
}

static int64_t percentages (struct cpustats *out, struct cpu_usage *cpu)
{
	int64_t total_change = 0;

	for (int i = 0; i < 6; ++i) {
		int64_t change = cpu->new.cs_time[i] - cpu->old.cs_time[i];
		if (change < 0)
			change = INT64_MAX - cpu->old.cs_time[i] - cpu->new.cs_time[i];

		cpu->diffs.cs_time[i] = change;
		total_change += change;
		cpu->old.cs_time[i] = cpu->new.cs_time[i];
	}

	if (total_change == 0)
		total_change = 1;
	
	for (int i = 0; i < 6; ++i) {
		out->cs_time[i] = (cpu->diffs.cs_time[i] * 1000 + total_change / 2) / total_change;
	}

	return total_change;
}

static int cpuspeed (void)
{
	static int history[CPUSPEEDHISTLEN];
	const int mib[2] = { CTL_HW, HW_CPUSPEED };
	int speed, sum = 0;
	size_t len = sizeof speed;

	if (sysctl (mib, 2, &speed, &len, NULL, 0) == -1)
		return -1;

	memmove (history + 1, history, (CPUSPEEDHISTLEN - 1) * sizeof *history);
	history[0] = speed;

	for (int i = 0; i < CPUSPEEDHISTLEN; ++i)
		sum += history[i];

	return sum / CPUSPEEDHISTLEN;
}

static int64_t read_sensor (int id, int type, int n)
{
	const int mib[5] = { CTL_HW, HW_SENSORS, id, type, n };
	struct sensor sensor;
	size_t len = sizeof sensor;
	int ret;

	ret = sysctl (mib, 5, &sensor, &len, NULL, 0);

	return (ret == -1 || sensor.status & SENSOR_FINVALID) ? -1 : sensor.value;
}

static const char *cpu (int ncpu, struct cpu_usage *cpus)
{
	static char buffer[64];
	uint64_t sum = 0;
	int64_t temp;
	int usage, speed;

	for (int i = 0; i < ncpu; ++i) {
		const int mib[] = { CTL_KERN, KERN_CPUSTATS, i };
		struct cpustats tmp;
		size_t size = sizeof tmp;

		if (sysctl (mib, 3, &cpus[i].new, &size, NULL, 0) < 0)
			continue;

		percentages (&tmp, &cpus[i]);
		sum += 1000 - tmp.cs_time[5];
	}

	usage = sum / ncpu / 10;
	speed = cpuspeed ();
	temp = cputemp_sensor != -1 ? read_sensor (cputemp_sensor, SENSOR_TEMP, 0) : -1;

	if (temp != -1) {
		temp = (temp - 273150000) / 1000000;
		snprintf (buffer, sizeof buffer, "%d%% (%d MHz/%d°C)", (int)usage, speed, (int)temp);
	} else {
		snprintf (buffer, sizeof buffer, "%d%% (%d MHz)", (int)usage, speed);
	}
	return buffer;
}

static const char *ram (void)
{
	struct unit {
		const char *s;
		unsigned long long u;
	} units[] = {
		{ "PiB",	1ull << 50 },
		{ "TiB",	1ull << 40 },
		{ "GiB",	1ull << 30 },
		{ "MiB",	1ull << 20 },
		{ "KiB",	1ull << 10 },
		{ NULL,		0 },
	};
	const int mib[] = { CTL_VM, VM_UVMEXP };
	struct uvmexp uvmexp;
	size_t size = sizeof uvmexp;
	uint64_t bytes;
	static char buffer[10];

	if (sysctl (mib, 2, &uvmexp, &size, NULL, 0) < 0)
		return "N/A";

	bytes = ((uint64_t)uvmexp.active << (uvmexp.pageshift - 10)) * 1024;

	for (const struct unit *u = units; u->s != NULL; ++u) {
		if (bytes < u->u)
			continue;

		const unsigned raw = bytes * 100 / u->u;

		if (raw >= 10000) {
			snprintf (buffer, sizeof buffer, "%u %s", raw / 100, u->s);
		} else if (raw >= 1000) {
			snprintf (buffer, sizeof buffer, "%u.%u %s", raw / 100, raw / 10 % 10, u->s);
		} else {
			snprintf (buffer, sizeof buffer, "%u.%02u %s", raw / 100, raw % 100, u->s);
		}
		return buffer;
	}
	snprintf (buffer, sizeof buffer, "%u B", (unsigned)bytes);
	return buffer;
}

static void bat (int fd, const char **bat_sym, unsigned *bat_perc, const char **bat_time)
{
	static char buffer[32];
	struct apm_power_info info;

	if (fd < 0 || ioctl (fd, APM_IOC_GETPOWER, &info) != 0) {
		*bat_sym = "N/A";
		*bat_perc = 0;
		*bat_time = "N/A";
		return;
	}

	*bat_perc = info.battery_life;

	if (info.minutes_left != (u_int)-1) {
		size_t pos = 0;
		unsigned minutes = info.minutes_left;
		const static struct unit {
			char c;
			unsigned v;
		} units[] = {
			{ .c = 'd', 24 * 60 },
			{ .c = 'h',      60 },
			{ .c = 'm',       1 },
			{ .c = '\0',      0 },
		};

		for (size_t i = 0; units[i].v != 0; ++i) {
			const struct unit u = units[i];
			if (minutes >= u.v) {
				pos += snprintf (buffer + pos, sizeof buffer - pos, "%u%c", minutes / u.v, u.c);
				minutes %= u.v;
			}
		}
		buffer[pos] = '\0';
		*bat_time = buffer;
	} else {
		*bat_time = "N/A";
	}

	switch (info.ac_state) {
	case APM_AC_OFF:
	case APM_AC_UNKNOWN:
	case APM_AC_BACKUP:
		if (info.battery_life < 25) {
			*bat_sym = "\uf244";
		} else if (info.battery_life < 50) {
			*bat_sym = "\uf243";
		} else if (info.battery_life < 75) {
			*bat_sym = "\uf242";
		} else if (info.battery_life < 99) {
			*bat_sym = "\uf241";
		} else {
			*bat_sym = "\uf240";
		}
		break;
	case APM_AC_ON:
		*bat_sym = "\uf5e7";
		break;
	}
}

static void find_sensors (void)
{
	for (int i = 0; i < MAXSENSORS; ++i) {
		const int mib[3] = { CTL_HW, HW_SENSORS, i };
		struct sensordev dev;
		size_t sdlen = sizeof dev;

		if (sysctl (mib, 3, &dev, &sdlen, NULL, 0) == -1)
			continue;

		if (dev.maxnumt[SENSOR_WATTS] > 0) {
			power_sensors[num_power_sensors++] = i;
		}

		if (dev.maxnumt[SENSOR_TEMP] > 0
		&& memcmp (dev.xname, "cpu", 3) == 0
		&& cputemp_sensor == -1) {
			cputemp_sensor = i;
		}
	}
}

static const char *power (void)
{
	static char power[32];
	int64_t total = 0;

	if (num_power_sensors == 0)
		return "";

	for (int i = 0; i < num_power_sensors; ++i) {
		const int mib[5] = { CTL_HW, HW_SENSORS, power_sensors[i], SENSOR_WATTS, 0 };
		struct sensor sensor;
		size_t slen = sizeof sensor;

		if (sysctl (mib, 5, &sensor, &slen, NULL, 0) == -1)
			continue;

		if (sensor.flags & SENSOR_FINVALID)
			continue;

		total += sensor.value;
	}

	const int sub = total / 10000;
	snprintf (power, sizeof power, "/%d.%02uW", sub / 100, sub % 100);
	return power;
} 

int main (int argc, char *argv[])
{
	int ncpu;
	struct cpu_usage *cpus;
	unsigned long root;
	char status[256];
	Display *dpy;
	int fd_apm, screen;

	(void)argv;

	if (argc != 1) {
		puts ("bedstatus-" VERSION);
		return 1;
	}

	dpy = XOpenDisplay (NULL);
	if (dpy == NULL)
		errx (1, "unable to open display");

	screen = XDefaultScreen (dpy);
	root = XRootWindow (dpy, screen);

	ncpu = num_cpu ();
	cpus = calloc (ncpu, sizeof (struct cpu_usage));
	fd_apm = open ("/dev/apm", O_RDONLY);
	find_sensors ();

	while (1) {
		const char *bat_sym, *bat_time;
		unsigned bat_perc;
		time_t t;
		struct tm *tm;

		bat (fd_apm, &bat_sym, &bat_perc, &bat_time);

		const int num = snprintf (
			status,
			sizeof status,
			"[CPU \uf2db %s] [RAM \uf538 %s] [BAT %s %u%% (%s%s)] ",
			cpu (ncpu, cpus),
			ram (),
			bat_sym,
			bat_perc,
			bat_time,
			power ()
		);

		t = time (NULL);
		tm = localtime (&t);
		strftime (status + num, sizeof status - num, "%a %F %T", tm);
		status[sizeof status - 1] = '\0';

		XStoreName (dpy, root, status);
		XFlush (dpy);
		sleep (1);
	}
}
