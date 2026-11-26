#include "rm_engine/game_x_3d/editor/SceneView3D.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "core/asset/ImportSettings.h"
#include "core/reflect/Data.h"
#include "core/reflect/FieldCommand.h"
#include "editor/lang/Lang.h"
#include "rm_engine/game_production/editor/DebugPanel.h"
#include "editor/project/Project.h"
#include "editor/tree/TreeModel.h"
#include "editor/tree/TreeView.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "rm_engine/game_production/editor/ComponentLook.h"
#include "rm_engine/game_x_3d/editor/SceneOverlay.h"
#include "rm_engine/game_x_3d/editor/SceneViewState.h"
#include "rm_engine/game_x_3d/runtime/Camera.h"
#include "rm_engine/game_x_3d/runtime/Light.h"
#include "rm_engine/game_x_3d/runtime/Material.h"
#include "rm_engine/game_x_3d/runtime/MeshPose.h"
#include "rm_engine/game_x_3d/runtime/MeshRenderer.h"
#include "rm_engine/game_x_3d/runtime/Sky.h"
#include "rm_engine/game_x_3d/runtime/SkyPaint.h"
#include "rm_engine/game_x_3d/runtime/WorldDraw.h"
#include "rm_engine/space3d/editor/Gizmo.h"
#include "rm_engine/space3d/runtime/Lighting.h"
#include "rm_engine/space3d/runtime/MeshFile.h"
#include "rm_engine/space3d/runtime/MeshLoader.h"
#include "rm_engine/space3d/runtime/Renderer3D.h"
#include "rm_engine/space3d/runtime/Texture.h"
#include "rm_engine/space3d/runtime/View.h"
#include "ui/Path.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string_view>
#include <cstring>
#include <deque>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rm::game_x_3d {

namespace {

namespace theme = editor::theme;
namespace w = editor::w;
namespace gizmo = space3d::gizmo;
using editor::Tr;
using editor::TrF;
using space3d::Mat4;
using space3d::Transform;
using space3d::operator*;  // Vec3 は rm::Vec3: 3D の演算子は space3d にある
using space3d::operator+;
using space3d::operator-;
using ui::Rect;

constexpr float kLookDegreesPerDip = 0.25f;
constexpr float kOrbitDegreesPerDip = 0.4f;
constexpr float kClickSlop = 4.f;  // これより小さくしか動かない押下はクリック
constexpr int kGridHalf = 20;      // 格子は -20 から 20 まで (1 本ごと、10 本ごとに濃く)
// 上の帯 (3D / 2D と見え方のスイッチ): 1 行の高さ、その中のボタンの高さ、ボタンの間。
// (2026-11-25: 左上に浮かんでいた 28 の高さのボタンをこの帯に移し、小さくした)
// (2026-11-25: 一度 28 / 22 / 6 と 30 / 3 まで小さくしたが小さすぎたので、前 (28 / 44 / 4) との中ほどに)
constexpr float kStripH = 31.f, kStripItemH = 25.f, kStripGap = 7.f;
constexpr float kToolSquare = 37.f, kToolGap = 4.f;   // 左の縦の列の正方形のボタン (最初は 44 / 4、小さすぎた 30 / 3)
constexpr float kSnapBoxW = 260.f, kSnapRowH = 30.f;  // グリッドの箱 (グリッド配置 の横)
constexpr double kModelCheckSeconds = 1.0;    // モデルのファイルを調べ直す間隔 (ディスクで変わったか)
constexpr double kModelForgetSeconds = 10.0;  // この間どのエンティティも使わないモデルは GPU から外す
constexpr double kMaterialLookSeconds = 0.25;  // マテリアル (.rmmat) を読み直す間隔 (インスペクターでの編集・ディスクでの変更)
constexpr float kFrustumShown = 10.f;  // カメラの形はここまでしか描かない (遠い面が 1000 でもよい)
constexpr float kMarkSize = 18.f;      // カメラ / ライト / 空のエンティティなどの印 (ピクセル)
constexpr float kMarkPick = 11.f;      // 印にこれだけ近いクリックで、そのエンティティを選ぶ
constexpr float kPreviewMaxW = 360.f;  // 右下のカメラプレビュー

// 左の縦の列 (正方形のボタン): グリッド配置・選択・移動・回転・拡縮。絵の左上に浮かぶ。
// (3D / 2D、ローカル / ワールド、それぞれ / 中心、影、空、デバッグ、形 は絵の上の帯: DrawStrip)
struct BarLayout {
    Rect grid, select, move, rotate, scale;
};

struct Placed {
    int id;
    Mat4 world;
    Mat4 parentWorld;
    bool active;
    // 描くもの: モデル (null: 単位立方体 - ファイルが無い メッシュ) と、その自分の空間での
    // 境界 - クリックが当たるものと、F で合わせる範囲。
    const space3d::GpuMesh* mesh = nullptr;
    bool plain = false;  // `mesh` が単純な形 (メッシュ の 形) で、マテリアルが無い: 箱と同じ色
    std::span<const space3d::DrawMaterial> materials;  // モデルの色と画像 (メッシュのマテリアルの欄で替えたもの)
    const SkinnedModel* skinned = nullptr;  // 骨のあるモデル: 骨とクリップ (姿勢は MeshPose.h の口に尋ねる)
    bool castShadow = true;
    Vec3 min{-0.5f, -0.5f, -0.5f}, max{0.5f, 0.5f, 0.5f};
    bool loading = false;  // モデルを読んでいる最中 (それまでは箱で、上に 読み込み中 を出す)
    // その上のカメラ / ライト (このフレーム用のコピー)。`marker`: メッシュ が無い - 自分では何も
    // 描かない (空のエンティティ、カメラ、ライト、パーティクル、音源 ...)。印 (`mark`: 階層での絵)
    // とカメラ / ライトの線で見え (クリックもできる)。
    std::optional<Camera> camera;
    std::optional<Light> light;
    bool ambient = false;  // 環境光がある (ライトの印で示す)
    bool marker = false;
    editor::w::Icon mark = editor::w::Icon::Point;

    // メッシュ を持つカメラやライトも印を残す。
    bool HasMark() const { return marker || camera || light || ambient; }
};

// Direct3D で描き、Direct2D で見せる絵 1 枚: ビューそのものか、その隅のカメラプレビュー
// (別の絵: 2 枚目を作るとき、1 枚目はまだ描かれるのを待っている)。
struct Picture {
    space3d::Renderer renderer;
    bool tried = false;
    // Direct2D のビットマップとしての絵。絵かウィンドウのコンテキストが変わると作り直す。
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
    ID3D11Texture2D* texture = nullptr;
    ID2D1DeviceContext* dc = nullptr;
    // 他のプラグインが世界に描くもの (WorldDraw.h: パーティクル)。最初のフレームで作る。
    // それと、この絵を最後に描いた時刻 (彼らの時間はその差だけ進む)。
    std::vector<std::unique_ptr<WorldDrawer>> drawers;
    bool drawersMade = false;
    std::optional<double> drawnAt;
    std::vector<space3d::ExtraDraw> extras;
    game_x_3d::SkyPainter sky;  // この絵が描くシーンの 空
    // 骨のあるモデルの姿勢を決めるもの (MeshPose.h: アニメーター)。最初のフレームで作る。
    std::vector<std::unique_ptr<MeshPoser>> posers;
    bool posersMade = false;
};

// r の真ん中にある、縦横比 `aspect` (幅 / 高さ) の最大の長方形。
Rect Fit(const Rect& r, float aspect) {
    const float a = std::max(aspect, 1e-3f);
    float w = r.w, h = r.w / a;
    if (h > r.h) {
        h = r.h;
        w = h * a;
    }
    return {r.x + (r.w - w) * 0.5f, r.y + (r.h - h) * 0.5f, w, h};
}

// d に直角な (そして互いに直角な) 2 つの向き: d を向く円。
std::pair<Vec3, Vec3> Across(Vec3 d) {
    const Vec3 u = space3d::Normalize(space3d::Cross(d, std::fabs(d.y) < 0.99f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}));
    return {u, space3d::Cross(u, d)};
}

// MeshRenderer に書かれたパスで 1 度読んで GPU に置いたままにするモデルファイル (同じモデルの
// エンティティ複数で共有)。ときどき調べ直す: ディスクで変わったファイル (3D ツールから書き出し
// 直したもの) は読み直し、読めないファイルは 1 度だけ知らせる。材質の画像ファイルは
// SceneView3D::textures_ で共有する (変わると自分で読み直す)。ファイルは読み込みスレッド
// (`ticket`) で読む。終わるまでは古い形のまま (最初は箱)。
struct ModelEntry {
    std::shared_ptr<const space3d::GpuMesh> gpu;
    space3d::ModelPaint paint;
    SkinnedModel skinned;  // 骨のあるモデルの骨とクリップ (無ければ skin は null)。読み直すと新しいもの
    space3d::MeshLoader::Ticket ticket = 0;  // 読み込み中
    std::filesystem::path file;              // ... このファイル
    std::string name;                        // その名前 (コンソール用)
    bool looked = false;  // ファイルを 1 度は探した
    bool loaded = false;  // ... そして `time` のときの状態で読んだ (読もうとした)
    bool told = false;    // 無い / プロジェクトの外にあることをコンソールに書いた
    std::filesystem::file_time_type time{};
    double checkedAt = 0, usedAt = 0;
};

// メッシュのマテリアルの欄が指すマテリアル (.rmmat)。kMaterialLookSeconds ごとに中身
// (SoftwareHost::AssetContent: インスペクターでの、まだ書いていない編集も) を見て、変わったら読み直す。
// 絵はテクスチャのキャッシュから毎フレーム (ファイルが変わるとキャッシュが読み直す)。
struct MaterialEntry {
    std::string bytes;              // 最後に読んだ中身
    std::optional<Material> material;
    space3d::DrawMaterial draw;     // このフレームの描き方 (MaterialLook)
    bool told = false;              // 読めないことをコンソールに書いた
    double lookedAt = -1e9, usedAt = 0;
};

// ギズモでドラッグしている 1 つ: ドラッグを始めたときの状態と、今の状態。
struct Edited {
    Transform start;
    Transform last;  // 最後にドラッグが残した状態
    Mat4 parentWorld, startWorld;
    std::function<reflect::EditTarget()> resolve;
};

const char* FieldOf(gizmo::Tool t) {
    switch (t) {
        case gizmo::Tool::Move: return "position";
        case gizmo::Tool::Rotate: return "rotation";
        case gizmo::Tool::Scale: return "scale";
    }
    return "position";
}

std::optional<size_t> FieldIndex(const char* name) {
    const auto fields = reflect::TypeOf<Transform>().fields;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (std::string_view(fields[i].name) == name) return i;
    }
    return std::nullopt;
}

