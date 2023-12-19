/*
Copyright: Boaz Segev, 2017-2019
License: MIT
*/
#if !defined(H_FIOBJ_IO_H) && (defined(__unix__) || defined(__APPLE__) ||      \
                               defined(__linux__) || defined(__CYGWIN__))

/**
 * A dynamic type for reading / writing to a local file,  a temporary file or an
 * in-memory string.
 *
 * Supports basic reak, write, seek, puts and gets operations.
 *
 * Writing is always performed at the end of the stream / memory buffer,
 * ignoring the current seek position.
 */
#define H_FIOBJ_IO_H

#include <fiobject.h>

#ifdef __cplusplus
extern "C" {
#endif

/* *****************************************************************************
Creating the Data Stream object
***************************************************************************** */

/** Creates a new local in-memory Data Stream object */
FIOBJ fiobj_data_newstr(void);

/**
 * Creates a Data object from an existing buffer. The buffer will be deallocated
 * using the provided `dealloc` function pointer. Use a NULL `dealloc` function
 * pointer if the buffer is static and shouldn't be freed.
 */
FIOBJ fiobj_data_newstr2(void *buffer, uintptr_t length,
                         void (*dealloc)(void *));

/** Creates a new local tempfile Data Stream object */
FIOBJ fiobj_data_newtmpfile(void);

/** Creates a new local file Data Stream object */
FIOBJ fiobj_data_newfd(int fd);

/** Creates a slice from an existing Data object. */
FIOBJ fiobj_data_slice(FIOBJ parent, intptr_t offset, uintptr_t length);

/* *****************************************************************************
Saving the Data Stream object
***************************************************************************** */

/** Creates a new local file Data Stream object */
int fiobj_data_save(FIOBJ io, const char *filename);

/* *****************************************************************************
Reading API
***************************************************************************** */

/**
 * Reads up to `length` bytes and returns a temporary(!) buffer object (not NUL
 * terminated).
 *
 * If `length` is zero or negative, it will be computed from the end of the
 * input backwards (0 == EOF).
 *
 * The C string object will be invalidate the next time a function call to the
 * Data Stream object is made.
 */
fio_str_info_s fiobj_data_read(FIOBJ io, intptr_t length);

/**
 * Reads until the `token` byte is encountered or until the end of the stream.
 *
 * Returns a temporary(!) C string including the end of line marker.
 *
 * Careful when using this call on large file streams, as the whole file
 * stream might be loaded into the memory.
 *
 * The C string object will be invalidate the next time a function call to the
 * Data Stream object is made.
 */
fio_str_info_s fiobj_data_read2ch(FIOBJ io, uint8_t token);

/**
 * Reads a line (until the '\n' byte is encountered) or until the end of the
 * available data.
 *
 * Returns a temporary(!) buffer object (not NUL terminated) including the end
 * of line marker.
 *
 * Careful when using this call on large file streams, as the whole file stream
 * might be loaded into the memory.
 *
 * The C string object will be invalidate the next time a function call to the
 * Data Stream object is made.
 */
#define fiobj_data_gets(io) fiobj_data_read2ch((io), '\n');

/**
 * Returns the current reading position. Returns -1 on error.
 */
intptr_t fiobj_data_pos(FIOBJ io);

/**
 * Returns the length of the stream.
 */
intptr_t fiobj_data_len(FIOBJ io);

/**
 * Moves the reading position to the requested position.
 */
void fiobj_data_seek(FIOBJ io, intptr_t position);

/**
 * Reads up to `length` bytes starting at `start_at` position and returns a
 * temporary(!) buffer object (not NUL terminated) string object. The reading
 * position is ignored and unchanged.
 *
 * The C string object will be invalidate the next time a function call to the
 * Data Stream object is made.
 */
fio_str_info_s fiobj_data_pread(FIOBJ io, intptr_t start_at, uintptr_t length);

/* *****************************************************************************
Writing API
***************************************************************************** */

/**
 * Writes `length` bytes at the end of the Data Stream stream, ignoring the
 * reading position.
 *
 * Behaves and returns the same value as the system call `write`.
 */
intptr_t fiobj_data_write(FIOBJ io, void *buffer, uintptr_t length);

/**
 * Writes `length` bytes at the end of the Data Stream stream, ignoring the
 * reading position, adding an EOL marker ("\r\n") to the end of the stream.
 *
 * Behaves and returns the same value as the system call `write`.
 */
intptr_t fiobj_data_puts(FIOBJ io, void *buffer, uintptr_t length);

/**
 * Makes sure the Data Stream object isn't attached to a static or external
 * string.
 *
 * If the Data Stream object is attached to a static or external string, the
 * data will be copied to a new memory block.
 *
 * If the Data Stream object is a slice from another Data Stream object, the
 * data will be copied and the type of Data Stream object (memory vs. tmpfile)
 * will be inherited.
 */
void fiobj_data_assert_dynamic(FIOBJ io);

#if DEBUG
void fiobj_data_test(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
