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
#include <sys/types.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <ncurses.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WT_DIR     "Documents/wt"
#define DATE_LEN   11
#define TIME_LEN   6
#define DESC_MAX   256
#define STATUS_LEN 80

struct row {
	char date[DATE_LEN];
	char start[TIME_LEN];
	char end[TIME_LEN];
	char *desc;
};

struct ctx {
	char *path;
	struct row *rows;
	size_t nrows;
	int running;
	time_t start_ts;
	char status[STATUS_LEN];
	char *new_desc;
};

static int K_CLEFT  = 0;
static int K_CRIGHT = 0;

static volatile sig_atomic_t got_signal = 0;
static volatile sig_atomic_t stop_session = 0;

static void
on_sig(int signo)
{
	if (signo == SIGUSR1)
		stop_session = 1;
	else
		got_signal = 1;
}

static void *
xmalloc(size_t n)
{
	void *p = malloc(n);
	if (!p) err(1, "malloc");
	return p;
}

static char *
xstrdup(const char *s)
{
	char *p = strdup(s);
	if (!p) err(1, "strdup");
	return p;
}

static void
trim(char *s)
{
	char *start = s;
	size_t len;

	while (*start && isspace((unsigned char)*start)) start++;
	if (start != s) memmove(s, start, strlen(start) + 1);

	len = strlen(s);
	while (len > 0 && isspace((unsigned char)s[len - 1]))
		s[--len] = '\0';
}

static int
looks_like_date(const char *s)
{
	if (strlen(s) != 10) return 0;
	if (s[4] != '-' || s[7] != '-') return 0;
	for (int i = 0; i < 10; ++i) {
		if (i == 4 || i == 7) continue;
		if (!isdigit((unsigned char)s[i])) return 0;
	}
	return 1;
}

static char *
path_join(const char *a, const char *b)
{
	size_t n = strlen(a) + 1 + strlen(b) + 1;
	char *p = xmalloc(n);
	snprintf(p, n, "%s/%s", a, b);
	return p;
}

static char *
wt_dir(void)
{
	const char *home = getenv("HOME");
	if (!home || !*home) errx(1, "HOME is not set");
	return path_join(home, WT_DIR);
}

static int
mkdir_p(const char *path)
{
	char *copy = xstrdup(path);
	int rc = 0;

	for (char *p = copy + 1; *p; ++p) {
		if (*p == '/') {
			*p = '\0';
			if (mkdir(copy, 0755) != 0 && errno != EEXIST) {
				rc = -1;
				goto out;
			}
			*p = '/';
		}
	}
	if (mkdir(copy, 0755) != 0 && errno != EEXIST)
		rc = -1;
out:
	free(copy);
	return rc;
}

static char *
csv_next(char **pp)
{
	char *p = *pp;
	char *start = p;
	char *out = p;

	if (*p == '"') {
		p++;
		while (*p) {
			if (*p == '"') {
				if (p[1] == '"') {
					*out++ = '"';
					p += 2;
				} else {
					p++;
					break;
				}
			} else {
				*out++ = *p++;
			}
		}
		*out = '\0';
		while (*p && *p != ',') p++;
		if (*p == ',') *p++ = '\0';
	} else {
		while (*p && *p != ',') p++;
		if (*p == ',') *p++ = '\0';
	}

	*pp = p;
	return start;
}

static void
write_csv_field(FILE *f, const char *s)
{
	int quote = 0;

	for (const char *p = s; *p; ++p) {
		if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') {
			quote = 1;
			break;
		}
	}
	if (!quote) {
		fputs(s, f);
		return;
	}
	fputc('"', f);
	for (const char *p = s; *p; ++p) {
		if (*p == '"') fputc('"', f);
		fputc(*p, f);
	}
	fputc('"', f);
}

