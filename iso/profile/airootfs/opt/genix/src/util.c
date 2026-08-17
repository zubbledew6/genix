/* fork/exec, mkdir -p, that kind of thing */
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

void
die(const char *fmt, ...)
{
	va_list ap;
	fputs("error: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

void
warnx(const char *fmt, ...)
{
	va_list ap;
	fputs("warning: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

void *
xmalloc(size_t n)
{
	void *p = malloc(n ? n : 1);
	if (!p) {
		fputs("oom\n", stderr);
		exit(1);
	}
	return p;
}

void *
xrealloc(void *p, size_t n)
{
	void *q = realloc(p, n ? n : 1);
	if (!q)
		die("out of memory");
	return q;
}

char *
xstrdup(const char *s)
{
	return xstrndup(s, s ? strlen(s) : 0);
}

char *
xstrndup(const char *s, size_t n)
{
	char *p;
	if (!s)
		s = "";
	p = xmalloc(n + 1);
	memcpy(p, s, n);
	p[n] = 0;
	return p;
}

char *
strf(const char *fmt, ...)
{
	va_list ap;
	int n;
	char *s;

	va_start(ap, fmt);
	n = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (n < 0)
		die("vsnprintf");
	s = xmalloc((size_t)n + 1);
	va_start(ap, fmt);
	vsnprintf(s, (size_t)n + 1, fmt, ap);
	va_end(ap);
	return s;
}

int exists(const char *path)
{
	return path && access(path, F_OK) == 0;
}

int is_dir(const char *path)
{
	struct stat st;
	return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int is_link(const char *path)
{
	struct stat st;
	return path && lstat(path, &st) == 0 && S_ISLNK(st.st_mode);
}

int which(const char *name)
{
	char *path, *save, *dir;
	char buf[512];
	int ok = 0;

	if (!name || !*name)
		return 0;
	if (strchr(name, '/'))
		return access(name, X_OK) == 0;

	path = getenv("PATH");
	if (!path || !*path)
		path = "/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin";
	path = xstrdup(path);
	for (dir = strtok_r(path, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
		snprintf(buf, sizeof buf, "%s/%s", dir, name);
		if (access(buf, X_OK) == 0) {
			ok = 1;
			break;
		}
	}
	free(path);
	return ok;
}

int
mkdir_p(const char *path)
{
	char *tmp, *p;

	if (!path || !*path)
		return -1;
	if (is_dir(path))
		return 0;
	tmp = xstrdup(path);
	for (p = tmp + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = 0;
		if (mkdir(tmp, 0755) < 0 && errno != EEXIST) {
			free(tmp);
			return -1;
		}
		*p = '/';
	}
	if (mkdir(tmp, 0755) < 0 && errno != EEXIST) {
		free(tmp);
		return -1;
	}
	free(tmp);
	return 0;
}

int
write_file(const char *path, const char *text, int mode, int dry)
{
	char *dir, *slash;
	FILE *f;

	if (dry) {
		printf("write %s\n", path);
		return 0;
	}
	dir = xstrdup(path);
	slash = strrchr(dir, '/');
	if (slash && slash != dir) {
		*slash = 0;
		if (mkdir_p(dir) < 0) {
			free(dir);
			return -1;
		}
	}
	free(dir);
	f = fopen(path, "w");
	if (!f)
		return -1;
	if (fputs(text ? text : "", f) == EOF) {
		fclose(f);
		return -1;
	}
	if (fclose(f) != 0)
		return -1;
	if (mode >= 0)
		chmod(path, (mode_t)mode);
	return 0;
}

int
copy_file(const char *src, const char *dst, int dry)
{
	if (dry) {
		printf("copy %s -> %s\n", src, dst);
		return 0;
	}
	return cmd(0, 0, "cp", "-a", src, dst, NULL);
}

char *
slurp(const char *path)
{
	FILE *f;
	char *buf;
	size_t cap = 0, n = 0;
	size_t got;

	f = fopen(path, "r");
	if (!f)
		return NULL;
	buf = NULL;
	for (;;) {
		if (n + 4096 >= cap) {
			cap = cap ? cap * 2 : 8192;
			buf = xrealloc(buf, cap);
		}
		got = fread(buf + n, 1, cap - n - 1, f);
		n += got;
		if (got == 0)
			break;
	}
	buf[n] = 0;
	fclose(f);
	return buf;
}

void
str_trim(char *s)
{
	char *p, *e;

	if (!s)
		return;
	p = s;
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
		p++;
	if (p != s)
		memmove(s, p, strlen(p) + 1);
	e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
		*--e = 0;
}

int
str_eq(const char *a, const char *b)
{
	return strcmp(a, b) == 0;
}

int
starts_with(const char *s, const char *pfx)
{
	return strncmp(s, pfx, strlen(pfx)) == 0;
}

int
ends_with(const char *s, const char *sfx)
{
	size_t n = strlen(s), m = strlen(sfx);
	return n >= m && strcmp(s + n - m, sfx) == 0;
}

static char **
va_argv(const char *arg0, va_list ap)
{
	char **argv;
	int n = 1, cap = 16;
	const char *a;

	argv = xmalloc(sizeof(char *) * cap);
	argv[0] = (char *)arg0;
	while ((a = va_arg(ap, const char *))) {
		if (n + 1 >= cap) {
			cap *= 2;
			argv = xrealloc(argv, sizeof(char *) * cap);
		}
		argv[n++] = (char *)a;
	}
	argv[n] = NULL;
	return argv;
}

static void
showcmd(char *const argv[])
{
	int i;
	fputc('+', stdout);
	for (i = 0; argv[i]; i++)
		printf(" %s", argv[i]);
	fputc('\n', stdout);
	fflush(stdout);
}

int
cmdv(int dry, int check, char *const argv[])
{
	pid_t pid;
	int st;

	showcmd(argv);
	if (dry)
		return 0;
	pid = fork();
	if (pid < 0) {
		perror("fork");
		exit(1);
	}
	if (pid == 0) {
		execvp(argv[0], argv);
		_exit(127);
	}
	if (waitpid(pid, &st, 0) < 0)
		die("waitpid: %s", strerror(errno));
	if (WIFEXITED(st)) {
		if (check && WEXITSTATUS(st) != 0)
			die("command failed (exit %d): %s", WEXITSTATUS(st), argv[0]);
		return WEXITSTATUS(st);
	}
	if (check)
		die("command aborted: %s", argv[0]);
	return 1;
}

int
cmd(int dry, int check, const char *arg0, ...)
{
	va_list ap;
	char **argv;
	int rc;

	va_start(ap, arg0);
	argv = va_argv(arg0, ap);
	va_end(ap);
	rc = cmdv(dry, check, argv);
	free(argv);
	return rc;
}

char *
cmd_out(const char *arg0, ...)
{
	va_list ap;
	char **argv;
	int pipefd[2];
	pid_t pid;
	int st;
	char *buf = NULL;
	size_t cap = 0, n = 0;
	ssize_t got;

	va_start(ap, arg0);
	argv = va_argv(arg0, ap);
	va_end(ap);

	if (pipe(pipefd) < 0) {
		free(argv);
		return NULL;
	}
	pid = fork();
	if (pid < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		free(argv);
		return NULL;
	}
	if (pid == 0) {
		dup2(pipefd[1], 1);
		close(pipefd[0]);
		close(pipefd[1]);
		/* drop stderr so callers can treat empty/NULL as "not found" */
		dup2(open("/dev/null", O_WRONLY), 2);
		execvp(argv[0], argv);
		_exit(127);
	}
	close(pipefd[1]);
	free(argv);
	for (;;) {
		if (n + 512 >= cap) {
			cap = cap ? cap * 2 : 1024;
			buf = xrealloc(buf, cap);
		}
		got = read(pipefd[0], buf + n, cap - n - 1);
		if (got <= 0)
			break;
		n += (size_t)got;
	}
	close(pipefd[0]);
	waitpid(pid, &st, 0);
	if (buf)
		buf[n] = 0;
	if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
		free(buf);
		return NULL;
	}
	return buf;
}

void
sl_init(StrList *s)
{
	s->v = NULL;
	s->n = 0;
	s->cap = 0;
}

void
sl_push(StrList *s, const char *x)
{
	if (s->n >= s->cap) {
		s->cap = s->cap ? s->cap * 2 : 8;
		s->v = xrealloc(s->v, sizeof(char *) * s->cap);
	}
	s->v[s->n++] = xstrdup(x ? x : "");
}

void
sl_free(StrList *s)
{
	int i;
	for (i = 0; i < s->n; i++)
		free(s->v[i]);
	free(s->v);
	sl_init(s);
}

int sl_has(const StrList *s, const char *x)
{
	int i;
	if (!x)
		return 0;
	for (i = 0; i < s->n; i++)
		if (strcmp(s->v[i], x) == 0)
			return 1;
	return 0;
}

static int
cmpstr(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

void
sl_sort(StrList *s)
{
	if (s->n > 1)
		qsort(s->v, s->n, sizeof(char *), cmpstr);
}
