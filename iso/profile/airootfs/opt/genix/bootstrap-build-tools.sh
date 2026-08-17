#!/bin/bash
# Install cmake/ninja into /usr/local for LFS + Portage --nodeps builds.
# Run as root once. Does not touch glibc/gcc.
set -euo pipefail

export PATH=/usr/local/bin:/usr/bin:/usr/sbin:/bin:/sbin

CMAKE_VERSION="${CMAKE_VERSION:-3.31.6}"
NINJA_VERSION="${NINJA_VERSION:-1.12.1}"
WORKDIR="${WORKDIR:-/var/tmp/genix-build-tools}"
WGET="wget --no-check-certificate"

log() { printf '==> %s\n' "$*"; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

[[ "${EUID}" -eq 0 ]] || die "run as root"

for cmd in gcc g++ make python3 wget tar; do
  command -v "${cmd}" >/dev/null 2>&1 || die "missing ${cmd}"
done

mkdir -p "${WORKDIR}"
cd "${WORKDIR}"

ensure_ninja() {
  if command -v ninja >/dev/null 2>&1; then
    log "ninja ok: $(command -v ninja)"
    return
  fi
  log "building ninja ${NINJA_VERSION}..."
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

ensure_cmake() {
  if command -v cmake >/dev/null 2>&1; then
    log "cmake ok: $(cmake --version | head -1)"
    return
  fi
  log "building cmake ${CMAKE_VERSION} (this takes a while)..."
  if [[ ! -f "cmake-${CMAKE_VERSION}.tar.gz" ]]; then
    ${WGET} -O "cmake-${CMAKE_VERSION}.tar.gz" \
      "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}.tar.gz"
  fi
  rm -rf "cmake-${CMAKE_VERSION}"
  tar xf "cmake-${CMAKE_VERSION}.tar.gz"
  cd "cmake-${CMAKE_VERSION}"
  ./bootstrap --prefix=/usr/local --parallel="$(nproc)"
  make -j"$(nproc)"
  make install
  cd "${WORKDIR}"
}

ensure_ninja
ensure_cmake

hash -r
command -v cmake >/dev/null 2>&1 || die "cmake missing after install"
command -v ninja >/dev/null 2>&1 || die "ninja missing after install"

cat << EOF

Build tools ready in /usr/local/bin:
  cmake $(cmake --version | head -1)
  ninja $(ninja --version)

Packages like cmatrix / fortune-mod should build with:
  FEATURES="-collision-protect" emerge -1 --nodeps -av <atom>

Add to PATH permanently:
  echo 'export PATH=/usr/local/bin:/usr/bin:/usr/sbin:/bin:/sbin' > /etc/profile.d/portage.sh

EOF
