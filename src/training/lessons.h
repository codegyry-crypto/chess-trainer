#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace training {

struct LessonStep {
    std::string fen, text, after;
    std::vector<std::string> arrows, solution;
};

struct Lesson {
    std::string id, title;
    std::vector<LessonStep> steps;
    std::string practiceFen, practiceBot, practiceGoal;
    bool practiceBlack = false;
};

struct Section {
    std::string title;
    std::vector<Lesson> lessons;
};

std::vector<Section> loadLessons(const std::filesystem::path& file);

}
