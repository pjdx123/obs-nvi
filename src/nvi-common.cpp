#include "nvi-common.h"
#include "nvi-context.h"
#include "nvi-plugin.h"

#include <obs-module.h>

#include <cmath>

/* ------------------------------------------------------------------------- */
/* 发现                                                                      */
/* ------------------------------------------------------------------------- */

std::string NviStreamInfo::display_name() const
{
	std::string out = alias.empty() ? uri : alias;

	std::string where = site.empty() ? domain : site;
	if (!where.empty() && !alias.empty())
		out += " [" + where + "]";

	if (out.empty())
		out = uri;

	return out;
}

std::vector<NviStreamInfo> nvi_discover_streams(int timeout_ms, bool loopback, bool rescan, const char *filter)
{
	std::vector<NviStreamInfo> result;

	NVI_CONTEXT ctx = nvi_context_acquire();
	if (!ctx)
		return result;

	const uint32_t kMaxStreams = 64;
	std::vector<NVINetworkStream> buffer(kMaxStreams);

	NVINetworkEnumParam param = {};
	param.streams = buffer.data();
	param.streams_size = (uint32_t)buffer.size();
	param.timeout_ms = timeout_ms;
	param.filter = filter;
	param.loopback = loopback ? 1u : 0u;
	param.rescan = rescan ? 1u : 0u;

	int32_t count = NVINetworkEnumStream(ctx, &param);
	if (count < 0) {
		NVI_LOG(LOG_WARNING, "NVINetworkEnumStream failed: %s", nvi_error_text(NVILastError()).c_str());
		nvi_context_release();
		return result;
	}

	if (count > (int32_t)buffer.size()) {
		NVI_LOG(LOG_WARNING, "NVI stream list truncated to %u entries", buffer.size());
		count = (int32_t)buffer.size();
	}

	result.reserve((size_t)count);
	for (int32_t i = 0; i < count; ++i) {
		const NVINetworkStream &src = buffer[(size_t)i];
		if (!src.uri || !*src.uri)
			continue;

		NviStreamInfo info;
		info.alias = src.alias ? src.alias : "";
		info.site = src.sites ? src.sites : "";
		info.domain = src.domain ? src.domain : "";
		info.uri = src.uri;
		info.tags = src.tags ? src.tags : "";
		info.number = src.number;
		info.instance = src.instance;
		info.ptz = src.caps_ptz != 0;
		info.proxy_video = src.caps_proxy_video != 0;
		result.push_back(std::move(info));
	}

	nvi_context_release();
	return result;
}

/* ------------------------------------------------------------------------- */
/* 视频格式映射                                                              */
/* ------------------------------------------------------------------------- */

bool nvi_pixel_format_to_obs(uint32_t nvi_format, int *obs_format)
{
	switch (nvi_format) {
	case NVIPixel_I420:
		*obs_format = VIDEO_FORMAT_I420;
		return true;
	case NVIPixel_NV12:
		*obs_format = VIDEO_FORMAT_NV12;
		return true;
	case NVIPixel_422P:
		*obs_format = VIDEO_FORMAT_I422;
		return true;
	case NVIPixel_420P10LE:
		*obs_format = VIDEO_FORMAT_I010;
		return true;
	case NVIPixel_P010LE:
		*obs_format = VIDEO_FORMAT_P010;
		return true;
	case NVIPixel_422P10LE:
		*obs_format = VIDEO_FORMAT_I210;
		return true;
	case NVIPixel_V210:
		*obs_format = VIDEO_FORMAT_V210;
		return true;
	case NVIPixel_Mono:
		*obs_format = VIDEO_FORMAT_Y800;
		return true;
	case NVIPixel_420A:
		*obs_format = VIDEO_FORMAT_I40A;
		return true;
	case NVIPixel_422A:
		*obs_format = VIDEO_FORMAT_I42A;
		return true;
	default:
		/* NV21/NV12A/NV21A 以及大端 10bit 需要额外转换，当前版本暂不支持 */
		return false;
	}
}

void nvi_colorspace_to_obs(const NVIColorSpace &cs, int *obs_colorspace, int *obs_trc)
{
	int colorspace;
	int trc;

	switch (cs.transfer) {
	case NVITransfer_SMPTEST2084:
		colorspace = VIDEO_CS_2100_PQ;
		trc = VIDEO_TRC_PQ;
		break;
	case NVITransfer_ARIB_STD_B67:
		colorspace = VIDEO_CS_2100_HLG;
		trc = VIDEO_TRC_HLG;
		break;
	default:
		trc = VIDEO_TRC_DEFAULT;
		switch (cs.matrix) {
		case NVIMatrix_BT470BG:
		case NVIMatrix_SMPTE170M:
			colorspace = VIDEO_CS_601;
			break;
		default:
			/* OBS 只有 601/709/2100 三套矩阵；非 HDR 的 BT.2020 也按 709 处理。 */
			colorspace = VIDEO_CS_709;
			break;
		}
		break;
	}

	if (obs_colorspace)
		*obs_colorspace = colorspace;
	if (obs_trc)
		*obs_trc = trc;
}

