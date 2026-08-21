/* switch / rollback / prune. render first, then emerge the delta. */
#include "boot.h"
#include "render.h"
#include "toml.h"
#include "util.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char *portage_files[] = {
	"make.conf", "package.use", "package.accept_keywords", "package.world", "package.provided", NULL
};

static int
next_gen(void)
{
	DIR *d;
	struct dirent *e;
	int n = 0;

	d = opendir(GENIX_GENS);
	if (!d)
		return 1;
	while ((e = readdir(d))) {
		if (e->d_name[0] && strspn(e->d_name, "0123456789") == strlen(e->d_name)) {
			int v = atoi(e->d_name);
			if (v > n)
				n = v;
		}
	}
	closedir(d);
	return n + 1;
}

static int
save_generation(Manifest *m, const char *config_path)
{
	int gid = next_gen();
	char *gdir, *p;
	time_t now = time(NULL);
	struct tm tm;
	char ts[64];

	mkdir_p(GENIX_GENS);
	gdir = strf("%s/%d", GENIX_GENS, gid);
	mkdir_p(gdir);
	gmtime_r(&now, &tm);
	strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%SZ", &tm);
	free(m->timestamp);
	m->timestamp = xstrdup(ts);
	m->generation = gid;
	p = strf("%s/manifest.json", gdir);
	manifest_write_json(m, p);
	free(p);
	p = strf("%s/configuration.toml", gdir);
	copy_file(config_path, p, 0);
	free(p);
	if (is_dir(GENIX_RENDERED "/portage")) {
		char *dst = strf("%s/portage", gdir);
		cmd(0, 0, "cp", "-a", GENIX_RENDERED "/portage", dst, NULL);
		free(dst);
	}
	{
		const char *names[] = { "os-release", "hostname", NULL };
		int i;
		for (i = 0; names[i]; i++) {
			char *src = strf("%s/%s", GENIX_RENDERED, names[i]);
			if (exists(src)) {
				char *dst = strf("%s/%s", gdir, names[i]);
				copy_file(src, dst, 0);
				free(dst);
			}
			free(src);
		}
	}
	unlink(GENIX_CURRENT);
	if (symlink(gdir, GENIX_CURRENT) < 0)
		warnx("cannot point current at %s", gdir);
	printf("generation %d saved\n", gid);
	free(gdir);
	return gid;
}

static int
active_gen_id(void)
{
	Manifest m;
	char *p;
	int gid;

	p = strf("%s/manifest.json", GENIX_CURRENT);
	if (manifest_read_json(p, &m) < 0) {
		free(p);
		return 0;
	}
	free(p);
	gid = m.generation;
	manifest_free(&m);
	return gid;
}

static const char *
which_init(void)
{
	/* sysv first — openrc also has /etc/init.d. systemd only if
	   we're actually running under it; having systemctl in PATH
	   doesn't mean much on a Gentoo box. */
	if (is_dir("/etc/rc.d/init.d"))
		return "sysv";
	if (which("rc-update") && is_dir("/etc/init.d"))
		return "openrc";
	if (which("systemctl") && exists("/run/systemd/system"))
		return "systemd";
	return NULL;
}

static void
sync_portage(int dry, const char *rendered)
{
	int i;
	char *srcbase = strf("%s/portage", rendered);

	if (!is_dir(GENIX_PORTAGE) && !dry) {
		printf("no /etc/portage, skipping portage sync\n");
		free(srcbase);
		return;
	}
	if (dry)
		printf("would update /etc/portage/\n");
	else
		mkdir_p(GENIX_PORTAGE);
	for (i = 0; portage_files[i]; i++) {
		char *src = strf("%s/%s", srcbase, portage_files[i]);
		char *dst = strf("%s/%s", GENIX_PORTAGE, portage_files[i]);
		if (!exists(src)) {
			free(src);
			free(dst);
			continue;
		}
		if (dry)
			printf("would sync %s\n", dst);
		else {
			printf("sync %s\n", dst);
			copy_file(src, dst, 0);
		}
		free(src);
		free(dst);
	}
	free(srcbase);
}

