#pragma once
// パーティクル - モジュールの元 (.rmfx): エンジンが読む独自の数行を足した HLSL。純粋 (文字を入れて
// データを出す): tests/TestParticles.cpp でテストする。
//
//   module "風で曲げる"                                  パーティクルタブでの名前 (なければファイル名)
//   param float  strength = 1.0 [0, 10];   // 強さ        モジュールのフィールド: 型、名前、初期値、
//   param float3 direction = (1, 0, 0);    // 向き        あれば [最小, 最大]、// の後ろがラベル
//   param color  tint = (1, 0.5, 0, 1);    // 色
//   attr  float  heat;                                   粒がそれぞれ持つ値 (p.heat)
//
//   void Spawn(inout Particle p, inout Random rnd) { ... }            粒が生まれる時に 1 度
//   void Update(inout Particle p, float dt, inout Random rnd) { ... } 毎フレーム
//
// param の型: float float2 float3 float4 color (色として見せる float4) int bool。コードの中では
// param を名前 (strength) で、attr を粒のフィールド (p.heat) として使う。`module`、`param`、`attr` の行は
// コードから取り除く (空行にする: コンパイラーの行番号がファイルのままになる)。

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace rm::particles {

enum class ParamType { Float, Float2, Float3, Float4, Color, Int, Bool };

// この型の param が数値いくつ分か (1〜4)。
int Components(ParamType type);

struct ModuleParam {
    std::string name;
    ParamType type = ParamType::Float;
    std::array<float, 4> value{};  // 初期値
    std::string label;             // の後ろ; 空: 名前
    bool hasRange = false;
    float min = 0, max = 0;
    int line = 0;  // 1 始まり
};

struct ModuleAttr {
    std::string name;
    std::string type;  // float float2 float3 float4 int uint
    int line = 0;
};

// エンジンが読む行の何が間違っているか (エディターは Tr で文にする; ログ用は Message())。
enum class ModuleErrorKind {
    BadLine,       // 読めない param / attr / module の行
    UnknownType,   // detail: 型
    BadName,       // detail: 名前 (a-z A-Z 0-9 _ 以外、または数字で始まる)
    SameName,      // detail: 名前 (同じ名前の param / attr が 2 つある)
    BadValue,      // detail: 書かれていたもの
    NoFunction,    // Spawn も Update も無い
};
struct ModuleError {
    int line = 0;  // 0: ファイル全体
    ModuleErrorKind kind = ModuleErrorKind::BadLine;
    std::string detail;
};
std::string Message(const ModuleError& e);  // 日本語 (ログ用)

struct ModuleSource {
    std::string title;  // `module "..."` (空: 無し)
    std::vector<ModuleParam> params;
    std::vector<ModuleAttr> attrs;
    bool hasSpawn = false, hasUpdate = false;
    std::string code;  // 残った HLSL (エンジンの行は空にしてある)
    std::vector<ModuleError> errors;
};

ModuleSource ParseModule(std::string_view text);

// この名前の param (null: 無い)。
const ModuleParam* FindParam(const ModuleSource& m, std::string_view name);

}  // namespace rm::particles
