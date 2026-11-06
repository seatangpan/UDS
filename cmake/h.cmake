# --------------------------------------------------------------------------------------------------
# 部署 Qt 运行时到可执行文件目录（windeployqt）
function(h_deploy_qt target)
  if(NOT WIN32)
    return()
  endif()

  get_target_property(qt_core_type Qt6::Core TYPE)
  if(qt_core_type STREQUAL "STATIC_LIBRARY")
    return()
  endif()

  if(NOT DEFINED WINDEPLOYQT_EXECUTABLE)
    get_target_property(qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(qt_bin_dir "${qmake_executable}" DIRECTORY)
    set(WINDEPLOYQT_EXECUTABLE "${qt_bin_dir}/windeployqt.exe")
  endif()

  if(NOT EXISTS "${WINDEPLOYQT_EXECUTABLE}")
    message(WARNING "[UDS] windeployqt not found at: ${WINDEPLOYQT_EXECUTABLE}")
    return()
  endif()

  set(deploy_args --no-translations --no-opengl-sw --no-system-d3d-compiler)
  if(MSVC)
    list(APPEND deploy_args --no-compiler-runtime)
  endif()
  if(NOT "${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    list(APPEND deploy_args --release)
  endif()

  add_custom_command(
    TARGET ${target}
    POST_BUILD
    COMMAND ${WINDEPLOYQT_EXECUTABLE} $<TARGET_FILE:${target}> ${deploy_args}
    COMMENT "部署 Qt 运行时..."
    VERBATIM)
endfunction()

# --------------------------------------------------------------------------------------------------
# 生成 zip 包，需先设置 H_ASSET_NAME
function(h_generate_zip target)
  if(NOT WIN32)
    return()
  endif()

  add_custom_target(
    ${target}_zip
    DEPENDS ${target}
    COMMAND ${CMAKE_COMMAND} -E rm -f ${target}/${target}.pdb
    COMMAND ${CMAKE_COMMAND} -E make_directory ${target}-zip
    COMMAND ${CMAKE_COMMAND} -E tar "cfv" ${target}-zip/${H_ASSET_NAME}.zip --format=zip ${target}
    WORKING_DIRECTORY "${H_BINS_DIR}"
    COMMENT "打包 ${H_ASSET_NAME}.zip")
endfunction()
