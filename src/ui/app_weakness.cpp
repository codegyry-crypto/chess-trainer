#include "ui/app.h"

#include "core/pgn.h"
#include "ui/ui.h"

#include <raylib.h>

#include <algorithm>
#include <cmath>

namespace ui {
namespace {
constexpr float M = 24;
constexpr int WeakGames = 100;

std::string weakKey(int64_t id) { return "weak:" + std::to_string(id); }
}

int App::addMistakePuzzles(const analysis::GameMistakes& g) {
    int added = 0;
    for (const analysis::Mistake& m : g.mistakes) {
        if (!m.puzzle) continue;
        const std::string theme = analysis::kindTheme(m.kind);
        profile_.srsAdd({analysis::mistakeKey(m), m.fen, m.prevUci + " " + m.bestUci,
                         theme.empty() ? "mine" : "mine " + theme, "mine", profile_.puzzleRating()},
                        storage::Profile::today() - 1);
        ++added;
    }
    return added;
}

void App::pollWeakness() {
    if (stockfishExe_.empty() || !profile_.isOpen() || weakFailed_) return;
    if (weakGameId_) {
        if (weakAnalyzer_.running()) return;
        if (!weakAnalyzer_.error().empty()) {
            weakFailed_ = true;
            return;
        }
        const auto g = analysis::findMistakes(weakAnalyzer_.fen(), weakAnalyzer_.moves(), weakAnalyzer_.snapshot(), weakColor_);
        profile_.setValue(weakKey(weakGameId_), analysis::encodeMistakes(g));
        addMistakePuzzles(g);
        weakGameId_ = 0;
        weakDirty_ = true;
    }
    if (screen_ == Screen::Game || GetTime() < weakNextCheck_) return;
    weakNextCheck_ = GetTime() + 3;
    for (const storage::GameRecord& r : profile_.recentGames(WeakGames)) {
        if (!profile_.value(weakKey(r.id)).empty()) continue;
        std::string err;
        const auto games = chess::parsePGN(r.pgn, &err);
        if (games.empty() || games[0].moves.empty()) {
            profile_.setValue(weakKey(r.id), "0 0 0\n");
            continue;
        }
        weakGameId_ = r.id;
        weakColor_ = r.color == "w" ? chess::White : chess::Black;
        weakAnalyzer_.start(stockfishExe_, games[0].startFen, games[0].moves, 150, 1);
        return;
    }
}

const analysis::WeaknessReport& App::weaknessReport() {
    if (!weakDirty_) return weakReport_;
    weakDirty_ = false;
    weakReport_ = {};
    weakPending_ = 0;
    for (const storage::GameRecord& r : profile_.recentGames(WeakGames)) {
        const std::string v = profile_.value(weakKey(r.id));
        if (v.empty()) {
            ++weakPending_;
            continue;
        }
        const auto g = analysis::decodeMistakes(v);
        if (g.moves[0] + g.moves[1] + g.moves[2] > 0) analysis::addToReport(weakReport_, g);
    }
    return weakReport_;
}

void App::drawProfileWeakness(float top) {
    const Theme& t = theme();
    const float W = (float)screenW(), x0 = M + 16, inner = W - 2 * x0;
    const analysis::WeaknessReport& r = weaknessReport();

    const Rectangle hc{x0, top, inner, 100};
    card(hc);
    std::string sub = "По " + std::to_string(r.games) + " разобранным партиям · ошибок и зевков: " +
                      std::to_string(r.errors) + " (неточности не считаются)";
    if (weakPending_) sub += " · в очереди на разбор: " + std::to_string(weakPending_);
    if (stockfishExe_.empty()) sub = "Нужен Stockfish: без него партии не разбираются";
    text(sub, {hc.x + 20, hc.y + 16}, 15, t.sub);
    std::string phrase = analysis::weaknessPhrase(r);
    if (phrase.empty())
        phrase = r.games ? "В разобранных партиях ошибок не нашлось — так держать!"
                         : weakPending_ ? "Разбираю твои партии в фоне — отчёт появится через минуту."
                                        : "Сыграй партию с ботом — она разберётся в фоне, и здесь появится отчёт.";
    textWrapped(phrase, {hc.x + 20, hc.y + 44, hc.width - 40, 64}, 20, t.text, true);

    struct Bar {
        std::string label;
        int n;
    };
    auto barsCard = [&](Rectangle c, const char* title, const std::vector<Bar>& bars) {
        card(c);
        text(title, {c.x + 20, c.y + 14}, 16, t.sub);
        int hi = 1;
        for (const Bar& b : bars) hi = std::max(hi, b.n);
        const float rowH = std::min(40.0f, (c.height - 52) / bars.size());
        for (size_t i = 0; i < bars.size(); ++i) {
            const float y = c.y + 46 + i * rowH;
            const std::string n = std::to_string(bars[i].n);
            text(bars[i].label, {c.x + 20, y}, 15, t.text);
            text(n, {c.x + c.width - 20 - measure(n, 15, true).x, y}, 15, bars[i].n == hi ? t.danger : t.sub, true);
            const Rectangle br{c.x + 20, y + 22, c.width - 40, 7};
            roundRect(br, 3.5f, mix(t.bg, t.text, 0.08f));
            if (bars[i].n)
                roundRect({br.x, br.y, std::max(br.height, br.width * bars[i].n / hi), br.height}, 3.5f,
                          bars[i].n == hi ? t.danger : fade(t.accent, 0.7f));
        }
    };

    const float gap = 16, row = top + 116, cw = (inner - 3 * gap) / 4, ch = 300;
    std::vector<Bar> phases, kinds, pieces, buckets;
    for (int p = 0; p < 3; ++p) phases.push_back({analysis::phaseName(p), r.phase[p]});
    for (int k = 0; k < analysis::ErrorKindCount; ++k)
        kinds.push_back({analysis::errorKindName(analysis::ErrorKind(k)), r.kind[k]});
    const char* pieceNames[] = {"Пешка", "Конь", "Слон", "Ладья", "Ферзь", "Король"};
    for (int p = 1; p <= 6; ++p) pieces.push_back({pieceNames[p - 1], r.piece[p]});
    for (int b = 0; b < analysis::MoveBuckets; ++b)
        buckets.push_back({std::string(analysis::bucketName(b)) + " ход", r.bucket[b]});
    barsCard({x0, row, cw, ch}, "Фаза партии", phases);
    barsCard({x0 + (cw + gap), row, cw, ch}, "Тип ошибки", kinds);
    barsCard({x0 + 2 * (cw + gap), row, cw, ch}, "Чем ходил", pieces);
    barsCard({x0 + 3 * (cw + gap), row, cw, ch}, "Номер хода", buckets);

    const Rectangle fc{x0, row + ch + gap, inner, 76};
    card(fc);
    const size_t mine = profile_.srsItems(storage::Profile::today(), false, "mine").size();
    text("Позиции, где ты ошибся, сами попадают в «Задачи → Мои ошибки». Сейчас там: " + std::to_string(mine),
         {fc.x + 20, fc.y + 28}, 16, t.text);
    const std::string weak = analysis::weakestTheme(r);
    float bx = fc.x + fc.width - 20;
    if (!weak.empty()) {
        const std::string lbl = std::string("Задачи: ") + training::themeName(weak);
        const float w = measure(lbl, 16).x + 40;
        bx -= w;
        if (button({bx, fc.y + 16, w, 44}, lbl, Style::Secondary, true, 16)) {
            puzzleTheme_ = weak;
            openPuzzles(PuzzleMode::Theme);
        }
        bx -= 12;
    }
    if (button({bx - 180, fc.y + 16, 180, 44}, "Мои ошибки", Style::Primary, mine > 0, 16)) openPuzzles(PuzzleMode::Mine);
}
}
