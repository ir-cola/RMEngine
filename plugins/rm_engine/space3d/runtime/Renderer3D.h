#pragma once
// 3D 空間 - 描画: Direct3D 11 で 3D の線・光を受ける箱・モデルを、専用の絵 (テクスチャ) に描く。
// 最大 8 個の光 (Lighting.h) と、向きのある光 1 つの影で照らす。エディターはこの絵をパネル
// (シーンビュー) に出し、書き出したゲームも同じやり方で自分のウィンドウに描く。ゲームもエディターも
// 知らない。(空間とその描画は 1 つのプラグイン: どちらか片方だけを使うことはない - 2026-11-05 に決めた。)
//
//   space3d::Renderer r;   r.Init(device, &error);
//   auto robot = r.Upload(meshData, &error);          // 1 回だけ: 形は GPU に残る
//   space3d::RenderFrame f{width, height, view, projection, clearColor, lines, boxes};
//   f.meshes = draws;   f.shadows = true;             // MeshDraw{robot.get(), world, color, ..., materials}
//   ID3D11Texture2D* picture = r.Render(context, f);
//
// 色 (物理ベース。金属 / 粗さは glTF と同じ): 光は線形の光で計算し、各ピクセルの最後に画面の数値
// (sRGB) へ直す。面は、色 (描く色 x 部品の材質の色 x その絵)、金属度と粗さ (x 金属 / 粗さの絵)、
// 法線 (法線マップで曲げる。接線は位置と UV の画面上の変化から作る - 頂点に接線は持たない)、
// 発光 (足す) を持つ。光 1 つごとに、拡散の部分 (Lambert。金属にはない) とハイライト
// (GGX / Smith / Schlick) を出す。空はその両方を少しずつ出す。モデルの部品 (MeshPart) は、
// それぞれの材質 (DrawMaterial: 数値と GPU 上の絵 - UploadTexture) で 1 つずつ描く。色と発光の絵は
// sRGB (GPU が読むときに線形の光へ直す)。金属 / 粗さと法線の絵は数値として読む。選んだものは
// 縁の光 (MeshDraw::highlight) で示し、材質の色は変えない。
// 描く物に渡す色 (MeshDraw::color・Box::color・highlight・線・床の影) は UI と同じく画面の色。
// 材質の色は線形の光 (Mesh.h)。
//
// シェーダーは Renderer3D.cpp に HLSL で書き、レンダラーの起動時にコンパイルする (D3DCompile。
// Windows 10 / 11 なら必ずある)。GPU が対応していれば 4x マルチサンプリングで描く。
//
// 影: シャドウマップ - 影を落とすものを、まず光から見た深度の絵に描く (Lighting.h: 太陽の 1 枚は
// 影を落とすものに合わせるか、視点に沿ったカスケードにする。スポットは 1 枚、点光源は 6 枚)。
// 各ピクセルは、その絵を見て、自分と光の間に何かあるかを調べる。3 x 3 か所を、GPU の比較フィルター
// (PCF: ふちがやわらかい) で見る。
// 床 (y = 0) は見えない面で、落ちてきた影だけを映す。
//
// CPU のコスト (tools/bench3d): 同じメッシュ・部品・絵の描画は 1 回のインスタンス描画にまとめる
// (DrawOrder.h が並べ替える: シェーダー、絵、メッシュの順)。シャドウマップは、それぞれ自分の視界
// (光の、カスケードの: Frustum) で調べ、中身が変わったときだけ描き直す。動くもの
// (Box / MeshDraw::moving) があるマップは、動かないものの層を残しておき、動くものだけを描く前に
// それを敷き戻す。

#include "base/Math.h"
#include "platform/Image.h"
#include "rm_engine/space3d/runtime/DrawOrder.h"
#include "rm_engine/space3d/runtime/Lighting.h"
#include "rm_engine/space3d/runtime/Mesh.h"
#include "rm_engine/space3d/runtime/Primitives.h"
#include "rm_engine/space3d/runtime/SkyShader.h"
#include "rm_engine/space3d/runtime/TexturePixels.h"
#include "rm_engine/space3d/runtime/TextureSampling.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace rm::space3d {

