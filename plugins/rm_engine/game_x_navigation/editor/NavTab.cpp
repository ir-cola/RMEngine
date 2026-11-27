#include "rm_engine/game_x_navigation/editor/NavTab.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "base/Version.h"
#include "core/reflect/Data.h"
#include "core/reflect/FieldCommand.h"
#include "core/reflect/Object.h"
#include "editor/app/Worker.h"
#include "editor/inspector/Inspector.h"
#include "editor/lang/Lang.h"
#include "editor/project/Project.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "rm_engine/game_production/editor/GameExport.h"
#include "rm_engine/game_x_navigation/editor/NavBake.h"
#include "rm_engine/game_x_navigation/editor/NavLines.h"
#include "rm_engine/game_x_navigation/editor/NavPreview.h"
#include "rm_engine/game_x_navigation/runtime/NavScene.h"
#include "rm_engine/game_x_navigation/runtime/NavWorld.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <format>
#include <limits>
#include <map>
#include <system_error>

namespace rm::game_x_navigation {

namespace {

namespace w = editor::w;
namespace theme = editor::theme;
namespace inspector = editor::inspector;
using editor::Tr;
using editor::TrF;
using editor::TrU8;
using game_production::Scene;
using game_production::SceneViewHost;
using ui::Rect;

constexpr float kPad = 8, kRow = 26, kBarH = 28, kSideW = 320;
constexpr float kMinHalfWidth = 2.f, kMaxHalfWidth = 200000.f;
constexpr float kToolSquare = 37.f, kToolGap = 4.f;
constexpr int kTools = 2;

void Note(ui::Context& c, const Rect& r, const std::wstring& text) {
    ui::TextStyle ts;
    ts.wrap = true;
    ts.color = theme::TextDim;
    ts.v = ui::VAlign::Top;
    c.P().Text(r, text, ts);
}

void Label(ui::Context& c, const Rect& r, const std::wstring& text, bool bold = false, Color color = theme::Text) {
    ui::TextStyle ts;
    ts.color = color;
    ts.bold = bold;
    c.P().Text(r, text, ts);
}

// 値 (`type` の物。`resolve` が今ある場所) を `change` で変え、変わったフィールドごとに reflect::SetFieldCommand にして、
// 履歴に Undo の 1 件として積む (編集 = Undo = 未保存: 憲法 7)。変わらなかったら false。
bool EditValue(editor::SoftwareHost& host, const reflect::TypeDesc& type, const std::function<reflect::EditTarget()>& resolve, const std::string& label,
               uint64_t merge, const std::function<void(void*)>& change) {
    const reflect::EditTarget t = resolve ? resolve() : reflect::EditTarget{};
    if (!t.object) return false;
    reflect::Object next(type);
    type.copy(next.Get(), t.object);
    change(next.Get());
    const uint64_t diff = reflect::Diff(type, t.object, next.Get());
    if (diff == 0) return false;
    std::vector<std::unique_ptr<ICommand>> cmds;
    for (size_t i = 0; i < type.fields.size() && i < 64; ++i) {
        if (!(diff & (uint64_t{1} << i))) continue;
        const reflect::FieldDesc& f = type.fields[i];
        cmds.push_back(std::make_unique<reflect::SetFieldCommand>(type, reflect::FieldPath{i}, reflect::SaveField(f, t.object, t.unknowns),
                                                                  reflect::SaveField(f, next.Get(), t.unknowns), resolve, label, merge));
    }
    if (cmds.size() == 1) {
        host.History().Push(std::move(cmds.front()), false);
        return true;
    }
    auto group = std::make_unique<CommandGroup>(label, merge);
    for (auto& cmd : cmds) group->Add(std::move(cmd));
    host.History().Push(std::move(group), false);
    return true;
}

// 地図の上の場所: 画面 (DIP) とワールドの (x, z)。画面の右が +X、上が +Z (SceneViewHost::Overhead)。
struct MapView {
    Rect r;
    Vec2 center;
    float scale = 1;  // 1 m あたりの DIP
    float CX() const { return r.x + r.w * 0.5f; }
    float CY() const { return r.y + r.h * 0.5f; }
    Vec2 ToScreen(float x, float z) const { return {CX() + (x - center.x) * scale, CY() - (z - center.y) * scale}; }
    Vec2 ToWorld(float sx, float sy) const { return {center.x + (sx - CX()) / scale, center.y - (sy - CY()) / scale}; }
};

enum class Status { None, NotBaked, Missing, Broken, Fresh, Stale };

std::wstring StatusText(Status s) {
    switch (s) {
        case Status::NotBaked: return Tr(L"焼いていない");
        case Status::Missing: return Tr(L"焼いた物のファイルがありません");
        case Status::Broken: return Tr(L"焼いた物が読めません");
        case Status::Fresh: return Tr(L"焼いた");
        case Status::Stale: return Tr(L"⚠ 古い (焼き直してください)");
        default: return {};
    }
}

std::wstring PathText(const navigation::NavPath& p) {
    switch (p.status) {
        case navigation::NavPath::Status::Found: return TrF(L"道の長さ {:.1f} m (角 {} 個)", p.length, p.points.size() > 2 ? p.points.size() - 2 : 0);
        case navigation::NavPath::Status::Partial: return TrF(L"行き先まで行けません: 一番近い所まで {:.1f} m", p.length);
        case navigation::NavPath::Status::StartOff: return Tr(L"始めがナビメッシュの外です");
        case navigation::NavPath::Status::EndOff: return Tr(L"終わりがナビメッシュの外です");
        default: return Tr(L"ナビメッシュがありません (先に焼いてください)");
    }
}

class NavTab final : public game_production::SceneView {
public:
    NavTab() : progress_(std::make_shared<Progress>()) {}

