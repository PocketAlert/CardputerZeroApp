#!/usr/bin/env bash
# Copies the newest .deb to the device and installs it.
#   CARDPUTER=me@192.168.1.42 ./deploy.sh
set -euo pipefail
cd "$(dirname "$0")"
target="${CARDPUTER:?set CARDPUTER=user@host}"
package="$(ls -t dist/pocketalert-cardputer_*_arm64.deb 2>/dev/null | head -n 1)"
[[ -n "$package" ]] || { echo "No package in dist/, run ./build-deb.sh first" >&2; exit 1; }
scp "$package" "$target:/tmp/"
# --reinstall: apt skips a package whose version is already installed, even if its files changed.
ssh -t "$target" "sudo apt install -y --reinstall /tmp/$(basename "$package") && rm -f /tmp/$(basename "$package") && dpkg-query -W pocketalert-cardputer"