static void
sync_identity(int dry, const char *rendered)
{
	char *rel = strf("%s/os-release", rendered);
	char *host = strf("%s/hostname", rendered);

	if (exists(rel)) {
		if (dry)
			printf("would sync /etc/os-release\n");
		else {
			printf("sync /etc/os-release\n");
			copy_file(rel, "/etc/os-release", 0);
			if (is_dir("/usr/lib")) {
				unlink("/usr/lib/os-release");
				symlink("../etc/os-release", "/usr/lib/os-release");
			}
		}
	}
	if (exists(host)) {
		char *name = slurp(host);
		if (name) {
			str_trim(name);
			if (name[0]) {
				if (dry)
					printf("would sync /etc/hostname -> %s\n", name);
				else {
					char *body = strf("%s\n", name);
					printf("sync /etc/hostname -> %s\n", name);
					write_file("/etc/hostname", body, 0644, 0);
					free(body);
					if (which("hostname"))
						cmd(0, 0, "hostname", name, NULL);
				}
			}
			free(name);
		}
	}
	free(rel);
	free(host);
}

static void
sync_services(const Manifest *m, int dry)
{
	const char *init;
	int i;

	if (!m->services.n)
		return;
	init = which_init();
	if (init && !strcmp(init, "openrc")) {
		for (i = 0; i < m->services.n; i++) {
			char *script = strf("/etc/init.d/%s", m->services.v[i]);
			if (!exists(script)) {
				printf("service script missing: %s\n", m->services.v[i]);
				free(script);
				continue;
			}
			free(script);
			if (dry)
				printf("would enable %s (openrc, default runlevel)\n", m->services.v[i]);
			else {
				printf("enable %s (openrc, default runlevel)\n", m->services.v[i]);
				cmd(0, 0, "rc-update", "add", m->services.v[i], "default", NULL);
			}
		}
		return;
	}
	if (init && !strcmp(init, "systemd")) {
		for (i = 0; i < m->services.n; i++) {
			char unit[128];
			if (ends_with(m->services.v[i], ".service"))
				snprintf(unit, sizeof unit, "%s", m->services.v[i]);
			else
				snprintf(unit, sizeof unit, "%s.service", m->services.v[i]);
			if (dry)
				printf("would enable %s (systemd)\n", unit);
			else {
				printf("enable %s (systemd)\n", unit);
				cmd(0, 0, "systemctl", "enable", unit, NULL);
			}
		}
		return;
	}
	if (!init || strcmp(init, "sysv") != 0) {
		printf("unknown init system, skipping services\n");
		return;
	}
	for (i = 0; i < m->services.n; i++) {
		char *script = strf("/etc/rc.d/init.d/%s", m->services.v[i]);
		int rl;
		if (!exists(script)) {
			printf("service script missing: %s\n", m->services.v[i]);
			free(script);
			continue;
		}
		free(script);
		for (rl = 3; rl <= 5; rl++) {
			char *rcd = strf("/etc/rc.d/rc%d.d", rl);
			char *pat;
			int already = 0;
			DIR *d;
			struct dirent *e;
			if (!is_dir(rcd)) {
				free(rcd);
				continue;
			}
			d = opendir(rcd);
			if (d) {
				while ((e = readdir(d))) {
					if (strstr(e->d_name, m->services.v[i]))
						already = 1;
				}
				closedir(d);
			}
			if (already) {
				printf("service %s already in rl %d\n", m->services.v[i], rl);
				free(rcd);
				continue;
			}
			pat = strf("%s/S50%s", rcd, m->services.v[i]);
			if (dry)
				printf("would enable %s rl %d -> %s\n", m->services.v[i], rl, pat);
			else {
				char *tgt = strf("../init.d/%s", m->services.v[i]);
				printf("enable %s rl %d -> %s\n", m->services.v[i], rl, pat);
				unlink(pat);
				symlink(tgt, pat);
				free(tgt);
			}
			free(pat);
			free(rcd);
		}
	}
}

