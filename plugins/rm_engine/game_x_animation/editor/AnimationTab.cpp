#include "rm_engine/game_x_animation/editor/AnimationTab.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "core/reflect/Data.h"
#include "core/reflect/FieldCommand.h"
#include "editor/lang/Lang.h"
#include "editor/project/Project.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "platform/Shell.h"
#include "rm_engine/animation/runtime/Controller.h"
#include "rm_engine/animation/runtime/Rules.h"
#include "rm_engine/game_x_animation/editor/Connector.h"
#include "rm_engine/game_x_animation/runtime/Animator.h"
#include "rm_engine/game_x_animation/runtime/AnimatorRun.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rm::game_x_animation {

namespace {

namespace fs = std::filesystem;
namespace w = editor::w;
namespace theme = editor::theme;
using animation::AnimController;
using animation::AnimParam;
using animation::AnimRule;
using animation::AnimState;
using animation::ParamType;
using animation::RuleCheck;
using animation::StateExit;
using editor::Tr;
using editor::TrF;

constexpr float kPad = 8, kRow = 26, kNote = 16, kHead = 26, kIcon = 24;

// ファイルの 1 回の編集: 前と後の中身 (1 つの欄に打つ間は 1 件)。
class RulesCommand final : public ICommand {
public:
    RulesCommand(editor::SoftwareHost& host, AssetRef ref, std::string before, std::string after, std::string label, uint64_t merge)
        : host_(host), ref_(std::move(ref)), before_(std::move(before)), after_(std::move(after)), label_(std::move(label)), merge_(merge) {}
    void Do() override { host_.SetAssetContent(ref_, after_); }
    void Undo() override { host_.SetAssetContent(ref_, before_); }
    std::string Label() const override { return label_; }
    bool IsNoOp() const override { return before_ == after_; }
    bool CanMergeWith(const ICommand& next) const override {
        const auto* n = dynamic_cast<const RulesCommand*>(&next);
        return n && merge_ != 0 && n->merge_ == merge_ && n->ref_ == ref_;
    }
    bool MergeWith(const ICommand& next) override {
        if (!CanMergeWith(next)) return false;
        after_ = static_cast<const RulesCommand&>(next).after_;
        return true;
    }
    size_t Bytes() const override { return before_.size() + after_.size(); }

private:
    editor::SoftwareHost& host_;
    AssetRef ref_;
    std::string before_, after_, label_;
    uint64_t merge_;
};

// 欄ごとの「続けて打つ間は 1 件」の番号 (0 は使わない)。
uint64_t MergeOf(std::string_view key) {
    uint64_t h = 1469598103934665603ull;
    for (const char ch : key) h = (h ^ static_cast<unsigned char>(ch)) * 1099511628211ull;
    return h == 0 ? 1 : h;
}

// `ref` の状態とルール (エディターが今持っているコピー) を変えて、履歴に積む。
void EditRules(editor::SoftwareHost& host, const AssetRef& ref, const std::string& label, uint64_t merge, const std::function<void(AnimController&)>& change) {
    const auto bytes = host.AssetContent(ref);
    if (!bytes) return;
    reflect::Unknowns unknowns;
    std::string err;
    auto c = animation::ReadController(*bytes, &err, &unknowns);
    if (!c) return;
    change(*c);
    std::string after = animation::WriteController(*c, &unknowns);
    if (after == *bytes) return;
    host.History().Push(std::make_unique<RulesCommand>(host, ref, *bytes, std::move(after), label, merge), false);
}

void Note(ui::Context& c, const ui::Rect& r, const std::wstring& text, ui::Color color = theme::TextDim, float size = 13) {
    ui::TextStyle ts;
    ts.wrap = true;
    ts.size = size;
    ts.color = color;
    ts.v = ui::VAlign::Top;
    c.P().Text(r, text, ts);
}

void Label(ui::Context& c, const ui::Rect& r, std::wstring_view text, ui::Color color = theme::Text, bool bold = false, float size = 13) {
    ui::TextStyle ts;
    ts.size = size;
    ts.bold = bold;
    ts.color = color;
    ts.v = ui::VAlign::Center;
    c.P().Text(r, text, ts);
}

// 行ごとの調べた結果を、短い言葉に。
std::wstring CheckText(RuleCheck check) {
    switch (check) {
        case RuleCheck::Chosen: return Tr(L"✓ これで決まった");
        case RuleCheck::False: return Tr(L"✗ 条件が偽");
        case RuleCheck::NotHere: return Tr(L"― 元が今の状態ではない");
        case RuleCheck::Blocked: return Tr(L"― 今の状態の出口で調べない");
        case RuleCheck::Below: return Tr(L"― 上の行で決まった");
        case RuleCheck::Broken: return Tr(L"✗ 当たらない");
    }
    return {};
}

class AnimationTab final : public game_production::SceneView {
public:
    void Draw(ui::Context& c, const ui::Rect& r, game_production::SceneViewHost& host) override {
        if (r.w < 60 || r.h < 60) return;
        const std::optional<Target> target = FindTarget(host);
        if (!target) {
            Note(c, r.Inset(24),
                 Tr(L"「アニメーター」の部品を付けたエンティティを選ぶと、その状態とルールをここで作れます。ルールは上から調べられ、元が合って条件が真の最初の 1 行で状態が決まります。"));
            return;
        }
        if (target->rules.Empty()) {
            const ui::Rect box = r.Inset(24);
            Note(c, {box.x, box.y, box.w, 60},
                 Tr(L"このアニメーターには、まだ状態とルールがありません (クリップの欄のクリップが流れています)。作ると Assets/Animations に入り、ここで変えられます。"));
            if (w::Button(c, "anim_tab.create", {box.x, box.y + 70, 220, 30}, Tr(L"状態とルールを作る"), w::ButtonKind::Primary)) Create(host, *target);
            return;
        }
        if (target->rules != ref_) {
            ref_ = target->rules;
            bytes_.clear();
            controller_.reset();
            texts_.clear();
        }
        Load(host);
        if (!controller_) {
            Note(c, r.Inset(24), TrF(L"状態とルールのファイルを読めません: {}", Utf8ToWide(loadError_)));
            return;
        }
        const AnimController& ctl = *controller_;
        AnimatorRun* run = host.playing ? target->run : nullptr;
        const bool live = run && run->rules;

        // 帯: ファイルの名前と、Play 中の今の状態。
        const ui::Rect top{r.x + kPad, r.y + kPad, r.w - 2 * kPad, kHead};
        Label(c, {top.x, top.y, top.w * 0.4f, top.h}, host.host.AssetFile(ref_).filename().wstring(), theme::Text, true);
        std::wstring now;
        if (live) {
            now = TrF(L"今: {} ・ t {:.2f} ・ ルール {}", Utf8ToWide(std::string(StateName(*run))), StateFraction(*run), run->rule >= 0 ? std::to_wstring(run->rule + 1) : L"-");
        } else {
            now = host.playing ? Tr(L"Play 中: このアニメーターはまだ動いていません (モデルを読んでいます)") : Tr(L"Play すると、今の状態と当たった行が光ります");
        }
        Label(c, {top.x + top.w * 0.4f, top.y, top.w * 0.6f, top.h}, now, live ? theme::Accent : theme::TextDim);

        const ui::Rect view{r.x, top.Bottom() + 4, r.w, r.Bottom() - top.Bottom() - 4};
        const float rulesH = static_cast<float>(ctl.rules.size()) * (kRow + kNote + 4);
        const float content = 3 * (kHead + 8) + static_cast<float>(ctl.params.size()) * (kRow + 4) + rulesH + static_cast<float>(ctl.states.size()) * (kRow + 4) +
                              3 * (kRow + 10) + 2 * kPad;
        const float off = w::BeginScroll(c, "anim_tab.scroll", view, content);
        float y = view.y + kPad - off;
        const float x = r.x + kPad, wdt = r.w - 2 * kPad - 10;
        y = DrawParams(c, host, ctl, run, x, y, wdt);
        y = DrawRules(c, host, ctl, live ? run : nullptr, x, y + 8, wdt);
        y = DrawStates(c, host, ctl, live ? run : nullptr, *target, x, y + 8, wdt);
        w::EndScroll(c, "anim_tab.scroll", view, content);
        if (live) c.RequestAnimation();
        ClipMenu(c, host);
    }

private:
    struct Target {
        int entity = 0;
        AssetRef rules;
        std::string name;
        AnimatorRun* run = nullptr;  // Play 中のもの (無い: null)
    };

