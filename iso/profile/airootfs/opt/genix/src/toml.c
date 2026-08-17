/* Enough TOML for our config. No dates, no fancy types. If it
   dies on your file, it's probably quotes or a trailing comma. */
#include "toml.h"
#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Toml *
new_val(TomlType type)
{
	Toml *t = xmalloc(sizeof(*t));
	memset(t, 0, sizeof(*t));
	t->type = type;
	return t;
}

void
toml_free(Toml *t)
{
	int i;

	if (!t)
		return;
	free(t->str);
	for (i = 0; i < t->n; i++) {
		free(t->keys[i]);
		toml_free(t->vals[i]);
	}
	free(t->keys);
	free(t->vals);
	free(t);
}

static Toml *
table_get(const Toml *t, const char *key)
{
	int i;

	if (!t || t->type != TOML_TABLE)
		return NULL;
	for (i = 0; i < t->n; i++)
		if (strcmp(t->keys[i], key) == 0)
			return t->vals[i];
	return NULL;
}

static Toml *
table_ensure(Toml *t, const char *key, TomlType type)
{
	Toml *v = table_get(t, key);

	if (v)
		return v;
	if (t->n % 8 == 0) {
		t->keys = xrealloc(t->keys, sizeof(char *) * (t->n + 8));
		t->vals = xrealloc(t->vals, sizeof(Toml *) * (t->n + 8));
	}
	t->keys[t->n] = xstrdup(key);
	t->vals[t->n] = new_val(type);
	return t->vals[t->n++];
}

static void
table_set(Toml *t, const char *key, Toml *v)
{
	int i;

	for (i = 0; i < t->n; i++) {
		if (!strcmp(t->keys[i], key)) {
			toml_free(t->vals[i]);
			t->vals[i] = v;
			return;
		}
	}
	if (t->n % 8 == 0) {
		t->keys = xrealloc(t->keys, sizeof(char *) * (t->n + 8));
		t->vals = xrealloc(t->vals, sizeof(Toml *) * (t->n + 8));
	}
	t->keys[t->n] = xstrdup(key);
	t->vals[t->n] = v;
	t->n++;
}

typedef struct {
	const char *s;
	const char *p;
	char *err;
} P;

static void
seterr(P *p, const char *msg)
{
	if (!p->err)
		p->err = xstrdup(msg);
}

static void
skip(P *p)
{
	for (;;) {
		while (*p->p == ' ' || *p->p == '\t' || *p->p == '\r')
			p->p++;
		if (*p->p == '#') {
			while (*p->p && *p->p != '\n')
				p->p++;
			continue;
		}
		break;
	}
}

static int
peek(P *p)
{
	skip(p);
	return (unsigned char)*p->p;
}

static int
eat(P *p, char c)
{
	if (peek(p) == c) {
		p->p++;
		return 1;
	}
	return 0;
}

static char *
parse_bare(P *p)
{
	const char *start;

	skip(p);
	start = p->p;
	if (!isalpha((unsigned char)*p->p) && *p->p != '_')
		return NULL;
	p->p++;
	while (isalnum((unsigned char)*p->p) || *p->p == '_' || *p->p == '-' || *p->p == '/')
		p->p++;
	return xstrndup(start, (size_t)(p->p - start));
}

static char *
parse_string(P *p)
{
	char q, *out;
	size_t n = 0, cap = 32;

	skip(p);
	q = *p->p;
	if (q != '"' && q != '\'')
		return NULL;
	p->p++;
	out = xmalloc(cap);
	while (*p->p && *p->p != q) {
		char c = *p->p++;
		if (c == '\\' && q == '"') {
			c = *p->p++;
			switch (c) {
			case 'n': c = '\n'; break;
			case 't': c = '\t'; break;
			case 'r': c = '\r'; break;
			case '\\':
			case '"':
				break;
			default:
				break;
			}
		}
		if (n + 1 >= cap) {
			cap *= 2;
			out = xrealloc(out, cap);
		}
		out[n++] = c;
	}
	if (*p->p != q) {
		free(out);
		seterr(p, "unterminated string");
		return NULL;
	}
	p->p++;
	out[n] = 0;
	return out;
}

