#!/bin/sh

set -eu

WAKE="${1:+$1/wake}"
WAKE="${WAKE:-wake}"

rm -rf wake.db wake.log .cas cas-out.txt cas-written.txt dedup1.txt dedup2.txt

"${WAKE}" -x 'testFileInCas Unit'
"${WAKE}" -x 'testWriteInCas Unit'
"${WAKE}" -x 'testCasDedup Unit'

rm -rf wake.db wake.log cas-out.txt cas-written.txt dedup1.txt dedup2.txt

