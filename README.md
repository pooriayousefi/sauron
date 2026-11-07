# SAURON

**Specialized Autonomous Units for Reasoning, Orchestration, and Navigation**

SAURON is a C++20 single-agent runtime that connects an OpenAI-compatible chat
model to locally configured Model Context Protocol (MCP) tool servers. The
orchestrator discovers tools at startup, presents them to the model as function
tools, dispatches requested calls, and returns tool observations to the model
until it can answer the user.

This repository describes the runtime as it exists today: one interactive
`orchestrator` agent, one configured LLM at a time, and an optional fleet of
standalone MCP servers.

## What is included

- `src/sauron.cpp`: interactive SAURON executable.
- `include/pooriayousefi/sauron/`: agent orchestration and OpenAI-compatible
  LLM client.
- `include/pooriayousefi/mcp/`: MCP JSON-RPC protocol, clients, servers,
  Streamable HTTP/stdio transports, and an OpenAI tool bridge.
- `include/pooriayousefi/process/`: cross-platform child-process and IPC
  helpers.
- `src/server/`: nine runnable MCP tool servers.
- `src/test/`: small executable demos for the MCP client/server and the agent.
- `cfg/`: provider, MCP-server, and system-prompt configuration.
- `include/nlohmann/json.hpp` and `include/cpp-httplib/httplib.h`: vendored
  header dependencies.

There is no CMake or Make build system yet. Build commands below compile each
executable directly.

## Requirements

- A C++20 compiler: Apple Clang/Clang or GCC. The commands below use
  `clang++`; replace it with `g++` if preferred.
- POSIX threads (`-pthread`).
- For Windows builds, link Winsock with `-lws2_32`.
- For HTTPS LLM providers, build cpp-httplib with SSL support and link OpenSSL:
  `-DCPPHTTPLIB_OPENSSL_SUPPORT -lssl -lcrypto` (and the appropriate OpenSSL
  include/library paths for your system).
- An OpenAI-compatible chat-completions endpoint. This may be a hosted API or
  a local server such as `llama-server`.

Build and run commands that refer to `cfg/` or `bin/` assume the working
directory is the repository root.

## Build every executable

From the repository root:

```sh
mkdir -p bin

clang++ -std=c++20 -O3 -pthread -I include src/sauron.cpp -o bin/sauron

clang++ -std=c++20 -O3 -pthread -I include src/server/directory_mcp_tool_server.cpp -o bin/directory_mcp_tool_server
clang++ -std=c++20 -O3 -pthread -I include src/server/file_mcp_tool_server.cpp -o bin/file_mcp_tool_server
clang++ -std=c++20 -O3 -pthread -I include src/server/git_mcp_tool_server.cpp -o bin/git_mcp_tool_server
clang++ -std=c++20 -O3 -pthread -I include src/server/memory_mcp_tool_server.cpp -o bin/memory_mcp_tool_server
clang++ -std=c++20 -O3 -pthread -I include src/server/random_number_mcp_tool_server.cpp -o bin/random_number_mcp_tool_server
clang++ -std=c++20 -O3 -pthread -I include src/server/shell_mcp_tool_server.cpp -o bin/shell_mcp_tool_server
clang++ -std=c++20 -O3 -pthread -I include src/server/statistics_mcp_tool_server.cpp -o bin/statistics_mcp_tool_server
clang++ -std=c++20 -O3 -pthread -I include src/server/time_mcp_tool_server.cpp -o bin/time_mcp_tool_server
clang++ -std=c++20 -O3 -pthread -I include src/server/utility_mcp_tool_server.cpp -o bin/utility_mcp_tool_server

clang++ -std=c++20 -O3 -pthread -I include src/test/test_mcp_client.cpp -o bin/test_mcp_client
clang++ -std=c++20 -O3 -pthread -I include src/test/test_mcp_server.cpp -o bin/test_mcp_server
clang++ -std=c++20 -O3 -pthread -I include src/test/test_sauron.cpp -o bin/test_sauron_agent
```

