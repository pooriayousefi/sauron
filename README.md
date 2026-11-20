<img width="508" height="337" alt="sauron_banner_animated" src="https://github.com/user-attachments/assets/feadd9a2-61e1-435f-ac09-1aef26e1df57" />

# SAURON

**Specialized Autonomous Units for Reasoning, Orchestration, and Navigation**

SAURON is a C++20, single-agent runtime that connects an OpenAI-compatible
chat-completions model to a configurable fleet of Model Context Protocol (MCP)
tool servers. Select one agent persona and one configured model at startup.
SAURON discovers the enabled tools, makes them available to the model, dispatches
tool calls, and returns their results to the model so it can continue or answer.

Personas are system prompts, not separate concurrently running agents. Every
selected persona uses the same configured MCP fleet and the same runtime.

## How SAURON works

```text
STARTUP (once per SAURON process)

  ./bin/sauron --agent NAME --provider NAME --model NAME
       |
       v
  Read cfg/system_prompts.json, cfg/ai_providers.json,
  and cfg/mcp_servers.json; select the requested persona and model.
       |
       v
  Agent setup
       +-- Verify connection to the already-running LLM endpoint.
       `-- Start/connect enabled MCP servers and discover their tools.

CHAT LOOP (repeats for each user message)

  User --message--> Agent --system prompt + history + tools--> LLM
                                                               |
                           +-----------------------------------+----------------+
                           |                                                    |
                    final response                                    one or more tool calls
                           |                                                    |
                           v                                                    v
                    Agent returns answer                         Agent dispatches each call
                           |                                     to its MCP tool server
                           v                                                    |
                          User                                                  v
                                                       Tool result/error --> Agent
                                                                               |
                                                 Add result to next LLM request
