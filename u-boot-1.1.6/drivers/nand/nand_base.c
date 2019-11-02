/*
 *  drivers/mtd/nand.c
 *
 *  Overview:
 *   This is the generic MTD driver for NAND flash devices. It should be
 *   capable of working with almost all NAND chips currently available.
 *   Basic support for AG-AND chips is provided.
 *
 *	Additional technical information is available on
 *	http://www.linux-mtd.infradead.org/tech/nand.html
 *
 *  Copyright (C) 2000 Steven J. Hill (sjhill@realitydiluted.com)
 * 		  2002 Thomas Gleixner (tglx@linutronix.de)
 *
 *  02-08-2004  tglx: support for strange chips, which cannot auto increment
 *		pages on read / read_oob
 *
 *  03-17-2004  tglx: Check ready before auto increment check. Simon Bayes
 *		pointed this out, as he marked an auto increment capable chip
 *		as NOAUTOINCR in the board driver.
 *		Make reads over block boundaries work too
 *
 *  04-14-2004	tglx: first working version for 2k page size chips
 *
 *  05-19-2004  tglx: Basic support for Renesas AG-AND chips
 *
 *  09-24-2004  tglx: add support for hardware controllers (e.g. ECC) shared
 *		among multiple independend devices. Suggestions and initial patch
 *		from Ben Dooks <ben-mtd@fluff.org>
 *
 * Credits:
 *	David Woodhouse for adding multichip support
 *
 *	Aleph One Ltd. and Toby Churchill Ltd. for supporting the
 *	rework for 2K page size chips
 *
 * TODO:
 *	Enable cached programming for 2k page size chips
 *	Check, if mtd->ecctype should be set to MTD_ECC_HW
 *	if we have HW ecc support.
 *	The AG-AND chips have nice features for speed improvement,
 *	which are not supported yet. Read / program 4 pages in one go.
 *
 * $Id: nand_base.c,v 1.126 2004/12/13 11:22:25 lavinen Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

/* XXX U-BOOT XXX */
#if 0
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/compatmac.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <asm/io.h>

#ifdef CONFIG_MTD_PARTITIONS
#include <linux/mtd/partitions.h>
#endif

#endif

#include <common.h>

#if (CONFIG_COMMANDS & CFG_CMD_NAND) && !defined(CFG_NAND_LEGACY)

#include <malloc.h>
#include <watchdog.h>
#include <linux/mtd/compat.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>

#include <asm/io.h>
#include <asm/errno.h>

#ifdef CONFIG_JFFS2_NAND
#include <jffs2/jffs2.h>
#endif

#if defined(CONFIG_S3C2400)
#include <s3c2400.h>
#elif defined(CONFIG_S3C2410)
#include <s3c2410.h>
#elif defined(CONFIG_S3C2440)
#include <s3c2440.h>
#endif

#ifdef CONFIG_SURPORT_WINCE
#include "../../wince/loader.h"
#endif

/* Define default oob placement schemes for large and small page devices */
static struct nand_oobinfo nand_oob_8 = {
	.useecc = MTD_NANDECC_AUTOPLACE,
	.eccbytes = 3,
	.eccpos = {0, 1, 2},
	.oobfree = { {3, 2}, {6, 2} }
};

static struct nand_oobinfo nand_oob_16 = {
	.useecc = MTD_NANDECC_AUTOPLACE,
	.eccbytes = 6,
	.eccpos = {0, 1, 2, 3, 6, 7},
	.oobfree = { {8, 8} }
};

static struct nand_oobinfo nand_oob_64 = {
	.useecc = MTD_NANDECC_AUTOPLACE,
#ifdef CONFIG_TQ2440_NAND_HWECC
	.eccbytes = 16,
	.eccpos = {
		40, 41, 42, 43, 44, 45, 46, 47,
		48, 49, 50, 51, 52, 53, 54, 55},
#else
	.eccbytes = 24,
	.eccpos = {
		40, 41, 42, 43, 44, 45, 46, 47,
		48, 49, 50, 51, 52, 53, 54, 55,
		56, 57, 58, 59, 60, 61, 62, 63},
#endif /* CONFIG_TQ2440_NAND_HWECC */
	.oobfree = { {2, 38} }
};

