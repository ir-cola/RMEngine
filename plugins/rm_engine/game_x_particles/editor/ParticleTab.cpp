#include "rm_engine/game_x_particles/editor/ParticleTab.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "core/reflect/Data.h"
#include "core/reflect/FieldCommand.h"
#include "editor/app/Worker.h"
#include "editor/lang/Lang.h"
#include "editor/project/Project.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "platform/Shell.h"
#include "platform/Win.h"
#include "rm_engine/game_x_particles/runtime/ParticleDraw.h"
#include "rm_engine/game_x_particles/runtime/ParticleSystem.h"
#include "rm_engine/particles/runtime/BuiltinModules.h"
#include "rm_engine/particles/runtime/ParticleEffect.h"
#include "rm_engine/particles/runtime/ParticlePlayer.h"
#include "rm_engine/particles/runtime/Presets.h"
#include "rm_engine/space3d/editor/Preview3D.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <shellapi.h>

namespace rm::game_x_particles {

namespace {

namespace fs = std::filesystem;
namespace w = editor::w;
namespace theme = editor::theme;
using editor::Tr;
using editor::TrF;
using editor::TrU8;
using particles::ModuleParam;
using particles::ModuleSource;
using particles::ParamType;
using particles::ParticleEffect;
using particles::ParticleEmitter;
using particles::ParticleModule;
using particles::ParticleValue;

constexpr float kPad = 8, kRow = 26, kIndent = 18;

// One edit of an effect's file: its contents before and after (typing into one field: one entry).
class EffectCommand final : public ICommand {
public:
    EffectCommand(editor::SoftwareHost& host, AssetRef ref, std::string before, std::string after, std::string label, uint64_t merge)
        : host_(host), ref_(std::move(ref)), before_(std::move(before)), after_(std::move(after)), label_(std::move(label)), merge_(merge) {}
    void Do() override { host_.SetAssetContent(ref_, after_); }
    void Undo() override { host_.SetAssetContent(ref_, before_); }
    std::string Label() const override { return label_; }
    bool IsNoOp() const override { return before_ == after_; }
    bool CanMergeWith(const ICommand& next) const override {
        const auto* n = dynamic_cast<const EffectCommand*>(&next);
        return n && merge_ != 0 && n->merge_ == merge_ && n->ref_ == ref_;
    }
    bool MergeWith(const ICommand& next) override {
        if (!CanMergeWith(next)) return false;
        after_ = static_cast<const EffectCommand&>(next).after_;
        return true;
    }
    size_t Bytes() const override { return before_.size() + after_.size(); }

private:
    editor::SoftwareHost& host_;
    AssetRef ref_;
    std::string before_, after_, label_;
    uint64_t merge_;
};

// Changes the effect of `ref` (a copy of it as the editor has it now) and puts it in the history.
void EditEffect(editor::SoftwareHost& host, const AssetRef& ref, const std::string& label, uint64_t merge,
                const std::function<void(ParticleEffect&)>& change) {
    const auto bytes = host.AssetContent(ref);
    if (!bytes) return;
    reflect::Unknowns unknowns;
    std::string err;
    auto effect = particles::ReadEffect(*bytes, &err, &unknowns);
    if (!effect) return;
    change(*effect);
    std::string after = particles::WriteEffect(*effect, &unknowns);
    if (after == *bytes) return;
    host.History().Push(std::make_unique<EffectCommand>(host, ref, *bytes, std::move(after), label, merge), false);
}

// The emitter shown in the インスペクター (kept alive by the inspector's functions, not by the tab).
struct EmitterInspect {
    editor::SoftwareHost* host = nullptr;
    AssetRef ref;
    size_t index = 0;
    std::string bytes;  // what `copy` was read from
    ParticleEmitter copy;
};

editor::Inspectable EmitterInspectable(std::shared_ptr<EmitterInspect> s) {
    editor::Inspectable in;
    in.title = Tr(L"エミッター");
    in.blocks = [s] {
        editor::InspectBlock b{&reflect::TypeOf<ParticleEmitter>(),
                               [s]() -> reflect::EditTarget {
                                   const auto bytes = s->host->AssetContent(s->ref);
                                   if (!bytes) return {};
                                   if (*bytes != s->bytes) {
                                       std::string err;
                                       const auto e = particles::ReadEffect(*bytes, &err);
                                       if (!e || s->index >= e->emitters.size()) return {};
                                       s->copy = e->emitters[s->index];
                                       s->bytes = *bytes;
                                   }
                                   return {&s->copy, nullptr};
                               },
                               {}, Tr(L"エフェクトのモジュールは「パーティクル」のタブで並べます"), {}};
        b.onEdit = [s](editor::InspectBlock::Change ch) {
            EditEffect(*s->host, s->ref, ch.label, ch.merge, [&](ParticleEffect& e) {
                if (s->index >= e.emitters.size()) return;
                const reflect::PathTarget pt = reflect::Resolve(reflect::TypeOf<ParticleEmitter>(), &e.emitters[s->index], nullptr, ch.path);
                if (pt.field) reflect::LoadField(*pt.field, pt.owner, ch.after, nullptr, nullptr);
            });
        };
        return std::vector<editor::InspectBlock>{std::move(b)};
    };
    return in;
}

// The value of a param in a module (given, else its default).
std::array<float, 4> ValueOf(const ParticleModule& m, const ModuleParam& p) {
    for (const ParticleValue& v : m.values) {
        if (v.name == p.name) return {v.x, v.y, v.z, v.w};
    }
    return p.value;
}

// A module of a file shown in the インスペクター (its file and 使う: the reference field picks a .rmfx).
struct ModuleInspect {
    editor::SoftwareHost* host = nullptr;
    AssetRef ref;
    size_t emitter = 0, module = 0;
    std::string bytes;
    ParticleModule copy;
};

editor::Inspectable ModuleInspectable(std::shared_ptr<ModuleInspect> s) {
    editor::Inspectable in;
    in.title = Tr(L"モジュール");
    in.blocks = [s] {
        editor::InspectBlock b{&reflect::TypeOf<ParticleModule>(),
                               [s]() -> reflect::EditTarget {
                                   const auto bytes = s->host->AssetContent(s->ref);
                                   if (!bytes) return {};
                                   if (*bytes != s->bytes) {
                                       std::string err;
                                       const auto e = particles::ReadEffect(*bytes, &err);
                                       if (!e || s->emitter >= e->emitters.size() || s->module >= e->emitters[s->emitter].modules.size()) return {};
                                       s->copy = e->emitters[s->emitter].modules[s->module];
                                       s->bytes = *bytes;
                                   }
                                   return {&s->copy, nullptr};
                               },
                               {}, Tr(L"HLSL で書いたモジュール (.rmfx) を選びます。値は「パーティクル」のタブで変えます"), {}};
        b.onEdit = [s](editor::InspectBlock::Change ch) {
            EditEffect(*s->host, s->ref, ch.label, ch.merge, [&](ParticleEffect& e) {
                if (s->emitter >= e.emitters.size() || s->module >= e.emitters[s->emitter].modules.size()) return;
                const reflect::PathTarget pt = reflect::Resolve(reflect::TypeOf<ParticleModule>(), &e.emitters[s->emitter].modules[s->module], nullptr, ch.path);
                if (pt.field) reflect::LoadField(*pt.field, pt.owner, ch.after, nullptr, nullptr);
            });
        };
        return std::vector<editor::InspectBlock>{std::move(b)};
    };
    return in;
}

// What is wrong with a line of a module (in the screen's language).
std::wstring ErrorText(const particles::ModuleError& e) {
    using K = particles::ModuleErrorKind;
    std::wstring what;
    const std::wstring detail = Utf8ToWide(e.detail);
    switch (e.kind) {
        case K::BadLine: what = Tr(L"この行を読めません (param 型 名前 = 値; / attr 型 名前; / module \"名前\")"); break;
        case K::UnknownType: what = TrF(L"「{}」という型はありません", detail); break;
        case K::BadName: what = TrF(L"「{}」は名前に使えません (英字・数字・_ で、数字から始めない)", detail); break;
        case K::SameName: what = TrF(L"「{}」が 2 つあります", detail); break;
        case K::BadValue: what = TrF(L"値「{}」を読めません", detail); break;
        case K::NoFunction: what = Tr(L"Spawn も Update もありません (どちらかを書きます)"); break;
    }
    return e.line > 0 ? TrF(L"{} 行目: {}", e.line, what) : what;
}

// Opens a file in the program Windows has for it (else asks which one) - after this frame.
void OpenOutside(const fs::path& file) {
    editor::PostToUi([file] {
        if (!platform::OpenFileOutside(file)) log::Error("ファイルを開けませんでした: {}", PathToUtf8(file.wstring()));
    });
}

// A new module's file: a short example to start from.
constexpr char kNewModule[] = R"(module "新しいモジュール"
// 欄になる値: param 型 名前 = 初めの値 [いちばん小さい, いちばん大きい]; // 欄の名前
// 型: float float2 float3 float4 color int bool
param float strength = 1 [0, 100]; // 強さ

// 粒が生まれたとき 1 回 (要らなければ消す):
// void Spawn(inout Particle p, inout Random rnd) { }

// 毎フレーム。p の値: position velocity color size rotation spin age lifetime alive (0 で消える)
// 使えるもの: Rand(rnd) RandRange(rnd, a, b) RandOnSphere(rnd) LifeRatio(p) Time EmitterPosition
void Update(inout Particle p, float dt, inout Random rnd) {
    p.velocity.y += strength * dt;
}
)";

class ParticleTab final : public game_production::SceneView {
public:
    void Draw(ui::Context& c, const ui::Rect& r, game_production::SceneViewHost& host) override {
        if (r.w < 40 || r.h < 40) return;
        const std::optional<Target> target = FindTarget(host);
        if (!target) {
            Note(c, r.Inset(24), Tr(L"「パーティクル」の部品を付けたエンティティを選ぶと、そのエフェクトをここで作れます。部品はインスペクターの「部品を足す」から付けます。"));
            return;
        }
        if (target->effect.Empty()) {
            const ui::Rect box = r.Inset(24);
            Note(c, {box.x, box.y, box.w, 60}, Tr(L"この「パーティクル」には、まだエフェクトのファイルがありません (見本の光が出ています)。作ると Assets/Particles に入り、ここで変えられます。"));
            const ui::Rect create{box.x, box.y + 70, 200, 30};
            if (w::Button(c, "particles.create", create, Tr(L"エフェクトを作る …"), w::ButtonKind::Primary)) createMenu_.OpenAt(create.x, create.Bottom());
            if (const int chosen = std::exchange(chosenCreate_, -1); chosen >= 0) {
                CreateEffect(host, *target, particles::EffectPresets()[static_cast<size_t>(chosen)]);
            }
            Menus(c);
            return;
        }
        if (target->effect != ref_) {
            ref_ = target->effect;
            emitter_ = 0;
            module_ = -1;
            bytes_.clear();
            effect_.reset();
            player_->Restart();
        }
        Load(host);
        if (!effect_) {
            Note(c, r.Inset(24), TrF(L"エフェクトのファイルを読めません: {}", Utf8ToWide(loadError_)));
            return;
        }
        emitter_ = std::min<size_t>(emitter_, effect_->emitters.empty() ? 0 : effect_->emitters.size() - 1);

        // The strip: the file's name, again from the start, a new emitter.
        const ui::Rect top{r.x + kPad, r.y + kPad, r.w - 2 * kPad, 28};
        ui::TextStyle ts;
        ts.color = theme::Text;
        ts.bold = true;
        c.P().Text({top.x, top.y, top.w - 250, top.h}, host.host.AssetFile(ref_).filename().wstring(), ts);
        if (w::Button(c, "particles.restart", {top.Right() - 240, top.y, 110, top.h}, Tr(L"始めから"))) player_->Restart();
        const ui::Rect addEmitter{top.Right() - 122, top.y, 122, top.h};
        if (w::Button(c, "particles.add_emitter", addEmitter, Tr(L"＋ エミッター"))) emitterMenu_.OpenAt(addEmitter.x, addEmitter.Bottom());

        const float previewH = std::max(120.f, (r.h - top.h - 3 * kPad) * 0.45f);
        const ui::Rect preview{r.x + kPad, top.Bottom() + 6, r.w - 2 * kPad, previewH};
        DrawPreview(c, preview, host);
        const float lowerY = preview.Bottom() + kPad;
        const float lowerH = r.Bottom() - kPad - lowerY;
        if (lowerH >= 30) {
            const float listW = std::floor((r.w - 3 * kPad) * 0.5f);
            DrawList(c, {r.x + kPad, lowerY, listW, lowerH}, host);
            DrawValues(c, {r.x + 2 * kPad + listW, lowerY, r.w - 3 * kPad - listW, lowerH}, host);
        }
        // What a menu chose last frame (its overlay ran after this view had drawn: nothing of the scene then).
        if (const int chosen = std::exchange(chosenEmitter_, -1); chosen >= 0) AddEmitters(host, particles::EffectPresets()[static_cast<size_t>(chosen)]);
        if (const int chosen = std::exchange(chosenModule_, -1); chosen >= 0) AddModule(host, chosen);
        Menus(c);
    }

