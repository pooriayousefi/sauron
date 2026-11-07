// ============================================================================
//  src/test/test_mcp_client.cpp — SAURON MCP Client & OpenAI Bridge Demo
//  Project: SAURON (Specialized Autonomous Units for Reasoning, Orchestration, and Navigation)
//  Developed by: Pooria Yousefi
//  License: Apache 2.0
//
//  Build:
//    clang++ -std=c++20 -O3 -pthread -I include src/test/test_mcp_client.cpp -o bin/test_mcp_client
//    g++     -std=c++20 -O3 -pthread -I include src/test/test_mcp_client.cpp -o bin/test_mcp_client
//
//  Run:
//    ./bin/test_mcp_server (in another terminal), then ./bin/test_mcp_client
// ============================================================================

#include <iostream>
#include "pooriayousefi/utility/common.hpp"
#include "pooriayousefi/mcp/client.hpp"
#include "pooriayousefi/mcp/openai_bridge.hpp"

int main()
{
    using namespace pooriayousefi;

    int exit_code{0};
    std::cout << "[test_mcp_client] Connecting to MCP Server at http://127.0.0.1:8080 ...\n";

    mcp::Client client{"127.0.0.1", 8080};

    auto tools_res = client.list_tools();
    if (tools_res.has_value())
    {
        const auto& tools = tools_res.get_value();
        std::cout << "[test_mcp_client] Discovered " << tools.size() << " tools from server:\n";
        for (const auto& tool : tools)
        {
            std::cout << "  - " << tool.name << ": " << tool.description << "\n";
        }

        Json openai_tools = mcp::OpenAIBridge::translate_tools_to_openai(tools);
        std::cout << "\n[OpenAI Bridge] Translated OpenAI Tool Schema:\n" << openai_tools.dump(2) << "\n\n";
    }
    else
    {
        std::cerr << "[test_mcp_client] Failed to list tools from server.\n";
        exit_code = 1;
    }

    Json calc_args{};
    calc_args["operation"] = "multiply";
    calc_args["a"] = 7.0;
    calc_args["b"] = 6.0;

    auto call_res = client.call_tool("calculator", calc_args);
    if (call_res.has_value())
    {
        const auto& res = call_res.get_value();
        std::cout << "[test_mcp_client] Tool call result (calculator 7 * 6): " << res.content << "\n";

        Json openai_chat_resp = mcp::OpenAIBridge::create_openai_chat_response(
            "gpt-4o",
            res.content,
            "calculator",
            calc_args,
            "call_calc_001"
        );
        std::cout << "\n[OpenAI Bridge] OpenAI Chat Completion Response:\n" << openai_chat_resp.dump(2) << "\n";
    }
    else
    {
        std::cerr << "[test_mcp_client] Tool call failed.\n";
        exit_code = 1;
    }

    return exit_code;
}