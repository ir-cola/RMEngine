#pragma once
// プラグインの RUNTIME の部分が互いに提供するもの - 書き出したゲーム (エディターを持たない) にも入る部分。
// ゲームのシステム (game_production::SystemInfo) とゲームが必要とする部品の型をここで提供するので、
// ゲームの exe とエディターの Play は同じ一覧を読む。
//
//   // plugins/<作者>/<名前>/runtime/Register.cpp
//   void RegisterRuntime(rm::RuntimeRegistry& r) {
//       static const game_production::SystemInfo kSpin{...};
//       r.Contribute(game_production::kSystemPoint, kSpin);
//   }
//   // plugins/RuntimePlugins.cpp に 1 回だけ並べる: {kPluginId, &RegisterRuntime}
//
// 読む側が拠点 (point) と項目の型を指名する。核はどちらも知らない (設計 RME_PLATFORM 4-f)。
// エディターの PluginRegistry (editor/plugin/Software.h) もこの 1 つ: ローダーはプラグインの
// RegisterRuntime を先に、次に RegisterEditor を、同じレジストリーに対して呼ぶ。

#include "core/PluginId.h"

#include <span>
#include <string>
#include <string_view>
#include <typeinfo>
#include <vector>

namespace rm {

template <class T>
struct Contributed {
    PluginId plugin;  // 提供した人
    const T* item;
};

class RuntimeRegistry {
public:
    // `item` はプログラムが終わるまで生きていること (static)。有効なプラグインだけが登録するので、
    // 一覧に出るのはその項目だけ。必須のものが先に登録される。
    template <class T>
    void Contribute(std::string_view point, const T& item) {
        contributions_.push_back({current_, std::string(point), &item, &typeid(T)});
    }
    // `point` の T 型の項目を、登録した順に (別の型の項目は飛ばす)。
    template <class T>
    std::vector<Contributed<T>> Contributions(std::string_view point) const {
        std::vector<Contributed<T>> out;
        for (const auto& c : contributions_) {
            if (c.point == point && *c.type == typeid(T)) out.push_back({c.plugin, static_cast<const T*>(c.item)});
        }
        return out;
    }

    // ローダーが各プラグインの登録の前後で使う。
    void BeginPlugin(const PluginId& id) { current_ = id; }
    void EndPlugin() { current_ = {}; }

protected:
    const PluginId& Current() const { return current_; }  // 今登録しているプラグイン

private:
    struct Contribution {
        PluginId plugin;
        std::string point;
        const void* item;
        const std::type_info* type;
    };
    PluginId current_;
    std::vector<Contribution> contributions_;
};

struct RuntimePlugin {
    PluginIdLiteral id;
    void (*registerRuntime)(RuntimeRegistry& registry);
};

// ランタイムの部分が何かを登録するプラグイン。plugins/RuntimePlugins.cpp で定義する
// (ランタイムの部分だけをインクルードするので、ゲームの exe がリンクできる)。
std::span<const RuntimePlugin> RuntimePlugins();

// `enabled` の各プラグインのうちランタイムの部分を持つものの登録を行う (必須のものが先 - 渡された順)。
// ゲームの exe は書き出したときのプラグインで、エディターのローダーは有効なプラグインで呼ぶ。
inline void RegisterRuntimeParts(RuntimeRegistry& registry, std::span<const PluginId> enabled) {
    for (const PluginId& id : enabled) {
        for (const RuntimePlugin& p : RuntimePlugins()) {
            if (p.id.Id() != id || !p.registerRuntime) continue;
            registry.BeginPlugin(id);
            p.registerRuntime(registry);
            registry.EndPlugin();
        }
    }
}

}  // namespace rm

// 書き出したゲームの生成 main がプラグインの登録を呼ぶときの C の名前 (editor/export:
// RuntimeEntryName = "rm_runtime__<作者>__<名前>")。プラグインの runtime/Register.cpp の
// RegisterRuntime の後に 1 回だけ書く:   RM_RUNTIME_ENTRY(rm_engine, game_x_3d, rm::game_x_3d::RegisterRuntime)
// ゲームは並べたプラグインだけをリンクするので、名前が出るのはそれだけ (誰からも名指しされない
// 静的ライブラリのオブジェクトは、リンクされない)。
#define RM_RUNTIME_ENTRY(author, name, fn)     extern "C" void rm_runtime__##author##__##name(::rm::RuntimeRegistry& registry) { fn(registry); }
