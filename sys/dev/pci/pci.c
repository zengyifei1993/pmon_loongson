/*	$OpenBSD: pci.c,v 1.16 2001/01/27 05:02:39 mickey Exp $	*/
/*	$NetBSD: pci.c,v 1.31 1997/06/06 23:48:04 thorpej Exp $	*/

/*
 * Copyright (c) 1995, 1996 Christopher G. Demetriou.  All rights reserved.
 * Copyright (c) 1994 Charles Hannum.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Charles Hannum.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * PCI bus autoconfiguration.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#ifdef INIT_TIME
#define PRINTD(...)	  
#else
#define PRINTD(...)		printf(__VA_ARGS__)
#endif

int pcimatch __P((struct device *, void *, void *));
void pciattach __P((struct device *, struct device *, void *));

struct cfattach pci_ca = {
	sizeof(struct device), pcimatch, pciattach
};

struct cfdriver pci_cd = {
	NULL, "pci", DV_DULL
};

int	pciprint __P((void *, const char *));
int	pcisubmatch __P((struct device *, void *, void *));

/*
 * Callback so that ISA/EISA bridges can attach their child busses
 * after PCI configuration is done.
 *
 * This works because:
 *	(1) there can be at most one ISA/EISA bridge per PCI bus, and
 *	(2) any ISA/EISA bridges must be attached to primary PCI
 *	    busses (i.e. bus zero).
 *
 * That boils down to: there can only be one of these outstanding
 * at a time, it is cleared when configuring PCI bus 0 before any
 * subdevices have been found, and it is run after all subdevices
 * of PCI bus 0 have been found.
 *
 * This is needed because there are some (legacy) PCI devices which
 * can show up as ISA/EISA devices as well (the prime example of which
 * are VGA controllers).  If you attach ISA from a PCI-ISA/EISA bridge,
 * and the bridge is seen before the video board is, the board can show
 * up as an ISA device, and that can (bogusly) complicate the PCI device's
 * attach code, or make the PCI device not be properly attached at all.
 */
static void	(*pci_isa_bridge_callback) __P((void *));
static void	*pci_isa_bridge_callback_arg;

int
pcimatch(parent, match, aux)
	struct device *parent;
	void *match, *aux;
{
	struct cfdata *cf = match;
	struct pcibus_attach_args *pba = aux;

	if (strcmp(pba->pba_busname, cf->cf_driver->cd_name))
		return (0);

	/* Check the locators */
	if (cf->pcibuscf_bus != PCIBUS_UNK_BUS &&
	    cf->pcibuscf_bus != pba->pba_bus)
		return (0);

	/* sanity */
	if (pba->pba_bus < 0 || pba->pba_bus > 255)
		return (0);

	/*
	 * XXX check other (hardware?) indicators
	 */

	return 1;
}

void
pciattach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
	struct pcibus_attach_args *pba = aux;
	bus_space_tag_t iot, memt;
	pci_chipset_tag_t pc;
	int bus, device, maxndevs, function, nfunctions;

	pci_attach_hook(parent, self, pba);
	PRINTD("\n");

	iot = pba->pba_iot;
	memt = pba->pba_memt;
	pc = pba->pba_pc;
	bus = pba->pba_bus;
	maxndevs = pci_bus_maxdevs(pc, bus);

	if (bus == 0)
		pci_isa_bridge_callback = NULL;

	for (device = 0; device < maxndevs; device++) {
		pcitag_t tag;
		pcireg_t id, class, intr, bhlcr;
		struct pci_attach_args pa;
		int pin;

		tag = _pci_make_tag(bus, device, 0);

		if(!_pci_canscan(tag)) {
			continue;	/* Skip if not configurable */
		}

		id = pci_conf_read(pc, tag, PCI_ID_REG);

		/* Invalid vendor ID value? */
		if (PCI_VENDOR(id) == PCI_VENDOR_INVALID)
			continue;
		/* XXX Not invalid, but we've done this ~forever. */
		if (PCI_VENDOR(id) == 0)
			continue;

		bhlcr = pci_conf_read(pc, tag, PCI_BHLC_REG);
		nfunctions = PCI_HDRTYPE_MULTIFN(bhlcr) ? 8 : 1;

		for (function = 0; function < nfunctions; function++) {
			tag = _pci_make_tag(bus, device, function);
			id = pci_conf_read(pc, tag, PCI_ID_REG);

			/* Invalid vendor ID value? */
			if (PCI_VENDOR(id) == PCI_VENDOR_INVALID)
				continue;
			/* XXX Not invalid, but we've done this ~forever. */
			if (PCI_VENDOR(id) == 0)
				continue;

			class = pci_conf_read(pc, tag, PCI_CLASS_REG);
			intr = pci_conf_read(pc, tag, PCI_INTERRUPT_REG);

			pa.pa_iot = iot;
			pa.pa_memt = memt;
			pa.pa_dmat = pba->pba_dmat;
			pa.pa_pc = pc;
			pa.pa_device = device;
			pa.pa_function = function;
			pa.pa_tag = tag;
			pa.pa_id = id;
			pa.pa_class = class;

			/* This is a simplification of the NetBSD code.
			   We don't support turning off I/O or memory
			   on broken hardware. <csapuntz@stanford.edu> */
			pa.pa_flags = PCI_FLAGS_IO_ENABLED | PCI_FLAGS_MEM_ENABLED;
#ifdef __i386__
			/*
			 * on i386 we really need to know the device tag
			 * and not the pci bridge tag, in intr_map
			 * to be able to program the device and the
			 * pci interrupt router.
			 */
			pa.pa_intrtag = tag;
			pa.pa_intrswiz = 0;
#else
			if (bus == 0) {
				pa.pa_intrswiz = 0;
				pa.pa_intrtag = tag;
			} else {
				pa.pa_intrswiz = pba->pba_intrswiz + device;
				pa.pa_intrtag = pba->pba_intrtag;
			}
#endif
			pin = PCI_INTERRUPT_PIN(intr);
			if (pin == PCI_INTERRUPT_PIN_NONE) {
				/* no interrupt */
				pa.pa_intrpin = 0;
			} else {
				/*
				 * swizzle it based on the number of
				 * busses we're behind and our device
				 * number.
				 */
				pa.pa_intrpin =			/* XXX */
				    ((pin + pa.pa_intrswiz - 1) % 4) + 1;
			}
			pa.pa_intrline = PCI_INTERRUPT_LINE(intr);

			config_found_sm(self, &pa, pciprint, pcisubmatch);
		}
	}

	if (bus == 0 && pci_isa_bridge_callback != NULL)
		(*pci_isa_bridge_callback)(pci_isa_bridge_callback_arg);
}

