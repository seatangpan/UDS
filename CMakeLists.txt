cmake_minimum_required(VERSION 3.22)

project(UDS
    VERSION 1.0.0
    LANGUAGES C CXX
    DESCRIPTION "UDS OTA 调试服务端")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/h.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/qifw/qifw.cmake)

set(H_APP_VERSION ${PROJECT_VERSION})
if(WIN32)
  set(H_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
  if(H_ARCH STREQUAL "AMD64")
    set(H_ARCH "x64")
  endif()
  string(TIMESTAMP H_BUILD_DATE "%Y%m%d")
  set(H_ASSET_NAME "${PROJECT_NAME}-${H_APP_VERSION}-${H_ARCH}-${H_BUILD_DATE}")
endif()

set(H_BINS_DIR "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${H_BINS_DIR}/${PROJECT_NAME}")

add_executable(UDS
    main.cpp
    mainwindow.cpp
    mainwindow.h
    canframe.h
    canthread.cpp
    canthread.h
    flashimage.cpp
    flashimage.h
    securityaccess.cpp
    securityaccess.h
    udsserver.cpp
    udsserver.h
    lib_iso15765/FIFOQUEUE.c
    lib_iso15765/FIFOQUEUE.h
    lib_iso15765/can_iso_tp.c
    lib_iso15765/can_iso_tp.h
    lib_iso15765/lib_iqueue.c
    lib_iso15765/lib_iqueue.h
    lib_iso15765/mcu_lock.c
    lib_iso15765/mcu_lock.h
)

target_include_directories(UDS PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/lib_iso15765
)

target_link_libraries(UDS PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    winmm
    ${CMAKE_CURRENT_SOURCE_DIR}/ControlCAN.lib
)

if(WIN32)
    add_custom_command(TARGET UDS POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/ControlCAN.dll"
            "$<TARGET_FILE_DIR:UDS>/ControlCAN.dll"
    )

    h_deploy_qt(UDS)
    h_generate_zip(UDS)
    h_generate_installer(UDS ${H_APP_VERSION})
endif()
