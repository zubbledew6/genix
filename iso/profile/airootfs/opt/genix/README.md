# Genix

Declarative system config on top of Portage. One TOML file, one command, saved generations.

```
configuration.toml  →  genix-rebuild switch  →  Portage + /etc/os-release  →  generation N
```

## Getting a Genix system

Build the live ISO and install from it — see [`iso/README.md`](iso/README.md).
It installs a **Gentoo stage3 with OpenRC** (no systemd) on a btrfs `@`
subvolume, with Genix already set up.

Genix also runs on an existing system: it needs Portage for packages, and it
supports OpenRC, systemd, and LFS/SysV for `services.enable`.

## Requirements

- Portage (`emerge`) for package management
- A C11 compiler (`gcc`) to build `genix-rebuild` / `genix-render` (`make`)
- Python 3.11+ for `genix-install` only
- Root for `install.sh`, `switch`, and `rollback`
- btrfs + GRUB for boot generations (optional)

Layering Portage onto an existing LFS system: `./bootstrap-portage.sh`

For cmake-based packages (cmatrix, fortune-mod, etc.) on LFS with `--nodeps`, also run once:

```bash
./bootstrap-build-tools.sh   # installs cmake + ninja to /usr/local
```

Without cmake in PATH, those ebuilds fail even though simpler packages (cowsay, nyancat, pv, vim) may work.

## Install

```bash
scp -r genix user@host:/tmp/
ssh user@host
su -
cd /tmp/genix && ./install.sh
```

Edit `/etc/genix/configuration.toml` (example copied on first install).

## Config

```toml
[system]
hostname = "genix"
use = ["-systemd", "openssl"]

[system.identity]
name = "Genix"
pretty_name = "Genix"
id = "genix"
id_like = "gentoo"
version = "0.1"

[system.portage]
accept_keywords = "amd64"
makeopts = "-j8"
lfs = true

[system.provided]
packages = [
  "sys-libs/glibc",
  "sys-devel/gcc",
]

[packages]
want = [
  "app-editors/vim",
  { name = "www-client/firefox", binary = true },
]
```

| Section | Effect on `switch` |
|---------|-------------------|
| `system.identity` | `/etc/os-release` → tools report **Genix** |
| `system.provided` | `/etc/portage/package.provided` → skip LFS-built base packages |
| `system.portage.lfs` | `emerge --nodeps` + `-collision-protect` |
| `packages.want` | `/etc/portage/package.world` |
| `packages.want` `{ nodeps = false }` | full deps for that atom when `lfs = true` |
| `[services].enable` | SysV symlinks in `/etc/rc.d/rc{3,4,5}.d/` |

Default is compile from source. Use `binary = true` or `bin>category/pkg` with a `binhost` for prebuilt packages.

Daily-driver guide: [docs/DAILY-DRIVER.md](docs/DAILY-DRIVER.md). Boot generations: [docs/BOOT-GENERATIONS.md](docs/BOOT-GENERATIONS.md). Full roadmap (ISO, installer, boot picker): [docs/ROADMAP.md](docs/ROADMAP.md).

## Commands

```bash
genix-render                      # render only
genix-rebuild switch              # apply config, emerge diff, save new generation
genix-rebuild switch --dry-run    # preview only — no /etc changes, no emerge
genix-rebuild switch --no-emerge  # apply config + save gen, skip emerge/unmerge
genix-rebuild rollback 3          # restore generation 3 (no new generation)
genix-rebuild rollback 3 --dry-run
genix-rebuild list
genix-rebuild delete 2          # remove old generation (not the active one)
genix-rebuild prune 5           # keep newest 5 generations
genix-rebuild prune 5 --dry-run
genix-rebuild doctor              # health check (PATH, config, tools, generations)
genix-rebuild boot status         # btrfs + snapshot status for boot-menu gens
genix-rebuild boot sync           # regenerate GRUB entries (root as root)
genix-install                     # TUI fresh install (live USB, destructive)
genix-install --dry-run           # show install plan only
```

Boot-menu generations (pick a saved gen at reboot) need **btrfs root** and `[system.boot] enabled = true`. See [docs/BOOT-GENERATIONS.md](docs/BOOT-GENERATIONS.md). On ext4, everything else still works — boot commands report status only.

### Generations

Each `switch` saves under `/var/lib/genix/generations/N/`:

- `manifest.json` — USE, packages, provided count, timestamp
- `configuration.toml` — exact config used
- `portage/` — rendered Portage snapshot
- `os-release`, `hostname` — identity snapshot

`rollback N` restores generation N's config and re-applies Portage + identity. Packages removed since that generation are **not** auto-unmerged (safe on LFS). On `switch`, dropped packages **are** removed with `emerge -C`. With btrfs + `[system.boot]`, each `switch` also snapshots `@genix-N` and updates GRUB.

## Fastfetch

`install.sh` installs the Genix ASCII logo to `/root/.config/fastfetch/` and `/etc/skel/`. Install `app-misc/fastfetch` separately.

## Roadmap

- [x] Config → Portage render
- [x] Incremental emerge on `switch`
- [x] Generations + `list`
- [x] `rollback`
- [x] LFS host-provided packages
- [x] OS identity (`/etc/os-release`)
- [x] SysV `[services].enable` on switch/rollback
- [x] Generation `delete` / `prune`
- [x] Boot-menu generations (GRUB + btrfs snapshots) — needs btrfs migrate on laptop
- [x] Per-package `nodeps`, `switch --no-emerge`
- [x] TUI installer (`genix-install`)
- [ ] Limine boot backend
- [ ] Live ISO (`iso/build-live.sh` — skeleton only)
- [ ] ISO / live image (full rootfs pipeline)

## License

MIT — see [LICENSE](LICENSE).
