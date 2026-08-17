#!/bin/bash
# Bootstrap Gentoo Portage on an existing LFS system (Genix phase 1b).
# Run as root. Does NOT overwrite your LFS base — installs Portage + syncs tree only.
set -euo pipefail

export PATH=/usr/local/bin:/usr/bin:/usr/sbin:/bin:/sbin

PORTAGE_VERSION="${PORTAGE_VERSION:-3.0.81}"
PORTAGE_URL="https://github.com/gentoo/portage/archive/portage-${PORTAGE_VERSION}.tar.gz"
MESON_VERSION="${MESON_VERSION:-1.7.0}"
NINJA_VERSION="${NINJA_VERSION:-1.12.1}"
WORKDIR="${WORKDIR:-/var/tmp/genix-portage-bootstrap}"
CHOST="${CHOST:-x86_64-pc-linux-gnu}"
WGET="wget --no-check-certificate"

log() { printf '==> %s\n' "$*"; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

[[ "${EUID}" -eq 0 ]] || die "run as root"

for cmd in python3 wget tar xz gcc make; do
  command -v "${cmd}" >/dev/null 2>&1 || die "missing ${cmd}"
done

python3 -c 'import tomllib' 2>/dev/null || die "need Python 3.11+ (tomllib)"

if command -v emerge >/dev/null 2>&1; then
  log "Portage already installed: $(emerge --version | head -1)"
  log "Run: genix-portage-sync  OR  genix-rebuild switch --dry-run"
  exit 0
fi

mkdir -p "${WORKDIR}"
cd "${WORKDIR}"

ensure_ninja() {
  if command -v ninja >/dev/null 2>&1; then
    return
  fi
  log "Bootstrapping ninja ${NINJA_VERSION}..."
  if [[ ! -f "ninja-${NINJA_VERSION}.tar.gz" ]]; then
    ${WGET} -O "ninja-${NINJA_VERSION}.tar.gz" \
      "https://github.com/ninja-build/ninja/archive/v${NINJA_VERSION}.tar.gz"
  fi
  rm -rf "ninja-${NINJA_VERSION}"
  tar xf "ninja-${NINJA_VERSION}.tar.gz"
  cd "ninja-${NINJA_VERSION}"
  python3 configure.py --bootstrap
  install -m755 ninja /usr/local/bin/ninja
  cd "${WORKDIR}"
}

ensure_meson() {
  if command -v meson >/dev/null 2>&1; then
    MESON=meson
    return
  fi
  log "Installing meson ${MESON_VERSION} (python module)..."
  if [[ ! -f "meson-${MESON_VERSION}.tar.gz" ]]; then
    ${WGET} -O "meson-${MESON_VERSION}.tar.gz" \
      "https://github.com/mesonbuild/meson/releases/download/${MESON_VERSION}/meson-${MESON_VERSION}.tar.gz"
  fi
  rm -rf "meson-${MESON_VERSION}"
  tar xf "meson-${MESON_VERSION}.tar.gz"
  install -d /usr/local/lib/meson
  cp -a "meson-${MESON_VERSION}/meson" /usr/local/lib/meson/
  install -d /usr/local/bin
  cat > /usr/local/bin/meson << 'EOF'
#!/bin/bash
exec python3 /usr/local/lib/meson/meson.py "$@"
EOF
  chmod 755 /usr/local/bin/meson
  MESON=meson
}

if [[ ! -f "portage-${PORTAGE_VERSION}.tar.gz" ]]; then
  log "Downloading Portage ${PORTAGE_VERSION} from GitHub..."
  ${WGET} -O "portage-${PORTAGE_VERSION}.tar.gz" "${PORTAGE_URL}"
fi

rm -rf "portage-portage-${PORTAGE_VERSION}" portage-build
tar xf "portage-${PORTAGE_VERSION}.tar.gz"

ensure_ninja
ensure_meson

log "Building and installing Portage ${PORTAGE_VERSION}..."
cd "portage-portage-${PORTAGE_VERSION}"
meson setup --buildtype=plain -Dnative-extensions=false build
ninja -C build
ninja -C build install
cd "${WORKDIR}"

hash -r
command -v emerge >/dev/null 2>&1 || die "emerge not in PATH after install"

log "Setting up Portage directories..."
install -d /etc/portage/{repos.conf,profile,package.use,package.accept_keywords}
install -d /var/db/repos/gentoo
install -d /var/cache/binhost

if [[ -f /usr/share/portage/config/repos.conf ]]; then
  install -m644 /usr/share/portage/config/repos.conf /etc/portage/repos.conf/gentoo.conf
else
  cat > /etc/portage/repos.conf/gentoo.conf << 'EOF'
[DEFAULT]
main-repo = gentoo

[gentoo]
location = /var/db/repos/gentoo
sync-type = rsync
sync-uri = rsync://rsync.gentoo.org/gentoo-portage
auto-sync = no
EOF
fi

log "Writing /etc/portage/make.conf (Genix + LFS bootstrap)..."
install -d /etc/genix/rendered/portage
if [[ -f /etc/genix/configuration.toml ]]; then
  genix-render || true
fi

{
  echo "# Genix + LFS Portage bootstrap — edit /etc/genix/configuration.toml, then genix-render"
  echo "CHOST=\"${CHOST}\""
  echo 'CFLAGS="-O2 -pipe -march=native"'
  echo 'CXXFLAGS="${CFLAGS}"'
  echo 'FEATURES="parallel-fetch parallel-install collision-protect protect-owned"'
  echo 'EMERGE_DEFAULT_OPTS="--verbose --complete-graph --with-bdeps=y"'
  echo ""
  if [[ -f /etc/genix/rendered/portage/make.conf ]]; then
    grep -E '^(USE|ACCEPT_KEYWORDS|MAKEOPTS|FEATURES)=' /etc/genix/rendered/portage/make.conf || true
  else
    echo 'USE="-systemd -bluetooth pulseaudio X openssl"'
    echo 'ACCEPT_KEYWORDS="amd64"'
    echo 'MAKEOPTS="-j$(nproc)"'
  fi
} > /etc/portage/make.conf

log "Marking LFS base packages (do not auto-rebuild @system)..."
cat > /etc/portage/profile/packages << 'EOF'
sys-libs/glibc
sys-devel/gcc
sys-devel/binutils
sys-kernel/linux-headers
sys-apps/bash
sys-apps/coreutils
sys-apps/util-linux
sys-process/procps
sys-apps/diffutils
sys-devel/make
EOF

log "Syncing Gentoo ebuild tree (~46MB snapshot)..."
install -d /var/db/repos/gentoo
if [[ ! -f portage-latest.tar.xz ]]; then
  ${WGET} -O portage-latest.tar.xz https://distfiles.gentoo.org/snapshots/portage-latest.tar.xz
fi
rm -rf /var/db/repos/gentoo/*
tar xf portage-latest.tar.xz -C /var/db/repos/gentoo --strip-components=1

if command -v eselect >/dev/null 2>&1; then
  log "Available profiles (pick openrc amd64 no-multilib):"
  eselect profile list | tail -20 || true
else
  log "Install app-admin/eselect later to manage profiles"
fi

cat << EOF

Portage bootstrap complete.

Next steps:
  1. genix-render
  2. genix-rebuild switch --dry-run
  3. emerge -pv app-editors/vim
  4. genix-rebuild switch

WARNING:
  - Do NOT run: emerge @world  or  emerge -e @system
  - HTTPS fetch will fail until ca-certificates is installed

EOF
