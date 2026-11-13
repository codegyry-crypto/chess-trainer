#include "ui/ui.h"

#include "ui/sound.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace ui {
namespace {
struct State {
    Theme theme;
    bool inputEnabled = true;
    bool wantHand = false;
    Vector2 pressPos{-1, -1};
    std::map<int, Font> regular, bold;
    std::vector<int> codepoints;
    std::string toastText;
    double toastUntil = 0;
};
State& S() {
    static State s;
    return s;
}

const char* RegularPath = "C:/Windows/Fonts/segoeui.ttf";
const char* BoldPath = "C:/Windows/Fonts/seguisb.ttf";

float dpiScale() {
    const float k = GetWindowScaleDPI().x;
    return k > 0.5f ? k : 1.0f;
}

Font& fontFor(float size, bool bold) {
    State& s = S();
    const int px = std::max(8, (int)std::lround(size * dpiScale()));
    auto& cache = bold ? s.bold : s.regular;
    auto it = cache.find(px);
    if (it != cache.end()) return it->second;

    Font f{};
    const char* path = bold ? BoldPath : RegularPath;
    if (FileExists(path)) {
        f = LoadFontEx(path, px, s.codepoints.data(), (int)s.codepoints.size());
        SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
    } else {
        f = GetFontDefault();
    }
    return cache.emplace(px, f).first->second;
}

}

void init() {
    State& s = S();
    for (int c = 32; c < 127; ++c) s.codepoints.push_back(c);
    for (int c = 0x400; c < 0x460; ++c) s.codepoints.push_back(c);
    for (int c : {0xA0, 0xAB, 0xBB, 0xB0, 0xB1, 0xB7, 0xD7, 0x2013, 0x2014, 0x2026, 0x2116, 0x2190, 0x2192,
                  0x2191, 0x2193, 0x2212, 0x221E, 0x2022, 0x2713, 0x2715, 0x2039, 0x203A, 0x2190})
        s.codepoints.push_back(c);
}

void shutdown() {
    State& s = S();
    for (auto* cache : {&s.regular, &s.bold})
        for (auto& [px, f] : *cache)
            if (f.texture.id != GetFontDefault().texture.id) UnloadFont(f);
    s.regular.clear();
    s.bold.clear();
}

void setTheme(const Theme& t) { S().theme = t; }
const Theme& theme() { return S().theme; }

void beginFrame() {
    State& s = S();
    s.wantHand = false;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) s.pressPos = mouse();
}

void endFrame() {
    SetMouseCursor(S().wantHand ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);
}

void setInputEnabled(bool on) { S().inputEnabled = on; }
bool inputEnabled() { return S().inputEnabled; }
void setHandCursor() { S().wantHand = true; }

float scale() { return dpiScale(); }
float screenW() { return GetScreenWidth() / dpiScale(); }
float screenH() { return GetScreenHeight() / dpiScale(); }
Vector2 mouse() {
    const Vector2 m = GetMousePosition();
    return {m.x / dpiScale(), m.y / dpiScale()};
}

void beginScaled() {
    Camera2D cam{};
    cam.zoom = dpiScale();
    BeginMode2D(cam);
}
void endScaled() { EndMode2D(); }

void beginClip(Rectangle r) {
    const float k = dpiScale();
    BeginScissorMode(int(r.x * k), int(r.y * k), int(r.width * k), int(r.height * k));
}
void endClip() { EndScissorMode(); }

void text(const std::string& str, Vector2 pos, float size, Color c, bool bold) {
    Font& f = fontFor(size, bold);
    const float k = dpiScale();
    DrawTextEx(f, str.c_str(), {std::round(pos.x * k) / k, std::round(pos.y * k) / k}, std::round(size * k) / k, 0, c);
}

Vector2 measure(const std::string& str, float size, bool bold) {
    Font& f = fontFor(size, bold);
    return MeasureTextEx(f, str.c_str(), std::round(size * dpiScale()) / dpiScale(), 0);
}

void textCentered(const std::string& str, Rectangle r, float size, Color c, bool bold) {
    const Vector2 m = measure(str, size, bold);
    text(str, {r.x + (r.width - m.x) / 2, r.y + (r.height - m.y) / 2}, size, c, bold);
}

float textWrapped(const std::string& str, Rectangle r, float size, Color c, bool bold, float lineGap) {
    const float lineH = size * lineGap;
    float y = r.y;
    std::string line, word;
    auto flush = [&] {
        if (!line.empty()) text(line, {r.x, y}, size, c, bold);
        y += lineH;
        line.clear();
    };
    auto addWord = [&] {
        if (word.empty()) return;
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && measure(candidate, size, bold).x > r.width) {
            flush();
            line = word;
        } else {
            line = candidate;
        }
        word.clear();
    };
    for (char ch : str) {
        if (ch == ' ') addWord();
        else if (ch == '\n') addWord(), flush();
        else word += ch;
    }
    addWord();
    if (!line.empty()) flush();
    return y - r.y;
}

void roundRect(Rectangle r, float radius, Color c) {
    const float m = std::min(r.width, r.height);
    if (m <= 0) return;
    DrawRectangleRounded(r, std::min(1.0f, 2 * radius / m), 8, c);
}

