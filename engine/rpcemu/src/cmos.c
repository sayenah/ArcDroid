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

/* I2C + CMOS RAM emulation

  I2C and Philips PCF8583.

  Includes code from Softgun by Jochen Karrer used under the terms of the
  GPLv2.
*/

/* ArcDroid modification (2026-10-06): declared in-memory CMOS, virtual RTC,
 * full transaction reset and contained invariant faults; no host services. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>


#include "rpcemu.h"
#include "cmos.h"
#include "rpc_rtc.h"

#define dbgprintf(...) ((void)0)

int i2cclock = 1; /**< The current value of the I2C clock pin */
int i2cdata  = 1; /**< The current value of the I2C data pin */

#define BIN2BCD(val)	((((val) / 10) << 4) | ((val) % 10))

static unsigned char cmosram[256];
static uint64_t cmos_writes;
static uint32_t i2c_devices; /**< Bitfield of devices on the I2C bus */

/****************************************************************************/

/* returnvalues for the start and write I2C Operations */
#define I2C_ACK		0
#define I2C_NACK	1
#define I2C_DONE	3

#define I2C_READ	5
#define I2C_WRITE	6

/** Struct to hold the function callbacks for operations on a class
    of I2C connected device */
typedef struct {
	int (*start)(void *dev, int i2c_addr, int operation);
	void (*stop)(void *dev);
	int (*write_byte)(void *dev, uint8_t data);
	int (*read_byte)(void *dev, uint8_t *data);
	/*
	 * -----------------------------------------------------
	 * Tell device if read was acked or not
	 * The device can then decide to prepare next
	 * or trigger some finishing action
	 * -----------------------------------------------------
	 */
	void (*read_ack)(void *dev, int ack);
} I2C_SlaveOps;

/** Struct to hold the representation of a class of I2C connected device */
typedef struct {
	const I2C_SlaveOps *devops;
	void *dev;
	uint8_t address;
} I2C_Slave;

/****************************************************************************/

#define PCF_STATE_ADDR	0
#define PCF_STATE_DATA	2

/** The state of the Philips RTC chip */
typedef struct {
	I2C_Slave i2c_slave;
	uint16_t reg_address;
	int state;
} PCF8583;

/* Declared bytes are never silently patched, checksummed or persisted. */
void arc_rpc_cmos_seed(const uint8_t bytes[256])
{
    if (!bytes) fatal("Missing declared CMOS seed");
    memcpy(cmosram, bytes, sizeof(cmosram));
    cmos_writes = 0;
}
void arc_rpc_cmos_snapshot(uint8_t bytes[256]) { memcpy(bytes, cmosram, 256); }
uint64_t arc_rpc_cmos_writes(void) { return cmos_writes; }
void cmos_init(void) { memset(cmosram, 0, sizeof(cmosram)); cmos_writes = 0; }
void cmos_reset(void) { /* Volatile CMOS survives reset. Bus reset is separate. */ }
void savecmos(void) { /* Persistence is outside the R1 session contract. */ }

static void cmosgettime(void)
{
    ArcRpcCalendar t;
    if (!arc_rpc_rtc_read(&t)) fatal("RTC outside declared epoch range");
    cmosram[1] = BIN2BCD(t.centisecond);
    cmosram[2] = BIN2BCD(t.second);
    cmosram[3] = BIN2BCD(t.minute);
    cmosram[4] = BIN2BCD(t.hour);
    cmosram[5] = ((t.year & 3) << 6) | BIN2BCD(t.day);
    cmosram[6] = (t.weekday << 5) | BIN2BCD(t.month);
}

/**
 * Write to the PCF8583. Handles write state-machine logic.
 *
 * @param dev  Pointer to the PCF8583 data block
 * @param data Byte being written (address or data)
 * @return I2C state-machine return code
 */
