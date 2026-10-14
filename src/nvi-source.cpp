#include "nvi-source.h"

#include "nvi-common.h"
#include "nvi-context.h"
#include "nvi-plugin.h"

#include <NVI/API.h>

#include <media-io/video-io.h>
#include <util/platform.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

/* ------------------------------- 属性键 -------------------------------- */
#define PROP_SOURCE "nvi_stream"
#define PROP_LOCAL "nvi_local"
#define PROP_VIDEO "nvi_video"
#define PROP_AUDIO "nvi_audio"
#define PROP_PROXY "nvi_proxy"
#define PROP_HWACCEL "nvi_hwaccel"
#define PROP_REFRESH "nvi_refresh"
#define PROP_STATUS "nvi_status"

#define RECV_TIMEOUT_MS 40
#define RECONNECT_DELAY_MS 1000
#define FINDER_TIMEOUT_MS 400
#define FINDER_POLL_MS 200
#define FINDER_POLL_TIMEOUT_MS 3000
#define FINDER_TTL_SECONDS 10

/* ------------------------------------------------------------------------- */
/* 流发现缓存（与 NDI 插件的 NDIFinder 等价的简化实现）                      */
/* ------------------------------------------------------------------------- */

namespace {

std::mutex g_finder_mutex;
std::vector<std::string> g_finder_names;
std::vector<std::string> g_finder_uris;
std::chrono::steady_clock::time_point g_finder_time{};
bool g_finder_busy = false;

void finder_invalidate()
{
	std::lock_guard<std::mutex> lock(g_finder_mutex);
	g_finder_names.clear();
	g_finder_uris.clear();
	g_finder_busy = false;
}

void finder_store(const std::vector<NviStreamInfo> &streams)
{
	std::lock_guard<std::mutex> lock(g_finder_mutex);
	g_finder_names.clear();
	g_finder_uris.clear();

	for (const NviStreamInfo &stream : streams) {
		g_finder_names.push_back(stream.display_name());
		g_finder_uris.push_back(stream.uri);
		NVI_LOG(LOG_DEBUG, "discovered NVI stream: name='%s' uri='%s'",
			g_finder_names.back().c_str(), stream.uri.c_str());
	}

	g_finder_time = std::chrono::steady_clock::now();
	g_finder_busy = false;
}

/* 返回缓存的 (显示名, uri)，必要时触发一次后台刷新 */
std::vector<std::pair<std::string, std::string>> finder_get(obs_source_t *source)
{
	std::vector<std::pair<std::string, std::string>> cached;
	bool need_refresh = false;

	{
		std::lock_guard<std::mutex> lock(g_finder_mutex);
		const auto now = std::chrono::steady_clock::now();
		const bool stale =
			g_finder_names.empty() || (now - g_finder_time) > std::chrono::seconds(FINDER_TTL_SECONDS);

		if (!g_finder_busy && stale) {
			g_finder_busy = true;
			need_refresh = true;
		}

		for (size_t i = 0; i < g_finder_names.size(); ++i)
			cached.emplace_back(g_finder_names[i], g_finder_uris[i]);
	}

	if (need_refresh && source) {
		obs_source_t *ref = obs_source_get_ref(source);
		if (ref) {
			std::thread([ref]() {
				/* NVI 的发现结果在上下文内异步累积：上下文刚建好时
				 * NVINetworkEnumStream 立即返回空（timeout_ms 不会等待）。
				 * 所以轮询到有结果或超时为止，避免把空列表缓存 10 秒。 */
				std::vector<NviStreamInfo> streams;
				for (int waited = 0;; waited += FINDER_POLL_MS) {
					streams = nvi_discover_streams(FINDER_TIMEOUT_MS, true, true, nullptr);
					if (!streams.empty() || waited >= FINDER_POLL_TIMEOUT_MS)
						break;
					std::this_thread::sleep_for(std::chrono::milliseconds(FINDER_POLL_MS));
				}
				finder_store(streams);

				/* 回到 UI 线程刷新属性页 */
				obs_queue_task(
					OBS_TASK_UI,
					[](void *param) {
						obs_source_update_properties((obs_source_t *)param);
						obs_source_release((obs_source_t *)param);
					},
					ref, false);
			}).detach();
		}
	}

	return cached;
}

/* 属性页下拉写回的可能是列表显示名（取决于 OBS 对下拉框的回写方式），
 * 这里统一解析回真正的流 uri，避免把显示名当作远程地址传给 NVIRecvAlloc。 */
std::string finder_resolve_uri(const std::string &text)
{
	if (text.empty())
		return text;

	std::lock_guard<std::mutex> lock(g_finder_mutex);

	for (size_t i = 0; i < g_finder_names.size(); ++i) {
		if (text == g_finder_names[i] && i < g_finder_uris.size())
			return g_finder_uris[i];
	}

	return text;
}

} // namespace

