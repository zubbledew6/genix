/* toml -> /etc/portage bits + a json snapshot of what we did */
#ifndef GENIX_RENDER_H
#define GENIX_RENDER_H

#include "util.h"

typedef struct {
	char *use;
	StrList packages;
	StrList provided;
	StrList services;
	char **bin_keys;
	int *bin_vals;
	int nbin;
	char **nodeps_keys;
	int *nodeps_vals;
	int nnodeps;
	int binary_default;
	int lfs_mode;
	char *binhost;
	char *hostname;
	char *os_id;
	char *os_name;
	int generation;
	char *timestamp;
} Manifest;

void manifest_init(Manifest *m);
void manifest_free(Manifest *m);
int manifest_bin(const Manifest *m, const char *pkg);
int manifest_nodeps(const Manifest *m, const char *pkg);

int render_config(const char *config_path, const char *output_dir, Manifest *out);
int manifest_write_json(const Manifest *m, const char *path);
int manifest_read_json(const char *path, Manifest *out);
int render_main(int argc, char **argv);

#endif