/* This is used for padding purposes in nand_write_oob */
static u_char ffchars[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

/*
 * NAND low-level MTD interface functions
 */
static void nand_write_buf(struct mtd_info *mtd, const u_char *buf, int len);
static void nand_read_buf(struct mtd_info *mtd, u_char *buf, int len);
static int nand_verify_buf(struct mtd_info *mtd, const u_char *buf, int len);

static int nand_read (struct mtd_info *mtd, loff_t from, size_t len, size_t * retlen, u_char * buf);
static int nand_read_ecc (struct mtd_info *mtd, loff_t from, size_t len,
			  size_t * retlen, u_char * buf, u_char * eccbuf, struct nand_oobinfo *oobsel);
static int nand_read_oob (struct mtd_info *mtd, loff_t from, size_t len, size_t * retlen, u_char * buf);
static int nand_write (struct mtd_info *mtd, loff_t to, size_t len, size_t * retlen, const u_char * buf);
static int nand_write_ecc (struct mtd_info *mtd, loff_t to, size_t len,
			   size_t * retlen, const u_char * buf, u_char * eccbuf, struct nand_oobinfo *oobsel);
static int nand_write_oob (struct mtd_info *mtd, loff_t to, size_t len, size_t * retlen, const u_char *buf);
/* XXX U-BOOT XXX */
#if 0
static int nand_writev (struct mtd_info *mtd, const struct kvec *vecs,
			unsigned long count, loff_t to, size_t * retlen);
static int nand_writev_ecc (struct mtd_info *mtd, const struct kvec *vecs,
			unsigned long count, loff_t to, size_t * retlen, u_char *eccbuf, struct nand_oobinfo *oobsel);
#endif
static int nand_erase (struct mtd_info *mtd, struct erase_info *instr);
static void nand_sync (struct mtd_info *mtd);

/* Some internal functions */
static int nand_write_page (struct mtd_info *mtd, struct nand_chip *chip, int page, u_char *oob_buf,
		struct nand_oobinfo *oobsel, int mode);
#ifdef CONFIG_MTD_NAND_VERIFY_WRITE
static int nand_verify_pages (struct mtd_info *mtd, struct nand_chip *chip, int page, int numpages,
	u_char *oob_buf, struct nand_oobinfo *oobsel, int chipnr, int oobmode);
#else
#define nand_verify_pages(...) (0)
#endif

static void nand_get_device (struct nand_chip *chip, struct mtd_info *mtd, int new_state);

/**
 * nand_release_device - [GENERIC] release chip
 * @mtd:	MTD device structure
 *
 * Deselect, release chip lock and wake up anyone waiting on the device
 */
/* XXX U-BOOT XXX */
#if 0
static void nand_release_device (struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;

	/* De-select the NAND device */
	chip->select_chip(mtd, -1);
	/* Do we have a hardware controller ? */
	if (chip->controller) {
		spin_lock(&chip->controller->lock);
		chip->controller->active = NULL;
		spin_unlock(&chip->controller->lock);
	}
	/* Release the chip */
	spin_lock (&chip->chip_lock);
	chip->state = FL_READY;
	wake_up (&chip->wq);
	spin_unlock (&chip->chip_lock);
}
#else
static void nand_release_device (struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;
	chip->select_chip(mtd, -1);	/* De-select the NAND device */
}
#endif

/**
 * nand_read_byte - [DEFAULT] read one byte from the chip
 * @mtd:	MTD device structure
 *
 * Default read function for 8bit buswith
 */
static u_char nand_read_byte(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;
	return readb(chip->IO_ADDR_R);
}

/**
 * nand_write_byte - [DEFAULT] write one byte to the chip
 * @mtd:	MTD device structure
 * @byte:	pointer to data byte to write
 *
 * Default write function for 8it buswith
 */
static void nand_write_byte(struct mtd_info *mtd, u_char byte)
{
	struct nand_chip *chip = mtd->priv;
	writeb(byte, chip->IO_ADDR_W);
}

/**
 * nand_read_byte16 - [DEFAULT] read one byte endianess aware from the chip
 * @mtd:	MTD device structure
 *
 * Default read function for 16bit buswith with
 * endianess conversion
 */
static u_char nand_read_byte16(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;
	return (u_char) cpu_to_le16(readw(chip->IO_ADDR_R));
}

/**
 * nand_write_byte16 - [DEFAULT] write one byte endianess aware to the chip
 * @mtd:	MTD device structure
 * @byte:	pointer to data byte to write
 *
 * Default write function for 16bit buswith with
 * endianess conversion
 */
static void nand_write_byte16(struct mtd_info *mtd, u_char byte)
{
	struct nand_chip *chip = mtd->priv;
	writew(le16_to_cpu((u16) byte), chip->IO_ADDR_W);
}

/**
 * nand_read_word - [DEFAULT] read one word from the chip
 * @mtd:	MTD device structure
 *
 * Default read function for 16bit buswith without
 * endianess conversion
 */
static u16 nand_read_word(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;
	return readw(chip->IO_ADDR_R);
}

/**
 * nand_write_word - [DEFAULT] write one word to the chip
 * @mtd:	MTD device structure
 * @word:	data word to write
 *
 * Default write function for 16bit buswith without
 * endianess conversion
 */
static void nand_write_word(struct mtd_info *mtd, u16 word)
{
	struct nand_chip *chip = mtd->priv;
	writew(word, chip->IO_ADDR_W);
}

/**
 * nand_select_chip - [DEFAULT] control CE line
 * @mtd:	MTD device structure
 * @chip:	chipnumber to select, -1 for deselect
 *
 * Default select function for 1 chip devices.
 */
static void nand_select_chip(struct mtd_info *mtd, int chipnr)
{
	struct nand_chip *chip = mtd->priv;
	switch(chipnr) {
	case -1:
		chip->hwcontrol(mtd, NAND_CTL_CLRNCE);
		break;
	case 0:
		chip->hwcontrol(mtd, NAND_CTL_SETNCE);
		break;

	default:
		BUG();
	}
}

/**
 * nand_write_buf - [DEFAULT] write buffer to chip
 * @mtd:	MTD device structure
 * @buf:	data buffer
 * @len:	number of bytes to write
 *
 * Default write function for 8bit buswith
 */
static void nand_write_buf(struct mtd_info *mtd, const u_char *buf, int len)
{
	int i;
	struct nand_chip *chip = mtd->priv;

	for (i=0; i<len; i++)
		writeb(buf[i], chip->IO_ADDR_W);
}

/**
 * nand_read_buf - [DEFAULT] read chip data into buffer
 * @mtd:	MTD device structure
 * @buf:	buffer to store date
 * @len:	number of bytes to read
 *
 * Default read function for 8bit buswith
 */
static void nand_read_buf(struct mtd_info *mtd, u_char *buf, int len)
{
	int i;
	struct nand_chip *chip = mtd->priv;

	for (i=0; i<len; i++)
		buf[i] = readb(chip->IO_ADDR_R);
}

/**
 * nand_verify_buf - [DEFAULT] Verify chip data against buffer
 * @mtd:	MTD device structure
 * @buf:	buffer containing the data to compare
 * @len:	number of bytes to compare
 *
 * Default verify function for 8bit buswith
 */
static int nand_verify_buf(struct mtd_info *mtd, const u_char *buf, int len)
{
	int i;
	struct nand_chip *chip = mtd->priv;

	for (i=0; i<len; i++)
		if (buf[i] != readb(chip->IO_ADDR_R))
			return -EFAULT;

	return 0;
}

/**
 * nand_write_buf16 - [DEFAULT] write buffer to chip
 * @mtd:	MTD device structure
 * @buf:	data buffer
 * @len:	number of bytes to write
 *
 * Default write function for 16bit buswith
 */
static void nand_write_buf16(struct mtd_info *mtd, const u_char *buf, int len)
{
	int i;
	struct nand_chip *chip = mtd->priv;
	u16 *p = (u16 *) buf;
	len >>= 1;

	for (i=0; i<len; i++)
		writew(p[i], chip->IO_ADDR_W);

}

/**
 * nand_read_buf16 - [DEFAULT] read chip data into buffer
 * @mtd:	MTD device structure
 * @buf:	buffer to store date
 * @len:	number of bytes to read
 *
 * Default read function for 16bit buswith
 */
static void nand_read_buf16(struct mtd_info *mtd, u_char *buf, int len)
{
	int i;
	struct nand_chip *chip = mtd->priv;
	u16 *p = (u16 *) buf;
	len >>= 1;

	for (i=0; i<len; i++)
		p[i] = readw(chip->IO_ADDR_R);
}

/**
 * nand_verify_buf16 - [DEFAULT] Verify chip data against buffer
 * @mtd:	MTD device structure
 * @buf:	buffer containing the data to compare
 * @len:	number of bytes to compare
 *
 * Default verify function for 16bit buswith
 */
static int nand_verify_buf16(struct mtd_info *mtd, const u_char *buf, int len)
{
	int i;
	struct nand_chip *chip = mtd->priv;
	u16 *p = (u16 *) buf;
	len >>= 1;

	for (i=0; i<len; i++)
		if (p[i] != readw(chip->IO_ADDR_R))
			return -EFAULT;

	return 0;
}

/**
 * nand_block_bad - [DEFAULT] Read bad block marker from the chip
 * @mtd:	MTD device structure
 * @ofs:	offset from device start
 * @getchip:	0, if the chip is already selected
 *
 * Check, if the block is bad.
 */
static int nand_block_bad(struct mtd_info *mtd, loff_t ofs, int getchip)
{
	int page, chipnr, res = 0;
	struct nand_chip *chip = mtd->priv;
	u16 bad;

	page = (int)(ofs >> chip->page_shift);
	chipnr = (int)(ofs >> chip->chip_shift);

	if (getchip) {

		/* Grab the lock and see if the device is available */
		nand_get_device (chip, mtd, FL_READING);

		/* Select the NAND device */
		chip->select_chip(mtd, chipnr);
	} 

	if (chip->options & NAND_BUSWIDTH_16) {
		chip->cmdfunc (mtd, NAND_CMD_READOOB, chip->badblockpos & 0xFE, page & chip->pagemask);
		bad = cpu_to_le16(chip->read_word(mtd));
		if (chip->badblockpos & 0x1)
			bad >>= 1;
		if ((bad & 0xFF) != 0xff)
			res = 1;
	} else {
#ifdef CONFIG_SURPORT_WINCE
#if 0
        char oob_buf[16];
        SectorInfo *psi = (SectorInfo *)oob_buf;
        int i;
		chip->cmdfunc (mtd, NAND_CMD_READOOB, 0, page & chip->pagemask);
        chip->read_buf(mtd, oob_buf, 16);

        if (psi->bOEMReserved == (OEM_BLOCK_RESERVED | OEM_BLOCK_READONLY) &&
            psi->bBadBlock == BADBLOCKMARK)
            res = 0;
        else if (psi->bBadBlock != 0xff)
    		res = 1;
#endif
        //printf("bad? ofs = 0x%x, RESERVED_BOOT_BLOCKS = 0x%x,  mtd->erasesize = 0x%x, mul = 0x%x\n", ofs, RESERVED_BOOT_BLOCKS, mtd->erasesize, RESERVED_BOOT_BLOCKS*mtd->erasesize);
        if (ofs < RESERVED_BOOT_BLOCKS * mtd->erasesize)
            res = 0;
        else
#endif        
        {
    		chip->cmdfunc (mtd, NAND_CMD_READOOB, chip->badblockpos, page & chip->pagemask);
    		if (chip->read_byte(mtd) != 0xff)
    			res = 1;
        }

        // Apply delay or wait for ready/busy pin
        // add by www.embedsky.net, if not, the erase will be failed
		if (!chip->dev_ready)
			udelay (chip->chip_delay);
		else
			while (!chip->dev_ready(mtd));

	}

	if (getchip) {
		/* Deselect and wake up anyone waiting on the device */
		nand_release_device(mtd);
	}

	return res;
}

/**
 * nand_default_block_markbad - [DEFAULT] mark a block bad
 * @mtd:	MTD device structure
 * @ofs:	offset from device start
 *
 * This is the default implementation, which can be overridden by
 * a hardware specific driver.
*/
static int nand_default_block_markbad(struct mtd_info *mtd, loff_t ofs)
{
	struct nand_chip *chip = mtd->priv;
	u_char buf[2] = {0, 0};
	size_t	retlen;
	int block;

	/* Get block number */
	block = ((int) ofs) >> chip->bbt_erase_shift;
	chip->bbt[block >> 2] |= 0x01 << ((block & 0x03) << 1);

	/* Do we have a flash based bad block table ? */
	if (chip->options & NAND_USE_FLASH_BBT)
		return nand_update_bbt (mtd, ofs);

	/* We write two bytes, so we dont have to mess with 16 bit access */
	ofs += mtd->oobsize + (chip->badblockpos & ~0x01);
	return nand_write_oob (mtd, ofs , 2, &retlen, buf);
}

/**
 * nand_check_wp - [GENERIC] check if the chip is write protected
 * @mtd:	MTD device structure
 * Check, if the device is write protected
 *
 * The function expects, that the device is already selected
 */
static int nand_check_wp (struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;
	/* Check the WP bit */
	chip->cmdfunc (mtd, NAND_CMD_STATUS, -1, -1);
	return (chip->read_byte(mtd) & 0x80) ? 0 : 1;
}

/**
 * nand_block_checkbad - [GENERIC] Check if a block is marked bad
 * @mtd:	MTD device structure
 * @ofs:	offset from device start
 * @getchip:	0, if the chip is already selected
 * @allowbbt:	1, if its allowed to access the bbt area
 *
 * Check, if the block is bad. Either by reading the bad block table or
 * calling of the scan function.
 */
static int nand_block_checkbad (struct mtd_info *mtd, loff_t ofs, int getchip, int allowbbt)
{
	struct nand_chip *chip = mtd->priv;

//	if (!chip->bbt)
		return chip->block_bad(mtd, ofs, getchip);

	/* Return info from the table */
//	return nand_isbad_bbt (mtd, ofs, allowbbt);
}

/**
 * nand_command - [DEFAULT] Send command to NAND device
 * @mtd:	MTD device structure
 * @command:	the command to be sent
 * @column:	the column address for this command, -1 if none
 * @page_addr:	the page address for this command, -1 if none
 *
 * Send command to NAND device. This function is used for small page
 * devices (256/512 Bytes per page)
 */
static void nand_command (struct mtd_info *mtd, unsigned command, int column, int page_addr)
{
	register struct nand_chip *chip = mtd->priv;

	/* Begin command latch cycle */
	chip->hwcontrol(mtd, NAND_CTL_SETCLE);
	/*
	 * Write out the command to the device.
	 */
	if (command == NAND_CMD_SEQIN) {
		int readcmd;

		if (column >= mtd->oobblock) {
			/* OOB area */
			column -= mtd->oobblock;
			readcmd = NAND_CMD_READOOB;
		} else if (column < 256) {
			/* First 256 bytes --> READ0 */
			readcmd = NAND_CMD_READ0;
		} else {
			column -= 256;
			readcmd = NAND_CMD_READ1;
		}
		chip->write_byte(mtd, readcmd);
	}
	chip->write_byte(mtd, command);

	/* Set ALE and clear CLE to start address cycle */
	chip->hwcontrol(mtd, NAND_CTL_CLRCLE);

	if (column != -1 || page_addr != -1) {
		chip->hwcontrol(mtd, NAND_CTL_SETALE);

		/* Serially input address */
		if (column != -1) {
			/* Adjust columns for 16 bit buswidth */
			if (chip->options & NAND_BUSWIDTH_16)
				column >>= 1;
			chip->write_byte(mtd, column);
//			udelay(15);		//HJ
		}
		if (page_addr != -1) {
			chip->write_byte(mtd, (unsigned char) (page_addr & 0xff));
//			udelay(15);		//HJ
			chip->write_byte(mtd, (unsigned char) ((page_addr >> 8) & 0xff));
//			udelay(15);		//HJ
			/* One more address cycle for devices > 32MiB */
			if (chip->chipsize > (32 << 20))
				chip->write_byte(mtd, (unsigned char) ((page_addr >> 16) & 0x0f));
//   			    udelay(15);		//HJ
		}
		/* Latch in address */
		chip->hwcontrol(mtd, NAND_CTL_CLRALE);
	}

	/*
	 * program and erase have their own busy handlers
	 * status and sequential in needs no delay
	*/
	switch (command) {

	case NAND_CMD_PAGEPROG:
	case NAND_CMD_ERASE1:
	case NAND_CMD_ERASE2:
	case NAND_CMD_SEQIN:
	case NAND_CMD_STATUS:
		return;

	case NAND_CMD_RESET:
		if (chip->dev_ready)
			break;
		udelay(chip->chip_delay);
		chip->hwcontrol(mtd, NAND_CTL_SETCLE);
		chip->write_byte(mtd, NAND_CMD_STATUS);
		chip->hwcontrol(mtd, NAND_CTL_CLRCLE);
		while ( !(chip->read_byte(mtd) & 0x40));
		return;

	/* This applies to read commands */
	default:
		/*
		 * If we don't have access to the busy pin, we apply the given
		 * command delay
		*/
		if (!chip->dev_ready) {
			udelay (chip->chip_delay);
			return;
		}
	}

	/* Apply this short delay always to ensure that we do wait tWB in
	 * any case on any machine. */
	ndelay (100);
	/* wait until command is processed */
	while (!chip->dev_ready(mtd));
}

/**
 * nand_command_lp - [DEFAULT] Send command to NAND large page device
 * @mtd:	MTD device structure
 * @command:	the command to be sent
 * @column:	the column address for this command, -1 if none
 * @page_addr:	the page address for this command, -1 if none
 *
 * Send command to NAND device. This is the version for the new large page devices
 * We dont have the seperate regions as we have in the small page devices.
 * We must emulate NAND_CMD_READOOB to keep the code compatible.
 *
 */
static void nand_command_lp (struct mtd_info *mtd, unsigned command, int column, int page_addr)
{
	register struct nand_chip *chip = mtd->priv;

	/* Emulate NAND_CMD_READOOB */
	if (command == NAND_CMD_READOOB) {
		column += mtd->oobblock;
		command = NAND_CMD_READ0;
	}


	/* Begin command latch cycle */
	chip->hwcontrol(mtd, NAND_CTL_SETCLE);
	/* Write out the command to the device. */
	chip->write_byte(mtd, command);
	/* End command latch cycle */
	chip->hwcontrol(mtd, NAND_CTL_CLRCLE);

	if (column != -1 || page_addr != -1) {
		chip->hwcontrol(mtd, NAND_CTL_SETALE);

		/* Serially input address */
		if (column != -1) {
#if 0
			/* Adjust columns for 16 bit buswidth */
			if (chip->options & NAND_BUSWIDTH_16)
				column >>= 1;
#endif
			chip->write_byte(mtd, column & 0xff);
			chip->write_byte(mtd, column >> 8);
		}
		if (page_addr != -1) {
			chip->write_byte(mtd, (unsigned char) (page_addr & 0xff));
			chip->write_byte(mtd, (unsigned char) ((page_addr >> 8) & 0xff));
			/* One more address cycle for devices > 128MiB */
			if (chip->chipsize > (128 << 20))
				chip->write_byte(mtd, (unsigned char) ((page_addr >> 16) & 0xff));
		}
		/* Latch in address */
		chip->hwcontrol(mtd, NAND_CTL_CLRALE);
	}

	/*
	 * program and erase have their own busy handlers
	 * status and sequential in needs no delay
	*/
	switch (command) {

	case NAND_CMD_CACHEDPROG:
	case NAND_CMD_PAGEPROG:
	case NAND_CMD_ERASE1:
	case NAND_CMD_ERASE2:
	case NAND_CMD_SEQIN:
	case NAND_CMD_STATUS:
		return;


	case NAND_CMD_RESET:
		if (chip->dev_ready)
			break;
		udelay(chip->chip_delay);
		chip->hwcontrol(mtd, NAND_CTL_SETCLE);
		chip->write_byte(mtd, NAND_CMD_STATUS);
		chip->hwcontrol(mtd, NAND_CTL_CLRCLE);
		while ( !(chip->read_byte(mtd) & 0x40));
		return;

	case NAND_CMD_READ0:
		/* Begin command latch cycle */
		chip->hwcontrol(mtd, NAND_CTL_SETCLE);
		/* Write out the start read command */
		chip->write_byte(mtd, NAND_CMD_READSTART);
		/* End command latch cycle */
		chip->hwcontrol(mtd, NAND_CTL_CLRCLE);
		/* Fall through into ready check */

	/* This applies to read commands */
	default:
		/*
		 * If we don't have access to the busy pin, we apply the given
		 * command delay
		*/
		if (!chip->dev_ready) {
			udelay (chip->chip_delay);
			return;
		}
	}

	/* Apply this short delay always to ensure that we do wait tWB in
	 * any case on any machine. */
	ndelay (100);
	/* wait until command is processed */
	while (!chip->dev_ready(mtd));
}

/**
 * nand_get_device - [GENERIC] Get chip for selected access
 * @chip:	the nand chip descriptor
 * @mtd:	MTD device structure
 * @new_state:	the state which is requested
 *
 * Get the device and lock it for exclusive access
 */
/* XXX U-BOOT XXX */
#if 0
static void nand_get_device (struct nand_chip *chip, struct mtd_info *mtd, int new_state)
{
	struct nand_chip *active = chip;

	DECLARE_WAITQUEUE (wait, current);

	/*
	 * Grab the lock and see if the device is available
	*/
retry:
	/* Hardware controller shared among independend devices */
	if (chip->controller) {
		spin_lock (&chip->controller->lock);
		if (chip->controller->active)
			active = chip->controller->active;
		else
			chip->controller->active = chip;
		spin_unlock (&chip->controller->lock);
	}

	if (active == chip) {
		spin_lock (&chip->chip_lock);
		if (chip->state == FL_READY) {
			chip->state = new_state;
			spin_unlock (&chip->chip_lock);
			return;
		}
	}
	set_current_state (TASK_UNINTERRUPTIBLE);
	add_wait_queue (&active->wq, &wait);
	spin_unlock (&active->chip_lock);
	schedule ();
	remove_wait_queue (&active->wq, &wait);
	goto retry;
}
#else
static void nand_get_device (struct nand_chip *chip, struct mtd_info *mtd, int new_state) {}
#endif

/**
 * nand_wait - [DEFAULT]  wait until the command is done
 * @mtd:	MTD device structure
 * @chip:	NAND chip structure
 * @state:	state to select the max. timeout value
 *
 * Wait for command done. This applies to erase and program only
 * Erase can take up to 400ms and program up to 20ms according to
 * general NAND and SmartMedia specs
 *
*/
/* XXX U-BOOT XXX */
#if 0
static int nand_wait(struct mtd_info *mtd, struct nand_chip *chip, int state)
{
	unsigned long	timeo = jiffies;
	int	status;

	if (state == FL_ERASING)
		 timeo += (HZ * 400) / 1000;
	else
		 timeo += (HZ * 20) / 1000;

	/* Apply this short delay always to ensure that we do wait tWB in
	 * any case on any machine. */
	ndelay (100);

	if ((state == FL_ERASING) && (chip->options & NAND_IS_AND))
		chip->cmdfunc (mtd, NAND_CMD_STATUS_MULTI, -1, -1);
	else
		chip->cmdfunc (mtd, NAND_CMD_STATUS, -1, -1);

	while (time_before(jiffies, timeo)) {
		/* Check, if we were interrupted */
		if (chip->state != state)
			return 0;

		if (chip->dev_ready) {
			if (chip->dev_ready(mtd))
				break;
		} else {
			if (chip->read_byte(mtd) & NAND_STATUS_READY)
				break;
		}
		yield ();
	}
	status = (int) chip->read_byte(mtd);
	return status;

	return 0;
}
#else
static int nand_wait(struct mtd_info *mtd, struct nand_chip *chip, int state)
{
	unsigned long	timeo;

	if (state == FL_ERASING)
		timeo = CFG_HZ * 400;
	else
		timeo = CFG_HZ * 20;

	if ((state == FL_ERASING) && (chip->options & NAND_IS_AND))
		chip->cmdfunc(mtd, NAND_CMD_STATUS_MULTI, -1, -1);
	else
		chip->cmdfunc(mtd, NAND_CMD_STATUS, -1, -1);

	reset_timer();

	while (1) {
		if (get_timer(0) > timeo) {
			printf("Timeout!");
			return 0;
			}

		if (chip->dev_ready) {
			if (chip->dev_ready(mtd))
				break;
		} else {
			if (chip->read_byte(mtd) & NAND_STATUS_READY)
				break;
		}
	}
#ifdef PPCHAMELON_NAND_TIMER_HACK
	reset_timer();
	while (get_timer(0) < 10);
#endif /*  PPCHAMELON_NAND_TIMER_HACK */

	return chip->read_byte(mtd);
}
#endif

/**
 * nand_write_page - [GENERIC] write one page
 * @mtd:	MTD device structure
 * @chip:	NAND chip structure
 * @page: 	startpage inside the chip, must be called with (page & chip->pagemask)
 * @oob_buf:	out of band data buffer
 * @oobsel:	out of band selecttion structre
 * @cached:	1 = enable cached programming if supported by chip
 *
 * Nand_page_program function is used for write and writev !
 * This function will always program a full page of data
 * If you call it with a non page aligned buffer, you're lost :)
 *
 * Cached programming is not supported yet.
 */
static int nand_write_page (struct mtd_info *mtd, struct nand_chip *chip, int page,
	u_char *oob_buf,  struct nand_oobinfo *oobsel, int cached)
{
	int 	i, status;
	u_char	ecc_code[32];
	int	eccmode = oobsel->useecc ? chip->eccmode : NAND_ECC_NONE;
	uint  	*oob_config = oobsel->eccpos;
	int	datidx = 0, eccidx = 0, eccsteps = chip->eccsteps;
	int	eccbytes = 0;
#ifdef CONFIG_TQ2440_NAND_HWECC
	int secc_start = 0;
#endif /* CONFIG_TQ2440_NAND_HWECC */

	/* FIXME: Enable cached programming */
	cached = 0;

	/* Send command to begin auto page programming */
	chip->cmdfunc (mtd, NAND_CMD_SEQIN, 0x00, page);

#ifdef CONFIG_TQ2440_NAND_HWECC
	eccbytes = chip->eccbytes;
	secc_start = mtd->oobsize - eccbytes;

#if 0
	if((page >= 0x80) && (page <= 0xbf))
	{
		for(i=0; i<64; i++)
			printf("0x%02x ", chip->data_poi[i]);
		printf("\n");
	}
#endif

	/* main area */
	for (i=0; eccsteps; eccsteps--, i+=eccbytes) {
		chip->enable_hwecc(mtd, NAND_ECC_WRITE);
		chip->write_buf(mtd, &chip->data_poi[datidx], chip->eccsize);
		chip->calculate_ecc(mtd, &chip->data_poi[datidx], &ecc_code[i]);
		datidx += chip->eccsize;
	}
	for (i = 0, eccidx=0; i < (chip->ecctotal); i++, eccidx++)
	{
		oob_buf[oob_config[eccidx]] = ecc_code[i];
	}

	/* spare area */
#if 0
	chip->enable_hwecc(mtd, NAND_ECC_WRITE);
	chip->write_buf(mtd, oob_buf, secc_start);
	chip->calculate_ecc(mtd, &chip->data_poi[datidx], ecc_code);
	for (i = 0; i < eccbytes; i++, eccidx++)
		oob_buf[secc_start + i] = ecc_code[i];
	chip->write_buf(mtd, oob_buf+secc_start, eccbytes);
#else
	chip->write_buf(mtd, oob_buf, mtd->oobsize);
#endif
#if 0
//	if((page >= 0x80) && (page <= 0xbf))
	if(page == 0x80)
	{
		for(i=0; i<64; i++)
			printf("0x%02x ", oob_buf[i]);
		printf("\n");
	}
#endif
#else
	/* Write out complete page of data, take care of eccmode */
	switch (eccmode) {
	/* No ecc, write all */
	case NAND_ECC_NONE:
		//printk (KERN_WARNING "Writing data without ECC to NAND-FLASH is not recommended\n");
		chip->write_buf(mtd, chip->data_poi, mtd->oobblock);
		break;

	/* Software ecc 3/256, write all */
	case NAND_ECC_SOFT:
		for (; eccsteps; eccsteps--) {
			chip->calculate_ecc(mtd, &chip->data_poi[datidx], ecc_code);
			for (i = 0; i < 3; i++, eccidx++)
				oob_buf[oob_config[eccidx]] = ecc_code[i];
			datidx += chip->eccsize;
		}
		chip->write_buf(mtd, chip->data_poi, mtd->oobblock);
		break;
	default:
		eccbytes = chip->eccbytes;
		for (; eccsteps; eccsteps--) {
			/* enable hardware ecc logic for write */
			chip->enable_hwecc(mtd, NAND_ECC_WRITE);
			chip->write_buf(mtd, &chip->data_poi[datidx], chip->eccsize);
			chip->calculate_ecc(mtd, &chip->data_poi[datidx], ecc_code);
			for (i = 0; i < eccbytes; i++, eccidx++)
				oob_buf[oob_config[eccidx]] = ecc_code[i];
			/* If the hardware ecc provides syndromes then
			 * the ecc code must be written immidiately after
			 * the data bytes (words) */
			if (chip->options & NAND_HWECC_SYNDROME)
				chip->write_buf(mtd, ecc_code, eccbytes);
			datidx += chip->eccsize;
		}
		break;
	}

	/* Write out OOB data */
	if (chip->options & NAND_HWECC_SYNDROME)
		chip->write_buf(mtd, &oob_buf[oobsel->eccbytes], mtd->oobsize - oobsel->eccbytes);
	else if (eccmode != NAND_ECC_NONE)
		chip->write_buf(mtd, oob_buf, mtd->oobsize);
	else {
		for (i = 0; i < mtd->oobsize; i++) {
			if (oob_buf[i] != 0xff)
				break;
		}
		if (i < mtd->oobsize) {
			printf("Bad oob_buf: page = 0x%x, data = ", page);
			for (i = 0; i < mtd->oobsize; i++) {
				printf("%02x ", oob_buf[i]);
			}
			printf("\n");
		}
	}
#endif /* CONFIG_TQ2440_NAND_HWECC */

	/* Send command to actually program the data */
	chip->cmdfunc (mtd, cached ? NAND_CMD_CACHEDPROG : NAND_CMD_PAGEPROG, -1, -1);

	if (!cached) {
		/* call wait ready function */
		status = chip->waitfunc (mtd, chip, FL_WRITING);
		/* See if device thinks it succeeded */
		if (status & 0x01) {
			DEBUG (MTD_DEBUG_LEVEL0, "%s: " "Failed write, page 0x%08x, ", __FUNCTION__, page);
			return -EIO;
		}
	} else {
		/* FIXME: Implement cached programming ! */
		/* wait until cache is ready*/
		/* status = chip->waitfunc (mtd, chip, FL_CACHEDRPG); */
	}
	return 0;
}

#ifdef CONFIG_MTD_NAND_VERIFY_WRITE
/**
 * nand_verify_pages - [GENERIC] verify the chip contents after a write
 * @mtd:	MTD device structure
 * @chip:	NAND chip structure
 * @page: 	startpage inside the chip, must be called with (page & chip->pagemask)
 * @numpages:	number of pages to verify
 * @oob_buf:	out of band data buffer
 * @oobsel:	out of band selecttion structre
 * @chipnr:	number of the current chip
 * @oobmode:	1 = full buffer verify, 0 = ecc only
 *
 * The NAND device assumes that it is always writing to a cleanly erased page.
 * Hence, it performs its internal write verification only on bits that
 * transitioned from 1 to 0. The device does NOT verify the whole page on a
 * byte by byte basis. It is possible that the page was not completely erased
 * or the page is becoming unusable due to wear. The read with ECC would catch
 * the error later when the ECC page check fails, but we would rather catch
 * it early in the page write stage. Better to write no data than invalid data.
 */
static int nand_verify_pages (struct mtd_info *mtd, struct nand_chip *chip, int page, int numpages,
	u_char *oob_buf, struct nand_oobinfo *oobsel, int chipnr, int oobmode)
{
	int 	i, j, datidx = 0, oobofs = 0, res = -EIO;
	int	eccsteps = chip->eccsteps;
	int	hweccbytes;
	u_char 	oobdata[64];

	hweccbytes = (chip->options & NAND_HWECC_SYNDROME) ? (oobsel->eccbytes / eccsteps) : 0;

	/* Send command to read back the first page */
	chip->cmdfunc (mtd, NAND_CMD_READ0, 0, page);

	for(;;) {
		for (j = 0; j < eccsteps; j++) {
			/* Loop through and verify the data */
			if (chip->verify_buf(mtd, &chip->data_poi[datidx], mtd->eccsize)) {
				DEBUG (MTD_DEBUG_LEVEL0, "%s: " "Failed write verify, page 0x%08x ", __FUNCTION__, page);
				goto out;
			}
			datidx += mtd->eccsize;
			/* Have we a hw generator layout ? */
			if (!hweccbytes)
				continue;
			if (chip->verify_buf(mtd, &chip->oob_buf[oobofs], hweccbytes)) {
				DEBUG (MTD_DEBUG_LEVEL0, "%s: " "Failed write verify, page 0x%08x ", __FUNCTION__, page);
				goto out;
			}
			oobofs += hweccbytes;
		}

		/* check, if we must compare all data or if we just have to
		 * compare the ecc bytes
		 */
		if (oobmode) {
			if (chip->verify_buf(mtd, &oob_buf[oobofs], mtd->oobsize - hweccbytes * eccsteps)) {
				DEBUG (MTD_DEBUG_LEVEL0, "%s: " "Failed write verify, page 0x%08x ", __FUNCTION__, page);
				goto out;
			}
		} else {
			/* Read always, else autoincrement fails */
			chip->read_buf(mtd, oobdata, mtd->oobsize - hweccbytes * eccsteps);

			if (oobsel->useecc != MTD_NANDECC_OFF && !hweccbytes) {
				int ecccnt = oobsel->eccbytes;

				for (i = 0; i < ecccnt; i++) {
					int idx = oobsel->eccpos[i];
					if (oobdata[idx] != oob_buf[oobofs + idx] ) {
						DEBUG (MTD_DEBUG_LEVEL0,
					       	"%s: Failed ECC write "
						"verify, page 0x%08x, " "%6i bytes were succesful\n", __FUNCTION__, page, i);
						goto out;
					}
				}
			}
		}
		oobofs += mtd->oobsize - hweccbytes * eccsteps;
		page++;
		numpages--;

		/* Apply delay or wait for ready/busy pin
		 * Do this before the AUTOINCR check, so no problems
		 * arise if a chip which does auto increment
		 * is marked as NOAUTOINCR by the board driver.
		 * Do this also before returning, so the chip is
		 * ready for the next command.
		*/
		if (!chip->dev_ready)
			udelay (chip->chip_delay);
		else
			while (!chip->dev_ready(mtd));

		/* All done, return happy */
		if (!numpages)
			return 0;


		/* Check, if the chip supports auto page increment */
		if (!NAND_CANAUTOINCR(chip))
			chip->cmdfunc (mtd, NAND_CMD_READ0, 0x00, page);
	}
	/*
	 * Terminate the read command. We come here in case of an error
	 * So we must issue a reset command.
	 */
out:
	chip->cmdfunc (mtd, NAND_CMD_RESET, -1, -1);
	return res;
}
#endif

/**
 * nand_read - [MTD Interface] MTD compability function for nand_read_ecc
 * @mtd:	MTD device structure
 * @from:	offset to read from
 * @len:	number of bytes to read
 * @retlen:	pointer to variable to store the number of read bytes
 * @buf:	the databuffer to put data
 *
 * This function simply calls nand_read_ecc with oob buffer and oobsel = NULL
*/
static int nand_read (struct mtd_info *mtd, loff_t from, size_t len, size_t * retlen, u_char * buf)
{
	return nand_read_ecc (mtd, from, len, retlen, buf, NULL, NULL);
}


/**
 * nand_read_ecc - [MTD Interface] Read data with ECC
 * @mtd:	MTD device structure
 * @from:	offset to read from
 * @len:	number of bytes to read
 * @retlen:	pointer to variable to store the number of read bytes
 * @buf:	the databuffer to put data
 * @oob_buf:	filesystem supplied oob data buffer
 * @oobsel:	oob selection structure
 *
 * NAND read with ECC
 */
static int nand_read_ecc (struct mtd_info *mtd, loff_t from, size_t len,
			  size_t * retlen, u_char * buf, u_char * oob_buf, struct nand_oobinfo *oobsel)
{
	int i, j, col, realpage, page, end, ecc, chipnr, sndcmd = 1;
	int read = 0, oob = 0, ecc_status = 0, ecc_failed = 0;
	struct nand_chip *chip = mtd->priv;
	u_char *data_poi, *oob_data = oob_buf;
	u_char ecc_calc[32];
	u_char ecc_code[32];
	int eccmode, eccsteps;
	unsigned *oob_config;
	int	datidx = 0;
	int	blockcheck = (1 << (chip->phys_erase_shift - chip->page_shift)) - 1;
	int	eccbytes;
	int	compareecc = 1;
	int	oobreadlen;
#ifdef CONFIG_TQ2440_NAND_HWECC
	int secc_start;
#endif /* CONFIG_TQ2440_NAND_HWECC */


	DEBUG (MTD_DEBUG_LEVEL3, "nand_read_ecc: from = 0x%08x, len = %i\n", (unsigned int) from, (int) len);

	/* Do not allow reads past end of device */
	if ((from + len) > mtd->size) {
		DEBUG (MTD_DEBUG_LEVEL0, "nand_read_ecc: Attempt read beyond end of device\n");
		*retlen = 0;
		return -EINVAL;
	}

	/* Grab the lock and see if the device is available */
	nand_get_device (chip, mtd ,FL_READING);

	/* use userspace supplied oobinfo, if zero */
	if (oobsel == NULL)
		oobsel = &mtd->oobinfo;

	/* Autoplace of oob data ? Use the default placement scheme */
	if (oobsel->useecc == MTD_NANDECC_AUTOPLACE)
		oobsel = chip->autooob;

	eccmode = oobsel->useecc ? chip->eccmode : NAND_ECC_NONE;
	oob_config = oobsel->eccpos;

	/* Select the NAND device */
	chipnr = (int)(from >> chip->chip_shift);
	chip->select_chip(mtd, chipnr);

	/* First we calculate the starting page */
	realpage = (int) (from >> chip->page_shift);
	page = realpage & chip->pagemask;

	/* Get raw starting column */
	col = from & (mtd->oobblock - 1);

	end = mtd->oobblock;
	ecc = chip->eccsize;
	eccbytes = chip->eccbytes;
#ifdef CONFIG_TQ2440_NAND_HWECC
	secc_start = mtd->oobsize - eccbytes;
#endif /* CONFIG_TQ2440_NAND_HWECC */

	if ((eccmode == NAND_ECC_NONE) || (chip->options & NAND_HWECC_SYNDROME))
		compareecc = 0;

	oobreadlen = mtd->oobsize;
	if (chip->options & NAND_HWECC_SYNDROME)
		oobreadlen -= oobsel->eccbytes;

	/* Loop until all data read */
	while (read < len) {

		int aligned = (!col && (len - read) >= end);
		/*
		 * If the read is not page aligned, we have to read into data buffer
		 * due to ecc, else we read into return buffer direct
		 */
		if (aligned)
			data_poi = &buf[read];
		else
			data_poi = chip->data_buf;

		/* Check, if we have this page in the buffer
		 *
		 * FIXME: Make it work when we must provide oob data too,
		 * check the usage of data_buf oob field
		 */
		if (realpage == chip->pagebuf && !oob_buf) {
			/* aligned read ? */
			if (aligned)
				memcpy (data_poi, chip->data_buf, end);
			goto readdata;
		}

		/* Check, if we must send the read command */
		if (sndcmd) {
			chip->cmdfunc (mtd, NAND_CMD_READ0, 0x00, page);
			sndcmd = 0;
		}

		/* get oob area, if we have no oob buffer from fs-driver */
		if (!oob_buf || oobsel->useecc == MTD_NANDECC_AUTOPLACE ||
			oobsel->useecc == MTD_NANDECC_AUTOPL_USR)
			oob_data = &chip->data_buf[end];

		eccsteps = chip->eccsteps;

#if 0
//	if((page >= 0x80) && (page <= 0xbf))
	if(page == 0x80)
	{
		printf("ecc=%d, datidx=%d\n", ecc, datidx);
		for(i=0; i<64; i++)
		{
			oob_data[i] = 0x0;
			printf("0x%02x ", oob_data[i]);
		}
		printf("\n");
//		printf("ecc_code=\n");
	}
#endif
#ifdef CONFIG_TQ2440_NAND_HWECC
#if 0
		col = mtd->oobblock;
		chip->cmdfunc(mtd, NAND_CMD_RNDOUT, col, page);
//		chip->cmdfunc(mtd, NAND_CMD_READ0, col, page);

		/* spare area */
#if 0
		chip->enable_hwecc(mtd, NAND_ECC_READ);
		chip->read_buf(mtd, oob_data, secc_start);
		chip->calculate_ecc(mtd, &chip->data_poi[datidx], ecc_code);
		chip->read_buf(mtd, oob_data + secc_start, eccbytes);
#else
		chip->read_buf(mtd, oob_data, mtd->oobsize);
#endif
#if 1
//	if((page >= 0x80) && (page <= 0xbf))
	if(page == 0x80)
	{
		printf("ecc=%d, datidx=%d\n", ecc, datidx);
		for(i=0; i<64; i++)
			printf("0x%02x ", oob_data[i]);
		printf("\n");
//		printf("ecc_code=\n");
	}
#endif
#endif

		col = 0;

		/* main area */
		for(i = 0, datidx = 0; eccsteps; eccsteps--, i += eccbytes, datidx +=ecc, col += ecc) {
//			chip->cmdfunc(mtd, NAND_CMD_RNDOUT, col, -1);
			chip->cmdfunc(mtd, NAND_CMD_READ0, col, page);
			chip->enable_hwecc(mtd, NAND_ECC_READ);
			chip->read_buf(mtd, &data_poi[datidx], ecc);
			chip->calculate_ecc(mtd, &data_poi[datidx], &ecc_calc[i]);
#if 1
		}
		col = mtd->oobblock;
		chip->cmdfunc(mtd, NAND_CMD_RNDOUT, col, page);
//		chip->cmdfunc(mtd, NAND_CMD_READ0, col, page);

		/* spare area */
#if 0
		chip->enable_hwecc(mtd, NAND_ECC_READ);
		chip->read_buf(mtd, oob_data, secc_start);
		chip->calculate_ecc(mtd, &chip->data_poi[datidx], ecc_code);
		chip->read_buf(mtd, oob_data + secc_start, eccbytes);
#else
		chip->read_buf(mtd, oob_data, mtd->oobsize);
#endif
#if 0
//	if((page >= 0x80) && (page <= 0xbf))
	if(page == 0x80)
	{
		printf("ecc=%d, datidx=%d\n", ecc, datidx);
		for(i=0; i<64; i++)
			printf("0x%02x ", oob_data[i]);
		printf("\n");
//		printf("ecc_code=\n");
	}
#endif

		for(i = 0, datidx = 0; eccsteps; eccsteps--, i += eccbytes, datidx +=ecc) {
			if(chip->correct_data(mtd, &data_poi[datidx], &oob_data[i], &ecc_code[i]) == -1) {
printf("error\n");
				ecc_failed++;
			}
			col = ecc * (chip->eccsteps + 1 - eccsteps);
		}
#else
			if(chip->correct_data(mtd, &data_poi[datidx], &oob_data[i], &ecc_code[i]) == -1) {
printf("error\n");
				ecc_failed++;
			}
			col = ecc * (chip->eccsteps + 1 - eccsteps);
		}
#endif
#else /* !CONFIG_TQ2440_NAND_HWECC */
		switch (eccmode) {
		case NAND_ECC_NONE: {	/* No ECC, Read in a page */
/* XXX U-BOOT XXX */
#if 0
			static unsigned long lastwhinge = 0;
			if ((lastwhinge / HZ) != (jiffies / HZ)) {
				printk (KERN_WARNING "Reading data from NAND FLASH without ECC is not recommended\n");
				lastwhinge = jiffies;
			}
#else
			//puts("Reading data from NAND FLASH without ECC is not recommended\n");
#endif
			chip->read_buf(mtd, data_poi, end);
			break;
		}

		case NAND_ECC_SOFT:	/* Software ECC 3/256: Read in a page + oob data */
			chip->read_buf(mtd, data_poi, end);
			for (i = 0, datidx = 0; eccsteps; eccsteps--, i+=3, datidx += ecc)
				chip->calculate_ecc(mtd, &data_poi[datidx], &ecc_calc[i]);
			break;

		default:
			for (i = 0, datidx = 0; eccsteps; eccsteps--, i+=eccbytes, datidx += ecc) {
				chip->enable_hwecc(mtd, NAND_ECC_READ);
				chip->read_buf(mtd, &data_poi[datidx], ecc);

				/* HW ecc with syndrome calculation must read the
				 * syndrome from flash immidiately after the data */
				if (!compareecc) {
					/* Some hw ecc generators need to know when the
					 * syndrome is read from flash */
					chip->enable_hwecc(mtd, NAND_ECC_READSYN);
					chip->read_buf(mtd, &oob_data[i], eccbytes);
					/* We calc error correction directly, it checks the hw
					 * generator for an error, reads back the syndrome and
					 * does the error correction on the fly */
					if (chip->correct_data(mtd, &data_poi[datidx], &oob_data[i], &ecc_code[i]) == -1) {
						DEBUG (MTD_DEBUG_LEVEL0, "nand_read_ecc: "
							"Failed ECC read, page 0x%08x on chip %d\n", page, chipnr);
						ecc_failed++;
					}
				} else {
					chip->calculate_ecc(mtd, &data_poi[datidx], &ecc_calc[i]);
				}
			}
			break;
		}

		/* read oobdata */
		chip->read_buf(mtd, &oob_data[mtd->oobsize - oobreadlen], oobreadlen);

		/* Skip ECC check, if not requested (ECC_NONE or HW_ECC with syndromes) */
		if (!compareecc)
			goto readoob;

		/* Pick the ECC bytes out of the oob data */
		for (j = 0; j < oobsel->eccbytes; j++)
			ecc_code[j] = oob_data[oob_config[j]];

		/* correct data, if neccecary */
		for (i = 0, j = 0, datidx = 0; i < chip->eccsteps; i++, datidx += ecc) {
			ecc_status = chip->correct_data(mtd, &data_poi[datidx], &ecc_code[j], &ecc_calc[j]);

			/* Get next chunk of ecc bytes */
			j += eccbytes;

			/* Check, if we have a fs supplied oob-buffer,
			 * This is the legacy mode. Used by YAFFS1
			 * Should go away some day
			 */
			if (oob_buf && oobsel->useecc == MTD_NANDECC_PLACE) {
				int *p = (int *)(&oob_data[mtd->oobsize]);
				p[i] = ecc_status;
			}

			if (ecc_status == -1) {
				DEBUG (MTD_DEBUG_LEVEL0, "nand_read_ecc: " "Failed ECC read, page 0x%08x\n", page);
				ecc_failed++;
			}
		}

#endif /* CONFIG_TQ2440_NAND_HWECC */
#if 0
//	if((page >= 0x80) && (page <= 0xbf))
	if((page >= 0x80) && (page <= 0x81))
//	if(page == 0x80)
	{
//		printf("ecc=%d, datidx=%d\n", ecc, datidx);
//		for(i=0; i<512; i++)
//			printf("0x%02x ", data_poi[i]);
		printf("\nread=%d,buf=\n", read);
		for(i=0; i<2048; i++)
			printf("0x%02x ", buf[read+i]);
//		printf("\ndata_buf=\n");
//		for(i=0; i<64; i++)
//			printf("0x%02x ", chip->data_buf[i]);
		printf("\n");
	}
#endif
	readoob:
		/* check, if we have a fs supplied oob-buffer */
		if (oob_buf) {
			/* without autoplace. Legacy mode used by YAFFS1 */
			switch(oobsel->useecc) {
			case MTD_NANDECC_AUTOPLACE:
			case MTD_NANDECC_AUTOPL_USR:
				/* Walk through the autoplace chunks */
				for (i = 0, j = 0; j < mtd->oobavail; i++) {
					int from = oobsel->oobfree[i][0];
					int num = oobsel->oobfree[i][1];
					memcpy(&oob_buf[oob], &oob_data[from], num);
					j+= num;
				}
				oob += mtd->oobavail;
				break;
			case MTD_NANDECC_PLACE:
				/* YAFFS1 legacy mode */
				oob_data += chip->eccsteps * sizeof (int);
			default:
				oob_data += mtd->oobsize;
			}
		}
	readdata:
		/* Partial page read, transfer data into fs buffer */
		if (!aligned) {
			for (j = col; j < end && read < len; j++)
				buf[read++] = data_poi[j];
			chip->pagebuf = realpage;
		} else
			read += mtd->oobblock;

		/* Apply delay or wait for ready/busy pin
		 * Do this before the AUTOINCR check, so no problems
		 * arise if a chip which does auto increment
		 * is marked as NOAUTOINCR by the board driver.
		*/
		if (!chip->dev_ready)
			udelay (chip->chip_delay);
		else
			while (!chip->dev_ready(mtd));

		if (read == len)
			break;

		/* For subsequent reads align to page boundary. */
		col = 0;
		/* Increment page address */
		realpage++;

		page = realpage & chip->pagemask;
		/* Check, if we cross a chip boundary */
		if (!page) {
			chipnr++;
			chip->select_chip(mtd, -1);
			chip->select_chip(mtd, chipnr);
		}
		/* Check, if the chip supports auto page increment
		 * or if we have hit a block boundary.
		*/
		if (!NAND_CANAUTOINCR(chip) || !(page & blockcheck))
			sndcmd = 1;
	}

	/* Deselect and wake up anyone waiting on the device */
	nand_release_device(mtd);

	/*
	 * Return success, if no ECC failures, else -EBADMSG
	 * fs driver will take care of that, because
	 * retlen == desired len and result == -EBADMSG
	 */
	*retlen = read;
	return ecc_failed ? -EBADMSG : 0;
}