static void
write_csv(struct ctx *ctx)
{
	size_t plen = strlen(ctx->path);
	char *tmp = xmalloc(plen + 9);
	FILE *f;

	snprintf(tmp, plen + 9, "%s.wt.tmp", ctx->path);

	f = fopen(tmp, "w");
	if (!f) err(1, "fopen('%s')", tmp);
	fprintf(f, "date,start_time,end_time,description\n");
	for (size_t i = 0; i < ctx->nrows; ++i) {
		struct row *r = &ctx->rows[i];
		if (!r->date[0] && !r->start[0] && !r->end[0] &&
		    (!r->desc || !r->desc[0]))
			continue;
		fprintf(f, "%s,%s,%s,", r->date, r->start, r->end);
		write_csv_field(f, r->desc ? r->desc : "");
		fputc('\n', f);
	}
	if (fflush(f) != 0) err(1, "fflush('%s')", tmp);
	if (fsync(fileno(f)) != 0) warn("fsync('%s')", tmp);
	if (fclose(f) != 0) err(1, "fclose('%s')", tmp);
	if (rename(tmp, ctx->path) != 0)
		err(1, "rename('%s', '%s')", tmp, ctx->path);
	free(tmp);
}

static void
load_csv(const char *path, struct ctx *ctx)
{
	FILE *f = fopen(path, "r");
	size_t cap = 16;
	char line[1024];

	ctx->rows = xmalloc(cap * sizeof(*ctx->rows));
	ctx->nrows = 0;

	if (!f) {
		if (errno != ENOENT) err(1, "fopen('%s')", path);
		return;
	}

	while (fgets(line, sizeof(line), f)) {
		char *p = line;
		char *f1 = csv_next(&p);
		char *f2 = csv_next(&p);
		char *f3 = csv_next(&p);
		char *f4 = csv_next(&p);
		if (!looks_like_date(f1)) continue;

		if (ctx->nrows == cap) {
			cap *= 2;
			ctx->rows = realloc(ctx->rows, cap * sizeof(*ctx->rows));
			if (!ctx->rows) err(1, "realloc");
		}
		struct row *r = &ctx->rows[ctx->nrows++];
		snprintf(r->date, sizeof(r->date), "%s", f1);
		snprintf(r->start, sizeof(r->start), "%s", f2);
		snprintf(r->end, sizeof(r->end), "%s", f3);
		trim(f4);
		r->desc = xstrdup(f4);
	}
	fclose(f);
}

static void
fill_date(char *buf, size_t n, time_t t)
{
	struct tm tm;
	localtime_r(&t, &tm);
	strftime(buf, n, "%Y-%m-%d", &tm);
}

static void
fill_time(char *buf, size_t n, time_t t)
{
	struct tm tm;
	localtime_r(&t, &tm);
	strftime(buf, n, "%H:%M", &tm);
}

static void
elapsed_str(long sec, char *buf, size_t n)
{
	int h, m, s;

	if (sec < 0) sec = 0;
	if (sec > 359999L) sec = 359999L;
	h = (int)(sec / 3600);
	m = (int)((sec / 60) % 60);
	s = (int)(sec % 60);
	snprintf(buf, n, "%02d:%02d:%02d", h, m, s);
}

static void
duration_str(const char *start, const char *end, char *buf, size_t n)
{
	int sh, sm, eh, em, mins, h, m;

	if (!end[0] ||
	    sscanf(start, "%d:%d", &sh, &sm) != 2 ||
	    sscanf(end, "%d:%d", &eh, &em) != 2) {
		snprintf(buf, n, "?");
		return;
	}
	mins = (eh * 60 + em) - (sh * 60 + sm);
	if (mins < 0) mins += 24 * 60;
	h = mins / 60;
	m = mins % 60;
	if (h > 0)
		snprintf(buf, n, "%dh%dm", h, m);
	else
		snprintf(buf, n, "%dm", m);
}

