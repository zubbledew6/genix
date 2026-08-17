/* GRUB entries point at @genix-N snapshots. subvolid=5 is the tree
   root — @ is just a subvol, mounting / doesn't let us snapshot it. */
#include "boot.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define GENS GENIX_GENS
#define GRUB_SNIPPET "/etc/grub.d/40_genix"
#define GRUB_CFG "/boot/grub/grub.cfg"
#define EFI_GRUB_CFG "/boot/efi/grub/grub.cfg"
#define BTRFS_TOP "/run/genix-btrfs-top"

static const char *
btrfs_bin(void)
{
	if (which("btrfs"))
		return "btrfs";
	if (exists("/usr/sbin/btrfs"))
		return "/usr/sbin/btrfs";
	if (exists("/sbin/btrfs"))
		return "/sbin/btrfs";
	if (exists("/usr/bin/btrfs"))
		return "/usr/bin/btrfs";
	return NULL;
}

int
root_mount(char *dev, size_t dsz, char *fstype, size_t fsz)
{
	FILE *f;
	char line[512], d[256], m[256], t[64];

	if (dev)
		dev[0] = 0;
	if (fstype)
		fstype[0] = 0;
	f = fopen("/proc/mounts", "r");
	if (!f)
		return 0;
	while (fgets(line, sizeof line, f)) {
		if (sscanf(line, "%255s %255s %63s", d, m, t) < 3)
			continue;
		if (strcmp(m, "/") == 0) {
			if (dev)
				snprintf(dev, dsz, "%s", d);
			if (fstype)
				snprintf(fstype, fsz, "%s", t);
			fclose(f);
			return 1;
		}
	}
	fclose(f);
	return 0;
}

int
is_btrfs_root(void)
{
	char fs[64];
	root_mount(NULL, 0, fs, sizeof fs);
	return strcmp(fs, "btrfs") == 0;
}

void
boot_config_from_toml(const Toml *cfg, BootCfg *bc)
{
	const char *s;

	memset(bc, 0, sizeof(*bc));
	snprintf(bc->backend, sizeof bc->backend, "grub");
	snprintf(bc->subvol_prefix, sizeof bc->subvol_prefix, "@genix-");
	snprintf(bc->default_subvol, sizeof bc->default_subvol, "@");
	if (!cfg)
		return;
	bc->enabled = toml_bool(cfg, "system.boot.enabled", 0);
	s = toml_str(cfg, "system.boot.backend", NULL);
	if (s)
		snprintf(bc->backend, sizeof bc->backend, "%s", s);
	s = toml_str(cfg, "system.boot.subvol_prefix", NULL);
	if (s)
		snprintf(bc->subvol_prefix, sizeof bc->subvol_prefix, "%s", s);
	s = toml_str(cfg, "system.boot.default_subvol", NULL);
	if (s)
		snprintf(bc->default_subvol, sizeof bc->default_subvol, "%s", s);
	s = toml_str(cfg, "system.boot.efi_grub_cfg", NULL);
	if (s)
		snprintf(bc->efi_grub_cfg, sizeof bc->efi_grub_cfg, "%s", s);
	s = toml_str(cfg, "system.boot.efi_kernel", NULL);
	if (s)
		snprintf(bc->efi_kernel, sizeof bc->efi_kernel, "%s", s);
	s = toml_str(cfg, "system.boot.efi_uuid", NULL);
	if (s)
		snprintf(bc->efi_uuid, sizeof bc->efi_uuid, "%s", s);
	s = toml_str(cfg, "system.boot.root", NULL);
	if (s)
		snprintf(bc->root_spec, sizeof bc->root_spec, "%s", s);
}

int
boot_config_load(BootCfg *bc)
{
	char *err = NULL;
	Toml *t = toml_parse_file(GENIX_ETC "/configuration.toml", &err);
	free(err);
	boot_config_from_toml(t, bc);
	toml_free(t);
	return 0;
}

static void
subvol_name(const char *prefix, int gen_id, char *out, size_t n)
{
	char p[64];
	size_t len;

	snprintf(p, sizeof p, "%s", prefix);
	len = strlen(p);
	while (len && p[len - 1] == '-')
		p[--len] = 0;
	if (p[0] == '@')
		snprintf(out, n, "%s-%d", p, gen_id);
	else
		snprintf(out, n, "@%s-%d", p, gen_id);
}