/**
 * nand_read_oob - [MTD Interface] NAND read out-of-band
 * @mtd:	MTD device structure
 * @from:	offset to read from
 * @len:	number of bytes to read
 * @retlen:	pointer to variable to store the number of read bytes
 * @buf:	the databuffer to put data
 *
 * NAND read out-of-band data from the spare area
 */
static int nand_read_oob (struct mtd_info *mtd, loff_t from, size_t len, size_t * retlen, u_char * buf)
{
	int i, col, page, chipnr;
	struct nand_chip *chip = mtd->priv;
	int	blockcheck = (1 << (chip->phys_erase_shift - chip->page_shift)) - 1;

	DEBUG (MTD_DEBUG_LEVEL3, "nand_read_oob: from = 0x%08x, len = %i\n", (unsigned int) from, (int) len);

	/* Shift to get page */
	page = (int)(from >> chip->page_shift);
	chipnr = (int)(from >> chip->chip_shift);

	/* Mask to get column */
	col = from & (mtd->oobsize - 1);

	/* Initialize return length value */
	*retlen = 0;

	/* Do not allow reads past end of device */
	if ((from + len) > mtd->size) {
		DEBUG (MTD_DEBUG_LEVEL0, "nand_read_oob: Attempt read beyond end of device\n");
		*retlen = 0;
		return -EINVAL;
	}

	/* Grab the lock and see if the device is available */
	nand_get_device (chip, mtd , FL_READING);

	/* Select the NAND device */
	chip->select_chip(mtd, chipnr);

	/* Send the read command */
	chip->cmdfunc (mtd, NAND_CMD_READOOB, col, page & chip->pagemask);
	/*
	 * Read the data, if we read more than one page
	 * oob data, let the device transfer the data !
	 */
	i = 0;
	while (i < len) {
		int thislen = mtd->oobsize - col;
		thislen = min_t(int, thislen, len);
		chip->read_buf(mtd, &buf[i], thislen);
		i += thislen;

		/* Apply delay or wait for ready/busy pin
		 * Do this before the AUTOINCR check, so no problems
		 * arise if a chip which does auto increment
		 * is marked as NOAUTOINCR by the board driver.
		*/
		if (!chip->dev_ready)
			udelay (chip->chip_delay);
		else
			while (!chip->dev_ready(mtd));

		/* Read more ? */
		if (i < len) {
			page++;
			col = 0;

			/* Check, if we cross a chip boundary */
			if (!(page & chip->pagemask)) {
				chipnr++;
				chip->select_chip(mtd, -1);
				chip->select_chip(mtd, chipnr);
			}

			/* Check, if the chip supports auto page increment
			 * or if we have hit a block boundary.
			*/
			if (!NAND_CANAUTOINCR(chip) || !(page & blockcheck)) {
				/* For subsequent page reads set offset to 0 */
				chip->cmdfunc (mtd, NAND_CMD_READOOB, 0x0, page & chip->pagemask);
			}
		}
	}

	/* Deselect and wake up anyone waiting on the device */
	nand_release_device(mtd);

	/* Return happy */
	*retlen = len;
	return 0;
}

