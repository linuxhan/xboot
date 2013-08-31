/*
 * drivers/bus/i2c/i2c-algo-bit.c
 *
 * Copyright(c) 2007-2013 jianjun jiang <jerryjianjun@gmail.com>
 * official site: http://xboot.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <xboot.h>
#include <bus/i2c-algo-bit.h>

static inline void sdalo(struct i2c_algo_bit_data_t * adapter)
{
	adapter->setsda(adapter, 0);
	udelay((adapter->udelay + 1) / 2);
}

static inline void sdahi(struct i2c_algo_bit_data_t * adapter)
{
	adapter->setsda(adapter, 1);
	udelay((adapter->udelay + 1) / 2);
}

static inline void scllo(struct i2c_algo_bit_data_t * adapter)
{
	adapter->setscl(adapter, 0);
	udelay(adapter->udelay / 2);
}

/*
 * Raise scl line, and do checking for delays. This is necessary for slower
 * devices.
 */
static int sclhi(struct i2c_algo_bit_data_t * adapter)
{
	unsigned long start;

	adapter->setscl(adapter->data, 1);

	/*
	 * Not all adapters have scl sense line...
	 */
	if(!adapter->getscl)
	{
		udelay(adapter->udelay);
		return 0;
	}

	start = jiffies;
	while(!adapter->getscl(adapter->data))
	{
		/*
		 * This hw knows how to read the clock line, so we wait
		 * until it actually gets high.  This is safer as some
		 * chips may hold it low ("clock stretching") while they
		 * are processing data internally.
		 */
		if(time_after(jiffies, start + adapter->timeout))
		{
			/*
			 * Test one last time, as we may have been preempted
			 * between last check and timeout test.
			 */
			if(adapter->getscl(adapter->data))
				break;
			return -1;
		}
	}

	udelay(adapter->udelay);
	return 0;
}

static void i2c_start(struct i2c_algo_bit_data_t * adapter)
{
	/*
	 * assert: scl, sda are high
	 */
	adapter->setsda(adapter->data, 0);
	udelay(adapter->udelay);
	scllo(adapter);
}

static void i2c_repstart(struct i2c_algo_bit_data_t * adapter)
{
	/*
	 * assert: scl is low
	 */
	sdahi(adapter);
	sclhi(adapter);
	adapter->setsda(adapter->data, 0);
	udelay(adapter->udelay);
	scllo(adapter);
}

static void i2c_stop(struct i2c_algo_bit_data_t * adapter)
{
	/*
	 * assert: scl is low
	 */
	sdalo(adapter);
	sclhi(adapter);
	adapter->setsda(adapter->data, 1);
	udelay(adapter->udelay);
}

/*
 * send a byte without start cond., look for arbitration,
 * check ackn. from slave
 *
 * returns:
 * 1 if the device acknowledged
 * 0 if the device did not ack
 * -1 if an error occurred (while raising the scl line)
 */
static int i2c_outb(struct i2c_t * i2c, unsigned char c)
{
	int i;
	int sb;
	int ack;
	struct i2c_algo_bit_data_t * adapter = i2c->priv;

	/*
	 * assert: scl is low
	 */
	for(i = 7; i >= 0; i--)
	{
		sb = (c >> i) & 1;
		adapter->setsda(adapter->data, sb);
		udelay((adapter->udelay + 1) / 2);

		/* timed out */
		if(sclhi(adapter) < 0)
			return -1;

		scllo(adapter);
	}

	sdahi(adapter);

	/* timeout */
	if(sclhi(adapter) < 0)
		return -1;

	/*
	 * read ack: SDA should be pulled down by slave, or it may
	 * NAK (usually to report problems with the data we wrote).
	 */
	/* ack: sda is pulled low -> success */
	ack = !adapter->getsda(adapter->data);
	scllo(adapter);

	/*
	 * assert: scl is low (sda undef)
	 */
	return ack;
}

static int i2c_inb(struct i2c_t * i2c)
{
	/*
	 * read byte via i2c port, without start/stop sequence
	 * acknowledge is sent in i2c_read.
	 */
	int i;
	unsigned char indata = 0;
	struct i2c_algo_bit_data_t * adapter = i2c->priv;

	/*
	 * assert: scl is low
	 */
	sdahi(adapter);

	for(i = 0; i < 8; i++)
	{
		/* timeout */
		if(sclhi(adapter) < 0)
			return -1;

		indata *= 2;
		if(adapter->getsda(adapter->data))
			indata |= 0x01;
		adapter->setscl(adapter->data, 0);
		udelay(i == 7 ? adapter->udelay / 2 : adapter->udelay);
	}

	/*
	 * assert: scl is low
	 */
	return indata;
}

/*
 * try_address tries to contact a chip for a number of
 * times before it gives up.
 * return values:
 * 1 chip answered
 * 0 chip did not answer
 * -x transmission error
 */
static int try_address(struct i2c_t * i2c, unsigned char addr, int retries)
{
	struct i2c_algo_bit_data_t * adapter = i2c->priv;
	int i, ret = 0;

	for (i = 0; i <= retries; i++)
	{
		ret = i2c_outb(i2c, addr);
		if (ret == 1 || i == retries)
			break;
		i2c_stop(adapter);
		udelay(adapter->udelay);
		i2c_start(adapter);
	}

	return ret;
}

