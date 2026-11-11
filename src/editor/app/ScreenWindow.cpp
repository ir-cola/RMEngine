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
constexpr UINT_PTR kDeviceTimer = 2;  // the GPU could not be made again: try again in a moment
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
        case VK_F2: return ui::Key::F2;
        case VK_F5: return ui::Key::F5;
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
        default: return ui::Key::None;  // includes VK_PROCESSKEY (keys eaten by the IME)
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
    desc.captionRgb = theme::CaptionRgb;  // the title bar in the colors of the theme in use
    desc.darkCaption = !theme::IsLight;
    if (!window_.Create(desc, this)) return false;
    if (!target_.Create(app_.Gfx(), window_.Handle(), window_.Dpi())) return false;
    painter_.Init(app_.Gfx().DWrite());
    ctx_.getClipboardText = [this] { return platform::GetClipboardText(Hwnd()); };
    ctx_.setClipboardText = [this](const std::wstring& s) { platform::SetClipboardText(Hwnd(), s); };
    // The IME is off until a text field takes focus.
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
        // Count the move, then put the cursor back where the drag started: it never reaches a screen edge.
        input_.mouseDX += x - anchorX_;
        input_.mouseDY += y - anchorY_;
        if (x != anchorX_ || y != anchorY_) {
            POINT p{static_cast<LONG>(std::lround(anchorX_ * s)), static_cast<LONG>(std::lround(anchorY_ * s))};
            ClientToScreen(Hwnd(), &p);
            SetCursorPos(p.x, p.y);  // its own WM_MOUSEMOVE comes at the anchor: a move of 0
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
        // A click while the IME is composing commits the composition first (like every Windows
        // text box). The result arrives synchronously as WM_IME_COMPOSITION, so the focused field
        // receives it in this same frame, before the click moves the focus.
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

// The GPU lost (or its devices made again by another window): made again before drawing - the
// swap chain and the painter's brushes here, the screen's own things in OnDeviceChanged, everything
// else by its owner when it sees Gfx::Generation change.
bool ScreenWindow::EnsureDevice() {
    platform::Gfx& gfx = app_.Gfx();
    if (gfx.Lost() && !app_.RecoverGraphics(*this)) {
        SetTimer(Hwnd(), kDeviceTimer, kDeviceRetryMs, nullptr);
        return false;
    }
    if (target_.Generation() == gfx.Generation()) return true;
    target_.Reset();  // a window has one swap chain at a time: the old one goes first
    if (!target_.Create(gfx, Hwnd(), window_.Dpi())) {
        SetTimer(Hwnd(), kDeviceTimer, kDeviceRetryMs, nullptr);  // tried again (a lost device: Lost() says so then)
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
    ctx_.BeginFrame(&painter_, &input_, {0, 0, w, h}, platform::NowSeconds());
    OnFrame(ctx_, w, h);
    ctx_.RunOverlays();
    ctx_.EndFrame();
    painter_.End();
    const HRESULT drawn = dc->EndDraw();
    const bool shown = target_.Present();
    // The GPU went away during this frame: everything is made again before the next one.
    if (drawn == D2DERR_RECREATE_TARGET || (!shown && app_.Gfx().Lost())) {
        app_.Gfx().MarkLost();
        Invalidate();
    }
    // Clicks and keys may change state after the widgets that show it were drawn
    // (Esc closing a dialog, a click opening a menu). Such a frame is always followed by one more.
    bool hadInput = !input_.keys.empty() || !input_.chars.empty() || !input_.imeResult.empty() || input_.wheel != 0.f;
    for (int i = 0; i < 3; ++i) hadInput = hadInput || input_.pressed[i] || input_.released[i];
    input_.EndFrame();
    inFrame_ = false;

    if (!alive_) return;  // OnFrame may have closed the screen
    UpdateIme();
    UpdateMouseLock();
    const ui::Cursor want = ctx_.GetCursor();
    if (want != cursor_) {
        cursor_ = want;
        if (input_.mouseInside && !mouseLocked_) SetCursor(LoadCursorW(nullptr, CursorResource(cursor_)));
    }
    // A frame that changed state (opened a menu, clicked a button ...) is followed by one
    // more frame so everything drawn earlier in the frame sees the new state. Bounded.
    if ((ctx_.RedrawRequested() || hadInput) && ++chainedFrames_ < 4) Invalidate();
    // Something is moving by itself (a flying camera): keep drawing, frame after frame.
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
    if (!alive_) return 0;  // not handled -> DefWindowProc
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
            if (m == WM_KILLFOCUS) std::fill(std::begin(input_.keyDown), std::end(input_.keyDown), false);  // the key-up goes elsewhere
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
            // Ctrl+Alt+Shift+F12: as if the GPU were lost (to try the way back without a real reset).
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
            handled = m == WM_KEYDOWN;  // keep Alt+F4 / Alt+Space working
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
        case WM_SYSKEYUP:
            // A lone Alt / F10 would enter the (empty) system menu mode and swallow the next keys.
            if (wp == VK_MENU || wp == VK_F10) return 0;
            handled = false;
            return 0;
        case WM_CHAR: {
            const wchar_t ch = static_cast<wchar_t>(wp);
            if (ch >= 0x20 && ch != 0x7F) {
                input_.chars.push_back(ch);
                OnInput();
            }
            return 0;
        }
        case WM_IME_SETCONTEXT:
            // We draw the composition string ourselves (inline in the field).
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
            return 0;  // not passed on, so no WM_IME_CHAR / WM_CHAR duplicates
        }
        case WM_IME_ENDCOMPOSITION:
            input_.imeComposition.clear();
            OnInput();
            return 0;
        case WM_IME_CHAR:
            return 0;
        case WM_SETCURSOR:
            if (mouseLocked_) {
                SetCursor(nullptr);  // hidden while a drag turns the camera
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
                Invalidate();  // the next frame tries the GPU again
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
            // Windows' light / dark setting changed (for "PC の設定に合わせる").
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
