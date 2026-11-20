#include "rm_engine/space3d/editor/Gizmo.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace rm::space3d::gizmo {

namespace {

constexpr float kRadToDeg = 180.f / std::numbers::pi_v<float>;

Vec2 Sub(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
float Dot2(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
float Cross2(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
float Len2(Vec2 a) { return std::sqrt(Dot2(a, a)); }

float Step(float v, float step) { return step > 0 ? std::round(v / step) * step : v; }

// 面のハンドルが動く 2 つの軸。
void PlaneAxes(Handle plane, int& a, int& b) {
    a = plane == Handle::YZ ? 1 : plane == Handle::ZX ? 2 : 0;
    b = plane == Handle::YZ ? 2 : plane == Handle::ZX ? 0 : 1;
}

bool InsideConvex(Vec2 p, const std::array<Vec2, 4>& q) {
    float sign = 0;
    for (size_t i = 0; i < q.size(); ++i) {
        const float c = Cross2(Sub(q[(i + 1) % q.size()], q[i]), Sub(p, q[i]));
        if (c == 0) continue;
        if (sign == 0) sign = c;
        if ((c > 0) != (sign > 0)) return false;
    }
    return sign != 0;
}

// `axis` (輪の面) に垂直な 2 つの単位ベクトル。
void Across(Vec3 axis, Vec3& u, Vec3& v) {
    const Vec3 helper = std::fabs(axis.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    u = Normalize(Cross(helper, axis));
    v = Cross(axis, u);
}

// 閉じた輪の、マウス (画面上) に一番近い点と、その距離。
std::optional<Vec3> NearestOnLoop(const Screen& s, const std::vector<Vec3>& loop, Vec2 mouse, float* distance) {
    std::optional<Vec3> best;
    float bestD = std::numeric_limits<float>::infinity();
    for (size_t i = 0; i + 1 < loop.size(); ++i) {
        const auto a = s.ToScreen(loop[i]), b = s.ToScreen(loop[i + 1]);
        if (!a || !b) continue;
        const float d = DistanceToSegment(mouse, *a, *b);
        if (d < bestD) {
            bestD = d;
            best = Len2(Sub(mouse, *a)) <= Len2(Sub(mouse, *b)) ? loop[i] : loop[i + 1];
        }
    }
    if (distance) *distance = bestD;
    return best;
}

// 画面上の閉じた輪の内側 (偶奇で数える交差判定)。
bool InsideLoop(const Screen& s, const std::vector<Vec3>& loop, Vec2 p) {
    bool inside = false;
    for (size_t i = 0; i + 1 < loop.size(); ++i) {
        const auto a = s.ToScreen(loop[i]), b = s.ToScreen(loop[i + 1]);
        if (!a || !b) return false;
        if ((a->y > p.y) != (b->y > p.y) && p.x < a->x + (b->x - a->x) * (p.y - a->y) / (b->y - a->y)) inside = !inside;
    }
    return inside;
}

// `grab` (`axis` のまわりの輪の上の点) が前向きに回るとき (軸のまわりの右手の向き) に進む画面上の
// 向き。分からないときは nullopt。
std::optional<Vec2> TangentAt(const Frame& f, const Screen& s, Vec3 axis, Vec3 grab) {
    const Vec3 forward = Normalize(Cross(axis, grab - f.origin));
    const auto a = s.ToScreen(grab), b = s.ToScreen(grab + forward * (f.size * 0.1f));
    const auto o = s.ToScreen(f.origin);
    if (!a || !b || !o) return std::nullopt;
    Vec2 t = Sub(*b, *a);
    if (Len2(t) < 1e-3f) {  // この点で輪を真横から見ている: 中心からの線に垂直にする
        const Vec2 radial = Sub(*a, *o);
        t = {-radial.y, radial.x};
    }
    const float len = Len2(t);
    if (len < 1e-6f) return std::nullopt;
    return Vec2{t.x / len, t.y / len};
}

// 輪の見える弧の、マウス (画面上) に一番近い点と、その距離。
std::optional<Vec3> NearestOnRing(const Frame& f, const Screen& s, Vec3 eye, int axis, Vec2 mouse, float* distance) {
    std::optional<Vec3> best;
    float bestD = std::numeric_limits<float>::infinity();
    for (const bool front : {true, false}) {
      for (const auto& arc : RingArcs(f, axis, eye, front)) {
        for (size_t i = 0; i + 1 < arc.size(); ++i) {
            const auto a = s.ToScreen(arc[i]), b = s.ToScreen(arc[i + 1]);
            if (!a || !b) continue;
            const float d = DistanceToSegment(mouse, *a, *b) + (front ? 0.f : kBackPenalty);
            if (d < bestD) {
                bestD = d;
                // 近い方の端がつかめるほど近い (周りに 64 点)。
                best = Len2(Sub(mouse, *a)) <= Len2(Sub(mouse, *b)) ? arc[i] : arc[i + 1];
            }
        }
      }
    }
    if (distance) *distance = bestD;
    return best;
}

}  // namespace

int AxisOf(Handle h) {
    switch (h) {
        case Handle::X: case Handle::YZ: return 0;
        case Handle::Y: case Handle::ZX: return 1;
        case Handle::Z: case Handle::XY: return 2;
        default: return -1;
    }
}

std::array<Vec3, 3> AxesOf(const Mat4& world, Tool tool, Space space) {
    std::array<Vec3, 3> axes{Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};
    if (tool != Tool::Scale && space == Space::World) return axes;
    for (Vec3& a : axes) {
        const Vec3 d = Normalize(TransformDirection(world, a));
        if (Length(d) > 0) a = d;  // スケール 0 には向きが無い: ワールドのものを使う
    }
    return axes;
}

std::optional<Frame> MakeFrame(const Mat4& world, Tool tool, Space space, const OrbitView& view, float viewHeight, float pixels) {
    return MakeFrame(TransformPoint(world, {0, 0, 0}), AxesOf(world, tool, space), view, viewHeight, pixels);
}

std::optional<Frame> MakeFrame(Vec3 origin, const std::array<Vec3, 3>& axes, const OrbitView& view, float viewHeight, float pixels) {
    Frame f;
    f.origin = origin;
    const float perPixel = view.WorldPerPixel(f.origin, viewHeight);
    if (perPixel <= 0) return std::nullopt;
    f.size = pixels * perPixel;
    f.pixels = pixels;
    f.axes = axes;
    return f;
}

std::optional<Vec2> Screen::ToScreen(Vec3 p) const {
    const auto ndc = ProjectToNdc(viewProjection, p);
    if (!ndc) return std::nullopt;
    return Vec2{x + (ndc->x + 1.f) * 0.5f * w, y + (1.f - ndc->y) * 0.5f * h};
}

Ray Screen::RayAt(Vec2 p) const { return ScreenRay(inverse, (p.x - x) / w * 2.f - 1.f, 1.f - (p.y - y) / h * 2.f); }

std::optional<Screen> MakeScreen(const Mat4& viewProjection, float x, float y, float w, float h) {
    const auto inv = Inverse(viewProjection);
    if (!inv || w <= 0 || h <= 0) return std::nullopt;
    return Screen{viewProjection, *inv, x, y, w, h};
}

Vec3 AxisEnd(const Frame& f, int axis) { return f.origin + f.axes[axis] * f.size; }

std::array<Vec3, 4> PlaneQuad(const Frame& f, Handle plane) {
    int a = 0, b = 1;
    PlaneAxes(plane, a, b);
    const Vec3 u = f.axes[a] * f.size, v = f.axes[b] * f.size;
    return {f.origin + u * kPlaneNear + v * kPlaneNear, f.origin + u * kPlaneFar + v * kPlaneNear,
            f.origin + u * kPlaneFar + v * kPlaneFar, f.origin + u * kPlaneNear + v * kPlaneFar};
}

std::vector<std::vector<Vec3>> RingArcs(const Frame& f, int axis, Vec3 eye, bool front, int segments) {
    Vec3 u, v;
    Across(f.axes[axis], u, v);
    const Vec3 toEye = Normalize(eye - f.origin);
    std::vector<Vec3> pts(static_cast<size_t>(segments));
    std::vector<bool> shown(static_cast<size_t>(segments));
    for (int i = 0; i < segments; ++i) {
        const float t = 2.f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(segments);
        const Vec3 dir = u * std::cos(t) + v * std::sin(t);
        pts[i] = f.origin + dir * f.size;
        // 目の方を向いた輪は全部が表側 (裏側は無い)。2 つの半分は両端を共有するので、表と裏を合わせると
        // 隙間なく 1 周の円になる。
        const float d = Dot(dir, toEye);
        shown[i] = front ? d >= -0.02f : d <= 0.02f && Dot(f.axes[axis], toEye) * Dot(f.axes[axis], toEye) < 0.9996f;
    }
    std::vector<std::vector<Vec3>> arcs;
    const auto start = std::find(shown.begin(), shown.end(), false);
    if (std::find(shown.begin(), shown.end(), true) == shown.end()) return arcs;  // 無し
    if (start == shown.end()) {  // 全部: 閉じた 1 つの輪
        pts.push_back(pts.front());
        arcs.push_back(std::move(pts));
        return arcs;
    }
    // 隠れた点から 1 周歩く。こうすると弧が添字 0 で途切れない。
    const size_t first = static_cast<size_t>(start - shown.begin());
    std::vector<Vec3> run;
    for (size_t k = 1; k <= static_cast<size_t>(segments); ++k) {
        const size_t i = (first + k) % static_cast<size_t>(segments);
        if (shown[i]) {
            run.push_back(pts[i]);
        } else if (!run.empty()) {
            if (run.size() > 1) arcs.push_back(std::move(run));
            run.clear();
        }
    }
    if (run.size() > 1) arcs.push_back(std::move(run));
    return arcs;
}

std::vector<Vec3> FacingCircle(const Frame& f, Vec3 eye, float radius, int segments) {
    Vec3 u, v;
    Across(Normalize(eye - f.origin), u, v);
    std::vector<Vec3> pts;
    pts.reserve(static_cast<size_t>(segments) + 1);
    for (int i = 0; i <= segments; ++i) {
        const float t = 2.f * std::numbers::pi_v<float> * static_cast<float>(i % segments) / static_cast<float>(segments);
        pts.push_back(f.origin + (u * std::cos(t) + v * std::sin(t)) * (f.size * radius));
    }
    return pts;
}

bool AxisShown(const Frame& f, const Screen& s, int axis) {
    const auto o = s.ToScreen(f.origin), e = s.ToScreen(AxisEnd(f, axis));
    return o && e && Len2(Sub(*e, *o)) > f.pixels * 0.12f;
}

bool PlaneShown(const Frame& f, const Screen& s, Handle plane, std::array<Vec2, 4>* out) {
    std::array<Vec2, 4> q;
    const auto quad = PlaneQuad(f, plane);
    for (size_t i = 0; i < 4; ++i) {
        const auto p = s.ToScreen(quad[i]);
        if (!p) return false;
        q[i] = *p;
    }
    if (out) *out = q;
    // 画面上の面積 (靴紐の公式) を、目の方を向いた正方形と比べる: 5 分の 1 未満なら真横から見ている。
    float area = 0;
    for (size_t i = 0; i < 4; ++i) area += Cross2(q[i], q[(i + 1) % 4]);
    const float side = (kPlaneFar - kPlaneNear) * f.pixels;
    return std::fabs(area) * 0.5f > side * side * 0.2f;
}

Handle HitTest(Tool tool, const Frame& f, const Screen& s, Vec3 eye, Vec2 mouse, float tolerance) {
    const auto origin = s.ToScreen(f.origin);
    if (!origin) return Handle::None;
    if (tool == Tool::Rotate) {
        Handle best = Handle::None;
        float bestD = tolerance;
        for (int a = 0; a < 3; ++a) {
            float d = 0;
            if (NearestOnRing(f, s, eye, a, mouse, &d) && d < bestD) {
                bestD = d;
                best = static_cast<Handle>(static_cast<int>(Handle::X) + a);
            }
        }
        float d = 0;
        if (NearestOnLoop(s, FacingCircle(f, eye, kViewRing), mouse, &d) && d < bestD) best = Handle::View;
        if (best == Handle::None && InsideLoop(s, FacingCircle(f, eye, 1.f), mouse)) best = Handle::Free;
        return best;
    }
    if (tool == Tool::Scale && Len2(Sub(mouse, *origin)) <= tolerance + 3.f) return Handle::Uniform;
    if (tool == Tool::Move) {
        for (Handle plane : {Handle::YZ, Handle::ZX, Handle::XY}) {
            std::array<Vec2, 4> q;
            if (PlaneShown(f, s, plane, &q) && InsideConvex(mouse, q)) return plane;
        }
    }
    Handle best = Handle::None;
    float bestD = tolerance;
    for (int a = 0; a < 3; ++a) {
        if (!AxisShown(f, s, a)) continue;
        const auto e = s.ToScreen(AxisEnd(f, a));
        const float d = DistanceToSegment(mouse, *origin, *e) - (Len2(Sub(mouse, *e)) < tolerance * 1.6f ? tolerance : 0.f);
        if (d < bestD) {
            bestD = d;
            best = static_cast<Handle>(static_cast<int>(Handle::X) + a);
        }
    }
    return best;
}

std::optional<Drag> BeginDrag(Tool tool, Handle handle, const Frame& f, const Screen& s, Vec3 eye, Vec2 mouse) {
    if (handle == Handle::None) return std::nullopt;
    Drag d;
    d.tool = tool;
    d.handle = handle;
    d.frame = f;
    d.startMouse = mouse;
    const Ray ray = s.RayAt(mouse);
    const int axis = AxisOf(handle);
    if (handle == Handle::Uniform) return d;
    if (tool == Tool::Rotate) {
        const Vec3 toEye = Normalize(eye - f.origin);
        if (handle == Handle::Free) {
            // ギズモの所での、ワールドの中の画面の右と上 (中心の横の光線から求める)。
            const auto o = s.ToScreen(f.origin);
            if (!o || Length(toEye) == 0) return std::nullopt;
            Vec3 right = s.RayAt({o->x + 1.f, o->y}).dir - s.RayAt(*o).dir;
            right = Normalize(right - toEye * Dot(right, toEye));
            if (Length(right) == 0) return std::nullopt;
            d.axis = toEye;
            d.right = right;
            d.up = Cross(toEye, right);
            return d;
        }
        // 輪: ギズモの軸、または外側の輪なら視線。
        const Vec3 axis3 = handle == Handle::View ? toEye : f.axes[axis];
        const auto grab = handle == Handle::View ? NearestOnLoop(s, FacingCircle(f, eye, kViewRing), mouse, nullptr)
                                                 : NearestOnRing(f, s, eye, axis, mouse, nullptr);
        if (!grab || Length(axis3) == 0) return std::nullopt;
        d.startPoint = *grab;
        d.axis = axis3;
        const auto t = TangentAt(f, s, axis3, *grab);
        if (!t) return std::nullopt;
        d.tangent = *t;
        return d;
    }
    if (handle == Handle::YZ || handle == Handle::ZX || handle == Handle::XY) {
        const auto hit = IntersectPlane(ray, f.origin, f.axes[axis]);
        if (!hit) return std::nullopt;
        d.startPoint = *hit;
        return d;
    }
    const auto t = ClosestOnLine(ray, f.origin, f.axes[axis]);
    if (!t) return std::nullopt;
    d.startT = *t;
    d.startPoint = f.origin + f.axes[axis] * *t;
    return d;
}

std::optional<Change> Update(const Drag& d, const Screen& s, Vec2 mouse, const Snap& snap) {
    Change c;
    c.tool = d.tool;
    const int axis = AxisOf(d.handle);
    c.axis = axis;
    const Frame& f = d.frame;
    if (d.handle == Handle::Uniform) {
        const Vec2 m = Sub(mouse, d.startMouse);
        float factor = 1.f + (m.x - m.y) * 0.01f;  // 右 / 上: 大きく
        if (snap.on) factor = 1.f + Step(factor - 1.f, snap.scale);
        c.factor = std::max(factor, kMinScaleFactor);
        return c;
    }
    if (d.tool == Tool::Rotate) {
        if (d.handle == Handle::Free) {
            // ボールを転がすように: 目の方を向いた側がマウスについてくる。遠いほど大きく動く。
            const Vec2 m = Sub(mouse, d.startMouse);
            const float len = Len2(m);
            const Vec3 axis3 = Normalize(Cross(d.axis, d.right * m.x - d.up * m.y));
            float degrees = len / std::max(f.pixels, 1.f) * kRadToDeg;
            if (snap.on) degrees = Step(degrees, snap.degrees);
            c.worldAxis = len > 0 && Length(axis3) > 0 ? axis3 : d.up;
            c.degrees = len > 0 ? degrees : 0.f;
            return c;
        }
        const float radius = d.handle == Handle::View ? kViewRing : 1.f;
        const float along = Dot2(Sub(mouse, d.startMouse), d.tangent);
        float degrees = along / std::max(f.pixels * radius, 1.f) * kRadToDeg;  // マウスの下の輪の長さ
        if (snap.on) degrees = Step(degrees, snap.degrees);
        c.degrees = degrees;
        c.worldAxis = d.axis;
        return c;
    }
    if (axis >= 0) c.worldAxis = f.axes[axis];
    const Ray ray = s.RayAt(mouse);
    if (d.handle == Handle::YZ || d.handle == Handle::ZX || d.handle == Handle::XY) {
        const auto hit = IntersectPlane(ray, f.origin, f.axes[axis]);
        if (!hit) return std::nullopt;
        int a = 0, b = 1;
        PlaneAxes(d.handle, a, b);
        const Vec3 moved = *hit - d.startPoint;
        float sa = Dot(moved, f.axes[a]), sb = Dot(moved, f.axes[b]);
        if (snap.on) sa = Step(sa, snap.move), sb = Step(sb, snap.move);
        c.move = f.axes[a] * sa + f.axes[b] * sb;
        return c;
    }
    const auto t = ClosestOnLine(ray, f.origin, f.axes[axis]);
    if (!t) return std::nullopt;
    if (d.tool == Tool::Scale) {
        float factor = std::fabs(d.startT) > 1e-6f ? *t / d.startT : 1.f;
        if (snap.on) factor = 1.f + Step(factor - 1.f, snap.scale);
        c.factor = std::max(factor, kMinScaleFactor);  // 均一の箱と同じ: 0 にも裏返しにもしない
        return c;
    }
    float along = *t - d.startT;
    if (snap.on) along = Step(along, snap.move);
    c.move = f.axes[axis] * along;
    return c;
}

Transform Apply(const Transform& start, const Mat4& parentWorld, const Mat4& startWorld, const Change& c, Space space, Vec3 hint,
                const std::optional<Vec3>& pivot) {
    Transform t = start;
    const auto toParent = Inverse(parentWorld);
    // ピボットのまわりに回す / 拡大縮小すると物も動く: `offset` = ピボットから見た、行く先。
    const Vec3 startPlace = TransformPoint(startWorld, {0, 0, 0});
    const Vec3 fromPivot = pivot ? startPlace - *pivot : Vec3{0, 0, 0};
    auto placeAt = [&](Vec3 offset) {
        if (pivot && toParent) t.position = TransformPoint(*toParent, *pivot + offset);
    };
    switch (c.tool) {
        case Tool::Move:
            if (toParent) t.position = start.position + TransformDirection(*toParent, c.move);
            break;
        case Tool::Rotate: {
            if (!toParent) break;
            // ギズモが見せる軸のまわり (輪: 各自の場所で回すときは、各自の軸)。親の空間 (rotation がある所)
            // で表す。
            const bool own = c.axis >= 0 && (!pivot || Length(c.worldAxis) == 0);
            const Vec3 worldAxis = own ? AxesOf(startWorld, Tool::Rotate, space)[c.axis] : Normalize(c.worldAxis);
            const Vec3 axis = Normalize(TransformDirection(*toParent, worldAxis));
            if (Length(axis) == 0) break;
            // 鏡のように反転した親 (1 つの軸のスケールが -1: こうして向きを逆にしたキャラクター) は、親の空間の
            // 向きを逆にする: そこでは、ワールドでこう回るように、回転が逆向きになる。
            const float(&m)[4][4] = parentWorld.m;
            const float det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                              m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
            const float degrees = det < 0 ? -c.degrees : c.degrees;
            t.rotation = EulerFromRotation(AxisAngle(axis, degrees) * RotationMatrix(start.rotation), hint);
            placeAt(TransformDirection(AxisAngle(worldAxis, c.degrees), fromPivot));
            break;
        }
        case Tool::Scale:
            if (c.axis < 0) {
                t.scale = start.scale * c.factor;
                placeAt(fromPivot * c.factor);
            } else {
                (&t.scale.x)[c.axis] = (&start.scale.x)[c.axis] * c.factor;
                const Vec3 along = Normalize(c.worldAxis);
                placeAt(fromPivot + along * (Dot(fromPivot, along) * (c.factor - 1.f)));
            }
            break;
    }
    return t;
}

std::optional<float> ClosestOnLine(const Ray& ray, Vec3 origin, Vec3 axis) {
    const Vec3 w0 = origin - ray.origin;
    const float b = Dot(axis, ray.dir);
    const float denom = 1.f - b * b;
    if (denom < 1e-4f) return std::nullopt;
    return (b * Dot(ray.dir, w0) - Dot(axis, w0)) / denom;
}

std::optional<Vec3> IntersectPlane(const Ray& ray, Vec3 point, Vec3 normal) {
    const float denom = Dot(normal, ray.dir);
    if (std::fabs(denom) < 1e-4f) return std::nullopt;
    const float t = Dot(point - ray.origin, normal) / denom;
    if (t < 0) return std::nullopt;
    return ray.origin + ray.dir * t;
}

float DistanceToSegment(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 ab = Sub(b, a);
    const float len2 = Dot2(ab, ab);
    const float t = len2 > 0 ? std::clamp(Dot2(Sub(p, a), ab) / len2, 0.f, 1.f) : 0.f;
    return Len2(Sub(p, {a.x + ab.x * t, a.y + ab.y * t}));
}

}  // namespace rm::space3d::gizmo
