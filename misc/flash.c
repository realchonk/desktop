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
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <err.h>

typedef unsigned long long ullong;

static int usage (void)
{
	fputs ("usage: flash [-c] [-b blocksize] <file> <device>\n", stderr);
	return 1;
}

static size_t read_all (int fd, char *buffer, size_t num)
{
	ssize_t n = -1;
	size_t i;

	for (i = 0; i < num && n != 0; i += n) {
		n = read (fd, buffer + i, num - i);
		if (n < 0)
			err (1, "read()");
	}

	return i;
}

static void write_all (int fd, const char *buffer, size_t num)
{
	ssize_t n;
	size_t i;

	for (i = 0; i < num; i += n) {
		n = write (fd, buffer + i, num - i);
		if (n <= 0)
			err (1, "write()");
	}
}

static void print_size (ullong i)
{
	static const struct unit {
		const char *s;
		ullong u;
	} units[] = {
		{ "PiB",	1ull << 50 },
		{ "TiB",	1ull << 40 },
		{ "GiB",	1ull << 30 },
		{ "MiB",	1ull << 20 },
		{ "KiB",	1ull << 10 },
		{ NULL,		0 },
	};

	for (const struct unit *u = units; u-> s != NULL; ++u) {
		if (i < u->u)
			continue;

		const unsigned raw = i * 100 / u->u;

		if (raw >= 10000) {
			fprintf (stderr, "%u", raw / 100);
		} else if (raw >= 1000) {
			fprintf (stderr, "%u.%u", raw / 100, raw / 10 % 10);
		} else {
			fprintf (stderr, "%u.%02u", raw / 100, raw % 100);
		}

		fprintf (stderr, " %s", u->s);
		return;
	}

	fprintf (stderr, "%u B", (unsigned)i);
}

static void update_progress (ullong i, ullong num)
{
	unsigned perc;

	perc = i * 100 / num;
	fprintf (stderr, "\rProgress: %u%% (", perc);
	print_size (i);
	fputc ('/', stderr);
	print_size (num);
	fputc (')', stderr);
}

static void copy (int srcfd, int devfd, char *buffer, size_t size, ullong num)
{
	ullong i;
	size_t n = -1;

	for (i = 0; i < num && n != 0; i += n) {
		update_progress (i, num);
		n = read_all (srcfd, buffer, size);
		write_all (devfd, buffer, n);
		if (fdatasync (devfd) != 0)
			err (1, "fdatasync()");
	}
	update_progress (i, num);
	fputc ('\n', stderr);
}

int main (int argc, char *argv[])
{
	struct stat st;
	size_t size = 1 << 20;
	char *buffer, *endp, *bn;
	int option, srcfd, devfd, dirfd, create = 0;

	while ((option = getopt (argc, argv, "b:c")) != -1) {
		switch (option) {
		case 'b':
			size = strtoul (optarg, &endp, 10);
			if (optarg[0] == '\0' || *endp != '\0')
				return usage ();
			break;
		case 'c':
			create = 1;
			break;
		default:
			return usage ();
		}
	}

	argv += optind;
	argc -= optind;

	if (argc != 2)
		return usage ();

	srcfd = open (argv[0], O_RDONLY);
	if (srcfd < 0)
		err (1, "open('%s')", argv[0]);

	if (create) {
		if (stat (argv[1], &st) == 0 && S_ISDIR (st.st_mode)) {
			dirfd = open (argv[1], O_RDONLY | O_DIRECTORY);
			if (dirfd < 0)
				err (1, "open('%s')", argv[1]);

			bn = strdup (argv[0]);
			devfd = openat (dirfd, basename (bn), O_WRONLY | O_CREAT | O_TRUNC, 0644);
			free (bn);
			close (dirfd);
		} else {
			devfd = open (argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
		}
	} else {
		devfd = open (argv[1], O_WRONLY);
	}
	if (devfd < 0)
		err (1, "open('%s')", argv[1]);


	buffer = malloc (size);
	if (buffer == NULL)
		err (1, "malloc()");

	if (fstat (srcfd, &st) != 0)
		err (1, "fstat('%s')", argv[0]);

	if (st.st_size == 0)
		errx (1, "file size is zero");

	copy (srcfd, devfd, buffer, size, (ullong)st.st_size);
	if (fsync (devfd) != 0)
		err (1, "fsync()");

	close (devfd);
	close (srcfd);
	return 0;
}
