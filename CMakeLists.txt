cmake_minimum_required(VERSION 3.20)
project(agent_minimal VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(llama
    GIT_REPOSITORY https://github.com/ggerganov/llama.cpp
    GIT_TAG master
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)
set(LLAMA_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER  OFF CACHE BOOL "" FORCE)

find_package(CUDAToolkit QUIET)
if(CUDAToolkit_FOUND)
    set(GGML_CUDA ON CACHE BOOL "" FORCE)
    message(STATUS "CUDA ${CUDAToolkit_VERSION} found – GPU acceleration enabled")
endif()

FetchContent_MakeAvailable(llama)

# ── Core library (embeddable, no CLI dependencies) ──────────────────
add_library(agent_core STATIC
    src/core/Model.h
    src/core/Model.cpp
    src/core/Sampler.h
    src/core/Sampler.cpp
    src/core/ChatHistory.h
    src/core/ChatHistory.cpp
    src/core/Agent.h
    src/core/Agent.cpp
    src/core/Tool.h
    src/core/Tool.cpp
    src/core/ToolRegistry.h
    src/core/ToolRegistry.cpp
    src/core/JsonHelper.h
)
target_include_directories(agent_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(agent_core PUBLIC llama)

# ── Tools library (optional for embedders who want built-in tools) ──
add_library(agent_tools STATIC
    src/tools/FileTool.h
    src/tools/FileTool.cpp
    src/tools/WebFetchTool.h
    src/tools/WebFetchTool.cpp
    src/tools/WebSearchTool.h
    src/tools/WebSearchTool.cpp
    src/tools/ExecuteCommandTool.h
    src/tools/ExecuteCommandTool.cpp
    src/tools/SearchPatternTool.h
    src/tools/SearchPatternTool.cpp
    src/tools/SearchFilesTool.h
    src/tools/SearchFilesTool.cpp
)
target_include_directories(agent_tools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(agent_tools PUBLIC agent_core)

# ── CLI executable (thin consumer) ─────────────────────────────────
add_executable(agent_minimal src/main.cpp)
target_link_libraries(agent_minimal PRIVATE agent_tools)

if(WIN32)
    target_link_libraries(agent_minimal PRIVATE winhttp)
endif()

# ── Windows: copy llama.cpp DLLs to executable output ──────────────
if(WIN32)
    add_custom_command(TARGET agent_minimal POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/llama.dll"
            "$<TARGET_FILE_DIR:agent_minimal>/llama.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/ggml.dll"
            "$<TARGET_FILE_DIR:agent_minimal>/ggml.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/ggml-base.dll"
            "$<TARGET_FILE_DIR:agent_minimal>/ggml-base.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/ggml-cpu.dll"
            "$<TARGET_FILE_DIR:agent_minimal>/ggml-cpu.dll"
        COMMENT "Copy llama.cpp DLLs"
    )
    if(CUDAToolkit_FOUND)
        add_custom_command(TARGET agent_minimal POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/ggml-cuda.dll"
                "$<TARGET_FILE_DIR:agent_minimal>/ggml-cuda.dll"
            COMMENT "Copy CUDA DLL"
        )
    endif()
endif()
