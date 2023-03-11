/*
 * Copyright 2017, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "libsyscall_intercept_hook_point.h"
#include "syscall_desc.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <assert.h>

#include "fsapi.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

int log_fd;

static char buffer[0x20000];
static size_t buffer_offset;

#define FS_SHM_KEY_BASE 20190301
#define FS_MAX_NUM_WORKER 20
#define NUM_MAX_FSP_FD (10000)
#define FSP_FD_TYPE_EMPTY (0)
#define FSP_FD_TYPE_FILE (1)
#define FSP_FD_TYPE_DIR (2)
#define FSP_FD_START_VAL 10000
#define UNUSED(x) (void)(x)
static const int kMaxNumFds = NUM_MAX_FSP_FD;
static const int kInvalidFd = -99;
static int g_fsp_fd_add_idx = 0;
static bool g_fsp_dbg = false;
static int fsp_fds[NUM_MAX_FSP_FD];
static int8_t fsp_fds_type[NUM_MAX_FSP_FD];
static struct CFS_DIR* fsp_fd_dirs[NUM_MAX_FSP_FD];
static unsigned int fsp_readdir_done_cnt[NUM_MAX_FSP_FD];
static const char *fsp_dir_prefix = "FSP";
// 3 == strlen(fsp_dir_prefix)
#define TO_NEW_PATH(path) (path + 3)

static key_t shm_keys[FS_MAX_NUM_WORKER];
static int g_num_workers = 0;

// https://man7.org/linux/man-pages/man2/getdents.2.html
// https://elixir.bootlin.com/linux/v4.7/source/fs/readdir.c#L150
struct linux_dirent {
           unsigned long  d_ino;
           unsigned long  d_off;
           unsigned short d_reclen;
           char           d_name[256];
		   unsigned char  d_type;
};

static void init_shm_keys(char *keys);
static void clean_exit() {
	exit(fs_exit());
}
static inline int check_if_fsp_path(const char *path) {
  return strncmp(path, fsp_dir_prefix, 3) == 0;
}

static int add_fsp_fd(int new_fd) {
	assert (new_fd > FSP_FD_START_VAL);
	int newfd_idx = -1;
	for (int i_add = 0; i_add < kMaxNumFds; i_add++) {
		int cur_idx = (g_fsp_fd_add_idx + i_add) % kMaxNumFds;
		if (fsp_fds[cur_idx] < 0) {
			newfd_idx = cur_idx;
			fsp_fds[cur_idx] = new_fd;
			g_fsp_fd_add_idx = cur_idx;
			break;
		}
	}
	return newfd_idx;
}

static int8_t set_fsp_fd_type(int fd, int idx, int8_t cur_type) {
	if (fsp_fds[idx] != fd) {
		return -1;
	}
	int8_t orig_type = fsp_fds_type[idx];
	fsp_fds_type[idx] = cur_type;
	return orig_type;
}

static int find_fsp_fd(int fd) {
	for (int i = 0; i < kMaxNumFds; i++) {
		if (fsp_fds[i] == fd) {
			return i;
		}
	}
	return -1;
}

static inline bool is_fsp_fd(int fd) {
	return find_fsp_fd(fd) >= 0;
}

static int del_fsp_fd(int fd) {
	int idx = find_fsp_fd(fd);
	if (idx >= 0) {
		fsp_fds[idx] = kInvalidFd;
		fsp_fds_type[idx] = FSP_FD_TYPE_EMPTY;
		fsp_fd_dirs[idx] = 0;
	}
	return idx;
}

static void init_fsp_fd_array() {
	for (int i = 0; i < kMaxNumFds; i++) {
		fsp_fds[i] = kInvalidFd;
	}
}


static bool
exchange_buffer_offset(size_t *expected, size_t new)
{
	return __atomic_compare_exchange_n(&buffer_offset, expected, new, false,
			__ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

#define DUMP_TRESHOLD (sizeof(buffer) - 0x1000)

static void
append_buffer(const char *data, ssize_t len)
{
	static long writers;
	size_t offset = buffer_offset;

	while (true) {
		while (offset >= DUMP_TRESHOLD) {
			syscall_no_intercept(SYS_sched_yield);
			offset = buffer_offset;
		}

		__atomic_fetch_add(&writers, 1, __ATOMIC_SEQ_CST);

		if (exchange_buffer_offset(&offset, offset + len)) {
			memcpy(buffer + offset, data, len);
			break;
		}

		__atomic_fetch_sub(&writers, 1, __ATOMIC_SEQ_CST);
	}

	if (offset + len > DUMP_TRESHOLD) {
		while (__atomic_load_n(&writers, __ATOMIC_SEQ_CST) != 0)
			syscall_no_intercept(SYS_sched_yield);

		syscall_no_intercept(SYS_write, log_fd, buffer, buffer_offset);
		__atomic_store_n(&buffer_offset, 0, __ATOMIC_SEQ_CST);
	}
}

static char *
print_cstr(char *dst, const char *str)
{
	while (*str != '\0')
		*dst++ = *str++;

	return dst;
}

static const char xdigit[16] = "0123456789abcdef";

static char *
print_hex(char *dst, long n)
{
	*dst++ = '0';
	*dst++ = 'x';

	int shift = 64;
	do {
		shift -= 4;
		*dst++ = xdigit[(n >> shift) % 0x10];
	} while (shift > 0);

	return dst;
}

static char *
print_number(char *dst, unsigned long n, long base)
{
	char digits[0x40];

	digits[sizeof(digits) - 1] = '\0';
	char *c = digits + sizeof(digits) - 1;

	do {
		*--c = xdigit[n % base];
		n /= base;
	} while (n > 0);

	while (*c != '\0')
		*dst++ = *c++;

	return dst;
}

static char *
print_signed_dec(char *dst, long n)
{
	unsigned long nu;
	if (n >= 0) {
		nu = (unsigned long)n;
	} else {
		*dst++ = '-';
		nu = ((unsigned long)((0l - 1L) - n)) + 1LU;
	}

	return print_number(dst, nu, 10);
}

static char *
print_fd(char *dst, long n)
{
	return print_signed_dec(dst, n);
}

static char *
print_atfd(char *dst, long n)
{
	if (n == AT_FDCWD)
		return print_cstr(dst, "AT_FDCWD");
	else
		return print_signed_dec(dst, n);
}

static char *
print_open_flags(char *dst, long flags)
{
	char *c = dst;

	*c = 0;

	if (flags == 0)
		return print_cstr(c, "O_RDONLY");

#ifdef O_EXEC
	if ((flags & O_EXEC) == O_EXEC)
		c = print_cstr(c, "O_EXEC | ");
#endif
	if ((flags & O_RDWR) == O_RDWR)
		c = print_cstr(c, "O_RDWR | ");
	if ((flags & O_WRONLY) == O_WRONLY)
		c = print_cstr(c, "O_WRONLY | ");
	if ((flags & (O_WRONLY|O_RDWR)) == 0)
		c = print_cstr(c, "O_RDONLY | ");
#ifdef O_SEARCH
	if ((flags & O_SEARCH) = O_SEARCH)
		c = print_cstr(c, "O_SEARCH | ");
#endif
	if ((flags & O_APPEND) == O_APPEND)
		c = print_cstr(c, "O_APPEND | ");
	if ((flags & O_CLOEXEC) == O_CLOEXEC)
		c = print_cstr(c, "O_CLOEXEC | ");
	if ((flags & O_CREAT) == O_CREAT)
		c = print_cstr(c, "O_CREAT | ");
	if ((flags & O_DIRECTORY) == O_DIRECTORY)
		c = print_cstr(c, "O_DIRECTORY | ");
	if ((flags & O_DSYNC) == O_DSYNC)
		c = print_cstr(c, "O_DSYNC | ");
	if ((flags & O_EXCL) == O_EXCL)
		c = print_cstr(c, "O_EXCL | ");
	if ((flags & O_NOCTTY) == O_NOCTTY)
		c = print_cstr(c, "O_NOCTTY | ");
	if ((flags & O_NOFOLLOW) == O_NOFOLLOW)
		c = print_cstr(c, "O_NOFOLLOW | ");
	if ((flags & O_NONBLOCK) == O_NONBLOCK)
		c = print_cstr(c, "O_NONBLOCK | ");
	if ((flags & O_RSYNC) == O_RSYNC)
		c = print_cstr(c, "O_RSYNC | ");
	if ((flags & O_SYNC) == O_SYNC)
		c = print_cstr(c, "O_SYNC | ");
	if ((flags & O_TRUNC) == O_TRUNC)
		c = print_cstr(c, "O_TRUNC | ");
#ifdef O_TTY_INIT
	if ((flags & O_TTY_INIT) == O_TTY_INIT)
		c = print_cstr(c, "O_TTY_INIT | ");
#endif

#ifdef O_EXEC
	flags &= ~O_EXEC;
#endif
#ifdef O_TTY_INIT
	flags &= ~O_TTY_INIT;
#endif
#ifdef O_SEARCH
	flags &= ~O_SEARCH;
#endif

	flags &= ~(O_RDONLY | O_RDWR | O_WRONLY | O_APPEND |
	    O_CLOEXEC | O_CREAT | O_DIRECTORY | O_DSYNC | O_EXCL |
	    O_NOCTTY | O_NOFOLLOW | O_NONBLOCK | O_RSYNC | O_SYNC |
	    O_TRUNC);

	if (flags != 0) {
		/*
		 * Some values in the flag were not recognized, just print the
		 * raw number.
		 * e.g.: "O_RDONLY | O_NONBLOCK | 0x9876"
		 */
		c = print_hex(c, flags);
	} else if (c != dst) {
		/*
		 * All bits in flag were parsed, and the pointer c does not
		 * point to the start of the buffer, therefore some text was
		 * written already, with a separator on the end. Remove the
		 * trailing three characters: " | "
		 *
		 * e.g.: "O_RDONLY | O_NONBLOCK | " -> "O_RDONLY | O_NONBLOCK"
		 */
		c -= 3;
	}

	return c;
}

