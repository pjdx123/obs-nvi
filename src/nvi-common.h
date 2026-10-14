#pragma once

#include <NVI/API.h>

#include <cstdint>
#include <string>
#include <vector>

/** 一条 NVI 网络流（来自 NVINetworkEnumStream）。 */
struct NviStreamInfo {
	std::string alias;
	std::string site;
	std::string domain;
	std::string uri;
	std::string tags;
	uint32_t number = 0;
	uint32_t instance = 0;
	bool ptz = false;
	bool proxy_video = false;

	/** 下拉列表中显示的文本。 */
	std::string display_name() const;
};

/**
 * 枚举 NVI 网络中的流。
 * @param timeout_ms 等待时间
 * @param loopback   是否包含本机回环流
 * @param rescan     是否主动重扫（false 为被动检测）
 * @param filter     标签过滤，逗号分隔，可为 nullptr
 */
std::vector<NviStreamInfo> nvi_discover_streams(int timeout_ms, bool loopback = true, bool rescan = true,
						const char *filter = nullptr);

/** NVI 像素格式 -> OBS video_format（enum video_format 的整数值）。 */
bool nvi_pixel_format_to_obs(uint32_t nvi_format, int *obs_format);

/**
 * NVI 色彩空间 -> OBS video_colorspace / video_trc（对应 enum 的整数值）。
 *
 * OBS 的 YUV->RGB 转换不做任何缺省推断，直接使用源给出的 color_matrix：
 * 必须依据 colorspace 用 video_format_get_parameters_for_format() 算出该矩阵，
 * 否则着色器里的 YUV_to_RGB() 恒为 0，画面全黑。
 */
void nvi_colorspace_to_obs(const NVIColorSpace &cs, int *obs_colorspace, int *obs_trc);

/** 像素格式可读名，用于日志。 */
const char *nvi_pixel_format_name(uint32_t nvi_format);

/**
 * 某个平面的行数。
 * I420/NV12/P010 等 4:2:0 格式的色度平面为高度一半（向上取整）。
 */
uint32_t nvi_plane_height(uint32_t nvi_format, uint32_t plane, uint32_t height);

/** 像素格式的默认行宽（当 buffer.strides 为 0 时兜底）。 */
uint32_t nvi_default_stride(uint32_t nvi_format, uint32_t plane, uint32_t width);

/** NVI tick -> 纳秒（视频 tick.freq_den 通常为 90000，音频为采样率）。 */
uint64_t nvi_tick_to_ns(const NVITimeTick &tick);

/** NVI 声道数 -> OBS speaker_layout（enum speaker_layout 的整数值）。 */
int nvi_channels_to_speakers(uint16_t channels);

/** 错误码文本，形如 "-425 (描述)"。 */
std::string nvi_error_text(int32_t error);
