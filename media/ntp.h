/*!
 *  @file libxutils/media/ntp.h
 *
 *  This source is part of "libxutils" project
 *  2019-2021  Sun Dro (f4tb0y@protonmail.com)
 * 
 * @brief Implementation of NTP protocol functionality
 */

#ifndef __XUTILS_NTP_H__
#define __XUTILS_NTP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "xtime.h"

int XNTP_GetDate(const char *pAddr, uint16_t nPort, xtime_t *pTime);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_NTP_H__ */