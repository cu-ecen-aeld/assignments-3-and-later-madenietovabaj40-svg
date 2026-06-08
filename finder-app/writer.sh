#!/bin/sh
if [ $# -ne 2 ]; then
    echo "Error: Two arguments required."
    exit 1
fi
writefile=$1
writestr=$2
dest_dir=$(dirname "$writefile")
mkdir -p "$dest_dir"
if ! echo "$writestr" > "$writefile" 2>/dev/null; then
    echo "Error: Could not create file."
    exit 1
fi
exit 0