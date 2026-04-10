#include "tool_executor.hpp"

namespace acecode {

void ToolExecutor::register_tool(const ToolImpl& tool) {
    tools_[tool.definition.name] = tool;
}

std::vector<ToolDef> ToolExecutor::get_tool_definitions() const {
    std::vector<ToolDef> defs;
    for (const auto& [name, impl] : tools_) {
        defs.push_back(impl.definition);
    }
    return defs;
}

ToolResult ToolExecutor::execute(const std::string& tool_name, const std::string& arguments_json) const {
    auto it = tools_.find(tool_name);
    if (it == tools_.end()) {
        return ToolResult{"[Error] Unknown tool: " + tool_name, false};
    }
    return it->second.execute(arguments_json);
}

bool ToolExecutor::has_tool(const std::string& name) const {
    return tools_.find(name) != tools_.end();
}

ChatMessage ToolExecutor::format_tool_result(const std::string& tool_call_id, const ToolResult& result) {
    ChatMessage msg;
    msg.role = "tool";
    msg.content = result.output;
    msg.tool_call_id = tool_call_id;
    return msg;
}

ChatMessage ToolExecutor::format_assistant_tool_calls(const ChatResponse& response) {
    ChatMessage msg;
    msg.role = "assistant";
    msg.content = response.content;

    nlohmann::json tc_array = nlohmann::json::array();
    for (const auto& tc : response.tool_calls) {
        nlohmann::json tc_obj;
        tc_obj["id"] = tc.id;
        tc_obj["type"] = "function";
        tc_obj["function"]["name"] = tc.function_name;
        tc_obj["function"]["arguments"] = tc.function_arguments;
        tc_array.push_back(tc_obj);
    }
    msg.tool_calls = tc_array;

    return msg;
}

} // namespace acecode
