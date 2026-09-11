#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <thread>
#include <iostream>
#include <lepus/utils/logger.hpp>
#include <lepus/hook/hook_manager.hpp>
#include <lepus/ntqq/ntqq_engine.hpp>
#include <lepus/ntqq/mojo_pipe_reader.hpp>
#include <lepus/network/protocol_server.hpp>
#include <lepus/core/agent_tool_registry.hpp>

void OpenDebugConsole() {
    if (AllocConsole()) {
        FILE* fp = nullptr;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        freopen_s(&fp, "CONIN$", "r", stdin);
        
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
        SetConsoleTitleA("Lepus NTQQ Core [Active Monitor]");
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode)) {
            SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
        std::cout << "\033[36m"
                  << "=========================================================\n"
                  << "        Lepus Core Active Diagnostic Console             \n"
                  << "   Universal NTQQ Memory Hook & Protocol Engine v1.0.0   \n"
                  << "=========================================================\n"
                  << "\033[0m" << std::endl;
    }
}

DWORD WINAPI LepusMainThread(LPVOID lpParam) {
    OpenDebugConsole();
    LEPUS_LOG_INFO("==================================================");
    LEPUS_LOG_INFO("  Lepus NTQQ In-Process Modern Protocol Engine    ");
    LEPUS_LOG_INFO("  Version: 1.0.0-Modern | C++20 Standard Binary   ");
    LEPUS_LOG_INFO("==================================================");

    // 1. 初始化 MinHook 引擎
    if (!lepus::hook::HookManager::instance().initialize()) {
        LEPUS_LOG_ERROR("Failed to initialize Lepus HookManager!");
        return 1;
    }

    // 2. 初始化 NTQQ Native 内核引擎
    lepus::ntqq::NTQQEngine::instance().initialize(10001);
    LEPUS_LOG_INFO("NTQQ kernel bridge initialized successfully.");

    // 3. 注册内置 Agent 智能工具集 (天气、网页抓取等示例)
    auto& tools = lepus::core::AgentToolRegistry::instance();
    nlohmann::json weather_schema = {
        {"type", "object"},
        {"properties", {
            {"city", {{"type", "string"}, {"description", "城市名"}}}
        }},
        {"required", nlohmann::json::array({"city"})}
    };
    tools.register_tool("weather", "查询实时天气情况", weather_schema, [](const nlohmann::json& args) -> nlohmann::json {
        std::string city = args.value("city", "未知");
        return {{"status", "ok"}, {"city", city}, {"weather", "晴朗"}, {"temperature", "24°C"}};
    });

    // 4. 启动 OneBot v11 & Modern Agent RPC Server
    static lepus::network::ProtocolServer server;
    server.start(3000, 3001);

    LEPUS_LOG_INFO("Lepus OneBot v11 HTTP: http://0.0.0.0:3000, WS: ws://0.0.0.0:3001");

    // 5. 启动 Mojo Named Pipe Reader 实时捕获 NTQQ 进站消息推送
    auto& pipe_reader = lepus::ntqq::MojoPipeReader::instance();
    pipe_reader.set_callback([](const lepus::ntqq::ParsedMessage& msg, const nlohmann::json& ob11_event) {
        // 传递给 NTQQEngine，NTQQEngine 会自动广播给 ProtocolServer 中连接的所有 WebSocket 客户端
        lepus::ntqq::NTQQEngine::instance().emit_incoming_message(ob11_event);
    });
    pipe_reader.start();
    LEPUS_LOG_INFO("NTQQ Mojo Pipe Reader started.");

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, LepusMainThread, nullptr, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        lepus::ntqq::MojoPipeReader::instance().stop();
        lepus::hook::HookManager::instance().shutdown();
        break;
    }
    return TRUE;
}
