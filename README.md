[![MIT License](https://img.shields.io/badge/License-MIT-brightgreen.svg?)](https://github.com/kala13x/libxutils/blob/main/LICENSE)
[![Linux](https://github.com/kala13x/libxutils/actions/workflows/linux.yml/badge.svg)](https://github.com/kala13x/libxutils/actions/workflows/linux.yml)
[![MacOS](https://github.com/kala13x/libxutils/actions/workflows/macos.yml/badge.svg)](https://github.com/kala13x/libxutils/actions/workflows/macos.yml)
[![Windows](https://github.com/kala13x/libxutils/actions/workflows/windows.yml/badge.svg)](https://github.com/kala13x/libxutils/actions/workflows/windows.yml)
[![CodeQL](https://github.com/kala13x/libxutils/actions/workflows/codeql.yml/badge.svg)](https://github.com/kala13x/libxutils/actions/workflows/codeql.yml)

## libxutils - Cross-platform C library release 2.x

`libxutils` is a cross-platform `C` library that provides safer implementations of various functionality to make routine tasks easier for programs written in `C` and it's compatible languages like `C++`, `Rust`, and `Objective C`. The library offers a range of features including containers, data structures, network tools, cryptography algorithms, string manipulations, system utilities, `HTTP/S` & `WS/WSS` client/server, `JSON` parser/serializer, `JWT` tokens, and etc. A list of key features can be found below.

`libxutils` is available for `Linux`, `Unix` and `Windows` operating systems and is released under the `MIT` License. To check the version of the library you have, you can refer to the file `src/xver.h`. Please note that the list of features provided in the README is incomplete. For more information about the full range of features, you can refer to the individual header files linked below or browse the source code.

#### Containrers:
- [Dynamically allocated array with sort and search features](https://github.com/kala13x/libxutils/blob/main/src/data/array.h)
- [Dynamically allocated byte and data buffers](https://github.com/kala13x/libxutils/blob/main/src/data/buf.h)
- [Dynamically allocated key-value pair map](https://github.com/kala13x/libxutils/blob/main/src/data/map.h)
- [Dynamically allocated hash map](https://github.com/kala13x/libxutils/blob/main/src/data/hash.h)
- [Dynamically allocated C string](https://github.com/kala13x/libxutils/blob/main/src/data/str.h)
- [Implementation of linked list](https://github.com/kala13x/libxutils/blob/main/src/data/list.h)

#### Network:
- [Event based client/server API for HTTP/S, WS/WSS and UNIX/TCP connections](https://github.com/kala13x/libxutils/blob/main/src/net/api.h)
- [Cross-platform socket library with RAW, UNIX, TCP, UDP and SSL support](https://github.com/kala13x/libxutils/blob/main/src/net/sock.h)
- [Cross-platform event library based on poll(), epoll() and WSAPoll()](https://github.com/kala13x/libxutils/blob/main/src/net/event.h)
- [Web Socket client/server library](https://github.com/kala13x/libxutils/blob/main/src/net/ws.h)
- [HTTP client/server library](https://github.com/kala13x/libxutils/blob/main/src/net/http.h)
- [MDTP client/server library](https://github.com/kala13x/libxutils/blob/main/src/net/mdtp.h)
- [RTP packet parser library](https://github.com/kala13x/libxutils/blob/main/src/net/rtp.h)

#### Cryptography:
- [Implementation of various encrypt/decrypt algorithms](https://github.com/kala13x/libxutils/blob/main/src/crypt/crypt.h)
- [Base64 and Base64Url encrypt/decrypt functions](https://github.com/kala13x/libxutils/blob/main/src/crypt/base64.h)
- [AES based on tiny-AES-c, added CBC and SIV modes with CMAC and S2V support](https://github.com/kala13x/libxutils/blob/main/src/crypt/aes.h)
- [Implementation of HMAC algorithm with SHA256 and MD5](https://github.com/kala13x/libxutils/blob/main/src/crypt/hmac.h)
- [RSA implementation based on OpenSSL library](https://github.com/kala13x/libxutils/blob/main/src/crypt/rsa.h)
- [Implementation of SHA256 calculations](https://github.com/kala13x/libxutils/blob/main/src/crypt/sha256.h)
- [Implementation of SHA1 calculations](https://github.com/kala13x/libxutils/blob/main/src/crypt/sha1.h)
- [Implementation of CRC32 calculations](https://github.com/kala13x/libxutils/blob/main/src/crypt/crc32.h)
- [Implementation of MD5 calculations](https://github.com/kala13x/libxutils/blob/main/src/crypt/md5.h)

#### System:
- [Cross-platform file and directory operations](https://github.com/kala13x/libxutils/blob/main/src/sys/xfs.h)
- [Cross-platform CPU affinity manipulation](https://github.com/kala13x/libxutils/blob/main/src/sys/cpu.h)
- [Implementation of advanced file search](https://github.com/kala13x/libxutils/blob/main/src/sys/srch.h)
- [System time manipulation library](https://github.com/kala13x/libxutils/blob/main/src/sys/xtime.h)
- [Performance monitoring library](https://github.com/kala13x/libxutils/blob/main/src/sys/mon.h)
- [Simple and fast memory pool](https://github.com/kala13x/libxutils/blob/main/src/sys/pool.h)
- [Advanced logging library](https://github.com/kala13x/libxutils/blob/main/src/sys/log.h)

#### Miscellaneous:
- [JSON parser and writer library with lint and minify support](https://github.com/kala13x/libxutils/blob/main/src/data/json.h)
- [Implementation of JSON Web Tokens with HS256 and RS256](https://github.com/kala13x/libxutils/blob/main/src/data/jwt.h)
- [Cross-platform synchronization library](https://github.com/kala13x/libxutils/blob/main/src/sys/sync.h)
- [Cross-platform multithreading library](https://github.com/kala13x/libxutils/blob/main/src/sys/thread.h)
- [Command-Line interface operations](https://github.com/kala13x/libxutils/blob/main/src/sys/cli.h)
- [C String manipulation library](https://github.com/kala13x/libxutils/blob/main/src/data/str.h)

### Installation
There are several ways to build and install the project.

#### Using included script (recommended on Linux/Mac).
A relatively simple way to build and install the libary and tools is to use the included build script:

```bash
git clone https://github.com/kala13x/libxutils && ./libxutils/build.sh --install
```

List options that build script supports:

- `--tool=<tool>` Specify `Makefile` generation tool or use included `Makefile`.
- `--install` Install library and the tools after the build.
- `--examples` Include examples in the build.
- `--tools` Include tools in the build.
- `--clean` Cleanup object files after build/installation.

You can either choose `cmake`, `smake` or `make` as the tool argument.
If the tool will not be specified the script will use `cmake` as default.

#### Using CMake
If you have a `CMake` tool installed in your operating system, here is how project can be built and installed using `cmake`:

```bash
git clone https://github.com/kala13x/libxutils
cd libxutils
cmake . && make
sudo make install
```

#### Using SMake
[SMake](https://github.com/kala13x/smake) is a simple Makefile generator tool for the Linux/Unix operating systems:

```bash
git clone https://github.com/kala13x/libxutils
cd libxutils
smake && make
sudo make install
```

#### Using Makefile
The project can also be built with a pre-generated `Makefile` for the Linux.

```bash
export XUTILS_USE_SSL=y # In case you want to use SSL features
git clone https://github.com/kala13x/libxutils
cd libxutils
make
sudo make install
```

### Dependencies
The only dependency that the library uses is the `openssl-devel` package for the `SSL` and `RSA` implementations.\
If `openssl` package is not installed on the system, the lib will automatically be compiled without `SSL` support.

#### Install OpenSSL development package
Red-Hat family: `sudo dnf install openssl-devel`\
Debian family: `sudo apt-get install libssl-dev`\
Windows: `choco install openssl`\
MacOS: `brew install openssl`

### Usage
Just include the required `<xutils/*.h>` header files in your source code and use `-lxutils`\
linker flag while compiling your project. See the example directory for more information.

### Tools & Examples
Use the included script to build or install CLI apps from the `tools` directory.\
The script can be used to build the sources from the `examples` directory as well.

```bash
./libxutils/build.sh --tools --examples
```

These sources can also be built by using the `CMake` tool or `Makefile` from that directory.\
You may need to export the SSL flag accordingly if you are doing a build without the script:

```bash
export XUTILS_USE_SSL=y # In case you want to use SSL features
cd examples
cmake .
make
```

#### XTOP and more

<p align="center">
    <img src="https://raw.githubusercontent.com/kala13x/libxutils/main/examples/xtop.png" alt="alternate text">
</p>

`XTOP` is `HTOP` like performance monitor that supports to monitor CPU, memory, and network traffic into a single window. In addition, it has powerful `REST API` client/server mode and much more.

After building the sources in `tools` directory, run `sudo make install` command to install following apps in the system:

- `xcrypt` - File and text encrypt/decrypt tool for CLI
- `xpass` - Secure password manager tool for CLI
- `xjson` - JSON linter and minifier tool for CLI
- `xhttp` - Powerful HTTP client tool for CLI
- `xhost` - CLI tool to search and modify hosts
- `xtop` - Advanced resource monithor for CLI
- `xsrc` - Advanced file search tool for CLI

Run each of this tool with `-h` argument to check out the usage and version information.