struct LineVertex {
    Vec3 position;
    Color color;  // アルファで混ぜる (薄れていくグリッド)
};

// 原点中心の単位立方体 (-0.5 .. 0.5) を `world` で置き、1 方向から光を当てる。
struct Box {
    Mat4 world;
    Color color;
    bool castShadow = true;
    // 最近動いた (ドラッグした・Play で動いた): 動かないものの残した層の上に、シャドウマップへ描く。
    // ただのヒント - 付けなくても動くものは正しく描かれる。その物が入るマップが、全部描き直される
    // だけ。
    bool moving = false;
};

// モデルの形が GPU にあるもの (Renderer::Upload)。描く側が持っておく。持っている間だけ生きる
// (フレームの合間にレンダラーは要らない)。
class GpuMesh {
public:
    uint64_t serial = 0;              // 2 つのメッシュで同じになることはない (シャドウマップの中身はこれで見分ける)
    Vec3 min{0, 0, 0}, max{0, 0, 0};  // モデル自身の空間での範囲
    uint32_t indexCount = 0;
    std::vector<MeshPart> parts;      // 材質ごとに 1 つずつ描く (最低 1 つ)
    std::vector<Vec3> partCenters;    // 各部品の範囲の真ん中 (透ける部品はこれで並べる)

private:
    friend class Renderer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertices, indices;
};

// GPU 上の絵 (Renderer::UploadTexture)。
class GpuTexture {
public:
    uint32_t width = 0, height = 0;  // 絵の大きさ (一番大きいレベル。GPU にあるかどうかは問わない)
    bool srgb = false;  // 色として読む (sRGB -> 線形) か、数値として読む
    TextureSampling sampling;  // フィルタ・端の扱い・ストリーミング: 画像の取り込みの設定
    uint32_t levels = 0;       // ミップマップの数 (1: なし)
    mutable size_t bytes = 0;  // 今 GPU にある量。ミップマップ込み (遠くでストリーミングされている間は減る)
    TextureFormat format = TextureFormat::Rgba8;  // 保存の形式 (BC: 圧縮)
    uint64_t madeBy = 0;  // 作った Renderer::Id (別のもの - 作り直した GPU - では使えない)
    // ストリーミング: 今 GPU にある最初のレベル (0: 全部)。
    uint32_t TopLevel() const { return stream_ ? stream_->top : 0; }

private:
    friend class Renderer;
    mutable Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    // ミップマップ・ストリーミング (レンダラーのもの。描画スレッドで動く): 全レベルをここに持ち、
    // GPU には一番上 .. 最後のレベルを置く。`want`: 最近の描画が求めた一番細かいレベル。時刻は `wantAt`。
    struct Stream {
        std::shared_ptr<const TexturePixels> pixels;
        uint32_t top = 0;
        uint32_t want = 0;
        double wantAt = -1e9;
    };
    mutable std::unique_ptr<Stream> stream_;
};

// モデルの材質 1 つの塗り方 (GPU 上の MeshMaterial。絵が null なら数値だけ)。
// 絵は共有される: 材質は、描かれている間は自分の絵を生かしておく。その間にキャッシュが
// ファイルを読み直して古い絵を手放しても同じ。
using TextureRef = std::shared_ptr<const GpuTexture>;
struct DrawMaterial {
    Color color{1, 1, 1, 1};  // 線形の光
    TextureRef texture;       // 色 (sRGB)
    AlphaMode alpha = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;  // Mask: これより小さいと描かない
    float metallic = 0.f, roughness = 0.8f;
    TextureRef metalRough;  // 青: 金属、緑: 粗さ
    TextureRef normal;      // 接線空間
    float normalScale = 1.f;
    Color emissive{0, 0, 0, 1};  // 線形の光。1 より大きくてもよい
    TextureRef emission;         // sRGB
};

