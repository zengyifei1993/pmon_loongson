
/*
 * Copyright (C) 2008 Freescale Semiconductor, Inc.
 *		Dave Liu <daveliu@freescale.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */
#include "bpfilter.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/syslog.h>

#include <ctype.h>

#if defined(__NetBSD__) || defined(__OpenBSD__)

#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/device.h>

#include <vm/vm.h>

#include <machine/cpu.h>
#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/if_fxpreg.h>

#else /* __FreeBSD__ */

#include <sys/sockio.h>

#include <vm/vm.h>		/* for vtophys */
#include <vm/vm_param.h>	/* for vtophys */
#include <vm/pmap.h>		/* for vtophys */
#include <machine/clock.h>	/* for DELAY */

#include <pci/pcivar.h>

#endif /* __NetBSD__ || __OpenBSD__ */

#if NGZIP > 0
#include <gzipfs.h>
#endif /* NGZIP */

#include <machine/intr.h>
#include <machine/bus.h>

#include <dev/ata/atareg.h>
#include <dev/ic/wdcreg.h>

#include <linux/libata.h>
#include <fis.h>
#include <part.h>
#include <pmon.h>

#include "ahcisata.h"
#include "ahci.h"
#include "ata.h"

#ifdef INIT_TIME
#define PRINTD(...)	  
#else
#define PRINTD(...)	printf(__VA_ARGS__)
#endif

#ifdef AHCI_DEBUG
#define ahci_debug(fmt, args...) printf("%s<%d>: "fmt, __func__,	\
		__LINE__, ##args)
#else
#define ahci_debug(fmt, arg...)
#endif

#undef PHYSADDR
#ifndef PHYSADDR
#if defined(LOONGSON_2G1A) || defined(LOONGSON_2F1A)
#define PHYSADDR(x) ((long)(x))//1a dma access address
#else
#define PHYSADDR(x) (((long)(x))&0x1fffffff)
#endif
#endif

#define cpu_to_le32(x) (x)
#define CFG_SATA_MAX_DEVICE 32
block_dev_desc_t *sata_dev_desc[CFG_SATA_MAX_DEVICE];

static int curr_port = -1;
static int fault_timeout;
int sata_using_flag = 0;

static void ahci_set_feature(struct ahci_sata_softc *sc, u8 * sataid);
static int ahci_port_start(struct ahci_sata_softc *sc);
static int ata_scsiop_inquiry(struct ahci_sata_softc *sc);
static void ahci_fill_cmd_slot(struct ahci_ioports *pp, u32 opts);

extern unsigned long strtoul(char *nptr, char **endptr, int base);
extern int strcmp(char *s1, char *s2);

ulong ahci_sata_read(struct ahci_sata_softc *sc, unsigned long blknr, lbaint_t blkcnt, void *buffer);
ulong ahci_sata_write(struct ahci_sata_softc *sc, unsigned long blknr, lbaint_t blkcnt, void *buffer);

#if defined(LOONGSON_2F1A)
static __inline__ int __ilog2_2f(unsigned int x) 
{
	int i, lz;

	for (i = 0; x; i++)
		x = x/2;
	/* 2F instruction set don't include the clz
	 * So replace clz instruction with C code
	 */
	lz = 31 - (--i);

	return 31 - lz;
}
#endif

static __inline__ int __ilog2(unsigned int x)
{
	int lz;

	asm volatile (".set\tnoreorder\n\t"
		      ".set\tnoat\n\t"
		      ".set\tmips32\n\t"
		      "clz\t%0,%1\n\t"
		      ".set\tmips0\n\t"
		      ".set\tat\n\t"
		      ".set\treorder"
		      :"=r" (lz)
		      :"r"(x));

	return 31 - lz;
}

static void ahci_start_engine(struct ahci_sata_softc *sc)
{
	int port = sc->port_no;
	struct ahci_probe_ent *probe_ent = sc->probe_ent;
	struct ahci_ioports *pp = &(probe_ent->port[port]);
	volatile u8 *port_mmio = (volatile u8 *)pp->port_mmio;
	u32 tmp;

	/* start DMA */
	tmp = readl(port_mmio + PORT_CMD);
	tmp |= PORT_CMD_START;
	writel(tmp, port_mmio + PORT_CMD);
	readl(port_mmio + PORT_CMD);	/* flush */
}

/**
 *	ata_wait_register - wait until register value changes
 *	@reg: IO-mapped register
 *	@mask: Mask to apply to read register value
 *	@val: Wait condition
 *	@interval: polling interval in milliseconds
 *	@timeout: timeout in milliseconds
 *
 *	Waiting for some bits of register to change is a common
 *	operation for ATA controllers.  This function reads 32bit LE
 *	IO-mapped register @reg and tests for the following condition.
 *
 *	(*@reg & mask) != val
 *
 *	If the condition is met, it returns; otherwise, the process is
 *	repeated after @interval_msec until timeout.
 *
 *	RETURNS:
 *	The final register value.
 */
u32 ata_wait_register(void *reg, u32 mask, u32 val,
		      unsigned long interval, unsigned long timeout_msec)
{
	u32 tmp, i;

	tmp = readl(reg);

	for (i = 0; ((val = readl(reg)) & mask) && i < timeout_msec;
	     i += interval) {
		msleep(interval);
	}

	tmp = readl(reg);
	return tmp;
}

static int ahci_stop_engine(struct ahci_sata_softc *sc)
{
	int port = sc->port_no;
	struct ahci_probe_ent *probe_ent = sc->probe_ent;
	struct ahci_ioports *pp = &(probe_ent->port[port]);
	volatile u8 *port_mmio = (volatile u8 *)pp->port_mmio;
	u32 tmp;

	tmp = readl(port_mmio + PORT_CMD);

	/* check if the HBA is idle */
	if ((tmp & (PORT_CMD_START | PORT_CMD_LIST_ON)) == 0)
		return 0;

	/* setting HBA to idle */
	tmp &= ~PORT_CMD_START;
	writel_with_flush(tmp, port_mmio + PORT_CMD);

	tmp = ata_wait_register((void *)(port_mmio + PORT_CMD),
				PORT_CMD_LIST_ON, PORT_CMD_LIST_ON, 1, 500);

	if (tmp & PORT_CMD_LIST_ON) {
		PRINTD("Cannot stop engine\n");
		return -EIO;
	}

	return 0;
}

