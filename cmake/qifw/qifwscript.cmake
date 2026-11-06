# 安装包生成脚本
# argTarget:       目标名称
# argRootDir:      安装包工作目录
# argVersion:      版本号
# argDataDir:      已部署的运行目录
# argTemplateDir:  安装包模板目录
# argBinaryCreator: Qt Installer Framework binarycreator
# argAssetName:    安装包文件名前缀

if(EXISTS "${argRootDir}")
  file(REMOVE_RECURSE "${argRootDir}")
endif()
file(MAKE_DIRECTORY "${argRootDir}")

file(COPY "${argTemplateDir}/" DESTINATION "${argRootDir}")
file(MAKE_DIRECTORY "${argRootDir}/packages/all/data")
file(COPY "${argDataDir}/" DESTINATION "${argRootDir}/packages/all/data")

string(TIMESTAMP current_date "%Y-%m-%d")

function(replace_tokens file_path)
  set(pairs ${ARGN})
  file(READ "${file_path}" content)
  while(pairs)
    list(POP_FRONT pairs token value)
    string(REPLACE "${token}" "${value}" content "${content}")
  endwhile()
  file(WRITE "${file_path}" "${content}")
endfunction()

replace_tokens("${argRootDir}/config/config.xml"
  argConfigName "${argTarget}"
  argConfigVersion "${argVersion}"
  argConfigTitle "${argTarget}"
  argConfigPublisher "HPM"
  argConfigStartMenuDir "${argTarget}"
  argConfigTargetDir "${argTarget}")

replace_tokens("${argRootDir}/packages/all/meta/package.xml"
  argPackageDisplayName "${argTarget}"
  argPackageDescription "UDS OTA 调试服务端"
  argPackageVersion "${argVersion}"
  argPackageReleaseDate "${current_date}")

replace_tokens("${argRootDir}/packages/all/meta/installscript.js"
  argProcessName "${argTarget}"
  argLinkName "${argTarget}")

file(RENAME "${argRootDir}/packages/all" "${argRootDir}/packages/${argTarget}")

execute_process(
  COMMAND "${argBinaryCreator}" -c config/config.xml -p packages "${argAssetName}-installer.exe"
  WORKING_DIRECTORY "${argRootDir}"
  COMMAND_ECHO STDOUT
  RESULT_VARIABLE bc_result)

if(NOT bc_result EQUAL 0)
  message(FATAL_ERROR "[UDS] binarycreator failed (exit: ${bc_result})")
endif()

message(STATUS "[UDS] Installer created: ${argRootDir}/${argAssetName}-installer.exe")
