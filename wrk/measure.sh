#! /usr/bin/env bash
THREADS=4
CONNECTIONS=400
DURATION_SECONDS=10

SUBJECT=$1


if [ "$SUBJECT" = "" ] ; then
    echo "usage: $0 subject    # subject: zig or go"
    exit 1
fi

if [ "$SUBJECT" = "zig" ] ; then
    zig build -Doptimize=ReleaseFast wrk > /dev/null
    ./zig-out/bin/wrk &
    PID=$!
    URL=http://127.0.0.1:3000
fi

if [ "$SUBJECT" = "zigstd" ] ; then
    zig build -Doptimize=ReleaseFast wrk_zigstd > /dev/null
    ./zig-out/bin/wrk_zigstd &
    PID=$!
    URL=http://127.0.0.1:3000
fi

if [ "$SUBJECT" = "go" ] ; then
    cd wrk/go && go build main.go 
    ./main &
    PID=$!
    URL=http://127.0.0.1:8090/hello
fi

if [ "$SUBJECT" = "python" ] ; then
    python wrk/python/main.py &
    PID=$!
    URL=http://127.0.0.1:8080
fi

if [ "$SUBJECT" = "sanic" ] ; then
    python wrk/sanic/sanic-app.py &
    PID=$!
    URL=http://127.0.0.1:8000
fi

if [ "$SUBJECT" = "rust" ] ; then
    cd wrk/rust/hello && cargo build --release
    ./target/release/hello &
    PID=$!
    URL=http://127.0.0.1:7878
fi

if [ "$SUBJECT" = "axum" ] ; then
    cd wrk/axum/hello-axum && cargo build --release
    ./target/release/hello-axum &
    PID=$!
    URL=http://127.0.0.1:3000
fi

if [ "$SUBJECT" = "csharp" ] ; then
    cd wrk/csharp && dotnet publish csharp.csproj -o ./out
    ./out/csharp --urls "http://127.0.0.1:5026" &
    PID=$!
    URL=http://127.0.0.1:5026
fi

if [ "$SUBJECT" = "cpp" ] ; then
    cd wrk/cpp && zig build -Doptimize=ReleaseFast
    ./zig-out/bin/cpp-beast 127.0.0.1 8070 . &
    PID=$!
    URL=http://127.0.0.1:8070
fi

sleep 1
echo "========================================================================"
echo "                          $SUBJECT"
echo "========================================================================"
wrk -c $CONNECTIONS -t $THREADS -d $DURATION_SECONDS --latency $URL 

kill $PID

