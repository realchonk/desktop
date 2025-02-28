#define _XOPEN_SOURCE 700
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <err.h>

int usage (void)
{
	fputs ("usage: slowcat [-s baud] [file...]\n", stderr);
	return 1;
}

#define NS 1000000000ull
int dofile (FILE *file, unsigned speed)
{
	char buf[128];
	unsigned long long ns;
	struct timespec ts;
	int n, div;

	div = speed / 1000;
	if (div < 1) {
		div = 1;
	} else if (div > sizeof (buf)) {
		div = sizeof (buf);
	}

	ns = NS / speed * 8 * div;
	ts.tv_sec = ns / NS;
	ts.tv_nsec = ns % NS;

	while ((n = fread (buf, 1, div, file)) > 0) {
		fwrite (buf, 1, n, stdout);
		fflush (stdout);
		nanosleep (&ts, NULL);
	}

	return 0;
}

int main (int argc, char *argv[])
{
	unsigned speed = 9600;
	char *endp;
	FILE *file;
	int ret, option;

	while ((option = getopt (argc, argv, "s:")) != -1) {
		switch (option) {
		case 's':
			speed = strtoul (optarg, &endp, 0);
			if (*endp != '\0' || *optarg == '\0' || speed == 0)
				errx (1, "invalid speed: %s", optarg);
			break;
		default:
			return usage ();
		}
	}

	argv += optind;
	argc -= optind;

	if (argc == 0)
		return dofile (stdin, speed);

	ret = 0;

	for (int i = 0; i < argc; ++i) {
		if (strcmp (argv[i], "-") == 0) {
			dofile (stdin, speed);
		} else {
			file = fopen (argv[i], "r");
			if (file == NULL) {
				warnx ("open('%s')", argv[i]);
				ret = 2;
				continue;
			}
			dofile (file, speed);
			fclose (file);
		}
	}

	return ret;
}
