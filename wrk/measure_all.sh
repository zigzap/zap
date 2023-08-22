#! /usr/bin/env bash

if [ ! -d ".git" ] ; then
    echo "This script must be run from the root directory of the repository!"
    echo "./wrk/measure_all.sh"
    exit 1
fi

SUBJECTS="zig go python sanic rust-bythebook rust-clean axum csharp cpp-beast"

rm -f wrk/*.perflog

for S in $SUBJECTS; do
    L="$S.perflog"
    for R in 1 2 3 ; do
        ./wrk/measure.sh $S | tee -a wrk/$L
    done
done

echo "Finished"
