/*
 * Copyright (c) 2025 Benjamin Stürz <benni@stuerz.xyz>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <X11/Xlib.h>
#include <X11/extensions/scrnsaver.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>

static volatile sig_atomic_t got_signal;

static void
sighandler (int sig)
{
	(void)sig;
	got_signal = 1;
}

static int
usage (void)
{
	fputs ("usage: bidle [-t timeout] [-p program] [-i interval] [-nv]\n",
	       stderr);
	return 1;
}

static void
spawn (const char *prog, bool dry, bool verbose, unsigned long idle_ms)
{
	pid_t pid;

	if (dry) {
		fprintf (stderr, "bidle: [dry-run] idle %lu ms, would run '%s'\n",
			 idle_ms, prog);
		return;
	}

	if (verbose)
		fprintf (stderr, "bidle: idle %lu ms, running '%s'\n",
			 idle_ms, prog);

	pid = fork ();
	if (pid < 0) {
		warn ("fork()");
		return;
	}

	if (pid == 0) {
		setsid ();
		execlp (prog, prog, (char *)NULL);
		warn ("execlp('%s')", prog);
		_exit (127);
	}
}

int
main (int argc, char *argv[])
{
	const char *prog = "slock";
	char *endp;
	int timeout = 300, interval = 1, option;
	bool dry = false, verbose = false, locked = false;
	Display *dpy;
	XScreenSaverInfo *info;
	int evt, errb;
	unsigned long timeout_ms, idle_ms;
	Status st;

	while ((option = getopt (argc, argv, "t:p:i:nv")) != -1) {
		switch (option) {
		case 't':
			timeout = (int)strtol (optarg, &endp, 10);
			if (*endp != '\0' || optarg[0] == '\0' || timeout <= 0)
				errx (1, "invalid timeout: %s", optarg);
			break;
		case 'p':
			prog = optarg;
			break;
		case 'i':
			interval = (int)strtol (optarg, &endp, 10);
			if (*endp != '\0' || optarg[0] == '\0' || interval <= 0)
				errx (1, "invalid interval: %s", optarg);
			break;
		case 'n':
			dry = true;
			break;
		case 'v':
			verbose = true;
			break;
		default:
			return usage ();
		}
	}

	if (optind != argc)
		return usage ();

	signal (SIGCHLD, SIG_IGN);
	signal (SIGINT, sighandler);
	signal (SIGTERM, sighandler);

	dpy = XOpenDisplay (NULL);
	if (dpy == NULL)
		errx (1, "cannot open display");

	if (XScreenSaverQueryExtension (dpy, &evt, &errb) == 0)
		errx (1, "no MIT-SCREEN-SAVER extension on the server");

	info = XScreenSaverAllocInfo ();
	if (info == NULL)
		err (1, "XScreenSaverAllocInfo");

#ifdef __OpenBSD__
	if (pledge ("stdio rpath proc exec", NULL) == -1)
		err (1, "pledge");
#endif

	if (verbose)
		fprintf (stderr, "bidle: timeout=%ds interval=%ds program='%s'\n",
			 timeout, interval, prog);

	timeout_ms = (unsigned long)timeout * 1000UL;

	for (;;) {
		if (got_signal)
			break;

		st = XScreenSaverQueryInfo (dpy, DefaultRootWindow (dpy), info);
		if (st == 0) {
			warnx ("XScreenSaverQueryInfo failed, exiting");
			break;
		}

		idle_ms = info->idle;

		if (verbose)
			fprintf (stderr, "bidle: idle=%lu ms%s\n", idle_ms,
				 locked ? " (locked)" : "");

		if (idle_ms >= timeout_ms) {
			if (!locked) {
				spawn (prog, dry, verbose, idle_ms);
				locked = true;
			}
		} else {
			locked = false;
		}

		XSync (dpy, False);
		sleep (interval);
	}

	XFree (info);
	XCloseDisplay (dpy);
	return 0;
}
