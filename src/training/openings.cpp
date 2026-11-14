#include "training/openings.h"

#include "core/notation.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>

namespace training {

namespace {
std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::istringstream in(s);
    for (std::string part; std::getline(in, part, sep);) out.push_back(trim(part));
    return out;
}

std::string readFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}

std::string customLineTitle(const std::vector<std::string>& moves) {
    std::string s = "Моя ветка:";
    const size_t from = moves.size() > 6 ? moves.size() - 6 : 0;
    for (size_t i = from; i < moves.size(); ++i) {
        if (i % 2 == 0) s += " " + std::to_string(i / 2 + 1) + ".";
        else if (i == from) s += " " + std::to_string(i / 2 + 1) + "...";
        else s += " ";
        s += moves[i];
    }
    return s;
}

std::vector<Repertoire> parseRepertoires(const std::string& text, bool custom) {
    std::vector<Repertoire> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);
        line = trim(line);
        if (line.empty()) continue;
        const std::string rest = trim(line.substr(1));
        if (line[0] == '#') {
            const auto p = split(rest, '|');
            if (p.size() >= 3) out.push_back({p[0], p[1], {}, p[2] == "b", {}});
            continue;
        }
        if (out.empty()) continue;
        Repertoire& r = out.back();
        if (line[0] == '=') {
            const auto p = split(rest, '|');
            OpeningLine l;
            l.title = p.empty() ? "" : p[0];
            l.custom = custom;
            if (p.size() > 1) {
                std::istringstream ms(p[1]);
                for (std::string m; ms >> m;) l.moves.push_back(m), l.notes.emplace_back();
            }
            r.lines.push_back(std::move(l));
        } else if (r.lines.empty()) {
            r.text += (r.text.empty() ? "" : "\n") + line;
        } else {
            const size_t sp = line.find_first_of(" \t");
            r.lines.back().moves.push_back(line.substr(0, sp));
            r.lines.back().notes.push_back(sp == std::string::npos ? "" : trim(line.substr(sp)));
        }
    }
    return out;
}

std::vector<Repertoire> loadRepertoires(const std::filesystem::path& builtin, const std::filesystem::path& user) {
    std::vector<Repertoire> reps = parseRepertoires(readFile(builtin));
    for (Repertoire& u : parseRepertoires(readFile(user), true)) {
        const auto it = std::find_if(reps.begin(), reps.end(), [&](const Repertoire& r) { return r.id == u.id; });
        if (it == reps.end()) reps.push_back(std::move(u));
        else it->lines.insert(it->lines.end(), u.lines.begin(), u.lines.end());
    }
    return reps;
}

void saveCustomLines(const std::vector<Repertoire>& reps, const std::filesystem::path& file) {
    std::ofstream out(file, std::ios::binary);
    for (const Repertoire& r : reps) {
        if (std::none_of(r.lines.begin(), r.lines.end(), [](const OpeningLine& l) { return l.custom; })) continue;
        out << "# " << r.id << " | " << r.title << " | " << (r.black ? "b" : "w") << "\n";
        for (const OpeningLine& l : r.lines) {
            if (!l.custom) continue;
            out << "= " << l.title << "\n";
            for (size_t i = 0; i < l.moves.size(); ++i)
                out << l.moves[i] << (l.notes[i].empty() ? "" : " " + l.notes[i]) << "\n";
        }
        out << "\n";
    }
}

std::string positionKey(const chess::Position& p) {
    const std::string fen = p.fen();
    size_t end = 0;
    for (int i = 0; i < 3; ++i) end = fen.find(' ', end + 1);
    return fen.substr(0, end);
}

std::vector<chess::Move> lineMoves(const OpeningLine& line) {
    std::vector<chess::Move> out;
    chess::Position p;
    for (const std::string& san : line.moves) {
        const chess::Move m = chess::parseSAN(p, san);
        if (m.isNone()) break;
        out.push_back(m);
        p.makeMove(m);
    }
    return out;
}

const std::vector<OpeningMove>* OpeningTree::at(const chess::Position& p) const {
    const auto it = next.find(positionKey(p));
    return it == next.end() ? nullptr : &it->second;
}

OpeningTree buildTree(const Repertoire& rep) {
    OpeningTree t;
    for (const OpeningLine& l : rep.lines) {
        chess::Position p;
        const auto moves = lineMoves(l);
        for (size_t i = 0; i < moves.size(); ++i) {
            auto& list = t.next[positionKey(p)];
            auto it = std::find_if(list.begin(), list.end(), [&](const OpeningMove& m) { return m.move == moves[i]; });
            if (it == list.end()) it = list.insert(list.end(), {moves[i], {}});
            if (it->note.empty()) it->note = l.notes[i];
            p.makeMove(moves[i]);
        }
    }
    return t;
}