    void Draw(ui::Context& c, const Rect& r, SceneViewHost& host) override {
        if (r.w < 120 || r.h < 120) return;
        ApplyPending(host);
        const Rect bar{r.x + kPad, r.y + kPad, r.w - 2 * kPad, kBarH};
        const Rect body{r.x + kPad, bar.Bottom() + kPad, r.w - 2 * kPad, r.Bottom() - kPad - (bar.Bottom() + kPad)};
        if (host.space == "2d") {
            Note(c, body, Tr(L"2D のナビメッシュは、これから作ります (段階 5)。今は 3D のシーンだけです。"));
            return;
        }
        const NavSettings* settings = host.scene.Setting<NavSettings>();
        if (!settings) {
            Note(c, body, Tr(L"このシーンにはナビの設定がありません。"));
            return;
        }
        const bool frame = w::Button(c, "nav.frame", {bar.Right() - 130, bar.y, 130, bar.h}, Tr(L"全体を見る"));
        {
            ui::TextStyle ts;
            ts.color = theme::TextDim;
            ts.size = 12;
            c.P().Text({bar.x, bar.y, bar.w - 140, bar.h}, Tr(L"床と壁から歩ける所 (ナビメッシュ) を焼き、2 点の間の道を試します"), ts);
        }
        worlds_ = WorldsOf(host.scene);
        volumes_ = Volumes(host.scene, worlds_);
        FollowSelection(host);
        if (!FindVol(state_.volume)) state_.volume = volumes_.empty() ? 0 : volumes_.front().entity;

        Rect mapR, side;
        if (body.w >= 560) {
            const float sideW = std::min(kSideW, body.w * 0.42f);
            mapR = {body.x, body.y, body.w - sideW - kPad, body.h};
            side = {mapR.Right() + kPad, body.y, sideW, body.h};
        } else {
            const float mapH = std::floor(body.h * 0.5f);
            mapR = {body.x, body.y, body.w, mapH};
            side = {body.x, mapR.Bottom() + kPad, body.w, body.h - mapH - kPad};
        }
        if (frame || !(state_.halfWidth > 0)) Frame(mapR);
        state_.halfWidth = std::clamp(std::isfinite(state_.halfWidth) ? state_.halfWidth : 50.f, kMinHalfWidth, kMaxHalfWidth);
        if (!std::isfinite(state_.center.x) || !std::isfinite(state_.center.y)) state_.center = {0, 0};
        state_.tool = std::clamp(state_.tool, 0, kTools - 1);
        if (!FindAreaKind(*settings, state_.area)) state_.area = settings->areas.empty() ? 0 : settings->areas.front().id;
        UpdateStatus(host, *settings);

        DrawMap(c, mapR, host, *settings);
        if (side.w >= 60 && side.h >= 60) DrawSide(c, side, host, *settings);
        if (worker_.Busy() || host.playing) c.RequestRedraw();  // (進み具合を動かす・Play 中は動く者が動く)
    }

    void LoadState(const json::Value& s) override {
        state_ = {};
        unknowns_ = {};
        if (s.IsObject()) reflect::Load(state_, s, &unknowns_);
    }
    json::Value SaveState() const override { return reflect::Save(state_, &unknowns_); }

private:
    struct Progress {
        std::atomic<int> done{0}, total{0};
    };
    struct Look {
        Status status = Status::None;
        uint64_t fingerprint = 0;
        std::chrono::steady_clock::time_point at{};
    };

    static bool CanEdit(const SceneViewHost& host) { return !host.playing && host.setting != nullptr; }
    static std::function<reflect::EditTarget()> SettingsTarget(const SceneViewHost& host) {
        return host.setting ? host.setting(reflect::TypeOf<NavSettings>().name) : std::function<reflect::EditTarget()>{};
    }
    bool EditSettings(SceneViewHost& host, const std::string& label, uint64_t merge, const std::function<void(NavSettings&)>& change) {
        if (!CanEdit(host)) return false;
        return EditValue(host.host, reflect::TypeOf<NavSettings>(), SettingsTarget(host), label, merge, [&](void* p) { change(*static_cast<NavSettings*>(p)); });
    }
    const NavVolume* FindVol(int entity) const {
        for (const NavVolume& v : volumes_) {
            if (v.entity == entity) return &v;
        }
        return nullptr;
    }
    std::vector<const NavSourceInfo*> Sources(const SceneViewHost& host) const {
        std::vector<const NavSourceInfo*> out;
        for (const auto& s : host.host.Registry().Contributions<NavSourceInfo>(kNavSourcePoint)) out.push_back(s.item);
        return out;
    }
    game_production::AssetAccess UiAssets(const SceneViewHost& host) const {
        game_production::AssetAccess a;
        editor::SoftwareHost* h = &host.host;
        a.file = [h](const AssetRef& ref) { return h->AssetFile(ref); };
        return a;
    }

    // 階層でナビメッシュを 1 つ選んだら、それを範囲にする。
    void FollowSelection(const SceneViewHost& host) {
        const std::vector<int> now(host.selection.begin(), host.selection.end());
        if (now == lastSelection_) return;
        lastSelection_ = now;
        for (const int id : now) {
            if (FindVol(id)) {
                state_.volume = id;
                ClearRoute();
                return;
            }
        }
    }

    // 範囲ごとの状態 (焼いていない / 焼いた / 古い ...)。指紋は少し間をあけて計り直す (軽いが毎フレームはしない)。
    void UpdateStatus(const SceneViewHost& host, const NavSettings& settings) {
        const auto now = std::chrono::steady_clock::now();
        const std::vector<const NavSourceInfo*> sources = Sources(host);
        const game_production::AssetAccess assets = UiAssets(host);
        for (const NavVolume& v : volumes_) {
            Look& l = looks_[v.entity];
            if (l.status != Status::None && now - l.at < std::chrono::milliseconds(300)) continue;
            l.at = now;
            if (v.volume.baked.Empty()) {
                l.status = Status::NotBaked;
                continue;
            }
            NavPreview* p = FindPreview(host.host, v.volume.baked);
            if (!p) {
                l.status = Status::Missing;
                continue;
            }
            if (!p->error.empty()) {
                l.status = Status::Broken;
                continue;
            }
            const NavAgentType* type = FindAgentType(settings, v.volume.agentType);
            l.fingerprint = type ? Fingerprint(host.scene, worlds_, v, *type, sources, &assets) : 0;
            l.status = navigation::FingerprintText(l.fingerprint) == p->head.fingerprint ? Status::Fresh : Status::Stale;
        }
    }
    Status StatusOf(int entity) const {
        const auto it = looks_.find(entity);
        return it != looks_.end() ? it->second.status : Status::None;
    }

