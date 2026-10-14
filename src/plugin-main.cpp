#include "nvi-plugin.h"

#include "nvi-context.h"
#include "nvi-output.h"
#include "nvi-source.h"

#include <obs-frontend-api.h>
#include <obs-module.h>

const char *PLUGIN_NAME = "nvi";
const char *PLUGIN_VERSION = "0.1.0";
const char *PLUGIN_DISPLAY_NAME = "NVI";

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

const char *obs_module_name()
{
	return PLUGIN_DISPLAY_NAME;
}

const char *obs_module_description()
{
	return obs_module_text("NVIPlugin.Description");
}

static obs_source_info s_source_info;
static obs_output_info s_output_info;

/* OBS 退出时主动停掉 NVI 输出，确保发送线程先于模块卸载结束 */
static void nvi_frontend_event(enum obs_frontend_event event, void *param)
{
	(void)param;

	if (event == OBS_FRONTEND_EVENT_EXIT)
		nvi_output_stop_current();
}

bool obs_module_load(void)
{
	s_source_info = nvi_source_get_info();
	obs_register_source(&s_source_info);

	s_output_info = nvi_output_get_info();
	obs_register_output(&s_output_info);

	/* 工具菜单需要 frontend；若仅由 libobs 加载则跳过 */
	if (obs_frontend_get_main_window()) {
		nvi_output_settings_register();
		obs_frontend_add_event_callback(nvi_frontend_event, nullptr);
	}

	/* 全程持有 NVI 上下文：网络发现依赖上下文内的异步累积，
	 * 若只在枚举时临时创建（用完即 Destory），列表会一直是空的。 */
	nvi_context_hold();

	blog(LOG_INFO, "[nvi] plugin loaded (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	nvi_output_stop_current();
	obs_frontend_remove_event_callback(nvi_frontend_event, nullptr);
	nvi_context_drop();

	blog(LOG_INFO, "[nvi] plugin unloaded");
}
