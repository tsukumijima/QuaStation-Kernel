/**
 * linux/drivers/usb/gadget/rtk-hsotg.c
 *
 * RTK USB2.0 High-speed / OtG driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/of_platform.h>
//#include <linux/phy/phy.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/phy.h>
#include <linux/usb/otg.h>

#include "rtk-hsotg.h"

struct platform_device;
enum rtk_hsotg_dmamode {
	RTK_HSOTG_DMA_NONE,		/* do not use DMA at-all */
	RTK_HSOTG_DMA_ONLY,		/* always use DMA */
	RTK_HSOTG_DMA_DRV,		/* DMA is chosen by driver */
};

/**
 * struct rtk_hsotg_plat - platform data for high-speed otg/udc
 * @dma: Whether to use DMA or not.
 * @is_osc: The clock source is an oscillator, not a crystal
 */
struct rtk_hsotg_plat {
	enum rtk_hsotg_dmamode	dma;
	unsigned int		is_osc:1;
	int                     phy_type;

	int (*phy_init)(struct platform_device *pdev, int type);
	int (*phy_exit)(struct platform_device *pdev, int type);
};

extern void rtk_hsotg_set_platdata(struct rtk_hsotg_plat *pd);

static const char * const rtk_hsotg_supply_names[] = {
	"vusb_d",		/* digital USB supply, 1.2V */
	"vusb_a",		/* analog USB supply, 1.1V */
};

/*
 * EP0_MPS_LIMIT
 *
 * Unfortunately there seems to be a limit of the amount of data that can
 * be transferred by IN transactions on EP0. This is either 127 bytes or 3
 * packets (which practically means 1 packet and 63 bytes of data) when the
 * MPS is set to 64.
 *
 * This means if we are wanting to move >127 bytes of data, we need to
 * split the transactions up, but just doing one packet at a time does
 * not work (this may be an implicit DATA0 PID on first packet of the
 * transaction) and doing 2 packets is outside the controller's limits.
 *
 * If we try to lower the MPS size for EP0, then no transfers work properly
 * for EP0, and the system will fail basic enumeration. As no cause for this
 * has currently been found, we cannot support any large IN transfers for
 * EP0.
 */
#define EP0_MPS_LIMIT	64

struct rtk_hsotg;
struct rtk_hsotg_req;

/**
 * struct rtk_hsotg_ep - driver endpoint definition.
 * @ep: The gadget layer representation of the endpoint.
 * @name: The driver generated name for the endpoint.
 * @queue: Queue of requests for this endpoint.
 * @parent: Reference back to the parent device structure.
 * @req: The current request that the endpoint is processing. This is
 *       used to indicate an request has been loaded onto the endpoint
 *       and has yet to be completed (maybe due to data move, or simply
 *	 awaiting an ack from the core all the data has been completed).
 * @debugfs: File entry for debugfs file for this endpoint.
 * @lock: State lock to protect contents of endpoint.
 * @dir_in: Set to true if this endpoint is of the IN direction, which
 *	    means that it is sending data to the Host.
 * @index: The index for the endpoint registers.
 * @mc: Multi Count - number of transactions per microframe
 * @interval - Interval for periodic endpoints
 * @name: The name array passed to the USB core.
 * @halted: Set if the endpoint has been halted.
 * @periodic: Set if this is a periodic ep, such as Interrupt
 * @isochronous: Set if this is a isochronous ep
 * @sent_zlp: Set if we've sent a zero-length packet.
 * @total_data: The total number of data bytes done.
 * @fifo_size: The size of the FIFO (for periodic IN endpoints)
 * @fifo_load: The amount of data loaded into the FIFO (periodic IN)
 * @last_load: The offset of data for the last start of request.
 * @size_loaded: The last loaded size for DxEPTSIZE for periodic IN
 *
 * This is the driver's state for each registered enpoint, allowing it
 * to keep track of transactions that need doing. Each endpoint has a
 * lock to protect the state, to try and avoid using an overall lock
 * for the host controller as much as possible.
 *
 * For periodic IN endpoints, we have fifo_size and fifo_load to try
 * and keep track of the amount of data in the periodic FIFO for each
 * of these as we don't have a status register that tells us how much
 * is in each of them. (note, this may actually be useless information
 * as in shared-fifo mode periodic in acts like a single-frame packet
 * buffer than a fifo)
 */
struct rtk_hsotg_ep {
	struct usb_ep		ep;
	struct list_head	queue;
	struct rtk_hsotg	*parent;
	struct rtk_hsotg_req	*req;
	struct dentry		*debugfs;


	unsigned long		total_data;
	unsigned int		size_loaded;
	unsigned int		last_load;
	unsigned int		fifo_load;
	unsigned short		fifo_size;

	unsigned char		dir_in;
	unsigned char		index;
	unsigned char		mc;
	unsigned char		interval;

	unsigned int		halted:1;
	unsigned int		periodic:1;
	unsigned int		isochronous:1;
	unsigned int		sent_zlp:1;

	char			name[10];
};

/**
 * struct rtk_hsotg - driver state.
 * @dev: The parent device supplied to the probe function
 * @driver: USB gadget driver
 * @phy: The otg phy transceiver structure for phy control.
 * @uphy: The otg phy transceiver structure for old USB phy control.
 * @plat: The platform specific configuration data. This can be removed once
 * all SoCs support usb transceiver.
 * @regs: The memory area mapped for accessing registers.
 * @irq: The IRQ number we are using
 * @supplies: Definition of USB power supplies
 * @phyif: PHY interface width
 * @dedicated_fifos: Set if the hardware has dedicated IN-EP fifos.
 * @num_of_eps: Number of available EPs (excluding EP0)
 * @debug_root: root directrory for debugfs.
 * @debug_file: main status file for debugfs.
 * @debug_fifo: FIFO status file for debugfs.
 * @ep0_reply: Request used for ep0 reply.
 * @ep0_buff: Buffer for EP0 reply data, if needed.
 * @ctrl_buff: Buffer for EP0 control requests.
 * @ctrl_req: Request for EP0 control packets.
 * @setup: NAK management for EP0 SETUP
 * @last_rst: Time of last reset
 * @eps: The endpoints being supplied to the gadget framework
 */
struct rtk_hsotg {
	struct device		 *dev;
	struct usb_gadget_driver *driver;
	struct phy		 *phy;
	struct usb_phy		 *uphy;
	struct rtk_hsotg_plat	 *plat;

	spinlock_t              lock;

	void __iomem		*regs;
	void __iomem		*phyregs;
	int			irq;
	struct clk		*clk;

	struct regulator_bulk_data supplies[ARRAY_SIZE(rtk_hsotg_supply_names)];

	u32			phyif;
	unsigned int		dedicated_fifos:1;
	unsigned char           num_of_eps;

	struct dentry		*debug_root;
	struct dentry		*debug_file;
	struct dentry		*debug_fifo;

	struct usb_request	*ep0_reply;
	struct usb_request	*ctrl_req;
	u8			ep0_buff[8];
	u8			ctrl_buff[8];

	struct usb_gadget	gadget;
	unsigned int		setup;
	unsigned long           last_rst;
	struct rtk_hsotg_ep	*eps;

#ifdef CONFIG_USB_RTK_UDC_OTG_SWITCH
	int otg_type;
#define RTK_HOST_MODE 0
#define RTK_DEVICE_MODE 1
#endif //CONFIG_USB_RTK_UDC_OTG_SWITCH
};

/**
 * struct rtk_hsotg_req - data transfer request
 * @req: The USB gadget request
 * @queue: The list of requests for the endpoint this is queued for.
 * @in_progress: Has already had size/packets written to core
 * @mapped: DMA buffer for this request has been mapped via dma_map_single().
 */
struct rtk_hsotg_req {
	struct usb_request	req;
	struct list_head	queue;
	unsigned char		in_progress;
	unsigned char		mapped;
};

/* conversion functions */
static inline struct rtk_hsotg_req *our_req(struct usb_request *req)
{
	return container_of(req, struct rtk_hsotg_req, req);
}

static inline struct rtk_hsotg_ep *our_ep(struct usb_ep *ep)
{
	return container_of(ep, struct rtk_hsotg_ep, ep);
}

static inline struct rtk_hsotg *to_hsotg(struct usb_gadget *gadget)
{
	return container_of(gadget, struct rtk_hsotg, gadget);
}

static inline void __orr32(void __iomem *ptr, u32 val)
{
	writel(readl(ptr) | val, ptr);
}

static inline void __bic32(void __iomem *ptr, u32 val)
{
	writel(readl(ptr) & ~val, ptr);
}

/* forward decleration of functions */
static void rtk_hsotg_dump(struct rtk_hsotg *hsotg);

/**
 * using_dma - return the DMA status of the driver.
 * @hsotg: The driver state.
 *
 * Return true if we're using DMA.
 *
 * Currently, we have the DMA support code worked into everywhere
 * that needs it, but the AMBA DMA implementation in the hardware can
 * only DMA from 32bit aligned addresses. This means that gadgets such
 * as the CDC Ethernet cannot work as they often pass packets which are
 * not 32bit aligned.
 *
 * Unfortunately the choice to use DMA or not is global to the controller
 * and seems to be only settable when the controller is being put through
 * a core reset. This means we either need to fix the gadgets to take
 * account of DMA alignment, or add bounce buffers (yuerk).
 *
 * Until this issue is sorted out, we always return 'false'.
 */
static inline bool using_dma(struct rtk_hsotg *hsotg)
{
	return false;	/* support is not complete */
}

/**
 * rtk_hsotg_en_gsint - enable one or more of the general interrupt
 * @hsotg: The device state
 * @ints: A bitmask of the interrupts to enable
 */
static void rtk_hsotg_en_gsint(struct rtk_hsotg *hsotg, u32 ints)
{
	u32 gsintmsk = readl(hsotg->regs + GINTMSK);
	u32 new_gsintmsk;

	new_gsintmsk = gsintmsk | ints;

	if (new_gsintmsk != gsintmsk) {
		dev_dbg(hsotg->dev, "gsintmsk now 0x%08x\n", new_gsintmsk);
		writel(new_gsintmsk, hsotg->regs + GINTMSK);
	}
}

/**
 * rtk_hsotg_disable_gsint - disable one or more of the general interrupt
 * @hsotg: The device state
 * @ints: A bitmask of the interrupts to enable
 */
static void rtk_hsotg_disable_gsint(struct rtk_hsotg *hsotg, u32 ints)
{
	u32 gsintmsk = readl(hsotg->regs + GINTMSK);
	u32 new_gsintmsk;

	new_gsintmsk = gsintmsk & ~ints;

	if (new_gsintmsk != gsintmsk)
		writel(new_gsintmsk, hsotg->regs + GINTMSK);
}

/**
 * rtk_hsotg_ctrl_epint - enable/disable an endpoint irq
 * @hsotg: The device state
 * @ep: The endpoint index
 * @dir_in: True if direction is in.
 * @en: The enable value, true to enable
 *
 * Set or clear the mask for an individual endpoint's interrupt
 * request.
 */
static void rtk_hsotg_ctrl_epint(struct rtk_hsotg *hsotg,
				 unsigned int ep, unsigned int dir_in,
				 unsigned int en)
{
	unsigned long flags;
	u32 bit = 1 << ep;
	u32 daint;

	if (!dir_in)
		bit <<= 16;

	local_irq_save(flags);
	daint = readl(hsotg->regs + DAINTMSK);
	if (en)
		daint |= bit;
	else
		daint &= ~bit;
	writel(daint, hsotg->regs + DAINTMSK);
	local_irq_restore(flags);
}

/**
 * rtk_hsotg_init_fifo - initialise non-periodic FIFOs
 * @hsotg: The device instance.
 */
static void rtk_hsotg_init_fifo(struct rtk_hsotg *hsotg)
{
	unsigned int ep;
	unsigned int addr;
	unsigned int size;
	int timeout;
	u32 val;

#define FIFO_SZ		512	//2048
#define FIFO_DEPTH	256	//1024
	/* set FIFO sizes to 512/256 2048/1024 */

	writel(FIFO_SZ, hsotg->regs + GRXFSIZ);
	writel(GNPTXFSIZ_NPTxFStAddr(FIFO_SZ) |
	       GNPTXFSIZ_NPTxFDep(FIFO_DEPTH),
	       hsotg->regs + GNPTXFSIZ);

	/*
	 * arange all the rest of the TX FIFOs, as some versions of this
	 * block have overlapping default addresses. This also ensures
	 * that if the settings have been changed, then they are set to
	 * known values.
	 */

	/* start at the end of the GNPTXFSIZ, rounded up */
	addr = FIFO_SZ + FIFO_DEPTH;
	size = 768;

	/*
	 * currently we allocate TX FIFOs for all possible endpoints,
	 * and assume that they are all the same size.
	 */

	for (ep = 1; ep <= 15; ep++) {
		val = addr;
		val |= size << DPTXFSIZn_DPTxFSize_SHIFT;
		addr += size;

		writel(val, hsotg->regs + DPTXFSIZn(ep));
	}

	/*
	 * according to p428 of the design guide, we need to ensure that
	 * all fifos are flushed before continuing
	 */

	writel(GRSTCTL_TxFNum(0x10) | GRSTCTL_TxFFlsh |
	       GRSTCTL_RxFFlsh, hsotg->regs + GRSTCTL);

	/* wait until the fifos are both flushed */
	timeout = 100;
	while (1) {
		val = readl(hsotg->regs + GRSTCTL);

		if ((val & (GRSTCTL_TxFFlsh | GRSTCTL_RxFFlsh)) == 0)
			break;

		if (--timeout == 0) {
			dev_err(hsotg->dev,
				"%s: timeout flushing fifos (GRSTCTL=%08x)\n",
				__func__, val);
		}

		udelay(1);
	}

	dev_dbg(hsotg->dev, "FIFOs reset, timeout at %d\n", timeout);
}

/**
 * @ep: USB endpoint to allocate request for.
 * @flags: Allocation flags
 *
 * Allocate a new USB request structure appropriate for the specified endpoint
 */
static struct usb_request *rtk_hsotg_ep_alloc_request(struct usb_ep *ep,
						      gfp_t flags)
{
	struct rtk_hsotg_req *req;

	req = kzalloc(sizeof(struct rtk_hsotg_req), flags);
	if (!req)
		return NULL;

	INIT_LIST_HEAD(&req->queue);

	return &req->req;
}

/**
 * is_ep_periodic - return true if the endpoint is in periodic mode.
 * @hs_ep: The endpoint to query.
 *
 * Returns true if the endpoint is in periodic mode, meaning it is being
 * used for an Interrupt or ISO transfer.
 */
static inline int is_ep_periodic(struct rtk_hsotg_ep *hs_ep)
{
	return hs_ep->periodic;
}

/**
 * rtk_hsotg_unmap_dma - unmap the DMA memory being used for the request
 * @hsotg: The device state.
 * @hs_ep: The endpoint for the request
 * @hs_req: The request being processed.
 *
 * This is the reverse of rtk_hsotg_map_dma(), called for the completion
 * of a request to ensure the buffer is ready for access by the caller.
 */
static void rtk_hsotg_unmap_dma(struct rtk_hsotg *hsotg,
				struct rtk_hsotg_ep *hs_ep,
				struct rtk_hsotg_req *hs_req)
{
	struct usb_request *req = &hs_req->req;

	/* ignore this if we're not moving any data */
	if (hs_req->req.length == 0)
		return;

	usb_gadget_unmap_request(&hsotg->gadget, req, hs_ep->dir_in);
}

/**
 * rtk_hsotg_write_fifo - write packet Data to the TxFIFO
 * @hsotg: The controller state.
 * @hs_ep: The endpoint we're going to write for.
 * @hs_req: The request to write data for.
 *
 * This is called when the TxFIFO has some space in it to hold a new
 * transmission and we have something to give it. The actual setup of
 * the data size is done elsewhere, so all we have to do is to actually
 * write the data.
 *
 * The return value is zero if there is more space (or nothing was done)
 * otherwise -ENOSPC is returned if the FIFO space was used up.
 *
 * This routine is only needed for PIO
 */
static int rtk_hsotg_write_fifo(struct rtk_hsotg *hsotg,
				struct rtk_hsotg_ep *hs_ep,
				struct rtk_hsotg_req *hs_req)
{
	bool periodic = is_ep_periodic(hs_ep);
	u32 gnptxsts = readl(hsotg->regs + GNPTXSTS);
	int buf_pos = hs_req->req.actual;
	int to_write = hs_ep->size_loaded;
	void *data;
	int can_write;
	int pkt_round;
	int max_transfer;

	to_write -= (buf_pos - hs_ep->last_load);

	/* if there's nothing to write, get out early */
	if (to_write == 0)
		return 0;