static char **
list_csvs(const char *dir, size_t *count)
{
	DIR *d = opendir(dir);
	char **arr = NULL;
	size_t cap = 0, n = 0;
	struct dirent *de;

	*count = 0;
	if (!d) return NULL;

	while ((de = readdir(d))) {
		size_t len = strlen(de->d_name);
		if (len < 4) continue;
		if (strcmp(de->d_name + len - 4, ".csv") != 0) continue;
		if (n == cap) {
			cap = cap ? cap * 2 : 8;
			arr = realloc(arr, cap * sizeof(*arr));
			if (!arr) err(1, "realloc");
		}
		arr[n++] = xstrdup(de->d_name);
	}
	closedir(d);

	for (size_t i = 1; i < n; ++i) {
		char *k = arr[i];
		size_t j = i;
		while (j > 0 && strcmp(arr[j - 1], k) > 0) {
			arr[j] = arr[j - 1];
			--j;
		}
		arr[j] = k;
	}

	*count = n;
	return arr;
}

static void
free_strv(char **arr, size_t n)
{
	for (size_t i = 0; i < n; ++i) free(arr[i]);
	free(arr);
}

static void
init_keys(void)
{
	K_CLEFT = key_defined("\033[1;5D");
	if (K_CLEFT == 0) {
		K_CLEFT = KEY_MAX + 1;
		define_key("\033[1;5D", K_CLEFT);
	}
	K_CRIGHT = key_defined("\033[1;5C");
	if (K_CRIGHT == 0) {
		K_CRIGHT = KEY_MAX + 2;
		define_key("\033[1;5C", K_CRIGHT);
	}
}

static int
prompt_input(const char *prompt, char *buf, size_t bufsize)
{
	int rows, cols;
	size_t len = strlen(buf);
	size_t cur = len;
	int accepted = 0;

	getmaxyx(stdscr, rows, cols);
	(void)cols;

	curs_set(1);
	timeout(-1);
	buf[len] = '\0';

	for (;;) {
		int prompt_len = (int)strlen(prompt) + 2;
		int avail = cols - prompt_len - 1;
		size_t show_start;

		if (avail < 1) avail = 1;
		show_start = cur >= (size_t)avail ? cur - (size_t)avail + 1 : 0;
		if (show_start > len) show_start = len;

		mvprintw(rows - 1, 0, "%s: ", prompt);
		for (size_t i = show_start; i < len && (int)(i - show_start) < avail; ++i)
			addch(buf[i]);
		clrtoeol();
		move(rows - 1, prompt_len + (int)(cur - show_start));
		refresh();

		int c = getch();
		if (c == ERR) {
			if (got_signal) break;
			continue;
		}

		if (c == '\r' || c == '\n' || c == KEY_ENTER) {
			accepted = 1;
			break;
		}
		if (c == 7) break;
		if (c == 27) {
			nodelay(stdscr, TRUE);
			int c2 = getch();
			if (c2 == ERR) {
				nodelay(stdscr, FALSE);
				break;
			}
			if (c2 == 'b' || c2 == 'B') {
				while (cur > 0 && isspace((unsigned char)buf[cur - 1])) cur--;
				while (cur > 0 && !isspace((unsigned char)buf[cur - 1])) cur--;
			} else if (c2 == 'f' || c2 == 'F') {
				while (cur < len && isspace((unsigned char)buf[cur])) cur++;
				while (cur < len && !isspace((unsigned char)buf[cur])) cur++;
			} else if (c2 == '[' || c2 == 'O') {
				char seq[8];
				char finalc = 0;
				int n = 0, modifier = 0, cc;
				while ((cc = getch()) != ERR && n < (int)sizeof(seq)) {
					seq[n++] = (char)cc;
					if (cc >= 0x40 && cc <= 0x7e) { finalc = (char)cc; break; }
				}
				for (int i = 0; i < n - 1; ++i) {
					if (seq[i] == ';' && i + 1 < n - 1 &&
					    isdigit((unsigned char)seq[i + 1])) {
						modifier = seq[i + 1] - '0';
						break;
					}
				}
				if (modifier == 5) {
					if (finalc == 'C') {
						while (cur < len && isspace((unsigned char)buf[cur])) cur++;
						while (cur < len && !isspace((unsigned char)buf[cur])) cur++;
					} else if (finalc == 'D') {
						while (cur > 0 && isspace((unsigned char)buf[cur - 1])) cur--;
						while (cur > 0 && !isspace((unsigned char)buf[cur - 1])) cur--;
					}
				}
			}
			nodelay(stdscr, FALSE);
			continue;
		}

		if (c == 1) {
			cur = 0;
		} else if (c == 2) {
			if (cur > 0) cur--;
		} else if (c == 5) {
			cur = len;
		} else if (c == 6) {
			if (cur < len) cur++;
		} else if (c == 4) {
			if (cur < len) {
				memmove(buf + cur, buf + cur + 1, len - cur);
				len--;
			}
		} else if (c == 11) {
			len = cur;
			buf[len] = '\0';
		} else if (c == 21) {
			memmove(buf, buf + cur, len - cur + 1);
			len -= cur;
			cur = 0;
		} else if (c == 23) {
			size_t start = cur;
			while (start > 0 && isspace((unsigned char)buf[start - 1])) start--;
			while (start > 0 && !isspace((unsigned char)buf[start - 1])) start--;
			memmove(buf + start, buf + cur, len - cur + 1);
			len -= cur - start;
			cur = start;
		} else if (c == 20 && len >= 2) {
			if (cur == 0) cur = 1;
			else if (cur == len) cur = len - 1;
			char t = buf[cur - 1];
			buf[cur - 1] = buf[cur];
			buf[cur] = t;
			cur++;
		} else if (c == 12) {
			redrawwin(stdscr);
		} else if (c == KEY_LEFT) {
			if (cur > 0) cur--;
		} else if (c == KEY_RIGHT) {
			if (cur < len) cur++;
		} else if (c == K_CLEFT) {
			while (cur > 0 && isspace((unsigned char)buf[cur - 1])) cur--;
			while (cur > 0 && !isspace((unsigned char)buf[cur - 1])) cur--;
		} else if (c == K_CRIGHT) {
			while (cur < len && isspace((unsigned char)buf[cur])) cur++;
			while (cur < len && !isspace((unsigned char)buf[cur])) cur++;
		} else if (c == KEY_HOME) {
			cur = 0;
		} else if (c == KEY_END) {
			cur = len;
		} else if (c == KEY_DC) {
			if (cur < len) {
				memmove(buf + cur, buf + cur + 1, len - cur);
				len--;
			}
		} else if (c == KEY_BACKSPACE || c == 127 || c == 8) {
			if (cur > 0) {
				memmove(buf + cur - 1, buf + cur, len - cur + 1);
				cur--;
				len--;
			}
		} else if (c >= 32 && c < 127) {
			if (len + 1 < bufsize) {
				memmove(buf + cur + 1, buf + cur, len - cur + 1);
				buf[cur] = (char)c;
				cur++;
				len++;
			}
		}
	}

	curs_set(0);
	return accepted;
}

