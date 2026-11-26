#include "rm_engine/navigation/runtime/NavFile.h"

#include <cstring>
#include <format>

namespace rm::navigation {

namespace {

constexpr char kMagic[8] = {'R', 'M', 'N', 'A', 'V', 0, 0, 0};
constexpr uint64_t kMaxChunk = 1ull << 31;  // 1 つのチャンクの上限 (壊れたファイルが大きな場所を求めない)

void PutU32(std::string& s, uint32_t v) { s.append(reinterpret_cast<const char*>(&v), 4); }
void PutI32(std::string& s, int32_t v) { s.append(reinterpret_cast<const char*>(&v), 4); }
void PutU64(std::string& s, uint64_t v) { s.append(reinterpret_cast<const char*>(&v), 8); }
void Chunk(std::string& s, const char tag[4], std::string_view body) {
    s.append(tag, 4);
    PutU64(s, body.size());
    s.append(body);
}

template <class T>
bool Get(std::string_view b, size_t& at, T& out) {
    if (at > b.size() || b.size() - at < sizeof(T)) return false;
    std::memcpy(&out, b.data() + at, sizeof(T));
    at += sizeof(T);
    return true;
}

json::Value VecJson(Vec3 v) {
    json::Value a = json::Value::MakeArray();
    a.Push(static_cast<double>(v.x));
    a.Push(static_cast<double>(v.y));
    a.Push(static_cast<double>(v.z));
    return a;
}
Vec3 JsonVec(const json::Value& v) {
    if (!v.IsArray() || v.Size() != 3) return {};
    return {static_cast<float>(v.At(0).AsDouble()), static_cast<float>(v.At(1).AsDouble()), static_cast<float>(v.At(2).AsDouble())};
}

// HEAD に書くキー (ほかは知らないキー)。
constexpr const char* kKnown[] = {"kind", "format", "engine", "bounds", "tileSize", "tilesX", "tilesZ", "maxPolysPerTile", "agent", "fingerprint"};

}  // namespace

std::string FingerprintText(uint64_t value) { return std::format("{:016x}", value); }

std::string WriteNavFile(const NavData& data, const NavFileHead& head) {
    json::Value h = head.extra.IsObject() ? head.extra : json::Value::MakeObject();
    for (const char* k : kKnown) h.Erase(k);
    json::Value known = json::Value::MakeObject();
    known.Set("kind", kNavFileKind);
    known.Set("format", static_cast<int64_t>(kNavFileFormat));
    known.Set("engine", head.engine);
    json::Value bounds = json::Value::MakeObject();
    bounds.Set("min", VecJson(data.bounds.min));
    bounds.Set("max", VecJson(data.bounds.max));
    known.Set("bounds", std::move(bounds));
    known.Set("tileSize", static_cast<double>(data.tileSize));
    known.Set("tilesX", static_cast<int64_t>(data.tilesX));
    known.Set("tilesZ", static_cast<int64_t>(data.tilesZ));
    known.Set("maxPolysPerTile", static_cast<int64_t>(data.maxPolysPerTile));
    json::Value agent = json::Value::MakeObject();
    agent.Set("id", static_cast<int64_t>(head.agentType));
    agent.Set("name", head.agentName);
    agent.Set("radius", static_cast<double>(data.agent.radius));
    agent.Set("height", static_cast<double>(data.agent.height));
    agent.Set("maxSlope", static_cast<double>(data.agent.maxSlope));
    agent.Set("maxClimb", static_cast<double>(data.agent.maxClimb));
    agent.Set("cellSize", static_cast<double>(data.agent.cellSize));
    agent.Set("cellHeight", static_cast<double>(data.agent.cellHeight));
    known.Set("agent", std::move(agent));
    known.Set("fingerprint", head.fingerprint);
    for (const auto& m : h.Members()) known.Set(m.first, m.second);  // (知らないキーは後ろに)

    std::string out(kMagic, sizeof kMagic);
    PutU32(out, kNavFileFormat);
    Chunk(out, "HEAD", json::Write(known, false));
    for (const NavTile& t : data.tiles) {
        std::string body;
        PutI32(body, t.x);
        PutI32(body, t.z);
        body.append(t.data);
        Chunk(out, "TILE", body);
    }
    return out;
}

std::optional<NavData> ReadNavFile(std::string_view bytes, NavFileHead* head, std::string* error) {
    auto fail = [&](const char* why) -> std::optional<NavData> {
        if (error) *error = why;
        return std::nullopt;
    };
    if (bytes.size() < sizeof kMagic + 4 || std::memcmp(bytes.data(), kMagic, sizeof kMagic) != 0) return fail("ナビメッシュのファイル (.rmnav) ではありません");
    size_t at = sizeof kMagic;
    uint32_t format = 0;
    Get(bytes, at, format);
    if (format == 0 || format > kNavFileFormat) return fail("新しい版のエンジンで焼いたナビメッシュです (このエンジンでは読めません)");
    NavData data;
    NavFileHead h;
    bool sawHead = false;
    while (at < bytes.size()) {
        if (bytes.size() - at < 12) return fail("ナビメッシュのファイルが壊れています (途中で切れています)");
        char tag[4];
        std::memcpy(tag, bytes.data() + at, 4);
        at += 4;
        uint64_t size = 0;
        Get(bytes, at, size);
        if (size > kMaxChunk || size > bytes.size() - at) return fail("ナビメッシュのファイルが壊れています (途中で切れています)");
        const std::string_view body = bytes.substr(at, static_cast<size_t>(size));
        at += static_cast<size_t>(size);
        if (std::memcmp(tag, "HEAD", 4) == 0) {
            const auto j = json::Parse(body);
            if (!j || !j->IsObject() || j->Get("kind").IsString() == false || j->Get("kind").AsString() != kNavFileKind) {
                return fail("ナビメッシュのファイルが壊れています (頭が読めません)");
            }
            sawHead = true;
            const json::Value& bj = j->Get("bounds");
            data.bounds.min = JsonVec(bj.Get("min"));
            data.bounds.max = JsonVec(bj.Get("max"));
            data.tileSize = static_cast<float>(j->Get("tileSize").AsDouble());
            data.tilesX = static_cast<int>(j->Get("tilesX").AsInt());
            data.tilesZ = static_cast<int>(j->Get("tilesZ").AsInt());
            data.maxPolysPerTile = static_cast<int>(j->Get("maxPolysPerTile").AsInt());
            const json::Value& a = j->Get("agent");
            data.agent.radius = static_cast<float>(a.Get("radius").AsDouble(0.3));
            data.agent.height = static_cast<float>(a.Get("height").AsDouble(1.8));
            data.agent.maxSlope = static_cast<float>(a.Get("maxSlope").AsDouble(45));
            data.agent.maxClimb = static_cast<float>(a.Get("maxClimb").AsDouble(0.3));
            data.agent.cellSize = static_cast<float>(a.Get("cellSize").AsDouble(0.1));
            data.agent.cellHeight = static_cast<float>(a.Get("cellHeight").AsDouble(0.05));
            h.agentType = static_cast<int>(a.Get("id").AsInt());
            if (a.Get("name").IsString()) h.agentName = a.Get("name").AsString();
            if (j->Get("engine").IsString()) h.engine = j->Get("engine").AsString();
            if (j->Get("fingerprint").IsString()) h.fingerprint = j->Get("fingerprint").AsString();
            for (const auto& m : j->Members()) {
                bool known = false;
                for (const char* k : kKnown) known = known || m.first == k;
                if (!known) h.extra.Set(m.first, m.second);
            }
        } else if (std::memcmp(tag, "TILE", 4) == 0) {
            size_t b = 0;
            NavTile t;
            int32_t x = 0, z = 0;
            if (!Get(body, b, x) || !Get(body, b, z)) return fail("ナビメッシュのファイルが壊れています (区画が読めません)");
            t.x = x, t.z = z;
            t.data.assign(body.substr(b));
            data.tiles.push_back(std::move(t));
        }
        // (知らないタグ: 飛ばす)
    }
    if (!sawHead) return fail("ナビメッシュのファイルが壊れています (頭がありません)");
    if (head) *head = std::move(h);
    return data;
}

}  // namespace rm::navigation
