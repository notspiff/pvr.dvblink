project(pvr.dvblink)

cmake_minimum_required(VERSION 2.6)

enable_language(CXX)

find_package(xbmc REQUIRED)
find_package(xbmcplatform REQUIRED)

include_directories(${xbmcplatform_INCLUDE_DIRS}
                    ${XBMC_INCLUDE_DIR})

add_definitions(-D__STDC_FORMAT_MACROS)

set(DVBLINK_SOURCES src/base64.cpp
                    src/client.cpp
                    src/DVBLinkClient.cpp
                    src/HttpPostClient.cpp
                    src/TimeShiftBuffer.cpp)

add_subdirectory(lib/libdvblinkremote)
set(DEPLIBS ${xbmcplatform_LIBRARIES} dvblinkremote)
build_addon(pvr.dvblink DVBLINK DEPLIBS)

include(CPack)