static int
list_gen_ids(int *ids, int max)
{
	FILE *p; /* lazy: ls instead of another opendir loop */
	char line[128];
	int n = 0, id, i, j;

	p = popen("ls -1 " GENS " 2>/dev/null", "r");
	if (!p)
		return 0;
	while (fgets(line, sizeof line, p) && n < max) {
		str_trim(line);
		if (line[0] && strspn(line, "0123456789") == strlen(line)) {
			id = atoi(line);
			ids[n++] = id;
		}
	}
	pclose(p);
	for (i = 0; i < n; i++) {
		for (j = i + 1; j < n; j++) {
			if (ids[j] < ids[i]) {
				int t = ids[i];
				ids[i] = ids[j];
				ids[j] = t;
			}
		}
	}
	return n;
}

static int
btrfs_subvol_exists(const char *name)
{
	const char *btrfs = btrfs_bin();
	char *out;

	if (!btrfs)
		return 0;
	out = cmd_out(btrfs, "subvolume", "list", "/", NULL);
	if (!out)
		return 0;
	{
		int ok = strstr(out, name) != NULL;
		free(out);
		return ok;
	}
}

static int
mount_btrfs_top(void)
{
	char dev[256];

	if (!root_mount(dev, sizeof dev, NULL, 0) || !dev[0])
		return 0;
	mkdir_p(BTRFS_TOP);
	if (cmd(0, 0, "mountpoint", "-q", BTRFS_TOP, NULL) == 0)
		return 1;
	return cmd(0, 0, "mount", "-o", "subvolid=5", dev, BTRFS_TOP, NULL) == 0; /* FS_TREE */
}

static void
unmount_btrfs_top(void)
{
	if (cmd(0, 0, "mountpoint", "-q", BTRFS_TOP, NULL) == 0)
		cmd(0, 0, "umount", BTRFS_TOP, NULL);
}

static int
snapshot_generation(int gen_id, const BootCfg *bc)
{
	char name[128], src[256], dst[256];
	const char *btrfs = btrfs_bin();
	char active[64];
	int rc;

	subvol_name(bc->subvol_prefix, gen_id, name, sizeof name);
	if (!btrfs) {
		printf("boot: btrfs tools missing — install btrfs-progs\n");
		return 0;
	}
	if (btrfs_subvol_exists(name)) {
		printf("boot: snapshot %s already exists\n", name);
		return 1;
	}
	if (!mount_btrfs_top()) {
		printf("boot: could not mount btrfs tree root\n");
		return 0;
	}
	snprintf(active, sizeof active, "%s", bc->default_subvol[0] ? bc->default_subvol : "@");
	if (active[0] == '/')
		memmove(active, active + 1, strlen(active));
	while (active[0] && active[strlen(active) - 1] == '/')
		active[strlen(active) - 1] = 0;
	snprintf(src, sizeof src, "%s/%s", BTRFS_TOP, active);
	snprintf(dst, sizeof dst, "%s/%s", BTRFS_TOP, name);
	printf("boot: snapshot %s -> %s\n", src, dst);
	rc = cmd(0, 0, btrfs, "subvolume", "snapshot", src, dst, NULL);
	unmount_btrfs_top();
	return rc == 0;
}

int
delete_snapshot(int gen_id, const BootCfg *bc)
{
	char name[128], path[256];
	const char *btrfs;

	if (!bc->enabled || !is_btrfs_root())
		return 1;
	btrfs = btrfs_bin();
	if (!btrfs) {
		printf("boot: btrfs tools missing — skip delete %d\n", gen_id);
		return 1;
	}
	subvol_name(bc->subvol_prefix, gen_id, name, sizeof name);
	if (!btrfs_subvol_exists(name))
		return 1;
	snprintf(path, sizeof path, "/%s", name);
	printf("boot: delete subvolume %s\n", name);
	return cmd(0, 0, btrfs, "subvolume", "delete", path, NULL) == 0;
}

static char *
clean_kargs(const char *kargs)
{
	char *tmp = xstrdup(kargs ? kargs : "");
	char *save, *tok, *out = xstrdup("");
	size_t n = 0, cap = 0;

	for (tok = strtok_r(tmp, " ", &save); tok; tok = strtok_r(NULL, " ", &save)) {
		if (starts_with(tok, "rootflags=subvol="))
			continue; /* we stamp our own per-entry */
		{
			size_t ln = strlen(tok);
			if (n + ln + 2 >= cap) {
				cap = cap ? cap * 2 : 128;
				out = xrealloc(out, cap);
			}
			if (n)
				out[n++] = ' ';
			memcpy(out + n, tok, ln);
			n += ln;
			out[n] = 0;
		}
	}
	free(tmp);
	return out;
}

