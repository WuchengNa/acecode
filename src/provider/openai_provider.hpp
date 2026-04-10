#pragma once

#include "llm_provider.hpp"
#include "../config/config.hpp"

namespace acecode {

class OpenAiCompatProvider : public LlmProvider {
public:
    OpenAiCompatProvider(const std::string& base_url,
                         const std::string& api_key,
                         const std::string& model);

    ChatResponse chat(
        const std::vector<ChatMessage>& messages,
        const std::vector<ToolDef>& tools
    ) override;

    std::string name() const override { return "openai"; }
    bool is_authenticated() override { return true; } // always ready if config is set

protected:
    // Build the request JSON body (reusable by CopilotProvider)
    nlohmann::json build_request_body(
        const std::vector<ChatMessage>& messages,
        const std::vector<ToolDef>& tools
    ) const;

    // Parse a chat completions response JSON (reusable by CopilotProvider)
    static ChatResponse parse_response(const nlohmann::json& j);

    std::string base_url_;
    std::string api_key_;
    std::string model_;
};

} // namespace acecode
