#include "nvi-context.h"
#include "nvi-plugin.h"

#include <util/platform.h>

#include <mutex>
#include <string>

static std::mutex g_ctx_mutex;
static NVI_CONTEXT g_ctx = nullptr;
static int g_refs = 0;

/* NVI.dll 是否存在（只探测一次） */
static std::once_flag g_runtime_once;
static bool g_runtime_available = false;

static bool nvi_runtime_available()
{
	std::call_once(g_runtime_once, []() {
		/* NVI API 通过导入库延迟加载：若 NVI.dll 不存在而直接调用，会由延迟加载
		 * 抛出结构异常。这里先自行探测一次，失败则跳过所有 NVI 调用，插件本身
		 * 仍能正常加载（只是源/输出提示“NVI 库不可用”）。 */
		void *handle = os_dlopen("NVI.dll");
		g_runtime_available = handle != nullptr;
		if (!g_runtime_available)
			NVI_LOG(LOG_WARNING, "NVI runtime library (NVI.dll) is not available");
	});

	return g_runtime_available;
}

static void nvi_log_callback(uint32_t level, const char *message, size_t length)
{
	if (!message || length == 0)
		return;

	/* message 不保证以 '\0' 结尾 */
	std::string msg(message, length);

	int obs_level;
	switch (level) {
	case NVILog_FATAL:
	case NVILog_ALERT:
	case NVILog_CRIT:
	case NVILog_FAULT:
		obs_level = LOG_ERROR;
		break;
	case NVILog_WARN:
		obs_level = LOG_WARNING;
		break;
	case NVILog_NOTICE:
		obs_level = LOG_INFO;
		break;
	case NVILog_INFO:
		obs_level = LOG_INFO;
		break;
	default:
		obs_level = LOG_DEBUG;
		break;
	}

	blog(obs_level, "[nvi] %s", msg.c_str());
}

NVI_CONTEXT nvi_context_acquire()
{
	if (!nvi_runtime_available())
		return nullptr;

	std::lock_guard<std::mutex> lock(g_ctx_mutex);

	if (!g_ctx) {
		NVILogConfigParam log_cfg = {};
		log_cfg.Message = nvi_log_callback;
		log_cfg.max_level = NVILog_INFO;
		log_cfg.enable_stdout = false;
		log_cfg.enable_prefix = false;
		NVILogConfig(&log_cfg);

		g_ctx = NVIContextCreate(nullptr);
		if (!g_ctx) {
			NVI_LOG(LOG_ERROR, "NVIContextCreate failed (error=%d)", NVILastError());
			return nullptr;
		}

		NVI_LOG(LOG_INFO, "NVI library '%s' initialized", NVIVersionReadable());
	}

	g_refs++;
	return g_ctx;
}

void nvi_context_release()
{
	std::lock_guard<std::mutex> lock(g_ctx_mutex);

	if (g_refs > 0)
		g_refs--;

	if (g_refs == 0 && g_ctx) {
		NVIContextDestory(g_ctx);
		g_ctx = nullptr;
		NVI_LOG(LOG_INFO, "NVI library released");
	}
}

void nvi_context_hold()
{
	if (!nvi_context_acquire())
		return;

	NVI_LOG(LOG_INFO, "NVI context held for the plugin lifetime");
}

void nvi_context_drop()
{
	nvi_context_release();
}

int nvi_context_refs()
{
	std::lock_guard<std::mutex> lock(g_ctx_mutex);
	return g_refs;
}