static char *
pick_file(const char *dir)
{
	for (;;) {
		size_t n;
		char **files = list_csvs(dir, &n);
		int sel = 0;

		for (;;) {
			int rows, cols;
			if (got_signal) {
				free_strv(files, n);
				return NULL;
			}
			stop_session = 0;

			getmaxyx(stdscr, rows, cols);

			clear();
			mvprintw(0, 0, "wt - %s", dir);
			mvprintw(1, 0, "[Up/Down] select  [Enter] open  [n] new  [q] quit");
			mvhline(2, 0, ACS_HLINE, cols);

			if (n == 0) {
				mvprintw(4, 0, "(no .csv files - press 'n' to create one)");
			} else {
				int list_h = rows - 4;
				if (list_h < 1) list_h = 1;
				for (int i = 0; i < list_h && i < (int)n; ++i) {
					if (i == sel) attron(A_REVERSE);
					mvprintw(3 + i, 2, "%s", files[i]);
					clrtoeol();
					if (i == sel) attroff(A_REVERSE);
				}
			}

			refresh();
			int c = getch();

			if (c == 'q' || c == 27) {
				free_strv(files, n);
				return NULL;
			} else if (c == 'n') {
				char name[DESC_MAX + 8];
				name[0] = '\0';
				if (prompt_input("New file name", name, sizeof(name)) && name[0]) {
					size_t len = strlen(name);
					if (len < 4 || strcmp(name + len - 4, ".csv") != 0)
						strcat(name, ".csv");
					char *path = path_join(dir, name);
					free_strv(files, n);
					FILE *f = fopen(path, "w");
					if (!f) err(1, "fopen('%s')", path);
					fclose(f);
					return path;
				}
			} else if (c == KEY_UP || c == 'k') {
				if (sel > 0) sel--;
			} else if (c == KEY_DOWN || c == 'j') {
				if (sel < (int)n - 1) sel++;
			} else if (c == '\n' || c == KEY_ENTER) {
				if (n > 0) {
					char *path = path_join(dir, files[sel]);
					free_strv(files, n);
					return path;
				}
			} else if (c == 12) {
				redrawwin(stdscr);
			}
		}
	}
}

