#include "bash_tool.hpp"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <array>
#include <string>

namespace acecode {

static ToolResult execute_bash(const std::string& arguments_json) {
    std::string command;
    try {
        auto args = nlohmann::json::parse(arguments_json);
        command = args.value("command", "");
    } catch (...) {
        return ToolResult{"[Error] Failed to parse tool arguments.", false};
    }

    if (command.empty()) {
        return ToolResult{"[Error] No command provided.", false};
    }

    // Execute via platform-appropriate shell
#ifdef _WIN32
    std::string full_cmd = "cmd.exe /c " + command + " 2>&1";
#else
    std::string full_cmd = "/bin/sh -c '" + command + "' 2>&1";
#endif

    std::array<char, 4096> buffer;
    std::string output;

#ifdef _WIN32
    FILE* pipe = _popen(full_cmd.c_str(), "r");
#else
    FILE* pipe = popen(full_cmd.c_str(), "r");
#endif

    if (!pipe) {
        return ToolResult{"[Error] Failed to execute command.", false};
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

#ifdef _WIN32
    int exit_code = _pclose(pipe);
#else
    int exit_code = pclose(pipe);
#endif

    if (output.empty()) {
        output = "(no output)";
    }

    return ToolResult{output, exit_code == 0};
}

ToolImpl create_bash_tool() {
    ToolDef def;
    def.name = "bash";
    def.description = "Execute a shell command and return its output. "
                      "Use this to run commands, check files, install packages, etc.";
    def.parameters = nlohmann::json({
        {"type", "object"},
        {"properties", {
            {"command", {
                {"type", "string"},
                {"description", "The shell command to execute"}
            }}
        }},
        {"required", nlohmann::json::array({"command"})}
    });

    return ToolImpl{def, execute_bash};
}

} // namespace acecode
