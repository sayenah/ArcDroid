/*
  RPCEmu - An Acorn system emulator

  Copyright (C) 2005-2010 Sarah Walker

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* ArcDroid modification (2026-10-17, lockstep-6b-m2b): test-class-only checkpoints, events and field serialization. */

/* ArcDroid modification (2026-10-06): contained static reset and explicit virtual device timing; desktop services removed. */


/* PS/2 keyboard and mouse emulation */

/*ARM command 0xFF - keyboard enters reset mode
  Sends 0xFA (ack) to ARM
  Performs self-test
  Sends 0xAA after 500-750 ms
  ARM command 0xEE - Echo - keyboard returns 0xEE
  ARM command 0xED - Set/Reset LEDs - following with argument
  ARM command 0xF3 - Set rate/delay - following with argument
  ARM command 0xF4 - Enable keyboard
  ARM command 0xF5 - Disable keyboard - cease scanning
  ARM command 0xF6 - Load default

  Keyboard acks (0xFA) after every byte
  */
#include "rpc_lockstep.h"
#include <stdint.h>
#include <string.h>

#include "rpcemu.h"
#include "rpc_timing.h"
#include "rpc_input.h"
#include "vidc20.h"
#include "mem.h"
#include "iomd.h"
#include "arm.h"
#include "i8042.h"

/* Keyboard Commands */
#define KBD_CMD_ENABLE		0xf4
#define KBD_CMD_RESET		0xff

/* Keyboard Replies */
#define KBD_REPLY_POR		0xaa	/* Power on reset */
#define KBD_REPLY_ACK		0xfa	/* Command ACK */

/* Mouse Commands */
#define AUX_SET_SCALE11		0xe6	/* Set 1:1 scaling */
#define AUX_SET_SCALE21		0xe7	/* Set 2:1 scaling */
#define AUX_SET_RES		0xe8	/* Set resolution */
#define AUX_GET_TYPE		0xf2	/* Get type */
#define AUX_SET_SAMPLE		0xf3	/* Set sample rate */
#define AUX_ENABLE_DEV		0xf4	/* Enable aux device */
#define AUX_RESEND		0xfe	/* Resend last packet */
#define AUX_RESET		0xff	/* Reset aux device */

/* Mouse Replies */
#define AUX_TEST_OK		0xaa	/* Self-test passed */
#define AUX_ACK			0xfa	/* Command byte acknowledge */

/* Bits within the IOMD PS/2 control/status registers for mouse and keyboard */
#define PS2_CONTROL_DATA_STATE	0x01
#define PS2_CONTROL_CLOCK_STATE	0x02
#define PS2_CONTROL_RXPARITY	0x04
#define PS2_CONTROL_ENABLE	0x08
#define PS2_CONTROL_RX_BUSY	0x10
#define PS2_CONTROL_RX_FULL	0x20
#define PS2_CONTROL_TX_BUSY	0x40
#define PS2_CONTROL_TX_EMPTY	0x80

#define PS2_QUEUE_SIZE 256

int kcallback = 0;
int mcallback = 0;

typedef struct {
	uint8_t	data[PS2_QUEUE_SIZE];
	int	rptr, wptr, count;
} PS2Queue;

static struct {
	int		enable;
	int		reset;
	uint8_t		stat;		/**< PS/2 control register for the keyboard */
	uint8_t		data;		/**< PS/2 data register for the keyboard */
	uint8_t		command;

	PS2Queue	queue;

	int		keys2[128];

#ifdef RPCEMU_MACOSX
	/* Non-zero if the last F12 keydown event was translated into Break because Cmd was pressed */
	int		f12transtobreak;
#endif
} kbd;


static struct {
	int x;		/**< host mouse x (absolute, including doublesize), updated via QT frontend */
	int y;		/**< host mouse y (absolute, including doublesize), updated via QT frontend */
	int buttons;	/**< Mouse button bitfield, updated via QT frontend */
	int dx;		/**< delta of relative mouse movement since previous mouse update message */
	int dy;		/**< delta of relative mouse movement since previous mouse update message */
} mouse;