    // アニメーター を持つ最初に選んだエンティティ。
    static std::optional<Target> FindTarget(const game_production::SceneViewHost& host) {
        const std::string type = reflect::TypeOf<Animator>().name;
        for (const int id : host.selection) {
            const game_production::SceneEntity* e = host.scene.Find(id);
            const game_production::Component* comp = e ? e->FindComponent(type) : nullptr;
            if (!comp || !comp->Known()) continue;
            const auto& a = *static_cast<const Animator*>(comp->value.Get());
            return Target{id, a.rules, e->data.name, a.run.get()};
        }
        return std::nullopt;
    }

    // エディターが今持っている中身 (変わったときだけ読み直す)。
    void Load(game_production::SceneViewHost& host) {
        const auto bytes = host.host.AssetContent(ref_);
        if (!bytes) {
            controller_.reset();
            loadError_ = editor::TrU8("ファイルが見つかりません");
            return;
        }
        if (controller_ && *bytes == bytes_) return;
        bytes_ = *bytes;
        std::string err;
        auto c = animation::ReadController(*bytes, &err);
        if (!c) {
            controller_.reset();
            loadError_ = err;
            return;
        }
        compiled_ = animation::Compile(*c);
        controller_ = std::make_shared<const AnimController>(std::move(*c));
    }

