#include "training/puzzles.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>

#include <fstream>
#include <memory>

namespace training {

bool downloadFile(const std::wstring& host, const std::wstring& path, const std::filesystem::path& to, ImportProgress& p) {
    using Handle = std::unique_ptr<void, decltype(&WinHttpCloseHandle)>;
    Handle session(WinHttpOpen(L"ChessTrainer", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0),
                   WinHttpCloseHandle);
    if (!session) return false;
    Handle conn(WinHttpConnect(session.get(), host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0), WinHttpCloseHandle);
    if (!conn) return false;
    Handle req(WinHttpOpenRequest(conn.get(), L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE),
               WinHttpCloseHandle);
    if (!req || !WinHttpSendRequest(req.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req.get(), nullptr))
        return false;
    DWORD status = 0, len = sizeof(status);
    WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &len, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) return false;
    wchar_t size[32] = {};
    len = sizeof(size);
    if (WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, size, &len,
                            WINHTTP_NO_HEADER_INDEX))
        p.total = _wtoi64(size);
    p.done = 0;

    std::filesystem::path part = to;
    part += ".part";
    {
        std::ofstream out(part, std::ios::binary);
        std::vector<char> buf(1 << 16);
        DWORD got = 0;
        while (!p.cancel && WinHttpReadData(req.get(), buf.data(), DWORD(buf.size()), &got) && got > 0) {
            out.write(buf.data(), got);
            p.done += got;
        }
        if (!out || p.cancel || (p.total > 0 && p.done != p.total)) {
            out.close();
            std::filesystem::remove(part);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(part, to, ec);
    return !ec;
}

}
