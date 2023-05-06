[![MIT License](https://img.shields.io/badge/License-MIT-brightgreen.svg?)](https://github.com/kala13x/libxutils/blob/main/LICENSE)
[![C/C++ CI](https://github.com/kala13x/libxutils/actions/workflows/make.yml/badge.svg)](https://github.com/kala13x/libxutils/actions/workflows/make.yml)
[![Build workflow](https://github.com/kala13x/libxutils/actions/workflows/build_libxutils.yml/badge.svg)](https://github.com/kala13x/libxutils/actions)
[![CodeQL](https://github.com/kala13x/libxutils/actions/workflows/codeql.yml/badge.svg)](https://github.com/kala13x/libxutils/actions/workflows/codeql.yml)
[![Coverity Scan Build Status](https://scan.coverity.com/projects/25173/badge.svg?)](https://scan.coverity.com/projects/libxutils)

## libxutils - Cross-platform C library release 2.x

libxutils is a cross-platform C library that provides safer implementations of various functions to make routine tasks easier for programs written in C and C-compatible languages like C++, Rust, and Objective C. The library offers a range of features including containers, network tools, cryptography algorithms, and system utilities. Some of the specific features include a dynamically allocated array with sort and search features, a byte and data buffer, a key-value pair map, a hash map, and a linked list. On the network side, the library provides an event library based on poll(), epoll(), and WSAPoll(), a socket library with TCP, UDP, and SSL support, an event-based high performance REST API client/server library, an HTTP client/server library, an MDTP client/server library, and an RTP packet parser library. Finally, the library includes cross-platform file and directory manipulation tools, a CPU affinity manipulation API, an advanced file search API, and a system time manipulation library.

libxutils is available for both Linux/Unix and Windows operating systems and is released under the MIT License. To check the version of libxutils you have, you can refer to the file `src/xver.h`. Please note that the list of features provided in the README is incomplete. For more information about the full range of features offered by libxutils, you can refer to the individual header files linked below or browse the library's source code.

#### Containrers:
- [Dynamically allocated array with sort and search features](https://github.com/kala13x/libxutils/blob/main/src/data/array.h)
- [Dynamically allocated byte and data buffers](https://github.com/kala13x/libxutils/blob/main/src/data/xbuf.h)
- [Dynamically allocated key-value pair map](https://github.com/kala13x/libxutils/blob/main/src/data/map.h)
- [Dynamically allocated hash map](https://github.com/kala13x/libxutils/blob/main/src/data/hash.h)
- [Dynamically allocated C string](https://github.com/kala13x/libxutils/blob/main/src/data/xstr.h)
- [Implementation of linked list](https://github.com/kala13x/libxutils/blob/main/src/data/list.h)

#### Network:
- [Cross-platform event library based on poll(), epoll() and WSAPoll()](https://github.com/kala13x/libxutils/blob/main/src/net/event.h)
- [Cross-platform socket library with TCP, UDP and SSL support](https://github.com/kala13x/libxutils/blob/main/src/net/sock.h)
- [Event based high performance REST API client/server library](https://github.com/kala13x/libxutils/blob/main/src/net/api.h)
- [Web Socket client/server library](https://github.com/kala13x/libxutils/blob/main/src/net/ws.h)
- [HTTP client/server library](https://github.com/kala13x/libxutils/blob/main/src/net/http.h)
- [MDTP client/server library](https://github.com/kala13x/libxutils/blob/main/src/net/mdtp.h)
- [RTP packet parser library](https://github.com/kala13x/libxutils/blob/main/src/net/rtp.h)

#### Cryptography:
- [Implementation of various encrypt/decrypt algorithms](https://github.com/kala13x/libxutils/blob/main/src/crypt/crypt.h)
- [Base64 and Base64Url encrypt/decrypt functions](https://github.com/kala13x/libxutils/blob/main/src/crypt/base64.h)
- [AES based on FIPS-197 implementation by Christophe Devine](https://github.com/kala13x/libxutils/blob/main/src/crypt/aes.h)
- [Implementation of HMAC algorithm with SHA256 and MD5](https://github.com/kala13x/libxutils/blob/main/src/crypt/hmac.h)
- [RSA implementation based on openssl library](https://github.com/kala13x/libxutils/blob/main/src/crypt/rsa.h)
- [Implementation of SHA256 calculations](https://github.com/kala13x/libxutils/blob/main/src/crypt/sha256.h)
- [Implementation of CRC32 calculations](https://github.com/kala13x/libxutils/blob/main/src/crypt/crc32.h)
- [Implementation of MD5 calculations](https://github.com/kala13x/libxutils/blob/main/src/crypt/md5.h)

#### System:
- [Cross-platform file and directory manipulations](https://github.com/kala13x/libxutils/blob/main/src/sys/xfs.h)
- [Cross-platform CPU affinity manipulation API](https://github.com/kala13x/libxutils/blob/main/src/sys/xcpu.h)
- [Implementation of advanced file search API](https://github.com/kala13x/libxutils/blob/main/src/sys/xfs.h)
- [System time manipulation library](https://github.com/kala13x/libxutils/blob/main/src/sys/xtime.h)
- [Performance monitoring library](https://github.com/kala13x/libxutils/blob/main/src/sys/xtop.h)
- [Advanced logging library](https://github.com/kala13x/libxutils/blob/main/src/sys/xlog.h)

#### Miscellaneous:
- [JSON parser and writer library with lint and minify support](https://github.com/kala13x/libxutils/blob/main/src/data/xjson.h)
- [Implementation of JSON Web Tokens with HS256 and RS256](https://github.com/kala13x/libxutils/blob/main/src/data/jwt.h)
- [Command-Line interface manipulations](https://github.com/kala13x/libxutils/blob/main/src/sys/xcli.h)
- [Cross-platform synchronization library](https://github.com/kala13x/libxutils/blob/main/src/sys/sync.h)
- [Cross-platform multithreading library](https://github.com/kala13x/libxutils/blob/main/src/sys/thread.h)
- [String manipulation library](https://github.com/kala13x/libxutils/blob/main/src/data/xstr.h)

### Installation
There are several ways to build and install the project.

#### Using CMake
If you have a `CMake` tool installed in your operating system, this is probably the easiest and best way to build a project:

```bash
git clone https://github.com/kala13x/libxutils.git
cd libxutils
mkdir build && cd build
cmake .. && make
sudo make install
```

#### Using SMake
[SMake](https://github.com/kala13x/smake) is a simple Makefile generator tool for the Linux/Unix operating systems:

```bash
git clone https://github.com/kala13x/libxutils.git
cd libxutils
smake && make
sudo make install
```

#### Using Makefile
If you do not have any of the above tools installed on your operating system,\
then you can try the build by using already generated `Makefile` for linux.

```bash
git clone https://github.com/kala13x/libxutils.git
cd libxutils
make
sudo make install
```

### Dependencies
The only dependency that the library uses is the `openssl-devel` package for the `SSL` and `RSA` implementations.\
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

### Tools & Examples

The project includes several examples and tools in the `examples` directory.\
Currently, the examples can be built only by using `Makefile` from that directory.

```bash
cd examples
make
```

#### XTOP and more

<p align="center">
    <img src="https://raw.githubusercontent.com/kala13x/libxutils/main/examples/xtop.png" alt="alternate text">
</p>

`XTOP` is `HTOP` like performance monitor that supports to monitor CPU, memory, and network traffic into a single window. In addition, it has powerful `REST API` client/server mode and much more.

After building the sources in example directory, run `sudo make install` command to install following tools in the system:

- `xcrypt` - File and text encrypt/decrypt tool for CLI
- `xpass` - Secure password manager tool for CLI
- `xjson` - JSON linter and minifier tool for CLI
- `xhttp` - Powerful HTTP client tool for CLI
- `xtop` - Advanced resource monithor for CLI
- `xsrc` - Advanced file search tool for CLI

Run each of this tool with `-h` argument to check out the usage and version information.