static int
parse_grub_linux(const char *text, char *kpath, size_t ksz, char *kargs, size_t asz, char *initrd, size_t isz)
{
	const char *p = text;
	kpath[0] = kargs[0] = initrd[0] = 0;
	while (p && *p) {
		const char *line = p;
		const char *nl = strchr(p, '\n');
		char buf[1024];
		size_t len = nl ? (size_t)(nl - p) : strlen(p);
		if (len >= sizeof buf)
			len = sizeof buf - 1;
		memcpy(buf, line, len);
		buf[len] = 0;
		str_trim(buf);
		if (starts_with(buf, "linux ") || starts_with(buf, "linux\t")) {
			char *rest = buf + 5;
			char *sp;
			while (*rest == ' ' || *rest == '\t')
				rest++;
			sp = strchr(rest, ' ');
			if (sp) {
				*sp = 0;
				snprintf(kpath, ksz, "%s", rest);
				snprintf(kargs, asz, "%s", sp + 1);
			} else {
				snprintf(kpath, ksz, "%s", rest);
			}
		} else if (starts_with(buf, "initrd ") || starts_with(buf, "initrd\t")) {
			char *rest = buf + 6;
			while (*rest == ' ' || *rest == '\t')
				rest++;
			snprintf(initrd, isz, "%s", rest);
		}
		p = nl ? nl + 1 : NULL;
	}
	return kpath[0] != 0;
}

static const char *
efi_grub_path(const BootCfg *bc)
{
	if (bc->efi_grub_cfg[0])
		return bc->efi_grub_cfg;
	if (exists(EFI_GRUB_CFG))
		return EFI_GRUB_CFG;
	return NULL;
}

static char *
efi_uuid_from_mount(void)
{
	char *u = cmd_out("findmnt", "-no", "UUID", "/boot/efi", NULL);
	if (u)
		str_trim(u);
	return u;
}

static void
write_grub_snippet(int *ids, int n, const BootCfg *bc)
{
	char kpath[256], kargs[1024], initrd[256];
	char *text, *base;
	char body[8192];
	size_t off = 0;
	int i;

	text = slurp(GRUB_CFG);
	kpath[0] = kargs[0] = initrd[0] = 0;
	if (text)
		parse_grub_linux(text, kpath, sizeof kpath, kargs, sizeof kargs, initrd, sizeof initrd);
	free(text);
	base = clean_kargs(kargs);

	off += (size_t)snprintf(body + off, sizeof body - off,
		"#!/bin/sh\nset -e\n\n# Genix boot generations — genix-rebuild boot sync\n\ncat << EOF\n");
	if (kpath[0]) {
		off += (size_t)snprintf(body + off, sizeof body - off,
			"menuentry 'Genix current (%s)' {\n  linux %s %s rootflags=subvol=%s\n%s%s%s}\n\n",
			bc->default_subvol, kpath, base, bc->default_subvol,
			initrd[0] ? "  initrd " : "", initrd[0] ? initrd : "", initrd[0] ? "\n" : "");
		for (i = n - 1; i >= 0; i--) {
			char sv[128];
			subvol_name(bc->subvol_prefix, ids[i], sv, sizeof sv);
			off += (size_t)snprintf(body + off, sizeof body - off,
				"menuentry 'Genix generation %d (%s)' {\n  linux %s %s rootflags=subvol=%s\n%s%s%s}\n\n",
				ids[i], sv, kpath, base, sv,
				initrd[0] ? "  initrd " : "", initrd[0] ? initrd : "", initrd[0] ? "\n" : "");
			if (off + 200 >= sizeof body)
				break;
		}
	} else {
		off += (size_t)snprintf(body + off, sizeof body - off, "# could not parse /boot/grub/grub.cfg\n");
	}
	snprintf(body + off, sizeof body - off, "EOF\n");
	write_file(GRUB_SNIPPET, body, 0755, 0);
	printf("boot: wrote %s\n", GRUB_SNIPPET);
	free(base);
}

static int
run_grub_mkconfig(void)
{
	if (!which("grub-mkconfig")) {
		printf("boot: grub-mkconfig not found\n");
		return 0;
	}
	printf("boot: grub-mkconfig -o /boot/grub/grub.cfg\n");
	return cmd(0, 0, "grub-mkconfig", "-o", "/boot/grub/grub.cfg", NULL) == 0;
}