/**
 * nand_read_raw - [GENERIC] Read raw data including oob into buffer
 * @mtd:	MTD device structure
 * @buf:	temporary buffer
 * @from:	offset to read from
 * @len:	number of bytes to read
 * @ooblen:	number of oob data bytes to read
 *
 * Read raw data including oob into buffer
 */
int nand_read_raw (struct mtd_info *mtd, uint8_t *buf, loff_t from, size_t len, size_t ooblen)
{
	struct nand_chip *chip = mtd->priv;
	int page = (int) (from >> chip->page_shift);
	int this = (int) (from >> chip->chip_shift);
	int sndcmd = 1;
	int cnt = 0;
	int pagesize = mtd->oobblock + mtd->oobsize;
	int	blockcheck = (1 << (chip->phys_erase_shift - chip->page_shift)) - 1;

	/* Do not allow reads past end of device */
	if ((from + len) > mtd->size) {
		DEBUG (MTD_DEBUG_LEVEL0, "nand_read_raw: Attempt read beyond end of device\n");
		return -EINVAL;
	}

	/* Grab the lock and see if the device is available */
	nand_get_device (chip, mtd , FL_READING);

	chip->select_chip (mtd, this);

	/* Add requested oob length */
	len += ooblen;

	while (len) {
		if (sndcmd)
			chip->cmdfunc (mtd, NAND_CMD_READ0, 0, page & chip->pagemask);
		sndcmd = 0;

		chip->read_buf (mtd, &buf[cnt], pagesize);

		len -= pagesize;
		cnt += pagesize;
		page++;

		if (!chip->dev_ready)
			udelay (chip->chip_delay);
		else
			while (!chip->dev_ready(mtd));

		/* Check, if the chip supports auto page increment */
		if (!NAND_CANAUTOINCR(chip) || !(page & blockcheck))
			sndcmd = 1;
	}

	/* Deselect and wake up anyone waiting on the device */
	nand_release_device(mtd);
	return 0;
}