	if (periodic && !hsotg->dedicated_fifos) {
		u32 epsize = readl(hsotg->regs + DIEPTSIZ(hs_ep->index));
		int size_left;
		int size_done;

		/*
		 * work out how much data was loaded so we can calculate
		 * how much data is left in the fifo.
		 */

		size_left = DxEPTSIZ_XferSize_GET(epsize);

		/*
		 * if shared fifo, we cannot write anything until the
		 * previous data has been completely sent.
		 */
		if (hs_ep->fifo_load != 0) {
			rtk_hsotg_en_gsint(hsotg, GINTSTS_PTxFEmp);
			return -ENOSPC;
		}

		dev_dbg(hsotg->dev, "%s: left=%d, load=%d, fifo=%d, size %d\n",
			__func__, size_left,
			hs_ep->size_loaded, hs_ep->fifo_load, hs_ep->fifo_size);

		/* how much of the data has moved */
		size_done = hs_ep->size_loaded - size_left;

		/* how much data is left in the fifo */
		can_write = hs_ep->fifo_load - size_done;
		dev_dbg(hsotg->dev, "%s: => can_write1=%d\n",
			__func__, can_write);

		can_write = hs_ep->fifo_size - can_write;
		dev_dbg(hsotg->dev, "%s: => can_write2=%d\n",
			__func__, can_write);

		if (can_write <= 0) {
			rtk_hsotg_en_gsint(hsotg, GINTSTS_PTxFEmp);
			return -ENOSPC;
		}
	} else if (hsotg->dedicated_fifos && hs_ep->index != 0) {
		can_write = readl(hsotg->regs + DTXFSTS(hs_ep->index));

		can_write &= 0xffff;
		can_write *= 4;
	} else {
		if (GNPTXSTS_NPTxQSpcAvail_GET(gnptxsts) == 0) {
			dev_dbg(hsotg->dev,
				"%s: no queue slots available (0x%08x)\n",
				__func__, gnptxsts);

			rtk_hsotg_en_gsint(hsotg, GINTSTS_NPTxFEmp);
			return -ENOSPC;
		}

		can_write = GNPTXSTS_NPTxFSpcAvail_GET(gnptxsts);
		can_write *= 4;	/* fifo size is in 32bit quantities. */
	}

	max_transfer = hs_ep->ep.maxpacket * hs_ep->mc;

	dev_dbg(hsotg->dev, "%s: GNPTXSTS=%08x, can=%d, to=%d, max_transfer %d\n",
		 __func__, gnptxsts, can_write, to_write, max_transfer);

	/*
	 * limit to 512 bytes of data, it seems at least on the non-periodic
	 * FIFO, requests of >512 cause the endpoint to get stuck with a
	 * fragment of the end of the transfer in it.
	 */
	if (can_write > 512 && !periodic)
		can_write = 512;

	/*
	 * limit the write to one max-packet size worth of data, but allow
	 * the transfer to return that it did not run out of fifo space
	 * doing it.
	 */
	if (to_write > max_transfer) {
		to_write = max_transfer;

		/* it's needed only when we do not use dedicated fifos */
		if (!hsotg->dedicated_fifos)
			rtk_hsotg_en_gsint(hsotg,
					   periodic ? GINTSTS_PTxFEmp :
					   GINTSTS_NPTxFEmp);
	}

	/* see if we can write data */

	if (to_write > can_write) {
		to_write = can_write;
		pkt_round = to_write % max_transfer;

		/*
		 * Round the write down to an
		 * exact number of packets.
		 *
		 * Note, we do not currently check to see if we can ever
		 * write a full packet or not to the FIFO.
		 */

		if (pkt_round)
			to_write -= pkt_round;

		/*
		 * enable correct FIFO interrupt to alert us when there
		 * is more room left.
		 */

		/* it's needed only when we do not use dedicated fifos */
		if (!hsotg->dedicated_fifos)
			rtk_hsotg_en_gsint(hsotg,
					   periodic ? GINTSTS_PTxFEmp :
					   GINTSTS_NPTxFEmp);
	}

	dev_dbg(hsotg->dev, "write %d/%d, can_write %d, done %d\n",
		 to_write, hs_req->req.length, can_write, buf_pos);

	if (to_write <= 0)
		return -ENOSPC;

	hs_req->req.actual = buf_pos + to_write;
	hs_ep->total_data += to_write;

	if (periodic)
		hs_ep->fifo_load += to_write;

	to_write = DIV_ROUND_UP(to_write, 4);
	data = hs_req->req.buf + buf_pos;

	iowrite32_rep(hsotg->regs + EPFIFO(hs_ep->index), data, to_write);

	return (to_write >= can_write) ? -ENOSPC : 0;
}

/**
 * get_ep_limit - get the maximum data legnth for this endpoint
 * @hs_ep: The endpoint
 *
 * Return the maximum data that can be queued in one go on a given endpoint
 * so that transfers that are too long can be split.
 */
static unsigned get_ep_limit(struct rtk_hsotg_ep *hs_ep)
{
	int index = hs_ep->index;
	unsigned maxsize;
	unsigned maxpkt;

	if (index != 0) {
		maxsize = DxEPTSIZ_XferSize_LIMIT + 1;
		maxpkt = DxEPTSIZ_PktCnt_LIMIT + 1;
	} else {
		maxsize = 64+64;
		if (hs_ep->dir_in)
			maxpkt = DIEPTSIZ0_PktCnt_LIMIT + 1;
		else
			maxpkt = 2;
	}

	/* we made the constant loading easier above by using +1 */
	maxpkt--;
	maxsize--;

	/*
	 * constrain by packet count if maxpkts*pktsize is greater
	 * than the length register size.
	 */

	if ((maxpkt * hs_ep->ep.maxpacket) < maxsize)
		maxsize = maxpkt * hs_ep->ep.maxpacket;

	return maxsize;
}

/**
 * rtk_hsotg_start_req - start a USB request from an endpoint's queue
 * @hsotg: The controller state.
 * @hs_ep: The endpoint to process a request for
 * @hs_req: The request to start.
 * @continuing: True if we are doing more for the current request.
 *
 * Start the given request running by setting the endpoint registers
 * appropriately, and writing any data to the FIFOs.
 */
static void rtk_hsotg_start_req(struct rtk_hsotg *hsotg,
				struct rtk_hsotg_ep *hs_ep,
				struct rtk_hsotg_req *hs_req,
				bool continuing)
{
	struct usb_request *ureq = &hs_req->req;
	int index = hs_ep->index;
	int dir_in = hs_ep->dir_in;
	u32 epctrl_reg;
	u32 epsize_reg;
	u32 epsize;
	u32 ctrl;
	unsigned length;
	unsigned packets;
	unsigned maxreq;

	if (index != 0) {
		if (hs_ep->req && !continuing) {
			dev_err(hsotg->dev, "%s: active request\n", __func__);
			WARN_ON(1);
			return;
		} else if (hs_ep->req != hs_req && continuing) {
			dev_err(hsotg->dev,
				"%s: continue different req\n", __func__);
			WARN_ON(1);
			return;
		}
	}

	epctrl_reg = dir_in ? DIEPCTL(index) : DOEPCTL(index);
	epsize_reg = dir_in ? DIEPTSIZ(index) : DOEPTSIZ(index);

	dev_dbg(hsotg->dev, "%s: DxEPCTL=0x%08x, ep %d, dir %s\n",
		__func__, readl(hsotg->regs + epctrl_reg), index,
		hs_ep->dir_in ? "in" : "out");

	/* If endpoint is stalled, we will restart request later */
	ctrl = readl(hsotg->regs + epctrl_reg);

	if (ctrl & DxEPCTL_Stall) {
		dev_warn(hsotg->dev, "%s: ep%d is stalled\n", __func__, index);
		return;
	}

	length = ureq->length - ureq->actual;
	dev_dbg(hsotg->dev, "ureq->length:%d ureq->actual:%d\n",
		ureq->length, ureq->actual);
	if (0)
		dev_dbg(hsotg->dev,
			"REQ buf %p len %d dma 0x%pad noi=%d zp=%d snok=%d\n",
			ureq->buf, length, &ureq->dma,
			ureq->no_interrupt, ureq->zero, ureq->short_not_ok);

	maxreq = get_ep_limit(hs_ep);
	if (length > maxreq) {
		int round = maxreq % hs_ep->ep.maxpacket;

		dev_dbg(hsotg->dev, "%s: length %d, max-req %d, r %d\n",
			__func__, length, maxreq, round);

		/* round down to multiple of packets */
		if (round)
			maxreq -= round;

		length = maxreq;
	}

	if (length)
		packets = DIV_ROUND_UP(length, hs_ep->ep.maxpacket);
	else
		packets = 1;	/* send one packet if length is zero. */

	if (hs_ep->isochronous && length > (hs_ep->mc * hs_ep->ep.maxpacket)) {
		dev_err(hsotg->dev, "req length > maxpacket*mc\n");
		return;
	}

	if (dir_in && index != 0)
		if (hs_ep->isochronous)
			epsize = DxEPTSIZ_MC(packets);
		else
			epsize = DxEPTSIZ_MC(1);
	else
		epsize = 0;

	if (index != 0 && ureq->zero) {
		/*
		 * test for the packets being exactly right for the
		 * transfer
		 */

		if (length == (packets * hs_ep->ep.maxpacket))
			packets++;
	}

	epsize |= DxEPTSIZ_PktCnt(packets);
	epsize |= DxEPTSIZ_XferSize(length);

	dev_dbg(hsotg->dev, "%s: %d@%d/%d, 0x%08x => 0x%08x\n",
		__func__, packets, length, ureq->length, epsize, epsize_reg);

	/* store the request as the current one we're doing */
	hs_ep->req = hs_req;

	/* write size / packets */
	writel(epsize, hsotg->regs + epsize_reg);

	if (using_dma(hsotg) && !continuing) {
		unsigned int dma_reg;

		/*
		 * write DMA address to control register, buffer already
		 * synced by rtk_hsotg_ep_queue().
		 */

		dma_reg = dir_in ? DIEPDMA(index) : DOEPDMA(index);
		writel(ureq->dma, hsotg->regs + dma_reg);

		dev_dbg(hsotg->dev, "%s: 0x%pad => 0x%08x\n",
			__func__, &ureq->dma, dma_reg);
	}

	ctrl |= DxEPCTL_EPEna;	/* ensure ep enabled */
	ctrl |= DxEPCTL_USBActEp;

	dev_dbg(hsotg->dev, "setup req:%d\n", hsotg->setup);

	/* For Setup request do not clear NAK */
#if 0	//barry
	if (hsotg->setup && index == 0)
		hsotg->setup = 0;
	else
#endif
		ctrl |= DxEPCTL_CNAK;	/* clear NAK set by core */


	dev_dbg(hsotg->dev, "%s: DxEPCTL=0x%08x\n", __func__, ctrl);
	writel(ctrl, hsotg->regs + epctrl_reg);

	/*
	 * set these, it seems that DMA support increments past the end
	 * of the packet buffer so we need to calculate the length from
	 * this information.
	 */
	hs_ep->size_loaded = length;
	hs_ep->last_load = ureq->actual;

	if (dir_in && !using_dma(hsotg)) {
		/* set these anyway, we may need them for non-periodic in */
		hs_ep->fifo_load = 0;

		rtk_hsotg_write_fifo(hsotg, hs_ep, hs_req);
	}

	/*
	 * clear the INTknTXFEmpMsk when we start request, more as a aide
	 * to debugging to see what is going on.
	 */
	if (dir_in)
		writel(DIEPMSK_INTknTXFEmpMsk,
		       hsotg->regs + DIEPINT(index));

	/* check ep is enabled */
	if (!(readl(hsotg->regs + epctrl_reg) & DxEPCTL_EPEna))
		dev_warn(hsotg->dev,
			 "ep%d: failed to become enabled (DxEPCTL=0x%08x)?\n",
			 index, readl(hsotg->regs + epctrl_reg));

	dev_dbg(hsotg->dev, "%s: DxEPCTL=0x%08x\n",
		__func__, readl(hsotg->regs + epctrl_reg));

	/* enable ep interrupts */
	rtk_hsotg_ctrl_epint(hsotg, hs_ep->index, hs_ep->dir_in, 1);
}

/**
 * rtk_hsotg_map_dma - map the DMA memory being used for the request
 * @hsotg: The device state.
 * @hs_ep: The endpoint the request is on.
 * @req: The request being processed.
 *
 * We've been asked to queue a request, so ensure that the memory buffer
 * is correctly setup for DMA. If we've been passed an extant DMA address
 * then ensure the buffer has been synced to memory. If our buffer has no
 * DMA memory, then we map the memory and mark our request to allow us to
 * cleanup on completion.
 */
static int rtk_hsotg_map_dma(struct rtk_hsotg *hsotg,
			     struct rtk_hsotg_ep *hs_ep,
			     struct usb_request *req)
{
	struct rtk_hsotg_req *hs_req = our_req(req);
	int ret;

	/* if the length is zero, ignore the DMA data */
	if (hs_req->req.length == 0)
		return 0;

	ret = usb_gadget_map_request(&hsotg->gadget, req, hs_ep->dir_in);
	if (ret)
		goto dma_error;

	return 0;

dma_error:
	dev_err(hsotg->dev, "%s: failed to map buffer %p, %d bytes\n",
		__func__, req->buf, req->length);

	return -EIO;
}

static int rtk_hsotg_ep_queue(struct usb_ep *ep, struct usb_request *req,
			      gfp_t gfp_flags)
{
	struct rtk_hsotg_req *hs_req = our_req(req);
	struct rtk_hsotg_ep *hs_ep = our_ep(ep);
	struct rtk_hsotg *hs = hs_ep->parent;
	bool first;

	dev_dbg(hs->dev, "%s: req %p: %d@%p, noi=%d, zero=%d, snok=%d\n",
		ep->name, req, req->length, req->buf, req->no_interrupt,
		req->zero, req->short_not_ok);

	/* initialise status of the request */
	INIT_LIST_HEAD(&hs_req->queue);
	req->actual = 0;
	req->status = -EINPROGRESS;

	/* if we're using DMA, sync the buffers as necessary */
	if (using_dma(hs)) {
		int ret = rtk_hsotg_map_dma(hs, hs_ep, req);
		if (ret)
			return ret;
	}

	first = list_empty(&hs_ep->queue);
	list_add_tail(&hs_req->queue, &hs_ep->queue);

	if (first)
		rtk_hsotg_start_req(hs, hs_ep, hs_req, false);

	return 0;
}

static int rtk_hsotg_ep_queue_lock(struct usb_ep *ep, struct usb_request *req,
			      gfp_t gfp_flags)
{
	struct rtk_hsotg_ep *hs_ep = our_ep(ep);
	struct rtk_hsotg *hs = hs_ep->parent;
	unsigned long flags = 0;
	int ret = 0;

	spin_lock_irqsave(&hs->lock, flags);
	ret = rtk_hsotg_ep_queue(ep, req, gfp_flags);
	spin_unlock_irqrestore(&hs->lock, flags);

	return ret;
}

static void rtk_hsotg_ep_free_request(struct usb_ep *ep,
				      struct usb_request *req)
{
	struct rtk_hsotg_req *hs_req = our_req(req);

	kfree(hs_req);
}

/**
 * rtk_hsotg_complete_oursetup - setup completion callback
 * @ep: The endpoint the request was on.
 * @req: The request completed.
 *
 * Called on completion of any requests the driver itself
 * submitted that need cleaning up.
 */
static void rtk_hsotg_complete_oursetup(struct usb_ep *ep,
					struct usb_request *req)
{
	struct rtk_hsotg_ep *hs_ep = our_ep(ep);
	struct rtk_hsotg *hsotg = hs_ep->parent;

	dev_dbg(hsotg->dev, "%s: ep %p, req %p\n", __func__, ep, req);

	rtk_hsotg_ep_free_request(ep, req);
}

/**
 * ep_from_windex - convert control wIndex value to endpoint
 * @hsotg: The driver state.
 * @windex: The control request wIndex field (in host order).
 *
 * Convert the given wIndex into a pointer to an driver endpoint
 * structure, or return NULL if it is not a valid endpoint.
 */
static struct rtk_hsotg_ep *ep_from_windex(struct rtk_hsotg *hsotg,
					   u32 windex)
{
	struct rtk_hsotg_ep *ep = &hsotg->eps[windex & 0x7F];
	int dir = (windex & USB_DIR_IN) ? 1 : 0;
	int idx = windex & 0x7F;

	if (windex >= 0x100)
		return NULL;

	if (idx > hsotg->num_of_eps)
		return NULL;

	if (idx && ep->dir_in != dir)
		return NULL;

	return ep;
}

/**
 * rtk_hsotg_send_reply - send reply to control request
 * @hsotg: The device state
 * @ep: Endpoint 0
 * @buff: Buffer for request
 * @length: Length of reply.
 *
 * Create a request and queue it on the given endpoint. This is useful as
 * an internal method of sending replies to certain control requests, etc.
 */
static int rtk_hsotg_send_reply(struct rtk_hsotg *hsotg,
				struct rtk_hsotg_ep *ep,
				void *buff,
				int length)
{
	struct usb_request *req;
	int ret;

	dev_dbg(hsotg->dev, "%s: buff %p, len %d\n", __func__, buff, length);

	req = rtk_hsotg_ep_alloc_request(&ep->ep, GFP_ATOMIC);
	hsotg->ep0_reply = req;
	if (!req) {
		dev_warn(hsotg->dev, "%s: cannot alloc req\n", __func__);
		return -ENOMEM;
	}

	req->buf = hsotg->ep0_buff;
	req->length = length;
	req->zero = 1; /* always do zero-length final transfer */
	req->complete = rtk_hsotg_complete_oursetup;

	if (length)
		memcpy(req->buf, buff, length);
	else
		ep->sent_zlp = 1;

	ret = rtk_hsotg_ep_queue(&ep->ep, req, GFP_ATOMIC);
	if (ret) {
		dev_warn(hsotg->dev, "%s: cannot queue req\n", __func__);
		return ret;
	}

	return 0;
}