static int
is_installed(const char *atom)
{
	const char *slash;
	char cat[128], pkg[128], *dir;
	DIR *d;
	struct dirent *e;
	size_t plen;
	int ok = 0;

	slash = strchr(atom, '/');
	if (!slash)
		return 0; /* not a cat/pkg atom, skip */
	snprintf(cat, sizeof cat, "%.*s", (int)(slash - atom), atom);
	snprintf(pkg, sizeof pkg, "%s", slash + 1);
	plen = strlen(pkg);
	dir = strf("/var/db/pkg/%s", cat);
	d = opendir(dir);
	free(dir);
	if (!d)
		return 0;
	while ((e = readdir(d))) {
		if (strncmp(e->d_name, pkg, plen) == 0 && e->d_name[plen] == '-') {
			ok = 1;
			break;
		}
	}
	closedir(d);
	return ok;
}

typedef struct {
	StrList add;
	StrList drop;
	StrList install;
	int use_changed;
} Plan;

static void
plan_free(Plan *p)
{
	sl_free(&p->add);
	sl_free(&p->drop);
	sl_free(&p->install);
}

static void
build_plan(const Manifest *prev, const Manifest *cur, Plan *plan)
{
	int i;
	char olduse[512] = "", newuse[512] = "";

	sl_init(&plan->add);
	sl_init(&plan->drop);
	sl_init(&plan->install);
	plan->use_changed = 0;

	for (i = 0; i < cur->packages.n; i++)
		if (!prev || !sl_has(&prev->packages, cur->packages.v[i]))
			sl_push(&plan->add, cur->packages.v[i]);
	if (prev) {
		for (i = 0; i < prev->packages.n; i++) {
			if (!sl_has(&cur->packages, prev->packages.v[i])) {
				if (sl_has(&cur->provided, prev->packages.v[i]))
					printf("skip unmerge %s (now in system.provided)\n", prev->packages.v[i]);
				else
					sl_push(&plan->drop, prev->packages.v[i]);
			}
		}
	}
	for (i = 0; i < cur->packages.n; i++)
		if (!is_installed(cur->packages.v[i]))
			sl_push(&plan->install, cur->packages.v[i]);

	if (prev && prev->use)
		snprintf(olduse, sizeof olduse, "%s", prev->use);
	if (cur->use)
		snprintf(newuse, sizeof newuse, "%s", cur->use);
	plan->use_changed = !str_eq(olduse, newuse);
}

static void
print_plan(const Plan *plan, const Manifest *m, const char *mode)
{
	int i;

	printf("plan\n");
	printf("  config add: ");
	if (!plan->add.n)
		printf("-\n");
	else {
		for (i = 0; i < plan->add.n; i++)
			printf("%s%s", i ? ", " : "", plan->add.v[i]);
		printf("\n");
	}
	printf("  install:    ");
	if (!plan->install.n)
		printf("-\n");
	else {
		for (i = 0; i < plan->install.n; i++)
			printf("%s%s", i ? ", " : "", plan->install.v[i]);
		printf("\n");
	}
	printf("  drop:       ");
	if (!plan->drop.n)
		printf("-\n");
	else {
		for (i = 0; i < plan->drop.n; i++)
			printf("%s%s", i ? ", " : "", plan->drop.v[i]);
		printf("\n");
	}
	printf("  use:        %s\n", plan->use_changed ? "changed" : "same");
	for (i = 0; i < plan->install.n; i++) {
		const char *pkg = plan->install.v[i];
		printf("    %s (%s%s)%s\n", pkg,
			manifest_bin(m, pkg) ? "bin" : "src",
			manifest_nodeps(m, pkg) ? " nodeps" : "",
			sl_has(&plan->add, pkg) ? "" : " (in config, not installed yet)");
	}
	for (i = 0; i < plan->drop.n; i++)
		printf("    %s (remove)\n", plan->drop.v[i]);
	if (plan->drop.n) {
		if (str_eq(mode, "switch"))
			printf("  remove: emerge -C on dropped packages\n");
		else
			printf("  note: rollback does not unmerge dropped packages\n");
	}
}