struct MeshDraw {
    const GpuMesh* mesh = nullptr;
    Mat4 world;
    Color color{1, 1, 1, 1};  // 全材質に掛ける (白: モデルのまま)
    bool castShadow = true;
    std::span<const DrawMaterial> materials;  // MeshPart::material で引く。ここに無い部品は白
    Color highlight{0, 0, 0, 0};              // 縁の光 (選択中): その色。アルファ = 強さ
    bool moving = false;                      // Box::moving と同じ
};

// 向きの地図 (RenderFrame::mapShapes) の形: 描かれず、その地図にだけ見える (天候: 雨や雪が
// 当たるコライダー - 箱・球・円柱 (ShapeOf)・モデル)。
struct MapShape {
    const GpuMesh* mesh = nullptr;
    Mat4 world;
    bool moving = false;  // Box::moving と同じ
};

// プラグインが全部の面に加える手 (RenderFrame::surface。RendererShaders.h): Surface(...) を
// 定義する HLSL (デバイスとテキストごとに 1 回コンパイルする) と、その数値 (cbuffer SurfaceData。
// 最大 kSurfaceData 個の float4)。
struct SurfaceTouch {
    std::string code;
    std::vector<float> data;
};

// プラグインが自分で絵に描くもの (パーティクル): 透ける部品の後、デバッグの線の前に、自分の
// シェーダーと状態で描く。絵のレンダーターゲットと深度バッファは結んである (MSAA。深度テストは
// オンで、深度は書かないこと)。変えたものは、終わった後で元に戻される。
struct ExtraDrawContext {
    ID3D11DeviceContext* ctx = nullptr;
    const Mat4& view;
    const Mat4& projection;
    Vec3 eye;
    uint32_t width = 0, height = 0;
    // シェーダーが読む絵の深度 (0 手前 .. 1 奥)。描いた後は、次の Render まで今回のフレームの深度が
    // 残る (パーティクルのコンピュートシェーダーが読む: 衝突)。GPU が読めないときは null。
    // samples > 1 のときはマルチサンプルのもの (Texture2DMS)。
    ID3D11ShaderResourceView* depth = nullptr;
    uint32_t samples = 1;
    // フレームの向きの地図 (RendererShaders.h の kDirectionMapHlsl: b6 と t8 に結び、終わったら外す)。
    // テクスチャが null: 無い (cbuffer を結んだとき、MapPast は何も邪魔していないと答える)。
    ID3D11Buffer* directionMapsCb = nullptr;
    ID3D11ShaderResourceView* directionMaps = nullptr;
};
using ExtraDraw = std::function<void(const ExtraDrawContext&)>;

// 空のシェーダーが GPU にあるもの (Renderer::MakeSky)。Renderer3D.cpp。
struct SkyProgram;

// 一番奥に描く空 (SkyShader.h): 他の何よりも先に絵全体へ描く。クリア色の代わりになる (その
// アルファは絵に残る)。
struct SkyDraw {
    std::shared_ptr<const SkyProgram> program;
    std::array<TextureRef, kSkyTextures> textures;  // sky0 .. sky5 (null: 無い - 黒として読む)
    SkyConstants constants{};  // invViewProj はレンダラーのもの (描くときに入れる)
};