static int msenable, msreset;
static uint8_t msstat;		/**< PS/2 control register for the mouse */
static uint8_t msdata;		/**< PS/2 data register for the mouse */
static int mousepoll;		/**< Are we in mouse Stream Mode */
static int msincommand;		/**< Used to store the command received that has a data byte following it */
static int justsent;
static PS2Queue msqueue;
static uint8_t mouse_type;	/**< 0 = PS/2, 3 = IMPS/2, 4 = IMEX */
static uint8_t mouse_detect_state;

static uint8_t arc_rpc_kbd_parameter;
static uint8_t oldmouseb;
static int oldz;

static inline void
keyboard_irq_rx_raise(void)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
	if (machine.model == Model_Phoebe) {
		i8042_keyboard_irq_raise();
	} else {
		iomd.irqb.status |= IOMD_IRQB_KEYBOARD_RX;
		updateirqs();
	}
}

static inline void
keyboard_irq_rx_lower(void)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
	if (machine.model == Model_Phoebe) {
		i8042_keyboard_irq_lower();
	} else {
		iomd.irqb.status &= ~IOMD_IRQB_KEYBOARD_RX;
		updateirqs();
	}
}

static inline void
keyboard_irq_tx_raise(void)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
	iomd.irqb.status |= IOMD_IRQB_KEYBOARD_TX;
	updateirqs();
}

static inline void
mouse_irq_tx_raise(void)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
	iomd.irqd.status |= IOMD_IRQD_MOUSE_TX;
	updateirqs();
}

static inline void
mouse_irq_tx_lower(void)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
	iomd.irqd.status &= ~IOMD_IRQD_MOUSE_TX;
	updateirqs();
}

static inline void
mouse_irq_rx_raise(void)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
	if (machine.model == Model_Phoebe) {
		i8042_mouse_irq_raise();
	} else {
		iomd.irqd.status |= IOMD_IRQD_MOUSE_RX;
		updateirqs();
	}
}

static inline void
mouse_irq_rx_lower(void)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
	if (machine.model == Model_Phoebe) {
		i8042_mouse_irq_lower();
	} else {
		iomd.irqd.status &= ~IOMD_IRQD_MOUSE_RX;
		updateirqs();
	}
}

static void
ps2_queue(PS2Queue *q, uint8_t b)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
	if (q->count >= PS2_QUEUE_SIZE) {
		return;
	}
	q->data[q->wptr] = b;
	if (++q->wptr == PS2_QUEUE_SIZE) {
		q->wptr = 0;
	}
	q->count++;
}

static int
calculateparity(uint8_t v)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
	int c, d = 0;

	for (c = 0; c < 8; c++) {
		if (v & (1 << c)) {
			d++;
		}
	}
	if (d & 1) {
		return 0;
	}
	return 1;
}

void
keyboard_reset(void)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
	arc_rpc_response(ARC_RPC_EVENT_KEYBOARD, &kcallback, 0);
	memset(&kbd, 0, sizeof(kbd));
    arc_rpc_kbd_parameter = 0;

	msqueue.rptr = 0;
	msqueue.wptr = 0;
	msqueue.count = 0;
	msenable = 0;
	arc_rpc_response(ARC_RPC_EVENT_MOUSE, &mcallback, 0);
	msreset = 0;
	msstat = 0;
	msincommand = 0;
	mousepoll = 0;
	justsent = 0;
	mouse_type = 0;
	mouse_detect_state = 0;

	memset(&mouse, 0, sizeof(mouse));
	memset(&msqueue, 0, sizeof(msqueue));
	msdata = oldmouseb = 0;
	oldz = 0;
}

static uint8_t
ps2_read_data(PS2Queue *q)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
	uint8_t val;

	if (q->count == 0) {
		int index = q->rptr - 1;
		if (index < 0) {
			index = PS2_QUEUE_SIZE - 1;
		}
		val = q->data[index];
	} else {
		val = q->data[q->rptr];
		if (++q->rptr == PS2_QUEUE_SIZE) {
			q->rptr = 0;
		}
		q->count--;
	}
	return val;
}

