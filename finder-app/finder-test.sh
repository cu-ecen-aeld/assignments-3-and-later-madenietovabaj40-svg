#!/bin/sh
# Tester script for assignment 4
# Author: Abay

set -e
set -u

NUMFILES=10
WRITESTR="AELD_IS_FUN"
WRITEDIR="/tmp/aeld-data"
username=$(cat /etc/finder-app/conf/username.txt)

if [ $# -ge 1 ]
then
	NUMFILES=$1
fi
if [ $# -ge 2 ]
then
	WRITESTR=$2
fi

CLEAN_WRITESTR=$(echo "$WRITESTR" | tr -d '\r')

echo "Writing $NUMFILES files containing $CLEAN_WRITESTR to $WRITEDIR"

rm -rf "$WRITEDIR"
mkdir -p "$WRITEDIR"

for i in $(seq 1 $NUMFILES)
do
	# Вызываем writer БЕЗ ./ (так как он будет в /usr/bin)
	writer "$WRITEDIR/${username}$i" "$CLEAN_WRITESTR"
done

# Вызываем finder.sh БЕЗ ./ и пишем результат в /tmp/assignment4-result.txt
finder.sh "$WRITEDIR" "$CLEAN_WRITESTR" > /tmp/assignment4-result.txt

EXPECTED_STR="The number of files are ${NUMFILES} and the number of matching lines are ${NUMFILES}"

if grep -q "$EXPECTED_STR" /tmp/assignment4-result.txt; then
    echo "success"
    exit 0
else
    echo "Failed!"
    exit 1
fi