#define CSTR_MAX_LEN 0x100

static char *
print_cstr_escaped(char *dst, const char *str)
{
	size_t len = 0;
	*dst++ = '"';
	while (*str != '\0' && len < CSTR_MAX_LEN) {
		if (*str == '\n') {
			*dst++ = '\\';
			*dst++ = 'n';
		} else if (*str == '\\') {
			*dst++ = '\\';
			*dst++ = '\\';
		} else if (*str == '\t') {
			*dst++ = '\\';
			*dst++ = 't';
		} else if (*str == '\"') {
			*dst++ = '\\';
			*dst++ = '"';
		} else if (isprint((unsigned char)*str)) {
			*dst++ = *str;
		} else {
			*dst++ = '\\';
			*dst++ = 'x';
			*dst++ = xdigit[((unsigned char)*str) / 0x10];
			*dst++ = xdigit[((unsigned char)*str) % 0x10];
		}

		++len;
		++str;
	}

	if (*str != '\0')
		dst = print_cstr(dst, "...");

	*dst++ = '"';

	return dst;
}

static const char *const error_codes[] = {
#ifdef EPERM
	[EPERM] = "Operation not permitted",
#endif
#ifdef ENOENT
	[ENOENT] = "No such file or directory",
#endif
#ifdef ESRCH
	[ESRCH] = "No such process",
#endif
#ifdef EINTR
	[EINTR] = "Interrupted system call",
#endif
#ifdef EIO
	[EIO] = "I/O error",
#endif
#ifdef ENXIO
	[ENXIO] = "No such device or address",
#endif
#ifdef E2BIG
	[E2BIG] = "Argument list too long",
#endif
#ifdef ENOEXEC
	[ENOEXEC] = "Exec format error",
#endif
#ifdef EBADF
	[EBADF] = "Bad file number",
#endif
#ifdef ECHILD
	[ECHILD] = "No child processes",
#endif
#ifdef EAGAIN
	[EAGAIN] = "Try again",
#endif
#ifdef ENOMEM
	[ENOMEM] = "Out of memory",
#endif
#ifdef EACCES
	[EACCES] = "Permission denied",
#endif
#ifdef EFAULT
	[EFAULT] = "Bad address",
#endif
#ifdef ENOTBLK
	[ENOTBLK] = "Block device required",
#endif
#ifdef EBUSY
	[EBUSY] = "Device or resource busy",
#endif
#ifdef EEXIST
	[EEXIST] = "File exists",
#endif
#ifdef EXDEV
	[EXDEV] = "Cross-device link",
#endif
#ifdef ENODEV
	[ENODEV] = "No such device",
#endif
#ifdef ENOTDIR
	[ENOTDIR] = "Not a directory",
#endif
#ifdef EISDIR
	[EISDIR] = "Is a directory",
#endif
#ifdef EINVAL
	[EINVAL] = "Invalid argument",
#endif
#ifdef ENFILE
	[ENFILE] = "File table overflow",
#endif
#ifdef EMFILE
	[EMFILE] = "Too many open files",
#endif
#ifdef ENOTTY
	[ENOTTY] = "Not a typewriter",
#endif
#ifdef ETXTBSY
	[ETXTBSY] = "Text file busy",
#endif
#ifdef EFBIG
	[EFBIG] = "File too large",
#endif
#ifdef ENOSPC
	[ENOSPC] = "No space left on device",
#endif
#ifdef ESPIPE
	[ESPIPE] = "Illegal seek",
#endif
#ifdef EROFS
	[EROFS] = "Read-only file system",
#endif
#ifdef EMLINK
	[EMLINK] = "Too many links",
#endif
#ifdef EPIPE
	[EPIPE] = "Broken pipe",
#endif
#ifdef EDOM
	[EDOM] = "Math argument out of domain of func",
#endif
#ifdef ERANGE
	[ERANGE] = "Math result not representable",
#endif
#ifdef EDEADLK
	[EDEADLK] = "Resource deadlock would occur",
#endif
#ifdef ENAMETOOLONG
	[ENAMETOOLONG] = "File name too long",
#endif
#ifdef ENOLCK
	[ENOLCK] = "No record locks available",
#endif
#ifdef ENOSYS
	[ENOSYS] = "Invalid system call number",
#endif
#ifdef ENOTEMPTY
	[ENOTEMPTY] = "Directory not empty",
#endif
#ifdef ELOOP
	[ELOOP] = "Too many symbolic links encountered",
#endif
#ifdef ENOMSG
	[ENOMSG] = "No message of desired type",
#endif
#ifdef EIDRM
	[EIDRM] = "Identifier removed",
#endif
#ifdef ECHRNG
	[ECHRNG] = "Channel number out of range",
#endif
#ifdef EL2NSYNC
	[EL2NSYNC] = "Level 2 not synchronized",
#endif
#ifdef EL3HLT
	[EL3HLT] = "Level 3 halted",
#endif
#ifdef EL3RST
	[EL3RST] = "Level 3 reset",
#endif
#ifdef ELNRNG
	[ELNRNG] = "Link number out of range",
#endif
#ifdef EUNATCH
	[EUNATCH] = "Protocol driver not attached",
#endif
#ifdef ENOCSI
	[ENOCSI] = "No CSI structure available",
#endif
#ifdef EL2HLT
	[EL2HLT] = "Level 2 halted",
#endif
#ifdef EBADE
	[EBADE] = "Invalid exchange",
#endif
#ifdef EBADR
	[EBADR] = "Invalid request descriptor",
#endif
#ifdef EXFULL
	[EXFULL] = "Exchange full",
#endif
#ifdef ENOANO
	[ENOANO] = "No anode",
#endif
#ifdef EBADRQC
	[EBADRQC] = "Invalid request code",
#endif
#ifdef EBADSLT
	[EBADSLT] = "Invalid slot",
#endif
#ifdef EBFONT
	[EBFONT] = "Bad font file format",
#endif
#ifdef ENOSTR
	[ENOSTR] = "Device not a stream",
#endif
#ifdef ENODATA
	[ENODATA] = "No data available",
#endif
#ifdef ETIME
	[ETIME] = "Timer expired",
#endif
#ifdef ENOSR
	[ENOSR] = "Out of streams resources",
#endif
#ifdef ENONET
	[ENONET] = "Machine is not on the network",
#endif
#ifdef ENOPKG
	[ENOPKG] = "Package not installed",
#endif
#ifdef EREMOTE
	[EREMOTE] = "Object is remote",
#endif
#ifdef ENOLINK
	[ENOLINK] = "Link has been severed",
#endif
#ifdef EADV
	[EADV] = "Advertise error",
#endif
#ifdef ESRMNT
	[ESRMNT] = "Srmount error",
#endif
#ifdef ECOMM
	[ECOMM] = "Communication error on send",
#endif
#ifdef EPROTO
	[EPROTO] = "Protocol error",
#endif
#ifdef EMULTIHOP
	[EMULTIHOP] = "Multihop attempted",
#endif
#ifdef EDOTDOT
	[EDOTDOT] = "RFS specific error",
#endif
#ifdef EBADMSG
	[EBADMSG] = "Not a data message",
#endif
#ifdef EOVERFLOW
	[EOVERFLOW] = "Value too large for defined data type",
#endif
#ifdef ENOTUNIQ
	[ENOTUNIQ] = "Name not unique on network",
#endif
#ifdef EBADFD
	[EBADFD] = "File descriptor in bad state",
#endif
#ifdef EREMCHG
	[EREMCHG] = "Remote address changed",
#endif
#ifdef ELIBACC
	[ELIBACC] = "Can not access a needed shared library",
#endif
#ifdef ELIBBAD
	[ELIBBAD] = "Accessing a corrupted shared library",
#endif
#ifdef ELIBSCN
	[ELIBSCN] = ".lib section in a.out corrupted",
#endif
#ifdef ELIBMAX
	[ELIBMAX] = "Attempting to link in too many shared libraries",
#endif
#ifdef ELIBEXEC
	[ELIBEXEC] = "Cannot exec a shared library directly",
#endif
#ifdef EILSEQ
	[EILSEQ] = "Illegal byte sequence",
#endif
#ifdef ERESTART
	[ERESTART] = "Interrupted system call should be restarted",
#endif
#ifdef ESTRPIPE
	[ESTRPIPE] = "Streams pipe error",
#endif
#ifdef EUSERS
	[EUSERS] = "Too many users",
#endif
#ifdef ENOTSOCK
	[ENOTSOCK] = "Socket operation on non-socket",
#endif
#ifdef EDESTADDRREQ
	[EDESTADDRREQ] = "Destination address required",
#endif
#ifdef EMSGSIZE
	[EMSGSIZE] = "Message too long",
#endif
#ifdef EPROTOTYPE
	[EPROTOTYPE] = "Protocol wrong type for socket",
#endif
#ifdef ENOPROTOOPT
	[ENOPROTOOPT] = "Protocol not available",
#endif
#ifdef EPROTONOSUPPORT
	[EPROTONOSUPPORT] = "Protocol not supported",
#endif
#ifdef ESOCKTNOSUPPORT
	[ESOCKTNOSUPPORT] = "Socket type not supported",
#endif
#ifdef EOPNOTSUPP
	[EOPNOTSUPP] = "Operation not supported on transport endpoint",
#endif
#ifdef EPFNOSUPPORT
	[EPFNOSUPPORT] = "Protocol family not supported",
#endif
#ifdef EAFNOSUPPORT
	[EAFNOSUPPORT] = "Address family not supported by protocol",
#endif
#ifdef EADDRINUSE
	[EADDRINUSE] = "Address already in use",
#endif
#ifdef EADDRNOTAVAIL
	[EADDRNOTAVAIL] = "Cannot assign requested address",
#endif
#ifdef ENETDOWN
	[ENETDOWN] = "Network is down",
#endif
#ifdef ENETUNREACH
	[ENETUNREACH] = "Network is unreachable",
#endif
#ifdef ENETRESET
	[ENETRESET] = "Network dropped connection because of reset",
#endif
#ifdef ECONNABORTED
	[ECONNABORTED] = "Software caused connection abort",
#endif
#ifdef ECONNRESET
	[ECONNRESET] = "Connection reset by peer",
#endif
#ifdef ENOBUFS
	[ENOBUFS] = "No buffer space available",
#endif
#ifdef EISCONN
	[EISCONN] = "Transport endpoint is already connected",
#endif
#ifdef ENOTCONN
	[ENOTCONN] = "Transport endpoint is not connected",
#endif
#ifdef ESHUTDOWN
	[ESHUTDOWN] = "Cannot send after transport endpoint shutdown",
#endif
#ifdef ETOOMANYREFS
	[ETOOMANYREFS] = "Too many references: cannot splice",
#endif
#ifdef ETIMEDOUT
	[ETIMEDOUT] = "Connection timed out",
#endif
#ifdef ECONNREFUSED
	[ECONNREFUSED] = "Connection refused",
#endif
#ifdef EHOSTDOWN
	[EHOSTDOWN] = "Host is down",
#endif
#ifdef EHOSTUNREACH
	[EHOSTUNREACH] = "No route to host",
#endif
#ifdef EALREADY
	[EALREADY] = "Operation already in progress",
#endif
#ifdef EINPROGRESS
	[EINPROGRESS] = "Operation now in progress",
#endif
#ifdef ESTALE
	[ESTALE] = "Stale file handle",
#endif
#ifdef EUCLEAN
	[EUCLEAN] = "Structure needs cleaning",
#endif
#ifdef ENOTNAM
	[ENOTNAM] = "Not a XENIX named type file",
#endif
#ifdef ENAVAIL
	[ENAVAIL] = "No XENIX semaphores available",
#endif
#ifdef EISNAM
	[EISNAM] = "Is a named type file",
#endif
#ifdef EREMOTEIO
	[EREMOTEIO] = "Remote I/O error",
#endif
#ifdef EDQUOT
	[EDQUOT] = "Quota exceeded",
#endif
#ifdef ENOMEDIUM
	[ENOMEDIUM] = "No medium found",
#endif
#ifdef EMEDIUMTYPE
	[EMEDIUMTYPE] = "Wrong medium type",
#endif
#ifdef ECANCELED
	[ECANCELED] = "Operation Canceled",
#endif
#ifdef ENOKEY
	[ENOKEY] = "Required key not available",
#endif
#ifdef EKEYEXPIRED
	[EKEYEXPIRED] = "Key has expired",
#endif
#ifdef EKEYREVOKED
	[EKEYREVOKED] = "Key has been revoked",
#endif
#ifdef EKEYREJECTED
	[EKEYREJECTED] = "Key was rejected by service",
#endif
#ifdef EOWNERDEAD
	[EOWNERDEAD] = "Owner died",
#endif
#ifdef ENOTRECOVERABLE
	[ENOTRECOVERABLE] = "State not recoverable",
#endif
#ifdef ERFKILL
	[ERFKILL] = "Operation not possible due to RF-kill",
#endif
#ifdef EHWPOISON
	[EHWPOISON] = "Memory page has hardware error",
#endif
};