static int
pcf8583_write(void *dev, uint8_t data)
{
	PCF8583 *pcf = dev;

	if (pcf->state == PCF_STATE_ADDR) {
		dbgprintf("PCF8583 Addr 0x%02x\n", data);
		pcf->reg_address = data;
		pcf->state = PCF_STATE_DATA;
	} else if (pcf->state == PCF_STATE_DATA) {
		dbgprintf("PCF8583 Write 0x%02x to %04x\n",
		          data, pcf->reg_address);

		cmosram[pcf->reg_address] = data;
        if (pcf->reg_address >= 16) cmos_writes++;

		// RISC OS updates the checksum byte after any change, so if
		// the write is to the RISC OS checksum byte, save the data
		if (pcf->reg_address == 0x3f) {
			savecmos();
		}

		pcf->reg_address = (pcf->reg_address + 1) & 0xff;
	}
	return I2C_ACK;
}

/**
 * Read from the PCF8583. Will use address previously written to the chip.
 *
 * @param dev  Pointer to the PCF8583 data block
 * @param data Filled in with the value at previously specified address
 * @return I2C state-machine return code
 */
static int
pcf8583_read(void *dev, uint8_t *data)
{
	PCF8583 *pcf = dev;

	if (pcf->reg_address < 0x10)
		cmosgettime();

	*data = cmosram[pcf->reg_address];

	dbgprintf("PCF8583 read 0x%02x from %04x\n", *data, pcf->reg_address);
	pcf->reg_address = (pcf->reg_address + 1) & 0xff;
	return I2C_DONE;
}

/**
 * Initialise the state of the Philips PCF8583 RTC chip.
 * Called when I2C first calls the chip
 *
 * @param dev       Pointer to the PCF8583 data block
 * @param i2c_addr  UNUSED
 * @param operation UNUSED
 * @return I2C state machine return code
 */
static int
pcf8583_start(void *dev, int i2c_addr, int operation)
{
	PCF8583 *pcf = dev;

	NOT_USED(i2c_addr);
	NOT_USED(operation);

	dbgprintf("pcf8583 start\n");
	pcf->state = PCF_STATE_ADDR;

	return I2C_ACK;
}

/**
 * Finalise the state of the Philips RTC chip.
 * Called when I2C thinks there's no more to this transaction.
 *
 * @param dev Pointer to the PCF8583 data block
 */
static void
pcf8583_stop(void *dev)
{
	PCF8583 *pcf = dev;

	dbgprintf("pcf8583 stop\n");

	pcf->state = PCF_STATE_ADDR;
}

/** Function pointers for the Philips RTC interaction */
static const I2C_SlaveOps pcf8583_ops = {
	pcf8583_start,
	pcf8583_stop,
	pcf8583_write,
	pcf8583_read,
	NULL
};

static PCF8583 pcf_s;
static PCF8583 *pcf = &pcf_s; /**< Handle of a PCF8583 state machine */

static I2C_Slave pcf8583_s;
static I2C_Slave *pcf8583 = &pcf8583_s; /**< Handle of the PCF8583 chip I2C slave device */

/****************************************************************************/

/** The state of the SPD chip */
typedef struct {
	I2C_Slave i2c_slave;
	uint8_t reg_address;
} SPD;

static uint8_t spd_data[128] = {
	128, 8, 4, 12, 10, 1, 64, 0, 
	0, 1, 1, 0, 0, 8, 0, 0,
	0xf, 2, 0x7f, 0x7f, 0x7f, 0x00, 0x3f, 0x10,
	0x10, 0x10, 0x10, 1, 1, 1, 1, 0x20
};

/**
 * Write to the SPD. Handles write state-machine logic.
 *
 * @param dev  Pointer to the SPD data block
 * @param data Byte being written (address or data)
 * @return I2C state-machine return code
 */
static int
spd_write(void *dev, uint8_t data)
{
	SPD *spd = dev;

	rpclog("spd_write : data = %02X\n", data);
	
	spd->reg_address = data & 0x7f;

	return I2C_ACK;
}

