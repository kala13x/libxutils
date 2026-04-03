# cpu.c

## Purpose

CPU-count discovery plus affinity helpers.

## API Reference

#### `int XCPU_GetCount(void)`

- Arguments:
  - none.
- Does:
  - returns a process-wide cached CPU count if already known.
  - otherwise:
    - on Windows, uses `GetSystemInfo()`
    - on POSIX, prefers `sysconf(_SC_NPROCESSORS_ONLN)`
    - if that fails on Linux, scans `/proc/cpuinfo` for `processor` lines
- Returns:
  - positive CPU count on success.
  - `XSTDERR` when detection fails.

#### `int XCPU_SetAffinity(int *pCPUs, size_t nCount, xpid_t nPID)`

- Arguments:
  - `pCPUs`: array of CPU indexes.
  - `nCount`: array length.
  - `nPID`: target pid/tid, or `XCPU_CALLER_PID`.
- Does:
  - builds an affinity mask from all valid CPU indexes.
  - on Linux, treats `XCPU_CALLER_PID` as the current thread id via `SYS_gettid`.
  - on Windows, calls `SetThreadAffinityMask()` on the current thread for each CPU in the list.
- Returns:
  - OS success code (`0` on Linux success, `XSTDOK` on Windows success).
  - `XSTDERR` on invalid args, invalid CPUs or unsupported platforms.

#### `int XCPU_SetSingle(int nCPU, xpid_t nPID)`

- Arguments:
  - one CPU index and a pid/tid selector.
- Does:
  - wraps `XCPU_SetAffinity()` with a one-element array.
- Returns:
  - forwarded status.

#### `int XCPU_AddAffinity(int nCPU, xpid_t nPID)`

- Arguments:
  - one CPU index and target pid/tid.
- Does:
  - on Linux, loads the current affinity mask and adds the CPU if it is not already present.
  - on Windows, falls back to `XCPU_SetSingle()`.
- Returns:
  - OS success code when the mask is updated.
  - `XSTDNON` when the CPU is already in the mask.
  - `XSTDERR` on invalid CPU, lookup failure or unsupported platforms.

#### `int XCPU_DelAffinity(int nCPU, xpid_t nPID)`

- Arguments:
  - one CPU index and target pid/tid.
- Does:
  - on Linux, loads the current affinity mask and removes the CPU if present.
- Returns:
  - OS success code when the mask is updated.
  - `XSTDNON` when the CPU was not in the mask.
  - `XSTDERR` on invalid CPU or unsupported platforms.

## Important Notes

- CPU count is cached globally and will not refresh automatically for CPU hotplug changes.
- Linux affinity helpers are thread-oriented when `XCPU_CALLER_PID` is used.
