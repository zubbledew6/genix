/* subset parser. enough for configuration.toml, not the spec. */
#ifndef GENIX_TOML_H
#define GENIX_TOML_H

typedef enum {
	TOML_NONE,
	TOML_STR,
	TOML_BOOL,
	TOML_INT,
	TOML_ARRAY,
	TOML_TABLE
} TomlType;

typedef struct Toml Toml;

struct Toml {
	TomlType type;
	char *str;
	long num;
	int boolean;
	char **keys;
	Toml **vals;
	int n;
};

Toml *toml_parse_file(const char *path, char **err);
void toml_free(Toml *t);

Toml *toml_get(const Toml *t, const char *dotted);
const char *toml_str(const Toml *t, const char *dotted, const char *fallback);
int toml_bool(const Toml *t, const char *dotted, int fallback);
long toml_int(const Toml *t, const char *dotted, long fallback);

#endif
