#!/bin/bash
# Everything: host tests (plain and under ASan/UBSan), mutation check, emulator tests.
#   md/test/all.sh          the lot (about 6 minutes)
#   md/test/all.sh quick    host tests only
set -euo pipefail
cd "$(dirname "$0")/../.."
md/test/run.sh
SANITIZE=1 md/test/run.sh
[ "${1:-}" = quick ] && exit 0
python3 md/test/mutate.py
python3 md/test/e2e.py
