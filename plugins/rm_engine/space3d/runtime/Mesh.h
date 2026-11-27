#pragma once
// 3D 空間 - ファイルから読んだモデルの形: 頂点 (位置・法線・UV) と三角形。
// 純粋: バイト列は渡される (MeshFile.h がディスクから読む)。tests/TestSpace3d.cpp でテストする。
//
//   glTF 2.0: .gltf (JSON。バッファーは別ファイル、または data: URI として中に書く) と .glb
//             (1 ファイル)。既定のシーンのすべてのメッシュを、ノード (行列または T / R / S) で
//             配置して、モデル自身の空間の 1 つのメッシュにする。三角形、ストリップ、ファン。
//   OBJ:      v / vt / vn / f (ポリゴンは三角形に切る。負の数は末尾から数える)。
//   FBX:      バイナリと ASCII の 7.x - Fbx.h。
//
// マテリアル (金属 / ラフネスの PBR。glTF と同じ): glTF の pbrMetallicRoughness (baseColorFactor /
// Texture、metallicFactor、roughnessFactor、metallicRoughnessTexture)、normalTexture (+ scale)、
// emissiveFactor / Texture (+ KHR_materials_emissive_strength)、alphaMode。絵は、モデルの隣のファイル、
// .glb の中、または data: URI にある。OBJ の .mtl (mtllib / usemtl: Kd、d / Tr、Ns、Pr、Pm、
// Ke、map_Kd、map_Ke、norm / map_Bump / bump - 法線マップとして扱う、-bm はその強さ)。
// 三角形はマテリアルごとにパーツ (MeshPart: 1 パーツ 1 描画) に並べ替える。絵はここでは
// デコードしない (MeshImage がバイト列かファイル名を持つ): それには Windows (WIC) が要る - Texture.h を見る。
//
// 骨 (glTF の skins・JOINTS_n / WEIGHTS_n・animations -> MeshData::skin / clips。Skeleton.h):
//   - 骨の木はモデルに 1 つ (すべてのスキンの joints を合わせる)。骨の親は、骨でもある一番近い祖先の
//     ノード。親が先に並ぶ。骨の名前はノードの名前 (無ければ「骨 N」。同じ名前もそのまま)
//   - スキンのあるメッシュはノードの行列で置かない (glTF の決まり)。頂点はバインドしたときの形
//   - スキンのあるモデルの、スキンの無いメッシュは、一番近い祖先 (か自分) の骨にウェイト 1 で付ける
//     (剣・帽子が骨について動く)。どの骨の下にも無いものは、動かない骨 kStaticBoneName に付ける
//   - 1 つの頂点に骨が 5 本以上: 重い 4 本 (警告に数)。骨が kMaxBones 本を超える: 読めない
//   - アニメーションは骨のノードの translation / rotation / scale だけ (LINEAR / STEP / CUBICSPLINE)。
//     時刻 0 から最後のキーまでを kDefaultClipRate ごとにサンプルし直す
// まだ読まないもの: オクルージョンマップ、glTF のサンプラー (絵は全部繰り返す)、TEXCOORD_1、その他の
// マテリアル拡張、モーフターゲット (と weights のアニメーション)、骨でないノードのアニメーション、
// スパースアクセサー、Draco。
// 無い法線は三角形から作る (スムーズ)。三角形の表は、どちらの形式も同じく反時計回り。UV (0, 0) は
// 絵の左上で、glTF や Direct3D と同じ (OBJ の V は上向きなので、ひっくり返す)。
//
//   std::string error;
//   auto mesh = space3d::ParseMesh(".glb", bytes, readRelated, &error);   // nullopt: error が理由を言う (日本語)

#include "base/Math.h"
#include "rm_engine/space3d/runtime/Skeleton.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rm::space3d {

// スキンのあるモデルで、どの骨の下にも無いスキンの無いメッシュが付く骨 (休みの姿勢は単位行列で、
// クリップは動かさない)。要るときだけ骨の木の最後に足す。
inline constexpr std::string_view kStaticBoneName = "(骨の外)";

struct MeshVertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
};

// マテリアルを塗る絵: モデルの隣のファイル (`file`: モデルのフォルダからの相対、フォルダの区切りは
// '/'、パーセント記号はデコード済み)、またはモデルの中に書かれた絵のバイト列 (`bytes`: .glb の
// バッファー、data: URI)。まだデコードしない。
struct MeshImage {
    std::string file;
    std::vector<uint8_t> bytes;
    std::string name;  // メッセージ用: ファイル名、または画像の名前 / "画像 2"
};