    void Edit(game_production::SceneViewHost& host, const std::string& label, uint64_t merge, const std::function<void(AnimController&)>& change) {
        EditRules(host.host, ref_, label, merge, change);
    }

    std::string& Text(const std::string& id) { return texts_[id]; }

    // 見出しと、その右の ＋ のボタン。
    bool Heading(ui::Context& c, std::string_view id, float x, float y, float wdt, std::wstring_view title, std::wstring_view add) {
        Label(c, {x, y, wdt - 160, kHead}, title, theme::Text, true);
        w::Separator(c, x, y + kHead + 2, wdt);
        return w::Button(c, id, {x + wdt - 150, y, 150, kHead - 2}, add);
    }

    float DrawParams(ui::Context& c, game_production::SceneViewHost& host, const AnimController& ctl, AnimatorRun* run, float x, float y, float wdt) {
        if (Heading(c, "anim_tab.add_param", x, y, wdt, Tr(L"パラメーター"), Tr(L"＋ パラメーター"))) {
            Edit(host, "パラメーターを足す", 0, [](AnimController& a) {
                std::string name = "param";
                for (int n = 1; std::any_of(a.params.begin(), a.params.end(), [&](const AnimParam& p) { return p.name == name; }); ++n) name = std::format("param{}", n);
                a.params.push_back({name, ParamType::Float, 0});
            });
        }
        y += kHead + 8;
        const std::wstring types[] = {Tr(L"数"), Tr(L"整数"), Tr(L"真偽"), Tr(L"トリガー")};
        const std::wstring_view typeLabels[] = {types[0], types[1], types[2], types[3]};
        for (size_t i = 0; i < ctl.params.size(); ++i) {
            const AnimParam& p = ctl.params[i];
            c.PushId(static_cast<uint64_t>(i));
            const float nameW = std::floor(wdt * 0.24f), typeW = std::floor(wdt * 0.36f);
            std::string name = p.name;
            if (w::TextField(c, "name", {x, y, nameW, kRow}, name, Tr(L"名前"))) {
                const std::string old = p.name;
                Edit(host, "パラメーターの名前", MergeOf(std::format("p{}/name", i)), [i, name, old](AnimController& a) {
                    if (i >= a.params.size()) return;
                    a.params[i].name = name;
                    // 式の中の名前も書き換える (文字で書く弱点を埋める: docs/ANIMATION.md 4-8)。
                    if (old.empty()) return;
                    for (AnimRule& r : a.rules) {
                        const animation::Condition cond = animation::Condition::Parse(r.condition);
                        if (!cond.ok() || std::find(cond.names().begin(), cond.names().end(), old) == cond.names().end()) continue;
                        r.condition = RenameIn(r.condition, old, name);
                    }
                });
            }
            const int pick = w::Segmented(c, "type", {x + nameW + 6, y, typeW, kRow}, typeLabels, static_cast<int>(p.type));
            if (pick >= 0 && pick != static_cast<int>(p.type)) {
                Edit(host, "パラメーターの種類", 0, [i, pick](AnimController& a) {
                    if (i < a.params.size()) a.params[i].type = static_cast<ParamType>(pick);
                });
            }
            float vx = x + nameW + typeW + 12;
            if (p.type != ParamType::Trigger) {
                Label(c, {vx, y, 34, kRow}, Tr(L"初め"), theme::TextDim);
                float v = p.value;
                if (w::NumberField(c, "start", {vx + 34, y, 60, kRow}, v, -1e6f, 1e6f, Text(std::format("ps{}", i)))) {
                    Edit(host, "パラメーターの初めの値", MergeOf(std::format("p{}/start", i)), [i, v](AnimController& a) {
                        if (i < a.params.size()) a.params[i].value = v;
                    });
                }
            }
            vx += 100;
            if (run && run->rules) {
                // Play 中: 今の値 (変えて試す)。
                Label(c, {vx, y, 22, kRow}, Tr(L"今"), theme::Accent);
                const float cur = RuleParam(*run, p.name);
                if (p.type == ParamType::Trigger) {
                    if (w::Button(c, "pull", {vx + 24, y, 64, kRow}, cur != 0 ? Tr(L"立っている") : Tr(L"引く"))) SetRuleParam(*run, p.name, 1.f, true);
                } else if (p.type == ParamType::Bool) {
                    bool on = cur != 0;
                    if (w::Checkbox(c, "now", {vx + 24, y, 64, kRow}, on, {})) SetRuleParam(*run, p.name, on ? 1.f : 0.f, false);
                } else {
                    float v = cur;
                    if (w::NumberField(c, "now", {vx + 24, y, 64, kRow}, v, -1e6f, 1e6f, Text(std::format("pn{}", i)))) SetRuleParam(*run, p.name, v, false);
                }
            }
            if (w::IconButton(c, "remove", {x + wdt - kIcon, y + 1, kIcon, kIcon}, w::Icon::Close)) {
                Edit(host, "パラメーターを消す", 0, [i](AnimController& a) {
                    if (i < a.params.size()) a.params.erase(a.params.begin() + static_cast<std::ptrdiff_t>(i));
                });
            }
            c.PopId();
            y += kRow + 4;
        }
        if (ctl.params.empty()) {
            Note(c, {x, y, wdt, kRow}, Tr(L"まだありません。スクリプトが入れる値 (speed・jump …) を足します"));
            y += kRow;
        }
        return y;
    }