const char *nvi_pixel_format_name(uint32_t nvi_format)
{
	switch (nvi_format) {
	case NVIPixel_I420:
		return "I420";
	case NVIPixel_420A:
		return "420A";
	case NVIPixel_NV12:
		return "NV12";
	case NVIPixel_NV12A:
		return "NV12A";
	case NVIPixel_NV21:
		return "NV21";
	case NVIPixel_NV21A:
		return "NV21A";
	case NVIPixel_422P:
		return "422P";
	case NVIPixel_422A:
		return "422A";
	case NVIPixel_P010LE:
		return "P010LE";
	case NVIPixel_P010BE:
		return "P010BE";
	case NVIPixel_420P10LE:
		return "420P10LE";
	case NVIPixel_420P10BE:
		return "420P10BE";
	case NVIPixel_422P10LE:
		return "422P10LE";
	case NVIPixel_422P10BE:
		return "422P10BE";
	case NVIPixel_V210:
		return "V210";
	case NVIPixel_Mono:
		return "Mono";
	default:
		return "unknown";
	}
}

uint32_t nvi_plane_height(uint32_t nvi_format, uint32_t plane, uint32_t height)
{
	const uint32_t half = (height + 1) / 2;

	switch (nvi_format) {
	case NVIPixel_I420:
	case NVIPixel_NV12:
	case NVIPixel_NV21:
	case NVIPixel_P010LE:
	case NVIPixel_P010BE:
	case NVIPixel_420P10LE:
	case NVIPixel_420P10BE:
		return plane == 0 ? height : half;

	case NVIPixel_420A:
		/* Y U V A */
		if (plane == 0 || plane == 3)
			return height;
		return half;

	case NVIPixel_NV12A:
	case NVIPixel_NV21A:
		/* Y UV A */
		if (plane == 1)
			return half;
		return height;

	case NVIPixel_422P:
	case NVIPixel_422A:
	case NVIPixel_422P10LE:
	case NVIPixel_422P10BE:
	case NVIPixel_V210:
	case NVIPixel_Mono:
		return height;

	default:
		return plane == 0 ? height : half;
	}
}

uint32_t nvi_default_stride(uint32_t nvi_format, uint32_t plane, uint32_t width)
{
	const uint32_t half = (width + 1) / 2;
	const uint32_t half2 = ((width + 1) / 2) * 2;

	switch (nvi_format) {
	case NVIPixel_I420:
		return plane == 0 ? width : half;
	case NVIPixel_NV12:
	case NVIPixel_NV21:
		return plane == 0 ? width : half2;

	case NVIPixel_420A:
		return (plane == 0 || plane == 3) ? width : half;
	case NVIPixel_NV12A:
	case NVIPixel_NV21A:
		return plane == 1 ? half2 : width;

	case NVIPixel_422P:
		return plane == 0 ? width : half;
	case NVIPixel_422A:
		return (plane == 0 || plane == 3) ? width : half;

	case NVIPixel_P010LE:
	case NVIPixel_P010BE:
		return plane == 0 ? width * 2 : half2;
	case NVIPixel_420P10LE:
	case NVIPixel_420P10BE:
	case NVIPixel_422P10LE:
	case NVIPixel_422P10BE:
		return plane == 0 ? width * 2 : half2;

	case NVIPixel_V210:
		/* v210: 每 6 个像素打包成 16 字节 */
		return (uint32_t)((((uint64_t)width + 5) / 6) * 16);

	case NVIPixel_Mono:
	default:
		return width;
	}
}

/* ------------------------------------------------------------------------- */
/* 时间/声道/错误                                                            */
/* ------------------------------------------------------------------------- */

uint64_t nvi_tick_to_ns(const NVITimeTick &tick)
{
	const uint32_t num = tick.freq_num ? tick.freq_num : 1u;
	if (tick.freq_den == 0)
		return 0;

	const double seconds = (double)tick.value * (double)num / (double)tick.freq_den;
	return (uint64_t)(seconds * 1000000000.0);
}

int nvi_channels_to_speakers(uint16_t channels)
{
	switch (channels) {
	case 1:
		return SPEAKERS_MONO;
	case 2:
		return SPEAKERS_STEREO;
	case 3:
		return SPEAKERS_2POINT1;
	case 4:
		return SPEAKERS_4POINT0;
	case 5:
		return SPEAKERS_4POINT1;
	case 6:
		return SPEAKERS_5POINT1;
	case 8:
		return SPEAKERS_7POINT1;
	default:
		return SPEAKERS_UNKNOWN;
	}
}

std::string nvi_error_text(int32_t error)
{
	if (error >= 0)
		return "0";

	std::string text = std::to_string(error);
	switch (error) {
	case -1:
		text += " (generic failure)";
		break;
	case -2:
		text += " (invalid argument)";
		break;
	case -3:
		text += " (not found)";
		break;
	case -4:
		text += " (timeout)";
		break;
	case -5:
		text += " (resource busy)";
		break;
	default:
		break;
	}
	return text;
}