/**
 * rtk_hsotg_process_req_status - process request GET_STATUS
 * @hsotg: The device state
 * @ctrl: USB control request
 */
static int rtk_hsotg_process_req_status(struct rtk_hsotg *hsotg,
					struct usb_ctrlrequest *ctrl)
{
	struct rtk_hsotg_ep *ep0 = &hsotg->eps[0];
	struct rtk_hsotg_ep *ep;
	__le16 reply;
	int ret;

	dev_dbg(hsotg->dev, "%s: USB_REQ_GET_STATUS\n", __func__);

	if (!ep0->dir_in) {
		dev_warn(hsotg->dev, "%s: direction out?\n", __func__);
		return -EINVAL;
	}

	switch (ctrl->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		reply = cpu_to_le16(0); /* bit 0 => self powered,
					 * bit 1 => remote wakeup */
		break;

	case USB_RECIP_INTERFACE:
		/* currently, the data result should be zero */
		reply = cpu_to_le16(0);
		break;

	case USB_RECIP_ENDPOINT:
		ep = ep_from_windex(hsotg, le16_to_cpu(ctrl->wIndex));
		if (!ep)
			return -ENOENT;

		reply = cpu_to_le16(ep->halted ? 1 : 0);
		break;

	default:
		return 0;
	}

	if (le16_to_cpu(ctrl->wLength) != 2)
		return -EINVAL;

	ret = rtk_hsotg_send_reply(hsotg, ep0, &reply, 2);
	if (ret) {
		dev_err(hsotg->dev, "%s: failed to send reply\n", __func__);
		return ret;
	}

	return 1;
}

static int rtk_hsotg_ep_sethalt(struct usb_ep *ep, int value);

/**
 * get_ep_head - return the first request on the endpoint
 * @hs_ep: The controller endpoint to get
 *
 * Get the first request on the endpoint.
 */
static struct rtk_hsotg_req *get_ep_head(struct rtk_hsotg_ep *hs_ep)
{
	if (list_empty(&hs_ep->queue))
		return NULL;

	return list_first_entry(&hs_ep->queue, struct rtk_hsotg_req, queue);
}

/**
 * rtk_hsotg_process_req_featire - process request {SET,CLEAR}_FEATURE
 * @hsotg: The device state
 * @ctrl: USB control request
 */
static int rtk_hsotg_process_req_feature(struct rtk_hsotg *hsotg,
					 struct usb_ctrlrequest *ctrl)
{
	struct rtk_hsotg_ep *ep0 = &hsotg->eps[0];
	struct rtk_hsotg_req *hs_req;
	bool restart;
	bool set = (ctrl->bRequest == USB_REQ_SET_FEATURE);
	struct rtk_hsotg_ep *ep;
	int ret;
	bool halted;

	dev_dbg(hsotg->dev, "%s: %s_FEATURE\n",
		__func__, set ? "SET" : "CLEAR");

	if (ctrl->bRequestType == USB_RECIP_ENDPOINT) {
		ep = ep_from_windex(hsotg, le16_to_cpu(ctrl->wIndex));
		if (!ep) {
			dev_dbg(hsotg->dev, "%s: no endpoint for 0x%04x\n",
				__func__, le16_to_cpu(ctrl->wIndex));
			return -ENOENT;
		}

		switch (le16_to_cpu(ctrl->wValue)) {
		case USB_ENDPOINT_HALT:
			halted = ep->halted;

			rtk_hsotg_ep_sethalt(&ep->ep, set);

			ret = rtk_hsotg_send_reply(hsotg, ep0, NULL, 0);
			if (ret) {
				dev_err(hsotg->dev,
					"%s: failed to send reply\n", __func__);
				return ret;
			}

			/*
			 * we have to complete all requests for ep if it was
			 * halted, and the halt was cleared by CLEAR_FEATURE
			 */

			if (!set && halted) {
				/*
				 * If we have request in progress,
				 * then complete it
				 */
				if (ep->req) {
					hs_req = ep->req;
					ep->req = NULL;
					list_del_init(&hs_req->queue);
					hs_req->req.complete(&ep->ep,
							     &hs_req->req);
				}

				/* If we have pending request, then start it */
				restart = !list_empty(&ep->queue);
				if (restart) {
					hs_req = get_ep_head(ep);
					rtk_hsotg_start_req(hsotg, ep,
							    hs_req, false);
				}
			}

			break;

		default:
			return -ENOENT;
		}
	} else
		return -ENOENT;  /* currently only deal with endpoint */

	return 1;
}

static void rtk_hsotg_enqueue_setup(struct rtk_hsotg *hsotg);
static void rtk_hsotg_disconnect(struct rtk_hsotg *hsotg);

/**
 * rtk_hsotg_stall_ep0 - stall ep0
 * @hsotg: The device state
 *
 * Set stall for ep0 as response for setup request.
 */
static void rtk_hsotg_stall_ep0(struct rtk_hsotg *hsotg) {
	struct rtk_hsotg_ep *ep0 = &hsotg->eps[0];
	u32 reg;
	u32 ctrl;

	dev_dbg(hsotg->dev, "ep0 stall (dir=%d)\n", ep0->dir_in);
	reg = (ep0->dir_in) ? DIEPCTL0 : DOEPCTL0;

	/*
	 * DxEPCTL_Stall will be cleared by EP once it has
	 * taken effect, so no need to clear later.
	 */

	ctrl = readl(hsotg->regs + reg);
	ctrl |= DxEPCTL_Stall;
	ctrl |= DxEPCTL_CNAK;
	writel(ctrl, hsotg->regs + reg);

	dev_dbg(hsotg->dev,
		"written DxEPCTL=0x%08x to %08x (DxEPCTL=0x%08x)\n",
		ctrl, reg, readl(hsotg->regs + reg));

	 /*
	  * complete won't be called, so we enqueue
	  * setup request here
	  */
	 rtk_hsotg_enqueue_setup(hsotg);
}

/**
 * rtk_hsotg_process_control - process a control request
 * @hsotg: The device state
 * @ctrl: The control request received
 *
 * The controller has received the SETUP phase of a control request, and
 * needs to work out what to do next (and whether to pass it on to the
 * gadget driver).
 */
static void rtk_hsotg_process_control(struct rtk_hsotg *hsotg,
				      struct usb_ctrlrequest *ctrl)
{
	struct rtk_hsotg_ep *ep0 = &hsotg->eps[0];
	int ret = 0;
	u32 dcfg;

	ep0->sent_zlp = 0;

	dev_dbg(hsotg->dev, "ctrl Req=%02x, Type=%02x, V=%04x, L=%04x\n",
		 ctrl->bRequest, ctrl->bRequestType,
		 ctrl->wValue, ctrl->wLength);

	/*
	 * record the direction of the request, for later use when enquing
	 * packets onto EP0.
	 */

	ep0->dir_in = (ctrl->bRequestType & USB_DIR_IN) ? 1 : 0;
	dev_dbg(hsotg->dev, "ctrl: dir_in=%d\n", ep0->dir_in);

	/*
	 * if we've no data with this request, then the last part of the
	 * transaction is going to implicitly be IN.
	 */
	if (ctrl->wLength == 0)
		ep0->dir_in = 1;

	if ((ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD) {
		switch (ctrl->bRequest) {
		case USB_REQ_SET_ADDRESS:
			rtk_hsotg_disconnect(hsotg);
			dcfg = readl(hsotg->regs + DCFG);
			dcfg &= ~DCFG_DevAddr_MASK;
			dcfg |= ctrl->wValue << DCFG_DevAddr_SHIFT;
			writel(dcfg, hsotg->regs + DCFG);

			dev_info(hsotg->dev, "new address %d\n", ctrl->wValue);

			ret = rtk_hsotg_send_reply(hsotg, ep0, NULL, 0);
			return;

		case USB_REQ_GET_STATUS:
			ret = rtk_hsotg_process_req_status(hsotg, ctrl);
			break;

		case USB_REQ_CLEAR_FEATURE:
		case USB_REQ_SET_FEATURE:
			ret = rtk_hsotg_process_req_feature(hsotg, ctrl);
			break;
		}
	}

	/* as a fallback, try delivering it to the driver to deal with */

	if (ret == 0 && hsotg->driver) {
		spin_unlock(&hsotg->lock);
		ret = hsotg->driver->setup(&hsotg->gadget, ctrl);
		spin_lock(&hsotg->lock);
		if (ret < 0)
			dev_dbg(hsotg->dev, "driver->setup() ret %d\n", ret);
	}

	/*
	 * the request is either unhandlable, or is not formatted correctly
	 * so respond with a STALL for the status stage to indicate failure.
	 */

	if (ret < 0)
		rtk_hsotg_stall_ep0(hsotg);
}

/**
 * rtk_hsotg_complete_setup - completion of a setup transfer
 * @ep: The endpoint the request was on.
 * @req: The request completed.
 *
 * Called on completion of any requests the driver itself submitted for
 * EP0 setup packets
 */
static void rtk_hsotg_complete_setup(struct usb_ep *ep,
				     struct usb_request *req)
{
	struct rtk_hsotg_ep *hs_ep = our_ep(ep);
	struct rtk_hsotg *hsotg = hs_ep->parent;

	if (req->status < 0) {
		dev_dbg(hsotg->dev, "%s: failed %d\n", __func__, req->status);
		return;
	}

	spin_lock(&hsotg->lock);
	if (req->actual == 0)
		rtk_hsotg_enqueue_setup(hsotg);
	else
		rtk_hsotg_process_control(hsotg, req->buf);
	spin_unlock(&hsotg->lock);
}

/**
 * rtk_hsotg_enqueue_setup - start a request for EP0 packets
 * @hsotg: The device state.
 *
 * Enqueue a request on EP0 if necessary to received any SETUP packets
 * received from the host.
 */
static void rtk_hsotg_enqueue_setup(struct rtk_hsotg *hsotg)
{
	struct usb_request *req = hsotg->ctrl_req;
	struct rtk_hsotg_req *hs_req = our_req(req);
	int ret;

	dev_dbg(hsotg->dev, "%s: queueing setup request\n", __func__);

	req->zero = 0;
	req->length = 8;
	req->buf = hsotg->ctrl_buff;
	req->complete = rtk_hsotg_complete_setup;

	if (!list_empty(&hs_req->queue)) {
		dev_dbg(hsotg->dev, "%s already queued???\n", __func__);
		return;
	}

	hsotg->eps[0].dir_in = 0;

	ret = rtk_hsotg_ep_queue(&hsotg->eps[0].ep, req, GFP_ATOMIC);
	if (ret < 0) {
		dev_err(hsotg->dev, "%s: failed queue (%d)\n", __func__, ret);
		/*
		 * Don't think there's much we can do other than watch the
		 * driver fail.
		 */
	}
}

/**
 * rtk_hsotg_complete_request - complete a request given to us
 * @hsotg: The device state.
 * @hs_ep: The endpoint the request was on.
 * @hs_req: The request to complete.
 * @result: The result code (0 => Ok, otherwise errno)
 *
 * The given request has finished, so call the necessary completion
 * if it has one and then look to see if we can start a new request
 * on the endpoint.
 *
 * Note, expects the ep to already be locked as appropriate.
 */
static void rtk_hsotg_complete_request(struct rtk_hsotg *hsotg,
				       struct rtk_hsotg_ep *hs_ep,
				       struct rtk_hsotg_req *hs_req,
				       int result)
{
	bool restart;

	if (!hs_req) {
		dev_dbg(hsotg->dev, "%s: nothing to complete?\n", __func__);
		return;
	}

	dev_dbg(hsotg->dev, "complete: ep %p %s, req %p, %d => %p\n",
		hs_ep, hs_ep->ep.name, hs_req, result, hs_req->req.complete);

	/*
	 * only replace the status if we've not already set an error
	 * from a previous transaction
	 */

	if (hs_req->req.status == -EINPROGRESS)
		hs_req->req.status = result;

	hs_ep->req = NULL;
	list_del_init(&hs_req->queue);

	if (using_dma(hsotg))
		rtk_hsotg_unmap_dma(hsotg, hs_ep, hs_req);

	/*
	 * call the complete request with the locks off, just in case the
	 * request tries to queue more work for this endpoint.
	 */

	if (hs_req->req.complete) {
		spin_unlock(&hsotg->lock);
		hs_req->req.complete(&hs_ep->ep, &hs_req->req);
		spin_lock(&hsotg->lock);
	}

	/*
	 * Look to see if there is anything else to do. Note, the completion
	 * of the previous request may have caused a new request to be started
	 * so be careful when doing this.
	 */

	if (!hs_ep->req && result >= 0) {
		restart = !list_empty(&hs_ep->queue);
		if (restart) {
			hs_req = get_ep_head(hs_ep);
			rtk_hsotg_start_req(hsotg, hs_ep, hs_req, false);
		}
	}
}

/**
 * rtk_hsotg_rx_data - receive data from the FIFO for an endpoint
 * @hsotg: The device state.
 * @ep_idx: The endpoint index for the data
 * @size: The size of data in the fifo, in bytes
 *
 * The FIFO status shows there is data to read from the FIFO for a given
 * endpoint, so sort out whether we need to read the data into a request
 * that has been made for that endpoint.
 */
static void rtk_hsotg_rx_data(struct rtk_hsotg *hsotg, int ep_idx, int size)
{
	struct rtk_hsotg_ep *hs_ep = &hsotg->eps[ep_idx];
	struct rtk_hsotg_req *hs_req = hs_ep->req;
	void __iomem *fifo = hsotg->regs + EPFIFO(ep_idx);
	int to_read;
	int max_req;
	int read_ptr;


	if (!hs_req) {
		u32 epctl = readl(hsotg->regs + DOEPCTL(ep_idx));
		int ptr;

		dev_warn(hsotg->dev,
			 "%s: FIFO %d bytes on ep%d but no req (DxEPCTl=0x%08x)\n",
			 __func__, size, ep_idx, epctl);

		/* dump the data from the FIFO, we've nothing we can do */
		for (ptr = 0; ptr < size; ptr += 4)
			(void)readl(fifo);

		return;
	}

	to_read = size;
	read_ptr = hs_req->req.actual;
	max_req = hs_req->req.length - read_ptr;

	dev_dbg(hsotg->dev, "%s: read %d/%d, done %d/%d\n",
		__func__, to_read, max_req, read_ptr, hs_req->req.length);

	if (to_read > max_req) {
		/*
		 * more data appeared than we where willing
		 * to deal with in this request.
		 */

		/* currently we don't deal this */
		WARN_ON_ONCE(1);
	}

	hs_ep->total_data += to_read;
	hs_req->req.actual += to_read;
	to_read = DIV_ROUND_UP(to_read, 4);

	/*
	 * note, we might over-write the buffer end by 3 bytes depending on
	 * alignment of the data.
	 */
	ioread32_rep(fifo, hs_req->req.buf + read_ptr, to_read);
}

/**
 * rtk_hsotg_send_zlp - send zero-length packet on control endpoint
 * @hsotg: The device instance
 * @req: The request currently on this endpoint
 *
 * Generate a zero-length IN packet request for terminating a SETUP
 * transaction.
 *
 * Note, since we don't write any data to the TxFIFO, then it is
 * currently believed that we do not need to wait for any space in
 * the TxFIFO.
 */
static void rtk_hsotg_send_zlp(struct rtk_hsotg *hsotg,
			       struct rtk_hsotg_req *req)
{
	u32 ctrl;

	if (!req) {
		dev_warn(hsotg->dev, "%s: no request?\n", __func__);
		return;
	}

	if (req->req.length == 0) {
		hsotg->eps[0].sent_zlp = 1;
		rtk_hsotg_enqueue_setup(hsotg);
		return;
	}

	hsotg->eps[0].dir_in = 1;
	hsotg->eps[0].sent_zlp = 1;

	dev_dbg(hsotg->dev, "sending zero-length packet\n");

	/* issue a zero-sized packet to terminate this */
	writel(DxEPTSIZ_MC(1) | DxEPTSIZ_PktCnt(1) |
	       DxEPTSIZ_XferSize(0), hsotg->regs + DIEPTSIZ(0));