    // 式の中の名前 `from` を `to` に (名前の文字の途中は変えない)。
    static std::string RenameIn(const std::string& text, const std::string& from, const std::string& to) {
        auto part = [](unsigned char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch >= 0x80; };
        std::string out;
        size_t i = 0;
        while (i < text.size()) {
            const bool startOk = i == 0 || !part(static_cast<unsigned char>(text[i - 1]));
            if (startOk && text.compare(i, from.size(), from) == 0) {
                const size_t end = i + from.size();
                if (end >= text.size() || !part(static_cast<unsigned char>(text[end]))) {
                    out += to;
                    i = end;
                    continue;
                }
            }
            out += text[i++];
        }
        return out;
    }

    float DrawRules(ui::Context& c, game_production::SceneViewHost& host, const AnimController& ctl, AnimatorRun* run, float x, float y, float wdt) {
        if (Heading(c, "anim_tab.add_rule", x, y, wdt, Tr(L"ルール (上から調べて、最初に当たった行)"), Tr(L"＋ ルール"))) {
            Edit(host, "ルールを足す", 0, [](AnimController& a) {
                AnimRule r;
                r.to = a.states.empty() ? std::string() : a.states.front().name;
                a.rules.push_back(r);
            });
        }
        y += kHead + 8;
        for (size_t i = 0; i < ctl.rules.size(); ++i) {
            const AnimRule& rule = ctl.rules[i];
            c.PushId(static_cast<uint64_t>(1000 + i));
            const RuleCheck check = run && i < run->checks.size() ? run->checks[i] : RuleCheck::Below;
            const bool chosen = run && run->rule == static_cast<int>(i) && check == RuleCheck::Chosen;
            const ui::Rect row{x - 4, y - 2, wdt + 8, kRow + kNote + 4};
            if (chosen) w::DrawRow(c, row, {true, false, true});
            Label(c, {x, y, 22, kRow}, chosen ? L"▶" : std::to_wstring(i + 1), chosen ? theme::Accent : theme::TextDim);
            float cx = x + 22;
            if (w::IconToggle(c, "interrupt", {cx, y + 1, kIcon, kIcon}, w::Icon::Step, rule.interrupt, true, Tr(L"割り込む (⚡): 今の状態の出口が「自分のルールだけ」でも調べる"))) {
                Edit(host, "ルールの割り込む", 0, [i](AnimController& a) {
                    if (i < a.rules.size()) a.rules[i].interrupt = !a.rules[i].interrupt;
                });
            }
            cx += kIcon + 4;
            const float tail = 4 * (kIcon + 2) + 60;  // (フェード・やり直す・↑ ↓ ×)
            const float rest = wdt - (cx - x) - tail;
            const float fromW = std::floor(rest * 0.2f), toW = std::floor(rest * 0.2f), condW = rest - fromW - toW - 24;
            std::string from = rule.from;
            if (w::TextField(c, "from", {cx, y, fromW, kRow}, from, Tr(L"どこでも"))) {
                Edit(host, "ルールの元", MergeOf(std::format("r{}/from", i)), [i, from](AnimController& a) {
                    if (i < a.rules.size()) a.rules[i].from = from;
                });
            }
            cx += fromW + 4;
            std::string cond = rule.condition;
            if (w::TextField(c, "cond", {cx, y, condW, kRow}, cond, Tr(L"(いつも)"))) {
                Edit(host, "ルールの条件", MergeOf(std::format("r{}/cond", i)), [i, cond](AnimController& a) {
                    if (i < a.rules.size()) a.rules[i].condition = cond;
                });
            }
            cx += condW + 2;
            Label(c, {cx, y, 18, kRow}, L"→", theme::TextDim);
            cx += 20;
            std::string to = rule.to;
            if (w::TextField(c, "to", {cx, y, toW, kRow}, to, Tr(L"先の状態"))) {
                Edit(host, "ルールの先", MergeOf(std::format("r{}/to", i)), [i, to](AnimController& a) {
                    if (i < a.rules.size()) a.rules[i].to = to;
                });
            }
            cx += toW + 4;
            float fade = rule.fade;
            if (w::NumberField(c, "fade", {cx, y, 54, kRow}, fade, 0.f, 10.f, Text(std::format("rf{}", i)))) {
                Edit(host, "ルールのフェード", MergeOf(std::format("r{}/fade", i)), [i, fade](AnimController& a) {
                    if (i < a.rules.size()) a.rules[i].fade = fade;
                });
            }
            cx += 58;
            if (w::IconToggle(c, "restart", {cx, y + 1, kIcon, kIcon}, w::Icon::Play, rule.restart, true, Tr(L"やり直す: 先が今の状態と同じでも、始めから流し直す"))) {
                Edit(host, "ルールのやり直す", 0, [i](AnimController& a) {
                    if (i < a.rules.size()) a.rules[i].restart = !a.rules[i].restart;
                });
            }
            cx += kIcon + 2;
            MoveButtons(c, host, cx, y, i, ctl.rules.size(), [](AnimController& a) -> auto& { return a.rules; }, "ルール");
            // 行の下: 問題 (式の間違い・無い状態) と、Play 中の結果。
            std::wstring note;
            ui::Color color = theme::TextFaint;
            const std::string& problem = i < compiled_.rules.size() ? compiled_.rules[i].problem : std::string();
            if (!problem.empty()) {
                note = Utf8ToWide(problem);
                color = compiled_.rules[i].to < 0 ? theme::Danger : theme::Warn;
            }
            if (run) {
                const std::wstring result = CheckText(check);
                note = note.empty() ? result : result + L" ・ " + note;
                if (problem.empty()) color = chosen ? theme::Accent : theme::TextDim;
            }
            if (!note.empty()) Label(c, {x + 50, y + kRow, wdt - 50, kNote}, note, color, false, 11);
            c.PopId();
            y += kRow + kNote + 4;
        }
        if (ctl.rules.empty()) {
            Note(c, {x, y, wdt, kRow * 2}, Tr(L"まだありません。例: 元 = どこでも、条件 = speed > 0.1、先 = Move。一番下に「(いつも) → Idle」を置くと、どれでもないときは待機になります"));
            y += kRow * 2;
        }
        return y;
    }

