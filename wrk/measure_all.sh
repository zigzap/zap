#! /usr/bin/env bash

if [ ! -d ".git" ] ; then
    echo "This script must be run from the root directory of the repository!"
    echo "./wrk/measure_all.sh"
    exit 1
fi

SUBJECTS="$1"

if [ "$SUBJECTS" = "README" ] ; then 
    rm -f wrk/*.perflog
    SUBJECTS="zig-zap go python-sanic rust-axum csharp cpp-beast"
fi

if [ -z "$SUBJECTS" ] ; then
    SUBJECTS="zig-zap go python python-sanic rust-bythebook rust-bythebook-improved rust-clean rust-axum csharp cpp-beast"
fi

for S in $SUBJECTS; do
    L="$S.perflog"
    rm -f wrk/$L
    for R in 1 2 3 ; do
        ./wrk/measure.sh $S | tee -a wrk/$L
    done
done

echo "Finished"
