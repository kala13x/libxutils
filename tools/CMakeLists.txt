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
list(APPEND EXTRA_LIBS xutils)

# if OpenSSL is found, add the definitions and libraries
find_package(OpenSSL)
if(OpenSSL_FOUND)
  add_definitions(-D_XUTILS_USE_SSL)
  list(APPEND EXTRA_LIBS ${OPENSSL_LIBRARIES})
endif()

# replace with your actual source files
set(SOURCE_FILES 
    xutils.c
    xcrypt.c
    xpass.c
    xhttp.c
    xjson.c
    xtop.c
    xsrc.c)

foreach(sourcefile ${SOURCE_FILES})
    string(REPLACE ".c" "" execname ${sourcefile})
    add_executable(${execname} ${sourcefile})
    target_link_libraries(${execname} Threads::Threads ${EXTRA_LIBS})
endforeach()

# install targets
install(TARGETS xutils xcrypt xpass xjson xhttp xtop xsrc 
        RUNTIME DESTINATION /usr/local/bin)

