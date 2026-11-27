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
#include "rm_engine/physics/runtime/Components.h"
#include "rm_engine/physics/runtime/PhysicsWorld.h"
#include "rm_engine/tilemap_x_physics/runtime/Register.h"
#include "core/RuntimeRegistry.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/space2d/runtime/Atlas.h"
#include "rm_engine/space2d/runtime/Transform2D.h"
#include "rm_engine/tilemap/runtime/TileCell.h"
#include "rm_engine/tilemap/runtime/TileCollision.h"
#include "rm_engine/tilemap/runtime/TilePaint.h"
#include "rm_engine/tilemap/runtime/TileStore.h"
#include "rm_engine/tilemap/runtime/Tileset.h"

#include <algorithm>
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

// --- 段階 2: 当たり・タグ・独自データ ---------------------------------------------------------------

namespace {

Tileset ShapeSet() {
    Tileset t;
    t.sources.push_back({});
    auto add = [&](int col, TileShape s) {
        TileInfo i;
        i.source = 1;
        i.column = col;
        i.shape = s;
        SetTile(t, i);
    };
    add(0, TileShape::Full);
    add(1, TileShape::Bottom);
    add(2, TileShape::SlopeBottomLeft);
    add(3, TileShape::Left);
    TileInfo custom;
    custom.source = 1;
    custom.column = 4;
    custom.shape = TileShape::Custom;
    custom.points = {{0, 0}, {1, 0}, {0.5f, 0.2f}, {0.5f, 1}};  // (へこみの点 0.5, 0.2 は包む形の中)
    SetTile(t, custom);
    return t;
}

std::vector<TileValue> EmptyChunk() { return std::vector<TileValue>(static_cast<size_t>(kChunkCells), 0); }
void Put(std::vector<TileValue>& cells, int x, int y, TileValue v) { cells[static_cast<size_t>(IndexInChunk({x, y}))] = v; }
float Area(const CollisionBox& b) { return (b.max.x - b.min.x) * (b.max.y - b.min.y); }

}  // namespace

RM_TEST(TilemapTilesStaySparseAndKeepData) {
    Tileset t;
    t.fields.push_back({"かたさ", TileFieldKind::Int, "1"});
    TileInfo i = TileOrDefault(t, 1, 2, 3);
    CHECK(IsDefaultTile(i) && i.source == 1 && i.column == 2 && i.row == 3);
    SetTile(t, i);
    CHECK(t.tiles.empty());  // (既定: ファイルに出ない)
    i.tags = {"壊せる"};
    SetTileData(t, i, "かたさ", "3");
    SetTile(t, i);
    CHECK(t.tiles.size() == 1 && TileData(t, *FindTile(t, 1, 2, 3), "かたさ") == "3");
    CHECK(TileData(t, TileOrDefault(t, 1, 0, 0), "かたさ") == "1");  // (値の無いタイル: 欄の既定値)
    SetTileData(t, i, "かたさ", "1");
    CHECK(i.data.empty());  // (既定値と同じ: 持たない)
    TileInfo first = TileOrDefault(t, 1, 0, 0);
    first.name = "草";
    SetTile(t, first);
    CHECK(t.tiles.size() == 2 && t.tiles[0].column == 0);  // (絵の元・行・列の順)
    // 欄を消しても値はファイルに残る。
    i.data.push_back({"移動コスト", "5"});
    SetTile(t, i);
    t.fields.clear();
    const auto back = ReadTileset(WriteTileset(t), nullptr);
    CHECK(back && TileData(*back, *FindTile(*back, 1, 2, 3), "移動コスト") == "5");
    i.tags.clear();
    i.data.clear();
    SetTile(t, i);
    CHECK(t.tiles.size() == 1);  // (既定に戻した: 消える)
}

RM_TEST(TilemapShapePolygonsAndFlips) {
    TileInfo i;
    i.shape = TileShape::SlopeBottomLeft;
    const std::vector<Vec2> p = ShapePolygon(i, 0);
    CHECK(p.size() == 3);
    const std::vector<Vec2> flipped = ShapePolygon(i, kFlipX);  // (左右反転: 直角が右下)
    i.shape = TileShape::SlopeBottomRight;
    std::vector<Vec2> want = ShapePolygon(i, 0);
    CHECK(flipped == want);
    i.shape = TileShape::Custom;
    i.points = {{0, 0}, {1, 0}, {0.5f, 0.2f}, {0.5f, 1}};
    CHECK(ShapePolygon(i, 0).size() == 3);  // (へこみの点は包む形に入らない)
    i.points = {{0, 0}, {1, 1}};
    CHECK(ShapePolygon(i, 0).empty());  // (2 点: 形にならない)
    CHECK(ConvexHull({{0, 0}, {1, 1}, {2, 2}}).empty());  // (一直線)
}

