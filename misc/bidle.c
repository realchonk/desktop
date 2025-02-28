#include <X11/extensions/scrnsaver.h>
#include <X11/Xlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <err.h>

static Display *dpy;
static int saver_event;

static void lock (void)
{
	pid_t pid;

	pid = fork ();
	if (pid < 0)
		err (1, "fork()");

	puts ("LOCKING SCREEN");

	if (pid == 0) {
		setsid ();
		execlp ("slock", "slock", NULL);
		err (1, "exec()");
	} else {
		wait (NULL);
		XSync (dpy, True);
	}
}

int main (void)
{
	XScreenSaverNotifyEvent *se;
	XEvent ev;
	int error, timeout;

	timeout = 5;

	dpy = XOpenDisplay (NULL);
	if (dpy == NULL)
		errx (1, "cannot open display");

	if (!XScreenSaverQueryExtension (dpy, &saver_event, &error))
		errx (1, "no screen XScreenSaver extension");

	XScreenSaverSelectInput (dpy, DefaultRootWindow (dpy), ScreenSaverNotifyMask);
	XSetScreenSaver (dpy, timeout, 0, DontPreferBlanking, DontAllowExposures);
	
	//signal (SIGINT, bye);
	//signal (SIGTERM, bye);

	while (1) {
		XNextEvent (dpy, &ev);

		if (ev.type == ClientMessage) {
			XCloseDisplay (dpy);
			return 0;
		} else if (ev.type == saver_event) {
			se = (XScreenSaverNotifyEvent *)&ev;

			if (se->forced == False)
				lock ();
		}

	}
}
