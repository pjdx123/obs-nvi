cmake_minimum_required(VERSION 3.28...3.30)

# =============================================================================
# obs-nvi —— OBS Studio 的 NVI（Network Video Interface）输入 / 输出插件
#
# 本 CMakeLists 支持两种构建模式：
#
#   A) 独立构建（standalone，推荐）
#      把本仓库单独 checkout 后构建，依赖已安装的 OBS 开发文件
#      （libobsConfig.cmake / obs-frontend-apiConfig.cmake）：
#
#          cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
#                -DOBS_INSTALL_DIR="C:/Program Files/obs-studio"
#          cmake --build build --config Release
#
#      产物会暂存到 build/rundir（见 README「安装 / 集成」）。
#
#   B) 树内构建（in-tree）
#      把本目录放进 obs-studio 源码树的 plugins/nvi，由 plugins/CMakeLists.txt
#      中的 add_obs_plugin(nvi PLATFORMS WINDOWS WITH_MESSAGE) 引入。
#      此时复用 obs-studio 的构建系统、输出目录与安装规则。
#
# 官方 SDK 以 <NVI/API.h> + NVI.lib 的形式提供（见 sdk/），运行时还需要
# NVI.dll 及其编解码插件（见下方 NVI_RUNTIME_DIR）。
# =============================================================================

# 顶层配置 = 独立构建；被 add_subdirectory 引入 = 树内构建。
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(OBS_NVI_STANDALONE ON)
else()
  set(OBS_NVI_STANDALONE OFF)
endif()

if(OBS_NVI_STANDALONE)
  project(obs-nvi VERSION 0.1.0 LANGUAGES CXX)

  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)

  if(NOT WIN32)
    message(FATAL_ERROR "obs-nvi 仅支持 Windows：它依赖 NVI 官方 SDK（NVI.lib / NVI.dll）。")
  endif()

  # OBS 安装目录（其下应有 lib/cmake/libobs 与 lib/cmake/obs-frontend-api）。
  # 也可以直接用 -DCMAKE_PREFIX_PATH=<OBS 安装目录> 指定。
  if(OBS_INSTALL_DIR)
    list(APPEND CMAKE_PREFIX_PATH "${OBS_INSTALL_DIR}")
  endif()

  find_package(libobs REQUIRED)
  find_package(obs-frontend-api REQUIRED)
endif()

option(ENABLE_NVI "Enable building the NVI plugin" ON)

if(NOT ENABLE_NVI OR NOT WIN32)
  if(OBS_NVI_STANDALONE)
    message(STATUS "obs-nvi: disabled (ENABLE_NVI=${ENABLE_NVI}, WIN32=${WIN32})")
    return()
  endif()
  target_disable(obs-nvi)
  return()
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# -----------------------------------------------------------------------------
# 目标
#
# 目标名与输出名都用 obs-nvi，不能叫 nvi：
#   1) Windows 文件名大小写不敏感，nvi.dll 会与 NVI 运行时的 NVI.dll 同名，
#      二者都放在 obs-plugins/64bit 会互相覆盖；
#   2) OBS 按 DLL 文件名推导模块数据目录（data/obs-plugins/%module%），
#      target 名与文件名一致，data/ 才会被装到 obs-plugins/obs-nvi（locale 才能被找到）。
# -----------------------------------------------------------------------------
add_library(obs-nvi MODULE)
add_library(OBS::nvi ALIAS obs-nvi)

target_sources(
  obs-nvi
  PRIVATE
    src/nvi-common.cpp
    src/nvi-common.h
    src/nvi-context.cpp
    src/nvi-context.h
    src/nvi-output-ui.cpp
    src/nvi-output.cpp
    src/nvi-output.h
    src/nvi-plugin.h
    src/nvi-source.cpp
    src/nvi-source.h
    src/plugin-main.cpp
)

target_include_directories(
  obs-nvi
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/sdk/include"
)

target_link_libraries(
  obs-nvi
  PRIVATE OBS::libobs OBS::frontend-api "${CMAKE_CURRENT_SOURCE_DIR}/sdk/lib/NVI.lib" Qt6::Core Qt6::Widgets
)

