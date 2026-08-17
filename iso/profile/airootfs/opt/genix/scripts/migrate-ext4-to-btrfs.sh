#!/bin/bash
# ext4 → btrfs migration helper for Genix boot generations
#
# Run steps 1–2 from the LIVE SYSTEM (backup only).
# Steps 3+ require a live USB — this script does NOT automate destructive work.
#
# See docs/BOOT-GENERATIONS.md for the full picture.

set -euo pipefail

ROOT_DEV="${ROOT_DEV:-/dev/nvme0n1p3}"
BACKUP_DIR="${BACKUP_DIR:-/root/genix-btrfs-migrate-backup}"
BTRFS_MOUNT="${BTRFS_MOUNT:-/mnt/btrfs-new}"

die() { echo "error: $*" >&2; exit 1; }

step_backup() {
  echo "=== backup Genix state ==="
  mkdir -p "${BACKUP_DIR}"
  declare -A names=(
    ["/etc/genix"]="etc-genix"
    ["/var/lib/genix"]="var-lib-genix"
    ["/etc/portage"]="etc-portage"
    ["/boot"]="boot"
  )
  for path in "${!names[@]}"; do
    if [[ -e "${path}" ]]; then
      out="${BACKUP_DIR}/${names[$path]}.tar"
      tar -C / -cf "${out}" "${path#/}"
      echo "  wrote ${out}"
    fi
  done
  echo "backup done → ${BACKUP_DIR}"
  echo "copy this directory OFF the disk before live-USB migrate!"
}

step_check() {
  echo "=== current root ==="
  findmnt -no SOURCE,FSTYPE,OPTIONS /
  echo
  echo "Genix generations:"
  genix-rebuild list 2>/dev/null || true
  echo
  genix-rebuild boot status 2>/dev/null || true
}

step_live_usb_notes() {
  cat <<'EOF'
=== from live USB (manual — read before running) ===

1. Wipe or shrink partition; create btrfs on ROOT_DEV.
2. Mount and create default subvolume:
     mkfs.btrfs -L genix-root ROOT_DEV
     mount ROOT_DEV /mnt
     btrfs subvolume create /mnt/@
     umount /mnt
     mount -o subvol=@ ROOT_DEV /mnt/btrfs-new

3. Rsync old root (adjust source mount):
     rsync -aHAXx --delete /old-root/ /mnt/btrfs-new/

4. Fix /mnt/btrfs-new/etc/fstab:
     UUID=...  /  btrfs  subvol=@,compress=zstd  0 0

5. Chroot and reinstall GRUB with btrfs support:
     mount --bind /dev /mnt/btrfs-new/dev
     mount --bind /proc /mnt/btrfs-new/proc
     mount --bind /sys /mnt/btrfs-new/sys
     chroot /mnt/btrfs-new
     grub-install /dev/nvme0n1
     grub-mkconfig -o /boot/grub/grub.cfg

6. Boot new system, enable in /etc/genix/configuration.toml:
     [system.boot]
     enabled = true

7. genix-rebuild boot sync
EOF
}

usage() {
  cat <<EOF
usage: $0 backup|check|notes

  backup  tar /etc/genix, /var/lib/genix, /etc/portage, /boot
  check   show root fs + genix-rebuild boot status
  notes   print live-USB migration checklist

env: ROOT_DEV, BACKUP_DIR, BTRFS_MOUNT
EOF
}

case "${1:-}" in
  backup) step_backup ;;
  check)  step_check ;;
  notes)  step_live_usb_notes ;;
  *)      usage; exit 1 ;;
esac