/* ------------------------------------------------------------------------- */
/* 源对象                                                                    */
/* ------------------------------------------------------------------------- */

struct nvi_source {
	obs_source_t *source = nullptr;

	std::mutex cfg_mutex;
	std::string remote;
	std::string local;
	bool want_video = true;
	bool want_audio = true;
	bool proxy = false;
	bool hw_accel = false;

	std::thread worker;
	std::atomic<bool> running{false};

	std::atomic<uint64_t> video_frames{0};
	std::atomic<uint64_t> audio_frames{0};

	std::mutex status_mutex;
	std::string status;

	/* 以下由接收线程独占使用 */
	obs_source_frame2 frame = {};
	std::vector<uint8_t> video_buffer;
	obs_source_audio audio = {};
	std::vector<uint8_t> audio_planes[MAX_AV_PLANES];
	long last_rotation = -1;
	uint32_t warned_format = 0;
};

static void nvi_source_set_status(nvi_source *s, const std::string &text)
{
	std::lock_guard<std::mutex> lock(s->status_mutex);
	s->status = text;
}

static bool nvi_source_wait(nvi_source *s, int ms)
{
	for (int waited = 0; waited < ms; waited += 50) {
		if (!s->running.load())
			return false;
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	return s->running.load();
}

/* ------------------------------------------------------------------------- */
/* 视频/音频输出                                                             */
/* ------------------------------------------------------------------------- */

static void nvi_source_output_video(nvi_source *s, const char *name, const NVIVideoImageFrame *img)
{
	int obs_format = 0;
	if (!nvi_pixel_format_to_obs(img->buffer.format, &obs_format)) {
		if (s->warned_format != img->buffer.format) {
			s->warned_format = img->buffer.format;
			NVI_LOG(LOG_WARNING, "'%s': NVI pixel format %s is not supported yet, frames dropped",
				name, nvi_pixel_format_name(img->buffer.format));
		}
		return;
	}

	const uint32_t width = img->info.width;
	const uint32_t height = img->info.height;
	if (width == 0 || height == 0) {
		NVI_LOG(LOG_WARNING, "'%s': NVI frame has invalid size %ux%u", name, width, height);
		return;
	}

	uint32_t strides[MAX_AV_PLANES] = {};
	uint32_t offsets[MAX_AV_PLANES] = {};
	uint32_t rows[MAX_AV_PLANES] = {};
	size_t total = 0;
	size_t planes = 0;

	for (uint32_t i = 0; i < NVI_kMaxPixelPlanes; ++i) {
		if (!img->buffer.planes[i])
			break;

		uint32_t stride = img->buffer.strides[i];
		if (stride == 0)
			stride = nvi_default_stride(img->buffer.format, i, width);

		const uint32_t plane_rows = nvi_plane_height(img->buffer.format, i, height);
		if (stride == 0 || plane_rows == 0)
			break;

		strides[i] = stride;
		rows[i] = plane_rows;
		offsets[i] = (uint32_t)total;
		total += (size_t)stride * plane_rows;
		planes = i + 1;
	}

	if (planes == 0 || total == 0) {
		NVI_LOG(LOG_WARNING, "'%s': NVI frame has no usable planes (format %s)", name,
			nvi_pixel_format_name(img->buffer.format));
		return;
	}

	s->video_buffer.resize(total);
	for (size_t i = 0; i < planes; ++i) {
		memcpy(s->video_buffer.data() + offsets[i], img->buffer.planes[i],
		       (size_t)strides[i] * rows[i]);
	}

	obs_source_frame2 *f = &s->frame;
	memset(f, 0, sizeof(*f));

	for (size_t i = 0; i < planes; ++i) {
		f->data[i] = s->video_buffer.data() + offsets[i];
		f->linesize[i] = strides[i];
	}

	f->width = width;
	f->height = height;
	f->format = (enum video_format)obs_format;

	const enum video_range_type range = (img->info.colorspace.range == NVIRange_Full)
						    ? VIDEO_RANGE_FULL
						    : VIDEO_RANGE_PARTIAL;
	f->range = range;

	int obs_colorspace = VIDEO_CS_709;
	int obs_trc = VIDEO_TRC_DEFAULT;
	nvi_colorspace_to_obs(img->info.colorspace, &obs_colorspace, &obs_trc);
	f->trc = (uint8_t)obs_trc;

	/* OBS 的 YUV->RGB 转换完全依赖 frame 自带的 color_matrix / color_range_*：
	 * 不填（全零）时着色器里的 YUV_to_RGB() 恒等于 0，画面就是纯黑。
	 * 与其他异步源一样，按色彩空间换算一次。 */
	video_format_get_parameters_for_format((enum video_colorspace)obs_colorspace, range,
					       (enum video_format)obs_format, f->color_matrix,
					       f->color_range_min, f->color_range_max);

	uint64_t ts = nvi_tick_to_ns(img->info.tick);
	if (ts == 0)
		ts = os_gettime_ns();
	f->timestamp = ts;

	if ((long)img->info.rotation != s->last_rotation) {
		s->last_rotation = (long)img->info.rotation;
		obs_source_set_async_rotation(s->source, s->last_rotation);
	}

	obs_source_output_video2(s->source, f);
	s->video_frames++;
}

static void nvi_source_output_audio(nvi_source *s, const NVIAudioWaveFrame *wave)
{
	const uint16_t channels = wave->info.channels;
	const uint32_t rate = wave->info.sample_rate;
	const uint8_t *src = wave->buffer.data;

	if (!src || channels == 0 || channels > MAX_AV_PLANES || rate == 0)
		return;

	if (wave->info.codec != NVICodec_LPCM) {
		if (s->warned_format != wave->info.codec) {
			s->warned_format = wave->info.codec;
			NVI_LOG(LOG_WARNING, "unsupported audio codec 0x%08x, audio dropped",
				wave->info.codec);
		}
		return;
	}

	const uint32_t bits = wave->info.depth & 0x00FF;
	const bool big_endian = (wave->info.depth & NVIWaveBit_Mask_BE) != 0;

	uint32_t bytes_per_sample = wave->buffer.align ? wave->buffer.align : (bits / 8);
	if (bytes_per_sample == 0)
		return;

	uint32_t samples = wave->buffer.samples;
	const size_t frame_bytes = (size_t)channels * bytes_per_sample;
	if (frame_bytes == 0 || (size_t)samples * frame_bytes > wave->buffer.size) {
		samples = frame_bytes ? (uint32_t)(wave->buffer.size / frame_bytes) : 0;
		if (samples == 0)
			return;
	}

	const size_t plane_size = (size_t)samples * sizeof(float);

	for (uint16_t c = 0; c < channels; ++c) {
		s->audio_planes[c].resize(plane_size);
		float *dst = (float *)s->audio_planes[c].data();

		for (uint32_t i = 0; i < samples; ++i) {
			const uint8_t *p = src + ((size_t)i * channels + c) * bytes_per_sample;
			float value = 0.0f;

			switch (bits) {
			case 0x10: {
				const int16_t raw = big_endian ? (int16_t)((p[1] << 8) | p[0])
							       : (int16_t)((p[0] << 8) | p[1]);
				value = raw / 32768.0f;
				break;
			}
			case 0x18: {
				int32_t raw;
				if (big_endian)
					raw = p[2] | (p[1] << 8) | ((int8_t)p[0] << 16);
				else
					raw = p[0] | (p[1] << 8) | ((int8_t)p[2] << 16);
				value = raw / 8388608.0f;
				break;
			}
			case 0x20: {
				uint32_t raw = big_endian
						       ? (uint32_t)(p[3] | (p[2] << 8) | (p[1] << 16) |
								    ((uint32_t)p[0] << 24))
						       : (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) |
								    ((uint32_t)p[3] << 24));
				memcpy(&value, &raw, sizeof(float));
				break;
			}
			default:
				return;
			}

			dst[i] = value;
		}

		s->audio.data[c] = s->audio_planes[c].data();
	}

	for (uint16_t c = channels; c < MAX_AV_PLANES; ++c)
		s->audio.data[c] = nullptr;

	s->audio.frames = samples;
	s->audio.samples_per_sec = rate;
	s->audio.speakers = (enum speaker_layout)nvi_channels_to_speakers(channels);
	s->audio.format = AUDIO_FORMAT_FLOAT_PLANAR;

	uint64_t ts = nvi_tick_to_ns(wave->info.tick);
	if (ts == 0)
		ts = os_gettime_ns();
	s->audio.timestamp = ts;

	obs_source_output_audio(s->source, &s->audio);
	s->audio_frames++;
}

