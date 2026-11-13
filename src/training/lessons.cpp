#include "training/lessons.h"

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

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

void append(std::string& to, const std::string& line) { to += (to.empty() ? "" : "\n") + line; }
}

std::vector<Section> loadLessons(const std::filesystem::path& file) {
    std::vector<Section> out;
    std::ifstream in(file);
    std::string line, fen;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (trim(line).empty()) continue;
        const char tag = line[0];
        const std::string rest = trim(line.substr(1));
        if (tag == '#') {
            out.push_back({rest, {}});
            continue;
        }
        if (out.empty()) continue;
        auto& lessons = out.back().lessons;
        if (tag == '=') {
            const auto p = split(rest, '|');
            lessons.push_back({p[0], p.size() > 1 ? p[1] : p[0], {}, {}, {}, {}, false});
            continue;
        }
        if (lessons.empty()) continue;
        Lesson& l = lessons.back();
        if (tag == '@') {
            if (!rest.empty()) fen = rest;
            l.steps.push_back({fen, {}, {}, {}, {}});
        } else if (tag == '%') {
            const auto p = split(rest, '|');
            if (p.size() < 4) continue;
            l.practiceFen = p[0];
            l.practiceBot = p[1];
            l.practiceBlack = p[2] == "b";
            l.practiceGoal = p[3];
        } else if (!l.steps.empty()) {
            LessonStep& s = l.steps.back();
            if (tag == '?') s.solution = words(rest);
            else if (tag == '>') s.arrows = words(rest);
            else if (tag == '!') append(s.after, rest);
            else append(s.text, trim(line));
        }
    }
    return out;
}

}
