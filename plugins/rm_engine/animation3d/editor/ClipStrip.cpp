#include "rm_engine/animation3d/editor/ClipStrip.h"

#include "base/Utf.h"
#include "core/asset/ImportSettings.h"
#include "core/reflect/Data.h"
#include "editor/inspector/Inspector.h"
#include "editor/lang/Lang.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "rm_engine/animation3d/editor/ClipEvents.h"
#include "rm_engine/animation3d/runtime/RootMotion.h"
#include "rm_engine/space3d/runtime/Mesh.h"
#include "rm_engine/space3d/runtime/MeshLoader.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace rm::animation3d {

namespace {

namespace theme = editor::theme;
namespace w = editor::w;
using editor::Tr;
using editor::TrF;
using ui::Rect;

// --- モデルのクリップの一覧 (別のスレッドで読んで覚える) -------------------------------------------
struct ClipInfo {
    std::string name;
    float duration = 0;  // 秒
};
struct Listing {
    bool known = false;  // 1 度は読み終えた (`ok` と `clips` が使える)
    bool ok = false;     // ... そして読めた
    std::vector<ClipInfo> clips;
    // ルートモーションの「1 回で動く分」を出すための骨の木とクリップ (骨が無いモデル: null)。
    std::shared_ptr<const space3d::Skeleton> skeleton;
    std::shared_ptr<const std::vector<space3d::Clip>> data;
    std::filesystem::file_time_type time{};  // 読んだときのファイルと .meta の新しい方の時刻
    std::chrono::steady_clock::time_point checked{};
    space3d::MeshLoader::Ticket ticket = 0;  // 読んでいる最中 (0: なし)
    std::filesystem::file_time_type reading{};  // ... その時刻
};

class Catalog {
public:
    // `file` の今わかっている一覧。読み直している間は前の物のまま (時刻を見るのは 1 秒に 1 回)。
    const Listing& Look(const std::filesystem::path& file) {
        Listing& l = lists_[file.native()];
        if (l.ticket != 0) {
            if (auto done = loader_.Take(l.ticket)) {
                l.ticket = 0;
                l.known = true;
                l.ok = done->mesh.has_value();
                l.time = l.reading;
                l.clips.clear();
                l.skeleton.reset();
                l.data.reset();
                if (done->mesh) {
                    for (const space3d::Clip& c : done->mesh->clips) l.clips.push_back({c.name, c.duration});
                    if (done->mesh->skin) l.skeleton = std::make_shared<const space3d::Skeleton>(done->mesh->skin->skeleton);
                    l.data = std::make_shared<const std::vector<space3d::Clip>>(std::move(done->mesh->clips));
                }
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (l.ticket == 0 && now - l.checked >= std::chrono::seconds(1)) {
            l.checked = now;
            std::error_code ec;
            const auto t = asset::ChangedAt(file, ec);
            if (ec) {
                l.known = true;  // (無い・開けない: 何も出さない。読めないことはシーンビューがコンソールに出す)
                l.ok = false;
                l.clips.clear();
            } else if (!l.known || t != l.time) {
                l.reading = t;
                l.ticket = loader_.Request(file);
            }
        }
        return l;
    }

private:
    space3d::MeshLoader loader_;  // 大きな FBX でも画面を止めない
    std::unordered_map<std::wstring, Listing> lists_;  // フルパスごと
};

Catalog& TheCatalog() {
    static Catalog catalog;
    return catalog;
}

// --- 1 つのモデルの「クリップとイベント」のブロックの見せ方 ---------------------------------------------------
struct Strip {
    editor::AssetImportAccess access;
    std::string clip;  // 選んだクリップの名前
    int event = -1;    // 選んだイベントの番号 (時刻の順。-1: なし)
    float cursor = -1; // 帯をクリックした時刻 = 「＋ イベント」で足す所 (-1: クリップの真ん中)
    // ▲ のドラッグ
    bool pressing = false, moved = false;
    float pressX = 0, grab = 0;  // 押した x と、印の x からのずれ
    uint64_t merge = 0;          // そのドラッグの Undo の鍵
    // 「イベント」のブロックの欄が見せるもの (描くたびに .meta の今の値から作る)
    ClipEvent shown;
    reflect::Unknowns shownUnknowns;
    // ルートモーションの「根の骨」の ▾ (親の無い骨の名前。0 番は「自動」)
    w::Popup bonePopup;
    std::vector<std::string> boneChoices;
    int boneChosen = -1;
};

// ルートモーションの 1 行と、その下の「1 回で …」の行。
constexpr float kRootRowH = 26.f;

constexpr float kRowH = 24.f;
constexpr float kGap = 6.f;
constexpr float kButtonH = 28.f;

ui::TextStyle Style(float size, ui::Color color, ui::HAlign h = ui::HAlign::Left) {
    ui::TextStyle s;
    s.size = size;
    s.color = color;
    s.h = h;
    return s;
}

json::Value ClipsOf(const json::Value& import) { return import.IsObject() ? import.Get(kClipsKey) : json::Value(); }

// 今のデータ (描いている間に変わっていてもよい) で、選んだイベントを消す。
void RemoveSelected(Strip& s) {
    const json::Value clips = ClipsOf(s.access.import());
    const std::vector<ClipEvent> events = ReadClipEvents(clips, s.clip);
    if (s.event < 0 || static_cast<size_t>(s.event) >= events.size()) return;
    s.access.setImportKey(kClipsKey, RemoveClipEvent(clips, s.clip, static_cast<size_t>(s.event)), "イベントを消す", 0);
    s.event = -1;
}

float StripHeight(size_t clipCount, bool root) {
    return static_cast<float>(clipCount) * kRowH + kGap + w::kTimeStripHeight + kGap + kButtonH + (root ? kGap * 2 + kRootRowH + kRowH : 0.f);
}

struct RootView {
    std::shared_ptr<const space3d::Skeleton> skeleton;
    std::shared_ptr<const std::vector<space3d::Clip>> data;
};

// ルートモーション (docs/ANIMATION.md 7-1。見本 A: 帯の下に 1 行):
//   ルートモーション [☑ 使う] [自動 (Hips) ▾] [☑ 横] [☐ 縦] [☑ 回転]
//   1 回で 前へ 1.42 m · 右へ 0.05 m · 回る 0°
// 変えるたびに .meta の "clips" の "root" を 1 件の Undo で書く (イベントと同じ道)。
void DrawRootRow(ui::Context& c, const Rect& r, Strip& s, const json::Value& data, const RootView& view) {
    auto& p = c.P();
    const ClipRootMotion now = ReadClipRoot(data, s.clip);
    ClipRootMotion want = now;
    float x = r.x;
    const std::wstring title = Tr(L"ルートモーション");
    const float titleW = p.TextWidth(title, 12) + 12;
    p.Text({x, r.y, titleW, kRootRowH}, title, Style(12, theme::TextDim));
    x += titleW;
    auto check = [&](const char* id, bool& v, std::wstring_view label) {
        const float wdt = p.TextWidth(label, 12) + 30;
        w::Checkbox(c, id, {x, r.y, wdt, kRootRowH}, v, label);
        x += wdt + 6;
    };
    check("root.use", want.enabled, Tr(L"使う"));
    // 根の骨: 親の無い骨だけ (6 章の 9)。空 = 自動。
    const int autoBone = view.skeleton ? RootBone(*view.skeleton, "") : -1;
    const std::wstring autoName = autoBone >= 0 ? TrF(L"自動 ({})", Utf8ToWide(view.skeleton->bones[static_cast<size_t>(autoBone)].name)) : Tr(L"自動");
    const std::wstring boneText = want.bone.empty() ? autoName : Utf8ToWide(want.bone);
    const float comboW = std::clamp(p.TextWidth(boneText, 12) + 40, 110.f, 220.f);
    if (w::ComboBox(c, "root.bone", {x, r.y + 1, comboW, kRootRowH - 2}, boneText, want.enabled).clicked) {
        s.boneChoices = {std::string()};
        if (view.skeleton) {
            for (const space3d::Bone& b : view.skeleton->bones) {
                if (b.parent < 0 && b.name != space3d::kStaticBoneName) s.boneChoices.push_back(b.name);
            }
        }
        s.bonePopup.OpenAt(x, r.y + kRootRowH);
    }
    x += comboW + 8;
    check("root.h", want.horizontal, Tr(L"横"));
    check("root.v", want.vertical, Tr(L"縦"));
    check("root.t", want.turn, Tr(L"回転"));
    if (const int chosen = std::exchange(s.boneChosen, -1); chosen >= 0 && static_cast<size_t>(chosen) < s.boneChoices.size()) {
        want.bone = s.boneChoices[static_cast<size_t>(chosen)];
    }
    if (!(want == now)) {
        s.access.setImportKey(kClipsKey, SetClipRoot(data, s.clip, want), "ルートモーションを変える", 0);
        c.RequestRedraw();
    }
    if (s.bonePopup.open) {
        c.AddOverlay([sp = &s, &c, autoName] {
            const bool was = c.InputEnabled();
            c.SetInputEnabled(true);
            std::vector<w::MenuItem> items;
            for (const std::string& name : sp->boneChoices) items.push_back({name.empty() ? autoName : Utf8ToWide(name)});
            if (const int k = w::MenuPopup(c, sp->bonePopup, items, 220); k >= 0) {
                sp->boneChosen = k;
                c.RequestRedraw();
            }
            c.SetInputEnabled(was);
        });
    }

    // 1 回で動く分 (今の設定で。根の骨が選べないときは理由)。
    const Rect line{r.x, r.y + kRootRowH, r.w, kRowH};
    if (!want.enabled) {
        p.Text(line, Tr(L"使うと、根の骨の動きを姿勢から取り出して、アニメーターのエンティティを動かします"), Style(11, theme::TextFaint));
        return;
    }
    const space3d::Clip* clip = nullptr;
    if (view.data) {
        for (const space3d::Clip& k : *view.data) {
            if (k.name == s.clip) clip = &k;
        }
    }
    const int root = view.skeleton ? RootBone(*view.skeleton, want.bone) : -1;
    if (!clip || !view.skeleton || root < 0) {
        p.Text(line, want.bone.empty() ? Tr(L"根にできる骨がありません (親の無い骨だけ選べます)") : TrF(L"骨「{}」が無いか、親があります (親の無い骨だけ選べます)", Utf8ToWide(want.bone)),
               Style(11, theme::Warn));
        return;
    }
    const RootDelta d = RootTotals(*clip, BindClip(*clip, *view.skeleton), *view.skeleton, root, want);
    p.Text(line, TrF(L"1 回で 前へ {:.2f} m · 右へ {:.2f} m · 上へ {:.2f} m · 回る {:.0f}°", d.move.z, d.move.x, d.move.y, d.turn * 57.29578f),
           Style(11, theme::TextDim));
}

void DrawStrip(ui::Context& c, const Rect& r, const std::shared_ptr<Strip>& sp, const std::vector<ClipInfo>& clips, bool reading, const RootView& view) {
    Strip& s = *sp;
    auto& p = c.P();
    if (reading) c.RequestAnimation();  // (読み直しが終わったら出す)
    const json::Value data = ClipsOf(s.access.import());
    const ClipInfo* cur = nullptr;
    for (const ClipInfo& ci : clips) {
        if (ci.name == s.clip) cur = &ci;
    }
    if (!cur) return;
    const float duration = cur->duration;
    const std::vector<ClipEvent> events = ReadClipEvents(data, s.clip);

    // クリップの一覧。
    float y = r.y;
    for (size_t i = 0; i < clips.size(); ++i) {
        const Rect row{r.x, y, r.w, kRowH};
        const bool selected = clips[i].name == s.clip;
        c.PushId(static_cast<uint64_t>(i));
        const auto b = w::Selectable(c, "clip", row, selected);
        c.PopId();
        const size_t count = ReadClipEvents(data, clips[i].name).size();
        const std::wstring right = count > 0 ? TrF(L"イベント {}  ·  {:.2f} 秒", count, clips[i].duration) : TrF(L"{:.2f} 秒", clips[i].duration);
        const float rightW = std::min(row.w * 0.5f, p.TextWidth(right, 12) + 8);
        p.Text({row.x + 8, row.y, row.w - rightW - 16, row.h}, Utf8ToWide(clips[i].name), Style(12, selected ? theme::Text : theme::TextDim));
        p.Text({row.Right() - rightW - 8, row.y, rightW, row.h}, right, Style(12, theme::TextFaint, ui::HAlign::Right));
        if (b.clicked && !selected) {
            s.clip = clips[i].name;
            s.event = -1;
            s.cursor = -1;
            s.pressing = false;
            c.RequestRedraw();
        }
        y += kRowH;
    }
    y += kGap;

    // 帯: 印のどれの上か (一番近いもの)。
    const Rect strip{r.x, y, r.w, w::kTimeStripHeight};
    const float mx = c.In().mouseX;
    int hovered = -1;
    float best = 1e9f;
    for (size_t i = 0; i < events.size(); ++i) {
        const float x = w::TimeStripX(strip, duration, events[i].time);
        if (c.IsHovered(w::TimeMarkerRect(strip, x)) && std::abs(mx - x) < best) {
            best = std::abs(mx - x);
            hovered = static_cast<int>(i);
        }
    }
    const ui::Id stripId = c.MakeId("strip");
    if (s.pressing) {
        // ▲ をドラッグしている: 動いた分だけ時刻を変える (1 回のドラッグが Undo の 1 件)。
        if (!c.In().down[0] || s.event < 0 || static_cast<size_t>(s.event) >= events.size()) {
            s.pressing = false;
        } else {
            c.SetCursor(ui::Cursor::SizeWE);
            if (!s.moved && std::abs(mx - s.pressX) >= 3.f) {
                s.moved = true;
                s.merge = editor::inspector::NewMergeKey();
            }
            if (s.moved) {
                const float t = ClampEventTime(w::TimeStripTime(strip, duration, mx - s.grab), duration);
                const size_t at = static_cast<size_t>(s.event);
                if (t != events[at].time) {
                    size_t moved = at;
                    s.access.setImportKey(kClipsKey, MoveClipEvent(data, s.clip, at, t, duration, &moved), "イベントを動かす", s.merge);
                    s.event = static_cast<int>(moved);
                    c.RequestRedraw();
                }
            }
        }
    } else if (hovered >= 0 && c.Active() != stripId) {
        c.SetCursor(ui::Cursor::SizeWE);
        if (c.MousePressed()) {
            // ▲ を押した: 選ぶ (動かせばドラッグ)。
            s.pressing = true;
            s.moved = false;
            s.pressX = mx;
            s.grab = mx - w::TimeStripX(strip, duration, events[static_cast<size_t>(hovered)].time);
            s.event = hovered;
            c.RequestRedraw();
        }
    } else {
        // 帯のほかの所: 押した (ドラッグした) 所が、「＋ イベント」で足す所。
        const auto b = c.ButtonBehavior(stripId, strip);
        if (b.hovered || b.held) c.SetCursor(ui::Cursor::Hand);
        if (b.held) {
            const float t = w::TimeStripTime(strip, duration, mx);
            if (t != s.cursor) {
                s.cursor = t;
                c.RequestRedraw();
            }
        }
    }
    w::DrawTimeStrip(c, strip, duration, s.cursor >= 0 ? std::min(s.cursor, duration) : -1.f);
    auto marker = [&](size_t k) {
        const bool sel = static_cast<int>(k) == s.event;
        w::DrawTimeMarker(c, strip, w::TimeStripX(strip, duration, events[k].time), Utf8ToWide(events[k].name), sel, static_cast<int>(k) == hovered);
    };
    for (size_t i = 0; i < events.size(); ++i) {
        if (static_cast<int>(i) != s.event) marker(i);
    }
    if (s.event >= 0 && static_cast<size_t>(s.event) < events.size()) marker(static_cast<size_t>(s.event));  // 選んだものは上に
    y += w::kTimeStripHeight + kGap;

    // ＋ イベント / 削除 と、足す所。
    const std::wstring addLabel = Tr(L"＋ イベント");
    const Rect add{r.x, y, w::ButtonWidth(c, addLabel), kButtonH};
    if (w::Button(c, "add", add, addLabel)) {
        ClipEvent e;
        e.time = s.cursor >= 0 ? s.cursor : duration * 0.5f;
        size_t at = 0;
        s.access.setImportKey(kClipsKey, AddClipEvent(data, s.clip, e, duration, &at), "イベントを足す", 0);
        s.event = static_cast<int>(at);
        c.RequestRedraw();
    }
    const std::wstring removeLabel = Tr(L"削除");
    const Rect remove{add.Right() + 6, y, w::ButtonWidth(c, removeLabel), kButtonH};
    if (w::Button(c, "remove", remove, removeLabel, w::ButtonKind::Secondary, s.event >= 0 && static_cast<size_t>(s.event) < events.size())) {
        RemoveSelected(s);
        c.RequestRedraw();
    }
    const std::wstring where = s.cursor >= 0 ? TrF(L"足す所 {:.2f} 秒", std::min(s.cursor, duration)) : Tr(L"帯をクリックすると、足す所を選べます");
    const float hx = remove.Right() + 10;
    if (r.Right() - hx > 40) p.Text({hx, y, r.Right() - hx, kButtonH}, where, Style(11, theme::TextFaint, ui::HAlign::Right));
    y += kButtonH + kGap;

    // ルートモーション (骨のあるモデルだけ)。
    if (view.skeleton) {
        w::Separator(c, r.x, y, r.w);
        DrawRootRow(c, {r.x, y + kGap, r.w, kRootRowH + kRowH}, s, data, view);
    }
}

std::vector<editor::InspectBlock> BlocksOf(const std::shared_ptr<Strip>& sp) {
    Strip& s = *sp;
    std::vector<editor::InspectBlock> out;
    const std::filesystem::path file = s.access.file ? s.access.file() : std::filesystem::path();
    if (file.empty()) return out;
    const Listing& l = TheCatalog().Look(file);
    if (!l.known) {
        if (l.ticket != 0) {
            // 初めて読んでいる (大きな FBX)。
            editor::InspectBlock b;
            b.title = Tr(L"クリップとイベント");
            b.customHeight = [](ui::Context&, float) { return 20.f; };
            b.customDraw = [](ui::Context& c, const Rect& r) {
                c.P().Text(r, Tr(L"クリップを読んでいます…"), Style(12, theme::TextFaint));
                c.RequestAnimation();
            };
            out.push_back(std::move(b));
        }
        return out;
    }
    if (!l.ok || l.clips.empty()) return out;  // クリップの無いモデル: 何も出さない

    // 選んだクリップ (無くなっていれば最初のもの) と、選んだイベント。
    const ClipInfo* cur = nullptr;
    for (const ClipInfo& ci : l.clips) {
        if (ci.name == s.clip) cur = &ci;
    }
    if (!cur) {
        cur = &l.clips.front();
        s.clip = cur->name;
        s.event = -1;
        s.cursor = -1;
    }
    const float duration = cur->duration;
    const std::vector<ClipEvent> events = ReadClipEvents(ClipsOf(s.access.import()), s.clip);
    if (s.event >= static_cast<int>(events.size())) s.event = -1;  // (Undo で減った)

    editor::InspectBlock strip;
    strip.title = Tr(L"クリップとイベント");
    strip.note = Tr(L"帯の ▲ がイベントです。再生がそこを通ると、スクリプトの OnEvent に名前・数・文字が届きます。▲ はドラッグで動かせます (Ctrl+Z で戻せます)");
    const size_t count = l.clips.size();
    const bool reading = l.ticket != 0;
    std::vector<ClipInfo> clips = l.clips;
    const RootView view{l.skeleton, l.data};
    const bool root = view.skeleton != nullptr;
    strip.customHeight = [count, root](ui::Context&, float) { return StripHeight(count, root); };
    strip.customDraw = [sp, clips = std::move(clips), reading, view](ui::Context& c, const Rect& r) { DrawStrip(c, r, sp, clips, reading, view); };
    out.push_back(std::move(strip));

    if (s.event >= 0) {
        // 選んだイベントの欄 (インスペクターの欄: 打つ間は Undo の 1 件)。
        s.shown = events[static_cast<size_t>(s.event)];
        s.shownUnknowns = {};
        editor::InspectBlock fields;
        fields.type = &reflect::TypeOf<ClipEvent>();
        fields.title = Tr(L"イベント");
        fields.resolve = [sp]() -> reflect::EditTarget { return {&sp->shown, &sp->shownUnknowns}; };
        w::Action remove;
        remove.label = Tr(L"削除");
        remove.run = [sp] { RemoveSelected(*sp); };
        fields.menu.push_back(std::move(remove));
        fields.onEdit = [sp, duration](editor::InspectBlock::Change ch) {
            Strip& st = *sp;
            const json::Value clipsNow = ClipsOf(st.access.import());
            const std::vector<ClipEvent> now = ReadClipEvents(clipsNow, st.clip);
            if (st.event < 0 || static_cast<size_t>(st.event) >= now.size()) return;
            ClipEvent e = now[static_cast<size_t>(st.event)];
            reflect::Unknowns unknowns;
            const reflect::PathTarget pt = reflect::Resolve(reflect::TypeOf<ClipEvent>(), &e, &unknowns, ch.path);
            if (!pt.field) return;
            reflect::LoadField(*pt.field, pt.owner, ch.after, pt.unknowns, nullptr);
            size_t at = static_cast<size_t>(st.event);
            st.access.setImportKey(kClipsKey, SetClipEvent(clipsNow, st.clip, at, e, duration, &at), std::move(ch.label), ch.merge);
            st.event = static_cast<int>(at);
        };
        out.push_back(std::move(fields));
    }
    return out;
}

}  // namespace

std::function<std::vector<editor::InspectBlock>()> OpenClipStrip(const editor::AssetImportAccess& access) {
    auto s = std::make_shared<Strip>();
    s->access = access;
    return [s] { return BlocksOf(s); };
}

}  // namespace rm::animation3d