	ctrl = readl(hsotg->regs + DIEPCTL0);
	ctrl |= DxEPCTL_CNAK;  /* clear NAK set by core */
	ctrl |= DxEPCTL_EPEna; /* ensure ep enabled */
	ctrl |= DxEPCTL_USBActEp;
	writel(ctrl, hsotg->regs + DIEPCTL0);
}

/**
 * rtk_hsotg_handle_outdone - handle receiving OutDone/SetupDone from RXFIFO
 * @hsotg: The device instance
 * @epnum: The endpoint received from
 * @was_setup: Set if processing a SetupDone event.
 *
 * The RXFIFO has delivered an OutDone event, which means that the data
 * transfer for an OUT endpoint has been completed, either by a short
 * packet or by the finish of a transfer.
 */
static void rtk_hsotg_handle_outdone(struct rtk_hsotg *hsotg,
				     int epnum, bool was_setup)
{
	u32 epsize = readl(hsotg->regs + DOEPTSIZ(epnum));
	struct rtk_hsotg_ep *hs_ep = &hsotg->eps[epnum];
	struct rtk_hsotg_req *hs_req = hs_ep->req;
	struct usb_request *req = &hs_req->req;
	unsigned size_left = DxEPTSIZ_XferSize_GET(epsize);
	int result = 0;

	if (!hs_req) {
		dev_dbg(hsotg->dev, "%s: no request active\n", __func__);
		return;
	}

	if (using_dma(hsotg)) {
		unsigned size_done;

		/*
		 * Calculate the size of the transfer by checking how much
		 * is left in the endpoint size register and then working it
		 * out from the amount we loaded for the transfer.
		 *
		 * We need to do this as DMA pointers are always 32bit aligned
		 * so may overshoot/undershoot the transfer.
		 */

		size_done = hs_ep->size_loaded - size_left;
		size_done += hs_ep->last_load;

		req->actual = size_done;
	}

	/* if there is more request to do, schedule new transfer */
	if (req->actual < req->length && size_left == 0) {
		rtk_hsotg_start_req(hsotg, hs_ep, hs_req, true);
		return;
	} else if (epnum == 0) {
		/*
		 * After was_setup = 1 =>
		 * set CNAK for non Setup requests
		 */
		hsotg->setup = was_setup ? 0 : 1;
	}

	if (req->actual < req->length && req->short_not_ok) {
		dev_dbg(hsotg->dev, "%s: got %d/%d (short not ok) => error\n",
			__func__, req->actual, req->length);

		/*
		 * todo - what should we return here? there's no one else
		 * even bothering to check the status.
		 */
	}

	if (epnum == 0) {
		/*
		 * Condition req->complete != rtk_hsotg_complete_setup says:
		 * send ZLP when we have an asynchronous request from gadget
		 */
		if (!was_setup && req->complete != rtk_hsotg_complete_setup)
			rtk_hsotg_send_zlp(hsotg, hs_req);
	}

	rtk_hsotg_complete_request(hsotg, hs_ep, hs_req, result);
}

/**
 * rtk_hsotg_read_frameno - read current frame number
 * @hsotg: The device instance
 *
 * Return the current frame number
 */
static u32 rtk_hsotg_read_frameno(struct rtk_hsotg *hsotg)
{
	u32 dsts;

	dsts = readl(hsotg->regs + DSTS);
	dsts &= DSTS_SOFFN_MASK;
	dsts >>= DSTS_SOFFN_SHIFT;

	return dsts;
}

/**
 * rtk_hsotg_handle_rx - RX FIFO has data
 * @hsotg: The device instance
 *
 * The IRQ handler has detected that the RX FIFO has some data in it
 * that requires processing, so find out what is in there and do the
 * appropriate read.
 *
 * The RXFIFO is a true FIFO, the packets coming out are still in packet
 * chunks, so if you have x packets received on an endpoint you'll get x
 * FIFO events delivered, each with a packet's worth of data in it.
 *
 * When using DMA, we should not be processing events from the RXFIFO
 * as the actual data should be sent to the memory directly and we turn
 * on the completion interrupts to get notifications of transfer completion.
 */
static void rtk_hsotg_handle_rx(struct rtk_hsotg *hsotg)
{
	u32 grxstsr = readl(hsotg->regs + GRXSTSP);
	u32 epnum, status, size;

	WARN_ON(using_dma(hsotg));

	epnum = grxstsr & GRXSTS_EPNum_MASK;
	status = grxstsr & GRXSTS_PktSts_MASK;

	size = grxstsr & GRXSTS_ByteCnt_MASK;
	size >>= GRXSTS_ByteCnt_SHIFT;

	if (1)
		dev_dbg(hsotg->dev, "%s: GRXSTSP=0x%08x (%d@%d)\n",
			__func__, grxstsr, size, epnum);

#define __status(x) ((x) >> GRXSTS_PktSts_SHIFT)

	switch (status >> GRXSTS_PktSts_SHIFT) {
	case __status(GRXSTS_PktSts_GlobalOutNAK):
		dev_dbg(hsotg->dev, "GlobalOutNAK\n");
		break;

	case __status(GRXSTS_PktSts_OutDone):
		dev_dbg(hsotg->dev, "OutDone (Frame=0x%08x)\n",
			rtk_hsotg_read_frameno(hsotg));

		if (!using_dma(hsotg))
			rtk_hsotg_handle_outdone(hsotg, epnum, false);
		break;

	case __status(GRXSTS_PktSts_SetupDone):
		dev_dbg(hsotg->dev,
			"SetupDone (Frame=0x%08x, DOPEPCTL=0x%08x)\n",
			rtk_hsotg_read_frameno(hsotg),
			readl(hsotg->regs + DOEPCTL(0)));

		rtk_hsotg_handle_outdone(hsotg, epnum, true);
		break;

	case __status(GRXSTS_PktSts_OutRX):
		rtk_hsotg_rx_data(hsotg, epnum, size);
		break;

	case __status(GRXSTS_PktSts_SetupRX):
		dev_dbg(hsotg->dev,
			"SetupRX (Frame=0x%08x, DOPEPCTL=0x%08x)\n",
			rtk_hsotg_read_frameno(hsotg),
			readl(hsotg->regs + DOEPCTL(0)));

		rtk_hsotg_rx_data(hsotg, epnum, size);
		break;

	default:
		dev_warn(hsotg->dev, "%s: unknown status %08x\n",
			 __func__, grxstsr);

		rtk_hsotg_dump(hsotg);
		break;
	}
}

/**
 * rtk_hsotg_ep0_mps - turn max packet size into register setting
 * @mps: The maximum packet size in bytes.
 */
static u32 rtk_hsotg_ep0_mps(unsigned int mps)
{
	switch (mps) {
	case 64:
		return D0EPCTL_MPS_64;
	case 32:
		return D0EPCTL_MPS_32;
	case 16:
		return D0EPCTL_MPS_16;
	case 8:
		return D0EPCTL_MPS_8;
	}

	/* bad max packet size, warn and return invalid result */
	WARN_ON(1);
	return (u32)-1;
}

/**
 * rtk_hsotg_set_ep_maxpacket - set endpoint's max-packet field
 * @hsotg: The driver state.
 * @ep: The index number of the endpoint
 * @mps: The maximum packet size in bytes
 *
 * Configure the maximum packet size for the given endpoint, updating
 * the hardware control registers to reflect this.
 */
static void rtk_hsotg_set_ep_maxpacket(struct rtk_hsotg *hsotg,
				       unsigned int ep, unsigned int mps)
{
	struct rtk_hsotg_ep *hs_ep = &hsotg->eps[ep];
	void __iomem *regs = hsotg->regs;
	u32 mpsval;
	u32 mcval;
	u32 reg;

	if (ep == 0) {
		/* EP0 is a special case */
		mpsval = rtk_hsotg_ep0_mps(mps);
		if (mpsval > 3)
			goto bad_mps;
		hs_ep->ep.maxpacket = mps;
		hs_ep->mc = 1;
	} else {
		mpsval = mps & DxEPCTL_MPS_MASK;
		if (mpsval > 1024)
			goto bad_mps;
		mcval = ((mps >> 11) & 0x3) + 1;
		hs_ep->mc = mcval;
		if (mcval > 3)
			goto bad_mps;
		hs_ep->ep.maxpacket = mpsval;
	}

	/*
	 * update both the in and out endpoint controldir_ registers, even
	 * if one of the directions may not be in use.
	 */

	reg = readl(regs + DIEPCTL(ep));
	reg &= ~DxEPCTL_MPS_MASK;
	reg |= mpsval;
	writel(reg, regs + DIEPCTL(ep));

	if (ep) {
		reg = readl(regs + DOEPCTL(ep));
		reg &= ~DxEPCTL_MPS_MASK;
		reg |= mpsval;
		writel(reg, regs + DOEPCTL(ep));
	}

	return;

bad_mps:
	dev_err(hsotg->dev, "ep%d: bad mps of %d\n", ep, mps);
}

/**
 * rtk_hsotg_txfifo_flush - flush Tx FIFO
 * @hsotg: The driver state
 * @idx: The index for the endpoint (0..15)
 */
static void rtk_hsotg_txfifo_flush(struct rtk_hsotg *hsotg, unsigned int idx)
{
	int timeout;
	int val;

	writel(GRSTCTL_TxFNum(idx) | GRSTCTL_TxFFlsh,
		hsotg->regs + GRSTCTL);

	/* wait until the fifo is flushed */
	timeout = 100;

	while (1) {
		val = readl(hsotg->regs + GRSTCTL);

		if ((val & (GRSTCTL_TxFFlsh)) == 0)
			break;

		if (--timeout == 0) {
			dev_err(hsotg->dev,
				"%s: timeout flushing fifo (GRSTCTL=%08x)\n",
				__func__, val);
		}

		udelay(1);
	}
}

/**
 * rtk_hsotg_trytx - check to see if anything needs transmitting
 * @hsotg: The driver state
 * @hs_ep: The driver endpoint to check.
 *
 * Check to see if there is a request that has data to send, and if so
 * make an attempt to write data into the FIFO.
 */
static int rtk_hsotg_trytx(struct rtk_hsotg *hsotg,
			   struct rtk_hsotg_ep *hs_ep)
{
	struct rtk_hsotg_req *hs_req = hs_ep->req;

	if (!hs_ep->dir_in || !hs_req) {
		/**
		 * if request is not enqueued, we disable interrupts
		 * for endpoints, excepting ep0
		 */
		if (hs_ep->index != 0)
			rtk_hsotg_ctrl_epint(hsotg, hs_ep->index,
					     hs_ep->dir_in, 0);
		return 0;
	}

	if (hs_req->req.actual < hs_req->req.length) {
		dev_dbg(hsotg->dev, "trying to write more for ep%d\n",
			hs_ep->index);
		return rtk_hsotg_write_fifo(hsotg, hs_ep, hs_req);
	}

	return 0;
}

/**
 * rtk_hsotg_complete_in - complete IN transfer
 * @hsotg: The device state.
 * @hs_ep: The endpoint that has just completed.
 *
 * An IN transfer has been completed, update the transfer's state and then
 * call the relevant completion routines.
 */
static void rtk_hsotg_complete_in(struct rtk_hsotg *hsotg,
				  struct rtk_hsotg_ep *hs_ep)
{
	struct rtk_hsotg_req *hs_req = hs_ep->req;
	u32 epsize = readl(hsotg->regs + DIEPTSIZ(hs_ep->index));
	int size_left, size_done;

	if (!hs_req) {
		dev_dbg(hsotg->dev, "XferCompl but no req\n");
		return;
	}

	/* Finish ZLP handling for IN EP0 transactions */
	if (hsotg->eps[0].sent_zlp) {
		dev_dbg(hsotg->dev, "zlp packet received\n");
		rtk_hsotg_complete_request(hsotg, hs_ep, hs_req, 0);
		return;
	}

	/*
	 * Calculate the size of the transfer by checking how much is left
	 * in the endpoint size register and then working it out from
	 * the amount we loaded for the transfer.
	 *
	 * We do this even for DMA, as the transfer may have incremented
	 * past the end of the buffer (DMA transfers are always 32bit
	 * aligned).
	 */

	size_left = DxEPTSIZ_XferSize_GET(epsize);

	size_done = hs_ep->size_loaded - size_left;
	size_done += hs_ep->last_load;

	if (hs_req->req.actual != size_done)
		dev_dbg(hsotg->dev, "%s: adjusting size done %d => %d\n",
			__func__, hs_req->req.actual, size_done);

	hs_req->req.actual = size_done;
	dev_dbg(hsotg->dev, "req->length:%d req->actual:%d req->zero:%d\n",
		hs_req->req.length, hs_req->req.actual, hs_req->req.zero);

	/*
	 * Check if dealing with Maximum Packet Size(MPS) IN transfer at EP0
	 * When sent data is a multiple MPS size (e.g. 64B ,128B ,192B
	 * ,256B ... ), after last MPS sized packet send IN ZLP packet to
	 * inform the host that no more data is available.
	 * The state of req.zero member is checked to be sure that the value to
	 * send is smaller than wValue expected from host.
	 * Check req.length to NOT send another ZLP when the current one is
	 * under completion (the one for which this completion has been called).
	 */
	if (hs_req->req.length && hs_ep->index == 0 && hs_req->req.zero &&
	    hs_req->req.length == hs_req->req.actual &&
	    !(hs_req->req.length % hs_ep->ep.maxpacket)) {

		dev_dbg(hsotg->dev, "ep0 zlp IN packet sent\n");
		rtk_hsotg_send_zlp(hsotg, hs_req);

		return;
	}

