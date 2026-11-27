#pragma once
// エディターが部品の型をどう見せるか。部品のエディター側の部分 (型そのものと、ゲームにとっての
// 意味はランタイム側のもの: ComponentInfo, runtime/Components.h)。型を出すプラグインが
// RegisterEditor から出す (文字は Tr を通す。ゲームには Tr がない):
//
//   static const game_production::ComponentLook kTransformLook{.type = &reflect::TypeOf<space3d::Transform>,
//       .derivedType = ..., .derive = ..., .underive = ..., .ownLabel = TrKey(L"ローカル"), .derivedLabel = TrKey(L"ワールド"), .icon = "cube"};
//   registry.Contribute(game_production::kComponentLookPoint, kTransformLook);
//
// 見た目のない型はそのまま素朴に表示する (階層の「object」の絵、自分の値だけ)。

#include "core/reflect/Data.h"
#include "editor/inspector/Inspector.h"

#include <functional>
#include <optional>
#include <string>
#include <span>
#include <vector>

namespace rm::editor {
class SoftwareHost;
}

namespace rm::game_production {

class Scene;
struct GameSettings;

// PluginRegistry の口: 項目は ComponentLook (static)。
inline constexpr char kComponentLookPoint[] = "rm_engine.game_production.component_look";

// ComponentLook::fixes の行ができること (行にコピーして持たせる: 行はメニューが閉じた後に動く)。
struct ComponentFixHost {
    editor::SoftwareHost* host = nullptr;
    // 開いているシーン (部品も、シーンの設定も) を `change` で変える。Undo は `label` (UTF-8・日本語) の 1 件。
    std::function<void(std::string label, std::function<void(Scene& scene)> change)> edit;
};

struct ComponentLook {
    const reflect::TypeDesc& (*type)();  // 対象の部品の型
    // 省略可: 子の部品を見せる 2 つ目の方法 - その部品と、親すべての同じ部品から求めた値 (ワールドでの
    // Transform など)。インスペクターは欄の上に ownLabel / derivedLabel の切り替えを置く (子のときだけ。
    // 最上位では両方同じ)。始めは自分の値で見せる (ゲーム制作はその意味を知らない)。5 つ全部か、なしか。
    //   derive:   `chain` = 一番上の親の値が先頭、このエンティティの値が最後 (部品のない親は
    //             飛ばす) -> `out` (derivedType() の値)。
    //   underive: ユーザーが打った求めた値 -> `component` (chain の最後のコピー。それを
    //             求めるように変えたもの)。
    const reflect::TypeDesc& (*derivedType)() = nullptr;
    void (*derive)(std::span<const void* const> chain, void* out) = nullptr;
    void (*underive)(std::span<const void* const> chain, const void* derived, void* component) = nullptr;
    const wchar_t* ownLabel = nullptr;      // 例: ローカル (TrKey の文字列: 見せるときに訳す)
    const wchar_t* derivedLabel = nullptr;  // e.g. ワールド
    // この部品を持つエンティティの階層での絵 (核が知っている名前: "cube")。複数の部品が持つときは、
    // エンティティの部品の最後のものが勝つ。なければ "object"。
    const char* icon = nullptr;
    // 省略可: インスペクターで打つ部品の欄 (1 つのエンティティの、自分の値)。`after` はその欄を変えた
    // 部品、`before` は変える前のもの。プラグインは他の欄も、つじつまが合うように変えてよい (UI の配置:
    // 新しいアンカーでも物は画面の同じ所にある)。変わったものはすべて 1 つの Undo になる。`scene` は
    // 編集の前のもの。
    void (*adjust)(const Scene& scene, const GameSettings& settings, int entity, const reflect::FieldPath& path, const void* before,
                   void* after) = nullptr;
    // 省略可: 文字の欄の選択肢 (インスペクターがドロップダウンにする: inspector::Block::choices)。エンティティ
    // `entity` (`scene` は見せているもの: Play 中は動いているコピー) のこの部品の欄 `path` に、今選べる値を
    // 返す (アニメーター: 同じエンティティのモデルのクリップの名前)。nullopt: ふつうの文字の欄。
    // インスペクターを描くたびに呼ぶので、重いもの (ファイルを読む) は自分で覚えておく。
    std::optional<std::vector<editor::inspector::Choice>> (*choices)(editor::SoftwareHost& host, const Scene& scene, int entity,
                                                                    const reflect::FieldPath& path) = nullptr;

    // 省略可: ブロックの見出しの下に足す印 (ナビの動く者: 「キャラクターと体の形が違います」)。空: 何も足さない。描くたびに呼ぶ。
    std::wstring (*note)(editor::SoftwareHost& host, const Scene& scene, int entity) = nullptr;
    // 省略可: 欄の上の 1 行の「直す」(fixLabel() という名前の行と、これらのドロップダウン: 「キャラクターに合わせる」)。
    // 行は host.edit でシーン (と設定) を変える = Undo の 1 件。空: 行なし。Play 中は出さない。
    std::vector<editor::w::Action> (*fixes)(const ComponentFixHost& host, const Scene& scene, int entity) = nullptr;
    std::wstring (*fixLabel)() = nullptr;

    bool HasDerived() const { return derivedType && derive && underive && ownLabel && derivedLabel; }
};

}  // namespace rm::game_production
