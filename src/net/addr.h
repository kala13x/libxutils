/*!
 *  @file libxutils/src/net/addr.h
 *
 *  This source is part of "libxutils" project
 *  2015-2020  Sun Dro (s.kalatoz@gmail.com)
 * 
 * @brief This source includes functions for detect 
 * IP and Mac address of the host operating system
 */

#ifndef __XUTILS_XADDR_H__
#define __XUTILS_XADDR_H__

#define XADDR_DNS_DEFAULT   "8.8.8.8"
#define XADDR_IFC_DEFAULT   "eth0"

#define XLINK_PROTOCOL_MAX  32
#define XLINK_INFO_MAX      32
#define XLINK_ADDR_MAX      256
#define XLINK_NAME_MAX      1024
#define XLINK_URL_MAX       2048
#define XLINK_MAX           4096

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @struct XLink
 * @brief Parsed information from the link
 */
typedef struct XLink {
    int nPort;
    char sUri[XLINK_URL_MAX];
    char sHost[XLINK_ADDR_MAX];
    char sAddr[XLINK_ADDR_MAX];
    char sUser[XLINK_INFO_MAX];
    char sPass[XLINK_INFO_MAX];
    char sFile[XLINK_NAME_MAX];
    char sProtocol[XLINK_INFO_MAX];
} xlink_t;

/*!
 * @brief Initialize xlink structure
 * @param pLink The pointer of the variable to init
 */
void XLink_Init(xlink_t *pLink);

/*!
 * @brief Parse link and store values in structure
 *
 * @param pLink The pointer to the XLink variable
 * @param pInput The string containing link to parse
 * @return On success, XSTDOK is returned, or XSTDERR in case of error
 */
int XLink_Parse(xlink_t *pLink, const char *pInput);

/*!
 * @brief Get default port for particular protocol
 *
 * @param pProtocol The string containing protocol name
 * @return On success, port number is returned, or -1 if not found
 */
int XAddr_GetDefaultPort(const char *pProtocol);

/*!
 * @brief Get MAC address of particular interface
 *
 * @param pIFace The string containing interface name
 * @param pAddr The pointer of the destination buffer
 * @param nSize Destination buffer size
 * @return On success, MAC address length is returned, or XSTDERR in case of error
 */
int XAddr_GetIFCMac(const char *pIFace, char *pAddr, int nSize);

/*!
 * @brief Get IP address of particular interface
 *
 * @param pIFace The string containing interface name
 * @param pAddr The pointer of the destination buffer
 * @param nSize Destination buffer size
 * @return On success, IP address length is returned, or XSTDERR in case of error
 */
int XAddr_GetIFCIP(const char *pIFace, char *pAddr, int nSize);

/*!
 * @brief Get MAC address of first available interface (except loopback)
 *
 * @param pAddr The pointer of the destination buffer
 * @param nSize Destination buffer size
 * @return On success, MAC address length is returned, or XSTDERR in case of error
 */
int XAddr_GetMAC(char *pAddr, int nSize);

/*!
 * @brief Get IP address by connecting to the DNS server
 *
 * @param pDNS The string containing DNS server address
 * @param pAddr The pointer of the destination buffer
 * @param nSize Destination buffer size
 * @return On success, IP address length is returned, or XSTDERR in case of error
 */
int XAddr_GetIP(const char *pDNS, char *pAddr, int nSize);

#ifdef __cplusplus
}
#endif

#endif /* __XUTILS_XADDR_H__ */
