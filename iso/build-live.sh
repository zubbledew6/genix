#!/bin/bash
# Build genix-live.iso from iso/work artifacts
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="${ROOT}/iso/work"
OUT="${ROOT}/iso/out"
LIVE="${WORK}/live"

need() {
  command -v "$1" >/dev/null || { echo "missing: $1" >&2; exit 1; }
}

need mksquashfs
need grub-mkrescue
need xorriso

mkdir -p "${OUT}" "${LIVE}/boot/grub"

if [[ ! -f "${WORK}/vmlinuz" ]]; then
  echo "place kernel at iso/work/vmlinuz" >&2
  echo "place initramfs at iso/work/initramfs (optional)" >&2
  echo "place rootfs tarball at iso/work/rootfs.tar.gz (optional)" >&2
  exit 1
fi

cp "${WORK}/vmlinuz" "${LIVE}/boot/"
[[ -f "${WORK}/initramfs" ]] && cp "${WORK}/initramfs" "${LIVE}/boot/"

# Overlay genix repo into squashfs root
RSYNC_SRC="${WORK}/root"
mkdir -p "${RSYNC_SRC}"
rsync -a --delete \
  --exclude='.git' --exclude='iso/work' --exclude='iso/out' \
  "${ROOT}/" "${RSYNC_SRC}/opt/genix/"

if [[ -f "${WORK}/rootfs.tar.gz" ]]; then
  echo "extracting rootfs tarball into squashfs source..."
  tar -xzf "${WORK}/rootfs.tar.gz" -C "${RSYNC_SRC}"
fi

cat > "${LIVE}/boot/grub/grub.cfg" <<'EOF'
set timeout=10
set default=0
menuentry "Genix Live (install)" {
  linux /boot/vmlinuz quiet genix.install=1
  initrd /boot/initramfs
}
EOF

mksquashfs "${RSYNC_SRC}" "${LIVE}/genix.squashfs" -comp zstd -noappend

grub-mkrescue -o "${OUT}/genix-live.iso" "${LIVE}" 2>/dev/null || \
  grub-mkrescue -o "${OUT}/genix-live.iso" "${LIVE}"

echo "built ${OUT}/genix-live.iso"
ls -lh "${OUT}/genix-live.iso"
