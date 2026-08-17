/* configuration.toml -> make.conf / package.world / os-release */
#include "render.h"
#include "toml.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void
manifest_init(Manifest *m)
{
	memset(m, 0, sizeof(*m));
	sl_init(&m->packages);
	sl_init(&m->provided);
	sl_init(&m->services);
}

void
manifest_free(Manifest *m)
{
	int i;

	free(m->use);
	sl_free(&m->packages);
	sl_free(&m->provided);
	sl_free(&m->services);
	for (i = 0; i < m->nbin; i++)
		free(m->bin_keys[i]);
	free(m->bin_keys);
	free(m->bin_vals);
	for (i = 0; i < m->nnodeps; i++)
		free(m->nodeps_keys[i]);
	free(m->nodeps_keys);
	free(m->nodeps_vals);
	free(m->binhost);
	free(m->hostname);
	free(m->os_id);
	free(m->os_name);
	free(m->timestamp);
	memset(m, 0, sizeof(*m));
}

static int
map_get(char **keys, int *vals, int n, const char *k, int *found)
{
	int i;
	for (i = 0; i < n; i++) {
		if (str_eq(keys[i], k)) {
			*found = 1;
			return vals[i];
		}
	}
	*found = 0;
	return 0;
}

int
manifest_bin(const Manifest *m, const char *pkg)
{
	int found, v;
	v = map_get(m->bin_keys, m->bin_vals, m->nbin, pkg, &found);
	return found ? v : m->binary_default;
}

int
manifest_nodeps(const Manifest *m, const char *pkg)
{
	int found, v;
	v = map_get(m->nodeps_keys, m->nodeps_vals, m->nnodeps, pkg, &found);
	if (found)
		return v;
	return m->lfs_mode;
}

static void
map_set(char ***keys, int **vals, int *n, const char *k, int v)
{
	int i, found;
	map_get(*keys, *vals, *n, k, &found);
	if (found) {
		for (i = 0; i < *n; i++) {
			if (str_eq((*keys)[i], k)) {
				(*vals)[i] = v;
				return;
			}
		}
	}
	*keys = xrealloc(*keys, sizeof(char *) * (*n + 1));
	*vals = xrealloc(*vals, sizeof(int) * (*n + 1));
	(*keys)[*n] = xstrdup(k);
	(*vals)[*n] = v;
	(*n)++;
}

static void
note_pkg(Manifest *m, const char *name, int *use_bin, int *use_nodeps)
{
	if (!name || !*name)
		die("empty package name");
	if (!sl_has(&m->packages, name))
		sl_push(&m->packages, name);
	if (use_bin)
		map_set(&m->bin_keys, &m->bin_vals, &m->nbin, name, *use_bin);
	if (use_nodeps)
		map_set(&m->nodeps_keys, &m->nodeps_vals, &m->nnodeps, name, *use_nodeps);
}

/* packages.want can say "bin>sys-apps/foo" or "src:sys-apps/foo" */
static char *
strip_prefix(char *s, int *use_bin)
{
	static const char *tags[] = { "bin>", "bin:", "src>", "src:", NULL };
	static const int flags[] = { 1, 1, 0, 0 };
	int i;

	for (i = 0; tags[i]; i++) {
		if (starts_with(s, tags[i])) {
			s += strlen(tags[i]);
			while (*s == ' ' || *s == '\t')
				s++;
			*use_bin = flags[i];
			return s;
		}
	}
	return s;
}

