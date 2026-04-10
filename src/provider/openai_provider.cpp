#include "openai_provider.hpp"
#include <cpr/cpr.h>
#include <stdexcept>

namespace acecode {

OpenAiCompatProvider::OpenAiCompatProvider(const std::string& base_url,
                                           const std::string& api_key,
                                           const std::string& model)
    : base_url_(base_url), api_key_(api_key), model_(model) {}

nlohmann::json OpenAiCompatProvider::build_request_body(
    const std::vector<ChatMessage>& messages,
    const std::vector<ToolDef>& tools
) const {
    nlohmann::json body;
    body["model"] = model_;

    // Build messages array
    nlohmann::json msgs_json = nlohmann::json::array();
    for (const auto& msg : messages) {
        nlohmann::json m;
        m["role"] = msg.role;

        if (msg.role == "assistant" && !msg.tool_calls.is_null() && !msg.tool_calls.empty()) {
            // Assistant message with tool calls
            if (!msg.content.empty()) {
                m["content"] = msg.content;
            } else {
                m["content"] = nullptr;
            }
            m["tool_calls"] = msg.tool_calls;
        } else if (msg.role == "tool") {
            m["content"] = msg.content;
            m["tool_call_id"] = msg.tool_call_id;
        } else {
            m["content"] = msg.content;
        }

        msgs_json.push_back(m);
    }
    body["messages"] = msgs_json;

    // Build tools array
    if (!tools.empty()) {
        nlohmann::json tools_json = nlohmann::json::array();
        for (const auto& tool : tools) {
            nlohmann::json t;
            t["type"] = "function";
            t["function"]["name"] = tool.name;
            t["function"]["description"] = tool.description;
            t["function"]["parameters"] = tool.parameters;
            tools_json.push_back(t);
        }
        body["tools"] = tools_json;
    }

    return body;
}

ChatResponse OpenAiCompatProvider::parse_response(const nlohmann::json& j) {
    ChatResponse resp;

    if (!j.contains("choices") || j["choices"].empty()) {
        resp.content = "[Error] No choices in API response.";
        resp.finish_reason = "error";
        return resp;
    }

    const auto& choice = j["choices"][0];
    resp.finish_reason = choice.value("finish_reason", "stop");

    const auto& message = choice["message"];

    if (message.contains("content") && !message["content"].is_null()) {
        resp.content = message["content"].get<std::string>();
    }

    if (message.contains("tool_calls") && message["tool_calls"].is_array()) {
        for (const auto& tc : message["tool_calls"]) {
            ToolCall call;
            call.id = tc["id"].get<std::string>();
            call.function_name = tc["function"]["name"].get<std::string>();
            call.function_arguments = tc["function"]["arguments"].get<std::string>();
            resp.tool_calls.push_back(call);
        }
    }

    return resp;
}

ChatResponse OpenAiCompatProvider::chat(
    const std::vector<ChatMessage>& messages,
    const std::vector<ToolDef>& tools
) {
    nlohmann::json body = build_request_body(messages, tools);

    std::string url = base_url_ + "/chat/completions";

    cpr::Header headers = {
        {"Content-Type", "application/json"}
    };
    if (!api_key_.empty()) {
        headers["Authorization"] = "Bearer " + api_key_;
    }

    cpr::Response r = cpr::Post(
        cpr::Url{url},
        headers,
        cpr::Body{body.dump()},
        cpr::Timeout{120000} // 2 minutes timeout for LLM responses
    );

    if (r.status_code == 0) {
        ChatResponse resp;
        resp.content = "[Error] Connection failed: " + r.error.message;
        resp.finish_reason = "error";
        return resp;
    }

    if (r.status_code != 200) {
        ChatResponse resp;
        resp.content = "[Error] HTTP " + std::to_string(r.status_code) + ": " + r.text;
        resp.finish_reason = "error";
        return resp;
    }

    try {
        nlohmann::json response_json = nlohmann::json::parse(r.text);
        return parse_response(response_json);
    } catch (const nlohmann::json::parse_error& e) {
        ChatResponse resp;
        resp.content = "[Error] Failed to parse response JSON: " + std::string(e.what());
        resp.finish_reason = "error";
        return resp;
    }
}

} // namespace acecode