/**
 * nand_prepare_oobbuf - [GENERIC] Prepare the out of band buffer
 * @mtd:	MTD device structure
 * @fsbuf:	buffer given by fs driver
 * @oobsel:	out of band selection structre
 * @autoplace:	1 = place given buffer into the oob bytes
 * @numpages:	number of pages to prepare
 *
 * Return:
 * 1. Filesystem buffer available and autoplacement is off,
 *    return filesystem buffer
 * 2. No filesystem buffer or autoplace is off, return internal
 *    buffer
 * 3. Filesystem buffer is given and autoplace selected
 *    put data from fs buffer into internal buffer and
 *    retrun internal buffer
 *
 * Note: The internal buffer is filled with 0xff. This must
 * be done only once, when no autoplacement happens
 * Autoplacement sets the buffer dirty flag, which
 * forces the 0xff fill before using the buffer again.
 *
*/
static u_char * nand_prepare_oobbuf (struct mtd_info *mtd, u_char *fsbuf, struct nand_oobinfo *oobsel,
		int autoplace, int numpages)
{
	struct nand_chip *chip = mtd->priv;
	int i, len, ofs;

	/* Zero copy fs supplied buffer */
	if (fsbuf && !autoplace)
		return fsbuf;

	/* Check, if the buffer must be filled with ff again */
	if (chip->oobdirty) {
		memset (chip->oob_buf, 0xff,
			mtd->oobsize << (chip->phys_erase_shift - chip->page_shift));
		chip->oobdirty = 0;
	}

	/* If we have no autoplacement or no fs buffer use the internal one */
	if (!autoplace || !fsbuf) {				//HJ_add start
		chip->oobdirty = 1;
		return chip->oob_buf;
	}						//HJ_add end

	/* Walk through the pages and place the data */
	chip->oobdirty = 1;
	ofs = 0;
	while (numpages--) {
		for (i = 0, len = 0; len < mtd->oobavail; i++) {
			int to = ofs + oobsel->oobfree[i][0];
			int num = oobsel->oobfree[i][1];
			memcpy (&chip->oob_buf[to], fsbuf, num);
			len += num;
			fsbuf += num;
		}
		ofs += mtd->oobavail;
	}
	return chip->oob_buf;
}

#define NOTALIGNED(x) (x & (mtd->oobblock-1)) != 0

/**
 * nand_write - [MTD Interface] compability function for nand_write_ecc
 * @mtd:	MTD device structure
 * @to:		offset to write to
 * @len:	number of bytes to write
 * @retlen:	pointer to variable to store the number of written bytes
 * @buf:	the data to write
 *
 * This function simply calls nand_write_ecc with oob buffer and oobsel = NULL
 *
*/
static int nand_write (struct mtd_info *mtd, loff_t to, size_t len, size_t * retlen, const u_char * buf)
{
	return (nand_write_ecc (mtd, to, len, retlen, buf, NULL, NULL));
}

/**
 * nand_write_ecc - [MTD Interface] NAND write with ECC
 * @mtd:	MTD device structure
 * @to:		offset to write to
 * @len:	number of bytes to write
 * @retlen:	pointer to variable to store the number of written bytes
 * @buf:	the data to write
 * @eccbuf:	filesystem supplied oob data buffer
 * @oobsel:	oob selection structure
 *
 * NAND write with ECC
 */
static int nand_write_ecc (struct mtd_info *mtd, loff_t to, size_t len_all,
			   size_t * retlen, const u_char * buf, u_char * eccbuf, struct nand_oobinfo *oobsel)
{
	int startpage, page, ret = -EIO, oob = 0, written = 0, chipnr;
	int autoplace = 0, numpages, totalpages;
	struct nand_chip *chip = mtd->priv;
	u_char *oobbuf, *bufstart;
	int	ppblock = (1 << (chip->phys_erase_shift - chip->page_shift));
#ifdef CONFIG_TQ2440_NAND_HWECC
	size_t len;
	int write_yaffs = (len_all == (mtd->oobblock+mtd->oobsize))?1:0;
	len = (write_yaffs)?(len_all-mtd->oobsize):(len_all);
	int i;
#endif /* CONFIG_TQ2440_NAND_HWECC */

	DEBUG (MTD_DEBUG_LEVEL3, "nand_write_ecc: to = 0x%08x, len = %i\n", (unsigned int) to, (int) len);

	/* Initialize retlen, in case of early exit */
	*retlen = 0;

	/* Do not allow write past end of device */
	if ((to + len) > mtd->size) {
		DEBUG (MTD_DEBUG_LEVEL0, "nand_write_ecc: Attempt to write past end of page\n");
		return -EINVAL;
	}

	/* reject writes, which are not page aligned */
	if (NOTALIGNED (to) || NOTALIGNED(len)) {
		printk (KERN_NOTICE "nand_write_ecc: Attempt to write not page aligned data\n");
		return -EINVAL;
	}

	/* Grab the lock and see if the device is available */
	nand_get_device (chip, mtd, FL_WRITING);

	/* Calculate chipnr */
	chipnr = (int)(to >> chip->chip_shift);
	/* Select the NAND device */
	chip->select_chip(mtd, chipnr);

	/* Check, if it is write protected */
	if (nand_check_wp(mtd))
		goto out;

	/* if oobsel is NULL, use chip defaults */
	if (oobsel == NULL)
		oobsel = &mtd->oobinfo;

	/* Autoplace of oob data ? Use the default placement scheme */
	if (oobsel->useecc == MTD_NANDECC_AUTOPLACE) {
		oobsel = chip->autooob;
		autoplace = 1;
	}
	if (oobsel->useecc == MTD_NANDECC_AUTOPL_USR)
		autoplace = 1;

	/* Setup variables and oob buffer */
	totalpages = len >> chip->page_shift;
	page = (int) (to >> chip->page_shift);
	/* Invalidate the page cache, if we write to the cached page */
	if (page <= chip->pagebuf && chip->pagebuf < (page + totalpages))
		chip->pagebuf = -1;

	/* Set it relative to chip */
	page &= chip->pagemask;
	startpage = page;
	/* Calc number of pages we can write in one go */
	numpages = min (ppblock - (startpage  & (ppblock - 1)), totalpages);
#ifdef CONFIG_TQ2440_NAND_HWECC
	if(write_yaffs)
		oobbuf = (u_char *)(buf + len);
	else
		oobbuf = nand_prepare_oobbuf (mtd, eccbuf, oobsel, autoplace, numpages);
#else
	oobbuf = nand_prepare_oobbuf (mtd, eccbuf, oobsel, autoplace, numpages);
#endif /* CONFIG_TQ2440_NAND_HWECC */
	bufstart = (u_char *)buf;

	/* Loop until all data is written */
	while (written < len) {

		chip->data_poi = (u_char*) &buf[written];
#if 0
//	if(to == 0x40000)
	if(page == 0x80)
//	if((page >= 0x80) && (page <= 0x81))
	{
		printf("page=0x%x\n", page);
		for(i=0; i<2048; i++)
			printf("0x%02x ", buf[i+written]);
		printf("\n");
//		for(i=0; i<64; i++)
//			printf("0x%02x ", chip->data_poi[i]);
//		printf("\n");
	}
#endif

		/* Write one page. If this is the last page to write
		 * or the last page in this block, then use the
		 * real pageprogram command, else select cached programming
		 * if supported by the chip.
		 */
		ret = nand_write_page (mtd, chip, page, &oobbuf[oob], oobsel, (--numpages > 0));
		if (ret) {
			DEBUG (MTD_DEBUG_LEVEL0, "nand_write_ecc: write_page failed %d\n", ret);
			goto out;
		}
		/* Next oob page */
		oob += mtd->oobsize;
		/* Update written bytes count */
		written += mtd->oobblock;
		if (written == len)
			goto cmp;

		/* Increment page address */
		page++;

		/* Have we hit a block boundary ? Then we have to verify and
		 * if verify is ok, we have to setup the oob buffer for
		 * the next pages.
		*/
		if (!(page & (ppblock - 1))){
			int ofs;
			chip->data_poi = bufstart;
			ret = nand_verify_pages (mtd, chip, startpage,
				page - startpage,
				oobbuf, oobsel, chipnr, (eccbuf != NULL));
			if (ret) {
				DEBUG (MTD_DEBUG_LEVEL0, "nand_write_ecc: verify_pages failed %d\n", ret);
				goto out;
			}
			*retlen = written;

			ofs = autoplace ? mtd->oobavail : mtd->oobsize;
			if (eccbuf)
				eccbuf += (page - startpage) * ofs;
			totalpages -= page - startpage;
			numpages = min (totalpages, ppblock);
			page &= chip->pagemask;
			startpage = page;
			oob = 0;
			chip->oobdirty = 1;
			oobbuf = nand_prepare_oobbuf (mtd, eccbuf, oobsel,
					autoplace, numpages);
			/* Check, if we cross a chip boundary */
			if (!page) {
				chipnr++;
				chip->select_chip(mtd, -1);
				chip->select_chip(mtd, chipnr);
			}
		}
	}
	/* Verify the remaining pages */
cmp:
	chip->data_poi = bufstart;
 	ret = nand_verify_pages (mtd, chip, startpage, totalpages,
		oobbuf, oobsel, chipnr, (eccbuf != NULL));
	if (!ret)
		*retlen = written;
	else
		DEBUG (MTD_DEBUG_LEVEL0, "nand_write_ecc: verify_pages failed %d\n", ret);

out:
	/* Deselect and wake up anyone waiting on the device */
	nand_release_device(mtd);

	return ret;
}