static char *
print_rdec(char *dst, long n)
{
	dst = print_signed_dec(dst, n);

	if (n < 0 && n > -((long)ARRAY_SIZE(error_codes))) {
		if (error_codes[-n] != NULL) {
			dst = print_cstr(dst, " (");
			dst = print_cstr(dst, error_codes[-n]);
			dst = print_cstr(dst, ")");
		}
	}

	return dst;
}

static char *
print_runsigned(char *dst, long n)
{
	return print_number(dst, (unsigned long)n, 10);
}

static char *
print_mode_t(char *dst, long n)
{
	*dst++ = '0';
	if (n < 0100)
		*dst++ = '0';
	if (n < 0010)
		*dst++ = '0';
	return print_number(dst, (unsigned long)n, 8);
}

#define MIN_AVAILABLE_REQUIRED (0x100 + 8 * CSTR_MAX_LEN)

static char *
print_unknown_syscall(char *dst, long syscall_number,
			const long args[static 6], long result)
{
	dst = print_cstr(dst, "syscall(");
	dst = print_number(dst, syscall_number, 10);
	for (unsigned i = 0; i < 6; ++i) {
		dst = print_cstr(dst, ", ");
		dst = print_hex(dst, args[i]);
	}
	dst = print_cstr(dst, ") = ");
	dst = print_hex(dst, result);
	return dst;
}

