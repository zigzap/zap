/* *****************************************************************************
This is a simple echo server example.

To try it out, compile using (avoids server state printout):

    FIO_PRINT=0 NAME=echo make

Than run:

    ./tmp/echo

To connect to this server run telnet, netcat or the client example, using:

    telnet localhost 3000
Or:
    nc localhost 3000

This example uses the core facil.io library (fio.h) and the Command Line
Interface library (fio_cli.h).
***************************************************************************** */

#include <fio.h>
#include <fio_cli.h>

/* *****************************************************************************
Echo connection callbacks
***************************************************************************** */

// A callback to be called whenever data is available on the socket
static void echo_on_data(intptr_t uuid, fio_protocol_s *prt) {
  // echo buffer
  char buffer[1024] = {'E', 'c', 'h', 'o', ':', ' '};
  ssize_t len;
  // Read to the buffer, starting after the "Echo: "
  while ((len = fio_read(uuid, buffer + 6, 1018)) > 0) {
    fprintf(stderr, "Read: %.*s", (int)len, buffer + 6);
    // Write back the message
    fio_write(uuid, buffer, len + 6);
    // Handle goodbye
    if ((buffer[6] | 32) == 'b' && (buffer[7] | 32) == 'y' &&
        (buffer[8] | 32) == 'e') {
      fio_write(uuid, "Goodbye.\n", 9);
      fio_close(uuid);
      return;
    }
  }
  (void)prt; // we can ignore the unused argument
}

// A callback called whenever a timeout is reach
static void echo_ping(intptr_t uuid, fio_protocol_s *prt) {
  fio_write(uuid, "Server: Are you there?\n", 23);
  (void)prt; // we can ignore the unused argument
}

// A callback called if the server is shutting down...
// ... while the connection is still open
static uint8_t echo_on_shutdown(intptr_t uuid, fio_protocol_s *prt) {
  fio_write(uuid, "Echo server shutting down\nGoodbye.\n", 35);
  return 0;
  (void)prt; // we can ignore the unused argument
}

static void echo_on_close(intptr_t uuid, fio_protocol_s *proto) {
  fprintf(stderr, "Connection %p closed.\n", (void *)proto);
  free(proto);
  (void)uuid;
}

/* *****************************************************************************
The main echo protocol creation callback
***************************************************************************** */

// A callback called for new connections
static void echo_on_open(intptr_t uuid, void *udata) {
  // Protocol objects MUST be dynamically allocated when multi-threading.
  fio_protocol_s *echo_proto = malloc(sizeof(*echo_proto));
  *echo_proto = (fio_protocol_s){.on_data = echo_on_data,
                                 .on_shutdown = echo_on_shutdown,
                                 .on_close = echo_on_close,
                                 .ping = echo_ping};
  fprintf(stderr, "New Connection %p received from %s\n", (void *)echo_proto,
          fio_peer_addr(uuid).data);
  fio_attach(uuid, echo_proto);
  fio_write2(uuid, .data.buffer = "Echo Service: Welcome\n", .length = 22,
             .after.dealloc = FIO_DEALLOC_NOOP);
  fio_timeout_set(uuid, 5);
  (void)udata; // ignore this
}

/* *****************************************************************************
The main function (listens to the `echo` connections and handles CLI)
***************************************************************************** */

// The main function starts listening to echo connections
int main(int argc, char const *argv[]) {
  /* Setup CLI arguments */
  fio_cli_start(argc, argv, 0, 0, "this example accepts the following options:",
                FIO_CLI_INT("-t -thread number of threads to run."),
                FIO_CLI_INT("-w -workers number of workers to run."),
                "-b, -address the address to bind to.",
                FIO_CLI_INT("-p,-port the port to bind to."),
                FIO_CLI_BOOL("-v -log enable logging."));

  /* Setup default values */
  fio_cli_set_default("-p", "3000");
  fio_cli_set_default("-t", "1");
  fio_cli_set_default("-w", "1");

  /* Listen for connections */
  if (fio_listen(.port = fio_cli_get("-p"), .on_open = echo_on_open) == -1) {
    perror("No listening socket available on port 3000");
    exit(-1);
  }
  /* Run the server and hang until a stop signal is received */
  fio_start(.threads = fio_cli_get_i("-t"), .workers = fio_cli_get_i("-w"));
}
