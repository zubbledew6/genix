# Genix development roadmap

Three big tracks: **boot-menu generations**, **TUI installer**, **live ISO**.

```
                    ┌─────────────────┐
                    │  Genix ISO      │
                    │  (live boot)    │
                    └────────┬────────┘
                             │ includes
                    ┌────────▼────────┐
                    │ genix-install   │  ← TUI: disk, btrfs, chroot, grub
                    └────────┬────────┘
                             │ first switch
                    ┌────────▼────────┐
                    │ genix-rebuild   │  ← generations + btrfs snapshots
                    └────────┬────────┘
                             │ boot sync
                    ┌────────▼────────┐
                    │ GRUB / Limine   │  ← pick generation at reboot
                    └─────────────────┘
```

## Track C — Boot-menu generations (Nix-style)

| Phase | What | Status |
|-------|------|--------|
| C1 | `boot status`, btrfs detect, docs | done |
| C2 | snapshot on `switch`, GRUB `40_genix`, `boot sync` | done |
| C3 | initrd in menu entries, newest-first, prune subvols | done |
| C4 | ext4→btrfs migrate script + live-USB notes | done (manual) |
| C5 | **Limine backend** (your main PC) | TODO |
| C6 | Boot-time gen marker (`/etc/genix/boot-generation`) | TODO |
| C7 | Rollback from booted subvol syncs `current` symlink | TODO |

**Blocked on laptop today:** root is ext4. Boot gens activate after btrfs install or migration.

**Test without laptop migrate:** main PC has btrfs — install genix, enable `[system.boot]`, run `switch`, `boot sync`, reboot.

## Track I — TUI installer

| Phase | What | Status |
|-------|------|--------|
| I1 | `genix-install` TUI — disk list, plan, `--dry-run` | **now** |
| I2 | GPT partition + btrfs `@` layout + mount | **now** |
| I3 | Rsync live root → target, chroot, `install.sh` | **now** |
| I4 | Seed `configuration.toml`, GRUB, first generation | **now** |
| I5 | Gentoo stage3 tarball install path | TODO |
| I6 | LUKS, separate `/home` subvol, Wi‑Fi in live env | TODO |

**Use today:** boot any live USB with Python 3.11+, copy genix repo, run `./install.sh` then `genix-install`.

## Track O — Live ISO

| Phase | What | Status |
|-------|------|--------|
| O1 | `iso/build-live.sh` skeleton + docs | **now** |
| O2 | Build rootfs tarball (genix + portage + installer) | TODO |
| O3 | squashfs + kernel + initramfs (dracut or busybox) | TODO |
| O4 | grub-mkrescue → `genix-live.iso` | TODO |
| O5 | CI / reproducible build | TODO |

**Practical order:** I1–I4 first (installer works from any live environment), then O2–O4 bake that into an ISO.

## Suggested build order

1. **GRUB picker on btrfs** — validate on main PC (fast feedback)
2. **genix-install dry-run** on laptop from live USB (no writes)
3. **genix-install real** on spare disk or VM
4. **ISO** once installer path is stable
5. **Limine** for main PC
6. **btrfs migrate** laptop when installer/ISO are trusted

## Commands (target state)

```bash
genix-install                    # TUI fresh install
genix-install --dry-run          # show plan only
genix-rebuild switch             # save gen + btrfs snapshot
genix-rebuild boot sync          # refresh GRUB entries
genix-rebuild boot status        # snapshots vs generations
./iso/build-live.sh              # build genix-live.iso
```
