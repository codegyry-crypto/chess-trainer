#include "ui/sound.h"

#include <raylib.h>

#include <algorithm>
#include <cmath>
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
    std::map<Id, Sound> game, ui, fx;
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

struct Note {
    float freq, start, len;
};

Sound synth(std::initializer_list<Note> notes) {
    const int rate = 44100;
    float end = 0;
    for (const Note& n : notes) end = std::max(end, n.start + n.len);
    std::vector<float> mixed(size_t(end * rate));
    for (const Note& n : notes)
        for (size_t i = 0, first = size_t(n.start * rate); i < size_t(n.len * rate) && first + i < mixed.size(); ++i) {
            const float t = float(i) / rate, w = 2 * PI * n.freq * t;
            const float env = std::min(1.0f, t / 0.004f) * std::exp(-5 * t / n.len);
            mixed[first + i] += env * (std::sin(w) + 0.3f * std::sin(2 * w) + 0.08f * std::sin(3 * w));
        }
    std::vector<short> pcm(mixed.size());
    for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = short(std::clamp(mixed[i] * 0.22f, -1.0f, 1.0f) * 32767);
    return LoadSoundFromWave(Wave{unsigned(pcm.size()), rate, 16, 1, pcm.data()});
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
    const float C5 = 523.25f, E5 = 659.26f, G5 = 783.99f, A5 = 880.0f, C6 = 1046.5f, D6 = 1174.66f, E6 = 1318.51f,
                G6 = 1567.98f, C7 = 2093.0f;
    s.fx[Id::Brilliant] = synth({{C6, 0, .6f}, {E6, .07f, .6f}, {G6, .14f, .6f}, {C7, .21f, .9f}, {G6, .21f, .9f}});
    s.fx[Id::Great] = synth({{G5, 0, .45f}, {D6, .08f, .7f}});
    s.fx[Id::Solved] = synth({{E5, 0, .3f}, {A5, .08f, .5f}});
    s.fx[Id::Streak] = synth({{C6, 0, .3f}, {D6, .05f, .3f}, {E6, .1f, .3f}, {G6, .15f, .7f}, {C6, .15f, .7f}});
    s.fx[Id::Achievement] =
        synth({{C5, 0, .5f}, {G5, 0, .5f}, {E5, .16f, .5f}, {C6, .16f, .5f}, {C6, .32f, 1.2f}, {E6, .32f, 1.2f}, {G6, .32f, 1.2f}});
}

void shutdown() {
    State& s = S();
    if (!s.ready) return;
    unloadMap(s.game);
    unloadMap(s.ui);
    unloadMap(s.fx);
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
}

void setEnabled(bool on) { S().enabled = on; }
void setUiEnabled(bool on) { S().uiEnabled = on; }
void setVolume(float v) { S().volume = v; }

void play(Id id) {
    State& s = S();
    if (!s.ready) return;
    if (isUi(id) ? !s.uiEnabled : !s.enabled) return;
    auto& m = isUi(id) ? s.ui : s.fx.contains(id) ? s.fx : s.game;
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
