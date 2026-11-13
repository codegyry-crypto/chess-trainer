#include "ui/grades.h"

#include "ui/ui.h"

#include <algorithm>
#include <cmath>

namespace ui {
using analysis::Grade;

namespace {
void tri(Vector2 a, Vector2 b, Vector2 c, Color col) {
    DrawTriangle(a, b, c, col);
    DrawTriangle(a, c, b, col);
}

void quad(Vector2 a, Vector2 b, Vector2 c, Vector2 d, Color col) {
    tri(a, b, c, col);
    tri(a, c, d, col);
}

void line(Vector2 c, float r, float x0, float y0, float x1, float y1) {
    DrawLineEx({c.x + x0 * r, c.y + y0 * r}, {c.x + x1 * r, c.y + y1 * r}, 0.22f * r, WHITE);
    DrawCircleV({c.x + x1 * r, c.y + y1 * r}, 0.11f * r, WHITE);
}

float easeOutBack(float t) {
    const float k = 1.9f, u = t - 1;
    return 1 + (k + 1) * u * u * u + k * u * u;
}
}

Color gradeColor(Grade g) {
    switch (g) {
    case Grade::Brilliant: return hex(0x26c2a3);
    case Grade::Great: return hex(0x5b8bd0);
    case Grade::Best: return hex(0x81b64c);
    case Grade::Excellent: return hex(0x81b64c);
    case Grade::Good: return hex(0x95b776);
    case Grade::Book: return hex(0xa88764);
    case Grade::Inaccuracy: return hex(0xf7c631);
    case Grade::Mistake: return hex(0xffa459);
    case Grade::Miss: return hex(0xff7769);
    case Grade::Blunder: return hex(0xfa412d);
    case Grade::Forced: return hex(0x97a59a);
    default: return theme().sub;
    }
}

bool isBadGrade(Grade g) {
    return g == Grade::Inaccuracy || g == Grade::Mistake || g == Grade::Miss || g == Grade::Blunder;
}

void drawGradeIcon(Vector2 c, float r, Grade g) {
    DrawCircleV(c, r, gradeColor(g));
    const Rectangle box{c.x - r, c.y - r, 2 * r, 2 * r};
    switch (g) {
    case Grade::Best: {
        Vector2 p[10];
        for (int k = 0; k < 10; ++k) {
            const float a = -PI / 2 + k * PI / 5, rad = (k % 2 ? 0.28f : 0.66f) * r;
            p[k] = {c.x + std::cos(a) * rad, c.y + std::sin(a) * rad};
        }
        for (int k = 0; k < 10; ++k) tri(c, p[k], p[(k + 1) % 10], WHITE);
        break;
    }
    case Grade::Excellent:
        DrawRectangleRec({c.x - 0.56f * r, c.y - 0.06f * r, 0.22f * r, 0.6f * r}, WHITE);
        DrawRectangleRounded({c.x - 0.27f * r, c.y - 0.14f * r, 0.78f * r, 0.68f * r}, 0.4f, 6, WHITE);
        DrawRectangleRounded({c.x - 0.2f * r, c.y - 0.64f * r, 0.32f * r, 0.66f * r}, 0.9f, 6, WHITE);
        break;
    case Grade::Good:
        line(c, r, -0.45f, 0.02f, -0.12f, 0.35f);
        line(c, r, -0.12f, 0.35f, 0.48f, -0.33f);
        break;
    case Grade::Book: {
        const Color page = WHITE;
        quad({c.x, c.y - 0.3f * r}, {c.x - 0.62f * r, c.y - 0.44f * r}, {c.x - 0.62f * r, c.y + 0.34f * r},
             {c.x, c.y + 0.48f * r}, page);
        quad({c.x, c.y - 0.3f * r}, {c.x + 0.62f * r, c.y - 0.44f * r}, {c.x + 0.62f * r, c.y + 0.34f * r},
             {c.x, c.y + 0.48f * r}, page);
        DrawLineEx({c.x, c.y - 0.3f * r}, {c.x, c.y + 0.48f * r}, 0.1f * r, gradeColor(g));
        break;
    }
    case Grade::Miss:
        line(c, r, -0.36f, -0.36f, 0.36f, 0.36f);
        line(c, r, -0.36f, 0.36f, 0.36f, -0.36f);
        break;
    case Grade::Forced:
        DrawLineEx({c.x - 0.5f * r, c.y}, {c.x + 0.1f * r, c.y}, 0.22f * r, WHITE);
        tri({c.x + 0.55f * r, c.y}, {c.x + 0.02f * r, c.y - 0.42f * r}, {c.x + 0.02f * r, c.y + 0.42f * r}, WHITE);
        break;
    case Grade::None: break;
    default: {
        const std::string s = analysis::gradeSymbol(g);
        textCentered(s, box, (s.size() > 1 ? 1.4f : 1.75f) * r, WHITE, true);
    }
    }
}

bool drawSquareGrade(Rectangle sq, int index, Grade g, GradeAnim& anim) {
    if (g == Grade::None) {
        anim.index = -1;
        return false;
    }
    const double now = GetTime();
    const bool fresh = anim.index != index || anim.grade != g;
    if (fresh) anim = {index, g, now};
    const float age = float(now - anim.born);
    const Color col = gradeColor(g);
    const Vector2 mid{sq.x + sq.width / 2, sq.y + sq.height / 2};
    if (g == Grade::Brilliant || g == Grade::Great) {
        const int waves = g == Grade::Brilliant ? 3 : 1;
        for (int w = 0; w < waves; ++w) {
            const float t = (age - w * 0.22f) / 0.9f;
            if (t < 0 || t > 1) continue;
            const float rr = sq.width * (0.3f + 0.75f * t);
            DrawRing(mid, rr - sq.width * 0.06f * (1 - t) - 2, rr, 0, 360, 48, fade(col, 0.85f * (1 - t)));
        }
        const float flash = std::max(0.0f, 1 - age / 1.1f);
        DrawRectangleRec(sq, fade(col, 0.5f * flash));
    }
    const float r = sq.width * 0.2f;
    const float s = r * easeOutBack(std::clamp(age / 0.3f, 0.0f, 1.0f));
    const Vector2 c{sq.x + sq.width - 0.35f * r, sq.y + 0.35f * r};
    if (g == Grade::Brilliant)
        DrawCircleV(c, s * (1.45f + 0.12f * std::sin(age * 4)), fade(col, 0.35f));
    DrawCircleV({c.x, c.y + 0.08f * r}, s + 1.5f, Color{0, 0, 0, 90});
    if (s > 1) drawGradeIcon(c, s, g);
    return fresh;
}

}