```

The LLM service is external to SAURON: SAURON connects to it but does not
start or manage it. When a tool call is made, SAURON dispatches it to the MCP
server that provided that tool. Its result or error is added to the
conversation, and SAURON sends the updated conversation to the LLM again.

At startup, SAURON loads the selected prompt and provider/model configuration,
checks the model endpoint, then connects to every enabled MCP server and
discovers its tools. If any enabled server fails to start or respond to
discovery, setup fails. During chat, the model can either return a final answer
or request one or more discovered tools. SAURON routes each call to the server
that advertised the tool and provides the result back to the model. The loop
ends when the model responds without a tool call or the configured cycle limit
is reached.

## Contents

- [SAURON](#sauron)
  - [How SAURON works](#how-sauron-works)
  - [Contents](#contents)
  - [Features and personas](#features-and-personas)
  - [What is included](#what-is-included)
  - [Requirements](#requirements)
  - [Build](#build)
  - [Configure providers and prompts](#configure-providers-and-prompts)
  - [LLM endpoint: local or remote](#llm-endpoint-local-or-remote)
  - [Run SAURON](#run-sauron)
    - [Hosted OpenAI-compatible API](#hosted-openai-compatible-api)
    - [Local OpenAI-compatible model server](#local-openai-compatible-model-server)
  - [Configure MCP tools](#configure-mcp-tools)
    - [Available tool groups](#available-tool-groups)
    - [Add an MCP server](#add-an-mcp-server)
  - [Run one MCP server directly](#run-one-mcp-server-directly)
  - [Security](#security)
    - [Shell tool execution](#shell-tool-execution)
    - [HTTP exposure](#http-exposure)
  - [Persistence and conversation lifetime](#persistence-and-conversation-lifetime)
  - [Examples and checks](#examples-and-checks)
    - [Smoke-test the MCP client/server demo](#smoke-test-the-mcp-clientserver-demo)
  - [Troubleshooting](#troubleshooting)
  - [Implementation notes](#implementation-notes)

## Features and personas

- **One active agent per process:** choose a prompt persona with `--agent`.
- **OpenAI-compatible model endpoint:** use a local or hosted chat-completions
  service that supports tool/function calling.
- **MCP tools:** discover tools at startup and route calls to the server that
  advertised them.
- **Interactive chat:** continue a conversation until `/exit` or `/quit`.
- **Configurable transports:** launch MCP servers over stdio or connect to
  Streamable HTTP servers.

The built-in prompt keys in `cfg/system_prompts.json` are:

| Agent key | Focus |
| --- | --- |
| `orchestrator` | General task completion and coordination of available tools |
| `cybersentinel` | Defensive security analysis and risk mitigation |
| `system-observer` | System inspection, diagnostics, and evidence-based reporting |
| `resume-specialist` | Resume and professional-profile review |
| `education-advisor` | Learning goals, education options, and study plans |
| `code-reviewer` | Evidence-based review of software changes |
| `qa-engineer` | Requirements-focused test design and validation |
| `project-planner` | Scope, sequencing, dependencies, risks, and completion criteria |
| `git-specialist` | Git workflows, repository state, and cautious execution of Git operations |

Use the exact JSON key as the value of `--agent`. The prompt guides the model's
behavior; available tools are determined by `cfg/mcp_servers.json`, not by the
persona.

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

## Requirements

- A C++20 compiler: Apple Clang/Clang or GCC. The commands below use
  `clang++`; replace it with `g++` if preferred.
- POSIX threads (`-pthread`).
- For Windows builds, link Winsock with `-lws2_32`.
- For HTTPS LLM providers, build cpp-httplib with SSL support and link OpenSSL:
  `-DCPPHTTPLIB_OPENSSL_SUPPORT -lssl -lcrypto` (and the appropriate OpenSSL
  include/library paths for your system).
- An already-running OpenAI-compatible chat-completions endpoint. SAURON never
  launches, supervises, or stops an inference server; the user owns its
  lifecycle.
- A model endpoint that supports OpenAI-compatible tool/function calls. A
  text-only completion endpoint is not sufficient for MCP tool use.

Build and run commands that refer to `cfg/` or `bin/` assume the working
directory is the repository root.

## Build

There is currently no CMake or Make build system. Compile from the repository
root. The following commands build the CLI, all nine MCP servers, and the
executable demos:

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

### macOS: HTTPS-enabled build

Install OpenSSL with Homebrew, then build SAURON from the repository root.
The Apple Security and CoreFoundation frameworks are needed by cpp-httplib's
system certificate loader:

```sh
brew install openssl@3
mkdir -p bin

clang++ -std=c++20 -O3 -pthread \
  -I include \
  -DCPPHTTPLIB_OPENSSL_SUPPORT \
  -I "$(brew --prefix openssl@3)/include" \
  src/sauron.cpp \
  -o bin/sauron \
  -L "$(brew --prefix openssl@3)/lib" \
  -Wl,-rpath,"$(brew --prefix openssl@3)/lib" \
  -lssl -lcrypto \
  -framework Security \
  -framework CoreFoundation
```

### Ubuntu: HTTPS-enabled build

Install Clang and the OpenSSL development headers/libraries:

```sh
sudo apt update
sudo apt install -y clang libssl-dev
mkdir -p bin

clang++ -std=c++20 -O3 -pthread \
  -I include \
  -DCPPHTTPLIB_OPENSSL_SUPPORT \
  src/sauron.cpp \
  -o bin/sauron \
  -lssl -lcrypto
```

### Windows: HTTPS-enabled build with MSYS2

Open an **MSYS2 UCRT64** terminal, install the compiler and OpenSSL package,
then build SAURON. The UCRT64 environment supplies the matching include and
library paths:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-openssl
mkdir -p bin

g++ -std=c++20 -O3 -pthread \
  -I include \
  -DCPPHTTPLIB_OPENSSL_SUPPORT \
  src/sauron.cpp \
  -o bin/sauron.exe \
  -lssl -lcrypto -lws2_32
```