static void
want_packages(const Toml *cfg, Manifest *m)
{
	Toml *want, *bin, *nodeps;
	int i;

	want = toml_get(cfg, "packages.want");
	if (want && want->type == TOML_ARRAY) {
		for (i = 0; i < want->n; i++) {
			Toml *e = want->vals[i];
			if (e->type == TOML_STR) {
				char *s = xstrdup(e->str);
				int binflag = -1;
				char *name = strip_prefix(s, &binflag);
				if (!*name)
					die("missing package name after prefix");
				if (binflag >= 0)
					note_pkg(m, name, &binflag, NULL);
				else
					note_pkg(m, name, NULL, NULL);
				free(s);
			} else if (e->type == TOML_TABLE) {
				const char *pkg = toml_str(e, "name", NULL);
				Toml *b, *nd;
				int bv, nv, *pbin = NULL, *pnd = NULL;
				if (!pkg)
					pkg = toml_str(e, "atom", NULL);
				if (!pkg)
					die("package entry needs a name");
				b = toml_get(e, "binary");
				nd = toml_get(e, "nodeps");
				if (b && b->type == TOML_BOOL) {
					bv = b->boolean;
					pbin = &bv;
				}
				if (nd && nd->type == TOML_BOOL) {
					nv = nd->boolean;
					pnd = &nv;
				}
				note_pkg(m, pkg, pbin, pnd);
			} else {
				die("packages.want entries must be strings or tables");
			}
		}
	} else if (want && want->type == TOML_TABLE) {
		for (i = 0; i < want->n; i++) {
			Toml *opts = want->vals[i];
			int bv, nv, *pbin = NULL, *pnd = NULL;
			if (opts && opts->type == TOML_TABLE) {
				Toml *b = toml_get(opts, "binary");
				Toml *nd = toml_get(opts, "nodeps");
				if (b && b->type == TOML_BOOL) {
					bv = b->boolean;
					pbin = &bv;
				}
				if (nd && nd->type == TOML_BOOL) {
					nv = nd->boolean;
					pnd = &nv;
				}
			}
			note_pkg(m, want->keys[i], pbin, pnd);
		}
	} else if (want) {
		die("packages.want must be a list or table");
	}

	bin = toml_get(cfg, "packages.binary");
	if (bin && bin->type == TOML_TABLE) {
		for (i = 0; i < bin->n; i++) {
			int found;
			map_get(m->bin_keys, m->bin_vals, m->nbin, bin->keys[i], &found);
			if (!found && bin->vals[i]->type == TOML_BOOL)
				map_set(&m->bin_keys, &m->bin_vals, &m->nbin, bin->keys[i], bin->vals[i]->boolean);
		}
	}
	nodeps = toml_get(cfg, "packages.nodeps");
	if (nodeps && nodeps->type == TOML_TABLE) {
		for (i = 0; i < nodeps->n; i++) {
			int found;
			map_get(m->nodeps_keys, m->nodeps_vals, m->nnodeps, nodeps->keys[i], &found);
			if (!found && nodeps->vals[i]->type == TOML_BOOL)
				map_set(&m->nodeps_keys, &m->nodeps_vals, &m->nnodeps, nodeps->keys[i], nodeps->vals[i]->boolean);
		}
	}
	sl_sort(&m->packages);
}

static void
provided_packages(const Toml *cfg, Manifest *m)
{
	Toml *block = toml_get(cfg, "system.provided");
	Toml *arr = NULL;
	int i;

	if (!block)
		return;
	if (block->type == TOML_ARRAY)
		arr = block;
	else if (block->type == TOML_TABLE)
		arr = toml_get(block, "packages");
	else
		die("system.provided must be a list or table");
	if (!arr || arr->type != TOML_ARRAY)
		return;
	for (i = 0; i < arr->n; i++) {
		if (arr->vals[i]->type == TOML_STR && arr->vals[i]->str[0])
			sl_push(&m->provided, arr->vals[i]->str);
	}
	sl_sort(&m->provided);
}

static char *
join_use(const Toml *use)
{
	int i;
	size_t n = 0;
	char *s, *p;

	if (!use || use->type != TOML_ARRAY)
		return xstrdup("");
	for (i = 0; i < use->n; i++) {
		if (use->vals[i]->type == TOML_STR)
			n += strlen(use->vals[i]->str) + 1;
	}
	s = xmalloc(n + 1);
	p = s;
	for (i = 0; i < use->n; i++) {
		if (use->vals[i]->type != TOML_STR)
			continue;
		if (p != s)
			*p++ = ' ';
		strcpy(p, use->vals[i]->str);
		p += strlen(use->vals[i]->str);
	}
	*p = 0;
	return s;
}

static void
append_line(char **buf, size_t *n, size_t *cap, const char *line)
{
	size_t ln = strlen(line);
	if (*n + ln + 2 >= *cap) {
		*cap = (*cap + ln + 256) * 2;
		*buf = xrealloc(*buf, *cap);
	}
	memcpy(*buf + *n, line, ln);
	*n += ln;
	(*buf)[(*n)++] = '\n';
	(*buf)[*n] = 0;
}