    // The menus: drawn over everything, without the tab's clip (2026-11-17 on screen: drawn in the tab,
    // ＋ エミッター's was hidden under the preview and ＋ モジュール's cut off at the tab's bottom edge). They
    // only remember what was chosen - Draw acts on it in the next frame.
    void Menus(ui::Context& c) {
        if (!createMenu_.open && !emitterMenu_.open && !addMenu_.open) return;
        c.AddOverlay([this, &c] {
            const bool was = c.InputEnabled();
            c.SetInputEnabled(true);
            if (const int chosen = PresetMenu(c, createMenu_); chosen >= 0) chosenCreate_ = chosen;
            if (const int chosen = PresetMenu(c, emitterMenu_); chosen >= 0) chosenEmitter_ = chosen;
            if (addMenu_.open) {
                if (const int chosen = w::MenuPopup(c, addMenu_, AddMenuItems(), 240); chosen >= 0) chosenModule_ = chosen;
            }
            c.SetInputEnabled(was);
            if (chosenCreate_ >= 0 || chosenEmitter_ >= 0 || chosenModule_ >= 0) c.RequestRedraw();
        });
    }

    // ＋ モジュール's rows: the modules the engine has (in their order), then a file's, then a new file.
    static std::vector<w::MenuItem> AddMenuItems() {
        std::vector<w::MenuItem> items;
        for (const particles::BuiltinModule& b : particles::BuiltinModules()) {
            const ModuleSource* s = particles::FindBuiltin(b.id);
            items.push_back({Utf8ToWide(s ? TrU8(s->title) : std::string(b.id))});
        }
        items.push_back({{}, {}, true, true});
        items.push_back({Tr(L"ファイルのモジュール (.rmfx) …")});
        items.push_back({Tr(L"新しいモジュールを作る")});
        return items;
    }

private:
    struct Target {
        int entity = 0;
        AssetRef effect;
        std::string name;
    };

