#!/usr/bin/env bash
set -euo pipefail
install -Dm644 /etc/os-release /usr/lib/os-release
# invoked via bash: the airootfs copy drops execute bits
bash /opt/genix/install.sh
chmod 755 /opt/genix/install.sh /opt/genix/bin/* 2>/dev/null || true
chmod 755 /usr/bin/genix-install /usr/local/bin/genix-install /usr/bin/genix-rebuild /usr/bin/genix-render 2>/dev/null || true
