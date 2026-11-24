#pragma once
// プラグインが作るアセットのファイルの種類 (中身はプラグインの RM_STRUCT 1 つをそのまま JSON にしたもの -
// ゲーム制作 × 3D のマテリアル .rmmat): アセットのパネルの右クリック「作成 ▸ <label>」で新しいファイルを
// 作り、そのファイルを選ぶとインスペクターで中身を編集できる。核はその中身の意味を知らない: 型の表
// (reflect) を辿って読み・書き・インスペクターを作るだけ (憲法 1)。
//
//   // プラグインの RegisterEditor で:
//   static const editor::AssetFileType kMaterial{".rmmat", kMaterialKind, &reflect::TypeOf<Material>, TrKey(L"マテリアル"),
//                                                TrKey(L"新しいマテリアル"), "Materials", TrKey(L"…")};
//   registry.Contribute(editor::kAssetFileTypePoint, kMaterial);
//
// 編集は 1 つずつソフトの履歴に積む (Ctrl+Z で前の中身に戻る。憲法 7)。中身はすぐにエディターの
// 持つもの (AssetLibrary::QueueContent / SoftwareHost::AssetContent: シーンビューがすぐ読む) になり、
// 編集が終わったとき (ボタンを離した・入力が済んだ) にファイルへ書く。知らないキーは書き戻す (憲法 6)。

#include "core/reflect/Reflect.h"

#include <string_view>

namespace rm::editor {

class PluginRegistry;

inline constexpr char kAssetFileTypePoint[] = "core.asset_file_type";

struct AssetFileType {
    const char* extension = nullptr;  // ".rmmat" (小文字)
    const char* kind = nullptr;       // ファイルの "kind" の値。違うファイルは編集しない (null: 見ない)
    const reflect::TypeDesc& (*type)() = nullptr;  // ファイル = この型の reflect::Save (新しいファイルは既定値)
    const wchar_t* label = nullptr;    // TrKey: 「作成」のメニューの項目と、インスペクターの見出し
    const wchar_t* newName = nullptr;  // TrKey: 新しいファイルの名前 (拡張子なし。使われていれば 2、3 …)
    // 何も選んでいないときに作る所 (Assets/ の下のフォルダ。無ければ作る)。null: Assets/ の一番上。
    // フォルダかファイルを選んでいれば、そのフォルダ (ファイルならそれがあるフォルダ) に作る。
    const char* folder = nullptr;
    const wchar_t* note = nullptr;  // TrKey: インスペクターの見出しの下の 1 行 (null: 決まった説明)
};

// そのファイル名 (拡張子で) の種類。なければ null。
const AssetFileType* FileTypeFor(const PluginRegistry& registry, std::string_view fileName);

}  // namespace rm::editor