For HTTPS model providers, apply the OpenSSL flags above to the SAURON and
`test_sauron_agent` compile commands. On Windows, add `-lws2_32` to each
compile/link command.

## First run: configure a model

The main executable reads these paths relative to the project root:

| File | Purpose |
| --- | --- |
| `cfg/ai_providers.json` | Provider, model, base URL, optional API key and optional local-server launch command |
| `cfg/system_prompts.json` | The orchestrator's system prompt |
| `cfg/mcp_servers.json` | MCP server commands, environment variables, and transports |

The checked-in provider file contains an `openai` provider and a
`llama-server` provider. Select both the provider's `name` and one of its
models' `model-name` values on the command line.

### Hosted OpenAI-compatible API

1. Set the environment variable named by the provider's `api-key-env` entry.
   The provided `openai` example expects `OPENAI_API_KEY`.
2. Confirm that the model name in `cfg/ai_providers.json` is available to your
   API account. Change the sample `model-name` if needed.
3. Build with HTTPS support as described above, then start SAURON:

```sh
export OPENAI_API_KEY='<your-api-key>'
./bin/sauron --provider openai --model gpt-6-luna
```

The API key is read from the environment; do not put a real key in the JSON
file or commit one to the repository.

### Local OpenAI-compatible model server

The sample `llama-server` model entries include machine-specific paths for the
server executable and model files. Edit `command`, `args`, `env`,
`startup-timeout-seconds`, and `base-url` to match your local installation.
When `command` is configured, SAURON starts that process and waits for its
configured endpoint before connecting to MCP servers.

For example, after adapting one model's command, arguments, and model name:

```sh
./bin/sauron --provider llama-server --model gpt-oss-20b
```

Use the exact provider/model names from your JSON file. Use `--help` to display
the executable's options. Chat commands are `/help`, `/exit`, and `/quit`.

## Configure MCP tools

`cfg/mcp_servers.json` contains nine tool servers. Each server can use stdio,
Streamable HTTP, or both. The checked-in configuration enables stdio for all
nine servers and gives each a command under `bin/`.

- **stdio** is the normal mode for SAURON: the runtime launches the configured
  command as a child process. Commands such as `bin/file_mcp_tool_server` are
  relative to the working directory, so start SAURON from the repository root.
- **HTTP** is useful when running a server separately. Configure `host` and
  `port` in the server's `http` section and set `enable` to `true`. Servers
  accept `http HOST PORT`; their JSON-RPC endpoint is `/mcp`.
- If both transports are enabled, SAURON uses stdio to communicate and asks the
  launched process to serve HTTP as well.

To disable a server, set both its stdio and HTTP `enable` values to `false`.
At least one server must remain enabled for agent setup to succeed. Every
enabled server must start and respond to tool discovery; a failed enabled
server prevents startup.

### Available tool groups

| Server | Tools |
| --- | --- |
| `file` | `read_file`, `read_file_base64`, `get_file_info`, `hash_file`, `search_in_file`, `write_file`, `replace_in_file`, `truncate_file`, `move_file`, `delete_file` |
| `directory` | `create_directory`, `list_directory`, `iterate_directories_recursively`, `delete_directory`, `get_last_modified_file` |
| `shell` | `run_command`, `run_program` |
| `git` | `git_status`, `git_diff`, `git_log`, `git_show`, `git_branch`, `git_current_branch`, `git_remote`, `git_init`, `git_add`, `git_commit`, `git_checkout`, `git_create_branch`, `git_reset`, `git_stash`, `git_fetch`, `git_pull`, `git_push`, `git_clone` |
| `time` | `get_current_time`, `get_local_time`, `get_unix_timestamp` |
| `random-number` | Uniform, Bernoulli, binomial, geometric, negative-binomial, Poisson, exponential, gamma, Weibull, extreme-value, normal, log-normal, chi-squared, Cauchy, Fisher-F, and Student-t sampling tools |
| `statistics` | `calculate_statistics`, arithmetic operations, arithmetic/geometric/harmonic/generalized and other means, weighted means, variance and standard-deviation tools |
| `memory` | `recall_memory`, `list_keys`, `save_memory` |
| `utility` | String tokenization/formatting, JSON extraction/validation/pretty-printing, and UUID generation |