    // ↑ ↓ × (並びの `list` の i 番目)。
    template <class Pick>
    void MoveButtons(ui::Context& c, game_production::SceneViewHost& host, float cx, float y, size_t i, size_t count, Pick pick, const char* what) {
        if (w::IconButton(c, "up", {cx, y + 1, kIcon, kIcon}, w::Icon::ChevronUp, i > 0)) {
            Edit(host, std::format("{}を上へ", what), 0, [i, pick](AnimController& a) {
                auto& list = pick(a);
                if (i > 0 && i < list.size()) std::swap(list[i - 1], list[i]);
            });
        }
        if (w::IconButton(c, "down", {cx + kIcon + 2, y + 1, kIcon, kIcon}, w::Icon::ChevronDown, i + 1 < count)) {
            Edit(host, std::format("{}を下へ", what), 0, [i, pick](AnimController& a) {
                auto& list = pick(a);
                if (i + 1 < list.size()) std::swap(list[i], list[i + 1]);
            });
        }
        if (w::IconButton(c, "remove", {cx + 2 * (kIcon + 2), y + 1, kIcon, kIcon}, w::Icon::Close)) {
            Edit(host, std::format("{}を消す", what), 0, [i, pick](AnimController& a) {
                auto& list = pick(a);
                if (i < list.size()) list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
            });
        }
    }