	if (!size_left && hs_req->req.actual < hs_req->req.length) {
		dev_dbg(hsotg->dev, "%s trying more for req...\n", __func__);
		rtk_hsotg_start_req(hsotg, hs_ep, hs_req, true);
	} else
		rtk_hsotg_complete_request(hsotg, hs_ep, hs_req, 0);
}

/**
 * rtk_hsotg_epint - handle an in/out endpoint interrupt
 * @hsotg: The driver state
 * @idx: The index for the endpoint (0..15)
 * @dir_in: Set if this is an IN endpoint
 *
 * Process and clear any interrupt pending for an individual endpoint
 */
static void rtk_hsotg_epint(struct rtk_hsotg *hsotg, unsigned int idx,
			    int dir_in)
{
	struct rtk_hsotg_ep *hs_ep = &hsotg->eps[idx];
	u32 epint_reg = dir_in ? DIEPINT(idx) : DOEPINT(idx);
	u32 epctl_reg = dir_in ? DIEPCTL(idx) : DOEPCTL(idx);
	u32 epsiz_reg = dir_in ? DIEPTSIZ(idx) : DOEPTSIZ(idx);
	u32 ints;
	u32 ctrl;

	ints = readl(hsotg->regs + epint_reg);
	ctrl = readl(hsotg->regs + epctl_reg);

	/* Clear endpoint interrupts */
	writel(ints, hsotg->regs + epint_reg);

	dev_dbg(hsotg->dev, "%s: ep%d(%s) DxEPINT=0x%08x\n",
		__func__, idx, dir_in ? "in" : "out", ints);

	if (ints & DxEPINT_XferCompl) {
		if (hs_ep->isochronous && hs_ep->interval == 1) {
			if (ctrl & DxEPCTL_EOFrNum)
				ctrl |= DxEPCTL_SetEvenFr;
			else
				ctrl |= DxEPCTL_SetOddFr;
			writel(ctrl, hsotg->regs + epctl_reg);
		}

		dev_dbg(hsotg->dev,
			"%s: XferCompl: DxEPCTL=0x%08x, DxEPTSIZ=%08x\n",
			__func__, readl(hsotg->regs + epctl_reg),
			readl(hsotg->regs + epsiz_reg));

		/*
		 * we get OutDone from the FIFO, so we only need to look
		 * at completing IN requests here
		 */
		if (dir_in) {
			rtk_hsotg_complete_in(hsotg, hs_ep);

			if (idx == 0 && !hs_ep->req)
				rtk_hsotg_enqueue_setup(hsotg);
		} else if (using_dma(hsotg)) {
			/*
			 * We're using DMA, we need to fire an OutDone here
			 * as we ignore the RXFIFO.
			 */

			rtk_hsotg_handle_outdone(hsotg, idx, false);
		}
	}

	if (ints & DxEPINT_EPDisbld) {
		dev_dbg(hsotg->dev, "%s: EPDisbld\n", __func__);

		if (dir_in) {
			int epctl = readl(hsotg->regs + epctl_reg);

			rtk_hsotg_txfifo_flush(hsotg, idx);

			if ((epctl & DxEPCTL_Stall) &&
				(epctl & DxEPCTL_EPType_Bulk)) {
				int dctl = readl(hsotg->regs + DCTL);

				dctl |= DCTL_CGNPInNAK;
				writel(dctl, hsotg->regs + DCTL);
			}
		}
	}

	if (ints & DxEPINT_AHBErr)
		dev_dbg(hsotg->dev, "%s: AHBErr\n", __func__);

	if (ints & DxEPINT_Setup) {  /* Setup or Timeout */
		dev_dbg(hsotg->dev, "%s: Setup/Timeout\n",  __func__);

		if (using_dma(hsotg) && idx == 0) {
			/*
			 * this is the notification we've received a
			 * setup packet. In non-DMA mode we'd get this
			 * from the RXFIFO, instead we need to process
			 * the setup here.
			 */

			if (dir_in)
				WARN_ON_ONCE(1);
			else
				rtk_hsotg_handle_outdone(hsotg, 0, true);
		}
	}

	if (ints & DxEPINT_Back2BackSetup)
		dev_dbg(hsotg->dev, "%s: B2BSetup/INEPNakEff\n", __func__);

	if (dir_in && !hs_ep->isochronous) {
		/* not sure if this is important, but we'll clear it anyway */
		if (ints & DIEPMSK_INTknTXFEmpMsk) {
			dev_dbg(hsotg->dev, "%s: ep%d: INTknTXFEmpMsk\n",
				__func__, idx);
		}

		/* this probably means something bad is happening */
		if (ints & DIEPMSK_INTknEPMisMsk) {
			dev_warn(hsotg->dev, "%s: ep%d: INTknEP\n",
				 __func__, idx);
		}

		/* FIFO has space or is empty (see GAHBCFG) */
		if (hsotg->dedicated_fifos &&
		    ints & DIEPMSK_TxFIFOEmpty) {
			dev_dbg(hsotg->dev, "%s: ep%d: TxFIFOEmpty\n",
				__func__, idx);
			if (!using_dma(hsotg))
				rtk_hsotg_trytx(hsotg, hs_ep);
		}
	}
}

/**
 * rtk_hsotg_irq_enumdone - Handle EnumDone interrupt (enumeration done)
 * @hsotg: The device state.
 *
 * Handle updating the device settings after the enumeration phase has
 * been completed.
 */
static void rtk_hsotg_irq_enumdone(struct rtk_hsotg *hsotg)
{
	u32 dsts = readl(hsotg->regs + DSTS);
	int ep0_mps = 0, ep_mps;

	/*
	 * This should signal the finish of the enumeration phase
	 * of the USB handshaking, so we should now know what rate
	 * we connected at.
	 */

	dev_dbg(hsotg->dev, "EnumDone (DSTS=0x%08x)\n", dsts);

	/*
	 * note, since we're limited by the size of transfer on EP0, and
	 * it seems IN transfers must be a even number of packets we do
	 * not advertise a 64byte MPS on EP0.
	 */

	/* catch both EnumSpd_FS and EnumSpd_FS48 */
	switch (dsts & DSTS_EnumSpd_MASK) {
	case DSTS_EnumSpd_FS:
	case DSTS_EnumSpd_FS48:
		hsotg->gadget.speed = USB_SPEED_FULL;
		ep0_mps = EP0_MPS_LIMIT;
		ep_mps = 1023;
	//	ep_mps = 64;	//barry
		break;

	case DSTS_EnumSpd_HS:
		hsotg->gadget.speed = USB_SPEED_HIGH;
		ep0_mps = EP0_MPS_LIMIT;
		ep_mps = 1024;
	//	ep_mps = 512;	//barry
		break;

	case DSTS_EnumSpd_LS:
		hsotg->gadget.speed = USB_SPEED_LOW;
		/*
		 * note, we don't actually support LS in this driver at the
		 * moment, and the documentation seems to imply that it isn't
		 * supported by the PHYs on some of the devices.
		 */
		break;
	}
	dev_info(hsotg->dev, "new device is %s\n",
		 usb_speed_string(hsotg->gadget.speed));

	/*
	 * we should now know the maximum packet size for an
	 * endpoint, so set the endpoints to a default value.
	 */

	if (ep0_mps) {
		int i;
		rtk_hsotg_set_ep_maxpacket(hsotg, 0, ep0_mps);
		for (i = 1; i < hsotg->num_of_eps; i++)
			rtk_hsotg_set_ep_maxpacket(hsotg, i, ep_mps);
	}

	/* ensure after enumeration our EP0 is active */

	rtk_hsotg_enqueue_setup(hsotg);

	dev_dbg(hsotg->dev, "EP0: DIEPCTL0=0x%08x, DOEPCTL0=0x%08x\n",
		readl(hsotg->regs + DIEPCTL0),
		readl(hsotg->regs + DOEPCTL0));
}

/**
 * kill_all_requests - remove all requests from the endpoint's queue
 * @hsotg: The device state.
 * @ep: The endpoint the requests may be on.
 * @result: The result code to use.
 * @force: Force removal of any current requests
 *
 * Go through the requests on the given endpoint and mark them
 * completed with the given result code.
 */
static void kill_all_requests(struct rtk_hsotg *hsotg,
			      struct rtk_hsotg_ep *ep,
			      int result, bool force)
{
	struct rtk_hsotg_req *req, *treq;

	list_for_each_entry_safe(req, treq, &ep->queue, queue) {
		/*
		 * currently, we can't do much about an already
		 * running request on an in endpoint
		 */

		if (ep->req == req && ep->dir_in && !force)
			continue;

		rtk_hsotg_complete_request(hsotg, ep, req,
					   result);
	}
	if(hsotg->dedicated_fifos)
		if ((readl(hsotg->regs + DTXFSTS(ep->index)) & 0xffff) * 4 < 3072)
			rtk_hsotg_txfifo_flush(hsotg, ep->index);
}

#define call_gadget(_hs, _entry) \
do { \
	if ((_hs)->gadget.speed != USB_SPEED_UNKNOWN &&	\
	    (_hs)->driver && (_hs)->driver->_entry) { \
		spin_unlock(&_hs->lock); \
		(_hs)->driver->_entry(&(_hs)->gadget); \
		spin_lock(&_hs->lock); \
	} \
} while (0)

/**
 * rtk_hsotg_disconnect - disconnect service
 * @hsotg: The device state.
 *
 * The device has been disconnected. Remove all current
 * transactions and signal the gadget driver that this
 * has happened.
 */
static void rtk_hsotg_disconnect(struct rtk_hsotg *hsotg)
{
	unsigned ep;

	for (ep = 0; ep < hsotg->num_of_eps; ep++)
		kill_all_requests(hsotg, &hsotg->eps[ep], -ESHUTDOWN, true);

	call_gadget(hsotg, disconnect);
}

/**
 * rtk_hsotg_irq_fifoempty - TX FIFO empty interrupt handler
 * @hsotg: The device state:
 * @periodic: True if this is a periodic FIFO interrupt
 */
static void rtk_hsotg_irq_fifoempty(struct rtk_hsotg *hsotg, bool periodic)
{
	struct rtk_hsotg_ep *ep;
	int epno, ret;

	/* look through for any more data to transmit */

	for (epno = 0; epno < hsotg->num_of_eps; epno++) {
		ep = &hsotg->eps[epno];

		if (!ep->dir_in)
			continue;

		if ((periodic && !ep->periodic) ||
		    (!periodic && ep->periodic))
			continue;

		ret = rtk_hsotg_trytx(hsotg, ep);
		if (ret < 0)
			break;
	}
}

/* IRQ flags which will trigger a retry around the IRQ loop */
#define IRQ_RETRY_MASK (GINTSTS_NPTxFEmp | \
			GINTSTS_PTxFEmp |  \
			GINTSTS_RxFLvl)

/**
 * rtk_hsotg_corereset - issue softreset to the core
 * @hsotg: The device state
 *
 * Issue a soft reset to the core, and await the core finishing it.
 */
static int rtk_hsotg_corereset(struct rtk_hsotg *hsotg)
{
	int timeout;
	u32 grstctl;

	dev_dbg(hsotg->dev, "resetting core\n");

	/* issue soft reset */
	writel(GRSTCTL_CSftRst, hsotg->regs + GRSTCTL);

	timeout = 10000;
	do {
		grstctl = readl(hsotg->regs + GRSTCTL);
	} while ((grstctl & GRSTCTL_CSftRst) && timeout-- > 0);

	if (grstctl & GRSTCTL_CSftRst) {
		dev_err(hsotg->dev, "Failed to get CSftRst asserted\n");
		return -EINVAL;
	}

	timeout = 10000;

	while (1) {
		u32 grstctl = readl(hsotg->regs + GRSTCTL);

		if (timeout-- < 0) {
			dev_info(hsotg->dev,
				 "%s: reset failed, GRSTCTL=%08x\n",
				 __func__, grstctl);
			return -ETIMEDOUT;
		}

		if (!(grstctl & GRSTCTL_AHBIdle))
			continue;

		break;		/* reset done */
	}

	dev_dbg(hsotg->dev, "reset successful\n");
	return 0;
}

/**
 * rtk_hsotg_core_init - issue softreset to the core
 * @hsotg: The device state
 *
 * Issue a soft reset to the core, and await the core finishing it.
 */
static void rtk_hsotg_core_init(struct rtk_hsotg *hsotg)
{
	rtk_hsotg_corereset(hsotg);

	/*
	 * we must now enable ep0 ready for host detection and then
	 * set configuration.
	 */

	/* set the PLL on, remove the HNP/SRP and set the PHY */
	writel(hsotg->phyif | GUSBCFG_TOutCal(7) |
	       (0x5 << 10), hsotg->regs + GUSBCFG);
	mdelay(100);

	rtk_hsotg_init_fifo(hsotg);

	__orr32(hsotg->regs + DCTL, DCTL_SftDiscon);

	writel(1 << 18 | DCFG_DevSpd_HS,  hsotg->regs + DCFG);

	/* Clear any pending OTG interrupts */
	writel(0xffffffff, hsotg->regs + GOTGINT);

	/* Clear any pending interrupts */
	writel(0xffffffff, hsotg->regs + GINTSTS);

	writel(GINTSTS_ErlySusp | GINTSTS_SessReqInt |
	       GINTSTS_GOUTNakEff | GINTSTS_GINNakEff |
	       GINTSTS_ConIDStsChng | GINTSTS_USBRst |
	       GINTSTS_EnumDone | GINTSTS_OTGInt |
	       GINTSTS_USBSusp | GINTSTS_WkUpInt,
	       hsotg->regs + GINTMSK);

	if (using_dma(hsotg))
		writel(GAHBCFG_GlblIntrEn | GAHBCFG_DMAEn |
		       GAHBCFG_HBstLen_Incr4,
		       hsotg->regs + GAHBCFG);
	else
		writel(((hsotg->dedicated_fifos) ? (GAHBCFG_NPTxFEmpLvl |
						    GAHBCFG_PTxFEmpLvl) : 0) |
		       GAHBCFG_GlblIntrEn,
		       hsotg->regs + GAHBCFG);

	/*
	 * If INTknTXFEmpMsk is enabled, it's important to disable ep interrupts
	 * when we have no data to transfer. Otherwise we get being flooded by
	 * interrupts.
	 */

	writel(((hsotg->dedicated_fifos) ? DIEPMSK_TxFIFOEmpty |
	       DIEPMSK_INTknTXFEmpMsk : 0) |
	       DIEPMSK_EPDisbldMsk | DIEPMSK_XferComplMsk |
	       DIEPMSK_TimeOUTMsk | DIEPMSK_AHBErrMsk |
	       DIEPMSK_INTknEPMisMsk,
	       hsotg->regs + DIEPMSK);

	/*
	 * don't need XferCompl, we get that from RXFIFO in slave mode. In
	 * DMA mode we may need this.
	 */
	writel((using_dma(hsotg) ? (DIEPMSK_XferComplMsk |
				    DIEPMSK_TimeOUTMsk) : 0) |
	       DOEPMSK_EPDisbldMsk | DOEPMSK_AHBErrMsk |
	       DOEPMSK_SetupMsk,
	       hsotg->regs + DOEPMSK);

	writel(0, hsotg->regs + DAINTMSK);

	dev_dbg(hsotg->dev, "EP0: DIEPCTL0=0x%08x, DOEPCTL0=0x%08x\n",
		readl(hsotg->regs + DIEPCTL0),
		readl(hsotg->regs + DOEPCTL0));

	/* enable in and out endpoint interrupts */
	rtk_hsotg_en_gsint(hsotg, GINTSTS_OEPInt | GINTSTS_IEPInt);

	/*
	 * Enable the RXFIFO when in slave mode, as this is how we collect
	 * the data. In DMA mode, we get events from the FIFO but also
	 * things we cannot process, so do not use it.
	 */
	if (!using_dma(hsotg))
		rtk_hsotg_en_gsint(hsotg, GINTSTS_RxFLvl);

	/* Enable interrupts for EP0 in and out */
	rtk_hsotg_ctrl_epint(hsotg, 0, 0, 1);
	rtk_hsotg_ctrl_epint(hsotg, 0, 1, 1);

	__orr32(hsotg->regs + DCTL, DCTL_PWROnPrgDone);
	udelay(10);  /* see openiboot */
	__bic32(hsotg->regs + DCTL, DCTL_PWROnPrgDone);

	dev_dbg(hsotg->dev, "DCTL=0x%08x\n", readl(hsotg->regs + DCTL));

	/*
	 * DxEPCTL_USBActEp says RO in manual, but seems to be set by
	 * writing to the EPCTL register..
	 */

	/* set to read 1 8byte packet */
	writel(DxEPTSIZ_MC(1) | DxEPTSIZ_PktCnt(1) |
	       DxEPTSIZ_XferSize(8), hsotg->regs + DOEPTSIZ0);

	writel(rtk_hsotg_ep0_mps(hsotg->eps[0].ep.maxpacket) |
	       DxEPCTL_CNAK | DxEPCTL_EPEna |
	       DxEPCTL_USBActEp,
	       hsotg->regs + DOEPCTL0);

	/* enable, but don't activate EP0in */
	writel(rtk_hsotg_ep0_mps(hsotg->eps[0].ep.maxpacket) |
	       DxEPCTL_USBActEp, hsotg->regs + DIEPCTL0);

	rtk_hsotg_enqueue_setup(hsotg);

	dev_dbg(hsotg->dev, "EP0: DIEPCTL0=0x%08x, DOEPCTL0=0x%08x\n",
		readl(hsotg->regs + DIEPCTL0),
		readl(hsotg->regs + DOEPCTL0));

	/* clear global NAKs */
	writel(DCTL_CGOUTNak | DCTL_CGNPInNAK,
	       hsotg->regs + DCTL);

	/* must be at-least 3ms to allow bus to see disconnect */
	mdelay(3);

	/* remove the soft-disconnect and let's go */
	__bic32(hsotg->regs + DCTL, DCTL_SftDiscon);
}

/**
 * rtk_hsotg_irq - handle device interrupt
 * @irq: The IRQ number triggered
 * @pw: The pw value when registered the handler.
 */
static irqreturn_t rtk_hsotg_irq(int irq, void *pw)
{
	struct rtk_hsotg *hsotg = pw;
	int retry_count = 8;
	u32 gintsts;
	u32 gintmsk;

	spin_lock(&hsotg->lock);
irq_retry:
	gintsts = readl(hsotg->regs + GINTSTS);
	gintmsk = readl(hsotg->regs + GINTMSK);

	dev_dbg(hsotg->dev, "%s: %08x %08x (%08x) retry %d\n",
		__func__, gintsts, gintsts & gintmsk, gintmsk, retry_count);

	gintsts &= gintmsk;

	if (gintsts & GINTSTS_OTGInt) {
		u32 otgint = readl(hsotg->regs + GOTGINT);

		dev_info(hsotg->dev, "OTGInt: %08x\n", otgint);

		writel(otgint, hsotg->regs + GOTGINT);
	}

	if (gintsts & GINTSTS_SessReqInt) {
		dev_dbg(hsotg->dev, "%s: SessReqInt\n", __func__);
		writel(GINTSTS_SessReqInt, hsotg->regs + GINTSTS);
	}

	if (gintsts & GINTSTS_EnumDone) {
		writel(GINTSTS_EnumDone, hsotg->regs + GINTSTS);

		rtk_hsotg_irq_enumdone(hsotg);
	}

	if (gintsts & GINTSTS_ConIDStsChng) {
		dev_dbg(hsotg->dev, "ConIDStsChg (DSTS=0x%08x, GOTCTL=%08x)\n",
			readl(hsotg->regs + DSTS),
			readl(hsotg->regs + GOTGCTL));

		writel(GINTSTS_ConIDStsChng, hsotg->regs + GINTSTS);
	}

	if (gintsts & (GINTSTS_OEPInt | GINTSTS_IEPInt)) {
		u32 daint = readl(hsotg->regs + DAINT);
		u32 daintmsk = readl(hsotg->regs + DAINTMSK);
		u32 daint_out, daint_in;
		int ep;

		daint &= daintmsk;
		daint_out = daint >> DAINT_OutEP_SHIFT;
		daint_in = daint & ~(daint_out << DAINT_OutEP_SHIFT);

		dev_dbg(hsotg->dev, "%s: daint=%08x\n", __func__, daint);

		for (ep = 0; ep < 15 && daint_out; ep++, daint_out >>= 1) {
			if (daint_out & 1)
				rtk_hsotg_epint(hsotg, ep, 0);
		}

		for (ep = 0; ep < 15 && daint_in; ep++, daint_in >>= 1) {
			if (daint_in & 1)
				rtk_hsotg_epint(hsotg, ep, 1);
		}
	}

	if (gintsts & GINTSTS_USBRst) {

		u32 usb_status = readl(hsotg->regs + GOTGCTL);

		dev_info(hsotg->dev, "%s: USBRst\n", __func__);
		dev_dbg(hsotg->dev, "GNPTXSTS=%08x\n",
			readl(hsotg->regs + GNPTXSTS));

		writel(GINTSTS_USBRst, hsotg->regs + GINTSTS);

		if (usb_status & GOTGCTL_BSESVLD) {
			if (time_after(jiffies, hsotg->last_rst +
				       msecs_to_jiffies(200))) {

				kill_all_requests(hsotg, &hsotg->eps[0],
							  -ECONNRESET, true);

				rtk_hsotg_core_init(hsotg);
				hsotg->last_rst = jiffies;
			}
		}
	}

	/* check both FIFOs */

	if (gintsts & GINTSTS_NPTxFEmp) {
		dev_dbg(hsotg->dev, "NPTxFEmp\n");

		/*
		 * Disable the interrupt to stop it happening again
		 * unless one of these endpoint routines decides that
		 * it needs re-enabling
		 */

		rtk_hsotg_disable_gsint(hsotg, GINTSTS_NPTxFEmp);
		rtk_hsotg_irq_fifoempty(hsotg, false);
	}

	if (gintsts & GINTSTS_PTxFEmp) {
		dev_dbg(hsotg->dev, "PTxFEmp\n");

		/* See note in GINTSTS_NPTxFEmp */

		rtk_hsotg_disable_gsint(hsotg, GINTSTS_PTxFEmp);
		rtk_hsotg_irq_fifoempty(hsotg, true);
	}

	if (gintsts & GINTSTS_RxFLvl) {
		/*
		 * note, since GINTSTS_RxFLvl doubles as FIFO-not-empty,
		 * we need to retry rtk_hsotg_handle_rx if this is still
		 * set.
		 */

		rtk_hsotg_handle_rx(hsotg);
	}

	if (gintsts & GINTSTS_ModeMis) {
		dev_warn(hsotg->dev, "warning, mode mismatch triggered\n");
		writel(GINTSTS_ModeMis, hsotg->regs + GINTSTS);
	}

	if (gintsts & GINTSTS_USBSusp) {
		dev_info(hsotg->dev, "GINTSTS_USBSusp\n");
		writel(GINTSTS_USBSusp, hsotg->regs + GINTSTS);

		call_gadget(hsotg, suspend);
	}

	if (gintsts & GINTSTS_WkUpInt) {
		dev_info(hsotg->dev, "GINTSTS_WkUpIn\n");
		writel(GINTSTS_WkUpInt, hsotg->regs + GINTSTS);

		call_gadget(hsotg, resume);
	}

	if (gintsts & GINTSTS_ErlySusp) {
		dev_dbg(hsotg->dev, "GINTSTS_ErlySusp\n");
		writel(GINTSTS_ErlySusp, hsotg->regs + GINTSTS);
	}

	/*
	 * these next two seem to crop-up occasionally causing the core
	 * to shutdown the USB transfer, so try clearing them and logging
	 * the occurrence.
	 */

	if (gintsts & GINTSTS_GOUTNakEff) {
		dev_info(hsotg->dev, "GOUTNakEff triggered\n");

		writel(DCTL_CGOUTNak, hsotg->regs + DCTL);

		rtk_hsotg_dump(hsotg);
	}

	if (gintsts & GINTSTS_GINNakEff) {
		dev_info(hsotg->dev, "GINNakEff triggered\n");

		writel(DCTL_CGNPInNAK, hsotg->regs + DCTL);

		rtk_hsotg_dump(hsotg);
	}

	/*
	 * if we've had fifo events, we should try and go around the
	 * loop again to see if there's any point in returning yet.
	 */

	if (gintsts & IRQ_RETRY_MASK && --retry_count > 0)
			goto irq_retry;

	spin_unlock(&hsotg->lock);

	return IRQ_HANDLED;
}