void roundRectLines(Rectangle r, float radius, float thick, Color c) {
    const float m = std::min(r.width, r.height);
    if (m <= 0) return;
    DrawRectangleRoundedLinesEx(r, std::min(1.0f, 2 * radius / m), 8, thick, c);
}

void card(Rectangle r, float radius) { roundRect(r, radius, theme().panel); }

bool hovered(Rectangle r) {
    return S().inputEnabled && CheckCollisionPointRec(mouse(), r);
}

static bool rawClicked(Rectangle r) {
    return hovered(r) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(S().pressPos, r);
}

bool clicked(Rectangle r) {
    if (!rawClicked(r)) return false;
    sound::play(sound::Id::Click);
    return true;
}

bool button(Rectangle r, const std::string& label, Style style, bool enabled, float fontSize) {
    const Theme& t = theme();
    const bool hot = enabled && hovered(r);
    const bool down = hot && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    Color bg{}, fg = t.text;
    switch (style) {
    case Style::Primary:
        bg = t.accent;
        fg = t.accentText;
        if (hot) bg = mix(bg, WHITE, 0.12f);
        break;
    case Style::Secondary:
        bg = t.panel2;
        if (hot) bg = mix(bg, t.text, 0.08f);
        break;
    case Style::Ghost:
        bg = hot ? t.panel2 : Color{0, 0, 0, 0};
        break;
    }
    if (down) bg = mix(bg, BLACK, 0.12f);
    if (!enabled) {
        bg = fade(bg, 0.5f);
        fg = t.sub;
    }
    if (bg.a) roundRect(r, 8, bg);
    textCentered(label, r, fontSize, fg, style == Style::Primary);
    if (hot) setHandCursor();
    return enabled && clicked(r);
}

bool chip(Rectangle r, const std::string& label, bool selected, bool enabled, float fontSize) {
    const Theme& t = theme();
    const bool hot = enabled && hovered(r);
    Color bg = selected ? fade(t.accent, 0.22f) : t.panel2;
    if (hot && !selected) bg = mix(bg, t.text, 0.08f);
    roundRect(r, 8, bg);
    if (selected) roundRectLines(r, 8, 2, t.accent);
    textCentered(label, r, fontSize, enabled ? (selected ? t.text : t.text) : t.sub, selected);
    if (hot) setHandCursor();
    return enabled && clicked(r);
}

bool toggle(Rectangle r, const std::string& label, bool& value) {
    const Theme& t = theme();
    const bool hot = hovered(r);
    if (hot) {
        roundRect(r, 8, fade(t.panel2, 0.6f));
        setHandCursor();
    }
    text(label, {r.x + 12, r.y + (r.height - 20) / 2}, 17, t.text);
    const Rectangle sw{r.x + r.width - 56, r.y + (r.height - 26) / 2, 44, 26};
    roundRect(sw, 13, value ? t.accent : t.panel2);
    const float knobX = value ? sw.x + sw.width - 13 : sw.x + 13;
    DrawCircleV({knobX, sw.y + 13}, 9, value ? t.accentText : t.sub);
    if (rawClicked(r)) {
        value = !value;
        sound::play(sound::Id::Toggle);
        return true;
    }
    return false;
}

bool slider(Rectangle r, float& value) {
    const Theme& t = theme();
    const float cy = r.y + r.height / 2;
    roundRect({r.x, cy - 3, r.width, 6}, 3, t.panel2);
    roundRect({r.x, cy - 3, r.width * value, 6}, 3, t.accent);
    DrawCircleV({r.x + r.width * value, cy}, 9, t.accentText);
    DrawCircleLinesV({r.x + r.width * value, cy}, 9, t.accent);
    if (hovered({r.x - 10, r.y, r.width + 20, r.height})) setHandCursor();
    if (S().inputEnabled && IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(S().pressPos, {r.x - 10, r.y, r.width + 20, r.height})) {
        const float v = std::clamp((mouse().x - r.x) / r.width, 0.0f, 1.0f);
        if (v != value) {
            value = v;
            return true;
        }
    }
    return false;
}

void badge(Vector2 pos, const std::string& label, Color bg, Color fg, float size) {
    const Vector2 m = measure(label, size, true);
    const Rectangle r{pos.x, pos.y, m.x + 14, m.y + 6};
    roundRect(r, 6, bg);
    textCentered(label, r, size, fg, true);
}

void toast(const std::string& message) {
    S().toastText = message;
    S().toastUntil = GetTime() + 2.8;
}

void drawToast() {
    State& s = S();
    if (s.toastText.empty() || GetTime() > s.toastUntil) return;
    const float alpha = (float)std::min(1.0, (s.toastUntil - GetTime()) / 0.4);
    const Vector2 m = measure(s.toastText, 17);
    const Rectangle r{(screenW() - m.x) / 2 - 20, screenH() - 80.0f, m.x + 40, 44};
    roundRect(r, 10, fade(s.theme.panel2, alpha));
    roundRectLines(r, 10, 1, fade(s.theme.accent, alpha));
    textCentered(s.toastText, r, 17, fade(s.theme.text, alpha));
}

}
