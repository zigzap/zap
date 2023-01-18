# other measurements

## zap wrk 'example' with and without logging

**NO** performance regressions observable:

With `logging=true`: 

```
[nix-shell:~/code/github.com/renerocksai/zap]$ ./wrk/measure.sh zig > out 2> /dev/null

[nix-shell:~/code/github.com/renerocksai/zap]$ cat out
========================================================================
                          zig
========================================================================
Running 10s test @ http://127.0.0.1:3000
  4 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   343.91us  286.75us  18.37ms   95.58%
    Req/Sec   162.61k     3.61k  174.96k    76.75%
  Latency Distribution
     50%  302.00us
     75%  342.00us
     90%  572.00us
     99%  697.00us
  6470789 requests in 10.01s, 0.96GB read
Requests/sec: 646459.59
Transfer/sec:     98.03MB
```

With `logging=false`:

```
[nix-shell:~/code/github.com/renerocksai/zap]$ ./wrk/measure.sh zig
Listening on 0.0.0.0:3000
========================================================================
                          zig
========================================================================
Running 10s test @ http://127.0.0.1:3000
  4 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   336.10us  122.28us  14.67ms   88.55%
    Req/Sec   159.82k     7.71k  176.75k    56.00%
  Latency Distribution
     50%  310.00us
     75%  343.00us
     90%  425.00us
     99%  699.00us
  6359415 requests in 10.01s, 0.94GB read
Requests/sec: 635186.96
Transfer/sec:     96.32MB
```
