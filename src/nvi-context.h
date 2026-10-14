#pragma once

#include <NVI/API.h>

/**
 * 进程级 NVI 上下文（引用计数单例）。
 *
 * NVI 官方说明“通常一个进程只有一个上下文实例”，因此输入源与输出共用同一个
 * NVI_CONTEXT：第一个使用者创建，最后一个使用者释放。
 */
NVI_CONTEXT nvi_context_acquire();

/** 释放一次引用，引用归零时 NVIContextDestory。 */
void nvi_context_release();

/**
 * 常驻引用：模块加载时持有、卸载时释放，使上下文在整个 OBS 会话内保持存活。
 *
 * NVI 的网络发现结果是在上下文内部异步累积的：上下文刚建好时
 * `NVINetworkEnumStream` 会返回空，需要同一个上下文再存活一小段时间才能看到流。
 * 若每次枚举都新建/销毁上下文（引用归零即 Destory），枚举将永远返回空 ——
 * 这正是“刚启动 OBS 时刷新不出信号源，等输出开始后（输出线程长期持有一个引用）
 * 才刷得出来”的原因。
 */
void nvi_context_hold();

/** 释放 nvi_context_hold 持有的常驻引用。 */
void nvi_context_drop();

/** 当前引用计数（仅用于日志/诊断）。 */
int nvi_context_refs();
