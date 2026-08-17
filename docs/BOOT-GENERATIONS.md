# boot menu generations

pick an old generation from grub when the running system is toast.
normal `genix-rebuild rollback N` is for when you still have a shell.

needs:
- btrfs root with subvols
- grub (limine backend not done yet)
- generations already under `/var/lib/genix/generations/`

layout looks like:

```
@              ← what you normally boot
@genix-1
@genix-15
```

on `switch` (with boot enabled): emerge + save gen, snapshot to `@genix-N`, refresh grub.
reboot → pick "genix 12" → kernel gets `rootflags=subvol=@genix-12`.

```toml
[system.boot]
enabled = true
backend = "grub"
subvol_prefix = "@genix-"
default_subvol = "@"
```

## commands

```bash
genix-rebuild boot status
genix-rebuild boot sync
```

prune/delete also drop the `@genix-N` subvols when boot is on.

## coming from ext4

painful one-time thing. live usb, backup `/etc/genix` + `/var/lib/genix` + portage + boot, make btrfs `@`, rsync, fix fstab, reinstall grub, then enable boot and `boot sync`.

there’s a script: `scripts/migrate-ext4-to-btrfs.sh`  
longer notes: `docs/LAPTOP-BTRFS-MIGRATE.md`
