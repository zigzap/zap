# ⚡blazingly fast⚡

I conducted a series of quick tests, using wrk with simple HTTP servers written
in GO and in zig zap. 

Just to get some sort of indication, I also included measurements for python
since I used to write my REST APIs in python before creating zig zap.

You can check out the scripts I used for the tests in [./wrk](wrk/).

## results

You can see the verbatim output of `wrk`, and some infos about the test machine
below the code snippets.

### requests / sec
![](wrk_requests.png)

### transfer MB / sec

![](wrk_transfer.png)


## zig code 

zig version .11.0-dev.1265+3ab43988c

```zig 
const std = @import("std");
const zap = @import("zap");

fn on_request_minimal(r: zap.SimpleRequest) void {
    _ = r.sendBody("<html><body><h1>Hello from ZAP!!!</h1></body></html>");
}

pub fn main() !void {
    var listener = zap.SimpleHttpListener.init(.{
        .port = 3000,
        .on_request = on_request_minimal,
        .log = false,
        .max_clients = 100000,
    });
    try listener.listen();

    std.debug.print("Listening on 0.0.0.0:3000\n", .{});

    // start worker threads
    zap.start(.{
        .threads = 4,
        .workers = 4,
    });
}
```

## go code 

go version go1.16.9 linux/amd64

```go 
package main

import (
	"fmt"
	"net/http"
)

func hello(w http.ResponseWriter, req *http.Request) {
	fmt.Fprintf(w, "hello from GO!!!\n")
}

func main() {
	print("listening on 0.0.0.0:8090\n")
	http.HandleFunc("/hello", hello)
	http.ListenAndServe(":8090", nil)
}
```

## python code

python version 3.9.6

```python 
# Python 3 server example
from http.server import BaseHTTPRequestHandler, HTTPServer

hostName = "127.0.0.1"
serverPort = 8080


class MyServer(BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-type", "text/html")
        self.end_headers()
        self.wfile.write(bytes("HELLO FROM PYTHON!!!", "utf-8"))

    def log_message(self, format, *args):
        return


if __name__ == "__main__":
    webServer = HTTPServer((hostName, serverPort), MyServer)
    print("Server started http://%s:%s" % (hostName, serverPort))

    try:
        webServer.serve_forever()
    except KeyboardInterrupt:
        pass

    webServer.server_close()
    print("Server stopped.")
```

## wrk output

wrk version: `wrk 4.1.0 [epoll] Copyright (C) 2012 Will Glozer`

