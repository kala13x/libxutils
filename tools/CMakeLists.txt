cmake_minimum_required(VERSION 3.10)
project(tools)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O2 -Wall")

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)

include_directories(${CMAKE_INSTALL_PREFIX}/include)

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
    xutils.c
    xcrypt.c
    xpass.c
    xhttp.c
    xjson.c
    xtop.c
    xsrc.c)

include(GNUInstallDirs)
foreach(sourcefile ${SOURCE_FILES})
    string(REPLACE ".c" "" execname ${sourcefile})
    add_executable(${execname}_tool ${sourcefile})
    target_link_libraries(${execname}_tool
            -L${CMAKE_INSTALL_PREFIX}/lib
            xutils
            Threads::Threads
            ${EXTRA_LIBS})

    add_dependencies(${execname}_tool xutils)

    install(TARGETS ${execname}_tool EXPORT tools
            ARCHIVE DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR})
endforeach()