/**
 * Write to the IOMD PS/2 keyboard Data register
 *
 * @param v Value to write
 */
void
keyboard_data_write(uint8_t v)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
    if (arc_rpc_kbd_parameter) {
        if (arc_rpc_kbd_parameter == 0xf3) arc_rpc_input_typematic(v);
        arc_rpc_kbd_parameter = 0;
    } else switch (v) {
    case KBD_CMD_RESET:
        memset(&kbd.queue,0,sizeof(kbd.queue));
        arc_rpc_input_scanning(false);
        kbd.reset = 2;
        arc_rpc_response(ARC_RPC_EVENT_KEYBOARD, &kcallback, 16);
        return;
    case 0xf3: case 0xed: case 0xf0: arc_rpc_kbd_parameter = v; break;
    case 0xf4: arc_rpc_input_scanning(true); break;
    case 0xf5: arc_rpc_input_scanning(false); break;
    case 0xf6: arc_rpc_input_typematic(0x2b);arc_rpc_input_scanning(true);break;
    case 0xee: ps2_queue(&kbd.queue,0xee);goto queued;
    default: break;
    }
    ps2_queue(&kbd.queue,KBD_REPLY_ACK);
queued:
    kbd.command = 0xfe;
    if (!kcallback && !(kbd.stat & PS2_CONTROL_RX_FULL))
        arc_rpc_response(ARC_RPC_EVENT_KEYBOARD, &kcallback, 4);

}

/**
 * Write to the IOMD PS/2 keyboard Control register
 *
 * @param v Value to write
 */
void
keyboard_control_write(uint8_t v)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
	if (v && !kbd.enable) {
		kbd.reset = 1;
		arc_rpc_response(ARC_RPC_EVENT_KEYBOARD, &kcallback, 5 * 4);
	}
    kbd.enable = v != 0;
	if (v) {
		kbd.stat |= PS2_CONTROL_ENABLE;
	} else {
		kbd.stat &= ~PS2_CONTROL_ENABLE;
	}
}

static void
keyboardsend(uint8_t v)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
	kbd.data = v;
	kbd.stat |= PS2_CONTROL_RX_FULL;
	if (calculateparity(v)) {
		kbd.stat |= PS2_CONTROL_RXPARITY;
	} else {
		kbd.stat &= ~PS2_CONTROL_RXPARITY;
	}
	keyboard_irq_rx_raise();
}

/* Cannot be called keyboard_callback() due to allegro name clash */
void
keyboard_callback_rpcemu(void)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
	PS2Queue *q = &kbd.queue;
    /* TX link readiness must not be blocked by a pre-enable unread make. */
    if ((kbd.stat & PS2_CONTROL_RX_FULL) && kbd.reset != 1) {
        /* RX owns the wire until drained. Keep reset phases live even when
         * the last queued make has already cleared kbd.command. */
        if (kbd.reset) arc_rpc_response(ARC_RPC_EVENT_KEYBOARD, &kcallback, 5 * 4);
        return;
    }

	if (kbd.reset == 1) {
		kbd.reset = 0;
		kbd.stat |= PS2_CONTROL_TX_EMPTY;
		keyboard_irq_tx_raise();

	} else if (kbd.reset == 2) {
		kbd.reset = 3;
        keyboardsend(KBD_REPLY_ACK);
		arc_rpc_response(ARC_RPC_EVENT_KEYBOARD, &kcallback, 500 * 4);

	} else if (kbd.reset == 3) {
		arc_rpc_response(ARC_RPC_EVENT_KEYBOARD, &kcallback, 0);
		kbd.reset = 0;
		keyboardsend(KBD_REPLY_POR);
        /* Reset restores scanning; physical held switches survive in input. */
        arc_rpc_input_typematic(0x2b);
        arc_rpc_input_scanning(true);

	} else switch (kbd.command) {
	case 1:
	case KBD_CMD_ENABLE:
		keyboardsend(KBD_REPLY_ACK);
		arc_rpc_response(ARC_RPC_EVENT_KEYBOARD, &kcallback, 0);
		kbd.command = 0;
		break;

	case 0xfe:
        if (!q->count) { kbd.command = 0; break; }
        keyboardsend(ps2_read_data(q));
		arc_rpc_response(ARC_RPC_EVENT_KEYBOARD, &kcallback, 0);
		if (q->count == 0) {
			kbd.command = 0;
		}
		break;
	}
}

