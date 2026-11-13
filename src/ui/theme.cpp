#include "ui/theme.h"

namespace ui {
namespace {
std::vector<Theme> makeThemes() {
    std::vector<Theme> t;
    t.push_back({
        "night", "Ночь", "Тёмная, сине-серая. Глаза не устают",
        hex(0x161a22), hex(0x1f2430), hex(0x2a3140), hex(0x323a4b),
        hex(0xe6e8ee), hex(0x8b93a7),
        hex(0x3d7bd9), hex(0xffffff),
        hex(0x5a4515), hex(0xf5d48a),
        hex(0x2a3140), hex(0xe6e8ee), hex(0x161a22),
        hex(0x4fc28a), hex(0xe5534b),
        hex(0xc9d3e3), hex(0x5b7299), hex(0xf0c850, 110), hex(0x3d7bd9, 140),
    });
    t.push_back({
        "wood", "Дерево", "Светлая классика, как настоящая доска",
        hex(0xf3ebdd), hex(0xfffaf1), hex(0xefe2cc), hex(0xe0d0b4),
        hex(0x3b2b1a), hex(0x8a7356),
        hex(0x7a4e2a), hex(0xffffff),
        hex(0xf6dd9a), hex(0x5e3d05),
        hex(0xe7d9c1), hex(0x3b2b1a), hex(0xfffaf1),
        hex(0x3b7d1f), hex(0xc0392b),
        hex(0xf0d9b5), hex(0xb58863), hex(0xcdd25a, 150), hex(0x6e8b3d, 150),
    });
    t.push_back({
        "arena", "Арена", "Чёрная с оранжевым, как на FACEIT",
        hex(0x0f0f12), hex(0x1a1a1f), hex(0x26262d), hex(0x2f2f38),
        hex(0xf2f2f2), hex(0x8d8d96),
        hex(0xff5500), hex(0xffffff),
        hex(0x4a2410), hex(0xffb37a),
        hex(0x26262d), hex(0xff5500), hex(0xffffff),
        hex(0x3ed07a), hex(0xff4040),
        hex(0xd9d9de), hex(0x4d4d57), hex(0xff7828, 115), hex(0xff5500, 140),
    });
    return t;
}

}

const std::vector<Theme>& themes() {
    static const std::vector<Theme> t = makeThemes();
    return t;
}

const Theme& themeById(const std::string& id) {
    for (const Theme& t : themes())
        if (t.id == id) return t;
    return themes().front();
}

const std::vector<BoardPalette>& boardPalettes() {
    static const std::vector<BoardPalette> p = {
        {"theme", "Как в теме", {}, {}, {}},
        {"green", "Зелёная", hex(0xeeeed2), hex(0x769656), hex(0xf6f669, 140)},
        {"brown", "Коричневая", hex(0xf0d9b5), hex(0xb58863), hex(0xcdd25a, 150)},
        {"blue", "Синяя", hex(0xdee3e6), hex(0x8ca2ad), hex(0x9bc700, 120)},
        {"gray", "Серая", hex(0xd6d6d6), hex(0x8a8a8a), hex(0xf0c850, 120)},
        {"purple", "Фиолетовая", hex(0xe8e0f5), hex(0x8877b7), hex(0xf0c850, 120)},
    };
    return p;
}

Theme withBoardPalette(const Theme& t, const std::string& paletteId) {
    Theme r = t;
    for (const BoardPalette& p : boardPalettes()) {
        if (p.id != paletteId || p.id == "theme") continue;
        r.light = p.light;
        r.dark = p.dark;
        if (p.lastMove.a) r.lastMove = p.lastMove;
    }
    return r;
}

Color fade(Color c, float alpha) {
    c.a = (unsigned char)(c.a * alpha);
    return c;
}

Color mix(Color a, Color b, float t) {
    auto m = [t](unsigned char x, unsigned char y) { return (unsigned char)(x + (y - x) * t); };
    return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a)};
}

}
