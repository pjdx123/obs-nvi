#include "nvi-output.h"

#include "nvi-common.h"
#include "nvi-context.h"
#include "nvi-plugin.h"

#include <NVI/API.h>

#include <util/platform.h>

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

/*
 * NVI 输出（OBS 裸帧 -> 单线程发送）
 *
 * OBS 通过 raw_video/raw_audio 回调把主输出的裸帧投递给本输出，回调运行在 OBS 的
 * 视频/音频线程上；而 NVI 要求同一路信号的所有 NVISend* 调用必须在固定的同一个
 * 线程中完成。因此这里采用“生产者/消费者”模型：
 *
 *   raw_video / raw_audio（OBS 线程）  ->  拷贝帧到队列  ->  发送线程
 *
 * 发送线程负责 NVISendAlloc / NVISendVideo / NVISendAudio 以及
 * NVISendFree，即所有 NVI 调用都集中在这一个线程里。
 */

/* 队列上限：超过后丢弃最旧的帧，避免网络阻塞时内存无限增长 */
#define NVI_MAX_QUEUE 256

/* 视频 tick 频率，与 NVI 示例保持一致 */
#define NVI_VIDEO_TICK_FREQ 90000

/* ------------------------------------------------------------------------- */
/* 设置映射                                                                  */
/* ------------------------------------------------------------------------- */

struct NviOutputConfig {
	std::string alias;
	std::string local;
	std::string tags;
	uint32_t codec = NVICodec_AVC;
	bool video = true;
	bool audio = true;
};

/* 下拉选项下标 -> NVICodecID */
static uint32_t nvi_codec_from_index(int64_t index)
{
	switch (index) {
	case NVI_CODEC_HEVC:
		return NVICodec_HEVC;
	case NVI_CODEC_HF:
		return NVICodec_HF;
	case NVI_CODEC_SUVC:
		return NVICodec_SUVC;
	case NVI_CODEC_AVC:
	default:
		return NVICodec_AVC;
	}
}

static const char *nvi_codec_name(uint32_t codec)
{
	switch (codec) {
	case NVICodec_HEVC:
		return "HEVC";
	case NVICodec_HF:
		return "HF";
	case NVICodec_SUVC:
		return "SUVC";
	case NVICodec_AVC:
	default:
		return "AVC";
	}
}

/* OBS video_format -> NVI 像素格式（仅映射原生支持的格式） */
static bool nvi_obs_format_to_pixel(enum video_format format, uint32_t *pixel)
{
	switch (format) {
	case VIDEO_FORMAT_I420:
		*pixel = NVIPixel_I420;
		return true;
	case VIDEO_FORMAT_NV12:
		*pixel = NVIPixel_NV12;
		return true;
	case VIDEO_FORMAT_I422:
		*pixel = NVIPixel_422P;
		return true;
	case VIDEO_FORMAT_I010:
		*pixel = NVIPixel_420P10LE;
		return true;
	case VIDEO_FORMAT_P010:
		*pixel = NVIPixel_P010LE;
		return true;
	case VIDEO_FORMAT_I210:
		*pixel = NVIPixel_422P10LE;
		return true;
	case VIDEO_FORMAT_V210:
		*pixel = NVIPixel_V210;
		return true;
	case VIDEO_FORMAT_Y800:
		*pixel = NVIPixel_Mono;
		return true;
	case VIDEO_FORMAT_I40A:
		*pixel = NVIPixel_420A;
		return true;
	case VIDEO_FORMAT_I42A:
		*pixel = NVIPixel_422A;
		return true;
	default:
		return false;
	}
}

