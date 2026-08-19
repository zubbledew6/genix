#!/bin/bash
set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
  echo "run as root" >&2
  exit 1
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Always rebuild. A copied build/ from a newer CPU (CachyOS -march=native)
# dies here with "CPU ISA level is lower than required".
echo "building genix..."
make -C "${ROOT}" clean
make -C "${ROOT}"

install -d /usr/lib/genix /usr/bin /etc/genix /var/lib/genix/generations
install -m755 "${ROOT}/build/genix-render" /usr/bin/genix-render
install -m755 "${ROOT}/build/genix-rebuild" /usr/bin/genix-rebuild
install -m644 "${ROOT}/lib/genix/installer.py" /usr/lib/genix/installer.py
install -m755 "${ROOT}/bin/genix-install" /usr/bin/genix-install

if [[ ! -f /etc/genix/configuration.toml ]]; then
  install -m644 "${ROOT}/example/configuration.toml" /etc/genix/configuration.toml.example
  cp -n "${ROOT}/example/configuration.toml" /etc/genix/configuration.toml
else
  install -m644 "${ROOT}/example/configuration.toml" /etc/genix/configuration.toml.example
fi

if [[ -d "${ROOT}/fastfetch" ]]; then
  for home in /root /etc/skel; do
    install -d "${home}/.config/fastfetch"
    install -m644 "${ROOT}/fastfetch/ascii-art-40-440.txt" "${home}/.config/fastfetch/"
    install -m644 "${ROOT}/fastfetch/config.jsonc" "${home}/.config/fastfetch/config.jsonc"
  done
fi

python3 -c 'import tomllib' 2>/dev/null || {
  echo "need python 3.11+ for genix-install" >&2
  exit 1
}

echo "installed"
echo "  edit /etc/genix/configuration.toml"
echo "  genix-rebuild switch"
echo "  genix-install          (fresh install from live USB)"
