#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
set -eu
cd "$(dirname "$0")/.."
exec python3 tests/run-tests.py --sdk "${AZ_SDK:-../../../../az-mods-sdk}" "$@"
