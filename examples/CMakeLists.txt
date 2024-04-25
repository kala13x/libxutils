cmake_minimum_required(VERSION 3.10)
project(examples)

set(CMAKE_C_STANDARD 11)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O2 -Wall")

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)

# if OpenSSL is found, add the definitions and libraries
set(XUTILS_USE_SSL $ENV{XUTILS_USE_SSL})
if(XUTILS_USE_SSL STREQUAL "y")
    find_package(OpenSSL)
    if(OpenSSL_FOUND)
        add_definitions(-D_XUTILS_USE_SSL)
        list(APPEND EXTRA_LIBS OpenSSL::SSL)#${OPENSSL_LIBRARIES})
    endif()
endif()

# this is a guarantee that we will use library packages outside of this project well
include_directories(${CMAKE_INSTALL_PREFIX}/include)
set(EXAMPLES_NAMES
    array.c
    events.c
    files.c
    http-server.c
    json.c
    jwt.c
    list.c
    ntp.c
    rsa.c
    statcov.c
    strings.c
    tcp-server.c
    thread.c
    ws-client.c
    ws-server.c
    xlog.c)

include(GNUInstallDirs)
foreach(sourcefile ${EXAMPLES_NAMES})
    string(REPLACE ".c" "" execname ${sourcefile})
    add_executable(${execname} ${sourcefile})
    target_link_libraries(${execname}
            -L${CMAKE_INSTALL_PREFIX}/lib
            xutils
            Threads::Threads
            ${EXTRA_LIBS})

    add_dependencies(${execname} xutils)

    install(TARGETS ${execname} EXPORT examples
            ARCHIVE DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR})
endforeach()

