// ============================================================================
//  Module: include/pooriayousefi/sauron/llm.hpp
//  Purpose: Provider configuration, endpoint validation, chat-completions
//           requests, and response/tool-call normalization.
//  Build: Header-only; compile an executable including this header, e.g.:
//    clang++ -std=c++20 -O3 -pthread -I include src/sauron.cpp -o bin/sauron
//  Run: Start the selected model server independently, configure
//       cfg/ai_providers.json, then run from the repository root:
//    ./bin/sauron --provider local --model gpt-oss-20b
//  Note: This module never launches, supervises, or terminates model servers.
// ============================================================================

#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "nlohmann/json.hpp"
#include "cpp-httplib/httplib.h"
#include "pooriayousefi/utility/common.hpp"
#include "pooriayousefi/utility/expected.hpp"

namespace pooriayousefi::llm
{
    struct ServerConfig
    {
        std::string name{};
        Url url{};
        bool enabled{false};
        bool stdio_enabled{false};
        bool http_enabled{false};
        std::string command{};
        std::string host{"127.0.0.1"};
        int port{0};
    };

    struct ParsedURL
    {
        std::string host{};
        std::string path{"/"};
        int port{80};
    };

    [[nodiscard]] inline ParsedURL parse_url(std::string_view url)
    {
        ParsedURL result{};

        std::string_view rest = url;
        if (rest.starts_with("http://"))
        {
            rest.remove_prefix(7);
        }
        else if (rest.starts_with("https://"))
        {
            rest.remove_prefix(8);
        }

        auto path_pos = rest.find('/');
        if (path_pos != std::string_view::npos)
        {
            result.path = std::string{rest.substr(path_pos)};
            rest = rest.substr(0, path_pos);
        }

        auto port_pos = rest.find(':');
        if (port_pos != std::string_view::npos)
        {
            result.host = std::string{rest.substr(0, port_pos)};
            std::string_view port_sv = rest.substr(port_pos + 1);
            int port_val{0};
            for (char c : port_sv)
            {
                if (c >= '0' && c <= '9')
                {
                    port_val = port_val * 10 + (c - '0');
                }
            }
            result.port = port_val;
        }
        else
        {
            result.host = std::string{rest};
        }

        return result;
    }

