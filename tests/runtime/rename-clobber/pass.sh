#! /bin/sh

# Confirms current rename-over-directory behavior (reproduces cargo rsc bug):
# - Job A creates target/release/binary_a
# - Job B (no dep on A) does temp-dir-rename over target/
# - deep_unlink removes Job A's files
# - Downstream job fails trying to use Job A's output

set -eu

WAKE="${1:+$1/wake}"
WAKE="${WAKE:-wake}"

cleanup() {
    rm -rf target temp_target wake.db wake.log .fuse.log
}
trap cleanup EXIT

cleanup

# Run wake - the scenario should fail because job1's output is clobbered
# Either Wake detects duplicate outputs, or the downstream cat fails
if "${WAKE}" -v runScenario 2>&1; then
    echo "ERROR: expected wake to fail (clobbered output)" >&2
    exit 1
fi

# Verify clobbering happened: job1's binary should be gone
if [ -e target/release/binary_a ]; then
    echo "ERROR: job1's binary still exists (clobbering did not happen)" >&2
    exit 1
fi

# Verify job2's binary exists (rename succeeded at filesystem level)
if [ ! -e target/release/binary_b ]; then
    echo "ERROR: job2's binary missing" >&2
    exit 1
fi

echo "PASS: clobbering behavior confirmed"

