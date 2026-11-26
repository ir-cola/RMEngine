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
#include "rm_engine/animation/runtime/Blend.h"
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
using animation::AnimLayer;
using animation::AnimParam;
using animation::AnimRule;
using animation::AnimState;
using animation::BlendMotion;
using animation::LayerMode;
using animation::ParamType;
using animation::RuleCheck;
using animation::StateExit;
using animation::StateKind;
using editor::Tr;
using editor::TrF;

constexpr float kPad = 8, kRow = 26, kNote = 16, kHead = 26, kIcon = 24, kPlotH = 120;

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

// レイヤー `layer` (0: 体 / 1..: layers[layer - 1]) の状態とルール。無いレイヤー: 体。
std::vector<AnimState>& StatesOf(AnimController& a, size_t layer) { return layer > 0 && layer - 1 < a.layers.size() ? a.layers[layer - 1].states : a.states; }
std::vector<AnimRule>& RulesOf(AnimController& a, size_t layer) { return layer > 0 && layer - 1 < a.layers.size() ? a.layers[layer - 1].rules : a.rules; }

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

// 式の中の名前 `from` を `to` に (名前の文字の途中は変えない)。
std::string RenameIn(const std::string& text, const std::string& from, const std::string& to) {
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

// カンマで区切った名前 (前後の空白は取る) <-> 一覧。
std::vector<std::string> SplitNames(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        const size_t a = cur.find_first_not_of(' '), b = cur.find_last_not_of(' ');
        if (a != std::string::npos) out.push_back(cur.substr(a, b - a + 1));
        cur.clear();
    };
    for (const char ch : text) {
        if (ch == ',') flush();
        else cur += ch;
    }
    flush();
    return out;
}
std::string JoinNames(const std::vector<std::string>& names) {
    std::string out;
    for (size_t i = 0; i < names.size(); ++i) out += (i ? ", " : "") + names[i];
    return out;
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
            layer_ = 0;
        }
        Load(host);
        if (!controller_) {
            Note(c, r.Inset(24), TrF(L"状態とルールのファイルを読めません: {}", Utf8ToWide(loadError_)));
            return;
        }
        const AnimController& ctl = *controller_;
        layer_ = std::min(layer_, ctl.layers.size());
        AnimatorRun* run = host.playing ? target->run : nullptr;
        const bool live = run && run->rules;
        entity_ = target->entity;

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
        const float off = w::BeginScroll(c, "anim_tab.scroll", view, content_);
        const float start = view.y + kPad - off;
        float y = start;
        const float x = r.x + kPad, wdt = r.w - 2 * kPad - 10;
        y = DrawParams(c, host, ctl, live ? run : nullptr, x, y, wdt);
        y = DrawLayerTabs(c, host, ctl, live ? run : nullptr, x, y + 10, wdt);
        y = DrawRules(c, host, ctl, live ? run : nullptr, x, y + 8, wdt);
        y = DrawStates(c, host, ctl, live ? run : nullptr, *target, x, y + 8, wdt);
        content_ = y - start + 2 * kPad;
        w::EndScroll(c, "anim_tab.scroll", view, content_);
        if (live) c.RequestAnimation();
        Menus(c, host);
    }

