#! /bin/sh

# Tests desired behavior: Job A's files SURVIVE when Job B renames over the
# shared path. This fails with current code, would pass with CAS or a fix.
#
# Same scenario as rename-clobber/pass.sh but expects success (file survives).

set -eu

WAKE="${1:+$1/wake}"
WAKE="${WAKE:-wake}"

cleanup() {
    rm -rf target temp_target wake.db wake.log .fuse.log
}
trap cleanup EXIT

cleanup

# Run wake - should succeed if job1's output survives the rename
"${WAKE}" -v runScenario

