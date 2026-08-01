// Copyright (c) 2026 Klaus Kramer - Licensed under the MIT License

#pragma once

#include "core/ToolRegistry.h"
#include <string>

void registerFileTools(ToolRegistry& registry);

void setAllowedRoot(const std::string &path);

// Returns true if the given path (relative or absolute) is inside the
// allowed root. On failure, *error holds a human-readable reason.
bool isPathAllowed(const std::string &path, std::string &error);