struct RenderFrame {
    uint32_t width = 0, height = 0;  // ピクセル
    Mat4 view, projection;
    Color clear{0, 0, 0, 1};
    std::span<const LineVertex> lines;  // 2 つで 1 組: 0-1, 2-3, ...
    // 一番最後に、全部の上に描く組: 手前に何もない所は指定の色で、何かある所は薄く (Play 中の
    // ゲームの rm::Debug の線)。GPU へのコピーは 1 回、描画は 2 回。
    std::span<const LineVertex> debugLines;
    std::span<const Box> boxes;
    std::span<const MeshDraw> meshes;
    // 光 (Lighting.h: 最大 kMaxLights)。無いとき: 白い向きのある光が `lightDirection` の向きに
    // 進む (上から、少し手前から)。
    std::span<const Light> lights;
    Vec3 lightDirection{-0.45f, -1.f, 0.3f};  // (左手系: 前の右手系の向きを Z で写したもの)
    Ambient ambient;       // どの面も受ける光 (環境光)
    bool shadows = false;  // 光の影 (どの光に影があるかは Lighting.h が決める)
    Color shadowColor{0, 0, 0, 0.4f};  // 床の影 (アルファ: 濃さ)
    std::span<const ExtraDraw> extras;  // この順 (ExtraDraw)
    // モデルにかかる霧 (天候の土砂降り / 吹雪): rgb がその色 (線形)、a が濃さ (1 m あたり。
    // 距離を使って exp(-a x 距離) だけ残る。0: 無し)。RendererShaderText から作ったシェーダーでは
    // Fogged(colour, wpos) として使える。
    Color fog{0, 0, 0, 0};
    // プラグインが固体として描くもの (天候の、地面に積もった雪): モデルの直後に、深度テストをして
    // 深度も書く (透けるもの、後ろや前のものはその後になる)。ピクセルシェーダーには、モデルの
    // ライティングが結んだままになっている - b0 .. b2 (フレーム・光・影)、t0 / t5 と s0 (シャドウマップ)、
    // b5 (プラグインの面の数値)、b6 / t8 (向きの地図)。だから RendererShaderText から作った
    // シェーダーは Shade(...) で、モデルとまったく同じに光を当てられる。これらは結んだままにすること。
    // 他に変えたものは、終わった後で元に戻される。ここでは ExtraDrawContext::depth は null。
    std::span<const ExtraDraw> solids;
    const SkyDraw* sky = nullptr;       // null (またはプログラム無し): クリア色だけが奥にある
    // 向きの地図 (RendererShaders.h): `mapShapes` (描かない) の深度を、この向きそれぞれから見たもの
    // (最大 kDirectionMaps。地図 k は k 番目) - `surface` と `extras` が読む。
    std::span<const Vec3> mapDirections;
    std::span<const MapShape> mapShapes;
    // mapMovingOnly[k] が 0 でないとき、地図 k は動く形だけを見る (天候: 雪の中を歩くものの下側。
    // 下から見る)。mapDirections より短くてよい: 残りは全部を見る。
    std::span<const uint8_t> mapMovingOnly;
    const SurfaceTouch* surface = nullptr;  // プラグインが全部の面に加える手 (null: 無し)
};

class Renderer {
public:
    static constexpr uint32_t kShadowMapSize = 2048;

    bool Init(ID3D11Device* device, std::string* error);
    bool Ready() const { return shared_ != nullptr; }
    // Init のたびに違う (どのレンダラーでも): GpuTexture が madeBy として覚えるもの。
    uint64_t Id() const { return id_; }
    // 形は GPU に 1 回だけ送る。できないときは null (と *error に日本語)。
    std::shared_ptr<const GpuMesh> Upload(const MeshData& mesh, std::string* error);
    // 絵 (PixelOrder::Rgba) をミップマップ付きで GPU に 1 回だけ送る。できないときは null (と *error)。
    // srgb: 色の絵 (線形の光として読む)。false: 数値 (金属 / 粗さ、法線)。
    // sampling: その画像の取り込みの設定 (TextureSampling.h: くっきり / 滑らか、ラップ、ストリーミング)。
    std::shared_ptr<const GpuTexture> UploadTexture(const platform::Image& rgba, bool srgb, std::string* error, const TextureSampling& sampling = {});
    // ミップマップを作り終えたものでの同じ処理 (速い方: GPU へのコピーだけ)。ストリーミング: 今は
    // 小さいレベルだけを送り、Render が絵が近づくにつれて細かいレベルを足す (遠ざかると手放す)。
    std::shared_ptr<const GpuTexture> UploadTexture(const TexturePixels& pixels, std::string* error, const TextureSampling& sampling = {});
    // 空の HLSL 全体 (AssembleSky) から空のシェーダーを作る: デバイスとテキストごとに 1 回
    // コンパイルし、同じテキストなら同じプログラム (使われている間は、そのデバイスの全レンダラーが
    // 共有する)。作れないときは null と *error (コンパイラのメッセージ: 空のコードのファイルと行)。
    std::shared_ptr<const SkyProgram> MakeSky(const std::string& hlsl, std::string* error);
    // 単純な形 (Primitives.h: 箱、球、円柱 ...)。Init が GPU に置く。Init の前は null。
    const GpuMesh* ShapeOf(Shape shape) const;
    // この GPU が BC7 (フィーチャーレベル 11.0) を読めるか。読めないと、アルファ付きの色は BC3 で持つ。
    bool SupportsBC7() const;
    // フレームを描いて、できた絵を返す (B8G8R8A8。Direct2D で使える)。失敗したら null。
    // 絵は次の Render まで残る (大きさはフレームに合わせる)。
    ID3D11Texture2D* Render(ID3D11DeviceContext* ctx, const RenderFrame& f);

