#pragma once
#include "training/puzzles.h"

#include <filesystem>
#include <string>

namespace update {

bool isNewer(const std::string& tag, const std::string& current);
std::string latestTag();
bool downloadInstaller(const std::string& tag, const std::filesystem::path& to, training::ImportProgress& p);
bool runInstaller(const std::filesystem::path& exe);

}