The examples above build the SAURON executable. To run the checked-in
configuration, also build its nine MCP server executables. On macOS or Ubuntu,
use this loop (from the repository root):

```sh
for source in src/server/*_mcp_tool_server.cpp; do
  name=$(basename "$source" .cpp)
  clang++ -std=c++20 -O3 -pthread -I include "$source" -o "bin/$name"
done
```

In the MSYS2 UCRT64 terminal, build the same servers with the Windows socket
library and `.exe` suffix:

```sh
for source in src/server/*_mcp_tool_server.cpp; do
  name=$(basename "$source" .cpp)
  g++ -std=c++20 -O3 -pthread -I include "$source" -o "bin/$name.exe" -lws2_32
done
```

HTTPS support is needed for SAURON when its configured LLM endpoint uses HTTPS;
it is not needed for the local MCP stdio servers. To build the HTTPS-enabled
`test_sauron_agent` demo, use the same platform-specific flags and libraries,
replacing `src/sauron.cpp` and `bin/sauron` with
`src/test/test_sauron.cpp` and `bin/test_sauron_agent` (or `bin/test_sauron_agent.exe`
on Windows).

## Configure providers and prompts

The CLI reads these paths relative to the current working directory:

| File | Purpose |
| --- | --- |
| `cfg/ai_providers.json` | Provider, model, endpoint URL, and optional API-key environment variable |
| `cfg/system_prompts.json` | System prompts keyed by agent name |
| `cfg/mcp_servers.json` | MCP server commands, environment variables, and transports |

The checked-in provider file contains an `openai` provider and a `local`
OpenAI-compatible provider with example endpoints. Select the provider's
`name` and one of its models' `model-name` values on the command line. The
model endpoint must be running before SAURON starts.

Provider configuration has a top-level `providers` array. For example:

```json
{
  "providers": [
    {
      "name": "local",
      "type": "openai-compatible",
      "models": [
        {
          "model-name": "gpt-oss-20b",
          "base-url": "http://127.0.0.1:8080/v1",
          "temp": 0.7,
          "max-tokens": 4096
        }
      ]
    }
  ]
}
```

`api-key-env` is optional and names an environment variable that contains the
provider's API key; it may be set on the provider or model entry. Never put the
key itself in this file. `temp` and `max-tokens` are optional model settings.

Each top-level key in `cfg/system_prompts.json` is an agent name. Add a new
persona by adding a non-empty string value under a unique key, then select that
key with `--agent`. Prompt content steers the model but is not an authorization
boundary: the model can see every tool exposed by the enabled MCP servers.

## LLM endpoint: local or remote

> **Run the LLM independently of SAURON.** A local inference server or remote
> model service is a separate process/service that you provide and operate.
> Start it (or confirm the remote service is available) before launching
> SAURON. SAURON only connects to the configured OpenAI-compatible `base-url`;
> it does not download models, start or stop inference servers, or manage their
> lifecycle. Starting or exiting SAURON has no effect on the LLM service.

| Endpoint | You are responsible for | SAURON does |
| --- | --- | --- |
| Local | Start and keep the inference server running; configure its URL and model identifier. | Connects to the local URL and checks that it is reachable. |
| Remote | Provide an accessible API endpoint and credentials, if required. | Connects to the configured remote URL and uses the named API-key environment variable, if configured. |

## Run SAURON

The CLI syntax is:

```text
./bin/sauron [--agent NAME] --provider NAME --model NAME
```

`--provider` and `--model` are required. `--agent` is optional and defaults to
`orchestrator`. The agent value must exactly match a key in
`cfg/system_prompts.json`. Show CLI help with `./bin/sauron --help`.
On Windows, use `.\bin\sauron.exe` instead of `./bin/sauron`.

### Hosted OpenAI-compatible API

1. Set the environment variable named by the provider's `api-key-env` entry.
   The provided `openai` example expects `OPENAI_API_KEY`.
2. Confirm that the model name in `cfg/ai_providers.json` is available to your
   API account. Change the sample `model-name` if needed.
