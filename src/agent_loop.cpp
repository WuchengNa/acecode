#include "agent_loop.hpp"
#include <nlohmann/json.hpp>

namespace acecode {

AgentLoop::AgentLoop(LlmProvider& provider, ToolExecutor& tools, AgentCallbacks callbacks)
    : provider_(provider)
    , tools_(tools)
    , callbacks_(std::move(callbacks))
{
}

void AgentLoop::submit(const std::string& user_message) {
    // Add user message
    ChatMessage user_msg;
    user_msg.role = "user";
    user_msg.content = user_message;
    messages_.push_back(user_msg);

    if (callbacks_.on_busy_changed) {
        callbacks_.on_busy_changed(true);
    }

    auto tool_defs = tools_.get_tool_definitions();

    // Agent loop: keep calling the provider until we get a pure text response
    while (true) {
        ChatResponse response;
        try {
            response = provider_.chat(messages_, tool_defs);
        } catch (const std::exception& e) {
            if (callbacks_.on_message) {
                callbacks_.on_message("error", std::string("[Error] ") + e.what(), false);
            }
            break;
        }

        if (!response.has_tool_calls()) {
            // Pure text response -- conversation turn is done
            ChatMessage assistant_msg;
            assistant_msg.role = "assistant";
            assistant_msg.content = response.content;
            messages_.push_back(assistant_msg);

            if (callbacks_.on_message) {
                callbacks_.on_message("assistant", response.content, false);
            }
            break;
        }

        // Assistant wants to call tools
        // Record the assistant message with tool_calls in the history
        messages_.push_back(ToolExecutor::format_assistant_tool_calls(response));

        // Execute each tool call
        for (const auto& tc : response.tool_calls) {
            // Notify TUI about the tool call
            if (callbacks_.on_message) {
                callbacks_.on_message("tool_call",
                    "[Tool: " + tc.function_name + "] " + tc.function_arguments, true);
            }

            // Ask for user confirmation
            if (callbacks_.on_tool_confirm) {
                bool approved = callbacks_.on_tool_confirm(tc.function_name, tc.function_arguments);
                if (!approved) {
                    // User denied -- insert a tool result saying "denied" and break the loop
                    ToolResult denied_result{"[User denied tool execution]", false};
                    messages_.push_back(ToolExecutor::format_tool_result(tc.id, denied_result));
                    if (callbacks_.on_message) {
                        callbacks_.on_message("tool_result", "[User denied tool execution]", true);
                    }
                    continue;
                }
            }

            // Execute the tool
            ToolResult result;
            if (tools_.has_tool(tc.function_name)) {
                result = tools_.execute(tc.function_name, tc.function_arguments);
            } else {
                result = ToolResult{"Unknown tool: " + tc.function_name, false};
            }

            // Record tool result in conversation
            messages_.push_back(ToolExecutor::format_tool_result(tc.id, result));

            if (callbacks_.on_message) {
                callbacks_.on_message("tool_result", result.output, true);
            }
        }

        // Loop back to call the provider again with the tool results
    }

    if (callbacks_.on_busy_changed) {
        callbacks_.on_busy_changed(false);
    }
}

} // namespace acecode
