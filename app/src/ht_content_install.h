#pragma once
#include <array>
#include <filesystem>
#include <functional>
#include <string>
namespace ht {
bool ReadyForLaunch(const std::filesystem::path& root);
void PrepareContent(const std::filesystem::path& root,
                    const std::array<std::filesystem::path, 3>& sources,
                    const std::function<void(std::string)>& progress);
}
