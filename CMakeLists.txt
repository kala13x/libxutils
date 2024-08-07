cmake_minimum_required(VERSION 3.2.0 FATAL_ERROR)
project(libxutils LANGUAGES C)

IF (WIN32)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /Od /W3")
ELSE()
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O2 -Wall")
ENDIF()

option(EXAMPLES "Build examples" ON)
option(TOOLS "Build tools" ON)

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

if(EXAMPLES)
  add_subdirectory(examples)
endif()

if(TOOLS)
  add_subdirectory(tools)
endif()

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