static char *
print_known_syscall(char *dst, const struct syscall_desc *desc,
			const long args[static 6], long result)
{
	dst = print_cstr(dst, desc->name);
	*dst++ = '(';

	for (unsigned i = 0; desc->args[i] != arg_none; ++i) {
		if (i > 0)
			dst = print_cstr(dst, ", ");

		switch (desc->args[i]) {
		case arg_fd:
			dst = print_fd(dst, args[i]);
			break;
		case arg_atfd:
			dst = print_atfd(dst, args[i]);
			break;
		case arg_cstr:
			dst = print_hex(dst, args[i]);
			dst = print_cstr_escaped(dst, (const char *)(args[i]));
			break;
		case arg_open_flags:
			dst = print_open_flags(dst, args[i]);
			break;
		case arg_mode:
			dst = print_mode_t(dst, result);
			break;
		default:
			dst = print_hex(dst, args[i]);
			break;
		}
	}

	*dst++ = ')';
	if (desc->return_type != rnoreturn) {
		dst = print_cstr(dst, " = ");
		switch (desc->return_type) {
		default:
		case rhex:
			dst = print_hex(dst, result);
			break;
		case rdec:
			dst = print_rdec(dst, result);
			break;
		case runsigned:
			dst = print_runsigned(dst, result);
			break;
		case rmode:
			dst = print_mode_t(dst, result);
			break;
		}
	}

	return dst;
}

