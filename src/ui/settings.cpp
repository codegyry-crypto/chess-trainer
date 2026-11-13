#include "ui/settings.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <variant>
#include <vector>

namespace ui {
namespace fs = std::filesystem;

namespace {
fs::path envPath(const wchar_t* name) {
    wchar_t* value = nullptr;
    size_t len = 0;
    if (_wdupenv_s(&value, &len, name) == 0 && value) {
        fs::path p(value);
        std::free(value);
        return p;
    }
    return {};
}

fs::path settingsFile() { return appDataDir() / "settings.ini"; }

}

fs::path exeDir() {
    wchar_t buf[4096];
    const DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
    return fs::path(std::wstring(buf, n)).parent_path();
}

fs::path appDataDir() {
    fs::path base = envPath(L"APPDATA");
    if (base.empty()) base = fs::current_path();
    fs::path dir = base / "ChessTrainer";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

fs::path documentsDir() {
    fs::path base;
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &raw)) && raw) base = raw;
    CoTaskMemFree(raw);
    if (base.empty()) base = envPath(L"USERPROFILE") / "Documents";
    fs::path dir = base / "ChessTrainer";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

using Field = std::pair<const char*, std::variant<std::string*, bool*, int*, float*>>;

std::vector<Field> fields(Settings& s) {
    return {{"theme", &s.theme},
            {"boardPalette", &s.boardPalette},
            {"pieceSet", &s.pieceSet},
            {"russianNotation", &s.russianNotation},
            {"showCoords", &s.showCoords},
            {"showLegalMoves", &s.showLegalMoves},
            {"highlightLastMove", &s.highlightLastMove},
            {"animate", &s.animate},
            {"showMoveArrow", &s.showMoveArrow},
            {"autoFlip", &s.autoFlip},
            {"soundOn", &s.soundOn},
            {"uiSounds", &s.uiSounds},
            {"soundSet", &s.soundPack},
            {"volume", &s.volume},
            {"vsBot", &s.vsBot},
            {"ratedGames", &s.ratedGames},
            {"coachMode", &s.coachMode},
            {"liveGrades", &s.liveGrades},
            {"botId", &s.botId},
            {"colorChoice", &s.colorChoice},
            {"tcIndex", &s.tcIndex}};
}

void Settings::load() {
    std::ifstream in(settingsFile());
    std::map<std::string, std::string> kv;
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string v = line.substr(eq + 1);
        if (!v.empty() && v.back() == '\r') v.pop_back();
        kv[line.substr(0, eq)] = v;
    }
    for (auto& [key, field] : fields(*this)) {
        const auto it = kv.find(key);
        if (it == kv.end()) continue;
        const std::string& v = it->second;
        std::visit(
            [&](auto* p) {
                using T = std::remove_pointer_t<decltype(p)>;
                if constexpr (std::is_same_v<T, std::string>) *p = v;
                else if constexpr (std::is_same_v<T, bool>) *p = v == "1";
                else if constexpr (std::is_same_v<T, int>) *p = std::atoi(v.c_str());
                else *p = std::strtof(v.c_str(), nullptr);
            },
            field);
    }
    colorChoice = std::clamp(colorChoice, 0, 2);
    tcIndex = std::max(0, tcIndex);
    if (!(volume >= 0 && volume <= 1)) volume = 0.7f;
}

void Settings::save() const {
    std::ofstream out(settingsFile());
    for (auto& [key, field] : fields(const_cast<Settings&>(*this))) {
        out << key << '=';
        std::visit(
            [&](auto* p) {
                if constexpr (std::is_same_v<std::remove_pointer_t<decltype(p)>, bool>) out << (*p ? 1 : 0);
                else out << *p;
            },
            field);
        out << '\n';
    }
}

}