```
========================================================================
                          zig
========================================================================
Running 10s test @ http://127.0.0.1:3000
  4 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   337.49us  109.24us   4.21ms   88.64%
    Req/Sec   157.78k     8.31k  172.93k    59.25%
  Latency Distribution
     50%  314.00us
     75%  345.00us
     90%  396.00us
     99%  699.00us
  6280964 requests in 10.01s, 1.13GB read
Requests/sec: 627277.99
Transfer/sec:    116.05MB


(base) rs@ryzen:~/code/github.com/renerocksai/zap$ ./wrk/measure.sh zig
Listening on 0.0.0.0:3000
========================================================================
                          zig
========================================================================
Running 10s test @ http://127.0.0.1:3000
  4 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   339.50us  176.93us  12.52ms   91.23%
    Req/Sec   158.24k     8.00k  170.45k    53.00%
  Latency Distribution
     50%  313.00us
     75%  345.00us
     90%  399.00us
     99%  697.00us
  6297146 requests in 10.02s, 1.14GB read
Requests/sec: 628768.81
Transfer/sec:    116.33MB


(base) rs@ryzen:~/code/github.com/renerocksai/zap$ ./wrk/measure.sh zig
Listening on 0.0.0.0:3000
========================================================================
                          zig
========================================================================
Running 10s test @ http://127.0.0.1:3000
  4 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   344.23us  185.46us  14.44ms   89.93%
    Req/Sec   157.56k    12.85k  171.21k    94.00%
  Latency Distribution
     50%  312.00us
     75%  346.00us
     90%  528.00us
     99%  747.00us
  6270913 requests in 10.02s, 1.13GB read
Requests/sec: 625756.37
Transfer/sec:    115.77MB



(base) rs@ryzen:~/code/github.com/renerocksai/zap$ ./wrk/measure.sh go
listening on 0.0.0.0:8090
========================================================================
                          go
========================================================================
Running 10s test @ http://127.0.0.1:8090/hello
  4 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   721.60us  755.26us  14.12ms   87.95%
    Req/Sec   123.79k     4.65k  135.57k    69.00%
  Latency Distribution
     50%  429.00us
     75%    0.88ms
     90%    1.65ms
     99%    3.63ms
  4927352 requests in 10.02s, 629.68MB read
Requests/sec: 491607.70
Transfer/sec:     62.82MB


(base) rs@ryzen:~/code/github.com/renerocksai/zap$ ./wrk/measure.sh go
listening on 0.0.0.0:8090
========================================================================
                          go
========================================================================
Running 10s test @ http://127.0.0.1:8090/hello
  4 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   698.35us  707.23us  11.03ms   87.80%
    Req/Sec   124.36k     4.27k  135.19k    69.50%
  Latency Distribution
     50%  419.00us
     75%    0.86ms
     90%    1.58ms
     99%    3.38ms
  4948380 requests in 10.01s, 632.37MB read
Requests/sec: 494338.77
Transfer/sec:     63.17MB


(base) rs@ryzen:~/code/github.com/renerocksai/zap$ ./wrk/measure.sh go
listening on 0.0.0.0:8090
========================================================================
                          go
========================================================================
Running 10s test @ http://127.0.0.1:8090/hello
  4 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   700.97us  710.99us  10.38ms   87.73%
    Req/Sec   124.38k     4.16k  135.31k    67.25%
  Latency Distribution
     50%  419.00us
     75%    0.86ms
     90%    1.59ms
     99%    3.39ms
  4950585 requests in 10.01s, 632.65MB read
Requests/sec: 494551.24
Transfer/sec:     63.20MB



(base) rs@ryzen:~/code/github.com/renerocksai/zap$ ./wrk/measure.sh python
Server started http://127.0.0.1:8080
========================================================================
                          python
========================================================================
Running 10s test @ http://127.0.0.1:8080
  4 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     4.55ms   57.21ms   1.66s    99.12%
    Req/Sec     2.56k     2.55k    7.57k    57.43%
  Latency Distribution
     50%  216.00us
     75%  343.00us
     90%  371.00us
     99%  765.00us
  27854 requests in 10.02s, 3.61MB read
  Socket errors: connect 0, read 27854, write 0, timeout 8
Requests/sec:   2779.87
Transfer/sec:    369.20KB


(base) rs@ryzen:~/code/github.com/renerocksai/zap$ ./wrk/measure.sh python
Server started http://127.0.0.1:8080
========================================================================
                          python
========================================================================
Running 10s test @ http://127.0.0.1:8080
  4 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     4.08ms   58.22ms   1.66s    99.27%
    Req/Sec     2.28k     2.13k    7.68k    50.00%
  Latency Distribution
     50%  226.00us
     75%  345.00us
     90%  374.00us
     99%  496.00us
  55353 requests in 10.03s, 7.18MB read
  Socket errors: connect 0, read 55353, write 0, timeout 8
Requests/sec:   5521.48
Transfer/sec:    733.33KB


(base) rs@ryzen:~/code/github.com/renerocksai/zap$ ./wrk/measure.sh python
Server started http://127.0.0.1:8080
========================================================================
                          python
========================================================================
Running 10s test @ http://127.0.0.1:8080
  4 threads and 400 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     6.05ms   70.02ms   1.66s    98.94%
    Req/Sec     2.40k     2.35k    7.43k    54.40%
  Latency Distribution
     50%  222.00us
     75%  313.00us
     90%  366.00us
     99%  162.93ms
  31959 requests in 10.02s, 4.15MB read
  Socket errors: connect 0, read 31959, write 0, timeout 6
Requests/sec:   3189.81
Transfer/sec:    423.65KB
```

## test machine

```
          ▗▄▄▄       ▗▄▄▄▄    ▄▄▄▖            rs@ryzen 
          ▜███▙       ▜███▙  ▟███▛            -------- 
           ▜███▙       ▜███▙▟███▛             OS: NixOS 22.05 (Quokka) x86_64 
            ▜███▙       ▜██████▛              Host: Micro-Star International Co., Ltd. B550-A PRO (MS-7C56) 
     ▟█████████████████▙ ▜████▛     ▟▙        Kernel: 6.0.15 
    ▟███████████████████▙ ▜███▙    ▟██▙       Uptime: 7 days, 5 hours, 29 mins 
           ▄▄▄▄▖           ▜███▙  ▟███▛       Packages: 5950 (nix-system), 893 (nix-user), 5 (flatpak) 
          ▟███▛             ▜██▛ ▟███▛        Shell: bash 5.1.16 
         ▟███▛               ▜▛ ▟███▛         Resolution: 3840x2160 
▟███████████▛                  ▟██████████▙   DE: none+i3 
▜██████████▛                  ▟███████████▛   WM: i3 
      ▟███▛ ▟▙               ▟███▛            Terminal: Neovim Terminal 
     ▟███▛ ▟██▙             ▟███▛             CPU: AMD Ryzen 5 5600X (12) @ 3.700GHz 
    ▟███▛  ▜███▙           ▝▀▀▀▀              GPU: AMD ATI Radeon RX 6700/6700 XT / 6800M 
    ▜██▛    ▜███▙ ▜██████████████████▛        Memory: 10378MiB / 32033MiB 
     ▜▛     ▟████▙ ▜████████████████▛
           ▟██████▙       ▜███▙                                       
          ▟███▛▜███▙       ▜███▙                                      
         ▟███▛  ▜███▙       ▜███▙
         ▝▀▀▀    ▀▀▀▀▘       ▀▀▀▘
```