/* ------------------------------------------------------------------------- */
/* 接收线程                                                                  */
/* ------------------------------------------------------------------------- */

static void nvi_source_thread(nvi_source *s)
{
	std::string remote;
	std::string local;
	bool want_video;
	bool want_audio;
	bool proxy;
	bool hw_accel;

	{
		std::lock_guard<std::mutex> lock(s->cfg_mutex);
		remote = s->remote;
		local = s->local;
		want_video = s->want_video;
		want_audio = s->want_audio;
		proxy = s->proxy;
		hw_accel = s->hw_accel;
	}

	const std::string name = obs_source_get_name(s->source);

	if (remote.empty()) {
		nvi_source_set_status(s, obs_module_text("NVIPlugin.Source.Status.NoStream"));
		NVI_LOG(LOG_INFO, "'%s': no NVI stream selected", name.c_str());
		return;
	}

	NVI_LOG(LOG_INFO, "'%s': receiving NVI stream '%s'", name.c_str(), remote.c_str());

	while (s->running.load()) {
		NVI_CONTEXT ctx = nvi_context_acquire();
		if (!ctx) {
			nvi_source_set_status(s, obs_module_text("NVIPlugin.Source.Status.NoLibrary"));
			break;
		}

		NVIRecvAllocParam param = {};
		param.local = local.empty() ? nullptr : local.c_str();
		param.remote = remote.c_str();
		param.flags_rx_proxy = proxy ? 1u : 0u;
		param.flags_off_video = want_video ? 0u : 1u;
		param.flags_off_audio = want_audio ? 0u : 1u;
		param.flags_off_meta = 1u;

		NVI_RECVER recver = NVIRecvAlloc(ctx, &param);
		if (!recver) {
			const std::string err = nvi_error_text(NVILastError());
			nvi_source_set_status(s, obs_module_text("NVIPlugin.Source.Status.ConnectFailed"));
			NVI_LOG(LOG_WARNING, "'%s': NVIRecvAlloc('%s') failed: %s", name.c_str(),
				remote.c_str(), err.c_str());
			nvi_context_release();

			if (!nvi_source_wait(s, RECONNECT_DELAY_MS))
				break;
			continue;
		}

		if (hw_accel) {
			NVIVideoAccelerate accel = {};
			accel.type = NVIAccel_Auto;
			accel.flags = 0; /* 不要求设备缓冲，硬件解码后仍回到主机内存 */
			if (NVIRecvVideoAccelPreset(recver, &accel) < 0)
				NVI_LOG(LOG_WARNING, "'%s': NVIRecvVideoAccelPreset failed: %s",
					name.c_str(), nvi_error_text(NVILastError()).c_str());
		}

		nvi_source_set_status(s, obs_module_text("NVIPlugin.Source.Status.Receiving"));

		uint64_t last_report = os_gettime_ns();
		uint64_t stat_video = 0;
		uint64_t stat_audio = 0;

		while (s->running.load()) {
			NVIRecvFrameOut out = {};
			out.timeout_ms = RECV_TIMEOUT_MS;

			const int32_t err = NVIRecvFrame(recver, &out);
			if (err < 0) {
				nvi_source_set_status(s, obs_module_text("NVIPlugin.Source.Status.Disconnected"));
				NVI_LOG(LOG_WARNING, "'%s': NVIRecvFrame failed: %s", name.c_str(),
					nvi_error_text(err).c_str());
				break;
			}

			if (out.image_out)
				stat_video++;
			if (out.wave_out)
				stat_audio++;

			if (out.image_out && want_video)
				nvi_source_output_video(s, name.c_str(), out.image_out);

			if (out.wave_out && want_audio)
				nvi_source_output_audio(s, out.wave_out);

			/* 每 2 秒报告一次：NVI 交付的帧数 vs 实际输出给 OBS 的帧数，
			 * 用于区分“帧没到”与“帧到了但被本插件丢弃”。 */
			const uint64_t now = os_gettime_ns();
			if (now - last_report >= 2000000000ull) {
				NVI_LOG(LOG_INFO,
					"'%s': recv stats: delivered video=%llu audio=%llu, output video=%llu audio=%llu",
					name.c_str(), (unsigned long long)stat_video,
					(unsigned long long)stat_audio,
					(unsigned long long)s->video_frames.load(),
					(unsigned long long)s->audio_frames.load());
				stat_video = 0;
				stat_audio = 0;
				last_report = now;
			}
		}

		NVIRecvFree(recver);
		nvi_context_release();

		/* 断开时清空最后一帧，避免画面停留在旧帧上 */
		obs_source_output_video2(s->source, nullptr);

		if (!nvi_source_wait(s, RECONNECT_DELAY_MS))
			break;
	}

	NVI_LOG(LOG_INFO, "'%s': NVI receive thread stopped", name.c_str());
}

