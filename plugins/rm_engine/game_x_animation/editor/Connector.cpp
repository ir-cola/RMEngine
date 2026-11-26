#include "rm_engine/game_x_animation/editor/Connector.h"

#include "base/Utf.h"
#include "core/asset/ImportSettings.h"
#include "editor/lang/Lang.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "rm_engine/game_production/editor/ComponentLook.h"
#include "rm_engine/game_production/editor/SceneView.h"
#include "rm_engine/game_x_animation/editor/AnimationTab.h"
#include "rm_engine/game_x_3d/editor/SceneOverlay.h"
#include "rm_engine/game_x_3d/runtime/MeshRenderer.h"
#include "rm_engine/game_x_animation/runtime/Animator.h"
#include "rm_engine/game_x_animation/runtime/AnimatorPoser.h"
#include "rm_engine/space3d/runtime/MeshFile.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rm::game_x_animation {

namespace {

namespace theme = editor::theme;
namespace w = editor::w;
using editor::Tr;
using editor::TrF;

// --- クリップの欄・骨の欄の選択肢 ------------------------------------------------------
// モデルのファイルごとに、クリップの名前と長さ・骨の名前を 1 度読んで覚える (1 秒ごとに、ファイルか .meta が変わったかを
// 見て、変わったら読み直す)。
struct ClipList {
    std::filesystem::path file;
    std::filesystem::file_time_type time{};
    std::chrono::steady_clock::time_point checked{};
    bool looked = false;  // 1 度は探した
    bool ok = false;      // ... そして読めた
    std::vector<std::pair<std::string, float>> clips;  // 名前と長さ (秒)
    std::vector<std::string> bones;                    // 骨の名前 (親が先)
};

const ClipList* ClipsOf(editor::SoftwareHost& host, const AssetRef& ref) {
    static std::unordered_map<std::string, ClipList> lists;  // アセット id ごと
    if (ref.Empty()) return nullptr;
    ClipList& l = lists[ref.guid];
    const auto now = std::chrono::steady_clock::now();
    if (l.looked && now - l.checked < std::chrono::seconds(1)) return l.ok ? &l : nullptr;
    l.checked = now;
    const std::filesystem::path file = host.AssetFile(ref);
    std::error_code ec;
    const auto time = file.empty() ? std::filesystem::file_time_type{} : asset::ChangedAt(file, ec);
    if (file.empty() || ec) {
        l.looked = true;
        l.ok = false;
        return nullptr;
    }
    if (l.looked && file == l.file && time == l.time) return l.ok ? &l : nullptr;
    l.looked = true;
    l.file = file;
    l.time = time;
    l.clips.clear();
    l.bones.clear();
    std::string err;  // (読めないことはシーンビューがコンソールに出す)
    const auto mesh = space3d::LoadMeshFile(file, &err);
    l.ok = mesh.has_value();
    if (mesh) {
        for (const space3d::Clip& c : mesh->clips) l.clips.emplace_back(c.name, c.duration);
        if (mesh->skin) {
            for (const space3d::Bone& b : mesh->skin->skeleton.bones) {
                if (b.name != space3d::kStaticBoneName) l.bones.push_back(b.name);
            }
        }
    }
    return l.ok ? &l : nullptr;
}

template <class T>
size_t FieldOf(std::string_view name) {
    const auto fields = reflect::TypeOf<T>().fields;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (std::string_view(fields[i].name) == name) return i;
    }
    return fields.size();
}

// エンティティの「メッシュ (3D モデル)」のモデル (形がモデルのとき。無ければ空)。
AssetRef ModelOn(const game_production::SceneEntity& e) {
    const game_production::Component* mc = e.FindComponent(reflect::TypeOf<game_x_3d::MeshRenderer>().name);
    if (!mc || !mc->Known()) return {};
    const auto& mr = *static_cast<const game_x_3d::MeshRenderer*>(mc->value.Get());
    return mr.shape == game_x_3d::MeshShape::Model ? mr.model : AssetRef{};
}

