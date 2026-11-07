// ============================================================================
//  src/sauron.cpp — SAURON multi-turn agent runtime
// ============================================================================

// ============================================================================
//  src/sauron.cpp — SAURON interactive agent runtime
//  Project: SAURON (Specialized Autonomous Units for Reasoning,
//           Orchestration, and Navigation)
//  License: Apache 2.0
//
//  Build:
//    clang++ -std=c++20 -O3 -pthread -I include src/sauron.cpp -o bin/sauron
//    g++     -std=c++20 -O3 -pthread -I include src/sauron.cpp -o bin/sauron
//    For HTTPS providers, add -DCPPHTTPLIB_OPENSSL_SUPPORT -lssl -lcrypto.
//
//  Run (from the project root):
//    ./bin/sauron --provider openai --model gpt-6-luna
//    ./bin/sauron --help
// ============================================================================

#include "pooriayousefi/sauron/agent.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
    void print_usage(std::ostream& output, std::string_view executable)
    {
        output << "Usage: " << executable
               << " --provider NAME --model NAME\n"
                  "\n"
                  "Options:\n"
                  "  --provider NAME    Provider name from ai_providers.json (required)\n"
                  "  --model NAME       Model name configured for that provider (required)\n"
                  "  --help             Show this help\n"
                  "\n"
                  "Startup:\n"
                  "  Configuration is read from cfg/ai_providers.json,\n"
                  "  cfg/system_prompts.json, and cfg/mcp_servers.json.\n"
                  "  SAURON loads the selected provider/model from the provider config.\n"
                  "  If that model has a command, SAURON starts it and waits for its endpoint.\n"
                  "  SAURON then starts/connects enabled MCP servers from the MCP config.\n"
                  "  The orchestrator prompt is loaded automatically; the chat starts\n"
                  "  only after the LLM and all enabled MCP servers are ready.\n"
                  "\n"
                  "Chat commands:\n"
                  "  /help            Show this help without querying the model\n"
                  "  /exit, /quit     End the chat\n";
    }
}

int main(int argc, char* argv[])
{
    using namespace pooriayousefi;

    const std::string agent_name{"orchestrator"};
    std::string provider_name{};
    std::string model_name{};
    constexpr std::string_view provider_config{"cfg/ai_providers.json"};
    constexpr std::string_view prompt_config{"cfg/system_prompts.json"};
    constexpr std::string_view mcp_config{"cfg/mcp_servers.json"};

    for (int index = 1; index < argc; ++index)
    {
        const std::string_view option{argv[index]};
        if (option == "--help" || option == "-h")
        {
            print_usage(std::cout, argv[0]);
            return 0;
        }
        if (index + 1 >= argc)
        {
            std::cerr << "Missing value for " << option << ".\n";
            print_usage(std::cerr, argv[0]);
            return 2;
        }

        const std::string value{argv[++index]};
        if (option == "--provider")
        {
            provider_name = value;
        }
        else if (option == "--model")
        {
            model_name = value;
        }
        else
        {
            std::cerr << "Unknown option: " << option << '\n';
            print_usage(std::cerr, argv[0]);
            return 2;
        }
    }

    if (provider_name.empty() || model_name.empty())
    {
        std::cerr << "Both --provider and --model are required.\n";
        print_usage(std::cerr, argv[0]);
        return 2;
    }

    const auto provider = llm::load_llm_provider_config(
        provider_config,
        provider_name,
        model_name
    );
    if (!provider.has_value())
    {
        std::cerr << "Failed to load provider: " << provider.get_error() << '\n';
        return 1;
    }

    const auto prompt = llm::load_system_prompt(prompt_config, agent_name);
    if (!prompt.has_value())
    {
        std::cerr << "Failed to load system prompt: " << prompt.get_error() << '\n';
        return 1;
    }

    sauron::Agent::Config config{};
    config.llm = provider.get_value();
    config.id = agent_name;
    config.system_prompt = prompt.get_value();
    config.mcp_config_file = mcp_config;

    try
    {
        std::cout << "Starting orchestrator with provider '" << provider_name
                  << "' and model '" << model_name << "'.\n";
        sauron::Agent agent{std::move(config)};
        const auto setup = agent.setup();
        if (!setup.has_value())
        {
            std::cerr << "Agent setup failed: " << setup.get_error() << '\n';
            return 1;
        }

        std::cout << agent_name << " is ready. Enter /exit to finish.\n";
        std::string user_input{};
        while (std::cout << "> " && std::getline(std::cin, user_input))
        {
            if (user_input == "/help")
            {
                print_usage(std::cout, argv[0]);
                continue;
            }
            if (user_input == "/exit" || user_input == "/quit")
            {
                break;
            }
            if (user_input.empty())
            {
                continue;
            }
            std::cout << agent.chat(user_input) << '\n';
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "Agent failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
