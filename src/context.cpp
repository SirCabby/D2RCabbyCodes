#include "context.h"

namespace d2rcc {
namespace {
const D2RL::PluginContext* volatile g_ctx = nullptr;
}
const D2RL::PluginContext* context() { return g_ctx; }
void set_context(const D2RL::PluginContext* ctx) { g_ctx = ctx; }
}  // namespace d2rcc