// アニメーターのクリップの欄: 同じエンティティの「メッシュ (3D モデル)」のモデルのクリップ。モデルが無い・
// 読めないときは、ふつうの文字の欄 (名前を打てる)。
std::optional<std::vector<editor::inspector::Choice>> ClipChoices(editor::SoftwareHost& host, const game_production::Scene& scene, int entity,
                                                                  const reflect::FieldPath& path) {
    if (path.size() != 1 || path[0] != FieldOf<Animator>("clip")) return std::nullopt;
    const game_production::SceneEntity* e = scene.Find(entity);
    const ClipList* list = e ? ClipsOf(host, ModelOn(*e)) : nullptr;
    if (!list) return std::nullopt;
    std::vector<editor::inspector::Choice> out;
    out.push_back({"", Tr(L"(最初のクリップ)")});
    for (const auto& [name, seconds] : list->clips) out.push_back({name, TrF(L"{} ({:.1f} 秒)", Utf8ToWide(name), seconds)});
    return out;
}

// 骨に付ける の骨の欄: アニメーターを持つ一番近い祖先の、メッシュのモデルの骨。見つからない・読めないときは、
// ふつうの文字の欄 (名前を打てる)。
std::optional<std::vector<editor::inspector::Choice>> BoneChoices(editor::SoftwareHost& host, const game_production::Scene& scene, int entity,
                                                                  const reflect::FieldPath& path) {
    if (path.size() != 1 || path[0] != FieldOf<BoneAttach>("bone")) return std::nullopt;
    const game_production::SceneEntity* e = scene.Find(entity);
    const std::string animator = reflect::TypeOf<Animator>().name;
    size_t steps = 0;
    for (const game_production::SceneEntity* p = e ? scene.Find(e->data.parent) : nullptr; p && steps++ <= scene.Entities().size();
         p = scene.Find(p->data.parent)) {
        if (!p->FindComponent(animator)) continue;
        const ClipList* list = ClipsOf(host, ModelOn(*p));
        if (!list || list->bones.empty()) return std::nullopt;
        std::vector<editor::inspector::Choice> out;
        out.push_back({"", Tr(L"(付けない)")});
        for (const std::string& b : list->bones) out.push_back({b, Utf8ToWide(b)});
        return out;
    }
    return std::nullopt;
}

// --- シーンビューのプレビュー --------------------------------------------------------------
constexpr float kBarH = 40.f, kBarMaxW = 480.f, kTimeW = 104.f;

const Animator* AnimatorOn(const game_production::Scene& scene, int entity) {
    const game_production::SceneEntity* e = entity != 0 ? scene.Find(entity) : nullptr;
    const game_production::Component* c = e ? e->FindComponent(reflect::TypeOf<Animator>().name) : nullptr;
    return c && c->Known() ? static_cast<const Animator*>(c->value.Get()) : nullptr;
}

// 選んだアニメーター (プレビューの時計の持ち主) があるときだけ、絵の左下に。
std::optional<ui::Rect> PreviewArea(const ui::Rect& view, const game_production::SceneViewHost& host) {
    const PreviewClock& clock = EditorPreview();
    if (clock.entity == 0 || std::find(host.selection.begin(), host.selection.end(), clock.entity) == host.selection.end()) return std::nullopt;
    if (!AnimatorOn(host.scene, clock.entity)) return std::nullopt;
    const float wdt = std::min(kBarMaxW, view.w - 24);
    if (wdt < 220 || view.h < 3 * kBarH) return std::nullopt;
    return ui::Rect{view.x + 12, view.Bottom() - 12 - kBarH, wdt, kBarH};
}