private:
    struct Target {
        int entity = 0;
        AssetRef rules;
        std::string name;
        AnimatorRun* run = nullptr;  // Play 中のもの (無い: null)
    };

    // 今のレイヤーの、Play 中の様子 (無い: null の欄)。
    struct Live {
        int state = -1, rule = -1;
        const std::vector<RuleCheck>* checks = nullptr;
        const std::vector<float>* blend = nullptr;
        float weight = 1;
    };
    Live LiveOf(AnimatorRun* run) const {
        Live l;
        if (!run || !run->rules) return l;
        if (layer_ == 0) {
            l.state = run->state, l.rule = run->rule, l.checks = &run->checks, l.blend = &run->blend;
        } else if (layer_ - 1 < run->layers.size()) {
            const AnimatorRun::Layer& lr = run->layers[layer_ - 1];
            l.state = lr.state, l.rule = lr.rule, l.checks = &lr.checks, l.blend = &lr.blend, l.weight = lr.weight;
        }
        return l;
    }
    // 今のレイヤーの解いたもの (問題の文字・混ぜる点)。
    const animation::CompiledController& Compiled() const {
        if (layer_ > 0 && layer_ - 1 < compiled_.layers.size()) return compiled_.layers[layer_ - 1].rules;
        return compiled_;
    }

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

    // --- パラメーター (全部のレイヤーで同じ) ---
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
                    // 式・混ぜる状態・レイヤーの重みの中の名前も書き換える (文字で書く弱点を埋める: docs/ANIMATION.md 4-8)。
                    if (old.empty()) return;
                    auto fixRules = [&](std::vector<AnimRule>& rules) {
                        for (AnimRule& r : rules) {
                            const animation::Condition cond = animation::Condition::Parse(r.condition);
                            if (cond.ok() && std::find(cond.names().begin(), cond.names().end(), old) != cond.names().end()) r.condition = RenameIn(r.condition, old, name);
                        }
                    };
                    auto fixStates = [&](std::vector<AnimState>& states) {
                        for (AnimState& s : states) {
                            if (s.paramX == old) s.paramX = name;
                            if (s.paramY == old) s.paramY = name;
                        }
                    };
                    fixRules(a.rules);
                    fixStates(a.states);
                    for (AnimLayer& l : a.layers) {
                        fixRules(l.rules);
                        fixStates(l.states);
                        if (l.weightParam == old) l.weightParam = name;
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
            if (run) {
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

    // --- レイヤーのタブ (B1) と、選んだレイヤーの設定 ---
    float DrawLayerTabs(ui::Context& c, game_production::SceneViewHost& host, const AnimController& ctl, AnimatorRun* run, float x, float y, float wdt) {
        std::vector<std::wstring> names{Tr(L"体")};
        for (size_t i = 0; i < ctl.layers.size(); ++i) {
            names.push_back(ctl.layers[i].name.empty() ? TrF(L"レイヤー {}", i + 1) : Utf8ToWide(ctl.layers[i].name));
        }
        const std::vector<std::wstring_view> labels(names.begin(), names.end());
        const float addW = 130;
        const float tabsW = std::min(wdt - addW - 8, 120.f * static_cast<float>(labels.size()));
        const int pick = w::Segmented(c, "anim_tab.layers", {x, y, tabsW, kRow}, labels, static_cast<int>(layer_));
        if (pick >= 0) layer_ = static_cast<size_t>(pick);
        if (w::Button(c, "anim_tab.add_layer", {x + wdt - addW, y, addW, kRow}, Tr(L"＋ レイヤー"))) {
            Edit(host, "レイヤーを足す", 0, [](AnimController& a) {
                AnimLayer l;
                l.name = std::format("Layer{}", a.layers.size() + 1);
                a.layers.push_back(l);
            });
            layer_ = ctl.layers.size() + 1;
        }
        y += kRow + 8;
        if (layer_ == 0) {
            Note(c, {x, y, wdt, kNote + 2}, Tr(L"体: 一番下の姿勢。レイヤーはこの上に、タブの順に重なります"), theme::TextFaint, 12);
            return y + kNote + 4;
        }
        const size_t li = layer_ - 1;
        const AnimLayer& l = ctl.layers[li];
        c.PushId(static_cast<uint64_t>(9000 + li));
        // 1 行目: 名前・重み (数かパラメーター)・重ね方・消す。
        float cx = x;
        std::string name = l.name;
        Label(c, {cx, y, 40, kRow}, Tr(L"名前"), theme::TextDim);
        if (w::TextField(c, "name", {cx + 40, y, 120, kRow}, name, Tr(L"名前"))) {
            Edit(host, "レイヤーの名前", MergeOf(std::format("l{}/name", li)), [li, name](AnimController& a) {
                if (li < a.layers.size()) a.layers[li].name = name;
            });
        }
        cx += 170;
        Label(c, {cx, y, 34, kRow}, Tr(L"重み"), theme::TextDim);
        float weight = l.weight;
        if (w::NumberField(c, "weight", {cx + 34, y, 52, kRow}, weight, 0.f, 1.f, Text(std::format("lw{}", li)))) {
            Edit(host, "レイヤーの重み", MergeOf(std::format("l{}/weight", li)), [li, weight](AnimController& a) {
                if (li < a.layers.size()) a.layers[li].weight = weight;
            });
        }
        cx += 92;
        std::string wp = l.weightParam;
        if (w::TextField(c, "wparam", {cx, y, 120, kRow}, wp, Tr(L"重みのパラメーター"))) {
            Edit(host, "レイヤーの重みのパラメーター", MergeOf(std::format("l{}/wparam", li)), [li, wp](AnimController& a) {
                if (li < a.layers.size()) a.layers[li].weightParam = wp;
            });
        }
        cx += 126;
        const std::wstring modes[] = {Tr(L"上書き"), Tr(L"足す")};
        const std::wstring_view modeLabels[] = {modes[0], modes[1]};
        const std::wstring modeTips[] = {Tr(L"上書き: このレイヤーの姿勢にする"), Tr(L"足す: 休みの姿勢からの差を足す (呼吸・揺れ)")};
        const std::wstring_view modeTipViews[] = {modeTips[0], modeTips[1]};
        const int mode = w::Segmented(c, "mode", {cx, y, 130, kRow}, modeLabels, static_cast<int>(l.mode), modeTipViews);
        if (mode >= 0 && mode != static_cast<int>(l.mode)) {
            Edit(host, "レイヤーの重ね方", 0, [li, mode](AnimController& a) {
                if (li < a.layers.size()) a.layers[li].mode = static_cast<LayerMode>(mode);
            });
        }
        if (w::Button(c, "remove", {x + wdt - 130, y, 130, kRow}, Tr(L"レイヤーを消す"))) {
            Edit(host, "レイヤーを消す", 0, [li](AnimController& a) {
                if (li < a.layers.size()) a.layers.erase(a.layers.begin() + static_cast<std::ptrdiff_t>(li));
            });
            layer_ = li;
        }
        y += kRow + 6;
        // 2 行目: マスク (骨の名前をカンマで。▾ で骨を足す) と、Play 中の重み。
        Label(c, {x, y, 50, kRow}, Tr(L"マスク"), theme::TextDim);
        std::string mask = JoinNames(l.mask);
        const float maskW = wdt - 50 - 30 - (run ? 120 : 0);
        if (w::TextField(c, "mask", {x + 50, y, maskW, kRow}, mask, Tr(L"全部の骨 (この骨とその下だけに重ねるなら骨の名前)"))) {
            const std::vector<std::string> names2 = SplitNames(mask);
            Edit(host, "レイヤーのマスク", MergeOf(std::format("l{}/mask", li)), [li, names2](AnimController& a) {
                if (li < a.layers.size()) a.layers[li].mask = names2;
            });
        }
        if (w::IconButton(c, "bones", {x + 50 + maskW + 2, y + 1, 24, kIcon}, w::Icon::ChevronDown)) {
            menu_ = {MenuKind::Bone, li, 0, 0};
            choices_ = ModelBoneNames(host.host, host.scene, entity_);
            popup_.OpenAt(x + 50, y + kRow);
        }
        if (run) Label(c, {x + wdt - 116, y, 116, kRow}, TrF(L"今の重み {:.2f}", LiveOf(run).weight), theme::Accent);
        c.PopId();
        return y + kRow + 4;
    }

    // --- ルール (今のレイヤーの) ---
    float DrawRules(ui::Context& c, game_production::SceneViewHost& host, const AnimController& ctl, AnimatorRun* run, float x, float y, float wdt) {
        const size_t layer = layer_;
        const std::vector<AnimRule>& rules = layer == 0 ? ctl.rules : ctl.layers[layer - 1].rules;
        const Live live = LiveOf(run);
        if (Heading(c, "anim_tab.add_rule", x, y, wdt, Tr(L"ルール (上から調べて、最初に当たった行)"), Tr(L"＋ ルール"))) {
            Edit(host, "ルールを足す", 0, [layer](AnimController& a) {
                AnimRule r;
                const auto& states = StatesOf(a, layer);
                r.to = states.empty() ? std::string() : states.front().name;
                RulesOf(a, layer).push_back(r);
            });
        }
        y += kHead + 8;
        const animation::CompiledController& compiled = Compiled();
        for (size_t i = 0; i < rules.size(); ++i) {
            const AnimRule& rule = rules[i];
            c.PushId(static_cast<uint64_t>(1000 + i));
            const RuleCheck check = live.checks && i < live.checks->size() ? (*live.checks)[i] : RuleCheck::Below;
            const bool chosen = live.checks && live.rule == static_cast<int>(i) && check == RuleCheck::Chosen;
            if (chosen) w::DrawRow(c, {x - 4, y - 2, wdt + 8, kRow + kNote + 4}, {true, false, true});
            Label(c, {x, y, 22, kRow}, chosen ? L"▶" : std::to_wstring(i + 1), chosen ? theme::Accent : theme::TextDim);
            float cx = x + 22;
            if (w::IconToggle(c, "interrupt", {cx, y + 1, kIcon, kIcon}, w::Icon::Step, rule.interrupt, true, Tr(L"割り込む (⚡): 今の状態の出口が「自分のルールだけ」でも調べる"))) {
                Edit(host, "ルールの割り込む", 0, [layer, i](AnimController& a) {
                    auto& list = RulesOf(a, layer);
                    if (i < list.size()) list[i].interrupt = !list[i].interrupt;
                });
            }
            cx += kIcon + 4;
            const float tail = 4 * (kIcon + 2) + 60;  // (フェード・やり直す・↑ ↓ ×)
            const float rest = wdt - (cx - x) - tail;
            const float fromW = std::floor(rest * 0.2f), toW = std::floor(rest * 0.2f), condW = rest - fromW - toW - 24;
            std::string from = rule.from;
            if (w::TextField(c, "from", {cx, y, fromW, kRow}, from, Tr(L"どこでも"))) {
                Edit(host, "ルールの元", MergeOf(std::format("{}r{}/from", layer, i)), [layer, i, from](AnimController& a) {
                    auto& list = RulesOf(a, layer);
                    if (i < list.size()) list[i].from = from;
                });
            }
            cx += fromW + 4;
            std::string cond = rule.condition;
            if (w::TextField(c, "cond", {cx, y, condW, kRow}, cond, Tr(L"(いつも)"))) {
                Edit(host, "ルールの条件", MergeOf(std::format("{}r{}/cond", layer, i)), [layer, i, cond](AnimController& a) {
                    auto& list = RulesOf(a, layer);
                    if (i < list.size()) list[i].condition = cond;
                });
            }
            cx += condW + 2;
            Label(c, {cx, y, 18, kRow}, L"→", theme::TextDim);
            cx += 20;
            std::string to = rule.to;
            if (w::TextField(c, "to", {cx, y, toW, kRow}, to, Tr(L"先の状態"))) {
                Edit(host, "ルールの先", MergeOf(std::format("{}r{}/to", layer, i)), [layer, i, to](AnimController& a) {
                    auto& list = RulesOf(a, layer);
                    if (i < list.size()) list[i].to = to;
                });
            }
            cx += toW + 4;
            float fade = rule.fade;
            if (w::NumberField(c, "fade", {cx, y, 54, kRow}, fade, 0.f, 10.f, Text(std::format("{}rf{}", layer, i)))) {
                Edit(host, "ルールのフェード", MergeOf(std::format("{}r{}/fade", layer, i)), [layer, i, fade](AnimController& a) {
                    auto& list = RulesOf(a, layer);
                    if (i < list.size()) list[i].fade = fade;
                });
            }
            cx += 58;
            if (w::IconToggle(c, "restart", {cx, y + 1, kIcon, kIcon}, w::Icon::Play, rule.restart, true, Tr(L"やり直す: 先が今の状態と同じでも、始めから流し直す"))) {
                Edit(host, "ルールのやり直す", 0, [layer, i](AnimController& a) {
                    auto& list = RulesOf(a, layer);
                    if (i < list.size()) list[i].restart = !list[i].restart;
                });
            }
            cx += kIcon + 2;
            MoveButtons(c, host, cx, y, i, rules.size(), [layer](AnimController& a) -> auto& { return RulesOf(a, layer); }, "ルール");
            // 行の下: 問題 (式の間違い・無い状態) と、Play 中の結果。
            std::wstring note;
            ui::Color color = theme::TextFaint;
            const std::string& problem = i < compiled.rules.size() ? compiled.rules[i].problem : std::string();
            if (!problem.empty()) {
                note = Utf8ToWide(problem);
                color = compiled.rules[i].to < 0 ? theme::Danger : theme::Warn;
            }
            if (live.checks) {
                const std::wstring result = CheckText(check);
                note = note.empty() ? result : result + L" ・ " + note;
                if (problem.empty()) color = chosen ? theme::Accent : theme::TextDim;
            }
            if (!note.empty()) Label(c, {x + 50, y + kRow, wdt - 50, kNote}, note, color, false, 11);
            c.PopId();
            y += kRow + kNote + 4;
        }
        if (rules.empty()) {
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

    // --- 状態 (今のレイヤーの) ---
    float DrawStates(ui::Context& c, game_production::SceneViewHost& host, const AnimController& ctl, AnimatorRun* run, const Target& target, float x, float y,
                     float wdt) {
        const size_t layer = layer_;
        const std::vector<AnimState>& states = layer == 0 ? ctl.states : ctl.layers[layer - 1].states;
        const Live live = LiveOf(run);
        if (Heading(c, "anim_tab.add_state", x, y, wdt, Tr(L"状態 (1 つ目から始まる)"), Tr(L"＋ 状態"))) {
            const std::vector<std::string> clips = ModelClipNames(host.host, host.scene, target.entity);
            Edit(host, "状態を足す", 0, [layer, clips](AnimController& a) {
                auto& list = StatesOf(a, layer);
                std::string name = "State";
                for (int n = 1; std::any_of(list.begin(), list.end(), [&](const AnimState& s) { return s.name == name; }); ++n) name = std::format("State{}", n);
                AnimState s;
                s.name = name;
                if (!clips.empty()) s.clip = clips.front();
                list.push_back(s);
            });
        }
        y += kHead + 8;
        const std::wstring exits[] = {Tr(L"自由"), Tr(L"自分だけ"), Tr(L"終わるまで")};
        const std::wstring exitTips[] = {Tr(L"自由: 全部のルールを調べる"), Tr(L"自分のルールだけ: 元がこの状態のルールと ⚡ のルールだけ (ジャンプの途中で歩きに戻らない)"),
                                         Tr(L"終わるまで自分のルールだけ: クリップが終わるまでは自分のルールだけ、終わったら全部 (攻撃・着地)")};
        const std::wstring_view exitLabels[] = {exits[0], exits[1], exits[2]};
        const std::wstring_view exitTipViews[] = {exitTips[0], exitTips[1], exitTips[2]};
        const std::wstring kinds[] = {Tr(L"クリップ"), Tr(L"1D"), Tr(L"2D")};
        const std::wstring_view kindLabels[] = {kinds[0], kinds[1], kinds[2]};
        const std::wstring kindTips[] = {Tr(L"クリップ: 1 つのクリップを流す"), Tr(L"1D で混ぜる: パラメーター 1 つの値で、数直線に置いたクリップを混ぜる (歩き ↔ 走り)"),
                                         Tr(L"2D で混ぜる: パラメーター 2 つの値で、平面に置いたクリップを混ぜる (8 方向の移動)")};
        const std::wstring_view kindTipViews[] = {kindTips[0], kindTips[1], kindTips[2]};
        for (size_t i = 0; i < states.size(); ++i) {
            const AnimState& s = states[i];
            c.PushId(static_cast<uint64_t>(5000 + i));
            const bool current = live.checks && live.state == static_cast<int>(i);
            const bool blend = s.kind != StateKind::Clip;
            if (current) w::DrawRow(c, {x - 4, y - 2, wdt + 8, kRow + 4}, {true, false, true});
            const float tail = 3 * (kIcon + 2);
            const float exitW = std::floor(wdt * 0.22f), nameW = std::floor(wdt * 0.14f), kindW = 120;
            const float midW = wdt - nameW - kindW - exitW - tail - 80 - 60 - 20;
            float cx = x;
            std::string name = s.name;
            if (w::TextField(c, "name", {cx, y, nameW, kRow}, name, Tr(L"名前"))) {
                const std::string old = s.name;
                Edit(host, "状態の名前", MergeOf(std::format("{}s{}/name", layer, i)), [layer, i, name, old](AnimController& a) {
                    auto& list = StatesOf(a, layer);
                    if (i >= list.size()) return;
                    list[i].name = name;
                    // ルールの元と先も書き換える。
                    if (old.empty()) return;
                    for (AnimRule& r : RulesOf(a, layer)) {
                        if (r.from == old) r.from = name;
                        if (r.to == old) r.to = name;
                    }
                });
            }
            cx += nameW + 4;
            const int kind = w::Segmented(c, "kind", {cx, y, kindW, kRow}, kindLabels, static_cast<int>(s.kind), kindTipViews);
            if (kind >= 0 && kind != static_cast<int>(s.kind)) {
                const std::vector<std::string> clips = ModelClipNames(host.host, host.scene, target.entity);
                Edit(host, "状態の種類", 0, [layer, i, kind, clips](AnimController& a) {
                    auto& list = StatesOf(a, layer);
                    if (i >= list.size()) return;
                    AnimState& st = list[i];
                    st.kind = static_cast<StateKind>(kind);
                    // 初めて混ぜる状態にしたら: 今のクリップと、もう 1 つを置く。
                    if (st.kind != StateKind::Clip && st.motions.empty()) {
                        st.motions.push_back({st.clip.empty() && !clips.empty() ? clips.front() : st.clip, 0, 0});
                        st.motions.push_back({clips.size() > 1 ? clips[1] : st.motions.front().clip, 1, 0});
                    }
                });
            }
            cx += kindW + 4;
            if (!blend) {
                std::string clip = s.clip;
                if (w::TextField(c, "clip", {cx, y, midW - 26, kRow}, clip, Tr(L"クリップ"))) {
                    Edit(host, "状態のクリップ", MergeOf(std::format("{}s{}/clip", layer, i)), [layer, i, clip](AnimController& a) {
                        auto& list = StatesOf(a, layer);
                        if (i < list.size()) list[i].clip = clip;
                    });
                }
                if (w::IconButton(c, "pick", {cx + midW - 24, y + 1, 22, kIcon}, w::Icon::ChevronDown)) OpenClipMenu(host, layer, i, -1, cx, y + kRow);
            } else {
                // 混ぜる位置のパラメーター (x と、2D なら y)。
                const bool two = s.kind == StateKind::Blend2D;
                const float pw = two ? std::floor((midW - 4) * 0.5f) : midW;
                std::string px = s.paramX;
                if (w::TextField(c, "px", {cx, y, pw, kRow}, px, two ? Tr(L"x のパラメーター") : Tr(L"パラメーター"))) {
                    Edit(host, "混ぜるパラメーター", MergeOf(std::format("{}s{}/px", layer, i)), [layer, i, px](AnimController& a) {
                        auto& list = StatesOf(a, layer);
                        if (i < list.size()) list[i].paramX = px;
                    });
                }
                if (two) {
                    std::string py = s.paramY;
                    if (w::TextField(c, "py", {cx + pw + 4, y, pw, kRow}, py, Tr(L"y のパラメーター"))) {
                        Edit(host, "混ぜるパラメーター (y)", MergeOf(std::format("{}s{}/py", layer, i)), [layer, i, py](AnimController& a) {
                            auto& list = StatesOf(a, layer);
                            if (i < list.size()) list[i].paramY = py;
                        });
                    }
                }
            }
            cx += midW + 4;
            bool loop = s.loop;
            if (w::Checkbox(c, "loop", {cx, y, 76, kRow}, loop, Tr(L"ループ"))) {
                Edit(host, "状態のループ", 0, [layer, i, loop](AnimController& a) {
                    auto& list = StatesOf(a, layer);
                    if (i < list.size()) list[i].loop = loop;
                });
            }
            cx += 80;
            float speed = s.speed;
            if (w::NumberField(c, "speed", {cx, y, 52, kRow}, speed, -10.f, 10.f, Text(std::format("{}ss{}", layer, i)))) {
                Edit(host, "状態の速さ", MergeOf(std::format("{}s{}/speed", layer, i)), [layer, i, speed](AnimController& a) {
                    auto& list = StatesOf(a, layer);
                    if (i < list.size()) list[i].speed = speed;
                });
            }
            cx += 60;
            const int pick = w::Segmented(c, "exit", {cx, y, exitW, kRow}, exitLabels, static_cast<int>(s.exit), exitTipViews);
            if (pick >= 0 && pick != static_cast<int>(s.exit)) {
                Edit(host, "状態の出口", 0, [layer, i, pick](AnimController& a) {
                    auto& list = StatesOf(a, layer);
                    if (i < list.size()) list[i].exit = static_cast<StateExit>(pick);
                });
            }
            cx += exitW + 6;
            MoveButtons(c, host, cx, y, i, states.size(), [layer](AnimController& a) -> auto& { return StatesOf(a, layer); }, "状態");
            y += kRow + 4;
            if (blend) y = DrawBlend(c, host, s, i, current ? live.blend : nullptr, run, x + 24, y + 2, wdt - 24);
            c.PopId();
        }
        if (states.empty()) {
            Note(c, {x, y, wdt, kRow}, Tr(L"まだありません。待機・歩く・跳ぶ … を足し、それぞれに流すクリップを選びます"));
            y += kRow;
        }
        return y;
    }

    // 混ぜる状態の図と表 (A1): 左に図 (点と試す印をドラッグ)、右に表 (クリップ・x・y・今の重み・×)。
    float DrawBlend(ui::Context& c, game_production::SceneViewHost& host, const AnimState& s, size_t i, const std::vector<float>* liveWeights, AnimatorRun* run,
                    float x, float y, float wdt) {
        const size_t layer = layer_;
        const bool two = s.kind == StateKind::Blend2D;
        // 試す印: Play 中は今のパラメーターの値、止まっているときは自分で動かす所。
        const std::string key = std::format("{}/{}", layer, i);
        Vec2& marker = markers_[key];
        if (run) marker = {RuleParam(*run, s.paramX), two ? RuleParam(*run, s.paramY) : 0.f};
        // 重み: Play 中で今の状態ならその値、ほかは印の場所で計算する。
        std::vector<float> weights;
        if (liveWeights && liveWeights->size() == s.motions.size()) {
            weights = *liveWeights;
        } else {
            std::vector<float> xs;
            std::vector<Vec2> pts;
            for (const BlendMotion& m : s.motions) xs.push_back(m.x), pts.push_back({m.x, m.y});
            if (two) {
                animation::BlendWeights2D(pts, animation::Triangulate(pts), marker, weights);
            } else {
                animation::BlendWeights1D(xs, marker.x, weights);
            }
        }
        std::vector<std::wstring> names;
        for (const BlendMotion& m : s.motions) names.push_back(Utf8ToWide(m.clip));
        std::vector<w::PlotPoint> points;
        for (size_t k = 0; k < s.motions.size(); ++k) points.push_back({{s.motions[k].x, s.motions[k].y}, names[k], k < weights.size() ? weights[k] : 0.f});
        const float plotW = std::floor(wdt * 0.5f);
        const float tableRows = static_cast<float>(s.motions.size()) + 1;
        const float h = std::max(kPlotH, tableRows * (kRow + 2) + 4);
        const w::PlotResult pr = w::BlendPlot(c, "plot", {x, y, plotW, two ? h : std::min(h, 90.f)}, points, two, marker, run == nullptr);
        if (pr.dragged >= 0) {
            const size_t k = static_cast<size_t>(pr.dragged);
            const Vec2 to = pr.to;
            Edit(host, "混ぜるクリップを動かす", MergeOf(std::format("{}s{}/m{}/drag", layer, i, k)), [layer, i, k, to](AnimController& a) {
                auto& list = StatesOf(a, layer);
                if (i >= list.size() || k >= list[i].motions.size()) return;
                list[i].motions[k].x = std::round(to.x * 100.f) / 100.f;
                list[i].motions[k].y = std::round(to.y * 100.f) / 100.f;
            });
        }
        // 表。
        const float tx = x + plotW + 10, tw = wdt - plotW - 10;
        float ty = y;
        const float valW = 56, weightW = 48;
        const float clipW = tw - valW * (two ? 2 : 1) - weightW - kIcon - 30 - 12;
        for (size_t k = 0; k < s.motions.size(); ++k) {
            const BlendMotion& m = s.motions[k];
            c.PushId(static_cast<uint64_t>(7000 + k));
            float cx = tx;
            std::string clip = m.clip;
            if (w::TextField(c, "clip", {cx, ty, clipW, kRow}, clip, Tr(L"クリップ"))) {
                Edit(host, "混ぜるクリップ", MergeOf(std::format("{}s{}/m{}/clip", layer, i, k)), [layer, i, k, clip](AnimController& a) {
                    auto& list = StatesOf(a, layer);
                    if (i < list.size() && k < list[i].motions.size()) list[i].motions[k].clip = clip;
                });
            }
            cx += clipW;
            if (w::IconButton(c, "pick", {cx + 2, ty + 1, 22, kIcon}, w::Icon::ChevronDown)) OpenClipMenu(host, layer, i, static_cast<int>(k), tx, ty + kRow);
            cx += 28;
            float vx = m.x;
            if (w::NumberField(c, "x", {cx, ty, valW - 4, kRow}, vx, -1e6f, 1e6f, Text(std::format("{}s{}m{}x", layer, i, k)))) {
                Edit(host, "混ぜるクリップの x", MergeOf(std::format("{}s{}/m{}/x", layer, i, k)), [layer, i, k, vx](AnimController& a) {
                    auto& list = StatesOf(a, layer);
                    if (i < list.size() && k < list[i].motions.size()) list[i].motions[k].x = vx;
                });
            }
            cx += valW;
            if (two) {
                float vy = m.y;
                if (w::NumberField(c, "y", {cx, ty, valW - 4, kRow}, vy, -1e6f, 1e6f, Text(std::format("{}s{}m{}y", layer, i, k)))) {
                    Edit(host, "混ぜるクリップの y", MergeOf(std::format("{}s{}/m{}/y", layer, i, k)), [layer, i, k, vy](AnimController& a) {
                        auto& list = StatesOf(a, layer);
                        if (i < list.size() && k < list[i].motions.size()) list[i].motions[k].y = vy;
                    });
                }
                cx += valW;
            }
            const float wk = k < weights.size() ? weights[k] : 0.f;
            Label(c, {cx, ty, weightW, kRow}, std::format(L"{:.0f} %", wk * 100.f), wk > 0.001f ? theme::Accent : theme::TextFaint, false, 12);
            cx += weightW;
            if (w::IconButton(c, "remove", {cx, ty + 1, kIcon, kIcon}, w::Icon::Close)) {
                Edit(host, "混ぜるクリップを消す", 0, [layer, i, k](AnimController& a) {
                    auto& list = StatesOf(a, layer);
                    if (i < list.size() && k < list[i].motions.size()) list[i].motions.erase(list[i].motions.begin() + static_cast<std::ptrdiff_t>(k));
                });
            }
            c.PopId();
            ty += kRow + 2;
        }
        if (w::Button(c, "add_motion", {tx, ty, 140, kRow}, Tr(L"＋ クリップ"))) {
            const std::vector<std::string> clips = ModelClipNames(host.host, host.scene, entity_);
            Edit(host, "混ぜるクリップを足す", 0, [layer, i, clips](AnimController& a) {
                auto& list = StatesOf(a, layer);
                if (i >= list.size()) return;
                auto& motions = list[i].motions;
                float most = 0;
                for (const BlendMotion& m : motions) most = std::max(most, m.x);
                motions.push_back({clips.empty() ? std::string() : clips.front(), motions.empty() ? 0.f : most + 1, 0});
            });
        }
        Label(c, {tx + 150, ty, tw - 150, kRow}, run ? Tr(L"印は今のパラメーターの値") : Tr(L"印をドラッグして重みを試せます"), theme::TextFaint, false, 11);
        return y + h + 8;
    }

    // クリップ (状態か混ぜるクリップ) と骨 (マスク) を選ぶメニュー (すべての上に。選んだものは次のフレームで入れる)。
    enum class MenuKind { None, Clip, Bone };
    struct MenuTarget {
        MenuKind kind = MenuKind::None;
        size_t layer = 0, state = 0;
        int motion = -1;  // (-1: 状態のクリップ)
    };
    void OpenClipMenu(game_production::SceneViewHost& host, size_t layer, size_t state, int motion, float x, float y) {
        menu_ = {MenuKind::Clip, layer, state, motion};
        choices_ = ModelClipNames(host.host, host.scene, entity_);
        popup_.OpenAt(x, y);
    }
    void Menus(ui::Context& c, game_production::SceneViewHost& host) {
        if (const int chosen = std::exchange(chosen_, -1); chosen >= 0 && static_cast<size_t>(chosen) < choices_.size()) {
            const std::string name = choices_[static_cast<size_t>(chosen)];
            const MenuTarget t = menu_;
            if (t.kind == MenuKind::Clip) {
                Edit(host, "クリップを選ぶ", 0, [t, name](AnimController& a) {
                    auto& list = StatesOf(a, t.layer);
                    if (t.state >= list.size()) return;
                    if (t.motion < 0) {
                        list[t.state].clip = name;
                    } else if (static_cast<size_t>(t.motion) < list[t.state].motions.size()) {
                        list[t.state].motions[static_cast<size_t>(t.motion)].clip = name;
                    }
                });
            } else if (t.kind == MenuKind::Bone) {
                Edit(host, "マスクに骨を足す", 0, [t, name](AnimController& a) {
                    if (t.layer >= a.layers.size()) return;
                    auto& mask = a.layers[t.layer].mask;
                    if (std::find(mask.begin(), mask.end(), name) == mask.end()) mask.push_back(name);
                });
            }
        }
        if (!popup_.open) return;
        c.AddOverlay([this, &c] {
            const bool was = c.InputEnabled();
            c.SetInputEnabled(true);
            std::vector<w::MenuItem> items;
            for (const std::string& name : choices_) items.push_back({Utf8ToWide(name)});
            if (items.empty()) items.push_back({menu_.kind == MenuKind::Bone ? Tr(L"(モデルに骨がありません)") : Tr(L"(モデルにクリップがありません)"), {}, false});
            if (const int chosen = w::MenuPopup(c, popup_, items, 240); chosen >= 0 && !choices_.empty()) chosen_ = chosen;
            c.SetInputEnabled(was);
            if (chosen_ >= 0) c.RequestRedraw();
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
    std::map<std::string, Vec2> markers_;       // 混ぜる図の試す印 (レイヤー/状態ごと)
    size_t layer_ = 0;                          // 今のタブ (0: 体)
    int entity_ = 0;
    float content_ = 400;  // 前のフレームの中身の高さ (スクロール)
    w::Popup popup_;
    MenuTarget menu_;
    int chosen_ = -1;
    std::vector<std::string> choices_;
};

}  // namespace

std::unique_ptr<game_production::SceneView> CreateAnimationTab() { return std::make_unique<AnimationTab>(); }

}  // namespace rm::game_x_animation
