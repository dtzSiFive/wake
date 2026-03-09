#!/bin/sh

set -eu

WAKE="${1:+$1/wake}"
WAKE="${WAKE:-wake}"

rm -rf wake.db wake.log out.txt link.txt testdir file1.txt file2.txt diff1.txt diff2.txt written.txt reuse.txt

"${WAKE}" -x 'testFileHash Unit'
"${WAKE}" -x 'testSymlinkHash Unit'
"${WAKE}" -x 'testDirHash Unit'
"${WAKE}" -x 'testSameContentSameHash Unit'
"${WAKE}" -x 'testDiffContentDiffHash Unit'
"${WAKE}" -x 'testWriteHash Unit'
"${WAKE}" -x 'testHashReuse Unit'

rm -rf wake.db wake.log out.txt link.txt testdir file1.txt file2.txt diff1.txt diff2.txt written.txt reuse.txt

