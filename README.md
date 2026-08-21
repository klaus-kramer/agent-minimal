# agent-minimal
A minimal, embeddable C++ local AI agent built on llama.cpp. It demonstrates tool calling and a clean architecture without heavy dependencies.


## Features v0.2.1
- Local LLM chat – Load GGUF model and chat interactively (atm recommended gemma4)    
- Tool/function calling – Built-in file I/O, web fetch/search,  pattern search, command execution    
- CUDA GPU acceleration – Automatic detection and enablement     
- Interactive CLI – slash commands, parameter tuning, session management    
- NEW v0.2.1 command /think     
- NEW v0.2.1 tested with qwen3.8 27b    

## Key Points:
- Embeddable C++ library - `libagent_core` can be linked into any C++ application via `#include <core/Agent.h>`    
- Zero runtime dependencies - No Python, no Node, no Docker – just the binary + a `.gguf` file    
- Portable single binary - The CLI is a thin client; the library is the product    
- Fully offline - Runs entirely without internet access    
- ~5 MB footprint - Minimal codebase


## commands:
  /exit              Exit the program    
  /reset             Clear conversation history    
  /system <text>     Set system prompt    
  /params            Show current parameters    
  /temp [value]      Show/change temperature    
  /tools             List available AI tools    
  /help              Show this help    
  /nosecurity        Toggle security confirmations off/on    
  /safe              Toggle read-only safe mode    
  /think [on|off]    Toggle thinking (Qwen3: empty <think></think> prefill)    


## ai-assistant-tools inside (in agent-minimal type /tools):
- read_file: Read the content of a file (e.g. read file LICENSE.txt)    
- list_directory: List files and subdirectories of a path    
- write_file: Write content to a file
- edit_file: Replace text in an existing file    
- web_fetch: Fetch content from a URL (e.g. show the output of URL https://example.com )
- web_search: Search the web using DuckDuckGo    
- search_pattern: Search file contents using a regex pattern    
- search_files: Find files by glob pattern (e.g. show all files ending "*.h")
- execute_command: Execute a shell command and return its output (e.g. execute command  "python3 helloworld.py" )

## CLI startparameter    
- path --config
- model -m     
- temperature -t
- context -c (example -c 8192)    
- maxtokens -n (example -n 4096)    
- gpulayer -ngl (example -ngl 99)
- systemtext -s
- listmodels -l
- help -h    
  

## Quick Start (for library user)
```cpp
#include <core/Agent.h>

Agent::init();
Agent agent;
agent.loadModel("models/gemma4.xyz.gguf");
std::string reply = agent.chat("Hello");
Agent::shutdown();
```

## Build
**Windows:**
```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```
**Linux:**
```
cmake -B build
cmake --build build -j$(nproc)
```
Or with Ninja: `cmake -B build -G Ninja && cmake --build build`   

## Requirements:
Windows - Visual Studio 2022, CMake 3.20+, CUDA Toolkit(optional, for GPU acceleration)   
Linux - `make` (or `ninja`), `pkg-config`, `libcurl` (for web tools)    

## Tested With
create models directory and download model. Developed and tested against:    
gemma4-12B    
qwen3.8 27b    
Other models may work but are used without guarantees – template detection heuristics may not cover all formats.    

## links:    
my new ai Grimmelshausen https://huggingface.co/Klaus-Kramer/Grimmelshausen    
my tool Vigil-Fluminis for Windows-security https://github.com/klaus-kramer/Vigil-Fluminis    

