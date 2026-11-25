// 3D アニメーションのクリップのイベント (plugins/rm_engine/animation3d/editor/ClipEvents) のテスト: .meta の
// "import" の "clips" に足す・動かす・変える・消す。時刻の順・時刻を収める・知らないキーを残す・空になったら消す。

#include "Test.h"

#include "base/Json.h"
#include "rm_engine/animation3d/editor/ClipEvents.h"

#include <limits>
#include <string>

using namespace rm;
using namespace rm::animation3d;

namespace {

json::Value P(const char* text) { return *json::Parse(text); }

const json::Value& Events(const json::Value& clips, const char* clip) { return clips.Get(clip).Get("events"); }

ClipEvent Ev(float time, std::string name) {
    ClipEvent e;
    e.time = time;
    e.name = std::move(name);
    return e;
}

}  // namespace

RM_TEST(ClipEvents_AddToNothingMakesTheShape) {
    size_t at = 99;
    const json::Value clips = AddClipEvent(json::Value(), "Wave", Ev(0.5f, "Footstep"), 2.f, &at);
    CHECK(at == 0);
    CHECK(clips.IsObject());
    const json::Value& list = Events(clips, "Wave");
    CHECK(list.IsArray() && list.Size() == 1);
    CHECK(list.At(0).Get("time").AsDouble() == 0.5);
    CHECK(list.At(0).Get("name").AsString() == "Footstep");
    CHECK(list.At(0).Get("number").IsNumber() && list.At(0).Get("number").AsDouble() == 0.0);
    CHECK(list.At(0).Get("text").IsString() && list.At(0).Get("text").AsString().empty());
    const std::vector<ClipEvent> back = ReadClipEvents(clips, "Wave");
    CHECK(back.size() == 1 && back[0].name == "Footstep" && back[0].time == 0.5f);
}

RM_TEST(ClipEvents_SortedByTimeAndIndexFollows) {
    json::Value clips;
    size_t at = 0;
    clips = AddClipEvent(clips, "Wave", Ev(1.0f, "B"), 2.f, &at);
    clips = AddClipEvent(clips, "Wave", Ev(1.5f, "C"), 2.f, &at);
    CHECK(at == 1);
    clips = AddClipEvent(clips, "Wave", Ev(0.25f, "A"), 2.f, &at);
    CHECK(at == 0);  // 先頭に入った
    std::vector<ClipEvent> e = ReadClipEvents(clips, "Wave");
    CHECK(e.size() == 3 && e[0].name == "A" && e[1].name == "B" && e[2].name == "C");
    // C を一番前へ動かすと、番号はそれを追う。
    size_t moved = 9;
    clips = MoveClipEvent(clips, "Wave", 2, 0.1f, 2.f, &moved);
    CHECK(moved == 0);
    e = ReadClipEvents(clips, "Wave");
    CHECK(e[0].name == "C" && e[1].name == "A" && e[2].name == "B");
    CHECK(e[0].time == 0.1f);
    // 同じ時刻は前の順のまま (安定)。
    clips = AddClipEvent(clips, "Wave", Ev(1.0f, "B2"), 2.f, &at);
    e = ReadClipEvents(clips, "Wave");
    CHECK(at == 3 && e[2].name == "B" && e[3].name == "B2");
}

RM_TEST(ClipEvents_TimeIsClampedToTheClip) {
    CHECK(ClampEventTime(-1.f, 2.f) == 0.f);
    CHECK(ClampEventTime(3.f, 2.f) == 2.f);
    CHECK(ClampEventTime(1.25f, 2.f) == 1.25f);
    CHECK(ClampEventTime(0.5f, -1.f) == 0.f);
    CHECK(ClampEventTime(std::numeric_limits<float>::quiet_NaN(), 2.f) == 0.f);
    json::Value clips = AddClipEvent(json::Value(), "Wave", Ev(5.f, "late"), 2.f);
    CHECK(ReadClipEvents(clips, "Wave")[0].time == 2.f);
    clips = MoveClipEvent(clips, "Wave", 0, -3.f, 2.f);
    CHECK(ReadClipEvents(clips, "Wave")[0].time == 0.f);
    ClipEvent e = Ev(9.f, "x");
    clips = SetClipEvent(clips, "Wave", 0, e, 1.5f);
    CHECK(ReadClipEvents(clips, "Wave")[0].time == 1.5f);
}

