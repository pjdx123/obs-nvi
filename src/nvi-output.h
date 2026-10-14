#pragma once

#include <obs-module.h>

#include <string>

/* 输出设置键（属性页与工具菜单对话框共用） */
#define NVI_OUT_ALIAS "nvi_alias"
#define NVI_OUT_CODEC "nvi_codec"
#define NVI_OUT_LOCAL "nvi_out_local"
#define NVI_OUT_TAGS "nvi_tags"
#define NVI_OUT_VIDEO "nvi_out_video"
#define NVI_OUT_AUDIO "nvi_out_audio"

/* 编码格式选项（对应 NVICodecID） */
#define NVI_CODEC_AVC 0
#define NVI_CODEC_HEVC 1
#define NVI_CODEC_HF 2
#define NVI_CODEC_SUVC 3

obs_output_info nvi_output_get_info();

/* 输出状态查询（工具菜单对话框使用） */
struct NviOutputStatus {
	bool running = false;
	bool valid = false;
	uint32_t pulled = 0;
	std::string last_error;
};

NviOutputStatus nvi_output_query_status();

/* 启动/停止由工具菜单创建的 NVI 输出 */
bool nvi_output_start_with(obs_data_t *settings);
void nvi_output_stop_current();
bool nvi_output_is_running();