/**
 * rtk_hsotg_ep_enable - enable the given endpoint
 * @ep: The USB endpint to configure
 * @desc: The USB endpoint descriptor to configure with.
 *
 * This is called from the USB gadget code's usb_ep_enable().
 */
static int rtk_hsotg_ep_enable(struct usb_ep *ep,
			       const struct usb_endpoint_descriptor *desc)
{
	struct rtk_hsotg_ep *hs_ep = our_ep(ep);
	struct rtk_hsotg *hsotg = hs_ep->parent;
	unsigned long flags;
	int index = hs_ep->index;
	u32 epctrl_reg;
	u32 epctrl;
	u32 mps;
	int dir_in;
	int ret = 0;

	dev_dbg(hsotg->dev,
		"%s: ep %s: a 0x%02x, attr 0x%02x, mps 0x%04x, intr %d\n",
		__func__, ep->name, desc->bEndpointAddress, desc->bmAttributes,
		desc->wMaxPacketSize, desc->bInterval);

	/* not to be called for EP0 */
	WARN_ON(index == 0);

	dir_in = (desc->bEndpointAddress & USB_ENDPOINT_DIR_MASK) ? 1 : 0;
	if (dir_in != hs_ep->dir_in) {
		dev_err(hsotg->dev, "%s: direction mismatch!\n", __func__);
		return -EINVAL;
	}

	mps = usb_endpoint_maxp(desc);

	/* note, we handle this here instead of rtk_hsotg_set_ep_maxpacket */

	epctrl_reg = dir_in ? DIEPCTL(index) : DOEPCTL(index);
	epctrl = readl(hsotg->regs + epctrl_reg);

	dev_dbg(hsotg->dev, "%s: read DxEPCTL=0x%08x from 0x%08x\n",
		__func__, epctrl, epctrl_reg);

	spin_lock_irqsave(&hsotg->lock, flags);

	epctrl &= ~(DxEPCTL_EPType_MASK | DxEPCTL_MPS_MASK);
	epctrl |= DxEPCTL_MPS(mps);

	/*
	 * mark the endpoint as active, otherwise the core may ignore
	 * transactions entirely for this endpoint
	 */
	epctrl |= DxEPCTL_USBActEp;

	/*
	 * set the NAK status on the endpoint, otherwise we might try and
	 * do something with data that we've yet got a request to process
	 * since the RXFIFO will take data for an endpoint even if the
	 * size register hasn't been set.
	 */

	epctrl |= DxEPCTL_SNAK;

	/* update the endpoint state */
	rtk_hsotg_set_ep_maxpacket(hsotg, hs_ep->index, mps);

	/* default, set to non-periodic */
	hs_ep->isochronous = 0;
	hs_ep->periodic = 0;
	hs_ep->halted = 0;
	hs_ep->interval = desc->bInterval;

	if (hs_ep->interval > 1 && hs_ep->mc > 1)
		dev_err(hsotg->dev, "MC > 1 when interval is not 1\n");

	switch (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) {
	case USB_ENDPOINT_XFER_ISOC:
		epctrl |= DxEPCTL_EPType_Iso;
		epctrl |= DxEPCTL_SetEvenFr;
		hs_ep->isochronous = 1;
		if (dir_in)
			hs_ep->periodic = 1;
		break;

	case USB_ENDPOINT_XFER_BULK:
		epctrl |= DxEPCTL_EPType_Bulk;
		break;

	case USB_ENDPOINT_XFER_INT:
		if (dir_in) {
			/*
			 * Allocate our TxFNum by simply using the index
			 * of the endpoint for the moment. We could do
			 * something better if the host indicates how
			 * many FIFOs we are expecting to use.
			 */

			hs_ep->periodic = 1;
			epctrl |= DxEPCTL_TxFNum(index);
		}

		epctrl |= DxEPCTL_EPType_Intterupt;
		break;

	case USB_ENDPOINT_XFER_CONTROL:
		epctrl |= DxEPCTL_EPType_Control;
		break;
	}

	/*
	 * if the hardware has dedicated fifos, we must give each IN EP
	 * a unique tx-fifo even if it is non-periodic.
	 */
	if (dir_in && hsotg->dedicated_fifos)
		epctrl |= DxEPCTL_TxFNum(index);

	/* for non control endpoints, set PID to D0 */
	if (index)
		epctrl |= DxEPCTL_SetD0PID;

	dev_dbg(hsotg->dev, "%s: write DxEPCTL=0x%08x\n",
		__func__, epctrl);

	writel(epctrl, hsotg->regs + epctrl_reg);
	dev_dbg(hsotg->dev, "%s: read DxEPCTL=0x%08x\n",
		__func__, readl(hsotg->regs + epctrl_reg));

	/* enable the endpoint interrupt */
	rtk_hsotg_ctrl_epint(hsotg, index, dir_in, 1);

	spin_unlock_irqrestore(&hsotg->lock, flags);
	return ret;
}

/**
 * rtk_hsotg_ep_disable - disable given endpoint
 * @ep: The endpoint to disable.
 */
static int rtk_hsotg_ep_disable(struct usb_ep *ep)
{
	struct rtk_hsotg_ep *hs_ep = our_ep(ep);
	struct rtk_hsotg *hsotg = hs_ep->parent;
	int dir_in = hs_ep->dir_in;
	int index = hs_ep->index;
	unsigned long flags;
	u32 epctrl_reg;
	u32 ctrl;

	dev_info(hsotg->dev, "%s(ep %p)\n", __func__, ep);

	if (ep == &hsotg->eps[0].ep) {
		dev_err(hsotg->dev, "%s: called for ep0\n", __func__);
		return -EINVAL;
	}

	epctrl_reg = dir_in ? DIEPCTL(index) : DOEPCTL(index);

	spin_lock_irqsave(&hsotg->lock, flags);
	/* terminate all requests with shutdown */
	kill_all_requests(hsotg, hs_ep, -ESHUTDOWN, false);


	ctrl = readl(hsotg->regs + epctrl_reg);
	ctrl &= ~DxEPCTL_EPEna;
	ctrl &= ~DxEPCTL_USBActEp;
	ctrl |= DxEPCTL_SNAK;

	dev_dbg(hsotg->dev, "%s: DxEPCTL=0x%08x\n", __func__, ctrl);
	writel(ctrl, hsotg->regs + epctrl_reg);

	/* disable endpoint interrupts */
	rtk_hsotg_ctrl_epint(hsotg, hs_ep->index, hs_ep->dir_in, 0);

	spin_unlock_irqrestore(&hsotg->lock, flags);
	return 0;
}

/**
 * on_list - check request is on the given endpoint
 * @ep: The endpoint to check.
 * @test: The request to test if it is on the endpoint.
 */
static bool on_list(struct rtk_hsotg_ep *ep, struct rtk_hsotg_req *test)
{
	struct rtk_hsotg_req *req, *treq;

	list_for_each_entry_safe(req, treq, &ep->queue, queue) {
		if (req == test)
			return true;
	}

	return false;
}

/**
 * rtk_hsotg_ep_dequeue - dequeue given endpoint
 * @ep: The endpoint to dequeue.
 * @req: The request to be removed from a queue.
 */
static int rtk_hsotg_ep_dequeue(struct usb_ep *ep, struct usb_request *req)
{
	struct rtk_hsotg_req *hs_req = our_req(req);
	struct rtk_hsotg_ep *hs_ep = our_ep(ep);
	struct rtk_hsotg *hs = hs_ep->parent;
	unsigned long flags;

	dev_info(hs->dev, "ep_dequeue(%p,%p)\n", ep, req);

	spin_lock_irqsave(&hs->lock, flags);

	if (!on_list(hs_ep, hs_req)) {
		spin_unlock_irqrestore(&hs->lock, flags);
		return -EINVAL;
	}

	rtk_hsotg_complete_request(hs, hs_ep, hs_req, -ECONNRESET);
	spin_unlock_irqrestore(&hs->lock, flags);

	return 0;
}

/**
 * rtk_hsotg_ep_sethalt - set halt on a given endpoint
 * @ep: The endpoint to set halt.
 * @value: Set or unset the halt.
 */
static int rtk_hsotg_ep_sethalt(struct usb_ep *ep, int value)
{
	struct rtk_hsotg_ep *hs_ep = our_ep(ep);
	struct rtk_hsotg *hs = hs_ep->parent;
	int index = hs_ep->index;
	u32 epreg;
	u32 epctl;
	u32 xfertype;

	dev_info(hs->dev, "%s(ep %p %s, %d)\n", __func__, ep, ep->name, value);

	if (index == 0) {
		if (value)
			rtk_hsotg_stall_ep0(hs);
		else
			dev_warn(hs->dev,
				 "%s: can't clear halt on ep0\n", __func__);
		return 0;
	}

	/* write both IN and OUT control registers */

	epreg = DIEPCTL(index);
	epctl = readl(hs->regs + epreg);

	if (value) {
		epctl |= DxEPCTL_Stall + DxEPCTL_SNAK;
		if (epctl & DxEPCTL_EPEna)
			epctl |= DxEPCTL_EPDis;
	} else {
		epctl &= ~DxEPCTL_Stall;
		xfertype = epctl & DxEPCTL_EPType_MASK;
		if (xfertype == DxEPCTL_EPType_Bulk ||
			xfertype == DxEPCTL_EPType_Intterupt)
				epctl |= DxEPCTL_SetD0PID;
	}

	writel(epctl, hs->regs + epreg);

	epreg = DOEPCTL(index);
	epctl = readl(hs->regs + epreg);

	if (value)
		epctl |= DxEPCTL_Stall;
	else {
		epctl &= ~DxEPCTL_Stall;
		xfertype = epctl & DxEPCTL_EPType_MASK;
		if (xfertype == DxEPCTL_EPType_Bulk ||
			xfertype == DxEPCTL_EPType_Intterupt)
				epctl |= DxEPCTL_SetD0PID;
	}

	writel(epctl, hs->regs + epreg);

	hs_ep->halted = value;

	return 0;
}

/**
 * rtk_hsotg_ep_sethalt_lock - set halt on a given endpoint with lock held
 * @ep: The endpoint to set halt.
 * @value: Set or unset the halt.
 */
static int rtk_hsotg_ep_sethalt_lock(struct usb_ep *ep, int value)
{
	struct rtk_hsotg_ep *hs_ep = our_ep(ep);
	struct rtk_hsotg *hs = hs_ep->parent;
	unsigned long flags = 0;
	int ret = 0;

	spin_lock_irqsave(&hs->lock, flags);
	ret = rtk_hsotg_ep_sethalt(ep, value);
	spin_unlock_irqrestore(&hs->lock, flags);

	return ret;
}

static struct usb_ep_ops rtk_hsotg_ep_ops = {
	.enable		= rtk_hsotg_ep_enable,
	.disable	= rtk_hsotg_ep_disable,
	.alloc_request	= rtk_hsotg_ep_alloc_request,
	.free_request	= rtk_hsotg_ep_free_request,
	.queue		= rtk_hsotg_ep_queue_lock,
	.dequeue	= rtk_hsotg_ep_dequeue,
	.set_halt	= rtk_hsotg_ep_sethalt_lock,
	/* note, don't believe we have any call for the fifo routines */
};

/**
 * rtk_hsotg_phy_enable - enable platform phy dev
 * @hsotg: The driver state
 *
 * A wrapper for platform code responsible for controlling
 * low-level USB code
 */
static void rtk_hsotg_phy_enable(struct rtk_hsotg *hsotg)
{
	struct platform_device *pdev = to_platform_device(hsotg->dev);

	if (hsotg->otg_type == RTK_DEVICE_MODE && hsotg->phyregs) {
		writel(readl(hsotg->phyregs+0xc)|BIT(0), hsotg->phyregs+0xc);
		mdelay(100);
		writel(readl(hsotg->phyregs+0x2c)|BIT(0), hsotg->phyregs+0x2c);
		mdelay(100);
		writel(readl(hsotg->phyregs+0x40)|BIT(0), hsotg->phyregs+0x40);
		writel(readl(hsotg->phyregs+0x44)|0x180, hsotg->phyregs+0x44);
		mdelay(100);
	}
}

/**
 * rtk_hsotg_phy_disable - disable platform phy dev
 * @hsotg: The driver state
 *
 * A wrapper for platform code responsible for controlling
 * low-level USB code
 */
static void rtk_hsotg_phy_disable(struct rtk_hsotg *hsotg)
{
#if 0	//barry
	struct platform_device *pdev = to_platform_device(hsotg->dev);

	if (hsotg->phy) {
		phy_power_off(hsotg->phy);
		phy_exit(hsotg->phy);
	} else if (hsotg->uphy)
		usb_phy_shutdown(hsotg->uphy);
	else if (hsotg->plat->phy_exit)
		hsotg->plat->phy_exit(pdev, hsotg->plat->phy_type);
#endif
#ifdef CONFIG_USB_OTG
	if(hsotg->phyregs)
		writel(readl(hsotg->phyregs+0x40)&~BIT(0), hsotg->phyregs+0x40);
#endif
}

/**
 * rtk_hsotg_init - initalize the usb core
 * @hsotg: The driver state
 */
static void rtk_hsotg_init(struct rtk_hsotg *hsotg)
{
	/* unmask subset of endpoint interrupts */

	writel(DIEPMSK_TimeOUTMsk | DIEPMSK_AHBErrMsk |
	       DIEPMSK_EPDisbldMsk | DIEPMSK_XferComplMsk,
	       hsotg->regs + DIEPMSK);

	writel(DOEPMSK_SetupMsk | DOEPMSK_AHBErrMsk |
	       DOEPMSK_EPDisbldMsk | DOEPMSK_XferComplMsk,
	       hsotg->regs + DOEPMSK);

	writel(0, hsotg->regs + DAINTMSK);

	/* Be in disconnected state until gadget is registered */
	__orr32(hsotg->regs + DCTL, DCTL_SftDiscon);

	if (0) {
		/* post global nak until we're ready */
		writel(DCTL_SGNPInNAK | DCTL_SGOUTNak,
		       hsotg->regs + DCTL);
	}

	/* setup fifos */

	dev_dbg(hsotg->dev, "GRXFSIZ=0x%08x, GNPTXFSIZ=0x%08x\n",
		readl(hsotg->regs + GRXFSIZ),
		readl(hsotg->regs + GNPTXFSIZ));

	rtk_hsotg_init_fifo(hsotg);

	/* set the PLL on, remove the HNP/SRP and set the PHY */
	writel(GUSBCFG_PHYIf16 | GUSBCFG_TOutCal(7) | (0x5 << 10),
	       hsotg->regs + GUSBCFG);
//	writel(hsotg->phyif | GUSBCFG_TOutCal(7) | (0x5 << 10),		//barry
//	       hsotg->regs + GUSBCFG);
	mdelay(100);

	writel(using_dma(hsotg) ? GAHBCFG_DMAEn : 0x0,
	       hsotg->regs + GAHBCFG);
}