int ahci_kick_engine(struct ahci_sata_softc *sc, int force_restart)
{
	int port = sc->port_no;
	struct ahci_probe_ent *probe_ent = sc->probe_ent;
	struct ahci_ioports *pp = &(probe_ent->port[port]);
	volatile u8 *port_mmio = (volatile u8 *)pp->port_mmio;
	u8 status = readl(port_mmio + PORT_TFDATA) & 0xFF;
	u32 tmp;
	int busy, rc;

	/* do we need to kick the port? */
	busy = status & (ATA_BUSY | ATA_DRQ);
	if (!busy && !force_restart)
		return 0;

	/* stop engine */
	rc = ahci_stop_engine(sc);
	if (rc)
		goto out_restart;

	busy = status & (ATA_BUSY | ATA_DRQ);
	if (!busy) {
		rc = 0;
		goto out_restart;
	}

	if (!(probe_ent->cap & HOST_CAP_CLO)) {
		rc = -EOPNOTSUPP;
		goto out_restart;
	}

	/* perform CLO */
	tmp = readl(port_mmio + PORT_CMD);
	tmp |= PORT_CMD_CLO;
	writel_with_flush(tmp, port_mmio + PORT_CMD);

	rc = 0;
	tmp = ata_wait_register((void *)(port_mmio + PORT_CMD),
				PORT_CMD_CLO, PORT_CMD_CLO, 1, 500);
	if (tmp & PORT_CMD_CLO) {
		PRINTD("Cannot set PORT_CMD_CLO\n");
		rc = -EIO;
	}

	/* restart engine */
out_restart:
	ahci_start_engine(sc);
	return rc;
}

static void ata_reset_fis_init(u8 pmp, int is_cmd, u8 * fis, int set)
{
	fis[0] = 0x27;
	fis[1] = pmp & 0xf;
	if (is_cmd)
		fis[1] |= (1 << 7);

	fis[2] = 0;
	fis[3] = 0;
	fis[4] = 0;
	fis[5] = 0;
	fis[6] = 0;
	fis[7] = 0;
	fis[8] = 0;
	fis[9] = 0;
	fis[10] = 0;
	fis[11] = 0;
	fis[12] = 0;
	fis[13] = 0;
	fis[14] = 0;
	if (set == 1)
		fis[15] |= ATA_SRST;
	else
		fis[15] &= ~ATA_SRST;
	fis[16] = 0;
	fis[17] = 0;
	fis[18] = 0;
	fis[19] = 0;
}

static int ahci_exec_polled_cmd(struct ahci_sata_softc *sc, int pmp, int is_cmd, u16 flags,
				unsigned long timeout_msec, int set)
{
	int port = sc->port_no;
	struct ahci_probe_ent *probe_ent = sc->probe_ent;
	const u32 cmd_fis_len = 5;	/* five dwords */
	struct ahci_ioports *pp = &(probe_ent->port[port]);
	volatile u8 *port_mmio = (volatile u8 *)pp->port_mmio;
	u32 tmp;
	u8 fis[20];

	ata_reset_fis_init(pmp, is_cmd, fis, set);
	memcpy((unsigned char *)pp->cmd_tbl, fis, 20);
	ahci_fill_cmd_slot(pp, cmd_fis_len | flags | (pmp << 12));

#if defined(LOONGSON_2G1A) || defined(LOONGSON_2F1A)
	CPU_IOFlushDCache(pp->cmd_slot, 32, SYNC_W);	/*32~256 */
	CPU_IOFlushDCache(pp->cmd_tbl, 0x60, SYNC_W);
	CPU_IOFlushDCache(pp->rx_fis, AHCI_RX_FIS_SZ, SYNC_R);
#endif
	/*issue & wait */
	writel_with_flush(1, port_mmio + PORT_CMD_ISSUE);

	if (timeout_msec) {
		tmp = ata_wait_register((void *)(port_mmio + PORT_CMD_ISSUE),
					0x1, 0x1, 1, timeout_msec);
		if (tmp & 0x1) {
			ahci_kick_engine(sc, 1);
			PRINTD("softereset issue failed\n");
			return -EBUSY;
		}
	} else {
		readl(port_mmio + PORT_CMD_ISSUE);
	}

	return 0;
}

static int ahci_check_ready(struct ahci_sata_softc *sc)
{
	int port = sc->port_no;
	struct ahci_probe_ent *probe_ent = sc->probe_ent;
	struct ahci_ioports *pp = &(probe_ent->port[port]);
	volatile u8 *port_mmio = (volatile u8 *)pp->port_mmio;
	u8 status = readl(port_mmio + PORT_TFDATA) & 0xFF;

	if (!(status & ATA_BUSY))
		return 1;

	/* 0xff indicates either no device or device not ready */
	if (status == 0xff)
		return -ENODEV;

	return 0;
}

int ahci_do_softreset(struct ahci_sata_softc *sc, int pmp, unsigned long deadline)
{
	int port = sc->port_no;
	struct ahci_probe_ent *probe_ent = sc->probe_ent;
	const char *reason = NULL;
	unsigned long msecs = 0x200;
	struct ahci_ioports *pp = &(probe_ent->port[port]);
	volatile u8 *port_mmio = (volatile u8 *)pp->port_mmio;
	int rc;

	/* prepare for SRST (AHCI-1.1 10.4.1) */

	rc = ahci_kick_engine(sc, 1);
	if (rc && rc != -EOPNOTSUPP)
		PRINTD("failed to reset engine (errno=%d)\n", rc);

	/* issue the first H2D Register FIS */
	if (ahci_exec_polled_cmd(sc, pmp, 0, AHCI_CMD_RESET
				 | AHCI_CMD_CLR_BUSY, msecs, 1)) {
		PRINTD("1st FIS failed\n");
		rc = -EIO;
		reason = "1st FIS failed";
		goto fail;
	}

	/* spec says at least 5us, but be generous and sleep for 1ms */
	msleep(1);

	/* issue the second H2D Register FIS */
	ahci_exec_polled_cmd(sc, pmp, 0, 0, msecs, 0);

	rc = 0;
	while (msecs-- && !rc) {
		rc = ahci_check_ready(sc);
	}

	/* link occupied, -ENODEV too is an error */
	if (rc == -ENODEV) {
		reason = "no device";
		goto fail;
	}
	if (rc == 0) {
		reason = "not ready";
		goto fail;
	}

	return 0;

fail:
	PRINTD("softreset failed (%s)\n", reason);
	return 1;
}