static void edit_description(struct row *);

static void
signal_bedstatus(void)
{
	FILE *p = popen("pgrep -x bedstatus", "r");
	char line[32];

	if (p == NULL)
		return;
	while (fgets(line, sizeof(line), p) != NULL) {
		pid_t pid = (pid_t)strtol(line, NULL, 10);
		if (pid > 1)
			kill(pid, SIGUSR1);
	}
	pclose(p);
}

static const char *
lock_path(void)
{
	static char *p = NULL;
	const char *dir;

	if (p != NULL)
		return p;

	dir = getenv("XDG_RUNTIME_DIR");
	if (dir == NULL || *dir == '\0')
		dir = "/tmp";

	size_t n = strlen(dir) + 1 + strlen("wt.active") + 1;
	p = xmalloc(n);
	snprintf(p, n, "%s/wt.active", dir);
	return p;
}

static int
lock_exists(void)
{
	struct stat st;
	return stat(lock_path(), &st) == 0;
}

static void
lock_create(const char *path)
{
	const char *base = strrchr(path, '/');
	char stem[256];
	size_t len;
	FILE *f;

	base = base ? base + 1 : path;
	snprintf(stem, sizeof(stem), "%s", base);
	len = strlen(stem);
	if (len > 4 && strcmp(stem + len - 4, ".csv") == 0)
		stem[len - 4] = '\0';

	f = fopen(lock_path(), "w");
	if (!f) err(1, "fopen('%s')", lock_path());
	fprintf(f, "%d %s\n", (int)getpid(), stem);
	fclose(f);

	signal_bedstatus();
}

static void
lock_remove(void)
{
	unlink(lock_path());
}

static int
confirm(const char *msg)
{
	int rows, cols;
	int c;

	getmaxyx(stdscr, rows, cols);
	(void)cols;

	timeout(-1);
	mvprintw(rows - 1, 0, "%s [y/N]: ", msg);
	clrtoeol();
	refresh();
	c = getch();
	return c == 'y' || c == 'Y';
}

