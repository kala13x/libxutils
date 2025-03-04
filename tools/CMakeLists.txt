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
        include_directories(${OPENSSL_INCLUDE_DIR})
        add_definitions(-D_XUTILS_USE_SSL)
        list(APPEND EXTRA_LIBS ${OPENSSL_LIBRARIES})
    endif()
endif()

# replace with your actual source files
set(SOURCE_FILES 
    xutils.c
    xcrypt.c
    xpass.c
    xhttp.c
    xhost.c
    xjson.c
    xtop.c
    xsrc.c)

foreach(sourcefile ${SOURCE_FILES})
    string(REPLACE ".c" "" execname ${sourcefile})
    add_executable(${execname} ${sourcefile})
    target_link_libraries(${execname} Threads::Threads ${EXTRA_LIBS})
endforeach()

# install targets
install(TARGETS xutils xcrypt xpass xjson xhttp xhost xtop xsrc
        RUNTIME DESTINATION /usr/local/bin)

