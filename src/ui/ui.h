#pragma once
#include "ui/theme.h"

#include <raylib.h>

#include <string>

namespace ui {
enum class Style { Primary, Secondary, Ghost };

void init();
void shutdown();

void setTheme(const Theme& t);
const Theme& theme();

float scale();
float screenW();
float screenH();
Vector2 mouse();
void beginScaled();
void endScaled();
void beginClip(Rectangle r);
void endClip();

void beginFrame();
void endFrame();

void setInputEnabled(bool on);
bool inputEnabled();

void text(const std::string& s, Vector2 pos, float size, Color c, bool bold = false);
Vector2 measure(const std::string& s, float size, bool bold = false);
void textCentered(const std::string& s, Rectangle r, float size, Color c, bool bold = false);
float textWrapped(const std::string& s, Rectangle r, float size, Color c, bool bold = false, float lineGap = 1.35f);

void roundRect(Rectangle r, float radius, Color c);
void roundRectLines(Rectangle r, float radius, float thick, Color c);
void card(Rectangle r, float radius = 12);

bool hovered(Rectangle r);
bool clicked(Rectangle r);
void setHandCursor();

bool button(Rectangle r, const std::string& label, Style style = Style::Secondary, bool enabled = true,
            float fontSize = 17);
bool chip(Rectangle r, const std::string& label, bool selected, bool enabled = true, float fontSize = 16);
bool toggle(Rectangle r, const std::string& label, bool& value);
bool slider(Rectangle r, float& value);
void badge(Vector2 pos, const std::string& label, Color bg, Color fg, float size = 13);
void streakFlame(Vector2 topRight, int days, float size);

void toast(const std::string& message);
void drawToast();

}
