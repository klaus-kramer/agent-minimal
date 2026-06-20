// Copyright (c) 2026 Klaus Kramer - Licensed under the MIT License

#pragma once

#include "core/ToolRegistry.h"
#include <string>

void registerFileTools(ToolRegistry& registry);

void setAllowedRoot(const std::string &path);
