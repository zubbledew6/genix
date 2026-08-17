# ext4 → btrfs on the laptop

keep the existing genix/lfs install, move root to btrfs `@` so boot generations work.

dont run `genix-install` for this — that does a fresh stage3. use btrfs-convert or rsync.

disk notes: `/dev/nvme0n1p3` was ext4 root, grub on `/dev/nvme0n1`.

---

## before live usb (still on the laptop)

### 1. Copy backups OFF the disk

Your backup lives on root — wiping/converting the partition kills it. From laptop:

```bash
# on laptop — copy to main PC (adjust IP)
tar -C /root -czf - genix-btrfs-migrate-backup | ssh user@MAIN_PC 'cat > ~/genix-laptop-backup.tar.gz'
```

Or plug in a USB stick and copy `/root/genix-btrfs-migrate-backup/` there.

### 2. Deploy latest genix

From **main PC** (your user, not root):

```bash
tar -C ~/Projects/genix -cf - \
  --exclude='genix/.git' --exclude='genix/__pycache__' \
  genix | ssh user@LAPTOP 'rm -rf /tmp/genix && tar -xf - -C /tmp'
```

On **laptop** as root:

```bash
cd /tmp/genix && ./install.sh
genix-rebuild doctor
```

### 3. Confirm boot config (already set)

```toml
[system.boot]
enabled = true
backend = "grub"
subvol_prefix = "@genix-"
default_subvol = "@"
```

### 4. Install tools needed on live USB

Any live environment with: `btrfs-progs`, `grub`, `rsync` (SystemRescue, Finnix, Arch ISO, etc.).

---

## Live USB session

Boot live USB on the **laptop**. All commands below as root.

### 1. Identify root (verify before destroy)

```bash
lsblk -f
# expect nvme0n1p3 = ext4, mounted or mountable
export ROOT_PART=/dev/nvme0n1p3
export DISK=/dev/nvme0n1
export MNT=/mnt/genix
```

### 2. Unmount root if mounted

```bash
umount ${ROOT_PART}* 2>/dev/null || true
umount /mnt/* 2>/dev/null || true
```

### 3. Convert ext4 → btrfs in place

```bash
btrfs-convert ${ROOT_PART}
```

Takes a while. Creates btrfs with your existing files at the **top level** (no `@` yet).

### 4. Create `@` subvolume and move root into it

```bash
mkdir -p ${MNT}
mount ${ROOT_PART} ${MNT}

btrfs subvolume create ${MNT}/@
mkdir -p ${MNT}/@/.top

# move everything except @ into @ (btrfs root can't be rsync src easily — use btrfs subvolume snapshot trick)
for d in ${MNT}/*; do
  base=$(basename "$d")
  [[ "$base" == "@" ]] && continue
  mv "$d" "${MNT}/@/"
done

umount ${MNT}
btrfs subvolume set-default $(btrfs subvolume list ${ROOT_PART} | awk '/\/@$/ {print $2}') ${ROOT_PART}
mount -o subvol=@ ${ROOT_PART} ${MNT}
```

**If `mv` fails** (busy/top-level files), use rsync instead:

```bash
mount ${ROOT_PART} ${MNT}
btrfs subvolume create ${MNT}/@
rsync -aHAXx --exclude=@ ${MNT}/ ${MNT}/@/
# then manually clean top-level after verifying @ has everything (advanced — ask if stuck)
```

### 5. Fix fstab

```bash
UUID=$(blkid -s UUID -o value ${ROOT_PART})
sed -i "s|.*UUID=.* / .*|UUID=${UUID}  /  btrfs  subvol=@,compress=zstd  0 0|" ${MNT}/etc/fstab
# or edit by hand:  nano ${MNT}/etc/fstab
cat ${MNT}/etc/fstab
```

### 6. Chroot + GRUB

