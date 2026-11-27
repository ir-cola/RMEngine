// タイルマップ (plugins/rm_engine/tilemap と game_x_tilemap、docs/TILEMAP.md 4-6): マスの値・マス目と区画の計算・
// ランレングスの往復 (core/paint)・線と四角・層の置き場 (区画の文字を書き直す)・スタンプ・タイルセットの読み書き
// (知らないキーの書き戻し・番号の直し)・切り方 (余白と間)・層の部品の保存 / 読み込み・一番近いグリッド。

#include "Test.h"

#include "base/Json.h"
#include "core/paint/GridPaint.h"
#include "core/paint/Runs.h"
#include "core/reflect/Data.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_x_tilemap/runtime/TileLayer.h"
#include "rm_engine/space2d/runtime/Atlas.h"
#include "rm_engine/space2d/runtime/Transform2D.h"
#include "rm_engine/tilemap/runtime/TileCell.h"
#include "rm_engine/tilemap/runtime/TilePaint.h"
#include "rm_engine/tilemap/runtime/TileStore.h"
#include "rm_engine/tilemap/runtime/Tileset.h"

#include <chrono>
#include <cmath>
#include <set>

using namespace rm;
using namespace rm::tilemap;
using paint::Cell;

RM_TEST(TilemapCellValuePacksSourceColumnRowAndFlips) {
    const TileValue v = MakeTile(3, 7, 12, kFlipX | kFlipDiagonal);
    CHECK(SourceOf(v) == 3 && ColumnOf(v) == 7 && RowOf(v) == 12);
    CHECK(FlipsOf(v) == (kFlipX | kFlipDiagonal));
    CHECK(!IsEmpty(v));
    CHECK(MakeTile(1, 0, 0) == 65536u);  // (設計図の例: 絵の元 1 の 0 列 0 行 = 65536)
    CHECK(IsEmpty(0) && IsEmpty(kFlipX));  // (絵の元 0 = 空)
    CHECK(MakeTile(0, 1, 1) == 0 && MakeTile(256, 0, 0) == 0 && MakeTile(1, 256, 0) == 0 && MakeTile(1, 0, -1) == 0);  // (範囲の外: 空)
}

RM_TEST(TilemapCellAtFloorsNegativeAndUsesCellSize) {
    CHECK((CellAt({0.5f, 0.5f}, {1, 1}) == Cell{0, 0}));
    CHECK((CellAt({-0.01f, -1.f}, {1, 1}) == Cell{-1, -1}));
    CHECK((CellAt({3.9f, -0.5f}, {2, 0.5f}) == Cell{1, -1}));
    CHECK((CellAt({1, 1}, {0, 0}) == Cell{1, 1}));  // (大きさ 0: 1 として)
    CHECK((CellMin({-2, 3}, {0.5f, 2}) == Vec2{-1, 6}) && (CellMax({-2, 3}, {0.5f, 2}) == Vec2{-0.5f, 8}));
    CHECK((CellCenter({0, -1}, {1, 1}) == Vec2{0.5f, -0.5f}));
    const CellRange r = CellsIn({2.5f, -0.5f}, {-0.5f, 1.5f}, {1, 1});  // (角の順はどちらでも)
    CHECK((r.min == Cell{-1, -1}) && (r.max == Cell{2, 1}));
}

RM_TEST(TilemapChunkOfAndIndexInChunk) {
    CHECK((ChunkOf({0, 0}) == Cell{0, 0}) && (ChunkOf({31, 31}) == Cell{0, 0}) && (ChunkOf({32, 0}) == Cell{1, 0}));
    CHECK((ChunkOf({-1, -1}) == Cell{-1, -1}) && (ChunkOf({-32, -33}) == Cell{-1, -2}));
    CHECK(IndexInChunk({0, 0}) == 0 && IndexInChunk({1, 0}) == 1 && IndexInChunk({0, 1}) == 32);  // (下の行から、左から右へ)
    CHECK(IndexInChunk({-1, -1}) == 1023 && IndexInChunk({-32, -32}) == 0);
    for (const Cell c : {Cell{5, 7}, Cell{-1, -40}, Cell{100, -3}}) CHECK((CellOfIndex(ChunkOf(c), IndexInChunk(c)) == c));
}

