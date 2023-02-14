#!/bin/bash
for i in $(cat targets.txt) ; do
    echo "-------------------------------------------"
    echo $i
    zig build $i
done