    static void Note(ui::Context& c, const ui::Rect& r, const std::wstring& text) {
        ui::TextStyle ts;
        ts.wrap = true;
        ts.color = theme::TextDim;
        ts.v = ui::VAlign::Top;
        c.P().Text(r, text, ts);
    }

    // The first selected entity with パーティクル.
    static std::optional<Target> FindTarget(const game_production::SceneViewHost& host) {
        const std::string type = reflect::TypeOf<ParticleSystem>().name;
        for (const int id : host.selection) {
            const game_production::SceneEntity* e = host.scene.Find(id);
            const game_production::Component* comp = e ? e->FindComponent(type) : nullptr;
            if (!comp || !comp->Known()) continue;
            return Target{id, static_cast<const ParticleSystem*>(comp->value.Get())->effect, e->data.name};
        }
        return std::nullopt;
    }

    // The effect as the editor has it now (read again only when its contents changed).
    void Load(game_production::SceneViewHost& host) {
        const auto bytes = host.host.AssetContent(ref_);
        if (!bytes) {
            effect_.reset();
            loadError_ = TrU8("ファイルが見つかりません");
            return;
        }
        if (effect_ && *bytes == bytes_) return;
        bytes_ = *bytes;
        std::string err;
        auto e = particles::ReadEffect(*bytes, &err);
        if (!e) {
            effect_.reset();
            loadError_ = err;
            return;
        }
        effect_ = std::make_shared<const ParticleEffect>(std::move(*e));
    }

