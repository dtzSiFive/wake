#!/bin/sh
# Two runs write different data to same file, with downstream jobs reading it.
# Check each run gets the expected data, and check reuse works as well.

WAKE="${1:+$1/wake}"
WAKE="${WAKE:-wake}"

# !!! For some reason NOT nuking .cas causes problems?
# rm -rf .cas
rm -f wake.db* wake.log output.txt result-a.txt result-b.txt

echo "Fresh concurrent runs:"

"${WAKE}" -x "consumerA Unit" &
"${WAKE}" -x "consumerB Unit" &

wait

tail output.txt result-a.txt result-b.txt

echo
echo "Reuse:"

"${WAKE}" -x "consumerA Unit" &
"${WAKE}" -x "consumerB Unit" &

wait

tail output.txt result-a.txt result-b.txt
