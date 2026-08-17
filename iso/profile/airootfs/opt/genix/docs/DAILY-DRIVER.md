# Daily driver path (phase A)

Grow the Genix laptop from demo toys into something you actually SSH into and use. Add **one layer at a time** — each `switch` saves a generation you can roll back.

## Layer 0 — already working

Your TikTok/demo stack (LFS + `lfs = true` + `--nodeps`):

- `app-editors/vim`, `sys-process/htop`
- `games-misc/cowsay`, `games-misc/nyancat`, `app-misc/cmatrix`
- `bootstrap-build-tools.sh` for cmake/ninja

Copy `example/configuration-daily.toml` as a starting point, or merge its `[packages].want` into your live config.

## Layer 1 — CLI comfort (low risk)

These usually work with `--nodeps` on your LFS base:

| Package | Why |
|---------|-----|
| `app-misc/fastfetch` | Genix branding, quick health glance |
| `app-misc/screen` or `app-misc/tmux` | persistent sessions over SSH |
| `sys-apps/man-pages` | man pages for Portage installs |
| `app-archives/unzip` | unpack things |

```bash
genix-rebuild switch --dry-run   # preview
genix-rebuild switch
```

## Layer 2 — network tools

Many pull deps — try **one** package, watch emerge output:

| Package | Notes |
|---------|-------|
| `net-misc/openssh` | likely already from LFS; skip if provided |
| `net-wireless/iw` | small, often nodeps-friendly |
| `net-wireless/wireless-tools` | legacy wifi CLI |

When a package needs Portage deps, opt out of nodeps for that atom:

```toml
{ name = "net-wireless/wpa_supplicant", nodeps = false }
```

Requires resolving dependency tree manually on LFS — expect pain. That's normal.

## Layer 3 — config-only changes

Use when tweaking identity, USE flags, or services without rebuilding:

```bash
genix-rebuild switch --no-emerge
```

Applies `/etc/portage`, os-release, services, saves a generation — skips `emerge` and unmerge.

## Layer 4 — desktop (long project)

XFCE, Hyprland, or a minimal WM stack is **BLFS-scale** work on LFS. Not an evening task.

Suggested order when you're ready:

1. `x11-base/xorg-server` or Wayland stack (many deps → use `nodeps = false` selectively)
2. Window manager / DE one package at a time
3. Browser last (`www-client/firefox` is huge)

Consider turning off global LFS mode only after you trust Portage to manage most of the system:

```toml
[system.portage]
lfs = false   # future — full dependency resolution
```

## Per-package nodeps (phase D)

With `lfs = true`, all packages default to `--nodeps`. Override per atom:

```toml
[packages]
want = [
  "games-misc/cowsay",
  { name = "app-misc/fastfetch", nodeps = false },
]

[packages.nodeps]
# or set overrides here
# "app-misc/fastfetch" = false
```

Plan output shows `(src nodeps)` vs `(src)` so you can see which path each package takes.

## Deploy to laptop

```bash
tar -C ~/Projects/arch-hyprland -cf - \
  --exclude='genix/.git' --exclude='genix/__pycache__' \
  genix | ssh zubb@192.168.68.64 'rm -rf /tmp/genix && tar -xf - -C /tmp'
# on laptop as root:
cd /tmp/genix && ./install.sh
```

## What "done" looks like for phase A

- Boot laptop → login → `genix-rebuild doctor` clean
- SSH from main PC, edit `/etc/genix/configuration.toml`, `switch`
- Fastfetch shows Genix; 3–5 real tools you use weekly
- Generations list grows slowly, not 15 demo configs

Desktop GUI is **optional** for A — a solid SSH + CLI daily driver counts.
