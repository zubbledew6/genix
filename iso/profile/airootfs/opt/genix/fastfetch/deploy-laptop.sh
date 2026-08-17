#!/bin/bash
set -euo pipefail

# LFS / Genix build laptop — SSH as zubb, then su - for install
HOST="${1:-zubb@192.168.68.63}"
DIR="$(cd "$(dirname "$0")" && pwd)"

echo "Deploying Genix fastfetch to $HOST ..."

ssh -o ConnectTimeout=10 "$HOST" 'mkdir -p ~/.config/fastfetch'

scp "$DIR/ascii-art-40-440.txt" "$HOST:~/.config/fastfetch/"
scp "$DIR/config.jsonc" "$HOST:~/.config/fastfetch/config.jsonc"

ssh "$HOST" 'command -v fastfetch >/dev/null 2>&1 || echo "NOTE: fastfetch not installed — run: emerge -av app-misc/fastfetch"'

echo ""
echo "Done. On the laptop run:"
echo "  fastfetch"
echo ""
ssh "$HOST" 'fastfetch 2>/dev/null || true'