static int
run_unmerges(const Plan *plan, int dry)
{
	StrList rm;
	int i, rc;
	char **argv;
	int n;

	if (!plan->drop.n)
		return 0;
	sl_init(&rm);
	for (i = 0; i < plan->drop.n; i++) {
		if (is_installed(plan->drop.v[i]))
			sl_push(&rm, plan->drop.v[i]);
		else
			printf("skip unmerge %s (not installed)\n", plan->drop.v[i]);
	}
	if (!rm.n) {
		sl_free(&rm);
		return 0;
	}
	if (!which("emerge")) {
		printf("emerge not found, dropped packages left installed\n");
		sl_free(&rm);
		return 0;
	}
	n = rm.n + 3;
	argv = xmalloc(sizeof(char *) * n);
	argv[0] = "emerge";
	argv[1] = "-C";
	for (i = 0; i < rm.n; i++)
		argv[2 + i] = rm.v[i];
	argv[2 + rm.n] = NULL;
	rc = cmdv(dry, 0, argv);
	free(argv);
	sl_free(&rm);
	return rc;
}

static int
emerge_subset(StrList *pkgs, int getbin, int nodeps, int dry, int lfs)
{
	char **argv;
	int n, i, a = 0, rc;
	if (!pkgs->n)
		return 0;
	n = pkgs->n + 8;
	argv = xmalloc(sizeof(char *) * n);
	argv[a++] = "emerge";
	argv[a++] = "-1";
	argv[a++] = "-av";
	if (getbin)
		argv[a++] = "--getbinpkg";
	if (nodeps)
		argv[a++] = "--nodeps";
	for (i = 0; i < pkgs->n; i++)
		argv[a++] = pkgs->v[i];
	argv[a] = NULL;
	if (lfs)
		setenv("FEATURES", "-collision-protect", 0); /* lfs stages fight this */
	rc = cmdv(dry, 0, argv);
	free(argv);
	return rc;
}

static int
run_emerges(const Plan *plan, const Manifest *m, int dry, int first_gen)
{
	int getbin;
	if (!which("emerge")) {
		printf("emerge not found, saved configs only\n");
		return 0;
	}
	if (plan->install.n) {
		if (m->lfs_mode) {
			if (!which("cmake") || !which("ninja"))
				printf("warning: cmake/ninja missing — run bootstrap-build-tools.sh\n");
		}
		for (getbin = 0; getbin <= 1; getbin++) {
			/* src first, then bin — keeps --getbinpkg off the compile set */
			StrList nodeps, full;
			int i;
			sl_init(&nodeps);
			sl_init(&full);
			for (i = 0; i < plan->install.n; i++) {
				const char *pkg = plan->install.v[i];
				if ((int)manifest_bin(m, pkg) != getbin)
					continue;
				if (manifest_nodeps(m, pkg))
					sl_push(&nodeps, pkg);
				else
					sl_push(&full, pkg);
			}
			if (emerge_subset(&nodeps, getbin, 1, dry, m->lfs_mode)) {
				sl_free(&nodeps);
				sl_free(&full);
				return 1;
			}
			if (emerge_subset(&full, getbin, 0, dry, m->lfs_mode)) {
				sl_free(&nodeps);
				sl_free(&full);
				return 1;
			}
			sl_free(&nodeps);
			sl_free(&full);
		}
	}
	if (plan->use_changed && !first_gen) {
		if (m->lfs_mode)
			printf("use flags synced to make.conf (lfs: skip auto rebuild)\n");
		else if (cmd(dry, 0, "emerge", "-av", "--changed-use", "@world", NULL))
			return 1;
	}
	if (!plan->install.n && !plan->use_changed && !plan->drop.n)
		printf("nothing to build\n");
	return 0;
}

