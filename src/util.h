/* paths + tiny helpers. keep this boring. */
#ifndef GENIX_UTIL_H
#define GENIX_UTIL_H

#include <stddef.h>
#include <sys/types.h>

#define GENIX_ETC "/etc/genix"
#define GENIX_PORTAGE "/etc/portage"
#define GENIX_GENS "/var/lib/genix/generations"
#define GENIX_CURRENT "/var/lib/genix/generations/current"
#define GENIX_RENDERED "/etc/genix/rendered"
#define GENIX_MNT "/mnt/genix-target"

void die(const char *fmt, ...);
void warnx(const char *fmt, ...);

void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
char *strf(const char *fmt, ...); /* malloc'd snprintf */

int exists(const char *path);
int is_dir(const char *path);
int is_link(const char *path);
int which(const char *name);
int mkdir_p(const char *path);
int write_file(const char *path, const char *text, int mode, int dry);
int copy_file(const char *src, const char *dst, int dry);
char *slurp(const char *path);

void str_trim(char *s);
int str_eq(const char *a, const char *b);
int starts_with(const char *s, const char *pfx);
int ends_with(const char *s, const char *sfx);

int cmd(int dry, int check, const char *arg0, ...);
int cmdv(int dry, int check, char *const argv[]);
char *cmd_out(const char *arg0, ...); /* stdout only; NULL if it failed */

typedef struct {
	char **v;
	int n, cap;
} StrList;

void sl_init(StrList *s);
void sl_push(StrList *s, const char *x);
void sl_free(StrList *s);
int sl_has(const StrList *s, const char *x);
void sl_sort(StrList *s);

#endif