struct ahci_sata_softc *ahci_find_byname(struct cfdriver cd, u8 *device_name)
{
	int len, unite;
	len = strlen(device_name);
	unite = device_name[len - 1] - '0';

	return ahci_sata_lookup(cd, unite);
}

void read_atap_model(char *name, struct ataparams *sc_params)
{
	u8 buf[41];
	u8 *s, *p;
	u8 c;
	int i, blank = 0;

	if (!name || !sc_params)
		return;

	for (s = sc_params->atap_model, i = 0, p = buf;
			i < sizeof(sc_params->atap_model); i++) {
		c = *s++;
		*(p + 1) = c;

		c = *s++;
		*p = c;

		p += 2;
		i++;
	}

	for (i = 0; i < 41; i++) {
		if (buf[i] == ' ') {
			if (blank) {
				buf[i - 1] = '\0';
				break;
			} else {
				blank = 1;
			}
		} else {
			blank = 0;
		}
	}

	strcpy(name, buf);
}

int ahci_sata_initialize(u32 reg, u32 port_no, struct ahci_sata_softc *sf)
{
	int rc;
	static int i;
	u8 *diskid;
	ahci_sata_t *sata;
	struct ahci_ioports *pp = &(sf->probe_ent->port[port_no]);
	volatile u8 *port_mmio = (volatile u8 *)pp->port_mmio;
	struct block_dev_desc *bd;

	if (i < 0 || i > (CFG_SATA_MAX_DEVICE - 1)) {
		PRINTD("The sata index %d is out of ranges\n", i);
		return -1;
	}
	sata_dev_desc[i] = bd = calloc(1, sizeof(struct block_dev_desc));
	sf->bd = bd;
	bd->priv = sf;
	bd->if_type = IF_TYPE_SATA;
	bd->dev = i;
	bd->part_type = PART_TYPE_UNKNOWN;

	if (pp->is_atapi) {
		bd->type = DEV_TYPE_CDROM;
		bd->blksz = 2048;	/* CD-ROM block size is 2048 */
	} else {
		bd->type = DEV_TYPE_HARDDISK;
		bd->blksz = ATA_SECT_SIZE;
	}

	bd->lba = 0;
	bd->block_read = ahci_sata_read;
	bd->block_write = ahci_sata_write;

#if 0
	sata = (ahci_sata_t *) malloc(sizeof(ahci_sata_t), M_DEVBUF, M_NOWAIT);
	if (!sata) {
		PRINTD("alloc the sata device struct failed\n");
		return -1;
	}
	memset((void *)sata, 0, sizeof(ahci_sata_t));
	bd->priv = (void *)sata;
	sprintf(sata->name, "SATA%d", i);

	sata->reg_base = reg;
#endif

	rc = ahci_port_start(sf);
	diskid = ata_scsiop_inquiry(sf);
	sf->sc_params = (struct ataparams *)diskid;

	read_atap_model(sf->name, sf->sc_params);

	if (diskid)
		ahci_set_feature(sf, diskid);

	curr_port = i;
	i++;
	return rc;
}

static int waiting_for_cmd_completed(volatile u8 * offset,
				     int timeout_msec, u32 sign)
{
	int i;
	u32 status;

	for (i = 0; ((status = readl(offset)) & sign) && i < timeout_msec; i++)
		;

	return (i < timeout_msec) ? 0 : -1;
}

#define MAX_DATA_BYTE_COUNT  (4*1024*1024)

static int ahci_fill_sg(struct ahci_sata_softc *sc, unsigned char *buf, int buf_len)
{
	int port = sc->port_no;
	struct ahci_probe_ent *probe_ent = sc->probe_ent;
	struct ahci_ioports *pp = &(probe_ent->port[port]);
	struct ahci_sg *ahci_sg = pp->cmd_tbl_sg;
	u32 sg_count;
	int i;

	sg_count = ((buf_len - 1) / MAX_DATA_BYTE_COUNT) + 1;
	if (sg_count > AHCI_MAX_SG) {
		PRINTD("Error:Too much sg!\n");
		return -1;
	}

	for (i = 0; i < sg_count; i++) {
		ahci_sg->addr =
		    cpu_to_le32(PHYSADDR((u32) buf + i * MAX_DATA_BYTE_COUNT));
		ahci_sg->addr_hi = 0;
		ahci_sg->flags_size = cpu_to_le32(0x3fffff &
						  (buf_len < MAX_DATA_BYTE_COUNT
						   ? (buf_len - 1)
						   : (MAX_DATA_BYTE_COUNT -
						      1)));
		ahci_sg++;
		buf_len -= MAX_DATA_BYTE_COUNT;
	}

	return sg_count;
}

static void ahci_fill_cmd_slot(struct ahci_ioports *pp, u32 opts)
{
	pp->cmd_slot->opts = cpu_to_le32(opts);
	pp->cmd_slot->status = 0;
	pp->cmd_slot->tbl_addr = cpu_to_le32(PHYSADDR(pp->cmd_tbl
						      & 0xffffffff));
	pp->cmd_slot->tbl_addr_hi = 0;
}