class SceneView3D final : public game_production::SceneView {
public:
    void Draw(ui::Context& c, const Rect& panel, game_production::SceneViewHost& host) override {
        Rect r = panel;
        if (r.w < 8 || r.h < 8) return;
        DropGpuIfNew(host);
        if (!Ready(host)) {
            ui::TextStyle ts;
            ts.size = 13;
            ts.wrap = true;
            ts.color = theme::Danger;
            ts.h = ui::HAlign::Center;
            c.P().Text(r.Inset(24), Tr(L"3D の描画を始められませんでした。くわしくはコンソールを見てください。"), ts);
            return;
        }
        transformType_ = reflect::TypeOf<Transform>().name;
        meshType_ = reflect::TypeOf<MeshRenderer>().name;
        cameraType_ = reflect::TypeOf<Camera>().name;
        lightType_ = reflect::TypeOf<Light>().name;
        ambientType_ = reflect::TypeOf<AmbientLight>().name;
        // 空の時間はゲームが動く間、ゲームの時間の速さで進む (雲が流れ、星が瞬き、雨粒と雪が降る)。
        // 一時停止の間 (timeScale 0) は止まる (2026-11-22: 前は雨粒と雪が降り続けた)。Play の外は 0。
        skySeconds_ = host.playing ? skySeconds_ + std::clamp(c.Time() - now_, 0.0, 0.1) * std::max(host.timeScale, 0.f) : 0.0;
        now_ = c.Time();
        if (!looksMade_) MakeLooks(host);
        Place(host);
        ForgetUnusedModels();
        // 他のスレッドで読み込み中のモデル (と画像) が届くまで描き直す。
        if (loader_.Pending() > 0 || textures_.Waiting(now_) > 0) c.RequestAnimation();
        if (host.overhead) {  // 真上から見た地図 (気候のタブ): 絵だけ。入力は持ち主のもの
            DrawOverhead(c, r, host);
            return;
        }
        if (host.game) {  // ゲームビュー: 絵だけを、ゲームのカメラ越しに
            DrawGame(c, r, host);
            return;
        }
        // 上の帯 (3D / 2D と見え方): 絵はその下。ビューが低すぎるときは帯を出さない。
        if (r.h > 3 * kStripH + 40 && r.w > 120) DrawStrip(c, r);
        const std::optional<BarLayout> bar = Bar(r);
        if (!bar) snapOpen_ = false;
        previewAt_ = PreviewRect(r, host);  // プレビューへのクリックはシーンに届かない
        OverlayAreas(r, host);              // ほかのプラグインの操作 (アニメーターのプレビュー) も
        if (HandleInput(c, r, host, bar)) Place(host);  // このフレームにギズモのドラッグで物が動いた
        Build(host, false);
        Show(c, r, host, main_, SceneFrame(r));
        DrawMarks(c, r, host);
        DrawGizmo(c, r, host);
        previewAt_ = PreviewRect(r, host);
        if (previewAt_) DrawPreview(c, *previewAt_, host);
        OverlayAreas(r, host);
        for (const auto& [o, at] : overlays_) o->draw(c, at, host);
        if (bar) DrawBar(c, *bar);
        if (bar && snapOpen_) DrawSnapBox(c, SnapBox(r, *bar));
    }

    void LoadState(const json::Value& s) override {
        state_ = {};
        unknowns_ = {};
        reflect::Warnings warnings;
        if (s.IsObject()) reflect::Load(state_, s, &unknowns_, &warnings);
        for (const auto& w : warnings) log::Warn("シーンビューの見え方: {}", w);
        // 3D のカメラは遠近法、2D のカメラは正面からの平行投影 (ファイルに何が書かれていても)。
        state_.camera.orthographic = false;
        state_.flatCamera.orthographic = true;
        state_.flatCamera.yaw = 0;
        state_.flatCamera.pitch = 0;
    }
    json::Value SaveState() const override { return reflect::Save(state_, &unknowns_); }

private:
    bool Ready(game_production::SceneViewHost& host) { return Start(main_, host); }

    // 今のシーンビューのカメラ: 3D (遠近法) か 2D (正面からの平行投影)。それぞれ自分の見え方を覚える。
    space3d::OrbitView& Cam() { return state_.flat ? state_.flatCamera : state_.camera; }
    const space3d::OrbitView& Cam() const { return state_.flat ? state_.flatCamera : state_.camera; }

    // GPU のデバイスが作り直された (GPU が失われた: platform::Gfx::Generation): 古いものの上に
    // 作ったものはすべて消す - レンダラー (シェーダー、バッファー、影のマップ、画像とそのビット
    // マップ)、モデル、材質の画像。すべて必要になったときに作り直す (モデルファイルも読み直す)。
    void DropGpuIfNew(game_production::SceneViewHost& host) {
        const uint64_t now = host.host.Graphics().Generation();
        if (now == generation_) return;
        if (generation_ != 0) log::Info("シーンビュー: GPU が作り直されたので、3D の描画とモデルを作り直します");
        generation_ = now;
        for (auto& [guid, m] : models_) loader_.Cancel(m.ticket);
        models_.clear();
        materials_.clear();
        textures_.Clear();
        main_ = Picture{};
        preview_ = Picture{};
    }

    static bool Start(Picture& pic, game_production::SceneViewHost& host) {
        if (!pic.tried) {
            pic.tried = true;
            std::string err;
            if (!pic.renderer.Init(host.host.Graphics().D3D(), &err)) log::Error("シーンビュー: {}", err);
        }
        return pic.renderer.Ready();
    }

    // 部品の型の印 (その 階層 の絵) を 1 度だけ作る。位置だけのもの (Transform の「立方体」) は
    // 自分の印を持たない: 空のエンティティの印は輪。
    void MakeLooks(game_production::SceneViewHost& host) {
        looksMade_ = true;
        for (const auto& l : host.host.Registry().Contributions<game_production::ComponentLook>(game_production::kComponentLookPoint)) {
            if (!l.item->icon) continue;
            const w::Icon icon = editor::tree::PictureOf(editor::tree::IconNamed(l.item->icon));
            if (icon != w::Icon::Cube && icon != w::Icon::Object) markOf_[l.item->type().name] = icon;
        }
    }

    // Transform を持つ全エンティティのワールドでの位置 (親の Transform も含める) と、
    // その見た目 (モデルか形。メッシュ が無ければ印)。
    void Place(game_production::SceneViewHost& host) {
        const game_production::Scene& scene = host.scene;
        placed_.clear();
        usedStores_ = 0;
        textures_.SetCacheFolder(host.host.GetProject().CacheDir() / L"Textures");
        ambient_.reset();
        std::unordered_map<int, Mat4> world;  // 親は TreeOrder で子より先に来る
        for (const auto& t : scene.TreeOrder()) {
            const auto& e = *t.entity;
            // 環境光: 最初に動くものが決める (そのエンティティに位置は要らない)。
            const game_production::Component* ac = e.FindComponent(ambientType_);
            const bool hasAmbient = ac && ac->Known();
            if (hasAmbient && t.activeInHierarchy && !ambient_) ambient_ = ToRenderAmbient(*static_cast<const AmbientLight*>(ac->value.Get()));
            const auto it = world.find(e.data.parent);
            const Mat4 parent = it != world.end() ? it->second : Mat4{};
            const game_production::Component* comp = e.FindComponent(transformType_);
            if (comp && comp->Known()) {
                const Mat4 w = space3d::WorldMatrix(parent, *static_cast<const Transform*>(comp->value.Get()));
                world[e.data.id] = w;
                Placed p{e.data.id, w, parent, t.activeInHierarchy};
                p.castShadow = t.activeInHierarchy;  // オフのものは薄く出し、影は付けない
                const game_production::Component* mc = e.FindComponent(meshType_);
                if (mc && mc->Known()) {
                    const auto& mr = *static_cast<const MeshRenderer*>(mc->value.Get());
                    p.castShadow = p.castShadow && mr.castShadows;
                    // マテリアルの欄 (形は 0 番、モデルは i 番が自身の i 番の代わり: Material.h の MeshMaterials)。
                    slots_.clear();
                    for (const AssetRef& ref : mr.materials) slots_.push_back(MaterialOf(ref, host));
                    if (const auto shape = ShapeOf(mr.shape)) {
                        if (const space3d::GpuMesh* g = main_.renderer.ShapeOf(*shape)) {
                            p.mesh = g;
                            p.materials = MeshMaterials(true, {}, slots_, Store());
                            p.plain = p.materials.empty();  // マテリアルがあればモデルと同じ描き方 (選ぶと縁が光る)
                            p.min = g->min;
                            p.max = g->max;
                        }
                    } else {
                        bool loading = false;
                        if (ModelEntry* m = Model(mr.model, host, loading)) {
                            p.mesh = m->gpu.get();
                            p.materials = MeshMaterials(false, m->paint.Materials(main_.renderer, textures_, now_), slots_, Store());
                            p.skinned = m->skinned.skin ? &m->skinned : nullptr;
                            p.min = p.mesh->min;
                            p.max = p.mesh->max;
                        }
                        p.loading = loading;
                    }
                }
                if (const game_production::Component* cc = e.FindComponent(cameraType_); cc && cc->Known()) {
                    p.camera = *static_cast<const Camera*>(cc->value.Get());
                }
                if (const game_production::Component* lc = e.FindComponent(lightType_); lc && lc->Known()) {
                    p.light = *static_cast<const Light*>(lc->value.Get());
                }
                p.ambient = hasAmbient;
                p.marker = !(mc && mc->Known());
                for (const game_production::Component& other : e.components) {  // 階層と同じ: 絵を持つ最後のもの
                    if (!other.Known()) continue;
                    if (const auto m = markOf_.find(other.type); m != markOf_.end()) p.mark = m->second;
                }
                placed_.push_back(p);
            } else {
                world[e.data.id] = parent;  // 自分の位置は無い: 子は親に付いてくる
            }
        }
    }

    // `ref` が指すマテリアルの、このフレームの描き方。null: 空の欄、見つからない、読めない (その欄は
    // 無いものとして描く。見つからない・読めないことは 1 度だけコンソールに)。
    const space3d::DrawMaterial* MaterialOf(const AssetRef& ref, game_production::SceneViewHost& host) {
        if (ref.Empty()) return nullptr;
        MaterialEntry& m = materials_[ref.guid];
        m.usedAt = now_;
        if (now_ - m.lookedAt >= kMaterialLookSeconds) {
            m.lookedAt = now_;
            const auto bytes = host.host.AssetContent(ref);
            if (!bytes) {
                m.material.reset();
                m.bytes.clear();
                if (!m.told) log::Warn("シーンビュー: マテリアル ({}) がプロジェクトに見つからないので、その欄なしで描きます", ref.guid);
                m.told = true;
            } else if (*bytes != m.bytes || !m.material) {
                const bool changed = *bytes != m.bytes;
                m.bytes = *bytes;
                std::string err;
                m.material = ReadMaterial(*bytes, &err);
                if (m.material) {
                    m.told = false;
                } else if (!m.told || changed) {
                    log::Warn("シーンビュー: マテリアル「{}」を読めないので、その欄なしで描きます: {}", PathToUtf8(host.host.AssetFile(ref).filename()), err);
                    m.told = true;
                }
            }
        }
        if (!m.material) return nullptr;
        // 絵はキャッシュから (毎フレーム頼む: 使っている間は残り、ファイルが変わると読み直される)。
        m.draw = MaterialLook(*m.material, [&](const MaterialPicture& pic, size_t) -> space3d::TextureRef {
            const std::filesystem::path file = host.host.AssetFile(pic.image);
            return file.empty() ? nullptr : textures_.Get(main_.renderer, file, pic.use, pic.srgb, now_);
        });
        return &m.draw;
    }

    // このフレームでマテリアルを替えた物の材質の置き場 (Placed::materials が指す。次の Place まで動かない)。
    std::vector<space3d::DrawMaterial>& Store() {
        if (usedStores_ == stores_.size()) stores_.emplace_back();
        return stores_[usedStores_++];
    }

    // `ref` が指すモデル (アセット - 今どこにあっても)。最初に求められたときに
    // (読み込みスレッドで) 読む。null: 箱を描く。`loading`: そのファイルを今読んでいる。
    ModelEntry* Model(const AssetRef& ref, game_production::SceneViewHost& host, bool& loading) {
        if (ref.Empty()) return nullptr;
        ModelEntry& m = models_[ref.guid];
        m.usedAt = now_;
        Arrived(m, host);
        auto found = [&] {
            loading = m.ticket != 0;
            return m.gpu ? &m : nullptr;
        };
        if (m.looked && now_ - m.checkedAt < kModelCheckSeconds) return found();
        m.looked = true;
        m.checkedAt = now_;
        const std::filesystem::path file = host.host.AssetFile(ref);
        // (コンソール用: ファイルの名前。ファイルが無いときは id)
        const std::string path = file.empty() ? ref.guid : PathToUtf8(file.filename());
        std::error_code ec;
        // (モデル、または .meta にある取り込みの設定: 大きさを変えると読み直す)
        const auto time = file.empty() ? std::filesystem::file_time_type{} : asset::ChangedAt(file, ec);
        if (file.empty() || ec) {
            loader_.Cancel(m.ticket);
            m.ticket = 0;
            m.gpu.reset();
            m.loaded = false;
            if (!m.told) {
                log::Warn("シーンビュー: モデル「{}」がプロジェクトに見つからないので箱で描きます (消されたか、アセットの外のファイル)", path);
                m.told = true;
            }
            return nullptr;
        }
        m.told = false;
        if (m.loaded && time == m.time) return found();  // 変わっていない
        // 新規か変更あり: 読み込みスレッドで読む (古いファイルの読み込みがまだ続いていれば捨てる)。
        m.loaded = true;
        m.time = time;
        loader_.Cancel(m.ticket);
        m.ticket = loader_.Request(file);
        m.file = file;
        m.name = path;
        return found();
    }

