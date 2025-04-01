/*!
 *  @file libxutils/src/sys/sig.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief Exit signal handling and backtrace.
 */


#ifndef __XUTILS_ERREX_H__
#define __XUTILS_ERREX_H__

#include "log.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void(*xsig_cb_t)(int);

void XUtils_Backtrace(void);
void XUtils_ErrExit(const char *pFmt, ...);
int XUtils_Daemonize(int bNoChdir, int bNoClose);

#define errex(...) XUtils_ErrExit(XLOG_THROW_LOCATION __VA_ARGS__)

int XSig_Register(int *pSignals, size_t nCount, xsig_cb_t callback);
int XSig_RegExitSigs(void);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_ERREX_H__ */