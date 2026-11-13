#include "core/pgn.h"

#include "core/notation.h"

#include <cctype>

namespace chess {
std::string Game::tag(const std::string& name) const {
    for (const auto& [k, v] : tags)
        if (k == name) return v;
    return "";
}

void Game::setTag(const std::string& name, const std::string& value) {
    for (auto& [k, v] : tags) {
        if (k == name) {
            v = value;
            return;
        }
    }
    tags.emplace_back(name, value);
}

Position Game::startPosition() const {
    Position p;
    if (!startFen.empty()) p.setFEN(startFen);
    return p;
}

Position Game::positionAt(size_t ply) const {
    Position p = startPosition();
    for (size_t i = 0; i < ply && i < moves.size(); ++i) p.makeMove(moves[i]);
    return p;
}

namespace {
std::string escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

bool isResult(const std::string& t) {
    return t == "1-0" || t == "0-1" || t == "1/2-1/2" || t == "*";
}

}

std::string toPGN(const Game& game) {
    std::string out;
    const char* roster[] = {"Event", "Site", "Date", "Round", "White", "Black", "Result"};
    const char* defaults[] = {"?", "?", "????.??.??", "?", "?", "?", "*"};
    for (int i = 0; i < 7; ++i) {
        std::string v = i == 6 ? game.result : game.tag(roster[i]);
        if (v.empty()) v = defaults[i];
        out += "[" + std::string(roster[i]) + " \"" + escape(v) + "\"]\n";
    }
    for (const auto& [k, v] : game.tags) {
        bool inRoster = false;
        for (const char* r : roster) inRoster |= (k == r);
        if (inRoster || k == "FEN" || k == "SetUp") continue;
        out += "[" + k + " \"" + escape(v) + "\"]\n";
    }
    if (!game.startFen.empty()) {
        out += "[SetUp \"1\"]\n";
        out += "[FEN \"" + escape(game.startFen) + "\"]\n";
    }
    out += '\n';

    Position pos = game.startPosition();
    std::string line;
    auto emit = [&](const std::string& token) {
        if (!line.empty() && line.size() + 1 + token.size() > 80) {
            out += line + '\n';
            line.clear();
        }
        if (!line.empty()) line += ' ';
        line += token;
    };
    for (size_t i = 0; i < game.moves.size(); ++i) {
        const Move m = game.moves[i];
        if (pos.sideToMove() == White) emit(std::to_string(pos.fullmoveNumber()) + ".");
        else if (i == 0) emit(std::to_string(pos.fullmoveNumber()) + "...");
        emit(toSAN(pos, m));
        pos.makeMove(m);
    }
    emit(game.result);
    out += line + "\n";
    return out;
}

std::vector<Game> parsePGN(const std::string& text, std::string* error) {
    std::vector<Game> games;
    Game game;
    Position pos;
    bool inGame = false;
    bool hasMoves = false;

    auto finish = [&] {
        if (inGame) games.push_back(std::move(game));
        game = Game();
        pos = Position();
        inGame = hasMoves = false;
    };
    auto fail = [&](const std::string& msg) {
        if (error) *error = "Партия " + std::to_string(games.size() + 1) + ": " + msg;
        return games;
    };

    size_t i = 0;
    const size_t n = text.size();
    while (i < n) {
        const char c = text[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
        } else if (c == '[') {
            if (hasMoves) finish();
            inGame = true;
            const size_t end = text.find(']', i);
            if (end == std::string::npos) return fail("незакрытый тег");
            std::string body = text.substr(i + 1, end - i - 1);
            const size_t q1 = body.find('"');
            const size_t q2 = body.rfind('"');
            if (q1 == std::string::npos || q2 == q1) return fail("неверный тег");
            std::string name = body.substr(0, q1);
            while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();
            std::string value;
            for (size_t k = q1 + 1; k < q2; ++k) {
                if (body[k] == '\\' && k + 1 < q2) ++k;
                value += body[k];
            }
            if (name == "FEN") {
                std::string err;
                if (!pos.setFEN(value, &err)) return fail(err);
                game.startFen = value;
            } else if (name != "SetUp") {
                game.setTag(name, value);
            }
            i = end + 1;
        } else if (c == '{') {
            const size_t end = text.find('}', i);
            i = end == std::string::npos ? n : end + 1;
        } else if (c == ';' || (c == '%' && (i == 0 || text[i - 1] == '\n'))) {
            const size_t end = text.find('\n', i);
            i = end == std::string::npos ? n : end + 1;
        } else if (c == '(') {
            int depth = 0;
            for (; i < n; ++i) {
                if (text[i] == '{') {
                    const size_t end = text.find('}', i);
                    i = end == std::string::npos ? n - 1 : end;
                } else if (text[i] == '(') ++depth;
                else if (text[i] == ')' && --depth == 0) break;
            }
            ++i;
        } else {
            size_t j = i;
            while (j < n && !std::isspace(static_cast<unsigned char>(text[j])) && text[j] != '{' &&
                   text[j] != '(' && text[j] != ')' && text[j] != '[' && text[j] != ';')
                ++j;
            std::string token = text.substr(i, j - i);
            i = j;
            if (token.empty()) {
                ++i;
                continue;
            }
            inGame = true;
            if (isResult(token)) {
                game.result = token;
                game.setTag("Result", token);
                finish();
                continue;
            }
            if (token[0] == '$') continue;
            size_t k = 0;
            while (k < token.size() && std::isdigit(static_cast<unsigned char>(token[k]))) ++k;
            if (k > 0 && k < token.size() && token[k] == '.') {
                while (k < token.size() && token[k] == '.') ++k;
                token = token.substr(k);
                if (token.empty()) continue;
            } else if (k == token.size()) {
                continue;
            }
            const Move m = parseSAN(pos, token);
            if (m.isNone()) return fail("неверный ход \"" + token + "\"");
            pos.makeMove(m);
            game.moves.push_back(m);
            hasMoves = true;
        }
    }
    if (inGame) {
        if (game.result == "*" && !game.tag("Result").empty()) game.result = game.tag("Result");
        finish();
    }
    return games;
}

}
