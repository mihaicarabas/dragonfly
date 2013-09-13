/*******************************************************************************

Copyright (c) 2006-2013, Myricom Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.

 2. Neither the name of the Myricom Inc, nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

$FreeBSD: head/sys/dev/mxge/if_mxge_var.h 247160 2013-02-22 19:23:33Z gallatin $

***************************************************************************/

#define MXGE_ETH_STOPPED		0
#define MXGE_ETH_STOPPING		1
#define MXGE_ETH_STARTING		2
#define MXGE_ETH_RUNNING		3
#define MXGE_ETH_OPEN_FAILED		4

#define MXGE_FW_OFFSET			(1024*1024)
#define MXGE_EEPROM_STRINGS_SIZE	256
#define MXGE_MAX_SEND_DESC		128

#define MXGE_VIRT_JUMBOS		0

typedef struct {
	void *addr;
	bus_addr_t bus_addr;
	bus_dma_tag_t dmat;
	bus_dmamap_t map;
} mxge_dma_t;

typedef struct {
	mcp_slot_t *entry;
	mxge_dma_t dma;
	int cnt;
	int idx;
	int mask;
} mxge_rx_done_t;

typedef struct {
	uint32_t data0;
	uint32_t data1;
	uint32_t data2;
} mxge_cmd_t;

struct mxge_rx_buffer_state {
	struct mbuf *m;
	bus_dmamap_t map;
};

struct mxge_tx_buffer_state {
	struct mbuf *m;
	bus_dmamap_t map;
	int flag;
};

typedef struct {
	volatile mcp_kreq_ether_recv_t *lanai;	/* lanai ptr for recv ring */
	mcp_kreq_ether_recv_t *shadow;	/* host shadow of recv ring */
	struct mxge_rx_buffer_state *info;
	bus_dma_tag_t dmat;
	bus_dmamap_t extra_map;
	int cnt;
	int nbufs;
	int cl_size;
	int alloc_fail;
	int mask;			/* number of rx slots -1 */
	int mlen;
} mxge_rx_ring_t;

typedef struct {
#ifdef IFNET_BUF_RING
	struct buf_ring *br;
#endif
	volatile mcp_kreq_ether_send_t *lanai;	/* lanai ptr for sendq	*/
	volatile uint32_t *send_go;		/* doorbell for sendq */
	volatile uint32_t *send_stop;		/* doorbell for sendq */
	mcp_kreq_ether_send_t *req_list;	/* host shadow of sendq */
	char *req_bytes;
	bus_dma_segment_t *seg_list;
	struct mxge_tx_buffer_state *info;
	bus_dma_tag_t dmat;
	int req;			/* transmits submitted	*/
	int mask;			/* number of transmit slots -1 */
	int done;			/* transmits completed	*/
	int pkt_done;			/* packets completed */
	int max_desc;			/* max descriptors per xmit */
	int queue_active;		/* fw currently polling this queue*/
	int activate;
	int deactivate;
	int stall;			/* #times hw queue exhausted */
	int wake;			/* #times irq re-enabled xmit */
	int watchdog_req;		/* cache of req */
	int watchdog_done;		/* cache of done */
	int watchdog_rx_pause;		/* cache of pause rq recvd */
	int defrag;
} mxge_tx_ring_t;

struct mxge_softc;
typedef struct mxge_softc mxge_softc_t;

struct mxge_slice_state {
	mxge_softc_t *sc;
	mxge_tx_ring_t tx;		/* transmit ring */
	mxge_rx_ring_t rx_small;
	mxge_rx_ring_t rx_big;
	mxge_rx_done_t rx_done;
	mcp_irq_data_t *fw_stats;
	volatile uint32_t *irq_claim;
	u_long ipackets;
	u_long opackets;
	u_long obytes;
	u_long omcasts;
	u_long oerrors;
	int if_drv_flags;
	mxge_dma_t fw_stats_dma;
	struct sysctl_oid *sysctl_tree;
	struct sysctl_ctx_list sysctl_ctx;
	char scratch[256];
};