/**
 * rtk_hsotg_udc_start - prepare the udc for work
 * @gadget: The usb gadget state
 * @driver: The usb gadget driver
 *
 * Perform initialization to prepare udc device and driver
 * to work.
 */
static int rtk_hsotg_udc_start(struct usb_gadget *gadget,
			   struct usb_gadget_driver *driver)
{
	struct rtk_hsotg *hsotg = to_hsotg(gadget);
	int ret;

	if (!hsotg) {
		pr_err("%s: called with no device\n", __func__);
		return -ENODEV;
	}

	if (!driver) {
		dev_err(hsotg->dev, "%s: no driver\n", __func__);
		return -EINVAL;
	}

	if (driver->max_speed < USB_SPEED_FULL)
		dev_err(hsotg->dev, "%s: bad speed\n", __func__);

	if (!driver->setup) {
		dev_err(hsotg->dev, "%s: missing entry points\n", __func__);
		return -EINVAL;
	}

	WARN_ON(hsotg->driver);

	driver->driver.bus = NULL;
	hsotg->driver = driver;
	hsotg->gadget.dev.of_node = hsotg->dev->of_node;
	hsotg->gadget.speed = USB_SPEED_UNKNOWN;
#if 0
	ret = regulator_bulk_enable(ARRAY_SIZE(hsotg->supplies),
				    hsotg->supplies);
	if (ret) {
		dev_err(hsotg->dev, "failed to enable supplies: %d\n", ret);
		goto err;
	}
#endif
	hsotg->last_rst = jiffies;
	dev_info(hsotg->dev, "bound driver %s\n", driver->driver.name);
	return 0;

err:
	hsotg->driver = NULL;
	return ret;
}

/**
 * rtk_hsotg_udc_stop - stop the udc
 * @gadget: The usb gadget state
 * @driver: The usb gadget driver
 *
 * Stop udc hw block and stay tunned for future transmissions
 */
static int rtk_hsotg_udc_stop(struct usb_gadget *gadget,
			  struct usb_gadget_driver *driver)
{
	struct rtk_hsotg *hsotg = to_hsotg(gadget);
	unsigned long flags = 0;
	int ep;

	if (!hsotg)
		return -ENODEV;

	/* all endpoints should be shutdown */
	for (ep = 0; ep < hsotg->num_of_eps; ep++)
		rtk_hsotg_ep_disable(&hsotg->eps[ep].ep);

	spin_lock_irqsave(&hsotg->lock, flags);

	rtk_hsotg_phy_disable(hsotg);

	if (!driver)
		hsotg->driver = NULL;

	hsotg->gadget.speed = USB_SPEED_UNKNOWN;

	spin_unlock_irqrestore(&hsotg->lock, flags);
#if 0
	regulator_bulk_disable(ARRAY_SIZE(hsotg->supplies), hsotg->supplies);
#endif
	return 0;
}

/**
 * rtk_hsotg_gadget_getframe - read the frame number
 * @gadget: The usb gadget state
 *
 * Read the {micro} frame number
 */
static int rtk_hsotg_gadget_getframe(struct usb_gadget *gadget)
{
	return rtk_hsotg_read_frameno(to_hsotg(gadget));
}

/**
 * rtk_hsotg_pullup - connect/disconnect the USB PHY
 * @gadget: The usb gadget state
 * @is_on: Current state of the USB PHY
 *
 * Connect/Disconnect the USB PHY pullup
 */
static int rtk_hsotg_pullup(struct usb_gadget *gadget, int is_on)
{
	struct rtk_hsotg *hsotg = to_hsotg(gadget);
	unsigned long flags = 0;

	dev_dbg(hsotg->dev, "%s: is_in: %d\n", __func__, is_on);

	spin_lock_irqsave(&hsotg->lock, flags);
	if (is_on) {
		rtk_hsotg_phy_enable(hsotg);
		rtk_hsotg_core_init(hsotg);
	} else {
		rtk_hsotg_disconnect(hsotg);
		rtk_hsotg_phy_disable(hsotg);
	}

	hsotg->gadget.speed = USB_SPEED_UNKNOWN;
	spin_unlock_irqrestore(&hsotg->lock, flags);

	return 0;
}

static const struct usb_gadget_ops rtk_hsotg_gadget_ops = {
	.get_frame	= rtk_hsotg_gadget_getframe,
	.udc_start		= rtk_hsotg_udc_start,
	.udc_stop		= rtk_hsotg_udc_stop,
	.pullup                 = rtk_hsotg_pullup,
};

/**
 * rtk_hsotg_initep - initialise a single endpoint
 * @hsotg: The device state.
 * @hs_ep: The endpoint to be initialised.
 * @epnum: The endpoint number
 *
 * Initialise the given endpoint (as part of the probe and device state
 * creation) to give to the gadget driver. Setup the endpoint name, any
 * direction information and other state that may be required.
 */
static void rtk_hsotg_initep(struct rtk_hsotg *hsotg,
				       struct rtk_hsotg_ep *hs_ep,
				       int epnum)
{
	u32 ptxfifo;
	char *dir;

	if (epnum == 0)
		dir = "";
	else if ((epnum % 2) == 0) {
		dir = "out";
	} else {
		dir = "in";
		hs_ep->dir_in = 1;
	}

	hs_ep->index = epnum;

	snprintf(hs_ep->name, sizeof(hs_ep->name), "ep%d%s", epnum, dir);

	INIT_LIST_HEAD(&hs_ep->queue);
	INIT_LIST_HEAD(&hs_ep->ep.ep_list);

	/* add to the list of endpoints known by the gadget driver */
	if (epnum)
		list_add_tail(&hs_ep->ep.ep_list, &hsotg->gadget.ep_list);

	hs_ep->parent = hsotg;
	hs_ep->ep.name = hs_ep->name;
	usb_ep_set_maxpacket_limit(&hs_ep->ep, epnum ? 1024 : EP0_MPS_LIMIT);
	hs_ep->ep.ops = &rtk_hsotg_ep_ops;

	/*
	 * Read the FIFO size for the Periodic TX FIFO, even if we're
	 * an OUT endpoint, we may as well do this if in future the
	 * code is changed to make each endpoint's direction changeable.
	 */

	ptxfifo = readl(hsotg->regs + DPTXFSIZn(epnum));
	hs_ep->fifo_size = DPTXFSIZn_DPTxFSize_GET(ptxfifo) * 4;

	/*
	 * if we're using dma, we need to set the next-endpoint pointer
	 * to be something valid.
	 */

	if (using_dma(hsotg)) {
		u32 next = DxEPCTL_NextEp((epnum + 1) % 15);
		writel(next, hsotg->regs + DIEPCTL(epnum));
		writel(next, hsotg->regs + DOEPCTL(epnum));
	}
}

/**
 * rtk_hsotg_hw_cfg - read HW configuration registers
 * @param: The device state
 *
 * Read the USB core HW configuration registers
 */
static void rtk_hsotg_hw_cfg(struct rtk_hsotg *hsotg)
{
	u32 cfg2, cfg4;
	/* check hardware configuration */

#ifdef CONFIG_USB_OTG
	hsotg->num_of_eps = 5;
	hsotg->dedicated_fifos = 0;
#else
	cfg2 = readl(hsotg->regs + 0x48);
	hsotg->num_of_eps = (cfg2 >> 10) & 0xF;

	dev_info(hsotg->dev, "EPs:%d\n", hsotg->num_of_eps);

	cfg4 = readl(hsotg->regs + 0x50);
	hsotg->dedicated_fifos = (cfg4 >> 25) & 1;
#endif
	dev_info(hsotg->dev, "%s fifos\n",
		 hsotg->dedicated_fifos ? "dedicated" : "shared");
}

/**
 * rtk_hsotg_dump - dump state of the udc
 * @param: The device state
 */
static void rtk_hsotg_dump(struct rtk_hsotg *hsotg)
{
#ifdef DEBUG
	struct device *dev = hsotg->dev;
	void __iomem *regs = hsotg->regs;
	u32 val;
	int idx;

	dev_info(dev, "DCFG=0x%08x, DCTL=0x%08x, DIEPMSK=%08x\n",
		 readl(regs + DCFG), readl(regs + DCTL),
		 readl(regs + DIEPMSK));

	dev_info(dev, "GAHBCFG=0x%08x, 0x44=0x%08x\n",
		 readl(regs + GAHBCFG), readl(regs + 0x44));

	dev_info(dev, "GRXFSIZ=0x%08x, GNPTXFSIZ=0x%08x\n",
		 readl(regs + GRXFSIZ), readl(regs + GNPTXFSIZ));

	/* show periodic fifo settings */

	for (idx = 1; idx <= 15; idx++) {
		val = readl(regs + DPTXFSIZn(idx));
		dev_info(dev, "DPTx[%d] FSize=%d, StAddr=0x%08x\n", idx,
			 val >> DPTXFSIZn_DPTxFSize_SHIFT,
			 val & DPTXFSIZn_DPTxFStAddr_MASK);
	}

	for (idx = 0; idx < 15; idx++) {
		dev_info(dev,
			 "ep%d-in: EPCTL=0x%08x, SIZ=0x%08x, DMA=0x%08x\n", idx,
			 readl(regs + DIEPCTL(idx)),
			 readl(regs + DIEPTSIZ(idx)),
			 readl(regs + DIEPDMA(idx)));

		val = readl(regs + DOEPCTL(idx));
		dev_info(dev,
			 "ep%d-out: EPCTL=0x%08x, SIZ=0x%08x, DMA=0x%08x\n",
			 idx, readl(regs + DOEPCTL(idx)),
			 readl(regs + DOEPTSIZ(idx)),
			 readl(regs + DOEPDMA(idx)));

	}

	dev_info(dev, "DVBUSDIS=0x%08x, DVBUSPULSE=%08x\n",
		 readl(regs + DVBUSDIS), readl(regs + DVBUSPULSE));
#endif
}

/**
 * state_show - debugfs: show overall driver and device state.
 * @seq: The seq file to write to.
 * @v: Unused parameter.
 *
 * This debugfs entry shows the overall state of the hardware and
 * some general information about each of the endpoints available
 * to the system.
 */
static int state_show(struct seq_file *seq, void *v)
{
	struct rtk_hsotg *hsotg = seq->private;
	void __iomem *regs = hsotg->regs;
	int idx;

	seq_printf(seq, "DCFG=0x%08x, DCTL=0x%08x, DSTS=0x%08x\n",
		 readl(regs + DCFG),
		 readl(regs + DCTL),
		 readl(regs + DSTS));

	seq_printf(seq, "DIEPMSK=0x%08x, DOEPMASK=0x%08x\n",
		   readl(regs + DIEPMSK), readl(regs + DOEPMSK));

	seq_printf(seq, "GINTMSK=0x%08x, GINTSTS=0x%08x\n",
		   readl(regs + GINTMSK),
		   readl(regs + GINTSTS));

	seq_printf(seq, "DAINTMSK=0x%08x, DAINT=0x%08x\n",
		   readl(regs + DAINTMSK),
		   readl(regs + DAINT));

	seq_printf(seq, "GNPTXSTS=0x%08x, GRXSTSR=%08x\n",
		   readl(regs + GNPTXSTS),
		   readl(regs + GRXSTSR));

	seq_puts(seq, "\nEndpoint status:\n");

	for (idx = 0; idx < 15; idx++) {
		u32 in, out;

		in = readl(regs + DIEPCTL(idx));
		out = readl(regs + DOEPCTL(idx));

		seq_printf(seq, "ep%d: DIEPCTL=0x%08x, DOEPCTL=0x%08x",
			   idx, in, out);

		in = readl(regs + DIEPTSIZ(idx));
		out = readl(regs + DOEPTSIZ(idx));

		seq_printf(seq, ", DIEPTSIZ=0x%08x, DOEPTSIZ=0x%08x",
			   in, out);

		seq_puts(seq, "\n");
	}

	return 0;
}

static int state_open(struct inode *inode, struct file *file)
{
	return single_open(file, state_show, inode->i_private);
}