static int
cmd_switch(int dry, int no_emerge)
{
	const char *cfg = GENIX_ETC "/configuration.toml";
	Manifest m, prev;
	Plan plan;
	int have_prev = 0;
	BootCfg bc;
	int gid;

	if (!exists(cfg)) {
		fprintf(stderr, "missing %s\n", cfg);
		return 1;
	}
	if (dry) {
		char tmp[] = "/tmp/genix-dry-XXXXXX";
		if (!mkdtemp(tmp))
			die("mkdtemp");
		printf("dry run (no system changes)\n");
		render_config(cfg, tmp, &m);
		sync_portage(1, tmp);
		sync_identity(1, tmp);
		sync_services(&m, 1);
		have_prev = manifest_read_json(GENIX_CURRENT "/manifest.json", &prev) == 0;
		build_plan(have_prev ? &prev : NULL, &m, &plan);
		print_plan(&plan, &m, "switch");
		if (!no_emerge) {
			if (run_unmerges(&plan, 1) || run_emerges(&plan, &m, 1, !have_prev)) {
				plan_free(&plan);
				manifest_free(&m);
				if (have_prev)
					manifest_free(&prev);
				cmd(0, 0, "rm", "-rf", tmp, NULL);
				return 1;
			}
		} else {
			printf("skip emerge (--no-emerge)\n");
		}
		printf("dry run, generation not saved\n");
		plan_free(&plan);
		manifest_free(&m);
		if (have_prev)
			manifest_free(&prev);
		cmd(0, 0, "rm", "-rf", tmp, NULL);
		return 0;
	}

	render_config(cfg, GENIX_RENDERED, &m);
	sync_portage(0, GENIX_RENDERED);
	sync_identity(0, GENIX_RENDERED);
	sync_services(&m, 0);
	have_prev = manifest_read_json(GENIX_CURRENT "/manifest.json", &prev) == 0;
	build_plan(have_prev ? &prev : NULL, &m, &plan);
	print_plan(&plan, &m, "switch");
	if (no_emerge)
		printf("skip emerge (--no-emerge)\n");
	else {
		if (run_unmerges(&plan, 0)) {
			fprintf(stderr, "unmerge failed, generation not saved\n");
			plan_free(&plan);
			manifest_free(&m);
			if (have_prev)
				manifest_free(&prev);
			return 1;
		}
		if (run_emerges(&plan, &m, 0, !have_prev)) {
			fprintf(stderr, "emerge failed, generation not saved\n");
			plan_free(&plan);
			manifest_free(&m);
			if (have_prev)
				manifest_free(&prev);
			return 1;
		}
	}
	gid = save_generation(&m, cfg);
	boot_config_load(&bc);
	after_generation_saved(gid, &bc);
	plan_free(&plan);
	manifest_free(&m);
	if (have_prev)
		manifest_free(&prev);
	return 0;
}