RM_TEST(TilemapCollisionMergesConnectedCellsIntoBoxes) {
    const CollisionTable table(ShapeSet());
    // 床一面 (32 マス x 2 行): 箱 1 つ。
    std::vector<TileValue> floor = EmptyChunk();
    for (int x = 0; x < 32; ++x) {
        Put(floor, x, 0, MakeTile(1, 0, 0));
        Put(floor, x, 1, MakeTile(1, 0, 0));
    }
    ChunkCollision c = CollideChunk(floor, {0, 0}, {1, 1}, table);
    CHECK(c.boxes.size() == 1 && c.polygons.empty());
    CHECK((c.boxes[0] == CollisionBox{{0, 0}, {32, 2}}));
    // L 字: 箱 2 つ。
    std::vector<TileValue> el = EmptyChunk();
    Put(el, 0, 0, MakeTile(1, 0, 0));
    Put(el, 1, 0, MakeTile(1, 0, 0));
    Put(el, 0, 1, MakeTile(1, 0, 0));
    c = CollideChunk(el, {0, 0}, {1, 1}, table);
    CHECK(c.boxes.size() == 2);
    // 穴のある 3 x 3: 箱 4 つ、面積 8 マス。
    std::vector<TileValue> ring = EmptyChunk();
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            if (x != 1 || y != 1) Put(ring, x, y, MakeTile(1, 0, 0));
        }
    }
    c = CollideChunk(ring, {0, 0}, {1, 1}, table);
    float area = 0;
    for (const auto& b : c.boxes) area += Area(b);
    CHECK(c.boxes.size() == 4 && std::fabs(area - 8) < 1e-4f);
    // 「全部」の上の「下半分」は 1 つの箱 (高さ 1.5)。左右反転した「左半分」= 右半分。
    std::vector<TileValue> half = EmptyChunk();
    Put(half, 5, 0, MakeTile(1, 0, 0));
    Put(half, 5, 1, MakeTile(1, 1, 0));
    Put(half, 10, 0, MakeTile(1, 3, 0, kFlipX));
    c = CollideChunk(half, {0, 0}, {1, 1}, table);
    CHECK(c.boxes.size() == 2);
    CHECK(std::any_of(c.boxes.begin(), c.boxes.end(), [](const CollisionBox& b) { return b == CollisionBox{{5, 0}, {6, 1.5f}}; }));
    CHECK(std::any_of(c.boxes.begin(), c.boxes.end(), [](const CollisionBox& b) { return b == CollisionBox{{10.5f, 0}, {11, 1}}; }));
    // 斜めと自分で描いた形: 1 マスずつの多角形。区画の場所・マスの大きさが効く。当たりの無いタイルは何も出さない。
    std::vector<TileValue> slopes = EmptyChunk();
    Put(slopes, 0, 0, MakeTile(1, 2, 0));
    Put(slopes, 1, 0, MakeTile(1, 4, 0));
    Put(slopes, 2, 0, MakeTile(1, 7, 0));
    c = CollideChunk(slopes, {-1, 0}, {2, 1}, table);
    CHECK(c.boxes.empty() && c.polygons.size() == 2);
    CHECK(!c.polygons.empty() && c.polygons[0][0].x <= -64 + 1e-4f);  // (区画 -1: x は -32 マス x 2 から)
}

RM_TEST(TilemapCollisionHasFewerShapesThanCells) {
    const CollisionTable table(ShapeSet());
    std::vector<TileValue> cells = EmptyChunk();
    size_t filled = 0;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            if ((x / 4 + y / 4) % 2 == 0) continue;  // (市松の 4 x 4 の島)
            Put(cells, x, y, MakeTile(1, 0, 0));
            ++filled;
        }
    }
    const ChunkCollision c = CollideChunk(cells, {0, 0}, {1, 1}, table);
    CHECK(c.boxes.size() * 5 < filled);  // (島 1 つ = 箱 1 つ: 64 個。マスは 512)
    CHECK(CollisionTable(Tileset{}).Empty() && CollideChunk(cells, {0, 0}, {1, 1}, CollisionTable(Tileset{})).Empty());
}