static void
print_syscall(const struct syscall_desc *desc,
		long syscall_number, const long args[6], long result)
{
	char local_buffer[0x300];
	char *c;

	if (desc != NULL)
		c = print_known_syscall(local_buffer, desc, args, result);
	else
		c = print_unknown_syscall(local_buffer, syscall_number,
					args, result);

	*c++ = '\n';
	append_buffer(local_buffer, c - local_buffer);
}

static void check_open_flag_fd_type(long open_flags, int8_t *fd_type) {
	if (open_flags & O_DIRECTORY) {
		if (g_fsp_dbg) append_buffer("Dir\n", 4);
		*fd_type = FSP_FD_TYPE_DIR;
	} else {
		if (g_fsp_dbg) append_buffer("File\n", 5);
	}
}

static int getdents_by_fsp_readdirs(int fd, struct linux_dirent *dirp, unsigned int count) {
	struct dirent *dent_p;
	assert (count > 0);
	int fd_idx = find_fsp_fd(fd);
	struct CFS_DIR* fsp_dirp = fsp_fd_dirs[fd_idx];
	struct linux_dirent *cur_linux_dirp = dirp;
	unsigned int done_count = 0;
	do {
		dent_p = fs_readdir(fsp_dirp);
		if (dent_p != NULL) {
			cur_linux_dirp = dirp + done_count;
			cur_linux_dirp->d_ino = (ino_t)dent_p->d_ino;
			cur_linux_dirp->d_off = (off_t)(fsp_readdir_done_cnt[fd_idx]) + done_count + 1;
			cur_linux_dirp->d_type = (unsigned char)dent_p->d_type;
			cur_linux_dirp->d_reclen = sizeof(struct linux_dirent);
			memcpy(cur_linux_dirp->d_name, dent_p->d_name, 256);
			done_count++;
			if (done_count == count) {
				break;
			}
		}
	} while (dent_p != NULL);
	fsp_readdir_done_cnt[fd_idx] += done_count;
	return done_count;
}