    // A new file Assets/Particles/<the entity's name>.rmparticle (the sample effect), put in the
    // component's エフェクト (one undo entry; the file stays).
    // The presets (炎・煙 ...) to choose from; the one chosen (-1: none yet).
    static int PresetMenu(ui::Context& c, w::Popup& popup) {
        if (!popup.open) return -1;
        std::vector<w::MenuItem> items;
        for (const particles::EffectPreset& p : particles::EffectPresets()) items.push_back({Utf8ToWide(TrU8(p.title))});
        return w::MenuPopup(c, popup, items, 220);
    }

    // A preset's effect, its emitters' names in the screen's language.
    static ParticleEffect Made(const particles::EffectPreset& preset) {
        ParticleEffect e = preset.make();
        for (ParticleEmitter& em : e.emitters) em.name = TrU8(em.name);
        return e;
    }

    void CreateEffect(game_production::SceneViewHost& host, const Target& t, const particles::EffectPreset& preset) {
        const fs::path dir = host.host.GetProject().AssetsDir() / L"Particles";
        std::error_code ec;
        fs::create_directories(dir, ec);
        std::wstring base = Utf8ToWide(t.name.empty() ? std::string("Effect") : t.name);
        for (wchar_t& ch : base) {
            if (ch < 32 || std::wstring_view(L"\\/:*?\"<>|").find(ch) != std::wstring_view::npos) ch = L'_';
        }
        fs::path file;
        for (int n = 1; n < 1000; ++n) {
            file = dir / ((n == 1 ? base : std::format(L"{} {}", base, n)) + Utf8ToWide(particles::kEffectExtension));
            if (!fs::exists(file, ec)) break;
        }
        std::string err;
        if (!platform::WriteFileAtomic(file, particles::WriteEffect(Made(preset)), &err)) {
            log::Error("パーティクルのエフェクトを作れません ({}): {}", PathToUtf8(file.wstring()), err);
            return;
        }
        const AssetRef ref = host.host.AssetOf(file);
        if (ref.Empty()) {
            log::Warn("パーティクルのエフェクトを作りましたが、アセットとして読めません: {}", PathToUtf8(file.wstring()));
            return;
        }
        const reflect::TypeDesc& type = reflect::TypeOf<ParticleSystem>();
        for (size_t i = 0; i < type.fields.size(); ++i) {
            if (std::string_view(type.fields[i].name) != "effect") continue;
            host.host.History().Push(std::make_unique<reflect::SetFieldCommand>(type, reflect::FieldPath{i}, json::Value(std::string()), json::Value(ref.guid),
                                                                               host.component(t.entity, type.name), "エフェクトを作る", 0),
                                     false);
        }
        log::Info("パーティクルのエフェクトを作りました: {}", PathToUtf8(file.wstring()));
    }

