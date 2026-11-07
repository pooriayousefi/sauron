// ============================================================================
//  pooriayousefi/sauron/agent.hpp — SAURON Single Agent Runtime (Stateless MCP 2026-07-28)
//  Project: SAURON (Specialized Autonomous Units for Reasoning, Orchestration, and Navigation)
//  Developed by: Pooria Yousefi
//  License: Apache 2.0
//
//  Build:
//    (Header-only agent runtime)
// ============================================================================

#pragma once

#include <algorithm>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "nlohmann/json.hpp"
#include "pooriayousefi/utility/common.hpp"
#include "pooriayousefi/utility/expected.hpp"
#include "pooriayousefi/sauron/llm.hpp"
#include "pooriayousefi/mcp/client.hpp"
#include "pooriayousefi/mcp/protocol.hpp"

namespace pooriayousefi::sauron
{
    class Agent
    {
    public:
        struct Config
        {
            llm::ProviderConfig llm{};
            Id id{};
            std::string system_prompt{};
            std::string mcp_config_file{"cfg/mcp_servers.json"};
            int max_cycles{50};
            std::size_t max_observation_length{4000};
        };

        using ToolMap = std::unordered_map<std::string, std::size_t>;
        using SchemaMap = std::unordered_map<std::string, Json>;

    private:
        Id id_;
        Config config_;
        llm::Client llm_client_;
        std::vector<std::unique_ptr<mcp::Client>> tool_clients_;
        std::vector<mcp::ToolDefinition> discovered_tools_;
        Json history_;
        ToolMap tool_map_;
        SchemaMap tool_schemas_;

        [[nodiscard]] static Json sanitize_parameters(const Json &params)
        {
            Json p = params;
            if (p.is_string())
            {
                Json parsed = Json::parse(p.get<std::string>(), nullptr, false);
                if (!parsed.is_discarded() && parsed.is_object())
                {
                    p = parsed;
                }
            }
            if (!p.is_object())
            {
                p = Json::object();
            }
            if (!p.contains("type") || !p["type"].is_string())
            {
                p["type"] = "object";
            }
            if (!p.contains("properties") || !p["properties"].is_object())
            {
                p["properties"] = Json::object();
            }
            return p;
        }

        Expected<void, std::string> setup_tools()
        {
            Expected<void, std::string> result{};

            auto config_result = llm::parse_server_config(config_.mcp_config_file);
            if (!config_result.has_value())
            {
                result.set_error(config_result.get_error());
            }
            else
            {
                std::size_t failed_servers{0};
                std::size_t enabled_servers{0};
                std::string failure_details{};
                for (auto &server_cfg : config_result.get_value())
                {
                    if (!server_cfg.enabled)
                    {
                        continue;
                    }
                    ++enabled_servers;

                    try
                    {
                        std::unique_ptr<mcp::Client> client{};
                        if (server_cfg.stdio_enabled)
                        {
                            std::vector<std::string> arguments{};
                            if (server_cfg.http_enabled)
                            {
                                arguments = {"both", server_cfg.host, std::to_string(server_cfg.port)};
                            }
                            else
                            {
                                arguments = {"stdio"};
                            }
                            arguments.insert(arguments.end(), server_cfg.args.begin(), server_cfg.args.end());
                            client = std::make_unique<mcp::Client>(
                                server_cfg.command, std::move(arguments), server_cfg.env);
                        }
                        else if (server_cfg.http_enabled)
                        {
                            const llm::ParsedURL parsed = llm::parse_url(server_cfg.url);
                            client = std::make_unique<mcp::Client>(parsed.host, parsed.port);
                        }
                        else
                        {
                            continue;
                        }

                        auto discover_result = client->list_tools();
                        if (!discover_result.has_value())
                        {
                            failed_servers++;
                            failure_details += "\n- Failed to discover tools from MCP server '" +
                                server_cfg.name + "': " + discover_result.get_error().message();
                            continue;
                        }

                        std::size_t client_index = tool_clients_.size();
                        for (const auto &tool : discover_result.get_value())
                        {
                            if (tool_map_.contains(tool.name))
                            {
                                continue;
                            }
                            discovered_tools_.push_back(tool);
                            tool_map_[tool.name] = client_index;
                            tool_schemas_[tool.name] = tool.input_schema;
                        }
                        std::cout << "[Agent] Connected MCP server '" << server_cfg.name
                                  << "' (" << discover_result.get_value().size()
                                  << " tools).\n";
                        tool_clients_.push_back(std::move(client));
                    }
                    catch (const std::exception& e)
                    {
                        ++failed_servers;
                        failure_details += "\n- Failed to start MCP server '" +
                            server_cfg.name + "': " + e.what();
                    }
                }

                if (enabled_servers == 0)
                {
                    result.set_error("No MCP servers are enabled in '" +
                                     config_.mcp_config_file + "'.");
                }
                else if (failed_servers > 0)
                {
                    result.set_error("Failed to connect to " +
                                     std::to_string(failed_servers) +
                                     " enabled MCP server(s):" + failure_details);
                }
                else if (tool_clients_.empty())
                {
                    result.set_error("No enabled MCP tool servers connected.");
                }
                else
                {
                    result.set_value();
                }
            }

            return result;
        }

