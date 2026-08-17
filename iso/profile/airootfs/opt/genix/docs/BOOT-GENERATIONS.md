# Boot-menu generations (phase C)

Nix-style rollback at **reboot**: pick generation 12 from GRUB/Limine instead of running `genix-rebuild rollback 12` from a running system.

## Prerequisites

1. **Root filesystem must be Btrfs** with subvolumes.
   Your Genix laptop is **ext4 today** — boot gens do nothing until you migrate (see below).
2. **GRUB or Limine** with Btrfs root support (you have GRUB on LFS).
3. Genix generations already saved under `/var/lib/genix/generations/N/` (you have this).

## Target layout

```
/dev/nvme0n1p3  btrfs
  @              ← active system (default subvol, read-write)
  @genix-1       ← snapshot taken after generation 1 switch
  @genix-15      ← snapshot taken after generation 15 switch
  @home          ← optional separate subvol
```

## Flow

```
genix-rebuild switch
  → emerge + save manifest (gen N)     [existing]
  → btrfs snapshot / → @genix-N        [new, if boot enabled]
  → grub/genix menu entries updated    [new]
```

At reboot: pick **Genix 12** in boot menu → kernel boots with `rootflags=subvol=@genix-12`.

CLI rollback (`genix-rebuild rollback N`) stays — it fixes a running system. Boot rollback is for when the running system is broken.

## Config

```toml
[system.boot]
enabled = true
backend = "grub"           # grub | limine
subvol_prefix = "@genix-"    # snapshot names: @genix-15
default_subvol = "@"         # subvol you boot for "latest"
```

See `example/configuration.toml` (commented block at bottom).

## Migration ext4 → Btrfs (one-time, painful)

Do from **live USB** when ready — not required for daily Genix use.

1. Backup `/etc/genix`, `/var/lib/genix`, `/etc/portage`, `/boot`.
2. Shrink or replace root partition; create btrfs, subvol `@`.
3. Rsync ext4 root → `@`, fix `/etc/fstab` (`btrfs`, `subvol=@`).
4. Reinstall GRUB with btrfs modules.
5. Boot, enable `[system.boot]`, run `genix-rebuild boot sync`.

Detailed migrate script: `scripts/migrate-ext4-to-btrfs.sh`.

## Commands (phase C rollout)

| Command | Status |
|---------|--------|
| `genix-rebuild boot status` | done |
| `genix-rebuild boot sync` | done (GRUB) |
| snapshot on `switch` | done when btrfs + enabled |
| `prune` / `delete` drop `@genix-N` subvols | done when btrfs + enabled |

## Phases

- **C1** — detect btrfs, `boot status`, docs ✓
- **C2** — snapshot on switch + `boot sync` for GRUB ✓
- **C3** — prune deletes subvols; Limine backend
- **C4** — migrate script + fresh Btrfs install guide

After C is usable → **A** (daily driver packages) → **D** (per-package nodeps, `--no-emerge`).