/**
 * Read from the SPD. Will use address previously written to the chip.
 *
 * @param dev  Pointer to the SPD data block
 * @param data Filled in with the value at previously specified address
 * @return I2C state-machine return code
 */
static int
spd_read(void *dev, uint8_t *data)
{
	SPD *spd = dev;

	*data = spd_data[spd->reg_address];

	rpclog("SPD read 0x%02x from %04x\n", *data, spd->reg_address);
	spd->reg_address = (spd->reg_address + 1) & 0x7f;
	return I2C_DONE;
}

/**
 * Initialise the state of the SPD chip.
 * Called when I2C first calls the chip
 *
 * @param dev       Pointer to the SPD data block
 * @param i2c_addr  UNUSED
 * @param operation UNUSED
 * @return I2C state machine return code
 */
static int
spd_start(void *dev, int i2c_addr, int operation)
{
	NOT_USED(dev);
	NOT_USED(i2c_addr);
	NOT_USED(operation);

	rpclog("spd start\n");

	return I2C_ACK;
}

/**
 * Finalise the state of the SPD chip.
 * Called when I2C thinks there's no more to this transaction.
 *
 * @param dev Pointer to the SPD data block
 */
static void
spd_stop(void *dev)
{
	NOT_USED(dev);

	rpclog("spd stop\n");
}

/** Function pointers for the SPD interaction */
static const I2C_SlaveOps spd_ops = {
	spd_start,
	spd_stop,
	spd_write,
	spd_read,
	NULL
};

static SPD spd_s;
static SPD *spd = &spd_s; /**< Handle of a SPD state machine */

static I2C_Slave spd_i2c_s;
static I2C_Slave *spd_i2c = &spd_i2c_s; /**< Handle of the SPD chip I2C slave device */

/****************************************************************************/

#define I2C_SDA	1
#define I2C_SCL	2

#define I2C_STATE_IDLE		0
#define I2C_STATE_ADDR		1
#define I2C_STATE_ACK_READ	2
#define I2C_STATE_ACK_READ_ADDR	3
#define I2C_STATE_ACK_WRITE	4
#define I2C_STATE_NACK_WRITE	5
#define I2C_STATE_READ		6
#define I2C_STATE_WRITE		7
#define I2C_STATE_WAIT		8

/** The I2C state machine */
typedef struct {
	I2C_Slave *active_slave; /* Todo: multiple simultaneously active devices */
	int slave_was_accessed;
	int address;
	uint8_t inbuf;
	uint8_t outbuf;
	int bitcount;
	int state;
	int oldpinstate;
} I2C_SerDes;

static I2C_SerDes serdes_s;
static I2C_SerDes *serdes = &serdes_s; /**< Handle of the I2C state machine */

/**
 * Reset the I2C state machine
 *
 * @param serdes State machine to reset
 */
static void
reset_serdes(I2C_SerDes *serdes)
{
	memset(serdes, 0, sizeof(*serdes));
    serdes->oldpinstate = I2C_SCL | I2C_SDA;
	serdes->state = I2C_STATE_IDLE;
	serdes->bitcount = 0;
	serdes->address = 0;
	serdes->active_slave = NULL;
	serdes->inbuf = 0;
	serdes->outbuf = 0;
	serdes->slave_was_accessed = 0;
	i2cclock = 1;
	i2cdata = 1;
}

/**
 * Handle a 'write' to the I2C bus.
 * Called from IOMD.
 *
 * @param scl Bool of the state of the I2C clock pin
 * @param sda Bool of the state of the I2C data pin
 */
