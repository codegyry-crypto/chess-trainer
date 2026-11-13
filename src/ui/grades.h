#pragma once
#include "analysis/review.h"

#include <raylib.h>

namespace ui {
struct GradeAnim {
    int index = -1;
    analysis::Grade grade = analysis::Grade::None;
    double born = 0;
};

Color gradeColor(analysis::Grade g);
bool isBadGrade(analysis::Grade g);
void drawGradeIcon(Vector2 c, float r, analysis::Grade g);
bool drawSquareGrade(Rectangle sq, int index, analysis::Grade g, GradeAnim& anim);

}
