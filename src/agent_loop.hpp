#pragma once

#include "provider/llm_provider.hpp"
#include "tool/tool_executor.hpp"

#include <vector>
#include <string>
#include <functional>
#include <mutex>

namespace acecode {

// Callbacks for the TUI to observe agent loop events
struct AgentCallbacks {
    // Called when a new message is added to the conversation
    std::function<void(const std::string& role, const std::string& content, bool is_tool)> on_message;

    // Called when the agent starts/stops processing
    std::function<void(bool busy)> on_busy_changed;

    // Called to request user confirmation for a tool call.
    // Returns true if user approves, false if denied.
    // tool_name and arguments are provided for display.
    std::function<bool(const std::string& tool_name, const std::string& arguments)> on_tool_confirm;
};

class AgentLoop {
public:
    AgentLoop(LlmProvider& provider, ToolExecutor& tools, AgentCallbacks callbacks);

    // Submit a user message and run the agent loop until a final text response.
    // This runs synchronously and should be called from a background thread.
    void submit(const std::string& user_message);

    // Get the full conversation history
    const std::vector<ChatMessage>& messages() const { return messages_; }

private:
    LlmProvider& provider_;
    ToolExecutor& tools_;
    AgentCallbacks callbacks_;
    std::vector<ChatMessage> messages_;
};

} // namespace acecode
