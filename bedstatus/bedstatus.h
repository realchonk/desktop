#ifndef FILE_BEDSTATUS_H
#define FILE_BEDSTATUS_H
#include <stdbool.h>
#include <stdint.h>

#define VPN_GATEWAY "192.168.64.1"
#define VPN_IFACE "wg0"

enum net_state {
	NET_UNKNOWN,	// cannot get network status
	NET_DOWN,	// down/no carrier/no link
	NET_ISOLATED,	// no connection to the internet
	NET_CONNECTED,	// connected to the internet
};

enum vpn_state {
	VPN_UNKNOWN,	// no VPN configured / cannot determine
	VPN_DOWN,	// VPN configured, but gateway is unreachable
	VPN_UP,		// VPN gateway is reachable
};

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

	enum net_state	net_eth;	// Ethernet status
	enum net_state	net_wifi;	// WiFi status
	enum vpn_state	vpn;		// VPN status
};
#undef E

void init_backend (void);
void update_status (struct status *);

#endif // FILE_BEDSTATUS_H