int
render_config(const char *config_path, const char *output_dir, Manifest *out)
{
	char *err = NULL;
	Toml *cfg, *use, *svc, *overrides, *vars;
	char *mc = NULL, *path;
	size_t n = 0, cap = 0;
	int i;
	const char *os_name, *os_id, *ver, *pretty;
	char *osrel;

	cfg = toml_parse_file(config_path, &err);
	if (!cfg)
		die("%s", err ? err : "bad configuration.toml");

	manifest_init(out);
	use = toml_get(cfg, "system.use");
	out->use = join_use(use);
	out->binary_default = toml_bool(cfg, "system.portage.binary", 0);
	out->lfs_mode = toml_bool(cfg, "system.portage.lfs", 0);
	if (toml_str(cfg, "system.portage.binhost", NULL))
		out->binhost = xstrdup(toml_str(cfg, "system.portage.binhost", ""));

	want_packages(cfg, out);
	provided_packages(cfg, out);

	if (out->use && out->use[0]) {
		char *line = strf("USE=\"%s\"", out->use);
		append_line(&mc, &n, &cap, line);
		free(line);
	}
	{
		const char *ak = toml_str(cfg, "system.portage.accept_keywords", NULL);
		const char *mo = toml_str(cfg, "system.portage.makeopts", NULL);
		const char *feat = toml_str(cfg, "system.portage.features", NULL);
		const char *eopts = toml_str(cfg, "system.portage.emerge_default_opts", NULL);
		char *features = xstrdup(feat ? feat : "");
		char *emerge_opts = xstrdup(eopts ? eopts : "");
		char *line;

		if (ak) {
			line = strf("ACCEPT_KEYWORDS=\"%s\"", ak);
			append_line(&mc, &n, &cap, line);
			free(line);
		}
		if (mo) {
			line = strf("MAKEOPTS=\"%s\"", mo);
			append_line(&mc, &n, &cap, line);
			free(line);
		}
		if (out->binary_default && !strstr(features, "getbinpkg")) {
			char *nf = features[0] ? strf("%s getbinpkg", features) : xstrdup("getbinpkg");
			free(features);
			features = nf;
		}
		if (features[0]) {
			line = strf("FEATURES=\"%s\"", features);
			append_line(&mc, &n, &cap, line);
			free(line);
		}
		if (out->binhost) {
			line = strf("PORTAGE_BINHOST=\"%s\"", out->binhost);
			append_line(&mc, &n, &cap, line);
			free(line);
		}
		if (out->binary_default && !strstr(emerge_opts, "--getbinpkg")) {
			char *nf = emerge_opts[0] ? strf("--getbinpkg %s", emerge_opts) : xstrdup("--getbinpkg");
			free(emerge_opts);
			emerge_opts = nf;
		}
		if (emerge_opts[0]) {
			line = strf("EMERGE_DEFAULT_OPTS=\"%s\"", emerge_opts);
			append_line(&mc, &n, &cap, line);
			free(line);
		}
		free(features);
		free(emerge_opts);
	}

	vars = toml_get(cfg, "system.portage.vars");
	if (vars && vars->type == TOML_TABLE) {
		for (i = 0; i < vars->n; i++) {
			const char *key = vars->keys[i];
			Toml *v = vars->vals[i];
			char *line;
			if (!key[0] || !(isalpha((unsigned char)key[0]) || key[0] == '_'))
				die("invalid make.conf variable name: %s", key);
			if (v->type == TOML_STR)
				line = strf("%s=\"%s\"", key, v->str);
			else if (v->type == TOML_ARRAY) {
				char *joined = join_use(v);
				line = strf("%s=\"%s\"", key, joined);
				free(joined);
			} else if (v->type == TOML_BOOL)
				line = strf("%s=\"%s\"", key, v->boolean ? "true" : "false");
			else
				line = strf("%s=\"%ld\"", key, v->num);
			append_line(&mc, &n, &cap, line);
			free(line);
		}
	}

	path = strf("%s/portage", output_dir);
	mkdir_p(path);
	{
		char *f = strf("%s/make.conf", path);
		if (write_file(f, mc ? mc : "\n", 0644, 0) < 0)
			die("cannot write %s", f);
		free(f);
	}
	{
		char *world = xstrdup("");
		size_t wn = 0, wcap = 0;
		char *f;
		for (i = 0; i < out->packages.n; i++)
			append_line(&world, &wn, &wcap, out->packages.v[i]);
		f = strf("%s/package.world", path);
		write_file(f, world, 0644, 0);
		free(f);
		free(world);
	}

	overrides = toml_get(cfg, "packages.use");
	{
		char *f = strf("%s/package.use", path);
		if (overrides && overrides->type == TOML_TABLE && overrides->n) {
			char *body = xstrdup("");
			size_t bn = 0, bcap = 0;
			/* sort atoms so diffs aren't noisy */
			int *idx = xmalloc(sizeof(int) * overrides->n);
			for (i = 0; i < overrides->n; i++)
				idx[i] = i;
			for (i = 0; i < overrides->n; i++) {
				int j;
				for (j = i + 1; j < overrides->n; j++) {
					if (strcmp(overrides->keys[idx[j]], overrides->keys[idx[i]]) < 0) {
						int tmp = idx[i];
						idx[i] = idx[j];
						idx[j] = tmp;
					}
				}
			}
			for (i = 0; i < overrides->n; i++) {
				Toml *flags = overrides->vals[idx[i]];
				char *line;
				if (flags->type == TOML_ARRAY) {
					char *joined = join_use(flags);
					line = strf("%s %s", overrides->keys[idx[i]], joined);
					free(joined);
				} else if (flags->type == TOML_STR)
					line = strf("%s %s", overrides->keys[idx[i]], flags->str);
				else
					line = strf("%s", overrides->keys[idx[i]]);
				append_line(&body, &bn, &bcap, line);
				free(line);
			}
			write_file(f, body, 0644, 0);
			free(body);
			free(idx);
		} else if (exists(f)) {
			unlink(f);
		}
		free(f);
	}

	{
		char *f = strf("%s/package.provided", path);
		if (out->provided.n) {
			char *body = xstrdup("");
			size_t bn = 0, bcap = 0;
			for (i = 0; i < out->provided.n; i++)
				append_line(&body, &bn, &bcap, out->provided.v[i]);
			write_file(f, body, 0644, 0);
			free(body);
		} else if (exists(f)) {
			unlink(f);
		}
		free(f);
	}
	free(path);
	free(mc);

	os_name = toml_str(cfg, "system.identity.name", "Genix");
	os_id = toml_str(cfg, "system.identity.id", "genix");
	ver = toml_str(cfg, "system.identity.version", "0.1");
	pretty = toml_str(cfg, "system.identity.pretty_name", os_name);
	out->hostname = xstrdup(toml_str(cfg, "system.hostname", os_id));
	out->os_id = xstrdup(os_id);
	out->os_name = xstrdup(pretty);

	osrel = strf(
		"NAME=\"%s\"\n"
		"PRETTY_NAME=\"%s\"\n"
		"ID=%s\n"
		"ID_LIKE=%s\n"
		"VERSION=\"%s\"\n"
		"VERSION_ID=\"%s\"\n",
		os_name, pretty, os_id,
		toml_str(cfg, "system.identity.id_like", "gentoo"),
		ver, toml_str(cfg, "system.identity.version_id", ver));
	{
		const char *home = toml_str(cfg, "system.identity.home_url", "");
		const char *logo = toml_str(cfg, "system.identity.logo", "");
		const char *ansi = toml_str(cfg, "system.identity.ansi_color", "");
		const char *bugs = toml_str(cfg, "system.identity.bug_report_url", "");
		const char *support = toml_str(cfg, "system.identity.support_url", "");
		char *extra;

		if (home[0]) {
			extra = strf("%sHOME_URL=\"%s\"\n", osrel, home);
			free(osrel);
			osrel = extra;
		}
		if (logo[0]) {
			extra = strf("%sLOGO=%s\n", osrel, logo);
			free(osrel);
			osrel = extra;
		}
		if (ansi[0]) {
			extra = strf("%sANSI_COLOR=\"%s\"\n", osrel, ansi);
			free(osrel);
			osrel = extra;
		}
		if (bugs[0]) {
			extra = strf("%sBUG_REPORT_URL=\"%s\"\n", osrel, bugs);
			free(osrel);
			osrel = extra;
		}
		if (support[0]) {
			extra = strf("%sSUPPORT_URL=\"%s\"\n", osrel, support);
			free(osrel);
			osrel = extra;
		}
	}
	path = strf("%s/os-release", output_dir);
	write_file(path, osrel, 0644, 0);
	free(path);
	free(osrel);
	path = strf("%s/hostname", output_dir);
	{
		char *h = strf("%s\n", out->hostname);
		write_file(path, h, 0644, 0);
		free(h);
	}
	free(path);

	svc = toml_get(cfg, "services.enable");
	if (svc && svc->type != TOML_ARRAY)
		die("services.enable must be a list");
	if (svc) {
		for (i = 0; i < svc->n; i++)
			if (svc->vals[i]->type == TOML_STR)
				sl_push(&out->services, svc->vals[i]->str);
	}

	toml_free(cfg);
	return 0;
}