static int
cmd_list(void)
{
	DIR *d;
	struct dirent *e;
	int ids[256], n = 0, i, j, active;

	if (!is_dir(GENIX_GENS)) {
		printf("no generations\n");
		return 0;
	}
	d = opendir(GENIX_GENS);
	if (!d) {
		printf("no generations\n");
		return 0;
	}
	while ((e = readdir(d))) {
		if (e->d_name[0] && strspn(e->d_name, "0123456789") == strlen(e->d_name) && n < 256)
			ids[n++] = atoi(e->d_name);
	}
	closedir(d);
	/* n is tiny, bubble is fine */
	for (i = 0; i < n; i++)
		for (j = i + 1; j < n; j++)
			if (ids[j] < ids[i]) {
				int t = ids[i];
				ids[i] = ids[j];
				ids[j] = t;
			}
	active = active_gen_id();
	for (i = 0; i < n; i++) {
		char *p = strf("%s/%d/manifest.json", GENIX_GENS, ids[i]);
		Manifest m;
		if (manifest_read_json(p, &m) < 0) {
			free(p);
			continue;
		}
		printf("%s %3d  %s\n", ids[i] == active ? "*" : " ", m.generation, m.timestamp ? m.timestamp : "?");
		printf("      use: %s\n", m.use && m.use[0] ? m.use : "-");
		printf("      pkgs: ");
		if (!m.packages.n)
			printf("-\n");
		else {
			int k;
			for (k = 0; k < m.packages.n; k++)
				printf("%s%s", k ? ", " : "", m.packages.v[k]);
			printf("\n");
		}
		if (m.provided.n)
			printf("      provided: %d\n", m.provided.n);
		if (m.services.n) {
			int k;
			printf("      services: ");
			for (k = 0; k < m.services.n; k++)
				printf("%s%s", k ? ", " : "", m.services.v[k]);
			printf("\n");
		}
		manifest_free(&m);
		free(p);
	}
	return 0;
}

static int
cmd_rollback(int gen_id, int dry)
{
	char *gdir = strf("%s/%d", GENIX_GENS, gen_id);
	char *cfg_src = strf("%s/configuration.toml", gdir);
	char *man_src = strf("%s/manifest.json", gdir);
	Manifest prev, target, m;
	Plan plan;
	int have_prev;

	if (!exists(man_src) || !exists(cfg_src)) {
		fprintf(stderr, "generation %d not found\n", gen_id);
		free(gdir);
		free(cfg_src);
		free(man_src);
		return 1;
	}
	have_prev = manifest_read_json(GENIX_CURRENT "/manifest.json", &prev) == 0;
	if (have_prev && prev.generation == gen_id) {
		printf("already on generation %d\n", gen_id);
		manifest_free(&prev);
		free(gdir);
		free(cfg_src);
		free(man_src);
		return 0;
	}
	if (manifest_read_json(man_src, &target) < 0) {
		fprintf(stderr, "generation %d missing manifest.json\n", gen_id);
		if (have_prev)
			manifest_free(&prev);
		free(gdir);
		free(cfg_src);
		free(man_src);
		return 1;
	}
	printf("rollback to generation %d\n", gen_id);
	if (dry) {
		build_plan(have_prev ? &prev : NULL, &target, &plan);
		print_plan(&plan, &target, "rollback");
		printf("dry run, system unchanged\n");
		plan_free(&plan);
		manifest_free(&target);
		if (have_prev)
			manifest_free(&prev);
		free(gdir);
		free(cfg_src);
		free(man_src);
		return 0;
	}
	copy_file(cfg_src, GENIX_ETC "/configuration.toml", 0);
	render_config(GENIX_ETC "/configuration.toml", GENIX_RENDERED, &m);
	sync_portage(0, GENIX_RENDERED);
	sync_identity(0, GENIX_RENDERED);
	sync_services(&m, 0);
	build_plan(have_prev ? &prev : NULL, &m, &plan);
	print_plan(&plan, &m, "rollback");
	if (run_emerges(&plan, &m, 0, 0)) {
		fprintf(stderr, "emerge failed, generation not activated\n");
		plan_free(&plan);
		manifest_free(&m);
		manifest_free(&target);
		if (have_prev)
			manifest_free(&prev);
		free(gdir);
		free(cfg_src);
		free(man_src);
		return 1;
	}
	unlink(GENIX_CURRENT);
	symlink(gdir, GENIX_CURRENT);
	plan_free(&plan);
	manifest_free(&m);
	manifest_free(&target);
	if (have_prev)
		manifest_free(&prev);
	free(gdir);
	free(cfg_src);
	free(man_src);
	return 0;
}