int
pciprint(aux, pnp)
	void *aux;
	const char *pnp;
{
	register struct pci_attach_args *pa = aux;
	char devinfo[256];
	int supported;

	if (pnp) {
                supported=1;
		_pci_devinfo(pa->pa_id, pa->pa_class, &supported, devinfo);
		PRINTD("%s at %s", devinfo, pnp);
	}
	PRINTD(" dev %d function %d", pa->pa_device, pa->pa_function);
	if (!pnp) {
                supported=0;
		_pci_devinfo(pa->pa_id, pa->pa_class, &supported, devinfo);
		PRINTD(" %s", devinfo);
	}

	return (UNCONF);
}

int
pcisubmatch(parent, match, aux)
	struct device *parent;
	void *match, *aux;
{
	struct cfdata *cf = match;
	struct pci_attach_args *pa = aux;
	int success;

	if (cf->pcicf_dev != PCI_UNK_DEV &&
	    cf->pcicf_dev != pa->pa_device)
		return 0;
	if (cf->pcicf_function != PCI_UNK_FUNCTION &&
	    cf->pcicf_function != pa->pa_function)
		return 0;

	success = (*cf->cf_attach->ca_match)(parent, match, aux);

	/* My Dell BIOS does not enable certain non-critical PCI devices
	   for IO and memory cycles (e.g. network card). This is
	   the generic approach to fixing this problem. Basically, if
	   we support the card, then we enable its IO cycles.
	*/
	if (success) {
		u_int32_t csr = pci_conf_read(pa->pa_pc, pa->pa_tag,
					      PCI_COMMAND_STATUS_REG);

		pci_conf_write(pa->pa_pc, pa->pa_tag, PCI_COMMAND_STATUS_REG,
			       csr | PCI_COMMAND_MASTER_ENABLE |
			       PCI_COMMAND_IO_ENABLE |
			       PCI_COMMAND_MEM_ENABLE);
	}

	return (success);
}

int
pci_matchbyid(struct pci_attach_args *pa, const struct pci_matchid *ids,
    int nent)
{
       const struct pci_matchid *pm;
       int i;

       for (i = 0, pm = ids; i < nent; i++, pm++)
               if (PCI_VENDOR(pa->pa_id) == pm->pm_vid &&
                   PCI_PRODUCT(pa->pa_id) == pm->pm_pid)
                       return (1);
       return (0);
}

void
set_pci_isa_bridge_callback(fn, arg)
	void (*fn) __P((void *));
	void *arg;
{

	if (pci_isa_bridge_callback != NULL)
		panic("set_pci_isa_bridge_callback");
	pci_isa_bridge_callback = fn;
	pci_isa_bridge_callback_arg = arg;
}

int
pci_get_capability(pc, tag, capid, offset, value)
	pci_chipset_tag_t pc;
	pcitag_t tag;
	int capid;
	int *offset;
	pcireg_t *value;
{
	pcireg_t reg;
	unsigned int ofs;

	reg = pci_conf_read(pc, tag, PCI_COMMAND_STATUS_REG);
	if (!(reg & PCI_STATUS_CAPLIST_SUPPORT))
		return (0);

	ofs = PCI_CAPLIST_PTR(pci_conf_read(pc, tag, PCI_CAPLISTPTR_REG));
	while (ofs != 0) {
#ifdef DIAGNOSTIC
		if ((ofs & 3) || (ofs < 0x40))
			panic("pci_get_capability");
#endif
		reg = pci_conf_read(pc, tag, ofs);
		if (PCI_CAPLIST_CAP(reg) == capid) {
			if (offset)
				*offset = ofs;
			if (value)
				*value = reg;
			return (1);
		}
		ofs = PCI_CAPLIST_NEXT(reg);
	}

	return (0);
}
