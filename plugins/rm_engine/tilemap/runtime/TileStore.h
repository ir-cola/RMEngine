#pragma once
// タイルマップ - 層のマスの置き場。純粋 (I/O も描画もしない)。tests/TestTilemap.cpp でテストする。
// 設計: docs/TILEMAP.md 3-4。
//
// 保存の形 (TileChunk の並び: 区画ごとに、その 1024 マスのランレングスの文字) が**正**。TileStore は
// そこから広げた写し (区画ごとの uint32_t[1024] と、変えるたびに 1 上がる版の番号)。空の区画は持たない
// (保存の形にも出さない)。マスを変えたら、その区画の文字だけを書き直す (WriteTo)。
//
//   { "x": 0, "y": 0, "cells": "0*96,65537*12,65538,0*915" }   区画 (0, 0)。下の行から、左から右へ

#include "core/reflect/Reflect.h"
#include "rm_engine/tilemap/runtime/TileCell.h"

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace rm::tilemap {

RM_STRUCT(Label="区画")
struct TileChunk {
    RM_PROPERTY() int x = 0;
    RM_PROPERTY() int y = 0;
    RM_PROPERTY() std::string cells;  // 区画のマスのランレングス (core/paint/Runs.h)
};

// 区画の文字を、区画の並びのその場所に書く (空: 区画を消す。新しい区画は下の行から左から右の順の場所に)。
void SetChunkCells(std::vector<TileChunk>& chunks, int x, int y, std::string cells);
// 区画の並びの中の文字 (無い: 空)。
std::string ChunkCells(std::span<const TileChunk> chunks, int x, int y);

class TileStore {
public:
    struct Chunk {
        std::array<TileValue, kChunkCells> cells{};
        int filled = 0;         // 空でないマスの数
        uint64_t version = 0;   // 変えるたびに 1 上がる (描く束・後の当たりとナビが見る)
    };

    // 保存の形から作り直す (前の中身は捨てる)。`chunkSize` はファイルの区画の一辺 (今と違えば詰め直す)。
    void Load(std::span<const TileChunk> chunks, int chunkSize = kChunkSize);
    TileValue Get(Cell c) const;
    // 変わったら true。
    bool Set(Cell c, TileValue v);
    void Clear() { chunks_.clear(); }

    // 区画 (空でないものだけ、下の行から左から右の順)。
    const std::map<std::pair<int, int>, Chunk>& Chunks() const { return chunks_; }
    const Chunk* FindChunk(Cell chunk) const;
    size_t ChunkCount() const { return chunks_.size(); }
    size_t FilledCount() const;
    // 区画のランレングスの文字 (無い区画: 空)。
    std::string Encode(Cell chunk) const;
    // `which` の区画の文字を、保存の形 `chunks` に書き直す (空になった区画は消す。新しい区画は順に入れる)。
    void WriteTo(std::vector<TileChunk>& chunks, std::span<const Cell> which) const;
    // 保存の形を丸ごと作る (区画の順)。
    std::vector<TileChunk> Save() const;

private:
    static std::pair<int, int> Key(Cell chunk) { return {chunk.y, chunk.x}; }  // (下の行から、左から右の順)
    std::map<std::pair<int, int>, Chunk> chunks_;
    uint64_t nextVersion_ = 1;
};

}  // namespace rm::tilemap