    // A preset's emitters at the end of the effect (a fire with smoke: two).
    void AddEmitters(game_production::SceneViewHost& host, const particles::EffectPreset& preset) {
        const ParticleEffect add = Made(preset);
        EditEffect(host.host, ref_, "エミッターを足す", 0, [&add](ParticleEffect& e) { e.emitters.insert(e.emitters.end(), add.emitters.begin(), add.emitters.end()); });
        emitter_ = effect_ ? effect_->emitters.size() : 0;
        module_ = -1;
    }

    void InspectEmitter(game_production::SceneViewHost& host, size_t index) {
        auto s = std::make_shared<EmitterInspect>();
        s->host = &host.host;
        s->ref = ref_;
        s->index = index;
        host.host.Inspect(EmitterInspectable(std::move(s)));
    }

    // The effect playing over a floor, from the start again whenever its modules change.
    void DrawPreview(ui::Context& c, const ui::Rect& r, game_production::SceneViewHost& host) {
        platform::Gfx& gfx = host.host.Graphics();
        if (gfx.Generation() != generation_) {
            player_ = std::make_unique<particles::EffectPlayer>();
            generation_ = gfx.Generation();
        }
        std::vector<space3d::ExtraDraw> extras;
        if (particles::CanRunParticles(gfx.D3D())) {
            const double now = c.Time();
            const float dt = drawnAt_ < 0 ? 0.f : static_cast<float>(std::clamp(now - drawnAt_, 0.0, 0.1));
            drawnAt_ = now;
            player_->Set(effect_, [&](const ParticleModule& m, std::string& file) { return Source(host, m, file); });
            static constexpr float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            player_->Advance(gfx.D3D(), gfx.D3DContext(), kIdentity, dt);
            for (const std::string& p : player_->TakeProblems()) {
                if (told_.insert(p).second) log::Warn("{}", p);
            }
            extras.push_back([this, &host, &gfx](const space3d::ExtraDrawContext& dc) {
                player_->Draw(dc.ctx, ViewOf(dc), [&](const AssetRef& ref) {
                    return images_.Find(ref, gfx, [&host](const AssetRef& r) { return host.host.AssetContent(r); }, true);
                });
            });
            c.RequestAnimation();
        }
        if (!preview_.Draw(c, r, gfx, "particles.preview", {{}, extras})) Note(c, r.Inset(12), Tr(L"3D の描画を始められませんでした。くわしくはコンソールを見てください。"));
    }