    // 読み込みスレッドが読み終えたモデル: 画像と一緒に GPU に載せる (ここ、描画スレッドで)。
    // それまで描いていた形の代わりにする。
    void Arrived(ModelEntry& m, game_production::SceneViewHost& host) {
        if (m.ticket == 0) return;
        std::optional<space3d::MeshLoader::Result> done = loader_.Take(m.ticket);
        if (!done) return;
        m.ticket = 0;
        const bool again = m.gpu != nullptr;
        if (!done->mesh) {
            m.gpu.reset();
            m.skinned = {};
            log::Warn("シーンビュー: モデル「{}」を読めないので箱で描きます: {}", m.name, done->error);
            return;
        }
        space3d::MeshData& mesh = *done->mesh;
        for (const std::string& w : mesh.warnings) log::Warn("シーンビュー: モデル「{}」: {}", m.name, w);
        std::string error;
        UseSlotZeroForBareModel(mesh);  // (材質の無いモデルにも、メッシュの 0 番のマテリアルが入る)
        m.gpu = main_.renderer.Upload(mesh, &error);
        if (!m.gpu) {
            m.skinned = {};
            log::Warn("シーンビュー: モデル「{}」を GPU に載せられないので箱で描きます: {}", m.name, error);
            return;
        }
        m.paint = {};
        m.paint.Load(mesh, m.file, host.host.GetProject().AssetsDir(), m.name);
        m.skinned = TakeSkinned(mesh);  // (GPU に上げた後で。姿勢を尋ねるときに渡す)
        log::Info("シーンビュー: モデル「{}」を{}読みました (頂点 {}・三角形 {}・材質 {}・テクスチャ {}・{:.2f} 秒)", m.name, again ? "もう一度" : "",
                  mesh.vertices.size(), mesh.indices.size() / 3, mesh.materials.size(), m.paint.Pictures(), done->seconds);
    }

    // しばらくどこからも使われないモデル (と画像) は GPU から外す (戻ってきたら読み直す)。
    void ForgetUnusedModels() {
        std::erase_if(models_, [&](const auto& kv) {
            if (now_ - kv.second.usedAt <= kModelForgetSeconds) return false;
            loader_.Cancel(kv.second.ticket);  // もう要らない: 読まない (または結果を捨てる)
            return true;
        });
        std::erase_if(materials_, [&](const auto& kv) { return now_ - kv.second.usedAt > kModelForgetSeconds; });
        textures_.ForgetUnused(now_, kModelForgetSeconds);
    }

    const Placed* FindPlaced(int id) const {
        for (const Placed& p : placed_) {
            if (p.id == id) return &p;
        }
        return nullptr;
    }

    // ギズモは、位置を持つ最後に選んだエンティティ (最後にクリックしたもの) に置く。
    const Placed* Primary(const game_production::SceneViewHost& host) const {
        for (auto it = host.selection.rbegin(); it != host.selection.rend(); ++it) {
            if (const Placed* p = FindPlaced(*it)) return p;
        }
        return nullptr;
    }

    // ギズモ: 最後に選んだ 1 つの上 (それぞれ)、または選んだもの全体の真ん中 (中心) -
    // 動くもの、つまり選んだ親の子は数えない - に置き、軸は最後のものに合わせる。
    std::optional<gizmo::Frame> GizmoFrame(const Rect& r, const game_production::SceneViewHost& host) const {
        if (state_.selecting) return std::nullopt;  // (選択の道具: ギズモは出さない)
        const Placed* p = Primary(host);
        if (!p) return std::nullopt;
        if (state_.pivot == gizmo::Pivot::Each) return gizmo::MakeFrame(p->world, state_.tool, state_.space, Cam(), r.h);
        Vec3 sum{0, 0, 0};
        int n = 0;
        for (const int id : host.scene.Roots(host.selection)) {
            if (const Placed* q = FindPlaced(id)) {
                sum = sum + space3d::TransformPoint(q->world, {0, 0, 0});
                ++n;
            }
        }
        const Vec3 center = n > 0 ? sum * (1.f / static_cast<float>(n)) : space3d::TransformPoint(p->world, {0, 0, 0});
        return gizmo::MakeFrame(center, gizmo::AxesOf(p->world, state_.tool, state_.space), Cam(), r.h);
    }

    Mat4 ViewProjection(const Rect& r) const { return Cam().ProjectionMatrix(r.w / r.h) * Cam().ViewMatrix(); }

    // 道具の列 (絵の左上)。入らないほどビューが小さいときは出さない。
    static std::optional<BarLayout> Bar(const Rect& r) {
        if (r.w < kToolSquare + 32 || r.h < 5 * (kToolSquare + kToolGap) + 16) return std::nullopt;
        BarLayout b;
        float ty = r.y + 7;
        for (Rect* sq : {&b.grid, &b.select, &b.move, &b.rotate, &b.scale}) {
            *sq = {r.x + 7, ty, kToolSquare, kToolSquare};
            ty += kToolSquare + kToolGap;
        }
        return b;
    }

    // グリッドの箱 (刻む幅): グリッド配置 の右 (ビューの中に収める)。
    static Rect SnapBox(const Rect& r, const BarLayout& b) {
        const float h = 10 + 3 * kSnapRowH + 30 + 8;
        return {b.grid.Right() + 6, std::min(b.grid.y, r.Bottom() - 8 - h), kSnapBoxW, h};
    }

    // ほかのプラグインが絵の上に置く操作 (SceneOverlay.h) の、今の場所。止まっているシーンビューだけ。
    void OverlayAreas(const Rect& r, const game_production::SceneViewHost& host) {
        overlays_.clear();
        if (host.playing || host.game) return;
        if (!overlayInfos_) {
            overlayInfos_.emplace();
            for (const auto& o : host.host.Registry().Contributions<SceneOverlayInfo>(kSceneOverlayPoint)) {
                if (o.item->area && o.item->draw) overlayInfos_->push_back(o.item);
            }
        }
        for (const SceneOverlayInfo* o : *overlayInfos_) {
            if (const auto at = o->area(r, host)) overlays_.emplace_back(o, *at);
        }
    }

    bool OverBar(const Rect& r, const std::optional<BarLayout>& bar, float x, float y) const {
        if (previewAt_ && previewAt_->Inset(-4, -24, -4, -4).Contains(x, y)) return true;  // カメラプレビュー
        for (const auto& [o, at] : overlays_) {
            if (at.Contains(x, y)) return true;
        }
        if (!bar) return false;
        for (const Rect& part : {bar->grid, bar->select, bar->move, bar->rotate, bar->scale}) {
            if (part.Contains(x, y)) return true;
        }
        return snapOpen_ && SnapBox(r, *bar).Contains(x, y);
    }

    // ギズモのドラッグがこのフレームでシーンを変えたときに true を返す。
    bool HandleInput(ui::Context& c, const Rect& r, game_production::SceneViewHost& host, const std::optional<BarLayout>& bar) {
        const ui::Input& in = c.In();
        const double now = c.Time();
        const float dt = static_cast<float>(std::clamp(now - lastTime_, 0.0, 0.1));
        lastTime_ = now;
        const bool overBar = OverBar(r, bar, in.mouseX, in.mouseY);
        bool hovered = c.IsHovered(r) && !overBar;
        // 刻みの箱は、外のクリック (そのクリックは他に何もしない) か Esc で閉じる。
        if (snapOpen_ && drag_ == Drag::None) {
            const bool pressed = c.MousePressed() || c.MousePressed(ui::Mouse::Right) || c.MousePressed(ui::Mouse::Middle);
            if ((pressed && !overBar) || (c.Focused() == 0 && c.KeyPressed(ui::Key::Escape))) {
                snapOpen_ = false;
                if (pressed) hovered = false;
            }
        }
        // マウスがどれだけ動いたか (カーソルがドラッグの始点に留められている間も数える)。
        const float dx = in.mouseDX, dy = in.mouseDY;
        const Vec2 mouse{in.mouseX, in.mouseY};
        bool changed = false;

        const std::optional<gizmo::Screen> screen = gizmo::MakeScreen(ViewProjection(r), r.x, r.y, r.w, r.h);
        // Play ではシーンは遊ぶ側のコピー: 見せるだけで編集しない (ギズモなし)。
        const std::optional<gizmo::Frame> frame = host.playing ? std::nullopt : GizmoFrame(r, host);
        // マウスの下のハンドル (黄色)。ほかに何も起きていないとき。
        if (drag_ == Drag::None) {
            hot_ = hovered && !in.alt && frame && screen
                       ? gizmo::HitTest(state_.tool, *frame, *screen, Cam().EyeFor(frame->origin), mouse)
                       : gizmo::Handle::None;
        } else if (drag_ != Drag::Gizmo) {
            hot_ = gizmo::Handle::None;  // 見回し中: ハンドルは光らない
        }

        // ドラッグを始める (ビューの中だけ)。ボタンを離すまで、外に出ても続く。
        // 2D (平行投影の正面): 回さない - 右ドラッグと Alt + 左ドラッグもずらす。
        if (drag_ == Drag::None && hovered) {
            dragButton_ = 0;
            if (c.MousePressed(ui::Mouse::Right)) {
                drag_ = state_.flat ? Drag::Pan : Drag::Look;
                dragButton_ = 1;
            } else if (c.MousePressed(ui::Mouse::Middle)) {
                drag_ = Drag::Pan;
                dragButton_ = 2;
            } else if (c.MousePressed() && in.alt) {
                drag_ = state_.flat ? Drag::Pan : Drag::Orbit;
            } else if (c.MousePressed() && hot_ != gizmo::Handle::None && BeginGizmo(host, *frame, *screen, mouse)) {
                drag_ = Drag::Gizmo;
            } else if (c.MousePressed()) {
                drag_ = Drag::Click;
                pressX_ = in.mouseX;
                pressY_ = in.mouseY;
            }
        } else if (drag_ != Drag::None) {
            // ボタンを押したまま Undo / Redo (Ctrl+Z): シーンがドラッグを始めたときのものでなくなる
            // ことがある (物が消えた、Transform が戻った) - ドラッグを止める (何も積まない)。
            if (drag_ == Drag::Gizmo && HistoryMoved(host)) {
                AbandonGizmo();
                drag_ = Drag::Cancelled;
            }
            const bool held = in.down[dragButton_];
            if (!held) {
                if (drag_ == Drag::Click && std::hypot(in.mouseX - pressX_, in.mouseY - pressY_) < kClickSlop) Pick(r, host);
                if (drag_ == Drag::Gizmo) EndGizmo(host);
                drag_ = Drag::None;
            } else if (drag_ == Drag::Look) {
                space3d::Look(Cam(), -dx * kLookDegreesPerDip, dy * kLookDegreesPerDip);
                Fly(c, dt);
            } else if (drag_ == Drag::Pan) {
                space3d::Pan(Cam(), dx, dy, r.h);
            } else if (drag_ == Drag::Orbit) {
                space3d::Orbit(Cam(), -dx * kOrbitDegreesPerDip, dy * kOrbitDegreesPerDip);
            } else if (drag_ == Drag::Gizmo) {
                if (c.KeyPressed(ui::Key::Escape)) {
                    CancelGizmo();  // すべて元の場所に戻る。ボタンはまだ離す必要がある
                    drag_ = Drag::Cancelled;
                } else if (screen) {
                    // Ctrl: 刻みで動く - 常に刻みのときは、なめらかに動く。
                    gizmo::Snap snap;
                    snap.on = in.ctrl != state_.snapAlways;
                    snap.move = state_.snapMove;
                    snap.degrees = state_.snapDegrees;
                    snap.scale = state_.snapScale;
                    if (const auto change = gizmo::Update(*gizmo_, *screen, mouse, snap)) ApplyGizmo(*change);
                }
                changed = true;
            }
        }
        // 回す / 動かすのは画面の端で止まらない: カーソルは隠れ、ドラッグを始めた所に留まり
        // (最初のフレームから)、ボタンを離すとそこへ戻る。
        if (drag_ == Drag::Look || drag_ == Drag::Pan || drag_ == Drag::Orbit) c.LockMouse();
        // ホイール: 3D は近づく / 遠ざかる。2D は見える大きさを変え、マウスの下の点はそこに残る。
        if (hovered && in.wheel != 0.f) {
            space3d::ZoomAt(Cam(), in.wheel, (in.mouseX - r.x) / r.w * 2.f - 1.f, 1.f - (in.mouseY - r.y) / r.h * 2.f, r.w / r.h);
        }
        if (hovered && c.Focused() == 0 && drag_ != Drag::Look && drag_ != Drag::Gizmo) {
            if (c.KeyPressed(ui::Key::F)) FrameSelection(host);
            if (c.KeyPressed(ui::Key::Q)) state_.selecting = true;
            if (c.KeyPressed(ui::Key::W)) PickTool(gizmo::Tool::Move);
            if (c.KeyPressed(ui::Key::E)) PickTool(gizmo::Tool::Rotate);
            if (c.KeyPressed(ui::Key::R)) PickTool(gizmo::Tool::Scale);
            if (c.KeyPressed(ui::Key::X)) ToggleSpace();
            if (c.KeyPressed(ui::Key::Z)) TogglePivot();
        }
        return changed;
    }