static int
cmd_delete(int gen_id, int dry)
{
	char *gdir = strf("%s/%d", GENIX_GENS, gen_id);
	char *man = strf("%s/manifest.json", gdir);
	BootCfg bc;

	if (!exists(man)) {
		fprintf(stderr, "generation %d not found\n", gen_id);
		free(man);
		free(gdir);
		return 1;
	}
	free(man);
	if (active_gen_id() == gen_id) {
		fprintf(stderr, "can't delete active generation %d\n", gen_id);
		free(gdir);
		return 1;
	}
	if (dry) {
		printf("would delete generation %d\n", gen_id);
		free(gdir);
		return 0;
	}
	boot_config_load(&bc);
	if (!delete_snapshot(gen_id, &bc)) {
		free(gdir);
		return 1;
	}
	cmd(0, 0, "rm", "-rf", gdir, NULL);
	printf("deleted generation %d\n", gen_id);
	free(gdir);
	return 0;
}

static int
cmd_prune(int keep, int dry)
{
	DIR *d;
	struct dirent *e;
	int ids[256], n = 0, i, j, active, dropped = 0;
	BootCfg bc;

	if (keep < 1) {
		fprintf(stderr, "keep must be at least 1\n");
		return 1;
	}
	if (!is_dir(GENIX_GENS)) {
		printf("nothing to prune (0 generations, keep %d)\n", keep);
		return 0;
	}
	d = opendir(GENIX_GENS);
	while (d && (e = readdir(d))) {
		if (e->d_name[0] && strspn(e->d_name, "0123456789") == strlen(e->d_name) && n < 256)
			ids[n++] = atoi(e->d_name);
	}
	if (d)
		closedir(d);
	for (i = 0; i < n; i++)
		for (j = i + 1; j < n; j++)
			if (ids[j] < ids[i]) {
				int t = ids[i];
				ids[i] = ids[j];
				ids[j] = t;
			}
	if (n <= keep) {
		printf("nothing to prune (%d generations, keep %d)\n", n, keep);
		return 0;
	}
	active = active_gen_id();
	boot_config_load(&bc);
	for (i = 0; i < n - keep; i++) {
		char *gdir;
		if (ids[i] == active) {
			printf("skip %d (active)\n", ids[i]);
			continue;
		}
		if (dry) {
			printf("would delete generation %d\n", ids[i]);
			dropped++;
			continue;
		}
		if (!delete_snapshot(ids[i], &bc))
			return 1;
		gdir = strf("%s/%d", GENIX_GENS, ids[i]);
		cmd(0, 0, "rm", "-rf", gdir, NULL);
		printf("deleted generation %d\n", ids[i]);
		free(gdir);
		dropped++;
	}
	if (!dropped)
		printf("nothing deleted (active gen is old)\n");
	return 0;
}

static void
doc_line(const char *kind, const char *msg)
{
	printf("%-5s %s\n", kind, msg);
}

