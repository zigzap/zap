#! /usr/bin/env bash
SUBJECTS="zig go python sanic rust rust2 axum csharp cpp"

for S in $SUBJECTS; do
    L="$S.perflog"
    for R in 1 2 3 ; do
        ./wrk/measure.sh $S | tee -a $L
    done
done