    public:
        explicit Agent(Agent::Config config)
            : id_{config.id}
            , config_{std::move(config)}
            , llm_client_{config_.llm}
            , history_(Json::array())
        {
        }

        Agent(const Agent &) = delete;
        Agent &operator=(const Agent &) = delete;
        Agent(Agent &&) = delete;
        Agent &operator=(Agent &&) = delete;
        ~Agent() = default;

        Expected<void, std::string> setup()
        {
            Expected<void, std::string> result{};

            std::cout << "[Agent] Waiting for the configured LLM endpoint...\n";
            auto llm_check = llm_client_.verify_connection();
            if (!llm_check.has_value())
            {
                result.set_error("LLM provider check failed: " + llm_check.get_error());
            }
            else
            {
                std::cout << "[Agent] LLM is ready. Connecting configured MCP servers...\n";
                auto tools_check = setup_tools();
                if (!tools_check.has_value())
                {
                    result.set_error(tools_check.get_error());
                }
                else
                {
                    std::cout << "[Agent] Connected " << tool_clients_.size()
                              << " MCP server(s); " << discovered_tools_.size()
                              << " tools are ready.\n";
                    result.set_value();
                }
            }

            return result;
        }

        [[nodiscard]] Json build_tools_schema() const
        {
            Json schema = Json::array();
            for (const auto &tool : discovered_tools_)
            {
                Json fn{};
                fn["name"] = tool.name;
                fn["description"] = tool.description.empty() ? ("Tool '" + tool.name + "'.") : tool.description;
                fn["parameters"] = sanitize_parameters(tool.input_schema);

                Json tool_schema{};
                tool_schema["type"] = "function";
                tool_schema["function"] = fn;
                schema.push_back(tool_schema);
            }
            return schema;
        }

        [[nodiscard]] Json dispatch_tool(const std::string &tool_name, const Json &args)
        {
            Json result{};
            auto it = tool_map_.find(tool_name);
            if (it == tool_map_.end())
            {
                result["error"] = "Tool not found: " + tool_name;
                result["is_error"] = true;
            }
            else
            {
                std::size_t client_index = it->second;
                auto call_res = tool_clients_[client_index]->call_tool(tool_name, args);
                if (!call_res.has_value())
                {
                    result["error"] = "MCP tool invocation failed: " + call_res.get_error().message();
                    result["is_error"] = true;
                }
                else
                {
                    const auto &res = call_res.get_value();
                    result["result"] = res.content;
                    result["is_error"] = res.is_error;
                }
            }
            return result;
        }

