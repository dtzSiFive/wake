#! /bin/sh

set -eu

WAKE="${1:+$1/wake}"
WAKE="${WAKE:-wake}"

rm -f wake.db

"${WAKE}" --history
