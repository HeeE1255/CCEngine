#pragma once
#include <filesystem>

int RunShaderReflectionCommand(const std::filesystem::path& bytecode,
    const std::filesystem::path& compilerLibrary);