        std::string reason_act_loop(Json &messages, int max_cycles)
        {
            std::string final_result{};
            Json tools_schema = build_tools_schema();
            int cycle_count{0};

            while (cycle_count < max_cycles && final_result.empty())
            {
                cycle_count++;
                std::cout << '[' << id_ << "] "
                          << (cycle_count == 1
                                  ? "Thinking..."
                                  : "Thinking with tool results...")
                          << '\n' << std::flush;
                Json llm_response = llm_client_.reasoning(messages, tools_schema);

                bool llm_error = llm_response.contains("is_error") && llm_response["is_error"].get<bool>();
                if (llm_error)
                {
                    const std::string error_message =
                        llm_response.contains("error") && llm_response["error"].is_string()
                            ? llm_response["error"].get<std::string>()
                            : "Unknown LLM error.";
                    std::cerr << '[' << id_ << "] LLM request failed: "
                              << error_message << '\n';
                    final_result = "(LLM request failed: " + error_message + ")";
                    break;
                }

                std::string content = llm_response.contains("content") && llm_response["content"].is_string()
                                      ? llm_response["content"].get<std::string>()
                                      : std::string{};

                Json tool_calls = Json::array();
                if (llm_response.contains("tool_calls") && llm_response["tool_calls"].is_array())
                {
                    tool_calls = llm_response["tool_calls"];
                }

                Json assistant_msg{};
                assistant_msg["role"] = "assistant";
                assistant_msg["content"] = content;
                if (!tool_calls.empty())
                {
                    assistant_msg["tool_calls"] = tool_calls;
                }
                messages.push_back(assistant_msg);

                if (!tool_calls.empty())
                {
                    for (const auto &tc : tool_calls)
                    {
                        std::string tool_name{};
                        std::string call_id{"call_0"};
                        if (tc.contains("id") && tc["id"].is_string())
                        {
                            call_id = tc["id"].get<std::string>();
                        }
                        if (tc.contains("function") && tc["function"].is_object() &&
                            tc["function"].contains("name") && tc["function"]["name"].is_string())
                        {
                            tool_name = tc["function"]["name"].get<std::string>();
                        }

                        std::string args_str = "{}" ;
                        if (tc.contains("function") && tc["function"].is_object() &&
                            tc["function"].contains("arguments") && tc["function"]["arguments"].is_string())
                        {
                            args_str = tc["function"]["arguments"].get<std::string>();
                        }

                        const std::string display_tool_name =
                            tool_name.empty() ? std::string{"<invalid tool call>"} : tool_name;
                        std::cout << '[' << id_ << "] Calling tool: "
                                  << display_tool_name << '\n'
                                  << "[tool: " << display_tool_name << "] Working...\n"
                                  << std::flush;

                        Json parsed_args = Json::parse(args_str, nullptr, false);
                        Json tool_res{};
                        if (parsed_args.is_discarded() || !parsed_args.is_object())
                        {
                            tool_res["error"] = "Tool arguments must be a valid JSON object.";
                            tool_res["is_error"] = true;
                        }
                        else if (tool_name.empty())
                        {
                            tool_res["error"] = "Tool call is missing a valid function name.";
                            tool_res["is_error"] = true;
                        }
                        else
                        {
                            tool_res = dispatch_tool(tool_name, parsed_args);
                        }
                        if (tool_res.contains("is_error") && tool_res["is_error"].is_boolean() &&
                            tool_res["is_error"].get<bool>())
                        {
                            const std::string error_message =
                                tool_res.contains("error") && tool_res["error"].is_string()
                                    ? tool_res["error"].get<std::string>()
                                    : "Tool call failed.";
                            std::cerr << "[tool: " << display_tool_name
                                      << "] Failed: " << error_message << '\n';
                        }
                        else
                        {
                            std::cout << "[tool: " << display_tool_name << "] Done.\n"
                                      << std::flush;
                        }

                        std::string obs = tool_res.contains("result") && tool_res["result"].is_string()
                            ? tool_res["result"].get<std::string>()
                            : tool_res.dump();
                        if (obs.size() > config_.max_observation_length)
                        {
                            constexpr std::string_view truncation_marker{"...[truncated]"};
                            const std::size_t limit = config_.max_observation_length;
                            if (limit >= truncation_marker.size())
                            {
                                obs.resize(limit - truncation_marker.size());
                                obs.append(truncation_marker);
                            }
                            else
                            {
                                obs.resize(limit);
                            }
                        }

                        Json tool_msg{};
                        tool_msg["role"] = "tool";
                        tool_msg["tool_call_id"] = call_id;
                        tool_msg["content"] = obs;
                        messages.push_back(tool_msg);
                    }
                }
                else
                {
                    std::cout << '[' << id_ << "] Response ready.\n" << std::flush;
                    final_result = content;
                    break;
                }
            }

            if (final_result.empty())
            {
                final_result = "(Agent reached max cycles without final answer.)";
            }

            return final_result;
        }

        std::string chat(std::string_view user_prompt)
        {
            if (!user_prompt.empty())
            {
                Json user_msg{};
                user_msg["role"] = "user";
                user_msg["content"] = std::string{user_prompt};
                history_.push_back(user_msg);
            }

            Json messages = Json::array();
            Json sys_msg{};
            sys_msg["role"] = "system";
            sys_msg["content"] = config_.system_prompt.empty() ? "You are a helpful assistant." : config_.system_prompt;
            messages.push_back(sys_msg);

            for (const auto &msg : history_)
            {
                messages.push_back(msg);
            }

            std::size_t history_size_before = history_.size();
            std::string final_text = reason_act_loop(messages, config_.max_cycles);

            for (std::size_t i = history_size_before + 1; i < messages.size(); ++i)
            {
                history_.push_back(messages[i]);
            }

            return final_text;
        }
    };
}