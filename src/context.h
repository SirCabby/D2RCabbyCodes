#pragma once

#include <D2RLPlugin/api.h>

// The plugin context D2RLoader hands us at load: valid from D2RLoaderLoadPlugin
// until D2RLoaderUnloadPlugin, on every thread.
namespace d2rcc {

const D2RL::PluginContext* context();
void set_context(const D2RL::PluginContext* ctx);

}  // namespace d2rcc
