#!/bin/bash
set -euo pipefail

# Deploy fastfetch config to a remote Genix/Gentoo host.
# usage: ./deploy-laptop.sh user@host

HOST="${1:?usage: $0 user@host}"
DIR="$(cd "$(dirname "$0")" && pwd)"

echo "Deploying Genix fastfetch to $HOST ..."

ssh -o ConnectTimeout=10 "$HOST" 'mkdir -p ~/.config/fastfetch'

scp "$DIR/ascii-art-40-440.txt" "$HOST:~/.config/fastfetch/"
scp "$DIR/config.jsonc" "$HOST:~/.config/fastfetch/config.jsonc"

ssh "$HOST" 'command -v fastfetch >/dev/null 2>&1 || echo "NOTE: fastfetch not installed — run: emerge -av app-misc/fastfetch"'

echo ""
echo "Done. On the remote host run:"
echo "  fastfetch"
echo ""
ssh "$HOST" 'fastfetch 2>/dev/null || true'
