#pragma once
#include <raylib.h>

#include <string>
#include <vector>

namespace ui {
constexpr Color hex(unsigned v, unsigned char a = 255) {
    return Color{(unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v, a};
}

struct Theme {
    std::string id;
    std::string name;
    std::string desc;

    Color bg;
    Color panel;
    Color panel2;
    Color border;
    Color text;
    Color sub;
    Color accent;
    Color accentText;
    Color hl;
    Color hlText;
    Color clock;
    Color clockActive;
    Color clockActiveText;
    Color good;
    Color danger;

    Color light;
    Color dark;
    Color lastMove;
    Color selected;
};

struct BoardPalette {
    std::string id;
    std::string name;
    Color light, dark, lastMove;
};

const std::vector<Theme>& themes();
const Theme& themeById(const std::string& id);

const std::vector<BoardPalette>& boardPalettes();
Theme withBoardPalette(const Theme& t, const std::string& paletteId);

Color fade(Color c, float alpha);
Color mix(Color a, Color b, float t);

}
