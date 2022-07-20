cmake_minimum_required(VERSION 3.2.0 FATAL_ERROR)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)
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

IF (APPLE)
find_package(RT REQUIRED)
ENDIF()

SET(HEADER_DST "include/xutils")
SET(SOURCE_DIR "./src")
SET(MEDIA_DIR "./media")
include_directories(${SOURCE_DIR} ${MEDIA_DIR})

add_library(xutils STATIC
    ${SOURCE_DIR}/xver.c
    ${SOURCE_DIR}/addr.c
    ${SOURCE_DIR}/array.c
    ${SOURCE_DIR}/crypt.c
    ${SOURCE_DIR}/errex.c
    ${SOURCE_DIR}/event.c
    ${SOURCE_DIR}/hash.c
    ${SOURCE_DIR}/http.c
    ${SOURCE_DIR}/list.c
    ${SOURCE_DIR}/sock.c
    ${SOURCE_DIR}/sync.c
    ${SOURCE_DIR}/thread.c
    ${SOURCE_DIR}/xaes.c
    ${SOURCE_DIR}/xbuf.c
    ${SOURCE_DIR}/xcli.c
    ${SOURCE_DIR}/xcpu.c
    ${SOURCE_DIR}/xfs.c
    ${SOURCE_DIR}/xjson.c
    ${SOURCE_DIR}/xlog.c
    ${SOURCE_DIR}/xmap.c
    ${SOURCE_DIR}/xstr.c
    ${SOURCE_DIR}/xtime.c
    ${SOURCE_DIR}/xtop.c
    ${SOURCE_DIR}/xtype.c
    ${MEDIA_DIR}/xapi.c
    ${MEDIA_DIR}/mdtp.c
    ${MEDIA_DIR}/ntp.c
    ${MEDIA_DIR}/rtp.c
    ${MEDIA_DIR}/ts.c
)

install(TARGETS xutils DESTINATION lib)

install(DIRECTORY "${SOURCE_DIR}/"
        DESTINATION "${HEADER_DST}"
        FILES_MATCHING
        PATTERN "*.h"
)

install(DIRECTORY "${MEDIA_DIR}/"
        DESTINATION "${HEADER_DST}"
        FILES_MATCHING
        PATTERN "*.h"
)