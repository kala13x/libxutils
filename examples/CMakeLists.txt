cmake_minimum_required(VERSION 3.10)
project(xutils)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O2 -Wall")
set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)

set(XUTILS_INCLUDE_DIRS
  ../src/crypt 
  ../src/data
  ../src/net
  ../src/sys
  ../src
)

include_directories(${XUTILS_INCLUDE_DIRS})
find_package(Threads REQUIRED)

set(XUTILS_LIBRARY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../build)
set(XUTILS_STATIC_LIB ${XUTILS_LIBRARY_ROOT}/libxutils.a)
set(XUTILS_SHARED_LIB ${XUTILS_LIBRARY_ROOT}/libxutils.so)

# Check if static library exists
if(EXISTS ${XUTILS_STATIC_LIB})
    list(APPEND EXTRA_LIBS ${XUTILS_STATIC_LIB})
elseif(EXISTS ${XUTILS_SHARED_LIB})
    list(APPEND EXTRA_LIBS ${XUTILS_SHARED_LIB})
else()
    message(FATAL_ERROR "Neither static nor shared library found for xutils.")
endif()

# if OpenSSL is found, add the definitions and libraries
set(XUTILS_USE_SSL $ENV{XUTILS_USE_SSL})
if(XUTILS_USE_SSL STREQUAL "y")
    find_package(OpenSSL)
    if(OpenSSL_FOUND)
        add_definitions(-D_XUTILS_USE_SSL)
        list(APPEND EXTRA_LIBS ${OPENSSL_LIBRARIES})
    endif()
endif()

# replace with your actual source files
set(SOURCE_FILES 
    array.c
    async-server.c
    async-client.c
    basic-server.c
    http-server.c
    ws-server.c
    ws-client.c
    statcov.c
    strings.c
    thread.c
    events.c
    files.c
    json.c
    log.c
    list.c
    ntp.c
    jwt.c
    rsa.c)

foreach(sourcefile ${SOURCE_FILES})
    string(REPLACE ".c" "" execname ${sourcefile})
    add_executable(${execname} ${sourcefile})
    target_link_libraries(${execname} Threads::Threads ${EXTRA_LIBS})
endforeach()

