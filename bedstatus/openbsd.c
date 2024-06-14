#include <sys/types.h>
#include <machine/apmvar.h>
#include <sys/sysctl.h>
#include <sys/ioctl.h>
#include <sys/sched.h>
#include <sys/time.h>
#include <sys/sensors.h>
#include <uvm/uvm_param.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include "bedstatus.h"

#define MAXSENSORS 16

struct cpu_usage {
	struct cpustats new, old, diffs;
};

static int ncpu = 1;
static struct cpu_usage *cpus;
static int apm_fd = -1;
static int cputemp_sensor = -1;
static int num_power_sensors = 0;
static int power_sensors[MAXSENSORS];

static bool read_sensor (struct sensor *sen, int which, int what)
{
	const int mib[5] = { CTL_HW, HW_SENSORS, which, what, 0 };
	size_t len = sizeof (*sen);

	return sysctl (mib, 5, sen, &len, NULL, 0) != -1;
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


static bool cpu_usage (int *usage)
{
	uint64_t sum = 0;

	for (int i = 0; i < ncpu; ++i) {
		const int mib[3] = { CTL_KERN, KERN_CPUSTATS, i };
		struct cpustats tmp;
		size_t len = sizeof (tmp);

		if (sysctl (mib, 3, &cpus[i].new, &len, NULL, 0) < 0)
			continue;

		percentages (&tmp, &cpus[i]);
		sum += 1000 - tmp.cs_time[5];
	}

	*usage = sum / ncpu / 10;
	return true;
}

static bool cpu_speed (int *speed)
{
	const int mib[2] = { CTL_HW, HW_CPUSPEED };
	size_t len = sizeof (*speed);

	return sysctl (mib, 2, speed, &len, NULL, 0) != -1;
}

static bool cpu_temp (int *temp)
{
	struct sensor sen;

	if (!read_sensor (&sen, cputemp_sensor, SENSOR_TEMP) || sen.status & SENSOR_FINVALID)
		return false;

	*temp = (sen.value - 273150000) / 1000000;;

	return true;
}

static bool mem_usage (uint64_t *usage)
{
	const int mib[2] = { CTL_VM, VM_UVMEXP };
	struct uvmexp uvm;
	size_t len = sizeof (uvm);

	if (sysctl (mib, 2, &uvm, &len, NULL, 0) == -1)
		return false;

	*usage = (uint64_t)uvm.active << uvm.pageshift;

	return true;
}

static void bat (struct status *st)
{
	struct apm_power_info info;

	if (apm_fd < 0 || ioctl (apm_fd, APM_IOC_GETPOWER, &info) != 0)
		return;

	st->has_bat_perc = true;
	st->bat_perc = info.battery_life;

	if (info.minutes_left != (u_int)-1) {
		st->has_bat_rem = true;
		st->bat_rem = info.minutes_left;
	}

	switch (info.ac_state) {
	case APM_AC_ON:
		st->has_bat_charging = true;
		st->bat_charging = true;
		break;
	case APM_AC_OFF:
	case APM_AC_BACKUP:
		st->has_bat_charging = true;
		st->bat_charging = false;
		break;
	}
}

static bool power (int *milliwatts)
{
	struct sensor sen;
	int64_t total = 0;

	if (num_power_sensors == 0)
		return false;

	for (int i = 0; i < num_power_sensors; ++i) {
		if (!read_sensor (&sen, power_sensors[i], SENSOR_WATTS))
			continue;

		total += sen.value;
	}

	*milliwatts = total / 1000;
	return true;
}

void init_backend (void)
{
	const int mib_ncpu[2] = { CTL_HW, HW_NCPU };
	size_t len = sizeof (ncpu);

	sysctl (mib_ncpu, 2, &ncpu, &len, NULL, 0);
	cpus = calloc (ncpu, sizeof (struct cpu_usage));

	apm_fd = open ("/dev/apm", O_RDONLY);

	for (int i = 0; i < MAXSENSORS; ++i) {
		const int mib[3] = { CTL_HW, HW_SENSORS, i};
		struct sensordev dev;

		len = sizeof (dev);

		if (sysctl (mib, 3, &dev, &len, NULL, 0) == -1)
			continue;

		if (dev.maxnumt[SENSOR_WATTS] > 0) {
			power_sensors[num_power_sensors++] = i;
		}

		if (dev.maxnumt[SENSOR_TEMP] > 0 && memcmp (dev.xname, "cpu", 3) == 0 && cputemp_sensor == -1) {
			cputemp_sensor = i;
		}
	}
}

void update_status (struct status *st)
{
	memset (st, 0, sizeof (*st));
	st->has_cpu_usage = cpu_usage (&st->cpu_usage);
	st->has_cpu_temp = cpu_temp (&st->cpu_temp);
	st->has_cpu_speed = cpu_speed (&st->cpu_speed);
	st->has_mem_usage = mem_usage (&st->mem_usage);
	bat (st);
	st->has_power = power (&st->power);
}
