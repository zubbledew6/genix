# Genix

gentoo, but one toml file and you can roll back.

site (placeholder): https://genix.hoi-hoi33666.workers.dev/  
repo: https://github.com/zubbledew6/genix

```
configuration.toml  →  genix-rebuild switch  →  generation N
```

## what it is

- live usb installer → gentoo stage3 + openrc on btrfs `@`
- edit `/etc/genix/configuration.toml`, run `genix-rebuild switch`
- generations: list / rollback / prune / delete
- optional grub boot menu via btrfs `@genix-N` snapshots
- wifi from the live stick gets copied over (iwd)
- default is compile from source (`binary = false`). binhost if you want it fast

ships with **openrc**. `services.enable` also works on **systemd** and **sysv**. runit/dinit later maybe, no date.

## install from iso

build on an arch/cachy box:

```bash
git clone https://github.com/zubbledew6/genix.git
cd genix
sudo ./iso/build.sh
```

iso ends up in `iso/out/`. flash it, boot the **machine you want to wipe/install**, not your main pc.

```bash
genix-install
```

when it asks to edit config:
- **no** → pulls binaries, faster
- **yes** → you edit use flags / -j / packages, it compiles

more detail: [iso/README.md](iso/README.md)

## already have gentoo / lfs

```bash
scp -r genix user@host:/tmp/
ssh user@host
su -
cd /tmp/genix && ./install.sh
```

## needs

- portage
- gcc (`make` builds the c tools)
- python 3.11+ only for `genix-install`
- root for install/switch/rollback
- btrfs + grub if you want boot generations

lfs bootstrap: `./bootstrap-portage.sh`

## config sketch

```toml
[system]
hostname = "genix"
use = ["-systemd", "elogind"]

[system.identity]
name = "Genix"
id = "genix"
id_like = "gentoo"
home_url = "https://genixos.org/"
logo = "genix"

[system.portage]
accept_keywords = "amd64"
makeopts = "-j8"
binary = false

[packages]
want = [
  "app-editors/vim",
  { name = "www-client/firefox", binary = true },
]

[services]
enable = ["iwd", "dhcpcd", "sshd"]
```

full example: `example/configuration.toml`

## commands

```bash
genix-rebuild switch
genix-rebuild switch --dry-run
genix-rebuild rollback 3
genix-rebuild list
genix-rebuild doctor
genix-rebuild boot sync
genix-install
```

## other notes

- [daily driver](docs/DAILY-DRIVER.md)
- [boot generations](docs/BOOT-GENERATIONS.md)
- [pushing to github](docs/PUBLISHING.md)
- [fastfetch](docs/FASTFETCH.md)
- [roadmap](docs/ROADMAP.md)

GPLv3 — [LICENSE](LICENSE)