    void PickTool(gizmo::Tool tool) {
        state_.tool = tool;
        state_.selecting = false;
    }
    void ToggleSpace() { state_.space = state_.space == gizmo::Space::Local ? gizmo::Space::World : gizmo::Space::Local; }
    void TogglePivot() { state_.pivot = state_.pivot == gizmo::Pivot::Each ? gizmo::Pivot::Center : gizmo::Pivot::Each; }

    // 見回している間 (右ボタンを押している): W A S D で前 / 左 / 後ろ / 右、Space (か E) で上、
    // Ctrl (か Q) で下 - Shift を押すとどれも速くなる。
    void Fly(ui::Context& c, float dt) {
        // 見回している間は、キーを押していなくてもフレームを続けて描く (2026-11-20)。前はキーを
        // 離すとフレームが止まり、次のキー (反対向きの D・S・Ctrl など) で起きたフレームの dt が
        // 止まっていた間の長さ (最大 0.1 秒) になって一瞬跳んだ。Ctrl だけはキーの知らせでフレームが
        // 起きないので、マウスを動かすまで固まっていた。
        c.RequestAnimation();
        auto axis = [&](ui::Key plus, ui::Key minus) { return (c.KeyDown(plus) ? 1.f : 0.f) - (c.KeyDown(minus) ? 1.f : 0.f); };
        const float up = (c.KeyDown(ui::Key::Space) || c.KeyDown(ui::Key::E) ? 1.f : 0.f) - (c.In().ctrl || c.KeyDown(ui::Key::Q) ? 1.f : 0.f);
        const Vec3 dir{axis(ui::Key::D, ui::Key::A), up, axis(ui::Key::W, ui::Key::S)};
        if (dir.x == 0 && dir.y == 0 && dir.z == 0) return;
        // ゆっくり。細かく動かすため (2026-11-19: 前の 4 分の 1 - 少なくとも 0.4 m/s、距離の 0.15 倍)。
        // 長い距離は Shift。
        const float speed = std::max(0.4f, Cam().distance * 0.15f) * (c.In().shift ? 4.f : 1.f);
        space3d::Fly(Cam(), dir * (speed * dt));
    }

    // --- ギズモ ------------------------------------------------------------------------------
    // 位置を持つ選んだものはすべてギズモと一緒に動く - ただし親も選ばれているものは別
    // (もう親と一緒に動く)。
    bool BeginGizmo(game_production::SceneViewHost& host, const gizmo::Frame& frame, const gizmo::Screen& screen, Vec2 mouse) {
        auto drag = gizmo::BeginDrag(state_.tool, hot_, frame, screen, Cam().EyeFor(frame.origin), mouse);
        if (!drag || !host.component) return false;
        edited_.clear();
        for (const int id : host.scene.Roots(host.selection)) {
            const Placed* p = FindPlaced(id);
            if (!p) continue;
            auto resolve = host.component(id, transformType_);
            const reflect::EditTarget t = resolve ? resolve() : reflect::EditTarget{};
            if (!t.object) continue;
            const Transform now = *static_cast<const Transform*>(t.object);
            edited_.push_back({now, now, p->parentWorld, p->world, std::move(resolve)});
        }
        if (edited_.empty()) return false;
        history_ = {host.host.History().UndoCount(), host.host.History().RedoCount()};
        gizmo_ = std::move(drag);
        dragTool_ = state_.tool;
        dragSpace_ = state_.space;
        // 中心: すべてが、ギズモのある真ん中を軸に回る / 拡大縮小する。
        pivot_ = state_.pivot == gizmo::Pivot::Center ? std::optional<Vec3>(frame.origin) : std::nullopt;
        return true;
    }

    // ドラッグ中は Transform を直接変える (インスペクターも追従する)。履歴にはボタンを離した
    // ときに 1 つだけ積む。
    void ApplyGizmo(const gizmo::Change& change) {
        for (Edited& e : edited_) {
            const reflect::EditTarget t = e.resolve();
            if (!t.object) continue;  // 消えた (ドラッグ中に Undo された)
            auto* tr = static_cast<Transform*>(t.object);
            *tr = gizmo::Apply(e.start, e.parentWorld, e.startWorld, change, dragSpace_, tr->rotation, pivot_);
            e.last = *tr;
        }
    }

    // ドラッグを始めてから履歴が変わった (Undo / Redo、または別の所での編集)。
    bool HistoryMoved(game_production::SceneViewHost& host) const {
        return history_ != std::pair{host.host.History().UndoCount(), host.host.History().RedoCount()};
    }

    // 履歴が動いたあとでドラッグを止める: ドラッグが変えた項目 - その後ほかに何も変わって
    // いないもの - は始めた値に戻る (Undo / Redo が設定したものは残る)。何も積まない。
    void AbandonGizmo() {
        for (const Edited& e : edited_) {
            const reflect::EditTarget t = e.resolve();
            if (!t.object) continue;  // 消えた
            auto& tr = *static_cast<Transform*>(t.object);
            if (tr.position == e.last.position) tr.position = e.start.position;
            if (tr.rotation == e.last.rotation) tr.rotation = e.start.rotation;
            if (tr.scale == e.last.scale) tr.scale = e.start.scale;
        }
        edited_.clear();
        gizmo_.reset();
        log::Info("ドラッグ中に元に戻す / やり直しがあったので、ドラッグをやめました");
    }

    void CancelGizmo() {
        for (const Edited& e : edited_) {
            const reflect::EditTarget t = e.resolve();
            if (t.object) *static_cast<Transform*>(t.object) = e.start;
        }
        edited_.clear();
        gizmo_.reset();
    }

    // 編集 = Undo の 1 つ = 未保存 (憲法 7): 何を動かしても、ドラッグ全体で 1 つ。
    void EndGizmo(game_production::SceneViewHost& host) {
        const auto index = FieldIndex(FieldOf(dragTool_));
        const char* label = dragTool_ == gizmo::Tool::Move ? "移動" : dragTool_ == gizmo::Tool::Rotate ? "回転" : "拡大縮小";
        auto group = std::make_unique<CommandGroup>(label);
        if (index) {
            const reflect::TypeDesc& type = reflect::TypeOf<Transform>();
            const reflect::FieldDesc& field = type.fields[*index];
            for (const Edited& e : edited_) {
                const reflect::EditTarget t = e.resolve();
                if (!t.object) continue;
                json::Value before = reflect::SaveField(field, &e.start, nullptr);
                json::Value after = reflect::SaveField(field, t.object, nullptr);
                if (before == after) continue;
                group->Add(std::make_unique<reflect::SetFieldCommand>(type, reflect::FieldPath{*index}, std::move(before), std::move(after),
                                                                      e.resolve, label));
            }
        }
        if (!group->Empty()) host.host.History().Push(std::move(group));  // ドラッグ中にもう適用済み
        edited_.clear();
        gizmo_.reset();
    }

    void Pick(const Rect& r, game_production::SceneViewHost& host) {
        const auto inv = space3d::Inverse(ViewProjection(r));
        if (!inv) return;
        const float ndcX = (pressX_ - r.x) / r.w * 2.f - 1.f;
        const float ndcY = 1.f - (pressY_ - r.y) / r.h * 2.f;
        // 印 (カメラ、ライト、空のエンティティ ...) は何よりも上に描く: マウスの下の一番近いものが先。
        if (const auto screen = gizmo::MakeScreen(ViewProjection(r), r.x, r.y, r.w, r.h)) {
            float nearest = kMarkPick;
            int mark = 0;
            for (const Placed& p : placed_) {
                if (!p.HasMark()) continue;
                const auto at = screen->ToScreen(space3d::TransformPoint(p.world, {0, 0, 0}));
                if (!at) continue;
                const float d = std::hypot(at->x - pressX_, at->y - pressY_);
                if (d <= nearest) {
                    nearest = d;
                    mark = p.id;
                }
            }
            if (mark != 0) {
                host.select({mark});
                return;
            }
        }
        const space3d::Ray ray = space3d::ScreenRay(*inv, ndcX, ndcY);
        std::optional<float> best;
        int hit = 0;
        for (const Placed& p : placed_) {
            if (p.marker) continue;  // 自分では当たるものが無い
            const auto t = space3d::IntersectPlacedBox(ray, p.world, p.min, p.max);
            if (t && (!best || *t < *best)) {
                best = t;
                hit = p.id;
            }
        }
        host.select(best ? std::vector<int>{hit} : std::vector<int>{});
    }

    void FrameSelection(game_production::SceneViewHost& host) {
        if (const Placed* p = Primary(host); p && p->marker) {
            space3d::Frame(Cam(), space3d::TransformPoint(p->world, {0, 0, 0}), 1.f);  // 印
        } else if (p) {
            // その境界の真ん中 (モデルの原点は足元にあることがある)。届く範囲まで。
            const Vec3 center = space3d::TransformPoint(p->world, (p->min + p->max) * 0.5f);
            const float size = space3d::Length(space3d::TransformDirection(p->world, p->max - p->min));
            space3d::Frame(Cam(), center, std::max(size * 0.5f, 0.5f));
        }
    }

    static bool IsSelected(const game_production::SceneViewHost& host, int id) {
        return std::find(host.selection.begin(), host.selection.end(), id) != host.selection.end();
    }

    // 最近動いたもの (ギズモでドラッグした、Play のシステムが回した): レンダラーは影のマップの
    // 1 つの層をそれらなしで持ち、動いている間はそれらだけを描き直す。1 秒間止まっていれば
    // また止まっているものとみなす (そのマップの層は、もう 1 度描かれる)。
    void NoteMotion() {
        constexpr double kStillSeconds = 1.0;
        std::unordered_map<int, Motion> next;
        next.reserve(placed_.size());
        for (const Placed& p : placed_) {
            Motion m{p.world, -kStillSeconds - 1};
            if (const auto it = motion_.find(p.id); it != motion_.end()) {
                m.movedAt = std::memcmp(&it->second.world, &p.world, sizeof(Mat4)) != 0 ? now_ : it->second.movedAt;
            }
            next.emplace(p.id, m);
        }
        motion_ = std::move(next);
    }
    bool Moving(int id) const {
        const auto it = motion_.find(id);
        return it != motion_.end() && now_ - it->second.movedAt < 1.0;
    }