/*
 * NAND write
 */
int
nand_write_calmecc(struct mtd_info *mtd, loff_t to, size_t len, 
           size_t *retlen, const u_char *buf, unsigned int *pdwECCVal)
{
	int i, page, col, cnt, status, chipnr;    
	struct nand_chip *chip = mtd->priv;
	S3C2440_NAND * const s3c2440nand = S3C2440_GetBase_NAND();

	DEBUG(MTD_DEBUG_LEVEL3,
		"%s(): to = 0x%08x, len = %i\n", __FUNCTION__, (unsigned int)to,
		(int) len);

	/* Do not allow write past end of page */
	if ((to + len) > mtd->size) {
		DEBUG(MTD_DEBUG_LEVEL0,
			"%s(): Attempted write past end of device\n", __FUNCTION__);
		return -EINVAL;
	}

	/* Shift to get page */
	page = ((int)to) >> chip->page_shift;

	/* Get the starting column */
	col = to & (mtd->oobblock - 1);

	/* Initialize return length value */
	*retlen = 0;

	/* Grab the lock and see if the device is available */
	nand_get_device (chip, mtd, FL_WRITING);

	/* Calculate chipnr */
	chipnr = (int)(to >> chip->chip_shift);
	/* Select the NAND device */
	chip->select_chip(mtd, chipnr);

	/* Check, if it is write protected */
	if (nand_check_wp(mtd))
		goto nand_write_exit;

	/* Loop until all data is written */
	while (*retlen < len) {
		/* Write data into buffer */
		if ((col + len) >= mtd->oobblock)
			for (i = col, cnt = 0; i < mtd->oobblock; i++, cnt++)
				chip->data_buf[i] = buf[(*retlen + cnt)];
		else
			for (i = col, cnt = 0; cnt < (len - *retlen); i++, cnt++)
				chip->data_buf[i] = buf[(*retlen + cnt)];
		/* Write ones for partial page programming */
		for (i = mtd->oobblock; i < (mtd->oobblock + mtd->oobsize); i++)
			chip->data_buf[i] = 0xff;
		
		/* Write pre-padding bytes into buffer */
		for (i = 0; i < col; i++)
			chip->data_buf[i] = 0xff;

		/* Write post-padding bytes into buffer */
		if ((col + (len - *retlen)) < mtd->oobblock) {
			for (i = (col + cnt); i < mtd->oobblock; i++)
				chip->data_buf[i] = 0xff;
		}

        /* HJ for cal ECC */
        s3c2440nand->NFCONT |= (1<<4);   // Reset ECC
        s3c2440nand->NFCONT &= ~(1<<5);  // UnLock MECC

		/* Send command to begin auto page programming */
		chip->cmdfunc(mtd, NAND_CMD_SEQIN, 0x00, page);

		/* Write out complete page of data */
		chip->write_buf(mtd, chip->data_buf, mtd->oobblock);

        s3c2440nand->NFCONT |= (1<<5);   // Lock MECC
        *pdwECCVal = s3c2440nand->NFMECC0; // Read MECC

		chip->write_buf(mtd, &chip->data_buf[mtd->oobblock], mtd->oobsize);
        
    	/* Send command to actually program the data */
    	chip->cmdfunc (mtd, NAND_CMD_PAGEPROG, -1, -1);

    	/* call wait ready function */
    	status = chip->waitfunc (mtd, chip, FL_WRITING);
    	/* See if device thinks it succeeded */
    	if (status & 0x01) {
    		DEBUG (MTD_DEBUG_LEVEL0, "%s: " "Failed write, page 0x%08x, ", __FUNCTION__, page);
    		return -EIO;
    	}

		/* 
		 * If we are writing a large amount of data and/or it
		 * crosses page or half-page boundaries, we set the
		 * the column to zero. It simplifies the program logic.
		 */
		if (col)
			col = 0x00;

		/* Update written bytes count */
		*retlen += cnt;

		/* Increment page address */
		page++;
		
	}

	/* Return happy */
	*retlen = len;
	i = 0;

nand_write_exit:
	/* Deselect and wake up anyone waiting on the device */
	nand_release_device(mtd);

	return i;
}


/**
 * nand_write_oob - [MTD Interface] NAND write out-of-band
 * @mtd:	MTD device structure
 * @to:		offset to write to
 * @len:	number of bytes to write
 * @retlen:	pointer to variable to store the number of written bytes
 * @buf:	the data to write
 *
 * NAND write out-of-band
 */
static int nand_write_oob (struct mtd_info *mtd, loff_t to, size_t len, size_t * retlen, const u_char * buf)
{
	int column, page, status, ret = -EIO, chipnr;
	struct nand_chip *chip = mtd->priv;

	DEBUG (MTD_DEBUG_LEVEL3, "nand_write_oob: to = 0x%08x, len = %i\n", (unsigned int) to, (int) len);

	/* Shift to get page */
	page = (int) (to >> chip->page_shift);
	chipnr = (int) (to >> chip->chip_shift);

	/* Mask to get column */
	column = to & (mtd->oobsize - 1);

	/* Initialize return length value */
	*retlen = 0;

	/* Do not allow write past end of page */
	if ((column + len) > mtd->oobsize) {
		DEBUG (MTD_DEBUG_LEVEL0, "nand_write_oob: Attempt to write past end of page\n");
		return -EINVAL;
	}

	/* Grab the lock and see if the device is available */
	nand_get_device (chip, mtd, FL_WRITING);

	/* Select the NAND device */
	chip->select_chip(mtd, chipnr);

	/* Reset the chip. Some chips (like the Toshiba TC5832DC found
	   in one of my DiskOnChip 2000 test units) will clear the whole
	   data page too if we don't do this. I have no clue why, but
	   I seem to have 'fixed' it in the doc2000 driver in
	   August 1999.  dwmw2. */
	chip->cmdfunc(mtd, NAND_CMD_RESET, -1, -1);

	/* Check, if it is write protected */
	if (nand_check_wp(mtd))
		goto out;

	/* Invalidate the page cache, if we write to the cached page */
	if (page == chip->pagebuf)
		chip->pagebuf = -1;

	if (NAND_MUST_PAD(chip)) {
		/* Write out desired data */
		chip->cmdfunc (mtd, NAND_CMD_SEQIN, mtd->oobblock, page & chip->pagemask);
		/* prepad 0xff for partial programming */
		chip->write_buf(mtd, ffchars, column);
		/* write data */
		chip->write_buf(mtd, buf, len);
		/* postpad 0xff for partial programming */
		chip->write_buf(mtd, ffchars, mtd->oobsize - (len+column));
	} else {
		/* Write out desired data */
		chip->cmdfunc (mtd, NAND_CMD_SEQIN, mtd->oobblock + column, page & chip->pagemask);
		/* write data */
		chip->write_buf(mtd, buf, len);
	}
	/* Send command to program the OOB data */
	chip->cmdfunc (mtd, NAND_CMD_PAGEPROG, -1, -1);

	status = chip->waitfunc (mtd, chip, FL_WRITING);

	/* See if device thinks it succeeded */
	if (status & 0x01) {
		DEBUG (MTD_DEBUG_LEVEL0, "nand_write_oob: " "Failed write, page 0x%08x\n", page);
		ret = -EIO;
		goto out;
	}
	/* Return happy */
	*retlen = len;

#ifdef CONFIG_MTD_NAND_VERIFY_WRITE
	/* Send command to read back the data */
	chip->cmdfunc (mtd, NAND_CMD_READOOB, column, page & chip->pagemask);

	if (chip->verify_buf(mtd, buf, len)) {
		DEBUG (MTD_DEBUG_LEVEL0, "nand_write_oob: " "Failed write verify, page 0x%08x\n", page);
		ret = -EIO;
		goto out;
	}
#endif
	ret = 0;
out:
	/* Deselect and wake up anyone waiting on the device */
	nand_release_device(mtd);

	return ret;
}

/* XXX U-BOOT XXX */
#if 0
/**
 * nand_writev - [MTD Interface] compabilty function for nand_writev_ecc
 * @mtd:	MTD device structure
 * @vecs:	the iovectors to write
 * @count:	number of vectors
 * @to:		offset to write to
 * @retlen:	pointer to variable to store the number of written bytes
 *
 * NAND write with kvec. This just calls the ecc function
 */
static int nand_writev (struct mtd_info *mtd, const struct kvec *vecs, unsigned long count,
		loff_t to, size_t * retlen)
{
	return (nand_writev_ecc (mtd, vecs, count, to, retlen, NULL, NULL));
}

/**
 * nand_writev_ecc - [MTD Interface] write with iovec with ecc
 * @mtd:	MTD device structure
 * @vecs:	the iovectors to write
 * @count:	number of vectors
 * @to:		offset to write to
 * @retlen:	pointer to variable to store the number of written bytes
 * @eccbuf:	filesystem supplied oob data buffer
 * @oobsel:	oob selection structure
 *
 * NAND write with iovec with ecc
 */