static char *
parse_key(P *p)
{
	char *k;

	skip(p);
	if (*p->p == '"' || *p->p == '\'')
		return parse_string(p);
	k = parse_bare(p);
	if (!k)
		seterr(p, "expected key");
	return k;
}

static Toml *parse_value(P *p);

static Toml *
parse_inline_table(P *p)
{
	Toml *t = new_val(TOML_TABLE);

	eat(p, '{');
	skip(p);
	if (eat(p, '}'))
		return t;
	for (;;) {
		char *k;
		Toml *v;

		k = parse_key(p);
		if (!k || !eat(p, '=')) {
			seterr(p, "bad inline table");
			free(k);
			toml_free(t);
			return NULL;
		}
		v = parse_value(p);
		if (!v) {
			free(k);
			toml_free(t);
			return NULL;
		}
		table_set(t, k, v);
		free(k);
		skip(p);
		if (eat(p, '}'))
			return t;
		if (!eat(p, ',')) {
			seterr(p, "expected , or } in inline table");
			toml_free(t);
			return NULL;
		}
	}
}

static Toml *
parse_array(P *p)
{
	Toml *t = new_val(TOML_ARRAY);

	eat(p, '[');
	for (;;) {
		skip(p);
		if (eat(p, ']'))
			return t;
		if (eat(p, '\n'))
			continue;
		if (t->n % 8 == 0) {
			t->keys = xrealloc(t->keys, sizeof(char *) * (t->n + 8));
			t->vals = xrealloc(t->vals, sizeof(Toml *) * (t->n + 8));
		}
		t->keys[t->n] = NULL;
		t->vals[t->n] = parse_value(p);
		if (!t->vals[t->n]) {
			toml_free(t);
			return NULL;
		}
		t->n++;
		skip(p);
		eat(p, ',');
	}
}

static Toml *
parse_value(P *p)
{
	int c = peek(p);
	Toml *t;

	if (c == '"' || c == '\'') {
		t = new_val(TOML_STR);
		t->str = parse_string(p);
		if (!t->str) {
			toml_free(t);
			return NULL;
		}
		return t;
	}
	if (c == '[')
		return parse_array(p);
	if (c == '{')
		return parse_inline_table(p);
	if (starts_with(p->p, "true") && !isalnum((unsigned char)p->p[4])) {
		p->p += 4;
		t = new_val(TOML_BOOL);
		t->boolean = 1;
		return t;
	}
	if (starts_with(p->p, "false") && !isalnum((unsigned char)p->p[5])) {
		p->p += 5;
		t = new_val(TOML_BOOL);
		t->boolean = 0;
		return t;
	}
	if (c == '-' || c == '+' || isdigit(c)) {
		char *end;
		t = new_val(TOML_INT);
		t->num = strtol(p->p, &end, 10);
		p->p = end;
		return t;
	}
	seterr(p, "expected value");
	return NULL;
}

static int
split_dotted(char *key, char ***out, int *n)
{
	char *save, *tok;
	int cap = 4;

	*out = xmalloc(sizeof(char *) * cap);
	*n = 0;
	for (tok = strtok_r(key, ".", &save); tok; tok = strtok_r(NULL, ".", &save)) {
		if (*n >= cap) {
			cap *= 2;
			*out = xrealloc(*out, sizeof(char *) * cap);
		}
		(*out)[(*n)++] = tok;
	}
	return *n > 0;
}

