#include "training/update.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <array>
#include <fstream>
#include <regex>
#include <sstream>

namespace update {

namespace {
const std::wstring Repo = L"/codegyry-crypto/chess-trainer";

std::array<int, 3> parse(std::string s) {
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.erase(0, 1);
    std::array<int, 3> v{};
    std::istringstream in(s);
    char dot = 0;
    in >> v[0] >> dot >> v[1] >> dot >> v[2];
    return v;
}
}

bool isNewer(const std::string& tag, const std::string& current) {
    return !tag.empty() && parse(tag) > parse(current);
}

std::string latestTag() {
    const auto file = std::filesystem::temp_directory_path() / "chess-trainer-latest.json";
    training::ImportProgress p;
    if (!training::downloadFile(L"api.github.com", L"/repos" + Repo + L"/releases/latest", file, p)) return {};
    std::ifstream in(file);
    const std::string json((std::istreambuf_iterator<char>(in)), {});
    std::smatch m;
    if (!std::regex_search(json, m, std::regex(R"re("tag_name"\s*:\s*"(v[0-9.]+)")re"))) return {};
    return m[1];
}

bool downloadInstaller(const std::string& tag, const std::filesystem::path& to, training::ImportProgress& p) {
    const std::wstring path = Repo + L"/releases/download/" + std::wstring(tag.begin(), tag.end()) + L"/ChessTrainer-Setup.exe";
    return training::downloadFile(L"github.com", path, to, p);
}

bool runInstaller(const std::filesystem::path& exe) {
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", exe.c_str(),
                                                   L"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART", nullptr,
                                                   SW_SHOWNORMAL)) > 32;
}

}