/* ------------------------------------------------------------------------- */
/* 生命周期                                                                  */
/* ------------------------------------------------------------------------- */

static const char *nvi_source_getname(void *)
{
	return obs_module_text("NVIPlugin.SourceName");
}

/* 应用设置，返回是否需要重启接收线程 */
static bool nvi_source_apply(nvi_source *s, obs_data_t *settings)
{
	/* 属性页写回的可能是显示名，统一解析成真正的流 uri */
	const std::string remote = finder_resolve_uri(obs_data_get_string(settings, PROP_SOURCE));
	const std::string local = obs_data_get_string(settings, PROP_LOCAL);
	const bool video = obs_data_get_bool(settings, PROP_VIDEO);
	const bool audio = obs_data_get_bool(settings, PROP_AUDIO);
	const bool proxy = obs_data_get_bool(settings, PROP_PROXY);
	const bool hw_accel = obs_data_get_bool(settings, PROP_HWACCEL);

	std::lock_guard<std::mutex> lock(s->cfg_mutex);

	const bool changed = remote != s->remote || local != s->local || video != s->want_video ||
			     audio != s->want_audio || proxy != s->proxy || hw_accel != s->hw_accel;

	s->remote = remote;
	s->local = local;
	s->want_video = video;
	s->want_audio = audio;
	s->proxy = proxy;
	s->hw_accel = hw_accel;

	return changed;
}

