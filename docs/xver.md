# xver.c

## Purpose

Compile-time version string accessors.

## API Reference

#### `const char *XUtils_Version(void)`

- Arguments:
  - none.
- Does:
  - returns the long version string formatted as:
    - `<major>.<minor> build <build> (<release-date>)`
- Returns:
  - pointer to a static string.

#### `const char *XUtils_VersionShort(void)`

- Arguments:
  - none.
- Does:
  - returns the short version string formatted as:
    - `<major>.<minor>.<build>`
- Returns:
  - pointer to a static string.

## Notes

- Both strings are compile-time constants built from `xver.h` macros.
- No allocation occurs and callers must not free the returned pointers.
