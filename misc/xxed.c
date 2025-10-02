#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <err.h>

static int fd;
static unsigned long bs;
static char *cmdline;

static void
chomp (char *s)
{
	size_t len;

	len = strlen (s);

	if (len > 0 && s[len - 1] == '\n')
		s[len - 1] = '\0';
}

static char *
next (void)
{
	return strsep (&cmdline, " \t");
}

struct cmd {
	const char *name;
	void (*f)(void);
};

static void
cmd_exit (void)
{
	exit (0);
}

static void
cmd_echo (void)
{
	char *arg;
	int i;

	for (i = 0; (arg = next ()) != NULL; ++i) {
		if (i > 0)
			putchar (' ');
		printf ("%s", arg);
	}
	putchar ('\n');
}

static bool
parse_size (const char *arg, long long *sz)
{
	char *endp;
	
	if (*arg == '\0')
		goto err;

	*sz = strtoll (arg, &endp, 0);

	switch (*endp) {
	case '\0':
		return true;
	case 'b':
		*sz *= bs;
		break;
	case 'w':
		*sz *= 2;
		break;
	case 'd':
		*sz *= 4;
		break;
	case 'q':
		*sz *= 8;
		break;
	case 's':
		*sz *= 512;
		break;
	case 'k':
	case 'K':
		*sz <<= 10;
		break;
	case 'm':
	case 'M':
		*sz <<= 20;
		break;
	case 'g':
	case 'G':
		*sz <<= 30;
		break;
	default:
		fprintf (stderr, "invalid size suffix: '%c'\n", *endp);
		return false;
	}

	++endp;

	if (*endp != '\0') {
	err:
		fprintf (stderr, "not a size: %s\n", arg);
		return false;
	}

	return true;
}

static void
cmd_print (void)
{
	unsigned char buf[16];
	off_t pos;
	ssize_t n;
	int i, j, spc;

	pos = lseek (fd, 0, SEEK_CUR);

	for (i = 0; i < 16; ++i, pos += n) {
		n = read (fd, buf, sizeof (buf));

		printf ("%08llx:", (unsigned long long)pos);

		spc = 16 * 2 + 3 + 3 + 2 + 1;

		for (j = 0; j < n; ++j) {
			if ((j % 8 == 0) && j != 0)
				putchar (' '), --spc;
			if (j % 2 == 0)
				putchar (' '), --spc;
			printf ("%02x", (unsigned)buf[j]);
			spc -= 2;
		}

		printf ("%*s | ", spc, "");

		for (j = 0; j < n; ++j) {
			putchar (isprint (buf[j]) ? buf[j] : '.');
		}

		putchar ('\n');

		if (n < sizeof (buf))
			break;
	}
}

static void
cmd_seek (void)
{
	char *arg;
	long long off;
	bool rel = false;

	arg = next ();
	if (arg == NULL) {
		rel = true;
		off = 0;
		goto seek;
	}

	if (*arg == '+' || *arg == '-')
		rel = true;

	if (!parse_size (arg, &off)) {
		fputs ("usage: seek [+-]off\n", stderr);
		return;
	}

seek:
	off = lseek (fd, off, rel ? SEEK_CUR : SEEK_SET);

	printf ("0x%08llx (block 0x%04llx)\n", (long long)off, (unsigned long long)(off / bs));
}

static struct cmd cmds[] = {
	{ .name = "exit",	.f = cmd_exit, },
	{ .name = "q",		.f = cmd_exit, },
	{ .name = "quit",	.f = cmd_exit, },
	{ .name = "echo",	.f = cmd_echo, },
	{ .name = "p",		.f = cmd_print, },
	{ .name = "print",	.f = cmd_print, },
	{ .name = "s",		.f = cmd_seek, },
	{ .name = "seek",	.f = cmd_seek, },
	{ NULL, NULL }
};

static struct cmd *
get_cmd (const char *name)
{
	struct cmd *cmd;

	for (cmd = cmds; cmd->name != NULL; ++cmd) {
		if (strcmp (name, cmd->name) == 0)
			return cmd;
	}

	return NULL;
}

#define MAXLINE 80
int
main (int argc, char *argv[])
{
	struct cmd *cmd;
	char *cmd_name, line[MAXLINE], lastline[MAXLINE];
	int option;

	bs = 512;

	while ((option = getopt (argc, argv, "b:")) != -1) {
		switch (option) {
		case 'b':
			bs = strtoul (optarg, NULL, 0);
			if (bs == 0)
				err (1, "invalid argument for `-b bs`");
		default:
			return 1;
		}
	}

	argv += optind;
	argc -= optind;

	if (argc != 1) {
		fputs ("usage: xxed [-b bs] file\n", stderr);
		return 1;
	}

	fd = open (argv[0], O_RDWR);
	if (fd < 0)
		err (1, "open('%s')", argv[0]);

	strlcpy (lastline, "p", sizeof (lastline));

	while (1) {
		fputs ("> ", stderr);
		if (fgets (line, sizeof (line), stdin) == NULL)
			break;

		chomp (line);

		if (line[0] == '\0') {
			memcpy (line, lastline, MAXLINE);
		} else {
			memcpy (lastline, line, MAXLINE);
		}

		cmdline = line;
		cmd_name = next ();
		if (cmd_name == NULL)
			continue;

		cmd = get_cmd (cmd_name);

		if (cmd != NULL) {
			cmd->f ();
		} else {
			fprintf (stderr, "command not found: %s\n", cmd_name);
		}
	}
	fputc ('\n', stderr);

	close (fd);
	return 0;
}
