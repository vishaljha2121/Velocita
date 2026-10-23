#!/bin/sh
set -eu
cd "$(dirname "$0")"
sudo -v
sudo /usr/bin/python3 -B installer/install_disabled.py \
  --bundle "$PWD/new" --bundle-sha256 7dc71f35fba5bf4f54d74752c161efb60b016fc1c28e2d0a312b15b967b79a56 \
  --previous-bundle "$PWD/previous" --previous-bundle-sha256 a36fa90a90d06a3847457430543b3f581d1b782d241e0e453bc2125fd164082c --apply
