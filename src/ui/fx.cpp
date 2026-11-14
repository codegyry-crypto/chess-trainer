#include "ui/fx.h"

#include "ui/grades.h"
#include "ui/sound.h"
#include "ui/ui.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace fx {
namespace {
struct Pop {
    Kind kind;
    Vector2 at;
    std::string label;
    double born;
};
bool enabled = true;
std::vector<Pop> pops;
constexpr float Life = 1.7f;

analysis::Grade iconOf(Kind k) {
    switch (k) {
    case Kind::Brilliant: return analysis::Grade::Brilliant;
    case Kind::Great: return analysis::Grade::Great;
    case Kind::Solved: return analysis::Grade::Good;
    default: return analysis::Grade::Best;
    }
}
}

void setEnabled(bool on) { enabled = on; }

void celebrate(Kind kind, Vector2 at, const std::string& label) {
    const sound::Id ids[] = {sound::Id::Brilliant, sound::Id::Great, sound::Id::Solved, sound::Id::Streak,
                             sound::Id::Achievement};
    sound::play(ids[int(kind)]);
    if (!enabled) return;
    std::erase_if(pops, [&](const Pop& p) { return p.kind == kind; });
    pops.push_back({kind, at, label, GetTime()});
}

void draw() {
    const double now = GetTime();
    std::erase_if(pops, [&](const Pop& p) { return now - p.born > Life; });
    for (const Pop& p : pops) {
        const float a = float(now - p.born);
        const float in = std::clamp(a / 0.3f, 0.0f, 1.0f);
        const float grow = 1 + 2.7f * (in - 1) * (in - 1) * (in - 1) + 1.7f * (in - 1) * (in - 1);
        const float out = std::clamp((Life - a) / 0.35f, 0.0f, 1.0f);
        const float r = (p.kind == Kind::Achievement ? 30.0f : 24.0f) * grow * out;
        const Vector2 c{p.at.x, p.at.y - 34 * std::sqrt(a)};
        const Color col = p.kind == Kind::Streak ? Color{255, 122, 26, 255}
                          : p.kind == Kind::Achievement ? Color{240, 184, 40, 255}
                                                        : ui::gradeColor(iconOf(p.kind));
        if (a < 0.7f) {
            const float k = a / 0.7f;
            DrawRing(c, r + 60 * k - 3, r + 60 * k, 0, 360, 48, ui::fade(col, 0.8f * (1 - k)));
            if (p.kind != Kind::Solved)
                for (int i = 0; i < 8; ++i) {
                    const float ang = i * PI / 4 + 0.3f;
                    const float d = r + 14 + 46 * k;
                    DrawCircleV({c.x + std::cos(ang) * d, c.y + std::sin(ang) * d}, 4 * (1 - k), col);
                }
        }
        if (r < 1) continue;
        if (p.kind == Kind::Streak || p.kind == Kind::Achievement) {
            DrawCircleV(c, r, col);
            if (p.kind == Kind::Streak) {
                const float h = r * 1.15f, rr = h * 0.32f;
                const Vector2 tip{c.x, c.y - h * 0.55f}, o{c.x, tip.y + h * 0.68f};
                DrawCircleV(o, rr, WHITE);
                DrawTriangle(tip, {o.x - rr, o.y}, {o.x + rr, o.y}, WHITE);
                DrawCircleV({o.x, o.y + rr * 0.25f}, rr * 0.5f, Color{255, 210, 63, 255});
            } else {
                Vector2 v[10];
                for (int k = 0; k < 10; ++k) {
                    const float ang = -PI / 2 + k * PI / 5, rad = (k % 2 ? 0.28f : 0.66f) * r;
                    v[k] = {c.x + std::cos(ang) * rad, c.y + std::sin(ang) * rad};
                }
                for (int k = 0; k < 10; ++k) DrawTriangle(c, v[(k + 1) % 10], v[k], WHITE);
            }
        } else {
            ui::drawGradeIcon(c, r, iconOf(p.kind));
        }
        if (p.label.empty()) continue;
        const Vector2 m = ui::measure(p.label, 17, true);
        const Rectangle box{c.x - m.x / 2 - 14, c.y + r + 8, m.x + 28, 34};
        ui::roundRect(box, 17, ui::fade(ui::theme().panel2, 0.95f * out));
        ui::roundRectLines(box, 17, 2, ui::fade(col, out));
        ui::textCentered(p.label, box, 17, ui::fade(ui::theme().text, out), true);
    }
}

}