    // --- 焼く -----------------------------------------------------------------------------------
    void StartBake(SceneViewHost& host, const NavSettings& settings, int entity) {
        const NavVolume* v = FindVol(entity);
        if (!v || host.playing || worker_.Busy()) return;
        const NavAgentType* type = FindAgentType(settings, v->volume.agentType);
        if (!type) {
            errors_[entity] = TrU8("エージェントの型がありません");
            return;
        }
        BakeJob job;
        job.scene = host.scene;
        job.volume = *v;
        job.type = *type;
        job.sources = Sources(host);
        for (const std::string& id : game_production::AssetIdsIn(host.scene.Save())) {
            if (auto f = host.host.AssetFile({id}); !f.empty()) job.files[id] = std::move(f);
        }
        // 書く所: 前に焼いたファイルがあればそこ。無ければ Assets/Navigation/<範囲の名前>.rmnav (重なれば _2 ...)。
        if (!v->volume.baked.Empty()) job.out = host.host.AssetFile(v->volume.baked);
        if (job.out.empty()) {
            const std::filesystem::path dir = host.host.GetProject().AssetsDir() / L"Navigation";
            const std::string base = FileNameOf(v->name);
            std::error_code ec;
            for (int n = 1; n < 1000; ++n) {
                const std::string name = n == 1 ? base + ".rmnav" : std::format("{}_{}.rmnav", base, n);
                job.out = dir / PathFromUtf8(name);
                if (!std::filesystem::exists(job.out, ec)) break;
            }
        }
        const game_production::AssetAccess assets = UiAssets(host);
        job.head.engine = kEngineVersion;
        job.head.agentType = type->id;
        job.head.agentName = type->name;
        job.head.fingerprint = navigation::FingerprintText(Fingerprint(host.scene, worlds_, *v, *type, job.sources, &assets));
        errors_.erase(entity);
        notes_.erase(entity);
        baking_ = entity;
        progress_->done = 0;
        progress_->total = 0;
        auto progress = progress_;
        worker_.Run([job = std::move(job), progress, this](std::stop_token stop, const editor::Worker::Post& post) {
            BakeOutcome out = RunBake(job, stop, [progress](int d, int t) {
                progress->done = d;
                progress->total = t;
            });
            post([this, out = std::move(out)]() mutable { pending_.push_back(std::move(out)); });
        });
        log::Info("ナビ: 「{}」を焼きます", v->name);
    }

    void StopBake() {
        worker_.Stop();
        queue_.clear();
        if (baking_) notes_[baking_] = Tr(L"やめました");
        baking_ = 0;
    }

    // 作業スレッドの結果を当てる (描画の始めに: host があるとき)。
    void ApplyPending(SceneViewHost& host) {
        while (!pending_.empty()) {
            BakeOutcome out = std::move(pending_.front());
            pending_.pop_front();
            baking_ = 0;
            looks_.erase(out.entity);
            if (out.stopped) {
                notes_[out.entity] = Tr(L"やめました");
                continue;
            }
            if (!out.ok) {
                errors_[out.entity] = out.error;
                log::Warn("ナビ: 焼けませんでした: {}", out.error);
                queue_.clear();
                continue;
            }
            const AssetRef ref = host.host.AssetOf(out.file);
            RememberPreview(ref, out.file, out.data, out.head);
            size_t polys = 0;
            if (const NavPreview* p = FindPreview(host.host, ref)) polys = p->polygonCount;
            notes_[out.entity] = TrF(L"{:.1f} 秒・区画 {}・多角形 {}・三角形 {}", out.ms / 1000.0, out.data.tiles.size(), polys, out.triangles);
            log::Info("ナビ: 焼きました ({}): {:.1f} 秒・区画 {}・多角形 {}・三角形 {}", PathToUtf8(out.file.filename()), out.ms / 1000.0, out.data.tiles.size(), polys,
                      out.triangles);
            if (out.data.tiles.empty()) errors_[out.entity] = TrU8("歩ける所がありません (床が範囲の中にあるか、坂が急すぎないかを見てください)");
            // 部品の「焼いた物」が違う (初めて焼いた) ときだけ、その欄を変える = Undo の 1 件。
            if (host.component && !ref.Empty()) {
                const auto resolve = host.component(out.entity, reflect::TypeOf<NavMeshVolume>().name);
                EditValue(host.host, reflect::TypeOf<NavMeshVolume>(), resolve, "ナビメッシュを焼く", 0, [&](void* p) { static_cast<NavMeshVolume*>(p)->baked = ref; });
            }
        }
        if (!worker_.Busy() && baking_ == 0 && !queue_.empty() && !host.playing) {
            const int next = queue_.front();
            queue_.pop_front();
            if (const NavSettings* s = host.scene.Setting<NavSettings>()) {
                worlds_ = WorldsOf(host.scene);
                volumes_ = Volumes(host.scene, worlds_);
                StartBake(host, *s, next);
            }
        }
    }

    // --- 地図 -----------------------------------------------------------------------------------
    MapView ViewOf(const Rect& r) const { return {r, state_.center, r.w / (2.f * state_.halfWidth)}; }
    static Rect ToolsRect(const Rect& map) { return {map.x + 7, map.y + 7, kToolSquare, kTools * kToolSquare + (kTools - 1) * kToolGap}; }
    bool OverTools(ui::Context& c, const Rect& map) const { return ToolsRect(map).Contains(c.In().mouseX, c.In().mouseY); }

