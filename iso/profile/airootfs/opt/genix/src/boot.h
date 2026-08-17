/* btrfs snapshots + grub menu entries for generations */
#ifndef GENIX_BOOT_H
#define GENIX_BOOT_H

#include "toml.h"
#include <stddef.h>

typedef struct {
	int enabled;
	char backend[32];
	char subvol_prefix[64];
	char default_subvol[64];
	char efi_grub_cfg[256];
	char efi_kernel[256];
	char efi_uuid[64];
	char root_spec[128];
} BootCfg;

void boot_config_from_toml(const Toml *cfg, BootCfg *bc);
int boot_config_load(BootCfg *bc);
int root_mount(char *dev, size_t dsz, char *fstype, size_t fsz);
int is_btrfs_root(void);

int boot_status(const BootCfg *bc);
int boot_sync(const BootCfg *bc);
int after_generation_saved(int gen_id, const BootCfg *bc);
int delete_snapshot(int gen_id, const BootCfg *bc);

#endif