RM_TEST(ClipEvents_UnknownKeysAreKeptEverywhere) {
    const json::Value start = P(R"({
        "future": {"a": 1},
        "Wave": {"loop": true, "range": [0, 1], "events": [
            {"time": 0.5, "name": "Hit", "number": 2, "text": "剣", "sound": "swoosh"},
            {"time": 1.0, "name": "End", "number": 0, "text": ""}
        ]},
        "Idle": {"events": [{"time": 0.2, "name": "Blink", "number": 0, "text": ""}]}
    })");
    // 1 つ目の名前を変える: そのイベントの "sound"、クリップの "loop" / "range"、"clips" の "future"、ほかのクリップが残る。
    std::vector<ClipEvent> e = ReadClipEvents(start, "Wave");
    CHECK(e.size() == 2 && e[0].name == "Hit" && e[0].number == 2.f && e[0].text == "剣");
    ClipEvent changed = e[0];
    changed.name = "強打";
    size_t at = 9;
    const json::Value out = SetClipEvent(start, "Wave", 0, changed, 2.f, &at);
    CHECK(at == 0);
    CHECK(out.Get("future") == start.Get("future"));
    CHECK(out.Get("Idle") == start.Get("Idle"));
    CHECK(out.Get("Wave").Get("loop").AsBool() == true);
    CHECK(out.Get("Wave").Get("range") == start.Get("Wave").Get("range"));
    const json::Value& first = Events(out, "Wave").At(0);
    CHECK(first.Get("name").AsString() == "強打");
    CHECK(first.Get("sound").AsString() == "swoosh");
    CHECK(first.Get("text").AsString() == "剣");
    CHECK(Events(out, "Wave").At(1) == Events(start, "Wave").At(1));  // 触っていないものはそのまま
    // 動かしても "sound" は残る。
    const json::Value moved = MoveClipEvent(out, "Wave", 0, 1.5f, 2.f, &at);
    CHECK(at == 1);
    CHECK(Events(moved, "Wave").At(1).Get("sound").AsString() == "swoosh");
    // 消しても、クリップのほかのキーは残る ("events" だけが消える)。
    json::Value removed = RemoveClipEvent(moved, "Wave", 0);
    removed = RemoveClipEvent(removed, "Wave", 0);
    CHECK(removed.Get("Wave").IsObject());
    CHECK(removed.Get("Wave").Find("events") == nullptr);
    CHECK(removed.Get("Wave").Get("loop").AsBool() == true);
    CHECK(removed.Get("future") == start.Get("future"));
}

RM_TEST(ClipEvents_LastEventRemovesTheEmptyParts) {
    json::Value clips = AddClipEvent(json::Value(), "Wave", Ev(0.5f, "x"), 2.f);
    clips = AddClipEvent(clips, "Idle", Ev(0.5f, "y"), 2.f);
    clips = RemoveClipEvent(clips, "Wave", 0);
    CHECK(clips.IsObject() && clips.Find("Wave") == nullptr);  // 空になったクリップは消える
    CHECK(clips.Find("Idle") != nullptr);
    clips = RemoveClipEvent(clips, "Idle", 0);
    CHECK(clips.IsNull());  // "clips" ごと消す (呼ぶ側がキーを消す)
    // 番号の外は何もしない。
    const json::Value one = AddClipEvent(json::Value(), "Wave", Ev(0.5f, "x"), 2.f);
    CHECK(RemoveClipEvent(one, "Wave", 5) == one);
    CHECK(SetClipEvent(one, "Wave", 5, Ev(1.f, "z"), 2.f) == one);
    CHECK(MoveClipEvent(one, "Wave", 5, 1.f, 2.f) == one);
    CHECK(ReadClipEvents(one, "Nope").empty());
}

RM_TEST(ClipEvents_WritesShortFloatsAndJapanese) {
    // 時刻は「同じ float に戻る一番短い 10 進」(0.1 であって 0.10000000149011612 ではない)。
    const json::Value clips = AddClipEvent(json::Value(), "走る", Ev(0.1f, "足音"), 1.f);
    const std::string text = json::Write(clips, false);
    CHECK(text.find("0.1,") != std::string::npos || text.find("0.1}") != std::string::npos);
    CHECK(text.find("0.10000") == std::string::npos);
    CHECK(text.find("足音") != std::string::npos);
    const json::Value back = *json::Parse(text);
    CHECK(ReadClipEvents(back, "走る").size() == 1 && ReadClipEvents(back, "走る")[0].name == "足音");
}

RM_TEST(ClipEvents_ReadsUnsortedAndOddFilesWithoutLosingThem) {
    // 手で書いた順でないファイル: 読むのはそのままの番号、書くと並ぶ。オブジェクトでない要素も 1 つと数える。
    const json::Value start = P(R"({"Wave": {"events": [
        {"time": 1.5, "name": "late"}, 7, {"time": 0.5, "name": "early"}
    ]}})");
    std::vector<ClipEvent> e = ReadClipEvents(start, "Wave");
    CHECK(e.size() == 3 && e[0].name == "late" && e[2].name == "early");
    CHECK(e[0].number == 0.f && e[0].text.empty());  // 無い欄は既定の値
    size_t at = 0;
    const json::Value out = SetClipEvent(start, "Wave", 0, e[0], 2.f, &at);
    const std::vector<ClipEvent> sorted = ReadClipEvents(out, "Wave");
    CHECK(sorted.size() == 3);
    CHECK(sorted[sorted.size() - 1].name == "late" && at == 2);
    // 壊れた "clips" (配列) は、オブジェクトに作り直して足す。
    const json::Value fixed = AddClipEvent(P("[1, 2]"), "Wave", Ev(0.f, "x"), 1.f);
    CHECK(fixed.IsObject() && ReadClipEvents(fixed, "Wave").size() == 1);
}
