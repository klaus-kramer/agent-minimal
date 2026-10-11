#pragma once

#include "core/ToolRegistry.h"
#include <string>

void registerFileTools(ToolRegistry& registry);

void setAllowedRoot(const std::string &path);

bool isPathAllowed(const std::string &path, std::string &error);