static int dir_fd_do_opendir(const char* path, int dir_fd_idx, int dir_fd) {
	assert (fsp_fds[dir_fd_idx] == dir_fd);
	assert(fsp_fd_dirs[dir_fd_idx] == 0);
	struct CFS_DIR* dirp = fs_opendir(path);
	if (dirp == NULL) {
		return -1;
	}
	fsp_fd_dirs[dir_fd_idx] = dirp;
	fsp_readdir_done_cnt[dir_fd_idx] = 0;
	return 0;
}

static bool fsp_syscall_handle(long syscall_number,
		const long args[6],
		long *result)
{
#define DO_ORIG_PATH_SYSCALL *result = \
	syscall_no_intercept(syscall_number, cur_path, args[1], args[2], args[3], args[4], args[5]); \
	handled = true;

#define DO_ORIG_PATH_SYSCALL1 *result = \
	syscall_no_intercept(syscall_number, args[0], cur_path, args[2], args[3], args[4], args[5]); \
	handled = true;

	bool handled = false;
	char *cur_path = (char*)args[0];
	int8_t cur_fd_type = FSP_FD_TYPE_FILE;
	
	// Path-based operations
	if (syscall_number == SYS_open || syscall_number == SYS_openat) {
		int open_flag_pos = 1;
		int open_mode_pos = 2;
		if (syscall_number == SYS_openat) {
			cur_path = (char*)args[1];
			open_flag_pos = 2;
			open_mode_pos = 3;
		}
		check_open_flag_fd_type(args[open_flag_pos], &cur_fd_type);
		if (check_if_fsp_path(cur_path)) {
			cur_path = TO_NEW_PATH(cur_path);
			int fd = fd = fs_open(cur_path, (int)args[open_flag_pos], (mode_t)args[open_mode_pos]);
			if (fd >= 0) {
				int idx = add_fsp_fd(fd);
				set_fsp_fd_type(fd, idx, cur_fd_type);
				if (cur_fd_type == FSP_FD_TYPE_DIR) {
					dir_fd_do_opendir(cur_path, fd, idx);
				}
			}
			*result = fd;
			handled = true;
		} else {
			DO_ORIG_PATH_SYSCALL;
		}
	}
	if (syscall_number == SYS_unlink) {
		if (check_if_fsp_path(cur_path)) {
			cur_path = TO_NEW_PATH(cur_path);
			int ret = fs_unlink(cur_path);
			*result = ret;
			handled = true;
		} else {
			DO_ORIG_PATH_SYSCALL;
		}
	}
	if (syscall_number == SYS_mkdir) {
		if (check_if_fsp_path(cur_path)) {
			cur_path = TO_NEW_PATH(cur_path);
			int ret = fs_mkdir(cur_path, args[1]);
			*result = ret;
			handled = true;
		} else {
			DO_ORIG_PATH_SYSCALL;
		}
	}
	if (syscall_number == SYS_rename) {
		char *dst_path = (char*)args[1];
		if (check_if_fsp_path(cur_path)) {
			if (check_if_fsp_path(dst_path)) {
				cur_path = TO_NEW_PATH(cur_path);
				dst_path = TO_NEW_PATH(dst_path);
				int ret = fs_rename(cur_path, dst_path);
				*result = ret;
				handled = true;
			} else {
				*result = -1;
				handled = true;
			}
		} else {
			*result = syscall_no_intercept(syscall_number, cur_path, dst_path,
				args[2], args[3], args[4], args[5]);
			handled = true;
		}
	}
	if (syscall_number == SYS_lstat) {
		if (check_if_fsp_path(cur_path)) {
			cur_path = TO_NEW_PATH(cur_path);
			int ret = fs_stat(cur_path, (struct stat *)args[1]);
			*result = ret;
		} else {
			DO_ORIG_PATH_SYSCALL;
		}
	}
	// Fd-based operations
	int cur_fd = (int)args[0];
	char local_fd_log_buf[100];
	UNUSED(local_fd_log_buf);
#define DO_ORIG_FD_SYSCALL *result = \
	syscall_no_intercept(syscall_number, cur_fd, args[1], args[2], args[3], args[4], args[5]); \
	handled = true;
#define DO_LOOKUP_FD if (is_fsp_fd(cur_fd)) { \
	sprintf(local_fd_log_buf, "fsp_fd(%d)\n", cur_fd); \
	append_buffer(local_fd_log_buf, strlen(local_fd_log_buf)); }
	if (syscall_number == SYS_getdents64 || syscall_number == SYS_getdents) {
		if (is_fsp_fd(cur_fd)) {
			*result = getdents_by_fsp_readdirs(cur_fd, (struct linux_dirent*)args[1], args[2]);
			handled = true;
		} else {
			DO_ORIG_FD_SYSCALL;
		}
	}
	if (syscall_number == SYS_fadvise64) {
		if (is_fsp_fd(cur_fd)) {
			// directly return success if the fd is from FSP
			*result = 0;
			handled = true;
		} else {
			DO_ORIG_FD_SYSCALL;
		}
	}
	if (syscall_number == SYS_read) {
		if (is_fsp_fd(cur_fd)) {
			*result = fs_allocated_read(cur_fd, (void*)args[1], args[2]);
			handled = true;
		} else {
			DO_ORIG_FD_SYSCALL;
		}
	}
	if (syscall_number == SYS_write) {
		if (is_fsp_fd(cur_fd)) {
			*result = fs_allocated_write(cur_fd, (void*)args[1], args[2]);
			handled = true;
		} else {
			DO_ORIG_FD_SYSCALL;
		}
	}
	if (syscall_number == SYS_close) {
		int idx = del_fsp_fd(cur_fd);
		if (idx >= 0) {
			if (g_fsp_dbg) {
				append_buffer("fsp_close\n", 10);
			}
		} else {
			DO_ORIG_FD_SYSCALL;
		}
	}
	if (syscall_number == SYS_fstat || syscall_number == SYS_newfstatat) {
		if (is_fsp_fd(cur_fd)) {
			*result = fs_fstat(cur_fd, (struct stat *)args[1]);
			handled = true;
		} else {
			DO_ORIG_FD_SYSCALL;
		}
	}

