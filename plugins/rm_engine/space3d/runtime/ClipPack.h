#pragma once
// 3D 空間 - クリップの圧縮 (docs/ANIMATION.md 7-3)。Clip::tracks のサンプルを PackedTrack (Skeleton.h) にする:
//   - 変わらない列は 1 つ (Constant)・動かない列は無し (Empty)
//   - 回転は 48 ビット (一番大きい成分を除いた 3 つを 15 ビットずつ + どれを除いたかの 2 ビット)
//   - 位置と大きさは列ごとの範囲 (lo .. lo + 65535 x step) で 16 ビット。範囲が大きすぎて誤差に収まらない列は float
//   - 誤差の中で直線で戻せるサンプルを消す (残すのが「鍵」)
// 誤差は骨ごとではなく骨の先の位置で測る: 骨の先 = その骨から一番遠い子孫の骨の場所 (休みの姿勢)。親の誤差は子に
// 積もるので、根から先までの鎖の骨の数で上限を分ける (鎖のどの先でも、足した誤差が上限の中)。
// メモリの中も圧縮のまま持ち、サンプルするときに前後 2 つの鍵だけ戻す (animation3d の Pose.cpp)。
// 純粋。tests/TestClipPack.cpp でテストする。

#include "rm_engine/space3d/runtime/Skeleton.h"

#include <cstddef>
#include <vector>

namespace rm::space3d {

// 骨の先で許す誤差 (m)。しない: 0 (圧縮しない)。
float PackTolerance(ClipCompression c);

// 回転の 48 ビット (3 つの 16 ビット)。戻した四元数は長さ 1・一番大きい成分が正 (q と -q は同じ回転)。
void PackQuat48(Quat q, uint16_t out[3]);
Quat UnpackQuat48(const uint16_t in[3]);

// `clip` を骨の先の誤差 `tolerance` (m) で圧縮する (tolerance <= 0・もう圧縮してある・frames が 65536 より多い:
// 何もしない)。骨は名前で `skeleton` から引く (無い骨: 先までを 1 m とする)。
void PackClip(Clip& clip, const Skeleton& skeleton, float tolerance);
// 全部のクリップを、それぞれの bake.compression で。
void PackClips(std::vector<Clip>& clips, const Skeleton& skeleton);

// 圧縮した列の、刻みの位置 `frame` (0 .. frames - 1。間は直線) の値。Empty: rest。
Vec3 SamplePackedVec(const PackedChannel& c, float frame, Vec3 rest);
Quat SamplePackedQuat(const PackedChannel& c, float frame, Quat rest);

// サンプルのメモリの大きさ (バイト)。ClipBytes: 今の形 (圧縮していれば圧縮したもの)。RawClipBytes: 圧縮しないときの大きさ
// (圧縮したクリップでも、列の長さから求める)。
size_t ClipBytes(const Clip& clip);
size_t RawClipBytes(const Clip& clip);

// 焼いたファイルから読んだ列が正しいか (鍵が 0 から frames - 1 まで増える順・数が合う)。違えば読み込みを止める。
bool PackedChannelValid(const PackedChannel& c, uint32_t frames, bool rotation);

}  // namespace rm::space3d
