/*!
 *  @file libxutils/src/xdef.h
 *
 *  This source is part of "libxutils" project
 *  2019-2020  Sun Dro (f4tb0y@protonmail.com)
 *
 * @brief Commonly used library definitions.
 */

#ifndef __XUTILS_STDDEF_H__
#define __XUTILS_STDDEF_H__

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

#define _XUTILS_DEBUG

#define XLOCATION_LVL1(line) #line
#define XLOCATION_LVL2(line) XLOCATION_LVL1(line)
#define XTHROW_LOCATION __FILE__ ":" XLOCATION_LVL2(__LINE__)

#define XASSERT_RET(condition, value)           \
    if (!condition) return value

#define XASSERT_VOID(condition)                 \
    if (!condition) return

#define XASSERT_DBG(condition, value)           \
    do {                                        \
        if (!condition) {                       \
            printf("Assert failed at: %s\n",    \
                        XTHROW_LOCATION);       \
            return value;                       \
        }                                       \
    }                                           \
    while (XSTDNON)

#ifdef _XUTILS_DEBUG
# define XASSERT XASSERT_DBG
#else
# define XASSERT XASSERT_RET
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

#define XSSL_MINIMAL_API 0x10000000L

typedef int XSTATUS;

#endif /* __XUTILS_STDDEF_H__ */