#include "editor/app/ScreenWindow.h"

#include "base/Log.h"
#include "editor/app/App.h"
#include "editor/widgets/Theme.h"
#include "platform/Shell.h"

#include <imm.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string_view>

namespace rm::editor {

namespace {

constexpr UINT kWmDeferred = WM_APP + 1;
constexpr UINT_PTR kCaretTimer = 1;
constexpr UINT_PTR kDeviceTimer = 2;  // GPU を作り直せなかった: 少し後にもう一度やってみる
constexpr UINT kDeviceRetryMs = 1000;

ui::Key MapKey(WPARAM vk) {
    switch (vk) {
        case VK_LEFT: return ui::Key::Left;
        case VK_RIGHT: return ui::Key::Right;
        case VK_UP: return ui::Key::Up;
        case VK_DOWN: return ui::Key::Down;
        case VK_HOME: return ui::Key::Home;
        case VK_END: return ui::Key::End;
        case VK_PRIOR: return ui::Key::PageUp;
        case VK_NEXT: return ui::Key::PageDown;
        case VK_BACK: return ui::Key::Backspace;
        case VK_DELETE: return ui::Key::Delete;
        case VK_RETURN: return ui::Key::Enter;
        case VK_ESCAPE: return ui::Key::Escape;
        case VK_TAB: return ui::Key::Tab;
        case VK_F1: return ui::Key::F1;
        case VK_F2: return ui::Key::F2;
        case VK_F5: return ui::Key::F5;
        case VK_SPACE: return ui::Key::Space;
        case 'A': return ui::Key::A;
        case 'C': return ui::Key::C;
        case 'D': return ui::Key::D;
        case 'E': return ui::Key::E;
        case 'F': return ui::Key::F;
        case 'Q': return ui::Key::Q;
        case 'R': return ui::Key::R;
        case 'W': return ui::Key::W;
        case 'N': return ui::Key::N;
        case 'O': return ui::Key::O;
        case 'P': return ui::Key::P;
        case 'S': return ui::Key::S;
        case 'V': return ui::Key::V;
        case 'X': return ui::Key::X;
        case 'Y': return ui::Key::Y;
        case 'Z': return ui::Key::Z;
        default: return ui::Key::None;  // VK_PROCESSKEY (IME が食べたキー) も含む
    }
}

std::wstring ReadImeString(HIMC imc, DWORD type) {
    const LONG bytes = ImmGetCompositionStringW(imc, type, nullptr, 0);
    if (bytes <= 0) return {};
    std::wstring s(static_cast<size_t>(bytes) / sizeof(wchar_t), L'\0');
    ImmGetCompositionStringW(imc, type, s.data(), static_cast<DWORD>(bytes));
    return s;
}

LPCWSTR CursorResource(ui::Cursor c) {
    switch (c) {
        case ui::Cursor::Hand: return IDC_HAND;
        case ui::Cursor::IBeam: return IDC_IBEAM;
        case ui::Cursor::SizeWE: return IDC_SIZEWE;
        case ui::Cursor::SizeNS: return IDC_SIZENS;
        default: return IDC_ARROW;
    }
}

}  // namespace

ScreenWindow::~ScreenWindow() { Close(); }

void ScreenWindow::Close() {
    alive_ = false;
    if (caretTimer_ && Hwnd()) KillTimer(Hwnd(), kCaretTimer);
    caretTimer_ = false;
    target_.Reset();
    window_.Destroy();
}

bool ScreenWindow::CreateScreenWindow(const platform::WindowDesc& descIn, int showCmd) {
    platform::WindowDesc desc = descIn;
    desc.captionRgb = theme::CaptionRgb;  // タイトルバーは、使っているテーマの色にする
    desc.darkCaption = !theme::IsLight;
    if (!window_.Create(desc, this)) return false;
    if (!target_.Create(app_.Gfx(), window_.Handle(), window_.Dpi())) return false;
    painter_.Init(app_.Gfx().DWrite());
    ctx_.getClipboardText = [this] { return platform::GetClipboardText(Hwnd()); };
    ctx_.setClipboardText = [this](const std::wstring& s) { platform::SetClipboardText(Hwnd(), s); };
    // テキスト欄がフォーカスを取るまで、IME は切っておく。
    ImmAssociateContextEx(Hwnd(), nullptr, 0);
    imeOn_ = false;
    alive_ = true;
    OnCreated();
    ShowWindow(Hwnd(), showCmd);
    if (showCmd != SW_SHOWNOACTIVATE) SetForegroundWindow(Hwnd());
    Invalidate();
    return true;
}

void ScreenWindow::OnLookChanged() {
    window_.SetCaption(!theme::IsLight, theme::CaptionRgb);
    Invalidate();
}

void ScreenWindow::Invalidate() {
    if (Hwnd()) InvalidateRect(Hwnd(), nullptr, FALSE);
}

void ScreenWindow::Defer(std::function<void()> fn) {
    deferred_.push_back(std::move(fn));
    if (Hwnd()) PostMessageW(Hwnd(), kWmDeferred, 0, 0);
}

void ScreenWindow::RunDeferred() {
    auto list = std::move(deferred_);
    deferred_.clear();
    for (auto& fn : list) {
        if (!alive_) break;
        fn();
    }
    Invalidate();
}

void ScreenWindow::OnInput() {
    chainedFrames_ = 0;
    Invalidate();
}

void ScreenWindow::UpdateModifiers() {
    input_.ctrl = GetKeyState(VK_CONTROL) < 0;
    input_.shift = GetKeyState(VK_SHIFT) < 0;
    input_.alt = GetKeyState(VK_MENU) < 0;
}

void ScreenWindow::UpdateMouse(LPARAM lp) {
    const float s = window_.Scale();
    input_.mouseX = GET_X_LPARAM(lp) / s;
    input_.mouseY = GET_Y_LPARAM(lp) / s;
    input_.mouseInside = true;
}

void ScreenWindow::MouseMoved(LPARAM lp) {
    const float s = window_.Scale();
    const float x = GET_X_LPARAM(lp) / s, y = GET_Y_LPARAM(lp) / s;
    if (mouseLocked_) {
        // 動きを数えてから、カーソルをドラッグを始めた所へ戻す。こうすれば画面の端に届かない。
        input_.mouseDX += x - anchorX_;
        input_.mouseDY += y - anchorY_;
        if (x != anchorX_ || y != anchorY_) {
            POINT p{static_cast<LONG>(std::lround(anchorX_ * s)), static_cast<LONG>(std::lround(anchorY_ * s))};
            ClientToScreen(Hwnd(), &p);
            SetCursorPos(p.x, p.y);  // その自身の WM_MOUSEMOVE が固定位置で来る: 動きは 0
        }
        input_.mouseX = anchorX_;
        input_.mouseY = anchorY_;
        input_.mouseInside = true;
        return;
    }
    if (input_.mouseInside) {
        input_.mouseDX += x - input_.mouseX;
        input_.mouseDY += y - input_.mouseY;
    }
    UpdateMouse(lp);
}

void ScreenWindow::UpdateMouseLock() {
    const bool want = ctx_.MouseLockRequested();
    if (want == mouseLocked_) return;
    mouseLocked_ = want;
    if (want) {
        anchorX_ = input_.mouseX;
        anchorY_ = input_.mouseY;
        SetCursor(nullptr);
    } else {
        SetCursor(LoadCursorW(nullptr, CursorResource(cursor_)));
    }
}

void ScreenWindow::MouseButton(int button, LPARAM lp, bool down, bool doubleClick) {
    UpdateMouse(lp);
    UpdateModifiers();
    if (down) {
        // IME が変換中にクリックされたら、まず変換を確定する (どの Windows のテキストボックスも
        // 同じ)。結果は WM_IME_COMPOSITION として同期的に届くので、フォーカスされた欄は、
        // クリックがフォーカスを動かす前の、この同じフレームで受け取る。
        if (!input_.imeComposition.empty()) {
            if (HIMC imc = ImmGetContext(Hwnd())) {
                ImmNotifyIME(imc, NI_COMPOSITIONSTR, CPS_COMPLETE, 0);
                ImmReleaseContext(Hwnd(), imc);
            }
        }
        input_.down[button] = true;
        input_.pressed[button] = true;
        if (doubleClick && button == 0) input_.doubleClicked = true;
        SetCapture(Hwnd());
    } else {
        input_.down[button] = false;
        input_.released[button] = true;
        if (!input_.down[0] && !input_.down[1] && !input_.down[2]) ReleaseCapture();
    }
    OnInput();
}

// GPU が失われた (または別のウィンドウがデバイスを作り直した): 描く前に作り直す。スワップ
// チェーンとペインターのブラシはここで、画面自身の物は OnDeviceChanged で、それ以外は
// 持ち主が Gfx::Generation の変化に気づいたときに作り直す。
bool ScreenWindow::EnsureDevice() {
    platform::Gfx& gfx = app_.Gfx();
    if (gfx.Lost() && !app_.RecoverGraphics(*this)) {
        SetTimer(Hwnd(), kDeviceTimer, kDeviceRetryMs, nullptr);
        return false;
    }
    if (target_.Generation() == gfx.Generation()) return true;
    target_.Reset();  // ウィンドウが持つスワップチェーンは同時に 1 つ: 古いほうを先に
    if (!target_.Create(gfx, Hwnd(), window_.Dpi())) {
        SetTimer(Hwnd(), kDeviceTimer, kDeviceRetryMs, nullptr);  // またやってみた (失われたデバイス: そのときは Lost() がそう言う)
        return false;
    }
    painter_.DeviceChanged();
    OnDeviceChanged();
    return true;
}

void ScreenWindow::RunFrame() {
    if (!alive_ || inFrame_ || !EnsureDevice() || !target_.Context()) return;
    inFrame_ = true;
    ID2D1DeviceContext* dc = target_.Context();
    const float scale = window_.Scale();
    const float w = target_.Width() / scale;
    const float h = target_.Height() / scale;

    dc->BeginDraw();
    painter_.Begin(dc);
    // このフレームの入力はここで取り、1 フレームだけ生きる物 (押した、ホイール、打ったキー) は
    // ウィンドウ自身の記録からすぐ消す。フレームを描いている間に届いたメッセージ (その中の呼び出しが
    // メッセージキューを回すことがある) は次のフレームで数える。見られないまま、このフレームと
    // 一緒に消されることはない。
    frameInput_ = input_;
    input_.EndFrame();
    ctx_.BeginFrame(&painter_, &frameInput_, {0, 0, w, h}, platform::NowSeconds());
    OnFrame(ctx_, w, h);
    ctx_.RunOverlays();
    ctx_.EndFrame();
    painter_.End();
    const HRESULT drawn = dc->EndDraw();
    const bool shown = target_.Present();
    // このフレームの間に GPU がなくなった: 次のフレームの前に全部作り直す。
    if (drawn == D2DERR_RECREATE_TARGET || (!shown && app_.Gfx().Lost())) {
        app_.Gfx().MarkLost();
        Invalidate();
    }
    // クリックやキーは、それを見せるウィジェットを描いた後で状態を変えることがある
    // (Esc でダイアログが閉じる、クリックでメニューが開くなど)。そういうフレームの後には、
    // 必ずもう 1 フレーム続ける。
    bool hadInput = !frameInput_.keys.empty() || !frameInput_.chars.empty() || !frameInput_.imeResult.empty() || frameInput_.wheel != 0.f;
    for (int i = 0; i < 3; ++i) hadInput = hadInput || frameInput_.pressed[i] || frameInput_.released[i];
    inFrame_ = false;

    if (!alive_) return;  // OnFrame が画面を閉じたかもしれない
    UpdateIme();
    UpdateMouseLock();
    const ui::Cursor want = ctx_.GetCursor();
    if (want != cursor_) {
        cursor_ = want;
        if (input_.mouseInside && !mouseLocked_) SetCursor(LoadCursorW(nullptr, CursorResource(cursor_)));
    }
    // 状態を変えたフレーム (メニューを開いた、ボタンを押したなど) の後にはもう 1 フレーム続け、
    // フレームの前のほうで描いた物も新しい状態を見られるようにする。回数に上限がある。
    if ((ctx_.RedrawRequested() || hadInput) && ++chainedFrames_ < 4) Invalidate();
    // 何かが勝手に動いている (飛んでいるカメラ): フレームを続けて描く。
    if (ctx_.AnimationRequested()) Invalidate();
}

void ScreenWindow::UpdateIme() {
    HWND h = Hwnd();
    const bool want = ctx_.WantsTextInput();
    if (want != imeOn_) {
        if (!want) {
            if (HIMC imc = ImmGetContext(h)) {
                ImmNotifyIME(imc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
                ImmReleaseContext(h, imc);
            }
            input_.imeComposition.clear();
        }
        ImmAssociateContextEx(h, nullptr, want ? IACE_DEFAULT : 0);
        imeOn_ = want;
    }
    if (want != caretTimer_) {
        if (want) {
            SetTimer(h, kCaretTimer, 500, nullptr);
        } else {
            KillTimer(h, kCaretTimer);
        }
        caretTimer_ = want;
    }
    if (!want) return;
    if (HIMC imc = ImmGetContext(h)) {
        const ui::Rect cr = ctx_.CaretRect();
        const float s = window_.Scale();
        COMPOSITIONFORM cf{};
        cf.dwStyle = CFS_POINT;
        cf.ptCurrentPos = {static_cast<LONG>(cr.x * s), static_cast<LONG>(cr.y * s)};
        ImmSetCompositionWindow(imc, &cf);
        CANDIDATEFORM cand{};
        cand.dwIndex = 0;
        cand.dwStyle = CFS_EXCLUDE;
        cand.ptCurrentPos = {static_cast<LONG>(cr.x * s), static_cast<LONG>(cr.Bottom() * s)};
        cand.rcArea = {static_cast<LONG>(cr.x * s), static_cast<LONG>(cr.y * s), static_cast<LONG>(cr.Right() * s),
                       static_cast<LONG>(cr.Bottom() * s)};
        ImmSetCandidateWindow(imc, &cand);
        ImmReleaseContext(h, imc);
    }
}

LRESULT ScreenWindow::OnMessage(HWND h, UINT m, WPARAM wp, LPARAM lp, bool& handled) {
    if (!alive_) return 0;  // 扱わなかった -> DefWindowProc
    {
        LRESULT result = 0;
        if (OnWindowMessage(m, wp, lp, result)) {
            handled = true;
            return result;
        }
    }
    handled = true;
    switch (m) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(h, &ps);
            EndPaint(h, &ps);
            RunFrame();
            return 0;
        }
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED) {
                target_.Resize(LOWORD(lp), HIWORD(lp));
                RunFrame();
            }
            return 0;
        case WM_DPICHANGED:
            target_.SetDpi(static_cast<float>(HIWORD(wp)));
            Invalidate();
            return 0;
        case WM_MOUSEMOVE:
            MouseMoved(lp);
            UpdateModifiers();
            if (!trackingMouse_) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, h, 0};
                TrackMouseEvent(&tme);
                trackingMouse_ = true;
            }
            OnInput();
            return 0;
        case WM_MOUSELEAVE:
            trackingMouse_ = false;
            input_.mouseInside = false;
            OnInput();
            return 0;
        case WM_LBUTTONDOWN: MouseButton(0, lp, true, false); return 0;
        case WM_LBUTTONDBLCLK: MouseButton(0, lp, true, true); return 0;
        case WM_LBUTTONUP: MouseButton(0, lp, false, false); return 0;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK: MouseButton(1, lp, true, false); return 0;
        case WM_RBUTTONUP: MouseButton(1, lp, false, false); return 0;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONDBLCLK: MouseButton(2, lp, true, false); return 0;
        case WM_MBUTTONUP: MouseButton(2, lp, false, false); return 0;
        case WM_MOUSEWHEEL:
            input_.wheel += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA;
            OnInput();
            return 0;
        case WM_CAPTURECHANGED:
        case WM_KILLFOCUS:
            if (m == WM_KILLFOCUS) std::fill(std::begin(input_.keyDown), std::end(input_.keyDown), false);  // キーを離したことは別の所へ行く
            for (int i = 0; i < 3; ++i) {
                if (input_.down[i]) input_.released[i] = true;
                input_.down[i] = false;
            }
            OnInput();
            handled = false;
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            UpdateModifiers();
            // Ctrl+Alt+Shift+F12: GPU が失われたことにする (本当にリセットせずに、戻る道を試すため)。
            if (wp == VK_F12 && input_.ctrl && input_.alt && input_.shift) {
                log::Warn("GPU のデバイスが失われたことにします (Ctrl+Alt+Shift+F12: 作り直しの試し)");
                app_.Gfx().MarkLost();
                Invalidate();
                return 0;
            }
            const ui::Key k = MapKey(wp);
            if (k != ui::Key::None) {
                input_.keys.push_back({k, input_.ctrl, input_.shift, input_.alt});
                input_.keyDown[static_cast<int>(k)] = true;
                OnInput();
            }
            handled = m == WM_KEYDOWN;  // Alt+F4 / Alt+Space が働くようにしておく
            return 0;
        }
        case WM_KEYUP: {
            UpdateModifiers();
            const ui::Key k = MapKey(wp);
            if (k != ui::Key::None) {
                input_.keyDown[static_cast<int>(k)] = false;
                OnInput();
            }
            return 0;
        }
        case WM_SYSKEYUP: {
            // Alt を押している間に離したキー (Alt+Space、Alt+W など) は WM_KEYUP ではなくここに来る。
            // それも離れたことにする (2026-11-19: 前は押されたままになり、3D のシーンビューが勝手に飛び続けた)。
            UpdateModifiers();
            if (const ui::Key k = MapKey(wp); k != ui::Key::None) {
                input_.keyDown[static_cast<int>(k)] = false;
                OnInput();
            }
            // Alt や F10 だけを押すと (空の) システムメニューのモードに入り、次のキーを飲み込んでしまう。
            if (wp == VK_MENU || wp == VK_F10) return 0;
            handled = false;
            return 0;
        }
        case WM_CHAR: {
            const wchar_t ch = static_cast<wchar_t>(wp);
            if (ch >= 0x20 && ch != 0x7F) {
                input_.chars.push_back(ch);
                OnInput();
            }
            return 0;
        }
        case WM_IME_SETCONTEXT:
            // 変換中の文字列は自分で描く (欄の中にそのまま)。
            return DefWindowProcW(h, m, wp, lp & ~static_cast<LPARAM>(ISC_SHOWUICOMPOSITIONWINDOW));
        case WM_IME_STARTCOMPOSITION:
            UpdateIme();
            return 0;
        case WM_IME_COMPOSITION: {
            if (HIMC imc = ImmGetContext(h)) {
                if (lp & GCS_RESULTSTR) input_.imeResult += ReadImeString(imc, GCS_RESULTSTR);
                if (lp & GCS_COMPSTR) {
                    input_.imeComposition = ReadImeString(imc, GCS_COMPSTR);
                    input_.imeCursor = (lp & GCS_CURSORPOS) ? LOWORD(ImmGetCompositionStringW(imc, GCS_CURSORPOS, nullptr, 0))
                                                            : static_cast<int>(input_.imeComposition.size());
                } else if (lp & GCS_RESULTSTR) {
                    input_.imeComposition.clear();
                }
                ImmReleaseContext(h, imc);
            }
            OnInput();
            return 0;  // 先へ渡さない。WM_IME_CHAR / WM_CHAR が重ならないように
        }
        case WM_IME_ENDCOMPOSITION:
            input_.imeComposition.clear();
            OnInput();
            return 0;
        case WM_IME_CHAR:
            return 0;
        case WM_SETCURSOR:
            if (mouseLocked_) {
                SetCursor(nullptr);  // ドラッグでカメラを回している間は隠す
                return TRUE;
            }
            if (LOWORD(lp) == HTCLIENT) {
                SetCursor(LoadCursorW(nullptr, CursorResource(cursor_)));
                return TRUE;
            }
            handled = false;
            return 0;
        case WM_TIMER:
            if (wp == kCaretTimer) {
                Invalidate();
            } else if (wp == kDeviceTimer) {
                KillTimer(h, kDeviceTimer);
                Invalidate();  // 次のフレームでもう一度 GPU を試す
            } else {
                OnTimer(wp);
            }
            return 0;
        case WM_CLOSE:
            OnCloseRequested();
            return 0;
        case WM_QUERYENDSESSION:
            return OnQueryEndSession() ? TRUE : FALSE;
        case WM_ENDSESSION:
            if (wp) OnEndSession();
            return 0;
        case kWmDeferred:
            RunDeferred();
            return 0;
        case WM_SETTINGCHANGE:
            // Windows の明るい / 暗いの設定が変わった ("PC の設定に合わせる" のため)。
            if (lp && std::wstring_view(reinterpret_cast<const wchar_t*>(lp)) == L"ImmersiveColorSet") app_.SystemThemeChanged();
            handled = false;
            return 0;
        default:
            OnOtherMessage(m, wp, lp);
            handled = false;
            return 0;
    }
}

}  // namespace rm::editor