static void ahci_set_feature(struct ahci_sata_softc *sc, u8 * sataid)
{
	int port = sc->port_no;
	struct ahci_probe_ent *probe_ent = sc->probe_ent;
	struct ahci_ioports *pp = &(probe_ent->port[port]);
	volatile u8 *port_mmio = (volatile u8 *)pp->port_mmio;
	u32 dma_cap = ((hd_driveid_t *) sataid)->dma_ultra & 0xff;
	u32 cmd_fis_len = 5;	/* five dwords */
	u8 fis[20];

	/*set feature */
	memset(fis, 0, 20);
	fis[0] = 0x27;
	fis[1] = 1 << 7;
	fis[2] = ATA_CMD_SETF;
	fis[3] = SETFEATURES_XFER;
#if defined(LOONGSON_2F1A)
	fis[12] = __ilog2_2f(probe_ent->udma_mask + 1) + 0x40 - 0x01;
#else
	fis[12] = __ilog2(probe_ent->udma_mask + 1) + 0x40 - 0x01;
#endif

	if (dma_cap == ATA_UDMA6)
		fis[12] = XFER_UDMA_6;
	if (dma_cap == ATA_UDMA5)
		fis[12] = XFER_UDMA_5;
	if (dma_cap == ATA_UDMA4)
		fis[12] = XFER_UDMA_4;
	if (dma_cap == ATA_UDMA3)
		fis[12] = XFER_UDMA_3;

	ahci_debug("fis[12] = %d\n", fis[12]);

	memcpy((unsigned char *)pp->cmd_tbl, fis, 20);
	ahci_fill_cmd_slot(pp, cmd_fis_len);

#if defined(LOONGSON_2G1A) || defined(LOONGSON_2F1A)
	CPU_IOFlushDCache(pp->cmd_slot, 32, SYNC_W);	/*32~256 */
	CPU_IOFlushDCache(pp->cmd_tbl, 0x60, SYNC_W);
	CPU_IOFlushDCache(pp->rx_fis, AHCI_RX_FIS_SZ, SYNC_R);
#endif

	writel_with_flush(1, port_mmio + PORT_CMD_ISSUE);
	readl(port_mmio + PORT_CMD_ISSUE);

	if (waiting_for_cmd_completed(port_mmio + PORT_CMD_ISSUE, 300, 0x1)) {
		PRINTD("set feature error!\n");
	}
}

static int ahci_port_start(struct ahci_sata_softc *sc)
{
	int port = sc->port_no;
	struct ahci_probe_ent *probe_ent = sc->probe_ent;
	struct ahci_ioports *pp = &(probe_ent->port[port]);
	volatile u8 *port_mmio = (volatile u8 *)pp->port_mmio;
	u32 port_status;
	u32 port_cmd;
	u32 mem;

	port_status = readl(port_mmio + PORT_SCR_STAT);
	/*ahci_debug*/PRINTD("Port %d, mmio:0x%x status: %x\n", port, port_mmio, port_status);
	if ((port_status & 0xf) != 0x03) {
		PRINTD("No Link on this port!\n");
		return -1;
	}

	port_cmd = readl(port_mmio + PORT_CMD) & ~PORT_CMD_ICC_MASK;
	/* wake up link */
	writel(port_cmd | PORT_CMD_ICC_ACTIVE, port_mmio + PORT_CMD);

	mem = (u32) malloc(AHCI_PORT_PRIV_DMA_SZ + 2048, M_DEVBUF, M_NOWAIT);
	if (!mem) {
		free(pp, M_DEVBUF);
		PRINTD("No mem for table!\n");
		return -ENOMEM;
	}

	mem = (mem + 0x800) & (~0x7ff);	/* Aligned to 2048-bytes */
	memset((u8 *) mem, 0, AHCI_PORT_PRIV_DMA_SZ);

	/*
	 * First item in chunk of DMA memory: 32-slot command table,
	 * 32 bytes each in size
	 */
	pp->cmd_slot = (struct ahci_cmd_hdr *)mem;
	PRINTD("cmd_slot = 0x%x\n", pp->cmd_slot);
	mem += (AHCI_CMD_SLOT_SZ + 224);

	/*
	 * Second item: Received-FIS area
	 */
	pp->rx_fis = mem;
	mem += AHCI_RX_FIS_SZ;

	/*
	 * Third item: data area for storing a single command
	 * and its scatter-gather table
	 */
	pp->cmd_tbl = mem;
	PRINTD("cmd_tbl_dma = 0x%x\n", pp->cmd_tbl);

	mem += AHCI_CMD_TBL_HDR;
	pp->cmd_tbl_sg = (struct ahci_sg *)mem;

	writel_with_flush(0,
			  port_mmio + PORT_LST_ADDR_HI);

	writel_with_flush(PHYSADDR((u32) pp->cmd_slot),
			  port_mmio + PORT_LST_ADDR);

	if((readl(port_mmio + PORT_CMD) & (PORT_CMD_FIS_ON | PORT_CMD_FIS_RX)) != 0){
        PRINTD("AHCI SATA error: FIS address is set when FR and FRE is not zero!\n");
    };
	writel_with_flush(PHYSADDR(pp->rx_fis), port_mmio + PORT_FIS_ADDR);

    //check precondition
	if((readl(port_mmio + PORT_CMD) & PORT_CMD_LIST_ON) != 0){
        PRINTD("AHCI SATA error: START is set when CR is not zero!\n");
    };

	PRINTD("cxk debug\n");
	writel_with_flush(PORT_CMD_ICC_ACTIVE | PORT_CMD_POWER_ON |
            PORT_CMD_SPIN_UP, port_mmio + PORT_CMD);
    port_cmd = readl(port_mmio + PORT_CMD);
	writel_with_flush(port_cmd | PORT_CMD_FIS_RX, port_mmio + PORT_CMD);

    //start port
    port_cmd = readl(port_mmio + PORT_CMD);
	writel_with_flush(port_cmd | PORT_CMD_START, port_mmio + PORT_CMD);

	PRINTD("Exit start port %d\n", port);

	return 0;
}

