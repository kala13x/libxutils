# Sys Modules

This section documents `xutils/src/sys/*`.

## Files

- [cli.md](cli.md): terminal input, window rendering and progress bars
- [cpu.md](cpu.md): CPU count and affinity helpers
- [log.md](log.md): global logger
- [mon.md](mon.md): Linux process/system monitoring
- [pool.md](pool.md): chained memory pool
- [sig.md](sig.md): signal handling, daemonize and backtrace helpers
- [srch.md](srch.md): recursive file/content search
- [sync.md](sync.md): mutexes, rwlocks, atomics and simple barriers
- [thread.md](thread.md): threads and lightweight repeating tasks
- [type.md](type.md): small type/format conversion helpers
- [xfs.md](xfs.md): file, path and directory utilities
- [xtime.md](xtime.md): time conversion and formatting helpers

## Common Rules

- Many modules are Linux-first. When a feature is absent or degraded on Windows, the docs call that out.
- Several helpers choose process termination on internal failure instead of propagating an error. This is intentional in the current implementation and must be understood before reuse in libraries.