    float DrawStates(ui::Context& c, game_production::SceneViewHost& host, const AnimController& ctl, AnimatorRun* run, const Target& target, float x, float y,
                     float wdt) {
        if (Heading(c, "anim_tab.add_state", x, y, wdt, Tr(L"状態 (1 つ目から始まる)"), Tr(L"＋ 状態"))) {
            const std::vector<std::string> clips = ModelClipNames(host.host, host.scene, target.entity);
            Edit(host, "状態を足す", 0, [clips](AnimController& a) {
                std::string name = "State";
                for (int n = 1; std::any_of(a.states.begin(), a.states.end(), [&](const AnimState& s) { return s.name == name; }); ++n) name = std::format("State{}", n);
                AnimState s;
                s.name = name;
                if (!clips.empty()) s.clip = clips.front();
                a.states.push_back(s);
            });
        }
        y += kHead + 8;
        const std::wstring exits[] = {Tr(L"自由"), Tr(L"自分だけ"), Tr(L"終わるまで")};
        const std::wstring exitTips[] = {Tr(L"自由: 全部のルールを調べる"), Tr(L"自分のルールだけ: 元がこの状態のルールと ⚡ のルールだけ (ジャンプの途中で歩きに戻らない)"),
                                         Tr(L"終わるまで自分のルールだけ: クリップが終わるまでは自分のルールだけ、終わったら全部 (攻撃・着地)")};
        const std::wstring_view exitLabels[] = {exits[0], exits[1], exits[2]};
        const std::wstring_view exitTipViews[] = {exitTips[0], exitTips[1], exitTips[2]};
        for (size_t i = 0; i < ctl.states.size(); ++i) {
            const AnimState& s = ctl.states[i];
            c.PushId(static_cast<uint64_t>(5000 + i));
            const bool current = run && run->state == static_cast<int>(i);
            if (current) w::DrawRow(c, {x - 4, y - 2, wdt + 8, kRow + 4}, {true, false, true});
            const float tail = 3 * (kIcon + 2);
            const float exitW = std::floor(wdt * 0.28f), nameW = std::floor(wdt * 0.17f);
            const float clipW = wdt - nameW - exitW - tail - 80 - 60 - 24;
            float cx = x;
            std::string name = s.name;
            if (w::TextField(c, "name", {cx, y, nameW, kRow}, name, Tr(L"名前"))) {
                const std::string old = s.name;
                Edit(host, "状態の名前", MergeOf(std::format("s{}/name", i)), [i, name, old](AnimController& a) {
                    if (i >= a.states.size()) return;
                    a.states[i].name = name;
                    // ルールの元と先も書き換える。
                    if (old.empty()) return;
                    for (AnimRule& r : a.rules) {
                        if (r.from == old) r.from = name;
                        if (r.to == old) r.to = name;
                    }
                });
            }
            cx += nameW + 4;
            std::string clip = s.clip;
            if (w::TextField(c, "clip", {cx, y, clipW, kRow}, clip, Tr(L"クリップ"))) {
                Edit(host, "状態のクリップ", MergeOf(std::format("s{}/clip", i)), [i, clip](AnimController& a) {
                    if (i < a.states.size()) a.states[i].clip = clip;
                });
            }
            cx += clipW;
            if (w::IconButton(c, "pick", {cx, y + 1, 22, kIcon}, w::Icon::ChevronDown)) {
                clipMenuState_ = static_cast<int>(i);
                clipChoices_ = ModelClipNames(host.host, host.scene, target.entity);
                clipMenu_.OpenAt(cx - clipW, y + kRow);
            }
            cx += 26;
            bool loop = s.loop;
            if (w::Checkbox(c, "loop", {cx, y, 76, kRow}, loop, Tr(L"ループ"))) {
                Edit(host, "状態のループ", 0, [i, loop](AnimController& a) {
                    if (i < a.states.size()) a.states[i].loop = loop;
                });
            }
            cx += 80;
            float speed = s.speed;
            if (w::NumberField(c, "speed", {cx, y, 52, kRow}, speed, -10.f, 10.f, Text(std::format("ss{}", i)))) {
                Edit(host, "状態の速さ", MergeOf(std::format("s{}/speed", i)), [i, speed](AnimController& a) {
                    if (i < a.states.size()) a.states[i].speed = speed;
                });
            }
            cx += 60;
            const int pick = w::Segmented(c, "exit", {cx, y, exitW, kRow}, exitLabels, static_cast<int>(s.exit), exitTipViews);
            if (pick >= 0 && pick != static_cast<int>(s.exit)) {
                Edit(host, "状態の出口", 0, [i, pick](AnimController& a) {
                    if (i < a.states.size()) a.states[i].exit = static_cast<StateExit>(pick);
                });
            }
            cx += exitW + 6;
            MoveButtons(c, host, cx, y, i, ctl.states.size(), [](AnimController& a) -> auto& { return a.states; }, "状態");
            c.PopId();
            y += kRow + 4;
        }
        if (ctl.states.empty()) {
            Note(c, {x, y, wdt, kRow}, Tr(L"まだありません。待機・歩く・跳ぶ … を足し、それぞれに流すクリップを選びます"));
            y += kRow;
        }
        return y;
    }

