#include "rm_engine/game_production/editor/GameSoftware.h"

#include "editor/widgets/Theme.h"

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
             [](ui::Context& c, const Rect& r) {
                 Placeholder(c, r, L"ゲームビュー", L"Play を押すと、ゲームのカメラから見た画面がここに映ります。");
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

private:
    editor::SoftwareHost& host_;  // history / project, used once the game has data
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
