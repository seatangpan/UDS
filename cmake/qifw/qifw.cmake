set(H_QIFW_LATEST_VERSION "none")
set(QT_DIR "${Qt6_DIR}")

if(DEFINED ENV{QIFW_PATH})
  set(QIFW_ROOT_DIR $ENV{QIFW_PATH})
  message(STATUS "[QIFW] Using custom QIFW path from environment: ${QIFW_ROOT_DIR}")
elseif(EXISTS "${QT_DIR}/../../../../Tools/QtInstallerFramework")
  set(QIFW_ROOT_DIR "${QT_DIR}/../../../../Tools/QtInstallerFramework")
  message(STATUS "[QIFW] Using Qt-relative QIFW path: ${QIFW_ROOT_DIR}")
elseif(EXISTS "${QT_DIR}/../../../../../Tools/QtInstallerFramework")
  set(QIFW_ROOT_DIR "${QT_DIR}/../../../../../Tools/QtInstallerFramework")
  message(STATUS "[QIFW] Using Qt-relative QIFW path: ${QIFW_ROOT_DIR}")
endif()

if(DEFINED QIFW_ROOT_DIR AND EXISTS "${QIFW_ROOT_DIR}")
  file(GLOB qifw_versions "${QIFW_ROOT_DIR}/*")
  foreach(dir ${qifw_versions})
    if(IS_DIRECTORY ${dir})
      get_filename_component(version_dir ${dir} NAME)
      list(APPEND version_list ${version_dir})
      set(H_QIFW_LATEST_VERSION ${version_dir})
    endif()
  endforeach()

  message(STATUS "[QIFW] Available versions: ${version_list}")
  message(STATUS "[QIFW] Selected version: ${H_QIFW_LATEST_VERSION}")
  set(H_QIFW_VERSION
      ${H_QIFW_LATEST_VERSION}
      CACHE STRING "Using the version of Qt Installer Framework")
  set_property(CACHE H_QIFW_VERSION PROPERTY STRINGS ${version_list})
endif()

if(WIN32)
  if(DEFINED QIFW_ROOT_DIR AND EXISTS "${QIFW_ROOT_DIR}/${H_QIFW_VERSION}/bin/binarycreator.exe")
    set(binarycreator "${QIFW_ROOT_DIR}/${H_QIFW_VERSION}/bin/binarycreator.exe")
    message(STATUS "[QIFW] binarycreator path: ${binarycreator}")
  else()
    set(binarycreator "${QT_DIR}/../../../../../Tools/QtInstallerFramework/${H_QIFW_VERSION}/bin/binarycreator.exe")
    message(WARNING "[QIFW] Using fallback binarycreator path: ${binarycreator}")
  endif()
endif()

# --------------------------------------------------------------------------------------------------
# 生成安装包，需先设置 H_ASSET_NAME
function(h_generate_installer target version)
  if(NOT WIN32)
    return()
  endif()

  set(args "-DargTarget=${target}")
  list(APPEND args "-DargRootDir=${H_BINS_DIR}/${target}-installer")
  list(APPEND args "-DargVersion=${version}")
  list(APPEND args "-DargDataDir=$<TARGET_FILE_DIR:${target}>")
  list(APPEND args "-DargTemplateDir=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/installer")
  list(APPEND args "-DargBinaryCreator=${binarycreator}")
  list(APPEND args "-DargAssetName=${H_ASSET_NAME}")

  add_custom_target(
    ${target}_installer
    DEPENDS ${target}
    COMMAND ${CMAKE_COMMAND} ${args} -P ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/qifwscript.cmake
    COMMENT "生成 ${H_ASSET_NAME}-installer.exe")
endfunction()