/**
 * Read from the IOMD PS/2 keyboard Status register
 *
 * @return Value of register
 */
uint8_t
keyboard_status_read(void)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
	return kbd.stat;
}

/**
 * Read from the IOMD PS/2 keyboard Data register
 *
 * @return Value of register
 */
uint8_t
keyboard_data_read(void)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
	keyboard_irq_rx_lower();
	kbd.stat &= ~PS2_CONTROL_RX_FULL;
	if (!kcallback && (kbd.command == 0xfe || kbd.reset == 2 || kbd.reset == 3)) {
		arc_rpc_response(ARC_RPC_EVENT_KEYBOARD, &kcallback, 5 * 4);
	}
	return kbd.data;
}

/**
 * Write to the IOMD PS/2 mouse Control register
 *
 * @param v Value to write
 */
void
mouse_control_write(uint8_t v)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
//        printf("Write mouse enable %02X\n",v);

        v &= PS2_CONTROL_ENABLE;
        if (v)// && !msenable)
        {
                msreset=1;
                arc_rpc_response(ARC_RPC_EVENT_MOUSE, &mcallback, 20);
        }
	if (v)
		msstat |= PS2_CONTROL_ENABLE;
	else
		msstat &= ~PS2_CONTROL_ENABLE;
}

/**
 * Write to the IOMD PS/2 mouse Data register
 *
 * @param val Value to write
 */
void
mouse_data_write(uint8_t val)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
        /* Set BUSY flag, clear EMPTY flag */
        msstat = (msstat & 0x3f) | PS2_CONTROL_TX_BUSY;

	mouse_irq_tx_lower();

        justsent=1;
        if (msincommand)
        {
                switch (msincommand)
                {
		/* Certain commands are followed by a data byte that
		   also needs to be acknowledged */
                case AUX_SET_RES:
			ps2_queue(&msqueue, AUX_ACK);
			msincommand = 0;
			arc_rpc_response(ARC_RPC_EVENT_MOUSE, &mcallback, 20);
			return;

                case AUX_SET_SAMPLE:
			/* Special values to set sample are used to place
			   the mouse into Intellimouse or Intellimouse Explorer
			   mode */
			switch (mouse_detect_state) {
			default:
			case 0:
				if (val == 200)
					mouse_detect_state = 1;
				break;
			case 1:
				if (val == 100)
					mouse_detect_state = 2;
				else if (val == 200)
					mouse_detect_state = 3;
				else
					mouse_detect_state = 0;
				break;
			case 2:
				if (val == 80)
					mouse_type = 3; /* IMPS/2 */
				mouse_detect_state = 0;
				break;
			case 3:
				if (val == 80)
					mouse_type = 4; /* IMEX */
				mouse_detect_state = 0;
				break;
			}

			ps2_queue(&msqueue, AUX_ACK);
			msincommand = 0;
                        arc_rpc_response(ARC_RPC_EVENT_MOUSE, &mcallback, 20);
                        return;
                }
        }
        else
        {
                switch (val)
                {
                case AUX_RESET:
                        msreset=2;

			/* Turn off Stream Mode */
                        mousepoll = 0;

                        arc_rpc_response(ARC_RPC_EVENT_MOUSE, &mcallback, 20);
                        break;

                case AUX_RESEND:
                        arc_rpc_response(ARC_RPC_EVENT_MOUSE, &mcallback, 150);
                        break;

                case AUX_ENABLE_DEV:
	                ps2_queue(&msqueue, AUX_ACK);

			/* Turn on Stream Mode */
	                mousepoll = 1;

			arc_rpc_response(ARC_RPC_EVENT_MOUSE, &mcallback, 20);
                        break;

                case AUX_SET_SAMPLE:
                        msincommand = AUX_SET_SAMPLE;
			ps2_queue(&msqueue, AUX_ACK);
                        arc_rpc_response(ARC_RPC_EVENT_MOUSE, &mcallback, 20);
                        break;

                case AUX_GET_TYPE:
			ps2_queue(&msqueue, AUX_ACK);
			ps2_queue(&msqueue, mouse_type);
                        arc_rpc_response(ARC_RPC_EVENT_MOUSE, &mcallback, 20);
                        break;

                case AUX_SET_RES:
                        msincommand = AUX_SET_RES;
			ps2_queue(&msqueue, AUX_ACK);
                        arc_rpc_response(ARC_RPC_EVENT_MOUSE, &mcallback, 20);
                        break;

                case AUX_SET_SCALE21:
			ps2_queue(&msqueue, AUX_ACK);
                        arc_rpc_response(ARC_RPC_EVENT_MOUSE, &mcallback, 20);
                        break;

                case AUX_SET_SCALE11:
			ps2_queue(&msqueue, AUX_ACK);
                        arc_rpc_response(ARC_RPC_EVENT_MOUSE, &mcallback, 20);
                        break;

                default:
                        /* Unsupported PS/2 command asks the guest to resend. */
                        ps2_queue(&msqueue, 0xfe);
                        arc_rpc_response(ARC_RPC_EVENT_MOUSE, &mcallback, 20);
                }
        }
}

