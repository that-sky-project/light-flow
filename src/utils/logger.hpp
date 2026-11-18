#ifndef __API_MOD_LOGGER_HH__
#define __API_MOD_LOGGER_HH__

#include <htmodloader/htmodloader.h>

// The original FLAME logger wrote to flame.log through OsApi. This project
// only targets Win64 and already has the HTModLoader console, so the same LOG
// macros are routed there directly.
#define LOG(msg, ...) HTTellText("[light-flow] " msg, ## __VA_ARGS__)
#define LOGI(msg, ...) HTTellText("[light-flow][I] " msg, ## __VA_ARGS__)
#define LOGW(msg, ...) HTTellText("[light-flow][W] " msg, ## __VA_ARGS__)
#define LOGE(msg, ...) HTTellText("[light-flow][E] " msg, ## __VA_ARGS__)

#endif
