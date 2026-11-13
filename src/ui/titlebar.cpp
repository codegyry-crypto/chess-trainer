#include "ui/titlebar.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>

namespace ui {
namespace {
constexpr DWORD UseImmersiveDarkMode = 20;
constexpr DWORD BorderColor = 34;
constexpr DWORD CaptionColor = 35;
constexpr DWORD TextColor = 36;

COLORREF toColorRef(unsigned rgb) {
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

}

void setTitleBarColors(void* handle, unsigned bg, unsigned text, unsigned border, bool dark) {
    HWND hwnd = static_cast<HWND>(handle);
    if (!hwnd) return;
    const BOOL darkMode = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, UseImmersiveDarkMode, &darkMode, sizeof darkMode);
    const COLORREF caption = toColorRef(bg), txt = toColorRef(text), frame = toColorRef(border);
    DwmSetWindowAttribute(hwnd, CaptionColor, &caption, sizeof caption);
    DwmSetWindowAttribute(hwnd, TextColor, &txt, sizeof txt);
    DwmSetWindowAttribute(hwnd, BorderColor, &frame, sizeof frame);
}

}
