project(pvr.dvblink)

cmake_minimum_required(VERSION 2.6)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR})

enable_language(CXX)

find_package(xbmc REQUIRED)
find_package(xbmcplatform REQUIRED)
find_package(TinyXML2 REQUIRED)

include_directories(${xbmcplatform_INCLUDE_DIRS}
                    ${XBMC_INCLUDE_DIR}
                    ${TINYXML2_INCLUDE_DIRS}
                    ${PROJECT_SOURCE_DIR}/lib)

add_definitions(-D__STDC_FORMAT_MACROS)

set(DVBLINK_SOURCES src/base64.cpp
                    src/client.cpp
                    src/DVBLinkClient.cpp
                    src/HttpPostClient.cpp
                    src/TimeShiftBuffer.cpp)

add_subdirectory(lib/libdvblinkremote)
set(DEPLIBS ${xbmcplatform_LIBRARIES} dvblinkremote ${TINYXML2_LIBRARIES})
build_addon(pvr.dvblink DVBLINK DEPLIBS)

include(CPack)