    // 最後の Render が箱とモデルをどうしたか。カリングは常にオン: カメラに全く映らない物
    // (View.h の BoxInView で、ワールドでの範囲を調べる) は描かない - が、影は落とす
    // (絵の中に落ちるかもしれない)。
    struct Stats {
        size_t things = 0;     // 渡された箱とモデル
        size_t drawn = 0;      // ... のうち絵に描いたもの (残りはカリングされた)
        size_t mapsDrawn = 0;  // 描いたシャドウマップ
        size_t mapsKept = 0;   // ... と描き直さなかったもの: すでに同じ中身だった (何も動かなかった)
        size_t layersDrawn = 0;  // 描き直した、残しておいた層 (動かないもの。動くものの下)
        size_t drawCalls = 0;    // GPU に出した描画の数 (インスタンス描画: 同じメッシュ・部品・絵が続くごとに 1 つ)
        size_t triangles = 0;    // ... とその三角形の数 (絵のパス)
        size_t textures = 0;     // 使っている別々の絵
        size_t textureBytes = 0; // ... 今 GPU にある量
        size_t streamed = 0;     // ... のうちストリーミングされているもの (ミップマップ・ストリーミング)
        size_t streamedCut = 0;  // ... と、今は一番細かいレベルを持たないもの (遠い)
    };
    // デバイスごと (そのデバイスの全レンダラーで共有: シーンビュー、カメラのプレビュー、
    // サムネイル): シェーダーと状態、立方体と床、定数バッファ、シャドウマップと各マップが今
    // 持っている中身。Renderer3D.cpp。
    struct Shared;
    const Stats& LastStats() const { return stats_; }

private:
    struct Item {
        const GpuMesh* mesh;
        const Mat4* world;
        Color color;
        bool castShadow;
        std::span<const DrawMaterial> materials;
        Color highlight;
        bool seen = true;  // カメラに映る範囲の中 (視錐台カリング)。どちらでもシャドウマップには描く
        Bounds bounds;     // ワールドでの範囲
        uint32_t thing = 0;          // things_ のどこにその数値があるか (float4 単位)
        uint32_t firstMaterial = 0;  // ... とその部品の材質。順に並ぶ
        bool moving = false;         // Box / MeshDraw::moving
        uint64_t hash = 0;           // そのメッシュと置き場所 (シャドウマップが覚えるもの)
        bool mapOnly = false;        // MapShape: 向きの地図にだけある
    };
    // 描画 1 つ: メッシュの三角形のいくつかと、物の数値と材質 (`slot`: draws_ での場所。
    // シェーダーがインスタンスのデータとして読む)。
    struct Cmd {
        const GpuMesh* mesh;
        uint32_t firstIndex, indexCount;
        uint32_t slot;
        const DrawMaterial* material;  // その絵 (null: 無し - シャドウマップ、床)
        uint32_t instances = 1;        // スロット slot .. slot + instances - 1: 1 回のインスタンス描画
    };

