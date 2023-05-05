/*!
 *  @file libxutils/src/sys/xsig.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief Exit signal handling and backtrace.
 */


#ifndef __XUTILS_ERREX_H__
#define __XUTILS_ERREX_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef void(*xsig_cb_t)(int);

int XSig_Register(int *pSignals, size_t nCount, xsig_cb_t callback);
int XSig_ExitSignals(void);

void xutils_dbg_backtrace(void);
void errex(const char * errmsg, ...);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_ERREX_H__ */