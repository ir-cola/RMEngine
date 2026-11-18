#include "rm_engine/game_production/editor/DebugPanel.h"

#include "base/Utf.h"
#include "editor/lang/Lang.h"
#include "editor/plugin/Software.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "platform/Gfx.h"
#include "platform/SystemInfo.h"
#include "platform/Win.h"
#include "rm_engine/game_production/runtime/GameSettings.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_production/runtime/Scripts.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "ui/Context.h"

#include <algorithm>
#include <format>
#include <map>

namespace rm::game_production {

namespace w = editor::w;
namespace theme = editor::theme;
using editor::Tr;
using editor::TrF;
using editor::TrKey;
using editor::TrU8;
using ui::Rect;

// --- The panel ---------------------------------------------------------------------------------------

// What is read of the PC once a second (it hardly changes).
struct DebugPanel::System {
    std::vector<platform::MonitorDesc> monitors;
    std::optional<platform::GpuDesc> gpu;
    platform::ProcessMemory memory;
};

namespace {

constexpr float kRowH = 20, kHeadH = 26, kLabelW = 170;

std::wstring Mb(uint64_t bytes) { return std::format(L"{:.0f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0)); }

std::wstring FramesText(const FrameMeter::Summary& s) {
    if (s.frames == 0) return Tr(L"描いていません (変わったときだけ描きます。パネルが閉じていても描きません)");
    return TrF(L"{:.0f} FPS ・ 平均 {:.1f} ms ・ 最長 {:.1f} ms ・ 描く時間 {:.2f} ms", s.fps, s.meanMs, s.worstMs, s.workMs);
}

// A pad's button by its place (PadA ..): "A", "LB", "Start" ...
std::wstring PadControlName(size_t i) {
    std::string n = ControlName(kPadControlBase + static_cast<int>(i));
    if (n.rfind("Pad", 0) == 0) n.erase(0, 3);
    return Utf8ToWide(n);
}

bool KeyHeld(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

}  // namespace

void DebugPanel::Draw(ui::Context& c, const Rect& r, const DebugView& v) {
    const double now = platform::NowSeconds();
    self_.Frame(now);
    if (live_) {
        c.RequestAnimation();  // (the values move by themselves: the mouse, the pads, the frames)
        padState_ = pads_.Read();
    }
    if (!system_ || (live_ && now - systemAt_ > 1.0)) {
        auto s = std::make_shared<System>();
        s->monitors = platform::Monitors(platform::ThisThreadsWindow());
        if (v.gfx) s->gpu = platform::GpuOf(v.gfx->D3D());
        s->memory = platform::MemoryOfThisProcess();
        system_ = std::move(s);
        systemAt_ = now;
    }

    // The bar on top: drawn again all the time or held.
    Rect bar{r.x + 10, r.y + 6, r.w - 20, 22};
    w::Checkbox(c, "debug.live", {bar.x, bar.y, 200, bar.h}, live_, Tr(L"ずっと更新する"));
    {
        ui::TextStyle ts;
        ts.size = 11;
        ts.color = theme::TextFaint;
        ts.v = ui::VAlign::Center;
        c.P().Text({bar.x + 206, bar.y, bar.w - 206, bar.h},
                   Tr(L"開いている間はエディターを毎フレーム描き直すので、ビューの FPS はモニターの速さに近くなります"), ts);
    }
    const Rect view{r.x, r.y + 34, r.w, std::max(0.f, r.h - 34)};
    float& contentH = c.FloatState(c.MakeId("debug.contentH"), 600.f);
    const float off = w::BeginScroll(c, "debug.scroll", view, contentH);
    const float x = view.x + 10, width = view.w - 20;
    float y = view.y + 4 - off;
    const float top = y;
    size_t section = 0;
    auto header = [&](std::wstring_view title) {
        if (open_.size() <= section) open_.resize(section + 1, true);
        bool open = open_[section];
        w::CollapsingHeader(c, std::format("debug.h{}", section), {x, y, width, kHeadH - 4}, title, open);
        open_[section] = open;
        ++section;
        y += kHeadH;
        return open;
    };
    auto row = [&](std::wstring_view label, std::wstring_view value, bool lit = false) {
        w::InfoRow(c, {x + 10, y, width - 10, kRowH}, label, value, kLabelW, lit);
        y += kRowH;
    };
    auto gap = [&] { y += 6; };

    // --- How often things are drawn --------------------------------------------------------------
    if (header(Tr(L"フレームの速さ"))) {
        row(Tr(L"シーンビュー"), FramesText(v.sceneView));
        row(Tr(L"ゲームビュー"), FramesText(v.gameView));
        if (v.playing) {
            const FrameMeter::Summary& u = v.update;
            row(Tr(L"ゲームの更新 (Play)"), u.frames == 0 ? Tr(L"止まっています")
                                                          : TrF(L"{:.0f} 回/秒 ・ 平均 {:.1f} ms ・ 最長 {:.1f} ms ・ 1 回の処理 {:.2f} ms", u.fps, u.meanMs,
                                                                u.worstMs, u.workMs));
        }
        const FrameMeter::Summary me = self_.Over(now);
        row(Tr(L"このパネル"), me.frames == 0 ? std::wstring() : TrF(L"{:.0f} FPS", me.fps));
        if (v.settings) row(Tr(L"目標 FPS (ゲームの設定)"), std::to_wstring(v.settings->targetFps));
        gap();
    }

    // --- Play ----------------------------------------------------------------------------------------
    if (header(Tr(L"Play"))) {
        row(Tr(L"状態"), !v.playing ? Tr(L"止まっています") : v.paused ? Tr(L"一時停止しています") : Tr(L"動いています"), v.playing && !v.paused);
        if (v.playing && v.world) {
            const ScriptWorld& wd = *v.world;
            row(Tr(L"ゲームの時間"), TrF(L"{:.2f} 秒 ・ {} フレーム", wd.time, wd.frame));
            row(Tr(L"本当の時間"), TrF(L"{:.2f} 秒", wd.unscaledTime));
            row(Tr(L"時間の速さ"), TrF(L"{:.2f} 倍", wd.timeScale), wd.timeScale != 1);
            row(Tr(L"コルーチン"), TrF(L"{} 個", std::count_if(wd.coroutines.begin(), wd.coroutines.end(), [](const CoroutineSlot& s) { return !s.dead; })));
            if (v.runner && v.scene) {
                const SceneEntity* cam = v.runner->Camera() ? v.scene->Find(static_cast<int>(v.runner->Camera())) : nullptr;
                row(Tr(L"ゲームのカメラ"), cam ? Utf8ToWide(cam->data.name) : Tr(L"(なし)"));
            }
        }
        row(Tr(L"スクリプトの種類"), TrF(L"{} 種類 (ビルドした DLL の中)", v.scriptTypes));
        gap();
    }

    // --- The mouse ---------------------------------------------------------------------------------
    if (header(Tr(L"マウス"))) {
        POINT cursor{};
        GetCursorPos(&cursor);
        row(Tr(L"画面 (ピクセル)"), std::format(L"{}, {}", cursor.x, cursor.y));
        const ui::Input& in = c.In();
        row(Tr(L"エディターの窓 (DIP)"), std::format(L"{:.0f}, {:.0f}", in.mouseX, in.mouseY));
        row(Tr(L"シーンビューの上"), v.sceneMouse ? std::format(L"{:.0f}, {:.0f}", v.sceneMouse->x, v.sceneMouse->y) : Tr(L"(外)"), v.sceneMouse.has_value());
        const Vec2 screen = v.settings ? Vec2{static_cast<float>(v.settings->display.width), static_cast<float>(v.settings->display.height)} : Vec2{};
        row(Tr(L"ゲームの画面"),
            v.gameMouse ? TrF(L"{:.0f}, {:.0f} (画面 {:.0f} x {:.0f}。スクリプトの rm::Mouse::Position)", v.gameMouse->x, v.gameMouse->y, screen.x, screen.y)
                        : Tr(L"(外)"),
            v.gameMouse.has_value());
        std::wstring held;
        const std::pair<int, const wchar_t*> buttons[] = {{VK_LBUTTON, TrKey(L"左")}, {VK_RBUTTON, TrKey(L"右")}, {VK_MBUTTON, TrKey(L"中")},
                                                           {VK_XBUTTON1, TrKey(L"戻る")}, {VK_XBUTTON2, TrKey(L"進む")}};
        for (const auto& [vk, name] : buttons) {
            if (KeyHeld(vk)) held += (held.empty() ? L"" : L" ・ ") + Tr(name);
        }
        row(Tr(L"押しているボタン"), held.empty() ? Tr(L"(なし)") : held, !held.empty());
        if (v.playing && v.world) row(Tr(L"ホイール (このフレーム)"), std::format(L"{:+.1f}", v.world->inputNow.wheel));
        gap();
    }

    // --- The keyboard --------------------------------------------------------------------------------
    if (header(Tr(L"キーボード"))) {
        std::wstring held;
        for (int vk = 8; vk < 256; ++vk) {
            if (vk == VK_XBUTTON1 || vk == VK_XBUTTON2 || !KeyHeld(vk)) continue;
            // (the left / right ones of Shift, Ctrl and Alt are the same key twice)
            if ((vk >= VK_LSHIFT && vk <= VK_RMENU)) continue;
            held += (held.empty() ? L"" : L" ・ ") + Utf8ToWide(ControlName(vk));
        }
        row(Tr(L"押しているキー"), held.empty() ? Tr(L"(なし)") : held, !held.empty());
        gap();
    }

    // --- The game pads -------------------------------------------------------------------------------
    if (header(Tr(L"コントローラー (XInput)"))) {
        for (int p = 0; p < kPlayers; ++p) {
            const PadState& s = padState_[static_cast<size_t>(p)];
            const std::wstring who = TrF(L"{}P (パッド {})", p + 1, p + 1);
            if (!s.connected) {
                row(who, Tr(L"つながっていません"));
                continue;
            }
            std::wstring held;
            for (size_t i = 0; i < 14; ++i) {
                if (s.values[i] >= 0.5f) held += (held.empty() ? L"" : L" ") + PadControlName(i);
            }
            std::wstring state = held.empty() ? Tr(L"つながっています (押していません)") : TrF(L"押している: {}", held);
            if (s.battery >= 0) state += TrF(L" ・ 電池 {}/3", s.battery);
            row(who, state, !held.empty());
            const auto& vals = s.values;
            row(L"", TrF(L"左スティック ({:+.2f}, {:+.2f}) ・ 右スティック ({:+.2f}, {:+.2f}) ・ LT {:.2f} ・ RT {:.2f}", vals[16], vals[17], vals[18], vals[19],
                         vals[14], vals[15]),
                std::fabs(vals[16]) + std::fabs(vals[17]) + std::fabs(vals[18]) + std::fabs(vals[19]) + vals[14] + vals[15] > 0);
        }
        if (v.settings) row(Tr(L"キーボードとマウス"), TrF(L"{}P (ゲームの設定)", v.settings->keyboardPlayer));
        gap();
    }

    // --- The game's input actions --------------------------------------------------------------------
    if (header(Tr(L"入力のアクション (Play 中)"))) {
        if (!v.playing || !v.world) {
            row(L"", Tr(L"Play 中に、ゲームの設定の「入力」の値が出ます (スクリプトが読む値)"));
        } else {
            for (const InputAction& a : v.world->Actions()) {
                const ActionValue val = EvaluateAction(a, v.world->inputNow);
                row(Utf8ToWide(a.name), val.down ? TrF(L"{:+.2f} ・ 押している", val.axis) : std::format(L"{:+.2f}", val.axis), val.down);
            }
            if (v.world->playerActions) row(L"", Tr(L"(遊ぶ人が変えた割り当てで読んでいます)"));
        }
        gap();
    }

    // --- The scene -----------------------------------------------------------------------------------
    if (header(Tr(L"シーン")) && v.scene) {
        const std::vector<TreeEntry> tree = v.scene->TreeOrder();
        size_t active = 0, roots = 0, components = 0, scripts = 0, missing = 0;
        std::map<std::string, size_t> byType;
        std::map<std::string, const reflect::TypeDesc*> types;
        for (const reflect::TypeDesc* t : v.scene->ComponentTypes()) types[t->name] = t;
        for (const TreeEntry& t : tree) {
            if (t.activeInHierarchy) ++active;
            if (t.depth == 0) ++roots;
            for (const Component& comp : t.entity->components) {
                ++components;
                ++byType[comp.type];
                const auto it = types.find(comp.type);
                if (!comp.Known() || it == types.end()) {
                    ++missing;
                } else if (it->second->kind == reflect::TypeKind::Script) {
                    ++scripts;
                }
            }
        }
        row(Tr(L"エンティティ"), TrF(L"{} 個 (有効 {} 個 ・ 一番上 {} 個)", tree.size(), active, roots));
        row(Tr(L"部品"), TrF(L"{} 個 (スクリプト {} 個 ・ プラグインが無い {} 個)", components, scripts, missing), missing > 0);
        std::vector<std::pair<size_t, std::string>> most;
        for (const auto& [type, n] : byType) most.push_back({n, type});
        std::sort(most.begin(), most.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
        for (size_t i = 0; i < most.size() && i < 10; ++i) {
            const auto it = types.find(most[i].second);
            const std::wstring label = it != types.end() ? Utf8ToWide(TrU8(it->second->label)) : Utf8ToWide(most[i].second);
            row(L"  " + label, TrF(L"{} 個", most[i].first));
        }
        gap();
    }

    // --- The plugins' sections (パーティクル ...) ------------------------------------------------------
    if (v.registry) {
        std::vector<const DebugInfoInfo*> infos;
        for (const auto& d : v.registry->Contributions<DebugInfoInfo>(kDebugInfoPoint)) {
            if (d.item->collect) infos.push_back(d.item);
        }
        std::stable_sort(infos.begin(), infos.end(), [](const DebugInfoInfo* a, const DebugInfoInfo* b) { return a->order < b->order; });
        for (const DebugInfoInfo* info : infos) {
            if (!header(Tr(info->title))) continue;
            std::vector<DebugInfoLine> lines;
            info->collect(lines);
            for (const DebugInfoLine& l : lines) row(l.label, l.value);
            if (lines.empty()) row(L"", Tr(L"(なし)"));
            gap();
        }
    }

    // --- The monitors --------------------------------------------------------------------------------
    if (header(Tr(L"モニター"))) {
        for (size_t i = 0; i < system_->monitors.size(); ++i) {
            const platform::MonitorDesc& m = system_->monitors[i];
            row(TrF(L"モニター {}", i + 1), m.name + (m.hasWindow ? Tr(L" ← このエディター") : std::wstring()), m.hasWindow);
            std::wstring facts = m.refreshHz > 0 ? TrF(L"{} x {} ・ {} Hz ・ 拡大 {:.0f} %", m.width, m.height, m.refreshHz, m.scale * 100)
                                                  : TrF(L"{} x {} ・ 拡大 {:.0f} %", m.width, m.height, m.scale * 100);
            if (m.primary) facts += Tr(L" ・ メイン");
            row(m.device, facts);
        }
        gap();
    }

    // --- The GPU and memory ----------------------------------------------------------------------------
    if (header(Tr(L"GPU とメモリ"))) {
        if (const auto& g = system_->gpu) {
            row(Tr(L"GPU"), g->name, true);
            const std::wstring vendor = platform::GpuVendorName(g->vendorId);
            row(Tr(L"メーカー"), (vendor.empty() ? std::wstring() : vendor + L" ・ ") + std::format(L"0x{:04X} / 0x{:04X}", g->vendorId, g->deviceId));
            row(Tr(L"GPU のメモリ"), TrF(L"専用 {} ・ 共有 {}", Mb(g->dedicatedBytes), Mb(g->sharedBytes)));
            if (g->budgetBytes > 0) {
                row(Tr(L"このソフトが使う GPU のメモリ"), TrF(L"{} (使える量 {})", Mb(g->usedBytes), Mb(g->budgetBytes)));
                w::ProgressBar(c, {x + 10 + kLabelW, y + 3, std::min(240.f, width - 20 - kLabelW), 8},
                               static_cast<float>(static_cast<double>(g->usedBytes) / static_cast<double>(g->budgetBytes)));
                y += 14;
            }
            if (!g->driver.empty()) row(Tr(L"ドライバー"), g->driver);
            row(Tr(L"Direct3D の機能レベル"), g->featureLevel);
        } else {
            row(Tr(L"GPU"), Tr(L"(読めません)"));
        }
        row(Tr(L"このソフトのメモリ"), TrF(L"{} (確保 {})", Mb(system_->memory.workingSet), Mb(system_->memory.committed)));
        gap();
    }

    contentH = y - top + 10;
    w::EndScroll(c, "debug.scroll", view, contentH);
}

}  // namespace rm::game_production
