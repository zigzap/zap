/*
Copyright: Boaz segev, 2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#ifndef H_FIO_CLI_HELPER_H
#define H_FIO_CLI_HELPER_H

/* support C++ */
#ifdef __cplusplus
extern "C" {
#endif

/* *****************************************************************************
CLI API
***************************************************************************** */

/** Indicates the CLI argument should be a String (default). */
#define FIO_CLI_STRING(line)
/** Indicates the CLI argument is a Boolean value. */
#define FIO_CLI_BOOL(line)
/** Indicates the CLI argument should be an Integer (numerical). */
#define FIO_CLI_INT(line)
/** Indicates the CLI string should be printed as is. */
#define FIO_CLI_PRINT(line)
/** Indicates the CLI string should be printed as a header. */
#define FIO_CLI_PRINT_HEADER(line)

/**
 * This function parses the Command Line Interface (CLI), creating a temporary
 * "dictionary" that allows easy access to the CLI using their names or aliases.
 *
 * Command line arguments may be typed. If an optional type requirement is
 * provided and the provided arument fails to match the required type, execution
 * will end and an error message will be printed along with a short "help".
 *
 * The function / macro accepts the following arguments:
 * - `argc`: command line argument count.
 * - `argv`: command line argument list (array).
 * - `unnamed_min`: the required minimum of un-named arguments.
 * - `unnamed_max`: the maximum limit of un-named arguments.
 * - `description`: a C string containing the program's description.
 * - named arguments list: a list of C strings describing named arguments.
 *
 * The following optional type requirements are:
 *
 * * FIO_CLI_STRING(desc_line)       - (default) string argument.
 * * FIO_CLI_BOOL(desc_line)         - boolean argument (no value).
 * * FIO_CLI_INT(desc_line)          - integer argument.
 * * FIO_CLI_PRINT_HEADER(desc_line) - extra header for output.
 * * FIO_CLI_PRINT(desc_line)        - extra information for output.
 *
 * Argument names MUST start with the '-' character. The first word starting
 * without the '-' character will begin the description for the CLI argument.
 *
 * The arguments "-?", "-h", "-help" and "--help" are automatically handled
 * unless overridden.
 *
 * Un-named arguments shouldn't be listed in the named arguments list.
 *
 * Example use:
 *
 *    fio_cli_start(argc, argv, 0, 0, "this example accepts the following:",
 *                  FIO_CLI_PRINT_HREADER("Concurrency:"),
 *                  FIO_CLI_INT("-t -thread number of threads to run."),
 *                  FIO_CLI_INT("-w -workers number of workers to run."),
 *                  FIO_CLI_PRINT_HREADER("Address Binding:"),
 *                  "-b, -address the address to bind to.",
 *                  FIO_CLI_INT("-p,-port the port to bind to."),
 *                  FIO_CLI_PRINT("\t\tset port to zero (0) for Unix s."),
 *                  FIO_CLI_PRINT_HREADER("Logging:"),
 *                  FIO_CLI_BOOL("-v -log enable logging."));
 *
 *
 * This would allow access to the named arguments:
 *
 *      fio_cli_get("-b") == fio_cli_get("-address");
 *
 *
 * Once all the data was accessed, free the parsed data dictionary using:
 *
 *      fio_cli_end();
 *
 * It should be noted, arguments will be recognized in a number of forms, i.e.:
 *
 *      app -t=1 -p3000 -a localhost
 *
 * This function is NOT thread safe.
 */
#define fio_cli_start(argc, argv, unnamed_min, unnamed_max, description, ...)  \
  fio_cli_start((argc), (argv), (unnamed_min), (unnamed_max), (description),   \
                (char const *[]){__VA_ARGS__, NULL})
#define FIO_CLI_IGNORE
/**
 * Never use the function directly, always use the MACRO, because the macro
 * attaches a NULL marker at the end of the `names` argument collection.
 */
void fio_cli_start FIO_CLI_IGNORE(int argc, char const *argv[], int unnamed_min,
                                  int unnamed_max, char const *description,
                                  char const **names);
/**
 * Clears the memory used by the CLI dictionary, removing all parsed data.
 *
 * This function is NOT thread safe.
 */
void fio_cli_end(void);

/** Returns the argument's value as a NUL terminated C String. */
char const *fio_cli_get(char const *name);

/** Returns the argument's value as an integer. */
int fio_cli_get_i(char const *name);

/** This MACRO returns the argument's value as a boolean. */
#define fio_cli_get_bool(name) (fio_cli_get((name)) != NULL)

/** Returns the number of unnamed argument. */
unsigned int fio_cli_unnamed_count(void);

/** Returns the unnamed argument using a 0 based `index`. */
char const *fio_cli_unnamed(unsigned int index);

/**
 * Sets the argument's value as a NUL terminated C String (no copy!).
 *
 * CAREFUL: This does not automatically detect aliases or type violations! it
 * will only effect the specific name given, even if invalid. i.e.:
 *
 *     fio_cli_start(argc, argv,
 *                  "this is example accepts the following options:",
 *                  "-p -port the port to bind to", FIO_CLI_INT;
 *
 *     fio_cli_set("-p", "hello"); // fio_cli_get("-p") != fio_cli_get("-port");
 *
 * Note: this does NOT copy the C strings to memory. Memory should be kept alive
 *       until `fio_cli_end` is called.
 *
 * This function is NOT thread safe.
 */
void fio_cli_set(char const *name, char const *value);

/**
 * This MACRO is the same as:
 *
 *     if(!fio_cli_get(name)) {
 *       fio_cli_set(name, value)
 *     }
 *
 * See fio_cli_set for notes and restrictions.
 */
#define fio_cli_set_default(name, value)                                       \
  if (!fio_cli_get((name)))                                                    \
    fio_cli_set(name, value);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* *****************************************************************************
Internal Macro Implementation
***************************************************************************** */

/** Used internally. */
#define FIO_CLI_STRING__TYPE_I 0x1
#define FIO_CLI_BOOL__TYPE_I 0x2
#define FIO_CLI_INT__TYPE_I 0x3
#define FIO_CLI_PRINT__TYPE_I 0x4
#define FIO_CLI_PRINT_HEADER__TYPE_I 0x5

#undef FIO_CLI_STRING
#undef FIO_CLI_BOOL
#undef FIO_CLI_INT
#undef FIO_CLI_PRINT
#undef FIO_CLI_PRINT_HEADER

/** Indicates the CLI argument should be a String (default). */
#define FIO_CLI_STRING(line) (line), ((char *)FIO_CLI_STRING__TYPE_I)
/** Indicates the CLI argument is a Boolean value. */
#define FIO_CLI_BOOL(line) (line), ((char *)FIO_CLI_BOOL__TYPE_I)
/** Indicates the CLI argument should be an Integer (numerical). */
#define FIO_CLI_INT(line) (line), ((char *)FIO_CLI_INT__TYPE_I)
/** Indicates the CLI string should be printed as is. */
#define FIO_CLI_PRINT(line) (line), ((char *)FIO_CLI_PRINT__TYPE_I)
/** Indicates the CLI string should be printed as a header. */
#define FIO_CLI_PRINT_HEADER(line)                                             \
  (line), ((char *)FIO_CLI_PRINT_HEADER__TYPE_I)

#endif