/**
 * Read from the IOMD PS/2 mouse Status register
 *
 * @return Value of register
 */
uint8_t
mouse_status_read(void)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
        return msstat;
}

/**
 * Read from the IOMD PS/2 mouse Data register
 *
 * @return Value of register
 */
uint8_t
mouse_data_read(void)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
        uint8_t temp = msdata;

        msstat &= ~PS2_CONTROL_RX_FULL;

        mouse_irq_rx_lower();

	/* If there's still more data to send, make sure to call us back the
	   next time */
	if (msqueue.count != 0 || msreset) {
                arc_rpc_response(ARC_RPC_EVENT_MOUSE, &mcallback, 20);
        }

        msdata = 0;
        return temp;
}

/**
 * Inform the host that the PS/2 mouse has put some data in the IOMD register
 * for it to consume, raising an interrupt
 */
static void
mouse_send(uint8_t v)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
        msdata = v;

        mouse_irq_rx_raise();

        msstat |= PS2_CONTROL_RX_FULL;

	if (calculateparity(v))
		msstat |= PS2_CONTROL_RXPARITY;
	else
		msstat &= ~PS2_CONTROL_RXPARITY;
}

/**
 * Handle sending queued PS/2 mouse messages to the emulated machine; this is
 * to introduce a slight delay between sent packets.
 *
 * Called once the mcallback variable reaches 0.
 */
void
mouse_ps2_callback(void)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
	if (mcallback != 0) fatal("Mouse callback invariant");
    if (msstat & PS2_CONTROL_RX_FULL) return;

        /* Set EMPTY Flag, clear BUSY flag */
        msstat = (msstat & 0x3f) | PS2_CONTROL_TX_EMPTY;

        if (justsent)
        {
		mouse_irq_tx_raise();

                justsent=0;
        }

        if (msreset==1)
        {
		mouse_irq_tx_raise();

                msreset=3;
                msstat |= PS2_CONTROL_TX_EMPTY;      /* This should be pointless - always set above */
                arc_rpc_response(ARC_RPC_EVENT_MOUSE, &mcallback, 20);
        }
        else if (msreset==2)
        {
                msreset=3;
                mouse_send(AUX_ACK);
                arc_rpc_response(ARC_RPC_EVENT_MOUSE, &mcallback, 40);
        }
        else if (msreset==3)
        {
                arc_rpc_response(ARC_RPC_EVENT_MOUSE, &mcallback, 20);
                mouse_send(AUX_TEST_OK);
                msreset=4;
        }
        else if (msreset==4)
        {
                msreset=0;
                mouse_send(0);
                arc_rpc_response(ARC_RPC_EVENT_MOUSE, &mcallback, 0);
        }
        else
        {
		/* For the callback to be sent, there must be some PS/2 data to send */
		if (msqueue.count == 0) return; /* A guest RESEND may have no packet. */

		/* Send the next byte of PS/2 data to the host */
                mouse_send(ps2_read_data(&msqueue));
        }
}