    [[nodiscard]] inline Expected<std::vector<ServerConfig>, std::string>
    parse_server_config(std::string_view filename)
    {
        Expected<std::vector<ServerConfig>, std::string> result{};

        std::ifstream file{std::string{filename}};
        if (!file.is_open())
        {
            result.set_error(std::string{"Cannot open config file: "} + std::string{filename});
        }
        else
        {
            std::string content{
                std::istreambuf_iterator<char>{file},
                std::istreambuf_iterator<char>{}
            };

            Json root = Json::parse(content, nullptr, false);
            if (root.is_discarded())
            {
                result.set_error(std::string{"Failed to parse config Json."});
            }
            else if (!root.contains("mcp_servers") || !root["mcp_servers"].is_array())
            {
                result.set_error(std::string{"Config missing 'mcp_servers' array."});
            }
            else
            {
                std::vector<ServerConfig> servers{};
                for (const auto &server_json : root["mcp_servers"])
                {
                    if (!server_json.is_object() || !server_json.contains("name") ||
                        !server_json["name"].is_string() || !server_json.contains("transports") ||
                        !server_json["transports"].is_object())
                    {
                        result.set_error(std::string{"Each MCP server needs a name and transports object."});
                        return result;
                    }

                    ServerConfig config{};
                    config.name = server_json["name"].get<std::string>();
                    const Json& transports = server_json["transports"];
                    for (const std::string_view transport_name : {"stdio", "http"})
                    {
                        if (!transports.contains(std::string{transport_name}) ||
                            !transports[std::string{transport_name}].is_object() ||
                            !transports[std::string{transport_name}].contains("enable") ||
                            !transports[std::string{transport_name}]["enable"].is_boolean())
                        {
                            result.set_error("MCP server '" + config.name +
                                             "' needs a boolean enable switch for " +
                                             std::string{transport_name} + ".");
                            return result;
                        }
                    }

                    const Json& stdio = transports["stdio"];
                    const Json& http = transports["http"];
                    config.stdio_enabled = stdio["enable"].get<bool>();
                    config.http_enabled = http["enable"].get<bool>();
                    config.enabled = config.stdio_enabled || config.http_enabled;
                    if (config.stdio_enabled)
                    {
                        if (!stdio.contains("command") || !stdio["command"].is_string() ||
                            stdio["command"].get<std::string>().empty())
                        {
                            result.set_error("Enabled stdio transport for '" + config.name +
                                             "' needs a command.");
                            return result;
                        }
                        config.command = stdio["command"].get<std::string>();
                        if (stdio.contains("args"))
                        {
                            if (!stdio["args"].is_array())
                            {
                                result.set_error("stdio args for '" + config.name + "' must be an array.");
                                return result;
                            }
                            for (const Json& arg : stdio["args"])
                            {
                                if (!arg.is_string())
                                {
                                    result.set_error("stdio args for '" + config.name +
                                                     "' must contain only strings.");
                                    return result;
                                }
                                config.args.push_back(arg.get<std::string>());
                            }
                        }
                        if (stdio.contains("env"))
                        {
                            if (!stdio["env"].is_object())
                            {
                                result.set_error("stdio env for '" + config.name + "' must be an object.");
                                return result;
                            }
                            for (auto it = stdio["env"].begin(); it != stdio["env"].end(); ++it)
                            {
                                if (!it.value().is_string())
                                {
                                    result.set_error("stdio env values for '" + config.name +
                                                     "' must be strings.");
                                    return result;
                                }
                                config.env.emplace(it.key(), it.value().get<std::string>());
                            }
                        }
                    }
                    if (config.http_enabled)
                    {
                        if (!http.contains("host") || !http["host"].is_string() ||
                            !http.contains("port") || !http["port"].is_number_integer())
                        {
                            result.set_error("Enabled HTTP transport for '" + config.name +
                                             "' needs a host and integer port.");
                            return result;
                        }
                        config.host = http["host"].get<std::string>();
                        config.port = http["port"].get<int>();
                        if (config.port < 1 || config.port > 65535)
                        {
                            result.set_error("HTTP port for '" + config.name +
                                             "' must be between 1 and 65535.");
                            return result;
                        }
                        config.url = "http://" + config.host + ":" +
                                     std::to_string(config.port) + "/mcp";
                    }
                    servers.push_back(std::move(config));
                }
                result.set_value(std::move(servers));
            }
        }

        return result;
    }

    struct ProviderConfig
    {
        std::string name{}, type{"openai-compatible"};
        std::string base_url{}, model_name{}, api_key{}, api_key_env{};
        double temperature{0.7};
        int max_tokens{4096};
    };

    struct BaseURLParts
    {
        std::string scheme_host_port{};
        std::string path_prefix{};
    };

    [[nodiscard]] inline BaseURLParts split_base_url(const std::string &base_url)
    {
        BaseURLParts result{};

        std::string_view rest{base_url};
        std::string_view scheme{"http"};

        if (rest.starts_with("https://"))
        {
            scheme = "https";
            rest.remove_prefix(8);
        }
        else if (rest.starts_with("http://"))
        {
            scheme = "http";
            rest.remove_prefix(7);
        }

        std::string_view authority{rest};
        const auto path_pos = rest.find('/');
        if (path_pos != std::string_view::npos)
        {
            authority = rest.substr(0, path_pos);
            result.path_prefix = std::string{rest.substr(path_pos)};
        }

        if (!authority.empty())
        {
            result.scheme_host_port = std::string{scheme} + "://" + std::string{authority};
            const bool has_port = authority.find(':') != std::string_view::npos;
            if (!has_port)
            {
                result.scheme_host_port += (scheme == "https") ? ":443" : ":80";
            }
        }

        return result;
    }

    [[nodiscard]] inline std::string make_endpoint_path(
        const std::string &path_prefix,
        const std::string &endpoint
    )
    {
        std::string result{path_prefix};
        while (!result.empty() && result.back() == '/')
        {
            result.pop_back();
        }
        result += endpoint;
        return result;
    }