static int nand_writev_ecc (struct mtd_info *mtd, const struct kvec *vecs, unsigned long count,
		loff_t to, size_t * retlen, u_char *eccbuf, struct nand_oobinfo *oobsel)
{
	int i, page, len, total_len, ret = -EIO, written = 0, chipnr;
	int oob, numpages, autoplace = 0, startpage;
	struct nand_chip *chip = mtd->priv;
	int	ppblock = (1 << (chip->phys_erase_shift - chip->page_shift));
	u_char *oobbuf, *bufstart;

	/* Preset written len for early exit */
	*retlen = 0;

	/* Calculate total length of data */
	total_len = 0;
	for (i = 0; i < count; i++)
		total_len += (int) vecs[i].iov_len;

	DEBUG (MTD_DEBUG_LEVEL3,
	       "nand_writev: to = 0x%08x, len = %i, count = %ld\n", (unsigned int) to, (unsigned int) total_len, count);

	/* Do not allow write past end of page */
	if ((to + total_len) > mtd->size) {
		DEBUG (MTD_DEBUG_LEVEL0, "nand_writev: Attempted write past end of device\n");
		return -EINVAL;
	}

	/* reject writes, which are not page aligned */
	if (NOTALIGNED (to) || NOTALIGNED(total_len)) {
		printk (KERN_NOTICE "nand_write_ecc: Attempt to write not page aligned data\n");
		return -EINVAL;
	}

	/* Grab the lock and see if the device is available */
	nand_get_device (chip, mtd, FL_WRITING);

	/* Get the current chip-nr */
	chipnr = (int) (to >> chip->chip_shift);
	/* Select the NAND device */
	chip->select_chip(mtd, chipnr);

	/* Check, if it is write protected */
	if (nand_check_wp(mtd))
		goto out;

	/* if oobsel is NULL, use chip defaults */
	if (oobsel == NULL)
		oobsel = &mtd->oobinfo;

	/* Autoplace of oob data ? Use the default placement scheme */
	if (oobsel->useecc == MTD_NANDECC_AUTOPLACE) {
		oobsel = chip->autooob;
		autoplace = 1;
	}
	if (oobsel->useecc == MTD_NANDECC_AUTOPL_USR)
		autoplace = 1;

	/* Setup start page */
	page = (int) (to >> chip->page_shift);
	/* Invalidate the page cache, if we write to the cached page */
	if (page <= chip->pagebuf && chip->pagebuf < ((to + total_len) >> chip->page_shift))
		chip->pagebuf = -1;

	startpage = page & chip->pagemask;

	/* Loop until all kvec' data has been written */
	len = 0;
	while (count) {
		/* If the given tuple is >= pagesize then
		 * write it out from the iov
		 */
		if ((vecs->iov_len - len) >= mtd->oobblock) {
			/* Calc number of pages we can write
			 * out of chip iov in one go */
			numpages = (vecs->iov_len - len) >> chip->page_shift;
			/* Do not cross block boundaries */
			numpages = min (ppblock - (startpage & (ppblock - 1)), numpages);
			oobbuf = nand_prepare_oobbuf (mtd, NULL, oobsel, autoplace, numpages);
			bufstart = (u_char *)vecs->iov_base;
			bufstart += len;
			chip->data_poi = bufstart;
			oob = 0;
			for (i = 1; i <= numpages; i++) {
				/* Write one page. If this is the last page to write
				 * then use the real pageprogram command, else select
				 * cached programming if supported by the chip.
				 */
				ret = nand_write_page (mtd, chip, page & chip->pagemask,
					&oobbuf[oob], oobsel, i != numpages);
				if (ret)
					goto out;
				chip->data_poi += mtd->oobblock;
				len += mtd->oobblock;
				oob += mtd->oobsize;
				page++;
			}
			/* Check, if we have to switch to the next tuple */
			if (len >= (int) vecs->iov_len) {
				vecs++;
				len = 0;
				count--;
			}
		} else {
			/* We must use the internal buffer, read data out of each
			 * tuple until we have a full page to write
			 */
			int cnt = 0;
			while (cnt < mtd->oobblock) {
				if (vecs->iov_base != NULL && vecs->iov_len)
					chip->data_buf[cnt++] = ((u_char *) vecs->iov_base)[len++];
				/* Check, if we have to switch to the next tuple */
				if (len >= (int) vecs->iov_len) {
					vecs++;
					len = 0;
					count--;
				}
			}
			chip->pagebuf = page;
			chip->data_poi = chip->data_buf;
			bufstart = chip->data_poi;
			numpages = 1;
			oobbuf = nand_prepare_oobbuf (mtd, NULL, oobsel, autoplace, numpages);
			ret = nand_write_page (mtd, chip, page & chip->pagemask,
				oobbuf, oobsel, 0);
			if (ret)
				goto out;
			page++;
		}

		chip->data_poi = bufstart;
		ret = nand_verify_pages (mtd, chip, startpage, numpages, oobbuf, oobsel, chipnr, 0);
		if (ret)
			goto out;

		written += mtd->oobblock * numpages;
		/* All done ? */
		if (!count)
			break;

		startpage = page & chip->pagemask;
		/* Check, if we cross a chip boundary */
		if (!startpage) {
			chipnr++;
			chip->select_chip(mtd, -1);
			chip->select_chip(mtd, chipnr);
		}
	}
	ret = 0;
out:
	/* Deselect and wake up anyone waiting on the device */
	nand_release_device(mtd);

	*retlen = written;
	return ret;
}
#endif

/**
 * single_erease_cmd - [GENERIC] NAND standard block erase command function
 * @mtd:	MTD device structure
 * @page:	the page address of the block which will be erased
 *
 * Standard erase command for NAND chips
 */
static void single_erase_cmd (struct mtd_info *mtd, int page)
{
	struct nand_chip *chip = mtd->priv;
	/* Send commands to erase a block */
	chip->cmdfunc (mtd, NAND_CMD_ERASE1, -1, page);
	chip->cmdfunc (mtd, NAND_CMD_ERASE2, -1, -1);
}

/**
 * multi_erease_cmd - [GENERIC] AND specific block erase command function
 * @mtd:	MTD device structure
 * @page:	the page address of the block which will be erased
 *
 * AND multi block erase command function
 * Erase 4 consecutive blocks
 */
static void multi_erase_cmd (struct mtd_info *mtd, int page)
{
	struct nand_chip *chip = mtd->priv;
	/* Send commands to erase a block */
	chip->cmdfunc (mtd, NAND_CMD_ERASE1, -1, page++);
	chip->cmdfunc (mtd, NAND_CMD_ERASE1, -1, page++);
	chip->cmdfunc (mtd, NAND_CMD_ERASE1, -1, page++);
	chip->cmdfunc (mtd, NAND_CMD_ERASE1, -1, page);
	chip->cmdfunc (mtd, NAND_CMD_ERASE2, -1, -1);
}

/**
 * nand_erase - [MTD Interface] erase block(s)
 * @mtd:	MTD device structure
 * @instr:	erase instruction
 *
 * Erase one ore more blocks
 */
static int nand_erase (struct mtd_info *mtd, struct erase_info *instr)
{
	return nand_erase_nand (mtd, instr, 0);
}

/**
 * nand_erase_intern - [NAND Interface] erase block(s)
 * @mtd:	MTD device structure
 * @instr:	erase instruction
 * @allowbbt:	allow erasing the bbt area
 *
 * Erase one ore more blocks
 */
int nand_erase_nand (struct mtd_info *mtd, struct erase_info *instr, int allowbbt)
{
	int page, len, status, pages_per_block, ret, chipnr;
	struct nand_chip *chip = mtd->priv;

	DEBUG (MTD_DEBUG_LEVEL3,
	       "nand_erase: start = 0x%08x, len = %i\n", (unsigned int) instr->addr, (unsigned int) instr->len);

	/* Start address must align on block boundary */
	if (instr->addr & ((1 << chip->phys_erase_shift) - 1)) {
		DEBUG (MTD_DEBUG_LEVEL0, "nand_erase: Unaligned address\n");
		return -EINVAL;
	}

	/* Length must align on block boundary */
	if (instr->len & ((1 << chip->phys_erase_shift) - 1)) {
		DEBUG (MTD_DEBUG_LEVEL0, "nand_erase: Length not block aligned\n");
		return -EINVAL;
	}

	/* Do not allow erase past end of device */
	if ((instr->len + instr->addr) > mtd->size) {
		DEBUG (MTD_DEBUG_LEVEL0, "nand_erase: Erase past end of device\n");
		return -EINVAL;
	}

	instr->fail_addr = 0xffffffff;

	/* Grab the lock and see if the device is available */
	nand_get_device (chip, mtd, FL_ERASING);

	/* Shift to get first page */
	page = (int) (instr->addr >> chip->page_shift);
	chipnr = (int) (instr->addr >> chip->chip_shift);

	/* Calculate pages in each block */
	pages_per_block = 1 << (chip->phys_erase_shift - chip->page_shift);

	/* Select the NAND device */
	chip->select_chip(mtd, chipnr);

	/* Check the WP bit */
	/* Check, if it is write protected */
	if (nand_check_wp(mtd)) {
		DEBUG (MTD_DEBUG_LEVEL0, "nand_erase: Device is write protected!!!\n");
		instr->state = MTD_ERASE_FAILED;
		goto erase_exit;
	}

	/* Loop through the pages */
	len = instr->len;

	instr->state = MTD_ERASING;

	while (len) {
#ifndef NAND_ALLOW_ERASE_ALL
		/* Check if we have a bad block, we do not erase bad blocks ! */
		if (nand_block_checkbad(mtd, ((loff_t) page) << chip->page_shift, 0, allowbbt)) {
			printk (KERN_WARNING "nand_erase: attempt to erase a bad block at page 0x%08x\n", page);
			instr->state = MTD_ERASE_FAILED;
			goto erase_exit;
		}
#endif
		/* Invalidate the page cache, if we erase the block which contains
		   the current cached page */
		if (page <= chip->pagebuf && chip->pagebuf < (page + pages_per_block))
			chip->pagebuf = -1;

		chip->erase_cmd (mtd, page & chip->pagemask);

		status = chip->waitfunc (mtd, chip, FL_ERASING);

		/* See if block erase succeeded */
		if (status & 0x01) {
			DEBUG (MTD_DEBUG_LEVEL0, "nand_erase: " "Failed erase, page 0x%08x\n", page);
			instr->state = MTD_ERASE_FAILED;
			instr->fail_addr = (page << chip->page_shift);
			goto erase_exit;
		}

		/* Increment page address and decrement length */
		len -= (1 << chip->phys_erase_shift);
		page += pages_per_block;

		/* Check, if we cross a chip boundary */
		if (len && !(page & chip->pagemask)) {
			chipnr++;
			chip->select_chip(mtd, -1);
			chip->select_chip(mtd, chipnr);
		}
	}
	instr->state = MTD_ERASE_DONE;

erase_exit:

	ret = instr->state == MTD_ERASE_DONE ? 0 : -EIO;
	/* Do call back function */
	if (!ret)
		mtd_erase_callback(instr);

	/* Deselect and wake up anyone waiting on the device */
	nand_release_device(mtd);

	/* Return more or less happy */
	return ret;
}

/**
 * nand_sync - [MTD Interface] sync
 * @mtd:	MTD device structure
 *
 * Sync is actually a wait for chip ready function
 */
static void nand_sync (struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;

	DEBUG (MTD_DEBUG_LEVEL3, "nand_sync: called\n");

	/* Grab the lock and see if the device is available */
	nand_get_device (chip, mtd, FL_SYNCING);
	/* Release it and go back */
	nand_release_device (mtd);
}


/**
 * nand_block_isbad - [MTD Interface] Check whether the block at the given offset is bad
 * @mtd:	MTD device structure
 * @ofs:	offset relative to mtd start
 */
static int nand_block_isbad (struct mtd_info *mtd, loff_t ofs)
{
	/* Check for invalid offset */
	if (ofs > mtd->size)
		return -EINVAL;

	return nand_block_checkbad (mtd, ofs, 1, 0);
}

/**
 * nand_block_markbad - [MTD Interface] Mark the block at the given offset as bad
 * @mtd:	MTD device structure
 * @ofs:	offset relative to mtd start
 */
static int nand_block_markbad (struct mtd_info *mtd, loff_t ofs)
{
	struct nand_chip *chip = mtd->priv;
	int ret;

	if ((ret = nand_block_isbad(mtd, ofs))) {
		/* If it was bad already, return success and do nothing. */
		if (ret > 0)
			return 0;
		return ret;
	}

	return chip->block_markbad(mtd, ofs);
}

/**
 * nand_scan - [NAND Interface] Scan for the NAND device
 * @mtd:	MTD device structure
 * @maxchips:	Number of chips to scan for
 *
 * This fills out all the not initialized function pointers
 * with the defaults.
 * The flash ID is read and the mtd/chip structures are
 * filled with the appropriate values. Buffers are allocated if
 * they are not provided by the board driver
 *
 */