    void Frame(const Rect& r) {
        float x0 = std::numeric_limits<float>::max(), z0 = x0, x1 = -x0, z1 = -x0;
        for (const NavVolume& v : volumes_) {
            if (!v.bounds.Valid()) continue;
            x0 = std::min(x0, v.bounds.min.x), z0 = std::min(z0, v.bounds.min.z);
            x1 = std::max(x1, v.bounds.max.x), z1 = std::max(z1, v.bounds.max.z);
        }
        if (x0 > x1) {
            for (const auto& [id, m] : worlds_) {
                const float x = m.m[3][0], z = m.m[3][2];
                if (!std::isfinite(x) || !std::isfinite(z)) continue;
                x0 = std::min(x0, x), x1 = std::max(x1, x), z0 = std::min(z0, z), z1 = std::max(z1, z);
            }
        }
        if (x0 > x1) {
            state_.center = {0, 0};
            state_.halfWidth = 25;
            return;
        }
        state_.center = {(x0 + x1) * 0.5f, (z0 + z1) * 0.5f};
        const float aspect = r.h > 1 ? r.w / r.h : 1.f;
        state_.halfWidth = std::clamp(std::max((x1 - x0) * 0.5f, (z1 - z0) * 0.5f * aspect) * 1.12f + 2.f, kMinHalfWidth, kMaxHalfWidth);
    }

    void DrawMap(ui::Context& c, const Rect& r, SceneViewHost& host, const NavSettings& settings) {
        if (r.w < 20 || r.h < 20) return;
        HandleInput(c, r, host, settings);
        if (!madeView_) {  // 3D のシーンビューを 1 つ (真上から描かせる)
            madeView_ = true;
            for (const auto& v : host.host.Registry().Contributions<game_production::SceneViewInfo>(game_production::kSceneViewPoint)) {
                if (v.item->create && v.item->space && std::string_view(v.item->space) == "3d") {
                    view_ = v.item->create();
                    break;
                }
            }
        }
        if (view_) {
            SceneViewHost overhead = host;
            overhead.selection = {};
            overhead.overhead = SceneViewHost::Overhead{state_.center, state_.halfWidth};
            view_->Draw(c, r, overhead);
        } else {
            w::DrawSurface(c, r, w::Surface::Sunken, 0);
        }
        c.PushClip(r);
        const MapView v = ViewOf(r);
        // 歩ける面 (エリアの色で薄く) と縁。
        std::vector<Vec2> pts;
        game_production::DebugDraw lines;
        for (const NavVolume& vol : volumes_) {
            NavPreview* p = FindPreview(host.host, vol.volume.baked);
            if (!p || !p->ok) continue;
            for (const navigation::NavPolygon& g : p->polygons) {
                pts.clear();
                for (const Vec3& q : g.points) pts.push_back(v.ToScreen(q.x, q.z));
                const NavAreaKind* kind = FindAreaKind(settings, AreaIdOfCode(g.area));
                w::DrawMapFill(c, pts, kind ? kind->color : theme::SceneNavEdge());
            }
            MeshLines(*p, lines);
        }
        for (const NavVolume& vol : volumes_) BoundsLines(vol.bounds, vol.entity == state_.volume ? theme::Accent : theme::SceneNavVolume(), lines);
        AreaLines(host.scene, worlds_, {}, true, lines);
        for (const game_production::DebugLine& l : lines.Lines()) {
            const Vec2 a = v.ToScreen(l.a.x, l.a.z), b = v.ToScreen(l.b.x, l.b.z);
            if (std::fabs(a.x - b.x) + std::fabs(a.y - b.y) < 0.5f) continue;  // (真上から見ると点になる縦の線)
            w::DrawMapLine(c, a.x, a.y, b.x, b.y, l.color);
        }
        DrawRoute(c, r, host, settings, v);
        if (host.playing) DrawAgents(c, host, settings, v);
        ui::TextStyle ts;
        ts.size = 11;
        ts.color = theme::TextDim;
        c.P().Text({r.x + 8, r.Bottom() - 38, r.w - 16, 16}, TrF(L"地図の幅 {} m", static_cast<int>(std::round(state_.halfWidth * 2))), ts);
        c.P().Text({r.x + 8, r.Bottom() - 20, r.w - 16, 16},
                   state_.tool == 1 ? Tr(L"クリック: 始め、もう一度: 終わり (その次は始めから) ・ ホイール: 拡大縮小 ・ 右 / 中ドラッグ: 動かす")
                                    : Tr(L"クリック: 範囲を選ぶ ・ ホイール: 拡大縮小 ・ 右 / 中ドラッグ: 動かす"),
                   ts);
        if (!view_) Note(c, r.Inset(16), Tr(L"3D 空間のプラグイン (ゲーム制作 × 3D) が入ると、ここにシーンを真上から見た絵が出ます。"));
        DrawTools(c, r);
        c.PopClip();
    }

    void DrawTools(ui::Context& c, const Rect& map) {
        const Rect col = ToolsRect(map);
        if (map.h < col.h + 40 || map.w < col.w + 60) return;
        const std::wstring tips[kTools] = {Tr(L"選ぶ: 地図の上の範囲 (ナビメッシュ) をクリックして選びます"),
                                           Tr(L"経路を試す: 2 か所をクリックすると、選んだ範囲のナビメッシュで道を探します")};
        constexpr w::Icon kIcons[kTools] = {w::Icon::Cursor, w::Icon::Route};
        constexpr const char* kIds[kTools] = {"nav.tool.select", "nav.tool.route"};
        for (int i = 0; i < kTools; ++i) {
            const Rect sq{col.x, col.y + static_cast<float>(i) * (kToolSquare + kToolGap), kToolSquare, kToolSquare};
            if (w::ToolSquare(c, kIds[i], sq, kIcons[i], state_.tool == i, tips[i]).clicked && state_.tool != i) {
                state_.tool = i;
                c.RequestRedraw();
            }
        }
    }