3. Build with HTTPS support as described above, then start SAURON:

```sh
export OPENAI_API_KEY='<your-api-key>'
./bin/sauron --agent orchestrator --provider openai --model gpt-6-luna
```

In Windows PowerShell, set the environment variable and run the `.exe`:

```powershell
$env:OPENAI_API_KEY = '<your-api-key>'
.\bin\sauron.exe --agent orchestrator --provider openai --model gpt-6-luna
```

The API key is read from the environment; do not put a real key in the JSON
file or commit one to the repository.

### Local OpenAI-compatible model server

Start the inference server yourself before launching SAURON. This can be
`llama-server`, vLLM, Ollama (with an OpenAI-compatible endpoint), or any
other compatible implementation. SAURON does not launch or stop these
processes and does not require a particular inference engine.

1. Start your chosen model server independently, using the model and runtime
   options appropriate for your machine.
2. Edit the selected model entry under the `local` provider in
   `cfg/ai_providers.json`. Set `base-url` to the server's OpenAI-compatible
   API base URL (including `/v1` when required), and `model-name`
   to the model identifier accepted by that endpoint.
3. If authentication is required, configure `api-key-env` and export the
   corresponding environment variable.
4. Confirm the server is reachable, then launch SAURON:

```sh
./bin/sauron --agent orchestrator --provider local --model gpt-oss-20b
```

On Windows PowerShell, run the equivalent command from the repository root:

```powershell
.\bin\sauron.exe --agent orchestrator --provider local --model gpt-oss-20b
```

Use `--agent NAME` to select any prompt key from `cfg/system_prompts.json`,
such as `resume-specialist`. If omitted, SAURON uses `orchestrator`.

For a server listening on `127.0.0.1:8080`, the base URL is commonly
`http://127.0.0.1:8080/v1`. The model server must already be ready when
SAURON starts. Connection failures are reported; SAURON does not start the
server or wait for it to become available.

To select another persona, change only the `--agent` value. For example:

```sh
./bin/sauron --agent resume-specialist --provider local --model gpt-oss-20b
./bin/sauron --agent code-reviewer --provider local --model gpt-oss-20b
./bin/sauron --agent qa-engineer --provider local --model gpt-oss-20b
./bin/sauron --agent project-planner --provider local --model gpt-oss-20b
./bin/sauron --agent git-specialist --provider local --model gpt-oss-20b
```

During chat, enter a message at the `>` prompt. `/help` shows the CLI help;
`/exit` and `/quit` end the session.

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
- If both transports are enabled, SAURON starts the configured command as a
  child process in both mode and communicates with it over stdio.

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

The `git-specialist` persona can use the configured Git MCP server for its
exposed operations. It should inspect repository state before acting and seek
confirmation before risky or destructive changes. The MCP server exposes only
the operations listed above, not every Git command; requests outside that set
can be explained but may not be executable through the Git tools. Git operations
that change local or remote state run with the permissions of the SAURON
process. For SSH, hooks, signatures, or other Git configuration, see **Security**
below; the shell server is separate and disabled by default.

### Add an MCP server

Add a server object to the `mcp_servers` array in `cfg/mcp_servers.json`. For
example, a stdio server entry has this shape:

```json
{
  "name": "my-tool",
  "transports": {
    "stdio": {
      "enable": true,
      "command": "bin/my_tool_server",
      "args": [],
      "env": {}
    },
    "http": {
      "enable": false,
      "host": "127.0.0.1",
      "port": 8100
    }
  }
}
```

Build the server executable first. Commands and relative paths are resolved
from SAURON's working directory; run from the repository root or use an
appropriate absolute executable path. Add credentials only through
environment variables, not source-controlled configuration. Every enabled
server must be reachable and complete tool discovery at startup.

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

## Security

