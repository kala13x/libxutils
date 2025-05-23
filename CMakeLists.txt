cmake_minimum_required(VERSION 3.5.0 FATAL_ERROR)
project(libxutils LANGUAGES C)

if (WIN32)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /Od /W3")
elseif(APPLE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O2 -Wall")
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O2 -Wall -D_XUTILS_USE_GNU -D_ASSERT_TIMED")
endif()

set(XUTILS_INSTALL_PREFIX "/usr/local")
string(SUBSTRING "${XUTILS_INSTALL_PREFIX}" 0 2 PREFIX_START)

if("${PREFIX_START}" STREQUAL "./")
  get_filename_component(CURRENT_WORKING_DIR "${CMAKE_SOURCE_DIR}" REALPATH)
  string(SUBSTRING "${XUTILS_INSTALL_PREFIX}" 1 -1 RELATIVE_PATH)
  set(CMAKE_INSTALL_PREFIX "${CURRENT_WORKING_DIR}${RELATIVE_PATH}")
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(OpenSSL)
if(OpenSSL_FOUND)
    include_directories(${OPENSSL_INCLUDE_DIR})
    link_directories(${OPENSSL_LIBRARIES})
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_XUTILS_USE_SSL")
endif()

SET(HEADER_DST "include/xutils")
SET(SOURCE_DIR "./src")
SET(CRYPT_DIR "./src/crypt")
SET(DATA_DIR "./src/data")
SET(NET_DIR "./src/net")
SET(SYS_DIR "./src/sys")

include_directories(
    ${SOURCE_DIR}
    ${CRYPT_DIR}
    ${DATA_DIR}
    ${NET_DIR}
    ${SYS_DIR}
)

add_library(xutils STATIC
    ./src/xver.c
    ./src/crypt/aes.c
    ./src/crypt/base64.c
    ./src/crypt/crc32.c
    ./src/crypt/crypt.c
    ./src/crypt/hmac.c
    ./src/crypt/md5.c
    ./src/crypt/rsa.c
    ./src/crypt/sha256.c
    ./src/crypt/sha1.c
    ./src/data/array.c
    ./src/data/hash.c
    ./src/data/json.c
    ./src/data/jwt.c
    ./src/data/list.c
    ./src/data/map.c
    ./src/data/buf.c
    ./src/data/str.c
    ./src/net/addr.c
    ./src/net/event.c
    ./src/net/http.c
    ./src/net/mdtp.c
    ./src/net/ntp.c
    ./src/net/rtp.c
    ./src/net/sock.c
    ./src/net/api.c
    ./src/net/ws.c
    ./src/sys/sync.c
    ./src/sys/pool.c
    ./src/sys/thread.c
    ./src/sys/type.c
    ./src/sys/cli.c
    ./src/sys/cpu.c
    ./src/sys/mon.c
    ./src/sys/log.c
    ./src/sys/sig.c
    ./src/sys/xfs.c
    ./src/sys/srch.c
    ./src/sys/xtime.c
)

install(TARGETS xutils DESTINATION lib)

install(DIRECTORY "${SOURCE_DIR}/"
    DESTINATION "${HEADER_DST}"
    FILES_MATCHING
    PATTERN "*.h"
)

install(DIRECTORY "${CRYPT_DIR}/"
    DESTINATION "${HEADER_DST}"
    FILES_MATCHING
    PATTERN "*.h"
)

install(DIRECTORY "${DATA_DIR}/"
    DESTINATION "${HEADER_DST}"
    FILES_MATCHING
    PATTERN "*.h"
)

install(DIRECTORY "${NET_DIR}/"
    DESTINATION "${HEADER_DST}"
    FILES_MATCHING
    PATTERN "*.h"
)

install(DIRECTORY "${SYS_DIR}/"
    DESTINATION "${HEADER_DST}"
    FILES_MATCHING
    PATTERN "*.h"
)