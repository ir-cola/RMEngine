#include "rm_engine/game_x_animation/editor/TabPreview.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "core/asset/ImportSettings.h"
#include "editor/lang/Lang.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "rm_engine/animation3d/runtime/RootMotion.h"
#include "rm_engine/game_x_3d/runtime/MeshRenderer.h"
#include "rm_engine/space3d/runtime/Texture.h"

#include <algorithm>
#include <cmath>

namespace rm::game_x_animation {

namespace {

namespace w = editor::w;
namespace theme = editor::theme;
using editor::Tr;
using editor::TrF;

constexpr float kBar = 30.f;

AssetRef ModelOf(const game_production::Scene& scene, int entity) {
    const game_production::SceneEntity* e = scene.Find(entity);
    const game_production::Component* c = e ? e->FindComponent(reflect::TypeOf<game_x_3d::MeshRenderer>().name) : nullptr;
    if (!c || !c->Known()) return {};
    const auto& mr = *static_cast<const game_x_3d::MeshRenderer*>(c->value.Get());
    return mr.shape == game_x_3d::MeshShape::Model ? mr.model : AssetRef{};
}

void Say(ui::Context& c, const ui::Rect& r, const std::wstring& text, ui::Color color, ui::HAlign h = ui::HAlign::Left) {
    ui::TextStyle ts;
    ts.size = 12;
    ts.color = color;
    ts.h = h;
    ts.v = ui::VAlign::Center;
    c.P().Text(r, text, ts);
}

}  // namespace

void TabPreview::Load(game_production::SceneViewHost& host, const AssetRef& model) {
    const auto now = std::chrono::steady_clock::now();
    if (model.guid != model_.guid) {
        // 別のモデル: 前のものは捨てる。
        if (ticket_ != 0) loader_.Cancel(ticket_);
        ticket_ = 0;
        model_ = model;
        file_ = model.Empty() ? std::filesystem::path() : host.host.AssetFile(model);
        loaded_ = false;
        gpu_.reset();
        skinned_ = {};
        BindModel(run_, nullptr, nullptr);
        error_.clear();
        time_ = {};
        checked_ = {};
    }
    if (file_.empty()) return;
    if (ticket_ != 0) {
        if (auto done = loader_.Take(ticket_)) {
            ticket_ = 0;
            time_ = reading_;
            loaded_ = true;
            gpu_.reset();
            skinned_ = {};
            error_.clear();
            if (!done->mesh) {
                error_ = done->error;
            } else if (space3d::Renderer* gpu = view_.Gpu(host.host.Graphics())) {
                std::string err;
                gpu_ = gpu->Upload(*done->mesh, &err);
                generation_ = view_.Generation();
                if (!gpu_) error_ = err;
                materials_.clear();
                for (const space3d::MeshMaterial& m : done->mesh->materials) materials_.push_back(space3d::MaterialNumbers(m));
                skinned_ = game_x_3d::TakeSkinned(*done->mesh);
                if (gpu_) {
                    // カメラをモデルの大きさに合わせる (初めて読んだとき)。
                    const Vec3 lo = gpu_->min, hi = gpu_->max;
                    const float size = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 0.1f});
                    view_.Camera().pivot = {0, (lo.y + hi.y) * 0.5f, 0};
                    view_.Camera().distance = size * 2.4f;
                }
            }
            BindModel(run_, skinned_.skin, skinned_.clips);
            if (rules_) BindRules(run_, anim_, rules_);
            Restart();
        }
    }
    // ファイルか .meta が変わった (ルートモーションの設定を変えた): 読み直す。1 秒に 1 回だけ見る。
    if (ticket_ == 0 && now - checked_ >= std::chrono::seconds(1)) {
        checked_ = now;
        std::error_code ec;
        const auto t = asset::ChangedAt(file_, ec);
        if (ec) {
            loaded_ = true;
            error_ = editor::TrU8("モデルのファイルが見つかりません");
        } else if (!loaded_ || t != time_ || (gpu_ && generation_ != view_.Generation())) {
            reading_ = t;
            ticket_ = loader_.Request(file_);
        }
    }
}

void TabPreview::Rebind(const Animator& animator, const std::shared_ptr<const animation::CompiledController>& rules) {
    rules_ = rules;
    // 部品の写し (ルールがクリップ・ループ・速さを書き換える。ゲームの部品は変えない)。
    anim_ = Animator{};
    anim_.rules = animator.rules;
    anim_.clip = animator.clip;
    anim_.loop = animator.loop;
    anim_.speed = animator.speed;
    anim_.rootMotion = animator.rootMotion;
    anim_.playOnStart = true;
    BindModel(run_, skinned_.skin, skinned_.clips);
    BindRules(run_, anim_, rules_);
    Restart();
}

void TabPreview::Restart() {
    position_ = {0, 0, 0};
    yaw_ = 0;
    speed_ = turnRate_ = 0;
    last_ = {};
}

