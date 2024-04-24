cmake_minimum_required(VERSION 3.2.0 FATAL_ERROR)
project(libxutils LANGUAGES C)

IF (WIN32)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /Od /W3")
ELSE()
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O2 -Wall")
ENDIF()

# TODO: CMAKE very smart and can count on usage CMAKE_INSTALL_PREFIX variable
#set(XUTILS_INSTALL_PREFIX "/usr/local")
#string(SUBSTRING "${XUTILS_INSTALL_PREFIX}" 0 2 PREFIX_START)
#
#if("${PREFIX_START}" STREQUAL "./")
#  get_filename_component(CURRENT_WORKING_DIR "${CMAKE_SOURCE_DIR}" REALPATH)
#  string(SUBSTRING "${XUTILS_INSTALL_PREFIX}" 1 -1 RELATIVE_PATH)
#  set(CMAKE_INSTALL_PREFIX "${CURRENT_WORKING_DIR}${RELATIVE_PATH}")
#endif()

# TODO: looks like it is not used
#set(THREADS_PREFER_PTHREAD_FLAG ON)
#find_package(Threads REQUIRED)

find_package(OpenSSL)
if(OpenSSL_FOUND)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_XUTILS_USE_SSL")
endif()

include_directories(
    src)

file(GLOB_RECURSE xutils_SOURCE src/*.c)
add_library(xutils STATIC
    ${xutils_SOURCE})

target_link_libraries(xutils
    PUBLIC
    OpenSSL::SSL)

include(GNUInstallDirs)
install(TARGETS xutils EXPORT xutils
        ARCHIVE DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR})

install(DIRECTORY
    src/
    DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}/xutils
    FILES_MATCHING
    PATTERN "*.h")