    void HandleInput(ui::Context& c, const Rect& r, SceneViewHost& host, const NavSettings& settings) {
        const ui::Input& in = c.In();
        const bool hovered = c.IsHovered(r) && !OverTools(c, r);
        const MapView v = ViewOf(r);
        if (hovered && in.wheel != 0) {
            const Vec2 at = v.ToWorld(in.mouseX, in.mouseY);
            const float hw = std::clamp(state_.halfWidth * std::pow(0.85f, in.wheel), kMinHalfWidth, kMaxHalfWidth);
            const float scale = r.w / (2.f * hw);
            state_.center = {at.x - (in.mouseX - v.CX()) / scale, at.y + (in.mouseY - v.CY()) / scale};
            state_.halfWidth = hw;
            c.RequestRedraw();
        }
        if (hovered && (c.MousePressed(ui::Mouse::Right) || c.MousePressed(ui::Mouse::Middle))) panning_ = true;
        if (panning_) {
            if (in.down[1] || in.down[2]) {
                state_.center.x -= in.mouseDX / v.scale;
                state_.center.y += in.mouseDY / v.scale;
                if (in.mouseDX != 0 || in.mouseDY != 0) c.RequestRedraw();
            } else {
                panning_ = false;
            }
        }
        auto b = c.ButtonBehavior(c.MakeId("nav.map"), r);
        if (!hovered || !b.clicked) return;
        const Vec2 at = v.ToWorld(in.mouseX, in.mouseY);
        if (state_.tool == 0) {
            int best = 0;
            float bestArea = std::numeric_limits<float>::max();
            for (const NavVolume& vol : volumes_) {
                const auto& bb = vol.bounds;
                if (at.x < bb.min.x || at.x > bb.max.x || at.y < bb.min.z || at.y > bb.max.z) continue;
                const float area = (bb.max.x - bb.min.x) * (bb.max.z - bb.min.z);
                if (area < bestArea) bestArea = area, best = vol.entity;
            }
            if (best) {
                state_.volume = best;
                lastSelection_ = {best};
                ClearRoute();
                if (host.select) host.select({best});
            }
            return;
        }
        // 経路を試す: 選んだ範囲のナビメッシュの上の、クリックした所に一番近い点。
        const NavVolume* vol = FindVol(state_.volume);
        NavPreview* p = vol ? FindPreview(host.host, vol->volume.baked) : nullptr;
        if (!p || !p->ok) {
            routeNote_ = Tr(L"選んだ範囲はまだ焼いていません (先に「焼く」)");
            return;
        }
        const float midY = (vol->bounds.min.y + vol->bounds.max.y) * 0.5f;
        const float halfY = (vol->bounds.max.y - vol->bounds.min.y) * 0.5f + 1.f;
        const auto point = p->MeshWith(BlockedOf(settings)).ClosestPoint({at.x, midY, at.y}, {0.75f, halfY, 0.75f}, FilterOf(settings));
        if (!point) {
            routeNote_ = Tr(L"そこはナビメッシュの外です");
            return;
        }
        routeNote_.clear();
        if (!hasA_ || hasB_) {
            a_ = *point, hasA_ = true, hasB_ = false;
        } else {
            b_ = *point, hasB_ = true;
        }
        c.RequestRedraw();
    }

    // Play 中の動く者: 道 (角の点) と体の丸・進む向き (見本 B)。
    void DrawAgents(ui::Context& c, const SceneViewHost& host, const NavSettings& settings, const MapView& v) {
        auto& scene = const_cast<Scene&>(host.scene);  // (Play 中のコピー。読むだけ)
        const NavWorld* world = FindNavWorld(scene);
        if (!world) return;
        std::vector<Vec3> path;
        std::vector<Vec2> pts;
        for (const auto& [id, a] : world->Agents()) {
            if (a.index < 0) continue;
            const NavWorld::Mesh& m = world->Meshes()[static_cast<size_t>(a.mesh)];
            if (m.crowd && a.hasTarget) {
                m.crowd->Path(a.index, path);
                pts.clear();
                for (const Vec3& q : path) pts.push_back(v.ToScreen(q.x, q.z));
                for (size_t i = 1; i < pts.size(); ++i) w::DrawMapLine(c, pts[i - 1].x, pts[i - 1].y, pts[i].x, pts[i].y, theme::NavRoute());
            }
            const game_production::SceneEntity* e = scene.Find(id);
            const game_production::Component* comp = e ? e->FindComponent(reflect::TypeOf<NavAgent>().name) : nullptr;
            const NavAgentType* t = comp && comp->Known() ? FindAgentType(settings, static_cast<const NavAgent*>(comp->value.Get())->agentType) : nullptr;
            const Vec2 p = v.ToScreen(a.view.position.x, a.view.position.z);
            w::DrawMapAgent(c, p.x, p.y, (t ? t->radius : 0.3f) * v.scale, a.view.velocity.x, -a.view.velocity.z);
        }
    }