// マテリアルのアルファの扱い (glTF の alphaMode): 使わない (普通はこれ。絵のアルファチャンネルは
// 無視する)、切り抜き (alphaCutoff 未満は穴)、または透過。
enum class AlphaMode : uint8_t { Opaque, Mask, Blend };

// パーツの見た目。色はリニアの光 (glTF の書き方どおり。OBJ の画面の色はそれに変える)。絵は
// MeshData::images の番号 (-1 = なし) で、頂点の UV で貼る:
//   色     = baseColor x 絵 (その rgb は画面の色: sRGB)
//   金属 / ラフネス x metallicRoughnessImage (glTF: 青 = 金属、緑 = ラフネス。リニア)
//   法線   = normalImage (接線空間、リニア)。その曲がりに normalScale を掛ける
//   発光   = emissive x emissiveImage (sRGB)。光に足す
struct MeshMaterial {
    std::string name;
    Color baseColor{1, 1, 1, 1};
    int image = -1;
    AlphaMode alpha = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    float metallic = 0.f;
    float roughness = 0.8f;
    int metallicRoughnessImage = -1;
    int normalImage = -1;
    float normalScale = 1.f;
    Color emissive{0, 0, 0, 1};  // rgb。1 より大きくてよい (アルファは使わない)
    int emissiveImage = -1;
};

// `indices` の三角形 [firstIndex, firstIndex + indexCount)。すべて 1 つのマテリアル (-1: なし -
// 白で描くので、描画の色が見える)。
struct MeshPart {
    uint32_t firstIndex = 0, indexCount = 0;
    int material = -1;
    bool operator==(const MeshPart&) const = default;
};

struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;  // 三角形 1 つにつき 3 個。表は反時計回り
    Vec3 min{0, 0, 0}, max{0, 0, 0};  // 境界 (モデル自身の軸に沿った、それを収める箱)
    std::vector<MeshPart> parts;      // `indices` を順に覆う。使われたマテリアルごとに 1 つ (最低 1 つ)
    std::vector<MeshMaterial> materials;
    std::vector<MeshImage> images;
    std::vector<std::string> warnings;  // 読めたが何かが足りなかった (OBJ の .mtl など) - 日本語
    // 骨のあるモデル (Skeleton.h): 頂点を骨で動かす。あるとき、頂点はバインドしたときの形 (モデルの空間)。
    std::optional<SkinData> skin;
    std::vector<Clip> clips;  // ファイルのアニメーション (glTF の animations・FBX のテイク)。骨は名前で指す
    // ブレンドシェイプ (Morph.h。表情)。名前の同じ形は 1 つ。morphDefaults: 形ごとの初めの重み (glTF の mesh.weights)。
    std::vector<MorphTarget> morphs;
    std::vector<float> morphDefaults;
};

// --- 1 つのモデルが読む間に使ってよい量 (どのリーダーも、場所を作る前に確かめる) ---
// 壊れた、または細工されたファイルは、中身よりはるかに多くを要求できる。データの無いアクセサーは、
// 数バイトで 5000 万個の頂点を名指しし、1 つのメッシュが 100 万個のノードで配置され、ノードは
// どの階層でも同じ子を 2 回名指しする (2^256 回の探索)。これらを超えると、モデルは理由つきで
// 「読めない」ことになる:
inline constexpr uint64_t kMaxModelVertices = 20'000'000;  // 完成したモデルの頂点数 (全パーツ)
inline constexpr uint64_t kMaxModelBytes = 2ull << 30;     // 読むのに使うメモリ (2 GB、おおよそで数える)
inline constexpr uint64_t kMaxModelSteps = 1'000'000;      // 歩いたノードとリンクの数 (glTF のノード、FBX のノード / リンク)

class ReadBudget {
public:
    // `cancel`: (別のスレッドが) 立てると、次に確かめるときに読むのをやめる。
    explicit ReadBudget(const std::atomic<bool>* cancel = nullptr, uint64_t maxVertices = kMaxModelVertices, uint64_t maxBytes = kMaxModelBytes,
                        uint64_t maxSteps = kMaxModelSteps)
        : cancel_(cancel), maxVertices_(maxVertices), maxBytes_(maxBytes), maxSteps_(maxSteps) {}
    // `vertices` 個の頂点と `bytes` バイトの場所。上限を超える、または読むのが取り消されたときは
    // false (と *error に日本語)。
    bool Take(uint64_t vertices, uint64_t bytes, std::string* error);
    // ノード / リンクをもう 1 つ歩いた (と、cancel の確認)。
    bool Step(std::string* error);
    uint64_t Vertices() const { return vertices_; }
    uint64_t Bytes() const { return bytes_; }

private:
    bool Cancelled(std::string* error) const;
    const std::atomic<bool>* cancel_;
    uint64_t maxVertices_, maxBytes_, maxSteps_;
    uint64_t vertices_ = 0, bytes_ = 0, steps_ = 0;
};

