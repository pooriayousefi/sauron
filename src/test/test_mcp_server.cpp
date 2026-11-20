// ============================================================================
//  src/test/test_mcp_server.cpp — SAURON MCP Streamable HTTP Server Demo
//  Project: SAURON (Specialized Autonomous Units for Reasoning, Orchestration, and Navigation)
//  Developed by: Pooria Yousefi
//  License: Apache 2.0
//
//  Build:
//    clang++ -std=c++20 -O3 -pthread -I include src/test/test_mcp_server.cpp -o bin/test_mcp_server
//    g++     -std=c++20 -O3 -pthread -I include src/test/test_mcp_server.cpp -o bin/test_mcp_server
//
//  Run:
//    ./bin/test_mcp_server (starts the demo server on 127.0.0.1:8082)
// ============================================================================

#include <iostream>
#include <string>
#include "pooriayousefi/mcp/server.hpp"

int main()
{
    using namespace pooriayousefi;
    int exit_code{0};
    std::cout << "[test_mcp_server] Starting MCP Server (2026-07-28 stateless spec) on http://127.0.0.1:8082/mcp ...\n";

    mcp::Server server{};

    mcp::ToolDefinition calc_tool{};
    calc_tool.name = "calculator";
    calc_tool.description = "Perform basic arithmetic calculations (add, subtract, multiply, divide)";
    calc_tool.input_schema = {
        {"type", "object"},
        {"properties", {
            {"operation", {{"type", "string"}, {"enum", {"add", "subtract", "multiply", "divide"}}}},
            {"a", {{"type", "number"}}},
            {"b", {{"type", "number"}}}
        }},
        {"required", {"operation", "a", "b"}}
    };

    server.register_tool(calc_tool, [](const Json& args)
    {
        mcp::ToolCallResult res{};
        std::string op = args.value("operation", "add");
        double a = args.value("a", 0.0);
        double b = args.value("b", 0.0);
        double calc_result{0.0};

        if (op == "add")
        {
            calc_result = a + b;
        }
        else if (op == "subtract")
        {
            calc_result = a - b;
        }
        else if (op == "multiply")
        {
            calc_result = a * b;
        }
        else if (op == "divide")
        {
            if (b != 0.0)
            {
                calc_result = a / b;
            }
            else
            {
                res.is_error = true;
                res.content = "Division by zero error";
                return res;
            }
        }
        else
        {
            res.is_error = true;
            res.content = "Unknown operation: " + op;
            return res;
        }

        res.is_error = false;
        res.content = std::to_string(calc_result);
        return res;
    });

    mcp::ToolDefinition summarizer_tool{};
    summarizer_tool.name = "text_summarizer";
    summarizer_tool.description = "Summarize text inputs for multi-agent workflows";
    summarizer_tool.input_schema = {
        {"type", "object"},
        {"properties", {
            {"text", {{"type", "string"}}}
        }},
        {"required", {"text"}}
    };

    server.register_tool(summarizer_tool, [](const Json& args)
    {
        mcp::ToolCallResult res{};
        std::string text = args.value("text", "");
        res.is_error = false;
        res.content = "Summary: [Processed " + std::to_string(text.size()) + " bytes of text successfully]";
        return res;
    });

    try
    {
        if (!server.start_http("127.0.0.1", 8082))
        {
            std::cerr << "[test_mcp_server] Failed to listen on 127.0.0.1:8082\n";
            exit_code = 1;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "[test_mcp_server] Error: " << e.what() << "\n";
        exit_code = 1;
    }

    return exit_code;
}