    // Play 中の動く者の一覧: 名前・状態・速さ・残り (見本 B)。下の y を返す。
    float DrawAgentList(ui::Context& c, const Rect& r, const SceneViewHost& host) {
        auto& scene = const_cast<Scene&>(host.scene);
        const NavWorld* world = FindNavWorld(scene);
        float y = r.y;
        Label(c, {r.x, y, r.w, kRow}, Tr(L"動く者 (Play 中)"), true);
        y += kRow + 2;
        if (!world || world->Agents().empty()) {
            Note(c, {r.x, y, r.w, 36}, Tr(L"「ナビの動く者」が付いた物がありません (スクリプトの rm::NavAgent で行き先を決めます)"));
            return y + 40;
        }
        std::vector<std::pair<int, const NavWorld::Agent*>> list;
        for (const auto& [id, a] : world->Agents()) list.push_back({id, &a});
        std::sort(list.begin(), list.end(), [](const auto& p, const auto& q) { return p.first < q.first; });
        const float rowH = kRow;
        const float content = rowH * static_cast<float>(list.size());
        const Rect box{r.x, y, r.w, std::min(content, std::max(rowH * 3, r.h * 0.3f))};
        const float scroll = w::BeginScroll(c, "nav.agents", box, content);
        ui::TextStyle ts;
        ts.color = theme::Text;
        ui::TextStyle dim;
        dim.color = theme::TextDim;
        dim.h = ui::HAlign::Right;
        for (size_t i = 0; i < list.size(); ++i) {
            const auto& [id, a] = list[i];
            const Rect row{box.x, box.y - scroll + rowH * static_cast<float>(i), box.w, rowH};
            const game_production::SceneEntity* e = scene.Find(id);
            if (w::Selectable(c, std::format("nav.ag{}", id), row, std::find(host.selection.begin(), host.selection.end(), id) != host.selection.end()).clicked &&
                host.select) {
                host.select({id});
            }
            c.P().Text({row.x + 8, row.y, row.w * 0.4f, row.h}, e ? Utf8ToWide(e->data.name) : std::wstring(), ts);
            const Vec3 vel = a->view.velocity;
            const float speed = std::sqrt(vel.x * vel.x + vel.z * vel.z);
            std::wstring state;
            if (a->index < 0) state = Tr(L"ナビメッシュの外");
            else if (a->stopped) state = Tr(L"止まっている");
            else if (a->arrived) state = Tr(L"着いた");
            else if (!a->hasTarget) state = Tr(L"行き先なし");
            else if (a->pending || a->view.target == navigation::AgentView::Target::Pending) state = Tr(L"道を探している");
            else if (a->view.target == navigation::AgentView::Target::Failed) state = Tr(L"行けない");
            else state = TrF(L"{:.1f} m/s ・ 残り {:.1f} m{}", speed, a->remaining, a->view.partial ? Tr(L" (一番近い所まで)") : std::wstring());
            c.P().Text({row.x + row.w * 0.4f, row.y, row.w * 0.6f - 8, row.h}, state, dim);
        }
        w::EndScroll(c, "nav.agents", box, content);
        return box.Bottom() + 10;
    }

    void ClearRoute() {
        hasA_ = hasB_ = false;
        routeNote_.clear();
    }

    void DrawRoute(ui::Context& c, const Rect& r, SceneViewHost& host, const NavSettings& settings, const MapView& v) {
        if (state_.tool != 1 && !hasA_) return;
        std::wstring text = routeNote_;
        std::vector<Vec2> pts;
        if (hasA_) {
            const NavVolume* vol = FindVol(state_.volume);
            NavPreview* p = vol ? FindPreview(host.host, vol->volume.baked) : nullptr;
            if (hasB_ && p && p->ok) {
                const float halfY = vol->bounds.max.y - vol->bounds.min.y + 1.f;
                const navigation::NavPath path = p->MeshWith(BlockedOf(settings)).FindPath(a_, b_, FilterOf(settings), {0.75f, halfY, 0.75f});
                for (const Vec3& q : path.points) pts.push_back(v.ToScreen(q.x, q.z));
                if (text.empty()) text = PathText(path);
            } else {
                pts.push_back(v.ToScreen(a_.x, a_.z));
                if (text.empty()) text = Tr(L"終わりの所をクリックしてください");
            }
        } else if (text.empty()) {
            text = Tr(L"始めの所をクリックしてください");
        }
        w::DrawMapRoute(c, pts);
        if (!text.empty()) w::DrawMapReadout(c, r.x + kToolSquare + 18, r.y + 10, text);
    }