# NVI.dll 只在真正调用 NVI API 时才需要，因此改成延迟加载：
# 即使缺少运行时，插件本身也能成功加载（源/输出在运行时提示 “NVI 库不可用”）。
target_link_options(obs-nvi PRIVATE "/DELAYLOAD:NVI.dll")

target_link_libraries(obs-nvi PRIVATE delayimp)

# -----------------------------------------------------------------------------
# NVI 运行时（NVI.dll + 编解码插件）落点
#
# 必须与 obs64.exe 同目录（bin/64bit）：
#   - libobs 用 LoadLibraryExW(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS) 加载模块，
#     exe 目录必定在搜索路径中；
#   - 若放在 obs-plugins/64bit，会与插件本体同名（大小写不敏感）互相覆盖。
# 默认从 sdk/runtime 读取，可用 -DNVI_RUNTIME_DIR=<dir> 指向别处。
# -----------------------------------------------------------------------------
set(NVI_RUNTIME_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/sdk/runtime"
    CACHE PATH "Directory containing the NVI runtime DLLs (NVI.dll and codec plugins)"
)

if(OBS_NVI_STANDALONE)
  set(OBS_NVI_RUNDIR "${CMAKE_BINARY_DIR}/rundir")
  set(OBS_NVI_BIN_DEST "${OBS_NVI_RUNDIR}/bin/64bit")
  set(OBS_NVI_PLUGIN_DEST "${OBS_NVI_RUNDIR}/obs-plugins/64bit")
  set(OBS_NVI_DATA_DEST "${OBS_NVI_RUNDIR}/data/obs-plugins/obs-nvi")
else()
  set(OBS_NVI_BIN_DEST "${OBS_OUTPUT_DIR}/$<CONFIG>/${OBS_EXECUTABLE_DESTINATION}")
endif()

if(EXISTS "${NVI_RUNTIME_DIR}")
  file(GLOB NVI_RUNTIME_DLLS "${NVI_RUNTIME_DIR}/*.dll")

  if(NVI_RUNTIME_DLLS)
    add_custom_command(
      TARGET obs-nvi
      POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${OBS_NVI_BIN_DEST}"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${NVI_RUNTIME_DLLS} "${OBS_NVI_BIN_DEST}/"
      COMMENT "Copy NVI runtime DLLs to ${OBS_NVI_BIN_DEST}"
    )
  endif()
endif()

# 独立构建：把插件本体与本地化资源暂存到一个可直接合并进 OBS 的 rundir。
# （树内构建时这些由 obs-studio 的 set_target_properties_obs 负责。）
if(OBS_NVI_STANDALONE)
  add_custom_command(
    TARGET obs-nvi
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${OBS_NVI_PLUGIN_DEST}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:obs-nvi>" "${OBS_NVI_PLUGIN_DEST}/"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${OBS_NVI_DATA_DEST}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/data" "${OBS_NVI_DATA_DEST}"
    COMMENT "Stage obs-nvi.dll and locale into ${OBS_NVI_RUNDIR}"
  )
endif()

if(OBS_NVI_STANDALONE)
  # 也可以直接安装到 OBS 目录：cmake --install build --prefix "<OBS 安装目录>"
  install(TARGETS obs-nvi RUNTIME DESTINATION "obs-plugins/64bit")
  install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/" DESTINATION "data/obs-plugins/obs-nvi")
  if(NVI_RUNTIME_DLLS)
    install(FILES ${NVI_RUNTIME_DLLS} DESTINATION "bin/64bit")
  endif()
endif()

# -----------------------------------------------------------------------------
# 目标属性
# -----------------------------------------------------------------------------
if(OBS_NVI_STANDALONE)
  set_target_properties(
    obs-nvi
    PROPERTIES PREFIX ""
               OUTPUT_NAME "obs-nvi"
               AUTOMOC ON
               CXX_STANDARD 17
               CXX_STANDARD_REQUIRED ON
  )
  set_target_properties(obs-nvi PROPERTIES FOLDER plugins/nvi)
else()
  set_target_properties_obs(
    obs-nvi
    PROPERTIES FOLDER plugins/nvi
               PREFIX ""
               OUTPUT_NAME "obs-nvi"
               COMPILE_WARNING_AS_ERROR OFF
               AUTOMOC ON
  )
endif()