    // The emitters and their modules (from the top: they run in this order).
    void DrawList(ui::Context& c, const ui::Rect& r, game_production::SceneViewHost& host) {
        float content = 0;
        for (const ParticleEmitter& e : effect_->emitters) content += (kRow + 2) * (2 + static_cast<float>(e.modules.size())) + 6;
        const float scroll = w::BeginScroll(c, "particles.list", r, content);
        float y = r.y - scroll;
        ui::TextStyle ts;
        ts.color = theme::Text;
        const auto& emitters = effect_->emitters;
        for (size_t i = 0; i < emitters.size(); ++i) {
            const ParticleEmitter& e = emitters[i];
            const std::string key = std::format("particles.e{}", i);
            const ui::Rect row{r.x, y, r.w, kRow};
            const auto res = w::Selectable(c, key, {row.x, row.y, row.w - kRow, row.h}, emitter_ == i && module_ < 0);
            ts.bold = true;
            c.P().Text(row.Inset(8, 0, kRow + 4, 0), e.enabled ? Utf8ToWide(e.name) : TrF(L"{} (使わない)", Utf8ToWide(e.name)), ts);
            ts.bold = false;
            if (res.clicked) {
                emitter_ = i;
                module_ = -1;
                InspectEmitter(host, i);
            }
            if (w::IconButton(c, key + ".remove", {row.Right() - kRow, row.y, kRow, kRow}, w::Icon::Close)) {
                EditEffect(host.host, ref_, "エミッターを消す", 0, [i](ParticleEffect& ef) {
                    if (i < ef.emitters.size()) ef.emitters.erase(ef.emitters.begin() + static_cast<std::ptrdiff_t>(i));
                });
                module_ = -1;
                break;  // (the list changed under us: drawn again next frame)
            }
            y += kRow + 2;
            for (size_t j = 0; j < e.modules.size(); ++j) {
                const ParticleModule& m = e.modules[j];
                const std::string mkey = std::format("{}.m{}", key, j);
                const ui::Rect mrow{r.x + kIndent, y, r.w - kIndent, kRow};
                bool on = m.enabled;
                if (w::Checkbox(c, mkey + ".on", {mrow.x, mrow.y + 4, 18, 18}, on, {})) {
                    EditEffect(host.host, ref_, on ? "モジュールを使う" : "モジュールを外す", 0, [i, j, on](ParticleEffect& ef) {
                        if (i < ef.emitters.size() && j < ef.emitters[i].modules.size()) ef.emitters[i].modules[j].enabled = on;
                    });
                }
                const ui::Rect label{mrow.x + 24, mrow.y, mrow.w - 24 - 3 * kRow, kRow};
                if (w::Selectable(c, mkey, label, emitter_ == i && module_ == static_cast<int>(j)).clicked) {
                    emitter_ = i;
                    module_ = static_cast<int>(j);
                    if (m.builtin.empty()) InspectModule(host, i, j);
                }
                std::string file;
                const auto src = Source(host, m, file);
                const bool wrong = m.enabled && (!src || !src->errors.empty() || !player_->Error(i).empty());
                ts.color = wrong ? theme::Danger : m.enabled ? theme::Text : theme::TextFaint;
                c.P().Text(label.Inset(6, 0, 4, 0), Title(m, src.get(), file), ts);
                ts.color = theme::Text;
                const float bx = mrow.Right() - 3 * kRow;
                if (w::IconButton(c, mkey + ".up", {bx, mrow.y, kRow, kRow}, w::Icon::ChevronUp, j > 0)) Move(host, i, j, -1);
                if (w::IconButton(c, mkey + ".down", {bx + kRow, mrow.y, kRow, kRow}, w::Icon::ChevronDown, j + 1 < e.modules.size())) Move(host, i, j, 1);
                if (w::IconButton(c, mkey + ".remove", {bx + 2 * kRow, mrow.y, kRow, kRow}, w::Icon::Close)) {
                    EditEffect(host.host, ref_, "モジュールを消す", 0, [i, j](ParticleEffect& ef) {
                        if (i < ef.emitters.size() && j < ef.emitters[i].modules.size())
                            ef.emitters[i].modules.erase(ef.emitters[i].modules.begin() + static_cast<std::ptrdiff_t>(j));
                    });
                    if (emitter_ == i && module_ == static_cast<int>(j)) module_ = -1;
                }
                y += kRow + 2;
            }
            const ui::Rect add{r.x + kIndent, y, 150, kRow};
            if (w::Button(c, key + ".add", add, Tr(L"＋ モジュール"), w::ButtonKind::Ghost)) {
                addTo_ = i;
                addMenu_.OpenAt(add.x, add.Bottom());
            }
            y += kRow + 8;
        }
        w::EndScroll(c, "particles.list", r, content);
    }

    // ＋ モジュール's row `chosen` (AddMenuItems) done to the emitter it was opened for.
    void AddModule(game_production::SceneViewHost& host, int chosen) {
        if (effect_) {
            std::vector<std::string> ids;
            for (const particles::BuiltinModule& b : particles::BuiltinModules()) ids.emplace_back(b.id);
            if (chosen == static_cast<int>(ids.size()) + 1) {
                AddFileModule(host, addTo_, {});
            } else if (chosen == static_cast<int>(ids.size()) + 2) {
                CreateModule(host, addTo_);
            } else if (chosen >= 0 && static_cast<size_t>(chosen) < ids.size()) {
                const size_t to = addTo_;
                const std::string id = ids[static_cast<size_t>(chosen)];
                EditEffect(host.host, ref_, "モジュールを足す", 0, [to, id](ParticleEffect& ef) {
                    if (to >= ef.emitters.size()) return;
                    ParticleModule m;
                    m.builtin = id;
                    ef.emitters[to].modules.push_back(std::move(m));
                });
                emitter_ = to;
                module_ = effect_ && to < effect_->emitters.size() ? static_cast<int>(effect_->emitters[to].modules.size()) : -1;
            }
        }
    }

    // A module's source (built in, or its file as the editor has it - read again now and then).
    std::shared_ptr<const ModuleSource> Source(game_production::SceneViewHost& host, const ParticleModule& m, std::string& file) {
        return modules_.Find(
            m, file, [&host](const AssetRef& ref) { return host.host.AssetContent(ref); },
            [&host](const AssetRef& ref) { return PathToUtf8(host.host.AssetFile(ref).filename().wstring()); }, true);
    }

    static std::wstring Title(const ParticleModule& m, const ModuleSource* src, const std::string& file) {
        if (src && !src->title.empty()) return Utf8ToWide(m.builtin.empty() ? src->title : TrU8(src->title));
        if (!m.builtin.empty()) return Tr(L"(読めないモジュール)");
        return m.file.Empty() ? Tr(L"(ファイルを選んでいないモジュール)") : Utf8ToWide(file);
    }

