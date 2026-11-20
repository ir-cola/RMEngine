#pragma once
// 名前を付けて保存したパネルレイアウト (純粋 (I/O も描画もしない)。RMTests でテストする)。
//
// ソフトごとに 1 つの一覧で、そのソフトの現在のレイアウトと一緒に <project>/UserSettings/Layout.json に置く:
//   "named": [ { "name": "絵を描く", "layout": { "main": ..., "floating": [...] }, ...未知のキー }, ... ]
// 項目自身のフィールドは下の RM_STRUCT (リフレクションで保存・読み込み)。"layout" は
// ドッキングの木自身の形式 (Layout::ToJson)。このバージョンが知らないキーは書き戻す。
// 項目ですらない要素 (name が無い) は、見せずにそのまま残す。
//
// 名前は、前後の空白 (全角も) を除いた打った文字のまま、大文字小文字も区別して比べる。

#include "core/reflect/Data.h"
#include "editor/dock/DockLayout.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rm::editor::dock {

RM_STRUCT()
struct NamedLayoutInfo {
    RM_PROPERTY() std::string name;
};

class NamedLayouts {
public:
    struct Entry {
        NamedLayoutInfo info;
        json::Value layout;          // 保存したまま (Layout::ToJson)。使うときに初めて Layout にする
        reflect::Unknowns unknowns;  // 新しいバージョンのキー
    };
    enum class NameCheck { Ok, Empty, Taken };

    const std::vector<Entry>& Entries() const { return entries_; }
    bool Empty() const { return entries_.empty(); }
    const Entry* Find(std::string_view name) const;

    // 前後の空白を取る。空、または別の項目の名前 (`self` は除く) は断る。
    static std::string CleanName(std::string_view name);
    NameCheck Check(std::string_view name, std::string_view self = {}) const;
    // まだ使っていない名前: base、"base 2"、"base 3" ...
    std::string FreeName(std::string_view base) const;

    // `layout` を `name` で保存する: その名前の項目をその場で置き換える (未知のキーは残る)か、
    // 末尾に足す。名前が空なら false を返して何もしない。
    bool Put(std::string_view name, const Layout& layout);
    bool Rename(std::string_view from, std::string_view to);  // false: 無い名前 / 断られた名前
    bool Remove(std::string_view name);
    // 項目 `name` (未知のキーごと) を `to` の末尾へ移す - プロジェクトの一覧と共有の一覧の間で。
    // false (何も動かない): `from` に無い、または `to` に同じ名前がすでにある。
    static bool Move(NamedLayouts& from, std::string_view name, NamedLayouts& to);
    // 保存したレイアウトを、今あるパネルに合わせて整えたもの。nullopt: 無い、または読めない。
    std::optional<Layout> Get(std::string_view name, const std::vector<std::string>& knownPanels) const;

    json::Value ToJson() const;
    static NamedLayouts FromJson(const json::Value& v, reflect::Warnings* warnings = nullptr);

private:
    std::vector<Entry> entries_;
    std::vector<json::Value> others_;  // 項目ではない要素: そのまま書き戻す
};

}  // namespace rm::editor::dock