static NVIColorSpace nvi_colorspace_from_obs(enum video_colorspace colorspace, enum video_range_type range)
{
	NVIColorSpace out = {};
	out.range = (range == VIDEO_RANGE_FULL) ? NVIRange_Full : NVIRange_Limited;

	switch (colorspace) {
	case VIDEO_CS_601:
		out.primary = NVIPrimary_BT470BG;
		out.transfer = NVITransfer_BT709;
		out.matrix = NVIMatrix_BT470BG;
		break;
	case VIDEO_CS_SRGB:
		out.primary = NVIPrimary_BT709;
		out.transfer = NVITransfer_IEC61966_2_1;
		out.matrix = NVIMatrix_BT709;
		out.range = NVIRange_Full;
		break;
	case VIDEO_CS_2100_PQ:
		out.primary = NVIPrimary_BT2020;
		out.transfer = NVITransfer_SMPTEST2084;
		out.matrix = NVIMatrix_BT2020_NCL;
		break;
	case VIDEO_CS_2100_HLG:
		out.primary = NVIPrimary_BT2020;
		out.transfer = NVITransfer_ARIB_STD_B67;
		out.matrix = NVIMatrix_BT2020_NCL;
		break;
	case VIDEO_CS_709:
	default:
		out.primary = NVIPrimary_BT709;
		out.transfer = NVITransfer_BT709;
		out.matrix = NVIMatrix_BT709;
		break;
	}

	return out;
}

/* 纳秒时间戳 -> NVI tick（先整再余，避免溢出与浮点精度损失） */
static uint64_t nvi_tick_from_ns(uint64_t ns, uint32_t freq)
{
	const uint64_t seconds = ns / 1000000000ull;
	const uint64_t rem = ns % 1000000000ull;
	return seconds * freq + (rem * freq) / 1000000000ull;
}

/* ------------------------------------------------------------------------- */
/* 对象                                                                      */
/* ------------------------------------------------------------------------- */

struct nvi_frame_item {
	bool video = false;
	std::vector<uint8_t> buffer;
	uint32_t strides[NVI_kMaxPixelPlanes] = {};
	uint32_t offsets[NVI_kMaxPixelPlanes] = {};
	uint32_t planes = 0;
	uint32_t samples = 0; /* 音频：每声道采样数 */
	uint64_t timestamp = 0;
};

struct nvi_output {
	obs_output_t *output = nullptr;

	std::mutex cfg_mutex;
	NviOutputConfig cfg;

	/* 视频/音频参数，start() 时确定，此后只读 */
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t fps_num = 0;
	uint32_t fps_den = 0;
	uint32_t nvi_pixel = 0;
	NVIColorSpace colorspace = {};
	uint32_t channels = 0;
	uint32_t sample_rate = 0;

	std::atomic<bool> started{false};

	/* 发送线程 */
	std::atomic<bool> worker_run{false};
	std::thread worker;
	std::mutex init_mutex;
	std::condition_variable init_cv;
	bool init_done = false;
	bool init_ok = false;

	/* 待发送帧队列 */
	std::mutex queue_mutex;
	std::condition_variable queue_cv;
	std::deque<nvi_frame_item> queue;
	std::atomic<uint64_t> dropped{0};

	/* 统计 */
	std::atomic<uint32_t> pulls{0};
	std::atomic<uint64_t> video_sent{0};
	std::atomic<uint64_t> audio_sent{0};

	std::mutex status_mutex;
	std::string last_error;
};

/* 进程内唯一的“当前输出”（工具菜单创建/停止时使用） */
static std::mutex g_singleton_mutex;
static obs_output_t *g_current_output = nullptr;
static nvi_output *g_current_data = nullptr;

static void nvi_output_set_error(nvi_output *o, const std::string &text)
{
	std::lock_guard<std::mutex> lock(o->status_mutex);
	o->last_error = text;
}

/* ------------------------------------------------------------------------- */
/* 设置读写                                                                  */
/* ------------------------------------------------------------------------- */

static void nvi_output_apply(nvi_output *o, obs_data_t *settings)
{
	std::lock_guard<std::mutex> lock(o->cfg_mutex);

	o->cfg.alias = obs_data_get_string(settings, NVI_OUT_ALIAS);
	/* nvi_out_local：本机网络接口（对应 NVISendAllocParam::local），留空表示由库自选 */
	o->cfg.local = obs_data_get_string(settings, NVI_OUT_LOCAL);
	o->cfg.tags = obs_data_get_string(settings, NVI_OUT_TAGS);
	o->cfg.codec = nvi_codec_from_index(obs_data_get_int(settings, NVI_OUT_CODEC));
	o->cfg.video = obs_data_get_bool(settings, NVI_OUT_VIDEO);
	o->cfg.audio = obs_data_get_bool(settings, NVI_OUT_AUDIO);
}