static int get_ahci_device_data(struct ahci_sata_softc *sc, u8 * fis, int fis_len, u8 * buf,
				int buf_len, u8 * cdb, int is_write)
{
	int port = sc->port_no;
	struct ahci_probe_ent *probe_ent = sc->probe_ent;
	struct ahci_ioports *pp = &(probe_ent->port[port]);
	volatile u8 *port_mmio = (volatile u8 *)pp->port_mmio;
	u32 opts;
	u32 port_status;
	int sg_count;

	if (port > probe_ent->n_ports) {
		PRINTD("Invaild port number %d\n", port);
		return -1;
	}

	port_status = readl(port_mmio + PORT_SCR_STAT);
	if ((port_status & 0xf) != 0x03) {
		PRINTD("No Link on port %d!\n", port);
		return -1;
	}

	memcpy((unsigned char *)pp->cmd_tbl, fis, fis_len);

	if (cdb != NULL) {
		memset((unsigned char *)pp->cmd_tbl + AHCI_CMD_TBL_CDB, 0, 32);
		memcpy((unsigned char *)pp->cmd_tbl + AHCI_CMD_TBL_CDB, cdb,
		       ATAPI_COMMAND_LEN);
	}

	sg_count = ahci_fill_sg(sc, buf, buf_len);
	opts = (fis_len >> 2) | (sg_count << 16);

	if (cdb != NULL) {
		opts |= AHCI_CMD_ATAPI | AHCI_CMD_PREFETCH;
	}
	ahci_fill_cmd_slot(pp, opts);

#if defined(LOONGSON_2G1A) || defined(LOONGSON_2F1A)
	CPU_IOFlushDCache(pp->cmd_slot, 32, SYNC_W);	/*32~256 */
	CPU_IOFlushDCache(pp->cmd_tbl, 0x60, SYNC_W);
	CPU_IOFlushDCache(pp->cmd_tbl_sg, sg_count * 16, SYNC_W);
	CPU_IOFlushDCache(pp->rx_fis, AHCI_RX_FIS_SZ, SYNC_R);
	CPU_IOFlushDCache(buf, buf_len, is_write ? SYNC_W : SYNC_R);
#endif

	/* [31:0]CI
	 * This field is bit significant. Each bit corresponds to a command
	 * slot, where bit 0 corresponds to command slot 0. This field is 
	 * set by the software to indicate to the Port that a command has
	 * been built in system memory for a command slot and may be sent
	 * to the device.
	 */
    //check precondition
	if((readl(port_mmio + PORT_CMD) & PORT_CMD_START) == 0){
        PRINTD("AHCI SATA error: CI is set when START is zero!\n");
    };
	writel_with_flush(1, port_mmio + PORT_CMD_ISSUE);
	if (waiting_for_cmd_completed(port_mmio + PORT_CMD_ISSUE, 2000000, 0x1)) {
		PRINTD("%s <line%d>: timeout exit! %d bytes transferred.\n", __func__, __LINE__,
		       pp->cmd_slot->status);

        PRINTD("PxIS: 0x%08x, PxSERR: 0x%08x\n", readl(port_mmio + PORT_IRQ_STAT), readl(port_mmio + PORT_SCR_ERR));
        PRINTD("PxTFD: 0x%08x, PxSSTS: 0x%08x\n", readl(port_mmio + PORT_TFDATA), readl(port_mmio + PORT_SCR_STAT));

	    if (waiting_for_cmd_completed(port_mmio + PORT_CMD_ISSUE, 2000000, 0x1)) {
            PRINTD("Waiting another 2s is useless.\n");
        }else{
            PRINTD("Waiting another 2s is usefull.\n");
        }

		return -1;
	}

	ahci_debug("%d byte transferred.\n", pp->cmd_slot->status);

	/* Indicates the current byte count that has transferred on device
	 * writes(system memory to device) or 
	 * device reads(device to system memory).
	 */
	return pp->cmd_slot->status;
}

static void dump_ataid(hd_driveid_t * ataid)
{
	int i;
	unsigned short *id = (unsigned short *)ataid;

	for (i = 0; i < 128; i++) {
		if (!(i % 8))
			PRINTD("\n%03d:", i);
		PRINTD(" %04x", id[i]);
	}
	PRINTD("\n");

	PRINTD("(00)ataid->config = 0x%x\n", ataid->config);
	PRINTD("(01)ataid->cyls = 0x%x\n", ataid->cyls);
	PRINTD("(02)ataid->reserved2 = 0x%x\n", ataid->reserved2);
	PRINTD("(49)ataid->capability = 0x%x\n", ataid->capability);
	PRINTD("(53)ataid->field_valid =0x%x\n", ataid->field_valid);
	PRINTD("(63)ataid->dma_mword = 0x%x\n", ataid->dma_mword);
	PRINTD("(64)ataid->eide_pio_modes = 0x%x\n", ataid->eide_pio_modes);
	PRINTD("(75)ataid->queue_depth = 0x%x\n", ataid->queue_depth);
	PRINTD("(80)ataid->major_rev_num = 0x%x\n", ataid->major_rev_num);
	PRINTD("(81)ataid->minor_rev_num = 0x%x\n", ataid->minor_rev_num);
	PRINTD("(82)ataid->command_set_1 = 0x%x\n", ataid->command_set_1);
	PRINTD("(83)ataid->command_set_2 = 0x%x\n", ataid->command_set_2);
	PRINTD("(84)ataid->cfsse = 0x%x\n", ataid->cfsse);
	PRINTD("(85)ataid->cfs_enable_1 = 0x%x\n", ataid->cfs_enable_1);
	PRINTD("(86)ataid->cfs_enable_2 = 0x%x\n", ataid->cfs_enable_2);
	PRINTD("(87)ataid->csf_default = 0x%x\n", ataid->csf_default);
	PRINTD("(88)ataid->dma_ultra = 0x%x\n", ataid->dma_ultra);
	PRINTD("(93)ataid->hw_config = 0x%x\n", ataid->hw_config);
}

static u64 sata_id_n_sectors(u16 * id)
{
	if (ata_id_has_lba(id)) {
		if (ata_id_has_lba48(id))
			return ata_id_u64(id, ATA_ID_LBA48_SECTORS);
		else
			return ata_id_u32(id, ATA_ID_LBA_SECTORS);
	} else {
		return 0;
	}
}

/*
 * SCSI INQUIRY command operation.
 */
static int ata_scsiop_inquiry(struct ahci_sata_softc *sc)
{
	int port = sc->port_no;
	struct ahci_probe_ent *probe_ent = sc->probe_ent;
	u8 fis[20];
	u8 *tmpid;
	int cmd, rc;
	struct ahci_ioports *pp = &(probe_ent->port[port]);
	volatile u8 *port_mmio = (volatile u8 *)pp->port_mmio;

	memset(fis, 0, 20);
	/* Construct the FIS */
	fis[0] = 0x27;		/* Host to device FIS. */
	fis[1] = 1 << 7;	/* Command FIS. */
	if (pp->is_atapi) {
		fis[2] = ATAPI_CMD_IDENT;	/* Command byte. */
		cmd = readl(port_mmio + PORT_CMD);
		cmd |= PORT_CMD_ATAPI;
		writel_with_flush(cmd, port_mmio + PORT_CMD);
	} else {
		fis[2] = ATA_CMD_IDENT;	/* Command byte. */
	}

	/* Read id from sata */
	if (!(tmpid = malloc(sizeof(hd_driveid_t), M_DEVBUF, M_NOWAIT))) {
		PRINTD("malloc in ata_scsiop_inquiry failed.\n");
		return NULL;
	}

	memset(tmpid, 0, sizeof(hd_driveid_t));

	rc = get_ahci_device_data(sc, (u8 *) & fis, 20,
				  tmpid, sizeof(hd_driveid_t), NULL, 0);
	if (rc == -1) {
		PRINTD("scsi_ahci: SCSI inquiry command failure.\n");
		return NULL;
	}

	sc->bd->lba = sata_id_n_sectors((u16 *) tmpid);
	if (ata_id_has_lba48((u16 *) tmpid)) {
		sc->bd->lba48 = 1;
	}
#ifdef AHCI_DEBUG
	dump_ataid((hd_driveid_t *) tmpid);
#endif
	return tmpid;
}