/**
 * Main interface to inform the emulated machine of changes in host OS mouse
 * movement (Quadrature & PS/2) and buttons (PS/2 only).
 *
 * This function returns is only called in Capture mouse mode. 'mousehack'
 * uses RISC OS specific SWI interception to provide the mouse data.
 *
 * Called when mouse system recieves mouse movement and button updates
 * from front end
 */
static void
mouse_process(void)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
	int x, y;
	int z, tmpz;
	uint8_t mouseb = mouse.buttons & 7;
	uint8_t b;



	/* Get the relative X/Y movements since the last call */
	x = mouse.dx;
	y = mouse.dy;
	mouse.dx = mouse.dy = 0;

	/* Get the absolute value of the scroll wheel position */
//	z = mouse_z; /* Allegro */
	z = 0; /* HACK */


	/* Update quadrature mouse */
	iomd.mousex = (iomd.mousex + (uint32_t)x) & 0xffffu;
	iomd.mousey = (iomd.mousey - (uint32_t)y) & 0xffffu; /* Allegro and RPC Y axis go in opposite directions */

        /* Return if not PS/2 mouse */
        if (machine.model != Model_A7000 && machine.model != Model_A7000plus && machine.model != Model_Phoebe) {
                return;
        }

	/* Are we in PS/2 Stream Mode? */
	if (!mousepoll) {
		return;
	}

	/* Has anything changed from previous poll? */
	if (x == 0 && y == 0 && (mouseb == oldmouseb) &&
	    (mouse_type == 0 || (mouse_type != 0 && z == oldz)))
	{
		return;
	}

        oldmouseb=mouseb;

	/* Maximum range you can fit in one PS/2 movement packet is -256 to 255 */
	/* TODO why not send multiple move packets? */
        if (x<-256) x=-256;
        if (x>255) x=255;
        if (y<-256) y=-256;
        if (y>255) y=255;

        y = -y;

	/* Calculate relative scrollwheel position from last call */
	tmpz = oldz - z;
	oldz = z;
	z = tmpz;

	/* Send PS/2 button/movement packet */
	{
		uint8_t tmp;

		if (config.mousetwobutton) {
			/* To help people with only two buttons on their mouse,
			   swap the behaviour of middle and right buttons */
			uint8_t mousel = mouseb & 1;
			uint8_t mouser = (mouseb & 2) >> 1;
			uint8_t mousem = (mouseb & 4) >> 2;

			mouseb = mousel | (mousem << 1) | (mouser << 2);
		}

		tmp = (mouseb & 7) | 8;

		if (x & 0x100) {
			tmp |= 0x10; /* X overflow bit */
		}
		if (y & 0x100) {
			tmp |= 0x20; /* Y overflow bit */
		}
		ps2_queue(&msqueue, tmp);
	}
	ps2_queue(&msqueue, x & 255);
	ps2_queue(&msqueue, y & 255);

	/* Extra byte for IMPS/2 or IMEX */
	switch (mouse_type) {
	default:
		break;
	case 3:
		if (z > 127)
			z = 127;
		else if (z < -127)
			z = -127;
		ps2_queue(&msqueue, z & 0xff);
		break;
	case 4:
		if (z > 7)
			z = 7;
		else if (z < -7)
			z = -7;
		b = z & 0x0f;
		ps2_queue(&msqueue, b);
		break;
	}

    if (!mcallback && !(msstat & PS2_CONTROL_RX_FULL))
        arc_rpc_response(ARC_RPC_EVENT_MOUSE, &mcallback, 20);
}

