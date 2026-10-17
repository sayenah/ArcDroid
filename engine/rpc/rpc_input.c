/* ArcDroid arbitrated input to PS/2 set 2 and model-correct mouse. GPL-2.0-only. */
#include "rpc_lockstep.h"
#include "rpc_input.h"
#include "rpc_timing.h"
#include "rpcemu.h"
#include "keyboard.h"
#include "arcdroid_core.h"
#include "libretro.h"
#include <string.h>
/* Set 2 bytes indexed by libretro key IDs; no desktop scancode dependency. */
static const uint8_t arc_rpc_letters[26] = {
    0x1c,0x32,0x21,0x23,0x24,0x2b,0x34,0x33,0x43,0x3b,0x42,0x4b,0x3a,
    0x31,0x44,0x4d,0x15,0x2d,0x1b,0x2c,0x3c,0x2a,0x1d,0x22,0x35,0x1a};
static const uint8_t arc_rpc_digits[10] = {0x45,0x16,0x1e,0x26,0x25,0x2e,0x36,0x3d,0x3e,0x46};
static const uint8_t arc_rpc_function[12] = {0x05,0x06,0x04,0x0c,0x03,0x0b,0x83,0x0a,0x01,0x09,0x78,0x07};
static const uint8_t arc_rpc_keypad[10] = {0x70,0x69,0x72,0x7a,0x6b,0x73,0x74,0x6c,0x75,0x7d};
static bool arc_rpc_held[ARC_INPUT_KEY_COUNT], arc_rpc_sent[ARC_INPUT_KEY_COUNT];
static bool arc_rpc_scanning, arc_rpc_factory, arc_rpc_delete;
static unsigned arc_rpc_repeat_key, arc_rpc_repeat_param;
static uint64_t arc_rpc_repeat_due, arc_rpc_repeat_fraction;
static uint64_t arc_rpc_factory_release;
static bool arc_rpc_scan(unsigned code, uint8_t bytes[8])
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_input));
    memset(bytes,0,8);
    if(code>=RETROK_a&&code<=RETROK_z)bytes[0]=arc_rpc_letters[code-RETROK_a];
    else if(code>=RETROK_0&&code<=RETROK_9)bytes[0]=arc_rpc_digits[code-RETROK_0];
    else if(code>=RETROK_F1&&code<=RETROK_F12)bytes[0]=arc_rpc_function[code-RETROK_F1];
    else if(code>=RETROK_KP0&&code<=RETROK_KP9)bytes[0]=arc_rpc_keypad[code-RETROK_KP0];
    else switch(code) {
#define KEY(k,b) case RETROK_##k: bytes[0]=b;break
#define EXT(k,b) case RETROK_##k: bytes[0]=0xe0;bytes[1]=b;break
    KEY(ESCAPE,0x76);KEY(BACKQUOTE,0x0e);KEY(MINUS,0x4e);KEY(EQUALS,0x55);
    KEY(BACKSPACE,0x66);KEY(TAB,0x0d);KEY(LEFTBRACKET,0x54);KEY(RIGHTBRACKET,0x5b);
    KEY(RETURN,0x5a);KEY(SEMICOLON,0x4c);KEY(QUOTE,0x52);KEY(BACKSLASH,0x5d);
    KEY(COMMA,0x41);KEY(PERIOD,0x49);KEY(SLASH,0x4a);KEY(SPACE,0x29);
    KEY(LSHIFT,0x12);KEY(RSHIFT,0x59);KEY(LCTRL,0x14);KEY(LALT,0x11);
    KEY(CAPSLOCK,0x58);KEY(NUMLOCK,0x77);KEY(SCROLLOCK,0x7e);
    KEY(KP_PERIOD,0x71);KEY(KP_MULTIPLY,0x7c);KEY(KP_MINUS,0x7b);KEY(KP_PLUS,0x79);
    EXT(DELETE,0x71);EXT(INSERT,0x70);EXT(HOME,0x6c);EXT(END,0x69);
    EXT(PAGEUP,0x7d);EXT(PAGEDOWN,0x7a);EXT(UP,0x75);EXT(DOWN,0x72);
    EXT(LEFT,0x6b);EXT(RIGHT,0x74);EXT(RCTRL,0x14);EXT(RALT,0x11);
    EXT(KP_ENTER,0x5a);EXT(KP_DIVIDE,0x4a);
    case RETROK_PAUSE: { const uint8_t pause[8]={0xe1,0x14,0x77,0xe1,0xf0,0x14,0xf0,0x77};memcpy(bytes,pause,8);break; }
    default:return false;
#undef KEY
#undef EXT
    }
    return true;
}
static bool arc_rpc_effective(unsigned key)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_input)); return arc_rpc_held[key] || (key==RETROK_DELETE && arc_rpc_delete); }
static void arc_rpc_input_arm(void)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_input));
    /* Poll tick bounds ingestion and repeat reconciliation; scheduler time only.
     * The exact factory release replaces a tick if it falls between ticks. */
    uint64_t now=arc_rpc_time_ns(), due=now+1000000;
    if(arc_rpc_factory&&arc_rpc_delete&&arc_rpc_factory_release<due)due=arc_rpc_factory_release;
    if(arc_rpc_repeat_key&&arc_rpc_repeat_due>now&&arc_rpc_repeat_due<due)due=arc_rpc_repeat_due;
    arc_rpc_event_after(ARC_RPC_EVENT_INPUT,due-now);
}
void arc_rpc_input_reset(void)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_input));
    memset(arc_rpc_held,0,sizeof(arc_rpc_held));memset(arc_rpc_sent,0,sizeof(arc_rpc_sent));
    arc_rpc_scanning=true;arc_rpc_factory=arc_rpc_delete=false;
    arc_rpc_repeat_key=0;arc_rpc_repeat_param=0x2b;arc_rpc_repeat_fraction=0;
    arc_rpc_input_arm();
}
void arc_rpc_input_typematic(unsigned value)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_input)); arc_rpc_repeat_param=value&127;arc_rpc_repeat_fraction=0; }
void arc_rpc_input_scanning(bool enabled)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_input));
    arc_rpc_scanning=enabled;
    if(!enabled){memset(arc_rpc_sent,0,sizeof(arc_rpc_sent));arc_rpc_repeat_key=0;}
}
void arc_rpc_input_key(unsigned key,bool down)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_input)); if(key<ARC_INPUT_KEY_COUNT)arc_rpc_held[key]=down; }
void arc_rpc_input_factory(bool enabled)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_input));
    arc_rpc_factory=arc_rpc_delete=enabled;
    arc_rpc_factory_release=arc_rpc_time_ns()+UINT64_C(12000000000);
    if(enabled)arc_rpc_input_event(); /* Internal Delete exists at time zero. */
}
bool arc_rpc_input_delete_held(void) {
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_input)); return arc_rpc_delete; }
void arc_rpc_input_factory_release(uint64_t release_ns)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_input)); arc_rpc_factory_release=release_ns; }
void arc_rpc_input_event(void)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_rpc_input));
    uint64_t now=arc_rpc_time_ns();
    if(arc_rpc_factory&&arc_rpc_delete&&now>=arc_rpc_factory_release)arc_rpc_delete=false;
    if(arc_rpc_scanning) {
        /* Releases before new presses. ArcDroid has already arbitrated sources. */
        for(unsigned pass=0;pass<2;++pass)for(unsigned key=1;key<ARC_INPUT_KEY_COUNT;++key) {
            bool down=arc_rpc_effective(key);uint8_t bytes[8];
            if(down!=!!pass||down==arc_rpc_sent[key]||!arc_rpc_scan(key,bytes))continue;
            if(arc_rpc_keyboard_room()<8)break;
            if(down)keyboard_key_press(bytes);else keyboard_key_release(bytes);
            arc_rpc_sent[key]=down;
            if(down&&!arc_input_is_modifier(key)&&key!=RETROK_PAUSE) {
                arc_rpc_repeat_key=key;arc_rpc_repeat_fraction=0;
                arc_rpc_repeat_due=now+(uint64_t)(((arc_rpc_repeat_param>>5)&3)+1)*250000000u;
            } else if(!down&&arc_rpc_repeat_key==key)arc_rpc_repeat_key=0;
        }
        if(arc_rpc_repeat_key&&now>=arc_rpc_repeat_due) {
            uint8_t bytes[8];
            if(arc_rpc_keyboard_room()>=8&&arc_rpc_scan(arc_rpc_repeat_key,bytes))keyboard_key_press(bytes);
            /* PS/2 period = (8+A)*2^B / 240 seconds. */
            uint64_t total=arc_rpc_repeat_fraction+UINT64_C(1000000000)*
                (8+(arc_rpc_repeat_param&7))*(1u<<((arc_rpc_repeat_param>>3)&3));
            uint64_t old=arc_rpc_repeat_fraction;arc_rpc_repeat_fraction=total%240;
            arc_rpc_repeat_due+=total/240+(arc_rpc_repeat_fraction!=0)-(old!=0);
        }
    }
    arc_rpc_input_arm();
}

#if defined(ARCDROID_RPC_LOCKSTEP)
ARC_RPC_LS_SERIALIZE_rpc_input
#endif