/* ------------------------------------------------------------------------- */
/* 发送线程                                                                  */
/* ------------------------------------------------------------------------- */

static void nvi_output_signal_init(nvi_output *o, bool ok)
{
	{
		std::lock_guard<std::mutex> lock(o->init_mutex);
		o->init_ok = ok;
		o->init_done = true;
	}
	o->init_cv.notify_all();
}

static bool nvi_output_worker_init(nvi_output *o, const NviOutputConfig &cfg, NVI_CONTEXT *ctx_out, NVI_SENDER *sender_out)
{
	NVI_CONTEXT ctx = nvi_context_acquire();
	if (!ctx) {
		nvi_output_set_error(o, "NVI context create failed");
		return false;
	}

	NVISendAllocParam param = {};
	param.alias = cfg.alias.empty() ? nullptr : cfg.alias.c_str();
	param.local = cfg.local.empty() ? nullptr : cfg.local.c_str();
	param.tags = cfg.tags.empty() ? nullptr : cfg.tags.c_str();

	NVI_SENDER sender = NVISendAlloc(ctx, &param);
	if (!sender) {
		const std::string err = nvi_error_text(NVILastError());
		nvi_output_set_error(o, err);
		NVI_LOG(LOG_ERROR, "NVISendAlloc('%s') failed: %s", cfg.alias.c_str(), err.c_str());
		nvi_context_release();
		return false;
	}

	/*
	 * 这里刻意不调用 NVISendPreset。
	 *
	 * 在 NVI 2.1.10 上，只要预设里同时给出尺寸、帧率和像素格式，编码器就不再产出
	 * 任何可解码的帧：接收端能连上（pulls 增加）、能枚举到信号，但一帧都收不到。
	 * 已用自发自收（probe.exe selftest）逐字段复现：codec+size、codec+fps、
	 * codec+format 单独都正常，三者同时出现即失效，补齐 profile/level/vbv/quality
	 * 也无效。
	 *
	 * 官方示例（SendVideo/SendYUV422HF）同样从不使用预设 —— 编码参数全部由每一帧的
	 * NVIVideoImageFrame::info 与 buffer 描述，编解码器的码率/GOP 由 NVI 运行时决定。
	 */

	*ctx_out = ctx;
	*sender_out = sender;
	return true;
}

static int32_t nvi_output_send_video(NVI_SENDER sender, const nvi_output *o, const NviOutputConfig &cfg,
				     const nvi_frame_item &item)
{
	NVIVideoImageFrame frame = {};
	frame.info.codec = cfg.codec;
	frame.info.width = o->width;
	frame.info.height = o->height;
	frame.info.frame_rate_num = o->fps_num;
	frame.info.frame_rate_den = o->fps_den;
	frame.info.rotation = 0;
	frame.info.frame_format = NVIFrame_Progressive;
	frame.info.colorspace = o->colorspace;
	frame.info.tick.value = nvi_tick_from_ns(item.timestamp, NVI_VIDEO_TICK_FREQ);
	frame.info.tick.freq_num = 1;
	frame.info.tick.freq_den = NVI_VIDEO_TICK_FREQ;
	frame.info.time = (NVIDateTime)(item.timestamp / 1000ull);

	frame.buffer.format = o->nvi_pixel;
	frame.buffer.type = NVIBuffer_HOST;
	for (uint32_t plane = 0; plane < item.planes; ++plane) {
		frame.buffer.planes[plane] = item.buffer.data() + item.offsets[plane];
		frame.buffer.strides[plane] = item.strides[plane];
	}

	frame.side.bytes = nullptr;
	frame.side.size = 0;
	frame.updated = nullptr;

	return NVISendVideo(sender, &frame);
}