static void nvi_source_restart(nvi_source *s)
{
	s->running = false;
	if (s->worker.joinable())
		s->worker.join();

	s->running = true;
	s->worker = std::thread(nvi_source_thread, s);
}

static void *nvi_source_create(obs_data_t *settings, obs_source_t *source)
{
	auto *s = new nvi_source();
	s->source = source;

	nvi_source_apply(s, settings);

	s->running = true;
	s->worker = std::thread(nvi_source_thread, s);

	return s;
}

static void nvi_source_destroy(void *data)
{
	auto *s = (nvi_source *)data;

	s->running = false;
	if (s->worker.joinable())
		s->worker.join();

	delete s;
}

static void nvi_source_update(void *data, obs_data_t *settings)
{
	auto *s = (nvi_source *)data;

	if (nvi_source_apply(s, settings))
		nvi_source_restart(s);
}

static void nvi_source_getdefaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, PROP_SOURCE, "");
	obs_data_set_default_string(settings, PROP_LOCAL, "");
	obs_data_set_default_bool(settings, PROP_VIDEO, true);
	obs_data_set_default_bool(settings, PROP_AUDIO, true);
	obs_data_set_default_bool(settings, PROP_PROXY, false);
	obs_data_set_default_bool(settings, PROP_HWACCEL, false);
}

