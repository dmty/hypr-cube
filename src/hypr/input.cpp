#include "input.hpp"
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/event/EventBus.hpp>

namespace hypr {
namespace {

struct DragListeners {
    CHyprSignalListener move, button, key;
};

DragListeners g_listeners;
Vector2D      g_lastCoords;
bool          g_haveLastCoords = false;

}

void beginDragGrab() {
    g_haveLastCoords = false;

    g_listeners.move = Event::bus()->m_events.input.mouse.move.listen(
        [](Vector2D, Event::SCallbackInfo& info) {
            // A listener that somehow outlives its session (it shouldn't, but must fail safe
            // if it does) must not swallow input with nothing left to drive: check before
            // cancelling, not after, so a stale listener is inert rather than a permanent trap.
            if (!g_session) return;
            info.cancelled = true;
            // The pointer still physically moves; track absolute position and diff it.
            const Vector2D now = g_pInputManager->getMouseCoordsInternal();
            if (g_haveLastCoords)
                g_session->state.addDragDelta((float)(now.x - g_lastCoords.x));
            g_lastCoords     = now;
            g_haveLastCoords = true;
        });

    g_listeners.button = Event::bus()->m_events.input.mouse.button.listen(
        [](IPointer::SButtonEvent e, Event::SCallbackInfo& info) {
            if (!g_session) return;
            info.cancelled = true;
            if (e.state == WL_POINTER_BUTTON_STATE_RELEASED) {
                g_session->state.release(nowMs());
                endDragGrab();
            }
        });

    g_listeners.key = Event::bus()->m_events.input.keyboard.key.listen(
        [](IKeyboard::SKeyEvent e, Event::SCallbackInfo& info) {
            if (!g_session) return;
            // Swallow everything while a drag is live: otherwise keystrokes reach the
            // focused window while the desktop is spinning.
            info.cancelled = true;
            // evdev KEY_ESC == 1; IKeyboard::SKeyEvent::keycode carries the evdev value
            // (Hyprland adds the xkbcommon +8 offset itself, downstream of this event).
            if (e.keycode == 1 && e.state == WL_KEYBOARD_KEY_STATE_PRESSED) {
                g_session->state.abort(nowMs());
                endDragGrab();
            }
        });
}

void endDragGrab() {
    g_listeners.move.reset();
    g_listeners.button.reset();
    g_listeners.key.reset();
    g_haveLastCoords = false;
}

}
