/*!
 *  @file libxutils/src/xstd.h
 *
 *  This source is part of "libxutils" project
 *  2019-2020  Sun Dro (s.kalatoz@gmail.com)
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

#ifdef __linux__
#include <sys/syscall.h>
#endif

#ifndef _WIN32
#include <pthread.h>
#include <termios.h>
#include <unistd.h>
#include <dirent.h>
#include <netdb.h>
#include <grp.h>
#include <pwd.h>

#include <sys/utsname.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/un.h>

#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/ethernet.h>
#include <net/if.h>
#else
#include <ws2tcpip.h>
#include <winsock2.h>
#include <windows.h>
#include <afunix.h>
#include <direct.h>
#include <io.h>
#endif

#include "xdef.h"

#endif /* __XUTILS_STDINC_H__ */