MCP tools run with the operating-system permissions of SAURON and can perform
mutating operations. The tool servers do not provide a per-call approval
boundary. Select and configure only tools that are appropriate for the
workspace and model trust level. Do not expose secrets in prompts, tool output,
or model-visible files.

### Shell tool execution

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

### HTTP exposure

MCP servers configured for HTTP bind to the configured host and port. The
current HTTP transport is not an authenticated remote-access boundary. Keep
servers bound to loopback unless you provide appropriate network controls.

## Persistence and conversation lifetime

The memory tool server loads and saves its JSON key-value store in
`sauron_memory.json` in its working directory. The old `mcpxx_memory.json`
file is read as a fallback if the new file does not exist; subsequent writes
go to `sauron_memory.json`. Both files are excluded from Git. This is separate
from chat history: conversation history is held in the running agent and is
not restored after the SAURON process exits.

## Examples and checks

### Smoke-test the MCP client/server demo

The demo server exposes a calculator and text summarizer over HTTP on
`127.0.0.1:8082/mcp`. Start it in one terminal and the client in another:

```sh
./bin/test_mcp_server
```

```sh
./bin/test_mcp_client
```

The client lists the demo tools, calls the calculator, and prints an
OpenAI-compatible tool schema/response. Stop the server with Ctrl+C.

`test_sauron_agent` is an executable integration demo: it accepts the same
`[--agent NAME] --provider NAME --model NAME` options, initializes the
configured agent (defaulting to `orchestrator`), and sends a fixed
directory-listing request. It requires valid provider settings and working
enabled MCP servers:

```sh
./bin/test_sauron_agent --agent orchestrator --provider openai --model gpt-6-luna
```

These are executable demos, not a standalone automated unit-test suite.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Provider or model not found | Use the exact `name` and `model-name` values in `cfg/ai_providers.json`. |
| Prompt cannot be loaded | Use an exact top-level key from `cfg/system_prompts.json`; verify the JSON parses. |
| LLM connection or authentication fails | Start the model endpoint first, confirm its `base-url`, model identifier and API-key environment variable, and check that HTTPS support was enabled when needed. |
| MCP startup fails | Run from the repository root, build each configured executable, verify its command/arguments/environment, and confirm every enabled server starts and responds to tool discovery. |
| Shell tools report execution disabled | This is the default. Set `SAURON_SHELL_ENABLED=1` in the shell server's `env` only if you deliberately accept the risks described above. |
| A tool is unavailable to the model | Confirm its MCP server is enabled and connected at startup; check for a duplicate tool name from an earlier server. |
| Agent stops without a final answer | It may have reached the maximum tool-call cycle count; inspect model and tool output and simplify or split the request. |

SAURON reads configuration paths relative to its working directory, not the
binary's location. Starting it outside the repository root can therefore
produce missing-config or missing-server errors.

## Implementation notes

- MCP messages use JSON-RPC 2.0 and this runtime's configured protocol version
  `2026-07-28`, including the required protocol/client metadata on requests.
- The LLM client connects only to the configured, already-running
  OpenAI-compatible endpoint, sends chat-completions requests, and reads
  function/tool calls from model responses.
- Tool calls are routed to the MCP client that advertised each tool. Duplicate
  tool names from later servers are skipped. The model receives discovered
  tools regardless of which persona is selected.
- The runtime defaults to at most 50 model/tool cycles per chat turn and
  truncates each tool observation to 4,000 characters. These limits can be
  changed through `sauron::Agent::Config`; the CLI uses the defaults in
  `include/pooriayousefi/sauron/agent.hpp`.
- File, directory, Git, and shell tools act in the permissions and working
  directory of the SAURON process. Review their operations before enabling
  them for an untrusted workflow.
- SAURON runs one selected prompt persona at a time. It does not create,
  delegate to, or coordinate multiple independent agents.
- There is no CMake or Make build system yet. The commands in this guide
  compile executables directly. The programs under `src/test/` are demos and
  integration harnesses, not a standalone automated unit-test suite.