std::vector<std::string> checkRepertoire(const Repertoire& rep) {
    std::vector<std::string> errors;
    const OpeningTree tree = buildTree(rep);
    const chess::Color me = rep.black ? chess::Black : chess::White;
    for (const OpeningLine& l : rep.lines) {
        const std::string where = rep.id + " «" + l.title + "»: ";
        const auto moves = lineMoves(l);
        if (moves.size() < l.moves.size()) {
            errors.push_back(where + "невозможный ход " + std::to_string(moves.size() / 2 + 1) + ". " + l.moves[moves.size()]);
            continue;
        }
        if (moves.empty()) errors.push_back(where + "нет ходов");
        chess::Position p;
        for (size_t i = 0; i < moves.size(); ++i) {
            const auto& list = *tree.at(p);
            if (p.sideToMove() == me) {
                if (list.size() > 1) errors.push_back(where + "два разных своих хода в позиции перед " + l.moves[i]);
                if (list[0].note.empty()) errors.push_back(where + "нет объяснения к ходу " + l.moves[i]);
            }
            p.makeMove(moves[i]);
        }
        if (!moves.empty() && p.sideToMove() == me && !tree.at(p)) errors.push_back(where + "ветка кончается ходом соперника");
    }
    std::sort(errors.begin(), errors.end());
    errors.erase(std::unique(errors.begin(), errors.end()), errors.end());
    return errors;
}

std::vector<OpeningLine> linesFromPGN(const std::string& pgn) {
    struct State {
        std::vector<std::string> moves, notes;
    };
    std::vector<OpeningLine> out;
    std::vector<State> stack;
    State cur;
    auto flush = [&] {
        OpeningLine l;
        chess::Position p;
        for (size_t i = 0; i < cur.moves.size(); ++i) {
            const chess::Move m = chess::parseSAN(p, cur.moves[i]);
            if (m.isNone()) break;
            l.moves.push_back(chess::toSAN(p, m));
            l.notes.push_back(cur.notes[i]);
            p.makeMove(m);
        }
        l.custom = true;
        l.title = customLineTitle(l.moves);
        if (!l.moves.empty()) out.push_back(std::move(l));
    };
    const size_t n = pgn.size();
    for (size_t i = 0; i < n;) {
        const char c = pgn[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
        } else if (c == '[') {
            if (stack.empty() && !cur.moves.empty()) flush(), cur = {};
            i = std::min(n, pgn.find(']', i)) + 1;
        } else if (c == '{') {
            const size_t e = std::min(n, pgn.find('}', i));
            std::string note = trim(pgn.substr(i + 1, e - i - 1));
            std::replace(note.begin(), note.end(), '\n', ' ');
            std::erase(note, '\r');
            if (!cur.notes.empty() && !note.empty()) cur.notes.back() += (cur.notes.back().empty() ? "" : " ") + note;
            i = e + 1;
        } else if (c == ';') {
            i = std::min(n, pgn.find('\n', i));
        } else if (c == '(') {
            stack.push_back(cur);
            if (!cur.moves.empty()) cur.moves.pop_back(), cur.notes.pop_back();
            ++i;
        } else if (c == ')') {
            flush();
            if (!stack.empty()) cur = stack.back(), stack.pop_back();
            ++i;
        } else {
            size_t j = i;
            while (j < n && !std::isspace(static_cast<unsigned char>(pgn[j])) && !std::strchr("{}()[];", pgn[j])) ++j;
            const std::string tok = pgn.substr(i, j - i);
            i = std::max(j, i + 1);
            if (tok == "*" || tok == "1-0" || tok == "0-1" || tok == "1/2-1/2") {
                if (stack.empty()) flush(), cur = {};
                continue;
            }
            const size_t start = tok.rfind("0-0", 0) == 0 ? 0 : tok.find_first_not_of("0123456789.");
            if (start == std::string::npos || tok[0] == '$') continue;
            cur.moves.push_back(tok.substr(start));
            cur.notes.emplace_back();
        }
    }
    if (!cur.moves.empty()) flush();
    return out;
}

OpeningCard reviewCard(OpeningCard c, bool correct, int day) {
    static const int intervals[] = {1, 3, 7, 14, 30, 60};
    if (!correct) return {0, day};
    return {std::min(c.step + 1, 5), day + intervals[std::min(c.step, 5)]};
}

}