int nand_scan (struct mtd_info *mtd, int maxchips)
{
	int i, j, nand_maf_id, nand_dev_id, busw;
	struct nand_chip *chip = mtd->priv;

	/* Get buswidth to select the correct functions*/
	busw = chip->options & NAND_BUSWIDTH_16;

	/* check for proper chip_delay setup, set 20us if not */
	if (!chip->chip_delay)
		chip->chip_delay = 20;

	/* check, if a user supplied command function given */
	if (chip->cmdfunc == NULL)
		chip->cmdfunc = nand_command;

	/* check, if a user supplied wait function given */
	if (chip->waitfunc == NULL)
		chip->waitfunc = nand_wait;

	if (!chip->select_chip)
		chip->select_chip = nand_select_chip;
	if (!chip->write_byte)
		chip->write_byte = busw ? nand_write_byte16 : nand_write_byte;
	if (!chip->read_byte)
		chip->read_byte = busw ? nand_read_byte16 : nand_read_byte;
	if (!chip->write_word)
		chip->write_word = nand_write_word;
	if (!chip->read_word)
		chip->read_word = nand_read_word;
	if (!chip->block_bad)
		chip->block_bad = nand_block_bad;
	if (!chip->block_markbad)
		chip->block_markbad = nand_default_block_markbad;
	if (!chip->write_buf)
		chip->write_buf = busw ? nand_write_buf16 : nand_write_buf;
	if (!chip->read_buf)
		chip->read_buf = busw ? nand_read_buf16 : nand_read_buf;
	if (!chip->verify_buf)
		chip->verify_buf = busw ? nand_verify_buf16 : nand_verify_buf;
	if (!chip->scan_bbt)
		chip->scan_bbt = nand_default_bbt;

	/* Select the device */
	chip->select_chip(mtd, 0);

	/* Send the command for reading device ID */
	chip->cmdfunc (mtd, NAND_CMD_READID, 0x00, -1);

	/* Read manufacturer and device IDs */
	nand_maf_id = chip->read_byte(mtd);
	nand_dev_id = chip->read_byte(mtd);

	/* Print and store flash device information */
	for (i = 0; nand_flash_ids[i].name != NULL; i++) {

		if (nand_dev_id != nand_flash_ids[i].id)
			continue;

		if (!mtd->name) mtd->name = nand_flash_ids[i].name;
		chip->chipsize = nand_flash_ids[i].chipsize << 20;

		/* New devices have all the information in additional id bytes */
		if (!nand_flash_ids[i].pagesize) {
			int extid;
			/* The 3rd id byte contains non relevant data ATM */
			extid = chip->read_byte(mtd);
			/* The 4th id byte is the important one */
			extid = chip->read_byte(mtd);
//			printf("extid = 0x%x\n", extid);		//HJ_del
			/* Calc pagesize */
			mtd->oobblock = 1024 << (extid & 0x3);
			extid >>= 2;
			/* Calc oobsize */
			mtd->oobsize = (8 << (extid & 0x03)) * (mtd->oobblock / 512);
			extid >>= 2;
			/* Calc blocksize. Blocksize is multiples of 64KiB */
			mtd->erasesize = (64 * 1024)  << (extid & 0x03);
			extid >>= 2;
			/* Get buswidth information */
			busw = (extid & 0x01) ? NAND_BUSWIDTH_16 : 0;
//			printf("busw = 0x%x\n", busw);		//HJ_del

		} else {
			/* Old devices have chip data hardcoded in the
			 * device id table */
			mtd->erasesize = nand_flash_ids[i].erasesize;
			mtd->oobblock = nand_flash_ids[i].pagesize;
			mtd->oobsize = mtd->oobblock / 32;
			busw = nand_flash_ids[i].options & NAND_BUSWIDTH_16;
		}

		/* Check, if buswidth is correct. Hardware drivers should set
		 * chip correct ! */
		if (busw != (chip->options & NAND_BUSWIDTH_16)) {
			printk (KERN_INFO "NAND device: Manufacturer ID:"
				" 0x%02x, Chip ID: 0x%02x (%s %s)\n", nand_maf_id, nand_dev_id,
				nand_manuf_ids[i].name , mtd->name);
			printk (KERN_WARNING
				"NAND bus width %d instead %d bit\n",
					(chip->options & NAND_BUSWIDTH_16) ? 16 : 8,
					busw ? 16 : 8);
			chip->select_chip(mtd, -1);
			return 1;
		}

		/* Calculate the address shift from the page size */
		chip->page_shift = ffs(mtd->oobblock) - 1;
		chip->bbt_erase_shift = chip->phys_erase_shift = ffs(mtd->erasesize) - 1;
		chip->chip_shift = ffs(chip->chipsize) - 1;

		/* Set the bad block position */
		chip->badblockpos = mtd->oobblock > 512 ?
			NAND_LARGE_BADBLOCK_POS : NAND_SMALL_BADBLOCK_POS;

		/* Get chip options, preserve non chip based options */
		chip->options &= ~NAND_CHIPOPTIONS_MSK;
		chip->options |= nand_flash_ids[i].options & NAND_CHIPOPTIONS_MSK;
		/* Set chip as a default. Board drivers can override it, if neccecary */
		chip->options |= NAND_NO_AUTOINCR;
		/* Check if chip is a not a samsung device. Do not clear the options
		 * for chips which are not having an extended id.
		 */
		if (((nand_maf_id != NAND_MFR_SAMSUNG) && (nand_maf_id != NAND_MFR_SPANSION)) && !nand_flash_ids[i].pagesize)
			chip->options &= ~NAND_SAMSUNG_LP_OPTIONS;

		/* Check for AND chips with 4 page planes */
		if (chip->options & NAND_4PAGE_ARRAY)
			chip->erase_cmd = multi_erase_cmd;
		else
			chip->erase_cmd = single_erase_cmd;

		/* Do not replace user supplied command function ! */
		if (mtd->oobblock > 512 && chip->cmdfunc == nand_command)
			chip->cmdfunc = nand_command_lp;

		/* Try to identify manufacturer */
		for (j = 0; nand_manuf_ids[j].id != 0x0; j++) {
			if (nand_manuf_ids[j].id == nand_maf_id)
				break;
		}
		break;
	}

	if (!nand_flash_ids[i].name) {
		printk (KERN_WARNING "No NAND device found!!!\n");
		chip->select_chip(mtd, -1);
		return 1;
	}

	for (i=1; i < maxchips; i++) {
		chip->select_chip(mtd, i);

		/* Send the command for reading device ID */
		chip->cmdfunc (mtd, NAND_CMD_READID, 0x00, -1);

		/* Read manufacturer and device IDs */
		if (nand_maf_id != chip->read_byte(mtd) ||
		    nand_dev_id != chip->read_byte(mtd))
			break;
	}
	if (i > 1)
		printk(KERN_INFO "%d NAND chips detected\n", i);

	/* Allocate buffers, if neccecary */
	if (!chip->oob_buf) {
		size_t len;
		len = mtd->oobsize << (chip->phys_erase_shift - chip->page_shift);
		chip->oob_buf = kmalloc (len, GFP_KERNEL);
		if (!chip->oob_buf) {
			printk (KERN_ERR "nand_scan(): Cannot allocate oob_buf\n");
			return -ENOMEM;
		}
		chip->options |= NAND_OOBBUF_ALLOC;
	}

	if (!chip->data_buf) {
		size_t len;
		len = mtd->oobblock + mtd->oobsize;
		chip->data_buf = kmalloc (len, GFP_KERNEL);
		if (!chip->data_buf) {
			if (chip->options & NAND_OOBBUF_ALLOC)
				kfree (chip->oob_buf);
			printk (KERN_ERR "nand_scan(): Cannot allocate data_buf\n");
			return -ENOMEM;
		}
		chip->options |= NAND_DATABUF_ALLOC;
	}

	/* Store the number of chips and calc total size for mtd */
	chip->numchips = i;
	mtd->size = i * chip->chipsize;
	/* Convert chipsize to number of pages per chip -1. */
	chip->pagemask = (chip->chipsize >> chip->page_shift) - 1;
	/* Preset the internal oob buffer */
	memset(chip->oob_buf, 0xff, mtd->oobsize << (chip->phys_erase_shift - chip->page_shift));

	/* If no default placement scheme is given, select an
	 * appropriate one */
	if (!chip->autooob) {
		/* Select the appropriate default oob placement scheme for
		 * placement agnostic filesystems */
		switch (mtd->oobsize) {
		case 8:
			chip->autooob = &nand_oob_8;
			break;
		case 16:
			chip->autooob = &nand_oob_16;
			break;
		case 64:
			chip->autooob = &nand_oob_64;
			break;
		default:
			printk (KERN_WARNING "No oob scheme defined for oobsize %d\n",
				mtd->oobsize);
/*			BUG(); */
		}
	}

	/* The number of bytes available for the filesystem to place fs dependend
	 * oob data */
	if (chip->options & NAND_BUSWIDTH_16) {
		mtd->oobavail = mtd->oobsize - (chip->autooob->eccbytes + 2);
		if (chip->autooob->eccbytes & 0x01)
			mtd->oobavail--;
	} else
		mtd->oobavail = mtd->oobsize - (chip->autooob->eccbytes + 1);

	/*
	 * check ECC mode, default to software
	 * if 3byte/512byte hardware ECC is selected and we have 256 byte pagesize
	 * fallback to software ECC
	*/
#ifdef CONFIG_TQ2440_NAND_HWECC
	mtd->eccsize = chip->eccsize;
	chip->eccsteps = mtd->oobblock / 512;
	chip->ecctotal = chip->eccsteps * chip->eccbytes;
#else
	chip->eccsize = 256;	/* set default eccsize */
	chip->eccbytes = 3;

	switch (chip->eccmode) {
	case NAND_ECC_HW12_2048:
		if (mtd->oobblock < 2048) {
			printk(KERN_WARNING "2048 byte HW ECC not possible on %d byte page size, fallback to SW ECC\n",
			       mtd->oobblock);
			chip->eccmode = NAND_ECC_SOFT;
			chip->calculate_ecc = nand_calculate_ecc;
			chip->correct_data = nand_correct_data;
		} else
			chip->eccsize = 2048;
		break;

	case NAND_ECC_HW3_512:
	case NAND_ECC_HW6_512:
	case NAND_ECC_HW8_512:
		if (mtd->oobblock == 256) {
			printk (KERN_WARNING "512 byte HW ECC not possible on 256 Byte pagesize, fallback to SW ECC \n");
			chip->eccmode = NAND_ECC_SOFT;
			chip->calculate_ecc = nand_calculate_ecc;
			chip->correct_data = nand_correct_data;
		} else
			chip->eccsize = 512; /* set eccsize to 512 */
		break;

	case NAND_ECC_HW3_256:
		break;

	case NAND_ECC_NONE:
		//printk (KERN_WARNING "NAND_ECC_NONE selected by board driver. chip is not recommended !!\n");
		chip->eccmode = NAND_ECC_NONE;
		break;

	case NAND_ECC_SOFT:
		chip->calculate_ecc = nand_calculate_ecc;
		chip->correct_data = nand_correct_data;
		break;

	default:
		printk (KERN_WARNING "Invalid NAND_ECC_MODE %d\n", chip->eccmode);
/*		BUG(); */
	}

	/* Check hardware ecc function availability and adjust number of ecc bytes per
	 * calculation step
	*/
	switch (chip->eccmode) {
	case NAND_ECC_HW12_2048:
		chip->eccbytes += 4;
	case NAND_ECC_HW8_512:
		chip->eccbytes += 2;
	case NAND_ECC_HW6_512:
		chip->eccbytes += 3;
	case NAND_ECC_HW3_512:
	case NAND_ECC_HW3_256:
		if (chip->calculate_ecc && chip->correct_data && chip->enable_hwecc)
			break;
		printk (KERN_WARNING "No ECC functions supplied, Hardware ECC not possible\n");
/*		BUG();	*/
	}

	mtd->eccsize = chip->eccsize;

	/* Set the number of read / write steps for one page to ensure ECC generation */
	switch (chip->eccmode) {
	case NAND_ECC_HW12_2048:
		chip->eccsteps = mtd->oobblock / 2048;
		break;
	case NAND_ECC_HW3_512:
	case NAND_ECC_HW6_512:
	case NAND_ECC_HW8_512:
		chip->eccsteps = mtd->oobblock / 512;
		break;
	case NAND_ECC_HW3_256:
	case NAND_ECC_SOFT:
		chip->eccsteps = mtd->oobblock / 256;
		break;

	case NAND_ECC_NONE:
		chip->eccsteps = 1;
		break;
	}
#endif /* CONFIG_TQ2440_NAND_HWECC */

/* XXX U-BOOT XXX */
#if 0
	/* Initialize state, waitqueue and spinlock */
	chip->state = FL_READY;
	init_waitqueue_head (&chip->wq);
	spin_lock_init (&chip->chip_lock);
#endif

	/* De-select the device */
	chip->select_chip(mtd, -1);

	/* Invalidate the pagebuffer reference */
	chip->pagebuf = -1;

	/* Fill in remaining MTD driver data */
	mtd->type = MTD_NANDFLASH;
	mtd->flags = MTD_CAP_NANDFLASH | MTD_ECC;
	mtd->ecctype = MTD_ECC_SW;
	mtd->erase = nand_erase;
	mtd->point = NULL;
	mtd->unpoint = NULL;
	if(!mtd->read)
		mtd->read = nand_read;
	if(!mtd->write)
		mtd->write = nand_write;
	if(!mtd->read_ecc)
		mtd->read_ecc = nand_read_ecc;
	if(!mtd->write_ecc)
		mtd->write_ecc = nand_write_ecc;
	if(!mtd->read_oob)
		mtd->read_oob = nand_read_oob;
	if(!mtd->write_oob)
		mtd->write_oob = nand_write_oob;
/* XXX U-BOOT XXX */
#if 0
	mtd->readv = NULL;
	mtd->writev = nand_writev;
	mtd->writev_ecc = nand_writev_ecc;
#endif
	mtd->sync = nand_sync;
/* XXX U-BOOT XXX */
#if 0
	mtd->lock = NULL;
	mtd->unlock = NULL;
	mtd->suspend = NULL;
	mtd->resume = NULL;
#endif
	mtd->block_isbad = nand_block_isbad;
	mtd->block_markbad = nand_block_markbad;

	/* and make the autooob the default one */
	memcpy(&mtd->oobinfo, chip->autooob, sizeof(mtd->oobinfo));
/* XXX U-BOOT XXX */
#if 0
	mtd->owner = THIS_MODULE;
#endif
//	printf("chip->options = 0x%x\n", chip->options);			//HJ_del
	/* Build bad block table */
	return 0; //chip->scan_bbt (mtd); // cancelled by www.embedsky.net, scan_bbt uses more time
}

/**
 * nand_release - [NAND Interface] Free resources held by the NAND device
 * @mtd:	MTD device structure
 */
void nand_release (struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;

#ifdef CONFIG_MTD_PARTITIONS
	/* Deregister partitions */
	del_mtd_partitions (mtd);
#endif
	/* Deregister the device */
/* XXX U-BOOT XXX */
#if 0
	del_mtd_device (mtd);
#endif
	/* Free bad block table memory, if allocated */
	if (chip->bbt)
		kfree (chip->bbt);
	/* Buffer allocated by nand_scan ? */
	if (chip->options & NAND_OOBBUF_ALLOC)
		kfree (chip->oob_buf);
	/* Buffer allocated by nand_scan ? */
	if (chip->options & NAND_DATABUF_ALLOC)
		kfree (chip->data_buf);
}

#endif
