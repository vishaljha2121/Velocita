#!/bin/sh
set -eu
cd "$(dirname "$0")"
sudo -v
sudo /usr/bin/python3 -B installer/install_disabled.py --bundle "$PWD/new" --bundle-sha256 f784a41cd6b53c26c5f741e0ca3f96c2a58b53b85887d61aab51b84397400a34 --previous-bundle "$PWD/previous" --previous-bundle-sha256 7dc71f35fba5bf4f54d74752c161efb60b016fc1c28e2d0a312b15b967b79a56 --apply