static void
json_esc(FILE *f, const char *s)
{
	fputc('"', f);
	if (!s) {
		fputc('"', f);
		return;
	}
	for (; *s; s++) {
		if (*s == '"' || *s == '\\') {
			fputc('\\', f);
			fputc(*s, f);
		} else if (*s == '\n')
			fputs("\\n", f);
		else
			fputc(*s, f);
	}
	fputc('"', f);
}

int
manifest_write_json(const Manifest *m, const char *path)
{
	FILE *f;
	int i;

	f = fopen(path, "w");
	if (!f)
		return -1;
	fprintf(f, "{\n");
	fprintf(f, "  \"generation\": %d,\n", m->generation);
	fprintf(f, "  \"timestamp\": ");
	json_esc(f, m->timestamp ? m->timestamp : "");
	fprintf(f, ",\n  \"use\": ");
	json_esc(f, m->use ? m->use : "");
	fprintf(f, ",\n  \"hostname\": ");
	json_esc(f, m->hostname ? m->hostname : "");
	fprintf(f, ",\n  \"os_id\": ");
	json_esc(f, m->os_id ? m->os_id : "");
	fprintf(f, ",\n  \"os_name\": ");
	json_esc(f, m->os_name ? m->os_name : "");
	fprintf(f, ",\n  \"binhost\": ");
	json_esc(f, m->binhost ? m->binhost : "");
	fprintf(f, ",\n  \"binary_default\": %s,\n", m->binary_default ? "true" : "false");
	fprintf(f, "  \"lfs_mode\": %s,\n", m->lfs_mode ? "true" : "false");

	fprintf(f, "  \"packages\": [");
	for (i = 0; i < m->packages.n; i++) {
		if (i)
			fputc(',', f);
		json_esc(f, m->packages.v[i]);
	}
	fprintf(f, "],\n  \"provided\": [");
	for (i = 0; i < m->provided.n; i++) {
		if (i)
			fputc(',', f);
		json_esc(f, m->provided.v[i]);
	}
	fprintf(f, "],\n  \"services\": [");
	for (i = 0; i < m->services.n; i++) {
		if (i)
			fputc(',', f);
		json_esc(f, m->services.v[i]);
	}
	fprintf(f, "],\n  \"package_binary\": {");
	for (i = 0; i < m->nbin; i++) {
		if (i)
			fputc(',', f);
		json_esc(f, m->bin_keys[i]);
		fprintf(f, ": %s", m->bin_vals[i] ? "true" : "false");
	}
	fprintf(f, "},\n  \"package_nodeps\": {");
	for (i = 0; i < m->nnodeps; i++) {
		if (i)
			fputc(',', f);
		json_esc(f, m->nodeps_keys[i]);
		fprintf(f, ": %s", m->nodeps_vals[i] ? "true" : "false");
	}
	fprintf(f, "}\n}\n");
	fclose(f);
	return 0;
}

