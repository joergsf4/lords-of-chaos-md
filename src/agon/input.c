#include "input.h"

#include "../core/chord.h"

bool input_poll(struct keyboard_event_t *e)
{
    if (!kbuf_poll_event(e))
        return false;
    if (e->vkey == 0 && e->ascii == 27)
        e->vkey = VK_ESC;
    return true;
}

/* Movement by arrow keys only (GDD 5.2): WASD is NOT mapped - w, a, s
 * and d are action keys there (wield, plus d = drop; a/s unused), and
 * the arrow branch of the event loop would shadow them. */
uint8_t input_arrow(uint8_t vkey)
{
    switch (vkey) {
    case VK_UP: return ARROW_UP;
    case VK_DOWN: return ARROW_DOWN;
    case VK_LEFT: return ARROW_LEFT;
    case VK_RIGHT: return ARROW_RIGHT;
    default: break;
    }
    switch (vkey) {                      /* numpad, NumLock on (D88) */
    case VK_KP_1: return ARROW_DOWN | ARROW_LEFT;
    case VK_KP_1 + 1: return ARROW_DOWN;
    case VK_KP_1 + 2: return ARROW_DOWN | ARROW_RIGHT;
    case VK_KP_1 + 3: return ARROW_LEFT;
    case VK_KP_1 + 5: return ARROW_RIGHT;
    case VK_KP_1 + 6: return ARROW_UP | ARROW_LEFT;
    case VK_KP_1 + 7: return ARROW_UP;
    case VK_KP_9: return ARROW_UP | ARROW_RIGHT;
    default: return 0;
    }
}

uint8_t input_diagonal(uint8_t vkey)
{
    switch (vkey) {
    case VK_HOME: return ARROW_UP | ARROW_LEFT;
    case VK_PGUP: return ARROW_UP | ARROW_RIGHT;
    case VK_END: return ARROW_DOWN | ARROW_LEFT;
    case VK_PGDN: return ARROW_DOWN | ARROW_RIGHT;
    default: return 0;
    }
}