#undef DO_ORIG_PATH_SYSCALL
#undef DO_ORIG_PATH_SYSCALL1
#undef DO_ORIG_FD_SYSCALL
#undef DO_LOOKUP_FD
	return handled;
}

static int
hook(long syscall_number,
		long arg0, long arg1,
		long arg2, long arg3,
		long arg4, long arg5,
		long *result)
{
	long args[6] = {arg0, arg1, arg2, arg3, arg4, arg5};
	const struct syscall_desc *desc =
		get_syscall_desc(syscall_number, args);

	if (desc != NULL && desc->return_type == rnoreturn) {
		print_syscall(desc, syscall_number, args, 0);
		if (syscall_number == SYS_exit_group && buffer_offset > 0)
			syscall_no_intercept(SYS_write, log_fd,
						buffer, buffer_offset);
	}

	int handled = fsp_syscall_handle(syscall_number,  args, result);
	if (!handled) {
		*result = syscall_no_intercept(syscall_number,
					arg0, arg1, arg2, arg3, arg4, arg5);
	}

	print_syscall(desc, syscall_number, args, *result);

	return 0;
}


static void init_shm_keys(char *keys) {
  char *token = strtok(keys, ",");
  key_t cur_key;
  int num_workers = 0;
  while (token != NULL) {
    cur_key = atoi(token);
    shm_keys[num_workers++] = (FS_SHM_KEY_BASE) + cur_key;
    token = strtok(NULL, ",");
  }
  g_num_workers = num_workers;
}