static void make_read_command_packet(u8 * pc, int sector, int nframes)
{
	pc[0] = GPCMD_READ_10;
	pc[2] = (sector >> 24) & 0xff;	/* Block start address */
	pc[3] = (sector >> 16) & 0xff;
	pc[4] = (sector >> 8) & 0xff;
	pc[5] = (sector >> 0) & 0xff;
	pc[7] = (nframes >> 8) & 0xff;	/* Blocks */
	pc[8] = nframes & 0xff;
}

static u32 ahci_sata_rw_cmd_ext(struct ahci_sata_softc *sc, u32 start, u32 blkcnt, u8 * buffer,
				int is_write)
{
	int port_no = sc->port_no;
	struct ahci_probe_ent *probe_ent = sc->probe_ent;
	struct ahci_ioports *pp = &(probe_ent->port[port_no]);
	struct sata_fis_h2d h2d;
	struct cfis *cfis;
	u64 block;
	u8 *pc;

	block = (u64) start;
	cfis = (struct cfis *)&h2d;

	memset((void *)cfis, 0, sizeof(struct cfis));

	cfis->fis_type = SATA_FIS_TYPE_REGISTER_H2D;
	((u8 *) cfis)[1] = 0x80;	/* is command */

	cfis->command = (is_write) ? ATA_CMD_WRITE_EXT : ATA_CMD_READ_EXT;

	cfis->lba_high_exp = (block >> 40) & 0xff;
	cfis->lba_mid_exp = (block >> 32) & 0xff;
	cfis->lba_low_exp = (block >> 24) & 0xff;
	cfis->lba_high = (block >> 16) & 0xff;
	cfis->lba_mid = (block >> 8) & 0xff;
	cfis->lba_low = block & 0xff;
	cfis->device = 0xe0;
	cfis->sector_count_exp = (blkcnt >> 8) & 0xff;
	cfis->sector_count = blkcnt & 0xff;

	pc = NULL;
	if (pp->is_atapi) {
		pc = (u8 *) malloc(ATAPI_COMMAND_LEN, M_DEVBUF, M_NOWAIT);
		if (pc == NULL) {
			PRINTD("%s:%d malloc failed.\n", __FILE__, __LINE__);
			return -1;
		}
		memset(pc, 0, ATAPI_COMMAND_LEN);
		make_read_command_packet(pc, block, blkcnt);
	}

	get_ahci_device_data(sc, (u8 *) cfis, sizeof(struct cfis), buffer,
			     sc->bd->blksz * blkcnt, pc,
			     is_write);
	return blkcnt;
}

static u32 ata_low_level_rw_lba48(struct ahci_sata_softc *sc, u32 blknr, u32 blkcnt,
				  void *buffer, int is_write)
{
	int port_no = sc->port_no;
	u32 start, blks;
	u8 *addr;
	int max_blks;
	u32 blk_sz;

	start = blknr;
	blks = blkcnt;
	addr = (u8 *) buffer;
	blk_sz = sc->bd->blksz;

	max_blks = ATA_MAX_SECTORS_LBA48;
	do {
		if (blks > max_blks) {
			ahci_sata_rw_cmd_ext(sc, start, max_blks, addr,
					     is_write);
			start += max_blks;
			blks -= max_blks;
			addr += blk_sz * max_blks;
		} else {
			ahci_sata_rw_cmd_ext(sc, start, blks, addr,
					     is_write);
			start += blks;
			blks = 0;
			addr += blk_sz * blks;
		}
	} while (blks != 0);

	return blkcnt;
}

static u32 ahci_sata_rw_cmd(struct ahci_sata_softc *sc, u32 start, u32 blkcnt, u8 * buffer,
			    int is_write)
{
	int port_no = sc->port_no;
	struct ahci_probe_ent *probe_ent = sc->probe_ent;
	struct ahci_ioports *pp = &(probe_ent->port[port_no]);
	struct sata_fis_h2d h2d;
	struct cfis *cfis;
	u8 *pc = NULL;
	u32 block;

	block = start;
	cfis = (struct cfis *)&h2d;
	memset((void *)cfis, 0, sizeof(struct cfis));
	cfis->fis_type = SATA_FIS_TYPE_REGISTER_H2D;
	cfis->FIS_Flag = 1;	/* is command */

	if (pp->is_atapi) {
		cfis->command = ATA_CMD_PACKET;
		cfis->features = ATAPI_PKT_DMA;
	} else {
		cfis->command = (is_write) ? ATA_CMD_WRITE : ATA_CMD_READ;
	}

	cfis->device = 0xe0;

	cfis->device |= (block >> 24) & 0xf;
	cfis->lba_high = (block >> 16) & 0xff;
	cfis->lba_mid = (block >> 8) & 0xff;
	cfis->lba_low = block & 0xff;
	cfis->sector_count = (u8) (blkcnt & 0xff);
	if (pp->is_atapi) {
		pc = (u8 *) malloc(ATAPI_COMMAND_LEN, M_DEVBUF, M_NOWAIT);
		if (pc == NULL) {
			PRINTD("%s:%d malloc failed.\n", __FILE__, __LINE__);
			return -1;
		}
		memset(pc, 0, ATAPI_COMMAND_LEN);
		make_read_command_packet(pc, block, blkcnt);
	}
	ahci_debug("block = 0x%x, blkcnt = 0x%x blksz = %d \n", block, blkcnt,
		   sc->bd->blksz);
	get_ahci_device_data(sc, (u8 *) cfis, sizeof(struct cfis), buffer,
			     sc->bd->blksz * blkcnt, pc,
			     is_write);

	return blkcnt;
}