    bool Resize(uint32_t width, uint32_t height);
    bool LineCapacity(size_t vertices);
    // フレームの数値が GPU にあるもの (必要なら大きくする)。
    bool UploadFrame(ID3D11DeviceContext* ctx);
    uint32_t AddThing(const Mat4& world, Color color, Color highlight);
    uint32_t AddMaterial(const DrawMaterial& m);
    uint32_t AddDraw(uint32_t thing, uint32_t material);
    // cmds_ に描画を足す - 直前の描画の次のインスタンスなら (同じメッシュ・部品・絵で、次の
    // スロット)、それに足す。mergeFloor_ より前の描画とはまとめない。
    void Emit(const GpuMesh* mesh, uint32_t firstIndex, uint32_t indexCount, uint32_t slot, const DrawMaterial* material);
    // 別々の絵の組ごとの番号 (0: 無し)。DrawKey::material 用。
    uint32_t PictureSet(const DrawMaterial* material);
    // ミップマップ・ストリーミング: ストリーミングされる各絵が、このフレームで見えるもの
    // (画面での大きさ) のために要るレベルを、GPU に置く・(しばらくして) 手放す - 何かを描く前に行う。
    void Stream(const RenderFrame& f, const Vec3& eye);

    template <class T>
    using Ptr = Microsoft::WRL::ComPtr<T>;
    std::shared_ptr<Shared> shared_;
    Ptr<ID3D11Device> device_;
    uint64_t id_ = 0;
    Ptr<ID3D11Buffer> lineVb_;
    size_t lineCapacity_ = 0;
    // 全部の物と材質の数値と、全部の描画 (物、材質) を、1 フレームに 1 回書く。
    std::vector<std::array<float, 4>> things_;
    std::vector<uint32_t> draws_;  // 2 つで 1 組
    Ptr<ID3D11Buffer> thingsBuffer_, drawsBuffer_;
    Ptr<ID3D11ShaderResourceView> thingsView_;
    size_t thingsCapacity_ = 0, drawsCapacity_ = 0;
    std::vector<Item> items_;
    std::unordered_map<const GpuTexture*, uint32_t> streamSeen_;  // Stream: 各絵がこのフレームで見られている一番細かいレベル
    std::vector<Cmd> cmds_;
    size_t mergeFloor_ = 0;
    std::vector<DrawKey> keys_;
    std::vector<uint32_t> order_, casterOrder_, mapOrder_, mapMovingOrder_, still_, moving_;
    std::vector<std::pair<uint32_t, uint32_t>> refs_;  // 各キーの (item, part)
    struct PictureSetHash {
        size_t operator()(const std::array<const void*, 5>& a) const noexcept {
            size_t h = 0;
            for (const void* p : a) h = h * 1000003u ^ std::hash<const void*>()(p);
            return h;
        }
    };
    std::unordered_map<std::array<const void*, 5>, uint32_t, PictureSetHash> pictureSets_;  // このフレームの分 (絵 4 つ、サンプラーの種類)
    std::vector<std::pair<size_t, const MeshPart*>> seeThrough_;  // 最後に、奥から手前の順に描く (item, part)
    std::vector<Vec3> centers_;
    Stats stats_;

    UINT samples_ = 1;
    uint32_t width_ = 0, height_ = 0;
    Ptr<ID3D11Texture2D> picture_;      // 返す絵 (リゾルブ済み)
    Ptr<ID3D11Texture2D> multisample_;  // samples_ > 1 のとき、これに描く
    Ptr<ID3D11RenderTargetView> rtv_;
    Ptr<ID3D11Texture2D> depthTex_;
    Ptr<ID3D11DepthStencilView> dsv_;
    Ptr<ID3D11ShaderResourceView> depthSrv_;  // シェーダー用の深度 (ExtraDrawContext::depth)。null: 無し
};

}  // namespace rm::space3d