// ▶ / ‖ ・クリップの名前・時間のつまみ・今の時刻 / 長さ。
void DrawPreviewBar(ui::Context& c, const ui::Rect& at, game_production::SceneViewHost& host) {
    PreviewClock& clock = EditorPreview();
    const Animator* a = AnimatorOn(host.scene, clock.entity);
    if (!a) return;
    w::DrawSurface(c, at, w::Surface::Popup);
    const ui::Rect in = at.Inset(6);
    const ui::Rect play{in.x, in.y, in.h, in.h};
    if (w::IconButton(c, "anim_preview.play", play, clock.paused ? w::Icon::Play : w::Icon::Pause, clock.hasClip)) {
        TogglePreview(clock, a->speed, clock.duration, a->loop);
        c.RequestRedraw();
    }
    ui::TextStyle ts;
    ts.size = 12;
    ts.color = theme::TextDim;
    ts.v = ui::VAlign::Center;
    const float x = play.Right() + 8;
    if (!clock.hasClip) {
        const std::wstring why = clock.clip.empty() ? Tr(L"動かせるクリップがありません (骨とクリップのあるモデルを「メッシュ (3D モデル)」に)")
                                                    : TrF(L"モデルにクリップ「{}」がありません", Utf8ToWide(clock.clip));
        c.P().Text({x, in.y, in.Right() - x, in.h}, why, ts);
        return;
    }
    const float nameW = std::min(120.f, in.w * 0.22f);
    ui::TextStyle name = ts;
    name.color = theme::Text;
    c.P().Text({x, in.y, nameW, in.h}, Utf8ToWide(clock.clip), name);
    const ui::Rect slider{x + nameW + 8, in.y, in.Right() - kTimeW - 8 - (x + nameW + 8), in.h};
    if (slider.w > 30) {
        float t = clock.time;
        if (w::Slider(c, "anim_preview.time", slider, t, 0.f, std::max(clock.duration, 1e-3f))) {
            SeekPreview(clock, t, clock.duration);
            c.RequestRedraw();
        }
    }
    ui::TextStyle time = ts;
    time.h = ui::HAlign::Right;
    c.P().Text({in.Right() - kTimeW, in.y, kTimeW, in.h}, TrF(L"{:.2f} / {:.2f} 秒", clock.time, clock.duration), time);
}

}  // namespace

std::vector<std::string> ModelClipNames(editor::SoftwareHost& host, const game_production::Scene& scene, int entity) {
    std::vector<std::string> out;
    const game_production::SceneEntity* e = scene.Find(entity);
    const ClipList* list = e ? ClipsOf(host, ModelOn(*e)) : nullptr;
    if (list) {
        for (const auto& [name, seconds] : list->clips) out.push_back(name);
    }
    return out;
}

std::vector<std::string> ModelBoneNames(editor::SoftwareHost& host, const game_production::Scene& scene, int entity) {
    const game_production::SceneEntity* e = scene.Find(entity);
    const ClipList* list = e ? ClipsOf(host, ModelOn(*e)) : nullptr;
    return list ? list->bones : std::vector<std::string>{};
}

void RegisterEditor(editor::PluginRegistry& registry) {
    // アニメーション タブ: 選んだアニメーターの状態とルール (.rmanim) を、ルールの並びで作る。
    static const game_production::ViewPanelInfo kTab{"animation", editor::TrKey(L"アニメーション"), &CreateAnimationTab};
    registry.Contribute(game_production::kViewPanelPoint, kTab);
    // アニメーターのクリップの欄: モデルのクリップから選ぶ。
    static const game_production::ComponentLook kLook{.type = &reflect::TypeOf<Animator>, .choices = &ClipChoices};
    registry.Contribute(game_production::kComponentLookPoint, kLook);
    // 骨に付ける の骨の欄: 祖先のアニメーターのモデルの骨から選ぶ。
    static const game_production::ComponentLook kAttachLook{.type = &reflect::TypeOf<BoneAttach>, .choices = &BoneChoices};
    registry.Contribute(game_production::kComponentLookPoint, kAttachLook);
    // 止まっているシーンビューで、選んだアニメーターを流して見る。
    static const game_x_3d::SceneOverlayInfo kOverlay{&PreviewArea, &DrawPreviewBar};
    registry.Contribute(game_x_3d::kSceneOverlayPoint, kOverlay);
}

}  // namespace rm::game_x_animation