    // このフレームで描くもの: 格子と軸、物 (箱、モデル)、ライト。カメラとライトの形と、選択の
    // 輪郭。`gameLook`: ゲームが見るとおり (ゲームビュー、カメラプレビュー) - オンのものだけで、
    // エディターのものは何も出さない。
    void Build(const game_production::SceneViewHost& host, bool gameLook) {
        lines_.clear();
        boxes_.clear();
        meshes_.clear();
        meshOwners_.clear();
        lights_.clear();
        auto line = [&](Vec3 a, Vec3 b, ui::Color col) {
            lines_.push_back({a, col});
            lines_.push_back({b, col});
        };
        // 床 (y = 0) の格子と、真ん中を通る軸。2D (正面から) は、カメラの方を向く XY の面 (z = 0) の格子。
        const float h = static_cast<float>(kGridHalf);
        for (int i = -kGridHalf; i <= kGridHalf && !gameLook; ++i) {
            const float v = static_cast<float>(i);
            const ui::Color minor = i % 10 == 0 ? theme::SceneGridMajor() : theme::SceneGrid();
            if (state_.flat) {
                line({v, -h, 0}, {v, h, 0}, i == 0 ? theme::AxisY() : minor);  // Y に沿って (線 x = 0 が Y 軸)
                line({-h, v, 0}, {h, v, 0}, i == 0 ? theme::AxisX() : minor);  // X に沿って
            } else {
                line({v, 0, -h}, {v, 0, h}, i == 0 ? theme::AxisZ() : minor);  // Z に沿って (線 x = 0 が Z 軸)
                line({-h, 0, v}, {h, 0, v}, i == 0 ? theme::AxisX() : minor);  // X に沿って
            }
        }
        if (!gameLook && !state_.flat) line({0, 0, 0}, {0, 1.5f, 0}, theme::AxisY());
        // Play: スクリプトが rm::Debug で 3D に描いた線 (このシーンビューだけ、デバッグ がオンのとき)。
        debugLines_.clear();
        if (host.debug && (gameLook || state_.debugDraw)) {
            for (const game_production::DebugLine& l : host.debug->Lines()) {
                if (l.space != game_production::DebugSpace::Space3D) continue;
                debugLines_.push_back({l.a, l.color});
                debugLines_.push_back({l.b, l.color});
            }
        }
        // プラグインがシーンの上に見せる線 (物理: コライダーの形): シーンビューだけ。
        if (!gameLook) {
            sceneLines_.Clear();
            for (const auto& l : host.host.Registry().Contributions<game_production::SceneLinesInfo>(game_production::kSceneLinesPoint)) {
                if (l.item->lines) l.item->lines(host.scene, host.selection, state_.allShapes, sceneLines_);
                if (l.item->hostLines) l.item->hostLines(host.host, host.scene, host.selection, state_.allShapes, sceneLines_);
            }
            for (const game_production::DebugLine& l : sceneLines_.Lines()) {
                if (l.space != game_production::DebugSpace::Space3D) continue;
                debugLines_.push_back({l.a, l.color});
                debugLines_.push_back({l.b, l.color});
            }
        }

        NoteMotion();
        for (const Placed& p : placed_) {
            if (p.light && p.active) lights_.push_back(ToRenderLight(*p.light, p.world));  // オフのライトは暗い
            if (gameLook && !p.active) continue;  // オフ: ゲームには出ない
            const bool selected = !gameLook && IsSelected(host, p.id);
            if (!gameLook && p.camera) CameraLines(p, selected, host.aspect);
            if (!gameLook && p.light) LightLines(p, selected);
            if (p.marker) continue;
            if (p.mesh && p.plain) {
                // 単純な形: 箱の色。
                const ui::Color col = selected ? theme::SceneObjectSelected() : p.active ? theme::SceneObject() : theme::SceneObjectInactive();
                meshes_.push_back({p.mesh, p.world, col, p.castShadow, {}, {0, 0, 0, 0}, Moving(p.id)});
                meshOwners_.push_back({p.id, nullptr});
            } else if (p.mesh) {
                // モデルは自分の色を持つ: 選んだものは縁が光る (枠も付く)。
                const ui::Color glow = theme::SceneSelectedGlow();
                meshes_.push_back({p.mesh, p.world, p.active ? theme::SceneModel() : theme::SceneModelInactive(), p.castShadow, p.materials,
                                   selected ? glow : glow.Alpha(0), Moving(p.id)});
                meshOwners_.push_back({p.id, p.skinned});
            } else {
                const ui::Color col = selected ? theme::SceneObjectSelected() : p.active ? theme::SceneObject() : theme::SceneObjectInactive();
                boxes_.push_back({p.world, col, p.castShadow, Moving(p.id)});
            }
            if (selected) Outline(p.world, p.min, p.max, theme::Accent);
        }
        if (lights_.size() > space3d::kMaxLights && lights_.size() != warnedLights_) {
            log::Warn("シーンビュー: 光が {} 個あります。描くのは最初の {} 個だけです", lights_.size(), space3d::kMaxLights);
        }
        warnedLights_ = lights_.size() > space3d::kMaxLights ? lights_.size() : 0;
        // 影が使えるマップを見つけられなかった点光源とスポット (番号ごとに 1 度だけ知らせる)。
        const size_t leftOut = gameLook ? warnedShadows_ : space3d::MakeLights(lights_, {}, {}, Cam().Eye()).shadowsLeftOut;
        if (leftOut > 0 && leftOut != warnedShadows_) {
            log::Warn("シーンビュー: 影の地図が足りないので、点光源・スポットの {} 個は影を落としません (地図は {} 枚: 点光源は 6 枚、スポットは 1 枚)", leftOut,
                      space3d::kLocalShadowSlices);
        }
        warnedShadows_ = leftOut;
    }

    // シーンビュー自身のカメラ (編集用のカメラ)。
    space3d::RenderFrame SceneFrame(const Rect& r) const {
        space3d::RenderFrame f;
        f.view = Cam().ViewMatrix();
        f.projection = Cam().ProjectionMatrix(r.w / r.h);
        f.clear = theme::SceneBackground();
        f.shadows = state_.shadows;
        f.shadowColor = theme::SceneShadow();
        return f;
    }

    // シーンのカメラ越し。ゲームが見るとおり。
    static space3d::RenderFrame CameraFrame(const Placed& cam, const game_production::SceneViewHost& host) {
        space3d::RenderFrame f;
        f.view = space3d::ViewFromWorld(cam.world);
        f.projection = ProjectionOf(*cam.camera, host.aspect);
        f.clear = BackgroundOf(*cam.camera, host.background);
        f.shadows = true;  // 光が決める (影を落とす)
        f.shadowColor = theme::SceneShadow();
        return f;
    }

