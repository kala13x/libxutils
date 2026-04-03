# Net Modules

This section documents `xutils/src/net/*`.

## Files

- [addr.md](addr.md): address and URL parsing helpers
- [api.md](api.md): high-level event-driven socket/HTTP/WebSocket/MDTP API
- [event.md](event.md): cross-platform event engine under `api.c`
- [http.md](http.md): HTTP parser, assembler and synchronous request helpers
- [mdtp.md](mdtp.md): Modern Data Transmit Protocol packet framing
- [ntp.md](ntp.md): NTP time query helper
- [rtp.md](rtp.md): minimal RTP packet parsing/assembly helpers
- [sock.md](sock.md): raw socket abstraction and SSL integration
- [ws.md](ws.md): WebSocket frame creation/parsing

## Common Rules

- `event.c` is the low-level loop, `api.c` is the higher-level session/runtime on top of it.
- `api.c` callback semantics matter more than names. `INTERRUPT` means actual loop interruption by signal/EINTR; `TICK` means one successful service cycle completed.
- HTTP, MDTP and WS parsers all support “extra data after first complete packet” patterns and may recursively continue parsing buffered payload.
