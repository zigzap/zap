# axum

```console
zap on  newwrk [$!?] via ↯ v0.11.0-dev.2837+b55b8e774 via  impure (nix-shell) 
➜ wrk/measure.sh axum
    Finished release [optimized] target(s) in 0.05s
========================================================================
                          axum
========================================================================
Running 10s test @ http://127.0.0.1:3000
  4 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   527.01us  260.08us   8.47ms   74.31%
    Req/Sec   151.11k     4.06k  166.63k    71.25%
  Latency Distribution
     50%  518.00us
     75%  644.00us
     90%  811.00us
     99%    1.39ms
  6014492 requests in 10.01s, 768.61MB read
Requests/sec: 600582.38
Transfer/sec:     76.75MB

zap on  newwrk [$!?] via ↯ v0.11.0-dev.2837+b55b8e774 via  impure (nix-shell) took 11s 
➜ wrk/measure.sh axum
    Finished release [optimized] target(s) in 0.05s
========================================================================
                          axum
========================================================================
Running 10s test @ http://127.0.0.1:3000
  4 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   534.89us  280.25us   7.37ms   76.81%
    Req/Sec   150.03k     4.26k  162.67k    72.75%
  Latency Distribution
     50%  520.00us
     75%  647.00us
     90%  831.00us
     99%    1.50ms
  5969526 requests in 10.01s, 762.86MB read
Requests/sec: 596134.58
Transfer/sec:     76.18MB

zap on  newwrk [$!?] via ↯ v0.11.0-dev.2837+b55b8e774 via  impure (nix-shell) took 11s 
➜ wrk/measure.sh axum
    Finished release [optimized] target(s) in 0.05s
========================================================================
                          axum
========================================================================
Running 10s test @ http://127.0.0.1:3000
  4 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   519.96us  269.86us  11.92ms   76.98%
    Req/Sec   151.29k     4.32k  164.52k    69.75%
  Latency Distribution
     50%  509.00us
     75%  635.00us
     90%  800.00us
     99%    1.41ms
  6021199 requests in 10.01s, 769.46MB read
Requests/sec: 601482.51
Transfer/sec:     76.86MB

zap on  newwrk [$!?] via ↯ v0.11.0-dev.2837+b55b8e774 via  impure (nix-shell) took 11s 
➜ 
```

# sanic