/**
 * Relative movements used in mouse capture modes
 * 
 * @param dx
 * @param dy
 */
void
mouse_mouse_move_relative(int dx, int dy)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));


	mouse.dx = dx;
	mouse.dy = dy;


	mouse_process();
}

/**
 * 
 * 
 * @param buttons Mouse button bitfield
 */
void
mouse_mouse_press(int buttons)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
	mouse.buttons |= buttons;

	// Capture mode
	if(!mousehack) {
		mouse_process();
	}
}

/**
 * 
 * 
 * @param buttons Mouse button bitfield
 */
void
mouse_mouse_release(int buttons)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
	mouse.buttons &= ~buttons;

	// Capture mode
	if(!mousehack) {
		mouse_process();
	}
}

/**
 * Used in iomd for Quadrature mouse button processing
 */
int
mouse_buttons_get(void)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
	return mouse.buttons;
}

/**
 * @param scan_codes pointer to array of ps2 set2 scan codes
 */
void
keyboard_key_press(const uint8_t *scan_codes)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
	if (scan_codes == NULL || arc_rpc_keyboard_room() < 8) {
		return;
	}

	ps2_queue(&kbd.queue, scan_codes[0]);
	if (scan_codes[0] == 0xe0) {
		/* 2-byte scan code */
		ps2_queue(&kbd.queue, scan_codes[1]);
	} else if (scan_codes[0] == 0xe1) {
		/* 8-byte scan code (only break key) */
		ps2_queue(&kbd.queue, scan_codes[1]);
		ps2_queue(&kbd.queue, scan_codes[2]);
		ps2_queue(&kbd.queue, scan_codes[3]);
		ps2_queue(&kbd.queue, scan_codes[4]);
		ps2_queue(&kbd.queue, scan_codes[5]);
		ps2_queue(&kbd.queue, scan_codes[6]);
		ps2_queue(&kbd.queue, scan_codes[7]);
	}
    if (!kcallback && !(kbd.stat & PS2_CONTROL_RX_FULL))
        arc_rpc_response(ARC_RPC_EVENT_KEYBOARD, &kcallback, 20);
    kbd.command = 0xfe;
}

/**
 * @param scan_codes pointer to array of ps2 set2 scan codes
 */
void
keyboard_key_release(const uint8_t *scan_codes)
{
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard));
	if (scan_codes == NULL || arc_rpc_keyboard_room() < 8) {
		return;
	}

	if (scan_codes[0] == 0xe1) return; /* Break has no release code. */

	if (scan_codes[1] == 0) {
		/* 1-byte scan code */
		ps2_queue(&kbd.queue, 0xf0); /* key-up modifier */
		ps2_queue(&kbd.queue, scan_codes[0]); /* byte */
	} else {
		/* 2-byte scan code */
		ps2_queue(&kbd.queue, scan_codes[0]); /* first byte */
		ps2_queue(&kbd.queue, 0xf0); /* key-up modifier */
		ps2_queue(&kbd.queue, scan_codes[1]); /* second byte */
	}
    if (!kcallback && !(kbd.stat & PS2_CONTROL_RX_FULL))
        arc_rpc_response(ARC_RPC_EVENT_KEYBOARD, &kcallback, 20);
    kbd.command = 0xfe;
}

unsigned arc_rpc_keyboard_room(void) {
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard)); return PS2_QUEUE_SIZE - kbd.queue.count; }
unsigned arc_rpc_mouse_room(void) {
    ARC_RPC_LOCKSTEP(dirty(ARC_RPC_LS_TU_keyboard)); return PS2_QUEUE_SIZE - msqueue.count; }

#if defined(ARCDROID_RPC_LOCKSTEP)
ARC_RPC_LS_SERIALIZE_keyboard
#endif