static int
show_sessions(struct ctx *ctx)
{
	int sel = (int)ctx->nrows - 1;
	int top = 0;

	if (sel < 0) sel = 0;

	for (;;) {
		int rows, cols;
		int list_h;

		if (got_signal) return 0;
		stop_session = 0;

		getmaxyx(stdscr, rows, cols);
		list_h = rows - 5;
		if (list_h < 1) list_h = 1;

		if (sel < top) top = sel;
		if (sel >= top + list_h) top = sel - list_h + 1;
		if (top < 0) top = 0;

		clear();
		mvprintw(0, 0, "wt - %s", ctx->path);
		mvprintw(1, 0, "%-10s %5s -> %-5s  %-8s %s",
		    "DATE", "START", "END", "DURATION", "DESCRIPTION");
		mvhline(2, 0, ACS_HLINE, cols);

		for (int i = 0; i < list_h; ++i) {
			int idx = top + i;
			if (idx >= (int)ctx->nrows) break;
			struct row *r = &ctx->rows[idx];
			char dur[32];
			if (r->end[0])
				duration_str(r->start, r->end, dur, sizeof(dur));
			else
				snprintf(dur, sizeof(dur), "?");

			if (idx == sel) attron(A_REVERSE);
			mvprintw(3 + i, 0, "%-10s %5s -> %-5s  %-8s %.*s",
			    r->date, r->start, r->end[0] ? r->end : "now",
			    dur, cols - 40, r->desc ? r->desc : "");
			clrtoeol();
			if (idx == sel) attroff(A_REVERSE);
		}

		if (ctx->nrows == 0)
			mvprintw(3, 0, "(no sessions yet - press Space to start one)");

		mvprintw(rows - 2, 0, "[Space] New session  [Enter] reuse desc  [Up/Down] navigate  [e] edit  [d] delete");
		mvprintw(rows - 1, 0, "[q] back");
		if (ctx->status[0])
			mvprintw(rows - 1, 30, "%s", ctx->status);

		refresh();
		int c = getch();
		ctx->status[0] = '\0';

		if (c == 'q' || c == 27) return 0;
		if (c == ' ' || c == '\n' || c == KEY_ENTER) {
			if (lock_exists()) {
				beep();
				snprintf(ctx->status, sizeof(ctx->status),
				    "session active (%s)", lock_path());
			} else {
				if ((c == '\n' || c == KEY_ENTER) && ctx->nrows > 0) {
					free(ctx->new_desc);
					ctx->new_desc = xstrdup(ctx->rows[sel].desc ?
					    ctx->rows[sel].desc : "");
				}
				return 1;
			}
		}
		if (c == 'e') {
			if (ctx->nrows > 0) {
				edit_description(&ctx->rows[sel]);
				write_csv(ctx);
				snprintf(ctx->status, sizeof(ctx->status), "saved");
			}
		} else if (c == 'd') {
			if (ctx->nrows > 0 && confirm("Delete this entry?")) {
				free(ctx->rows[sel].desc);
				memmove(&ctx->rows[sel], &ctx->rows[sel + 1],
				    (ctx->nrows - sel - 1) * sizeof(ctx->rows[0]));
				ctx->nrows--;
				write_csv(ctx);
				if (sel >= (int)ctx->nrows && sel > 0) sel--;
				snprintf(ctx->status, sizeof(ctx->status), "deleted");
			}
		} else if (c == KEY_UP || c == 'k') {
			if (sel > 0) sel--;
		} else if (c == KEY_DOWN || c == 'j') {
			if (sel < (int)ctx->nrows - 1) sel++;
		} else if (c == 12) {
			redrawwin(stdscr);
		}
	}
}

static void
draw_running(struct ctx *ctx, long elapsed)
{
	int rows, cols;
	char el[16];
	struct row *r = &ctx->rows[ctx->nrows - 1];

	elapsed_str(elapsed, el, sizeof(el));

	getmaxyx(stdscr, rows, cols);

	clear();
	mvprintw(0, 0, "wt - RUNNING");
	mvprintw(1, 0, "started %s %s", r->date, r->start);

	attron(A_BOLD);
	mvprintw(rows / 3, (cols - (int)strlen(el)) / 2, "%s", el);
	attroff(A_BOLD);
	mvprintw(rows / 3 + 1, (cols - 8) / 2, "elapsed");

	mvprintw(rows / 3 + 4, 0, "Description: %s%s",
	    r->desc ? r->desc : "",
	    (r->desc && r->desc[0]) ? "" : " (empty - press 'e' to set)");

	mvprintw(rows - 3, 0, "[Space] stop  [e] edit description  [c] cancel");
	mvprintw(rows - 2, 0, "[q] refused while running");
	mvprintw(rows - 1, 0, "[^L] redraw");
	if (ctx->status[0]) {
		attron(A_BOLD);
		mvprintw(rows - 1, 30, "%s", ctx->status);
		attroff(A_BOLD);
	}

	refresh();
}

static void
edit_description(struct row *r)
{
	char buf[DESC_MAX];

	snprintf(buf, sizeof(buf), "%s", r->desc ? r->desc : "");
	if (prompt_input("Description", buf, sizeof(buf))) {
		trim(buf);
		free(r->desc);
		r->desc = xstrdup(buf);
	}
}