void
cmosi2cchange(int scl, int sda)
{
	int oldscl = serdes->oldpinstate & I2C_SCL;
	int oldsda = serdes->oldpinstate & I2C_SDA;

	dbgprintf("scl %x, sda %x, prev %02x, state %d\n",
	          scl, sda, serdes->oldpinstate, serdes->state);

	/* Detect Start/Repeated condition */
	if (scl && oldsda  && !sda) {
		reset_serdes(serdes);
		dbgprintf("Start Condition\n");
		serdes->state = I2C_STATE_ADDR;

		/* Leaving Function, store current state of i2c pins */
		serdes->oldpinstate = (scl << 1) | sda;
		return;
	}
	/* Stop condition */
	if (scl && !oldsda  && sda) {
		I2C_Slave *slave;
		slave = serdes->active_slave;
		if (slave) {
			slave->devops->stop(slave->dev);
		}
		reset_serdes(serdes);
		dbgprintf("Stop Condition\n");

		/* Leaving Function, store current state of i2c pins */
		serdes->oldpinstate = (scl << 1) | sda;
		return;
	}

	switch (serdes->state) {
	case I2C_STATE_IDLE:
		i2cclock = 1;
		i2cdata  = 1;
		break;

	case I2C_STATE_ADDR:
		if (oldscl && !scl) {
			i2cclock = 1;
			i2cdata  = 1;
		} else if (!oldscl && scl) {
			serdes->bitcount++;
			serdes->inbuf <<= 1;
			if (sda) {
				serdes->inbuf |= 1;
			}
			dbgprintf("inbuf 0x%02x\n", serdes->inbuf);
			if (serdes->bitcount == 8) {
				I2C_Slave *slave = NULL;

				serdes->address = serdes->inbuf >> 1;

				/* Detect which device is being talked to */
				if ((serdes->address == pcf8583->address) && (i2c_devices & I2C_PCF8583)) {
					slave = pcf8583;
				} else if ((serdes->address == spd_i2c->address) && (i2c_devices & I2C_SPD_DIMM0)) {
					slave = spd_i2c;
				} else {
					//fprintf(stderr, "Request for unhandled I2C device %02X\n",
					//        serdes->address);
					//rpclog("Request for unhandled I2C device %02X\n", serdes->address);
				}

				dbgprintf("I2C-Address %02x slave %p\n",
				          serdes->inbuf >> 1, slave);
				if (slave) {
					int result;

					if (serdes->inbuf & 1) {
						serdes->state = I2C_STATE_ACK_READ_ADDR;
						result = slave->devops->start(slave->dev, serdes->address, I2C_READ);
					} else {
						serdes->state = I2C_STATE_ACK_WRITE;
						result = slave->devops->start(slave->dev, serdes->address, I2C_WRITE);
					}
					if (result == I2C_ACK) {
						serdes->active_slave = slave;
						i2cclock = 1;
						i2cdata  = 1;
					} else if (result == I2C_NACK) {
						i2cclock = 1;
						i2cdata  = 1;
						serdes->state = I2C_STATE_WAIT;
					}
				} else {
					reset_serdes(serdes);
				}
			}
		}
		break;

	case I2C_STATE_ACK_READ_ADDR:
		if (oldscl && !scl) {
			i2cclock = 1;
			i2cdata  = 0;
		} else if (!oldscl && scl) {
			serdes->state = I2C_STATE_READ;
			serdes->bitcount = 8;
			serdes->slave_was_accessed = 1;
		}
		break;

	case I2C_STATE_ACK_READ:
		if (oldscl && !scl) {
			/* release the lines */
			i2cclock = 1;
			i2cdata  = 1;
		} else if (!oldscl && scl) {
			I2C_Slave *slave = serdes->active_slave;

			if (slave == NULL) {
                fatal("I2C read without active slave");
			}
			if (sda) {
				/* Last was not acknowledged, so read nothing */
				dbgprintf("Not acked\n");
				serdes->state = I2C_STATE_WAIT;
				/*
				 * -------------------------------------------
				 * forward nack to device for example
				 * to trigger a NACK interrupt
				 * -------------------------------------------
				 */
				if (slave->devops->read_ack) {
					slave->devops->read_ack(slave->dev, I2C_NACK);
				}
				if (!i2cdata) {
                    fatal("I2C unexpected driven ACK");
				}
			} else {
				/*
				 * -------------------------------------------
				 * forward ack to device because it
				 * can start preparing next data byte for read
				 * -------------------------------------------
				 */
				if (slave->devops->read_ack) {
					slave->devops->read_ack(slave->dev, I2C_ACK);
				}
				serdes->state = I2C_STATE_READ;
				serdes->bitcount = 8;
				serdes->slave_was_accessed = 1;
			}
		}
		break;

	case I2C_STATE_ACK_WRITE:
	case I2C_STATE_NACK_WRITE:
		if (oldscl && !scl) {
			i2cclock = 1;
			i2cdata  = 0;
			if (serdes->state == I2C_STATE_NACK_WRITE) {
				i2cdata = 1;
			}
			break;
		} else if (!oldscl && scl) {
			dbgprintf("goto write state addr %02x\n", serdes->address);
			serdes->state = I2C_STATE_WRITE;
			serdes->bitcount = 0;
			serdes->inbuf = 0;
			break;
		}
		break;

	case I2C_STATE_READ:
		/* We change output after falling edge of scl */
		if (oldscl && !scl) {
			I2C_Slave *slave = serdes->active_slave;
            if (!slave) fatal("I2C read without active slave");
			if (serdes->bitcount == 8) {
				(void) slave->devops->read_byte(slave->dev, &serdes->outbuf);
			}
			if (serdes->bitcount > 0) {
				serdes->bitcount--;
				i2cclock = 1; // should be delayed
				i2cdata  = 0;
				if ((serdes->outbuf & (1 << serdes->bitcount)) != 0) {
					i2cdata = 1;
				}
			} else {
                fatal("I2C read bit count invalid");
			}
		} else if (!oldscl && scl) {
			if (serdes->bitcount == 0) {
				serdes->state = I2C_STATE_ACK_READ;
			}
		}
		break;

	case I2C_STATE_WRITE:
		if (oldscl && !scl) {
			i2cclock = 1;
			i2cdata  = 1;
		} else if (!oldscl && scl) {
			serdes->bitcount++;
			serdes->inbuf <<= 1;
			if (sda) {
				serdes->inbuf |= 1;
			}
			if (serdes->bitcount == 8) {
				I2C_Slave *slave;
				int result;

				slave = serdes->active_slave;
				if (slave == NULL) {
                    fatal("I2C write without active slave");
				}
				result = slave->devops->write_byte(slave->dev, serdes->inbuf);
				serdes->slave_was_accessed = 1;
				if (result == I2C_ACK) {
					serdes->state = I2C_STATE_ACK_WRITE;
					i2cclock = 1;
				} else if (result == I2C_NACK) {
					serdes->state = I2C_STATE_NACK_WRITE;
					i2cclock = 1;
				} else {
                    fatal("I2C device response invalid");
				}
			}
		}
		break;

	case I2C_STATE_WAIT:
		break;

	default:
        fatal("I2C state invalid");
	}

	/* Leaving Function, store current state of I2C pins */
	serdes->oldpinstate = (scl << 1) | sda;
}

/**
 * Reset the I2C emulation and attached Philips PCF8583 RTC
 *
 * @param chosen_i2c_devices Bitfield of devices to attach to I2C bus
 */
void
reseti2c(uint32_t chosen_i2c_devices)
{
	i2cclock = 1;
	i2cdata = 1;
	i2c_devices = chosen_i2c_devices;
    memset(pcf, 0, sizeof(*pcf));
    memset(pcf8583, 0, sizeof(*pcf8583));
    memset(spd, 0, sizeof(*spd));
    memset(spd_i2c, 0, sizeof(*spd_i2c));

	/* Prepare the Philips Real Time Clock chip slave device */
	pcf8583->devops = &pcf8583_ops;
	pcf8583->address = 0x50;
	pcf8583->dev = pcf;

	/* Prepare the SPD slave device */
	spd_i2c->devops = &spd_ops;
	spd_i2c->address = 0x54;
	spd_i2c->dev = spd;
	spd->reg_address = 0;

	/* Initialise the I2C state machine */
	reset_serdes(serdes);
}
