#pragma once

#include <obs-module.h>

/* 由 plugin-main.cpp 提供 */
extern const char *PLUGIN_NAME;
extern const char *PLUGIN_VERSION;
extern const char *PLUGIN_DISPLAY_NAME;

/* 统一日志前缀，便于在 OBS 日志里过滤 NVI 相关输出 */
#define NVI_LOG(level, ...) blog(level, "[nvi] " __VA_ARGS__)

/* 组件注册入口 */
obs_source_info nvi_source_get_info();
obs_output_info nvi_output_get_info();

/* 输出设置对话框（工具菜单）注册 */
void nvi_output_settings_register();
