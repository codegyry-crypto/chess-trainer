#pragma once
#include "core/position.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace training {

struct OpeningLine {
    std::string title;
    std::vector<std::string> moves, notes;
    bool custom = false;
};

struct Repertoire {
    std::string id, title, text;
    bool black = false;
    std::vector<OpeningLine> lines;
};

struct OpeningMove {
    chess::Move move;
    std::string note;
};

struct OpeningTree {
    std::unordered_map<std::string, std::vector<OpeningMove>> next;
    const std::vector<OpeningMove>* at(const chess::Position& p) const;
};

struct OpeningCard {
    int step = 0, due = 0;
};

std::vector<Repertoire> parseRepertoires(const std::string& text, bool custom = false);
std::vector<Repertoire> loadRepertoires(const std::filesystem::path& builtin, const std::filesystem::path& user);
void saveCustomLines(const std::vector<Repertoire>& reps, const std::filesystem::path& file);

std::string positionKey(const chess::Position& p);
std::vector<chess::Move> lineMoves(const OpeningLine& line);
OpeningTree buildTree(const Repertoire& rep);
std::vector<std::string> checkRepertoire(const Repertoire& rep);
std::vector<OpeningLine> linesFromPGN(const std::string& pgn);
std::string customLineTitle(const std::vector<std::string>& moves);

OpeningCard reviewCard(OpeningCard c, bool correct, int day);

}
