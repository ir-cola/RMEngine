#include "rm_engine/game_x_particles/editor/ParticleTab.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "core/reflect/Data.h"
#include "core/reflect/FieldCommand.h"
#include "editor/lang/Lang.h"
#include "editor/project/Project.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "platform/Shell.h"
#include "rm_engine/game_x_particles/runtime/ParticleDraw.h"
#include "rm_engine/game_x_particles/runtime/ParticleSystem.h"
#include "rm_engine/particles/runtime/BuiltinModules.h"
#include "rm_engine/particles/runtime/ParticleEffect.h"
#include "rm_engine/particles/runtime/ParticlePlayer.h"
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
#include <vector>

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

std::wstring Title(const ParticleModule& m) {
    if (const ModuleSource* s = particles::FindBuiltin(m.builtin)) return Utf8ToWide(TrU8(s->title));
    return Tr(L"(読めないモジュール)");
}

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
            if (w::Button(c, "particles.create", {box.x, box.y + 70, 180, 30}, Tr(L"エフェクトを作る"), w::ButtonKind::Primary)) CreateEffect(host, *target);
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
        if (w::Button(c, "particles.add_emitter", {top.Right() - 122, top.y, 122, top.h}, Tr(L"＋ エミッター"))) AddEmitter(host);

        const float previewH = std::max(120.f, (r.h - top.h - 3 * kPad) * 0.45f);
        const ui::Rect preview{r.x + kPad, top.Bottom() + 6, r.w - 2 * kPad, previewH};
        DrawPreview(c, preview, host);
        const float lowerY = preview.Bottom() + kPad;
        const float lowerH = r.Bottom() - kPad - lowerY;
        if (lowerH < 30) return;
        const float listW = std::floor((r.w - 3 * kPad) * 0.5f);
        DrawList(c, {r.x + kPad, lowerY, listW, lowerH}, host);
        DrawValues(c, {r.x + 2 * kPad + listW, lowerY, r.w - 3 * kPad - listW, lowerH}, host);
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
    void CreateEffect(game_production::SceneViewHost& host, const Target& t) {
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
        if (!platform::WriteFileAtomic(file, particles::WriteEffect(particles::NewEffect()), &err)) {
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

    void AddEmitter(game_production::SceneViewHost& host) {
        EditEffect(host.host, ref_, "エミッターを足す", 0, [](ParticleEffect& e) {
            ParticleEmitter add = particles::NewEffect().emitters[0];
            add.name = std::format("{} {}", TrU8("エミッター"), e.emitters.size() + 1);
            e.emitters.push_back(std::move(add));
        });
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
            player_->Set(effect_, &particles::BuiltinLookup);
            static constexpr float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            player_->Advance(gfx.D3D(), gfx.D3DContext(), kIdentity, dt);
            for (const std::string& p : player_->TakeProblems()) {
                if (told_.insert(p).second) log::Warn("{}", p);
            }
            extras.push_back([this](const space3d::ExtraDrawContext& dc) { player_->Draw(dc.ctx, ViewOf(dc)); });
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
                }
                ts.color = m.enabled ? theme::Text : theme::TextFaint;
                c.P().Text(label.Inset(6, 0, 4, 0), Title(m), ts);
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

        // The modules the engine has (in their order).
        if (addMenu_.open) {
            std::vector<w::MenuItem> items;
            std::vector<std::string> ids;
            for (const particles::BuiltinModule& b : particles::BuiltinModules()) {
                const ModuleSource* s = particles::FindBuiltin(b.id);
                items.push_back({Utf8ToWide(s ? TrU8(s->title) : std::string(b.id))});
                ids.emplace_back(b.id);
            }
            const int chosen = w::MenuPopup(c, addMenu_, items, 220);
            if (chosen >= 0 && static_cast<size_t>(chosen) < ids.size()) {
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
        const ModuleSource* src = m ? particles::FindBuiltin(m->builtin) : nullptr;
        if (!m || !src) {
            Note(c, r, m ? Tr(L"このモジュールは読めません (ファイルのモジュールは、まだ使えません)")
                         : Tr(L"モジュールを選ぶと、その値をここで変えられます。エミッターの行を選ぶと、出す数や重ね方をインスペクターで変えられます。"));
            return;
        }
        ui::TextStyle ts;
        ts.color = theme::Text;
        ts.bold = true;
        c.P().Text({r.x, r.y, r.w, kRow}, Utf8ToWide(TrU8(src->title)), ts);
        ts.bold = false;
        ts.color = theme::TextDim;
        const float content = (kRow + 4) * static_cast<float>(src->params.size() + 1);
        const ui::Rect body{r.x, r.y + kRow + 4, r.w, r.h - kRow - 4};
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
    size_t addTo_ = 0;
    std::set<std::string> told_;
};

}  // namespace

std::unique_ptr<game_production::SceneView> CreateParticleTab() { return std::make_unique<ParticleTab>(); }

}  // namespace rm::game_x_particles
