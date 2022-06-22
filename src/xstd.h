/*!
 *  @file libxutils/src/xmap.c
 *
 *  This source is part of "libxutils" project
 *  2019-2020  Sun Dro (f4tb0y@protonmail.com)
 *
 * @brief Standart includes.
 */

#ifndef __XUTILS_STDINC_H__
#define __XUTILS_STDINC_H__

/* C includes */
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <limits.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <math.h>

#include <sys/types.h>
#include <sys/stat.h>

#ifndef _WIN32
/* Linux includes */
#include <pthread.h>
#include <dirent.h>
#include <unistd.h>
#include <netdb.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/utsname.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <net/if.h>
#ifdef __linux__
#include <sys/syscall.h>
#include <grp.h>
#include <pwd.h>
#endif
#else
#include <ws2tcpip.h>
#include <winsock2.h>
#include <windows.h>
#include <direct.h>
#include <io.h>
#endif

#ifdef _XUTILS_DEBUG
#include "xlog.h"
#endif

typedef int	XSTATUS;

#ifndef XSTD_MIN
#define XSTD_MIN(a,b)(a<b?a:b)
#endif

#ifndef XSTD_MAX
#define XSTD_MAX(a,b)(a>b?a:b)
#endif

#ifndef XSTD_FIRSTOF
#define XSTD_FIRSTOF(a,b)(a?a:b)
#endif

#ifndef XMSG_MIN
#define XMSG_MIN 2048
#endif

#ifndef XMSG_MID
#define XMSG_MID 4098
#endif

#ifndef XMSG_MAX
#define XMSG_MAX 8196
#endif

#ifndef XPATH_MAX
#define XPATH_MAX 2048
#endif

#ifdef LINE_MAX
#define XLINE_MAX LINE_MAX
#else
#define XLINE_MAX 2048
#endif

#ifndef XADDR_MAX
#define XADDR_MAX 64
#endif

#ifndef XNAME_MAX
#define XNAME_MAX 256
#endif

#ifndef XADDR_MAX
#define XADDR_MAX 64
#endif

#ifndef XPERM_MAX
#define XPERM_MAX 16
#endif

#ifndef XPERM_LEN
#define XPERM_LEN 9
#endif

#ifndef XSTDNON
#define XSTDNON 0
#endif

#ifndef XSTDERR
#define XSTDERR -1
#endif

#ifndef XSTDINV
#define XSTDINV -2
#endif

#ifndef XSTDOK
#define XSTDOK 1
#endif

#ifndef XSTDUSR
#define XSTDUSR 2
#endif

#endif /* __XUTILS_STDINC_H__ */