static void
run_session(struct ctx *ctx)
{
	time_t start_ts = time(NULL);
	time_t next_flush = (start_ts / 60 + 1) * 60;
	struct row *r;

	ctx->rows = realloc(ctx->rows, (ctx->nrows + 1) * sizeof(*ctx->rows));
	if (!ctx->rows) err(1, "realloc");
	r = &ctx->rows[ctx->nrows++];
	fill_date(r->date, sizeof(r->date), start_ts);
	fill_time(r->start, sizeof(r->start), start_ts);
	r->end[0] = '\0';
	r->desc = ctx->new_desc ? ctx->new_desc : xstrdup("");
	ctx->new_desc = NULL;
	ctx->running = 1;
	ctx->start_ts = start_ts;

	fill_time(r->end, sizeof(r->end), start_ts);
	write_csv(ctx);
	r->end[0] = '\0';

	lock_create(ctx->path);

	for (;;) {
		time_t now;
		long elapsed;

		if (got_signal) {
			now = time(NULL);
			fill_time(r->end, sizeof(r->end), now);
			write_csv(ctx);
			ctx->running = 0;
			lock_remove();
			return;
		}

		if (stop_session) {
			stop_session = 0;
			now = time(NULL);
			fill_time(r->end, sizeof(r->end), now);
			write_csv(ctx);
			ctx->running = 0;
			lock_remove();
			snprintf(ctx->status, sizeof(ctx->status),
			    "session ended (screen lock)");
			return;
		}

		timeout(1000);
		now = time(NULL);
		elapsed = (long)(now - start_ts);

		if (now >= next_flush) {
			fill_time(r->end, sizeof(r->end), now);
			write_csv(ctx);
			r->end[0] = '\0';
			next_flush = (now / 60 + 1) * 60;
		}

		draw_running(ctx, elapsed);
		int ch = getch();

		if (ch == ERR) continue;
		ctx->status[0] = '\0';

		if (ch == ' ') {
			fill_time(r->end, sizeof(r->end), now);
			write_csv(ctx);
			ctx->running = 0;
			lock_remove();
			snprintf(ctx->status, sizeof(ctx->status), "session saved");
			return;
		} else if (ch == 'c') {
			free(r->desc);
			r->desc = NULL;
			ctx->nrows--;
			write_csv(ctx);
			ctx->running = 0;
			lock_remove();
			snprintf(ctx->status, sizeof(ctx->status),
			    "session cancelled");
			return;
		} else if (ch == 'e') {
			edit_description(r);
			fill_time(r->end, sizeof(r->end), now);
			write_csv(ctx);
			r->end[0] = '\0';
		} else if (ch == 'q' || ch == 27) {
			beep();
			snprintf(ctx->status, sizeof(ctx->status),
			    "stop or cancel first (Space / c)");
		} else if (ch == 12) {
			redrawwin(stdscr);
		}
	}
}

static void
free_ctx(struct ctx *ctx)
{
	for (size_t i = 0; i < ctx->nrows; ++i)
		free(ctx->rows[i].desc);
	free(ctx->rows);
	free(ctx->path);
	free(ctx->new_desc);
}

static void
restore_terminal(void)
{
	endwin();
}

int
main(void)
{
	char *dir;
	struct ctx ctx;
	struct sigaction sa;

	memset(&ctx, 0, sizeof(ctx));
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_sig;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGUSR1, &sa, NULL);

	dir = wt_dir();
	if (mkdir_p(dir) != 0)
		err(1, "mkdir('%s')", dir);

	initscr();
	atexit(restore_terminal);
	set_escdelay(250);
	cbreak();
	noecho();
	keypad(stdscr, TRUE);
	init_keys();
	curs_set(0);

	ctx.path = NULL;

	for (;;) {
		free_ctx(&ctx);
		memset(&ctx, 0, sizeof(ctx));

		ctx.path = pick_file(dir);
		if (!ctx.path || got_signal) goto done;

		load_csv(ctx.path, &ctx);

		while (show_sessions(&ctx) && !got_signal)
			run_session(&ctx);
	}

done:
	free_ctx(&ctx);
	free(dir);
	return 0;
}