The detailed input schema for each operation is advertised by the server and
discovered by SAURON at startup.

## Run one MCP server directly

Standalone servers default to stdio mode. With no arguments they wait for
newline-delimited JSON-RPC requests on stdin; stdio is normally used when a
client launches the server. To run an HTTP server for manual testing, use the
corresponding port below:

| Executable | Default HTTP port |
| --- | ---: |
| `file_mcp_tool_server` | 8093 |
| `directory_mcp_tool_server` | 8094 |
| `shell_mcp_tool_server` | 8090 |
| `time_mcp_tool_server` | 8091 |
| `git_mcp_tool_server` | 8099 |
| `random_number_mcp_tool_server` | 8095 |
| `statistics_mcp_tool_server` | 8096 |
| `memory_mcp_tool_server` | 8097 |
| `utility_mcp_tool_server` | 8098 |

For example:

```sh
./bin/time_mcp_tool_server http 127.0.0.1 8091
```

HTTP servers bind to the specified host. Keep them on loopback unless you have
provided appropriate network controls; the HTTP transport is not an
authenticated remote-access boundary.

## Shell tool safety

The shell server's `run_command` and `run_program` tools are disabled by
default. Enabling them permits local command/program execution; the command
tool supports shell operators. A denylist is not a security sandbox.

If you deliberately choose to enable these tools, configure the environment
for the shell server's stdio child in `cfg/mcp_servers.json`:

```json
"env": {
  "SAURON_SHELL_ENABLED": "1"
}
```

Use this only in a separately sandboxed environment with an account and
filesystem appropriate for the commands the model may invoke. The file,
directory, and git servers also expose mutating operations. Disable any
servers or tools that are outside your intended trust boundary.

## Persistence

The memory tool server loads and saves its JSON key-value store in
`sauron_memory.json` in its working directory. The old `mcpxx_memory.json`
file is read as a fallback if the new file does not exist; subsequent writes
go to `sauron_memory.json`. Both files are excluded from Git.

## Smoke-test the MCP client/server demo

The demo server exposes a calculator and text summarizer over HTTP on
`127.0.0.1:8080/mcp`. Start it in one terminal and the client in another:

```sh
./bin/test_mcp_server
```

```sh
./bin/test_mcp_client
```

The client lists the demo tools, calls the calculator, and prints an
OpenAI-compatible tool schema/response. Stop the server with Ctrl+C.

`test_sauron_agent` is an executable integration demo: it accepts the same
`--provider NAME --model NAME` options, initializes the configured agent, and
sends a fixed directory-listing request. It requires valid provider settings
and working enabled MCP servers:

```sh
./bin/test_sauron_agent --provider openai --model gpt-6-luna
```

These are executable demos, not a standalone automated unit-test suite.

## Protocol and implementation notes

- MCP messages use JSON-RPC 2.0 and this runtime's configured protocol version
  `2026-07-28`, including the required protocol/client metadata on requests.
- The LLM client sends OpenAI-compatible chat-completions requests and reads
  function/tool calls from model responses.
- Tool calls are routed to the MCP client that advertised each tool. Duplicate
  tool names from later servers are skipped.
- Agent limits and observation size are configurable through
  `sauron::Agent::Config` in the C++ API; the CLI uses the defaults in
  `include/pooriayousefi/sauron/agent.hpp`.
- File, directory, Git, and shell tools act in the permissions and working
  directory of the SAURON process. Review their operations before enabling
  them for an untrusted workflow.
