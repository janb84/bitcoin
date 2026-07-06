# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

include_guard(GLOBAL)

function(setup_split_debug_script)
  if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(OBJCOPY ${CMAKE_OBJCOPY})
    set(STRIP ${CMAKE_STRIP})
    configure_file(
      contrib/devtools/split-debug.sh.in split-debug.sh
      FILE_PERMISSIONS OWNER_READ OWNER_EXECUTE
                       GROUP_READ GROUP_EXECUTE
                       WORLD_READ
      @ONLY
    )
  endif()
endfunction()

function(add_windows_deploy_target)
  configure_file(${PROJECT_SOURCE_DIR}/cmake/script/GenerateWindowsInstaller.cmake.in ${PROJECT_BINARY_DIR}/GenerateWindowsInstaller.cmake USE_SOURCE_PERMISSIONS @ONLY)
  if(MINGW AND TARGET bitcoin AND TARGET bitcoind AND TARGET bitcoin-cli AND TARGET bitcoin-tx AND TARGET bitcoin-wallet AND TARGET bitcoin-util AND TARGET test_bitcoin)
    add_custom_command(
      OUTPUT ${PROJECT_BINARY_DIR}/bitcoin-win64-setup.exe
      WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
      COMMAND ${CMAKE_COMMAND} -E make_directory release
      COMMAND ${CMAKE_STRIP} $<TARGET_FILE:bitcoin> -o release/$<TARGET_FILE_NAME:bitcoin>
      COMMAND ${CMAKE_STRIP} $<TARGET_FILE:bitcoind> -o release/$<TARGET_FILE_NAME:bitcoind>
      COMMAND ${CMAKE_STRIP} $<TARGET_FILE:bitcoin-cli> -o release/$<TARGET_FILE_NAME:bitcoin-cli>
      COMMAND ${CMAKE_STRIP} $<TARGET_FILE:bitcoin-tx> -o release/$<TARGET_FILE_NAME:bitcoin-tx>
      COMMAND ${CMAKE_STRIP} $<TARGET_FILE:bitcoin-wallet> -o release/$<TARGET_FILE_NAME:bitcoin-wallet>
      COMMAND ${CMAKE_STRIP} $<TARGET_FILE:bitcoin-util> -o release/$<TARGET_FILE_NAME:bitcoin-util>
      COMMAND ${CMAKE_STRIP} $<TARGET_FILE:test_bitcoin> -o release/$<TARGET_FILE_NAME:test_bitcoin>
      COMMAND ${CMAKE_COMMAND} -D BIN_DIR=release -D LIBEXEC_DIR=release -P GenerateWindowsInstaller.cmake
    )
    add_custom_target(deploy DEPENDS ${PROJECT_BINARY_DIR}/bitcoin-win64-setup.exe)
  endif()
endfunction()

function(add_macos_deploy_target)
endfunction()
