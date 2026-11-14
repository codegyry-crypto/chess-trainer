#include "uci/uci_engine.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <chrono>
#include <map>
#include <sstream>

namespace uci {
namespace fs = std::filesystem;

Engine::~Engine() { stop(); }

bool Engine::start(const fs::path& exe, std::string* error) {
    stop();
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        stop();
        return false;
    };
    if (!fs::exists(exe)) return fail("нет файла движка");

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr;
    if (!CreatePipe(&inRead, &inWrite, &sa, 0) || !CreatePipe(&outRead, &outWrite, &sa, 0))
        return fail("не удалось создать каналы");
    SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inRead;
    si.hStdOutput = outWrite;
    si.hStdError = outWrite;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe.wstring() + L"\"";
    const std::wstring dir = exe.parent_path().wstring();
    const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                   dir.c_str(), &si, &pi);
    CloseHandle(inRead);
    CloseHandle(outWrite);
    if (!ok) {
        CloseHandle(inWrite);
        CloseHandle(outRead);
        return fail("не удалось запустить движок");
    }
    CloseHandle(pi.hThread);
    process_ = pi.hProcess;
    stdinWrite_ = inWrite;
    stdoutRead_ = outRead;
    eof_ = false;
    lines_.clear();
    reader_ = std::thread(&Engine::readerLoop, this);

    send("uci");
    std::string line;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!readLine(line, 1000)) {
            if (eof_) break;
            continue;
        }
        if (line.rfind("id name ", 0) == 0) name_ = line.substr(8);
        if (line == "uciok") return true;
    }
    return fail("движок не ответил");
}

void Engine::stop() {
    if (process_) {
        send("quit");
        if (WaitForSingleObject(static_cast<HANDLE>(process_), 1500) == WAIT_TIMEOUT)
            TerminateProcess(static_cast<HANDLE>(process_), 0);
    }
    if (stdinWrite_) CloseHandle(static_cast<HANDLE>(stdinWrite_)), stdinWrite_ = nullptr;
    if (stdoutRead_) {
        CancelIoEx(static_cast<HANDLE>(stdoutRead_), nullptr);
    }
    if (reader_.joinable()) reader_.join();
    if (stdoutRead_) CloseHandle(static_cast<HANDLE>(stdoutRead_)), stdoutRead_ = nullptr;
    if (process_) CloseHandle(static_cast<HANDLE>(process_)), process_ = nullptr;
}

void Engine::send(const std::string& line) {
    if (!stdinWrite_) return;
    const std::string s = line + "\n";
    DWORD written = 0;
    WriteFile(static_cast<HANDLE>(stdinWrite_), s.data(), DWORD(s.size()), &written, nullptr);
}

void Engine::readerLoop() {
    std::string buf;
    char chunk[4096];
    for (;;) {
        DWORD n = 0;
        if (!ReadFile(static_cast<HANDLE>(stdoutRead_), chunk, sizeof chunk, &n, nullptr) || n == 0) break;
        buf.append(chunk, n);
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            buf.erase(0, pos + 1);
            std::lock_guard<std::mutex> lock(mutex_);
            lines_.push_back(std::move(line));
            cv_.notify_one();
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    eof_ = true;
    cv_.notify_all();
}

bool Engine::readLine(std::string& out, int timeoutMs) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return !lines_.empty() || eof_; }))
        return false;
    if (lines_.empty()) return false;
    out = std::move(lines_.front());
    lines_.pop_front();
    return true;
}

void Engine::setOption(const std::string& name, const std::string& value) {
    send("setoption name " + name + " value " + value);
}

bool Engine::isReady(int timeoutMs) {
    send("isready");
    std::string line;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!readLine(line, 200)) {
            if (eof_) return false;
            continue;
        }
        if (line == "readyok") return true;
    }
    return false;
}

void Engine::newGame() {
    send("ucinewgame");
    isReady();
}

bool parseInfo(const std::string& line, Line& out) {
    if (line.rfind("info ", 0) != 0) return false;
    std::istringstream in(line);
    std::string tok;
    bool hasScore = false;
    Line l;
    in >> tok;
    while (in >> tok) {
        if (tok == "depth") in >> l.depth;
        else if (tok == "multipv") in >> l.multipv;
        else if (tok == "score") {
            std::string kind;
            in >> kind >> l.score.value;
            l.score.mate = kind == "mate";
            hasScore = true;
        } else if (tok == "lowerbound" || tok == "upperbound") {
            return false;
        } else if (tok == "pv") {
            while (in >> tok) l.pv.push_back(tok);
        }
    }
    if (!hasScore || l.pv.empty()) return false;
    out = std::move(l);
    return true;
}

Analysis Engine::analyse(const std::string& fen, const std::vector<std::string>& moves, int depth, int movetimeMs,
                         int multipv, std::atomic<bool>* stop) {
    Analysis res;
    if (!running()) return res;
    setOption("MultiPV", std::to_string(multipv));
    std::istringstream fields(fen);
    std::string castling;
    fields >> castling >> castling >> castling;
    setOption("UCI_Chess960", castling.find_first_not_of("KQkq-") == std::string::npos ? "false" : "true");
    std::string pos = fen == "startpos" || fen.empty() ? "position startpos" : "position fen " + fen;
    if (!moves.empty()) {
        pos += " moves";
        for (const std::string& m : moves) pos += " " + m;
    }
    send(pos);
    std::string go = "go";
    if (depth > 0) go += " depth " + std::to_string(depth);
    if (movetimeMs > 0) go += " movetime " + std::to_string(movetimeMs);
    send(go);

    std::map<int, Line> best;
    bool stopSent = false;
    std::string line;
    for (;;) {
        if (stop && stop->load() && !stopSent) {
            send("stop");
            stopSent = true;
        }
        if (!readLine(line, 100)) {
            if (eof_) break;
            continue;
        }
        Line l;
        if (parseInfo(line, l)) best[l.multipv] = std::move(l);
        else if (line.rfind("bestmove", 0) == 0) {
            std::istringstream in(line);
            std::string tok, mv;
            in >> tok >> mv;
            res.bestmove = mv == "(none)" ? "" : mv;
            break;
        }
    }
    for (auto& [k, l] : best) res.lines.push_back(std::move(l));
    return res;
}

fs::path findStockfish(const fs::path& dir) {
    std::error_code ec;
    const bool avx2 = IsProcessorFeaturePresent(40 ) != 0;
    fs::path fallback;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string name = e.path().filename().string();
        if (e.path().extension() != ".exe" || name.find("stockfish") == std::string::npos) continue;
        if (name.find("avx2") != std::string::npos) {
            if (avx2) return e.path();
        } else {
            fallback = e.path();
        }
    }
    return fallback;
}

}