static __attribute__((constructor)) void
start(void)
{
	const char *path = getenv("SYSCALL_LOG_PATH");

	if (path == NULL)
		syscall_no_intercept(SYS_exit_group, 3);

	log_fd = (int)syscall_no_intercept(SYS_open,
			path, O_CREAT | O_RDWR, (mode_t)0700);

	if (log_fd < 0)
		syscall_no_intercept(SYS_exit_group, 4);

	// fsp init
	char *fsp_num_workers_str = getenv("FSP_KEY_LISTS");
	if (fsp_num_workers_str == NULL) {
		syscall_no_intercept(SYS_exit_group, 3);
	}
	init_shm_keys(fsp_num_workers_str);
	int rt = fs_init_multi(g_num_workers, shm_keys);
	if (rt < 0) {
		syscall_no_intercept(SYS_exit_group, 3);
	}

	char *fsp_intercept_dbg = getenv("FSP_INTERCEPT_DBG");
	if (fsp_intercept_dbg == NULL) {
		g_fsp_dbg = (fsp_intercept_dbg[0] == 'T');
	}

	volatile void *ptr = fs_malloc(1024);
	fs_free((void *)ptr);

	init_fsp_fd_array();

	intercept_hook_point = &hook;
}

static __attribute__((destructor)) void fsops_shutdown() {
	clean_exit();
}