    // 状態のクリップを、モデルのクリップから選ぶメニュー (すべての上に。選んだものは次のフレームで入れる)。
    void ClipMenu(ui::Context& c, game_production::SceneViewHost& host) {
        if (const int chosen = std::exchange(clipChosen_, -1); chosen >= 0 && clipMenuState_ >= 0 && static_cast<size_t>(chosen) < clipChoices_.size()) {
            const size_t i = static_cast<size_t>(clipMenuState_);
            const std::string clip = clipChoices_[static_cast<size_t>(chosen)];
            Edit(host, "状態のクリップ", 0, [i, clip](AnimController& a) {
                if (i < a.states.size()) a.states[i].clip = clip;
            });
        }
        if (!clipMenu_.open) return;
        c.AddOverlay([this, &c] {
            const bool was = c.InputEnabled();
            c.SetInputEnabled(true);
            std::vector<w::MenuItem> items;
            for (const std::string& name : clipChoices_) items.push_back({Utf8ToWide(name)});
            if (items.empty()) items.push_back({Tr(L"(モデルにクリップがありません)"), {}, false});
            if (const int chosen = w::MenuPopup(c, clipMenu_, items, 240); chosen >= 0 && !clipChoices_.empty()) clipChosen_ = chosen;
            c.SetInputEnabled(was);
            if (clipChosen_ >= 0) c.RequestRedraw();
        });
    }

