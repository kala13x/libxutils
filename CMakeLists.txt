cmake_minimum_required(VERSION 3.2.0 FATAL_ERROR)
project(libxutils LANGUAGES C)

IF (WIN32)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /Od /W3")
ELSE()
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O2 -Wall")
ENDIF()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(OpenSSL)
IF(OpenSSL_FOUND)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_XUTILS_USE_SSL")
ENDIF()

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
    ./src/data/jwt.c
    ./src/data/list.c
    ./src/data/map.c
    ./src/data/xbuf.c
    ./src/data/xjson.c
    ./src/data/xstr.c
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
    ./src/sys/thread.c
    ./src/sys/xcli.c
    ./src/sys/xcpu.c
    ./src/sys/xfs.c
    ./src/sys/xlog.c
    ./src/sys/xsig.c
    ./src/sys/xtime.c
    ./src/sys/xtop.c
    ./src/sys/xtype.c
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