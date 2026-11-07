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