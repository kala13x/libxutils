<p align="center">
    <img src="https://raw.githubusercontent.com/kala13x/libxutils/main/logo.png" alt="alternate text">
</p>

[![MIT License](https://img.shields.io/apm/l/atomic-design-ui.svg?)](https://github.com/kala13x/libxutils/blob/master/LICENSE)
[![Build workflow](https://github.com/kala13x/libxutils/actions/workflows/build_libxutils.yml/badge.svg)](https://github.com/kala13x/libxutils/actions)
[![Coverity Scan Build Status](https://scan.coverity.com/projects/25173/badge.svg?)](https://scan.coverity.com/projects/libxutils)
[![Total alerts](https://img.shields.io/lgtm/alerts/g/kala13x/libxutils.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/kala13x/libxutils/alerts/)
[![Language grade: C/C++](https://img.shields.io/lgtm/grade/cpp/g/kala13x/libxutils.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/kala13x/libxutils/context:cpp)

## libxutils - C/C++ Utility Library release 2.x
The `xUtils` C Library is the general-purpose, cross-platform library for all Linux/Unix and Windows operating systems. It provides the safer implementations of various functions to make some routines easier for all the programs written in C and C-compatible languages such as C++, Rust, and Objective C. This directory contains the sources of the xUtils C Library. See the file `src/xver.h` to check out what release version you have.

### Here is an incomplete list of what this library offers.
#### Containrers:
- [Dynamically allocated array with sorting and search features](https://github.com/kala13x/libxutils/blob/main/src/array.h)
- [Dynamically allocated byte and data buffers](https://github.com/kala13x/libxutils/blob/main/src/xbuf.h)
- [Dynamically allocated key-pair map](https://github.com/kala13x/libxutils/blob/main/src/xmap.h)
- [Dynamically allocated hash map](https://github.com/kala13x/libxutils/blob/main/src/hash.h)
- [Implementation of linked list](https://github.com/kala13x/libxutils/blob/main/src/list.h)

#### Network:
- [Advanced socket library](https://github.com/kala13x/libxutils/blob/main/src/sock.h)
- [Cross-platform event library](https://github.com/kala13x/libxutils/blob/main/src/event.h)
- [MDTP client/server library](https://github.com/kala13x/libxutils/blob/main/media/mdtp.h)
- [HTTP client/server library](https://github.com/kala13x/libxutils/blob/main/src/http.h)
- [REST API client/server library](https://github.com/kala13x/libxutils/blob/main/media/xapi.h)
- [RTP packet parser library](https://github.com/kala13x/libxutils/blob/main/media/rtp.h)

#### System:
- [Cross-platform file and directory manipulations](https://github.com/kala13x/libxutils/blob/main/src/xfs.h)
- [System time manipulation library](https://github.com/kala13x/libxutils/blob/main/src/xtime.h)
- [Performance monitoring library](https://github.com/kala13x/libxutils/blob/main/src/xtop.h)
- [Advanced logging library](https://github.com/kala13x/libxutils/blob/main/src/xlog.h)

#### Various implementations:
- [JSON parser and writer library with lint and minify](https://github.com/kala13x/libxutils/blob/main/src/xjson.h)
- [Implementation of various encrypt/decrypt algorithms](https://github.com/kala13x/libxutils/blob/main/src/crypt.h)
- [Command Line interface manipulations](https://github.com/kala13x/libxutils/blob/main/src/xcli.h)
- [Cross-platform synchronization library](https://github.com/kala13x/libxutils/blob/main/src/sync.h)
- [Cross-platform thread library](https://github.com/kala13x/libxutils/blob/main/src/thread.h)
- [String manipulation library](https://github.com/kala13x/libxutils/blob/main/src/xstr.h)


### Installation
There are several ways to build and install the project.

#### Using CMake
If you have a `CMake` tool installed in your operating system, this is probably the easiest and best way to build a project:
```
git clone https://github.com/kala13x/libxutils.git
cd libxutils
mkdir build && cd build
cmake .. && make
sudo make install
```

#### Using SMake
[SMake](https://github.com/kala13x/smake) is a simple Makefile generator tool for the Linux/Unix operating systems:
```
git clone https://github.com/kala13x/libxutils.git
cd libxutils
smake && make
sudo make install
```

#### Using Makefile
If you do not have any of the above tools installed on your operating system,\
then you can try the build by using already generated `Makefile` for linux.
```
git clone https://github.com/kala13x/libxutils.git
cd libxutils
make
sudo make install
```

### Dependencies
The only dependency that the library uses is the `openssl-devel` package for the `SSL` implementations.\
You can either install the `openssl-devel` package or disable the `SSL` support in the library.

#### Install OpenSSL development package
Red-Hat family: `sudo dnf install openssl-devel`\
Debian family: `sudo apt-get install libssl-dev`

#### Disable SSL support in the library
If you use the `CMake` tool for building the project, you do not need to disable anything manually,\
`CMake` will automatically disable SSL support if the OpenSSL library is not installed in the system.

If you use `Makefile` to build the project, all you need to adjust `CFLAGS` and `LIBS` in `Makefile`.
- Remove `-D_XUTILS_USE_SSL` entry from the `CFLAGS`.
- Remove `-lssl` and `-lcrypto` entries from the `LIBS`.

### Usage
If you want to use the library, include the required `<xutils/*.h>` header files in your source code and\
use `-lxutils` linker flag while compiling your project. See the example directory for more information.

### Examples
The project includes several examples and tools in the `examples` directory.\
Currently, the examples can be built only by using Makefile on Linux.

```
cd examples
make
```

There is also possible to install the tools from the example directory in the operating system.\
After building the examples, run `sudo make install` command to install following tools in the system:

- `xcrypt` - File and text encrypt/decrypt tool for CLI
- `xpass` - Secure password manager tool for CLI
- `xjson` - JSON linter and minifier tool for CLI
- `xhttp` - Powerful HTTP client tool for CLI
- `xtop` - Advanced resource monithor for CLI
- `xsrc` - Advanced file search tool for CLI

Run each of this tool with `-h` argument to check out the usage and version information.
