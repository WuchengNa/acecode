#pragma once

#include "llm_provider.hpp"
#include "../config/config.hpp"
#include <memory>

namespace acecode {

std::unique_ptr<LlmProvider> create_provider(const AppConfig& config);

} // namespace acecode
