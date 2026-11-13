#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace uci {
struct Score {
    bool mate = false;
    int value = 0;
};

struct Line {
    int multipv = 1;
    int depth = 0;
    Score score;
    std::vector<std::string> pv;
};

struct Analysis {
    std::string bestmove;
    std::vector<Line> lines;
};

class Engine {
public:
    Engine() = default;
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool start(const std::filesystem::path& exe, std::string* error = nullptr);
    void stop();
    bool running() const { return process_ != nullptr; }
    const std::string& name() const { return name_; }

    void setOption(const std::string& name, const std::string& value);
    bool isReady(int timeoutMs = 10000);
    void newGame();

    Analysis analyse(const std::string& fen, const std::vector<std::string>& moves, int depth, int movetimeMs,
                     int multipv = 1, std::atomic<bool>* stop = nullptr);

private:
    void send(const std::string& line);
    bool readLine(std::string& out, int timeoutMs);
    void readerLoop();

    void* process_ = nullptr;
    void* stdinWrite_ = nullptr;
    void* stdoutRead_ = nullptr;
    std::string name_;
    std::thread reader_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::string> lines_;
    bool eof_ = false;
};

std::filesystem::path findStockfish(const std::filesystem::path& dir);

bool parseInfo(const std::string& line, Line& out);

}