    [[nodiscard]] inline Expected<ProviderConfig, std::string>
    load_llm_provider_config(
        std::string_view filename,
        std::string_view provider_name,
        std::string_view model_name
    )
    {
        Expected<ProviderConfig, std::string> result{};

        std::ifstream file{std::string{filename}};
        if (!file.is_open())
        {
            result.set_error(std::string{"Cannot open LLM provider config file: "} + std::string{filename});
        }
        else
        {
            std::string content{
                std::istreambuf_iterator<char>{file},
                std::istreambuf_iterator<char>{}
            };

            const Json root = Json::parse(content, nullptr, false);
            if (root.is_discarded() || !root.is_object())
            {
                result.set_error(std::string{"Failed to parse provider config Json: "} + std::string{filename});
            }
            else if (!root.contains("providers") || !root["providers"].is_array())
            {
                result.set_error(std::string{"Provider config needs a 'providers' array."});
            }
            else
            {
                const Json* selected{nullptr};
                for (const Json& provider : root["providers"])
                {
                    if (!provider.is_object() || !provider.contains("name") ||
                        !provider["name"].is_string())
                    {
                        result.set_error("Every provider needs a string 'name' field.");
                        return result;
                    }
                    if (provider["name"].get<std::string>() == provider_name)
                    {
                        if (selected != nullptr)
                        {
                            result.set_error("Provider names must be unique: '" +
                                             std::string{provider_name} + "'.");
                            return result;
                        }
                        selected = &provider;
                    }
                }

                if (selected == nullptr)
                {
                    result.set_error("AI provider '" + std::string{provider_name} +
                                     "' was not found in the provider config.");
                    return result;
                }

                if (!selected->contains("models") || !(*selected)["models"].is_array())
                {
                    result.set_error("Provider '" + std::string{provider_name} +
                                     "' needs a 'models' array.");
                    return result;
                }

                const Json* selected_model{nullptr};
                for (const Json& model : (*selected)["models"])
                {
                    if (!model.is_object() || !model.contains("model-name") ||
                        !model["model-name"].is_string())
                    {
                        result.set_error("Every model for provider '" +
                                         std::string{provider_name} +
                                         "' needs a string 'model-name' field.");
                        return result;
                    }
                    if (model["model-name"].get<std::string>() == model_name)
                    {
                        if (selected_model != nullptr)
                        {
                            result.set_error("Model names must be unique for provider '" +
                                             std::string{provider_name} + "': '" +
                                             std::string{model_name} + "'.");
                            return result;
                        }
                        selected_model = &model;
                    }
                }
                if (selected_model == nullptr)
                {
                    result.set_error("Model '" + std::string{model_name} +
                                     "' was not found for provider '" +
                                     std::string{provider_name} + "'.");
                    return result;
                }
                if (!selected_model->contains("base-url") ||
                    !(*selected_model)["base-url"].is_string())
                {
                    result.set_error("Model '" + std::string{model_name} +
                                     "' for provider '" + std::string{provider_name} +
                                     "' needs a 'base-url' string.");
                    return result;
                }

                ProviderConfig config{};
                config.base_url = (*selected_model)["base-url"].get<std::string>();
                config.model_name = std::string{model_name};
#if !defined(CPPHTTPLIB_SSL_ENABLED)
                if (config.base_url.starts_with("https://"))
                {
                    result.set_error(
                        "HTTPS providers require cpp-httplib SSL support "
                        "(CPPHTTPLIB_OPENSSL_SUPPORT and OpenSSL development libraries).");
                    return result;
                }
#endif
                if (selected->contains("name") && (*selected)["name"].is_string())
                {
                    config.name = (*selected)["name"].get<std::string>();
                }
                if (selected->contains("type") && (*selected)["type"].is_string())
                {
                    config.type = (*selected)["type"].get<std::string>();
                }
                if (config.type != "openai-compatible")
                {
                    result.set_error("Unsupported AI provider type '" + config.type +
                                     "'; this client supports openai-compatible endpoints.");
                    return result;
                }
                if (selected_model->contains("temp") && (*selected_model)["temp"].is_number())
                {
                    config.temperature = (*selected_model)["temp"].get<double>();
                }
                if (selected_model->contains("max-tokens") &&
                    (*selected_model)["max-tokens"].is_number_integer())
                {
                    config.max_tokens = (*selected_model)["max-tokens"].get<int>();
                }
                const Json* api_key_env = nullptr;
                if (selected_model->contains("api-key-env"))
                {
                    api_key_env = &(*selected_model)["api-key-env"];
                }
                else if (selected->contains("api-key-env"))
                {
                    api_key_env = &(*selected)["api-key-env"];
                }
                if (api_key_env != nullptr)
                {
                    if (!api_key_env->is_string())
                    {
                        result.set_error("Provider api-key-env must name an environment variable.");
                        return result;
                    }
                    config.api_key_env = api_key_env->get<std::string>();
                    const char* api_key = std::getenv(config.api_key_env.c_str());
                    if (api_key == nullptr || *api_key == '\0')
                    {
                        result.set_error("Required provider environment variable '" +
                                         config.api_key_env + "' is not set.");
                        return result;
                    }
                    config.api_key = api_key;
                }
                result.set_value(std::move(config));
            }
        }

        return result;
    }

