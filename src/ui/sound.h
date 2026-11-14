#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace sound {
enum class Id {
    Move, Capture, Check, GameStart, Checkmate, Draw, LowTime, Illegal, Brilliant, Great, Solved, Streak, Achievement,
    Click, Toggle,
};

struct Pack {
    std::string id;
    std::string name;
    std::string desc;
};

void init(const std::filesystem::path& soundsDir);
void shutdown();
const std::vector<Pack>& packs();
void setPack(const std::string& id);
void setEnabled(bool on);
void setUiEnabled(bool on);
void setVolume(float v);
void play(Id id);

}
