#pragma once

#include "core/ToolRegistry.h"
#include <string>

void registerFileTools(ToolRegistry& registry);

void setAllowedRoot(const std::string &path);

bool isPathAllowed(const std::string &path, std::string &error);

// Resolves a user/model supplied path against the allowed project root.
// Relative paths are resolved against the root. On Windows a leading '/'
// or '\' is treated as project-relative (e.g. "/src" -> "<root>/src").
bool resolvePathInRoot(const std::string &path, std::string &resolvedOut, std::string &error);

std::string getAllowedRootPath();