    // Build が作ったものを `pic` に描き、r に見せる (このウィンドウのピクセルで: 高 DPI の画面でも
    // くっきり)。
    void Show(ui::Context& c, const Rect& r, game_production::SceneViewHost& host, Picture& pic, space3d::RenderFrame f) {
        ID2D1DeviceContext* dc = c.P().DC();
        float dpiX = 96, dpiY = 96;
        dc->GetDpi(&dpiX, &dpiY);
        const float scale = dpiX / 96.f;
        f.width = static_cast<uint32_t>(std::max(1.f, std::round(r.w * scale)));
        f.height = static_cast<uint32_t>(std::max(1.f, std::round(r.h * scale)));
        f.lines = lines_;
        f.debugLines = debugLines_;
        f.boxes = boxes_;
        f.meshes = meshes_;
        f.ambient = ambient_.value_or(space3d::Ambient{});
        // シーンの 空 は何よりも後ろ (シーンビュー自身のカメラ: 空 がオンのとき)。それに従う光も
        // 同じ - シーンビューでもゲームでも同じになる。
        frameLights_ = lights_;
        // (真上から見た地図: 空とそれに従う光・天候の粒は描かない)
        const game_x_3d::Sky* sky = overhead_ ? nullptr : host.scene.Setting<game_x_3d::Sky>();
        const bool ownCamera = &pic == &main_ && !host.game;
        if (sky && sky->kind != game_x_3d::SkyKind::None) {
            Vec3 eye{0, 0, 0};  // (場所ごとの雲の地図はカメラのまわり)
            if (const auto toWorld = space3d::Inverse(f.view)) eye = space3d::TransformPoint(*toWorld, {0, 0, 0});
            skyLook_ = game_x_3d::LookOf(*sky, skySeconds_, game_x_3d::SkyWeatherOf(&host.host.Registry(), host.scene, skySeconds_, eye));
            const game_x_3d::SkyLook& look = skyLook_;
            if (!ownCamera || state_.sky) {
                game_x_3d::SkyPainter::Sources sources;
                sources.picture = [&](const AssetRef& ref) {
                    textures_.SetCacheFolder(host.host.GetProject().CacheDir() / L"Textures");
                    return textures_.Get(main_.renderer, host.host.AssetFile(ref), space3d::TextureUse::Color, true, now_);
                };
                // (エディターが持つとおり: 保存した .rmsky の編集がすぐ出る)
                sources.code = [&host](const AssetRef& ref) { return host.host.AssetContent(ref); };
                sources.name = [&host](const AssetRef& ref) { return PathToUtf8(host.host.AssetFile(ref).filename().wstring()); };
                sources.now = now_;
                f.sky = pic.sky.Prepare(pic.renderer, *sky, look, sources);
            }
            game_x_3d::FollowSky(look, frameLights_, f.ambient);
        }
        f.lights = frameLights_;
        // この絵の時間: 前に描いたときからの秒数 (Play 中はゲームの時間の速さを掛ける)。他のプラグインの絵と
        // 骨の姿勢は、その分だけ進む。
        const float dt = (pic.drawnAt ? static_cast<float>(std::clamp(now_ - *pic.drawnAt, 0.0, 0.1)) : 0.f) * (host.playing ? host.timeScale : 1.f);
        pic.drawnAt = now_;
        const char* pictureName = host.game ? "game" : &pic == &main_ ? "scene" : "preview";
        // (空と一緒の 天候: シーンビュー自身のカメラは、空 がオンのとき見せる)
        if (overhead_) {
            pic.extras.clear();
        } else {
            PrepareExtras(c, host, pic, f, sky && sky->kind != game_x_3d::SkyKind::None ? &skyLook_ : nullptr, !ownCamera || state_.sky, dt, pictureName);
        }
        f.extras = pic.extras;
        // 骨のあるモデルの姿勢 (アニメーター: MeshPose.h。書き出したゲームと同じ ApplyPoses)。
        if (!pic.posersMade) {
            pic.posersMade = true;
            pic.posers = MakePosers(&host.host.Registry());
        }
        const MeshPoseFrame pf{host.scene, dt, host.playing, host.selection, skySeconds_, pictureName, &host.host.Registry()};
        if (ApplyPoses(pic.posers, pf, meshOwners_, meshes_)) c.RequestAnimation();
        f.meshes = meshes_;

        // Direct2D のここまでの描画をまず GPU に送り、そのあと Direct3D が自分の絵に描く。
        dc->Flush();
        ID3D11Texture2D* picture = pic.renderer.Render(host.host.Graphics().D3DContext(), f);
        if (game_production::ViewReportsWanted() && !overhead_ && (host.game || &pic == &main_)) ReportDrawing(host, pic, f);
        if (!picture) return;
        if (picture != pic.texture || dc != pic.dc || !pic.bitmap) {
            pic.bitmap.Reset();
            Microsoft::WRL::ComPtr<IDXGISurface> surface;
            if (FAILED(picture->QueryInterface(IID_PPV_ARGS(&surface)))) return;
            const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpiX, dpiY);
            if (FAILED(dc->CreateBitmapFromDxgiSurface(surface.Get(), &props, &pic.bitmap))) return;
            pic.texture = picture;
            pic.dc = dc;
        }
        c.P().Image(pic.bitmap.Get(), r);
    }

    // デバッグ ▸ 描画とカメラ: 最後の Render が描いたものと、カメラの位置。
    void ReportDrawing(const game_production::SceneViewHost& host, const Picture& pic, const space3d::RenderFrame& f) {
        using editor::Tr;
        using editor::TrF;
        const space3d::Renderer::Stats& st = pic.renderer.LastStats();
        game_production::ViewReport r;
        r.title = host.game ? Tr(L"ゲームビュー (3D)") : Tr(L"シーンビュー (3D)");
        r.lines.push_back({Tr(L"描いた物"), TrF(L"{} 個 / {} 個 (カメラの外で省いた物 {} 個)", st.drawn, st.things, st.things - st.drawn)});
        r.lines.push_back({Tr(L"ドローコール"), TrF(L"{} 回", st.drawCalls)});
        r.lines.push_back({Tr(L"三角形"), TrF(L"{} 個", st.triangles)});
        r.lines.push_back({Tr(L"絵 (テクスチャ)"), TrF(L"{} 枚 ・ GPU のメモリ {:.1f} MB", st.textures, static_cast<double>(st.textureBytes) / (1024.0 * 1024.0))});
        if (st.streamed > 0) r.lines.push_back({Tr(L"ストリーミング"), TrF(L"{} 枚 (細かい段を外している {} 枚)", st.streamed, st.streamedCut)});
        r.lines.push_back({Tr(L"影のマップ"), TrF(L"描いた {} 枚 ・ そのまま使った {} 枚", st.mapsDrawn, st.mapsKept)});
        r.lines.push_back({Tr(L"光"), TrF(L"{} 個", f.lights.size())});
        r.lines.push_back({Tr(L"大きさ"), TrF(L"{} x {} ピクセル", f.width, f.height)});
        if (const auto toWorld = space3d::Inverse(f.view)) {
            const Vec3 eye{toWorld->m[3][0], toWorld->m[3][1], toWorld->m[3][2]};
            const Vec3 ahead = space3d::Normalize(Vec3{-toWorld->m[2][0], -toWorld->m[2][1], -toWorld->m[2][2]});
            r.lines.push_back({Tr(L"カメラの位置"), std::format(L"({:.2f}, {:.2f}, {:.2f})", eye.x, eye.y, eye.z)});
            r.lines.push_back({Tr(L"カメラの向き"), std::format(L"({:.2f}, {:.2f}, {:.2f})", ahead.x, ahead.y, ahead.z)});
        }
        game_production::ReportView(&pic, std::move(r));
    }

    // 他のプラグインがこの絵に描くもの (パーティクル): オンのものの位置を渡して、1 フレームに
    // 1 度尋ねる。Play していないときは、描き手は選んだものが動くのを見せる。その動きがある
    // 間、ビューは描き直される。
    void PrepareExtras(ui::Context& c, game_production::SceneViewHost& host, Picture& pic, space3d::RenderFrame& frame, const SkyLook* sky,
                       bool atmosphere, float dt, const char* picture) {
        pic.extras.clear();
        if (!pic.drawersMade) {
            pic.drawersMade = true;
            for (const auto& d : host.host.Registry().Contributions<WorldDrawInfo>(kWorldDrawPoint)) {
                if (d.item->create) pic.drawers.push_back(d.item->create());
            }
        }
        if (pic.drawers.empty()) return;
        WorldDrawFrame wf{host.scene, host.host.Graphics(),
                          [this](int id) -> const Mat4* {
                              const Placed* p = FindPlaced(id);
                              return p && p->active ? &p->world : nullptr;
                          },
                          dt, host.playing, host.selection,
                          // (エディターが持つとおり: パーティクル タブの編集がすぐ出る)
                          [&host](const AssetRef& ref) { return host.host.AssetContent(ref); },
                          [&host](const AssetRef& ref) { return PathToUtf8(host.host.AssetFile(ref).filename().wstring()); },
                          &host.host.Registry(),
                          picture};
        wf.seconds = skySeconds_;
        wf.sky = sky;
        wf.atmosphere = atmosphere;
        wf.model = [this](int id) -> const space3d::GpuMesh* {
            const Placed* p = FindPlaced(id);
            return p && p->active ? p->mesh : nullptr;
        };
        wf.shape = [&pic](space3d::Shape s) { return pic.renderer.ShapeOf(s); };
        // それらがフレームに足すもの (天候: 向きの地図、濡れ)。そのあと、それらが描くもの。
        for (const auto& d : pic.drawers) {
            FrameAdditions add;
            d->PrepareFrame(wf, add);
            if (frame.mapDirections.empty() && !add.mapDirections.empty()) {
                frame.mapDirections = add.mapDirections;
                frame.mapShapes = add.mapShapes;
                frame.mapMovingOnly = add.mapMovingOnly;
            }
            if (!frame.surface) frame.surface = add.surface;
            if (frame.solids.empty()) frame.solids = add.solids;
            if (!(frame.fog.a > 0)) frame.fog = add.fog;
        }
        bool moving = false;
        for (const auto& d : pic.drawers) {
            d->Prepare(wf, pic.extras);
            moving = moving || d->Animating();
        }
        if (moving) c.RequestAnimation();
    }

    // --- ゲームビュー ---------------------------------------------------------------------
    // ゲーム設定の 最初のカメラ 越しに、ゲーム画面の形で描く。できないときは足りないもの
    // (と、設定へ行く道) を出す。
    void DrawGame(ui::Context& c, const Rect& r, game_production::SceneViewHost& host) {
        std::wstring problem;
        const Placed* cam = nullptr;
        const game_production::SceneEntity* e = host.camera != 0 ? host.scene.Find(static_cast<int>(host.camera)) : nullptr;
        if (e) cam = FindPlaced(e->data.id);
        if (host.camera == 0) {
            problem = Tr(L"ゲームの設定の「最初のカメラ」が選ばれていません。カメラの部品を付けたエンティティを選ぶと、そこから見た絵がここに映ります。");
        } else if (!e) {
            problem = Tr(L"「最初のカメラ」のエンティティがシーンにありません (消されたかもしれません)。ゲームの設定で選び直してください。");
        } else if (!cam || !cam->camera) {
            problem = TrF(L"「{}」にカメラの部品がありません (Transform も要ります)。インスペクターの「部品を足す」でカメラを付けてください。", Utf8ToWide(e->data.name));
        } else if (!cam->active) {
            problem = TrF(L"カメラの「{}」が有効ではありません。有効にすると、そこから見た絵が映ります。", Utf8ToWide(e->data.name));
        }
        if (!problem.empty()) {
            const float cy = r.y + r.h * 0.5f;
            ui::TextStyle ts;
            ts.size = 13;
            ts.wrap = true;
            ts.color = theme::TextDim;
            ts.h = ui::HAlign::Center;
            ts.v = ui::VAlign::Bottom;
            c.P().Text({r.x + 24, r.y + 8, r.w - 48, cy - r.y}, problem, ts);
            const Rect button{r.x + (r.w - 200) * 0.5f, cy + 16, 200, 36};
            if (host.openSettings && w::Button(c, "game_camera_settings", button, Tr(L"ゲームの設定を開く"))) host.openSettings();
            return;
        }
        Build(host, true);
        Show(c, Fit(r, host.aspect), host, main_, CameraFrame(*cam, host));
    }

    // --- 真上から見た地図 (SceneViewHost::overhead) -----------------------------------------
    // 正射影でまっすぐ見下ろす。画面の右が +X、上が +Z (LookAt の上の向きを +Z に)。高さの範囲は
    // 置いた物の上から下まで (深度と影の地図をむだに広げない)。
    void DrawOverhead(ui::Context& c, const Rect& r, game_production::SceneViewHost& host) {
        const game_production::SceneViewHost::Overhead& o = *host.overhead;
        const float halfW = std::isfinite(o.halfWidth) ? std::max(o.halfWidth, 0.01f) : 50.f;
        const float halfH = halfW * r.h / std::max(r.w, 1.f);
        float top = 1.f, bottom = -1.f;
        for (const Placed& p : placed_) {
            if (!p.active || p.marker) continue;
            const space3d::Bounds b = space3d::WorldBounds(p.world, p.min, p.max);
            if (!std::isfinite(b.min.y) || !std::isfinite(b.max.y)) continue;
            top = std::max(top, b.max.y);
            bottom = std::min(bottom, b.min.y);
        }
        const float eyeY = top + 10.f;
        const Vec3 eye{o.center.x, eyeY, o.center.y};
        Build(host, true);
        space3d::RenderFrame f;
        f.view = space3d::LookAt(eye, {eye.x, eye.y - 1.f, eye.z}, {0, 0, 1});
        f.projection = space3d::Orthographic(-halfW, halfW, -halfH, halfH, 0.5f, eyeY - bottom + 10.f);
        f.clear = theme::SceneBackground();
        f.shadows = true;
        f.shadowColor = theme::SceneShadow();
        overhead_ = true;
        Show(c, r, host, main_, f);
        overhead_ = false;
    }

    // --- シーンビューのカメラとライト ---------------------------------------------------
    // カメラの形: 見えるもの (近い面と遠い面 - 遠い面は kFrustumShown までしか離さない - と、
    // その間の辺) と、上にある小さな屋根 (どちらが上か)。
    void CameraLines(const Placed& p, bool selected, float aspect) {
        const ui::Color col = selected ? theme::Accent : p.active ? theme::SceneCamera() : theme::SceneCamera().Alpha(0.3f);
        const Camera shown = ShownNear(*p.camera, kFrustumShown);
        const auto k = space3d::FrustumCorners(ProjectionOf(shown, aspect) * space3d::ViewFromWorld(p.world));
        if (!k) return;
        auto line = [&](Vec3 a, Vec3 b) {
            lines_.push_back({a, col});
            lines_.push_back({b, col});
        };
        for (size_t i = 0; i < 4; ++i) {
            line((*k)[i], (*k)[(i + 1) % 4]);
            line((*k)[4 + i], (*k)[4 + (i + 1) % 4]);
            line((*k)[i], (*k)[4 + i]);
        }
        const Vec3 topLeft = (*k)[7], topRight = (*k)[6];
        const Vec3 a = topLeft + (topRight - topLeft) * 0.35f, b = topLeft + (topRight - topLeft) * 0.65f;
        const Vec3 apex = (a + b) * 0.5f + space3d::Normalize(topLeft - (*k)[4]) * (space3d::Length(topRight - topLeft) * 0.15f);
        line(a, apex);
        line(apex, b);
    }

    // ライトの形: 平行光 = 輪と、ライトの向きに進む光線。スポット = 届く所までの円錐。
    // 点光源 = 届く距離 (選んでいる間は輪が 3 つ)。
    void LightLines(const Placed& p, bool selected) {
        const ui::Color col = selected ? theme::Accent : p.active ? theme::SceneLight() : theme::SceneLight().Alpha(0.35f);
        const space3d::Light l = ToRenderLight(*p.light, p.world);
        const Vec3 d = space3d::Length(l.direction) > 0 ? l.direction : Vec3{0, -1, 0};
        const auto [u, v] = Across(d);
        auto line = [&](Vec3 a, Vec3 b) {
            lines_.push_back({a, col});
            lines_.push_back({b, col});
        };
        auto ring = [&](Vec3 center, Vec3 x, Vec3 y, float radius) {
            constexpr int n = 32;
            for (int i = 0; i < n; ++i) {
                const float a0 = static_cast<float>(i) / n * 6.2831853f, a1 = static_cast<float>(i + 1) / n * 6.2831853f;
                line(center + (x * std::cos(a0) + y * std::sin(a0)) * radius, center + (x * std::cos(a1) + y * std::sin(a1)) * radius);
            }
        };
        switch (p.light->kind) {
            case LightKind::Directional: {
                constexpr float radius = 0.3f, length = 1.5f;
                ring(l.position, u, v, radius);
                for (int i = 0; i < 4; ++i) {
                    const float a = static_cast<float>(i) * 1.5707963f;
                    const Vec3 at = l.position + (u * std::cos(a) + v * std::sin(a)) * radius;
                    line(at, at + d * length);
                }
                line(l.position, l.position + d * (length + 0.5f));
                break;
            }
            case LightKind::Spot: {
                const float half = std::clamp(p.light->spotAngle, 1.f, 179.f) * 0.5f * 0.01745329f;
                const float reach = std::max(p.light->range, 0.01f);
                const Vec3 end = l.position + d * (reach * std::cos(half));
                const float radius = reach * std::sin(half);
                ring(end, u, v, radius);
                for (int i = 0; i < 4; ++i) {
                    const float a = static_cast<float>(i) * 1.5707963f;
                    line(l.position, end + (u * std::cos(a) + v * std::sin(a)) * radius);
                }
                break;
            }
            case LightKind::Point:
                if (selected) {
                    const float reach = std::max(p.light->range, 0.01f);
                    ring(l.position, {1, 0, 0}, {0, 1, 0}, reach);
                    ring(l.position, {0, 1, 0}, {0, 0, 1}, reach);
                    ring(l.position, {0, 0, 1}, {1, 0, 0}, reach);
                }
                break;
        }
    }

    // メッシュ の無いものがある所の印 - カメラ、ライト、空のエンティティ、パーティクル ...
    // (クリックで選べる。ギズモで動かせる)。
    void DrawMarks(ui::Context& c, const Rect& r, const game_production::SceneViewHost& host) {
        const auto screen = gizmo::MakeScreen(ViewProjection(r), r.x, r.y, r.w, r.h);
        if (!screen) return;
        c.P().PushClip(r);
        // まだ読み込み中のモデル: その箱と、上に 読み込み中。
        ui::TextStyle loadingStyle;
        loadingStyle.size = 11;
        loadingStyle.color = theme::TextDim;
        loadingStyle.h = ui::HAlign::Center;
        loadingStyle.v = ui::VAlign::Bottom;
        for (const Placed& p : placed_) {
            if (!p.loading) continue;
            const auto at = screen->ToScreen(space3d::TransformPoint(p.world, {0, p.max.y, 0}));
            if (at) c.P().Text({at->x - 80, at->y - 22, 160, 18}, Tr(L"読み込み中…"), loadingStyle);
        }
        for (const Placed& p : placed_) {
            if (!p.HasMark()) continue;
            const auto at = screen->ToScreen(space3d::TransformPoint(p.world, {0, 0, 0}));
            if (!at) continue;
            const ui::Color col = IsSelected(host, p.id)      ? theme::Accent
                                  : !p.active                 ? theme::TextFaint
                                  : p.mark == w::Icon::Light  ? theme::SceneLight()
                                  : p.mark == w::Icon::Camera ? theme::SceneCamera()
                                                              : theme::SceneMark();
            w::DrawIcon(c, {at->x - kMarkSize * 0.5f, at->y - kMarkSize * 0.5f, kMarkSize, kMarkSize}, p.mark, col);
        }
        c.P().PopClip();
    }

    // 選んだカメラの絵を、右下に小さく (ゲーム画面の形で)。
    std::optional<Rect> PreviewRect(const Rect& r, const game_production::SceneViewHost& host) const {
        const Placed* p = Primary(host);
        if (!p || !p->camera) return std::nullopt;
        const float aspect = std::max(host.aspect, 1e-3f);
        float w = std::clamp(r.w * 0.3f, 140.f, kPreviewMaxW), h = w / aspect;
        if (h > r.h * 0.4f) {
            h = r.h * 0.4f;
            w = h * aspect;
        }
        if (w < 60 || h < 34 || r.w < w + 40) return std::nullopt;
        return Rect{r.Right() - w - 12, r.Bottom() - h - 12, w, h};
    }

    void DrawPreview(ui::Context& c, const Rect& at, game_production::SceneViewHost& host) {
        const Placed* p = Primary(host);
        const game_production::SceneEntity* e = p ? host.scene.Find(p->id) : nullptr;
        if (!p || !p->camera || !e) return;
        w::DrawSurface(c, at.Inset(-4, -24, -4, -4), w::Surface::Popup);
        ui::TextStyle ts;
        ts.size = 11;
        ts.color = theme::TextDim;
        ts.v = ui::VAlign::Center;
        c.P().Text({at.x + 2, at.y - 22, at.w - 4, 20}, TrF(L"カメラのプレビュー: {}", Utf8ToWide(e->data.name)), ts);
        if (!Start(preview_, host)) return;
        Build(host, true);
        Show(c, at, host, preview_, CameraFrame(*p, host));
    }

    // その物自身の空間にある箱 (単位立方体か、モデルの境界) の 12 本の辺を、置いて描く。
    void Outline(const Mat4& w, Vec3 mn, Vec3 mx, ui::Color col) {
        Vec3 k[8];
        for (int i = 0; i < 8; ++i) {
            k[i] = space3d::TransformPoint(w, {(i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z});
        }
        const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (const auto& e : edges) {
            lines_.push_back({k[e[0]], col});
            lines_.push_back({k[e[1]], col});
        }
    }

    // 絵の上のギズモ。常に手前 (gizmo:: の形から画面に描く: 描いたものがつかめるもの)。
    void DrawGizmo(ui::Context& c, const Rect& r, const game_production::SceneViewHost& host) {
        if (host.playing) return;  // Play: 編集しない
        const auto frame = GizmoFrame(r, host);
        const auto screen = gizmo::MakeScreen(ViewProjection(r), r.x, r.y, r.w, r.h);
        if (!frame || !screen) return;
        const gizmo::Frame& f = *frame;
        const gizmo::Screen& s = *screen;
        const auto o = s.ToScreen(f.origin);
        if (!o) return;
        const gizmo::Handle active = drag_ == Drag::Gizmo && gizmo_ ? gizmo_->handle : hot_;
        const gizmo::Tool tool = drag_ == Drag::Gizmo ? dragTool_ : state_.tool;
        auto& p = c.P();
        auto axisColor = [&](int a) { return a == 0 ? theme::AxisX() : a == 1 ? theme::AxisY() : theme::AxisZ(); };
        auto handleOf = [](int a) { return static_cast<gizmo::Handle>(static_cast<int>(gizmo::Handle::X) + a); };
        auto colorOf = [&](gizmo::Handle h, int a) { return h == active ? theme::GizmoHot() : axisColor(a); };
        auto square = [&](Vec2 m, float half, ui::Color col) {
            ui::Path sq;
            sq.Begin({m.x - half, m.y - half});
            sq.Line({m.x + half, m.y - half});
            sq.Line({m.x + half, m.y + half});
            sq.Line({m.x - half, m.y + half});
            p.FillPath(sq, col);
        };

        auto polyline = [&](const std::vector<Vec3>& pts, ui::Color col, float width) {
            for (size_t i = 0; i + 1 < pts.size(); ++i) {
                const auto a0 = s.ToScreen(pts[i]), a1 = s.ToScreen(pts[i + 1]);
                if (a0 && a1) p.Line(a0->x, a0->y, a1->x, a1->y, col, width);
            }
        };
        const ui::Color viewColor = theme::Text.Alpha(0.85f);
        const Vec3 eye = Cam().EyeFor(f.origin);  // (2D: 真正面から - 輪はどれも同じ向きで見える)

        p.PushClip(r);
        if (tool == gizmo::Tool::Rotate) {
            // 球の輪郭 (その中のドラッグは自由に回す: 回せるときは薄く塗る)。
            const std::vector<Vec3> ball = gizmo::FacingCircle(f, eye, 1.f);
            if (active == gizmo::Handle::Free) {
                ui::Path disc;
                for (size_t i = 0; i < ball.size(); ++i) {
                    const auto q = s.ToScreen(ball[i]);
                    if (!q) continue;
                    if (disc.steps.empty()) {
                        disc.Begin(*q);
                    } else {
                        disc.Line(*q);
                    }
                }
                p.FillPath(disc, theme::GizmoHot().Alpha(0.16f));
            }
            polyline(ball, viewColor.Alpha(active == gizmo::Handle::Free ? 0.6f : 0.25f), 1.f);
            // 後ろの半分を先に薄く (どの輪も 1 つの円に見える)、そのあと手前を描く。
            for (const bool front : {false, true}) {
                for (int a = 0; a < 3; ++a) {
                    const bool hot = handleOf(a) == active;
                    const ui::Color col = colorOf(handleOf(a), a).Alpha(front ? 1.f : hot ? 0.6f : 0.3f);
                    const float width = front ? (hot ? 3.f : 2.f) : 1.5f;
                    for (const auto& arc : gizmo::RingArcs(f, a, eye, front)) polyline(arc, col, width);
                }
            }
            // 目の方を向く外側の輪: 視線を軸に回す。
            const bool viewHot = active == gizmo::Handle::View;
            polyline(gizmo::FacingCircle(f, eye, gizmo::kViewRing), viewHot ? theme::GizmoHot() : viewColor,
                     viewHot ? 3.f : 2.f);
        } else {
            if (tool == gizmo::Tool::Move) {
                for (gizmo::Handle plane : {gizmo::Handle::YZ, gizmo::Handle::ZX, gizmo::Handle::XY}) {
                    std::array<Vec2, 4> q;
                    if (!gizmo::PlaneShown(f, s, plane, &q)) continue;
                    const ui::Color col = colorOf(plane, gizmo::AxisOf(plane));
                    ui::Path quad;
                    quad.Begin(q[0]);
                    for (size_t i = 1; i < 4; ++i) quad.Line(q[i]);
                    p.FillPath(quad, col.Alpha(plane == active ? 0.55f : 0.28f));
                    for (size_t i = 0; i < 4; ++i) p.Line(q[i].x, q[i].y, q[(i + 1) % 4].x, q[(i + 1) % 4].y, col.Alpha(0.9f), 1.f);
                }
            }
            for (int a = 0; a < 3; ++a) {
                if (!gizmo::AxisShown(f, s, a)) continue;
                const ui::Color col = colorOf(handleOf(a), a);
                const auto tip = s.ToScreen(gizmo::AxisEnd(f, a));
                if (!tip) continue;
                if (tool == gizmo::Tool::Move) {
                    // 軸の棒と矢じり (画面上の三角形。0.8 から端まで)。
                    const auto base = s.ToScreen(f.origin + f.axes[a] * (f.size * gizmo::kHeadStart));
                    if (!base) continue;
                    p.Line(o->x, o->y, base->x, base->y, col, 2.f);
                    const float dxh = tip->x - base->x, dyh = tip->y - base->y;
                    const float len = std::max(std::hypot(dxh, dyh), 1e-3f);
                    const Vec2 across{-dyh / len * 5.5f, dxh / len * 5.5f};
                    ui::Path head;
                    head.Begin(*tip);
                    head.Line({base->x + across.x, base->y + across.y});
                    head.Line({base->x - across.x, base->y - across.y});
                    p.FillPath(head, col);
                } else {
                    p.Line(o->x, o->y, tip->x, tip->y, col, 2.f);
                    square(*tip, 4.5f, col);
                }
            }
            if (tool == gizmo::Tool::Scale) square(*o, 6.f, active == gizmo::Handle::Uniform ? theme::GizmoHot() : theme::Text);
        }
        p.PopClip();
    }

    // 左の縦の列 (グリッド配置・選択・移動・回転・拡縮)。絵の左上に浮かぶ。
    void DrawBar(ui::Context& c, const BarLayout& bar) {
        // グリッド配置: 押すと切り替え (オンの間はギズモが常にグリッドの幅で動く)。右クリックで幅の箱。
        const auto grid = w::ToolSquare(c, "tool_grid", bar.grid, w::Icon::Grid, state_.snapAlways, Tr(L"グリッド配置 (右クリックで幅)"));
        if (grid.clicked) state_.snapAlways = !state_.snapAlways;
        if (grid.rightClicked) snapOpen_ = !snapOpen_;
        if (w::ToolSquare(c, "tool_select", bar.select, w::Icon::Cursor, state_.selecting, Tr(L"選択 (Q)")).clicked) state_.selecting = true;
        if (w::ToolSquare(c, "tool_move", bar.move, w::Icon::Move, !state_.selecting && state_.tool == gizmo::Tool::Move, Tr(L"移動 (W)")).clicked) {
            PickTool(gizmo::Tool::Move);
        }
        if (w::ToolSquare(c, "tool_rotate", bar.rotate, w::Icon::Rotate, !state_.selecting && state_.tool == gizmo::Tool::Rotate, Tr(L"回転 (E)")).clicked) {
            PickTool(gizmo::Tool::Rotate);
        }
        if (w::ToolSquare(c, "tool_scale", bar.scale, w::Icon::Scale, !state_.selecting && state_.tool == gizmo::Tool::Scale, Tr(L"拡縮 (R)")).clicked) {
            PickTool(gizmo::Tool::Scale);
        }
    }

    // 絵の上の帯 (`r` の上から取る。ビューが狭いときは 2 行目、3 行目に回る): 3D / 2D (同じシーンを
    // 遠近法か、正面から平行投影で)、ローカル / ワールド、それぞれ / 中心、影、空、デバッグ、形。
    // それぞれの幅は文字に合わせる (英語でも切れない)。
    void DrawStrip(ui::Context& c, Rect& r) {
        const std::wstring d3 = Tr(L"3D"), d2 = Tr(L"2D");
        const std::wstring d3Tip = Tr(L"遠近法で見る (奥ほど小さい)。右ドラッグで見回し、Alt + 左ドラッグで回る");
        const std::wstring d2Tip = Tr(L"同じシーンを正面 (Z の向き) から平行投影で見る (奥へ行っても狭まらない)。ドラッグでずらし、ホイールで大きさを変える");
        const std::array<std::wstring_view, 2> views{d3, d2};
        const std::array<std::wstring_view, 2> viewTips{d3Tip, d2Tip};
        const std::wstring local = Tr(L"ローカル"), world = Tr(L"ワールド");
        const std::wstring localTip = Tr(L"選んだ物の向きに合わせる (X で切り替え)"), worldTip = Tr(L"世界の向き (X で切り替え)。大きさは常に物の向き");
        const std::array<std::wstring_view, 2> spaces{local, world};
        const std::array<std::wstring_view, 2> spaceTips{localTip, worldTip};
        const std::wstring each = Tr(L"それぞれ"), center = Tr(L"中心");
        const std::wstring eachTip = Tr(L"選んだ物をそれぞれの場所で回す・大きさを変える。ギズモは最後に選んだ物に (Z で切り替え)");
        const std::wstring centerTip = Tr(L"選んだ物全部の真ん中にギズモを出し、そこを中心に回す・大きさを変える (Z で切り替え)");
        const std::array<std::wstring_view, 2> pivots{each, center};
        const std::array<std::wstring_view, 2> pivotTips{eachTip, centerTip};
        const std::wstring shadow = Tr(L"影"), sky = Tr(L"空"), debug = Tr(L"デバッグ");
        const std::wstring shapesSel = Tr(L"形: 選択"), shapesAll = Tr(L"形: 全部");
        // 並べる: 幅を測り、左から右へ、入らなければ次の行。3D / 2D の後ろは少し広く空ける。
        const float widths[] = {w::SegmentedWidth(c, views),
                                w::SegmentedWidth(c, spaces),
                                w::SegmentedWidth(c, pivots),
                                w::ButtonWidth(c, shadow),
                                w::ButtonWidth(c, sky),
                                w::ButtonWidth(c, debug),
                                std::max(w::ButtonWidth(c, shapesSel), w::ButtonWidth(c, shapesAll))};
        constexpr size_t kItems = std::size(widths);
        std::array<Rect, kItems> at{};
        const float left = r.x + 8, pad = (kStripH - kStripItemH) * 0.5f;
        float x = left, y = 0;
        for (size_t i = 0; i < kItems; ++i) {
            if (x > left && x + widths[i] > r.Right() - 8) {
                x = left;
                y += kStripH;
            }
            at[i] = {x, y + pad, widths[i], kStripItemH};
            x += widths[i] + (i == 0 ? 2 * kStripGap : kStripGap);
        }
        const Rect strip = ui::TakeTop(r, y + kStripH);
        w::DrawBar(c, strip, theme::Bg);
        for (Rect& a : at) a.y += strip.y;
        if (const int i = w::Segmented(c, "scene_view_mode", at[0], views, state_.flat ? 1 : 0, viewTips); i >= 0 && (i == 1) != state_.flat) {
            state_.flat = i == 1;
            drag_ = Drag::None;  // (始めていたドラッグは、前のカメラのもの)
            log::Info("シーンビュー: {} で見ます", state_.flat ? "2D (正面からの平行投影)" : "3D (遠近法)");
        }
        if (const int i = w::Segmented(c, "gizmo_space", at[1], spaces, static_cast<int>(state_.space), spaceTips); i >= 0) {
            state_.space = static_cast<gizmo::Space>(i);
        }
        if (const int i = w::Segmented(c, "gizmo_pivot", at[2], pivots, static_cast<int>(state_.pivot), pivotTips); i >= 0) {
            state_.pivot = static_cast<gizmo::Pivot>(i);
        }
        // 影: ライトが床と物に落とす影 (オンの間は強調色)。
        if (w::Button(c, "scene_shadows", at[3], shadow, state_.shadows ? w::ButtonKind::Primary : w::ButtonKind::Secondary)) {
            state_.shadows = !state_.shadows;
        }
        if (c.IsHovered(at[3])) w::Tooltip(c, Tr(L"光の影を床と物に落とす (光の部品の「影を落とす」に従う。光の部品が無いときは上からの光。押すと切り替え)"));
        // 空: シーンの空をこのビューの後ろにも出す (ゲームビューは常に出す)。
        if (w::Button(c, "scene_sky", at[4], sky, state_.sky ? w::ButtonKind::Primary : w::ButtonKind::Secondary)) {
            state_.sky = !state_.sky;
        }
        if (c.IsHovered(at[4])) w::Tooltip(c, Tr(L"シーンの「空」をこのシーンビューの後ろにも描く (階層の一番上の行を選ぶと設定できます。ゲームビューにはいつも出る。押すと切り替え)"));
        // デバッグ: ゲームのスクリプトが動く間に描く線 (rm::Debug) を、このビューに出す。
        if (w::Button(c, "scene_debug_draw", at[5], debug, state_.debugDraw ? w::ButtonKind::Primary : w::ButtonKind::Secondary)) {
            state_.debugDraw = !state_.debugDraw;
        }
        if (c.IsHovered(at[5])) w::Tooltip(c, Tr(L"Play 中にスクリプトが rm::Debug で描く線を、このシーンビューに出す (押すと切り替え。ゲームビューにはいつも出る)"));
        // 形: プラグインがシーンの上に見せる線 (物理: コライダー) - 選んだものか、すべてか。
        if (w::Button(c, "scene_shapes", at[6], state_.allShapes ? shapesAll : shapesSel, w::ButtonKind::Secondary)) {
            state_.allShapes = !state_.allShapes;
        }
        if (c.IsHovered(at[6])) w::Tooltip(c, Tr(L"コライダー (物理の当たりの形) などの線を、選んだ物だけに出すか、全部の物に出すか (押すと切り替え)"));
    }

    // ギズモの刻み: 1 回に移動・回転・拡大縮小がどれだけ進むか。それと、常にそうかどうか。
    // ツールと同じビューの状態 (UserSettings に残す。プロジェクトの編集ではない)。
    void DrawSnapBox(ui::Context& c, const Rect& box) {
        w::DrawSurface(c, box, w::Surface::Popup);
        ui::TextStyle label;
        label.size = theme::FontSize;
        label.color = theme::TextDim;
        label.v = ui::VAlign::Center;
        struct Row {
            const char* id;
            std::wstring name;
            float* value;
            float min, max;
        };
        Row rows[] = {{"snap_move", Tr(L"移動"), &state_.snapMove, 0.001f, 1000.f},
                      {"snap_degrees", Tr(L"回転 (度)"), &state_.snapDegrees, 0.1f, 180.f},
                      {"snap_scale", Tr(L"大きさ"), &state_.snapScale, 0.001f, 10.f}};
        float y = box.y + 10;
        for (size_t i = 0; i < std::size(rows); ++i, y += kSnapRowH) {
            Row& row = rows[i];
            c.P().Text({box.x + 12, y, 120, kSnapRowH - 6}, row.name, label);
            // 打っている間は打ったままの文字。それ以外は値。
            std::string& text = snapText_[i];
            if (c.Focused() != c.MakeId(row.id)) text = std::format("{:g}", *row.value);
            if (w::TextField(c, row.id, {box.Right() - 12 - 110, y, 110, kSnapRowH - 6}, text, {}, true, true)) {
                float v = 0;
                const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
                if (ec == std::errc{} && end == text.data() + text.size() && v > 0) *row.value = std::clamp(v, row.min, row.max);
            }
        }
        w::Checkbox(c, "snap_always", {box.x + 12, y + 2, box.w - 24, 24}, state_.snapAlways, Tr(L"グリッド配置"));
    }

    enum class Drag { None, Look, Pan, Orbit, Click, Gizmo, Cancelled };

    SceneViewState state_;       // 選んだカメラとギズモ (UserSettings に残す)
    reflect::Unknowns unknowns_;  // 新しい版のキー: そのまま書き戻す
    uint64_t generation_ = 0;  // 以下のすべてを作った GPU のデバイス (platform::Gfx::Generation)
    Picture main_;     // ビュー
    Picture preview_;  // 選んだカメラの絵 (最初に必要になったときに始める)
    std::optional<Rect> previewAt_;
    std::optional<std::vector<const SceneOverlayInfo*>> overlayInfos_;           // 寄せられた操作 (最初に集める)
    std::vector<std::pair<const SceneOverlayInfo*, Rect>> overlays_;             // ... このフレームに出すものと場所
    std::string transformType_, meshType_, cameraType_, lightType_, ambientType_;
    std::unordered_map<std::string, w::Icon> markOf_;  // 部品の型 -> その印 (MakeLooks)
    std::vector<space3d::Light> frameLights_;          // 絵が描くとおりの lights_ (空の光)
    double skySeconds_ = 0;       // 空の時間 (SkyLook): Play 中はゲームのもの
    game_x_3d::SkyLook skyLook_;  // 描いている絵のもの (天候の粒はこれで照らされる)
    bool looksMade_ = false;
    bool overhead_ = false;  // 真上から見た地図を描いている最中 (Show が空と天候を描かない)
    std::optional<space3d::Ambient> ambient_;  // シーンの 環境光 (無ければ既定)
    size_t warnedShadows_ = 0;  // 影のマップが残っていないと知らせたライト
    std::vector<Placed> placed_;
    std::vector<space3d::LineVertex> lines_;
    std::vector<space3d::LineVertex> debugLines_;  // Play: スクリプトの rm::Debug の線 (3D)。それとプラグインのシーンの線
    game_production::DebugDraw sceneLines_;          // このフレームのプラグインのシーンの線 (SceneLinesInfo)
    std::vector<space3d::Box> boxes_;
    std::vector<space3d::MeshDraw> meshes_;
    std::vector<PoseOwner> meshOwners_;  // meshes_ と同じ並び: 持ち主のエンティティと、骨のあるモデル
    std::vector<space3d::Light> lights_;
    size_t warnedLights_ = 0;  // 知らせたライトの数 (多すぎる)
    std::unordered_map<std::string, ModelEntry> models_;  // MeshRenderer::model (アセットの id) ごと
    space3d::MeshLoader loader_;                          // モデルファイルを自分のスレッドで読む
    space3d::TextureCache textures_;                      // モデルとマテリアルの画像ファイル。フルパスごと
    std::unordered_map<std::string, MaterialEntry> materials_;  // マテリアルの欄 (アセットの id) ごと
    std::vector<const space3d::DrawMaterial*> slots_;           // Place: 今のエンティティのマテリアルの欄
    std::deque<std::vector<space3d::DrawMaterial>> stores_;     // Store (deque: 足しても前のものは動かない)
    size_t usedStores_ = 0;
    double now_ = 0;
    struct Motion {
        Mat4 world;
        double movedAt;  // 位置が最後に変わった時刻 (c.Time())
    };
    std::unordered_map<int, Motion> motion_;  // エンティティごと

    Drag drag_ = Drag::None;
    int dragButton_ = 0;  // ドラッグを続けるボタン (ui::Input::down の番号: 0 左、1 右、2 中)
    float pressX_ = 0, pressY_ = 0;
    double lastTime_ = 0;

    gizmo::Handle hot_ = gizmo::Handle::None;  // マウスの下
    std::optional<gizmo::Drag> gizmo_;          // 今のギズモのドラッグ
    gizmo::Tool dragTool_ = gizmo::Tool::Move;  // 始めたときの状態 (キーで変わることがある)
    gizmo::Space dragSpace_ = gizmo::Space::Local;
    std::optional<Vec3> pivot_;                 // 中心: ドラッグが回す / 拡大縮小する軸
    std::vector<Edited> edited_;
    std::pair<size_t, size_t> history_{0, 0};  // ドラッグを始めたときの履歴の Undo / Redo の数
    bool snapOpen_ = false;                     // 刻みの箱が開いている
    std::array<std::string, 3> snapText_;       // その欄の文字
};

}  // namespace

std::unique_ptr<game_production::SceneView> CreateSceneView3D() { return std::make_unique<SceneView3D>(); }

}  // namespace rm::game_x_3d