/* only has to round-trip the json we write. not a real parser. */
typedef struct {
	const char *p;
} JP;

static void
jskip(JP *j)
{
	while (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r' || *j->p == ',')
		j->p++;
}

static int
jeat(JP *j, char c)
{
	jskip(j);
	if (*j->p == c) {
		j->p++;
		return 1;
	}
	return 0;
}

static char *
jstr(JP *j)
{
	char *out;
	size_t n = 0, cap = 32;

	jskip(j);
	if (*j->p != '"')
		return NULL;
	j->p++;
	out = xmalloc(cap);
	while (*j->p && *j->p != '"') {
		char c = *j->p++;
		if (c == '\\' && *j->p) {
			c = *j->p++;
			if (c == 'n')
				c = '\n';
		}
		if (n + 1 >= cap) {
			cap *= 2;
			out = xrealloc(out, cap);
		}
		out[n++] = c;
	}
	if (*j->p == '"')
		j->p++;
	out[n] = 0;
	return out;
}

static int
jbool(JP *j, int *out)
{
	jskip(j);
	if (starts_with(j->p, "true")) {
		j->p += 4;
		*out = 1;
		return 1;
	}
	if (starts_with(j->p, "false")) {
		j->p += 5;
		*out = 0;
		return 1;
	}
	return 0;
}

static int
jnum(JP *j, int *out)
{
	char *end;
	jskip(j);
	*out = (int)strtol(j->p, &end, 10);
	if (end == j->p)
		return 0;
	j->p = end;
	return 1;
}

static void
jarray_strings(JP *j, StrList *sl)
{
	jeat(j, '[');
	for (;;) {
		char *s;
		jskip(j);
		if (jeat(j, ']'))
			return;
		s = jstr(j);
		if (!s)
			return;
		sl_push(sl, s);
		free(s);
	}
}

static void
jmap_bool(JP *j, char ***keys, int **vals, int *n)
{
	jeat(j, '{');
	for (;;) {
		char *k;
		int v;
		jskip(j);
		if (jeat(j, '}'))
			return;
		k = jstr(j);
		if (!k)
			return;
		jeat(j, ':');
		if (!jbool(j, &v))
			v = 0;
		map_set(keys, vals, n, k, v);
		free(k);
	}
}

int
manifest_read_json(const char *path, Manifest *out)
{
	char *src;
	JP j;

	src = slurp(path);
	if (!src)
		return -1;
	manifest_init(out);
	j.p = src;
	jeat(&j, '{');
	while (*j.p && *j.p != '}') {
		char *key;
		jskip(&j);
		if (jeat(&j, '}'))
			break;
		key = jstr(&j);
		if (!key)
			break;
		jeat(&j, ':');
		if (str_eq(key, "generation"))
			jnum(&j, &out->generation);
		else if (str_eq(key, "timestamp"))
			out->timestamp = jstr(&j);
		else if (str_eq(key, "use"))
			out->use = jstr(&j);
		else if (str_eq(key, "hostname"))
			out->hostname = jstr(&j);
		else if (str_eq(key, "os_id"))
			out->os_id = jstr(&j);
		else if (str_eq(key, "os_name"))
			out->os_name = jstr(&j);
		else if (str_eq(key, "binhost"))
			out->binhost = jstr(&j);
		else if (str_eq(key, "binary_default"))
			jbool(&j, &out->binary_default);
		else if (str_eq(key, "lfs_mode"))
			jbool(&j, &out->lfs_mode);
		else if (str_eq(key, "packages"))
			jarray_strings(&j, &out->packages);
		else if (str_eq(key, "provided"))
			jarray_strings(&j, &out->provided);
		else if (str_eq(key, "services"))
			jarray_strings(&j, &out->services);
		else if (str_eq(key, "package_binary"))
			jmap_bool(&j, &out->bin_keys, &out->bin_vals, &out->nbin);
		else if (str_eq(key, "package_nodeps"))
			jmap_bool(&j, &out->nodeps_keys, &out->nodeps_vals, &out->nnodeps);
		else {
			/* old manifests might have extra keys, just skip them */
			jskip(&j);
			if (*j.p == '"')
				free(jstr(&j));
			else if (*j.p == '{') {
				int depth = 0;
				do {
					if (*j.p == '{')
						depth++;
					else if (*j.p == '}')
						depth--;
					j.p++;
				} while (depth && *j.p);
			} else if (*j.p == '[') {
				int depth = 0;
				do {
					if (*j.p == '[')
						depth++;
					else if (*j.p == ']')
						depth--;
					j.p++;
				} while (depth && *j.p);
			} else {
				while (*j.p && *j.p != ',' && *j.p != '}')
					j.p++;
			}
		}
		free(key);
	}
	free(src);
	return 0;
}

int
render_main(int argc, char **argv)
{
	const char *cfg = GENIX_ETC "/configuration.toml";
	const char *out = GENIX_RENDERED;
	Manifest m;
	int i;

	if (argc > 1)
		cfg = argv[1];
	if (argc > 2)
		out = argv[2];
	if (!exists(cfg)) {
		fprintf(stderr, "genix-render: no config at %s\n", cfg);
		return 1;
	}
	render_config(cfg, out, &m);
	printf("rendered %s/portage\n", out);
	printf("use: %s\n", m.use && m.use[0] ? m.use : "-");
	if (m.provided.n)
		printf("provided: %d packages\n", m.provided.n);
	printf("os: %s (%s)\n", m.os_name, m.os_id);
	printf("host: %s\n", m.hostname);
	for (i = 0; i < m.packages.n; i++)
		printf("  %s (%s)\n", m.packages.v[i], manifest_bin(&m, m.packages.v[i]) ? "bin" : "src");
	manifest_free(&m);
	return 0;
}