struct mxge_softc {
	struct arpcom arpcom;
	struct ifnet* ifp;		/* points to arpcom.ac_if */
	struct mxge_slice_state *ss;
	int tx_boundary;		/* boundary transmits cannot cross*/
	bus_dma_tag_t	parent_dmat;
	volatile uint8_t *sram;
	int sram_size;
	volatile uint32_t *irq_deassert;
	mcp_cmd_response_t *cmd;
	mxge_dma_t cmd_dma;
	mxge_dma_t zeropad_dma;
	struct pci_dev *pdev;
	int irq_rid;
	int irq_type;
	int link_state;
	unsigned int rdma_tags_available;
	int intr_coal_delay;
	volatile uint32_t *intr_coal_delay_ptr;
	int wc;
	int wake_queue;
	int stop_queue;
	int down_cnt;
	int watchdog_resets;
	int watchdog_countdown;
	int pause;
	struct resource *mem_res;
	struct resource *irq_res;
	struct resource **msix_irq_res;
	struct resource *msix_table_res;
	struct resource *msix_pba_res;
	void *ih; 
	void **msix_ih;
	char *fw_name;
	char eeprom_strings[MXGE_EEPROM_STRINGS_SIZE];
	char fw_version[128];
	int fw_ver_major;
	int fw_ver_minor;
	int fw_ver_tiny;
	int adopted_rx_filter_bug;
	device_t dev;
	struct ifmedia media;
	int read_dma;
	int write_dma;
	int read_write_dma;
	int fw_multicast_support;
	int link_width;
	int max_mtu;
	int throttle;
	int tx_defrag;
	int media_flags;
	int need_media_probe;
	int num_slices;
	int rx_ring_size;
	int dying;
	int connector;
	int current_media;
	mxge_dma_t dmabench_dma;
	struct callout co_hdl;
	struct sysctl_ctx_list sysctl_ctx;
	struct sysctl_oid *sysctl_tree;
	struct sysctl_oid *slice_sysctl_tree;
	struct sysctl_ctx_list slice_sysctl_ctx;
	char *mac_addr_string;
	uint8_t	mac_addr[6];		/* eeprom mac address */
	uint16_t pectl;			/* save PCIe CTL state */
	char product_code_string[64];
	char serial_number_string[64];
};

#define MXGE_PCI_VENDOR_MYRICOM 	0x14c1
#define MXGE_PCI_DEVICE_Z8E 		0x0008
#define MXGE_PCI_DEVICE_Z8E_9 		0x0009
#define MXGE_PCI_REV_Z8E		0
#define MXGE_PCI_REV_Z8ES		1

#define MXGE_XFP_COMPLIANCE_BYTE	131
#define MXGE_SFP_COMPLIANCE_BYTE	3

#define MXGE_MIN_THROTTLE		416
#define MXGE_MAX_THROTTLE		4096

/* Types of connectors on NICs supported by this driver */
#define MXGE_CX4 0
#define MXGE_XFP 1
#define MXGE_SFP 2
#define MXGE_QRF 3

#define MXGE_HIGHPART_TO_U32(X) \
	(sizeof (X) == 8) ? ((uint32_t)((uint64_t)(X) >> 32)) : (0)
#define MXGE_LOWPART_TO_U32(X) ((uint32_t)(X))

struct mxge_media_type {
	int flag;
	uint8_t bitmask;
	char *name;
};

#if defined(__GNUC__)
#if defined(__i386__) || defined(__x86_64__)
#define wmb()  __asm__ __volatile__ ("sfence;": : :"memory")
#else
#error "unknown arch"
#endif
#else
#error "unknown compiler"
#endif

static __inline void
mxge_pio_copy(volatile void *to_v, void *from_v, size_t size)
{
	register volatile uintptr_t *to;
	volatile uintptr_t *from;
	size_t i;

	to = (volatile uintptr_t *)to_v;
	from = from_v;
	for (i = (size / sizeof(uintptr_t)); i; i--) {
		*to = *from;
		to++;
		from++;
	}
}
