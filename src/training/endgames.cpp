#include "training/endgames.h"

namespace training {

const std::vector<Endgame>& endgames() {
    static const std::vector<Endgame> list = {
        {"queen", "Мат ферзём", "Ставь ферзя на ход коня от чужого короля — так его клетка сужается. "
         "Прижми короля к краю, не дай пата и подведи своего короля.", "8/8/8/3k4/8/8/7Q/1K6 w - - 0 1", Goal::Mate, 14},
        {"rook", "Мат ладьёй", "Ладья отрезает короля по линии, свой король встаёт в оппозицию, "
         "шах ладьёй сдвигает чужого короля на линию к краю.", "8/8/8/4k3/8/8/8/R3K3 w - - 0 1", Goal::Mate, 25},
        {"opposition", "Король и пешка: оппозиция", "Король идёт впереди пешки. Займи оппозицию — встань напротив "
         "чужого короля через клетку, чтобы он уступил дорогу.", "8/8/4k3/8/3K4/4P3/8/8 w - - 0 1", Goal::Promote, 16},
        {"opposition-def", "Защита: король против пешки", "Встань в оппозицию к чужому королю и не отходи от пешки в сторону. "
         "Отступай прямо назад, к полю превращения.", "8/8/4k3/8/3K4/4P3/8/8 b - - 0 1", Goal::Draw, 15},
        {"lucena", "Позиция Лусены", "Король спрятан перед пешкой, но вылезти не даёт ладья с шахами. "
         "Построй «мост»: ладья на четвёртую горизонталь закроет короля от шахов.",
         "3K4/3P1k2/8/8/8/8/2r5/4R3 w - - 0 1", Goal::Promote, 12},
        {"philidor", "Позиция Филидора", "Держи ладью на шестой горизонтали, не пуская короля. Как только пешка "
         "пойдёт вперёд — уводи ладью назад и шахуй сзади.", "4k3/R7/7r/3KP3/8/8/8/8 b - - 0 1", Goal::Draw, 15},
        {"queen-pawn", "Ферзь против пешки", "Шахами и связками заставь короля встать перед пешкой, "
         "в это время подтягивай короля. Повтори, пока пешка не падёт.", "7K/8/8/8/8/8/2kp4/6Q1 w - - 0 1", Goal::Mate, 35},
        {"breakthrough", "Пешечный прорыв", "Три пешки против трёх: удар по центральной пешке сопернику "
         "не остановить — одна пешка прорвётся в ферзи.", "7k/ppp5/8/PPP5/8/8/8/7K w - - 0 1", Goal::Promote, 6},
    };
    return list;
}

std::string goalText(const Endgame& e) {
    const std::string n = std::to_string(e.moves);
    if (e.goal == Goal::Mate) return "Поставь мат за " + n + " ходов";
    if (e.goal == Goal::Promote) return "Проведи пешку в ферзи за " + n + " ходов";
    return "Удержи ничью " + n + " ходов";
}

Verdict judge(Goal goal, int limit, int played, int score, bool promoted) {
    if (goal == Goal::Draw) return score < -300 ? Verdict::Lost : played >= limit ? Verdict::Won : Verdict::Continue;
    if (score < 300) return Verdict::Lost;
    if (goal == Goal::Promote && promoted) return Verdict::Won;
    return played >= limit ? Verdict::Lost : Verdict::Continue;
}

}