    // 新しいファイル Assets/Animations/<エンティティの名前>.rmanim (待機と動くの見本) を、部品の「状態とルール」に入れる
    // (Undo の 1 件。ファイルは残る)。
    void Create(game_production::SceneViewHost& host, const Target& t) {
        const fs::path dir = host.host.GetProject().AssetsDir() / L"Animations";
        std::error_code ec;
        fs::create_directories(dir, ec);
        std::wstring base = Utf8ToWide(t.name.empty() ? std::string("Animator") : t.name);
        for (wchar_t& ch : base) {
            if (ch < 32 || std::wstring_view(L"\\/:*?\"<>|").find(ch) != std::wstring_view::npos) ch = L'_';
        }
        fs::path file;
        for (int n = 1; n < 1000; ++n) {
            file = dir / ((n == 1 ? base : std::format(L"{} {}", base, n)) + Utf8ToWide(animation::kControllerExtension));
            if (!fs::exists(file, ec)) break;
        }
        // 見本: speed で 待機 ↔ 動く。クリップはモデルの 1 つ目と 2 つ目 (無ければ空)。
        const std::vector<std::string> clips = ModelClipNames(host.host, host.scene, t.entity);
        AnimController sample;
        sample.params = {{"speed", ParamType::Float, 0}};
        sample.states = {{"Idle", clips.empty() ? std::string() : clips[0], true, 1, StateExit::Free},
                         {"Move", clips.size() > 1 ? clips[1] : (clips.empty() ? std::string() : clips[0]), true, 1, StateExit::Free}};
        sample.rules = {{"", "speed > 0.1", "Move", 0.2f, false, false}, {"", "", "Idle", 0.2f, false, false}};
        std::string err;
        if (!platform::WriteFileAtomic(file, animation::WriteController(sample), &err)) {
            log::Error("状態とルールのファイルを作れません ({}): {}", PathToUtf8(file.wstring()), err);
            return;
        }
        const AssetRef ref = host.host.AssetOf(file);
        if (ref.Empty()) {
            log::Warn("状態とルールのファイルを作りましたが、アセットとして読めません: {}", PathToUtf8(file.wstring()));
            return;
        }
        const reflect::TypeDesc& type = reflect::TypeOf<Animator>();
        for (size_t i = 0; i < type.fields.size(); ++i) {
            if (std::string_view(type.fields[i].name) != "rules") continue;
            host.host.History().Push(std::make_unique<reflect::SetFieldCommand>(type, reflect::FieldPath{i}, json::Value(std::string()), json::Value(ref.guid),
                                                                               host.component(t.entity, type.name), "状態とルールを作る", 0),
                                     false);
        }
        log::Info("状態とルールのファイルを作りました: {}", PathToUtf8(file.wstring()));
    }

    AssetRef ref_;
    std::string bytes_;
    std::string loadError_;
    std::shared_ptr<const AnimController> controller_;
    animation::CompiledController compiled_;
    std::map<std::string, std::string> texts_;  // 数の欄の打っている文字
    w::Popup clipMenu_;
    int clipMenuState_ = -1, clipChosen_ = -1;
    std::vector<std::string> clipChoices_;
};

}  // namespace

std::unique_ptr<game_production::SceneView> CreateAnimationTab() { return std::make_unique<AnimationTab>(); }

}  // namespace rm::game_x_animation
