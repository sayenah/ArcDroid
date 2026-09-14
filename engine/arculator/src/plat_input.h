#ifndef _PLAT_INPUT_H_
#define _PLAT_INPUT_H_

/* ArcDroid modification: use libretro's stable RETROK namespace directly;
 * the SDL platform keyboard header is deliberately not part of this port. */
#include "libretro.h"

void input_init();
void input_close();

void mouse_poll_host();
void mouse_get_mickeys(int *x, int *y);
int mouse_get_buttons();
void mouse_capture_enable();
void mouse_capture_disable();

void get_mouse_deltas(int *x, int *y, int *b);

void keyboard_poll_host();
extern int key[512];

#define KEY_ESC RETROK_ESCAPE
#define KEY_1 RETROK_1
#define KEY_2 RETROK_2
#define KEY_3 RETROK_3
#define KEY_4 RETROK_4
#define KEY_5 RETROK_5
#define KEY_6 RETROK_6
#define KEY_7 RETROK_7
#define KEY_8 RETROK_8
#define KEY_9 RETROK_9
#define KEY_0 RETROK_0
#define KEY_MINUS RETROK_MINUS
#define KEY_EQUALS RETROK_EQUALS
#define KEY_BACKSPACE RETROK_BACKSPACE
#define KEY_TAB RETROK_TAB
#define KEY_Q RETROK_q
#define KEY_W RETROK_w
#define KEY_E RETROK_e
#define KEY_R RETROK_r
#define KEY_T RETROK_t
#define KEY_Y RETROK_y
#define KEY_U RETROK_u
#define KEY_I RETROK_i
#define KEY_O RETROK_o
#define KEY_P RETROK_p
#define KEY_OPENBRACE RETROK_LEFTBRACKET
#define KEY_CLOSEBRACE RETROK_RIGHTBRACKET
#define KEY_ENTER RETROK_RETURN
#define KEY_CONTROL RETROK_LCTRL
#define KEY_LCONTROL RETROK_LCTRL
#define KEY_A RETROK_a
#define KEY_S RETROK_s
#define KEY_D RETROK_d
#define KEY_F RETROK_f
#define KEY_G RETROK_g
#define KEY_H RETROK_h
#define KEY_J RETROK_j
#define KEY_K RETROK_k
#define KEY_L RETROK_l
#define KEY_COLON RETROK_SEMICOLON
#define KEY_SEMICOLON RETROK_SEMICOLON
#define KEY_QUOTE RETROK_QUOTE
#define KEY_TILDE RETROK_BACKQUOTE
#define KEY_LSHIFT RETROK_LSHIFT
#define KEY_BACKSLASH RETROK_BACKSLASH
#define KEY_Z RETROK_z
#define KEY_X RETROK_x
#define KEY_C RETROK_c
#define KEY_V RETROK_v
#define KEY_B RETROK_b
#define KEY_N RETROK_n
#define KEY_M RETROK_m
#define KEY_COMMA RETROK_COMMA
#define KEY_STOP RETROK_PERIOD
#define KEY_SLASH RETROK_SLASH
#define KEY_RSHIFT RETROK_RSHIFT
#define KEY_ASTERISK RETROK_KP_MULTIPLY
#define KEY_ALT RETROK_LALT
#define KEY_SPACE RETROK_SPACE
#define KEY_CAPSLOCK RETROK_CAPSLOCK
#define KEY_F1 RETROK_F1
#define KEY_F2 RETROK_F2
#define KEY_F3 RETROK_F3
#define KEY_F4 RETROK_F4
#define KEY_F5 RETROK_F5
#define KEY_F6 RETROK_F6
#define KEY_F7 RETROK_F7
#define KEY_F8 RETROK_F8
#define KEY_F9 RETROK_F9
#define KEY_F10 RETROK_F10
#define KEY_F11 RETROK_F11
#define KEY_F12 RETROK_F12
#define KEY_NUMLOCK RETROK_NUMLOCK
#define KEY_SCRLOCK RETROK_SCROLLOCK
#define KEY_HOME RETROK_HOME
#define KEY_UP RETROK_UP
#define KEY_PGUP RETROK_PAGEUP
#define KEY_MINUS_PAD RETROK_KP_MINUS
#define KEY_LEFT RETROK_LEFT
#define KEY_RIGHT RETROK_RIGHT
#define KEY_PLUS_PAD RETROK_KP_PLUS
#define KEY_END RETROK_END
#define KEY_DOWN RETROK_DOWN
#define KEY_PGDN RETROK_PAGEDOWN
#define KEY_INSERT RETROK_INSERT
#define KEY_DEL RETROK_DELETE
#define KEY_PRTSCR RETROK_PRINT
#define KEY_LWIN RETROK_LMETA
#define KEY_RWIN RETROK_RMETA
#define KEY_MENU RETROK_MENU
#define KEY_RCONTROL RETROK_RCTRL
#define KEY_ALTGR RETROK_RALT
#define KEY_SLASH2 RETROK_SLASH
#define KEY_PAUSE RETROK_PAUSE
#define KEY_0_PAD RETROK_KP0
#define KEY_1_PAD RETROK_KP1
#define KEY_2_PAD RETROK_KP2
#define KEY_3_PAD RETROK_KP3
#define KEY_4_PAD RETROK_KP4
#define KEY_5_PAD RETROK_KP5
#define KEY_6_PAD RETROK_KP6
#define KEY_7_PAD RETROK_KP7
#define KEY_8_PAD RETROK_KP8
#define KEY_9_PAD RETROK_KP9
#define KEY_SLASH_PAD RETROK_KP_DIVIDE
#define KEY_BACKSLASH2 RETROK_BACKSLASH
#define KEY_DEL_PAD RETROK_KP_PERIOD
#define KEY_ENTER_PAD RETROK_KP_ENTER

#endif /*_PLAT_INPUT_H_*/