    // --- 右の側 -----------------------------------------------------------------------------------
    void DrawSide(ui::Context& c, const Rect& r, SceneViewHost& host, const NavSettings& settings) {
        const bool canEdit = CanEdit(host);
        float y = r.y;
        Label(c, {r.x, y, r.w - 120, kRow}, Tr(L"範囲 (ナビメッシュ)"), true);
        if (w::Button(c, "nav.add", {r.Right() - 112, y, 112, kRow}, Tr(L"＋ 範囲を置く"), w::ButtonKind::Secondary, canEdit && host.editScene != nullptr)) {
            host.editScene({}, "ナビメッシュを置く", [](Scene& s) { return std::vector<int>{MakeVolume(s, 0, Scene::kEnd)}; });
        }
        y += kRow + 4;
        if (volumes_.empty()) {
            Note(c, {r.x, y, r.w, 64}, Tr(L"シーンに「ナビメッシュ」がありません。「＋ 範囲を置く」か、階層の右クリック ▸ 追加 ▸ ナビ ▸ ナビメッシュで置き、床と壁が入る大きさにします。"));
            y += 70;
        } else {
            y = DrawVolumeList(c, {r.x, y, r.w, r.Bottom() - y}, host);
            y = DrawBakeRow(c, {r.x, y, r.w, r.Bottom() - y}, host, settings);
        }
        if (host.playing && r.Bottom() - y >= 60) y = DrawAgentList(c, {r.x, y, r.w, r.Bottom() - y}, host);
        if (r.Bottom() - y < 60) return;
        y = DrawAreaList(c, {r.x, y, r.w, r.Bottom() - y}, host, settings);
        if (r.Bottom() - y < 40) return;

        // 選んだ範囲の欄・その型・選んだエリアの値: インスペクターと同じ描き方。
        std::vector<inspector::Block> blocks;
        const NavVolume* vol = FindVol(state_.volume);
        const game_production::SceneEntity* ve = vol ? host.scene.Find(vol->entity) : nullptr;
        const game_production::Component* vc = ve ? ve->FindComponent(reflect::TypeOf<NavMeshVolume>().name) : nullptr;
        if (vc && vc->Known()) {
            inspector::Block b;
            b.type = &reflect::TypeOf<NavMeshVolume>();
            b.object = const_cast<void*>(vc->value.Get());  // (インスペクターは書かない: 変更は Edit で返る)
            b.unknowns = &vc->unknowns;
            b.title = TrF(L"範囲「{}」", Utf8ToWide(vol->name));
            b.readOnly = !canEdit;
            b.fields = {"agentType", "meshes", "colliders", "size", "offset"};
            const NavSettings* s = &settings;
            b.choices = [s](const reflect::FieldPath& path) -> std::optional<std::vector<inspector::Choice>> {
                const auto& f = reflect::TypeOf<NavMeshVolume>().fields;
                if (path.size() != 1 || path[0] >= f.size() || std::string_view(f[path[0]].name) != "agentType") return std::nullopt;
                std::vector<inspector::Choice> out;
                for (const NavAgentType& t : s->agentTypes) out.push_back({std::to_string(t.id), Utf8ToWide(TrU8(t.name))});
                return out;
            };
            blocks.push_back(std::move(b));
        }
        const NavAgentType* type = vol ? FindAgentType(settings, vol->volume.agentType) : (settings.agentTypes.empty() ? nullptr : &settings.agentTypes.front());
        if (type) {
            typeCopy_ = *type;
            inspector::Block b;
            b.type = &reflect::TypeOf<NavAgentType>();
            b.object = &typeCopy_;
            b.title = TrF(L"エージェントの型「{}」", Utf8ToWide(TrU8(type->name)));
            b.note = Tr(L"体の形を変えたら、焼き直します (古い印が出ます)");
            b.readOnly = !canEdit;
            blocks.push_back(std::move(b));
        }
        if (const NavAreaKind* area = FindAreaKind(settings, state_.area)) {
            areaCopy_ = *area;
            inspector::Block b;
            b.type = &reflect::TypeOf<NavAreaKind>();
            b.object = &areaCopy_;
            b.title = TrF(L"エリア「{}」", Utf8ToWide(TrU8(area->name)));
            b.note = area->id == 0 ? Tr(L"ふつうの床 (エリアを決めていない所) です") : Tr(L"費用と「通れない」は焼き直さなくても効きます");
            b.readOnly = !canEdit;
            blocks.push_back(std::move(b));
        }
        const auto edit = inspector::Draw(c, inspector_, {r.x, y, r.w, r.Bottom() - y}, blocks);
        if (!edit || !canEdit || edit->block >= blocks.size()) return;
        const reflect::TypeDesc* t = blocks[edit->block].type;
        if (t == &reflect::TypeOf<NavMeshVolume>() && vol && host.component) {
            host.host.History().Push(std::make_unique<reflect::SetFieldCommand>(*t, edit->path, edit->before, edit->after,
                                                                                host.component(vol->entity, t->name), edit->label, edit->merge),
                                     false);
        } else if (t == &reflect::TypeOf<NavAgentType>()) {
            const int id = typeCopy_.id;
            EditSettings(host, edit->label, edit->merge, [&](NavSettings& s) {
                for (NavAgentType& k : s.agentTypes) {
                    if (k.id != id) continue;
                    const reflect::PathTarget pt = reflect::Resolve(reflect::TypeOf<NavAgentType>(), &k, nullptr, edit->path);
                    if (pt.field) reflect::LoadField(*pt.field, pt.owner, edit->after, nullptr, nullptr);
                    break;
                }
            });
        } else if (t == &reflect::TypeOf<NavAreaKind>()) {
            const int id = areaCopy_.id;
            EditSettings(host, edit->label, edit->merge, [&](NavSettings& s) {
                for (NavAreaKind& k : s.areas) {
                    if (k.id != id) continue;
                    const reflect::PathTarget pt = reflect::Resolve(reflect::TypeOf<NavAreaKind>(), &k, nullptr, edit->path);
                    if (pt.field) reflect::LoadField(*pt.field, pt.owner, edit->after, nullptr, nullptr);
                    break;
                }
            });
        }
        c.RequestRedraw();
    }

    // 範囲の一覧 (名前と状態)。下の y を返す。
    float DrawVolumeList(ui::Context& c, const Rect& r, SceneViewHost& host) {
        const float rowH = kRow + 2;
        const float content = rowH * static_cast<float>(volumes_.size());
        const float listH = std::min(content, std::max(rowH * 2, (r.h) * 0.22f));
        const Rect list{r.x, r.y, r.w, listH};
        const float scroll = w::BeginScroll(c, "nav.volumes", list, content);
        ui::TextStyle ts;
        ts.color = theme::Text;
        ui::TextStyle dim;
        dim.color = theme::TextDim;
        dim.h = ui::HAlign::Right;
        for (size_t i = 0; i < volumes_.size(); ++i) {
            const NavVolume& v = volumes_[i];
            const Rect row{list.x, list.y - scroll + rowH * static_cast<float>(i), list.w, kRow};
            if (w::Selectable(c, std::format("nav.v{}", v.entity), row, v.entity == state_.volume).clicked && v.entity != state_.volume) {
                state_.volume = v.entity;
                lastSelection_ = {v.entity};
                ClearRoute();
                if (host.select) host.select({v.entity});
            }
            c.P().Text({row.x + 8, row.y, row.w * 0.5f - 8, row.h}, Utf8ToWide(v.name), ts);
            const Status s = baking_ == v.entity ? Status::None : StatusOf(v.entity);
            dim.color = s == Status::Stale || s == Status::Missing || s == Status::Broken ? theme::Warn : theme::TextDim;
            c.P().Text({row.x + row.w * 0.5f, row.y, row.w * 0.5f - 8, row.h}, baking_ == v.entity ? Tr(L"焼いています…") : StatusText(s), dim);
        }
        w::EndScroll(c, "nav.volumes", list, content);
        return list.Bottom() + 6;
    }

