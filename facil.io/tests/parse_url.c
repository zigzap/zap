#include "fio.h"

int main(int argc, char const *argv[]) {
  if (argc < 2)
    return -1;
  fio_url_s u = fio_url_parse(argv[1], strlen(argv[1]));
  fprintf(stderr,
          "Parsed URL:\n"
          "\tscheme:\t %.*s\n"
          "\tuser:\t%.*s\n"
          "\tpass:\t%.*s\n"
          "\thost:\t%.*s\n"
          "\tport:\t%.*s\n"
          "\tpath:\t%.*s\n"
          "\tquery:\t%.*s\n"
          "\ttarget:\t%.*s\n",
          (int)u.scheme.len, u.scheme.data, (int)u.user.len, u.user.data,
          (int)u.password.len, u.password.data, (int)u.host.len, u.host.data,
          (int)u.port.len, u.port.data, (int)u.path.len, u.path.data,
          (int)u.query.len, u.query.data, (int)u.target.len, u.target.data);
  return 0;
}
