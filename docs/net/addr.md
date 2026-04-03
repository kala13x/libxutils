# addr.c

## Purpose

Link parsing plus local IP/MAC discovery helpers.

## API Reference

### `void XLink_Init(xlink_t *pLink)`

- Args:
  - `pLink`: destination link struct.
- Does:
  - zeroes all fields.

### `int XLink_Parse(xlink_t *pLink, const char *pInput)`

- Args:
  - `pLink`: destination struct.
  - `pInput`: link text such as `scheme://user:pass@host:port/path`.
- Does:
  - parses protocol, user, password, host, port, URI and trailing file name.
  - lowercases protocol.
  - fills `sAddr` with bare host and `sHost` with host plus default port when missing.
- Returns:
  - `XSTDOK` on success.
  - `XSTDERR` on invalid input.

### `int XAddr_GetDefaultPort(const char *pProtocol)`

- Args:
  - protocol string.
- Does:
  - returns built-in default port.
- Returns:
  - port number or `-1`.
- Caveat:
  - uses prefix comparison based on input length, so partial protocol names can match.

### `int XAddr_GetIFCMac(const char *pIFace, char *pAddr, int nSize)`

### `int XAddr_GetIFCIP(const char *pIFace, char *pAddr, int nSize)`

- Args:
  - interface name, output buffer and output size.
- Does:
  - query Linux interface MAC/IP.
- Returns:
  - written length or `XSTDERR`.

### `int XAddr_GetMAC(char *pAddr, int nSize)`

- Finds first non-loopback interface MAC.
- Returns written length or `XSTDERR`.

### `int XAddr_GetIP(const char *pDNS, char *pAddr, int nSize)`

- Connects a UDP socket to `pDNS` and reads the chosen local address.
- Returns written length or `XSTDERR`.