static int
write_efi_grub_cfg(int *ids, int n, const BootCfg *bc)
{
	const char *path = efi_grub_path(bc);
	char kpath[256], kargs[1024], initrd[256], title[128];
	char *text, *uuid, *base;
	char body[16384];
	size_t off = 0;
	int i;

	if (!path)
		return 1;
	kpath[0] = kargs[0] = initrd[0] = 0;
	snprintf(title, sizeof title, "Linux");
	text = slurp(path);
	if (text)
		parse_grub_linux(text, kpath, sizeof kpath, kargs, sizeof kargs, initrd, sizeof initrd);
	free(text);
	if (bc->efi_kernel[0])
		snprintf(kpath, sizeof kpath, "%s", bc->efi_kernel);
	if (!kpath[0])
		snprintf(kpath, sizeof kpath, "/vmlinuz-live"); /* installer ISO leftover */
	if (!kargs[0]) {
		char dev[256];
		root_mount(dev, sizeof dev, NULL, 0);
		snprintf(kargs, sizeof kargs, "root=%s rootfstype=btrfs rw nomodeset console=tty1 loglevel=4",
			bc->root_spec[0] ? bc->root_spec : (dev[0] ? dev : "/dev/root"));
	}
	uuid = bc->efi_uuid[0] ? xstrdup(bc->efi_uuid) : efi_uuid_from_mount();
	if (!uuid || !uuid[0]) {
		printf("boot: EFI UUID unknown — set system.boot.efi_uuid in configuration.toml\n");
		free(uuid);
		return 0;
	}
	base = clean_kargs(kargs);
	off += (size_t)snprintf(body + off, sizeof body - off, "set timeout=15\nset default=0\n\n");

	off += (size_t)snprintf(body + off, sizeof body - off,
		"menuentry 'Genix current (%s)' {\n"
		"  insmod part_gpt\n  insmod fat\n  insmod linux\n"
		"  search.fs_uuid %s root\n"
		"  linux %s %s rootflags=subvol=%s\n",
		bc->default_subvol, uuid, kpath, base, bc->default_subvol);
	if (initrd[0])
		off += (size_t)snprintf(body + off, sizeof body - off, "  initrd %s\n", initrd);
	off += (size_t)snprintf(body + off, sizeof body - off, "}\n\n");

	for (i = n - 1; i >= 0; i--) {
		char sv[128];
		subvol_name(bc->subvol_prefix, ids[i], sv, sizeof sv);
		off += (size_t)snprintf(body + off, sizeof body - off,
			"menuentry 'Genix generation %d (%s)' {\n"
			"  insmod part_gpt\n  insmod fat\n  insmod linux\n"
			"  search.fs_uuid %s root\n"
			"  linux %s %s rootflags=subvol=%s\n",
			ids[i], sv, uuid, kpath, base, sv);
		if (initrd[0])
			off += (size_t)snprintf(body + off, sizeof body - off, "  initrd %s\n", initrd);
		off += (size_t)snprintf(body + off, sizeof body - off, "}\n\n");
		if (off + 400 >= sizeof body)
			break;
	}
	write_file(path, body, 0644, 0);
	printf("boot: wrote %s\n", path);
	free(uuid);
	free(base);
	return 1;
}

int
boot_status(const BootCfg *bc)
{
	char dev[256], fs[64];
	int ids[256], n, i;
	const char *efi;

	root_mount(dev, sizeof dev, fs, sizeof fs);
	printf("boot status\n");
	printf("  root: %s %s\n", dev[0] ? dev : "?", fs[0] ? fs : "?");
	printf("  btrfs: %s\n", is_btrfs_root() ? "yes" : "no");
	printf("  enabled in config: %s\n", bc->enabled ? "true" : "false");
	printf("  backend: %s\n", bc->backend);
	efi = efi_grub_path(bc);
	printf("  efi grub: %s\n", efi ? efi : "none");
	if (!is_btrfs_root()) {
		printf("  note: boot generations need btrfs root\n");
		return 0;
	}
	if (!bc->enabled) {
		printf("  note: set [system.boot] enabled = true after migration\n");
		return 0;
	}
	n = list_gen_ids(ids, 256);
	for (i = 0; i < n; i++) {
		char sv[128];
		subvol_name(bc->subvol_prefix, ids[i], sv, sizeof sv);
		printf("  gen %3d  %s  (%s)\n", ids[i], sv, btrfs_subvol_exists(sv) ? "snap" : "no snap");
	}
	return 0;
}

int
boot_sync(const BootCfg *bc)
{
	int ids[256], n;

	if (!is_btrfs_root()) {
		printf("boot sync: root is not btrfs\n");
		return 1;
	}
	if (!str_eq(bc->backend, "grub")) {
		printf("boot sync: only grub backend implemented so far\n");
		return 1;
	}
	n = list_gen_ids(ids, 256);
	write_grub_snippet(ids, n, bc);
	{
		int mk = run_grub_mkconfig();
		if (efi_grub_path(bc))
			return write_efi_grub_cfg(ids, n, bc) ? 0 : 1;
		return mk ? 0 : 1;
	}
}

int
after_generation_saved(int gen_id, const BootCfg *bc)
{
	if (!bc->enabled || !is_btrfs_root())
		return 0;
	if (!str_eq(bc->backend, "grub"))
		return 0;
	if (!snapshot_generation(gen_id, bc))
		return 1;
	return boot_sync(bc);
}
