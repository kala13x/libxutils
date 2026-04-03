# mon.c

## Purpose

Linux-oriented host/process monitor backed by `/proc` and `/sys/class/net`.

## Platform Scope

- The whole public implementation in `mon.c` is compiled only on non-Windows builds.
- In practice it is Linux-specific because it depends on `/proc` and `/sys/class/net`.

## API Reference

#### `int XMon_InitStats(xmon_stats_t *pStats)`

- Arguments:
  - `pStats`: monitoring state object.
- Does:
  - initializes the CPU core array, network-interface array, memory counters, worker-task state and network lock.
- Returns:
  - `XSTDOK` on success.
  - `XSTDERR` when array initialization fails.

#### `void XMon_DestroyStats(xmon_stats_t *pStats)`

- Arguments:
  - `pStats`: monitoring state.
- Does:
  - destroys CPU/network arrays and the network lock.
- Returns:
  - no return value.
- Note:
  - this function does not stop the worker thread for you.

#### `int XMon_StartMonitoring(xmon_stats_t *pStats, uint32_t nIntervalU, xpid_t nPID)`

- Arguments:
  - `pStats`: monitoring state.
  - `nIntervalU`: update interval in microseconds.
  - `nPID`: specific process id, or `<= 0` for the current process.
- Does:
  - validates `/proc/<pid>` when a specific pid is requested.
  - stores interval and pid.
  - starts a detached repeating worker via `XTask_Start()`.
- Returns:
  - current task status stored in `pStats->monitoring.nStatus`.
  - typically `XTASK_STAT_CREATED` on successful thread creation.
  - `XTASK_STAT_FAIL` on worker creation failure.
  - `XSTDERR` when the target `/proc/<pid>` directory does not exist.

#### `uint32_t XMon_StopMonitoring(xmon_stats_t *pStats, uint32_t nWaitUsecs)`

- Arguments:
  - `pStats`: monitoring state.
  - `nWaitUsecs`: sleep interval passed into `XTask_Stop()`.
- Does:
  - requests worker shutdown and waits until status becomes `XTASK_STAT_STOPPED`.
- Returns:
  - approximate waited microseconds.

#### `uint32_t XMon_WaitLoad(xmon_stats_t *pStats, uint32_t nWaitUsecs)`

- Arguments:
  - `pStats`: monitoring state.
  - `nWaitUsecs`: sleep interval between polls.
- Does:
  - waits until `nLoadDone` becomes true.
- Returns:
  - approximate waited microseconds.

#### `int XMon_GetMemoryInfo(xmon_stats_t *pStats, xmem_info_t *pInfo)`

- Arguments:
  - live stats object and destination memory snapshot.
- Does:
  - copies all memory-related atomic fields into `pInfo`.
- Returns:
  - `pInfo->nMemoryTotal`.
  - `0` when the monitor has not populated memory totals yet.

#### `int XMon_GetCPUStats(xmon_stats_t *pStats, xcpu_stats_t *pCpuStats)`

- Arguments:
  - live stats object and destination CPU snapshot.
- Does:
  - deep-copies aggregate CPU usage, total CPU info and every tracked core into a caller-owned `pCpuStats->cores` array.
- Returns:
  - positive core count on success.
  - `0` when no cores are available yet.
  - `-1` when the destination array cannot be initialized.
  - `-2` when copy-out produced no usable cores.
- Ownership:
  - caller must destroy `pCpuStats->cores` after use.

#### `int XMon_GetNetworkStats(xmon_stats_t *pStats, xarray_t *pIfaces)`

- Arguments:
  - live stats object and destination array.
- Does:
  - locks `netLock`.
  - deep-copies only active network interfaces into `pIfaces`.
- Returns:
  - copied interface count.
  - `0` when there are no interfaces yet or destination array init fails.
- Ownership:
  - caller must destroy `pIfaces` after use.

## Data Sources and Calculation Rules

- memory:
  - `/proc/meminfo`
  - `/proc/<pid>/status` or `/proc/self/status`
- CPU:
  - `/proc/stat`
  - `/proc/loadavg`
  - `/proc/<pid>/stat` or `/proc/self/stat`
- network:
  - `/sys/class/net/*`
  - IP addresses are resolved through `XAddr_GetIFCIP()`

CPU percentages are calculated from differences between two consecutive raw snapshots, so the first update mainly seeds baseline state.

## Important Notes

- The source currently copies `KernelSpaceChilds` from `UserSpaceChilds` in two places. Treat per-process child CPU split fields carefully.
- Network bandwidth-per-second counters depend on `nIntervalU / XMON_INTERVAL_USEC`; choose a sane non-zero update interval.
