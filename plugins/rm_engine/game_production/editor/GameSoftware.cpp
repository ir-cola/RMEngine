#include "rm_engine/game_production/editor/GameSoftware.h"

#include "base/Log.h"
#include "core/reflect/Data.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "rm_engine/game_production/runtime/GameSettings.h"

namespace rm::game_production {

namespace {

namespace theme = editor::theme;
using ui::Rect;

constexpr char kSceneView[] = "scene_view";
constexpr char kGameView[] = "game_view";

void Placeholder(ui::Context& c, const Rect& r, const wchar_t* title, const wchar_t* body) {
    auto& p = c.P();
    const float cy = r.y + r.h * 0.5f;
    ui::TextStyle ts;
    ts.size = 18;
    ts.bold = true;
    ts.color = theme::Text;
    ts.h = ui::HAlign::Center;
    p.Text({r.x, cy - 40, r.w, 28}, title, ts);
    ts.size = 13;
    ts.bold = false;
    ts.color = theme::TextDim;
    ts.wrap = true;
    ts.v = ui::VAlign::Top;
    p.Text({r.x + 32, cy - 4, r.w - 64, 80}, body, ts);
}

class GameSoftware final : public editor::Software {
public:
    explicit GameSoftware(editor::SoftwareHost& host) : host_(host) {}

    std::vector<editor::dock::PanelType> Panels() override {
        return {
            {kSceneView, L"シーン",
             [](ui::Context& c, const Rect& r) {
                 Placeholder(c, r, L"シーンビュー",
                             L"3D 空間 / 2D 空間のプラグインが入ると、ここにシーンが映り、エンティティを置いて動かせます。");
             }},
            {kGameView, L"ゲーム",
             [this](ui::Context& c, const Rect& r) {
                 Placeholder(c, r, L"ゲームビュー", L"Play を押すと、ゲームのカメラから見た画面がここに映ります。");
                 const Rect button{r.x + (r.w - 200) * 0.5f, r.y + r.h * 0.5f + 70, 200, 36};
                 if (editor::w::Button(c, "settings", button, L"ゲームの設定を開く")) InspectSettings();
             }},
        };
    }

    editor::dock::Layout DefaultLayout() const override {
        using editor::dock::Axis;
        namespace cp = editor::core_panel;
        editor::dock::Layout l;
        auto top = l.MakeSplit(Axis::Horizontal, 0.17f, l.MakeLeaf({cp::kHierarchy}),
                               l.MakeSplit(Axis::Horizontal, 0.78f, l.MakeLeaf({kSceneView, kGameView}),
                                           l.MakeLeaf({cp::kInspector})));
        l.main = l.MakeSplit(Axis::Vertical, 0.72f, std::move(top), l.MakeLeaf({cp::kConsole, cp::kAssets}));
        return l;
    }

    void LoadData(const json::Value& data) override {
        hadData_ = data.IsObject();
        if (hadData_) {
            reflect::Warnings warnings;
            reflect::Load(settings_, data.Get("settings"), &unknowns_, &warnings);
            for (const auto& w : warnings) log::Warn("ゲームの設定: {}", w);
        }
        InspectSettings();
    }

    bool SaveData(json::Value& data) const override {
        // Nothing is written (and the plugin is not recorded as used) until the settings differ
        // from the defaults - opening a project once must not tie it to this plugin.
        const GameSettings defaults;
        if (!hadData_ && reflect::Equals(reflect::TypeOf<GameSettings>(), &settings_, &defaults) && unknowns_.Empty()) return false;
        data.Set("settings", reflect::Save(settings_, &unknowns_));
        return true;
    }

private:
    void InspectSettings() {
        host_.Inspect({&reflect::TypeOf<GameSettings>(), [this] { return reflect::EditTarget{&settings_, &unknowns_}; },
                       L"ゲームの設定"});
    }

    editor::SoftwareHost& host_;
    GameSettings settings_;
    reflect::Unknowns unknowns_;  // keys / enum names of a newer version: written back unchanged
    bool hadData_ = false;
};

}  // namespace

void RegisterEditor(editor::PluginRegistry& registry) {
    editor::SoftwareType t;
    t.id = "game";
    t.title = L"ゲーム";
    t.create = [](editor::SoftwareHost& host) -> std::unique_ptr<editor::Software> {
        return std::make_unique<GameSoftware>(host);
    };
    registry.AddSoftware(std::move(t));
}

}  // namespace rm::game_production