static int sendbytes(struct i2c_t * i2c, struct i2c_msg_t * msg)
{
	const unsigned char * temp = msg->buf;
	int count = msg->len;
	unsigned short nak_ok = msg->flags & I2C_M_IGNORE_NAK;
	int retval;
	int wrcount = 0;

	while(count > 0)
	{
		retval = i2c_outb(i2c, *temp);

		/*
		 * OK/ACK; or ignored NAK
		 */
		if((retval > 0) || (nak_ok && (retval == 0)))
		{
			count--;
			temp++;
			wrcount++;

		/*
		 * A slave NAKing the master means the slave didn't like
		 * something about the data it saw.  For example, maybe
		 * the SMBus PEC was wrong.
		 */
		}
		else if (retval == 0)
		{
			return -1;
		}
		else
		{
			return retval;
		}
	}

	return wrcount;
}

static int acknak(struct i2c_t * i2c, int is_ack)
{
	struct i2c_algo_bit_data_t * adapter = i2c->priv;

	/*
	 * assert: sda is high
	 */
	if(is_ack)
		adapter->setsda(adapter->data, 0);
	udelay((adapter->udelay + 1) / 2);

	/* timeout */
	if(sclhi(adapter) < 0)
		return -1;

	scllo(adapter);
	return 0;
}

static int readbytes(struct i2c_t * i2c, struct i2c_msg_t * msg)
{
	int inval;
	int rdcount = 0;
	unsigned char *temp = msg->buf;
	int count = msg->len;
	const unsigned flags = msg->flags;

	while(count > 0)
	{
		inval = i2c_inb(i2c);
		if(inval >= 0)
		{
			*temp = inval;
			rdcount++;
		}
		else
		{
			/* read timed out */
			break;
		}

		temp++;
		count--;

		/*
		 * Some SMBus transactions require that we receive the
		 * transaction length as the first read byte.
		 */
		if(rdcount == 1 && (flags & I2C_M_RECV_LEN))
		{
			if (inval <= 0 || inval > 32)
			{
				if(!(flags & I2C_M_NO_RD_ACK))
					acknak(i2c, 0);
				return -1;
			}

			/*
			 * The original count value accounts for the extra
			 * bytes, that is, either 1 for a regular transaction,
			 * or 2 for a PEC transaction.
			 */
			count += inval;
			msg->len += inval;
		}

		if(!(flags & I2C_M_NO_RD_ACK))
		{
			inval = acknak(i2c, count);
			if(inval < 0)
				return inval;
		}
	}

	return rdcount;
}

/*
 * doAddress initiates the transfer by generating the start condition (in
 * try_address) and transmits the address in the necessary format to handle
 * reads, writes as well as 10bit-addresses.
 * returns:
 *  0 everything went okay, the chip ack'ed, or IGNORE_NAK flag was set
 * -x an error occurred (like: -ENXIO if the device did not answer, or
 *	-ETIMEDOUT, for example if the lines are stuck...)
 */
static int bit_doAddress(struct i2c_t * i2c, struct i2c_msg_t * msg)
{
	unsigned short flags = msg->flags;
	unsigned short nak_ok = msg->flags & I2C_M_IGNORE_NAK;
	struct i2c_algo_bit_data_t * adapter = i2c->priv;
	unsigned char addr;
	int ret, retries;

//TODO FIXME by jjj
	//retries = nak_ok ? 0 : i2c->retries;
	retries = nak_ok ? 0 : 3;

	if (flags & I2C_M_TEN)
	{
		/*
		 * a ten bit address
		 */
		addr = 0xf0 | ((msg->addr >> 7) & 0x06);

		/*
		 * try extended address code...
		 */
		ret = try_address(i2c, addr, retries);
		if((ret != 1) && !nak_ok)
		{
			return -1;
		}

		/*
		 * the remaining 8 bit address
		 */
		ret = i2c_outb(i2c, msg->addr & 0xff);
		if((ret != 1) && !nak_ok)
		{
			/* the chip did not ack / xmission error occurred */
			return -1;
		}

		if(flags & I2C_M_RD)
		{
			i2c_repstart(adapter);
			/*
			 * okay, now switch into reading mode
			 */
			addr |= 0x01;
			ret = try_address(i2c, addr, retries);
			if((ret != 1) && !nak_ok)
			{
				return -1;
			}
		}
	}
	else
	{
		/*
		 * normal 7bit address
		 */
		addr = msg->addr << 1;
		if(flags & I2C_M_RD)
			addr |= 1;
		if(flags & I2C_M_REV_DIR_ADDR)
			addr ^= 1;
		ret = try_address(i2c, addr, retries);
		if((ret != 1) && !nak_ok)
			return -1;
	}

	return 0;
}

static int bit_xfer(struct i2c_t * i2c, struct i2c_msg_t * msgs, int num)
{
	struct i2c_msg_t * pmsg;
	struct i2c_algo_bit_data_t * adapter = i2c->priv;
	int i, ret;
	unsigned short nak_ok;

	i2c_start(adapter);
	for(i = 0; i < num; i++)
	{
		pmsg = &msgs[i];
		nak_ok = pmsg->flags & I2C_M_IGNORE_NAK;
		if(!(pmsg->flags & I2C_M_NOSTART))
		{
			if(i)
			{
				i2c_repstart(adapter);
			}
			ret = bit_doAddress(i2c, pmsg);
			if((ret != 0) && !nak_ok)
			{
				goto bailout;
			}
		}
		if(pmsg->flags & I2C_M_RD)
		{
			/* read bytes into buffer*/
			ret = readbytes(i2c, pmsg);
			if(ret < pmsg->len)
			{
				if (ret >= 0)
					ret = -1;
				goto bailout;
			}
		}
		else
		{
			/* write bytes from buffer */
			ret = sendbytes(i2c, pmsg);
			if(ret < pmsg->len)
			{
				if (ret >= 0)
					ret = -1;
				goto bailout;
			}
		}
	}
	ret = i;

bailout:
	i2c_stop(adapter);

	return ret;
}