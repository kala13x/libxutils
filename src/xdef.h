/*!
 *  @file libxutils/src/xdef.h
 *
 *  This source is part of "libxutils" project
 *  2019-2020  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Commonly used library definitions.
 */

#ifndef __XUTILS_STDDEF_H__
#define __XUTILS_STDDEF_H__

#include <errno.h>
#include <string.h>

#ifdef _WIN32
typedef int                 xsocklen_t;
typedef long                xatomic_t;
typedef int                 xmode_t;
typedef int                 xpid_t;
#else
typedef socklen_t           xsocklen_t;
typedef uint32_t            xatomic_t;
typedef mode_t              xmode_t;
typedef pid_t               xpid_t;
#endif

typedef uint8_t             xbool_t;
#define XTRUE               1
#define XFALSE              0

#define XATOMIC             volatile xatomic_t

#define XCHAR(var,size) char var[size] = {'\0'}
#define XARG_SIZE(val)  val, sizeof(val)

#define XFTON(x) ((x)>=0.0f?(int)((x)+0.5f):(int)((x)-0.5f))

#define XFLAGS_CHECK(c, f) (((c) & (f)) == (f))
#define XFLAGS_ENABLE(c, f) ((c) |= (f))
#define XFLAGS_DISABLE(c, f) ((c) &= ~(f))

#ifndef XMSG_MIN
#define XMSG_MIN        2048
#endif

#ifndef XMSG_MID
#define XMSG_MID        4098
#endif

#ifndef XMSG_MAX
#define XMSG_MAX        8196
#endif

#ifndef XPATH_MAX
#define XPATH_MAX       2048
#endif

#ifdef LINE_MAX
# define XLINE_MAX      LINE_MAX
#else
# define XLINE_MAX      2048
#endif

#ifndef XADDR_MAX
#define XADDR_MAX       64
#endif

#ifndef XNAME_MAX
#define XNAME_MAX       256
#endif

#ifndef XADDR_MAX
#define XADDR_MAX       64
#endif

#ifndef XPERM_MAX
#define XPERM_MAX       16
#endif

#ifndef XPERM_LEN
#define XPERM_LEN       9
#endif

#ifndef XSTDNON
#define XSTDNON         0
#endif

#ifndef XSTDERR
#define XSTDERR         -1
#endif

#ifndef XSTDINV
#define XSTDINV         -2
#endif

#ifndef XSTDEXC
#define XSTDEXC         -3
#endif

#ifndef XSTDOK
#define XSTDOK          1
#endif

#ifndef XSTDUSR
#define XSTDUSR         2
#endif

#define XCLR_RED        "\x1B[31m"
#define XCLR_RES        "\x1B[0m"

#define XLOCATION_LVL1(line)    #line
#define XLOCATION_LVL2(line)    XLOCATION_LVL1(line)
#define __XLOCATION__           XLOCATION_LVL2(__LINE__)

#ifdef _ASSERT_TIMED
#include <time.h>
#define XTROW_LOCATION                          \
    do {                                        \
        char timeStr[20];                       \
        time_t now = time(NULL);                \
        struct tm *tmInfo = localtime(&now);    \
        strftime(timeStr, sizeof(timeStr),      \
                "%H:%M:%S.%03d", tmInfo );      \
        printf("%s %s<error>%s "                \
            "Assert failed: "                   \
            "%s:%s():%s\n",                     \
            timeStr,                            \
            XCLR_RED, XCLR_RES,                 \
            __FILE__,                           \
            __FUNCTION__,                       \
            __XLOCATION__);                     \
    }                                           \
    while (XSTDNON)
#else
#define XTROW_LOCATION                          \
            printf("%s<error>%s "               \
                "Assert failed: "               \
                "%s:%s():%s\n",                 \
                XCLR_RED, XCLR_RES,             \
                __FILE__,                       \
                __FUNCTION__,                   \
                __XLOCATION__)
#endif

#define XASSERT_RET(condition, value)           \
    if (!condition) return value

#define XASSERT_LOG(condition, value)           \
    do {                                        \
        if (!condition) {                       \
            XTROW_LOCATION;                     \
            return value;                       \
        }                                       \
    }                                           \
    while (XSTDNON)

#define XASSERT_VOID_RET(condition)             \
    if (!condition) return

#define XASSERT_VOID_LOG(condition)             \
    do {                                        \
        if (!condition) {                       \
            XTROW_LOCATION;                     \
            return;                             \
        }                                       \
    }                                           \
    while (XSTDNON)

#define XASSERT_FREE_RET(condition, var, value) \
    do {                                        \
        if (!condition) {                       \
            free(var);                          \
            return value;                       \
        }                                       \
    }                                           \
    while (XSTDNON)

#define XASSERT_FREE_LOG(condition, var, value) \
    do {                                        \
        if (!condition) {                       \
            XTROW_LOCATION;                     \
            free(var);                          \
            return value;                       \
        }                                       \
    }                                           \
    while (XSTDNON)

#define XASSERT_CALL_RET(cnd, func, var, val)   \
    do {                                        \
        if (!cnd) {                             \
            func(var);                          \
            return val;                         \
        }                                       \
    }                                           \
    while (XSTDNON)

#define XASSERT_CALL_LOG(cnd, func, var, val)   \
    do {                                        \
        if (!cnd) {                             \
            XTROW_LOCATION;                     \
            func(var);                          \
            return val;                         \
        }                                       \
    }                                           \
    while (XSTDNON)

#ifdef _XUTILS_DEBUG
# define XASSERT        XASSERT_LOG
# define XASSERT_VOID   XASSERT_VOID_LOG
# define XASSERT_FREE   XASSERT_FREE_LOG
# define XASSERT_CALL   XASSERT_CALL_LOG
#else
# define XASSERT        XASSERT_RET
# define XASSERT_VOID   XASSERT_VOID_RET
# define XASSERT_FREE   XASSERT_FREE_RET
# define XASSERT_CALL   XASSERT_CALL_RET
#endif

#ifndef XSTD_MIN
#define XSTD_MIN(a,b)(a<b?a:b)
#endif

#ifndef XSTD_MAX
#define XSTD_MAX(a,b)(a>b?a:b)
#endif

#ifndef XSTD_FIRSTOF
#define XSTD_FIRSTOF(a,b)(a?a:b)
#endif

#ifdef _WIN32
static inline const char* XSTR_Error()
{
    static char buf[XMSG_MID];
    strerror_s(buf, sizeof(buf), errno);
    return buf;
}
#else
static inline const char* XSTR_Error()
{
    return strerror(errno);
}
#endif

#ifndef XSTRERR
#define XSTRERR XSTR_Error()
#endif

#define XSSL_MINIMAL_API 0x10000000L

typedef int XSTATUS;

#endif /* __XUTILS_STDDEF_H__ */