#include <X11/Xlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <err.h>

#ifdef __OpenBSD__
# include "openbsd.c"
#elif defined(__linux__)
# include "linux.c"
#elif defined(__FreeBSD__)
# include "freebsd.c"
#else
# include "unsupported.c"
#endif

#define SYM_CPU "\uf2db "
#define SYM_RAM "\uf538 "
#define SYM_BAT_CHARGING "\uf5e7 "
#define SYM_BAT_EMPTY "\uf244 "
#define SYM_BAT_LOW "\uf243 "
#define SYM_BAT_HALF "\uf242 "
#define SYM_BAT_HIGH "\uf241 "
#define SYM_BAT_FULL "\uf240 "

static size_t pos;
static char buf[256];

static void append (const char *fmt, ...)
{
	va_list ap;
	int n;

	if (pos >= sizeof (buf))
		return;
	
	va_start (ap, fmt);
	n = vsnprintf (buf + pos, sizeof (buf) - pos, fmt, ap);
	va_end (ap);

	if (n > 0)
		pos += n;
}

static void format_cpu (const struct status *st)
{
	if (!st->has_cpu_usage && !st->has_cpu_speed && !st->has_cpu_temp)
		return;

	append ("[CPU " SYM_CPU);
	
	if (st->has_cpu_usage) {
		append ("%d%%", st->cpu_usage);
		if (st->has_cpu_speed || st->has_cpu_temp)
			append (" (");
	}

	if (st->has_cpu_speed) {
		append ("%d MHz", st->cpu_speed);

		if (st->has_cpu_temp)
			append ("/");
	}

	if (st->has_cpu_temp)
		append ("%d°C", st->cpu_temp);

	if (st->has_cpu_usage && (st->has_cpu_speed || st->has_cpu_temp))
		append (")");

	append ("] ");
}

static void format_ram (const struct status *st)
{
	uint64_t bytes;
	struct unit {
		const char *s;
		uint64_t u;
	} units[] = {
		{ "PiB",	1ull << 50 },
		{ "TiB",	1ull << 40 },
		{ "GiB",	1ull << 30 },
		{ "MiB",	1ull << 20 },
		{ "KiB",	1ull << 10 },
		{ NULL,		0 },
	};

	if (!st->has_mem_usage)
		return;

	append ("[RAM " SYM_RAM);

	bytes = st->mem_usage;

	for (const struct unit *u = units; u-> s != NULL; ++u) {
		if (bytes < u->u)
			continue;

		const unsigned raw = bytes * 100 / u->u;

		if (raw >= 10000) {
			append ("%u", raw / 100);
		} else if (raw >= 1000) {
			append ("%u.%u", raw / 100, raw / 10 % 10);
		} else {
			append ("%u.%02u", raw / 100, raw % 100);
		}

		append (" %s", u->s);
		goto done;
	}

	append ("%u B", (unsigned)bytes);

done:
	append ("] ");
}

static void format_bat (const struct status *st)
{
	if (!st->has_bat_charging && !st->has_bat_perc && !st->has_bat_rem && !st->has_power)
		return;

	append ("[BAT ");

	if (st->has_bat_perc && (!st->has_bat_charging || !st->bat_charging)) {
		if (st->bat_perc < 25) {
			append (SYM_BAT_EMPTY);
		} else if (st->bat_perc < 50) {
			append (SYM_BAT_LOW);
		} else if (st->bat_perc < 75) {
			append (SYM_BAT_HALF);
		} else if (st->bat_perc < 99) {
			append (SYM_BAT_HIGH);
		} else {
			append (SYM_BAT_FULL);
		}
	} else {
		append (SYM_BAT_CHARGING);
	}

	if (st->has_bat_perc) {
		append ("%d%%", st->bat_perc);

		if (st->has_bat_rem || st->has_power)
			append (" (");
	}

	if (st->has_bat_rem) {
		unsigned rem = st->bat_rem;
		struct unit {
			char c;
			unsigned v;
		} units[] = {
			{ 'd',	24 * 60 },
			{ 'h',	     60 },
			{ 'm',	      1 },
			{ '\0',	      0 },
		};

		for (size_t i = 0; units[i].v != 0; ++i) {
			const struct unit u = units[i];

			if (rem < u.v)
				continue;

			append ("%u%c", rem / u.v, u.c);
			rem %= u.v;
		}

		if (st->has_power)
			append ("/");
	}

	if (st->has_power) {
		unsigned pwr = st->power;

		append ("%u.%02uW", pwr / 1000, pwr % 1000 / 10);
	}

	if (st->has_bat_perc && (st->has_bat_rem || st->has_power))
		append (")");

	append ("] ");
}

static void format_time (void)
{
	struct tm *tm;
	time_t t;

	t = time (NULL);
	tm = localtime (&t);
	strftime (buf + pos, sizeof (buf) - pos, "%a %F %T", tm);
}

static void format_status (const struct status *st)
{
	format_cpu (st);
	format_ram (st);
	format_bat (st);
	format_time ();
}

int main (int argc, char *argv[])
{
	Display *dpy;
	int screen;
	unsigned long root;

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

	init_backend ();

	while (1) {
		struct status st;

		update_status (&st);
		pos = 0;
		format_status (&st);

		XStoreName (dpy, root, buf);
		XFlush (dpy);
		sleep (1);
	}
}