static int32_t nvi_output_send_audio(NVI_SENDER sender, const nvi_output *o, const nvi_frame_item &item)
{
	NVIAudioWaveFrame wave = {};
	wave.info.codec = NVICodec_LPCM;
	wave.info.sample_rate = o->sample_rate;
	wave.info.depth = NVIWaveBit_F32;
	wave.info.channels = (uint16_t)o->channels;
	wave.info.tick.value = nvi_tick_from_ns(item.timestamp, o->sample_rate);
	wave.info.tick.freq_num = 1;
	wave.info.tick.freq_den = o->sample_rate;
	wave.info.time = (NVIDateTime)(item.timestamp / 1000ull);

	wave.buffer.data = item.buffer.data();
	wave.buffer.size = item.buffer.size();
	wave.buffer.align = (uint16_t)sizeof(float);
	wave.buffer.samples = (uint16_t)item.samples;

	return NVISendAudio(sender, &wave);
}

static void nvi_output_worker(nvi_output *o)
{
	NviOutputConfig cfg;
	{
		std::lock_guard<std::mutex> lock(o->cfg_mutex);
		cfg = o->cfg;
	}

	NVI_CONTEXT ctx = nullptr;
	NVI_SENDER sender = nullptr;

	if (!nvi_output_worker_init(o, cfg, &ctx, &sender)) {
		nvi_output_signal_init(o, false);
		return;
	}

	nvi_output_signal_init(o, true);

	uint64_t status_at = os_gettime_ns();

	while (o->worker_run.load()) {
		nvi_frame_item item;
		{
			std::unique_lock<std::mutex> lock(o->queue_mutex);
			o->queue_cv.wait(lock, [o] { return !o->queue.empty() || !o->worker_run.load(); });
			if (o->queue.empty())
				break;
			item = std::move(o->queue.front());
			o->queue.pop_front();
		}

		const int32_t err = item.video ? nvi_output_send_video(sender, o, cfg, item)
					       : nvi_output_send_audio(sender, o, item);
		if (err < 0) {
			const std::string text = nvi_error_text(err);
			nvi_output_set_error(o, text);
			NVI_LOG(LOG_ERROR, "NVI send failed: %s", text.c_str());
			o->worker_run = false;
			break;
		}

		if (item.video)
			o->video_sent++;
		else
			o->audio_sent++;

		const uint64_t now = os_gettime_ns();
		if (now - status_at >= 1000000000ull) {
			status_at = now;
			NVISendStatus status = {};
			NVISendStatusGet(sender, &status);
			o->pulls = status.pulls;
		}
	}

	NVISendFree(sender);
	nvi_context_release();

	NVI_LOG(LOG_INFO, "'%s': NVI send thread stopped (%llu video / %llu audio frames)",
		cfg.alias.c_str(), (unsigned long long)o->video_sent.load(),
		(unsigned long long)o->audio_sent.load());
}

static void nvi_output_enqueue(nvi_output *o, nvi_frame_item &&item)
{
	{
		std::lock_guard<std::mutex> lock(o->queue_mutex);
		if (o->queue.size() >= NVI_MAX_QUEUE) {
			o->queue.pop_front();
			const uint64_t count = ++o->dropped;
			if (count == 1 || (count % 120) == 0)
				NVI_LOG(LOG_WARNING, "NVI send queue overflow, %llu frames dropped",
					(unsigned long long)count);
		}
		o->queue.push_back(std::move(item));
	}
	o->queue_cv.notify_one();
}

/* ------------------------------------------------------------------------- */
/* 输出回调                                                                  */
/* ------------------------------------------------------------------------- */

static const char *nvi_output_get_name(void *)
{
	return obs_module_text("NVIPlugin.OutputName");
}

static void nvi_output_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, NVI_OUT_ALIAS, "OBS NVI Output");
	obs_data_set_default_int(settings, NVI_OUT_CODEC, NVI_CODEC_AVC);
	obs_data_set_default_string(settings, NVI_OUT_LOCAL, "");
	obs_data_set_default_string(settings, NVI_OUT_TAGS, "");
	obs_data_set_default_bool(settings, NVI_OUT_VIDEO, true);
	obs_data_set_default_bool(settings, NVI_OUT_AUDIO, true);
}

