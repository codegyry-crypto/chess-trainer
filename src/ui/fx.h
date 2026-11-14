#pragma once
#include <raylib.h>

#include <string>

namespace fx {
enum class Kind { Brilliant, Great, Solved, Streak, Achievement };

void setEnabled(bool on);
void celebrate(Kind kind, Vector2 at, const std::string& label = {});
void draw();

}