    void InspectModule(game_production::SceneViewHost& host, size_t emitter, size_t module) {
        auto s = std::make_shared<ModuleInspect>();
        s->host = &host.host;
        s->ref = ref_;
        s->emitter = emitter;
        s->module = module;
        host.host.Inspect(ModuleInspectable(std::move(s)));
    }

    // A module of a file at the end of emitter `to` (none chosen yet: the インスペクター picks it).
    void AddFileModule(game_production::SceneViewHost& host, size_t to, const AssetRef& file) {
        const size_t at = effect_ && to < effect_->emitters.size() ? effect_->emitters[to].modules.size() : 0;
        EditEffect(host.host, ref_, "モジュールを足す", 0, [to, file](ParticleEffect& ef) {
            if (to >= ef.emitters.size()) return;
            ParticleModule m;
            m.file = file;
            ef.emitters[to].modules.push_back(std::move(m));
        });
        emitter_ = to;
        module_ = static_cast<int>(at);
        InspectModule(host, to, at);
    }

    // A new .rmfx in Assets/Particles/Modules (an example to start from), added to emitter `to` and opened.
    void CreateModule(game_production::SceneViewHost& host, size_t to) {
        const fs::path dir = host.host.GetProject().AssetsDir() / L"Particles" / L"Modules";
        std::error_code ec;
        fs::create_directories(dir, ec);
        fs::path file;
        for (int n = 1; n < 1000; ++n) {
            file = dir / (n == 1 ? std::wstring(L"NewModule.rmfx") : std::format(L"NewModule {}.rmfx", n));
            if (!fs::exists(file, ec)) break;
        }
        std::string err;
        if (!platform::WriteFileAtomic(file, kNewModule, &err)) {
            log::Error("モジュールを作れません ({}): {}", PathToUtf8(file.wstring()), err);
            return;
        }
        const AssetRef ref = host.host.AssetOf(file);
        if (ref.Empty()) {
            log::Warn("モジュールを作りましたが、アセットとして読めません: {}", PathToUtf8(file.wstring()));
            return;
        }
        log::Info("パーティクルのモジュールを作りました: {}", PathToUtf8(file.wstring()));
        AddFileModule(host, to, ref);
        OpenOutside(file);
    }

    void Move(game_production::SceneViewHost& host, size_t i, size_t j, int by) {
        EditEffect(host.host, ref_, "モジュールを動かす", 0, [i, j, by](ParticleEffect& ef) {
            if (i >= ef.emitters.size()) return;
            auto& mods = ef.emitters[i].modules;
            const size_t k = by < 0 ? j - 1 : j + 1;
            if (j < mods.size() && k < mods.size()) std::swap(mods[j], mods[k]);
        });
        if (emitter_ == i && module_ == static_cast<int>(j)) module_ += by;
    }

