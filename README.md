# agent-minimal
A minimal, embeddable C++ local LLM agent built on llama.cpp


## Features v0.1.0
- Local LLM chat – Load GGUF model and chat interactively (recommended gemma4)    
- Tool/function calling – Built-in file I/O, web fetch/search, command execution, code search   
- Multi-step planning – Autonomous plan creation, step tracking, and execution
- CUDA GPU acceleration – Automatic detection and enablement
- Interactive CLI – REPL with slash commands, parameter tuning, session management

## Unique Selling Points
> Embeddable C++ library - `libagent_core` can be linked into any C++17 application via `#include <core/Agent.h>`    
> Zero runtime dependencies - No Python, no Node, no Docker – just the binary + a `.gguf` file    
> Portable single binary - The CLI is a thin consumer; the library is the product    
> Fully offline - Runs entirely without internet access    
> Thread-safe API - Synchronous `chat()` and streaming `chatStreaming()` with proper lifecycle management    
> ~5 MB footprint - Minimal codebase, no bloat   

## Quick Start (library consumer)
```cpp
#include <core/Agent.h>

Agent::init();
Agent agent;
agent.loadModel("models/qwen2.5.gguf");
std::string reply = agent.chat("Hello!");
Agent::shutdown();
```

## Build
```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```
Requirements: Visual Studio 2022, CMake 3.20+, CUDA Toolkit(optional, for GPU acceleration)    

## Tested With
Developed and tested against gemma4-12B and Qwen2.5-7B. Other models may work but are used without guarantees – template detection heuristics may not cover all formats.    