static Toml *
parse_doc(const char *src, char **err)
{
	P p = { src, src, NULL };
	Toml *root = new_val(TOML_TABLE);
	Toml *cur = root;

	while (*p.p) {
		int c;

		skip(&p);
		if (*p.p == '\n') {
			p.p++;
			continue;
		}
		if (!*p.p)
			break;
		c = peek(&p);
		if (c == '[') {
			char *inside, **parts;
			int nparts, i, arr = 0;

			p.p++;
			if (eat(&p, '['))
				arr = 1;
			inside = parse_key(&p);
			if (!inside)
				goto fail;
			/* [a.b.c] — keep eating .key until the ] */
			{
				char *acc = xstrdup(inside);
				free(inside);
				while (eat(&p, '.')) {
					char *more = parse_key(&p);
					char *join;
					if (!more)
						break;
					join = strf("%s.%s", acc, more);
					free(acc);
					free(more);
					acc = join;
				}
				inside = acc;
			}
			if (arr) {
				if (!eat(&p, ']') || !eat(&p, ']')) {
					seterr(&p, "bad [[table]]");
					free(inside);
					goto fail;
				}
			} else if (!eat(&p, ']')) {
				seterr(&p, "bad [table]");
				free(inside);
				goto fail;
			}
			if (!split_dotted(inside, &parts, &nparts)) {
				free(inside);
				goto fail;
			}
			cur = root;
			for (i = 0; i < nparts; i++)
				cur = table_ensure(cur, parts[i], TOML_TABLE);
			free(parts);
			free(inside);
			continue;
		}

		{
			char *k, *acc, **parts;
			int nparts, i;
			Toml *v, *parent;

			k = parse_key(&p);
			if (!k)
				goto fail;
			acc = xstrdup(k);
			free(k);
			while (eat(&p, '.')) {
				char *more = parse_key(&p);
				char *join;
				if (!more)
					break;
				join = strf("%s.%s", acc, more);
				free(acc);
				free(more);
				acc = join;
			}
			if (!eat(&p, '=')) {
				seterr(&p, "expected =");
				free(acc);
				goto fail;
			}
			v = parse_value(&p);
			if (!v) {
				free(acc);
				goto fail;
			}
			if (!split_dotted(acc, &parts, &nparts)) {
				toml_free(v);
				free(acc);
				goto fail;
			}
			parent = cur;
			for (i = 0; i < nparts - 1; i++)
				parent = table_ensure(parent, parts[i], TOML_TABLE);
			table_set(parent, parts[nparts - 1], v);
			free(parts);
			free(acc);
		}
	}

	if (p.err)
		goto fail;
	return root;
fail:
	if (err)
		*err = p.err ? p.err : xstrdup("toml parse error");
	else
		free(p.err);
	toml_free(root);
	return NULL;
}

Toml *
toml_parse_file(const char *path, char **err)
{
	char *src;
	Toml *t;

	src = slurp(path);
	if (!src) {
		if (err)
			*err = strf("cannot read %s", path);
		return NULL;
	}
	t = parse_doc(src, err);
	free(src);
	return t;
}

Toml *
toml_get(const Toml *t, const char *dotted)
{
	char *tmp, **parts;
	int n, i;
	const Toml *cur = t;

	if (!t || !dotted)
		return NULL;
	tmp = xstrdup(dotted);
	if (!split_dotted(tmp, &parts, &n)) {
		free(tmp);
		free(parts);
		return NULL;
	}
	for (i = 0; i < n; i++) {
		cur = table_get(cur, parts[i]);
		if (!cur) {
			free(parts);
			free(tmp);
			return NULL;
		}
	}
	free(parts);
	free(tmp);
	return (Toml *)cur;
}

const char *
toml_str(const Toml *t, const char *dotted, const char *fallback)
{
	Toml *v = toml_get(t, dotted);
	if (v && v->type == TOML_STR && v->str)
		return v->str;
	return fallback;
}

int
toml_bool(const Toml *t, const char *dotted, int fallback)
{
	Toml *v = toml_get(t, dotted);
	if (v && v->type == TOML_BOOL)
		return v->boolean;
	return fallback;
}

long
toml_int(const Toml *t, const char *dotted, long fallback)
{
	Toml *v = toml_get(t, dotted);
	if (v && v->type == TOML_INT)
		return v->num;
	if (v && v->type == TOML_STR && v->str)
		return strtol(v->str, NULL, 10);
	return fallback;
}
