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
    ${SOURCE_DIR}/xver.c
    ${DATA_DIR}/array.c
    ${DATA_DIR}/xjson.c
    ${DATA_DIR}/xbuf.c
    ${DATA_DIR}/hash.c
    ${DATA_DIR}/list.c
    ${DATA_DIR}/xstr.c
    ${DATA_DIR}/map.c
    ${DATA_DIR}/jwt.c
    ${CRYPT_DIR}/crypt.c
    ${CRYPT_DIR}/aes.c
    ${CRYPT_DIR}/base64.c
    ${CRYPT_DIR}/sha256.c
    ${CRYPT_DIR}/sha1.c
    ${CRYPT_DIR}/crc32.c
    ${CRYPT_DIR}/hmac.c
    ${CRYPT_DIR}/md5.c
    ${CRYPT_DIR}/rsa.c
    ${NET_DIR}/event.c
    ${NET_DIR}/addr.c
    ${NET_DIR}/http.c
    ${NET_DIR}/mdtp.c
    ${NET_DIR}/sock.c
    ${NET_DIR}/api.c
    ${NET_DIR}/ntp.c
    ${NET_DIR}/rtp.c
    ${NET_DIR}/ws.c
    ${SYS_DIR}/thread.c
    ${SYS_DIR}/xtype.c
    ${SYS_DIR}/xtime.c
    ${SYS_DIR}/sync.c
    ${SYS_DIR}/xsig.c
    ${SYS_DIR}/xcli.c
    ${SYS_DIR}/xcpu.c
    ${SYS_DIR}/xlog.c
    ${SYS_DIR}/xtop.c
    ${SYS_DIR}/xfs.c
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