#ifndef FILE_BEDSTATUS_H
#define FILE_BEDSTATUS_H
#include <stdbool.h>
#include <stdint.h>

#define E(t, n) t n; bool has_##n
struct status {
	E(int, cpu_usage);		// CPU Usage in %
	E(int, cpu_speed);		// CPU Frequency in MHz
	E(int, cpu_temp);		// CPU Temperature in DegC
	E(uint64_t, mem_usage);		// RAM Usage in Bytes
	E(int, bat_perc);		// Battery Level in %
	E(int, bat_rem);		// Remaining Battery Life in Minutes
	E(bool, bat_charging);		// Is the AC connected?
	E(int, power);			// Power Drain in Milli-Watts
};
#undef E

void init_backend (void);
void update_status (struct status *);

#endif // FILE_BEDSTATUS_H