```bash
mount --bind /dev  ${MNT}/dev
mount --bind /proc ${MNT}/proc
mount --bind /sys  ${MNT}/sys
mount --bind /run  ${MNT}/run

chroot ${MNT}
grub-install ${DISK}
grub-mkconfig -o /boot/grub/grub.cfg
exit
```

### 7. Boot back into laptop system

```bash
umount ${MNT}/run ${MNT}/sys ${MNT}/proc ${MNT}/dev
umount ${MNT}
reboot
```

---

## After first btrfs boot (normal laptop system)

```bash
findmnt -no FSTYPE /
# btrfs

genix-rebuild boot status
# btrfs: yes

genix-rebuild switch --no-emerge
genix-rebuild boot sync
reboot
```

GRUB should show **Genix current (@)** and **Genix generation N (@genix-N)** entries.

Each future `genix-rebuild switch` snapshots `@genix-N` automatically.

---

## Pick up here (laptop, Aug 2026)

If `@genix-19` shows `(no snap)` because `btrfs` was missing:

### 1. Live USB — copy btrfs tools

```bash
export MNT=/mnt/genix
mkdir -p ${MNT}
mount -o subvol=@ /dev/nvme0n1p3 ${MNT}

\cp -a /usr/sbin/btrfs* ${MNT}/usr/sbin/ 2>/dev/null || true
\cp -a /sbin/btrfs* ${MNT}/sbin/ 2>/dev/null || true
\cp -a /usr/bin/btrfs* ${MNT}/usr/bin/ 2>/dev/null || true
ls ${MNT}/usr/sbin/btrfs

cd /
umount -R ${MNT}
reboot
```

(Or on a running system with Wi‑Fi: `emerge -1 -av --nodeps sys-fs/btrfs-progs`.)

### 2. Root on laptop — snapshot gen 19

Boot **LFS boot (live kernel)**. As root:

```bash
export PATH=/usr/sbin:/sbin:/usr/bin:/bin:$PATH
which btrfs
btrfs subvolume snapshot / @genix-19
genix-rebuild boot status   # gen 19 → (snap)
```

### 3. Deploy latest genix from main PC

```bash
tar -C ~/Projects/arch-hyprland -cf - \
  --exclude='genix/.git' --exclude='genix/__pycache__' --exclude='*.pyc' \
  genix | ssh user@LAPTOP 'rm -rf /tmp/genix && tar -xf - -C /tmp'
```

On laptop as root: `cd /tmp/genix && ./install.sh`

### 4. EFI GRUB + Genix menu

You boot from **`/boot/efi/grub/grub.cfg`** + **`vmlinuz-live`**, not btrfs `/boot/grub/grub.cfg`.

Latest genix `boot sync` also writes Genix entries to `/boot/efi/grub/grub.cfg` when that file exists (parses your live-kernel line and adds `rootflags=subvol=@genix-N` per generation).

```bash
genix-rebuild boot sync
reboot
```

Optional in `configuration.toml` if auto-detect fails:

```toml
[system.boot]
efi_uuid = "6555-CC8E"
efi_kernel = "/vmlinuz-live"
root = "/dev/nvme0n1p3"
```

### 5. Later

Rebuild LFS 6.16.1 kernel with Gentoo-like DRM/i915, or keep `vmlinuz-live` + matching modules.

---

## If something breaks

- Boot live USB, mount `-o subvol=@ ${ROOT_PART}` at `/mnt`, chroot, fix fstab/grub
- Restore from `~/genix-laptop-backup.tar.gz` on main PC (config/generations only — not full disk)
- `genix-rebuild rollback 18` from a bootable system

---

## Quick reference

| Step | Where |
|------|-------|
| Backup off-disk | Laptop (now) |
| `./install.sh` | Laptop (now) |
| `btrfs-convert` | Live USB |
| `@` subvol + fstab | Live USB |
| `grub-install` | Live USB chroot |
| `boot sync` | Laptop after reboot |