namespace {

// 床 (区画 (0, 0) の下の 1 行、「全部」) を塗った層と、その上の動く箱。タイルセットはアセットのバイトとして渡す。
struct TileWorld {
    game_production::Scene scene;
    RuntimeRegistry registry;
    game_production::AssetAccess assets;
    std::string tileset;
    int layer = 0, box = 0;
};

void MakeTileWorld(TileWorld& w, float boxX) {
    using namespace rm::game_x_tilemap;
    w.scene.SetComponentTypes({&reflect::TypeOf<space2d::Transform2D>(), &reflect::TypeOf<TileGrid>(), &reflect::TypeOf<TileLayer>(),
                               &reflect::TypeOf<physics::Rigidbody>(), &reflect::TypeOf<physics::Collider2D>()});
    Tileset set;
    set.sources.push_back({});
    TileInfo floor;
    floor.source = 1;
    floor.shape = tilemap::TileShape::Full;
    SetTile(set, floor);
    w.tileset = WriteTileset(set);
    w.assets.bytes = [&w](const AssetRef& ref) -> std::optional<std::string> {
        if (ref.guid == "tiles") return w.tileset;
        return std::nullopt;
    };
    const int grid = w.scene.Create("grid");
    w.scene.AddComponent(grid, reflect::TypeOf<space2d::Transform2D>());
    w.scene.AddComponent(grid, reflect::TypeOf<TileGrid>());
    w.layer = w.scene.Create("ground", grid);
    w.scene.AddComponent(w.layer, reflect::TypeOf<space2d::Transform2D>());
    w.scene.AddComponent(w.layer, reflect::TypeOf<TileLayer>());
    auto& layer = *static_cast<TileLayer*>(w.scene.Find(w.layer)->FindComponent(reflect::TypeOf<TileLayer>().name)->value.Get());
    layer.tileset.guid = "tiles";
    layer.chunks = {{0, 0, "65536*32,0*992"}};  // (y = 0 の行: 0 〜 32 m、上の面は 1 m)
    w.box = w.scene.Create("box");
    w.scene.AddComponent(w.box, reflect::TypeOf<space2d::Transform2D>());
    static_cast<space2d::Transform2D*>(w.scene.Find(w.box)->FindComponent(reflect::TypeOf<space2d::Transform2D>().name)->value.Get())->position = {boxX, 4};
    w.scene.AddComponent(w.box, reflect::TypeOf<physics::Collider2D>());
    w.scene.AddComponent(w.box, reflect::TypeOf<physics::Rigidbody>());
    tilemap_x_physics::RegisterRuntime(w.registry);
}

Vec2 BoxAt(TileWorld& w) {
    return static_cast<space2d::Transform2D*>(w.scene.Find(w.box)->FindComponent(reflect::TypeOf<space2d::Transform2D>().name)->value.Get())->position;
}

}  // namespace

RM_TEST(TilemapPhysicsBoxRestsOnTheTileFloor) {
    TileWorld w;
    MakeTileWorld(w, 5.5f);
    physics::PhysicsWorld world;
    std::vector<game_production::HookCall> hooks;
    for (int i = 0; i < 180; ++i) world.Step(w.scene, 1.f / 60.f, {0, -9.81f, 0}, &hooks, &w.assets, &w.registry);
    CHECK(world.StaticPieces() == 1);  // (区画 1 つ = 動かない物体 1 つ)
    const Vec2 at = BoxAt(w);
    CHECK(at.y > 1.3f && at.y < 1.7f);  // (床の上の面 1 m の上に、高さ 1 の箱の真ん中)
    // 当たった相手は層のエンティティ。
    CHECK(std::any_of(hooks.begin(), hooks.end(), [&](const game_production::HookCall& h) { return h.entity == w.box && h.other == w.layer; }));
    // 光線も層に当たる。
    const auto hit = world.Raycast({20, 5, 0}, {0, -1, 0}, 10);
    CHECK(hit && hit->entity == w.layer && std::fabs(hit->point.y - 1) < 0.05f);
}

RM_TEST(TilemapPhysicsRebuildsOnlyChangedChunksAndFollowsTheLayer) {
    TileWorld w;
    MakeTileWorld(w, -10.f);  // (箱は床の外: 落ちていく)
    physics::PhysicsWorld world;
    world.Step(w.scene, 1.f / 60.f, {0, -9.81f, 0}, nullptr, &w.assets, &w.registry);
    CHECK(world.StaticPieces() == 1);
    auto& layer = *static_cast<game_x_tilemap::TileLayer*>(w.scene.Find(w.layer)->FindComponent(reflect::TypeOf<game_x_tilemap::TileLayer>().name)->value.Get());
    // ゲーム中に区画 (-1, 0) を足す: 物体が 2 つ。消すと 1 つに戻る。
    layer.chunks.insert(layer.chunks.begin(), tilemap::TileChunk{-1, 0, "65536*32,0*992"});
    world.Step(w.scene, 1.f / 60.f, {0, -9.81f, 0}, nullptr, &w.assets, &w.registry);
    CHECK(world.StaticPieces() == 2);
    layer.chunks.erase(layer.chunks.begin());
    world.Step(w.scene, 1.f / 60.f, {0, -9.81f, 0}, nullptr, &w.assets, &w.registry);
    CHECK(world.StaticPieces() == 1);
    // 「当たりを作る」を外すと消える。層を有効でなくしても消える。
    layer.collision = false;
    world.Step(w.scene, 1.f / 60.f, {0, -9.81f, 0}, nullptr, &w.assets, &w.registry);
    CHECK(world.StaticPieces() == 0);
    layer.collision = true;
    w.scene.Find(w.layer)->data.active = false;
    world.Step(w.scene, 1.f / 60.f, {0, -9.81f, 0}, nullptr, &w.assets, &w.registry);
    CHECK(world.StaticPieces() == 0);
    // 口が無い (registry なし): 作らない。
    TileWorld w2;
    MakeTileWorld(w2, 5.5f);
    physics::PhysicsWorld none;
    none.Step(w2.scene, 1.f / 60.f, {0, -9.81f, 0}, nullptr, &w2.assets);
    CHECK(none.StaticPieces() == 0);
}