static u32 ata_low_level_rw_lba28(struct ahci_sata_softc *sc, u32 blknr, u32 blkcnt,
				  void *buffer, int is_write)
{
	int port_no = sc->port_no;
	struct ahci_probe_ent *probe_ent = sc->probe_ent;
	u32 start, blks;
	u8 *addr;
	int max_blks;
	u32 blk_sz;
	blk_sz = sc->bd->blksz;
	start = blknr;
	blks = blkcnt;
	addr = (u8 *) buffer;

	max_blks = ATA_MAX_SECTORS;
	do {
		if (blks > max_blks) {
			ahci_sata_rw_cmd(sc, start, max_blks, addr,
					 is_write);
			start += max_blks;
			blks -= max_blks;
			addr += blk_sz * max_blks;
		} else {
			ahci_sata_rw_cmd(sc, start, blks, addr, is_write);
			start += blks;
			blks = 0;
			addr += blk_sz * blks;
		}
	} while (blks != 0);

	return blkcnt;
}

/*
 * SATA interface between low level driver and command layer
 * */
ulong ahci_sata_read(struct ahci_sata_softc *sc, unsigned long blknr, lbaint_t blkcnt,
		     void *buffer)
{
	int port_no = sc->port_no;
	struct ahci_probe_ent *probe_ent = sc->probe_ent;
	u32 rc;

	if (sc->bd->lba48) {
		rc = ata_low_level_rw_lba48(sc, blknr, blkcnt, buffer,
					    READ_CMD);
	} else {
		rc = ata_low_level_rw_lba28(sc, blknr, blkcnt, buffer,
					    READ_CMD);
	}

	return rc;
}

ulong ahci_sata_write(struct ahci_sata_softc *sc, unsigned long blknr, lbaint_t blkcnt,
		      void *buffer)
{
	int port_no = sc->port_no;
	struct ahci_probe_ent *probe_ent = sc->probe_ent;
	u32 rc;

	if (sc->bd->lba48) {
		rc = ata_low_level_rw_lba48(sc, blknr, blkcnt, buffer,
					    WRITE_CMD);
	} else {
		rc = ata_low_level_rw_lba28(sc, blknr, blkcnt, buffer,
					    WRITE_CMD);
	}

	return rc;
}

/******************satafs***********************/
/*
 * Supported paths
 * 	/dev/sata
 */

#include <fcntl.h>
#include <file.h>
#include <sys/buf.h>
#include <ramfile.h>
#include <sys/unistd.h>
#undef _KERNEL_
#include <errno.h>

void ahci_sata_strategy(struct buf *bp, struct ahci_sata_softc *priv)
{
	struct ahci_probe_ent *probe_ent = priv->probe_ent;
	unsigned int blkno, blkcnt;
	int ret;

	blkno = bp->b_blkno;

	blkno = blkno / (priv->bs / DEV_BSIZE);
	blkcnt = howmany(bp->b_bcount, priv->bs);

	/* Valid request ? */
	if (bp->b_blkno < 0 ||
	    (bp->b_bcount % priv->bs) != 0 ||
	    (bp->b_bcount / priv->bs) >= (1 << NBBY)) {
		bp->b_error = EINVAL;
		PRINTD("Invalid request\n");
		goto bad;
	}

	/* If it's a null transfer, return immediately. */
	if (bp->b_bcount == 0)
		goto done;

	if (bp->b_flags & B_READ) {
		fault_timeout = 0;
		ret = ahci_sata_read(priv, blkno, blkcnt, bp->b_data);
		if (ret != blkcnt || fault_timeout)
			bp->b_flags |= B_ERROR;
		dotik(30000, 0);
	} else {
		fault_timeout = 0;
		ret = ahci_sata_write(priv, blkno, blkcnt, bp->b_data);
		if (ret != blkcnt || fault_timeout)
			bp->b_flags |= B_ERROR;
		dotik(30000, 0);
	}
done:
	biodone(bp);
	return;
bad:
	bp->b_flags |= B_ERROR;
	biodone(bp);
}

/*@flag 0: unload the media, 1: load the media 
 */
int cd_prepare(struct ahci_sata_softc *sc, int flag)
{
	int port_no = sc->port_no;
	struct ahci_probe_ent *probe_ent = sc->probe_ent;
	struct ahci_ioports *pp;
	volatile u8 *port_mmio;
	struct cfis *cfis;
	u8 *pc;
	u32 cfis_len, opts;

	pp = &(probe_ent->port[port_no]);
	port_mmio = (volatile u8 *)pp->port_mmio;

	cfis = (unsigned char *)pp->cmd_tbl;
	cfis_len = sizeof(struct cfis);
	memset((void *)cfis, 0, sizeof(struct cfis));
	cfis->fis_type = SATA_FIS_TYPE_REGISTER_H2D;
	cfis->FIS_Flag = 1;
	cfis->command = ATA_CMD_PACKET;
	cfis->features = ATAPI_PKT_DMA;

	pc = (u8 *) pp->cmd_tbl + AHCI_CMD_TBL_CDB;
	memset(pc, 0, ATAPI_COMMAND_LEN);
	pc[0] = GPCMD_START_STOP_UNIT;
	pc[4] = 0x2 + flag;

	opts = (cfis_len >> 2) | AHCI_CMD_ATAPI | AHCI_CMD_PREFETCH;
	ahci_fill_cmd_slot(pp, opts);
	writel_with_flush(1, port_mmio + PORT_CMD_ISSUE);

	if (waiting_for_cmd_completed(port_mmio + PORT_CMD_ISSUE, 200, 0x1)) {
		ahci_debug("timeout! %d bytes transferred. tfd: 0x%08x\n",
			   pp->cmd_slot->status,
			   readl(port_mmio + PORT_TFDATA));
		return -1;
	}

	return pp->cmd_slot->status;
}