static const struct file_operations state_fops = {
	.owner		= THIS_MODULE,
	.open		= state_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/**
 * fifo_show - debugfs: show the fifo information
 * @seq: The seq_file to write data to.
 * @v: Unused parameter.
 *
 * Show the FIFO information for the overall fifo and all the
 * periodic transmission FIFOs.
 */
static int fifo_show(struct seq_file *seq, void *v)
{
	struct rtk_hsotg *hsotg = seq->private;
	void __iomem *regs = hsotg->regs;
	u32 val;
	int idx;

	seq_puts(seq, "Non-periodic FIFOs:\n");
	seq_printf(seq, "RXFIFO: Size %d\n", readl(regs + GRXFSIZ));

	val = readl(regs + GNPTXFSIZ);
	seq_printf(seq, "NPTXFIFO: Size %d, Start 0x%08x\n",
		   val >> GNPTXFSIZ_NPTxFDep_SHIFT,
		   val & GNPTXFSIZ_NPTxFStAddr_MASK);

	seq_puts(seq, "\nPeriodic TXFIFOs:\n");

	for (idx = 1; idx <= 15; idx++) {
		val = readl(regs + DPTXFSIZn(idx));

		seq_printf(seq, "\tDPTXFIFO%2d: Size %d, Start 0x%08x\n", idx,
			   val >> DPTXFSIZn_DPTxFSize_SHIFT,
			   val & DPTXFSIZn_DPTxFStAddr_MASK);
	}

	return 0;
}

static int fifo_open(struct inode *inode, struct file *file)
{
	return single_open(file, fifo_show, inode->i_private);
}

static const struct file_operations fifo_fops = {
	.owner		= THIS_MODULE,
	.open		= fifo_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};


static const char *decode_direction(int is_in)
{
	return is_in ? "in" : "out";
}

/**
 * ep_show - debugfs: show the state of an endpoint.
 * @seq: The seq_file to write data to.
 * @v: Unused parameter.
 *
 * This debugfs entry shows the state of the given endpoint (one is
 * registered for each available).
 */
static int ep_show(struct seq_file *seq, void *v)
{
	struct rtk_hsotg_ep *ep = seq->private;
	struct rtk_hsotg *hsotg = ep->parent;
	struct rtk_hsotg_req *req;
	void __iomem *regs = hsotg->regs;
	int index = ep->index;
	int show_limit = 15;
	unsigned long flags;

	seq_printf(seq, "Endpoint index %d, named %s,  dir %s:\n",
		   ep->index, ep->ep.name, decode_direction(ep->dir_in));

	/* first show the register state */

	seq_printf(seq, "\tDIEPCTL=0x%08x, DOEPCTL=0x%08x\n",
		   readl(regs + DIEPCTL(index)),
		   readl(regs + DOEPCTL(index)));

	seq_printf(seq, "\tDIEPDMA=0x%08x, DOEPDMA=0x%08x\n",
		   readl(regs + DIEPDMA(index)),
		   readl(regs + DOEPDMA(index)));

	seq_printf(seq, "\tDIEPINT=0x%08x, DOEPINT=0x%08x\n",
		   readl(regs + DIEPINT(index)),
		   readl(regs + DOEPINT(index)));

	seq_printf(seq, "\tDIEPTSIZ=0x%08x, DOEPTSIZ=0x%08x\n",
		   readl(regs + DIEPTSIZ(index)),
		   readl(regs + DOEPTSIZ(index)));

	seq_puts(seq, "\n");
	seq_printf(seq, "mps %d\n", ep->ep.maxpacket);
	seq_printf(seq, "total_data=%ld\n", ep->total_data);

	seq_printf(seq, "request list (%p,%p):\n",
		   ep->queue.next, ep->queue.prev);

	spin_lock_irqsave(&hsotg->lock, flags);

	list_for_each_entry(req, &ep->queue, queue) {
		if (--show_limit < 0) {
			seq_puts(seq, "not showing more requests...\n");
			break;
		}

		seq_printf(seq, "%c req %p: %d bytes @%p, ",
			   req == ep->req ? '*' : ' ',
			   req, req->req.length, req->req.buf);
		seq_printf(seq, "%d done, res %d\n",
			   req->req.actual, req->req.status);
	}

	spin_unlock_irqrestore(&hsotg->lock, flags);

	return 0;
}

static int ep_open(struct inode *inode, struct file *file)
{
	return single_open(file, ep_show, inode->i_private);
}

static const struct file_operations ep_fops = {
	.owner		= THIS_MODULE,
	.open		= ep_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/**
 * rtk_hsotg_create_debug - create debugfs directory and files
 * @hsotg: The driver state
 *
 * Create the debugfs files to allow the user to get information
 * about the state of the system. The directory name is created
 * with the same name as the device itself, in case we end up
 * with multiple blocks in future systems.
 */
static void rtk_hsotg_create_debug(struct rtk_hsotg *hsotg)
{
	struct dentry *root;
	unsigned epidx;

	root = debugfs_create_dir(dev_name(hsotg->dev), NULL);
	hsotg->debug_root = root;
	if (IS_ERR(root)) {
		dev_err(hsotg->dev, "cannot create debug root\n");
		return;
	}

	/* create general state file */

	hsotg->debug_file = debugfs_create_file("state", 0444, root,
						hsotg, &state_fops);

	if (IS_ERR(hsotg->debug_file))
		dev_err(hsotg->dev, "%s: failed to create state\n", __func__);

	hsotg->debug_fifo = debugfs_create_file("fifo", 0444, root,
						hsotg, &fifo_fops);

	if (IS_ERR(hsotg->debug_fifo))
		dev_err(hsotg->dev, "%s: failed to create fifo\n", __func__);

	/* create one file for each endpoint */

	for (epidx = 0; epidx < hsotg->num_of_eps; epidx++) {
		struct rtk_hsotg_ep *ep = &hsotg->eps[epidx];

		ep->debugfs = debugfs_create_file(ep->name, 0444,
						  root, ep, &ep_fops);

		if (IS_ERR(ep->debugfs))
			dev_err(hsotg->dev, "failed to create %s debug file\n",
				ep->name);
	}
}

/**
 * rtk_hsotg_delete_debug - cleanup debugfs entries
 * @hsotg: The driver state
 *
 * Cleanup (remove) the debugfs files for use on module exit.
 */
static void rtk_hsotg_delete_debug(struct rtk_hsotg *hsotg)
{
	unsigned epidx;

	for (epidx = 0; epidx < hsotg->num_of_eps; epidx++) {
		struct rtk_hsotg_ep *ep = &hsotg->eps[epidx];
		debugfs_remove(ep->debugfs);
	}

	debugfs_remove(hsotg->debug_file);
	debugfs_remove(hsotg->debug_fifo);
	debugfs_remove(hsotg->debug_root);
}

#ifdef CONFIG_USB_RTK_UDC_OTG_SWITCH
static int rtk_hsotg_gadget_suspend(struct platform_device *pdev)
{
	struct rtk_hsotg *hsotg = platform_get_drvdata(pdev);
	unsigned long flags;
	int ret = 0;

	if (hsotg->driver)
		dev_info(hsotg->dev, "suspending usb gadget %s\n",
			 hsotg->driver->driver.name);

	spin_lock_irqsave(&hsotg->lock, flags);
	rtk_hsotg_disconnect(hsotg);
	rtk_hsotg_phy_disable(hsotg);
	hsotg->gadget.speed = USB_SPEED_UNKNOWN;
	spin_unlock_irqrestore(&hsotg->lock, flags);

	if (hsotg->driver) {
		int ep;
		for (ep = 0; ep < hsotg->num_of_eps; ep++)
			rtk_hsotg_ep_disable(&hsotg->eps[ep].ep);
	}

	return ret;
}

static int rtk_hsotg_gadget_resume(struct platform_device *pdev)
{
	struct rtk_hsotg *hsotg = platform_get_drvdata(pdev);
	unsigned long flags;
	int ret = 0;

	if (hsotg->driver) {
		dev_info(hsotg->dev, "resuming usb gadget %s\n",
			 hsotg->driver->driver.name);
	}

	spin_lock_irqsave(&hsotg->lock, flags);
	hsotg->last_rst = jiffies;
	rtk_hsotg_phy_enable(hsotg);
	rtk_hsotg_core_init(hsotg);
	spin_unlock_irqrestore(&hsotg->lock, flags);

	return ret;
}

static int switch_to_device_mode(struct rtk_hsotg *hsotg)
{
	struct platform_device *pdev = to_platform_device(hsotg->dev);
	return rtk_hsotg_gadget_resume(pdev);
}

static int switch_to_host_mode(struct rtk_hsotg *hsotg)
{
	struct platform_device *pdev = to_platform_device(hsotg->dev);
	int ret;

	ret = rtk_hsotg_gadget_suspend(pdev);

	if(hsotg->phyregs) {
		writel(readl(hsotg->phyregs+0xc)|BIT(0), hsotg->phyregs+0xc);
		mdelay(100);
		writel(readl(hsotg->phyregs+0x2c)|BIT(0), hsotg->phyregs+0x2c);
		mdelay(100);
		writel(readl(hsotg->phyregs+0x40) & ~BIT(0), hsotg->phyregs+0x40);
		writel(readl(hsotg->phyregs+0x44) & ~0x180, hsotg->phyregs+0x44);
		mdelay(100);
	}
	return ret;
}

static ssize_t switch_mode_show(struct device *dev, struct device_attribute *attr, char *buffer)
{
	struct rtk_hsotg *hsotg = dev_get_drvdata(dev);

	return sprintf(buffer, "%s\n", hsotg->otg_type?"In device mode":"In host mode");
}

static ssize_t switch_mode_store(struct device *dev, struct device_attribute *attr,
		const char *buffer, size_t size)
{
	struct rtk_hsotg *hsotg = dev_get_drvdata(dev);
	int ret;
	char str[8];

	ret = sscanf(buffer, "%7s", str);
	if (ret != 1)
		return -EINVAL;

	if (!strcmp(str, "device")) {
		hsotg->otg_type = RTK_DEVICE_MODE;
		dev_info(hsotg->dev, "switch to device mode\n");
		switch_to_device_mode(hsotg);
	} else if (!strcmp(str, "host")) {
		hsotg->otg_type = RTK_HOST_MODE;
		dev_info(hsotg->dev, "switch to host mode\n");
		switch_to_host_mode(hsotg);
	} else {
		dev_err(hsotg->dev, "ERROR switch to %s mode\n", str);
	}
	return size;
}

static DEVICE_ATTR(switch_mode, 0644, switch_mode_show, switch_mode_store);

static const struct attribute *switch_mode_attrs[] = {
	&dev_attr_switch_mode.attr,
	NULL
};

static int rtk_hsotg_create_sysfs_by_otg(struct rtk_hsotg *hsotg)
{
	struct device *dev = hsotg->dev;
	int retval;

	hsotg->otg_type = RTK_HOST_MODE;
	dev_info(hsotg->dev, "switch to host mode\n");
	switch_to_host_mode(hsotg);

	retval = sysfs_create_files(&dev->kobj,
			(const struct attribute **)switch_mode_attrs);

	return retval;
}
#endif // CONFIG_USB_RTK_UDC_OTG_SWITCH

/**
 * rtk_hsotg_probe - probe function for hsotg driver
 * @pdev: The platform information for the driver
 */
static int rtk_hsotg_probe(struct platform_device *pdev)
{
	struct rtk_hsotg_plat *plat = dev_get_platdata(&pdev->dev);
//	struct phy *phy;
	struct usb_phy *uphy;
	struct device *dev = &pdev->dev;
	struct rtk_hsotg_ep *eps;
	struct rtk_hsotg *hsotg;
	struct resource *res;
	int epnum;
	int ret;
//	int i;

	hsotg = devm_kzalloc(&pdev->dev, sizeof(struct rtk_hsotg), GFP_KERNEL);
	if (!hsotg) {
		dev_err(dev, "cannot get memory\n");
		return -ENOMEM;
	}

	/*
	 * Attempt to find a generic PHY, then look for an old style
	 * USB PHY, finally fall back to pdata
	 */
	uphy = devm_usb_get_phy_by_phandle(dev, "usb-phy", 0);
	if (IS_ERR(uphy)) {
		/* Fallback for pdata */
		plat = dev_get_platdata(&pdev->dev);
		if (!plat) {
			dev_err(&pdev->dev, "no platform data or transceiver defined\n");
			return -EPROBE_DEFER;
		}
		hsotg->plat = plat;
	} else {
		hsotg->uphy = uphy;
		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		hsotg->phyregs = devm_ioremap_nocache(dev, res->start, resource_size(res));
	}

	hsotg->dev = dev;
#if 0	//barry
	hsotg->clk = devm_clk_get(&pdev->dev, "otg");
	if (IS_ERR(hsotg->clk)) {
		dev_err(dev, "cannot get otg clock\n");
		return PTR_ERR(hsotg->clk);
	}
#endif
	platform_set_drvdata(pdev, hsotg);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	hsotg->regs = devm_ioremap_nocache(dev, res->start, resource_size(res));
	//hsotg->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hsotg->regs)) {
		ret = PTR_ERR(hsotg->regs);
		goto err_clk;
	}

	ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err(dev, "cannot find IRQ\n");
		goto err_clk;
	}

	spin_lock_init(&hsotg->lock);

	hsotg->irq = ret;

	ret = devm_request_irq(&pdev->dev, hsotg->irq, rtk_hsotg_irq, IRQF_SHARED,
				dev_name(dev), hsotg);
	if (ret < 0) {
		dev_err(dev, "cannot claim IRQ\n");
		goto err_clk;
	}

	dev_info(dev, "regs %p, irq %d\n", hsotg->regs, hsotg->irq);

	hsotg->gadget.max_speed = USB_SPEED_HIGH;
	hsotg->gadget.ops = &rtk_hsotg_gadget_ops;
	hsotg->gadget.name = dev_name(dev);

	/* reset the system */
#if 0	//barry
	clk_prepare_enable(hsotg->clk);

	/* regulators */

	for (i = 0; i < ARRAY_SIZE(hsotg->supplies); i++)
		hsotg->supplies[i].supply = rtk_hsotg_supply_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(hsotg->supplies),
				 hsotg->supplies);
	if (ret) {
		dev_err(dev, "failed to request supplies: %d\n", ret);
		goto err_clk;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(hsotg->supplies),
				    hsotg->supplies);

	if (ret) {
		dev_err(hsotg->dev, "failed to enable supplies: %d\n", ret);
		goto err_supplies;
	}
#endif
	/* Set default UTMI width */
	//hsotg->phyif = GUSBCFG_PHYIf16;
	hsotg->phyif = GUSBCFG_PHYIf16|GUSBCFG_FORCEDEVMODE;
	//hsotg->phyif = GUSBCFG_PHYIf8|GUSBCFG_FORCEDEVMODE;

	/* usb phy enable */
	//	rtk_usb_phy_init();
	rtk_hsotg_phy_enable(hsotg);
	rtk_hsotg_corereset(hsotg);
	rtk_hsotg_init(hsotg);
	rtk_hsotg_hw_cfg(hsotg);

	/* hsotg->num_of_eps holds number of EPs other than ep0 */

	if (hsotg->num_of_eps == 0) {
		dev_err(dev, "wrong number of EPs (zero)\n");
		ret = -EINVAL;
		goto err_supplies;
	}

	eps = kcalloc(hsotg->num_of_eps + 1, sizeof(struct rtk_hsotg_ep),
		      GFP_KERNEL);
	if (!eps) {
		dev_err(dev, "cannot get memory\n");
		ret = -ENOMEM;
		goto err_supplies;
	}

	hsotg->eps = eps;

	/* setup endpoint information */

	INIT_LIST_HEAD(&hsotg->gadget.ep_list);
	hsotg->gadget.ep0 = &hsotg->eps[0].ep;

	/* allocate EP0 request */

	hsotg->ctrl_req = rtk_hsotg_ep_alloc_request(&hsotg->eps[0].ep,
						     GFP_KERNEL);
	if (!hsotg->ctrl_req) {
		dev_err(dev, "failed to allocate ctrl req\n");
		ret = -ENOMEM;
		goto err_ep_mem;
	}

	/* initialise the endpoints now the core has been initialised */
	for (epnum = 0; epnum < hsotg->num_of_eps; epnum++)
		rtk_hsotg_initep(hsotg, &hsotg->eps[epnum], epnum);

	/* disable power and clock */
#if 0
	ret = regulator_bulk_disable(ARRAY_SIZE(hsotg->supplies),
				    hsotg->supplies);
	if (ret) {
		dev_err(hsotg->dev, "failed to disable supplies: %d\n", ret);
		goto err_ep_mem;
	}
#endif
	rtk_hsotg_phy_disable(hsotg);

	ret = usb_add_gadget_udc(&pdev->dev, &hsotg->gadget);
	if (ret)
		goto err_ep_mem;

	rtk_hsotg_create_debug(hsotg);

#ifdef CONFIG_USB_RTK_UDC_OTG_SWITCH
	rtk_hsotg_create_sysfs_by_otg(hsotg);
#endif // CONFIG_USB_RTK_UDC_OTG_SWITCH

	rtk_hsotg_dump(hsotg);

#ifdef CONFIG_USB_OTG
	otg_set_peripheral(hsotg->uphy->otg, &hsotg->gadget);
#endif
	return 0;

err_ep_mem:
	kfree(eps);
err_supplies:
	rtk_hsotg_phy_disable(hsotg);
err_clk:
	clk_disable_unprepare(hsotg->clk);

	return ret;
}

/**
 * rtk_hsotg_remove - remove function for hsotg driver
 * @pdev: The platform information for the driver
 */
static int rtk_hsotg_remove(struct platform_device *pdev)
{
	struct rtk_hsotg *hsotg = platform_get_drvdata(pdev);

	usb_del_gadget_udc(&hsotg->gadget);

	rtk_hsotg_delete_debug(hsotg);

	if (hsotg->driver) {
		/* should have been done already by driver model core */
		usb_gadget_unregister_driver(hsotg->driver);
	}

	rtk_hsotg_phy_disable(hsotg);
#if 0	//barry
	if (hsotg->phy)
		phy_exit(hsotg->phy);
	clk_disable_unprepare(hsotg->clk);
#endif
	return 0;
}

#if 0	//barry
static int rtk_hsotg_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct rtk_hsotg *hsotg = platform_get_drvdata(pdev);
	unsigned long flags;
	int ret = 0;

	if (hsotg->driver)
		dev_info(hsotg->dev, "suspending usb gadget %s\n",
			 hsotg->driver->driver.name);

	spin_lock_irqsave(&hsotg->lock, flags);
	rtk_hsotg_disconnect(hsotg);
	rtk_hsotg_phy_disable(hsotg);
	hsotg->gadget.speed = USB_SPEED_UNKNOWN;
	spin_unlock_irqrestore(&hsotg->lock, flags);

	if (hsotg->driver) {
		int ep;
		for (ep = 0; ep < hsotg->num_of_eps; ep++)
			rtk_hsotg_ep_disable(&hsotg->eps[ep].ep);

		ret = regulator_bulk_disable(ARRAY_SIZE(hsotg->supplies),
					     hsotg->supplies);
	}

	return ret;
}

static int rtk_hsotg_resume(struct platform_device *pdev)
{
	struct rtk_hsotg *hsotg = platform_get_drvdata(pdev);
	unsigned long flags;
	int ret = 0;

	if (hsotg->driver) {
		dev_info(hsotg->dev, "resuming usb gadget %s\n",
			 hsotg->driver->driver.name);
		ret = regulator_bulk_enable(ARRAY_SIZE(hsotg->supplies),
				      hsotg->supplies);
	}

	spin_lock_irqsave(&hsotg->lock, flags);
	hsotg->last_rst = jiffies;
	rtk_hsotg_phy_enable(hsotg);
	rtk_hsotg_core_init(hsotg);
	spin_unlock_irqrestore(&hsotg->lock, flags);

	return ret;
}
#else
#define rtk_hsotg_suspend NULL
#define rtk_hsotg_resume NULL
#endif

#ifdef CONFIG_OF
static const struct of_device_id rtk_hsotg_of_ids[] = {
	{ .compatible = "Realtek,rtd129x-usb2-udc" },
	{ .compatible = "snps,dwc2", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rtk_hsotg_of_ids);
#endif

#ifdef CONFIG_USB_OTG
static int rtk_hsotg_otg_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct rtk_hsotg *hsotg = platform_get_drvdata(pdev);
	rtk_hsotg_phy_disable(hsotg);
	return 0;
}
static int rtk_hsotg_otg_resume(struct platform_device *pdev)
{
	struct rtk_hsotg *hsotg = platform_get_drvdata(pdev);
	rtk_hsotg_phy_enable(hsotg);
//	rtk_hsotg_core_init(hsotg);
	return 0;
}
#else
#define rtk_hsotg_otg_suspend NULL
#define rtk_hsotg_otg_resume NULL
#endif

static struct platform_driver rtk_hsotg_driver = {
	.driver		= {
		.name	= "rtk-hsotg",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(rtk_hsotg_of_ids),
#ifdef CONFIG_USB_OTG
		.suspend = rtk_hsotg_otg_suspend,
		.resume  = rtk_hsotg_otg_resume,
#endif
	},
	.probe		= rtk_hsotg_probe,
	.remove		= rtk_hsotg_remove,
	.suspend	= rtk_hsotg_suspend,
	.resume		= rtk_hsotg_resume,
};

module_platform_driver(rtk_hsotg_driver);

MODULE_DESCRIPTION("Realtek USB High-speed/OtG device");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rtk-hsotg");