    [[nodiscard]] inline Expected<std::string, std::string>
    load_system_prompt(std::string_view filename, std::string_view agent_name)
    {
        Expected<std::string, std::string> result{};

        std::ifstream file{std::string{filename}, std::ios::binary};
        if (!file.is_open())
        {
            result.set_error(std::string{"Cannot open system prompt file: "} + std::string{filename});
        }
        else
        {
            std::string content{
                std::istreambuf_iterator<char>{file},
                std::istreambuf_iterator<char>{}
            };
            const Json prompts = Json::parse(content, nullptr, false);
            if (prompts.is_discarded() || !prompts.is_object())
            {
                result.set_error("System prompt file must contain a JSON object.");
            }
            else if (!prompts.contains(std::string{agent_name}) ||
                     !prompts[std::string{agent_name}].is_string())
            {
                result.set_error("No string system prompt found for agent '" +
                                 std::string{agent_name} + "'.");
            }
            else
            {
                result.set_value(prompts[std::string{agent_name}].get<std::string>());
            }
        }

        return result;
    }

    class Client
    {
    private:
        ProviderConfig config_{};
        std::string chat_completions_path_{};
        std::string models_path_{};
        std::unique_ptr<httplib::Client> client_{nullptr};
        std::mutex mutex_{};

        [[nodiscard]] static Json error(std::string message)
        {
            Json j{};
            j["error"] = std::move(message);
            j["is_error"] = true;
            return j;
        }

        [[nodiscard]] std::string invalid_base_url_message() const
        {
            std::string result{};
            result = "LLM base Url '" + config_.base_url + "' is invalid.";
            return result;
        }

    public:
        explicit Client(ProviderConfig config)
            : config_{std::move(config)}
            , chat_completions_path_{}
            , models_path_{}
            , client_{nullptr}
            , mutex_{}
        {
            const BaseURLParts parts = split_base_url(config_.base_url);
            chat_completions_path_ = make_endpoint_path(parts.path_prefix, "/chat/completions");
            models_path_ = make_endpoint_path(parts.path_prefix, "/models");

            if (!parts.scheme_host_port.empty())
            {
                client_ = std::make_unique<httplib::Client>(parts.scheme_host_port);
                client_->set_connection_timeout(std::chrono::seconds(10));
                client_->set_read_timeout(std::chrono::seconds(300));
                client_->set_write_timeout(std::chrono::seconds(60));
            }

        }

