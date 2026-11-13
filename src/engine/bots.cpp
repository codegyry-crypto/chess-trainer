#include "engine/bots.h"

#include "core/notation.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <sstream>

namespace engine {
using namespace chess;

namespace {
std::vector<BotProfile> makeBots() {
    std::vector<BotProfile> b;
    EvalParams normal;

    b.push_back({"novice", "Новичок", 400, "Часто зевает фигуры",
                 "Только учится. Видит ход вперёд и регулярно оставляет фигуры под боем.", 0x8fbf6a,
                 false, 1, 0, false, 220, 0.30, false, normal});
    b.push_back({"student", "Ученик", 700, "Видит взятия, не видит вилок",
                 "Забирает то, что плохо лежит, но не считает ответ соперника до конца.", 0x5fb3a8,
                 false, 2, 0, false, 110, 0.12, false, normal});
    b.push_back({"amateur", "Любитель", 1000, "Играет нормально, ошибается в тактике",
                 "Знает дебютные принципы и досчитывает размены, но комбинации пропускает.", 0x5b8fd9,
                 false, 2, 0, true, 60, 0.05, true, normal});

    EvalParams aggressive;
    aggressive.kingAttack = 280;
    aggressive.kingSafety = 60;
    aggressive.mobility = 130;
    b.push_back({"aggressor", "Агрессор", 1200, "Лезет в атаку на короля",
                 "Подводит фигуры к твоему королю и не жалеет пешек. Держи оборону!", 0xe0603c,
                 false, 3, 0, true, 45, 0.04, true, aggressive});

    EvalParams positional;
    positional.mobility = 150;
    positional.pawnStructure = 160;
    positional.center = 140;
    positional.kingAttack = 70;
    positional.tradeWhenAhead = 50;
    b.push_back({"positional", "Позиционщик", 1400, "Медленно давит, любит размены",
                 "Улучшает фигуры, бьёт по слабым пешкам, а получив перевес — меняется.", 0x9a6fd6,
                 false, 4, 0, true, 15, 0.01, true, positional});

    EvalParams tactical;
    tactical.kingAttack = 150;
    b.push_back({"tactician", "Тактик", 1600, "Ищет комбинации",
                 "Считает варианты и наказывает за незащищённые фигуры.", 0xe2a33a,
                 true, 5, 600, true, 0, 0.0, true, tactical});
    b.push_back({"club", "Клубный", 1800, "Крепкий универсал",
                 "Играет как сильный клубный шахматист: без явных слабостей.", 0x3f9b5a,
                 true, 7, 1200, true, 0, 0.0, true, normal});
    b.push_back({"candidate", "Кандидат", 2000, "Максимум нашего движка",
                 "Думает дольше всех и играет в полную силу.", 0xc9435b,
                 true, 64, 2500, true, 0, 0.0, true, normal});
    b.push_back({"master", "Мастер", 2200, "Stockfish: уровень мастера",
                 "Крепко играет все стадии партии и редко ошибается.", 0x2f7fd0,
                 true, 64, 2500, true, 0, 0.0, true, normal, 2200});
    b.push_back({"grandmaster", "Гроссмейстер", 2600, "Stockfish почти в полную силу",
                 "Сильнейший соперник. Ничья с ним — уже успех!", 0x6b4bd6,
                 true, 64, 2500, true, 0, 0.0, true, normal, 2600});
    return b;
}

const char* const BookLines[] = {
    "e4 e5 Nf3 Nc6 Bc4 Bc5 c3 Nf6 d3 d6",
    "e4 e5 Nf3 Nc6 Bc4 Nf6 d3 Be7 O-O O-O",
    "e4 e5 Nf3 Nc6 Bb5 a6 Ba4 Nf6 O-O Be7 Re1 b5 Bb3 d6",
    "e4 e5 Nf3 Nc6 Bb5 Nf6 O-O Nxe4 d4 Nd6",
    "e4 e5 Nf3 Nc6 d4 exd4 Nxd4 Nf6 Nxc6 bxc6",
    "e4 e5 Nf3 Nc6 Nc3 Nf6 d4 exd4 Nxd4 Bb4",
    "e4 e5 Nf3 Nf6 Nxe5 d6 Nf3 Nxe4 d4 d5",
    "e4 e5 Nf3 d6 d4 Nf6 Nc3 Nbd7",
    "e4 c5 Nf3 d6 d4 cxd4 Nxd4 Nf6 Nc3 a6",
    "e4 c5 Nf3 Nc6 d4 cxd4 Nxd4 Nf6 Nc3 e5",
    "e4 c5 Nf3 e6 d4 cxd4 Nxd4 Nc6 Nc3 Qc7",
    "e4 c5 Nc3 Nc6 g3 g6 Bg2 Bg7",
    "e4 e6 d4 d5 Nc3 Nf6 Bg5 Be7 e5 Nfd7",
    "e4 e6 d4 d5 e5 c5 c3 Nc6 Nf3 Qb6",
    "e4 c6 d4 d5 Nc3 dxe4 Nxe4 Bf5 Ng3 Bg6",
    "e4 c6 d4 d5 e5 Bf5 Nf3 e6 Be2 c5",
    "e4 d5 exd5 Qxd5 Nc3 Qa5 d4 Nf6 Nf3 c6",
    "e4 g6 d4 Bg7 Nc3 d6 Nf3 Nf6",
    "e4 Nf6 e5 Nd5 d4 d6 Nf3 Bg4",
    "d4 d5 c4 e6 Nc3 Nf6 Bg5 Be7 e3 O-O",
    "d4 d5 c4 c6 Nf3 Nf6 Nc3 dxc4 a4 Bf5",
    "d4 d5 c4 dxc4 Nf3 Nf6 e3 e6 Bxc4 c5",
    "d4 Nf6 c4 e6 Nc3 Bb4 e3 O-O Bd3 d5",
    "d4 Nf6 c4 g6 Nc3 Bg7 e4 d6 Nf3 O-O",
    "d4 Nf6 c4 e6 Nf3 b6 g3 Bb7 Bg2 Be7",
    "d4 d5 Nf3 Nf6 Bf4 e6 e3 c5 c3 Nc6",
    "d4 d5 Bf4 Nf6 e3 c5 c3 Nc6 Nd2 e6",
    "c4 e5 Nc3 Nf6 Nf3 Nc6 g3 d5 cxd5 Nxd5",
    "c4 Nf6 Nc3 e6 e4 d5 e5 d4",
    "Nf3 d5 g3 Nf6 Bg2 c6 O-O Bg4",
};

struct Book {
    std::vector<std::vector<Move>> lines;
    std::string error;
    Book() {
        for (const char* line : BookLines) {
            Position p;
            std::vector<Move> moves;
            std::istringstream in(line);
            std::string san;
            while (in >> san) {
                const Move m = parseSAN(p, san);
                if (m.isNone()) {
                    error += std::string("ход ") + san + " в линии \"" + line + "\"; ";
                    break;
                }
                moves.push_back(m);
                p.makeMove(m);
            }
            lines.push_back(std::move(moves));
        }
    }
};
const Book& book() {
    static const Book b;
    return b;
}

}

const std::vector<BotProfile>& bots() {
    static const std::vector<BotProfile> b = makeBots();
    return b;
}

const BotProfile* botById(const std::string& id) {
    for (const BotProfile& b : bots())
        if (b.id == id) return &b;
    return nullptr;
}

BotProfile strengthBot(int elo) {
    const BotProfile *lo = nullptr, *hi = nullptr;
    for (const BotProfile& b : bots()) {
        if (b.stockfishElo > 0) continue;
        if (b.rating <= elo) lo = &b;
        if (b.rating >= elo && !hi) hi = &b;
    }
    if (!lo) lo = hi;
    if (!hi) hi = lo;
    BotProfile r = *lo;
    r.rating = std::clamp(elo, bots().front().rating, hi->rating);
    if (hi == lo) return r;
    const double t = double(elo - lo->rating) / (hi->rating - lo->rating);
    auto lerp = [t](double a, double b) { return a + (b - a) * t; };
    r.noise = int(std::lround(lerp(lo->noise, hi->noise)));
    r.blunderChance = lerp(lo->blunderChance, hi->blunderChance);
    r.depth = int(std::lround(lerp(lo->depth, hi->depth)));
    if (lo->fullSearch == hi->fullSearch) r.timeMs = int(std::lround(lerp(lo->timeMs, hi->timeMs)));
    return r;
}

BotProfile pickOpponent(int target, bool calibrating, bool stockfish, std::mt19937& rng) {
    const int elo = (calibrating ? target : target + int(rng() % 201) - 100) / 10 * 10;
    if (stockfish && elo > 2000) {
        BotProfile sf = *botById("master");
        sf.id = "stockfish";
        sf.name = "Stockfish";
        sf.rating = sf.stockfishElo = elo;
        sf.style = "Подстраивается под твою силу";
        sf.desc = "Stockfish с ограниченной силой под твой рейтинг.";
        return sf;
    }
    static const std::pair<const char*, const char*> styles[] = {
        {"aggressor", "Агрессор"}, {"positional", "Позиционщик"}, {"tactician", "Тактик"}, {"club", "Универсал"}};
    const auto& [id, name] = styles[rng() % std::size(styles)];
    const BotProfile& s = *botById(id);
    BotProfile r = strengthBot(elo);
    r.id = s.id;
    r.name = name;
    r.style = s.style;
    r.desc = s.desc;
    r.color = s.color;
    r.params = s.params;
    return r;
}

std::vector<Move> bookMoves(const std::vector<Move>& gameMoves) {
    std::vector<Move> out;
    for (const auto& line : book().lines) {
        if (line.size() <= gameMoves.size()) continue;
        if (!std::equal(gameMoves.begin(), gameMoves.end(), line.begin())) continue;
        const Move next = line[gameMoves.size()];
        if (std::find(out.begin(), out.end(), next) == out.end()) out.push_back(next);
    }
    return out;
}

int bookLineCount() { return int(book().lines.size()); }

bool bookIsValid(std::string* error) {
    if (error) *error = book().error;
    return book().error.empty();
}

BotMove chooseMove(const Position& pos, const std::vector<Move>& gameMoves, const BotProfile& bot, Searcher& searcher,
                   std::mt19937& rng, int timeBudgetMs, std::atomic<bool>* stop) {
    BotMove result;
    Position p = pos;
    const std::vector<Move> legal = p.legalMoves();
    if (legal.empty()) return result;

    if (bot.useBook) {
        const std::vector<Move> candidates = bookMoves(gameMoves);
        if (!candidates.empty()) {
            std::uniform_int_distribution<size_t> pick(0, candidates.size() - 1);
            const Move m = candidates[pick(rng)];
            for (const Move& l : legal)
                if (l == m) {
                    result.move = l;
                    result.fromBook = true;
                    return result;
                }
        }
    }

    if (bot.fullSearch) {
        Limits lim;
        lim.maxDepth = bot.depth;
        lim.timeMs = bot.timeMs;
        if (timeBudgetMs > 0) lim.timeMs = std::max(50, std::min(lim.timeMs, timeBudgetMs));
        lim.quiescence = bot.quiescence;
        const Result r = searcher.search(p, lim, bot.params, stop);
        result.move = r.best;
        result.score = r.score;
        result.depth = r.depth;
        return result;
    }

    Limits lim;
    lim.timeMs = timeBudgetMs > 0 ? std::max(50, std::min(1500, timeBudgetMs)) : 1500;
    lim.quiescence = bot.quiescence;
    std::vector<RootMove> scored = searcher.scoreRootMoves(p, bot.depth, lim, bot.params, stop);
    if (scored.empty()) {
        result.move = legal[0];
        return result;
    }
    result.depth = bot.depth;

    if (bot.id != "novice" && scored[0].score > MateBound) {
        result.move = scored[0].move;
        result.score = scored[0].score;
        return result;
    }

    std::uniform_real_distribution<double> coin(0, 1);
    if (coin(rng) < bot.blunderChance) {
        size_t pool = scored.size();
        if (bot.id != "novice") pool = std::max<size_t>(1, (pool + 1) / 2);
        std::uniform_int_distribution<size_t> pick(0, pool - 1);
        const RootMove& rm = scored[pick(rng)];
        result.move = rm.move;
        result.score = rm.score;
        return result;
    }

    std::normal_distribution<double> noise(0.0, double(bot.noise));
    double bestScore = -1e9;
    for (const RootMove& rm : scored) {
        const double s = rm.score + (std::abs(rm.score) > MateBound ? 0.0 : noise(rng));
        if (s > bestScore) {
            bestScore = s;
            result.move = rm.move;
            result.score = rm.score;
        }
    }
    return result;
}

}
