#include "ui/sound.h"

#include <raylib.h>

#include <map>

namespace sound {
namespace fs = std::filesystem;

namespace {
const Pack KnownPacks[] = {
    {"marble", "Мрамор", "Звонкий стук каменных фигур"},
    {"wood", "Дерево", "Настоящие фигуры на деревянной доске"},
    {"sfx", "Эффекты", "Мягкие игровые эффекты"},
    {"piano", "Пианино", "Каждый ход — нота"},
    {"futuristic", "Футуризм", "Электронные, космические"},
    {"nes", "Ретро", "8-бит, как в старых приставках"},
    {"lisp", "Необычный", "Странный и смешной набор"},
};

const char* fileFor(Id id) {
    switch (id) {
    case Id::Move: return "Move";
    case Id::Capture: return "Capture";
    case Id::Check: return "Check";
    case Id::GameStart: return "GenericNotify";
    case Id::Checkmate: return "Checkmate";
    case Id::Draw: return "Draw";
    case Id::LowTime: return "LowTime";
    case Id::Illegal: return "Error";
    case Id::Brilliant: return "Brilliant";
    case Id::Click: return "Click";
    case Id::Toggle: return "Toggle";
    }
    return "";
}

bool isUi(Id id) { return id >= Id::Click; }

struct State {
    bool ready = false;
    bool enabled = true, uiEnabled = true;
    float volume = 0.7f;
    fs::path dir;
    std::vector<Pack> packs;
    std::string pack;
    std::map<Id, Sound> game, ui;
};
State& S() {
    static State s;
    return s;
}

Sound loadFile(const fs::path& p) {
    if (!fs::exists(p)) return Sound{};
    const std::u8string u8 = p.u8string();
    return LoadSound(reinterpret_cast<const char*>(u8.c_str()));
}

void unloadMap(std::map<Id, Sound>& m) {
    for (auto& [id, s] : m)
        if (s.frameCount) UnloadSound(s);
    m.clear();
}

}

void init(const fs::path& soundsDir) {
    State& s = S();
    s.dir = soundsDir;
    for (const Pack& p : KnownPacks)
        if (fs::exists(soundsDir / p.id / "Move.ogg")) s.packs.push_back(p);
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;
    s.ready = true;
    for (Id id : {Id::Click, Id::Toggle})
        s.ui[id] = loadFile(soundsDir / "ui" / (std::string(fileFor(id)) + ".ogg"));
}

void shutdown() {
    State& s = S();
    if (!s.ready) return;
    unloadMap(s.game);
    unloadMap(s.ui);
    CloseAudioDevice();
    s.ready = false;
}

const std::vector<Pack>& packs() { return S().packs; }

void setPack(const std::string& id) {
    State& s = S();
    if (!s.ready || id == s.pack) return;
    std::string use = id;
    if (!fs::exists(s.dir / use / "Move.ogg")) use = s.packs.empty() ? "" : s.packs.front().id;
    unloadMap(s.game);
    s.pack = id;
    if (use.empty()) return;
    for (Id g : {Id::Move, Id::Capture, Id::Check, Id::GameStart, Id::Checkmate, Id::Draw, Id::LowTime, Id::Illegal}) {
        s.game[g] = loadFile(s.dir / use / (std::string(fileFor(g)) + ".ogg"));
    }
    s.game[Id::Brilliant] = loadFile(s.dir / "ui" / "Brilliant.ogg");
}

void setEnabled(bool on) { S().enabled = on; }
void setUiEnabled(bool on) { S().uiEnabled = on; }
void setVolume(float v) { S().volume = v; }

void play(Id id) {
    State& s = S();
    if (!s.ready) return;
    if (isUi(id) ? !s.uiEnabled : !s.enabled) return;
    auto& m = isUi(id) ? s.ui : s.game;
    auto it = m.find(id);
    if (it == m.end() || !it->second.frameCount) {
        if (id == Id::Check) return play(Id::Move);
        if (id == Id::Checkmate) return play(Id::Check);
        return;
    }
    SetSoundVolume(it->second, isUi(id) ? s.volume * 0.5f : s.volume);
    const bool vary = id == Id::Move || id == Id::Capture || id == Id::Check || id == Id::Click;
    SetSoundPitch(it->second, vary ? 1.0f + GetRandomValue(-45, 45) / 1000.0f : 1.0f);
    PlaySound(it->second);
}

}
