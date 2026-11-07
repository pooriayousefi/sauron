// ============================================================================
//  src/test/test_sauron.cpp — SAURON Agent Executable Test Harness
//  Project: SAURON (Specialized Autonomous Units for Reasoning, Orchestration, and Navigation)
//  Developed by: Pooria Yousefi
//  License: Apache 2.0
//
//  Build:
//    clang++ -std=c++20 -O3 -pthread -I include src/test/test_sauron.cpp -o bin/test_sauron_agent
//    g++     -std=c++20 -O3 -pthread -I include src/test/test_sauron.cpp -o bin/test_sauron_agent
//    For HTTPS providers, add -DCPPHTTPLIB_OPENSSL_SUPPORT -lssl -lcrypto.
//
//  Run:
//    ./bin/test_sauron_agent --provider NAME --model NAME
// ============================================================================

#include <iostream>
#include "pooriayousefi/sauron/llm.hpp"
#include "pooriayousefi/sauron/agent.hpp"

using namespace pooriayousefi;

int main(int argc, char* argv[])
{
    int exit_code{0};
    std::string provider_name{};
    std::string model_name{};
    for (int index = 1; index + 1 < argc; ++index)
    {
        const std::string option{argv[index]};
        if (option == "--provider")
        {
            provider_name = argv[++index];
        }
        else if (option == "--model")
        {
            model_name = argv[++index];
        }
    }
    if (provider_name.empty() || model_name.empty())
    {
        std::cerr << "Usage: " << argv[0] << " --provider NAME --model NAME\n";
        return 2;
    }

    std::cout << "[SAURON] Initializing Agent Runtime...\n";

    auto llm_config_res =
        llm::load_llm_provider_config("cfg/ai_providers.json", provider_name, model_name);
    if (!llm_config_res.has_value())
    {
        std::cerr << "[SAURON] Error loading LLM config: " << llm_config_res.get_error() << "\n";
        return 1;
    }

    sauron::Agent::Config agent_config{};
    agent_config.llm = llm_config_res.get_value();
    agent_config.id = "orchestrator";
    auto prompt_res = llm::load_system_prompt("cfg/system_prompts.json", "orchestrator");
    if (!prompt_res.has_value())
    {
        std::cerr << "[SAURON] Error loading system prompt: " << prompt_res.get_error() << "\n";
        return 1;
    }
    agent_config.system_prompt = prompt_res.get_value();
    agent_config.mcp_config_file = "cfg/mcp_servers.json";

    sauron::Agent agent{agent_config};
    auto setup_res = agent.setup();
    if (!setup_res.has_value())
    {
        std::cerr << "[SAURON] Setup failed: " << setup_res.get_error() << "\n";
        return 1;
    }

    std::cout << "[SAURON] Setup successful. Running test query...\n";
    std::string response = agent.chat("List the contents of the current directory.");
    std::cout << "\n[Agent Response]:\n" << response << "\n";

    return exit_code;
}