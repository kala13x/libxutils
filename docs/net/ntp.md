# ntp.c

## Purpose

Blocking one-shot NTP client.

## API Reference

### `int XNTP_GetDate(const char *pAddr, uint16_t nPort, xtime_t *pTime)`

- Args:
  - `pAddr`: NTP server address.
  - `nPort`: UDP port.
  - `pTime`: destination time struct.
- Does:
  - sends an NTP request over UDP and converts server time into `xtime_t`.
- Returns:
  - `XSTDOK` on success.
  - `XSTDERR` on network or parse failure.