    // The chosen module's values (its `param`s: their labels, types and ranges are the module's).
    void DrawValues(ui::Context& c, const ui::Rect& r, game_production::SceneViewHost& host) {
        const ParticleModule* m = nullptr;
        if (emitter_ < effect_->emitters.size() && module_ >= 0 && static_cast<size_t>(module_) < effect_->emitters[emitter_].modules.size()) {
            m = &effect_->emitters[emitter_].modules[static_cast<size_t>(module_)];
        }
        if (!m) {
            Note(c, r, Tr(L"モジュールを選ぶと、その値をここで変えられます。エミッターの行を選ぶと、出す数や重ね方をインスペクターで変えられます。"));
            return;
        }
        std::string file;
        const auto src = Source(host, *m, file);
        ui::TextStyle ts;
        ts.color = theme::Text;
        ts.bold = true;
        c.P().Text({r.x, r.y, r.w - 110, kRow}, Title(*m, src.get(), file), ts);
        ts.bold = false;
        float top = r.y + kRow + 4;
        // A module of a file: open it (to write it), and what is wrong with it.
        if (m->builtin.empty()) {
            const fs::path path = m->file.Empty() ? fs::path() : host.host.AssetFile(m->file);
            if (!path.empty() && w::Button(c, "particles.open", {r.Right() - 104, r.y, 104, kRow}, Tr(L"ファイルを開く"))) OpenOutside(path);
            std::vector<std::wstring> problems;
            if (m->file.Empty()) problems.push_back(Tr(L"インスペクターの「モジュールのファイル」で .rmfx を選びます (アセットのパネルからドラッグもできます)"));
            else if (!src) problems.push_back(Tr(L"ファイルが見つかりません"));
            if (src) {
                for (const particles::ModuleError& e : src->errors) problems.push_back(ErrorText(e));
            }
            if (const std::string err = player_->Error(emitter_); !err.empty() && src && src->errors.empty()) problems.push_back(TrF(L"シェーダーを作れません: {}", Utf8ToWide(err)));
            if (!problems.empty()) {
                ui::TextStyle es;
                es.wrap = true;
                es.v = ui::VAlign::Top;
                es.color = theme::Danger;
                std::wstring all;
                for (const std::wstring& p : problems) all += (all.empty() ? L"" : L"\n") + p;
                const float h = std::min(r.Bottom() - top, 18.f * (2.f + static_cast<float>(problems.size())) + 40.f);
                c.P().Text({r.x, top, r.w, h}, all, es);
                top += h + 4;
            }
        }
        if (!src || !src->errors.empty()) return;
        ts.color = theme::TextDim;
        const float content = (kRow + 4) * static_cast<float>(src->params.size() + 1);
        const ui::Rect body{r.x, top, r.w, r.Bottom() - top};
        const float scroll = w::BeginScroll(c, "particles.values", body, content);
        float y = body.y - scroll;
        const float labelW = std::floor(body.w * 0.45f);
        for (const ModuleParam& p : src->params) {
            const ui::Rect row{body.x, y, body.w, kRow};
            c.P().Text({row.x, row.y, labelW - 6, row.h}, Utf8ToWide(TrU8(p.label.empty() ? p.name : p.label)), ts);
            std::array<float, 4> v = ValueOf(*m, p);
            const std::array<float, 4> was = v;
            const std::string key = std::format("particles.v.{}.{}.{}", emitter_, module_, p.name);
            const ui::Rect fields{row.x + labelW, row.y, row.w - labelW, row.h};
            bool changed = false;
            if (p.type == ParamType::Bool) {
                bool on = v[0] != 0;
                if (w::Checkbox(c, key, {fields.x, fields.y + 4, 18, 18}, on, {})) {
                    v[0] = on ? 1.f : 0.f;
                    changed = true;
                }
            } else {
                const int n = particles::Components(p.type);
                const float fw = (fields.w - 4 * static_cast<float>(n - 1)) / static_cast<float>(n);
                const float lo = p.hasRange ? p.min : -1e9f, hi = p.hasRange ? p.max : 1e9f;
                for (int k = 0; k < n; ++k) {
                    std::string& text = texts_[key + std::to_string(k)];
                    float value = v[static_cast<size_t>(k)];
                    if (w::NumberField(c, key + std::to_string(k), {fields.x + (fw + 4) * static_cast<float>(k), fields.y, fw, fields.h}, value, lo, hi, text)) {
                        if (p.type == ParamType::Int) value = std::round(value);
                        v[static_cast<size_t>(k)] = value;
                        changed = true;
                    }
                }
            }
            if (changed && v != was) {
                const size_t i = emitter_, j = static_cast<size_t>(module_);
                const std::string name = p.name;
                const uint64_t merge = std::hash<std::string>{}(key) | 1u;
                EditEffect(host.host, ref_, std::format("「{}」を変える", p.label.empty() ? p.name : p.label), merge, [i, j, name, v](ParticleEffect& ef) {
                    if (i >= ef.emitters.size() || j >= ef.emitters[i].modules.size()) return;
                    auto& values = ef.emitters[i].modules[j].values;
                    auto it = std::find_if(values.begin(), values.end(), [&](const ParticleValue& pv) { return pv.name == name; });
                    if (it == values.end()) it = values.insert(values.end(), ParticleValue{name});
                    it->x = v[0];
                    it->y = v[1];
                    it->z = v[2];
                    it->w = v[3];
                });
            }
            y += kRow + 4;
        }
        w::EndScroll(c, "particles.values", body, content);
    }

    AssetRef ref_;
    std::string bytes_;
    std::shared_ptr<const ParticleEffect> effect_;
    std::string loadError_;
    size_t emitter_ = 0;
    int module_ = -1;
    space3d::Preview3D preview_;
    std::unique_ptr<particles::EffectPlayer> player_ = std::make_unique<particles::EffectPlayer>();
    uint64_t generation_ = 0;
    double drawnAt_ = -1;
    std::map<std::string, std::string> texts_;  // the number fields' text while typed
    w::Popup addMenu_;
    w::Popup createMenu_, emitterMenu_;  // the presets: a new effect / its emitters added
    int chosenCreate_ = -1, chosenEmitter_ = -1, chosenModule_ = -1;  // what a menu chose (done at the next Draw)
    size_t addTo_ = 0;
    std::set<std::string> told_;
    ModuleFiles modules_;
    ParticleImages images_;
};

}  // namespace

std::unique_ptr<game_production::SceneView> CreateParticleTab() { return std::make_unique<ParticleTab>(); }

}  // namespace rm::game_x_particles