// モデルが指す別のファイル (.gltf のバッファー "model.bin"、OBJ の "model.mtl")。モデルのフォルダ
// からの相対パス (パーセント記号はデコード済み、フォルダの区切りは '/') で指す。読めない
// ときは nullopt。
using ReadRelated = std::function<std::optional<std::vector<uint8_t>>(std::string_view uri)>;

// ファイルの拡張子で (".gltf" / ".glb" / ".obj" / ".fbx"、大文字小文字は問わない)。`budget`: null = 上の上限。
// `fileAxes` / `fileUnits`: FBX のファイルに書いてある軸 / 単位に直すか (ParseFbx)。
std::optional<MeshData> ParseMesh(std::string_view extension, std::span<const uint8_t> bytes, const ReadRelated& read,
                                  std::string* error, ReadBudget* budget = nullptr, bool fileAxes = true, bool fileUnits = true);
// .gltf または .glb (先頭のバイトで見分ける)。
std::optional<MeshData> ParseGltf(std::span<const uint8_t> bytes, const ReadRelated& read, std::string* error, ReadBudget* budget = nullptr);
// `read` が .mtl ファイルを探す (null: マテリアル無し)。
std::optional<MeshData> ParseObj(std::string_view text, const ReadRelated& read, std::string* error, ReadBudget* budget = nullptr);
inline std::optional<MeshData> ParseObj(std::string_view text, std::string* error) { return ParseObj(text, nullptr, error); }

// 1 つの .mtl ファイルのマテリアル (folder: .mtl のある場所。モデルのフォルダからの相対 - "" や
// "mats/" - で、モデルから絵を探せるようにする)。知らない行は飛ばす。
std::vector<MeshMaterial> ParseMtl(std::string_view text, std::string_view folder, std::vector<MeshImage>& images);

// 色の成分を、リニアの光 (glTF の係数、レンダラーの光) と画面の数 (sRGB: ペイントツール、UI の色、
// .mtl) の間で変える。0..1。
float LinearToScreen(float linear);
float ScreenToLinear(float screen);

// 三角形をマテリアルで並べ替えたもの (安定: マテリアルの中の順序は保つ) と、各マテリアルの区間が
// どこかを言うパーツ。`materialOfTriangle` は三角形 1 つにつき 1 項目。
void SortByMaterial(MeshData& mesh, std::span<const int> materialOfTriangle);

// `part` の頂点と三角形を `out` に足す。`world` で配置する (法線はその逆転置行列で。反転する
// `world` は三角形を裏返して、表が反時計回りのままにする)。
// パーツ / マテリアルには触らない (リーダーが最後にマテリアルで並べ替える)。
void AppendPlaced(MeshData& out, const MeshData& part, const Mat4& world);

// どのインデックスも、ある頂点を指している (リーダーは読みながら確かめる。これは、使う前の、
// ほかで作ったメッシュ - プラグイン自前のもの - 用: Renderer::Upload)。false と *error。
bool IndicesInRange(const MeshData& mesh, std::string* error);

// 三角形からのスムーズな法線 (各三角形は面積の分だけ効く)。どの三角形にも属さない頂点は
// 上を向く。
void ComputeNormals(MeshData& mesh);
void ComputeBounds(MeshData& mesh);
// モデルの座標系: モデルのどの向きが、このエンジン (Y が上・左手系: 右・上・奥が +) の上 (+Y) と
// 奥 (+Z) になるか、モデルが左手系か。右手系なら右を反転する (鏡に映す)。
struct ModelBasis {
    Vec3 up{0, 1, 0};
    Vec3 depth{0, 0, -1};
    bool leftHanded = false;
};
// アセットの取り込みの設定 (core/asset の ModelImport) をかける: 座標系 (位置と法線。鏡に映すときは
// 三角形を裏返して表を保つ)、前後の反転 (Y のまわりに 180 度)、`scale` 倍。境界もそれに合わせる。
// 上と奥が同じ向きなら、Y が上・右手系 (+Y と -Z) として扱う。
void ApplyModelImport(MeshData& mesh, const ModelBasis& basis, bool turnAround, float scale);

// 読み込みの部品。それぞれ単独でテストする。
std::optional<std::vector<uint8_t>> DecodeBase64(std::string_view text);
std::string DecodePercent(std::string_view uri);  // "my%20model.bin" -> "my model.bin"

}  // namespace rm::space3d