static int
cmd_doctor(void)
{
	int fails = 0, warns = 0;
	char *err = NULL;
	Toml *cfg = toml_parse_file(GENIX_ETC "/configuration.toml", &err);
	const char *init;
	BootCfg bc;
	int lfs = 0;

	printf("genix doctor\n");
	if (cfg) {
		doc_line("ok", "configuration.toml parses");
		lfs = toml_bool(cfg, "system.portage.lfs", 0);
	} else {
		doc_line("FAIL", err ? err : "missing configuration.toml");
		fails++;
	}
	free(err);
	if (which("emerge"))
		doc_line("ok", "emerge: in PATH");
	else {
		doc_line("FAIL", "emerge not in PATH");
		fails++;
	}
	init = which_init();
	if (init) {
		char *s = strf("init: %s (services.enable supported)", init);
		doc_line("ok", s);
		free(s);
	} else {
		doc_line("warn", "init not recognised — services.enable will be skipped");
		warns++;
	}
	if (cfg && lfs) {
		doc_line("ok", "lfs mode enabled in config");
		if (!which("cmake")) {
			doc_line("warn", "cmake missing — run bootstrap-build-tools.sh");
			warns++;
		} else
			doc_line("ok", "cmake: in PATH");
		if (!which("ninja")) {
			doc_line("warn", "ninja missing — run bootstrap-build-tools.sh");
			warns++;
		} else
			doc_line("ok", "ninja: in PATH");
	} else {
		doc_line("ok", "lfs mode off (or config unreadable)");
	}
	if (exists(GENIX_PORTAGE "/make.conf"))
		doc_line("ok", "/etc/portage/make.conf exists");
	else
		doc_line("warn", "no /etc/portage/make.conf yet");
	if (exists(GENIX_CURRENT)) {
		char *s = strf("active generation: %d", active_gen_id());
		doc_line("ok", s);
		free(s);
	} else {
		doc_line("warn", "no active generation");
		warns++;
	}
	boot_config_from_toml(cfg, &bc);
	{
		char fs[64];
		root_mount(NULL, 0, fs, sizeof fs);
		if (bc.enabled) {
			if (str_eq(fs, "btrfs"))
				doc_line("ok", "boot generations enabled (btrfs root)");
			else {
				doc_line("warn", "boot enabled but root is not btrfs");
				warns++;
			}
		} else if (str_eq(fs, "btrfs")) {
			doc_line("ok", "btrfs root (enable [system.boot] for boot-menu gens)");
		}
	}
	toml_free(cfg);
	if (fails) {
		printf("result: %d fail, %d warn\n", fails, warns);
		return 1;
	}
	if (warns)
		printf("result: ok (%d warn)\n", warns);
	else
		printf("result: ok\n");
	return 0;
}

static int
cmd_boot(const char *action)
{
	BootCfg bc;
	boot_config_load(&bc);
	if (!action || strcmp(action, "status") == 0)
		return boot_status(&bc);
	if (strcmp(action, "sync") == 0) {
		if (geteuid() != 0) {
			fprintf(stderr, "run as root\n");
			return 1;
		}
		return boot_sync(&bc);
	}
	fprintf(stderr, "usage: genix-rebuild boot status|sync\n");
	return 1;
}

int
main(int argc, char **argv)
{
	int dry = 0, no_emerge = 0, i, narg = 0;
	char *args[16];

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--dry-run") == 0)
			dry = 1;
		else if (strcmp(argv[i], "--no-emerge") == 0)
			no_emerge = 1;
		else if (narg < 15)
			args[narg++] = argv[i];
	}
	if (!narg) {
		fprintf(stderr, "usage: genix-rebuild switch|rollback <N>|list|delete <N>|prune <keep>|doctor|boot status|sync [--dry-run] [--no-emerge]\n");
		return 1;
	}
	if ((str_eq(args[0], "switch") || str_eq(args[0], "rollback") ||
	     str_eq(args[0], "delete") || str_eq(args[0], "prune")) && geteuid() != 0) {
		fprintf(stderr, "run as root\n");
		return 1;
	}
	if (str_eq(args[0], "switch"))
		return cmd_switch(dry, no_emerge);
	if (str_eq(args[0], "list"))
		return cmd_list();
	if (str_eq(args[0], "doctor"))
		return cmd_doctor();
	if (str_eq(args[0], "boot"))
		return cmd_boot(narg > 1 ? args[1] : "status");
	if (str_eq(args[0], "rollback")) {
		if (narg < 2) {
			fprintf(stderr, "usage: genix-rebuild rollback <N>\n");
			return 1;
		}
		return cmd_rollback(atoi(args[1]), dry);
	}
	if (str_eq(args[0], "delete")) {
		if (narg < 2) {
			fprintf(stderr, "usage: genix-rebuild delete <N>\n");
			return 1;
		}
		return cmd_delete(atoi(args[1]), dry);
	}
	if (str_eq(args[0], "prune")) {
		if (narg < 2) {
			fprintf(stderr, "usage: genix-rebuild prune <keep>\n");
			return 1;
		}
		return cmd_prune(atoi(args[1]), dry);
	}
	fprintf(stderr, "unknown command: %s\n", args[0]);
	return 1;
}
