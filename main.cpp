#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

#ifdef _WIN32
#include <windows.h>
#endif

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "config/config.hpp"
#include "provider/provider_factory.hpp"
#include "provider/copilot_provider.hpp"
#include "tool/tool_executor.hpp"
#include "tool/bash_tool.hpp"
#include "agent_loop.hpp"

using namespace ftxui;
using namespace acecode;

// ---- Shared TUI state ----
struct TuiState {
    struct Message {
        std::string role;
        std::string content;
        bool is_tool = false;
    };

    std::vector<Message> conversation;
    std::string input_text;
    bool is_waiting = false;
    std::string status_line; // for auth/provider status

    // Tool confirmation state
    bool confirm_pending = false;
    std::string confirm_tool_name;
    std::string confirm_tool_args;
    bool confirm_result = false;
    std::condition_variable confirm_cv;

    std::mutex mu;
};

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    // ---- Load config ----
    AppConfig config = load_config();

    // ---- Create provider ----
    auto provider = create_provider(config);

    // ---- Setup tools ----
    ToolExecutor tools;
    tools.register_tool(create_bash_tool());

    // ---- TUI state ----
    TuiState state;
    state.status_line = "[" + provider->name() + "] model: " +
        (config.provider == "copilot" ? config.copilot.model : config.openai.model);

    auto screen = ScreenInteractive::TerminalOutput();

    // ---- Copilot auth flow (background thread) ----
    std::atomic<bool> auth_done{false};
    std::thread auth_thread;

    if (config.provider == "copilot") {
        auto* copilot = dynamic_cast<CopilotProvider*>(provider.get());
        if (copilot && !copilot->is_authenticated()) {
            {
                std::lock_guard<std::mutex> lk(state.mu);
                state.is_waiting = true;
                state.conversation.push_back({"system", "Authenticating with GitHub Copilot...", false});
            }
            screen.PostEvent(Event::Custom);

            auth_thread = std::thread([&] {
                // Try silent auth first (saved token)
                if (copilot->try_silent_auth()) {
                    {
                        std::lock_guard<std::mutex> lk(state.mu);
                        state.conversation.push_back({"system", "Authenticated (saved token).", false});
                        state.is_waiting = false;
                    }
                    auth_done = true;
                    screen.PostEvent(Event::Custom);
                    return;
                }

                // Need interactive device flow
                auto dc = request_device_code();
                {
                    std::lock_guard<std::mutex> lk(state.mu);
                    state.conversation.push_back({"system",
                        "Open " + dc.verification_uri + " and enter code: " + dc.user_code, false});
                }
                screen.PostEvent(Event::Custom);

                copilot->run_device_flow([&](const std::string& status) {
                    std::lock_guard<std::mutex> lk(state.mu);
                    state.status_line = status;
                    screen.PostEvent(Event::Custom);
                });

                if (copilot->is_authenticated()) {
                    {
                        std::lock_guard<std::mutex> lk(state.mu);
                        state.conversation.push_back({"system", "GitHub Copilot authenticated!", false});
                        state.is_waiting = false;
                        state.status_line = "[copilot] model: " + config.copilot.model;
                    }
                } else {
                    std::lock_guard<std::mutex> lk(state.mu);
                    state.conversation.push_back({"system", "[Error] Authentication failed.", false});
                    state.is_waiting = false;
                }
                auth_done = true;
                screen.PostEvent(Event::Custom);
            });
        } else {
            auth_done = true;
        }
    } else {
        auth_done = true;
    }

    // ---- Agent callbacks ----
    AgentCallbacks callbacks;
    callbacks.on_message = [&](const std::string& role, const std::string& content, bool is_tool) {
        std::lock_guard<std::mutex> lk(state.mu);
        state.conversation.push_back({role, content, is_tool});
        screen.PostEvent(Event::Custom);
    };
    callbacks.on_busy_changed = [&](bool busy) {
        std::lock_guard<std::mutex> lk(state.mu);
        state.is_waiting = busy;
        screen.PostEvent(Event::Custom);
    };
    callbacks.on_tool_confirm = [&](const std::string& tool_name, const std::string& args) -> bool {
        {
            std::lock_guard<std::mutex> lk(state.mu);
            state.confirm_pending = true;
            state.confirm_tool_name = tool_name;
            state.confirm_tool_args = args;
        }
        screen.PostEvent(Event::Custom);

        // Block the agent thread until the user presses y/n in the TUI
        std::unique_lock<std::mutex> lk(state.mu);
        state.confirm_cv.wait(lk, [&] { return !state.confirm_pending; });
        return state.confirm_result;
    };

    AgentLoop agent_loop(*provider, tools, callbacks);

    // ---- Input handling ----
    InputOption input_option;
    input_option.on_enter = [&] {
        std::lock_guard<std::mutex> lk(state.mu);

        // Handle tool confirmation y/n
        if (state.confirm_pending) {
            std::string answer = state.input_text;
            state.input_text.clear();
            bool approved = (!answer.empty() && (answer[0] == 'y' || answer[0] == 'Y'));
            state.confirm_result = approved;
            state.confirm_pending = false;
            state.confirm_cv.notify_one();
            return;
        }

        if (state.is_waiting || state.input_text.empty()) return;
        if (!auth_done) return;

        std::string prompt = state.input_text;
        state.input_text.clear();

        std::thread([&agent_loop, prompt]() {
            agent_loop.submit(prompt);
        }).detach();
    };

    Component input = Input(&state.input_text, "Type your prompt here...", input_option);

    auto renderer = Renderer(input, [&] {
        std::lock_guard<std::mutex> lk(state.mu);

        Elements message_elements;
        for (const auto& msg : state.conversation) {
            if (msg.role == "user") {
                message_elements.push_back(
                    paragraph("[User]: " + msg.content) | color(Color::Blue));
            } else if (msg.role == "assistant") {
                message_elements.push_back(
                    paragraph("[Agent]: " + msg.content) | color(Color::Green));
            } else if (msg.role == "tool_call") {
                message_elements.push_back(
                    paragraph("  -> " + msg.content) | color(Color::Magenta));
            } else if (msg.role == "tool_result") {
                message_elements.push_back(
                    paragraph("  <- " + msg.content) | color(Color::GrayDark));
            } else if (msg.role == "system") {
                message_elements.push_back(
                    paragraph("[*] " + msg.content) | color(Color::Yellow));
            } else if (msg.role == "error") {
                message_elements.push_back(paragraph(msg.content) | color(Color::Red));
            }
        }

        // Prompt line
        std::string prompt_prefix;
        if (state.confirm_pending) {
            prompt_prefix = "[" + state.confirm_tool_name + "] Allow? (y/n): ";
        } else if (state.is_waiting) {
            prompt_prefix = "[wait] ";
        } else {
            prompt_prefix = "> ";
        }

        return vbox({
            hbox({
                text("=== AceCode CLI Agent ===") | bold | color(Color::Cyan),
                text("  "),
                text(state.status_line) | color(Color::GrayLight),
            }),
            separator(),
            vbox(std::move(message_elements)) | flex,
            separator(),
            hbox({
                text(prompt_prefix) | color(state.confirm_pending ? Color::Magenta
                    : (state.is_waiting ? Color::Yellow : Color::Green)),
                input->Render()
            })
        }) | border;
    });

    screen.Loop(renderer);

    if (auth_thread.joinable()) {
        auth_thread.join();
    }

    return 0;
}