static obs_properties_t *nvi_output_get_properties(void *)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_text(props, NVI_OUT_ALIAS, obs_module_text("NVIPlugin.OutputProps.Alias"), OBS_TEXT_DEFAULT);

	obs_property_t *codecs = obs_properties_add_list(props, NVI_OUT_CODEC,
							 obs_module_text("NVIPlugin.OutputProps.Codec"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(codecs, obs_module_text("NVIPlugin.OutputProps.Codec.AVC"), NVI_CODEC_AVC);
	obs_property_list_add_int(codecs, obs_module_text("NVIPlugin.OutputProps.Codec.HEVC"), NVI_CODEC_HEVC);
	obs_property_list_add_int(codecs, obs_module_text("NVIPlugin.OutputProps.Codec.HF"), NVI_CODEC_HF);
	obs_property_list_add_int(codecs, obs_module_text("NVIPlugin.OutputProps.Codec.SUVC"), NVI_CODEC_SUVC);

	obs_properties_add_text(props, NVI_OUT_LOCAL, obs_module_text("NVIPlugin.OutputProps.Local"), OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, NVI_OUT_TAGS, obs_module_text("NVIPlugin.OutputProps.Tags"), OBS_TEXT_DEFAULT);

	obs_properties_add_bool(props, NVI_OUT_VIDEO, obs_module_text("NVIPlugin.OutputProps.Video"));
	obs_properties_add_bool(props, NVI_OUT_AUDIO, obs_module_text("NVIPlugin.OutputProps.Audio"));

	return props;
}

static void *nvi_output_create(obs_data_t *settings, obs_output_t *output)
{
	auto *o = new nvi_output();
	o->output = output;

	nvi_output_apply(o, settings);

	{
		std::lock_guard<std::mutex> lock(g_singleton_mutex);
		g_current_data = o;
	}

	return o;
}

static void nvi_output_stop(void *data, uint64_t ts)
{
	(void)ts;

	auto *o = (nvi_output *)data;
	if (!o)
		return;

	if (o->started.load()) {
		o->started = false;
		obs_output_end_data_capture(o->output);
	}

	o->worker_run = false;
	o->queue_cv.notify_all();

	if (o->worker.joinable())
		o->worker.join();

	{
		std::lock_guard<std::mutex> lock(o->queue_mutex);
		o->queue.clear();
	}

	NVI_LOG(LOG_INFO, "NVI output stopped (%llu video / %llu audio frames sent)",
		(unsigned long long)o->video_sent.load(), (unsigned long long)o->audio_sent.load());
}

static void nvi_output_destroy(void *data)
{
	auto *o = (nvi_output *)data;
	if (!o)
		return;

	nvi_output_stop(o, 0);

	{
		std::lock_guard<std::mutex> lock(g_singleton_mutex);
		if (g_current_data == o)
			g_current_data = nullptr;
	}

	delete o;
}

static void nvi_output_update(void *data, obs_data_t *settings)
{
	auto *o = (nvi_output *)data;
	if (!o)
		return;

	nvi_output_apply(o, settings);

	if (o->started.load())
		NVI_LOG(LOG_INFO, "NVI output settings changed; restart the output to take effect");
}

static bool nvi_output_start(void *data)
{
	auto *o = (nvi_output *)data;
	if (!o)
		return false;
	if (o->started.load())
		return false;

	obs_output_set_last_error(o->output, "");

	NviOutputConfig cfg;
	{
		std::lock_guard<std::mutex> lock(o->cfg_mutex);
		cfg = o->cfg;
	}

	video_t *video = obs_output_video(o->output);
	audio_t *audio = obs_output_audio(o->output);

	const bool use_video = cfg.video && video;
	const bool use_audio = cfg.audio && audio;

	if (cfg.video && !video)
		NVI_LOG(LOG_WARNING, "no video data available, video disabled");
	if (cfg.audio && !audio)
		NVI_LOG(LOG_WARNING, "no audio data available, audio disabled");
	if (!use_video && !use_audio) {
		obs_output_set_last_error(o->output, obs_module_text("NVIPlugin.OutputProps.NoAV"));
		return false;
	}

	uint32_t flags = 0;

	if (use_video) {
		const struct video_output_info *vi = video_output_get_info(video);
		uint32_t pixel = 0;
		enum video_format target = vi->format;

		/* HF 编码只接受 422P 输入，其它格式下 NVISendVideo 会直接返回错误 */
		if (cfg.codec == NVICodec_HF)
			target = VIDEO_FORMAT_I422;

		if (!nvi_obs_format_to_pixel(target, &pixel)) {
			NVI_LOG(LOG_WARNING, "canvas format %d is not supported natively, converting to NV12",
				(int)target);
			target = VIDEO_FORMAT_NV12;
			if (!nvi_obs_format_to_pixel(target, &pixel)) {
				obs_output_set_last_error(o->output, "no usable NVI pixel format");
				return false;
			}
		}

		o->width = vi->width;
		o->height = vi->height;
		o->fps_num = vi->fps_num;
		o->fps_den = vi->fps_den;
		o->nvi_pixel = pixel;
		o->colorspace = nvi_colorspace_from_obs(vi->colorspace, vi->range);

		if (target != vi->format) {
			struct video_scale_info conversion = {};
			conversion.format = target;
			conversion.width = o->width;
			conversion.height = o->height;
			conversion.colorspace = vi->colorspace;
			conversion.range = vi->range;
			obs_output_set_video_conversion(o->output, &conversion);
		}

		flags |= OBS_OUTPUT_VIDEO;
	} else {
		o->nvi_pixel = 0;
	}

	if (use_audio) {
		o->sample_rate = audio_output_get_sample_rate(audio);
		o->channels = (uint32_t)audio_output_get_channels(audio);

		/* NVI 只接受交错 PCM，这里让 OBS 直接把音频转成交错 float32 */
		struct audio_convert_info conversion = {};
		conversion.format = AUDIO_FORMAT_FLOAT;
		conversion.samples_per_sec = o->sample_rate;
		conversion.speakers = audio_output_get_info(audio)->speakers;
		obs_output_set_audio_conversion(o->output, &conversion);

		flags |= OBS_OUTPUT_AUDIO;
	} else {
		o->channels = 0;
		o->sample_rate = 0;
	}

	/* 启动发送线程并等待 NVI 发送器就绪 */
	{
		std::lock_guard<std::mutex> lock(o->init_mutex);
		o->init_done = false;
		o->init_ok = false;
	}
	o->worker_run = true;
	o->worker = std::thread(nvi_output_worker, o);

	{
		std::unique_lock<std::mutex> lock(o->init_mutex);
		o->init_cv.wait(lock, [o] { return o->init_done; });
	}

	if (!o->init_ok) {
		o->worker_run = false;
		o->queue_cv.notify_all();
		if (o->worker.joinable())
			o->worker.join();

		std::string error;
		{
			std::lock_guard<std::mutex> lock(o->status_mutex);
			error = o->last_error;
		}
		if (error.empty())
			error = obs_module_text("NVIPlugin.OutputProps.StartFailed");
		obs_output_set_last_error(o->output, error.c_str());
		return false;
	}

	o->started = true;

	if (!obs_output_begin_data_capture(o->output, flags)) {
		o->started = false;
		o->worker_run = false;
		o->queue_cv.notify_all();
		if (o->worker.joinable())
			o->worker.join();
		return false;
	}

	NVI_LOG(LOG_INFO, "NVI output '%s' started: %s %ux%u %.3f fps, video %s, audio %s",
		cfg.alias.c_str(), nvi_codec_name(cfg.codec), o->width, o->height,
		o->fps_den ? (double)o->fps_num / (double)o->fps_den : 0.0, use_video ? "on" : "off",
		use_audio ? "on" : "off");

	return true;
}

static void nvi_output_raw_video(void *data, struct video_data *frame)
{
	auto *o = (nvi_output *)data;
	if (!o || !o->started.load() || o->nvi_pixel == 0 || o->width == 0 || o->height == 0)
		return;

	nvi_frame_item item;
	item.video = true;
	item.timestamp = frame->timestamp;

	size_t total = 0;
	for (uint32_t plane = 0; plane < NVI_kMaxPixelPlanes; ++plane) {
		if (!frame->data[plane])
			break;

		const uint32_t stride = frame->linesize[plane];
		const uint32_t rows = nvi_plane_height(o->nvi_pixel, plane, o->height);
		if (stride == 0 || rows == 0)
			break;

		item.strides[plane] = stride;
		item.offsets[plane] = (uint32_t)total;
		total += (size_t)stride * rows;
		item.planes = plane + 1;
	}

	if (item.planes == 0 || total == 0)
		return;

	item.buffer.resize(total);
	for (uint32_t plane = 0; plane < item.planes; ++plane) {
		const uint32_t rows = nvi_plane_height(o->nvi_pixel, plane, o->height);
		memcpy(item.buffer.data() + item.offsets[plane], frame->data[plane], (size_t)item.strides[plane] * rows);
	}

	nvi_output_enqueue(o, std::move(item));
}

static void nvi_output_raw_audio(void *data, struct audio_data *frame)
{
	auto *o = (nvi_output *)data;
	if (!o || !o->started.load() || o->channels == 0 || o->sample_rate == 0)
		return;
	if (!frame->data[0] || frame->frames == 0)
		return;

	const size_t bytes = (size_t)frame->frames * o->channels * sizeof(float);

	nvi_frame_item item;
	item.video = false;
	item.timestamp = frame->timestamp;
	item.samples = frame->frames;
	item.buffer.resize(bytes);
	memcpy(item.buffer.data(), frame->data[0], bytes);

	nvi_output_enqueue(o, std::move(item));
}

obs_output_info nvi_output_get_info()
{
	obs_output_info info = {};

	info.id = "nvi_output";
	info.flags = OBS_OUTPUT_AV;

	info.get_name = nvi_output_get_name;
	info.create = nvi_output_create;
	info.destroy = nvi_output_destroy;
	info.start = nvi_output_start;
	info.stop = nvi_output_stop;
	info.update = nvi_output_update;
	info.get_defaults = nvi_output_get_defaults;
	info.get_properties = nvi_output_get_properties;

	info.raw_video = nvi_output_raw_video;
	info.raw_audio = nvi_output_raw_audio;

	return info;
}

/* ------------------------------------------------------------------------- */
/* 工具菜单接口                                                              */
/* ------------------------------------------------------------------------- */

bool nvi_output_start_with(obs_data_t *settings)
{
	nvi_output_stop_current();

	obs_data_t *owned = nullptr;
	if (!settings) {
		owned = obs_data_create();
		settings = owned;
	}

	obs_output_t *output =
		obs_output_create("nvi_output", obs_module_text("NVIPlugin.OutputName"), settings, nullptr);
	if (owned)
		obs_data_release(owned);

	if (!output) {
		NVI_LOG(LOG_ERROR, "failed to create NVI output");
		return false;
	}

	if (!obs_output_start(output)) {
		const char *error = obs_output_get_last_error(output);
		NVI_LOG(LOG_ERROR, "failed to start NVI output: %s", (error && *error) ? error : "unknown error");
		obs_output_release(output);
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(g_singleton_mutex);
		g_current_output = output;
	}

	return true;
}

void nvi_output_stop_current()
{
	obs_output_t *output = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_singleton_mutex);
		output = g_current_output;
		g_current_output = nullptr;
	}

	if (output) {
		obs_output_stop(output);
		obs_output_release(output);
	}
}

bool nvi_output_is_running()
{
	std::lock_guard<std::mutex> lock(g_singleton_mutex);
	return g_current_data && g_current_data->started.load();
}

NviOutputStatus nvi_output_query_status()
{
	NviOutputStatus status;

	std::lock_guard<std::mutex> lock(g_singleton_mutex);

	nvi_output *o = g_current_data;
	if (!o)
		return status;

	status.valid = true;
	status.running = o->started.load();
	status.pulled = o->pulls.load();

	{
		std::lock_guard<std::mutex> status_lock(o->status_mutex);
		status.last_error = o->last_error;
	}

	return status;
}