void TabPreview::Draw(ui::Context& c, const ui::Rect& r, game_production::SceneViewHost& host, int entity, const Animator& animator,
                      const std::shared_ptr<const animation::CompiledController>& rules, AnimatorRun* live) {
    if (r.w < 80 || r.h < 60) return;
    if (entity != entity_ || rules != rules_) {
        entity_ = entity;
        Rebind(animator, rules);
    }
    Load(host, ModelOf(host.scene, entity));
    if (ticket_ != 0) c.RequestAnimation();  // (読み終えたら出す)

    // 進める: 止まっているときは自分の再生、Play 中はゲームの再生を見るだけ。
    const auto now = std::chrono::steady_clock::now();
    float dt = last_ == std::chrono::steady_clock::time_point{} ? 0.f : std::chrono::duration<float>(now - last_).count();
    dt = std::clamp(dt, 0.f, 0.1f);
    last_ = now;
    const AnimatorRun* shown = nullptr;
    if (live && live->skin) {
        shown = live;
        dt = live->rootDt;
        c.RequestAnimation();
    } else if (!live && run_.skin) {
        if (!paused_) {
            StepAnimatorRules(run_, anim_, dt);
            fired_.clear();
            StepAnimator(run_, anim_, dt, fired_, &space3d::NextPoseVersion);
            c.RequestAnimation();
        } else {
            dt = 0;
            run_.root = {};
        }
        shown = &run_;
    }
    // ルートモーション: 「動かす」なら回して床を流す。速さはならして出す。
    if (shown && dt > 0) {
        const animation3d::RootDelta& d = shown->root;
        if (moving_) {
            const Vec3 m = animation3d::RotateY(d.move, yaw_);
            position_ = {position_.x + m.x, position_.y, position_.z + m.z};
            yaw_ = std::remainder(yaw_ + d.turn, 6.2831853f);
        }
        const float v = std::hypot(d.move.x, d.move.z) / dt, t = d.turn / dt * 57.29578f;
        speed_ += (v - speed_) * 0.2f;
        turnRate_ += (t - turnRate_) * 0.2f;
    } else if (!shown || paused_) {
        speed_ = turnRate_ = 0;
    }

    // 絵。
    std::vector<space3d::MeshDraw> draws;
    if (gpu_ && generation_ == view_.Generation()) {
        space3d::MeshDraw d;
        d.mesh = gpu_.get();
        d.materials = materials_;
        d.moving = true;
        if (moving_) d.world = space3d::QuatMatrix(animation3d::YawQuat(yaw_));
        if (shown && shown->skin && gpu_->skinned && shown->bones.size() == gpu_->boneCount) d.bones = shown->bones;
        draws.push_back(d);
    }
    // 上の操作 (絵の上に重ねない: 押しても絵のカメラが回らないように): [その場で | 動かす] ▶ / ‖ ■
    const std::wstring modes[] = {Tr(L"その場で"), Tr(L"動かす")};
    const std::wstring_view labels[] = {modes[0], modes[1]};
    const float segW = w::SegmentedWidth(c, labels);
    const int pick = w::Segmented(c, "anim_tab.preview.mode", {r.x, r.y, segW, kBar}, labels, moving_ ? 1 : 0);
    if (pick >= 0 && (pick == 1) != moving_) {
        moving_ = pick == 1;
        position_ = {0, 0, 0};
        yaw_ = 0;
    }
    const ui::Rect play{r.x + segW + 8, r.y, kBar, kBar};
    if (w::IconButton(c, "anim_tab.preview.play", play, paused_ ? w::Icon::Play : w::Icon::Pause, !live)) paused_ = !paused_;
    if (c.IsHovered(play)) w::Tooltip(c, live ? Tr(L"Play 中はゲームの姿勢を見せています") : paused_ ? Tr(L"流す") : Tr(L"止める"));
    const ui::Rect reset{play.Right() + 4, play.y, kBar, kBar};
    if (w::IconButton(c, "anim_tab.preview.reset", reset, w::Icon::Stop, !live)) Rebind(animator, rules);
    if (c.IsHovered(reset)) w::Tooltip(c, Tr(L"始めから (1 つ目の状態・パラメーターは初めの値・置いた所へ)"));

    const ui::Rect pic{r.x, r.y + kBar + 4, r.w, r.h - kBar - 4};
    space3d::Preview3D::Content content;
    content.meshes = draws;
    if (moving_) content.gridShift = {-(position_.x - std::floor(position_.x)), 0, -(position_.z - std::floor(position_.z))};
    if (!view_.Draw(c, pic, host.host.Graphics(), "anim_tab.preview", content)) {
        Say(c, pic.Inset(12), Tr(L"3D の描画を始められませんでした。くわしくはコンソールを見てください。"), theme::TextDim);
        return;
    }

    // 絵の下の端: 今の状態と根の速さ、または読めない理由 (文字だけ)。
    const ui::Rect info{pic.x + 10, pic.Bottom() - 26, pic.w - 20, 22};
    std::wstring text;
    ui::Color color = theme::TextDim;
    if (model_.Empty()) {
        text = Tr(L"同じエンティティの「メッシュ (3D モデル)」に、骨とクリップのあるモデルを入れると、ここで動きます");
    } else if (!error_.empty()) {
        text = TrF(L"モデルを読めません: {}", Utf8ToWide(error_));
        color = theme::Warn;
    } else if (!loaded_ || ticket_ != 0) {
        text = Tr(L"モデルを読んでいます…");
    } else if (!skinned_.skin) {
        text = Tr(L"このモデルには骨がありません");
        color = theme::Warn;
    } else if (shown) {
        const std::string_view state = StateName(*shown);
        text = TrF(L"{}: {} ・ 動き {:.2f} m/s ・ 回る {:.0f}°/s", live ? Tr(L"ゲーム") : Tr(L"プレビュー"),
                   state.empty() ? std::wstring(L"-") : Utf8ToWide(std::string(state)), speed_, turnRate_);
        if (!moving_ && speed_ > 0.01f) text += Tr(L" (その場で: 動きは捨てています)");
    }
    if (!text.empty()) {
        const float tw = std::min(info.w, c.P().TextWidth(text, 12) + 16);
        w::DrawSurface(c, {info.x - 4, info.y, tw, info.h}, w::Surface::Popup);
        Say(c, {info.x + 4, info.y, tw - 8, info.h}, text, color);
    }
}

}  // namespace rm::game_x_animation
