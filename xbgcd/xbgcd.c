/* See LICENSE file for copyright and license details.
 *
 * X background color daemon is really simple. It just uses interpolation
 * between steps to get the new background color. If the substep reaches 1.0f,
 * it resets it, and starts the next step.
 */
#include <X11/Xlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct MyColor {
	float r, g, b;
};

#include "config.h"

const size_t num_steps = (sizeof steps) / (sizeof *steps);

static Display *dpy;
static int screen;
static Window root;

static unsigned long MyColorToPixel (const struct MyColor *color);
static unsigned long NameToPixel (const char *color);
static void FillBackground (const struct MyColor *color);
static void die (const char *s);

int
main(int argc, char *argv[])
{
	if (argc == 2 && !strcmp (argv[1], "-v")) {
		die ("xkbcd-" VERSION);
	} else if (argc != 1) {
		die ("usage: xbcd [-v]");
	}

	dpy = XOpenDisplay (NULL);
	if (!dpy)
		die ("xkbcd: unable to open display");

#ifdef __OpenBSD__
	if (pledge ("stdio", NULL) == -1)
		die ("pledge");
#endif // __OpenBSD__

	screen = DefaultScreen (dpy);
	root = RootWindow (dpy, screen);
 
	size_t step = 0;
	float sub_step = 0.0f;
	struct MyColor prev = initial_color;

	while (1) {
		const struct MyColor cur = steps[step];
		const struct MyColor c = {
			.r = prev.r + (cur.r - prev.r) * sub_step,
			.g = prev.g + (cur.g - prev.g) * sub_step,
			.b = prev.b + (cur.b - prev.b) * sub_step,
		};

		FillBackground (&c);

		sub_step += sub_step_inc;

		if (sub_step > 1.0f) {
			step = (step + 1) % num_steps;
			prev = cur;
			sub_step -= 1.0f;
		}
		usleep (delay * 1000);
	}
	XCloseDisplay (dpy);
	return 0;
}

static unsigned long
MyColorToPixel (const struct MyColor *color)
{
	char str[8];
	const int r = (int)(color->r * 255.0f);
	const int g = (int)(color->g * 255.0f);
	const int b = (int)(color->b * 255.0f);

	snprintf (str, sizeof str, "#%02X%02X%02X", r, g, b);

	return NameToPixel (str);
}

// This function is taken from xsetroot: https://github.com/freedesktop/xorg-xsetroot/blob/master/xsetroot.c
static unsigned long
NameToPixel (const char *name)
{
	XColor ecolor;

	if (!name || !*name)
		return BlackPixel (dpy, screen);

	Colormap colormap = DefaultColormap (dpy, screen);

	if (!XParseColor (dpy, colormap, name, &ecolor)) {
		fprintf (stderr, "xbgcd: unknown color '%s'\n", name);
		return BlackPixel (dpy, screen);
	}

	if (!XAllocColor (dpy, colormap, &ecolor)) {
		fprintf (stderr, "xbgcd: unable to allocate color for '%s'\n", name);
		return BlackPixel (dpy, screen);
	}

	return ecolor.pixel;
}

static void
FillBackground (const struct MyColor *color)
{
	unsigned long pixel = MyColorToPixel (color);
	XSetWindowBackground (dpy, root, pixel);
	XClearWindow (dpy, root);
	XFlush (dpy);
}

static void
die (const char *s)
{
	fprintf (stderr, "%s\n", s);
	exit (1);
}