int cd_test_unit_ready(struct ahci_sata_softc *sc)
{
	int port_no = sc->port_no;
	struct ahci_probe_ent *probe_ent = sc->probe_ent;
	struct ahci_ioports *pp;
	volatile u8 *port_mmio;
	struct cfis *cfis;
	u8 *pc;
	u32 cfis_len, opts;

	pp = &(probe_ent->port[port_no]);
	port_mmio = (volatile u8 *)pp->port_mmio;

	cfis = (unsigned char *)pp->cmd_tbl;
	cfis_len = sizeof(struct cfis);
	memset((void *)cfis, 0, sizeof(struct cfis));
	cfis->fis_type = SATA_FIS_TYPE_REGISTER_H2D;
	cfis->FIS_Flag = 1;
	cfis->command = ATA_CMD_PACKET;
	cfis->features = ATAPI_PKT_DMA;

	pc = (u8 *) pp->cmd_tbl + AHCI_CMD_TBL_CDB;
	memset(pc, 0, ATAPI_COMMAND_LEN);
	pc[0] = GPCMD_TEST_UNIT_READY;

	opts = (cfis_len >> 2) | AHCI_CMD_ATAPI | AHCI_CMD_PREFETCH;
	ahci_fill_cmd_slot(pp, opts);
	writel_with_flush(1, port_mmio + PORT_CMD_ISSUE);

	if (waiting_for_cmd_completed(port_mmio + PORT_CMD_ISSUE, 600, 0x1)) {
		ahci_debug("timeout! %d bytes transferred. tfd: 0x%08x\n",
			   pp->cmd_slot->status,
			   readl(port_mmio + PORT_TFDATA));
		return -1;
	}

	return pp->cmd_slot->status;
}

/******************** satafs end *********************/

int cmd_sata_ahci(int argc, char *argv[])
{
	int rc = 0;
	int i = 0;
	switch (argc) {
	case 0:
	case 1:
		PRINTD("Hello Sata!!!\n");
		return 1;
	case 2:
		if (strncmp(argv[1], "inf", 3) == 0) {
			int i;
			PRINTD("\n");
			for (i = 0; i < CFG_SATA_MAX_DEVICE && sata_dev_desc[i]; ++i) {
				if (sata_dev_desc[i]->type == DEV_TYPE_UNKNOWN) {
					PRINTD("sata_dev_desc[%d].type = %d\n",
					       i, sata_dev_desc[i]->type);
					continue;
				}
				PRINTD("SATA device %d:\n", i);
			}
			return 0;
		} else if (strncmp(argv[1], "dev", 3) == 0) {
			if ((curr_port < 0)
			    || (curr_port >= CFG_SATA_MAX_DEVICE)) {
				PRINTD("dev-curr_port=%d\n", curr_port);
				PRINTD("no SATA devices available\n");
				return 1;
			}
			PRINTD("SATA device %d:\n", curr_port);
			return 0;
		} else if (strncmp(argv[1], "part", 4) == 0) {
			int dev, ok;

			for (ok = 0, dev = 0; dev < CFG_SATA_MAX_DEVICE && sata_dev_desc[dev]; ++dev) {
				if (sata_dev_desc[dev]->part_type !=
				    PART_TYPE_UNKNOWN) {
					++ok;
				}
			}
			if (!ok) {
				PRINTD("\nno SATA devices available\n");
				rc++;
			}
			return rc;
		} else if (strcmp(argv[1], "inquiry") == 0) {
			ata_scsiop_inquiry(sata_dev_desc[curr_port]->priv);
		}
		return 1;
	case 3:
		if (strncmp(argv[1], "dev", 3) == 0) {
			int dev = (int)strtoul(argv[2], NULL, 10);

			PRINTD("SATA device %d:\n", dev);
			if (dev >= CFG_SATA_MAX_DEVICE) {
				PRINTD("unknown device\n");
				return 1;
			}

			if (sata_dev_desc[dev]->type == DEV_TYPE_UNKNOWN)
				return 1;

			curr_port = dev;

			PRINTD("... is now current device\n");

			return 0;
		} else if (strncmp(argv[1], "part", 4) == 0) {
			int dev = (int)strtoul(argv[2], NULL, 10);

			if (sata_dev_desc[dev]->part_type != PART_TYPE_UNKNOWN) {
			} else {
				PRINTD("\nSATA device %d not available\n", dev);
				rc = 1;
			}
			return rc;
		}
		return 1;

	default:		/* at least 4 args */
		if (strcmp(argv[1], "read") == 0) {
			ulong addr = strtoul(argv[2], NULL, 16);
			ulong cnt = strtoul(argv[4], NULL, 16);
			ulong n;
			lbaint_t blk = strtoul(argv[3], NULL, 16);

			PRINTD
			    ("\nSATA read: device %d block # %ld, count %ld ... ",
			     curr_port, blk, cnt);

			n = ahci_sata_read(sata_dev_desc[curr_port]->priv, blk, cnt, (u32 *) addr);
#if 0
			PRINTD("the buffer address is 0x%x\n", addr);
			for (i = 0; i < n * ATA_SECT_SIZE;) {
				PRINTD("%8x", *((u32 *) addr + i));
				i++;
				if (i % 8 == 0)
					PRINTD("\n");

			}
#endif

			/* flush cache after read */
#if 0
			flush_cache(addr, cnt * sata_dev_desc[curr_port]->blksz);
#endif

			PRINTD("n = %d,cnt = %d\n", n, cnt);
			PRINTD("%ld blocks read: %s\n", n,
			       (n == cnt) ? "OK" : "ERROR");
			return (n == cnt) ? 0 : 1;
		} else {
			if (strcmp(argv[1], "write") == 0) {
				ulong addr = strtoul(argv[2], NULL, 16);
				ulong cnt = strtoul(argv[4], NULL, 16);
				ulong n;

				lbaint_t blk = strtoul(argv[3], NULL, 16);

				PRINTD("\nSATA write: device %d block # %ld,"
				       "count %ld ... ", curr_port, blk, cnt);

				n = ahci_sata_write(sata_dev_desc[curr_port]->priv, blk, cnt,
						    (u32 *) addr);

				PRINTD("%ld blocks written: %s\n",
				       n, (n == cnt) ? "OK" : "ERROR");
				return (n == cnt) ? 0 : 1;
			} else {
				rc = 1;
			}
		}

		return rc;
	}
}

/*
 *  *  Command table registration
 *   *  ==========================
 *    */

static const Cmd Cmds[] = {
	{"ahcisata"},
	{"ahcisata", "[info device part read write]", 0, "ahci sata read write",
	 cmd_sata_ahci, 1, 99, 0},
	{0, 0}
};

static void init_cmd __P((void)) __attribute__ ((constructor));

void init_cmd()
{
	cmdlist_expand(Cmds, 1);
}
