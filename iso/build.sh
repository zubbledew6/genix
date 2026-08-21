#!/bin/bash
# Build genix-live.iso on Arch/CachyOS (MAIN PC only — writes iso/out/, not your disk)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROFILE="${ROOT}/iso/profile"
OUT="${ROOT}/iso/out"
WORK="${ROOT}/iso/work"
RELENG="/usr/share/archiso/configs/releng"

if [[ "${EUID}" -ne 0 ]]; then
  echo "run as root: sudo $0" >&2
  exit 1
fi

if [[ ! -d "${RELENG}" ]]; then
  echo "need archiso: sudo pacman -S archiso" >&2
  exit 1
fi

DEPS=(archiso squashfs-tools grub xorriso arch-install-scripts gcc make)
MISSING=()
for p in "${DEPS[@]}"; do
  pacman -Q "${p}" &>/dev/null || MISSING+=("${p}")
done
if ((${#MISSING[@]})); then
  echo "installing: ${MISSING[*]}"
  pacman -Sy --needed --noconfirm "${MISSING[@]}"
fi

echo "building genix binaries (x86-64-v2, ignores host CFLAGS)..."
make -C "${ROOT}" clean
make -C "${ROOT}"

echo "building profile from archiso releng + genix overlay..."
rm -rf "${PROFILE}"
cp -a "${RELENG}" "${PROFILE}"

# Genix branding
sed -i 's/^iso_name=.*/iso_name="genix"/' "${PROFILE}/profiledef.sh"
sed -i 's/^iso_label=.*/iso_label="GENIX_$(date +%Y%m)"/' "${PROFILE}/profiledef.sh"
sed -i 's/^iso_publisher=.*/iso_publisher="Genix"/' "${PROFILE}/profiledef.sh"
sed -i 's/^iso_application=.*/iso_application="Genix Live Installer"/' "${PROFILE}/profiledef.sh"
sed -i 's/^install_dir=.*/install_dir="genix"/' "${PROFILE}/profiledef.sh"

rebrand() {
  local f
  while IFS= read -r -d '' f; do
    sed -i \
      -e 's/Arch Linux install medium/Genix live/g' \
      -e 's/Arch Linux live medium/Genix live/g' \
      -e 's/Arch Linux/Genix/g' \
      -e 's/install Arch Linux/install Genix/g' \
      -e 's/MENU TITLE Arch Linux/MENU TITLE Genix/g' \
      -e 's/default=archlinux/default=genix/g' \
      -e "s/id 'archlinux'/id 'genix'/g" \
      -e "s/id 'archlinux-accessibility'/id 'genix-accessibility'/g" \
      -e 's/--class arch /--class genix /g' \
      -e 's/Live Arch Environment/Genix live environment/g' \
      -e 's/Arch Linux repository mirrorlist/Genix live mirrorlist/g' \
      "$f"
  done < <(find "${PROFILE}" -type f \( -name '*.cfg' -o -name '*.conf' -o -name '*.service' \) -print0)
}
rebrand

sed -i \
  -e 's/^DEFAULT arch$/DEFAULT genix/' \
  -e 's/^LABEL arch$/LABEL genix/' \
  -e 's/^LABEL archspeech$/LABEL genixspeech/' \
  "${PROFILE}/syslinux/archiso_sys.cfg" \
  "${PROFILE}/syslinux/archiso_sys-linux.cfg" \
  "${PROFILE}/syslinux/archiso_pxe-linux.cfg" 2>/dev/null || true
sed -i '/^MENU BACKGROUND/d' "${PROFILE}/syslinux/archiso_head.cfg"

mv "${PROFILE}/efiboot/loader/entries/01-archiso-linux.conf" \
  "${PROFILE}/efiboot/loader/entries/01-genix-linux.conf"
mv "${PROFILE}/efiboot/loader/entries/02-archiso-speech-linux.conf" \
  "${PROFILE}/efiboot/loader/entries/02-genix-speech-linux.conf"
cat > "${PROFILE}/efiboot/loader/loader.conf" <<'EOF'
timeout 15
console-mode keep
default 01-genix-linux.conf
EOF

cat > "${PROFILE}/airootfs/etc/os-release" <<'EOF'
NAME="Genix"
PRETTY_NAME="Genix Live"
ID=genix
ID_LIKE=arch
VERSION="live"
HOME_URL="https://github.com/zubbledew6/genix"
EOF

echo genix > "${PROFILE}/airootfs/etc/hostname"

cat > "${PROFILE}/airootfs/etc/issue" <<'EOF'
Genix live (%h)

EOF

mkdir -p "${PROFILE}/airootfs/etc/systemd/system/getty@tty1.service.d"
cat > "${PROFILE}/airootfs/etc/systemd/system/getty@tty1.service.d/autologin.conf" <<'EOF'
[Service]
ExecStart=
ExecStart=-/usr/bin/agetty --noreset --noclear --autologin root - ${TERM}
EOF

cat > "${PROFILE}/airootfs/usr/local/bin/Installation_guide" <<'EOF'
#!/bin/sh
exec xdg-open 'https://github.com/zubbledew6/genix/blob/main/iso/README.md'
EOF
chmod 755 "${PROFILE}/airootfs/usr/local/bin/Installation_guide"

# Live environment only — nothing here is copied to the target. The installed
# system is a Gentoo stage3 fetched at install time.
cat >> "${PROFILE}/packages.x86_64" <<'EOF'
btrfs-progs
parted
dosfstools
python
nano
wget
curl
iwd
wpa_supplicant
gcc
make
EOF

# Genix repo on live image
mkdir -p "${PROFILE}/airootfs/opt/genix"
rsync -a --delete \
  --exclude='.git' \
  --exclude='iso/work' \
  --exclude='iso/out' \
  --exclude='iso/profile' \
  "${ROOT}/" "${PROFILE}/airootfs/opt/genix/"

cat > "${PROFILE}/airootfs/etc/motd" <<'EOF'

  Genix live — boot on TARGET laptop only
  ───────────────────────────────────────
  Installs Gentoo + OpenRC + Genix. Needs network.

  WiFi:    iwctl station wlan0 connect "SSID"
  Plan:    genix-install --dry-run
  Install: genix-install

  The installer can erase the whole disk, install into
  free space next to Windows, or hand you cfdisk.

EOF

mkdir -p "${PROFILE}/airootfs/usr/bin" "${PROFILE}/airootfs/usr/local/bin"
install -m755 "${ROOT}/build/genix-rebuild" "${PROFILE}/airootfs/usr/bin/genix-rebuild"
install -m755 "${ROOT}/build/genix-render" "${PROFILE}/airootfs/usr/bin/genix-render"

GENIX_INSTALL_WRAPPER='#!/bin/bash
set -euo pipefail
export PATH="/opt/genix/bin:/usr/local/bin:/usr/bin:/usr/sbin:/bin:/sbin"
export GENIX_SRC="/opt/genix"
export GENIX_LIB="/opt/genix/lib/genix"
exec python3 /opt/genix/lib/genix/installer.py "$@"'

printf '%s\n' "${GENIX_INSTALL_WRAPPER}" > "${PROFILE}/airootfs/usr/local/bin/genix-install"
printf '%s\n' "${GENIX_INSTALL_WRAPPER}" > "${PROFILE}/airootfs/usr/bin/genix-install"
chmod 755 "${PROFILE}/airootfs/usr/local/bin/genix-install" "${PROFILE}/airootfs/usr/bin/genix-install"

cat > "${PROFILE}/airootfs/root/customize_airootfs.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
install -Dm644 /etc/os-release /usr/lib/os-release
# invoked via bash: the airootfs copy drops execute bits
bash /opt/genix/install.sh
chmod 755 /opt/genix/install.sh /opt/genix/bin/* 2>/dev/null || true
chmod 755 /usr/bin/genix-install /usr/local/bin/genix-install /usr/bin/genix-rebuild /usr/bin/genix-render 2>/dev/null || true
EOF
chmod 755 "${PROFILE}/airootfs/root/customize_airootfs.sh"

cat > "${PROFILE}/airootfs/root/.zshrc" <<'EOF'
if [[ -o interactive ]]; then
  cat /etc/motd
fi
EOF

cat > "${PROFILE}/airootfs/root/.zlogin" <<'EOF'
#!/usr/bin/env bash
[[ -f /root/.automated_script.sh ]] && /root/.automated_script.sh
EOF
chmod 755 "${PROFILE}/airootfs/root/.zlogin"

# mkarchiso copies airootfs with --no-preserve=mode, so every execute bit is lost
# unless the path is listed in file_permissions. Derive the list from the real files.
python3 - <<'PY' "${PROFILE}"
import os
import pathlib
import sys

profile = pathlib.Path(sys.argv[1])
airootfs = profile / "airootfs"
profiledef = profile / "profiledef.sh"

entries = []
for path in sorted(airootfs.rglob("*")):
    if not path.is_file() or path.is_symlink():
        continue
    if not os.access(path, os.X_OK):
        continue
    entries.append('  ["/%s"]="0:0:755"\n' % path.relative_to(airootfs).as_posix())

text = profiledef.read_text()
anchor = '  ["/usr/local/bin/livecd-sound"]="0:0:755"\n'
if anchor not in text:
    raise SystemExit("profiledef.sh: missing file_permissions anchor")

existing = {line.split('"')[1] for line in text.splitlines() if line.strip().startswith('["/')}
new = [e for e in entries if e.split('"')[1] not in existing]
if new:
    profiledef.write_text(text.replace(anchor, anchor + "".join(new), 1))
print("file_permissions: %d executable path(s) preserved" % len(new))
PY

mkdir -p "${OUT}"
rm -f "${OUT}/genix-live.iso"
# mkarchiso's _run_once skips any step whose marker file exists in the work dir,
# so leaving the work dir in place makes a rebuild silently reuse the old
# airootfs. Packages still come from the host pacman cache, not the network.
rm -rf "${WORK}"
export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-$(date +%s)}"
BUILD_START="$(date +%s)"
echo "mkarchiso (10-30 min; every run is a full rebuild)..."
mkarchiso -v -w "${WORK}" -o "${OUT}" "${PROFILE}"

ISO="$(find "${OUT}" -maxdepth 1 -name '*.iso' ! -name 'genix-live.iso' -printf '%T@ %p\n' 2>/dev/null | sort -rn | head -1 | cut -d' ' -f2-)"
if [[ -z "${ISO}" || ! -f "${ISO}" ]]; then
  echo "mkarchiso did not produce an iso in ${OUT}" >&2
  exit 1
fi
if [[ "$(stat -c %Y "${ISO}")" -lt "${BUILD_START}" ]]; then
  echo "${ISO} predates this build — mkarchiso reused a stale image" >&2
  exit 1
fi
ln -sf "$(basename "${ISO}")" "${OUT}/genix-live.iso"

SFS="${WORK}/iso/genix/x86_64/airootfs.sfs"
if [[ -f "${SFS}" ]]; then
  for f in usr/local/bin/genix-install usr/bin/genix-install usr/bin/genix-rebuild usr/bin/genix-render opt/genix/install.sh; do
    MODE="$(unsquashfs -ll "${SFS}" 2>/dev/null | awk -v f="$f" 'index($NF, f) {print $1; exit}')"
    if [[ "${MODE}" != *x* ]]; then
      echo "${f} is not executable in ${SFS} (mode=${MODE:-missing})" >&2
      exit 1
    fi
  done
fi

sha256sum "${ISO}" | tee "${ISO}.sha256" >/dev/null

echo ""
echo "DONE: ${ISO}"
ls -lh "${ISO}" "${OUT}/genix-live.iso"
echo ""
echo "SHA256: $(awk '{print $1}' "${ISO}.sha256")"
echo "GitHub release: ./scripts/release-iso.sh --dry-run"