        Client() = delete;
        Client(const Client &) = delete;
        Client &operator=(const Client &) = delete;
        Client(Client &&) = delete;
        Client &operator=(Client &&) = delete;
        [[nodiscard]] Expected<void, std::string> verify_connection()
        {
            Expected<void, std::string> result{};

            if (config_.base_url.empty())
            {
                result.set_error("LLM base Url is empty.");
                return result;
            }
            if (!client_)
            {
                result.set_error(invalid_base_url_message());
                return result;
            }

            std::optional<int> response_status{};
            {
                std::lock_guard<std::mutex> lock(mutex_);
                httplib::Headers headers{};
                if (!config_.api_key.empty())
                {
                    headers.emplace("Authorization", "Bearer " + config_.api_key);
                }
                const auto response = client_->Get(models_path_, headers);
                if (response)
                {
                    response_status = response->status;
                }
            }

            if (response_status && *response_status >= 200 && *response_status < 300)
            {
                result.set_value();
            }
            else if (response_status && (*response_status == 401 || *response_status == 403))
            {
                result.set_error("LLM endpoint rejected API key.");
            }
            else if (!response_status)
            {
                result.set_error("Cannot reach LLM endpoint '" + config_.base_url +
                                 "'. Start the model server independently and verify its host, port, and API route.");
            }
            else
            {
                result.set_error("LLM endpoint returned HTTP status " +
                                 std::to_string(*response_status) + ".");
            }
            return result;
        }

        [[nodiscard]] Json reasoning(const Json &messages, const Json &tools_schema = Json::array())
        {
            Json result{};

            if (config_.model_name.empty())
            {
                result = error("LLM model name is empty.");
            }
            else if (config_.base_url.empty())
            {
                result = error("LLM base Url is empty.");
            }
            else if (!client_)
            {
                result = error(invalid_base_url_message());
            }
            else
            {
                std::lock_guard<std::mutex> lock(mutex_);

                Json request{
                    {"model", config_.model_name},
                    {"messages", messages},
                    {"stream", false},
                    {"max_tokens", config_.max_tokens},
                    {"temperature", config_.temperature}
                };

                if (tools_schema.is_array() && !tools_schema.empty())
                {
                    request["tools"] = tools_schema;
                    request["tool_choice"] = "auto";
                }

                httplib::Headers headers{};
                if (!config_.api_key.empty())
                {
                    headers.emplace("Authorization", "Bearer " + config_.api_key);
                }

                const auto response = client_->Post(
                    chat_completions_path_,
                    headers,
                    request.dump(),
                    "application/json"
                );

                if (!response)
                {
                    result = error("Failed to connect to LLM endpoint.");
                }
                else
                {
                    const Json body = Json::parse(response->body, nullptr, false);
                    if (response->status < 200 || response->status >= 300)
                    {
                        result = error("LLM endpoint returned HTTP error.");
                    }
                    else if (body.is_discarded())
                    {
                        result = error("LLM returned invalid Json.");
                    }
                    else
                    {
                        if (!body.contains("choices") || !body["choices"].is_array() || body["choices"].empty())
                        {
                            result = error("LLM response has no choices.");
                        }
                        else
                        {
                            const Json &choice = body["choices"][0];
                            const Json &message = choice["message"];

                            result["role"] = "assistant";
                            result["content"] = message.contains("content") && message["content"].is_string()
                                                ? message["content"]
                                                : Json{""};

                            if (message.contains("tool_calls") && message["tool_calls"].is_array())
                            {
                                Json calls = Json::array();
                                for (std::size_t i = 0; i < message["tool_calls"].size(); ++i)
                                {
                                    const Json &call = message["tool_calls"][i];
                                    if (!call.contains("function") || !call["function"].is_object())
                                    {
                                        continue;
                                    }
                                    const Json &fn = call["function"];
                                    if (!fn.contains("name") || !fn["name"].is_string())
                                    {
                                        continue;
                                    }

                                    std::string call_id = "call_" + std::to_string(i);
                                    if (call.contains("id") && call["id"].is_string())
                                    {
                                        call_id = call["id"].get<std::string>();
                                    }

                                    Json normalized{
                                        {"id", call_id},
                                        {"type", "function"},
                                        {"function", {
                                            {"name", fn["name"]},
                                            {"arguments", fn.contains("arguments") && fn["arguments"].is_string()
                                                          ? fn["arguments"]
                                                          : Json{"{}"}}
                                        }}
                                    };
                                    calls.push_back(std::move(normalized));
                                }
                                if (!calls.empty())
                                {
                                    result["tool_calls"] = std::move(calls);
                                }
                            }
                        }
                    }
                }
            }

            return result;
        }
    };
}