RM_TEST(TilemapRunsRoundTripLargeValues) {
    const std::vector<uint32_t> cells{0, 0, 65537, 65537, 65537, 0xFFFFFFu | kFlipX, 0};
    const std::string text = paint::EncodeRuns(std::span<const uint32_t>(cells));
    CHECK(text == "0*2,65537*3," + std::to_string(0xFFFFFFu | kFlipX) + ",0");
    CHECK(paint::DecodeRunsU32(text, cells.size()) == cells);
    CHECK(paint::DecodeRunsU32("65536*2", 4) == (std::vector<uint32_t>{65536, 65536, 0, 0}));  // (足りない分は 0)
    CHECK(paint::DecodeRunsU32("99999999999,7", 2) == (std::vector<uint32_t>{0, 7}));     // (32 ビットに入らない: 0)
    CHECK(paint::DecodeRuns("5*2,-3", 3) == (std::vector<int>{5, 5, 0}));                // (int の方も同じ 1 本)
}

RM_TEST(TilemapLineCellsHaveNoGaps) {
    const std::vector<Cell> line = paint::LineCells({0, 0}, {5, -2});
    CHECK((line.front() == Cell{0, 0}) && (line.back() == Cell{5, -2}));
    for (size_t i = 1; i < line.size(); ++i) {
        CHECK(std::abs(line[i].x - line[i - 1].x) <= 1 && std::abs(line[i].y - line[i - 1].y) <= 1);  // (隣のマスへ 1 歩ずつ)
    }
    CHECK(line.size() == 6);
    CHECK((paint::LineCells({3, 3}, {3, 3}) == std::vector<Cell>{{3, 3}}));
}

RM_TEST(TilemapRectCellsAndOutlineAndLimit) {
    CHECK(paint::RectCells({1, 1}, {0, 0}).size() == 4);
    CHECK(paint::RectCells({0, 0}, {3, 2}, true).size() == 10);  // (4 x 3 の縁: 12 - 2)
    CHECK(paint::RectCount({0, 0}, {999, 999}) == 1'000'000);
    CHECK(paint::RectCells({0, 0}, {1000, 1000}).empty());  // (上限を超える: 塗らない)
}

RM_TEST(TilemapGridCutWithMarginAndSpacing) {
    const space2d::GridCut cut{16, 16, 1, 2};
    // 1 + 16 + 2 + 16 + 2 + 16 + 1 = 54 ピクセルの幅に 3 つ。
    CHECK((space2d::GridCountOf(54, 37, cut) == space2d::GridCount{3, 2}));
    CHECK((space2d::GridCellOf(54, 37, cut, 1, 1) == space2d::PixelRect{19, 19, 16, 16}));
    CHECK(space2d::GridCellOf(54, 37, cut, 3, 0).w == 0);   // (範囲の外: 空)
    CHECK((space2d::GridCountOf(10, 10, cut) == space2d::GridCount{0, 0}));  // (1 つも入らない)
    CHECK((space2d::GridCountOf(64, 32, {16, 16, 0, 0}) == space2d::GridCount{4, 2}));
}

RM_TEST(TilemapStoreSetGetAndChunksStaySparse) {
    TileStore s;
    CHECK(s.Set({0, 0}, MakeTile(1, 2, 3)));
    CHECK(!s.Set({0, 0}, MakeTile(1, 2, 3)));  // (同じ: 変わらない)
    CHECK(s.Set({-1, 40}, MakeTile(2, 0, 0)));
    CHECK(s.Get({0, 0}) == MakeTile(1, 2, 3) && s.Get({-1, 40}) == MakeTile(2, 0, 0) && s.Get({5, 5}) == 0);
    CHECK(s.ChunkCount() == 2 && s.FilledCount() == 2);
    const uint64_t before = s.FindChunk({0, 0})->version;
    s.Set({1, 0}, MakeTile(1, 0, 0));
    CHECK(s.FindChunk({0, 0})->version > before);  // (変えるたびに版が上がる)
    s.Set({0, 0}, 0);
    s.Set({1, 0}, kFlipX);  // (反転のビットだけ: 空)
    CHECK(s.ChunkCount() == 1 && !s.FindChunk({0, 0}));  // (空の区画は持たない)
}

