#include "training/update.h"
#include "ui/app.h"
#include "ui/ui.h"

namespace ui {

void App::startUpdateCheck() {
    updateTag_.clear();
    updateChecked_ = false;
    updateCheck_ = std::async(std::launch::async, [] {
        const std::string tag = update::latestTag();
        return update::isNewer(tag, CHESS_VERSION) ? tag : std::string{};
    });
}

void App::pollUpdateCheck() {
    if (!updateCheck_.valid() || updateCheck_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    updateTag_ = updateCheck_.get();
    updateChecked_ = true;
    if (manualUpdateCheck_)
        toast(updateTag_.empty() ? "У тебя последняя версия " CHESS_VERSION
                                 : "Доступно обновление " + updateTag_ + " — кнопка «Обновить» на главном экране");
    manualUpdateCheck_ = false;
}

bool App::drawUpdateBanner(Rectangle r) {
    if (settings_.checkUpdates && !updateChecked_ && !updateCheck_.valid()) startUpdateCheck();
    pollUpdateCheck();
    if (updateTag_.empty()) return false;

    const Theme& t = theme();
    const auto file = std::filesystem::temp_directory_path() / ("ChessTrainer-Setup-" + updateTag_ + ".exe");
    roundRect(r, 10, t.panel);
    roundRectLines(r, 10, 1.5f, t.accent);
    const Rectangle btn{r.x + r.width - 128, r.y + 5, 120, r.height - 10};
    const float ty = r.y + (r.height - 17) / 2;
    if (updateDownload_.valid()) {
        if (updateDownload_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            const int64_t done = updateProgress_.done, total = updateProgress_.total;
            text("Скачиваю " + updateTag_ + "…", {r.x + 16, ty}, 17, t.text);
            const Rectangle bar{btn.x, r.y + r.height / 2 - 4, btn.width, 8};
            roundRect(bar, 4, mix(t.bg, t.text, 0.08f));
            if (total > 0) roundRect({bar.x, bar.y, std::max(8.0f, bar.width * done / total), bar.height}, 4, t.accent);
            return true;
        }
        if (updateDownload_.get() && update::runInstaller(file)) quit_ = true;
        else toast("Не получилось скачать обновление. Проверь интернет и попробуй ещё раз");
    }
    text("Доступно обновление " + updateTag_, {r.x + 16, ty}, 17, t.text, true);
    if (button(btn, "Обновить", Style::Primary, true, 16)) {
        updateProgress_.cancel = false;
        updateDownload_ = std::async(std::launch::async, [this, file] {
            return update::downloadInstaller(updateTag_, file, updateProgress_);
        });
    }
    return true;
}

}
