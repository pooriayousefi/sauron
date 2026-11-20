// ============================================================================
//  Module: include/pooriayousefi/utility/common.hpp
//  Purpose: Defines common SAURON utility aliases and shared error codes.
//  Build: Header-only; build a consuming executable, e.g. clang++ -std=c++20 -O3 -pthread -I include src/sauron.cpp -o bin/sauron.
//  Run: Run the consuming executable from the repository root after configuring cfg/ai_providers.json and cfg/mcp_servers.json.
// ============================================================================

// ============================================================================
//  pooriayousefi/utility/common.hpp — Common utility functions and types
//  Project: SAURON (Specialized Autonomous Units for Reasoning, Orchestration, and Navigation)
//  Developed by: Pooria Yousefi
//  License: Apache 2.0
// ============================================================================

#pragma once

#include <string>
#include "nlohmann/json.hpp"

namespace pooriayousefi
{
    using Json = nlohmann::json;
    using Url = std::string;
    using Id = std::string;
}