RM_TEST(TilemapStoreWritesOnlyTouchedChunksInOrder) {
    std::vector<TileChunk> saved{{0, 0, "0*1024"}, {5, 5, "65536*1024"}};
    TileStore s;
    s.Load(saved);
    CHECK(s.ChunkCount() == 1);  // (全部空の区画は読まない)
    s.Set({-1, -1}, MakeTile(1, 1, 0));
    s.Set({0, 0}, MakeTile(1, 0, 0));
    const std::vector<Cell> touched{{-1, -1}, {0, 0}};
    s.WriteTo(saved, touched);
    CHECK(saved.size() == 3);
    CHECK(saved[0].x == -1 && saved[0].y == -1);  // (新しい区画は下の行から、左から右の順の場所に)
    CHECK(saved[1].x == 0 && saved[1].y == 0 && saved[1].cells == "65536,0*1023");
    CHECK(saved[2].x == 5 && saved[2].cells == "65536*1024");  // (触っていない区画はそのまま)
    s.Set({0, 0}, 0);
    s.WriteTo(saved, std::vector<Cell>{{0, 0}});
    CHECK(saved.size() == 2 && ChunkCells(saved, 0, 0).empty());  // (空になった区画は消す)
    TileStore again;
    again.Load(saved);
    CHECK(again.Get({-1, -1}) == MakeTile(1, 1, 0) && again.Get({5 * 32 + 3, 5 * 32 + 31}) == 65536u);
}

RM_TEST(TilemapStoreRepacksOtherChunkSizes) {
    // 将来の 16 x 16 の区画のファイル: 今の 32 の区画に詰め直して読む。
    std::vector<TileChunk> saved{{-1, 1, "65537,0*255"}};
    TileStore s;
    s.Load(saved, 16);
    CHECK(s.Get({-16, 16}) == 65537u && s.ChunkCount() == 1 && s.FilledCount() == 1);
}

RM_TEST(TilemapStampsPlaceFillPickAndErase) {
    TileStore s;
    ChunkSet touched;
    const Stamp two = StampOfTiles(1, 4, 2, 2, 1);  // (横に 2 つ: 4 列と 5 列)
    CHECK(PlaceStamp(s, {31, 10}, two, touched));
    CHECK(s.Get({31, 10}) == MakeTile(1, 4, 2) && s.Get({32, 10}) == MakeTile(1, 5, 2));
    CHECK(touched.size() == 2);  // (区画の境目をまたいだ)
    const Stamp tall = StampOfTiles(1, 0, 0, 1, 2);  // (縦に 2 つ: 上が 0 行、その下が 1 行)
    PlaceStamp(s, {0, 0}, tall, touched);
    CHECK(s.Get({0, 0}) == MakeTile(1, 0, 0) && s.Get({0, -1}) == MakeTile(1, 0, 1));
    // 四角を 2 x 2 のスタンプで敷き詰める (左上がスタンプの左上)。
    TileStore r;
    ChunkSet t2;
    CHECK(FillRect(r, {0, 0}, {2, -2}, StampOfTiles(1, 0, 0, 2, 2), false, t2));
    CHECK(r.Get({0, 0}) == MakeTile(1, 0, 0) && r.Get({1, 0}) == MakeTile(1, 1, 0) && r.Get({2, 0}) == MakeTile(1, 0, 0));
    CHECK(r.Get({0, -1}) == MakeTile(1, 0, 1) && r.Get({0, -2}) == MakeTile(1, 0, 0));
    CHECK(r.FilledCount() == 9);
    TileStore o;
    FillRect(o, {0, 0}, {3, 3}, StampOfTiles(1, 0, 0, 1, 1), true, t2);
    CHECK(o.FilledCount() == 12 && o.Get({1, 1}) == 0);  // (枠だけ)
    // スポイト: 2 つの角の間 (上の行から)。空のマスは 0 で、置くときは下を残す。
    const Stamp picked = PickStamp(r, {0, 0}, {1, -1});
    CHECK(picked.width == 2 && picked.height == 2 && picked.At(0, 0) == MakeTile(1, 0, 0) && picked.At(1, 1) == MakeTile(1, 1, 1));
    Stamp holes = PickStamp(s, {0, 0}, {1, 0});  // ({1, 0} は空)
    TileStore u;
    u.Set({11, 5}, MakeTile(2, 0, 0));
    ChunkSet t3;
    PlaceStamp(u, {10, 5}, holes, t3);
    CHECK(u.Get({10, 5}) == MakeTile(1, 0, 0) && u.Get({11, 5}) == MakeTile(2, 0, 0));
    // 消す。
    CHECK(EraseRect(r, {0, 0}, {2, -2}, false, t2) && r.FilledCount() == 0);
    CHECK(!EraseAt(r, {0, 0}, 1, 1, t2));  // (もう空: 変わらない)
}

