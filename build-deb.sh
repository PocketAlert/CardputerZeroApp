#!/usr/bin/env bash
# Builds dist/pocketalert-cardputer_<version>-<revision>_arm64.deb inside Debian trixie arm64,
# after the unit tests pass in the same container.
#   ./build-deb.sh              dev build: revision 1+dev<timestamp>, newer than the last one,
#                               so apt on the device always upgrades to it
#   RELEASE=1 ./build-deb.sh    release build: revision 1
set -euo pipefail
cd "$(dirname "$0")"
if [[ "${RELEASE:-}" == 1 ]]; then
  revision=1
else
  revision="1+dev$(date +%Y%m%d%H%M)"
fi
# Old builds would pile up and confuse deploy.sh, which takes the newest file.
rm -f dist/pocketalert-cardputer_*_arm64.deb
docker build -q -t pocketalert-cardputer-build packaging >/dev/null
docker run --rm --platform=linux/arm64 -v "$PWD":/src -w /src -e REVISION="$revision" pocketalert-cardputer-build \
  bash -c 'set -e
    cmake --preset cp0-docker -DPA_BUILD_TESTS=ON -DAPP_DEBIAN_REVISION="$REVISION" >/dev/null
    cmake --build --preset cp0-docker-rel
    build/cp0-docker/Release/pa_tests
    cd build/cp0-docker && cpack -C Release'
ls -1 dist/*.deb