static obs_properties_t *nvi_source_getproperties(void *data)
{
	auto *s = (nvi_source *)data;
	obs_properties_t *props = obs_properties_create();

	/* 必须是 LIST：EDITABLE 下拉时 OBS 回写的是编辑框文本（即显示名），
	 * LIST 才会回写列表项的 value（流 uri）。 */
	obs_property_t *streams = obs_properties_add_list(props, PROP_SOURCE,
							  obs_module_text("NVIPlugin.SourceProps.Stream"),
							  OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_set_long_description(streams, obs_module_text("NVIPlugin.SourceProps.Stream.Tip"));

	for (const auto &entry : finder_get(s ? s->source : nullptr))
		obs_property_list_add_string(streams, entry.first.c_str(), entry.second.c_str());

	obs_properties_add_text(props, PROP_LOCAL, obs_module_text("NVIPlugin.SourceProps.Local"),
				OBS_TEXT_DEFAULT);

	obs_properties_add_bool(props, PROP_VIDEO, obs_module_text("NVIPlugin.SourceProps.Video"));
	obs_properties_add_bool(props, PROP_AUDIO, obs_module_text("NVIPlugin.SourceProps.Audio"));
	obs_properties_add_bool(props, PROP_PROXY, obs_module_text("NVIPlugin.SourceProps.Proxy"));
	obs_properties_add_bool(props, PROP_HWACCEL, obs_module_text("NVIPlugin.SourceProps.HWAccel"));

	obs_properties_add_button2(
		props, PROP_REFRESH, obs_module_text("NVIPlugin.SourceProps.Refresh"),
		[](obs_properties_t *, obs_property_t *, void *) {
			finder_invalidate();
			return true;
		},
		nullptr);

	if (s) {
		std::lock_guard<std::mutex> lock(s->status_mutex);
		if (!s->status.empty()) {
			obs_property_t *info = obs_properties_add_text(props, PROP_STATUS,
								      s->status.c_str(), OBS_TEXT_INFO);
			obs_property_text_set_info_type(info, OBS_TEXT_INFO_NORMAL);
		}
	}

	return props;
}

obs_source_info nvi_source_get_info()
{
	obs_source_info info = {};

	info.id = "nvi_source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE;

	info.get_name = nvi_source_getname;
	info.create = nvi_source_create;
	info.destroy = nvi_source_destroy;
	info.update = nvi_source_update;
	info.get_defaults = nvi_source_getdefaults;
	info.get_properties = nvi_source_getproperties;
	info.icon_type = OBS_ICON_TYPE_MEDIA;

	return info;
}