    // 焼くボタン・進み具合・結果。下の y を返す。
    float DrawBakeRow(ui::Context& c, const Rect& r, SceneViewHost& host, const NavSettings& settings) {
        float y = r.y;
        const bool busy = worker_.Busy() || baking_ != 0;
        const bool can = CanEdit(host) && !busy && FindVol(state_.volume);
        const float half = std::floor((r.w - 6) * 0.5f);
        if (w::Button(c, "nav.bake", {r.x, y, half, kRow}, Tr(L"焼く"), w::ButtonKind::Primary, can)) StartBake(host, settings, state_.volume);
        if (busy) {
            if (w::Button(c, "nav.stop", {r.x + half + 6, y, r.w - half - 6, kRow}, Tr(L"やめる"))) StopBake();
        } else if (w::Button(c, "nav.bakeAll", {r.x + half + 6, y, r.w - half - 6, kRow}, Tr(L"全部を焼く"), w::ButtonKind::Secondary, can && volumes_.size() > 1)) {
            queue_.clear();
            for (const NavVolume& v : volumes_) {
                if (v.entity != state_.volume) queue_.push_back(v.entity);
            }
            StartBake(host, settings, state_.volume);
        }
        y += kRow + 6;
        if (busy) {
            const int total = progress_->total.load(), done = progress_->done.load();
            w::ProgressBar(c, {r.x, y + 4, r.w, 8}, total > 0 ? static_cast<float>(done) / static_cast<float>(total) : 0.f);
            y += 16;
            Note(c, {r.x, y, r.w, 18}, total > 0 ? TrF(L"区画 {} / {}", done, total) : Tr(L"焼く物を集めています…"));
            y += 22;
        } else if (host.playing) {
            Note(c, {r.x, y, r.w, 18}, Tr(L"Play 中は焼けません (■ で止めてから)"));
            y += 22;
        }
        if (const auto e = errors_.find(state_.volume); e != errors_.end()) {
            ui::TextStyle ts;
            ts.wrap = true;
            ts.color = theme::Warn;
            ts.v = ui::VAlign::Top;
            c.P().Text({r.x, y, r.w, 36}, Utf8ToWide(TrU8(e->second)), ts);
            y += 40;
        } else if (const auto n = notes_.find(state_.volume); n != notes_.end()) {
            Note(c, {r.x, y, r.w, 18}, n->second);
            y += 22;
        }
        return y + 6;
    }

    // エリアの種類の一覧と ＋ / 消す。下の y を返す。
    float DrawAreaList(ui::Context& c, const Rect& r, SceneViewHost& host, const NavSettings& settings) {
        const bool canEdit = CanEdit(host);
        float y = r.y;
        Label(c, {r.x, y, r.w - 150, kRow}, Tr(L"エリアの種類"), true);
        if (w::Button(c, "nav.area.add", {r.Right() - 144, y, 76, kRow}, Tr(L"＋ 足す"), w::ButtonKind::Secondary, canEdit && NewAreaKindId(settings) != 0)) {
            NavAreaKind k;
            k.id = NewAreaKindId(settings);
            k.name = TrU8("新しいエリア");
            k.color = AreaKindColor(settings.areas.size());
            const int id = k.id;
            if (EditSettings(host, "エリアの種類を足す", 0, [&k](NavSettings& s) { s.areas.push_back(k); })) state_.area = id;
        }
        if (w::Button(c, "nav.area.remove", {r.Right() - 64, y, 64, kRow}, Tr(L"消す"), w::ButtonKind::Secondary, canEdit && state_.area != 0 && FindAreaKind(settings, state_.area))) {
            const int id = state_.area;
            const NavAreaKind* k = FindAreaKind(settings, id);
            EditSettings(host, std::format("エリアの種類「{}」を消す", k ? k->name : std::string()), 0,
                         [id](NavSettings& s) { std::erase_if(s.areas, [id](const NavAreaKind& a) { return a.id == id; }); });
            state_.area = 0;
        }
        y += kRow + 4;
        const float rowH = kRow + 2;
        const float content = rowH * static_cast<float>(settings.areas.size());
        const float listH = std::min(content, std::max(rowH * 3, (r.Bottom() - y) * 0.3f));
        const Rect list{r.x, y, r.w, listH};
        const float scroll = w::BeginScroll(c, "nav.areas", list, content);
        ui::TextStyle ts;
        ts.color = theme::Text;
        ui::TextStyle dim;
        dim.color = theme::TextDim;
        dim.h = ui::HAlign::Right;
        for (size_t i = 0; i < settings.areas.size(); ++i) {
            const NavAreaKind& k = settings.areas[i];
            const Rect row{list.x, list.y - scroll + rowH * static_cast<float>(i), list.w, kRow};
            if (w::Selectable(c, std::format("nav.a{}", k.id), row, k.id == state_.area).clicked) state_.area = k.id;
            w::DrawSwatch(c, {row.x + 6, row.y + 5, 16, 16}, k.color);
            c.P().Text({row.x + 30, row.y, row.w - 150, row.h}, Utf8ToWide(TrU8(k.name)), ts);
            c.P().Text({row.Right() - 124, row.y, 116, row.h}, k.blocked ? Tr(L"通れない") : TrF(L"費用 {:g}", k.cost), dim);
        }
        w::EndScroll(c, "nav.areas", list, content);
        return list.Bottom() + 10;
    }

    NavTabState state_;
    reflect::Unknowns unknowns_;
    std::unique_ptr<game_production::SceneView> view_;  // 3D のシーンビュー (真上から描かせる)
    bool madeView_ = false;
    bool panning_ = false;
    Worlds worlds_;
    std::vector<NavVolume> volumes_;
    std::vector<int> lastSelection_;
    std::map<int, Look> looks_;
    std::map<int, std::string> errors_;   // 範囲ごとの最後の焼けなかったわけ (UTF-8・日本語。見せるときに訳す)
    std::map<int, std::wstring> notes_;   // 範囲ごとの最後の結果
    editor::Worker worker_;
    std::shared_ptr<Progress> progress_;
    std::deque<BakeOutcome> pending_;
    std::deque<int> queue_;  // 「全部を焼く」の残り
    int baking_ = 0;
    bool hasA_ = false, hasB_ = false;
    Vec3 a_{}, b_{};
    std::wstring routeNote_;
    NavAgentType typeCopy_;
    NavAreaKind areaCopy_;
    inspector::State inspector_;
};

}  // namespace

std::unique_ptr<game_production::SceneView> CreateNavTab() { return std::make_unique<NavTab>(); }

}  // namespace rm::game_x_navigation