RM_TEST(TilemapTilesetKeepsUnknownKeysAndFixesIds) {
    const std::string text =
        R"({"kind": "rm_engine.tileset", "format": 1, "future": {"a": 1},)"
        R"( "sources": [{"id": 3, "name": "草原", "tileWidth": 32, "later": true}, {"id": 3, "name": "洞窟"}, {"id": 0}]})";
    reflect::Unknowns u;
    std::string err;
    auto t = ReadTileset(text, &err, &u);
    CHECK(t.has_value());
    if (!t) return;
    CHECK(t->sources.size() == 3 && t->sources[0].id == 3 && t->sources[0].tileWidth == 32);
    CHECK(t->sources[1].id == 1 && t->sources[2].id == 2);  // (重なり・範囲の外: 使っていない一番小さい番号に)
    CHECK(NewSourceId(*t) == 4 && FindSource(*t, 2) == &t->sources[2]);
    const auto back = json::Parse(WriteTileset(*t, &u));
    CHECK(back && back->Get("future").Get("a").AsInt() == 1);  // (知らないキーは書き戻す: 憲法 6)
    CHECK(back && back->Get("sources").At(0).Get("later").AsBool());
    CHECK(!ReadTileset(R"({"kind": "rm_engine.material"})", &err).has_value());  // (違う種類のファイル)
    const space2d::GridCut cut = CutOf(TileSource{.tileWidth = 0, .margin = -3});
    CHECK(cut.cellWidth == 1 && cut.margin == 0);
}

RM_TEST(TilemapLayerSavesChunksAndFindsNearestGrid) {
    using namespace rm::game_x_tilemap;
    game_production::Scene scene;
    scene.SetComponentTypes({&reflect::TypeOf<space2d::Transform2D>(), &reflect::TypeOf<TileGrid>(), &reflect::TypeOf<TileLayer>()});
    const int grid = scene.Create("grid");
    scene.AddComponent(grid, reflect::TypeOf<TileGrid>());
    static_cast<TileGrid*>(scene.Find(grid)->FindComponent(reflect::TypeOf<TileGrid>().name)->value.Get())->cellSize = {2, 0.5f};
    const int group = scene.Create("group", grid);
    const int layer = scene.Create("layer", group);
    scene.AddComponent(layer, reflect::TypeOf<TileLayer>());
    CHECK((GridCellSize(scene, layer) == Vec2{2, 0.5f}));  // (親を順にたどって一番近いグリッド)
    CHECK((GridCellSize(scene, grid) == Vec2{2, 0.5f}));
    const int lone = scene.Create("lone");
    CHECK((GridCellSize(scene, lone) == Vec2{1, 1}));  // (グリッドが無い: 1 x 1)
    CHECK(LayerOf(scene, layer) != nullptr && LayerOf(scene, grid) == nullptr);
    // 層の部品の保存の形 (自己記述の JSON) と、知らないキー。
    TileLayer l;
    l.chunks = {{0, 0, "65536,0*1023"}};
    reflect::Unknowns u;
    TileLayer read;
    const auto in = json::Parse(R"({"order": 3, "chunk": 32, "chunks": [{"x": -1, "y": 2, "cells": "1*2", "z": 9}], "later": "x"})");
    CHECK(in.has_value());
    if (!in) return;
    reflect::Load(read, *in, &u);
    CHECK(read.order == 3 && read.chunks.size() == 1 && read.chunks[0].x == -1 && read.chunks[0].cells == "1*2");
    const json::Value out = reflect::Save(read, &u);
    CHECK(out.Get("later").AsString() == "x" && out.Get("chunks").At(0).Get("z").AsInt() == 9);
}

RM_TEST(TilemapThousandSquareLayerLoadsAndEditsQuickly) {
    // 1000 x 1000 マス (約 1000 区画) の層を読み、1 マス変えて 1 区画を書き直す (設計図 4-6: ゆるい時間)。
    std::vector<TileChunk> chunks;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) chunks.push_back({x, y, "65536*512,65537*512"});
    }
    const auto t0 = std::chrono::steady_clock::now();
    TileStore s;
    s.Load(chunks);
    s.Set({500, 500}, MakeTile(2, 1, 1));
    s.WriteTo(chunks, std::vector<Cell>{ChunkOf({500, 500})});
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    CHECK(s.ChunkCount() == 1024 && s.Get({500, 500}) == MakeTile(2, 1, 1));
    CHECK(ChunkCells(chunks, 15, 15) != "65536*512,65537*512");
    CHECK(ms < 2000);  // (Release でふつう数十 ms。Debug でも通るゆるさ)
}
