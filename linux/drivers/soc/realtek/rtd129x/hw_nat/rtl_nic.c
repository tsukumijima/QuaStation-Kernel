/*
 * Copyright c                Realtek Semiconductor Corporation, 2003
 * All rights reserved.
 *
 * $Header: /home/cvsroot/linux-2.6.19/linux-2.6.x/drivers/net/re865x/rtl_nic.c,v 1.22 2008/04/11 10:49:14 bo_zhao Exp $
 *
 * $Author: bo_zhao $
 *
 * Abstract: Pure L2 NIC driver, without RTL865X's advanced L3/4 features.
 *
 *   re865x_nic.c: NIC driver for the RealTek 865*
 *
 */

#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)
#define DRV_RELDATE		"Jan 27, 2014"
#include <linux/kconfig.h>
#else
#define DRV_RELDATE		"Mar 25, 2004"
#include <linux/config.h>
#endif
//#include <linux/autoconf.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/if_vlan.h>
#include <linux/crc32.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/signal.h>
#include <linux/proc_fs.h>
#include <linux/time.h>
#include <linux/rtc.h>
#if !defined(CONFIG_RTD_1295_HWNAT)
#include <bsp/bspchip.h>
#endif //!defined(CONFIG_RTD_1295_HWNAT)
#include <linux/timer.h>
#if defined(CONFIG_RTK_VLAN_SUPPORT)
#include <net/rtl/rtk_vlan.h>
#endif

#ifdef CONFIG_RTL_VLAN_8021Q
#include <linux/if_vlan.h>
#endif

#if	defined(CONFIG_RTL8196_RTL8366) && defined(CONFIG_RTL_IGMP_SNOOPING)
#undef	CONFIG_RTL_IGMP_SNOOPING
#endif

//if you need to use the fake eth driver, please also disable the "CONFIG_RTL_ETH_PRIV_SK" in kernel
//#define CONFIG_RTK_FAKE_ETH 1

#include "version.h"
#include <net/rtl/rtl_types.h>
#include <net/rtl/rtl_glue.h>

#include "AsicDriver/asicRegs.h"
#include "AsicDriver/rtl865x_asicCom.h"
#include "AsicDriver/rtl865x_asicL2.h"
#ifdef CONFIG_RTL_LAYERED_ASIC_DRIVER_L3
#include "AsicDriver/rtl865x_asicL3.h"
#endif

#include "common/mbuf.h"
#include <net/rtl/rtl_queue.h>
#include "common/rtl_errno.h"
#include "rtl865xc_swNic.h"

/*common*/
#include "common/rtl865x_vlan.h"
#include <net/rtl/rtl865x_netif.h>
#include "common/rtl865x_netif_local.h"

/*l2*/
#ifdef CONFIG_RTL_LAYERED_DRIVER_L2
#include "l2Driver/rtl865x_fdb.h"
#include <net/rtl/rtl865x_fdb_api.h>
#endif

/*l3*/
#ifdef CONFIG_RTL_LAYERED_DRIVER_L3
#include "l3Driver/rtl865x_ip.h"
#include "l3Driver/rtl865x_nexthop.h"
#include <net/rtl/rtl865x_ppp.h>
#include "l3Driver/rtl865x_ppp_local.h"
#include "l3Driver/rtl865x_route.h"
#include "l3Driver/rtl865x_arp.h"
#include <net/rtl/rtl865x_nat.h>
#endif

#if defined(CONFIG_RTL_HARDWARE_IPV6_SUPPORT)
#include "l3Driver/rtl8198c_nexthopIpv6.h"
#include "l3Driver/rtl8198c_routeIpv6.h"
#include "l3Driver/rtl8198c_arpIpv6.h"
#endif

#include "rtl865xc_swNic.h"

/*l4*/
#ifdef	CONFIG_RTL865X_ROMEPERF
#include "romeperf.h"
#endif
#include <net/rtl/rtl_nic.h>
#if defined(CONFIG_RTL_FASTBRIDGE)
#include <net/rtl/features/fast_bridge.h>
#endif
#if defined(CONFIG_RTL_HW_QOS_SUPPORT) && defined(CONFIG_NET_SCHED) && defined(CONFIG_RTL_LAYERED_DRIVER)
#include <net/rtl/rtl865x_outputQueue.h>
#endif

#ifdef CONFIG_RTL_STP
#include <net/rtl/rtk_stp.h>
#endif

#if defined(CONFIG_RTL_HW_STP)
#include <net/rtl/rtk_stp.h>
#endif

#if defined(CONFIG_RTL_8196C_ESD) || defined(CONFIG_RTL_8198_ESD)
#include <linux/reboot.h>
#endif

#if defined(CONFIG_RTL_8367R_SUPPORT)
#include "rtl8367r/rtk_types.h"
#endif

#if defined(CONFIG_RTL_SWITCH_NEW_DESCRIPTOR)
#include "rtl819x_swNic.h"
#endif

#if defined (CONFIG_RTL_IGMP_SNOOPING)||defined(CONFIG_BRIDGE_IGMP_SNOOPING)
#include <net/rtl/rtl865x_igmpsnooping.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#if defined (CONFIG_RTL_MLD_SNOOPING)||defined(CONFIG_BRIDGE_IGMP_SNOOPING)
#if defined(CONFIG_RTD_1295_HWNAT)
#include <uapi/linux/ipv6.h>
#else //defined(CONFIG_RTD_1295_HWNAT)
#include <linux/ipv6.h>
#endif //defined(CONFIG_RTD_1295_HWNAT)
int mldSnoopEnabled;
#endif
uint32 nicIgmpModuleIndex=0xFFFFFFFF;
extern int  igmpsnoopenabled;
extern uint32 brIgmpModuleIndex;
#if defined (CONFIG_RTL_HARDWARE_MULTICAST)
extern struct net_bridge *bridge0;
extern uint32 br0SwFwdPortMask;
#if defined (CONFIG_RTL_HW_MCAST_WIFI)
extern int hwwifiEnable;
#endif
#endif
#endif

#if defined(CONFIG_RTD_1295_HWNAT)
#define MODULENAME "rtd1295-hwnat"
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_net.h>
#include <linux/of_address.h>
#include <soc/realtek/rtd129x_cpu.h>
#include "AsicDriver/rtl865x_asicBasic.h"

#define RTL_DEBUG	1
#ifdef RTL_DEBUG
#define DBG(fmt, ...) printk(KERN_ERR "%s:%d: " fmt "\n", \
						__func__, __LINE__, ## __VA_ARGS__)
#else
#define DBG(fmt, ...)
#endif

void __iomem *rtl_hwnat_mmio;
void __iomem *rtl_hwnat_clk_mmio;
void __iomem *rtl_hwnat_sata_mmio;
unsigned int rtl_hwnat_base;
struct platform_device *rtl819x_pdev;
void rtl_dump_data(void *p, int len);
extern uint8 hwnat_mac0_enable; /* 0: interface used by SATA, 1: interface used by NAT */
extern uint8 hwnat_mac0_mode; /* 0:RGMII, 1:SGMII */
extern uint8 hwnat_mac5_conn_to; /* 0:PHY, 1:MAC */
extern uint8 hwnat_rgmii_voltage; /* 1:1.8V, 2:2.5V, 3:3.3V */
#define RTL_SWNAT	1
#if defined(RTL_SWNAT)
bool rtl_hwnat_enable;

void ntohl_array(u32 *org_buf, u32 *dst_buf, unsigned int words)
{
	int i = 0;

	if (!org_buf || !dst_buf)
		return;
	while (words--) {
		dst_buf[i] = ntohl(org_buf[i]);
		i++;
	}

	return;
}
#else //defined(RTL_SWNAT)
extern bool rtl_hwnat_enable;
#endif //defined(RTL_SWNAT)
extern void rtd129x_hwnat_clk_init(void);
extern void rtk_polling_mac0_lnk_status(void);
#define CHECK_IF_HWNAT_READY if (!rtl_hwnat_enable) return SUCCESS
#endif //defined(CONFIG_RTD_1295_HWNAT)

#if defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)
uint16 sw_pvid[RTL8651_PORT_NUMBER];
#endif

#if defined(CONFIG_RTL_ETH_802DOT1X_SUPPORT)
#include <net/rtl/rtl_dot1x.h>
extern rtl802Dot1xConfig dot1x_config;
extern void rtl_82dot1xCheckPortState(void);
extern int rtl_802dot1xIoctl(struct net_device *dev, struct ifreq *rq, int cmd);
extern __MIPS16
int  rtl_802dot1xRxHandle(struct sk_buff *skb,rtl_nicRx_info *info);
extern inline int rtl_is802dot1xFrame(uint8 *macFrame);
extern void  rtl_802dot1xFilltxInfo(struct sk_buff *skb,rtl_nicTx_info *info);
#endif

#if defined(CONFIG_RTL_DNS_TRAP)
int rtl_add_Acl_for_dns_trap(struct rtl865x_vlanConfig* vlanConfig);
int rtl_remove_Acl_for_dns_trap(struct rtl865x_vlanConfig* vlanConfig);
#if defined(CONFIG_RTL_8367R_SUPPORT)
extern int rtl_8367_add_acl_for_dns(unsigned int acl_idx);
extern int rtl_8367_remove_acl_for_dns(unsigned int acl_idx);
#endif
#endif

// to_be_checked !!!
#if !defined(CONFIG_RTL_8198C) &&  !defined(CONFIG_RTL_8197F)
#define CONFIG_TR181_ETH		1
#endif
#if defined(CONFIG_RLX) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)) // added by lynn_pu, 2014-10-30
#undef CONFIG_TR181_ETH
#endif

//#define CONFIG_RTL_JATE_TEST		1

#ifdef CONFIG_RTL_JATE_TEST
static int32 rtl819x_jate_proc_init(void);
#endif

#if defined(CONFIG_RTK_VLAN_WAN_TAG_SUPPORT)
uint32 nicIgmpModuleIndex_2=0xFFFFFFFF;
extern uint32 brIgmpModuleIndex_2;
#define VLAN_CONFIG_SIZE sizeof(vlanconfig)/sizeof(struct rtl865x_vlanConfig)
#define VLAN_CONFIG_PPPOE_INDEX (VLAN_CONFIG_SIZE-1)
#endif

#if defined (CONFIG_RTL_8198_INBAND_AP) || defined (CONFIG_RTL_8198_NFBI_BOARD)
#define CONFIG_819X_PHY_RW 1
#endif

#if defined(CONFIG_RTL_ISP_MULTI_WAN_SUPPORT)
#include <net/rtl/rtl_multi_wan.h>
#define RTL_BRIDGE_WAN_SUPPORT_PORT_MAPPING 1
int enable_port_mapping=1;
int enable_vlan_grouping=0;
static unsigned int pvid_per_port[RTL8651_PORT_NUMBER+3]={[0 ... RTL8651_PORT_NUMBER+2]=RTL_LANVLANID};
static struct lan_dev_bind_mask_drv lan_dev_bind_mask_mapping_drv[LAN_DEV_NUM_MAX] = {
	{RTL_DRV_ETHLAN_P0_NETIF_NAME, 		RTL_ETH_LAN0_BIND_MASK},
	{RTL_DRV_LAN_P1_NETIF_NAME, 			RTL_ETH_LAN1_BIND_MASK},
	{RTL_DRV_LAN_P2_NETIF_NAME, 			RTL_ETH_LAN2_BIND_MASK},
	{RTL_DRV_LAN_P3_NETIF_NAME, 			RTL_ETH_LAN3_BIND_MASK},
	{RTL_DRV_WLAN0_DEV_NAME, 			RTL_WLAN0_BIND_MASK},
	{RTL_DRV_WLAN0_VA0_DEV_NAME, 		RTL_WLAN0_VA0_BIND_MASK},
	{RTL_DRV_WLAN0_VA1_DEV_NAME, 		RTL_WLAN0_VA1_BIND_MASK},
	{RTL_DRV_WLAN0_VA2_DEV_NAME, 		RTL_WLAN0_VA2_BIND_MASK},
	{RTL_DRV_WLAN0_VA3_DEV_NAME, 		RTL_WLAN0_VA3_BIND_MASK},
	{RTL_DRV_WLAN1_DEV_NAME, 			RTL_WLAN1_BIND_MASK},
	{RTL_DRV_WLAN1_VA0_DEV_NAME, 		RTL_WLAN1_VA0_BIND_MASK},
	{RTL_DRV_WLAN1_VA1_DEV_NAME, 		RTL_WLAN1_VA1_BIND_MASK},
	{RTL_DRV_WLAN1_VA2_DEV_NAME, 		RTL_WLAN1_VA2_BIND_MASK},
	{RTL_DRV_WLAN1_VA3_DEV_NAME, 		RTL_WLAN1_VA3_BIND_MASK},
};
#endif

#ifdef CONFIG_SMP
int lock_owner = -1;
int lock_owner_rx = -1;
int lock_owner_tx = -1;
int lock_owner_buf = -1;
int lock_owner_hw = -1;
spinlock_t lock_eth_other;
spinlock_t lock_eth_rx;
spinlock_t lock_eth_tx;
spinlock_t lock_eth_buf;
spinlock_t lock_eth_hw;
#endif

static unsigned int curLinkPortMask=0;
static unsigned int newLinkPortMask=0;

#ifdef CONFIG_RTL_JUMBO_FRAME
uint32 	jumboFrameFlag=0;
static uint32 	jumboFrameSize=0;
#define JUMBO_FRAME_SIZE_NORMAL	1536
#define JUMBO_FRAME_SIZE_9K 	9000
#define JUMBO_FRAME_SIZE_16K	16000
#endif

#if defined(CONFIG_RTL_8197D_DYN_THR) || defined(CONFIG_RTL_LONG_ETH_CABLE_REFINE)
int do_it_once = 0;
#endif

#ifdef CONFIG_RTL_8198C
#define CONFIG_RTL_8198C_10M_REFINE		1
static int _8198c_link_check = 0;
extern int giga_lite_enabled;
#endif

extern rtl8651_tblAsic_ethernet_t 	rtl8651AsicEthernetTable[];

#define SET_MODULE_OWNER(dev) do { } while (0)

#if defined (CONFIG_RTL_HARDWARE_MULTICAST)
#include <net/rtl/rtl865x_multicast.h>
#endif

#ifdef CONFIG_RTL8196_RTL8366
#include "RTL8366RB_DRIVER/gpio.h"
#include "RTL8366RB_DRIVER/rtl8366rb_apiBasic.h"
#endif

#ifdef CONFIG_RTK_VOIP_PORT_LINK
#include <net/netlink.h>
#include <linux/rtnetlink.h>
static int rtnl_fill_ifinfo_voip(struct sk_buff *skb, struct net_device *dev,
			int type, u32 pid, u32 seq, u32 change, unsigned int flags);
static void rtmsg_ifinfo_voip(int type, struct net_device *dev, unsigned change);
#endif

#ifdef CONFIG_RTK_VOIP_ETHERNET_DSP
void voip_dsp_L2_pkt_rx(unsigned char* eth_pkt);
#endif

#ifdef CONFIG_RTK_VOIP_ETHERNET_DSP
//merged from r8627 may conflict later
void ( *voip_dsp_L2_pkt_rx_trap )(unsigned char* eth_pkt, unsigned long size) = NULL;	// pkshih: eth_pkt content may be modified!!
#endif

#if (defined(CONFIG_RTL_CUSTOM_PASSTHRU) && !defined(CONFIG_RTL8196_RTL8366))
__DRAM_FWD static int oldStatus;
static char passThru_flag[1];
static int32 __init rtl8651_customPassthru_init(void);
static inline int32 rtl_isPassthruFrame(uint8 *data);
extern unsigned int _br0_ip;
extern unsigned int _br0_mask;
#endif

static int32 rtl8651_initStormCtrl(void);
#if 0//defined(CONFIG_RTL_8197F)
static int32 rtl8651_initExtendStormCtrl(void);
#endif
//#define CONFIG_RTL_WAN_PORT_SETTING 1
#if defined(CONFIG_RTL_WAN_PORT_SETTING)
static int32 rtl819x_initWanPortSetting(void);
static void __exit rtl819x_exitWanPortSetting(void);
#endif

#if defined(CONFIG_RTL_ETH_PRIV_SKB) && (defined(CONFIG_NET_WIRELESS_AGN) || defined(CONFIG_NET_WIRELESS_AG) || defined(CONFIG_WIRELESS))
#include <net/dst.h>
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)
#define NETDRV_PRIV(X) netdev_priv(X)
#else
#define NETDRV_PRIV(X) ((X)->priv)
#endif

#if 0
#define DEBUG_ERR printk
#else
#define DEBUG_ERR(format, args...)
#endif

#if defined (CONFIG_RTL_LOCAL_PUBLIC)
#include <net/rtl/rtl865x_localPublic.h>
#endif

static int32 __865X_Config;
#if defined(DYNAMIC_ADJUST_TASKLET) || defined(BR_SHORTCUT)
#if 0
static int eth_flag=12; // 0 dynamic tasklet, 1 - disable tasklet, 2 - always tasklet , bit2 - bridge shortcut enabled
#endif
#endif
#if defined(BR_SHORTCUT)
__DRAM_FWD  unsigned char cached_eth_addr[ETHER_ADDR_LEN];
EXPORT_SYMBOL(cached_eth_addr);
__DRAM_FWD  struct net_device *cached_dev;
EXPORT_SYMBOL(cached_dev);
#if defined(CONFIG_WIRELESS_LAN_MODULE)
struct net_device* (*wirelessnet_hook_shortcut)(unsigned char *da) = NULL;
EXPORT_SYMBOL(wirelessnet_hook_shortcut);
int (*wirelessnet_hook)(void) = NULL;
EXPORT_SYMBOL(wirelessnet_hook);
#endif
__DRAM_FWD  unsigned char cached_eth_addr2[ETHER_ADDR_LEN];
EXPORT_SYMBOL(cached_eth_addr2);
__DRAM_FWD  struct net_device *cached_dev2;
EXPORT_SYMBOL(cached_dev2);
int last_used = 1;
__DRAM_FWD  unsigned char cached_eth_addr3[ETHER_ADDR_LEN];
EXPORT_SYMBOL(cached_eth_addr3);
__DRAM_FWD  struct net_device *cached_dev3;
EXPORT_SYMBOL(cached_dev3);
__DRAM_FWD  unsigned char cached_eth_addr4[ETHER_ADDR_LEN];
EXPORT_SYMBOL(cached_eth_addr4);
__DRAM_FWD  struct net_device *cached_dev4;
EXPORT_SYMBOL(cached_dev4);
#endif

#if defined(CONFIG_RTL_REINIT_SWITCH_CORE)
#define STATE_NO_ERROR 0
#define STATE_SW_CLK_ENABLE_WAITING 1
#define STATE_TO_REINIT_SWITCH_CORE  2
int rtl865x_duringReInitSwtichCore=0;
int rtl865x_reInitState=STATE_NO_ERROR;
int rtl865x_reInitWaitCnt=0;
#if defined(CONFIG_RTL_CHECK_SWITCH_TX_HANGUP)
static int32 rtl_last_tx_done_idx = 0;
static int32 rtl_swCore_tx_hang_cnt = 0;
static uint32 rtl_check_swCore_timer = 0;
static int32 rtl_check_swCore_tx_hang_interval = 5;
static int32 rtl_reinit_swCore_threshold = 3;
static int32 rtl_reinit_swCore_counter = 0;
void rtl_check_swCore_tx_hang(void);
extern int32 rtl_check_tx_done_desc_swCore_own(int32 *tx_done_inx);
#endif
#endif
#if defined (CONFIG_RTL_UNKOWN_UNICAST_CONTROL)
static rtlMacRecord	macRecord[RTL_MAC_RECORD_NUM];
static uint32	macRecordIdx;
static uint8	lanIfName[NETIF_NUMBER];
static void	rtl_unkownUnicastUpdate(uint8 *mac);
static void rtl_unkownUnicastTimer(unsigned long data);
#endif

#if !defined(CONFIG_RTD_1295_HWNAT)
#if defined(DYNAMIC_ADJUST_TASKLET) || defined(RTL8196C_EEE_MAC) || defined(CONFIG_RTL_8198_ESD)
static void one_sec_timer(unsigned long task_priv);
#endif
#endif //!defined(CONFIG_RTD_1295_HWNAT)

#ifdef CONFIG_RTL8196C_GREEN_ETHERNET
static void power_save_timer(unsigned long task_priv);
#endif

//#define CONFIG_RTL_LINKSTATE
#if defined(CONFIG_RTL_LINKSTATE)
static struct timer_list s_timer;
static void linkup_time_handle(unsigned long arg);
static int32 initPortStateCtrl(void);
static void  exitPortStateCtrl(void);
#endif

#ifdef CONFIG_RTL_HW_VLAN_SUPPORT
#define PORT_NUMBER 6
struct hw_vlan_port_setting{
	  int32 vlan_port_enabled;
	  int32 vlan_port_bridge;
	  int32 vlan_port_tag;
	  int32 vlan_port_vid;
};
int rtl_hw_vlan_ignore_tagged_mc = 1;

struct hw_vlan_port_setting hw_vlan_info[PORT_NUMBER];

__DRAM_FWD int32     rtl_hw_vlan_enable = 0;
#endif


#if defined(CONFIG_RTL_ETH_PRIV_SKB)
__MIPS16 __IRAM_FWD static struct sk_buff *dev_alloc_skb_priv_eth(unsigned int size);
static void init_priv_eth_skb_buf(void);
#endif

#if defined(CONFIG_RTK_QOS_FOR_CABLE_MODEM)
static void rtl_initVlanTableForCableMode(void);
#endif

static int32 rtl819x_eee_proc_init(void);

__DRAM_FWD static struct ring_que	rx_skb_queue;
int skb_num=0;

#if defined(CONFIG_RTL_MULTIPLE_WAN) ||defined(CONFIG_RTL_MAC_BASED_NETIF)
#define MULTICAST_NETIF_VLAN_ID		678
static char multiCastNetIf[20]={"multiCastNetIf"};
static char multiCastNetIfMac[6]={ 0x00, 0x11, 0x12, 0x13, 0x14, 0x15 };
static int rtl865x_addMultiCastNetif(void);
#endif
#if defined(CONFIG_RTL_MULTIPLE_WAN)
#include <linux/if_arp.h>
static struct net_device *rtl_multiWan_net_dev;
static char igmp_proxy_wan_dev[MAX_IFNAMESIZE] = {"eth0"};
#ifndef CONFIG_RTL_PROC_NEW
static struct proc_dir_entry *res_igmp_proxy_wan_dev=NULL;
#endif
static int rtl_regist_multipleWan_dev(void);
static int rtl_config_multipleWan_netif(int32 cmd);
static int rtl_port_used_by_device(uint32 portMask);

static int rtl865x_isArpFrame(struct sk_buff *skb, int* ah_offset);
extern int get_dev_ip_mask(const char * name, unsigned int *ip, unsigned int *mask);
static inline int rtl865x_decideUcDevice(unsigned char *data, int pid, rtl_nicRx_info *info);
static inline int rtl865x_decideMcDevice(int pid, rtl_nicRx_info *info);
static inline int rtl865x_decideBcDevice(struct sk_buff *skb, int pid, rtl_nicRx_info *info);
static int32 rtl819x_igmp_proxy_wan_dev_proc_init(void);
#if 0
static int rtl865x_delMultiCastNetif(void);
int rtl865x_setMultiCastSrcMac(unsigned char *srcMac);
#endif
#endif

int32 rtl865x_init(void);
int32 rtl865x_config(struct rtl865x_vlanConfig vlanconfig[]);

/* These identify the driver base version and may not be removed. */
MODULE_DESCRIPTION("RealTek RTL-8650 series 10/100 Ethernet driver");
MODULE_LICENSE("GPL");

#ifdef CONFIG_DEFAULTS_KERNEL_2_6
static char* multicast_filter_limit = "maximum number of filtered multicast addresses";
module_param (multicast_filter_limit,charp, S_IRUGO);
#else
/* Maximum number of multicast addresses to filter (vs. Rx-all-multicast).
   The RTL chips use a 64 element hash table based on the Ethernet CRC.  */
MODULE_PARM (multicast_filter_limit, "i");
MODULE_PARM_DESC (multicast_filter_limit, "maximum number of filtered multicast addresses");
#endif

#define DRV_NAME		"re865x_nic"
#define PFX			DRV_NAME ": "
#define DRV_VERSION		"0.1"
#define TX_TIMEOUT		(10*HZ)
#define BDINFO_ADDR 0xbe3fc000

#define CONFIG_RTL_REPORT_LINK_STATUS

#define CONFIG_RTL_PHY_POWER_CTRL

#if defined (CONFIG_RTL_PHY_POWER_CTRL)
static int32 rtl865x_initPhyPowerCtrl(void);
#endif

#ifdef CONFIG_TR181_ETH
#ifdef CONFIG_RTL_PROC_NEW
static int tr181_eth_link_read(struct seq_file *s, void *v);
static int tr181_eth_if_read(struct seq_file *s, void *v);
static int tr181_eth_stats_read(struct seq_file *s, void *v);
static int tr181_eth_set_read(struct seq_file *s, void *v);
#else
static int tr181_eth_link_read(char *page, char **start, off_t off, int count, int *eof, void *data);
static int tr181_eth_if_read(char *page, char **start, off_t off, int count, int *eof, void *data);
static int tr181_eth_stats_read(char *page, char **start, off_t off, int count, int *eof, void *data);
static int tr181_eth_set_read(char *page, char **start, off_t off, int count, int *eof, void *data);
#endif
static int tr181_eth_link_write(struct file *file, const char *buffer, unsigned long count, void *data);
static int tr181_eth_if_write(struct file *file, const char *buffer, unsigned long count, void *data);
static int tr181_eth_stats_write(struct file *file, const char *buffer, unsigned long count, void *data);
static int tr181_eth_set_write(struct file *file, const char *buffer, unsigned long count, void *data);
#endif

#ifdef CONFIG_RTL_INBAND_CTL_ACL
#ifdef CONFIG_RTL_PROC_NEW
static int inband_ctl_acl_read(struct seq_file *s, void *v);
#else
static int inband_ctl_acl_read(char *page, char **start, off_t off, int count, int *eof, void *data);
#endif
static int inband_ctl_acl_write(struct file *file, const char *buffer, unsigned long count, void *data);
static int inband_ctl_acl_ethernet_type;
#endif

#if defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)
#if defined(CONFIG_RTL_HW_DSLITE_SUPPORT)
int dslite_hw_fw = 0 ;
#endif
#if defined(CONFIG_RTL_HW_6RD_SUPPORT)
int sixrd_hw_fw = 0 ;
#endif
#endif

#if	defined (CONFIG_PPPOE_VLANTAG)
struct vlan_info * rtl_get_PPPOE_vlanInfo(void);

static int read_PPPOE_proc_vlan(char *page, char **start, off_t off,
		int count, int *eof, void *data);

static int write_PPPOE_proc_vlan(struct file *file, const char *buffer,
		unsigned long count, void *data);

#endif

#if defined(RX_TASKLET)
__DRAM_GEN int rtl_rx_tasklet_running;
#endif
#if defined(TX_TASKLET)
__DRAM_GEN int rtl_tx_tasklet_running;
#endif

__IRAM_GEN static inline void rtl_rx_interrupt_process(unsigned int status, struct dev_priv *cp);
__IRAM_GEN static inline void rtl_tx_interrupt_process(unsigned int status, struct dev_priv *cp);
#if	defined(CONFIG_RTL_IGMP_SNOOPING)||defined(CONFIG_RTL_LINKCHG_PROCESS) || defined (CONFIG_RTL_PHY_PATCH) || defined(CONFIG_RTL_ETH_802DOT1X_SUPPORT)
__IRAM_GEN static inline void rtl_link_change_interrupt_process(unsigned int status, struct dev_priv *cp);
#endif

#if defined(CONFIG_RTD_1295_HWNAT)
atomic_t rtl_rxTxDoneCnt;
atomic_t rtl_devOpened;
#else //defined(CONFIG_RTD_1295_HWNAT)
static int rtl_rxTxDoneCnt = 0;
static atomic_t rtl_devOpened;
#endif //defined(CONFIG_RTD_1295_HWNAT)
#if defined(CONFIG_RTL_PROC_DEBUG)||defined(CONFIG_RTL_DEBUG_TOOL)
extern unsigned int rx_noBuffer_cnt;
extern unsigned int tx_ringFull_cnt;
extern unsigned int tx_drop_cnt;
#endif

#if defined(CONFIG_RTL_819XD)
static int rtl_port0Refined = 0;
#endif

#if defined(CONFIG_RTL_8370_SUPPORT)
static int rtl8211eLinkStsSpeed = 0;
#endif

__MIPS16 __IRAM_GEN void rtl_rxSetTxDone(int enable)
{
	if (unlikely(rtl_devOpened.counter==0))
		return;

	#if defined(CONFIG_RTD_1295_HWNAT)
	if (FALSE==enable)
	{
		atomic_dec(&rtl_rxTxDoneCnt);
		if (atomic_read(&rtl_rxTxDoneCnt) == -1)
			REG32(CPUIIMR) &= ~(TX_ALL_DONE_IE_ALL);
	}
	else
	{
		atomic_inc(&rtl_rxTxDoneCnt);
		if (atomic_read(&rtl_rxTxDoneCnt) == 0)
			REG32(CPUIIMR) |= (TX_ALL_DONE_IE_ALL);
	}
	#else //defined(CONFIG_RTD_1295_HWNAT)
	if (FALSE==enable)
	{
		rtl_rxTxDoneCnt--;
		if (rtl_rxTxDoneCnt==-1)
			REG32(CPUIIMR) &= ~(TX_ALL_DONE_IE_ALL);
	}
	else
	{
		rtl_rxTxDoneCnt++;
		if (rtl_rxTxDoneCnt==0)
			REG32(CPUIIMR) |= (TX_ALL_DONE_IE_ALL);
	}
	#endif //defined(CONFIG_RTD_1295_HWNAT)
}

#define NEXT_DEV(cp)			(cp->dev_next ? cp->dev_next : cp->dev_prev)
#define NEXT_CP(cp)			((struct dev_priv *)((NEXT_DEV(cp))->priv))
#define IS_FIRST_DEV(cp)	(NEXT_CP(cp)->opened ? 0 : 1)
#define GET_IRQ_OWNER(cp) (cp->irq_owner ? cp->dev : NEXT_DEV(cp))

#define MAX_PORT_NUM 9

static unsigned int rxRingSize[RTL865X_SWNIC_RXRING_HW_PKTDESC] =
	{NUM_RX_PKTHDR_DESC,
	NUM_RX_PKTHDR_DESC1,
	NUM_RX_PKTHDR_DESC2,
	NUM_RX_PKTHDR_DESC3,
	NUM_RX_PKTHDR_DESC4,
	NUM_RX_PKTHDR_DESC5};

#if defined(CONFIG_RTL_819XD) || defined(CONFIG_RTL_8196E) || defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)
static unsigned int txRingSize[RTL865X_SWNIC_TXRING_HW_PKTDESC] =
	{NUM_TX_PKTHDR_DESC,
	NUM_TX_PKTHDR_DESC1,
	NUM_TX_PKTHDR_DESC2,
	NUM_TX_PKTHDR_DESC3
	};
#else
static unsigned int txRingSize[RTL865X_SWNIC_TXRING_HW_PKTDESC] =
	{NUM_TX_PKTHDR_DESC,
	NUM_TX_PKTHDR_DESC1};
#endif

#if defined (CONFIG_RTL_MULTI_LAN_DEV) ||defined(CONFIG_RTK_VLAN_SUPPORT) ||defined(CONFIG_RTL_VLAN_8021Q) || defined(CONFIG_RTL_DNS_TRAP)
static  struct rtl865x_vlanConfig packedVlanConfig[NETIF_NUMBER*2];
#endif

#ifdef CONFIG_RTK_VLAN_WAN_TAG_SUPPORT
	int  vlan_tag;
	int  vlan_host_pri;
	int  vlan_bridge_tag;
	int  vlan_bridge_port;
	int  vlan_bridge_multicast_tag;
#endif

/*
linux protocol stack netif VS rtl819x driver network interface
the name of ps netif maybe different with driver.
*/
static ps_drv_netif_mapping_t ps_drv_netif_mapping[NETIF_NUMBER];

#if defined(CONFIG_RTL_ISP_MULTI_WAN_SUPPORT)
static struct rtl865x_vlanConfig vlanconfig[] = {
/*      	ifName  W/L      If type		VID	 FID	   Member Port	UntagSet		mtu		MAC Addr	is_slave	proto							*/
/*		=====  ===   =======	===	 ===   =========   =======	====	====================================	*/

/* LAN interface */
#ifdef CONFIG_RTL_MULTI_LAN_DEV
	{ 	RTL_DRV_LAN_P0_NETIF_NAME,	0,	IF_ETHER,	RTL_LANVLANID,		RTL_LAN_FID,		RTL_LANPORT_MASK_4,		RTL_LANPORT_MASK_4,		1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x90 } }, 0, 0},
	{ 	RTL_DRV_LAN_P4_NETIF_NAME,	1,	IF_ETHER,	RTL_WANVLANID,		RTL_WAN_FID,		RTL_WANPORT_MASK,			RTL_WANPORT_MASK,			1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 0, 0 },
	{	RTL_DRV_LAN_P1_NETIF_NAME,	0,	IF_ETHER,	RTL_LANVLANID,		RTL_LAN_FID,		RTL_LANPORT_MASK_3,		RTL_LANPORT_MASK_3,		1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x90 } }, 0, 0},
	{	RTL_DRV_LAN_P2_NETIF_NAME,	0,	IF_ETHER,	RTL_LANVLANID,		RTL_LAN_FID,		RTL_LANPORT_MASK_2,		RTL_LANPORT_MASK_2,		1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x90 } }, 0, 0},
	{	RTL_DRV_LAN_P3_NETIF_NAME,	0,	IF_ETHER,	RTL_LANVLANID,		RTL_LAN_FID, 		RTL_LANPORT_MASK_1,		RTL_LANPORT_MASK_1,		1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x90 } }, 0, 0},
#else
	{ 	RTL_DRV_LAN_NETIF_NAME,		0,	IF_ETHER,	RTL_LANVLANID,		RTL_LAN_FID, 		RTL_LANPORT_MASK,			RTL_LANPORT_MASK,			1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x90 } }, 0, 0},
#endif

/* 	Multi-WAN interface (all vid = 0 by default, it means all multi-wan netif's "name" have not been used yet, "net_device" will be created in smux)
	When you create a new WAN connection , please call rtl_set_wanport_vlanconfig() (or rtl_register_multi_wan_dev)
	This function will help you handle the setting of Network interface table, VLAN table and PVID in hw.
*/
#ifndef CONFIG_RTL_MULTI_LAN_DEV
	{ 	RTL_DRV_WAN0_NETIF_NAME,		1,	IF_ETHER,		RTL_WANVLANID,	RTL_WAN_FID,		RTL_WANPORT_MASK,		RTL_WANPORT_MASK,		1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 0, 0 },
#endif
	{ 	RTL_DRV_ETHWAN1_NETIF_NAME,	1,	IF_ETHER,		0,					RTL_WAN_FID,    	RTL_WANPORT_MASK,		RTL_WANPORT_MASK,     	1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 0, 0 },
	{ 	RTL_DRV_ETHWAN2_NETIF_NAME,	1,	IF_ETHER,		0,					RTL_WAN_FID,    	RTL_WANPORT_MASK,		RTL_WANPORT_MASK,     	1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 0, 0 },
	{ 	RTL_DRV_ETHWAN3_NETIF_NAME,	1,	IF_ETHER,		0,					RTL_WAN_FID,    	RTL_WANPORT_MASK,		RTL_WANPORT_MASK,     	1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 0, 0 },
	{ 	RTL_DRV_ETHWAN4_NETIF_NAME,	1,	IF_ETHER,		0,					RTL_WAN_FID,    	RTL_WANPORT_MASK,		RTL_WANPORT_MASK,     	1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 0, 0 },
	{ 	RTL_DRV_ETHWAN5_NETIF_NAME,	1,	IF_ETHER,		0,					RTL_WAN_FID,    	RTL_WANPORT_MASK,		RTL_WANPORT_MASK,     	1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 0, 0 },
	{ 	RTL_DRV_ETHWAN6_NETIF_NAME,	1,	IF_ETHER,		0,					RTL_WAN_FID,    	RTL_WANPORT_MASK,		RTL_WANPORT_MASK,     	1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 0, 0 },
	{ 	RTL_DRV_ETHWAN7_NETIF_NAME,	1,	IF_ETHER,		0,					RTL_WAN_FID,    	RTL_WANPORT_MASK,		RTL_WANPORT_MASK,     	1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 0, 0 },

	{	RTL_DRV_ETHWAN1_NETIF_NAME_EXTEND,	1,	IF_ETHER,		0,					RTL_WAN_FID,    	RTL_WANPORT_MASK,		RTL_WANPORT_MASK,     	1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 0, 0 },
	{ 	RTL_DRV_ETHWAN2_NETIF_NAME_EXTEND,	1,	IF_ETHER,		0,					RTL_WAN_FID,    	RTL_WANPORT_MASK,		RTL_WANPORT_MASK,     	1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 0, 0 },
	{ 	RTL_DRV_ETHWAN3_NETIF_NAME_EXTEND,	1,	IF_ETHER,		0,					RTL_WAN_FID,    	RTL_WANPORT_MASK,		RTL_WANPORT_MASK,     	1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 0, 0 },
	{ 	RTL_DRV_ETHWAN4_NETIF_NAME_EXTEND,	1,	IF_ETHER,		0,					RTL_WAN_FID,    	RTL_WANPORT_MASK,		RTL_WANPORT_MASK,     	1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 0, 0 },
	{ 	RTL_DRV_ETHWAN5_NETIF_NAME_EXTEND,	1,	IF_ETHER,		0,					RTL_WAN_FID,    	RTL_WANPORT_MASK,		RTL_WANPORT_MASK,     	1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 0, 0 },
	{ 	RTL_DRV_ETHWAN6_NETIF_NAME_EXTEND,	1,	IF_ETHER,		0,					RTL_WAN_FID,    	RTL_WANPORT_MASK,		RTL_WANPORT_MASK,     	1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 0, 0 },
	{ 	RTL_DRV_ETHWAN7_NETIF_NAME_EXTEND,	1,	IF_ETHER,		0,					RTL_WAN_FID,    	RTL_WANPORT_MASK,		RTL_WANPORT_MASK,     	1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 0, 0 },

/* PPP interface , please use rtl_add_ppp_netif() to create sw_netif (just data structure, not the actual net_device) in driver layer */
	{	RTL_DRV_PPP0_NETIF_NAME,		1,   IF_PPPOE,	0,				RTL_WAN_FID,    	RTL_WANPORT_MASK,		RTL_WANPORT_MASK,     	1500, 	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 1, 0 },
	{	RTL_DRV_PPP1_NETIF_NAME,		1,   IF_PPPOE,	0,				RTL_WAN_FID,    	RTL_WANPORT_MASK,		RTL_WANPORT_MASK,     	1500, 	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 1, 0 },
	{	RTL_DRV_PPP2_NETIF_NAME,		1,   IF_PPPOE,	0,				RTL_WAN_FID,    	RTL_WANPORT_MASK,		RTL_WANPORT_MASK,     	1500, 	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 1, 0 },
	{	RTL_DRV_PPP3_NETIF_NAME,		1,   IF_PPPOE,	0,				RTL_WAN_FID,    	RTL_WANPORT_MASK,		RTL_WANPORT_MASK,     	1500, 	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 1, 0 },
	{	RTL_DRV_PPP4_NETIF_NAME,		1,   IF_PPPOE,	0,				RTL_WAN_FID,    	RTL_WANPORT_MASK,		RTL_WANPORT_MASK,     	1500, 	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 1, 0 },
	{	RTL_DRV_PPP5_NETIF_NAME,		1,   IF_PPPOE,	0,				RTL_WAN_FID,    	RTL_WANPORT_MASK,		RTL_WANPORT_MASK,     	1500, 	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 1, 0 },
	{	RTL_DRV_PPP6_NETIF_NAME,		1,   IF_PPPOE,	0,				RTL_WAN_FID,    	RTL_WANPORT_MASK,		RTL_WANPORT_MASK,     	1500, 	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 1, 0 },
	{	RTL_DRV_PPP7_NETIF_NAME,		1,   IF_PPPOE,	0,				RTL_WAN_FID,    	RTL_WANPORT_MASK,		RTL_WANPORT_MASK,     	1500, 	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 1, 0 },

	RTL865X_CONFIG_END,
};
#else
static struct rtl865x_vlanConfig vlanconfig[] = {
/*      	ifName  W/L      If type		VID	 FID	   Member Port	UntagSet		mtu		MAC Addr	is_slave								*/
/*		=====  ===   =======	===	 ===   =========   =======	====	====================================	*/

#if defined(CONFIG_RTD_1295_HWNAT)
	{	RTL_DRV_LAN_P4_NETIF_NAME,	1,	IF_ETHER,	RTL_WANVLANID,		RTL_WAN_FID,		RTL_WANPORT_MASK,		RTL_WANPORT_MASK,		1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x94 } }, 0	},
	{ 	RTL_DRV_LAN_P0_NETIF_NAME,	0,	IF_ETHER,	RTL_LANVLANID,		RTL_LAN_FID,		RTL_LANPORT_MASK_1,		RTL_LANPORT_MASK_1,		1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x90 } }, 0	},
	{	RTL_DRV_LAN_P5_NETIF_NAME, 	0,	IF_ETHER,	RTL_LANVLANID,		RTL_LAN_FID,		RTL_LANPORT_MASK_5, 	RTL_LANPORT_MASK_5, 	1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x95 } }, 0	},

#if 0
	{	RTL_DRV_LAN_P1_NETIF_NAME,	0,	IF_NONE,	RTL_LANVLANID,		RTL_LAN_FID,		RTL_LANPORT_MASK_3,		RTL_LANPORT_MASK_3,		1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x90 } }, 0	},
	{	RTL_DRV_LAN_P2_NETIF_NAME,	0,	IF_NONE,	RTL_LANVLANID,		RTL_LAN_FID,		RTL_LANPORT_MASK_2,		RTL_LANPORT_MASK_2,		1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x90 } }, 0	},
	{	RTL_DRV_LAN_P3_NETIF_NAME,	0,	IF_NONE,	RTL_LANVLANID,		RTL_LAN_FID, 	RTL_LANPORT_MASK_1,		RTL_LANPORT_MASK_1,		1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x90 } }, 0	},
#endif

#else //defined(CONFIG_RTD_1295_HWNAT)
#ifdef CONFIG_BRIDGE
#if defined (CONFIG_RTL_MULTI_LAN_DEV)
	{ 	RTL_DRV_LAN_P0_NETIF_NAME,	0,	IF_ETHER,	RTL_LANVLANID,		RTL_LAN_FID,		RTL_LANPORT_MASK_4,		RTL_LANPORT_MASK_4,		1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x90 } }, 0	},
	{	RTL_DRV_LAN_P4_NETIF_NAME,	1,	IF_ETHER,	RTL_WANVLANID,		RTL_WAN_FID,	RTL_WANPORT_MASK,		RTL_WANPORT_MASK,		1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 0	},
	{	RTL_DRV_LAN_P1_NETIF_NAME,	0,	IF_ETHER,	RTL_LANVLANID,		RTL_LAN_FID,		RTL_LANPORT_MASK_3,		RTL_LANPORT_MASK_3,		1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x90 } }, 0	},
	{	RTL_DRV_LAN_P2_NETIF_NAME,	0,	IF_ETHER,	RTL_LANVLANID,		RTL_LAN_FID,		RTL_LANPORT_MASK_2,		RTL_LANPORT_MASK_2,		1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x90 } }, 0	},
	{	RTL_DRV_LAN_P3_NETIF_NAME,	0,	IF_ETHER,	RTL_LANVLANID,		RTL_LAN_FID, 	RTL_LANPORT_MASK_1,		RTL_LANPORT_MASK_1,		1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x90 } }, 0	},
#ifdef CONFIG_8198_PORT5_GMII
	{	RTL_DRV_LAN_P5_NETIF_NAME, 	0,	 IF_ETHER,	RTL_LANVLANID,	RTL_LAN_FID,		RTL_LANPORT_MASK_5, 	RTL_LANPORT_MASK_5, 	1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x95 } }, 0	},
#endif //CONFIG_8198_PORT5_GMII
#if defined(CONFIG_RTK_VLAN_SUPPORT)&&defined(CONFIG_RTK_BRIDGE_VLAN_SUPPORT)
	{	RTL_DRV_LAN_P7_NETIF_NAME,	0,	 IF_ETHER,	RTL_LANVLANID,	RTL_LAN_FID,	0, 	0, 	1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x97 } }, 0	},
#endif
#else	/*CONFIG_RTL_MULTI_LAN_DEV*/
#if defined(CONFIG_RTK_VLAN_SUPPORT)
	{ 	RTL_DRV_LAN_NETIF_NAME,	 0,   IF_ETHER, 	RTL_LANVLANID, 	   	RTL_LAN_FID, 	RTL_LANPORT_MASK_4, 	RTL_LANPORT_MASK_4,		1500, 	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x90 } }, 0	},
#else
	{ 	RTL_DRV_LAN_NETIF_NAME,	 0,   IF_ETHER, 	RTL_LANVLANID, 	   	RTL_LAN_FID, 	RTL_LANPORT_MASK, 		RTL_LANPORT_MASK,		1500, 	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x90 } }, 0	},
#endif
#if defined(CONFIG_RTL_PUBLIC_SSID)
	{	RTL_GW_WAN_DEVICE_NAME,	 1,   IF_ETHER,	RTL_WANVLANID,	   RTL_WAN_FID,		RTL_WANPORT_MASK,		RTL_WANPORT_MASK,		1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 0	},
#else
	{	RTL_DRV_WAN0_NETIF_NAME,	 1,   IF_ETHER,	RTL_WANVLANID,		RTL_WAN_FID,	RTL_WANPORT_MASK,		RTL_WANPORT_MASK,		1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 0	},
#endif
#if defined(CONFIG_RTK_VLAN_SUPPORT)
	{	RTL_DRV_LAN_P1_NETIF_NAME,	0,   IF_ETHER, 	RTL_LANVLANID,	RTL_LAN_FID, 	RTL_LANPORT_MASK_3, 	RTL_LANPORT_MASK_3,		1500, 	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x92 } }, 0	},
	{	RTL_DRV_LAN_P2_NETIF_NAME, 	0,   IF_ETHER, 	RTL_LANVLANID,	RTL_LAN_FID, 	RTL_LANPORT_MASK_2, 	RTL_LANPORT_MASK_2,		1500, 	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x93 } }, 0	},
	{	RTL_DRV_LAN_P3_NETIF_NAME, 	0,   IF_ETHER, 	RTL_LANVLANID,	RTL_LAN_FID, 	RTL_LANPORT_MASK_1, 	RTL_LANPORT_MASK_1,		1500, 	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x94 } }, 0	},
#ifdef CONFIG_8198_PORT5_GMII
	{	RTL_DRV_LAN_P5_NETIF_NAME, 	0,	 IF_ETHER,	RTL_LANVLANID,	RTL_LAN_FID,		RTL_LANPORT_MASK_5, 	RTL_LANPORT_MASK_5, 	1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x95 } }, 0	},
#endif //CONFIG_8198_PORT5_GMII
#ifdef CONFIG_RTK_BRIDGE_VLAN_SUPPORT
	{	RTL_DRV_LAN_P7_NETIF_NAME,	0,	 IF_ETHER,	RTL_LANVLANID,	RTL_LAN_FID,	0, 	0, 	1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x97 } }, 0	},
#endif
#endif
#endif
#else	/*CONFIG_BRIDGE*/
#if defined (CONFIG_RTL_MULTI_LAN_DEV)
	{ 	RTL_DRV_LAN_P0_NETIF_NAME,	0,	IF_ETHER,	RTL_LANVLANID,		RTL_LAN_FID,		RTL_LANPORT_MASK_4,		RTL_LANPORT_MASK_4,		1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x90 } }, 0	},
	{	RTL_DRV_LAN_P4_NETIF_NAME,	1,	IF_ETHER,	RTL_WANVLANID,		RTL_WAN_FID,	RTL_WANPORT_MASK,		RTL_WANPORT_MASK,		1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 0	},
	{	RTL_DRV_LAN_P1_NETIF_NAME,	0,	IF_ETHER,	RTL_LANVLANID,		RTL_LAN_FID,		RTL_LANPORT_MASK_3,		RTL_LANPORT_MASK_3,		1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x90 } }, 0	},
	{	RTL_DRV_LAN_P2_NETIF_NAME,	0,	IF_ETHER,	RTL_LANVLANID,		RTL_LAN_FID,		RTL_LANPORT_MASK_2,		RTL_LANPORT_MASK_2,		1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x90 } }, 0	},
	{	RTL_DRV_LAN_P3_NETIF_NAME,	0,	IF_ETHER,	RTL_LANVLANID,		RTL_LAN_FID, 	RTL_LANPORT_MASK_1,		RTL_LANPORT_MASK_1,		1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x90 } }, 0	},
#if defined(CONFIG_RTK_VLAN_SUPPORT)&&defined(CONFIG_RTK_BRIDGE_VLAN_SUPPORT)
	{	RTL_DRV_LAN_P7_NETIF_NAME,	0,	 IF_ETHER,	RTL_LANVLANID,	RTL_LAN_FID,	0, 	0, 	1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x97 } }, 0	},
#endif
#else /*CONFIG_RTL_MULTI_LAN_DEV*/
	{ 	RTL_DRV_LAN_NETIF_NAME,	 0,   IF_ETHER, 	RTL_LANVLANID, 	   	RTL_LAN_FID, 	RTL_LANPORT_MASK, 		RTL_LANPORT_MASK,		1500, 	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x90 } }, 0	},
	{	RTL_DRV_WAN0_NETIF_NAME,	 1,   IF_ETHER,	RTL_WANVLANID,	   	RTL_WAN_FID,	RTL_WANPORT_MASK,		RTL_WANPORT_MASK,		1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 0	},
#if defined(CONFIG_RTK_VLAN_SUPPORT)
	{	RTL_DRV_LAN_P1_NETIF_NAME,	0,   IF_ETHER, 	RTL_LANVLANID,	RTL_LAN_FID,		RTL_LANPORT_MASK_3, 	RTL_LANPORT_MASK_3,		1500, 	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x92 } }, 0	},
	{	RTL_DRV_LAN_P2_NETIF_NAME, 	0,   IF_ETHER, 	RTL_LANVLANID,	RTL_LAN_FID,		RTL_LANPORT_MASK_2, 	RTL_LANPORT_MASK_2,		1500, 	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x93 } }, 0	},
	{	RTL_DRV_LAN_P3_NETIF_NAME, 	0,   IF_ETHER, 	RTL_LANVLANID, 	RTL_LAN_FID,		RTL_LANPORT_MASK_1, 	RTL_LANPORT_MASK_1,		1500, 	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x94 } }, 0	},
#ifdef CONFIG_RTK_BRIDGE_VLAN_SUPPORT
	{	RTL_DRV_LAN_P7_NETIF_NAME,	0,	 IF_ETHER,	RTL_LANVLANID,	RTL_LAN_FID,	0,	0,	1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x97 } }, 0	},
#endif
#endif
#endif
#endif
	{	RTL_DRV_PPP_NETIF_NAME, 1,   IF_PPPOE,    	RTL_WANVLANID,		RTL_WAN_FID,    	RTL_WANPORT_MASK,         	RTL_WANPORT_MASK,     	1500, { { 0x00, 0x12, 0x34, 0x56, 0x78, 0x91 } }, 1 },
#if defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)
#if defined(CONFIG_RTL_HW_DSLITE_SUPPORT)
	{	RTL_DRV_DSLT_NETIF_NAME, 1,   IF_DSLT,    	RTL_WANVLANID,		RTL_WAN_FID,    	RTL_WANPORT_MASK,         	RTL_WANPORT_MASK,     	1500, { { 0x00, 0x12, 0x34, 0x56, 0x78, 0x99 } }, 1 },
#endif
#if defined(CONFIG_RTL_HW_6RD_SUPPORT)
	{	RTL_DRV_6RD_NETIF_NAME, 1,   IF_6RD,    	RTL_WANVLANID,		RTL_WAN_FID,    	RTL_WANPORT_MASK,         	RTL_WANPORT_MASK,     	1500, { { 0x00, 0x12, 0x34, 0x56, 0x78, 0x98 } }, 1 },
#endif
#endif
#endif //defined(CONFIG_RTD_1295_HWNAT)
	RTL865X_CONFIG_END,
};
#endif
#if defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)
#if defined(CONFIG_RTL_HW_DSLITE_SUPPORT)
int  get_P2P_local_ip(unsigned char *ifname, unsigned int *ipAddr)
{
	struct in_ifaddr *ifap = NULL;
	struct net_device *dev = NULL;
	struct in_device *dslite_dev= NULL;

	if(NULL==ifname || NULL==ipAddr)
		return FAILED;

	dev = __dev_get_by_name(&init_net, ifname);
	dslite_dev=(struct in_device *)(dev->ip_ptr);
	if (dslite_dev != NULL) {
		for (ifap=dslite_dev->ifa_list; ifap != NULL; ifap=ifap->ifa_next) {
			if (strcmp(ifname, ifap->ifa_label) == 0){
				*ipAddr = htonl(ifap->ifa_local);
				//printk("%s,%d........p2p local ip(%u)\n",__FUNCTION__,__LINE__,ifap->ifa_local);
				return 1;
			}
		}
	}
	return 0;
}
#endif
#endif

#if defined(CONFIG_RTL_MULTIPLE_WAN)
static struct rtl865x_vlanConfig rtl_multiWan_config = { RTL_DRV_WAN1_NETIF_NAME,	 1,   IF_ETHER, RTL_WAN_1_VLANID,		RTL_WAN_FID,	RTL_WANPORT_MASK,		RTL_WANPORT_MASK,		1500,	{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x99 } }, 0	};
#endif

#if (defined(CONFIG_PROC_FS) && defined(CONFIG_NET_SCHED) && defined(CONFIG_RTL_LAYERED_DRIVER))||(defined (CONFIG_RTL_HW_QOS_SUPPORT))
static uint8	netIfName[NETIF_NUMBER][IFNAMSIZ] = {{0}};
#endif

#if defined(CONFIG_RTL_ETH_PRIV_SKB)
/*	The following structure's field orders was arranged for special purpose,
	it should NOT be modify	*/

struct priv_skb_buf2 {
	unsigned char magic[ETH_MAGIC_LEN];
	void			*buf_pointer;
	/* the below 2 filed MUST together */
	struct list_head	list;
	unsigned char buf[ETH_SKB_BUF_SIZE];
};

static struct priv_skb_buf2 eth_skb_buf[MAX_ETH_SKB_NUM+1];
__DRAM_FWD static struct list_head eth_skbbuf_list;
__DRAM_FWD int eth_skb_free_num;
EXPORT_SYMBOL(eth_skb_free_num);
extern struct sk_buff *dev_alloc_8190_skb(unsigned char *data, int size);
struct sk_buff *priv_skb_copy(struct sk_buff *skb);
#endif

#if defined(CONFIG_RTK_BRIDGE_VLAN_SUPPORT)
	struct vlan_info management_vlan;
#endif

#if defined(CONFIG_POCKET_AP_SUPPORT) || defined(CONFIG_RTL_AP_PACKAGE)
int rtl865x_curOpMode=BRIDGE_MODE;
#else
int rtl865x_curOpMode=GATEWAY_MODE;
#endif
#if defined(CONFIG_RTL_EXT_PORT_SUPPORT)
extern int extPortEnabled;
#endif

__DRAM_FWD static  struct re865x_priv _rtl86xx_dev;

#ifdef CONFIG_RTL8196C_GREEN_ETHERNET
 static   struct timer_list expire_timer2;
#endif

#if defined(CONFIG_RTL_ETH_NAPI_SUPPORT)
#define RTL865X_NAPI_WEIGHT 64
static int rtl865x_poll(struct napi_struct *napi, int budget);
#endif


#if 0//def CONFIG_RTL_STP
static unsigned char STPmac[] = { 1, 0x80, 0xc2, 0,0,0};
#ifdef CONFIG_RTK_MESH
int8 STP_PortDev_Mapping[MAX_RE865X_STP_PORT] ={NO_MAPPING, NO_MAPPING, NO_MAPPING, NO_MAPPING, NO_MAPPING, WLAN_PSEUDO_IF_INDEX, WLAN_MESH_PSEUDO_IF_INDEX};
#else
int8 STP_PortDev_Mapping[MAX_RE865X_STP_PORT] ={NO_MAPPING, NO_MAPPING, NO_MAPPING, NO_MAPPING, NO_MAPPING, WLAN_PSEUDO_IF_INDEX};
#endif
static int re865x_stp_get_pseudodevno(uint32 port_num);
static int getVidByPort(uint32 port_num);
#endif

static int re865x_ioctl (struct net_device *dev, struct ifreq *rq, int cmd);
static int32 reinit_vlan_configure(struct rtl865x_vlanConfig new_vlanconfig[]);
#if defined(CONFIG_RTL_REPORT_LINK_STATUS)
/*FIX ME if mutiple-wan*/
static unsigned char rtk_linkEvent;
static int block_link_change = 0;
#define RTK_BLOCK_LINK_CHANGE_PERIOD 1
#ifdef CONFIG_RTL_PROC_NEW
static int32 rtk_link_event_read(struct seq_file *s, void *v);
static int32 rtk_link_status_read(struct seq_file *s, void *v);
#else
static int32 rtk_link_event_read( char *page, char **start, off_t off, int count, int *eof, void *data );
static int32 rtk_link_status_read( char *page, char **start, off_t off, int count, int *eof, void *data );
#endif
static int32 rtk_link_event_write( struct file *filp, const char *buff,unsigned long len, void *data );
static int32 rtk_link_status_write( struct file *filp, const char *buff,unsigned long len, void *data );
#endif

#ifdef CONFIG_RTL_VLAN_8021Q
extern linux_vlan_ctl_t *vlan_ctl_p;
extern int linux_vlan_enable;
extern int linux_vlan_hw_support_enable;
extern uint16 rtk_vlan_wan_vid;
extern uint16 rtk_vlan_hw_nat_lan_vid;

#define RTK_DEDICATE_WAN_IF 1 //mark_vc , use eth1 as nat wan , not eth0.xxx

#if 1
#define RTK_WAN_PORT_MASK RTL_WANPORT_MASK
#else
#define RTK_WAN_PORT_IDX 4
#define RTK_WAN_PORT_MASK  (0x3f & (1 << RTK_WAN_PORT_IDX ))
#endif

#define RTK_CPU_PORT_IDX 6
#define RTK_CPU_PORT_MASK  (1 << RTK_CPU_PORT_IDX )

// switch internal port maping
#define RTK_HW_CPU_PORT_IDX 8
#define RTK_HW_CPU_PORT_MASK  (1 << RTK_HW_CPU_PORT_IDX )

#define RTK_PHY_PORT_MASK  (0x1ff)
#define RTK_PHY_PORT_NUM  9

#define VID_FROM_PVID 0
#define VID_FROM_PACKET 1

static  struct rtl865x_vlanConfig packVlanConfig[NETIF_NUMBER];  //mark_vc , compatible

static int is_nat_wan_vlan(uint16 vid);
static inline int rtk_isHwlookup_vlan(uint16 vid, struct sk_buff *skb, uint32 *portlist);
static int rtk_decide_vlan_id(int pid,struct sk_buff *skb,uint16 *vid);
static struct dev_priv *rtk_decide_vlan_cp(int pid,struct sk_buff *skb);
static int rtk_process_11q_rx(int port, struct sk_buff *skb);
static inline int32 rtl_isWanPort(struct dev_priv *cp, uint16 vid, struct sk_buff *skb, int pid);
#if defined CONFIG_RTL_MULTICAST_PORT_MAPPING
static int rtl865x_nic_MulticastHardwareAccelerate(unsigned int brFwdPortMask,
												unsigned int srcPort,unsigned int srcVlanId,
												unsigned int srcIpAddr, unsigned int destIpAddr, unsigned int mapPortMask);

#else

static int rtl865x_nic_MulticastHardwareAccelerate(unsigned int brFwdPortMask,
												unsigned int srcPort,unsigned int srcVlanId,
												unsigned int srcIpAddr, unsigned int destIpAddr);
#endif
uint32 rtk_get_vlan_portmask(uint16 vid);
uint8 rtk_get_vlan_type(uint16 vid);
uint16 rtk_get_vid_by_port(int pid);
void rtk_init_11q_setting(void);
void rtk_add_eth_vlan(uint16 vid , uint32 portmask,uint32 tagmask);
static void rtk_rm_skb_vid(struct sk_buff *skb);
uint16 rtk_get_skb_vid(struct sk_buff *skb);
#if defined(CONFIG_RTL_8367R_SUPPORT)
extern int rtl865x_disableRtl8367ToCpuAcl(void);
#endif

static inline void rtk_insert_vlan_tag(uint16 vid,struct sk_buff *skb)
{
	memmove(skb->data-VLAN_HLEN, skb->data, ETH_ALEN<<1);
	skb_push(skb,VLAN_HLEN);
	*((uint16*)(skb->data+(ETH_ALEN<<1))) = __constant_htons(ETH_P_8021Q);
	*((uint16*)(skb->data+(ETH_ALEN<<1)+2)) = __constant_htons(vid);
}

#define REMOVE_TAG(skb) { \
	memmove(skb->data+VLAN_HLEN, skb->data, ETH_ALEN*2); \
	skb_pull(skb, VLAN_HLEN); \
}
#endif

// for ipv6 ready logo
#ifdef CONFIG_RTL_HARDWARE_IPV6_SUPPORT
int rtk_hwL3v6_enable;
#ifdef CONFIG_RTL_PROC_NEW
static int32 rtk_hwL3v6_read(struct seq_file *s, void *v);
#else
static int32 rtk_hwL3v6_read( char *page, char **start, off_t off, int count, int *eof, void *data );
#endif
static int32 rtk_hwL3v6_write( struct file *filp, const char *buff,unsigned long len, void *data );
#endif

//static void rtl_print_vlanconfig(struct rtl865x_vlanConfig new_vlanconfig[]);
#if defined(CONFIG_RTK_VLAN_SUPPORT)
#ifdef CONFIG_RTL_PROC_NEW
static int read_proc_vlan(struct seq_file *s, void *v);
static int32 rtk_vlan_support_read(struct seq_file *s, void *v);
#else
static int read_proc_vlan(char *page, char **start, off_t off,int count, int *eof, void *data);
static int32 rtk_vlan_support_read( char *page, char **start, off_t off, int count, int *eof, void *data );
#endif
static int write_proc_vlan(struct file *file, const char *buffer,unsigned long count, void *data);
static int32 rtk_vlan_support_write( struct file *filp, const char *buff,unsigned long len, void *data );
#if defined(CONFIG_RTK_BRIDGE_VLAN_SUPPORT)
#ifdef CONFIG_RTL_PROC_NEW
static int rtk_vlan_management_read(struct seq_file *s, void *v);
#else
static int rtk_vlan_management_read(char *page, char **start, off_t off, int count, int *eof, void *data);
#endif
static int rtk_vlan_management_write(struct file *file, const char *buffer, unsigned long len, void *data);
#endif

//__DRAM_FWD int rtk_vlan_support_enable;
int rtk_vlan_support_enable;

#if defined(CONFIG_RTL_8367R_SUPPORT)
#define CONFIG_RTK_REFINE_PORT_DUPLEX_MODE 1
extern int rtl865x_enableRtl8367ToCpuAcl(void);
extern int rtl865x_disableRtl8367ToCpuAcl(void);
extern int RTL8367R_vlan_set(void);
#if  defined(CONFIG_RTK_REFINE_PORT_DUPLEX_MODE)
extern int rtk_refinePortDuplexMode(void);
#endif
#endif

#ifdef CONFIG_RTK_VLAN_WAN_TAG_SUPPORT
static int32 rtk_vlan_wan_tag_getportmask(int bridge_port);
#if defined(CONFIG_RTL_PROC_NEW)
static int32 rtk_vlan_wan_tag_support_read(struct seq_file *s, void *v);
#else
static int32 rtk_vlan_wan_tag_support_read( char *page, char **start, off_t off, int count, int *eof, void *data );
#endif
static int32 rtk_vlan_wan_tag_support_write( struct file *filp, const char *buff,unsigned long len, void *data );
#endif

#ifdef CONFIG_RTL_JUMBO_FRAME

#ifdef CONFIG_RTL_PROC_NEW
static int32 jumbo_frame_support_read(struct seq_file *s, void *v);
static int32 jumbo_frame_support_write( struct file *filp, const char *buff,unsigned long len, void *data );

int jumbo_frame_single_open(struct inode *inode, struct file *file)
{
	return(single_open(file, jumbo_frame_support_read, NULL));
}
static ssize_t jumbo_frame_single_write(struct file * file, const char __user * userbuf,
		size_t count, loff_t * off)
{
	return jumbo_frame_support_write(file, userbuf,count, off);
}
struct file_operations jumbo_frame_proc_fops= {
		.open           = jumbo_frame_single_open,
		.write		    = jumbo_frame_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};
#else
static int32 jumbo_frame_support_read( char *page, char **start, off_t off, int count, int *eof, void *data );
static int32 jumbo_frame_support_write( struct file *filp, const char *buff,unsigned long len, void *data );
#endif
#endif

#if defined(CONFIG_819X_PHY_RW) //#if defined(CONFIG_RTK_VLAN_FOR_CABLE_MODEM)
static int32 rtl_phy_status_read( char *page, char **start, off_t off, int count, int *eof, void *data );
static int32 rtl_phy_status_write( struct file *filp, const char *buff,unsigned long len, void *data );
static int32 port_mibStats_read_proc( char *page, char **start, off_t off, int count, int *eof, void *data );
static int32 port_mibStats_write_proc( struct file *filp, const char *buff,unsigned long len, void *data );

struct port_mibStatistics  {

	/*here is in counters  definition*/
	uint32 ifInOctets;
	uint64 ifHCInOctets;
	uint32 ifInUcastPkts;
	uint64 ifHCInUcastPkts;
	uint32 ifInMulticastPkts;
	uint64 ifHCInMulticastPkts;
	uint64 ifHCInBroadcastPkts;
	uint32 ifInBroadcastPkts;
	uint32 ifInDiscards;
	uint32 ifInErrors;
	uint64 etherStatsOctets;
	uint32 etherStatsUndersizePkts;
	uint32 etherStatsFraments;
	uint32 etherStatsPkts64Octets;
	uint32 etherStatsPkts65to127Octets;
	uint32 etherStatsPkts128to255Octets;
	uint32 etherStatsPkts256to511Octets;
	uint32 etherStatsPkts512to1023Octets;
	uint32 etherStatsPkts1024to1518Octets;
	uint32 etherStatsOversizePkts;
	uint32 etherStatsJabbers;
	uint32 dot1dTpPortInDiscards;
	uint32 etherStatusDropEvents;
	uint32 dot3FCSErrors;
	uint32 dot3StatsSymbolErrors;
	uint32 dot3ControlInUnknownOpcodes;
	uint32 dot3InPauseFrames;

	/*here is out counters  definition*/
	uint32 ifOutOctets;
	uint64 ifHCOutOctets;
	uint32 ifOutUcastPkts;
	uint64 ifHCOutUcastPkts;
	uint64 ifHCOutMulticastPkts;
	uint64 ifHCOutBroadcastPkts;
	uint32 ifOutMulticastPkts;
	uint32 ifOutBroadcastPkts;
	uint32 ifOutDiscards;
	uint32 ifOutErrors;
	uint32 dot3StatsSingleCollisionFrames;
	uint32 dot3StatsMultipleCollisionFrames;
	uint32 dot3StatsDefferedTransmissions;
	uint32 dot3StatsLateCollisions;
	uint32 dot3StatsExcessiveCollisions;
	uint32 dot3OutPauseFrames;
	uint32 dot1dBasePortDelayExceededDiscards;
	uint32 etherStatsCollisions;

	/*here is whole system couters definition*/
	uint32 dot1dTpLearnedEntryDiscards;
	uint32 etherStatsCpuEventPkts;
	uint32 ifInUnknownProtos;
	uint32 ifSpeed;
	uint32 ifHighSpeed;
	uint32 ifConnectorPresent;
	uint32 ifCounterDiscontinuityTime;
};
#endif	//#if defined(CONFIG_819X_PHY_RW)
#endif

#if defined(CONFIG_RTL_HW_VLAN_SUPPORT)
#if defined(CONFIG_RTL_PROC_NEW)
static int rtl_hw_vlan_support_read(struct seq_file *s, void *v);
#else
static int32 rtl_hw_vlan_support_read( char *page, char **start, off_t off, int count, int *eof, void *data );
#endif
static int32 rtl_hw_vlan_support_write( struct file *filp, const char *buff,unsigned long len, void *data );
#if defined(CONFIG_RTL_PROC_NEW)
static int rtl_hw_vlan_tagged_bridge_multicast_read(struct seq_file *s, void *v);
#else
static int32 rtl_hw_vlan_tagged_bridge_multicast_read( char *page, char **start, off_t off, int count, int *eof, void *data );
#endif
static int32 rtl_hw_vlan_tagged_bridge_multicast_write( struct file *filp, const char *buff,unsigned long len, void *data );
int rtl_process_hw_vlan_tx(rtl_nicTx_info *txInfo);
#endif

#if defined(CONFIG_RTL_8367R_SUPPORT)
#if defined(CONFIG_RTL_PROC_NEW)
int32 rtl_8367r_vlan_read(struct seq_file *s, void *v);
#else
int32 rtl_8367r_vlan_read( char *page, char **start, off_t off, int count, int *eof, void *data );
#endif
#endif

#ifdef CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT
//#define RTK_WAN_PORT_IDX 4
#define RTK_WAN_PORT_IDX (ffs(RTL_WANPORT_MASK) - (1))
#define RTK_MASK_TO_PORT(x) (ffs(x) - (1))
#define RTK_CPU_PORT 8

#define FW_BRIDGE_VLAN 1
#define FW_NAT_VLAN 2
#define RTL_CPUPORT_MASK (1 << RTK_CPU_PORT)

int  RTK_NAT_VLANID=10;
int  RTK_NAT_WAN_TAGGED=0;
int RTK_MC_VLANID= 50 ;
int RTK_QUERYFORBRIDGEPORT = 1;
#define RTK_MANAGE_VLAN_IF 1 // use new eth interface as management vlan

#define VLAN_CONFIG_SIZE sizeof(vlanconfig)/sizeof(struct rtl865x_vlanConfig)
#define VLAN_CONFIG_PPPOE_INDEX (VLAN_CONFIG_SIZE-2)

struct net_device *manage_vlan_dev=NULL;

#ifdef  RTK_MANAGE_VLAN_IF
static void  rtk_add_manage_vlan_dev(uint32 vid);
void rtk_add_manage_vlan(void);
struct vlan_info *rtl_get_man_vlaninfo(void);
#define RTK_MANAGE_IFNAME  ("eth7")
//#define  RTK_MANAGE_VLAN_IF_DEBUG_MSG  1  //mark_manv
#endif

#if defined(CONFIG_RTL8192CD)
extern int is_rtl_nat_wlan_vlan(struct net_device *dev) ; //from wlan driver
#endif
static struct vlan_info wan_management_vlan;

static struct dev_priv *rtl_get_wan_cp(void)
{
	#if defined(CONFIG_RTD_1295_HWNAT)
	return (struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[RTL_WAN_IDX]);
	#else //defined(CONFIG_RTD_1295_HWNAT)
	return (struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]);
	#endif //defined(CONFIG_RTD_1295_HWNAT)
}

static uint16 get_skb_vid( struct sk_buff *skb)
{
	uint16 vid=0; //untag or NoVlan group

	if( skb->tag.f.tpid == htons(ETH_P_8021Q) )
		vid = ntohs(skb->tag.f.pci & 0xfff);

	return vid;
}

int is_rtl_nat_vlan (struct sk_buff *skb)
{
	uint16 vid = get_skb_vid(skb);

	if(vid == 0 || vid == RTK_NAT_VLANID)//vid =0 is vlan disable
		return 1;

	return 0;
}

int is_rtl_multi_nat_vlan (struct sk_buff *skb, uint16 *vid)
{
	uint16 skb_vid = get_skb_vid(skb);

	if(skb_vid != 0 && skb_vid != RTK_NAT_VLANID)//vid =0 is vlan disable
	{
		*vid = skb_vid; // mark_wvlan , what if wlan nat vid untag ?? FIXME
		return 1;
	}

	return 0;
}
int is_rtl_wan_mac(struct sk_buff *skb)
{
	// NOTE , this can only be called when skb is come from WAN (is not LAN)
	if (!memcmp(skb->dev->dev_addr, skb->data, 6))
		return 1;

	return 0;
}
int rtl_vlan_pass_frame_up(struct sk_buff *skb)
{
	if(!rtl_hw_vlan_enable)
		return 1;
	// if it is a NAT paclet , pass up
	if(is_rtl_nat_vlan(skb))
	return 1;

	//if it is from wlan , then maybe natvid is other
	#if defined(CONFIG_RTL8192CD)
	if(memcmp(skb->dev->name,"wlan",4) == 0)	//check if come from wlan
		if(is_rtl_nat_wlan_vlan(skb->dev)) //from wlan driver
			return 1;
	#endif

	return 0;
}

static void rtl_update_lan_cp_vinfo(struct dev_priv *cp,int enable,int vid,int fw,int wan_tag)
{
	cp->vlan_setting.global_vlan =1;
	cp->vlan_setting.vlan =enable;
	cp->vlan_setting.is_lan = 1;
	cp->vlan_setting.id =vid ;
	cp->vlan_setting.tag = wan_tag;
	cp->vlan_setting.forwarding_rule = fw;
}

static void rtl_update_wan_cp_vinfo(struct dev_priv *cp,int enable,int vid,int wan_tag)
{
	cp->vlan_setting.global_vlan =1;
	cp->vlan_setting.vlan =enable;
	cp->vlan_setting.is_lan = 0;
	cp->vlan_setting.id =vid ;
	cp->vlan_setting.tag = wan_tag;
	cp->vlan_setting.forwarding_rule = FW_NAT_VLAN;
}

static void rtl_update_manage_vlan(int enable,int vid,int wan_tag)
{

	wan_management_vlan.global_vlan =1;
	wan_management_vlan.vlan =enable;
	wan_management_vlan.id =vid ;
	wan_management_vlan.tag = wan_tag;
	wan_management_vlan.forwarding_rule = FW_NAT_VLAN;
#ifdef RTK_MANAGE_VLAN_IF

	if(manage_vlan_dev != NULL )
	{
		// update manage cp info
		rtl_update_wan_cp_vinfo((struct dev_priv *)NETDRV_PRIV(manage_vlan_dev),enable,vid,wan_tag);
		//update cp vid
		((struct dev_priv *)NETDRV_PRIV(manage_vlan_dev))->id = vid;
	}
#endif

}

inline static int is_ARP_packet(struct sk_buff *skb)
{
	uint8 *ptr;
	uint8 *macFrame = skb->data;

	ptr=macFrame+12;

	/*it's arp packet*/
	if(*(int16 *)(ptr)==(int16)htons(ETH_P_ARP))
	{
		return 1;
	}

	return 0;
}

static inline struct iphdr * getIpv4Header(uint8 *macFrame)
{
	uint8 *ptr;
	struct iphdr *iph=NULL;

	ptr=macFrame+12;

	/*it's not ipv4 packet*/
	if(*(int16 *)(ptr)!=(int16)htons(ETH_P_IP))
	{
		return NULL;
	}

	iph=(struct iphdr *)(ptr+2);

	return iph;
}

inline static int is_DHCP_packet(struct sk_buff *skb)
{
	struct iphdr *iph;
	struct udphdr *udph;

	 iph = getIpv4Header(skb->data);
	 if(iph == NULL)
	 	return 0;

	 udph = (void *) iph + iph->ihl*4;
	 if (iph->protocol == IPPROTO_UDP )
		if (udph->source == 68 || udph->source == 67 )  // DHCP packet
			return 1;

	return 0;
}
struct net_device *rtl_get_dev_by_vid(uint16 vid)
{
	int i;
	struct dev_priv	*cp;

	if(vid == 0)
		return NULL;

	for(i=0; i< PORT_NUMBER ; i++)
	{
	  cp =((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[i]));
	  if( cp->id == vid)
	  	return (struct net_device *)cp->dev;

	}
	return NULL;
}

static  struct dev_priv *rtl_get_cp_by_vid(uint16 vid)
{
	int i;
	struct dev_priv *cp;

	if (wan_management_vlan.vlan && (vid == wan_management_vlan.id))
	{
#ifdef RTK_MANAGE_VLAN_IF
	if(manage_vlan_dev)
	{
#ifdef  RTK_MANAGE_VLAN_IF_DEBUG_MSG
	printk("retrun  mana vlan cp = %x \n",(int) manage_vlan_dev->priv);
#endif
	 	 return ((struct dev_priv *)NETDRV_PRIV(manage_vlan_dev));
	}
	else
#endif
		#if defined(CONFIG_RTD_1295_HWNAT)
		 return ((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[RTL_WAN_IDX])); //default return Wan nat if
		#else //defined(CONFIG_RTD_1295_HWNAT)
		 return ((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1])); //default return Wan nat if
		#endif //defined(CONFIG_RTD_1295_HWNAT)

	}
	for(i=0; i< PORT_NUMBER ; i++)
	{
	  cp =((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[i]));
	  if( cp->id == vid)
	  	return (struct dev_priv *)cp;

	}
	//printk("cant find cp , unknow vid default go to eht1 !!\n");
	#if defined(CONFIG_RTD_1295_HWNAT)
	return ((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[RTL_WAN_IDX])); //default return Wan nat if
	#else //defined(CONFIG_RTD_1295_HWNAT)
	return ((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1])); //default return Wan nat if
	#endif //defined(CONFIG_RTD_1295_HWNAT)
}

struct vlan_info *rtl_get_vinfo_by_vid(uint16 vid)
{
	int i;
	struct dev_priv	*cp;

	if(vid == 0)
		return NULL;
#ifdef  RTK_MANAGE_VLAN_IF
	if (wan_management_vlan.vlan && (vid == wan_management_vlan.id))
		  return ((struct vlan_info *)rtl_get_man_vlaninfo());
#endif
	for(i=0; i< PORT_NUMBER ; i++)
	{
	  cp =((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[i]));
	  if( cp->id == vid)
	  	return (struct vlan_info *)&cp->vlan_setting;

	}
	return NULL;
}
int is_rtl_manage_vlan_tagged(struct sk_buff *skb, uint16 *vid)
{
	uint16 skb_vid=0;
	*vid = (unsigned short)wan_management_vlan.id;
	// bypass if manage vlan not enable or tag
#ifdef RTK_MANAGE_VLAN_IF
	if((wan_management_vlan.vlan) && (wan_management_vlan.tag))
	{
			//if txIf is manage vlan interface and set to tagout , then  insert mana vlan tag
		 if (!strcmp((char *)skb->dev->name, RTK_MANAGE_IFNAME)) //mark_test
		 {
#ifdef RTK_MANAGE_VLAN_IF_DEBUG_MSG
		 printk("tx to manage eth interface(%s) and add tag \n",skb->dev->name);
#endif
			 return 1;
		 }
	}

	return 0;
	//ignore below
#endif
	if((!wan_management_vlan.vlan) || (!wan_management_vlan.tag))
		return 0;
	//if it is already a manage vlan packet
	skb_vid = get_skb_vid(skb);
	if( skb_vid == wan_management_vlan.id )
		return 1;

	// bypass if skb not come from Wan protocol stack , from PS skb->tag = 0
	if(skb->tag.f.tpid != 0)
		return 0;
	else // bypass special case  even it is from Wan protocol stack but only belong to NAT vlan
	{
	// bypass if not arp , dhcp , igmp
	if(is_ARP_packet(skb))
		return 0;
	if(is_DHCP_packet(skb))
		return 0;

#if defined (CONFIG_RTL_IGMP_SNOOPING)
	if((skb->data[0]==0x01) && (skb->data[1]==0x00)&& (skb->data[2]==0x5e))
		return 0;
#endif
#if defined (CONFIG_RTL_MLD_SNOOPING)
	 if ((skb->data[0]==0x33) && (skb->data[1]==0x33) && (skb->data[2]!=0xff))
	 	return 0;
#endif
	}

	return 1;
}

int is_multicast_pkt_towan(struct sk_buff *skb)
{
	struct iphdr *iph;
	struct icmphdr *icmph;
	struct ipv6hdr *ipv6h=NULL;
	uint8 *ptr;

	if (skb == NULL)
		return 0;

	ptr = skb->data + 12;
	//untag
	if(*(int16 *)(ptr)==(int16)htons(ETH_P_IP))
	{
		//ipv4
		iph=(struct iphdr *)(ptr+2);
		icmph = (void *)iph + iph->ihl*4;
		if (((skb->data[0] & 0x01) &&(skb->data[0] != 0xFF ))&&(skb->tag.f.tpid == 0)&&
			(!memcmp(skb->dev->dev_addr, skb->data+6, 6)))//from protocol stack
		{
			//using wan vlan information
			return 1;

		}
	}
	else if(*(int16 *)(ptr)==(int16)htons(ETH_P_IPV6))
	{
		//ipv6
		ipv6h=(struct ipv6hdr *)(ptr+2);
		if (((skb->data[0] & 0x01) &&(skb->data[0] != 0xFF ))&&(skb->tag.f.tpid == 0)&&
			(!memcmp(skb->dev->dev_addr, skb->data+6, 6)))//from protocol stack
		{
			//using wan vlan information
			return 1;

		}

	}

	return 0;
}
int is_rtl_mc_vlan_tagged2(struct sk_buff *skb, uint16 *vid)
{
	struct vlan_info *mc_info = rtl_get_vinfo_by_vid(RTK_MC_VLANID);
	struct vlan_info *info;
	if (is_multicast_pkt_towan(skb)==1)
	{
		//panic_printk("%s %d \n", __FUNCTION__, __LINE__);
		#ifdef  RTK_MANAGE_VLAN_IF
		if (wan_management_vlan.vlan)
		{
			info = ((struct vlan_info *)rtl_get_man_vlaninfo());
			if (info)
			{
				*vid = info->id;
				//panic_printk("%s %d *vid=%d \n", __FUNCTION__, __LINE__, *vid);
				return info->tag;
			}
		}
		#endif
	}
	*vid = RTK_MC_VLANID;
	if ( (mc_info) )
	{
		return mc_info->tag;
	}

	return 0;
}


int is_rtl_mc_vlan_tagged(uint16 *vid)
{
	struct vlan_info *mc_info = rtl_get_vinfo_by_vid(RTK_MC_VLANID);
	*vid = RTK_MC_VLANID;
	if ( (mc_info) )
	{
		return mc_info->tag;
	}
	return 0;
}

void rtl_set_nat_vlan_info(int vid ,int wan_tag)
{
	RTK_NAT_VLANID = vid;
	RTK_NAT_WAN_TAGGED = wan_tag;
}

static uint32 rtl_get_WanTagPortmask(int tag)
{
	uint32 portmask =0 ;

	if(tag)
		portmask =  0x3f & (1 << RTK_WAN_PORT_IDX );

	return portmask;
}
uint32 rtk_getTagPortmaskByVid(uint16 vid , void *data_p)
{
	uint16 tag_portmask=0;
	struct vlan_info *info=NULL;
	unsigned char *data = (unsigned char *)data_p;

	//if( hw_vlan_support)
	if(!rtl_hw_vlan_enable)
		return 0;
	info = rtl_get_vinfo_by_vid(vid);
	if(info)
		tag_portmask = rtl_get_WanTagPortmask(info->tag);

	if(tag_portmask)
	{
		//if it is a mc and if mc is untag out ,then Wan port should not tag Wan port out for this Mc packet.
		if(((data[0] & 0x01) &&(data[0] != 0xFF )) &&( RTK_MC_VLANID != 0)&& (!is_rtl_mc_vlan_tagged(&vid)))
			tag_portmask = 0; //mark_hwv
	}
	return tag_portmask;
}

static int rtl_get_vlan_num(struct rtl865x_vlanConfig s_vlanconfig[])
{
   int32 totalVlans=((sizeof(vlanconfig))/(sizeof(struct rtl865x_vlanConfig)))-1;
   int i,vlan_num=0;
   for(i=0;i<totalVlans;i++)  //ignore eth0 , eth1 , and nat vid ,just find br vid num
   {
	if( s_vlanconfig[i].vid != 0)
	{
		if( (i>1) && (s_vlanconfig[i].vid == RTK_NAT_VLANID )) //mark_hwv ,bypass pp case ? ,FIXME
			continue;
			vlan_num++;
   }
   }
   return vlan_num;
}

static int rtk_multicast_scream_vid_write(struct file *file, const char *buffer,
			  unsigned long len, void *data)
{
	char tmpbuf[128];
	int num;

	if (buffer && !copy_from_user(tmpbuf, buffer, len))
	{
		tmpbuf[len] = '\0';

		num = sscanf(tmpbuf, "%d",	&RTK_MC_VLANID);

		if (num !=  1) {
			printk("invalid  parameter!\n");
		}

	}

	return len;
}
#if defined(CONFIG_RTL_PROC_NEW)
static int32 rtk_multicast_scream_vid_read(struct seq_file *s, void *v)
{

	seq_printf(s,  "%s %d\n", "rtk_multicast_scream_vid:",RTK_MC_VLANID);

	return 0;
}
#else
static int32 rtk_multicast_scream_vid_read( char *page, char **start, off_t off, int count, int *eof, void *data )
{
	int len;
	len = sprintf(page, "%s %d\n", "rtk_multicast_scream_vid:",RTK_MC_VLANID);


	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count)
		len = count;
	if (len<0)
	  	len = 0;

	return len;
}
#endif
static int rtk_query_for_bridge_port_write(struct file *file, const char *buffer,
			  unsigned long len, void *data)
{
	char tmpbuf[128];
	int num;

	if (buffer && !copy_from_user(tmpbuf, buffer, len))
	{
		tmpbuf[len] = '\0';

		num = sscanf(tmpbuf, "%d",	&RTK_QUERYFORBRIDGEPORT);

		if (num !=  1) {
			printk("invalid  parameter!\n");
		}

	}

	return len;
}
#if defined(CONFIG_RTL_PROC_NEW)
static int32 rtk_query_for_bridge_port_read(struct seq_file *s, void *v)
{

	seq_printf(s,  "%s %d\n", "query for bridge port:",RTK_QUERYFORBRIDGEPORT);

	return 0;
}
#else
static int32 rtk_query_for_bridge_port_read( char *page, char **start, off_t off, int count, int *eof, void *data )
{
	int len;
	len = sprintf(page, "%s %d\n", "query for bridge port:",RTK_QUERYFORBRIDGEPORT);


	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count)
		len = count;
	if (len<0)
	  	len = 0;

	return len;
}
#endif
int checkMulticatTxInfo(struct sk_buff *skb,struct net_device *dev, rtl_nicTx_info	*nicTx)
{
	uint16 vid = 0;
	struct dev_priv *cp;

	cp = NETDRV_PRIV(dev);

	if(!rtl_hw_vlan_enable)
		return 0;

	if (cp->vlan_setting.forwarding_rule == 1)//Bridge out
	{
		if (!RTK_QUERYFORBRIDGEPORT)
			return 0;

		if((skb==NULL) || (dev==NULL) ||(nicTx==NULL))
		{
			return -1;
		}

		vid = get_skb_vid(skb);
		if ((skb->tag.f.tpid == 0) && (skb->data[0]&0x01) && (skb->data[0] != 0xff) && (vid == 0))
		{
			if (!strncmp(dev->name, "eth0", strlen("eth0"))||
				!strncmp(dev->name, "eth2", strlen("eth2"))||
				!strncmp(dev->name, "eth3", strlen("eth3"))||
				!strncmp(dev->name, "eth4", strlen("eth4")))
			{
				nicTx->portlist &= ~RTL_WANPORT_MASK;
			}
		}
	}

	return 0;
}


#ifdef CONFIG_RTL_8367R_SUPPORT	 //mark_8367
//sycn some define from rtk_api.c
#define RTL8367R_LAN_EFID				2
extern uint32 r8367_cpu_port;

void  rtk_8367_vlan_mapping(void)
{
	int i;
	int retVal=0;
	struct net_device *dev;
	   struct dev_priv	  *dp;
	   uint32 pvid;
	uint32 portmsk = 0, untagmsk = 0, fid = 0;
	int32 totalVlans=((sizeof(vlanconfig))/(sizeof(struct rtl865x_vlanConfig)))-2;

	//clear all vlan
	for(i=0;i<4096;i++)
	{
		//retVal=rtk_vlan_set(i, BIT(RTL8367R_WAN) |BIT(r8367_cpu_port) , 0, 0);
		retVal=rtk_vlan_set(i, 0 , 0, 0); //mark_8367
	}
	// clear efid seeting
	for(i=0;i<5;i++)
	{
		retVal = rtk_port_efid_set(i,RTL8367R_LAN_EFID);
	}
	// map  real 819x vlan setting to 8367
	for(i=0; i< totalVlans ; i++)
	{
		dev=(struct net_device *)_rtl86xx_dev.dev[i];
		if (!dev)
		   continue;
		dp = NETDRV_PRIV(dev);
		if (!dp)
		   continue;
		if(dp->id != 0 )
		{
			rtl865x_tblAsicDrv_vlanParam_t vlan;
			unsigned short vid ;
			vid = dp->id ;
			rtl8651_findAsicVlanIndexByVid(&vid);
			printk("dp->id = %d , vid = %d \n",dp->id,vid);
			if ( rtl8651_getAsicVlan(vid, &vlan ) == FAILED )
			{
				printk("no HW vlan entry found \n");
			   	continue;
			}
			//  if this vlan tagge to CPU in 819x then transfer it to tagged to  8367 CPU
			//if wan port untag ,then to CPU no need tag
			//if((vlan.memberPortMask & vlan.untagPortMask  & BIT(RTL8367R_WAN)) == BIT(RTL8367R_WAN))
			if((vlan.memberPortMask & vlan.untagPortMask  & RTL_WANPORT_MASK) == RTL_WANPORT_MASK)
				vlan.untagPortMask  = (vlan.untagPortMask & 0x3f) | BIT(r8367_cpu_port) ;
			else if((vlan.memberPortMask & vlan.untagPortMask  & RTL_CPUPORT_MASK) == RTL_CPUPORT_MASK)
				vlan.untagPortMask  = (vlan.untagPortMask & 0x3f) | BIT(r8367_cpu_port) ;
			else
				vlan.untagPortMask  = (vlan.untagPortMask & 0x3f) ;
			// exclude 819x cpu port , and always add 8367 cpu port
			vlan.memberPortMask = (vlan.memberPortMask & 0x3f) | BIT(r8367_cpu_port);
					 //mark_mcdebug
			if(i==1) // wan dev, always set wan vlan with all port and untag to lan port to avoid some issue when 819X  CPU tag enabled.
			{
				#if 0
				vlan.memberPortMask = vlan.memberPortMask | 0xf; //include port0~port3
				vlan.untagPortMask	=  vlan.untagPortMask | 0xf; //include port0~port3
				#else
				vlan.memberPortMask = vlan.memberPortMask | (RTL_LANPORT_MASK&0x1f); //include port0~port3
				vlan.untagPortMask	=  vlan.untagPortMask | (RTL_LANPORT_MASK&0x1f); //include port0~port3
				#endif
			}
		 	retVal=rtk_vlan_set(vlan.vid, vlan.memberPortMask, vlan.untagPortMask, vlan.fid);
		}

	}

	if(wan_management_vlan.vlan)
	{

		retVal = rtk_vlan_get(wan_management_vlan.id, &portmsk, &untagmsk, &fid);
		if (retVal == 0)
		{
			//wan vlan already exist. wan equal nat vlan, tag behavior should consistency.
			portmsk |= RTL_WANPORT_MASK |BIT(r8367_cpu_port);
			retVal=rtk_vlan_set(wan_management_vlan.id, portmsk , untagmsk, 1);
		}
		else
		{
		if(wan_management_vlan.tag)
			//retVal=rtk_vlan_set(wan_management_vlan.id, BIT(RTL8367R_WAN) |BIT(r8367_cpu_port) , 0, 1);
			retVal=rtk_vlan_set(wan_management_vlan.id, RTL_WANPORT_MASK |BIT(r8367_cpu_port) , 0, 1);
		else // wan untag out
			//retVal=rtk_vlan_set(wan_management_vlan.id, BIT(RTL8367R_WAN) |BIT(r8367_cpu_port) , BIT(RTL8367R_WAN) , 1);
			retVal=rtk_vlan_set(wan_management_vlan.id, RTL_WANPORT_MASK |BIT(r8367_cpu_port) , RTL_WANPORT_MASK , 1);
		}
	}

	// map  real 819x pvid setting to 8367
	for(i=0;i<5;i++)
	{
		retVal = rtl8651_getAsicPVlanId(i,&pvid);
		retVal=rtk_vlan_portPvid_set(i, pvid,0);
	}
	// 819x cpu pvid to 8367
	retVal = rtl8651_getAsicPVlanId(RTK_CPU_PORT,&pvid);
	retVal=rtk_vlan_portPvid_set(r8367_cpu_port, pvid,0);

	// set p4(wan) -> p6(cpu) transpatent , p6-> p4 transparent
	//retVal = rtk_vlan_transparent_set(RTL8367R_WAN, BIT(r8367_cpu_port), 1); //mark_8367
	//retVal = rtk_vlan_transparent_set(r8367_cpu_port, BIT(RTL8367R_WAN), 1);

}

void rtk_get_real_nicTxVid(struct sk_buff *skb , unsigned short *nictx_vid)
{
	unsigned short vid ;
	if(!rtl_hw_vlan_enable)
		return ;

	if( (*nictx_vid) != RTK_NAT_VLANID) //only NAT WAN port need to consider how to direct tx a vid , if it need to be changed for, manage or MC
		return ;

	if(((skb->data[0] & 0x01) &&(skb->data[0] != 0xFF )) && is_rtl_mc_vlan_tagged(&vid))
		(*nictx_vid) = vid;
#ifndef 	RTK_MANAGE_VLAN_IF
	 else if( is_rtl_manage_vlan_tagged(skb,&vid))     //if it is manamgat vlan packet from CPU ?
		(*nictx_vid) = vid;
#endif

	return ;
}
#endif
#endif

#if defined(RTL8196C_EEE_MAC)
extern int eee_enabled;
extern void eee_phy_enable(void);
extern void eee_phy_disable(void);
#ifdef CONFIG_RTL8196C_REVISION_B
static unsigned char prev_port_sts[MAX_PORT_NUMBER] = { 0, 0, 0, 0, 0 };
#endif
#endif
#if defined(CONFIG_RTL_LOCAL_PUBLIC)
static int32 rtl865x_getPortlistByMac(const unsigned char *mac,uint32 *portlist);
#endif
static inline int rtl_isWanDev(struct dev_priv *cp);

#ifdef CONFIG_RTL8196C_ETH_IOT
uint32 port_link_sts = 0;	// the port which already linked up does not need to check ...
uint32 port_linkpartner_eee = 0;
#endif

#ifdef CONFIG_RTL_8196C_ESD
int _96c_esd_counter = 0;
int _96c_esd_reboot_counter = 0;
#endif

#if defined(PATCH_GPIO_FOR_LED)
#define MIB_RX_PKT_CNT	0
#define MIB_TX_PKT_CNT	1

#ifdef CONFIG_RTL_8197F
#define PORT_GPIO_BASE		22	// Base of P0~P4 at PEFGH, port 0~4: G6~H2
#define PGPIO_CNR 			PEFGH_CNR
#define PGPIO_DIR 			PEFGH_DIR
#define PGPIO_DAT 			PEFGH_DAT
#else
#define PORT_PABCD_BASE	10	// Base of P0~P1 at PABCD
#define P0_PABCD_BIT		10
#define P1_PABCD_BIT		11
#define P2_PABCD_BIT		12
#define P3_PABCD_BIT		13
#define P4_PABCD_BIT		14
#define PORT_GPIO_BASE		PORT_PABCD_BASE
#define PGPIO_CNR 			PABCD_CNR
#define PGPIO_DIR 			PABCD_DIR
#define PGPIO_DAT 			PABCD_DAT
#endif

#define SUCCESS 0
#define FAILED -1

#define GPIO_LED_NOBLINK_TIME		(12*HZ/10)	// time more than 1-sec timer interval
#define GPIO_LED_ON_TIME			(4*HZ/100)	// 40ms
#define GPIO_LED_ON					0
#define GPIO_LED_OFF					1
#define GPIO_LINK_STATUS			1
#define GPIO_LINK_STATE_CHANGE 0x80000000

#define GPIO_UINT32_DIFF(a, b)		((a >= b)? (a - b):(0xffffffff - b + a + 1))

struct ctrl_led {
	struct timer_list	LED_Timer;
	unsigned int			LED_Interval;
	unsigned char		LED_Toggle;
	unsigned char		LED_ToggleStart;
	unsigned int			LED_tx_cnt_log;
	unsigned int			LED_rx_cnt_log;
	unsigned int			LED_tx_cnt;
	unsigned int			LED_rx_cnt;
	unsigned int			link_status;
	unsigned char			blink_once_done;		// 1: blink once done
} led_cb[5];

static int32 rtl819x_getAsicMibCounter(int port, uint32 counter, uint32 *value)
{
	rtl865x_tblAsicDrv_simpleCounterParam_t simpleCounter;
	rtl8651_getSimpleAsicMIBCounter(port, &simpleCounter);

	switch(counter){
		case MIB_RX_PKT_CNT:
			*value=simpleCounter.rxPkts;
			break;
		case MIB_TX_PKT_CNT:
			*value=simpleCounter.txPkts;
			break;
		default:
			return FAILED;
	}
	return SUCCESS;
}
static void gpio_set_led(int port, int flag){
	if (flag == GPIO_LED_OFF){
/*		RTL_W32(PGPIO_CNR, RTL_R32(PGPIO_CNR) & (~((0x1<<port)<<PORT_GPIO_BASE)));	//set GPIO pin
*		RTL_W32(PGPIO_DIR, RTL_R32(PGPIO_DIR) | ((0x1<<port)<<PORT_GPIO_BASE));//output pin
*/
		RTL_W32(PGPIO_DAT, (RTL_R32(PGPIO_DAT) | ((0x1<<port)<<PORT_GPIO_BASE)));//set 1
	}
	else{
/*		RTL_W32(PGPIO_CNR, RTL_R32(PGPIO_CNR) & (~((0x1<<port)<<PORT_GPIO_BASE)));	//set GPIO pin
*		RTL_W32(PGPIO_DIR, RTL_R32(PGPIO_DIR) | ((0x1<<port)<<PORT_GPIO_BASE));//output pin
*/
		RTL_W32(PGPIO_DAT, (RTL_R32(PGPIO_DAT) & (~((0x1<<port)<<PORT_GPIO_BASE))));//set 0
	}
}

static void gpio_led_Interval_timeout(unsigned long port)
{
	struct ctrl_led *cb	= &led_cb[port];
#ifndef CONFIG_RTL_8198C
	unsigned long flags=0;

	SMP_LOCK_ETH(flags);
#endif

	if (cb->link_status & GPIO_LINK_STATE_CHANGE) {
		cb->link_status &= ~GPIO_LINK_STATE_CHANGE;
		if (cb->link_status & GPIO_LINK_STATUS)
			cb->LED_ToggleStart = GPIO_LED_ON;
		else
			cb->LED_ToggleStart = GPIO_LED_OFF;
		cb->LED_Toggle = cb->LED_ToggleStart;
		gpio_set_led(port, cb->LED_Toggle);
	}
	else {
		if (cb->link_status & GPIO_LINK_STATUS)
			gpio_set_led(port, cb->LED_Toggle);
	}

	if (cb->link_status & GPIO_LINK_STATUS)	 {
		if (cb->LED_Toggle == cb->LED_ToggleStart){
			mod_timer(&cb->LED_Timer, jiffies + cb->LED_Interval);
			cb->blink_once_done=1;
		}
		else{
			mod_timer(&cb->LED_Timer, jiffies + GPIO_LED_ON_TIME);
			cb->blink_once_done=0;
		}

		cb->LED_Toggle = (cb->LED_Toggle + 1) & 0x1;	//cb->LED_Toggle = (cb->LED_Toggle + 1) % 2;
	}
#ifndef CONFIG_RTL_8198C
	SMP_UNLOCK_ETH(flags);
#endif
}

void calculate_led_interval(int port)
{
	struct ctrl_led *cb	= &led_cb[port];

	unsigned int delta = 0;

	/* calculate counter delta	*/
	delta += GPIO_UINT32_DIFF(cb->LED_tx_cnt, cb->LED_tx_cnt_log);
	delta += GPIO_UINT32_DIFF(cb->LED_rx_cnt, cb->LED_rx_cnt_log);
	cb->LED_tx_cnt_log = cb->LED_tx_cnt;
	cb->LED_rx_cnt_log = cb->LED_rx_cnt;

	/* update interval according to delta	*/
	if (delta == 0) {
		if (cb->LED_Interval == GPIO_LED_NOBLINK_TIME)
			mod_timer(&(cb->LED_Timer), jiffies + cb->LED_Interval);
		else{
			cb->LED_Interval = GPIO_LED_NOBLINK_TIME;
			if(cb->blink_once_done==1){
				mod_timer(&(cb->LED_Timer), jiffies + cb->LED_Interval);
				cb->blink_once_done=0;
			}
		}
	}
	else
	{
		if(delta>25){		//That is: 200/delta-GPIO_LED_ON_TIME < GPIO_LED_ON_TIME
			cb->LED_Interval = GPIO_LED_ON_TIME;
		}
		else{
			/*	if delta is odd, should be +1 into even.		*/
			/*	just make led blink more stable and smooth.	*/
			if((delta & 0x1) == 1)
				delta++;

			cb->LED_Interval=200/delta-GPIO_LED_ON_TIME;		/* rx 1pkt + tx 1pkt => blink one time!	*/

/*			if (cb->LED_Interval < GPIO_LED_ON_TIME)
*				cb->LED_Interval = GPIO_LED_ON_TIME;
*/
		}
	}
}

void update_mib_counter(int port)
{
	uint32 regVal;
	struct ctrl_led *cb	= &led_cb[port];

	regVal=READ_MEM32(PSRP0+(port<<2));
	if((regVal&PortStatusLinkUp)!=0){
		//link up
		if (!(cb->link_status & GPIO_LINK_STATUS)) {
			cb->link_status = GPIO_LINK_STATE_CHANGE | 1;
		}
		rtl819x_getAsicMibCounter(port, MIB_TX_PKT_CNT, (uint32 *)&cb->LED_tx_cnt);
		rtl819x_getAsicMibCounter(port, MIB_RX_PKT_CNT, (uint32 *)&cb->LED_rx_cnt);
	}
	else{
		//link down
		if (cb->link_status & GPIO_LINK_STATUS) {
			cb->link_status = GPIO_LINK_STATE_CHANGE;
		}
	}
}

void init_led_ctrl(int port)
{
	struct ctrl_led *cb	= &led_cb[port];

	RTL_W32(PGPIO_CNR, RTL_R32(PGPIO_CNR) & (~((0x1<<port)<<PORT_GPIO_BASE)));	//set GPIO pin
	RTL_W32(PGPIO_DIR, RTL_R32(PGPIO_DIR) | ((0x1<<port)<<PORT_GPIO_BASE));//output pin

	memset(cb, '\0', sizeof(struct ctrl_led));

	update_mib_counter(port);
	calculate_led_interval(port);
	cb->link_status |= GPIO_LINK_STATE_CHANGE;

	init_timer(&cb->LED_Timer);
	cb->LED_Timer.expires = jiffies + cb->LED_Interval;
	cb->LED_Timer.data = (unsigned long)port;
	cb->LED_Timer.function = gpio_led_Interval_timeout;
	mod_timer(&cb->LED_Timer, jiffies + cb->LED_Interval);

	gpio_led_Interval_timeout(port);
}

void disable_led_ctrl(int port)
{
	struct ctrl_led *cb	= &led_cb[port];
	gpio_set_led(port, GPIO_LED_OFF);

	if (timer_pending(&cb->LED_Timer))
		del_timer_sync(&cb->LED_Timer);
}
#endif // PATCH_GPIO_FOR_LED

#if defined(CONFIG_RTL_819XD)&&defined(CONFIG_RTL_8211DS_SUPPORT)&&defined(CONFIG_RTL_8197D)
int lanPortMask = 0x10f;
int wanPortMask = 0x10;
#if defined(CONFIG_RTK_VLAN_SUPPORT) || defined (CONFIG_RTL_MULTI_LAN_DEV)
int  lanPortMask1 = 0x8;
int  lanPortMask2 = 0x4;
int  lanPortMask3 = 0x2;
int  lanPortMask4 = 0x1;
#endif

#undef	RTL_WANPORT_MASK
#undef	RTL_LANPORT_MASK
#if defined(CONFIG_RTK_VLAN_SUPPORT) || defined (CONFIG_RTL_MULTI_LAN_DEV)
#undef 	RTL_LANPORT_MASK_1
#undef	RTL_LANPORT_MASK_2
#undef 	RTL_LANPORT_MASK_3
#undef 	RTL_LANPORT_MASK_4
#endif

#define	RTL_WANPORT_MASK		wanPortMask
#define	RTL_LANPORT_MASK		lanPortMask
#if defined(CONFIG_RTK_VLAN_SUPPORT) || defined (CONFIG_RTL_MULTI_LAN_DEV)
#define 	RTL_LANPORT_MASK_1	lanPortMask1
#define	RTL_LANPORT_MASK_2	lanPortMask2
#define 	RTL_LANPORT_MASK_3	lanPortMask3
#define 	RTL_LANPORT_MASK_4	lanPortMask4
#endif

void rtl_setPppMask(void)
{
	int i;
	int totalVlans;
	totalVlans=((sizeof(vlanconfig))/(sizeof(struct rtl865x_vlanConfig)))-1;
	for(i=0;i<totalVlans;i++)
	{
		if(vlanconfig[i].if_type==IF_PPPOE){
			vlanconfig[i].memPort = 0x1;
			vlanconfig[i].untagSet = 0x1;
		}
	}
}

void rtl_resetRegisterNotFound8211ds(void)
{
	REG32(0xbb804104) =0x00FF2039;
	REG32(0xbb80414c) =0;
	REG32(0xbb804100) =0;
}

void rtl_setPortMask(uint32 reg_data)
{
	if((reg_data != 0)&&(reg_data != 0xFFFF))
	{
		/*8211ds is found*/
		lanPortMask   = 0x11e;
		wanPortMask = 0x1;
		#if defined(CONFIG_RTK_VLAN_SUPPORT) || defined (CONFIG_RTL_MULTI_LAN_DEV)
		lanPortMask1 = 0x10;
		lanPortMask2 = 0x8;
		lanPortMask3 = 0x4;
		lanPortMask4 = 0x2;
		#endif
		rtl_setPppMask();

		// Flow control DSC tolerance: change to 32 pages to fix "port 0 (8211D) has Rx CRC" issue.
		REG32(MACCR) = (REG32(MACCR) & ~CF_FCDSC_MASK) | (0x20 << CF_FCDSC_OFFSET);
	}
	else
	{
		/*8211ds is not found*/
		lanPortMask   = 0x10f;
		wanPortMask = 0x10;
		#if defined(CONFIG_RTK_VLAN_SUPPORT) || defined (CONFIG_RTL_MULTI_LAN_DEV)
		lanPortMask1 = 0x8;
		lanPortMask2 = 0x4;
		lanPortMask3 = 0x2;
		lanPortMask4 = 0x1;
		#endif
		rtl_resetRegisterNotFound8211ds();
	}
}
#endif


/*
device mapping mainten
*/
struct rtl865x_vlanConfig * rtl_get_vlanconfig_by_netif_name(const char *name)
{
	int i;
	for(i= 0; vlanconfig[i].vid != 0;i++)
	{
		if(memcmp(vlanconfig[i].ifname,name,strlen(name)) == 0)
			return &vlanconfig[i];
	}

	#if defined(CONFIG_RTL_MULTIPLE_WAN)
	//check multiple wan device
	if(memcmp(rtl_multiWan_config.ifname,name,strlen(name)) == 0)
		return &rtl_multiWan_config;
	#endif

	return NULL;
}

#if 1
void rtl_ps_drv_netif_mapping_show(void)
{
	int i;
	DEBUG_ERR("linux netif name VS driver netif name mapping:\n");
	for(i = 0; i < NETIF_NUMBER;i++)
	{
		DEBUG_ERR("valid(%d),linux netif name(%s) <---->drv netif name(%s)\n",ps_drv_netif_mapping[i].valid,
			ps_drv_netif_mapping[i].ps_netif?ps_drv_netif_mapping[i].ps_netif->name:NULL,ps_drv_netif_mapping[i].drvName);
	}
}
#endif

static int rtl_ps_drv_netif_mapping_init(void)
{
	memset(ps_drv_netif_mapping,0,NETIF_NUMBER * sizeof(ps_drv_netif_mapping_t));
	return SUCCESS;
}

int rtl_get_ps_drv_netif_mapping_by_psdev_name(const char *psname, char *netifName)
{
	int i;
	if(netifName == NULL || strlen(psname) >= MAX_IFNAMESIZE )
		return FAILED;

	for(i = 0; i < NETIF_NUMBER;i++)
	{
		if(ps_drv_netif_mapping[i].valid == 1 && memcmp(ps_drv_netif_mapping[i].ps_netif->name,psname,strlen(psname)) == 0)
		{
			memcpy(netifName,ps_drv_netif_mapping[i].drvName,MAX_IFNAMESIZE);
			return SUCCESS;
		}
	}

	//back compatible,user use br0 to get lan netif
	if(memcmp(psname,RTL_PS_BR0_DEV_NAME,strlen(RTL_PS_BR0_DEV_NAME)) == 0)
	{
		for(i = 0; i < NETIF_NUMBER;i++)
		{
			if(ps_drv_netif_mapping[i].valid == 1 &&
				memcmp(ps_drv_netif_mapping[i].drvName,RTL_DRV_LAN_NETIF_NAME,strlen(RTL_DRV_LAN_NETIF_NAME)) == 0)
				{
					memcpy(netifName,ps_drv_netif_mapping[i].drvName,MAX_IFNAMESIZE);
					return SUCCESS;
				}
		}
	}

	return FAILED;
}

ps_drv_netif_mapping_t* rtl_get_ps_drv_netif_mapping_by_psdev(struct net_device *dev)
{
	int i;
	for(i = 0; i < NETIF_NUMBER;i++)
	{
		if(ps_drv_netif_mapping[i].valid == 1 && ps_drv_netif_mapping[i].ps_netif == dev)
			return &ps_drv_netif_mapping[i];
	}

	//back compatible,user use br0 to get lan netif
	if(memcmp(dev->name,RTL_PS_BR0_DEV_NAME,strlen(RTL_PS_BR0_DEV_NAME)) == 0)
	{
		for(i = 0; i < NETIF_NUMBER;i++)
		{
			if(ps_drv_netif_mapping[i].valid == 1 &&
				memcmp(ps_drv_netif_mapping[i].drvName,RTL_DRV_LAN_NETIF_NAME,strlen(RTL_DRV_LAN_NETIF_NAME)) == 0)
				return &ps_drv_netif_mapping[i];
		}
	}

	return NULL;
}

int rtl_add_ps_drv_netif_mapping(struct net_device *dev, const char *name)
{
	int i;

	//duplicate check
	if(rtl_get_ps_drv_netif_mapping_by_psdev(dev) !=NULL)
		return FAILED;

	for(i = 0; i < NETIF_NUMBER;i++)
	{
		if(ps_drv_netif_mapping[i].valid == 0)
			break;
	}

	if(i == NETIF_NUMBER)
		return FAILED;

	ps_drv_netif_mapping[i].ps_netif = dev;
	memcpy(ps_drv_netif_mapping[i].drvName,name,strlen(name));
	ps_drv_netif_mapping[i].valid = 1;
	return SUCCESS;
}
EXPORT_SYMBOL(rtl_add_ps_drv_netif_mapping);

int rtl_del_ps_drv_netif_mapping(struct net_device *dev)
{
	ps_drv_netif_mapping_t *entry;
	entry = rtl_get_ps_drv_netif_mapping_by_psdev(dev);
	if(entry)
		entry->valid = 0;

	return SUCCESS;
}
EXPORT_SYMBOL(rtl_del_ps_drv_netif_mapping);

/*
 * Disable TX/RX through IO_CMD register
 */
static void rtl8186_stop_hw(struct net_device *dev, struct dev_priv *cp)
{

#if defined(PATCH_GPIO_FOR_LED)
	if (cp->id == RTL_LANVLANID) {
		int port;
		for (port=0; port<RTL8651_PHY_NUMBER; port++)
			disable_led_ctrl(port);
	}
#endif

}

//#ifndef CONFIG_RTL_8198C // modified by lynn_pu, 2014-10-16
#if !defined(CONFIG_RTL_8198C) && (LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0))
/* Set or clear the multicast filter for this adaptor.
 * This routine is not state sensitive and need not be SMP locked.
 */
static void re865x_set_rx_mode (struct net_device *dev){
/*	Not yet implemented.
	unsigned long flags;
	spin_lock_irqsave (&_rtl86xx_dev.lock, flags);
	spin_unlock_irqrestore (&_rtl86xx_dev.lock, flags);
*/
}
#endif

#if defined(CONFIG_RTL_8367R_SUPPORT) || defined(CONFIG_RTL_8370_SUPPORT)
rtk_stat_port_cntr_t  port_cntrs;
#endif

#if defined (CONFIG_RTL_INBAND_CTL_API)
struct port_stats port_stats[6];
int rtl_get_portRate(unsigned int port,unsigned long* rxRate, unsigned long *txRate)
{
	int ret =FAILED;
	if((port>=0)&&port<CPU)
	{
		*rxRate = port_stats[port].rx_rate>>7; //byte->kb
		*txRate = port_stats[port].tx_rate>>7;
		ret=SUCCESS;
	}
	return ret;
}
#endif

#if defined (CONFIG_RTL_NIC_HWSTATS)
 void  re865x_accumulate_port_stats(uint32 portnum, struct net_device_stats *net_stats)
{
#if defined(CONFIG_RTL_8367R_SUPPORT) || defined(CONFIG_RTL_8370_SUPPORT)
	#ifdef CONFIG_RTL_8367R_SUPPORT
	if( portnum < 0 ||  portnum > CPU)
			return ;
	#endif

	rtk_stat_port_getAll(portnum, &port_cntrs);

	/* rx_pkt = rx_unicast +rx_multicast + rx_broadcast */
	net_stats->rx_packets += port_cntrs.ifInUcastPkts;
	net_stats->rx_packets += port_cntrs.etherStatsMcastPkts;
	net_stats->rx_packets += port_cntrs.etherStatsBcastPkts;

	/* tx_pkt = tx_unicast +tx_multicast + tx_broadcast*/
	net_stats->tx_packets += port_cntrs.ifOutUcastPkts;
	net_stats->tx_packets += port_cntrs.ifOutMulticastPkts;
	net_stats->tx_packets += port_cntrs.ifOutBrocastPkts;

	net_stats->rx_bytes += port_cntrs.ifInOctets;
	net_stats->tx_bytes += port_cntrs.ifOutOctets;
	#if defined (CONFIG_RTL_INBAND_CTL_API)
	port_stats[portnum].rx_bytes_current= port_cntrs.ifInOctets;
	port_stats[portnum].tx_bytes_current= port_cntrs.ifOutOctets;
	#endif

	/*rx_errors = CRC error + Jabber error  + Fragment error*/
	net_stats->rx_errors += port_cntrs.dot3StatsFCSErrors;
	net_stats->rx_errors += port_cntrs.etherStatsJabbers;
	net_stats->rx_errors += port_cntrs.etherStatsFragments;
	net_stats->tx_dropped += port_cntrs.dot1dTpPortInDiscards;
	net_stats->multicast += port_cntrs.etherStatsMcastPkts;

	net_stats->collisions += port_cntrs.etherStatsCollisions;
	net_stats->rx_crc_errors += port_cntrs.dot3StatsFCSErrors;
#else
	uint32 addrOffset_fromP0 =0;

	if( portnum < 0 ||  portnum > CPU)
			return ;
	addrOffset_fromP0 = portnum * MIB_ADDROFFSETBYPORT;
	/* rx_pkt = rx_unicast +rx_multicast + rx_broadcast */
	net_stats->rx_packets += rtl8651_returnAsicCounter( OFFSET_IFINUCASTPKTS_P0 + addrOffset_fromP0 ) ;
	net_stats->rx_packets += rtl8651_returnAsicCounter( OFFSET_ETHERSTATSMULTICASTPKTS_P0 + addrOffset_fromP0 ) ;
	net_stats->rx_packets += rtl8651_returnAsicCounter( OFFSET_ETHERSTATSBROADCASTPKTS_P0 + addrOffset_fromP0 ) ;
	/* tx_pkt = tx_unicast +tx_multicast + tx_broadcast*/
	net_stats->tx_packets += rtl8651_returnAsicCounter( OFFSET_IFOUTUCASTPKTS_P0 + addrOffset_fromP0 ) ;
	net_stats->tx_packets += rtl8651_returnAsicCounter( OFFSET_IFOUTMULTICASTPKTS_P0 + addrOffset_fromP0 ) ;
	net_stats->tx_packets += rtl8651_returnAsicCounter( OFFSET_IFOUTBROADCASTPKTS_P0 + addrOffset_fromP0 ) ;

	net_stats->rx_bytes += rtl8651_returnAsicCounter( OFFSET_IFINOCTETS_P0 + addrOffset_fromP0 ) ;
	net_stats->rx_bytes += (rtl8651_returnAsicCounter( OFFSET_IFINOCTETS_P0+4 + addrOffset_fromP0 )<<22);
	net_stats->tx_bytes += rtl8651_returnAsicCounter( OFFSET_IFOUTOCTETS_P0 + addrOffset_fromP0 ) ;
	net_stats->tx_bytes += (rtl8651_returnAsicCounter( OFFSET_IFOUTOCTETS_P0+4 + addrOffset_fromP0 )<<22) ;
	#if defined (CONFIG_RTL_INBAND_CTL_API)
	port_stats[portnum].rx_bytes_current= rtl8651_returnAsicCounter( OFFSET_IFINOCTETS_P0 + addrOffset_fromP0 ) ;;
	port_stats[portnum].rx_bytes_current+= (rtl8651_returnAsicCounter( OFFSET_IFINOCTETS_P0+4 + addrOffset_fromP0 )<<22);
	port_stats[portnum].tx_bytes_current= rtl8651_returnAsicCounter( OFFSET_IFOUTOCTETS_P0 + addrOffset_fromP0 );
	port_stats[portnum].tx_bytes_current+= (rtl8651_returnAsicCounter( OFFSET_IFOUTOCTETS_P0+4 + addrOffset_fromP0 )<<22) ;
	#endif

	/*rx_errors = CRC error + Jabber error  + Fragment error*/
	net_stats->rx_errors += rtl8651_returnAsicCounter( OFFSET_DOT3STATSFCSERRORS_P0 + addrOffset_fromP0 ) ;
	net_stats->rx_errors += rtl8651_returnAsicCounter( OFFSET_ETHERSTATSJABBERS_P0 + addrOffset_fromP0 ) ;
	net_stats->rx_errors += rtl8651_returnAsicCounter( OFFSET_ETHERSTATSOVERSIZEPKTS_P0 + addrOffset_fromP0 ) ;
	//OFFSET_DOT1DTPPORTINDISCARDS_P0?
	//net_stats->rx_dropped += rtl8651_returnAsicCounter( OFFSET_DOT1DTPPORTINDISCARDS_P0 + addrOffset_fromP0 ) ;
	net_stats->tx_dropped += rtl8651_returnAsicCounter( OFFSET_IFOUTDISCARDS + addrOffset_fromP0 ) ;
	net_stats->multicast += rtl8651_returnAsicCounter( OFFSET_ETHERSTATSMULTICASTPKTS_P0 + addrOffset_fromP0 ) ;

	net_stats->collisions += rtl8651_returnAsicCounter( OFFSET_ETHERSTATSCOLLISIONS_P0 + addrOffset_fromP0 ) ;
	net_stats->rx_crc_errors += rtl8651_returnAsicCounter( OFFSET_DOT3STATSFCSERRORS_P0 + addrOffset_fromP0 ) ;
#endif
	return;
}

void re865x_get_hardwareStats(struct dev_priv  *priv)
{
	uint32 portmask;
	uint32 port;

	portmask = priv->portmask;
	//rx_dropped = priv->net_stats.rx_dropped;
	//memset( &priv->net_stats, 0, sizeof(struct net_device_stats) );
	/* reset counters to 0 */
	priv->net_stats.rx_packets = 0;
	priv->net_stats.tx_packets = 0;
	priv->net_stats.rx_bytes = 0;
	priv->net_stats.tx_bytes = 0;
	priv->net_stats.rx_errors = 0;
	priv->net_stats.tx_dropped = 0;
	priv->net_stats.multicast = 0;
	priv->net_stats.collisions = 0;
	priv->net_stats.rx_crc_errors = 0;
	#if defined (CONFIG_RTL_INBAND_CTL_API)
	for( port = 0; port < CPU; port++)
	{
		port_stats[port].rx_rate = 0;
		port_stats[port].tx_rate = 0;
	}
	#endif

#ifdef CONFIG_RTL_8370_SUPPORT
	if (priv->id == RTL_LANVLANID) {
		for( port = 0; port < 8; port++) {
#if defined (CONFIG_RTL_INBAND_CTL_API)
			port_stats[port].rx_bytes_last = port_stats[port].rx_bytes_current;
			port_stats[port].tx_bytes_last = port_stats[port].tx_bytes_current;
#endif
			re865x_accumulate_port_stats(port, &priv->net_stats);
#if defined (CONFIG_RTL_INBAND_CTL_API)
			port_stats[port].rx_rate= port_stats[port].rx_bytes_current-port_stats[port].rx_bytes_last;
			port_stats[port].tx_rate= port_stats[port].tx_bytes_current-port_stats[port].tx_bytes_last;
#endif
		}
	}
	else if (priv->id == RTL_WANVLANID) {
#if defined (CONFIG_RTL_INBAND_CTL_API)
			port_stats[RTL83XX_WAN].rx_bytes_last = port_stats[RTL83XX_WAN].rx_bytes_current;
			port_stats[RTL83XX_WAN].tx_bytes_last = port_stats[RTL83XX_WAN].tx_bytes_current;
#endif
		re865x_accumulate_port_stats(RTL83XX_WAN, &priv->net_stats);
#if defined (CONFIG_RTL_INBAND_CTL_API)
			port_stats[RTL83XX_WAN].rx_rate= port_stats[RTL83XX_WAN].rx_bytes_current-port_stats[RTL83XX_WAN].rx_bytes_last;
			port_stats[RTL83XX_WAN].tx_rate= port_stats[RTL83XX_WAN].tx_bytes_current-port_stats[RTL83XX_WAN].tx_bytes_last;
#endif

	}
	return;
#endif

	for( port = 0; port < CPU; port++)
	{
		if((1<<port) & portmask)
		{
#if defined (CONFIG_RTL_INBAND_CTL_API)
			port_stats[port].rx_bytes_last = port_stats[port].rx_bytes_current;
			port_stats[port].tx_bytes_last = port_stats[port].tx_bytes_current;
#endif
			re865x_accumulate_port_stats(port, &priv->net_stats);
#if defined (CONFIG_RTL_INBAND_CTL_API)
			port_stats[port].rx_rate= port_stats[port].rx_bytes_current-port_stats[port].rx_bytes_last;
			port_stats[port].tx_rate= port_stats[port].tx_bytes_current-port_stats[port].tx_bytes_last;
#endif
		}

	}
	return;
}
#endif

static struct net_device_stats *re865x_get_stats(struct net_device *dev){
	struct dev_priv  *dp;
	dp = NETDRV_PRIV(dev);
	#if defined (CONFIG_RTL_NIC_HWSTATS)
		re865x_get_hardwareStats(dp);
	#endif
	return &dp->net_stats;
}

#if defined (CONFIG_RTL_MULTI_LAN_DEV) || defined(CONFIG_RTK_VLAN_SUPPORT) || defined(CONFIG_RTL_VLAN_8021Q) || defined(CONFIG_RTL_DNS_TRAP)
static int32 re865x_packVlanConfig(struct rtl865x_vlanConfig vlanConfig1[],  struct rtl865x_vlanConfig vlanConfig2[])
{
	int i, j;
	uint32 vlanCnt=0;
	uint32 found=FALSE;
	/*get input vlan config entry number*/
	for(i=0; vlanConfig1[i].ifname[0] != '\0'; i++)
	{
		if(vlanConfig1[i].vid != 0)
			vlanCnt++;
	}

	if(vlanCnt+1 > NETIF_NUMBER)
		DEBUG_ERR("ERROR,vlanCnt(%d) > max size %d\n",vlanCnt,NETIF_NUMBER-1);

	/*initialize output vlan config*/
	memset(vlanConfig2, 0 , (vlanCnt+1)*sizeof(struct rtl865x_vlanConfig));

	for(i=0; vlanConfig1[i].ifname[0] != '\0'; i++)
		{

		found=FALSE;

		if(vlanConfig1[i].vid == 0)
			continue;

		for(j=0; j<vlanCnt; j++)
		{
			if(vlanConfig1[i].if_type != vlanConfig2[j].if_type)
			{
				continue;
			}
			else
			{
				if(vlanConfig1[i].if_type==IF_ETHER)
				{
					/*ethernet interface*/
#if defined(CONFIG_RTL_MAC_BASED_NETIF)
					/* Multiple WAN can has the same vlan id*/
					if ((vlanConfig1[i].isWan == 1) || (vlanConfig1[i].vid!=vlanConfig2[j].vid))
#else

					/*if multiple vlan config has the same vlan id*/
					/*the first one will decide the  real network interface name/asic mtu/hardware address*/

					/*ethernet interface*/
					if(vlanConfig1[i].vid!=vlanConfig2[j].vid)
#endif
					{
						continue;
					}
					else
					{
						found=TRUE;
						break;
					}
				}
				else
				{
					/*PPP interface*/
					if(strcmp(vlanConfig1[i].ifname, vlanConfig2[j].ifname))
					{
						continue;
					}
					else
					{
						found=TRUE;
						break;
					}
				}
			}
		}

		if(found==TRUE)
		{
			/*merge port mask*/

			vlanConfig2[j].memPort |=vlanConfig1[i].memPort;
			vlanConfig2[j].untagSet |=vlanConfig1[i].untagSet;
		}
		else
		{
			/*find an empty entry to store this vlan config*/
			for(j=0; j<vlanCnt; j++)
			{
				if(vlanConfig2[j].vid==0)
				{
					memcpy(&vlanConfig2[j], &vlanConfig1[i], sizeof(struct rtl865x_vlanConfig));

					if(( vlanConfig1[i].if_type==IF_ETHER) && (vlanConfig1[i].isWan==FALSE) )
					{
							/*add cpu port to lan member list*/
						vlanConfig2[j].memPort|=0x100;
						vlanConfig2[j].untagSet|=0x100;
					}
					break;

				}
			}
		}

	}
	return SUCCESS;
}
#endif

static void rtl865x_disableDevPortForward(struct net_device *dev, struct dev_priv *cp)
{
	int port;

#if defined(CONFIG_RTL_8370_SUPPORT)
	extern int rtk_port_adminEnable_set(uint32 port, uint32 enable);
	if (dev->name[3] == '0') {	// eth0
		for(port=0;port<8;port++)
			rtk_port_adminEnable_set(port, 0);
	}
	else if (dev->name[3] == '1') {	// eth1
		rtk_port_adminEnable_set(RTL83XX_WAN, 0);
	}
	return;
#endif

	for(port=0;port<RTL8651_AGGREGATOR_NUMBER;port++)
	{
		if((1<<port) & cp->portmask)
		{
#ifdef CONFIG_RTL_8367R_SUPPORT
			if (port <=4) {
				//extern int rtk_stp_mstpState_set(int msti, int port, int stp_state);
				//rtk_stp_mstpState_set(0 , port, 1); // 1: STP_STATE_BLOCKING
				extern int rtk_port_adminEnable_set(uint32 port, uint32 enable);
				rtk_port_adminEnable_set(port, 0);
			}
#else
			REG32(PCRP0+(port<<2))= ((REG32(PCRP0+(port<<2)))&(~ForceLink));
			REG32(PCRP0+(port<<2))= ((REG32(PCRP0+(port<<2)))&(~EnablePHYIf));
			TOGGLE_BIT_IN_REG_TWICE(PCRP0+(port<<2),EnablePHYIf);
			TOGGLE_BIT_IN_REG_TWICE(PCRP0+(port<<2),ForceLink);
			TOGGLE_BIT_IN_REG_TWICE(PCRP0+(port<<2),EnForceMode);
#endif
		}
	}
#ifdef CONFIG_RTL_8196C_ESD
	if ((cp->portmask) & 0x10)			// port 4
		_96c_esd_counter = 0;		// stop counting
#endif
}

#if !defined(CONFIG_RTL_8196C)
static void rtl865x_restartDevPHYNway(struct net_device *dev, struct dev_priv *cp)
{
	int port;
	for(port=0;port<RTL8651_AGGREGATOR_NUMBER;port++)
	{
		if((1<<port) & cp->portmask)
		{
			rtl8651_restartAsicEthernetPHYNway(port);
		}
	}
	return;
}
#endif

#if defined(CONFIG_819X_PHY_RW) || defined(CONFIG_RTL_HW_VLAN_SUPPORT)
static void rtl865x_setPortForward(int port_num, int forward)
{
	if(port_num < 0 || port_num >= RTL8651_AGGREGATOR_NUMBER)
		return;

	if(forward == FALSE) {
		REG32(PCRP0+(port_num<<2))= ((REG32(PCRP0+(port_num<<2)))&(~EnablePHYIf));
#ifdef CONFIG_RTL_8196C_ESD
		if (port_num == 4)				// port 4
			_96c_esd_counter = 0;		// stop counting
#endif
	}
	else {
		REG32(PCRP0+(port_num<<2))= ((REG32(PCRP0+(port_num<<2)))|(EnablePHYIf));

#ifdef CONFIG_RTL_8196C_ESD
		if (port_num == 4) {				// port 4
			_96c_esd_counter = 1;		// start counting and check ESD
			_96c_esd_reboot_counter = 0;	// reset counter
		}
#endif
	}
	TOGGLE_BIT_IN_REG_TWICE(PCRP0+(port_num<<2),EnForceMode);

}
#endif	//#if defined(CONFIG_819X_PHY_RW)
static void rtl865x_enableDevPortForward(struct net_device *dev, struct dev_priv *cp)
{
	int port;

#if defined(CONFIG_RTL_8370_SUPPORT)
	extern int rtk_port_adminEnable_set(uint32 port, uint32 enable);
	if (dev->name[3] == '0') {	// eth0
		for(port=0;port<8;port++)
			rtk_port_adminEnable_set(port, 1);
	}
	else if (dev->name[3] == '1') {	// eth1
		rtk_port_adminEnable_set(RTL83XX_WAN, 1);
	}
	return;
#endif

	for(port=0;port<RTL8651_AGGREGATOR_NUMBER;port++)
	{
		if((1<<port) & cp->portmask)
		{
#ifdef CONFIG_RTL_8367R_SUPPORT
			if (port <=4) {
				//extern int rtk_stp_mstpState_set(int msti, int port, int stp_state);
				//rtk_stp_mstpState_set(0 , port, 3); // 3: STP_STATE_FORWARDING
				extern int rtk_port_adminEnable_set(uint32 port, uint32 enable);
				rtk_port_adminEnable_set(port, 1);
			}
#else
			REG32(PCRP0+(port<<2))= ((REG32(PCRP0+(port<<2)))|(ForceLink));
			REG32(PCRP0+(port<<2))= ((REG32(PCRP0+(port<<2)))|(EnablePHYIf));
			TOGGLE_BIT_IN_REG_TWICE(PCRP0+(port<<2),EnablePHYIf);
			TOGGLE_BIT_IN_REG_TWICE(PCRP0+(port<<2),ForceLink);
#endif
		}
	}
#ifdef CONFIG_RTL_8196C_ESD
	if ((cp->portmask) & 0x10) {			// port 4
		_96c_esd_counter = 1;		// start counting and check ESD
		_96c_esd_reboot_counter = 0;	// reset counter
	}
#endif
}

static void rtl865x_disableInterrupt(void)
{
	REG32(CPUICR) = 0;
	REG32(CPUIIMR) = 0;
	REG32(CPUIISR) = REG32(CPUIISR);
}

static void rtk_queue_init(struct ring_que *que)
{
	memset(que, 0, sizeof(struct ring_que));
	que->ring = (struct sk_buff **)kmalloc(
		(sizeof(struct skb_buff*)*(rtl865x_maxPreAllocRxSkb+1))
		,GFP_ATOMIC);
	memset(que->ring, 0, (sizeof(struct sk_buff *))*(rtl865x_maxPreAllocRxSkb+1));
	que->qmax = rtl865x_maxPreAllocRxSkb;
}

static void rtk_queue_exit(struct ring_que *que)
{

	if(que->ring!=NULL)
	{
		kfree(que->ring);
		que->ring=NULL;
	}
}
__MIPS16
__IRAM_FWD
static int rtk_queue_tail(struct ring_que *que, struct sk_buff *skb)
{
	int next;


	if (que->head == que->qmax)
		next = 0;
	else
		next = que->head + 1;

	if (que->qlen >= que->qmax || next == que->tail) {
		return 0;
	}

	que->ring[que->head] = skb;
	que->head = next;
	que->qlen++;

	return 1;
}

__MIPS16
__IRAM_FWD
static struct sk_buff *rtk_dequeue(struct ring_que *que)
{
	struct sk_buff *skb;

	if (que->qlen <= 0 || que->tail == que->head)
	{
		return NULL;
	}

	skb = que->ring[que->tail];

	if (que->tail == que->qmax)
		que->tail  = 0;
	else
		que->tail++;

	que->qlen--;

	return (struct sk_buff *)skb;
}

//#if defined(CONFIG_RTL_ETH_PRIV_SKB_DEBUG)
#if defined(CONFIG_RTL_PROC_DEBUG)||defined(CONFIG_RTL_DEBUG_TOOL)
int get_buf_in_rx_skb_queue(void)
{
	return rx_skb_queue.qlen;
}

#if defined(CONFIG_RTL_ETH_PRIV_SKB)
int get_buf_in_poll(void)
{
	return eth_skb_free_num;
}
#endif
#endif

//__MIPS16
__IRAM_FWD
static void refill_rx_skb(void)
{
	struct sk_buff	*skb;
	unsigned long	flags=0;
	int			idx;

	idx = RTL865X_SWNIC_RXRING_MAX_PKTDESC -1;

#if defined(DELAY_REFILL_ETH_RX_BUF) || defined(ALLOW_RX_RING_PARTIAL_EMPTY)
	while (rx_skb_queue.qlen < rtl865x_maxPreAllocRxSkb || ((idx>=0)&&(SUCCESS==check_rx_pkthdr_ring(idx, &idx))))
#else
	while (rx_skb_queue.qlen < rtl865x_maxPreAllocRxSkb)
#endif
	{
		#if defined(CONFIG_RTL_ETH_PRIV_SKB)
		skb = dev_alloc_skb_priv_eth(CROSS_LAN_MBUF_LEN);
		#else
		skb = dev_alloc_skb(CROSS_LAN_MBUF_LEN);
		#endif

		if (skb == NULL) {
			DEBUG_ERR("EthDrv: dev_alloc_skb() failed!\n");
			return;
		}
		skb_reserve(skb, RX_OFFSET);

		#if defined(DELAY_REFILL_ETH_RX_BUF) || defined(ALLOW_RX_RING_PARTIAL_EMPTY)
		// return to rx descriptor ring first if the rings still have the OWN bit which be RISC_OWNED.
		if (idx>=0) {
			if  (SUCCESS==check_and_return_to_rx_pkthdr_ring(skb, idx)) {
				continue;
			}
		}
		#endif

		#if defined(RTK_QUE)
		SMP_LOCK_ETH_BUF(flags);
		rtk_queue_tail(&rx_skb_queue, skb);
		SMP_UNLOCK_ETH_BUF(flags);
		#else
		__skb_queue_tail(&rx_skb_queue, skb);
		#endif
	}
}

//---------------------------------------------------------------------------
static void free_rx_skb(void)
{
	struct sk_buff *skb;
	#if defined(CONFIG_SMP)
	unsigned long flags = 0;
	#endif

	RTL_swNic_freeRxBuf();

	while  (rx_skb_queue.qlen > 0) {
		#if defined(CONFIG_SMP)
		SMP_LOCK_ETH_BUF(flags);
		#endif
#if defined(RTK_QUE)
		skb = rtk_dequeue(&rx_skb_queue);
#else
		skb = __skb_dequeue(&rx_skb_queue);
#endif
		#if defined(CONFIG_SMP)
		SMP_UNLOCK_ETH_BUF(flags);
		#endif
		dev_kfree_skb_any(skb);
	}
}

//---------------------------------------------------------------------------
__IRAM_FWD
unsigned char *alloc_rx_buf(void **skb, int buflen)
{
	struct sk_buff *new_skb;
	unsigned long flags=0;
#ifdef CONFIG_RTL_JUMBO_FRAME
	if(buflen>CROSS_LAN_MBUF_LEN){
		new_skb = dev_alloc_skb(buflen);
		if (new_skb == NULL) {
			DEBUG_ERR("EthDrv: alloc skb failed!\n");
		}
		else
			skb_reserve(new_skb, RX_OFFSET);
	}
	else{
#endif
		if (rx_skb_queue.qlen == 0) {
			#if defined(CONFIG_RTL_ETH_PRIV_SKB)
			new_skb = dev_alloc_skb_priv_eth(CROSS_LAN_MBUF_LEN);
			#else
			new_skb = dev_alloc_skb(CROSS_LAN_MBUF_LEN);
			#endif
			if (new_skb == NULL) {
				DEBUG_ERR("EthDrv: alloc skb failed!\n");
			}
			else
				skb_reserve(new_skb, RX_OFFSET);
		}
		else {
			#if defined(RTK_QUE)
			SMP_LOCK_ETH_BUF(flags);
			new_skb = rtk_dequeue(&rx_skb_queue);
			SMP_UNLOCK_ETH_BUF(flags);
			#else
			new_skb = __skb_dequeue(&rx_skb_queue);
			#endif
		}
#ifdef CONFIG_RTL_JUMBO_FRAME
	}
#endif
	if (new_skb == NULL)
		return NULL;

#if 0//defined(CONFIG_RTL_ULINKER_BRSC)
	skb_reserve(new_skb, 2);
#endif

	*skb = new_skb;

	return new_skb->data;
}

#ifdef PRIV_BUF_CAN_USE_KERNEL_BUF
__IRAM_FWD
unsigned char *alloc_rx_buf_init(void **skb, int buflen)
{
	struct sk_buff *new_skb;
	unsigned long flags=0;
#ifdef CONFIG_RTL_JUMBO_FRAME
	if(buflen>CROSS_LAN_MBUF_LEN){
		new_skb = dev_alloc_skb(buflen);
		if (new_skb == NULL) {
			DEBUG_ERR("EthDrv: alloc skb failed!\n");
		}
		else
			skb_reserve(new_skb, RX_OFFSET);
	}
	else{
#endif
		if (rx_skb_queue.qlen == 0) {
			#if defined(CONFIG_RTL_ETH_PRIV_SKB)
			new_skb = dev_alloc_skb_priv_eth(CROSS_LAN_MBUF_LEN);
			if (new_skb == NULL) {
				new_skb = dev_alloc_skb(CROSS_LAN_MBUF_LEN);
			}
			#else
			new_skb = dev_alloc_skb(CROSS_LAN_MBUF_LEN);
			#endif
			if (new_skb == NULL) {
				DEBUG_ERR("EthDrv: alloc skb failed!\n");
			}
			else
				skb_reserve(new_skb, RX_OFFSET);
		}
		else {
			#if defined(RTK_QUE)
			SMP_LOCK_ETH_BUF(flags);
			new_skb = rtk_dequeue(&rx_skb_queue);
			SMP_UNLOCK_ETH_BUF(flags);
			#else
			new_skb = __skb_dequeue(&rx_skb_queue);
			#endif
		}
#ifdef CONFIG_RTL_JUMBO_FRAME
	}
#endif
	if (new_skb == NULL)
		return NULL;

	*skb = new_skb;

	return new_skb->data;
}
#endif

//---------------------------------------------------------------------------
#if !defined(CONFIG_RTD_1295_HWNAT)
inline void free_rx_buf(void *skb)
{
	dev_kfree_skb_any((struct sk_buff *)skb);
}
#endif //!defined(CONFIG_RTD_1295_HWNAT)

#if defined(CONFIG_RTD_1295_HWNAT)
void sync_rx_data_for_device(dma_addr_t addr, int len)
{
	struct device *d;

	d = &rtl819x_pdev->dev;
	//DBG("dma_sync_single_for_device(dev 0x%p, mapping 0x%x, len %d, dir %d)", d, (uint) addr, len, DMA_FROM_DEVICE);
	dma_sync_single_for_device(d, addr, len, DMA_FROM_DEVICE);
	//DBG("dma_sync_single_for_device() done");
	return;
}

/* skb->len and skb->data_len are invalid in the meanwhile. */
void sync_rx_data_for_cpu(dma_addr_t addr, int len)
{
	struct device *d;

	d = &rtl819x_pdev->dev;
	//DBG("dma_sync_single_for_cpu(dev 0x%p, mapping 0x%x, len %d, dir %d)", d, (uint) addr, len, DMA_FROM_DEVICE);
	dma_sync_single_for_cpu(d, addr, len, DMA_FROM_DEVICE);
	//DBG("dma_sync_single_for_cpu() done");
	return;
}

void sync_tx_data_for_device(dma_addr_t addr, int len)
{
	struct device *d;

	d = &rtl819x_pdev->dev;
	//DBG("dma_sync_single_for_device(dev 0x%p, mapping 0x%x, len %d, dir %d)", d, (uint) addr, len, DMA_TO_DEVICE);
	dma_sync_single_for_device(d, addr, len, DMA_TO_DEVICE);
	//DBG("dma_sync_single_for_device() done");
	return;
}

void rtl_dump_data(void *p, int len) {
	int i;
	unsigned char *ptr;

	ptr = (unsigned char *) p;
	for (i = 0; i < len; i++) {
		if ((i % 16) == 0)
			printk("\n%04X  ", i);
		if ((i % 8) == 0)
			printk("  ");

		printk("%02x ", ptr[i]);
	}
	printk("\n");
}
#endif //defined(CONFIG_RTD_1295_HWNAT)

//---------------------------------------------------------------------------
#if defined(CONFIG_RTL_FAST_BRIDGE)
void tx_done_callback(void *skb)
{
#if defined(CONFIG_RTL_FAST_BRIDGE)
	#define RTL_PRIV_DATA_SIZE	128
	unsigned long flags=0;
	//hyking:
	//queue private skb and buffer
	if (((struct sk_buff*)skb)->fast_br_forwarding_flags == 1 && is_rtl865x_eth_priv_buf(((struct sk_buff *)skb)->head))
	{
		if(!(skb_cloned(skb)))
		{
			//disable irq
			SMP_LOCK_ETH_BUF(flags);
			if(rtk_queue_tail(&rx_skb_queue,(struct sk_buff *)skb))
			{
				unsigned char *data;
				struct skb_shared_info *shinfo;
				int size;
				data = (((struct sk_buff *)skb)->head);

				 memset(skb, 0, offsetof(struct sk_buff, truesize));
					atomic_set(&((struct sk_buff *)skb)->users, 1);
					((struct sk_buff *)skb)->head = data;
					((struct sk_buff *)skb)->data = data;
					((struct sk_buff *)skb)->tail = data;

				size = (CROSS_LAN_MBUF_LEN+128+NET_SKB_PAD);

					((struct sk_buff *)skb)->end  = data + size;
					((struct sk_buff *)skb)->truesize = size + sizeof(struct sk_buff);

					/* make sure we initialize shinfo sequentially */
					shinfo = skb_shinfo(skb);
					atomic_set(&shinfo->dataref, 1);
					shinfo->nr_frags  = 0;
					shinfo->gso_size = 0;
					shinfo->gso_segs = 0;
					shinfo->gso_type = 0;
					shinfo->ip6_frag_id = 0;
					shinfo->frag_list = NULL;

#ifdef CONFIG_RTK_VOIP_VLAN_ID
					((struct sk_buff *)skb)->rx_vlan = 0;
					((struct sk_buff *)skb)->rx_wlan = 0;
					((struct sk_buff *)skb)->priority = 0;
#endif

#if defined(CONFIG_RTL_HARDWARE_MULTICAST) ||defined(CONFIG_RTL_QOS_VLANID_SUPPORT)
				((struct sk_buff *)skb)->srcPort=0xFFFF;
				((struct sk_buff *)skb)->srcVlanId=0;
#endif

#if	defined(CONFIG_RTL_QOS_8021P_SUPPORT)
				((struct sk_buff *)skb)->srcVlanPriority=0;
#endif

#if defined(CONFIG_RTL_VLANPRI_IPTABLE_CHECK)
		((struct sk_buff *)skb)->original_vlanpri = 0;
#endif

#if defined(CONFIG_NETFILTER_XT_MATCH_PHYPORT)|| defined(CONFIG_RTL_FAST_FILTER) || defined(CONFIG_RTL_QOS_PATCH) || defined(CONFIG_RTK_VOIP_QOS) || defined(CONFIG_RTK_VLAN_WAN_TAG_SUPPORT) ||defined(CONFIG_RTL_MAC_FILTER_CARE_INPORT) ||defined(CONFIG_RTL_HW_QOS_SUPPORT_WLAN)
				((struct sk_buff *)skb)->srcPhyPort=0xFF;
				((struct sk_buff *)skb)->dstPhyPort=0xFF;
#endif

#if defined(CONFIG_RTK_VLAN_SUPPORT)
				((struct sk_buff *)skb)->tag.v = 0;
#endif

#if defined (CONFIG_RTL_LOCAL_PUBLIC)
				((struct sk_buff *)skb)->srcLocalPublicIp=0;
				((struct sk_buff *)skb)->fromLocalPublic=0;
				((struct sk_buff *)skb)->toLocalPublic=0;
#endif

#ifdef CONFIG_RTK_VOIP_VLAN_ID
				skb_reserve(((struct sk_buff *)skb), RTL_PRIV_DATA_SIZE+4); // for VLAN TAG insertion
#else
				skb_reserve(((struct sk_buff *)skb), RTL_PRIV_DATA_SIZE);
#endif
				//reserve for 4 byte alignment
				skb_reserve(((struct sk_buff *)skb), RX_OFFSET);
				//enable irq
				SMP_UNLOCK_ETH_BUF(flags);
				return;
			}

			//enable irq
			SMP_UNLOCK_ETH_BUF(flags);
		}
	}
#endif
	dev_kfree_skb_any((struct sk_buff *)skb);
}
#endif

#if defined (CONFIG_RTL_IGMP_SNOOPING)||defined(CONFIG_BRIDGE_IGMP_SNOOPING)
#if defined (CONFIG_BRIDGE) && defined (CONFIG_RTL_IGMP_SNOOPING)
extern void br_signal_igmpProxy(void);
#endif

static inline struct iphdr * re865x_getIpv4Header(uint8 *macFrame)
{
	uint8 *ptr;
	struct iphdr *iph=NULL;

	ptr=macFrame+12;
	if(*(int16 *)(ptr)==(int16)htons(ETH_P_8021Q))
	{
		ptr=ptr+4;
	}
	if(*(int16 *)(ptr)==(int16)htons(ETH_P_IPV6))
	{
		ptr=ptr+sizeof(struct ipv6hdr);
	}
	/*it's not ipv4 packet*/
	if(*(int16 *)(ptr)!=(int16)htons(ETH_P_IP))
	{
		return NULL;
	}

	iph=(struct iphdr *)(ptr+2);

	return iph;
}

#if defined (CONFIG_RTL_MLD_SNOOPING)||defined(CONFIG_BRIDGE_IGMP_SNOOPING)
static inline struct ipv6hdr* re865x_getIpv6Header(uint8 *macFrame)
{
	uint8 *ptr;
	struct ipv6hdr *ipv6h=NULL;

	ptr=macFrame+12;
	if(*(int16 *)(ptr)==(int16)htons(ETH_P_8021Q))
	{
		ptr=ptr+4;
	}

	if(*(int16 *)(ptr)==(int16)htons(ETH_P_IP))
	{
		ptr=ptr+sizeof(struct iphdr);
	}

	/*it's not ipv6 packet*/
	if(*(int16 *)(ptr)!=(int16)htons(ETH_P_IPV6))
	{
		return NULL;
	}

	ipv6h=(struct ipv6hdr *)(ptr+2);

	return ipv6h;
}
#define IPV4_MCAST_PACKET 	0x1
#define	IPV6_MCAST_PACKET 	0x2
#define DSLITE_MCAST_PACKET 0x3
#define	SIXRD_MCAST_PACKET 	0x4
#define	IPV6_MCAST_PACKET2 	0x5

#define IN6_IS_ADDR_MULTICAST(a) ((a[0]& htonl(0xff000000))== htonl(0xff000000))
//#define IN6_IS_ADDR_MULTICAST(a) (((__const uint8_t *) (a))[0] == 0xff)

static inline int rtl_isMcastPkt(struct sk_buff *skb)
{
	int ret=0;
	struct iphdr *iph=NULL;
	struct ipv6hdr *ipv6h=NULL;
	uint8 *ptr;
	if((skb->data[0]&0x1)&&(skb->data[0]!=0xff))
	{

		if((skb->data[0]==0x01) && (skb->data[1]==0x00)&& (skb->data[2]==0x5e))
		{
			ret=IPV4_MCAST_PACKET;
		}
		else if((skb->data[0]==0x33) && (skb->data[1]==0x33)&& (skb->data[2]!=0xff))
		{
			ret=IPV6_MCAST_PACKET;
		}
		else if((skb->data[0]==0x33) && (skb->data[1]==0x33)&& (skb->data[2]==0xff))
		{
			ret=IPV6_MCAST_PACKET2;
		}
		goto out;
	}
	iph=re865x_getIpv4Header(skb->data);

	/*6rd multicast packet*/
	if(iph)
	{
		if(iph->protocol==IPPROTO_IPV6)
		{
			ptr =(uint8 *)iph;
			ptr=ptr+sizeof(struct iphdr);
			ipv6h =(struct ipv6hdr *)ptr;
			if(ipv6h &&(ipv6h->version ==IP_VERSION6)&&(IN6_IS_ADDR_MULTICAST(ipv6h->daddr.s6_addr32)))
			{
				ret=SIXRD_MCAST_PACKET;
				goto out;
			}
		}
	}
	ipv6h=re865x_getIpv6Header(skb->data);
	/*dslite multicast packet*/
	if(ipv6h)
	{
		if(ipv6h->nexthdr==IPPROTO_IPIP)

		{
			ptr =(uint8 *)ipv6h;
			ptr=ptr+sizeof(struct ipv6hdr);
			iph =(struct iphdr *)ptr;
			if(iph &&(iph->version ==IP_VERSION4)&&(ipv4_is_multicast(iph->daddr))){
				ret=DSLITE_MCAST_PACKET;
				goto out;
			}

		}
	}
out:
	return ret;
}

#define IPV6_ROUTER_ALTER_OPTION 0x05020000
#define  HOP_BY_HOP_OPTIONS_HEADER 0
#define ROUTING_HEADER 43
#define  FRAGMENT_HEADER 44
#define DESTINATION_OPTION_HEADER 60

#define PIM_PROTOCOL 103
#define MOSPF_PROTOCOL 89
#define TCP_PROTOCOL 6
#define UDP_PROTOCOL 17
#define NO_NEXT_HEADER 59
#define ICMP_PROTOCOL 58

#define MLD_QUERY 130
#define MLDV1_REPORT 131
#define MLDV1_DONE 132
#define MLDV2_REPORT 143

#define IS_IPV6_PIM_ADDR(ipv6addr) ((ipv6addr[0] == 0xFF020000)&&(ipv6addr[1] == 0x00000000)&&(ipv6addr[2] == 0x00000000)&&(ipv6addr[3] ==0x0000000D))
#define IS_IPV6_MOSPF_ADDR1(ipv6addr) ((ipv6addr[0] == 0xFF020000)&&(ipv6addr[1] == 0x00000000)&&(ipv6addr[2] == 0x00000000)&&(ipv6addr[3] ==0x00000005))
#define IS_IPV6_MOSPF_ADDR2(ipv6addr) ((ipv6addr[0] == 0xFF020000)&&(ipv6addr[1] == 0x00000000)&&(ipv6addr[2] == 0x00000000)&&(ipv6addr[3] ==0x00000006))


int re865x_getIpv6TransportProtocol(struct ipv6hdr* ipv6h)
{

	unsigned char *ptr=NULL;
	unsigned char *startPtr=NULL;
	unsigned char *lastPtr=NULL;
	unsigned char nextHeader=0;
	unsigned short extensionHdrLen=0;

	unsigned char  optionDataLen=0;
	unsigned char  optionType=0;
	unsigned int ipv6RAO=0;
	unsigned int ipv6addr[4] = {0};

	if(ipv6h==NULL)
	{
		return -1;
	}

	if(ipv6h->version!=6)
	{
		return -1;
	}

	startPtr= (unsigned char *)ipv6h;
	lastPtr=startPtr+sizeof(struct ipv6hdr)+ntohs(ipv6h->payload_len);
	nextHeader= ipv6h ->nexthdr;
	ptr=startPtr+sizeof(struct ipv6hdr);

	ipv6addr[0] = ntohl(ipv6h->daddr.s6_addr32[0]);
	ipv6addr[1] = ntohl(ipv6h->daddr.s6_addr32[1]);
	ipv6addr[2] = ntohl(ipv6h->daddr.s6_addr32[2]);
	ipv6addr[3] = ntohl(ipv6h->daddr.s6_addr32[3]);

	while(ptr<lastPtr)
	{
		switch(nextHeader)
		{
			case HOP_BY_HOP_OPTIONS_HEADER:
				/*parse hop-by-hop option*/
				nextHeader=ptr[0];
				extensionHdrLen=((uint16)(ptr[1])+1)*8;
				ptr=ptr+2;

				while(ptr<(startPtr+extensionHdrLen+sizeof(struct ipv6hdr)))
				{
					optionType=ptr[0];
					/*pad1 option*/
					if(optionType==0)
					{
						ptr=ptr+1;
						continue;
					}

					/*padN option*/
					if(optionType==1)
					{
						optionDataLen=ptr[1];
						ptr=ptr+optionDataLen+2;
						continue;
					}

					/*router altert option*/
					if(ntohl(*(uint32 *)(ptr))==IPV6_ROUTER_ALTER_OPTION)
					{
						ipv6RAO=IPV6_ROUTER_ALTER_OPTION;
						ptr=ptr+4;
						continue;
					}

					/*other TLV option*/
					if((optionType!=0) && (optionType!=1))
					{
						optionDataLen=ptr[1];
						ptr=ptr+optionDataLen+2;
						continue;
					}


				}

				break;

			case ROUTING_HEADER:
				nextHeader=ptr[0];
				extensionHdrLen=((uint16)(ptr[1])+1)*8;
							ptr=ptr+extensionHdrLen;
				break;

			case FRAGMENT_HEADER:
				nextHeader=ptr[0];
				ptr=ptr+8;
				break;

			case DESTINATION_OPTION_HEADER:
				nextHeader=ptr[0];
				extensionHdrLen=((uint16)(ptr[1])+1)*8;
				ptr=ptr+extensionHdrLen;
				break;

			case ICMP_PROTOCOL:
				nextHeader=NO_NEXT_HEADER;
				if((ptr[0]==MLD_QUERY) ||(ptr[0]==MLDV1_REPORT) ||(ptr[0]==MLDV1_DONE) ||(ptr[0]==MLDV2_REPORT))
				{
					return ICMP_PROTOCOL;

				}
				break;

			case PIM_PROTOCOL:
				nextHeader=NO_NEXT_HEADER;
				if(IS_IPV6_PIM_ADDR(ipv6addr))
				{
					return PIM_PROTOCOL;
				}

				break;

			case MOSPF_PROTOCOL:
				nextHeader=NO_NEXT_HEADER;
				if(IS_IPV6_MOSPF_ADDR1(ipv6addr) || IS_IPV6_MOSPF_ADDR2(ipv6addr))
				{
					return MOSPF_PROTOCOL;
				}
				break;

			case TCP_PROTOCOL:
				nextHeader=NO_NEXT_HEADER;
				return TCP_PROTOCOL;

				break;

			case UDP_PROTOCOL:
				nextHeader=NO_NEXT_HEADER;
				return UDP_PROTOCOL;

				break;

			default:
				/*not ipv6 multicast protocol*/
				return -1;
				break;
		}

	}
	return -1;
}
#endif
static inline void re865x_relayTrappedMCast(struct sk_buff *skb, unsigned int vid, unsigned int mcastFwdPortMask, unsigned int keepOrigSkb)
{
	rtl_nicTx_info	nicTx;
	struct sk_buff *skb2=NULL;

	#ifdef CONFIG_RTK_VLAN_WAN_TAG_SUPPORT
	struct sk_buff *skb_wan=NULL;
	rtl_nicTx_info	nicTx_wan;
	#endif

#if 0//def CONFIG_RTL_VLAN_8021Q
	uint8 portMask;
	uint8 tagMask;
	uint8 untagMask;
	struct sk_buff *pskb=NULL;
#endif

	if(mcastFwdPortMask==0)
	{
		return;
	}

	if(keepOrigSkb==TRUE)
	{
		skb2= skb_clone(skb, GFP_ATOMIC);
	}
	else
	{
		skb2=skb;
	}

	if(skb2!=NULL)
	{
		nicTx.txIdx=0;

#ifdef CONFIG_RTK_VLAN_WAN_TAG_SUPPORT
		if( vlan_bridge_tag && vlan_bridge_multicast_tag &&(vid == vlan_bridge_tag)&&(mcastFwdPortMask & RTL_WANPORT_MASK))
		{
			mcastFwdPortMask &= (~RTL_WANPORT_MASK);
			skb_wan = skb_copy(skb, GFP_ATOMIC);
			 if(skb_wan!=NULL)
			 {
			   	nicTx_wan.txIdx=0;
				nicTx_wan.vid = vlan_bridge_multicast_tag;
				nicTx_wan.portlist = RTL_WANPORT_MASK;
				nicTx_wan.srcExtPort = 0;

				nicTx_wan.tagport = RTL_WANPORT_MASK;

				// flush cache 0515 by tim
				RTL_CACHE_WBACK((unsigned long) skb_wan->data, skb_wan->len);
				if (RTL_swNic_send((void *)skb_wan, skb_wan->data, skb_wan->len, &nicTx_wan) < 0)
				{
					dev_kfree_skb_any(skb_wan);
				}
			}
		}

		if(!mcastFwdPortMask)
		{
			dev_kfree_skb_any(skb2);
			return;
		}
#endif

#if defined(CONFIG_RTL_VLAN_PASSTHROUGH_SUPPORT)
		rtl_process_vlan_passthrough_tx(&skb2);
#endif
		nicTx.txIdx=0;

#if defined(CONFIG_RTL_QOS_PATCH)|| defined(CONFIG_RTK_VOIP_QOS)
		if(((struct sk_buff *)skb)->srcPhyPort == QOS_PATCH_RX_FROM_LOCAL)
		{
			nicTx.priority = QOS_PATCH_HIGH_QUEUE_PRIO;
			nicTx.txIdx=RTL865X_SWNIC_TXRING_MAX_PKTDESC-1;	//use the highest tx ring index, note: not RTL865X_SWNIC_TXRING_HW_PKTDESC-1
		}
#endif
		nicTx.vid = vid;
		#ifndef CONFIG_RTL_VLAN_8021Q
		nicTx.portlist = mcastFwdPortMask&((struct dev_priv *)(NETDRV_PRIV(skb->dev)))->portmask;
		#else
		nicTx.portlist = mcastFwdPortMask;
		#endif
		nicTx.srcExtPort = 0;
		nicTx.flags = (PKTHDR_USED|PKT_OUTGOING);
	#ifdef CONFIG_RTK_VLAN_WAN_TAG_SUPPORT
		if( (vlan_tag && vid == vlan_tag) || (vlan_bridge_tag && (vid == vlan_bridge_tag)))
		{
			nicTx.tagport = RTL_WANPORT_MASK;
		}
	#endif

#if 0//def CONFIG_RTL_VLAN_8021Q
		/*make igmp snoop LAN to LAN work when linux vlan is enable*/
		if(linux_vlan_enable){
			if(*((uint16*)(skb2->data+(ETH_ALEN<<1))) == __constant_htons(ETH_P_8021Q)){
				vid=*(uint16*)(skb2->data+(ETH_ALEN<<1)+2);
				portMask=nicTx.portlist&vlan_ctl_p->group[vid].memberMask&0xff;
				if(portMask){
					tagMask=portMask&vlan_ctl_p->group[vid].tagMemberMask&0xff;
					untagMask=portMask&(~tagMask);
					if(untagMask){
						nicTx.portlist=untagMask;
						if(tagMask){
							pskb=skb_copy(skb2,GFP_ATOMIC);
							if (pskb != NULL) {
								memmove(pskb->data+VLAN_HLEN, pskb->data, ETH_ALEN<<1);
								skb_pull(pskb,VLAN_HLEN);
								_dma_cache_wback_inv((unsigned long) pskb->data, pskb->len);
								if (RTL_swNic_send((void *)pskb, pskb->data, pskb->len, &nicTx) < 0)
								{
									dev_kfree_skb_any(pskb);
								}
							}
						}
						else{
							if (skb_cloned(skb2)){
								pskb = skb_copy(skb2, GFP_ATOMIC);
								if (pskb == NULL) {
									dev_kfree_skb_any(skb2);
									return;
								}
								dev_kfree_skb_any(skb2);
								skb2 = pskb;
								pskb = NULL;
							}
							memmove(skb2->data+VLAN_HLEN, skb2->data, ETH_ALEN<<1);
							skb_pull(skb2,VLAN_HLEN);
						}
					}

					if(tagMask)
						nicTx.portlist=tagMask;
				}
				else{
					dev_kfree_skb_any(skb2);
					return;
				}
			}
		}
#endif

		RTL_CACHE_WBACK((unsigned long)skb2->data, skb2->len);
		if (RTL_swNic_send((void *)skb2, skb2->data, skb2->len, &nicTx) < 0)
		{
			dev_kfree_skb_any(skb2);
		}
	}
	return;
}

#if defined (CONFIG_RTL_IGMP_SNOOPING)
extern void rtl865x_igmpLinkStatusChangeCallback(uint32 moduleIndex, rtl_igmpPortInfo_t * portInfo);
void rtl865x_igmpSyncLinkStatus(void)
{
	rtl_igmpPortInfo_t portInfo;

	portInfo.linkPortMask=newLinkPortMask;
	//rtl865x_igmpLinkStatusChangeCallback(nicIgmpModuleIndex, &portInfo);

	#if defined (CONFIG_BRIDGE)
	if((newLinkPortMask & (~curLinkPortMask))!=0)
	{
		/*there is some port linking up*/
		/*notice igmp proxy daemon to send general query to newly link up client*/
		#ifdef CONFIG_RTL8196BU_8186SDK_MP_SPI
		#else
		br_signal_igmpProxy();
		#endif
	}
	#endif

	return;

}

#endif	/*	defined (CONFIG_RTL_IGMP_SNOOPING)	*/
#endif  /*  defined (CONFIG_RTL_IGMP_SNOOPING) || defined(CONFIG_BRIDGE_IGMP_SNOOPING) */


#if defined(CONFIG_BRIDGE_IGMP_SNOOPING)||defined (CONFIG_RTL_IGMP_SNOOPING)
#if defined(CONFIG_RTL_IGMP_SNOOPING)
#if defined (CONFIG_NETFILTER)
unsigned int (*IgmpRxFilter_Hook)(struct sk_buff *skb,
		 unsigned int hook,
		 const struct net_device *in,
		 const struct net_device *out,
		 struct xt_table *table);
EXPORT_SYMBOL(IgmpRxFilter_Hook);
static bool rtl_MulticastRxFilterOff(struct sk_buff *skb, int ipversion)
{
	bool ret =  true;
	if(IgmpRxFilter_Hook == NULL)
	{
		DEBUG_ERR("IgmpRxFilter_hook is NULL\n");
		return false;
	}

#if defined(CONFIG_RTD_1295_HWNAT)
	if(ipversion ==4)
		skb_set_network_header(skb, (int) ((uint8 *)re865x_getIpv4Header(skb->data) - skb->data));
	else if(ipversion ==6)
		skb_set_network_header(skb, (int) ((uint8 *)re865x_getIpv6Header(skb->data) - skb->data));
	else
		return ret;//error shouldn't happen
	skb_reset_mac_header(skb);
#else /* !CONFIG_RTD_1295_HWNAT */
	if(ipversion ==4)
		skb->network_header = (sk_buff_data_t)re865x_getIpv4Header(skb->data);
	else if(ipversion ==6)
		skb->network_header = (sk_buff_data_t)re865x_getIpv6Header(skb->data);
	else
		return ret;//error shouldn't happen
#ifdef NET_SKBUFF_DATA_USES_OFFSET
	skb->mac_header = (sk_buff_data_t)(skb->data - skb->head);
#else
	skb->mac_header = (sk_buff_data_t)skb->data;
#endif
#endif /* CONFIG_RTD_1295_HWNAT */

	//data should point to l3 header while doing iptables check
		skb->data = skb->data+ETH_HLEN;

	if(ipversion ==4)
	{
		struct net_device	*origDev=skb->dev;

//#ifndef CONFIG_RTL_8198C // modified by lynn_pu, 2014-10-22
#if !defined(CONFIG_RTL_8198C) && !defined(CONFIG_RTL_8197F) && (!defined(CONFIG_RLX)&&(LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)))
		if((skb->dev->br_port!=NULL))
		{
			skb->dev=__dev_get_by_name(dev_net(skb->dev), RTL_PS_BR0_DEV_NAME);
		}
#else
		if (skb->dev->priv_flags & IFF_BRIDGE_PORT)
		{
			skb->dev=__dev_get_by_name(dev_net(skb->dev), RTL_PS_BR0_DEV_NAME);
		}
#endif
		ret = ((IgmpRxFilter_Hook(skb, NF_INET_LOCAL_IN,  skb->dev, NULL,dev_net(skb->dev)->ipv4.iptable_filter)) !=NF_ACCEPT);
		skb->dev=origDev;
	}
	else if(ipversion ==6)
		ret = false;//ipv6 hava no iptables rule now

	if(ret)
	{
		DEBUG_ERR(" filter a v%d pkt\n", ipversion);
	}
	// return point to l2 header
	skb->data = skb->data-ETH_HLEN;
	return ret;
}
#endif/*CONFIG_NETFILTER*/
#endif/*CONFIG_RTL_IGMP_SNOOPING*/

extern uint32 rtl865x_getVlanPortMask(uint32 vid);
#if defined (CONFIG_RT_MULTIPLE_BR_SUPPORT)
unsigned int rtl_get_brIgmpModuleIndexbyVid(unsigned int vid);
unsigned int rtl_get_brIgmpModuleIndexbyId(int idx,char *name);
int rtl_get_brIgmpModuleIndexbyName(char *name, int * index);
#endif
__MIPS16
int  rtl_MulticastRxCheck(struct sk_buff *skb,rtl_nicRx_info *info,int mCastFlag)
{
	unsigned int  vlanRelayPortMask=0;
#if defined(CONFIG_RTL_IGMP_SNOOPING)
	struct iphdr *iph=NULL;
	struct rtl_multicastDataInfo multicastDataInfo;
	struct rtl_multicastFwdInfo nicMCastFwdInfo;
	struct rtl_multicastFwdInfo br0MCastFwdInfo;
	unsigned int l4Protocol=0;
	int ret=FAILED;
#endif

#if defined (CONFIG_RTL_MLD_SNOOPING)
	struct ipv6hdr *ipv6h=NULL;
#endif
#if defined (CONFIG_RTL_MLD_SNOOPING) || defined(CONFIG_RTL_VLAN_PASSTHROUGH_SUPPORT)
	struct dev_priv *cp_this=info->priv;
#endif
#ifdef CONFIG_RTL_VLAN_8021Q
	//unsigned int  vlanportmask=0;
	int type=0;
#endif
#ifdef CONFIG_RTL_VLAN_PASSTHROUGH_SUPPORT
	int pvid = 0;
#endif
#if defined (CONFIG_RT_MULTIPLE_BR_SUPPORT)
	unsigned int igmpModuleIndex =0xFFFFFFFF, mapPortMask = 0xFFFFFFFF;
	char brName[16] = {0};
	int toCpuFlag = 0;
	int i = 0;
	struct rtl_multicastFwdInfo multicastFwdInfo;
	int joinBrNum=0;
	int br_index = 0;
	//int igmpcnt=0;
#endif
#if defined (CONFIG_RTL_HW_MCAST_WIFI)
	rtl865x_tblDrv_mCast_t * existMulticastEntry;
#endif

	int vid=info->vid;
	int pid=info->pid;

#if defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)
	if ((info->vid==0) && (rtl819x_getSwEthPvid(pid, &(info->vid))==SUCCESS))
		vid = info->vid;
#endif

	#if 0
	if((skb->data[0] &0x01) ==0)
	{
		return -1;
	}
	#endif


#ifdef  CONFIG_RTL_VLAN_8021Q
	//if (linux_vlan_enable && linux_vlan_hw_support_enable)
	if (linux_vlan_enable)
	{
		uint16 skb_vid;

		if(skb->data[0] ==0xff)//broadcast packets just return
		{
			return 0;
		}

		#if defined (CONFIG_RTL_CUSTOM_PASSTHRU)
		if(strncmp(skb->dev->name, "peth", 4)==0)
		{
			skb->srcVlanId=vid;
			skb->srcPort=pid;

			#if defined(CONFIG_RTL_8021Q_VLAN_SUPPORT_SRC_TAG)
			skb->passThruFlag = IS_IP6_MC_PASSTHRU;
			#endif
			return 0;
		}
		#endif

		// change vid to  the packet's real vlan group
		type = rtk_decide_vlan_id(pid, skb,&skb_vid);
		vid = skb_vid ;
		if(type == VID_FROM_PACKET)
		{
			//if packet with tag inside , we need reomve it here and recover it
			// after mc relay done.  bcz hw can auto add tag .
			REMOVE_TAG(skb);
		}
	}
#endif

	/*set flooding port mask first*/
	#ifdef CONFIG_RTL_VLAN_PASSTHROUGH_SUPPORT
	rtl8651_getAsicPVlanId(pid, &pvid);
	vlanRelayPortMask=rtl865x_getVlanPortMask(pvid) & (~(1<<pid)) & ((1<<RTL8651_MAC_NUMBER)-1);
	#else
	vlanRelayPortMask=rtl865x_getVlanPortMask(vid) & (~(1<<pid)) & ((1<<RTL8651_MAC_NUMBER)-1);
	#endif

	//panic_printk("[%s, %d] vid=%d, pid=%d, vlanRelayPortMask=%02x\n",__FUNCTION__,__LINE__,vid,pid,vlanRelayPortMask);
#if 0
	if((skb->data[0]==0x01) && (skb->data[1]==0x00)&& (skb->data[2]==0x5e))
#else
	if((mCastFlag ==IPV4_MCAST_PACKET)||mCastFlag ==DSLITE_MCAST_PACKET)
#endif
	{
		#if defined(CONFIG_RTK_VLAN_SUPPORT)
		if(rtk_vlan_support_enable)
		{
			/*let bridge handle it*/
			#ifdef CONFIG_RTL_VLAN_8021Q
			goto relay_out;
			#else
			return 0;
			#endif
		}
		#endif


#if defined (CONFIG_RTL_HARDWARE_MULTICAST)
		#ifdef CONFIG_RTL_VLAN_PASSTHROUGH_SUPPORT
		if (*(uint16*)skb->vlan_passthrough_saved_tag == __constant_htons(ETH_P_8021Q))
		{
			vid = *((unsigned short *)(skb->vlan_passthrough_saved_tag+2));
			vid = __constant_ntohs(vid) & 0xfff;
			skb->srcVlanId = vid;
		}
		else
		{
			skb->srcVlanId=pvid;
			vid = pvid;
		}
		#else
		skb->srcVlanId=vid;
		#endif
		skb->srcPort=pid;
		#if 0
		if(net_ratelimit())
			printk("[%s:%d]skb->srcVlanId is %d\n",__FUNCTION__,__LINE__,skb->srcVlanId);
		#endif
#endif

		#if defined (CONFIG_RTL_MULTI_LAN_DEV)
		return 0;
		#endif


		/*not process multicast packet from wan*/
		#if defined(CONFIG_RTL_VLAN_8021Q)
		if (linux_vlan_enable)
		{
			//panic_printk("%s %d vid=%d pid=%d ret=%d\n", __FUNCTION__, __LINE__, vid, pid, rtl_isWanPort(info->priv, vid, skb, pid));
			if (rtl_isWanPort(info->priv, vid, skb, pid))
			{
				//panic_printk("%s %d vid=%d pid=%d \n", __FUNCTION__, __LINE__, vid, pid);
				goto relay_out;
			}
		}
		else
		{
			if(rtl_isWanDev(info->priv))
				return 0;
		}
		#else
		if(rtl_isWanDev(info->priv))
			return 0;
		#endif

		//panic_printk("[%s, %d] Not dropped yet!\n",__FUNCTION__,__LINE__);

#if defined(CONFIG_RTL_IGMP_SNOOPING)
		/*hardware ip multicast table will trap 0x01-00-5e-XX-XX-XX type packets*/
		/*how about other packets not trapped by hardware multicast table?---->we assume it has been flooded by l2 table*/
		iph=re865x_getIpv4Header(skb->data);
		if(iph!=NULL)
		{
			/*udp or tcp packet*/
			l4Protocol=iph->protocol;

			if((l4Protocol==IPPROTO_UDP) || (l4Protocol==IPPROTO_TCP))
			{
				/*relay packets which are trapped by hardware multicast table*/
				#if defined (CONFIG_RTL_HARDWARE_MULTICAST)
				#ifdef CONFIG_RTK_VLAN_WAN_TAG_SUPPORT
				if(vlan_bridge_tag && !strcmp(cp_this->dev->name,RTL_PS_ETH_NAME_ETH2))
				{
					if(igmpsnoopenabled && (nicIgmpModuleIndex_2!=0xFFFFFFFF))
					{
						multicastDataInfo.ipVersion=4;
						multicastDataInfo.sourceIp[0]=  (uint32)(iph->saddr);
						multicastDataInfo.groupAddr[0]=  (uint32)(iph->daddr);

						multicastDataInfo.sourceIp[0] = ntohl(multicastDataInfo.sourceIp[0]);
						multicastDataInfo.groupAddr[0] = ntohl(multicastDataInfo.groupAddr[0]);

						ret=rtl_getMulticastDataFwdInfo(nicIgmpModuleIndex_2, &multicastDataInfo, &nicMCastFwdInfo);
						#ifdef CONFIG_RTL_VLAN_PASSTHROUGH_SUPPORT
						vlanRelayPortMask=rtl865x_getVlanPortMask(pvid)& (~(1<<pid)) & nicMCastFwdInfo.fwdPortMask &cp_this->portmask& ((1<<RTL8651_MAC_NUMBER)-1);
						#else
						vlanRelayPortMask=rtl865x_getVlanPortMask(vid)& (~(1<<pid)) & nicMCastFwdInfo.fwdPortMask &cp_this->portmask& ((1<<RTL8651_MAC_NUMBER)-1);
						#endif
						if(ret==SUCCESS)
						{

						}
						else
						{
							ret=rtl_getMulticastDataFwdInfo(brIgmpModuleIndex_2, &multicastDataInfo, &br0MCastFwdInfo);
							if(ret==SUCCESS)
							{
								/*there is wireless client,can not flooding in vlan */
								vlanRelayPortMask=0;
							}
						}

					}
				}
				else
				#endif
				if(igmpsnoopenabled && (nicIgmpModuleIndex!=0xFFFFFFFF))
				{
					multicastDataInfo.ipVersion=4;
				#if defined(CONFIG_RTL_ULINKER_BRSC) // assign value will cause coredump
					memcpy(&multicastDataInfo.sourceIp, &iph->saddr, 4);
					memcpy(&multicastDataInfo.groupAddr, &iph->daddr, 4);
					multicastDataInfo.sourceIp[0] = ntohl(multicastDataInfo.sourceIp[0]);
					multicastDataInfo.groupAddr[0] = ntohl(multicastDataInfo.groupAddr[0]);
				#else
					multicastDataInfo.sourceIp[0]=  (uint32)(iph->saddr);
					multicastDataInfo.groupAddr[0]=  (uint32)(iph->daddr);
					multicastDataInfo.sourceIp[0] = ntohl(multicastDataInfo.sourceIp[0]);
					multicastDataInfo.groupAddr[0] = ntohl(multicastDataInfo.groupAddr[0]);
				#endif

				#if defined (CONFIG_RTL_HW_MCAST_WIFI)
					if(hwwifiEnable)
					{
						existMulticastEntry=rtl865x_findMCastEntry(multicastDataInfo.groupAddr[0], multicastDataInfo.sourceIp[0], (unsigned short)vid, (unsigned short)pid);
						if(existMulticastEntry!=NULL)
						{
							/*it's already in cache */
							#if defined CONFIG_RTL_VLAN_8021Q
							if(linux_vlan_enable)
								goto relay_out;
							else
								return 0;
							#else
							return 0;
							#endif
						}
					}
				#endif

					ret=rtl_getMulticastDataFwdInfo(nicIgmpModuleIndex, &multicastDataInfo, &nicMCastFwdInfo);
					#ifndef CONFIG_RTL_VLAN_8021Q
					#ifdef CONFIG_RTL_VLAN_PASSTHROUGH_SUPPORT
					vlanRelayPortMask=rtl865x_getVlanPortMask(pvid)& (~(1<<pid)) & nicMCastFwdInfo.fwdPortMask &cp_this->portmask& ((1<<RTL8651_MAC_NUMBER)-1);
					#else
					vlanRelayPortMask=rtl865x_getVlanPortMask(vid)& (~(1<<pid)) & nicMCastFwdInfo.fwdPortMask &cp_this->portmask& ((1<<RTL8651_MAC_NUMBER)-1);
					#endif
					#else
					vlanRelayPortMask=rtl865x_getVlanPortMask(vid)& (~(1<<pid)) & nicMCastFwdInfo.fwdPortMask & ((1<<RTL8651_MAC_NUMBER)-1);
					#endif
					if(ret==SUCCESS)
					{
						//group exsits in nic module.
						#if defined (CONFIG_RT_MULTIPLE_BR_SUPPORT) && defined(CONFIG_RTL_VLAN_8021Q)
						if(linux_vlan_enable)
						{
							if(vlanRelayPortMask==0)
							{
								//current br module eth client not join, but the other br module eth client join -> flooding
								igmpModuleIndex=rtl_get_brIgmpModuleIndexbyVid(vid);
								if(igmpModuleIndex!=0xFFFFFFFF)
								{
							 		ret=rtl_getMulticastDataFwdInfo(igmpModuleIndex, &multicastDataInfo, &multicastFwdInfo);
									if(ret!=SUCCESS)
									{
										vlanRelayPortMask=rtl865x_getVlanPortMask(vid)& (~(1<<pid))& ((1<<RTL8651_MAC_NUMBER)-1);

									}
								}
							}
						}
						#endif
					}
					else
					{
					#if defined (CONFIG_RT_MULTIPLE_BR_SUPPORT) && defined(CONFIG_RTL_VLAN_8021Q)
						if(linux_vlan_enable)
						{
							igmpModuleIndex=rtl_get_brIgmpModuleIndexbyVid(vid);
							if(igmpModuleIndex!=0xFFFFFFFF)
							{
								ret=rtl_getMulticastDataFwdInfo(igmpModuleIndex, &multicastDataInfo, &multicastFwdInfo);
								if(ret==SUCCESS)
								{
									vlanRelayPortMask=0;
								}
							}
						}
						else
						{
							igmpModuleIndex=rtl_get_brIgmpModuleIndexbyName(RTL_BR_NAME, &br_index);
							ret=rtl_getMulticastDataFwdInfo(igmpModuleIndex, &multicastDataInfo, &br0MCastFwdInfo);
							if(ret==SUCCESS)
							{
								/*there is wireless client,can not flooding in vlan */
								vlanRelayPortMask=0;
							}
						}

					#else
						{
							ret=rtl_getMulticastDataFwdInfo(brIgmpModuleIndex, &multicastDataInfo, &br0MCastFwdInfo);
							if(ret==SUCCESS)
							{
								/*there is wireless client,can not flooding in vlan */
								vlanRelayPortMask=0;
							}
						}
					#endif

					}
				}
				#ifdef CONFIG_RTL_VLAN_8021Q
				if(linux_vlan_enable && linux_vlan_hw_support_enable)
				{
				#if defined CONFIG_RT_MULTIPLE_BR_SUPPORT
					if(is_nat_wan_vlan(vid))
					{
						//special case: lan-wan bridge and lan-wan nat have same vid
						for (i=0;i<RTL_IMGP_MAX_BRMODULE;i++)
						{
							igmpModuleIndex=rtl_get_brIgmpModuleIndexbyId(i,brName);
							if(igmpModuleIndex==0xFFFFFFFF)
								continue;

							ret= rtl_getMulticastDataFwdInfo(igmpModuleIndex, &multicastDataInfo, &multicastFwdInfo);
							if(ret==SUCCESS)
							{
								toCpuFlag |= multicastFwdInfo.cpuFlag;
								joinBrNum ++;
							}


						}
						if(toCpuFlag==0 && joinBrNum != 0)
						{
					#if defined CONFIG_RTL_MULTICAST_PORT_MAPPING
							//mapPortMask need to be checked!
							mapPortMask = rtl865x_getVlanPortMask(vid) & ((1<<RTL8651_MAC_NUMBER)-1);
							rtl865x_nic_MulticastHardwareAccelerate(nicMCastFwdInfo.fwdPortMask,pid,vid, multicastDataInfo.sourceIp[0], multicastDataInfo.groupAddr[0], mapPortMask);
					#else
							rtl865x_nic_MulticastHardwareAccelerate(nicMCastFwdInfo.fwdPortMask,pid,vid, multicastDataInfo.sourceIp[0], multicastDataInfo.groupAddr[0]);
					#endif
						}
					}
				#else
					if(is_nat_wan_vlan(vid) || (vid !=info->priv->id))
					{
						rtl865x_nic_MulticastHardwareAccelerate(nicMCastFwdInfo.fwdPortMask,pid,vid, multicastDataInfo.sourceIp[0], multicastDataInfo.groupAddr[0]);
					}
				#endif
				}
				#if 0
				vlanportmask = (~(1<<pid)) & nicMCastFwdInfo.fwdPortMask &cp_this->portmask& ((1<<RTL8651_MAC_NUMBER)-1);
				if(linux_vlan_enable)
					re865x_relayTrappedMCast( skb, vid, vlanportmask, TRUE);
				else
				#endif
				#endif
				re865x_relayTrappedMCast( skb, vid, vlanRelayPortMask, TRUE);
				#endif/*end of CONFIG_RTL_HARDWARE_MULTICAST*/
			}
			else if(l4Protocol==IPPROTO_IGMP)
			{
				#ifdef CONFIG_RTK_VLAN_WAN_TAG_SUPPORT
				if(vlan_bridge_tag && !strcmp(cp_this->dev->name,RTL_PS_ETH_NAME_ETH2))
				{
					if(igmpsnoopenabled && (nicIgmpModuleIndex_2!=0xFFFFFFFF))
					{
						rtl_igmpMldProcess(nicIgmpModuleIndex_2, skb->data, pid, &vlanRelayPortMask);
						//just flooding
						vlanRelayPortMask=rtl865x_getVlanPortMask(vid) & (~(1<<pid)) & ((1<<RTL8651_MAC_NUMBER)-1);
					}
				}
				else
				{
				#endif
				if(igmpsnoopenabled && (nicIgmpModuleIndex!=0xFFFFFFFF))
				{
					/*igmp packet*/
				#if defined (CONFIG_NETFILTER)
					if(rtl_MulticastRxFilterOff(skb, 4) == true)
					{
						#ifdef CONFIG_RTL_VLAN_8021Q
						if(linux_vlan_enable)
							goto relay_out;
						else
							return 0;
						#else
						return 0;//filter by iptables
						#endif
					}
				#endif
					rtl_igmpMldProcess(nicIgmpModuleIndex, skb->data, pid, &vlanRelayPortMask);
					#ifdef CONFIG_RTL_VLAN_PASSTHROUGH_SUPPORT
					vlanRelayPortMask=rtl865x_getVlanPortMask(pvid) & vlanRelayPortMask & ((1<<RTL8651_MAC_NUMBER)-1);
					#else
					vlanRelayPortMask=rtl865x_getVlanPortMask(vid) & vlanRelayPortMask & ((1<<RTL8651_MAC_NUMBER)-1);
					#endif
				}
		#ifdef CONFIG_RTK_VLAN_WAN_TAG_SUPPORT
				}
		#endif
				re865x_relayTrappedMCast( skb, vid, vlanRelayPortMask, TRUE);

			}
			else
			{

				#if defined (CONFIG_RTL_HARDWARE_MULTICAST)
				re865x_relayTrappedMCast( skb, vid, vlanRelayPortMask, TRUE);
				#endif

			}
		}
#endif

	}
#if defined (CONFIG_RTL_MLD_SNOOPING)
#if 0
	else if ((skb->data[0]==0x33) && (skb->data[1]==0x33) && (skb->data[2]!=0xff))
#else
	else if((mCastFlag ==IPV6_MCAST_PACKET)||mCastFlag ==SIXRD_MCAST_PACKET)
#endif
	{

		#if defined (CONFIG_RTL_HARDWARE_MULTICAST)
		skb->srcVlanId=vid;
		skb->srcPort=pid;
		#endif

		if(mldSnoopEnabled!=TRUE)
		{
			#ifdef CONFIG_RTL_VLAN_8021Q
			if(linux_vlan_enable)
				goto relay_out;
			else
				return 0;
			#else
			return 0;
			#endif
		}

		#if 1//defined(CONFIG_RTL_CUSTOM_PASSTHRU)
		#if defined(CONFIG_RTL_VLAN_8021Q)
		if (linux_vlan_enable)
		{
			if (rtl_isWanPort(info->priv, vid, skb, pid))
			{
				goto relay_out;
			}
		}
		else
		{
			if (rtl_isWanDev(cp_this)==TRUE)
			{
				/*don't relay it,let linux protocol stack bridge handle it*/
				return 0;
			}

		}
		#else
		if (rtl_isWanDev(cp_this)==TRUE)
		{
			/*don't relay it,let linux protocol stack bridge handle it*/
			return 0;
		}
		#endif
		#endif

		#if defined(CONFIG_RTK_VLAN_SUPPORT)
		if(rtk_vlan_support_enable)
		{
			/*let bridge handle it*/
			#ifdef CONFIG_RTL_VLAN_8021Q
			goto relay_out;
			#else
			return 0;
			#endif
		}
		#endif

		#if defined (CONFIG_RTL_MULTI_LAN_DEV)
		return 0;
		#endif

		/*when enable mld snooping, gateway will add acl to trap packet with dmac equal to 0x33-33-xx-xx-xx-xx */
		ipv6h=re865x_getIpv6Header(skb->data);
		if(ipv6h!=NULL)
		{
			l4Protocol=re865x_getIpv6TransportProtocol(ipv6h);
			/*udp or tcp packet*/
			if((l4Protocol==IPPROTO_UDP) || (l4Protocol==IPPROTO_TCP))
			{

				if(nicIgmpModuleIndex!=0xFFFFFFFF)
				{
					multicastDataInfo.ipVersion=6;
					memcpy(&multicastDataInfo.sourceIp, &ipv6h->saddr, 16);
					memcpy(&multicastDataInfo.groupAddr, &ipv6h->daddr, 16);

					multicastDataInfo.sourceIp[0] = ntohl(multicastDataInfo.sourceIp[0]);
					multicastDataInfo.sourceIp[1] = ntohl(multicastDataInfo.sourceIp[1]);
					multicastDataInfo.sourceIp[2] = ntohl(multicastDataInfo.sourceIp[2]);
					multicastDataInfo.sourceIp[3] = ntohl(multicastDataInfo.sourceIp[3]);
					multicastDataInfo.groupAddr[0] = ntohl(multicastDataInfo.groupAddr[0]);
					multicastDataInfo.groupAddr[1] = ntohl(multicastDataInfo.groupAddr[1]);
					multicastDataInfo.groupAddr[2] = ntohl(multicastDataInfo.groupAddr[2]);
					multicastDataInfo.groupAddr[3] = ntohl(multicastDataInfo.groupAddr[3]);

					ret=rtl_getMulticastDataFwdInfo(nicIgmpModuleIndex, &multicastDataInfo, &nicMCastFwdInfo);
					#ifndef CONFIG_RTL_VLAN_8021Q
					#ifdef CONFIG_RTL_VLAN_PASSTHROUGH_SUPPORT
					vlanRelayPortMask=rtl865x_getVlanPortMask(pvid)& (~(1<<pid)) & nicMCastFwdInfo.fwdPortMask &cp_this->portmask& ((1<<RTL8651_MAC_NUMBER)-1);
					#else
					vlanRelayPortMask=rtl865x_getVlanPortMask(vid)& (~(1<<pid)) & nicMCastFwdInfo.fwdPortMask &cp_this->portmask& ((1<<RTL8651_MAC_NUMBER)-1);
					#endif
					#else
					vlanRelayPortMask=rtl865x_getVlanPortMask(vid)& (~(1<<pid)) & nicMCastFwdInfo.fwdPortMask & ((1<<RTL8651_MAC_NUMBER)-1);
					#endif
					if(ret==SUCCESS)
					{
#if defined (CONFIG_RT_MULTIPLE_BR_SUPPORT) && defined(CONFIG_RTL_VLAN_8021Q)
						if(linux_vlan_enable)
						{
							if(vlanRelayPortMask==0)
							{
								//current br module eth client not join, but the other br module eth client join -> flooding
								igmpModuleIndex=rtl_get_brIgmpModuleIndexbyVid(vid);
								if(igmpModuleIndex!=0xFFFFFFFF)
								{
							 		ret=rtl_getMulticastDataFwdInfo(igmpModuleIndex, &multicastDataInfo, &multicastFwdInfo);
									if(ret!=SUCCESS)
									{
										vlanRelayPortMask=rtl865x_getVlanPortMask(vid)& (~(1<<pid))& ((1<<RTL8651_MAC_NUMBER)-1);
									}
								}
							}
						}
#endif

					}
					else
					{
#if defined (CONFIG_RT_MULTIPLE_BR_SUPPORT) && defined(CONFIG_RTL_VLAN_8021Q)
						if(linux_vlan_enable)
						{
							igmpModuleIndex=rtl_get_brIgmpModuleIndexbyVid(vid);
							if(igmpModuleIndex!=0xFFFFFFFF)
							{
								ret=rtl_getMulticastDataFwdInfo(igmpModuleIndex, &multicastDataInfo, &multicastFwdInfo);
								if(ret==SUCCESS)
								{
									vlanRelayPortMask=0;
								}
							}
						}
						else
						{
							igmpModuleIndex=rtl_get_brIgmpModuleIndexbyName(RTL_BR_NAME, &br_index);
							ret=rtl_getMulticastDataFwdInfo(igmpModuleIndex, &multicastDataInfo, &br0MCastFwdInfo);
							if(ret==SUCCESS)
							{
								/*there is wireless client,can not flooding in vlan */
								vlanRelayPortMask=0;
							}
						}

#else
						ret=rtl_getMulticastDataFwdInfo(brIgmpModuleIndex, &multicastDataInfo, &br0MCastFwdInfo);
						if(ret==SUCCESS)
						{
							/*there is wireless client,can not flooding in vlan */
							vlanRelayPortMask=0;
						}
#endif
					}
				}

				re865x_relayTrappedMCast( skb, vid, vlanRelayPortMask, TRUE);

			}
			else if(l4Protocol==IPPROTO_ICMPV6)
			{
				/*icmp packet*/
				if(nicIgmpModuleIndex!=0xFFFFFFFF)
				{
					rtl_igmpMldProcess(nicIgmpModuleIndex, skb->data, pid, &vlanRelayPortMask);
					#ifdef CONFIG_RTL_VLAN_PASSTHROUGH_SUPPORT
					vlanRelayPortMask=rtl865x_getVlanPortMask(pvid) & vlanRelayPortMask & ((1<<RTL8651_MAC_NUMBER)-1);
					#else
					vlanRelayPortMask=rtl865x_getVlanPortMask(vid) & vlanRelayPortMask & ((1<<RTL8651_MAC_NUMBER)-1);
					#endif
				}

				re865x_relayTrappedMCast( skb, vid, vlanRelayPortMask, TRUE);

			}
			else
			{
				re865x_relayTrappedMCast( skb, vid, vlanRelayPortMask, TRUE);
			}
		}
		else
		{
			re865x_relayTrappedMCast( skb, vid, vlanRelayPortMask, TRUE);
		}

	}
#if (defined (CONFIG_RTL_8198C)&& defined (CONFIG_RTL_HARDWARE_MULTICAST))
	else if (mCastFlag ==IPV6_MCAST_PACKET2)
	{
#if defined (CONFIG_RTL_HARDWARE_MULTICAST)
		skb->srcVlanId=vid;
		skb->srcPort=pid;
#endif
		vlanRelayPortMask=rtl865x_getVlanPortMask(vid)& (~(1<<pid))  &cp_this->portmask& ((1<<RTL8651_MAC_NUMBER)-1);
		re865x_relayTrappedMCast( skb, vid, vlanRelayPortMask, TRUE);
	}
#endif
#endif
	else
	{
		#if defined (CONFIG_RTL_HARDWARE_MULTICAST)
		skb->srcVlanId=0;
		skb->srcPort=0xFFFF;
		#endif
	}

	#if 0
	if(rx_skb_queue.qlen < (rtl865x_maxPreAllocRxSkb/3))
	{
		refill_rx_skb();
	}
	#endif

#ifdef  CONFIG_RTL_VLAN_8021Q
relay_out:
	if(vlan_ctl_p->flag) // vlan enable
	{
		if(type == VID_FROM_PACKET)
		{
			//recover packet's tag if exist here for protcol stack process
			rtk_insert_vlan_tag(vid,skb);
		}
	}
#endif
	return 0;
}
#endif	/*end of CONFIG_RTL865X_IGMP_SNOOPING*/

static inline int32 rtl_isWanDev(struct dev_priv *cp)
{
#if defined(CONFIG_RTK_VLAN_SUPPORT)
	return (!cp->vlan_setting.is_lan);
#else
	#if defined(CONFIG_RTL_MULTIPLE_WAN)
		return (cp->id==RTL_WANVLANID || cp->id == RTL_WAN_1_VLANID);
	#else
		#if defined(CONFIG_RTL_VLAN_8021Q) && !defined(CONFIG_RTL_8021Q_VLAN_SUPPORT_SRC_TAG)
		if (linux_vlan_enable && linux_vlan_hw_support_enable)
			return (cp->id==rtk_vlan_wan_vid); //mark_vc
		else
		#endif
			return (cp->id==RTL_WANVLANID);
	#endif
#endif
}

#if defined(CONFIG_RTL_CUSTOM_PASSTHRU)
static inline int32 rtl_isPassthruFrame(uint8 *data)
{
	int	ret;

	ret = FAILED;
	if (oldStatus)
	{
		if (oldStatus&IP6_PASSTHRU_MASK)
		{
			if ((*((uint16*)(data+(ETH_ALEN<<1)))==__constant_htons(ETH_P_IPV6)) ||
				((*((uint16*)(data+(ETH_ALEN<<1)))==__constant_htons(ETH_P_8021Q))&&(*((uint16*)(data+(ETH_ALEN<<1)+VLAN_HLEN))==__constant_htons(ETH_P_IPV6))))
			{
				ret = SUCCESS;
			}
		}
		#if defined(CONFIG_RTL_CUSTOM_PASSTHRU_PPPOE)
		if (oldStatus&PPPOE_PASSTHRU_MASK)
		{
			if (((*((uint16*)(data+(ETH_ALEN<<1)))==__constant_htons(ETH_P_PPP_SES))||(*((uint16*)(data+(ETH_ALEN<<1)))==__constant_htons(ETH_P_PPP_DISC))) ||
				((*((uint16*)(data+(ETH_ALEN<<1)))==__constant_htons(ETH_P_8021Q))&&((*((uint16*)(data+(ETH_ALEN<<1)+VLAN_HLEN))==__constant_htons(ETH_P_PPP_SES))||(*((uint16*)(data+(ETH_ALEN<<1)+VLAN_HLEN))==__constant_htons(ETH_P_PPP_DISC)))))
			{
				ret = SUCCESS;
			}
		}
		#endif
	}

	return ret;
}

int rtl_getPassthruMask(void)
{

	return oldStatus;
}

#if defined(CONFIG_IPV6)
int rtl_isIpv6McFrame(char * data)
{
	int ret = FAILED;

	if ((data[0]==0x33) && (data[1]==0x33))
		ret = SUCCESS;

	return ret;
}
#endif
#endif

#if defined(RTL_CPU_QOS_ENABLED)
#define MAX_HIGH_PRIO_TRY	20	//Try to rx high prio pkt for MAX_HIGH_PRIO_TRY times.
__DRAM_FWD static 	int	highPrioRxTryCnt;
__DRAM_FWD	static	int	highestPriority;
__DRAM_FWD static 	int	cpuQosHoldLow;
__DRAM_FWD static 	int	totalLowQueueCnt;

static rtl_queue_entry	pktQueueByPri[RTL865X_SWNIC_RXRING_MAX_PKTDESC] = {{0}};

__MIPS16
__IRAM_FWD
int rtl_enqueueSkb(rtl_nicRx_info *info)
{
	rtl_queue_entry	*entry;

	entry = &pktQueueByPri[info->priority];

	if (info->priority<cpuQosHoldLow)
		totalLowQueueCnt++;

	memcpy(&entry->entry[entry->end], info, sizeof(rtl_nicRx_info));
	entry->cnt++;
	entry->end = (entry->end==(RTL_NIC_QUEUE_LEN-1))?0:(entry->end+1);

	if (entry->cnt==RTL_NIC_QUEUE_LEN)
		return SUCCESS;
	else
		return FAILED;
}

__MIPS16
__IRAM_FWD
int rtl_dequeueSkb(rtl_nicRx_info *info)
{
	rtl_queue_entry	*entry;

	entry = &pktQueueByPri[info->priority];

	if(entry->cnt==0)
	{
		return FAILED;
	}
	else
	{
		memcpy(info, &entry->entry[entry->start], sizeof(rtl_nicRx_info));
		entry->cnt--;
		entry->start = (entry->start==(RTL_NIC_QUEUE_LEN-1))?0:(entry->start+1);
		return SUCCESS;
	}
}
#endif

#if defined(RTL_CPU_QOS_ENABLED)
__MIPS16
__IRAM_FWD
static inline int32 rtl_processReceivedInfo(rtl_nicRx_info *info, int nicRxRet)
{
	int	ret;

	ret = RTL_RX_PROCESS_RETURN_BREAK;
	switch(nicRxRet)
	{
		case RTL_NICRX_OK:
			{
				if (highestPriority<info->priority)
				{
					highestPriority = info->priority;
					cpuQosHoldLow = highestPriority;
				}

				if (info->priority==(RTL865X_SWNIC_RXRING_MAX_PKTDESC-1))
				{
					ret = RTL_RX_PROCESS_RETURN_SUCCESS;
				}
				else	if (rtl_enqueueSkb(info) == SUCCESS)
				{
					rtl_dequeueSkb(info);
					ret = RTL_RX_PROCESS_RETURN_SUCCESS;
				}
				else
					ret = RTL_RX_PROCESS_RETURN_CONTINUE;

				break;
			}
		case RTL_NICRX_NULL:
			{
				info->priority = cpuQosHoldLow;
				if (rtl_dequeueSkb(info)==SUCCESS)
				{
					ret = RTL_RX_PROCESS_RETURN_SUCCESS;
				}
				else if((highestPriority>0) && ((--highPrioRxTryCnt)<0))
				{
					//Only for using rxring 0 and rxring 5
					highPrioRxTryCnt=MAX_HIGH_PRIO_TRY;
					highestPriority=0;
					cpuQosHoldLow=highestPriority;
					ret = RTL_RX_PROCESS_RETURN_CONTINUE;
				}
				else if(highestPriority==0)
				{
					/* highestPriority=0 */
					if (cpuQosHoldLow>0)
					{
						swNic_flushRxRingByPriority(cpuQosHoldLow);
					}
					ret = RTL_RX_PROCESS_RETURN_BREAK;
				}

				break;
			}
		case RTL_NICRX_REPEAT:
			ret = RTL_RX_PROCESS_RETURN_BREAK;
			break;
	}
	return ret;
}
#else	/*	defined(RTL_CPU_QOS_ENABLED)	*/
__MIPS16
__IRAM_FWD
static inline int32 rtl_processReceivedInfo(rtl_nicRx_info *info, int nicRxRet)
{
	int	ret;

	ret = RTL_RX_PROCESS_RETURN_BREAK;
	switch(nicRxRet)
	{
		case RTL_NICRX_OK:
			{
				ret = RTL_RX_PROCESS_RETURN_SUCCESS;
				break;
			}
		case RTL_NICRX_NULL:
		case RTL_NICRX_REPEAT:
			break;
	}
	return ret;
}
#endif	/*	defined(RTL_CPU_QOS_ENABLED)	*/

__MIPS16
__IRAM_FWD
static inline int32 rtl_decideRxDevice(rtl_nicRx_info *info)
{
	struct dev_priv	*cp;
	int32			pid, i, ret;
	struct sk_buff 	*skb;
	uint8*			data;
	#if 0//defined(CONFIG_RTL_STP)
	int32 			dev_no;
	#endif
#if defined(CONFIG_RTL_CUSTOM_PASSTHRU)
	unsigned char dest_mac[MAX_ADDR_LEN];
#endif

#ifdef CONFIG_RTK_VLAN_WAN_TAG_SUPPORT
	int32 vid=0;
	vid = info->vid;
#endif
	pid = info->pid;
	skb = info->input;

	data = skb->data;
#if defined(CONFIG_RTL_CUSTOM_PASSTHRU)
	memcpy(dest_mac, data, 6);
	#if defined(CONFIG_IPV6)
	info->oriPriv = NULL;
	#endif
#endif

	info->isPdev=FALSE;
	ret = SUCCESS;

	#if 0//defined(CONFIG_RTL_STP)
	info->isStpVirtualDev=FALSE;
	if ((data[0]&0x01) && !memcmp(data, STPmac, 5) && !(data[5] & 0xF0))
	{
		/* It's a BPDU */
		dev_no = re865x_stp_get_pseudodevno(pid);
		if (dev_no != NO_MAPPING)
		{
			info->priv = _rtl86xx_dev.stp_port[dev_no]->priv;
			info->isStpVirtualDev=TRUE;
		}
		else
		{
			dev_kfree_skb_any(skb);
			ret = FAILED;
		}
	}
	else
	#endif
	{
#ifdef  CONFIG_RTL_VLAN_8021Q
		if (linux_vlan_enable && linux_vlan_hw_support_enable)
		{
			 cp = (struct dev_priv *)rtk_decide_vlan_cp(pid,skb);
			if(cp)
			{
				info->priv = cp;
				//panic_printk("%s %d cp->dev->name %s\n", __FUNCTION__, __LINE__, cp->dev->name);
#if defined(CONFIG_RTL_CUSTOM_PASSTHRU)
				if (SUCCESS==rtl_isPassthruFrame(data)&&(rtl_isWanDev(cp)==TRUE)
					&& (compare_ether_addr((char* )cp->dev->dev_addr, (char*)dest_mac)))
				{
#if defined(CONFIG_IPV6)
					if (rtl_isIpv6McFrame(data)==SUCCESS)
						info->oriPriv = info->priv;
#endif

					info->priv = NETDRV_PRIV(_rtl86xx_dev.pdev);
					info->isPdev=TRUE;
				}

#endif
				goto out;
			}
		}
#endif
		#if defined(CONFIG_RTL_MULTIPLE_WAN)
		if (rtl865x_curOpMode == GATEWAY_MODE) {
			if (0==(skb->data[0]&0x01)) {
				if (rtl865x_decideUcDevice(skb->data, pid, info) == SUCCESS) {
					ret = SUCCESS;
					goto out;
				}
			} else {
				if (skb->data[0] != 0xff) {
					if (rtl865x_decideMcDevice(pid, info) == SUCCESS) {
						ret = SUCCESS;
						goto out;
					}
				} else {
					if (rtl865x_decideBcDevice(skb, pid, info) == SUCCESS) {
						ret = SUCCESS;
						goto out;
					}
				}
			}
		}
		#endif
		#ifdef CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT
		// wan port always use wan cp
		if( rtl_hw_vlan_enable && (pid == RTK_WAN_PORT_IDX))
		{
			cp = (struct dev_priv *)rtl_get_wan_cp();
			info->priv = cp;
			goto out;
		}
		//p0~03 , decide by cp port-base search below
		#endif

		for(i = 0; i < ETH_INTF_NUM; i++)
		{
			cp = ((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[i]));
			//printk("=========%s(%d),cp(%s),i(%d)\n",__FUNCTION__,__LINE__,cp->dev->name,i);
#ifdef CONFIG_RTK_VLAN_WAN_TAG_SUPPORT
			if(*((unsigned short *)(skb->data+(ETH_ALEN<<1)))== __constant_htons(ETH_P_8021Q))
			{
				vid = *((unsigned short *)(skb->data+(ETH_ALEN<<1)+2))&0x0fff;

				if( vlan_tag && vid == vlan_tag)
				{
					if(rtl865x_curOpMode == BRIDGE_MODE)
						vid = RTL_LANVLANID;
				}
				else if (vlan_bridge_tag && vid == vlan_bridge_tag)
				{
					vid = vlan_bridge_tag;
				}
				else if(vlan_bridge_multicast_tag && vid == vlan_bridge_multicast_tag)
				{
					vid = vlan_bridge_tag;
				}
				#ifdef CONFIG_VLAN_8021Q
				else
				{
					#if defined(CONFIG_RTD_1295_HWNAT)
					vid = vlanconfig[RTL_WAN_IDX].vid;//default from WAN eth1
					#else //defined(CONFIG_RTD_1295_HWNAT)
					vid = vlanconfig[1].vid;//default from WAN eth1
					#endif //defined(CONFIG_RTD_1295_HWNAT)
				}
				#endif
				if(_rtl86xx_dev.dev[i] && cp && cp->opened && (vid==cp->id))
				{
					info->priv = cp;
					ret = SUCCESS;
					break;
				}
			}
			else
			{
				if(_rtl86xx_dev.dev[i] && cp && cp->opened && (cp->portmask & (1<<pid)))
				{
					info->priv = cp;
					ret = SUCCESS;
					break;
				}
			}
#elif (defined(CONFIG_RTL_8367R_SUPPORT) || defined(CONFIG_RTL_8370_SUPPORT))&& !defined(CONFIG_RTL_CPU_TAG)
			if(_rtl86xx_dev.dev[i] && cp && cp->opened && (cp->id == info->vid))
			{
				info->priv = cp;
				ret = SUCCESS;
				break;
			}

#else
			if(_rtl86xx_dev.dev[i] && cp && cp->opened && (cp->portmask & (1<<pid)))
			{

				info->priv = cp;
				break;
			}
#endif
		}

		//printk("====%s(%d),dev(%s),i(%d)\n",__FUNCTION__,__LINE__,cp->dev->name,i);
		if(ETH_INTF_NUM==i)
		{
			info->priv = NULL;
			dev_kfree_skb_any(skb);
			ret = FAILED;
		}
		#if defined(CONFIG_RTL_CUSTOM_PASSTHRU)
		else if (SUCCESS==rtl_isPassthruFrame(data)&&(rtl_isWanDev(cp)==TRUE)
			&& (!ether_addr_equal((char* )cp->dev->dev_addr, (char*)dest_mac)))
		{
		#if defined(CONFIG_IPV6)
			if (rtl_isIpv6McFrame(data)==SUCCESS)
				info->oriPriv = info->priv;
		#endif
			info->priv = NETDRV_PRIV(_rtl86xx_dev.pdev);
			info->isPdev=TRUE;
		}
		#endif
	}
#if defined(CONFIG_RTL_MULTIPLE_WAN) || defined(CONFIG_RTL_VLAN_8021Q) ||  defined(CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT)
out:
#endif
	return ret;
}

#if defined(CONFIG_RTL_ULINKER_BRSC)
#include "linux/ulinker_brsc.h"
#endif

#if defined(CONFIG_RTL_HTTP_REDIRECT)
inline int is_skb_http_packet(struct sk_buff* skb)
{
	if(!skb)
		return 0;
	if(*(unsigned short *)(skb->data+ETH_ALEN*2)==htons(0x0800) &&
	   *(unsigned char *)(skb->data+0x17) == 6 &&
		*(unsigned short *)(skb->data+0x24) == htons(80))
		return 1;
	return 0;
}
#endif
#if defined(CONFIG_RTL_DNS_TRAP)
int rtl_add_Acl_for_dns_trap(struct rtl865x_vlanConfig* vlanConfig)
{
	int i;
	#if defined (CONFIG_RTL_MULTI_LAN_DEV) || defined(CONFIG_RTK_VLAN_SUPPORT) || defined(CONFIG_RTL_VLAN_8021Q)
	static struct rtl865x_vlanConfig tmpVlanConfig[NETIF_NUMBER];
	#endif
	struct rtl865x_vlanConfig *pVlanConfig=NULL;
	rtl865x_AclRule_t	rule;
	int ret=FAILED;

	if(vlanConfig==NULL)
	{
		return FAILED;
	}

#if defined (CONFIG_RTL_MULTI_LAN_DEV) ||defined(CONFIG_RTK_VLAN_SUPPORT) || defined(CONFIG_RTL_VLAN_8021Q)
	re865x_packVlanConfig(vlanConfig, tmpVlanConfig);
	pVlanConfig=tmpVlanConfig;
#else
	pVlanConfig=vlanConfig;
#endif

	for(i=0; pVlanConfig[i].vid != 0; i++)
	{
		if (IF_ETHER!=pVlanConfig[i].if_type)
		{
			continue;
		}

		if(pVlanConfig[i].isWan==0)/*lan config*/
		{

			rtl865x_regist_aclChain(pVlanConfig[i].ifname, RTL865X_ACL_DNSTRAP_USED, RTL865X_ACL_INGRESS);

			//permit  from cpu port
			bzero((void*)&rule,sizeof(rtl865x_AclRule_t));
			rule.ruleType_ = RTL865X_ACL_SRCFILTER;
			rule.actionType_		= RTL865X_ACL_PERMIT;
			rule.pktOpApp_ 		= RTL865X_ACL_ALL_LAYER;

			rule.srcFilterPort_ = 1<<8; //cpu port
			rule.srcFilterPortMask_ = 1<<8;

			rule.srcFilterPortUpperBound_ = 65535;
			rule.srcFilterPortLowerBound_ = 0;

			ret= rtl865x_add_acl(&rule, pVlanConfig[i].ifname, RTL865X_ACL_DNSTRAP_USED);

			//trap dns packets to cpu
			bzero((void*)&rule,sizeof(rtl865x_AclRule_t));
			rule.ruleType_ = RTL865X_ACL_DSTFILTER;
			rule.actionType_		= RTL865X_ACL_TOCPU;
			rule.pktOpApp_ 		= RTL865X_ACL_ALL_LAYER;

			rule.dstFilterPortUpperBound_ = 53;
			rule.dstFilterPortLowerBound_ = 53;

			ret= rtl865x_add_acl(&rule, pVlanConfig[i].ifname, RTL865X_ACL_DNSTRAP_USED);

		}
		else/*wan config*/
		{

			//trap dns packets to cpu
			rtl865x_regist_aclChain(pVlanConfig[i].ifname, RTL865X_ACL_DNSTRAP_USED, RTL865X_ACL_INGRESS);

			//permit  from cpu port
			bzero((void*)&rule,sizeof(rtl865x_AclRule_t));
			rule.ruleType_ = RTL865X_ACL_SRCFILTER;
			rule.actionType_		= RTL865X_ACL_PERMIT;
			rule.pktOpApp_ 		= RTL865X_ACL_ALL_LAYER;

			rule.srcFilterPort_ = 1<<8; //cpu port
			rule.srcFilterPortMask_ = 1<<8;

			ret= rtl865x_add_acl(&rule, pVlanConfig[i].ifname, RTL865X_ACL_DNSTRAP_USED);

			//trap dns packets to cpu
			bzero((void*)&rule,sizeof(rtl865x_AclRule_t));
			rule.ruleType_ = RTL865X_ACL_DSTFILTER;
			rule.actionType_		= RTL865X_ACL_TOCPU;
			rule.pktOpApp_ 		= RTL865X_ACL_ALL_LAYER;

			rule.dstFilterPortUpperBound_ = 53;
			rule.dstFilterPortLowerBound_ = 53;

			ret= rtl865x_add_acl(&rule, pVlanConfig[i].ifname, RTL865X_ACL_DNSTRAP_USED);
		}

		#if defined(CONFIG_RTL_IPTABLES_RULE_2_ACL)
		if(rtl865x_curOpMode == WISP_MODE)
		#endif
			rtl865x_reConfigDefaultAcl(pVlanConfig[i].ifname);
	}
	#if defined(CONFIG_RTL_8367R_SUPPORT)
	rtl_8367_add_acl_for_dns(0);
	#endif
	return SUCCESS;
}
int rtl_remove_Acl_for_dns_trap(struct rtl865x_vlanConfig* vlanConfig)
{
	int i;
	#if defined (CONFIG_RTL_MULTI_LAN_DEV) ||defined(CONFIG_RTK_VLAN_SUPPORT) || defined(CONFIG_RTL_VLAN_8021Q)
	struct rtl865x_vlanConfig tmpVlanConfig[NETIF_NUMBER];
	#endif

	struct rtl865x_vlanConfig *pVlanConfig=NULL;

	if(vlanConfig==NULL)
	{
		return FAILED;
	}
#if defined (CONFIG_RTL_MULTI_LAN_DEV) ||defined(CONFIG_RTK_VLAN_SUPPORT) || defined(CONFIG_RTL_VLAN_8021Q)
	re865x_packVlanConfig(vlanConfig, tmpVlanConfig);
	pVlanConfig=tmpVlanConfig;
#else
	pVlanConfig=vlanConfig;
#endif

	for(i=0; pVlanConfig[i].vid != 0; i++)
	{
		if (IF_ETHER!=pVlanConfig[i].if_type)
		{
			continue;
		}

		if(pVlanConfig[i].isWan==0)/*lan config*/
		{
			rtl865x_unRegist_aclChain(pVlanConfig[i].ifname, RTL865X_ACL_DNSTRAP_USED, RTL865X_ACL_INGRESS);
		}
		else/*wan config*/
		{
			rtl865x_unRegist_aclChain(pVlanConfig[i].ifname, RTL865X_ACL_DNSTRAP_USED, RTL865X_ACL_INGRESS);
		}

	}


#if defined (CONFIG_RTL_IPTABLES_RULE_2_ACL)

#else
#if defined (CONFIG_RTK_VLAN_SUPPORT)
		if(rtk_vlan_support_enable==0)
		{
			rtl865x_setDefACLForAllNetif(RTL865X_ACLTBL_PERMIT_ALL, RTL865X_ACLTBL_PERMIT_ALL, RTL865X_ACLTBL_PERMIT_ALL, RTL865X_ACLTBL_PERMIT_ALL);
		}
		else
		{
			rtl865x_setDefACLForAllNetif(RTL865X_ACLTBL_ALL_TO_CPU, RTL865X_ACLTBL_ALL_TO_CPU, RTL865X_ACLTBL_PERMIT_ALL, RTL865X_ACLTBL_PERMIT_ALL);
		}
#else
		rtl865x_setDefACLForAllNetif(RTL865X_ACLTBL_PERMIT_ALL, RTL865X_ACLTBL_PERMIT_ALL, RTL865X_ACLTBL_PERMIT_ALL, RTL865X_ACLTBL_PERMIT_ALL);
#endif
#endif

	#if defined(CONFIG_RTL_8367R_SUPPORT)
	rtl_8367_remove_acl_for_dns(0);
	#endif

	return SUCCESS;
}

inline int is_skb_dns_packet(struct sk_buff* skb)
{
	if(!skb)
		return 0;
	if(*(unsigned short *)(skb->data+ETH_ALEN*2)==htons(0x0800) &&
		*(unsigned char *)(skb->data+0x17) == 17 &&
		*(unsigned short *)(skb->data+0x24) == htons(53))
		return 1;
	return 0;
}
#endif

#if defined(BR_SHORTCUT)
__MIPS16
__IRAM_FWD
static inline int32 rtl_processBridgeShortCut(struct sk_buff *skb, struct dev_priv *cp_this, rtl_nicRx_info *info)
{
	struct net_device *dev;
	#if defined(CONFIG_RTL_VLAN_8021Q)&& defined(CONFIG_RTL_BRSHORTCUT_LINUX_VLAN_CTL)
	unsigned short vid = 0;
	struct sk_buff *newskb;
	#endif

	/*2011-09-13 fix wlan sta can not access internet when wan mac clone sta mac*/
	if(rtl_isWanDev(cp_this) && (rtl865x_curOpMode == GATEWAY_MODE))
	{
	#if defined(CONFIG_RTL_ULINKER_BRSC)
		;//tmp, fixme
	#else
		return FAILED;
	#endif
	}

	/*if lltd, don't go shortcut*/
	if(*(unsigned short *)(skb->data+ETH_ALEN*2)==htons(0x88d9))
		return FAILED;

#if defined(CONFIG_RTL_HTTP_REDIRECT)
	if(is_skb_http_packet(skb))
		return FAILED;
#endif
#if defined(CONFIG_RTL_DNS_TRAP)
	if(is_skb_dns_packet(skb))
		return FAILED;
#endif


	if (
		#if 0
		(eth_flag & BIT(2)) &&
		#endif
#if defined(CONFIG_DOMAIN_NAME_QUERY_SUPPORT) || defined(CONFIG_RTL_ULINKER)
				(skb->data[37] != 68) && /*port 68 is dhcp dest port. In order to hack dns ip, so dhcp packet can't enter bridge short cut.*/
#endif
	#if defined(CONFIG_WIRELESS_LAN_MODULE)
		(wirelessnet_hook_shortcut !=NULL ) && ((dev = wirelessnet_hook_shortcut(skb->data)) != NULL)
	#else
	  #if defined(CONFIG_RTL_ULINKER_BRSC)
		(((dev=brsc_get_cached_dev(0, skb->data))!=NULL) || ((dev = get_shortcut_dev(skb->data)) != NULL))
	  #else
		((dev = get_shortcut_dev(skb->data)) != NULL)
	  #endif
	#endif
	#ifdef CONFIG_RTK_VLAN_WAN_TAG_SUPPORT
		&& rtl865x_same_root(skb->dev,dev)
	#endif
	)
	{
	#if defined(CONFIG_RTL_ULINKER_BRSC)
		if (cached_usb.dev && dev == cached_usb.dev) {
			BRSC_COUNTER_UPDATE(tx_eth_sc);
			BDBG_BRSC("BRSC: get shortcut dev[%s]\n", cached_usb.dev->name);

			if (skb->dev)
				brsc_cache_dev(2, skb->dev, skb->data+ETH_ALEN);
		}
		else
	#endif /* #if defined(CONFIG_RTL_ULINKER_BRSC) */
	{
		#if defined(CONFIG_RTL_HARDWARE_NAT)
		if (memcmp(&skb->data[ETH_ALEN], cp_this->dev->dev_addr, ETH_ALEN))
		#endif
		{
			if (memcmp(cached_eth_addr, &skb->data[ETH_ALEN], ETH_ALEN) == 0) {
				//memcpy(cached_eth_addr, &skb->data[ETH_ALEN], ETH_ALEN);
				cached_dev = cp_this->dev;
				last_used = 0;
			}
			else if (memcmp(cached_eth_addr2, &skb->data[ETH_ALEN], ETH_ALEN) == 0) {
				//memcpy(cached_eth_addr2, &skb->data[ETH_ALEN], ETH_ALEN);
				cached_dev2 = cp_this->dev;
				last_used = 1;
			}
			else if (memcmp(cached_eth_addr3, &skb->data[ETH_ALEN], ETH_ALEN) == 0) {
				cached_dev3 = cp_this->dev;
				last_used = 2;
			}
			else if (memcmp(cached_eth_addr4, &skb->data[ETH_ALEN], ETH_ALEN) == 0) {
				cached_dev4 = cp_this->dev;
				last_used = 3;
			}
			else if (last_used == 3) {
				memcpy(cached_eth_addr3, &skb->data[ETH_ALEN], ETH_ALEN);
				cached_dev3 = cp_this->dev;
				last_used = 2;
			}
			else if (last_used == 2) {
				memcpy(cached_eth_addr2, &skb->data[ETH_ALEN], ETH_ALEN);
				cached_dev2 = cp_this->dev;
				last_used = 1;
			}
			else if (last_used == 1) {
				memcpy(cached_eth_addr, &skb->data[ETH_ALEN], ETH_ALEN);
				cached_dev = cp_this->dev;
				last_used = 0;
			}
			else {
				memcpy(cached_eth_addr4, &skb->data[ETH_ALEN], ETH_ALEN);
				cached_dev4 = cp_this->dev;
				last_used = 3;
			}
		}
	}
		#if defined(CONFIG_RTL_HW_QOS_SUPPORT) && defined(CONFIG_RTL_HARDWARE_NAT)
		skb->cb[0] = info->rxPri;
		#endif

#if defined(CONFIG_RTL_VLAN_8021Q) && defined(CONFIG_RTL_BRSHORTCUT_LINUX_VLAN_CTL)
		/* remove vlan tag if it is not need to tagged out*/
		if(linux_vlan_enable){
			if(*((unsigned short *)(skb->data+(ETH_ALEN<<1))) == __constant_htons(ETH_P_8021Q)){
				vid = ntohs(*(uint16*)(skb->data+(ETH_ALEN<<1)+2)) & 0x0fff;
				if(!(vlan_ctl_p->group[vid].tagMemberMask&(1<<dev->vlan_member_map))){
					if (skb_cloned(skb)){
						newskb = skb_copy(skb, GFP_ATOMIC);
						if (newskb == NULL) {
							return FAILED;
						}
						dev_kfree_skb_any(skb);
						skb = newskb;
					}
					memmove(skb->data+VLAN_HLEN, skb->data, ETH_ALEN<<1);
					skb_pull(skb,VLAN_HLEN);
				}
			}
		}
#endif
		/*skb->dev = dev;*/ /* for performance */
		#if defined(CONFIG_COMPAT_NET_DEV_OPS)
		dev->hard_start_xmit(skb, dev);
		#else
		dev->netdev_ops->ndo_start_xmit(skb,dev);
		#endif
		DEBUG_ERR("[%s][%d]-[%s]\n", __FUNCTION__, __LINE__, skb->dev->name);
		return SUCCESS;
	#if 0
	} else if (info->vid == PKTHDR_EXTPORT_MAGIC) {
		/*	the pkt should be tx out by a wlanx interface.
		*	But we don't not which one...
		*	Pass it to protocol stack.
		*/
		/*
		cp_this->net_stats.rx_dropped++;
		dev_kfree_skb_any(skb);
		return SUCCESS;
		*/
	#endif
	}
	return FAILED;
}
#elif defined(CONFIG_RTL_HARDWARE_NAT)&&(defined(CONFIG_RTL8192SE)||defined(CONFIG_RTL8192CD))
#if 0
__MIPS16
__IRAM_FWD
static inline int32 rtl_processExtensionPortShortCut(struct sk_buff *skb, struct dev_priv *cp_this, rtl_nicRx_info *info)
{
	struct net_device *dev;

	if ((PKTHDR_EXTPORT_MAGIC!=info->vid)||(info->pid!=PKTHDR_EXTPORT_P3))
		return FAILED;

	dev = get_shortcut_dev(skb->data);

	if (dev!=NULL) {
		#if defined(CONFIG_RTL_HW_QOS_SUPPORT) && defined(CONFIG_RTL_HARDWARE_NAT)
		skb->cb[0] = info->rxPri;
		#endif
		/*skb->dev = dev;*/ /* for performance */
		#if defined(CONFIG_COMPAT_NET_DEV_OPS)
		dev->hard_start_xmit(skb, dev);
		#else
		dev->netdev_ops->ndo_start_xmit(skb,dev);
		#endif
		DEBUG_ERR("[%s][%d]-[%s]\n", __FUNCTION__, __LINE__, skb->dev->name);

		return SUCCESS;
	#if 0
	} else {
		/*	the pkt should be tx out by a wlanx interface.
		*	But we don't not which one...
		*	Pass it to protocol stack.
		*/
		/*
		cp_this->net_stats.rx_dropped++;
		dev_kfree_skb_any(skb);
		*/
	#endif
	}

	return FAILED;
}
#endif
#endif

__MIPS16
__IRAM_FWD
static inline void rtl_processRxToProtcolStack(struct sk_buff *skb, struct dev_priv *cp_this)
{
#ifdef CONFIG_RTL_8198C
	unsigned long flags=0;
	SMP_LOCK_ETH(flags);
#endif

	skb->protocol = eth_type_trans(skb, skb->dev);

#ifdef CONFIG_RTL_IPV6READYLOGO
	if(skb->protocol == 0x86dd)
		skb->ip_summed = CHECKSUM_NONE;
	else
		skb->ip_summed = CHECKSUM_UNNECESSARY;
#else
	//skb->ip_summed = CHECKSUM_NONE;
	/* It must be a TCP or UDP packet with a valid checksum */
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	//printk("[%s][%d]-skb->dev[%s],proto(0x%x)\n", __FUNCTION__, __LINE__, skb->dev->name,skb->protocol);
#endif

#if defined(CONFIG_RTL_IP_POLICY_ROUTING_SUPPORT)
	//skb->vlan_member = cp_this->port_member;
	//skb->src_port = IF_SWITCH;
	skb->switch_port = skb->dev->name;
#endif

#if defined(RX_TASKLET)
	#if defined(CONFIG_RTL_LOCAL_PUBLIC)
	skb->localPublicFlags = 0;
	#endif
	#if defined(CONFIG_RTL_FAST_BRIDGE)
	skb->fast_br_forwarding_flags = 0;
	#endif

	#ifdef CONFIG_SMP
	netif_rx(skb);
	#else

	#ifdef CONFIG_RTL_8197F
	netif_rx(skb);
	#else
	netif_receive_skb(skb);
	#endif
	#endif

#else	/*	defined(RX_TASKLET)	*/
	netif_rx(skb);
#endif	/*	defined(RX_TASKLET)	*/
#ifdef CONFIG_RTL_8198C
	SMP_UNLOCK_ETH(flags);
#endif
}

#if defined(CONFIG_RTK_VLAN_SUPPORT)
#if defined(CONFIG_RTK_BRIDGE_VLAN_SUPPORT) || defined(CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT)
extern int  rx_vlan_process(struct net_device *dev, struct vlan_info *info_ori, struct sk_buff *skb, struct sk_buff **new_skb);
extern int  tx_vlan_process(struct net_device *dev, struct vlan_info *info_ori, struct sk_buff *skb, int wlan_pri);
#else
extern int  rx_vlan_process(struct net_device *dev, struct vlan_info *info, struct sk_buff *skb);
extern int  tx_vlan_process(struct net_device *dev, struct vlan_info *info, struct sk_buff *skb, int wlan_pri);
#endif
#endif

#ifdef CONFIG_RTK_SUPPORT_DHCP_PORT_IP_BIND
#define SUPPORT_DHCP_PORT_IP_BIND
#endif

#ifdef SUPPORT_DHCP_PORT_IP_BIND
	typedef struct dhcp_port_mac
	{
		uint8 client_mac[6];
		uint16 port_id;
	}DHCP_PORT_MAC;

	DHCP_PORT_MAC port_mac_table[RTL8651_PHY_NUMBER];
#endif

#if defined (CONFIG_RTL_EXT_PORT_SUPPORT)
extern struct net_device * rtl_get_wlan_dev_by_mac(unsigned char *da);
int rtl_checkDstToExtPort(rtl_nicRx_info *info)
{

	if(info->vid==PKTHDR_EXTPORT_MAGIC)
	{
		return SUCCESS;
	}

	return FAILED;
}

int rtl_checkSrcFromExtPort(rtl_nicRx_info *info)
{

	if(info->vid==PKTHDR_EXTPORT_MAGIC4)
	{
		return SUCCESS;
	}

	return FAILED;
}
static inline int32 rtl_fwdToExtPort(struct sk_buff *skb, struct dev_priv *cp_this, rtl_nicRx_info *info)
{
	struct net_device *dev=NULL;
	int ret=FAILED;

	if(rtl_checkDstToExtPort(info)!=SUCCESS){
		return FAILED;
	}

	dev = rtl_get_wlan_dev_by_mac(skb->data);
	if(dev != NULL)
	{
		#if 0
		if(net_ratelimit()){
			printk("dev:%s,%x,%x,[%s]:[%d].\n",dev->name,info->vid,info->pid,__FUNCTION__,__LINE__);
			memDump((void*)skb->data, 64, "RX");
		}
		#endif
		#if defined(CONFIG_RTL_HW_QOS_SUPPORT) && defined(CONFIG_RTL_HARDWARE_NAT)
		#endif
		skb->dev = dev;
		#if defined(CONFIG_COMPAT_NET_DEV_OPS)
		ret=dev->hard_start_xmit(skb, dev);
		#else
		ret=dev->netdev_ops->ndo_start_xmit(skb,dev);
		#endif

		return SUCCESS;
	}
	else
	{
		#if 0
		printk("[%s]:[%d].\n",__FUNCTION__,__LINE__);
		memDump((void*)skb->data, 64, "TX");
		#endif
		panic_printk("eth ext Tx:can not find dst  ext  port device!\n");
		dev_kfree_skb_any(skb);
		return SUCCESS;
	}
	return FAILED;
}

static int32 rtl_fwdFromExtPort(struct sk_buff *skb, struct dev_priv *cp_this, rtl_nicRx_info *info)
{
	int ret=FAILED;
	struct net_device *ori_wlan_dev = NULL;
	ret=rtl_checkSrcFromExtPort(info);
	if(ret==SUCCESS)
	{
		if(1)//(info->pid==PKTHDR_EXTPORT_P3)
		{
			/*just use PKTHDR_EXTPORT_P3<->wlan*/
			ori_wlan_dev = rtl_get_wlan_dev_by_mac(skb->data+ETH_ALEN);
			if(ori_wlan_dev)
			{
				skb->dev=ori_wlan_dev;
				/*to-do, for vlan consideration, may add back vlan tag*/
				rtl_processRxToProtcolStack(skb, cp_this);
				return SUCCESS;
			}
			else
			{
				panic_printk("eth ext Rx:can not find src ext port device!\n");
				dev_kfree_skb_any(skb);
				return SUCCESS;
			}
		}
	}
	return FAILED;
}
#endif



__MIPS16
__IRAM_FWD
static inline void rtl_processRxFrame(rtl_nicRx_info *info)
{
	struct dev_priv	*cp_this;
	struct sk_buff 	*skb;
	uint32			vid, pid, len;
	uint8			*data;
	#if defined(CONFIG_RTL_ETH_802DOT1X_SUPPORT) || defined(CONFIG_RTL_VLAN_8021Q)
	int ret;
	#endif

#if defined (CONFIG_RTL_IGMP_SNOOPING)||defined(CONFIG_BRIDGE_IGMP_SNOOPING)
	int mCastFlag=0;
#endif
	cp_this = info->priv;
	skb = info->input;
	vid = info->vid;
	#if defined(CONFIG_RTD_1295_HWNAT)
	data = skb->data;
	skb_reset_tail_pointer(skb);
	#else //defined(CONFIG_RTD_1295_HWNAT)
	data = skb->tail = skb->data;
	#endif //defined(CONFIG_RTD_1295_HWNAT)

	#if 0
	if(net_ratelimit())
		printk("[%s:%d]vid=%d\n",__FUNCTION__,__LINE__,vid);
	#endif
#if 0//defined(CONFIG_RTL_STP)
	if(info->isStpVirtualDev){
		pid = info->pid;
		len = info->len;
		skb->len = 0;
		skb_put(skb, len);
		skb->dev=info->priv->dev;
		goto RxToPs;
	}
#endif
#if 0
	/*	sanity check	*/
	if  ((memcmp(&data[ETH_ALEN], cp_this->dev->dev_addr, ETH_ALEN)==0)||PKTHDR_EXTPORT_MAGIC2==vid||PKTHDR_EXTPORT_MAGIC==vid)// check source mac
	{
		#if	defined(CONFIG_RTL_HARDWARE_NAT)&&(defined(CONFIG_RTL8192SE)||defined(CONFIG_RTL8192CD))
				#ifdef CONFIG_RTK_VLAN_WAN_TAG_SUPPORT
		if ((PKTHDR_EXTPORT_MAGIC!=vid)||(info->pid!=PKTHDR_EXTPORT_P3 &&info->pid!=PKTHDR_EXTPORT_P2))
				 #else
		if ((PKTHDR_EXTPORT_MAGIC!=vid)||(info->pid!=PKTHDR_EXTPORT_P3))
		#endif
		#endif
		{
			cp_this->net_stats.rx_dropped++;
			dev_kfree_skb_any(skb);
			return;
		}
	}
#endif
	#if defined(CONFIG_RTD_1295_HWNAT)
	if (skb->head==NULL||skb->end==0)
	#else //defined(CONFIG_RTD_1295_HWNAT)
	if (skb->head==NULL||skb->end==NULL)
	#endif //defined(CONFIG_RTD_1295_HWNAT)
	{
		dev_kfree_skb_any(skb);
		return;
	}
	/*	sanity check end 	*/

	pid = info->pid;
	len = info->len;
	skb->len = 0;
	skb_put(skb, len);
#if defined (CONFIG_RTL_EXT_PORT_SUPPORT)
	if(extPortEnabled&&((skb->data[0]&0x1)==0))
	{
		if(rtl_fwdToExtPort(skb,cp_this,info)==SUCCESS)
		{
			return;
		}
		else if(rtl_fwdFromExtPort(skb,cp_this,info)==SUCCESS)
		{
			return;
		}
	}

#endif
	skb->dev=info->isPdev?_rtl86xx_dev.pdev:info->priv->dev;
	//skb->dev=cp_this->dev;

	//if((1 << pid) & RTL_WANPORT_MASK)
		//if(skb->data[0]&0x01 && skb->data[0]!=0xff)
			//panic_printk("WAN recv mc pkt!\n");

#if defined(CONFIG_RTL_ISP_MULTI_WAN_SUPPORT)
	skb->from_dev = skb->dev;
#endif

#ifdef CONFIG_RTK_VOIP_ETHERNET_DSP
	//merged from r8627 may conflict later
	//printk("type: %x, skb->mac=%p, skb->data=%p\n", skb->mac.ethernet->h_proto, skb->mac.ethernet, skb->data);
	//extern void voip_dsp_L2_pkt_rx(unsigned char* eth_pkt);
	if (*(uint16*)(&(skb->data[12])) == htons(0x8899) && voip_dsp_L2_pkt_rx_trap )
	{
		//dsp_id = *(uint16*)(&(skb->data[14]);
		voip_dsp_L2_pkt_rx_trap(skb->data, skb->len);
		dev_kfree_skb_any(skb);
		//printk("0x%x ", *(uint16*)(&(skb->data[12])));
		return;
	}
#endif


#if defined(CONFIG_NETFILTER_XT_MATCH_PHYPORT) || defined(CONFIG_RTL_FAST_FILTER) || defined(CONFIG_RTL_QOS_PATCH) || defined(CONFIG_RTK_VOIP_QOS) || defined(CONFIG_RTK_VLAN_WAN_TAG_SUPPORT) ||defined(CONFIG_RTL_MAC_FILTER_CARE_INPORT) ||defined(CONFIG_RTL_HW_QOS_SUPPORT_WLAN)
	skb->srcPhyPort=(uint8)pid;
#endif

	//printk("=======%s(%d),skb->dev:%s,cp_this(%s),vlan:%d,is_lan:%d,id(%d)\n",__FUNCTION__,__LINE__,skb->dev->name,cp_this->dev->name, cp_this->vlan_setting.global_vlan,cp_this->vlan_setting.is_lan,cp_this->vlan_setting.id);
	/*	vlan process (including strip vlan tag)	*/
	#if defined(CONFIG_RTK_VLAN_SUPPORT)
#ifdef CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT
	if (cp_this->vlan_setting.global_vlan)
#else
	if (rtk_vlan_support_enable && cp_this->vlan_setting.global_vlan)
#endif
	{
	#if defined(CONFIG_RTK_BRIDGE_VLAN_SUPPORT) || defined(CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT)
		struct sk_buff *new_skb = NULL;
		int vlan_check;
		vlan_check = rx_vlan_process(cp_this->dev, &cp_this->vlan_setting, skb, &new_skb);
		if((vlan_check==0) && new_skb){
			rtl_processRxToProtcolStack(new_skb, cp_this);
		}else if(vlan_check == 1){
			cp_this->net_stats.rx_dropped++;
			dev_kfree_skb_any(skb);
			return;
		}else if((vlan_check == 2) && new_skb){
			dev_kfree_skb_any(new_skb);
		}
	#else
		if (rx_vlan_process(cp_this->dev, &cp_this->vlan_setting, skb))
		{
			cp_this->net_stats.rx_dropped++;
			dev_kfree_skb_any(skb);
			return;
		}
	#endif

		#if defined(CONFIG_RTK_VLAN_FOR_CABLE_MODEM)
		if(rtk_vlan_support_enable == 2)
		{
			DEBUG_ERR("====%s(%d),cp(%s),cp->vlan.id(%d),skb->tag.vid(%d)\n",__FUNCTION__,__LINE__,
				cp_this->dev->name,cp_this->vlan_setting.id,skb->tag.f.pci&0xfff);
			if(cp_this->vlan_setting.vlan && skb->tag.f.tpid == htons(ETH_P_8021Q) && (skb->tag.f.pci & 0xfff) != cp_this->vlan_setting.id)
			{
				if(cp_this->vlan_setting.is_lan == 0)
				{
					struct net_device* vap;
					/*	look up vap	*/
					vap = get_dev_by_vid(skb->tag.f.pci & 0xfff);
					if(vap)
					{

						skb->dev = vap;
						vap->netdev_ops->ndo_start_xmit(skb,vap);
						return;
					}
				}
			}
		}
		#endif
	}
	#endif	/*	defined(CONFIG_RTK_VLAN_SUPPORT)	*/

	// add for 802.1p qos
	if (*((uint16*)(skb->data+(ETH_ALEN<<1))) == __constant_htons(ETH_P_8021Q)){
		vid = ntohs(*(uint16*)(skb->data+(ETH_ALEN<<1)+2)) & 0x0fff;
		#if	defined(CONFIG_RTL_QOS_8021P_SUPPORT)
		skb->srcVlanPriority = ntohs(*(uint16*)(data+(ETH_ALEN<<1)+2))>>13;
		#endif

		#if defined(CONFIG_RTL_VLANPRI_IPTABLE_CHECK)
		skb->original_vlanpri = ntohs(*(uint16*)(data+(ETH_ALEN<<1)+2))>>13;
		#endif
	}

#ifdef CONFIG_RTL_VLAN_8021Q
	if(linux_vlan_enable && linux_vlan_hw_support_enable)
	{
		ret = rtk_process_11q_rx(pid,skb);
		if(ret == 1)
		{
			data = skb->data;
			goto reserve_vlan_tag;
		}
		else if(ret == 2)
		{
			cp_this->net_stats.rx_dropped++;
			dev_kfree_skb_any(skb);
			return;
		}
	}

	if(linux_vlan_enable && (!linux_vlan_hw_support_enable)){
		/*add vlan tag if pkt is untagged*/
		if(*((uint16*)(skb->data+(ETH_ALEN<<1))) != __constant_htons(ETH_P_8021Q)){
			#if 0
			if((info->srcvid!=RTL_LANVLANID) && (info->srcvid!=RTL_WANVLANID))
			#endif
			if((vlan_ctl_p->pvid[info->pid]!=RTL_LANVLANID) && (vlan_ctl_p->pvid[info->pid]!=RTL_WANVLANID))
			{
				memmove(skb->data-VLAN_HLEN, skb->data, ETH_ALEN<<1);
				skb_push(skb,VLAN_HLEN);
				*((uint16*)(skb->data+(ETH_ALEN<<1))) = __constant_htons(ETH_P_8021Q);
				#if 0
				if(info->srcvid){
					*((uint16*)(skb->data+(ETH_ALEN<<1)+2)) = info->srcvid;
				}
				else{
					*((uint16*)(skb->data+(ETH_ALEN<<1)+2)) = vlan_ctl_p->pvid[info->pid];
				}
				#else
				*((uint16*)(skb->data+(ETH_ALEN<<1)+2)) = __constant_htons(vlan_ctl_p->pvid[info->pid]);
				#endif
				data = skb->data;
			}
		}
	}
	else
#endif

	if (*((uint16*)(skb->data+(ETH_ALEN<<1))) == __constant_htons(ETH_P_8021Q)) {
		#if defined(CONFIG_RTL_VLAN_PASSTHROUGH_SUPPORT)
		memcpy(skb->vlan_passthrough_saved_tag, skb->data+ETH_ALEN*2, 4);
		#endif
		#if defined(CONFIG_RTL_HW_VLAN_SUPPORT)
		if((skb->data[0]&1)||((skb->data[0]==0x33)&&(skb->data[1]==0x33)&&(skb->data[2]!=0xFF)))
		{
			memcpy(&skb->tag, skb->data+ETH_ALEN*2, sizeof(struct vlan_tag));
		}
		#endif
		vid = ntohs(*(uint16*)(data+(ETH_ALEN<<1)+2)) & 0x0fff;
		#if 0
		#if	defined(CONFIG_RTL_QOS_8021P_SUPPORT)
		//skb->srcVlanPriority = (vid>>13)&0x7;
		skb->srcVlanPriority = ntohs(*(uint16*)(data+(ETH_ALEN<<1)+2))>>13;
		#endif
		#if defined(CONFIG_RTL_VLANPRI_IPTABLE_CHECK)
		//skb->original_vlanpri = (vid>>13)&0x7;
		skb->original_vlanpri = ntohs(*(uint16*)(data+(ETH_ALEN<<1)+2))>>13;
		#endif
		if(net_ratelimit()){
			printk("[%s:%d] skb->srcVlanPriority: %d\n", __FUNCTION__, __LINE__, skb->srcVlanPriority);
			printk("[%s:%d] info->rxPri: %d\n", __FUNCTION__, __LINE__, info->rxPri);
		}
		#endif
		//vid &= 0x0fff;

	#ifdef CONFIG_RTK_VLAN_WAN_TAG_SUPPORT
		//for later arp reply to WAN port
		//memcpy(&(skb->tag), skb->data+ETH_ALEN*2, VLAN_HLEN);

		if(vlan_tag && vid == vlan_tag)
		{
			goto Remove_tag;
		}
		else if(vlan_bridge_tag && vid == vlan_bridge_tag)
		{
			if(vlan_bridge_multicast_tag)
			{
				if((skb->data[0]==0x01) && (skb->data[1]==0x00) && (skb->data[2]==0x5e))
				{
					cp_this->net_stats.rx_dropped++;
					dev_kfree_skb_any(skb);
					return;
				}
			}
			goto Remove_tag;
		}
		else if(vlan_bridge_multicast_tag && vid == vlan_bridge_multicast_tag)
		{
			if((skb->data[0]==0x01) && (skb->data[1]==0x00) && (skb->data[2]==0x5e))
			{
				info->vid = vlan_bridge_tag;//for later check ???
				goto Remove_tag;
			}
		}
		else
		{
		#ifndef CONFIG_VLAN_8021Q
			cp_this->net_stats.rx_dropped++;
			dev_kfree_skb_any(skb);
			return;
		#endif
		}
	#ifdef CONFIG_VLAN_8021Q
		goto Reserve_tag;
	#endif
Remove_tag:
	#endif
#if !defined(CONFIG_RTL_ISP_MULTI_WAN_SUPPORT)
		memmove(data + VLAN_HLEN, data, VLAN_ETH_ALEN<<1);
		skb_pull(skb, VLAN_HLEN);
#endif
#if defined(CONFIG_VLAN_8021Q) && defined(CONFIG_RTK_VLAN_WAN_TAG_SUPPORT)
Reserve_tag:
	;//don't remove tag
#endif
	}
	/*	vlan process end (vlan tag has already stripped)	*/
#ifdef CONFIG_RTL_VLAN_8021Q
		reserve_vlan_tag:
#endif

#if defined(CONFIG_RTL_CUSTOM_PASSTHRU)&&defined(CONFIG_IPV6)
	if (info->oriPriv) {
		struct sk_buff *newSkb;
		newSkb = skb_clone(skb, GFP_ATOMIC);

		/*Send this newSkb to IPv6 protocol stack*/
		if (newSkb) {
			newSkb->dev = info->oriPriv->dev;
			rtl_processRxToProtcolStack(newSkb, info->oriPriv);
		}
	}
#endif

	/*	update statistics	*/
	#if !defined(CONFIG_RTL_NIC_HWSTATS)
	cp_this->net_stats.rx_packets++;
	cp_this->net_stats.rx_bytes += skb->len;
	#endif
	cp_this->dev->last_rx = jiffies;
	/*	update statistics end	*/

#ifdef SUPPORT_DHCP_PORT_IP_BIND
	if(isDHCPpkt(skb))
	{
		uint16 port_id;
		uint8 i=0;
		uint8 client_mac[ETH_ALEN];
		port_id=info->pid;
		memcpy(client_mac, data+ETH_ALEN, ETH_ALEN);
		if(port_id<RTL8651_PHY_NUMBER)
		{
			port_mac_table[port_id].port_id=port_id+1;
			memcpy(port_mac_table[port_id].client_mac, client_mac, ETH_ALEN);
					 for(i=0; i<RTL8651_PHY_NUMBER;i++)
					 {
				if((i!=port_id) && (memcmp(port_mac_table[i].client_mac, client_mac, ETH_ALEN)==0))
					memset(port_mac_table[i].client_mac, 0, ETH_ALEN);
					 }
		}
		else
			panic_printk("%s:%d port_id=%d\n",__FUNCTION__,__LINE__,port_id);
	}
#endif
#ifdef CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT
	if(rtl_hw_vlan_enable)
	{
		//the cp_this ,  vid  is possbile chaged in rx_vlan_process due to wan have multiple vid.
		// map updated vid to real cp_this here
		uint16 newvid=0;
		newvid = get_skb_vid(skb);
		//bypass some case that no need to update cp
		if(cp_this->id == RTL_LANVLANID ) //bypass nat LAN group , nerver change
			goto updated_end;
		if((newvid == 0)) //from vlan disable (LAN)
			goto updated_end;

//		if(newvid == wan_management_vlan.id )  // management always from wan cp , bypass
//			goto updated_end;   //mark_manv

		if(newvid == cp_this->id ) //no need change
			goto updated_end;

		// now , find the real mapping cp_this
		cp_this = (struct dev_priv *)rtl_get_cp_by_vid(newvid);

		// update other info , and skb->dev
		skb->dev = cp_this->dev;
			info->priv = cp_this;
			info->vid = cp_this->id;
			vid = info->vid;
			cp_this->dev->last_rx = jiffies; //last_rx at new cp
		}
updated_end:
#endif

	#if defined(CONFIG_RTL_ETH_802DOT1X_SUPPORT)
	//if (dot1x_config.enable)
	if ((dot1x_config.enable)&&(rtl_is802dot1xFrame(skb->data)))
	{
		ret = rtl_802dot1xRxHandle(skb,info);
		//panic_printk("%s %d ret=%d\n", __FUNCTION__, __LINE__, ret);
		if (ret == DOT1X_CONSUME_PKT || ret == DOT1X_DROP_PKT)
		{
			if (ret == DOT1X_DROP_PKT)
				cp_this->net_stats.rx_dropped++;
			return ;
		}
	}
	#endif

	if (0==(data[0]&0x01))		/*	unicast pkt only	*/
	{
		#if defined(CONFIG_RTL_QOS_VLANID_SUPPORT)
		#if defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)
		if ((info->vid==0) && (rtl819x_getSwEthPvid(pid, &(info->vid))==SUCCESS))
			vid = info->vid;
		#endif
		skb->srcVlanId = vid;
		#endif

		#if !defined(CONFIG_RTL_BRSHORTCUT_LINUX_VLAN_CTL)
		#ifdef CONFIG_RTL_VLAN_8021Q
		if(!linux_vlan_enable){
		#endif
		#endif
		/*	shortcut process	*/
		#if defined(BR_SHORTCUT)
		if (SUCCESS==rtl_processBridgeShortCut(skb,cp_this, info)) {
			return;
		}
		#elif defined(CONFIG_RTL_HARDWARE_NAT)&&(defined(CONFIG_RTL8192SE)||defined(CONFIG_RTL8192CD))
		/*
		if (SUCCESS==rtl_processExtensionPortShortCut(skb,cp_this, info)) {
			return;
		}
		*/
		#endif

		#if defined(CONFIG_RTL_FASTBRIDGE)
		if (RTL_FB_RETURN_SUCCESS==rtl_fb_process_in_nic(skb, cp_this->dev))
			return;
		#endif
		/*	shortcut process end	*/

		/*	unknow unicast control	*/
		#if defined (CONFIG_RTL_UNKOWN_UNICAST_CONTROL)
		rtl_unkownUnicastUpdate(skb->data);
		#endif
		#if !defined(CONFIG_RTL_BRSHORTCUT_LINUX_VLAN_CTL)
		#ifdef CONFIG_RTL_VLAN_8021Q
		}
		#endif
		#endif
		/*	unknow unicast control end	*/
	}

	#if defined (CONFIG_RTL_IGMP_SNOOPING)||defined(CONFIG_BRIDGE_IGMP_SNOOPING)
	if((mCastFlag=rtl_isMcastPkt(skb))!=0)	/*	multicast		*/

	{
		/*	multicast process	*/
		#if defined (CONFIG_RTL_IGMP_SNOOPING)||defined(CONFIG_BRIDGE_IGMP_SNOOPING)
		//rtl_MulticastRxCheck(skb, cp_this, vid, pid);
		rtl_MulticastRxCheck(skb, info,mCastFlag);
		#endif	/*end of CONFIG_RTL865X_IGMP_SNOOPING*/
		/*	multicast process end	*/
	}
	#endif

#if 0//defined(CONFIG_RTL_STP)
RxToPs:
#endif
	/*	finally successfuly rx to protocol stack	*/
	rtl_processRxToProtcolStack(skb, cp_this);
}

#if defined (CONFIG_RTK_VOIP_QOS)
int ( *check_voip_channel_loading )( void );
#endif

__DRAM_FWD rtlInterruptRxData	RxIntData;

#if defined (CONFIG_RTK_VOIP_QOS) && !defined (CONFIG_RTK_VOIP_ETHERNET_DSP_IS_HOST)
__IRAM_FWD
static inline int32 interrupt_dsr_rx_per_packe_check(rtlInterruptRxData *rxData, unsigned long task_priv)
{
	if (( (rxData->voip_rx_cnt++ > 100) || ((jiffies - rxData->voip_rx_start_time) >= 1 ))&& (check_voip_channel_loading && (check_voip_channel_loading() > 0)))
	{
	#ifdef RX_TASKLET
		// avoid cp  change!
		tasklet_schedule(&(( struct dev_priv *)task_priv)->rx_dsr_tasklet);
	#endif
		return RTL_RX_PROCESS_RETURN_BREAK;
	}
	return RTL_RX_PROCESS_RETURN_SUCCESS;
}
#endif

#if defined (CONFIG_RTK_VOIP_QOS) && !defined (CONFIG_RTK_VOIP_ETHERNET_DSP_IS_HOST)
static inline int32 interrupt_dsr_rx_check(rtlInterruptRxData *rxData)
{
	rxData->voip_rx_start_time = jiffies;
	rxData->voip_rx_cnt = 0;
	return RTL_RX_PROCESS_RETURN_SUCCESS;
}
#endif

__IRAM_FWD static void interrupt_dsr_rx_done(rtlInterruptRxData *rxData)
{
#ifdef RX_TASKLET
	unsigned long flags=0;

	SMP_LOCK_ETH(flags);
	rtl_rxSetTxDone(TRUE);
		REG32(CPUIIMR) |= (RX_DONE_IE_ALL | PKTHDR_DESC_RUNOUT_IE_ALL);
	rtl_rx_tasklet_running = 0;
	SMP_UNLOCK_ETH(flags);
#endif
}


__MIPS16
__IRAM_FWD
#if defined(CONFIG_RTL_ETH_NAPI_SUPPORT)
static int32 interrupt_dsr_rx(struct dev_priv* task_priv, uint32 budget)
#else
static int32 interrupt_dsr_rx(unsigned long task_priv)
#endif
{
	static __DRAM_FWD rtl_nicRx_info	info;
	int	ret, count, rx_ok=0;
	#if defined(CONFIG_RTL_ETH_NAPI_SUPPORT)
	uint32 rx_left, rx_count;
	#endif
	#if defined(CONFIG_SMP)
	unsigned long flags = 0;
	SMP_LOCK_ETH_RECV(flags);
	#endif

	//int MAX_RX_NUM = 160;

	//RTL_swNic_txDone(0);
	//DBG("budget = %d, priority = %d\n", budget, RTL_ASSIGN_RX_PRIORITY);

	#if defined (CONFIG_RTK_VOIP_QOS) && !defined (CONFIG_RTK_VOIP_ETHERNET_DSP_IS_HOST)
	interrupt_dsr_rx_check(&RxIntData);
	#endif
	rx_ok=0;
	#if defined(CONFIG_RTL_ETH_NAPI_SUPPORT)
	for (rx_left=budget; rx_left>0; rx_left--)
	#else
	while (1)
	#endif
	{
		#if !defined(CONFIG_RTL_ETH_NAPI_SUPPORT)
		if (rx_ok > (NUM_RX_PKTHDR_DESC))
			break;
		#endif

		#if defined (CONFIG_RTK_VOIP_QOS) && !defined (CONFIG_RTK_VOIP_ETHERNET_DSP_IS_HOST)
		if (RTL_RX_PROCESS_RETURN_BREAK==interrupt_dsr_rx_per_packe_check(&RxIntData, task_priv))
			break;
		#endif

		#if defined(RTL_MULTIPLE_RX_TX_RING)
		info.priority = RTL_ASSIGN_RX_PRIORITY;
		#endif

		count = 0;
		do {
			ret = RTL_swNic_receive(&info, count++);
		} while (ret==RTL_NICRX_REPEAT);
		rx_ok++;

		switch(rtl_processReceivedInfo(&info,  ret)) {
			case RTL_RX_PROCESS_RETURN_SUCCESS:
				if (SUCCESS==rtl_decideRxDevice(&info)) {
					rtl_processRxFrame(&info);
				}
				break;
			case RTL_RX_PROCESS_RETURN_BREAK:
				{
					#if defined(CONFIG_RTL_ETH_NAPI_SUPPORT)
					rx_left++;
					#endif
					goto rx_out;
				}
			default:
				break;
		}
	}

rx_out:
//panic_printk("%s:%d rx_ok=%d\n", __FUNCTION__, __LINE__, rx_ok);
	#if defined(CONFIG_RTL_ETH_NAPI_SUPPORT)
	rx_count = budget - rx_left;
	#if defined(CONFIG_SMP)
	SMP_UNLOCK_ETH_RECV(flags);
	#endif
	//DBG("rx_count = %d, rx_ok = %d", rx_count, rx_ok);
	return rx_count;
	#else
	#if defined(CONFIG_SMP)
	SMP_UNLOCK_ETH_RECV(flags);
	#endif
	interrupt_dsr_rx_done(&RxIntData);
	return RTL_NICRX_OK;
	#endif
}


__IRAM_FWD
static void interrupt_dsr_tx(unsigned long task_priv)
{
	int32	idx;
	#ifdef TX_TASKLET
	unsigned long flags=0;
	#endif

	for(idx=RTL865X_SWNIC_TXRING_MAX_PKTDESC-1;idx>=0;idx--)
	{
		RTL_swNic_txDone(idx);
	}

	refill_rx_skb();
	#ifdef TX_TASKLET
	SMP_LOCK_ETH(flags);
	rtl_rxSetTxDone(TRUE);
	rtl_tx_tasklet_running = 0;
	SMP_UNLOCK_ETH(flags);
	#endif
}

#ifdef CONFIG_RTL8196C_ETH_IOT
static int re865x_setPhyGrayCode(void)
{
	uint32 agc, cb0, snr, val;
	uint32 DUT_eee, Linkpartner_eee;
	int i;

	for(i=0; i<5; i++) {

		if ((curLinkPortMask & (1<<i)) != (newLinkPortMask & (1<<i))) { // port i link changed

			if (newLinkPortMask & (1<<i)) {		// link up

				/*=========== ###03 ===========*/
				// read DUT eee 100 ability
				rtl8651_setAsicEthernetPHYReg( i, 13, 0x7 );
				rtl8651_setAsicEthernetPHYReg( i, 14, 0x3c );
				rtl8651_setAsicEthernetPHYReg( i, 13, 0x4007 );
				rtl8651_getAsicEthernetPHYReg( i, 14, &DUT_eee );

				// read Link partner eee 100 ability
				rtl8651_setAsicEthernetPHYReg( i, 13, 0x7 );
				rtl8651_setAsicEthernetPHYReg( i, 14, 0x3d );
				rtl8651_setAsicEthernetPHYReg( i, 13, 0x4007 );
				rtl8651_getAsicEthernetPHYReg( i, 14, &Linkpartner_eee );

				if (  ( ((DUT_eee & 0x2) ) != 0)  && ( ((Linkpartner_eee & 0x2) ) != 0) )  {
					rtl8651_getAsicEthernetPHYReg( i, 21, &snr );
					snr = snr | 0x4000;
					rtl8651_setAsicEthernetPHYReg( i, 21, snr );
				}

				if ( ((Linkpartner_eee & 0x2) ) != 0)
					port_linkpartner_eee |= (1 << i);
				else
					port_linkpartner_eee &= ~(1 << i);

				/*=========== ###02 ===========*/
				/*
				  1.      reg17 = 0x1f10�Aread reg29, for SNR
				  2.      reg17 =  0x1f11�Aread reg29, for AGC
				  3.      reg17 = 0x1f18�Aread reg29, for cb0
				 */
				// 1. for SNR
				snr = 0;
				for(val=0; val<3; val++) {
					rtl8651_getAsicEthernetPHYReg(i, 29, &agc);
					snr += agc;
				}
				snr = snr / 3;

				// 3. for cb0
				rtl8651_getAsicEthernetPHYReg( i, 17, &val );
				val = (val & 0xfff0) | 0x8;
				rtl8651_setAsicEthernetPHYReg( i, 17, val );
				rtl8651_getAsicEthernetPHYReg( i, 29, &cb0 );

				// 2. for AGC
				val = (val & 0xfff0) | 0x1;
				rtl8651_setAsicEthernetPHYReg( i, 17, val );
				rtl8651_getAsicEthernetPHYReg( i, 29, &agc );

				// set bit 3~0 to 0x0 for reg 17
				val = val & 0xfff0;
				rtl8651_setAsicEthernetPHYReg( i, 17, val );

				if ( ( (((agc & 0x70) >> 4) < 4) && ((cb0 & 0x80) != 0) ) || (snr > 4150) ) { // "> 4150" = "< 18dB"
					rtl8651_restartAsicEthernetPHYNway(i);
				}
			}
			else {		// link down

				/*=========== ###01 ===========*/
				extern void set_gray_code_by_port(int);

				/*=========== ###03 ===========*/
				// read DUT eee 100 ability
				rtl8651_setAsicEthernetPHYReg( i, 13, 0x7 );
				rtl8651_setAsicEthernetPHYReg( i, 14, 0x3c );
				rtl8651_setAsicEthernetPHYReg( i, 13, 0x4007 );
				rtl8651_getAsicEthernetPHYReg( i, 14, &DUT_eee );

				// due to the RJ45 cable is plug out now, we can't read the eee 100 ability of link partner.
				// we use the variable port_linkpartner_eee to keep link partner's eee 100 ability after RJ45 cable is plug in.
				if (  ( ((DUT_eee & 0x2) ) == 0)  || ((port_linkpartner_eee & (1<<i)) == 0) )  {
					rtl8651_getAsicEthernetPHYReg( i, 21, &val );
					val = val & ~(0x4000);
					rtl8651_setAsicEthernetPHYReg( i, 21, val );
				}
				set_gray_code_by_port(i);
			}
		}
	}
	return SUCCESS;
}
#endif

#if defined (CONFIG_RTL_PHY_PATCH)
#define SNR_THRESHOLD 1000

//db = -(10 * log10(sum/262144));
//18db:SNR_THRESHOLD=4155
//20db:SNR_THRESHOLD=2621
//21db:SNR_THRESHOLD=2082
//22db:SNR_THRESHOLD=1654
//24.18db:SNR_THRESHOLD=1000
#define MAX_RESTART_NWAY_INTERVAL		(60*HZ)
#define MAX_RESTART_NWAY_CNT		3

struct rtl_nWayCtrl_s
{
	unsigned long restartNWayTime;
	unsigned long restartNWayCnt;
};

struct rtl_nWayCtrl_s re865x_restartNWayCtrl[RTL8651_PHY_NUMBER];

unsigned int re865x_getPhySnr(unsigned int port)
{
	unsigned int sum=0;
	unsigned int j;
	unsigned int regData;

	if(port >= RTL8651_PHY_NUMBER)
	{
		return -1;
	}

	if (REG32(PSRP0 + (port * 4)) & PortStatusLinkUp)
	{
		for (j=0, sum=0;j<10;j++)
		{
			rtl8651_getAsicEthernetPHYReg(port, 29, &regData);
			sum += regData;
			mdelay(10);
		}
		sum /= 10;
		return sum;
	}

	return 0;
}

#if defined(CONFIG_RTL_8196C)
#if 1
int re865x_checkPhySnr(void)
{

	unsigned int port;
	unsigned int snr=0;
	unsigned int  val, cb0, agc;
	for (port=0; port<RTL8651_PHY_NUMBER; port++)
	{
		if((1<<port) & (newLinkPortMask & (~curLinkPortMask)) )
		{
			snr=re865x_getPhySnr(port);

			// 3. for cb0
						rtl8651_getAsicEthernetPHYReg( port, 17, &val );
						val = (val & 0xfff0) | 0x8;
						rtl8651_setAsicEthernetPHYReg( port, 17, val );
						rtl8651_getAsicEthernetPHYReg( port, 29, &cb0 );

			  // 2. for AGC
						val = (val & 0xfff0) | 0x1;
						rtl8651_setAsicEthernetPHYReg( port, 17, val );
						rtl8651_getAsicEthernetPHYReg(port, 29, &agc );
			//printk("snr is %d\n",snr);
			//printk("cb0 is 0x%x,agc is 0x%x\n",cb0,agc);
			//if( ((cb0 & 0x80) != 0)|| (snr>4155))
		  	if ( ( ( ((agc & 0x70) >> 4) < 4 ) && ((cb0 & 0x80) != 0) ) || (snr > 4155) )
			{
				//printk("restart nway\n");
				rtl8651_restartAsicEthernetPHYNway(port);
			}
			val = val & 0xfff0;
			rtl8651_setAsicEthernetPHYReg( port, 17, val );

		}

	}
	return 0;

}
#else
static int re865x_checkPhySnr(void)
{

	unsigned int port;
	unsigned int snr=0;

	for (port=0; port<RTL8651_PHY_NUMBER; port++)
	{
		if((1<<port) & (newLinkPortMask & (~curLinkPortMask)))
		{
			snr=re865x_getPhySnr(port);
			//printk("%s:%d, port is %d, snr is %d\n",__FUNCTION__,__LINE__,port,snr);
			if(snr>SNR_THRESHOLD)
			{
				/*poor snr, we should restart n-way*/
				if(re865x_restartNWayCtrl[port].restartNWayTime==0)
				{
					/*last time snr is good*/
					re865x_restartNWayCtrl[port].restartNWayTime=jiffies;
					re865x_restartNWayCtrl[port].restartNWayCnt=1;

					rtl8651_restartAsicEthernetPHYNway(port);

				}
				else if(time_after(jiffies,re865x_restartNWayCtrl[port].restartNWayTime+MAX_RESTART_NWAY_INTERVAL) )
				{
					/*last time SNR is bad, but interval is long enough, we can take another restart n-way action*/
					re865x_restartNWayCtrl[port].restartNWayTime=jiffies;
					re865x_restartNWayCtrl[port].restartNWayCnt=1;
					rtl8651_restartAsicEthernetPHYNway(port);

				}
				else if (re865x_restartNWayCtrl[port].restartNWayCnt>MAX_RESTART_NWAY_CNT)
				{
					/*within restart n-way interval and exceed max try cnt*/
					/*shouldn't do it any more, otherwise it will cause phy link up/down repeatly*/
					//printk("%s:%d,restartNWayCnt>MAX_RESTART_NWAY_CNT,stop do restart n-way\n",__FUNCTION__,__LINE__);
				}
				else
				{
					//printk("%s:%d,restartNWayCnt is %lu\n",__FUNCTION__,__LINE__,re865x_restartNWayCtrl[port].restartNWayCnt);
					re865x_restartNWayCtrl[port].restartNWayCnt++;
					rtl8651_restartAsicEthernetPHYNway(port);
				}
			}
			else
			{
				re865x_restartNWayCtrl[port].restartNWayTime=0;
				re865x_restartNWayCtrl[port].restartNWayCnt=0;
			}

		}

	}
	return 0;

}
#endif
#endif
#endif

unsigned int rtl865x_getPhysicalPortLinkStatus(void)
{
	unsigned int port_num=0;
	unsigned int linkPortMask=0;
	for(port_num=0;port_num<=RTL8651_PHY_NUMBER;port_num++)
	{
		if((READ_MEM32(PSRP0+(port_num<<2))&PortStatusLinkUp)!=0)
		{
			linkPortMask |= 1<<port_num;
		}

	}
	return linkPortMask;
}

#if defined(CONFIG_RTL_REPORT_LINK_STATUS)
//#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
#ifdef CONFIG_AUTO_DHCP_CHECK
void rtl_signal_linkdetect(void);
static struct sock *nl_eventd_sk = NULL;
struct sock *get_nl_eventd_sk(void);
struct sock *rtk_eventd_netlink_init(void);
int get_nl_eventd_pid(void);
void rtk_eventd_netlink_send(int pid, struct sock *nl_sk, int eventID, char *ifname, char *data, int data_len);


int rtl_event_indicate_api(unsigned int curLinkPortMask, unsigned int newportmask)
{
	//struct sock *nl_eventd_sk = NULL;
	int rtk_eventd_pid = 0;
	char data[32] = {0};
	int data_len = 0;

	int event;
	int portId=-1;
	unsigned int offPortMask=0;
	unsigned int onPortMask=0;
	onPortMask=(newportmask & (~curLinkPortMask));
	offPortMask =((~newportmask) & curLinkPortMask);
	#define WIRE_PLUG_OFF 2
	#define WIRE_PLUG_ON 3
	//panic_printk("%x,%x,%x,%x[%s]:[%d].\n",newportmask,curLinkPortMask,offPortMask,onPortMask,__FUNCTION__,__LINE__);

	/*
	if(!(nl_eventd_sk = get_nl_eventd_sk()))
	{
		nl_eventd_sk = rtk_eventd_netlink_init();
	}
	*/

	rtk_eventd_pid = get_nl_eventd_pid();

	if((rtk_eventd_pid==0)||(nl_eventd_sk==NULL))
	{
		//panic_printk("%s:%d ERROR!###########\n",__FUNCTION__,__LINE__);
		return -1;
	}

	//plug off
	if(offPortMask!=0)
	{
		event=WIRE_PLUG_OFF;
		portId=(ffs(offPortMask) - (1));
		sprintf(data, "%x", portId);
		data_len = strlen(data);
		//panic_printk("OFF:%x,%d,[%s]:[%d].\n",offPortMask,portId,__FUNCTION__,__LINE__);
		if(portId!=-1)
			rtk_eventd_netlink_send(rtk_eventd_pid, nl_eventd_sk, event, NULL, data, data_len);
	}
	portId=-1;
	//plug on
	if (onPortMask!=0)
	{
		event=WIRE_PLUG_ON;
		portId=(ffs(onPortMask) - (1));
		sprintf(data, "%x", portId);
		data_len = strlen(data);

		//panic_printk("On:%x,%d,[%s]:[%d].\n",onPortMask,portId,__FUNCTION__,__LINE__);
		if(portId!=-1)
			rtk_eventd_netlink_send(rtk_eventd_pid, nl_eventd_sk, event, NULL, data, data_len);
	}



	return 0;
}
#endif
unsigned int rtl865x_setLinkEventFlag(unsigned int curLinkPortMask, unsigned int newportmask)
{
	int i;
	struct dev_priv * cp=NULL;
//#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
#ifdef CONFIG_AUTO_DHCP_CHECK
	rtl_event_indicate_api(curLinkPortMask, newportmask);
#endif
	if(rtl865x_curOpMode == GATEWAY_MODE)
	{
		for(i = 0; i < ETH_INTF_NUM; i++)
		{
			cp = ((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[i]));
			#if defined(CONFIG_RTL_ISP_MULTI_WAN_SUPPORT)
			if (_rtl86xx_dev.dev[i] && cp && cp->opened && rtl_isWanDev(cp))
			#else
			if (cp && cp->opened && rtl_isWanDev(cp))
			#endif
			{
				/*#ifdef CONFIG_AUTO_DHCP_CHECK
				if((newportmask & cp->portmask)^((curLinkPortMask & cp->portmask)))
				{
					rtl_signal_linkdetect();
				}
				#endif*/
				if(_rtl86xx_dev.dev[i] && ((cp->portmask & curLinkPortMask)==0) && (cp->portmask & newportmask))
				{
					rtk_linkEvent= 1;
					return SUCCESS;
				}
			}
		}
	}
	/*#ifdef CONFIG_AUTO_DHCP_CHECK
	if(newportmask !=curLinkPortMask)
	{
		rtl_signal_linkdetect();
	}
	if(rtk_linkEvent)
		return SUCCESS;
	else
	#endif*/
	return 0;
}


#endif


#ifdef CONFIG_RTL_819X_SWCORE
int cnt_swcore = 0;
int cnt_swcore_tx = 0;
int cnt_swcore_rx = 0;
int cnt_swcore_link = 0;
int cnt_swcore_err = 0;
#endif


#ifdef CONFIG_RTL_8198_ESD
static uint32 phy_reg30[RTL8651_PHY_NUMBER] = { 0, 0, 0, 0, 0};
static int one_second_counter = 0;
static int first_time_read_reg6 = 1;
static int need_to_check_esd2 = 1;
static int esd3_skip_one = 1;

inline static int diff_more_than_1(uint32 a, uint32 b)
{
	uint32 c;

	if (a==b)
		return 0;
	if (a<b)
		{ c=a; a=b; b=c; }
	if ((a-b) >=2)
		return 1;
	else
		return 0;
}

static int esd_recovery(void)
{
	uint32 val;
	int i;

	for (i=0; i<RTL8651_PHY_NUMBER; i++) {
		/************ Link down  ************/
		if ((REG32(PSRP0 + (i * 4)) & PortStatusLinkUp) == 0) {
			phy_reg30[i] = 0;
		}
		/************ Link up  ************/
		else if (phy_reg30[i] == 0) {
			rtl8651_getAsicEthernetPHYReg( i, 22, &val );
			rtl8651_setAsicEthernetPHYReg( i, 22, ((val & (0xff00)) | 0x17) );
			rtl8651_getAsicEthernetPHYReg( i, 30, &val );
			phy_reg30[i] = BIT(31) | (val & 0xfff);
		}
	}
	return SUCCESS;
}
#endif

#if defined(CONFIG_RTL_819XD) || defined(CONFIG_RTL_8196E)
/*
krammer add this according to " Jim Hsieh/5458 "'s algorithm
Description:
when phy0's link partner link in force mode, we will force to half duplex mode,
in this condition, if there is some traffic in port0's rx, sometimes port0 will die.
so we add this patch, only work when phy0's link state change, if link partner
is link in force mode, we will change our port0 into full duplex.
*/
void rtl819x_port0_force_refined(void){
	unsigned int regData;

	if((curLinkPortMask & 0x01) == (newLinkPortMask & 0x01)){
		//no change -> return
		return;
	}

	//default disable phy i/f(0xbb804104 b0=0)
	REG32(PCRP0) &= ~(EnablePHYIf);

	//if port#0=link up
	if(newLinkPortMask & 0x01){
		//if AN =0(reg6[0])
		rtl8651_getAsicEthernetPHYReg(rtl8651_tblAsicDrvPara.externalPHYId[0], 6, &regData);
		if((regData & 0x01) == 0){
			rtl8651_getAsicEthernetPHYReg(rtl8651_tblAsicDrvPara.externalPHYId[0], 5, &regData);
			if(regData & 0x080){//if spd = 100M(reg5[7])
				//force MAC 100M Full duplex
				//(set 0xbb804104 b25=1 b24=1 b23=0 b20-19=1 b18=1 b17-16=0x3)
				regData = REG32(PCRP0);
				regData |= (EnForceMode | PollLinkStatus | ForceDuplex | PauseFlowControlEtxErx);
				regData &= ~(ForceLink | ForceSpeedMask);
				regData |= (ForceSpeed100M);
				//panic_printk("phy0 link partner is force 100M, we force to 100M Full duplex!regData = 0x%x\n", regData);
				REG32(PCRP0) = regData;
			}
			else if(regData & 0x20){//else spd = 10M(reg5[5])
				//force MAC 10M Full duplex
				//(set 0xbb804104 b25=1 b24=1 b23=0 b20-19=0 b18=1 b17-16=0x3)
				regData = REG32(PCRP0);
				regData |= (EnForceMode | PollLinkStatus | ForceDuplex | PauseFlowControlEtxErx);
				regData &= ~(ForceLink | ForceSpeedMask);
				regData |= (ForceSpeed10M);
				//panic_printk("phy0 link partner is force 10M, we force to 10M Full duplex!regData = 0x%x\n", regData);
				REG32(PCRP0) = regData;
			}
		}
		//enable phy i/f
		//set 0xbb804104 b0=1
		REG32(PCRP0) |= EnablePHYIf;
		//panic_printk("phy0 link up, PCRP0 = 0x%x\n", REG32(PCRP0));
	}
	else{//if port#0=link down
		//recovery MAC AN setting and disable phy i/f
		//(set 0xbb804104 b25=0 b20-18 0x7 b0=0)
		regData = REG32(PCRP0);
		regData &= ~(EnForceMode | EnablePHYIf);
		regData |= (ForceSpeed1000M | ForceDuplex);
		//panic_printk("phy0 link down, we reset phy0!regData = 0x%x\n", regData);
		REG32(PCRP0) = regData;
	}
}

#if defined(CONFIG_RTL_EEE_DISABLED)&&!defined(CONFIG_RTL_8367R_SUPPORT)
#define CONFIG_RTK_CHECK_ETH_PORT_RX_HANG 1

static uint32 AutoDownSpeed_10M[MAX_PORT_NUMBER];
static uint32 DownSpeed_counter[MAX_PORT_NUMBER];
static uint32 ReverSpeed_flag[MAX_PORT_NUMBER];
static uint32 match_count[MAX_PORT_NUMBER];
void Set_GPHYWB(uint32 phyid, uint32 page, uint32 reg, uint32 mask, uint32 val);

#if defined(CONFIG_RTL_8211F_SUPPORT)
#define CHECK_SNR_START_PORT	1
#else
#define CHECK_SNR_START_PORT	0
#endif

#ifdef CONFIG_RTK_CHECK_ETH_PORT_RX_HANG
static uint64 pre_asic_rx_bytes[MAX_PORT_NUMBER];
static uint64 pre_asic_tx_bytes[MAX_PORT_NUMBER];
static uint32 count_no_rx[MAX_PORT_NUMBER];
static uint32 count_tx_increase[MAX_PORT_NUMBER];
static uint32 count_no_tx[MAX_PORT_NUMBER];
int rtl819xD_check_eth_port_rx_hang(uint32 port);
#endif

#ifdef CONFIG_RTL_REPORT_LINK_STATUS
#define _BLOCK_LINK_CHANGE() {block_link_change = RTK_BLOCK_LINK_CHANGE_PERIOD;}
#else
#define _BLOCK_LINK_CHANGE()
#endif

static int rtl819xD_check_phy_cb_snr(void)
{
	int  curr_sts=0;
	uint32  val=0, cb=0, snr=0, cache_lpi=0;
	uint8 cb_low=0, cb_high=0, link_speed_10M, port;
	uint8 cb_flag, snr_flag, phyid;

	for (port=CHECK_SNR_START_PORT; port<RTL8651_PHY_NUMBER; port++)
	{

#ifdef CONFIG_RTK_CHECK_ETH_PORT_RX_HANG
		if (!rtl819xD_check_eth_port_rx_hang(port))
			continue;
#endif

	 	cb_flag = 0;
		snr_flag= 0;

		curr_sts = (REG32(PSRP0 + (port * 4)) & PortStatusLinkUp) >> 4;
		//PortStatusLinkSpeed10M
		if ((REG32(PSRP0 + (port * 4)) & PortStatusLinkSpeed_MASK) == PortStatusLinkSpeed10M)
			link_speed_10M = 1;
		else
			link_speed_10M = 0;

		//link_speed_10M= (REG32(PSRP0 + (port * 4)) & PortStatusLinkSpeed10M);

		if (AutoDownSpeed_10M[port] == 0x12345678) {
		   	DownSpeed_counter[port] = DownSpeed_counter[port]+1;
			if ((!curr_sts) && (ReverSpeed_flag[port]==1)) {
				REG32(PCRP0+(port*4)) |= (NwayAbility1000MF|NwayAbility100MF|NwayAbility100MH);
				DownSpeed_counter[port] = 0;
				AutoDownSpeed_10M[port] = 0;
				ReverSpeed_flag[port] = 0;
				match_count[port] = 0;
				_BLOCK_LINK_CHANGE();
				rtl8651_restartAsicEthernetPHYNway(port);
				//rtlglue_printf(" => check_cb_snr: port= %d, ph1\n", port);
			}
			if ((!curr_sts) && (DownSpeed_counter[port]>5)) {
				REG32(PCRP0+(port*4)) |= (NwayAbility1000MF|NwayAbility100MF|NwayAbility100MH);
				DownSpeed_counter[port] = 0;
				AutoDownSpeed_10M[port] = 0;
				ReverSpeed_flag[port] = 0;
				match_count[port] = 0;
				_BLOCK_LINK_CHANGE();
				rtl8651_restartAsicEthernetPHYNway(port);
				//rtlglue_printf(" => check_cb_snr: port= %d, ph2\n", port);
			} else if (curr_sts && (DownSpeed_counter[port]<5)) {
				//Connect to 10M Successfully
				ReverSpeed_flag[port] = 1;
				//rtlglue_printf(" => check_cb_snr: port= %d, ph3\n", port);
			}
		} else {
			AutoDownSpeed_10M[port] = 0;
			DownSpeed_counter[port] = 0;
			ReverSpeed_flag[port] = 0;
		}

		if ((curr_sts==1) && (!link_speed_10M)) {

			phyid = rtl8651AsicEthernetTable[port].phyId;
			// Read CB (bit[15]&[7])==1
			rtl8651_setAsicEthernetPHYReg( phyid, 25, (0x6964) );
			rtl8651_getAsicEthernetPHYReg( phyid, 26, &val );
			rtl8651_setAsicEthernetPHYReg( phyid, 26, ((val&0xBF00)|0x9E) ); // Close new_SD.
			rtl8651_getAsicEthernetPHYReg( phyid, 17, &val );
			rtl8651_setAsicEthernetPHYReg( phyid, 17, ((val&0xFFF0)|0x8) );  //CB bit
			rtl8651_getAsicEthernetPHYReg( phyid, 29, &cb );

			cb_low = cb&0xff;
			cb_high = (cb&0xff00)>>8;
			if ((((cb&(1<<15))>>15) ||(cb_high<=5)) && (((cb&(1<<7))>>7) ||(cb_low<=5))) {
	 			cb_flag = 1;
				match_count[port]++;
				_BLOCK_LINK_CHANGE();
				rtl8651_restartAsicEthernetPHYNway(port);
				//rtlglue_printf("AN1-->cb\r\n" );
			}
			rtl8651_setAsicEthernetPHYReg( phyid, 25, (0x6964) );
			rtl8651_getAsicEthernetPHYReg( phyid, 26, &val );
			rtl8651_setAsicEthernetPHYReg( phyid, 26, ((val&0xBF00)|0x9E) ); // Close new_SD.
			rtl8651_getAsicEthernetPHYReg( phyid, 17, &val );
			rtl8651_setAsicEthernetPHYReg( phyid, 17, ((val&0xFFF0)) );//SNR bit
			rtl8651_getAsicEthernetPHYReg( phyid, 29, &snr );
			if (snr > 0x1000) {
				if (cb_flag == 0)
				{
					snr_flag = 1;
					match_count[port]++;
					_BLOCK_LINK_CHANGE();
					rtl8651_restartAsicEthernetPHYNway(port);
					//rtlglue_printf("AN2-->snr\r\n" );
				}
			}
			//panic_printk("cb:%x snr:%x\r\n",cb,snr );
			rtl8651_setAsicEthernetPHYReg( phyid, 25, (0x2040) );
			rtl8651_getAsicEthernetPHYReg( phyid, 26, &val );
			rtl8651_setAsicEthernetPHYReg( phyid, 26, ((val&0xBF00)|0x0c) ); // Close new_SD.
			Set_GPHYWB(port, 4, 25, 0xffff-(0xf<<0), 0x3<<0);
			rtl8651_getAsicEthernetPHYReg( phyid, 29, &cache_lpi );

			if (cache_lpi & 0x60) {
				if ((cb_flag==0) && (snr_flag==0))
				{
					match_count[port]++;
					_BLOCK_LINK_CHANGE();
					rtl8651_restartAsicEthernetPHYNway(port);
					//rtlglue_printf("AN3-->cache_lpi\r\n" );
				}
			}

			if (match_count[port] >= 5) {
				REG32(PCRP0+(port*4)) &= ~(NwayAbility1000MF|NwayAbility100MF|NwayAbility100MH);
				_BLOCK_LINK_CHANGE();
#if defined(CONFIG_RTL_LONG_ETH_CABLE_REFINE)
				local_bh_disable();
				fe_ads[port].down_speed_renway = 1;
				fe_ads[port].cb_snr_cache_lpi_down_speed = 1; // for debug only
				local_bh_enable();
#endif
				rtl8651_restartAsicEthernetPHYNway(port);
				AutoDownSpeed_10M[port]=0x12345678;
				DownSpeed_counter[port]=0;
				ReverSpeed_flag[port]=0;
				match_count[port] = 0;
				//rtlglue_printf("Down speed to 10M for port %d\n", port);
			}
		}
	}
	return 0;
}

#ifdef CONFIG_RTK_CHECK_ETH_PORT_RX_HANG
uint64 rtl819xD_get_trx_bytes(uint32 port, int direction)
{
	uint32 addrOffset_fromP0 = port * MIB_ADDROFFSETBYPORT;

	if (port > RTL8651_PORT_NUMBER)
		return 0;

	if (0 == direction)
		return rtl865xC_returnAsicCounter64( OFFSET_IFINOCTETS_P0 + addrOffset_fromP0 );

	if (1 == direction)
		return rtl865xC_returnAsicCounter64( OFFSET_IFOUTOCTETS_P0 + addrOffset_fromP0 );

	return 0;
}

int rtl819xD_check_eth_port_rx_hang(uint32 port)
{
	uint64 asic_rx_bytes;
	uint64 asic_tx_bytes;

	/*If eth port link down. do not check*/
	if ((REG32(PSRP0 + (port * 4)) & PortStatusLinkUp) == 0)
		return 2;

	asic_rx_bytes = rtl819xD_get_trx_bytes(port, 0);
	asic_tx_bytes = rtl819xD_get_trx_bytes(port, 1);

	//diag_printf("rx %llu tx %llu\n",asic_rx_bytes,asic_tx_bytes);
	//diag_printf("pre rx %llu pre tx %llu\n",pre_asic_rx_bytes,pre_asic_tx_bytes);

	if (asic_rx_bytes == pre_asic_rx_bytes[port]) {
		count_no_rx[port]++;
		if (asic_tx_bytes == pre_asic_tx_bytes[port]) {
			count_no_tx[port]++;
		} else {
			count_tx_increase[port]++;
		}
	} else {
		count_no_rx[port] = 0;
		count_tx_increase[port] = 0;
	}

	pre_asic_rx_bytes[port] = asic_rx_bytes;
	pre_asic_tx_bytes[port] = asic_tx_bytes;

	//diag_printf("count_no_rx %d count_tx_increase %d\n",count_no_rx,count_tx_increase);
	/*never see rx in 20 seconds but there is tx exists*/
	//if ((count_no_rx[port]>20) && (count_tx_increase[port]>1)) {
	if (count_no_rx[port] > 20) {
		/*recount*/
		count_no_rx[port] = 0;
		count_tx_increase[port] = 0;
		return 1;
	}

	return 0;
}
#endif

#endif	//end of #if defined(CONFIG_RTL_EEE_DISABLED)&&!defined(CONFIG_RTL_8367R_SUPPORT)

#endif	//end of #if defined(CONFIG_RTL_819XD) || defined(CONFIG_RTL_8196E)

#if defined(CONFIG_RTL_8881A)
#define SNR_DOWN_SPEED_THRESHOLD	0x4E0
#elif defined(CONFIG_RTL_819XD) || defined(CONFIG_RTL_8196E)
#define SNR_DOWN_SPEED_THRESHOLD	0x800
#elif defined(CONFIG_RTL_8197F) || defined(CONFIG_RTL_8196C)
#define SNR_DOWN_SPEED_THRESHOLD	0x660
#endif

#if defined(CONFIG_RTL_LONG_ETH_CABLE_REFINE)
/*
 * this function is for the port can linked with long cable, but link quality is poor.
 * there are 2 results: link down (and link up / link down again and again)
 *     or keep linked but Tx/Rx packet do not work.
 * we can check cb and snr, and then down speed to 10M when it reach the criteria.
 */
static int long_cable_check_with_linked(uint32 port)
{
	uint32 val=0, cb=0, snr=0, snr_tmp=0;
	int i, phyid;
	uint8 cb_low=0;

	if ((REG32(PSRP0 + (port * 4)) & PortStatusLinkSpeed_MASK) == PortStatusLinkSpeed10M)
		return SUCCESS;

	phyid = rtl8651AsicEthernetTable[port].phyId;
	rtl8651_setAsicEthernetPHYReg( phyid, 25, (0x6964) );
	rtl8651_getAsicEthernetPHYReg( phyid, 26, &val );
	rtl8651_setAsicEthernetPHYReg( phyid, 26, ((val&0xBF00) |0x9E) ); // Close new_SD.
	rtl8651_getAsicEthernetPHYReg( phyid, 17, &val );
	rtl8651_setAsicEthernetPHYReg( phyid, 17, ((val&0xFFF0) |0x8) );
	rtl8651_getAsicEthernetPHYReg( phyid, 29, &cb );

	cb_low = cb&0xff;
	if (_debug_flag & _DEBUG_ETH_PHY)
		rtlglue_printf(" ==> port[%d] link up, cb_low= 0x%x\n", port, cb_low);

	if ( !((cb_low & 0x80) && (cb_low<=0xfa)) )
		return SUCCESS;

	for (i=0; i<10; i++)
	{
		rtl8651_setAsicEthernetPHYReg( phyid, 25, (0x6964) );
		rtl8651_getAsicEthernetPHYReg( phyid, 26, &val );
		rtl8651_setAsicEthernetPHYReg( phyid, 26, ((val&0xBF00) |0x9E) ); // Close new_SD.
		rtl8651_getAsicEthernetPHYReg( phyid, 17, &val );
		rtl8651_setAsicEthernetPHYReg( phyid, 17, ((val&0xFFF0)) );
		rtl8651_getAsicEthernetPHYReg( phyid, 29, &snr_tmp );

		snr += snr_tmp;
	}
	snr = snr/10;

	if (_debug_flag & _DEBUG_ETH_PHY)
		rtlglue_printf(" ==> port[%d]: snr= 0x%x\n", port, snr);

	if (snr >= SNR_DOWN_SPEED_THRESHOLD) {
		REG32(PCRP0+(port*4)) &= ~(NwayAbility1000MF|NwayAbility100MF|NwayAbility100MH);
		local_bh_disable();
		fe_ads[port].down_speed_renway = 1;
		fe_ads[port].cb_snr_down_speed = 1; // for debug only
		local_bh_enable();
		rtl8651_restartAsicEthernetPHYNway(port);
		//rtlglue_printf("port %d down speed to 10M by cb and snr!\n", port);
	}
	return SUCCESS;
}

static int long_cable_check(void)
{
	uint32 port;

	/* Check each port. */
	for ( port = ADS_PORT_START; port < RTL8651_PHY_NUMBER; port++ )
	{
		/* Read Port Status Register to know the port is link-up or link-down. */
		if ( ( (REG32(PSRP0 + (port*4))) & PortStatusLinkUp ) == FALSE ) {
			/* Link is down. */
			if ((1<<port) & curLinkPortMask) {
				local_bh_disable();
				if (fe_ads[port].down_speed_renway == 0) {
					//rtlglue_printf("Port[%d] link down, down_speed_renway is %d\n", port, fe_ads[port].down_speed_renway);
					REG32(PCRP0+(port*4)) |= (NwayAbility1000MF|NwayAbility100MF|NwayAbility100MH);
					fe_ads[port].cb_snr_down_speed = 0;
					fe_ads[port].cb_snr_cache_lpi_down_speed = 0;
				} else {
					/* this link down is caused by calling rtl8651_restartAsicEthernetPHYNway() func in long_cable_check_with_linked()
					 *		or by calling rtl8651_restartAsicEthernetPHYNway() func in rtl819xD_check_phy_cb_snr().
					 * do not set 100M Ability in PCR
					 */
					//rtlglue_printf("Port[%d] link down, down_speed_renway is %d\n", port, fe_ads[port].down_speed_renway);
					fe_ads[port].down_speed_renway = 0;
				}
				local_bh_enable();
			}
		} else {
			/* Link is up. */
			if (((1<<port)&curLinkPortMask) == 0)
				long_cable_check_with_linked(port);
		}
	}
	return SUCCESS;
}
#endif

#ifdef CONFIG_RTL_8198C_10M_REFINE
int32 rtl8198c_10M_refine(uint32 old_sts, uint32 new_sts)
{
	int32 i, phyid;
	uint32 data;

	if (old_sts == new_sts)
		return SUCCESS;

	for (i=0; i<5; i++) {
		if ((old_sts & BIT(i)) != (new_sts & BIT(i))) { // port i link status changed

			if(new_sts & BIT(i)) { // link up
				phyid = rtl8651AsicEthernetTable[i].phyId;

				rtl8651_setAsicEthernetPHYReg( phyid, 31, 0xbcc);
				rtl8651_getAsicEthernetPHYReg( phyid, 16, &data);
				rtl8651_setAsicEthernetPHYReg( phyid, 16, (data | BIT(3)));
				rtl8651_getAsicEthernetPHYReg( phyid, 18, &data);
				if ((REG32(PSRP0 + (i*4)) & 0x3) == 0) { // 10M
					rtl8651_setAsicEthernetPHYReg( phyid, 18, ((data & 0x00ff) | 0xcc00));
				}
				else { // 100M or Giga
					rtl8651_setAsicEthernetPHYReg( phyid, 18, (data & 0x00ff));
				}
				rtl8651_setAsicEthernetPHYReg( phyid, 31, 0);
			}
			else { // link down
				phyid = rtl8651AsicEthernetTable[i].phyId;
				rtl8651_setAsicEthernetPHYReg( phyid, 31, 0xbcc);
				rtl8651_getAsicEthernetPHYReg( phyid, 18, &data);
				rtl8651_setAsicEthernetPHYReg( phyid, 18, (data & 0x00ff));
				rtl8651_setAsicEthernetPHYReg( phyid, 31, 0);
			}

		}
	}

	return SUCCESS;
}
#endif

#if 0
void clearL2Table_forLinkDownPort(uint32 old_sts, uint32 new_sts)
{
	int32 i;

	if (old_sts != new_sts)
		for (i=0; i<5; i++) {
			if (((old_sts & BIT(i)) != (new_sts & BIT(i))) &&
				(!(new_sts & BIT(i)))) {
					_rtl865x_ClearFDBEntryByPort(i);
			}
		}
}
#endif

static void interrupt_dsr_link(unsigned long task_priv)
{
#ifdef CONFIG_RTL_8198_ESD
	esd_recovery();
#endif

	newLinkPortMask=rtl865x_getPhysicalPortLinkStatus();

	//clearL2Table_forLinkDownPort(curLinkPortMask, newLinkPortMask);

#ifdef CONFIG_RTL8196C_ETH_IOT
	re865x_setPhyGrayCode();
#endif

#ifdef CONFIG_RTL_8197D_DYN_THR
	rtl819x_setQosThreshold(curLinkPortMask, newLinkPortMask);
#endif

#if defined(CONFIG_RTL_REPORT_LINK_STATUS)
//	rtl865x_setLinkStatusFlag(newLinkPortMask);
	if (block_link_change == 0)
		rtl865x_setLinkEventFlag(curLinkPortMask, newLinkPortMask);
#endif
#if defined(CONFIG_RTL_IGMP_SNOOPING)
		rtl865x_igmpSyncLinkStatus();
#endif

#if defined(CONFIG_RTL_LINKCHG_PROCESS)
		rtl865x_LinkChange_Process();
#endif

#if defined(CONFIG_RTL_LONG_ETH_CABLE_REFINE)
	long_cable_check();
#endif

#if 0 //defined(CONFIG_RTL_8196C) && defined(CONFIG_RTL_PHY_PATCH)
	// re865x_checkPhySnr() function is done in re865x_setPhyGrayCode() funtion.
	re865x_checkPhySnr();
#endif

#if defined(CONFIG_RTL_819XD)
	if (rtl_port0Refined == 1)
	{
		rtl819x_port0_force_refined();
	}
#endif

#ifdef CONFIG_RTL_8198C_10M_REFINE
	rtl8198c_10M_refine(curLinkPortMask, newLinkPortMask);
#endif

	curLinkPortMask=newLinkPortMask;

#ifdef LINK_TASKLET
	REG32(CPUIIMR) |= (LINK_CHANGE_IP);
#endif

	return;
}


__IRAM_GEN static inline void rtl_rx_interrupt_process(unsigned int status, struct dev_priv *cp)
{
	#if defined(CONFIG_RTL_REINIT_SWITCH_CORE)
	if(rtl865x_duringReInitSwtichCore==1) {
		return;
	}
	#endif

#ifdef CONFIG_RTL_8197F
	if (status & (RX_DONE_IP_ALL | PKTHDR_DESC_RUNOUT_IP_ALL))
#else
	//if (status & (RX_DONE_IP_ALL | PKTHDR_DESC_RUNOUT_IP_ALL))
#endif
	{
#if defined(CONFIG_RTL_ETH_NAPI_SUPPORT)
		if ((rtl_rx_tasklet_running==0)&&(cp->napi.poll)) {
			rtl_rx_tasklet_running=1;
			REG32(CPUIIMR) &= ~(RX_DONE_IE_ALL | PKTHDR_DESC_RUNOUT_IE_ALL);
			rtl_rxSetTxDone(FALSE);
			napi_schedule(&cp->napi);
		}
#else
		#if defined(RX_TASKLET)
		if (rtl_rx_tasklet_running==0) {
			rtl_rx_tasklet_running=1;
			REG32(CPUIIMR) &= ~(RX_DONE_IE_ALL | PKTHDR_DESC_RUNOUT_IE_ALL);
			rtl_rxSetTxDone(FALSE);
			tasklet_hi_schedule(&cp->rx_dsr_tasklet);
		}
		#else
		interrupt_dsr_rx((unsigned long)cp);
		#endif
#endif
	}
}

__IRAM_GEN static inline void rtl_tx_interrupt_process(unsigned int status, struct dev_priv *cp)
{
	if (status & TX_ALL_DONE_IP_ALL)
	{
		#if defined(TX_TASKLET)
		if (rtl_tx_tasklet_running==0) {
			rtl_tx_tasklet_running=1;
			rtl_rxSetTxDone(FALSE);
			tasklet_hi_schedule(&cp->tx_dsr_tasklet);
		}
		#else
		interrupt_dsr_tx((unsigned long)cp);
		#endif
	}
}

#if	defined(CONFIG_RTL_IGMP_SNOOPING)||defined(CONFIG_RTL_LINKCHG_PROCESS) || defined (CONFIG_RTL_PHY_PATCH) || defined(CONFIG_RTL_ETH_802DOT1X_SUPPORT)
__IRAM_GEN static inline void rtl_link_change_interrupt_process(unsigned int status, struct dev_priv *cp)
{
	if (status & LINK_CHANGE_IP)
	{
		#if defined(LINK_TASKLET)
	  		REG32(CPUIIMR) &= ~LINK_CHANGE_IP;
			tasklet_schedule(&cp->link_dsr_tasklet);
	  	#else
			interrupt_dsr_link((unsigned long)cp);
		#endif
		#ifdef CONFIG_RTK_VOIP_PORT_LINK
		if(cp->opened)
			rtmsg_ifinfo_voip(RTM_LINKCHANGE, cp->dev, ~0U);
		#endif
	}
}
#endif

__MIPS16
__IRAM_FWD
irqreturn_t interrupt_isr(int irq, void *dev_instance)
{
	struct net_device *dev = dev_instance;
	struct dev_priv *cp;
	unsigned int status;
	cp = NETDRV_PRIV(dev);
	status = REG32(CPUIISR);
	REG32(CPUIISR) = status;
	status &= REG32(CPUIIMR);

#ifdef CONFIG_RTL_819X_SWCORE
	cnt_swcore++;
	if (status & (RX_DONE_IP_ALL))
		cnt_swcore_rx++;
	if (status & TX_ALL_DONE_IP_ALL)
		cnt_swcore_tx++;
	if (status&LINK_CHANGE_IP)
		cnt_swcore_link++;
	if (status&(PKTHDR_DESC_RUNOUT_IP_ALL|MBUF_DESC_RUNOUT_IP_ALL))
		cnt_swcore_err++;
#endif

	rtl_rx_interrupt_process(status, cp);

	rtl_tx_interrupt_process(status, cp);

	#if	defined(CONFIG_RTL_IGMP_SNOOPING)||defined(CONFIG_RTL_LINKCHG_PROCESS) || defined (CONFIG_RTL_PHY_PATCH) || defined(CONFIG_RTL_ETH_802DOT1X_SUPPORT) || defined(CONFIG_RTL_8198C_10M_REFINE)
	if (status & LINK_CHANGE_IP)
		rtl_link_change_interrupt_process(status, cp);
	#endif

	return IRQ_HANDLED;
}

static int rtl865x_init_hw(void)
{
	unsigned int mbufRingSize;
	int	i, ret;

	mbufRingSize = rtl865x_rxSkbPktHdrDescNum;	/* rx ring 0	*/
	for(i=1;i<RTL865X_SWNIC_RXRING_HW_PKTDESC;i++)
	{
		mbufRingSize += rxRingSize[i];
	}

	/* Initialize NIC module */
	ret = RTL_swNic_init(rxRingSize, mbufRingSize, txRingSize, MBUF_LEN);

	if (ret != SUCCESS)
	{
		printk("819x-nic: swNic_init failed!\n");
		return FAILED;
	}

	return SUCCESS;
}

#ifdef CONFIG_RTL_HARDWARE_NAT
static void reset_hw_mib_counter(struct net_device *dev)
{
	int i, port;
	int32 totalVlans=((sizeof(vlanconfig))/(sizeof(struct rtl865x_vlanConfig)))-1;
	uint32 set_value = 0;

#if defined(CONFIG_RTL_8370_SUPPORT)
	extern int rtk_stat_port_reset(int port);
	if (dev->name[3] == '0') {	// eth0
		for(port=0;port<8;port++)
			rtk_stat_port_reset(port);
	}
	else if (dev->name[3] == '1') {	// eth1
		rtk_stat_port_reset(RTL83XX_WAN);
	}
	return;
#endif

	for(i=0;i<totalVlans;i++)
	{
		if (IF_ETHER!=vlanconfig[i].if_type)
		{
			continue;
		}
		if (!memcmp(vlanconfig[i].mac.octet, dev->dev_addr, 6))
		{
			for (port=0; port<RTL8651_AGGREGATOR_NUMBER; port++)
			{
				if (vlanconfig[i].memPort & (1<<port))
				{
					//WRITE_MEM32(MIB_CONTROL, (1<<port*2) | (1<<(port*2+1)));
					set_value |= ((1<<port*2) | (1<<(port*2+1)));
#ifdef CONFIG_RTL_8367R_SUPPORT
					{
					extern int rtk_stat_port_reset(int port);
					rtk_stat_port_reset(port);
					}
#endif
				}
				WRITE_MEM32(MIB_CONTROL, set_value);
			}
			return;
		}
	}
}
#endif
#if defined (CONFIG_RTL_8197D)
//extern void rtl819x_poll_sw(void);
#endif
#ifdef CONFIG_RTL8196C_GREEN_ETHERNET
int total_time_for_5_port = 15*HZ;
int port_pwr_save_low = 0;
#endif

#ifdef CONFIG_RTL_8196E
void	refine_phy_setting(void)
{
	int i, start_port = 0;
	uint32 val;

	val = (REG32(BOND_OPTION) & BOND_ID_MASK);
	if ((val == BOND_8196ES) || (val == BOND_8196ES1) || (val == BOND_8196ES2) || (val == BOND_8196ES3))
		return;

	if ((val == BOND_8196EU) || (val == BOND_8196EU1) || (val == BOND_8196EU2) || (val == BOND_8196EU3))
		start_port = 4;

	for (i=start_port; i<5; i++) {
		rtl8651_setAsicEthernetPHYReg( i, 25, 0x6964);
		rtl8651_getAsicEthernetPHYReg(i, 26, &val);
		rtl8651_setAsicEthernetPHYReg(i, 26, ((val & (0xff00)) | 0x9E) );

		rtl8651_getAsicEthernetPHYReg(i, 17, &val);
		rtl8651_setAsicEthernetPHYReg( i, 17, ((val & (0xfff0)) | 0x8)  );

		rtl8651_getAsicEthernetPHYReg( i, 29, &val );
		if ((val & 0x8080) == 0x8080) {
			rtl8651_getAsicEthernetPHYReg( i, 21, &val );
			rtl8651_setAsicEthernetPHYReg( i, 21, (val  | 0x8000) );
			rtl8651_setAsicEthernetPHYReg( i, 21, (val & ~0x8000) );
		}
	}
	return;
}
#endif

#if defined(CONFIG_RTL_IVL_SUPPORT)
uint32 port_link_sts2 = 0;

int flush_l2(void)
{
	int port;
	uint32 cur_link_sts=0;

	for(port=0;port<5;port++)
	{
#if defined(CONFIG_RTL_8367R_SUPPORT)
		uint32 phyData;
		extern int rtl8367b_getAsicPHYReg(unsigned int phyNo, unsigned int phyAddr, unsigned int* pRegData );
		/*Get PHY status register*/
		if (0 != rtl8367b_getAsicPHYReg(port, 1 /* PHY_STATUS_REG */, &phyData))
			return (-1);

		/*check link status*/
		if (phyData & (1<<2))
			cur_link_sts |= (1<<port);
#else
		if ((REG32(PSRP0 + (port * 4)) & PortStatusLinkUp) != 0) {
			cur_link_sts |= (1<<port);
		}

#endif
	}

	if (port_link_sts2 != cur_link_sts)
	{
		// clear wan port entries in SoC L2 table
#if defined(CONFIG_RTL_8367R_SUPPORT)
		if ((cur_link_sts & (1 << RTL83XX_WAN)) && (cur_link_sts & (0x1f & ~(1 << RTL83XX_WAN)))) // wan is up and "one of LANs" is link up
		{
			extern int32 _rtl865x_ClearFDBEntryByPort(int32 port_num);
			_rtl865x_ClearFDBEntryByPort(RTL83XX_WAN);
		}
#else
		#if defined(CONFIG_RTD_1295_HWNAT)
		if ((cur_link_sts & (vlanconfig[RTL_WAN_IDX].memPort)) && (cur_link_sts & (0x1f & ~(vlanconfig[RTL_WAN_IDX].memPort)))) // wan is up and "one of LANs" is link up
		#else //defined(CONFIG_RTD_1295_HWNAT)
		if ((cur_link_sts & (vlanconfig[1].memPort)) && (cur_link_sts & (0x1f & ~(vlanconfig[1].memPort)))) // wan is up and "one of LANs" is link up
		#endif //defined(CONFIG_RTD_1295_HWNAT)
		{
			extern int32 _rtl865x_ClearFDBEntryByPort(int32 port_num);
			// memPort is port mask, draw out the wan port by the for loop.
			for(port=0;port<5;port++)
				#if defined(CONFIG_RTD_1295_HWNAT)
				if ((vlanconfig[RTL_WAN_IDX].memPort & (1 << port))) break;
				#else //defined(CONFIG_RTD_1295_HWNAT)
				if ((vlanconfig[1].memPort & (1 << port))) break;
				#endif //defined(CONFIG_RTD_1295_HWNAT)
			_rtl865x_ClearFDBEntryByPort(port);
		}
#endif
		port_link_sts2 = cur_link_sts;
	}
	return 0;
}
#endif

#if defined(CONFIG_TR181_ETH)

extern unsigned int get_uptime_by_sec(void);

unsigned int port_change_time[8] = {0, 0, 0, 0, 0, 0, 1, 0};
unsigned int if_change_time[2] = {0, 0};

uint32 port_link_sts3 = 0;

#if defined(CONFIG_RTL_8367R_SUPPORT)
int check_link_change(void)
{
	int port;
	uint32 cur_link_sts=0;

	for(port=0;port<5;port++)
	{
		uint32 phyData;

		/*Get PHY status register*/
		if (0 != rtl8367b_getAsicPHYReg(port, 1 /* PHY_STATUS_REG */, &phyData))
			return (-1);

		/*check link status*/
		if (phyData & (1<<2))
			cur_link_sts |= (1<<port);

		if ((port_link_sts3 & (1<<port)) != (cur_link_sts & (1<<port)))
			// record uptime to port_change_time[]
			port_change_time[port] = get_uptime_by_sec();
	}
	port_link_sts3 = cur_link_sts;
	return 0;
}
#endif
#endif
#if defined(CONFIG_RTL_8367R_SUPPORT)
uint32 rtl_get_8367r_port_link_status(void)
{
	uint32 port;
	uint32 linkPortMask = 0;

	for (port=PHY0; port<=PHY4; port++)
	{
		extern int  rtk_port_macStatus_get(int port, rtk_port_mac_ability_t *pPortstatus);
		rtk_port_mac_ability_t sts;

		if ((rtk_port_macStatus_get(port, &sts)) == 0) {

			if (sts.link == 1)
				linkPortMask |= 1<<port;
		}
	}

	return linkPortMask;
}
#endif

inline void link_enhancement(void)
{
#if defined(CONFIG_RTL_FE_AUTO_DOWN_SPEED)
	extern void ads_check(void);
	ads_check();
#endif

#if defined(CONFIG_RTL_819XD) || defined(CONFIG_RTL_8196E)
	#if defined(CONFIG_RTL_EEE_DISABLED)&&!defined(CONFIG_RTL_8367R_SUPPORT)
	rtl819xD_check_phy_cb_snr();
	#endif
#endif

#ifdef CONFIG_RTL_8196E
	refine_phy_setting();
#endif

#if defined(CONFIG_RTL_8367R_SUPPORT) && defined(CONFIG_RTK_REFINE_PORT_DUPLEX_MODE)
	rtk_refinePortDuplexMode();
#endif
}

#if !defined(CONFIG_RTD_1295_HWNAT)
#if defined(DYNAMIC_ADJUST_TASKLET) || defined(CONFIG_RTL8196C_REVISION_B) || defined(CONFIG_RTL_8198) || defined(RTL_CPU_QOS_ENABLED) || defined(CONFIG_RTL_819XD) || defined(CONFIG_RTL_8196E) || defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)
extern int rtl865x_reinitSwitchCore(void);
static void one_sec_timer(unsigned long task_priv)
{
	unsigned long		flags=0;
	struct dev_priv	*cp;
#if defined(CONFIG_RTL_REINIT_SWITCH_CORE) && !defined(CONFIG_RTL_8197F)
	int i;
#endif

	cp = NETDRV_PRIV((struct net_device *)task_priv);

	//spin_lock_irqsave (&cp->lock, flags);
	SMP_LOCK_ETH(flags);

#if defined(PATCH_GPIO_FOR_LED)
	if (((struct net_device *)task_priv)->name[3] == '0' ) {	// eth0
		int port;

		for (port=0; port<RTL8651_PHY_NUMBER; port++) {
			update_mib_counter(port);
			calculate_led_interval(port);
			if (led_cb[port].link_status & GPIO_LINK_STATE_CHANGE)
			{
				gpio_led_Interval_timeout(port);
			}
		}
	}
#endif

#if defined(DYNAMIC_ADJUST_TASKLET)
	if (((struct net_device *)task_priv)->name[3] == '0' && rx_pkt_thres > 0 &&
									((eth_flag&TASKLET_MASK) == 0))  {
		if (rx_cnt > rx_pkt_thres) {
			if (!rx_tasklet_enabled) {
				rx_tasklet_enabled = 1;
			}
		}
		else {
			if (rx_tasklet_enabled) { // tasklet enabled
				rx_tasklet_enabled = 0;
			}
		}
		rx_cnt = 0;
	}
#endif

#if defined(CONFIG_RTL_8197D_DYN_THR) || defined(CONFIG_RTL_LONG_ETH_CABLE_REFINE)
	/* for the case: plug in ethernet cable first, then power on the device. */
	if (do_it_once == 0) {
		newLinkPortMask=rtl865x_getPhysicalPortLinkStatus();

		#ifdef CONFIG_RTL_8367R_SUPPORT
		curLinkPortMask = 0;
		#endif

		#ifdef CONFIG_RTL_8197D_DYN_THR
		rtl819x_setQosThreshold(curLinkPortMask, newLinkPortMask);
		#endif

		#if defined(CONFIG_RTL_LONG_ETH_CABLE_REFINE)
		long_cable_check();
		#endif

		curLinkPortMask=newLinkPortMask;
		do_it_once = 1;
	}
#endif

#if defined(CONFIG_RTL_8367R_SUPPORT)
	newLinkPortMask = rtl_get_8367r_port_link_status();
	#if defined(CONFIG_AUTO_DHCP_CHECK)
	rtl865x_setLinkEventFlag(curLinkPortMask, newLinkPortMask);
	#endif
	curLinkPortMask = newLinkPortMask;
#endif
#ifdef CONFIG_RTL8196C_REVISION_B
	if ((REG32(REVR) == RTL8196C_REVISION_A) && (eee_enabled)) {
			int i, curr_sts;
			uint32 reg;

			/*
				prev_port_sts[] = 0, current = 0	:	do nothing
				prev_port_sts[] = 0, current = 1	:	update prev_port_sts[]
				prev_port_sts[] = 1, current = 0	:	update prev_port_sts[], disable EEE
				prev_port_sts[] = 1, current = 1	:	enable EEE if EEE is disabled
			 */
			for (i=0; i<MAX_PORT_NUMBER; i++)
			{
				curr_sts = (REG32(PSRP0 + (i * 4)) & PortStatusLinkUp) >> 4;

				if ((prev_port_sts[i] == 1) && (curr_sts == 0)) {
					// disable EEE MAC
					REG32(EEECR) = (REG32(EEECR) & ~(0x1f << (i * 5))) |
						((EN_P0_FRC_EEE|FRC_P0_EEE_100) << (i * 5));
					//printk("	disable EEE for port %d\n", i);
				}
				else if ((prev_port_sts[i] == 1) && (curr_sts == 1)) {
					reg = REG32(EEECR);
					if ((reg & (1 << (i * 5))) == 0) {
						// enable EEE MAC
						REG32(EEECR) = (reg & ~(0x1f << (i * 5))) |
							((FRC_P0_EEE_100|EN_P0_TX_EEE|EN_P0_RX_EEE) << (i * 5));
						//printk("	enable EEE for port %d\n", i);
					}
				}
				prev_port_sts[i] = curr_sts;
			}
			//printk(" %d %d %d %d %d\n", port_sts[0], port_sts[1], port_sts[2], port_sts[3], port_sts[4]);
		}
#endif

#if defined(RTL_CPU_QOS_ENABLED)
	totalLowQueueCnt = 0;
#endif

#ifdef CONFIG_RTL_8198_ESD
#if defined(CONFIG_PRINTK)
#define panic_printk         printk
#endif
	{
	int phy;
	uint32 val;
	if (first_time_read_reg6) {
		first_time_read_reg6 = 0;

		for (phy=0; phy<5; phy++) {
			rtl8651_setAsicEthernetPHYReg( phy, 31, 5  );
			rtl8651_getAsicEthernetPHYReg(phy, 1, &val);
			rtl8651_setAsicEthernetPHYReg(phy, 1, val | 0x4);

			rtl8651_setAsicEthernetPHYReg(phy, 5, 0xfff6);
			rtl8651_getAsicEthernetPHYReg(phy, 6, &val);
			rtl8651_setAsicEthernetPHYReg( phy, 31, 0  );

			if ((val & 0xff) == 0xFF) {
				need_to_check_esd2 = 0;
			}
			if (phy_reg30[phy] != 0) {
				rtl8651_getAsicEthernetPHYReg( phy, 22, &val );
				rtl8651_setAsicEthernetPHYReg( phy, 22, ((val & (0xff00)) | 0x17) );
				rtl8651_getAsicEthernetPHYReg( phy, 30, &val );

				phy_reg30[phy] = BIT(31) | (val & 0xfff);
			}
		}
	}

	if (++one_second_counter >= 10) {
		for (phy=0; phy<5; phy++)
		{
			if (phy_reg30[phy] != 0) {
				rtl8651_setAsicEthernetPHYReg( phy, 31, 5  );
				rtl8651_setAsicEthernetPHYReg(phy, 5, 0);

				rtl8651_getAsicEthernetPHYReg(phy, 6, &val);
				rtl8651_setAsicEthernetPHYReg( phy, 31, 0  );

				if ((val & 0xffff) != 0xAE04)
				{
					panic_printk("  ESD-1\n");
					//do {} while(1); // reboot
					machine_restart(NULL);
				}

				if (need_to_check_esd2) {
					rtl8651_setAsicEthernetPHYReg( phy, 31, 5  );
					rtl8651_setAsicEthernetPHYReg(phy, 5, 0xfff6);

					rtl8651_getAsicEthernetPHYReg(phy, 6, &val);
					rtl8651_setAsicEthernetPHYReg( phy, 31, 0  );

					if ((val & 0xff) != 0xFC)
					{
						panic_printk("  ESD-2\n");
						//do {} while(1); // reboot
						machine_restart(NULL);
					}
				}

				rtl8651_getAsicEthernetPHYReg( phy, 22, &val );
				rtl8651_setAsicEthernetPHYReg( phy, 22, ((val & (0xff00)) | 0x17) );
				rtl8651_getAsicEthernetPHYReg( phy, 30, &val );

				if (esd3_skip_one == 1) {
					phy_reg30[phy] = BIT(31) | (val & 0xfff);
				}
				else
				if ((phy_reg30[phy] & 0xfff) != (val & 0xfff)) {
					if (diff_more_than_1((phy_reg30[phy] & 0xf), (val & 0xf)) ||
						diff_more_than_1(((phy_reg30[phy] >> 4) & 0xf), ((val >> 4) & 0xf)) ||
						diff_more_than_1(((phy_reg30[phy] >> 8) & 0xf), ((val >> 8) & 0xf))
						) {
						panic_printk("  ESD-3: old= 0x%x, new= 0x%x\n", phy_reg30[phy] & 0xfff, val & 0xfff);
						//do {} while(1); // reboot
						machine_restart(NULL);
					}
					phy_reg30[phy] = BIT(31) | (val & 0xfff);
				}
			}

		}
		if (esd3_skip_one == 1) {
			esd3_skip_one = 0;
		}
		one_second_counter = 0;
	}
	}
#endif

#ifdef CONFIG_RTL_8196C_ESD
#if defined(CONFIG_PRINTK)
#define panic_printk         printk
#endif
	if (_96c_esd_counter) {
		if( (RTL_R32(PCRP4) & EnablePHYIf) == 0)
		{
			if (++_96c_esd_reboot_counter >= 20) {
				panic_printk("  ESD reboot...\n");
				machine_restart(NULL);
			}
		}
		else {
			_96c_esd_reboot_counter = 0;
		}
	}
#endif

#if defined(CONFIG_RTL_REINIT_SWITCH_CORE) && !defined(CONFIG_RTL_8197F)
	for(i = 0; i < ETH_INTF_NUM; i++)
	{
		struct dev_priv *tmp_cp;

		int portnum, startport=0;

#if defined(CONFIG_RTL_CHECK_SWITCH_TX_HANGUP)
		if (rtl865x_duringReInitSwtichCore == 1)
			continue;
#endif
		tmp_cp = ((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[i]));
		if(_rtl86xx_dev.dev[i] && tmp_cp && tmp_cp->portmask && tmp_cp->opened) {

#if defined(CONFIG_RTL_819XD)
			if (rtl_port0Refined == 1)
			{
				startport = 1;
			}
#endif
			for(portnum=startport;portnum<5;portnum++)
			{
				if(tmp_cp->portmask & (1<<portnum))
					break;
			}
			if(5 == portnum)
				continue;
			if((RTL_R32(PCRP0+portnum*4) & EnablePHYIf) == 0)
			{
				switch(rtl865x_reInitState)
				{
					case STATE_NO_ERROR:
						#if !defined(CONFIG_RTD_1295_HWNAT)
						if(( REG32(SYS_CLK_MAG) & SYS_SW_CLK_ENABLE)==0)
						{
							rtl865x_reInitState=STATE_SW_CLK_ENABLE_WAITING;
							rtl865x_reInitWaitCnt=2;
							REG32(SYS_CLK_MAG)=REG32(SYS_CLK_MAG)|SYS_SW_CLK_ENABLE;
						}
						else
						#endif //!defined(CONFIG_RTD_1295_HWNAT)
						{
							rtl865x_reinitSwitchCore();
							rtl865x_reInitState=STATE_NO_ERROR;
						}
						break;

					case STATE_SW_CLK_ENABLE_WAITING:
						rtl865x_reInitWaitCnt--;
						if(rtl865x_reInitWaitCnt<=0)
						{
							rtl865x_reInitWaitCnt=2;
							rtl865x_reInitState=STATE_TO_REINIT_SWITCH_CORE;

						}
						break;

					case STATE_TO_REINIT_SWITCH_CORE:
						rtl865x_reInitWaitCnt--;
						if(rtl865x_reInitWaitCnt<=0)
						{
							rtl865x_reinitSwitchCore();
							rtl865x_reInitState=STATE_NO_ERROR;
						}
						break;

					default :
						rtl865x_reinitSwitchCore();
						rtl865x_reInitState=STATE_NO_ERROR;
						break;
				}
				break;
			}
		}
	}
#if defined (CONFIG_RTL_8197D) || defined(CONFIG_RTL_8196D)
	//rtl819x_poll_sw();
#endif
#if defined(CONFIG_RTL_CHECK_SWITCH_TX_HANGUP)
	rtl_check_swCore_tx_hang();
#endif
#endif

#if defined(CONFIG_RTL_REINIT_SWITCH_CORE) && defined(CONFIG_RTL_CHECK_SWITCH_TX_HANGUP) && defined(CONFIG_RTL_8197F)
	rtl_check_swCore_tx_hang();
#endif

#ifdef CONFIG_RTL_REPORT_LINK_STATUS
	if (block_link_change > 0)
		block_link_change = 0;
#endif

	link_enhancement();

#if defined(CONFIG_RTL_IVL_SUPPORT)
	if ((rtl865x_curOpMode == BRIDGE_MODE) || (rtl865x_curOpMode == WISP_MODE))
		flush_l2();
#endif

#ifdef CONFIG_RTL_8881A
	{
	extern int Lx1_check(void);
	Lx1_check();
	}
#endif

#if defined(CONFIG_TR181_ETH)
	#if defined(CONFIG_RTL_8367R_SUPPORT)
	check_link_change();
	#endif
#endif

#if defined(CONFIG_RTL_ETH_802DOT1X_SUPPORT)
	if (dot1x_config.enable)
	{
		rtl_82dot1xCheckPortState();
	}
#endif

#ifdef CONFIG_RTL_8198C_10M_REFINE
	if (_8198c_link_check == 0) { // just do it once
		curLinkPortMask = 0;
		newLinkPortMask=rtl865x_getPhysicalPortLinkStatus();
		rtl8198c_10M_refine(curLinkPortMask, newLinkPortMask);
		curLinkPortMask=newLinkPortMask;
		_8198c_link_check = 1;
	}
#endif

#if defined(CONFIG_RTL_8370_SUPPORT)
	/* due to the RTL8370 RGMII port limitation (it must be set to FORCE mode),
	   we must polling the RTL8211E link status and speed, then set RTL8370 EXT1 port.
	 */
	{
	extern void RTL8370_Ext1_set(int speed); /* SPD_10M=0, SPD_100M=1,SPD_1000M=2 */
	uint32 rData;

	/* IEEE 802.3
	22.2.4.2.13 Link Status
	When read as a logic one, bit 1.2 indicates that the PHY has determined that a valid link has been established.
	When read as a logic zero, bit 1.2 indicates that the link is not valid. The criteria for determining link
	validity is PHY specific. The Link Status bit shall be implemented with a latching function, such that the
	occurrence of a link failure condition will cause the Link Status bit to become cleared and remain cleared
	until it is read via the management interface. This status indication is intended to support the management
	attribute defined in 30.5.1.1.4, aMediaAvailable.
	 */
	/* so we read it twice */
	rtl8651_getAsicEthernetPHYReg(P0_EXT_PHY_ID, 1, &rData);
	rtl8651_getAsicEthernetPHYReg(P0_EXT_PHY_ID, 1, &rData);

	if ((rtl8211eLinkStsSpeed & BIT(2)) != (rData & BIT(2))) { // status changed
		if (rData & BIT(2)) { // link up
			rtl8651_getAsicEthernetPHYReg(P0_EXT_PHY_ID, 17, &rData);
			RTL8370_Ext1_set((rData & 0xC000) >> 14); // reconfigure RTL8370 EXT1 port
			rtl8211eLinkStsSpeed = (rtl8211eLinkStsSpeed & (~0x3)) | ((rData & 0xC000) >> 14) | BIT(2); // update status
		}
		else { // link down
			rtl8211eLinkStsSpeed &= ~BIT(2); // update status
		}
	}
	}
#endif

#ifdef CONFIG_RTL_GIGA_LITE_REFINE
	if (giga_lite_enabled) {
		extern void set_giga_lite2(void);
		set_giga_lite2();
	}
#endif

#if defined(CONFIG_RTL_CUSTOM_PASSTHRU_PPPOE)
	/*If br0 mask less than 255.255.255.0, set_firewall will echo -1 > /proc/hw_nat,
	then it will call rtl865x_reChangeOpMode, it will delete passthru vlan entry. */
	if (((_br0_mask!=0)&&(_br0_mask<0xffffff00)) && (oldStatus&PPPOE_PASSTHRU_MASK)) {
		if (_rtl8651_getVlanTableEntry(PASSTHRU_VLAN_ID) == NULL) {
			SMP_UNLOCK_ETH(flags);
			rtl865x_addVlan(PASSTHRU_VLAN_ID);
			#if defined(CONFIG_POCKET_ROUTER_SUPPORT)
			rtl865x_addVlanPortMember(PASSTHRU_VLAN_ID, rtl865x_lanPortMask|rtl865x_wanPortMask|0x100);
			#else
			rtl865x_addVlanPortMember(PASSTHRU_VLAN_ID, rtl865x_lanPortMask|rtl865x_wanPortMask);
			#endif
			SMP_LOCK_ETH(flags);
		}
	}
#endif

#if defined(CONFIG_RTL_8197F)&&defined(RX_TASKLET)
		 if (rtl_rx_tasklet_running == 0) {
				   rtl_rx_tasklet_running = 1;
				   REG32(CPUIIMR) &= ~(RX_DONE_IE_ALL | PKTHDR_DESC_RUNOUT_IE_ALL);
				   rtl_rxSetTxDone(FALSE);
				   tasklet_hi_schedule(&cp->rx_dsr_tasklet);
		 }
#endif

#if defined (CONFIG_RTL_NIC_HWSTATS)&& defined (CONFIG_RTL_INBAND_CTL_API)
	re865x_get_hardwareStats(cp);
	//printk("rx_bytes_ps:%d,[%s]:[%d].\n",rx_bytes_ps,__FUNCTION__,__LINE__);
#endif

	mod_timer(&cp->expire_timer, jiffies + HZ);

	//spin_unlock_irqrestore(&cp->lock, flags);
	SMP_UNLOCK_ETH(flags);
}
#endif
#endif //!defined(CONFIG_RTD_1295_HWNAT)

#ifdef CONFIG_RTL8196C_GREEN_ETHERNET
static void power_save_timer(unsigned long task_priv)
{
	unsigned long		flags=0;
	//struct dev_priv	*cp;

	//cp = ((struct net_device *)task_priv)->priv;

	//spin_lock_irqsave (&cp->lock, flags);
	SMP_LOCK_ETH(flags);

	set_phy_pwr_save(port_pwr_save_low, 1);

	port_pwr_save_low = (port_pwr_save_low + 1) % 5;
	set_phy_pwr_save(port_pwr_save_low, 0);

	//mod_timer(&cp->expire_timer2, jiffies + (total_time_for_5_port / 5 / 10));
	mod_timer(&expire_timer2, jiffies + (total_time_for_5_port / 5 / 10));

	//spin_unlock_irqrestore(&cp->lock, flags);
	SMP_UNLOCK_ETH(flags);
}
#endif

static struct net_device *irqDev=NULL;
static int re865x_open (struct net_device *dev)
{
	struct dev_priv *cp;
	unsigned long flags=0;
	int rc;
	#if defined(CONFIG_RTL_MULTIPLE_WAN)
	char drvNetif_name[MAX_IFNAMESIZE];
	#endif

	cp = NETDRV_PRIV(dev);
#ifdef CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT
	//printk("re865x_open dev=%s , id = %d \n",dev->name,cp->id);
	if( cp->id == 0)
	{
		//printk("FAILED open vid=0");
		return FAILED;
	}
#endif
	if (cp->opened)
		return SUCCESS;

	/*	The first device be opened	*/
	#if defined(CONFIG_RTD_1295_HWNAT)
	if (atomic_inc_return(&rtl_devOpened)==1)
	#else //defined(CONFIG_RTD_1295_HWNAT)
	if (atomic_read(&rtl_devOpened)==0)
	#endif //defined(CONFIG_RTD_1295_HWNAT)
	{
		/* this is the first open dev */
		/* should not call rtl865x_down() here */
		/* rtl865x_down();*/
#ifdef CONFIG_RTL_8198
		{
		extern void disable_phy_power_down(void);
		disable_phy_power_down();
		}
#endif
		//spin_lock_irqsave(&cp->lock, flags);
		SMP_LOCK_ETH_BUF(flags);
		rtk_queue_init(&rx_skb_queue);
		SMP_UNLOCK_ETH_BUF(flags);
		#if !defined(CONFIG_RTD_1295_HWNAT)
		SMP_LOCK_ETH(flags);
		#endif //!defined(CONFIG_RTD_1295_HWNAT)
		rc = rtl865x_init_hw();
		#if !defined(CONFIG_RTD_1295_HWNAT)
		SMP_UNLOCK_ETH(flags);
		#endif //!defined(CONFIG_RTD_1295_HWNAT)

		#if !defined(CONFIG_RTD_1295_HWNAT)
		atomic_inc(&rtl_devOpened);
		#endif //!defined(CONFIG_RTD_1295_HWNAT)
		refill_rx_skb();
		//spin_unlock_irqrestore(&cp->lock, flags);
		if (rc) {
			//printk("rtl865x_init_hw() failed!\n");
			atomic_dec(&rtl_devOpened);
			return FAILED;
		}
#if defined(CONFIG_RTL_ETH_NAPI_SUPPORT)
		netif_napi_add(dev, &cp->napi, rtl865x_poll, RTL865X_NAPI_WEIGHT);
		napi_enable(&cp->napi);
#else
#if defined(RX_TASKLET)
		tasklet_init(&cp->rx_dsr_tasklet, (void *)interrupt_dsr_rx, (unsigned long)cp);
#endif
#endif
#ifdef TX_TASKLET
		tasklet_init(&cp->tx_dsr_tasklet, interrupt_dsr_tx, (unsigned long)cp);
#endif

#ifdef LINK_TASKLET
		tasklet_init(&cp->link_dsr_tasklet, interrupt_dsr_link, (unsigned long)cp);
#ifdef CONFIG_RTD_1295_MAC0_SGMII_LINK_MON
		if (hwnat_mac0_enable == 1 && hwnat_mac0_mode == 1 /*SGMII*/ &&
			get_rtd129x_cpu_revision() == RTD129x_CHIP_REVISION_A00)
			rtk_polling_mac0_lnk_status();
#endif /* CONFIG_RTD_1295_MAC0_SGMII_LINK_MON */
#endif

#ifdef CONFIG_RTL_PHY_PATCH
		memset(re865x_restartNWayCtrl,0, sizeof(re865x_restartNWayCtrl));
#endif


		#if defined(CONFIG_RTD_1295_HWNAT)
		rc = request_irq(dev->irq, interrupt_isr, IRQF_SHARED, dev->name, dev);
		#else //defined(CONFIG_RTD_1295_HWNAT)
		rc = request_irq(dev->irq, interrupt_isr, IRQF_DISABLED, dev->name, dev);
		#endif //defined(CONFIG_RTD_1295_HWNAT)
		if (rc)
		{
			printk("request_irq() error!\n");
			atomic_dec(&rtl_devOpened);
			goto err_out_hw;
		}
		irqDev=dev;
		//cp->irq_owner =1;
		rtl865x_start();
	}
	#if !defined(CONFIG_RTD_1295_HWNAT)
	else {
		atomic_inc(&rtl_devOpened);
	}
	#endif //!defined(CONFIG_RTD_1295_HWNAT)
	cp->opened = 1;

#ifdef CONFIG_RTL_HARDWARE_NAT
	reset_hw_mib_counter(dev);
#endif

	netif_start_queue(dev);

#if defined(CONFIG_TR181_ETH)
	if (dev->name[3] == '0')
		if_change_time[0] = get_uptime_by_sec();
	else
		if_change_time[1] = get_uptime_by_sec();
#endif

#if !defined(CONFIG_RTD_1295_HWNAT)
#if defined(DYNAMIC_ADJUST_TASKLET) || defined(CONFIG_RTL8196C_REVISION_B)|| defined(CONFIG_RTL_8198) || defined(CONFIG_RTL_819XD) || defined(CONFIG_RTL_8196E) || defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)
	if (dev->name[3] == '0')
	{
		init_timer(&cp->expire_timer);
		cp->expire_timer.expires = jiffies + HZ;
		cp->expire_timer.data = (unsigned long)dev;
		cp->expire_timer.function = one_sec_timer;
		mod_timer(&cp->expire_timer, jiffies + HZ);
#ifdef DYNAMIC_ADJUST_TASKLET
		rx_cnt = 0;
#endif
	}
#endif
#endif //!defined(CONFIG_RTD_1295_HWNAT)

#ifdef CONFIG_RTL8196C_GREEN_ETHERNET
	if (REG32(REVR) == RTL8196C_REVISION_B) {
		//if (dev->name[3] == '0')
		if (atomic_read(&rtl_devOpened)==1)
		{
			#if 0
			init_timer(&cp->expire_timer2);
			cp->expire_timer2.expires = jiffies + HZ;
			cp->expire_timer2.data = (unsigned long)dev;
			cp->expire_timer2.function = power_save_timer;
			mod_timer(&cp->expire_timer2, jiffies + HZ);
			#else
			init_timer(&expire_timer2);
			expire_timer2.expires = jiffies + HZ;
			expire_timer2.data = (unsigned long)dev;
			expire_timer2.function = power_save_timer;
			mod_timer(&expire_timer2, jiffies + HZ);
			#endif
		}
	}
#endif

#ifdef CONFIG_RTL_LAYERED_DRIVER_L3
	/*FIXME_hyking: should add default route to cpu....*/
	if(rtl865x_curOpMode == GATEWAY_MODE)
#if defined(CONFIG_RTL_PUBLIC_SSID)
		rtl865x_addRoute(0,0,0,RTL_GW_WAN_DEVICE_NAME,0);
#else
		rtl865x_addRoute(0,0,0,RTL_DRV_WAN0_NETIF_NAME,0);
#endif
#endif

#if defined(CONFIG_RTL_HARDWARE_IPV6_SUPPORT)
	if (rtl865x_curOpMode == GATEWAY_MODE) {
		inv6_addr_t ipAddr, nexthop;
		memset(&ipAddr, 0, sizeof(inv6_addr_t));
		memset(&nexthop, 0, sizeof(inv6_addr_t));

		#if defined(CONFIG_RTL_PUBLIC_SSID)
		rtl8198c_addIpv6Route(ipAddr, 0, nexthop, RTL_GW_WAN_DEVICE_NAME);
		#else
		rtl8198c_addIpv6Route(ipAddr, 0, nexthop, RTL_DRV_WAN0_NETIF_NAME);
		#endif
	}
#endif

	#if defined(CONFIG_RTL_MULTIPLE_WAN)
	memset(drvNetif_name,0,MAX_IFNAMESIZE);
	if(rtl_get_ps_drv_netif_mapping_by_psdev_name(dev->name,drvNetif_name) == SUCCESS)
		rtl_enable_advRt_by_netifName(drvNetif_name);
	#endif
	rtl865x_enableDevPortForward( dev, cp);

	return SUCCESS;

err_out_hw:
	rtl8186_stop_hw(dev, cp);
	rtl865x_down();
	return rc;
}

static int re865x_close (struct net_device *dev)
{
	struct dev_priv *cp;
#if defined(CONFIG_RTL_MULTIPLE_WAN)
	char drvNetif_name[MAX_IFNAMESIZE];
#endif
#if defined(CONFIG_RTL_ETH_NAPI_SUPPORT)
	unsigned long flags;
#endif

	cp = NETDRV_PRIV(dev);
	if (!cp->opened)
		return SUCCESS;

	netif_stop_queue(dev);
	/* The last opened device	*/
	#if defined(CONFIG_RTD_1295_HWNAT)
	if (atomic_dec_return(&rtl_devOpened) == 0)
	#else //defined(CONFIG_RTD_1295_HWNAT)
	if (atomic_read(&rtl_devOpened)==1)
	#endif //defined(CONFIG_RTD_1295_HWNAT)
	{
		/*	warning:
			1.if we don't reboot,we shouldn't hold switch core from rx/tx, otherwise there will be some problem during change operation mode
			2.only when two devices go down,can we shut down nic interrupt
			3.the interrupt will be re_enable by rtl865x_start()
		*/
		rtl865x_disableInterrupt();

		//free_irq(dev->irq, GET_IRQ_OWNER(cp));
		//((struct dev_priv *)((GET_IRQ_OWNER(cp))->priv))->irq_owner = 0;
		free_irq(dev->irq, irqDev);
		//((struct dev_priv *)(irqDev->priv))->irq_owner = 0;

#if !defined(CONFIG_RTL_ETH_NAPI_SUPPORT)
#ifdef RX_TASKLET
		tasklet_kill(&cp->rx_dsr_tasklet);
#endif
#endif

#ifdef TX_TASKLET
		tasklet_kill(&cp->tx_dsr_tasklet);
#endif

#ifdef LINK_TASKLET
		tasklet_kill(&cp->link_dsr_tasklet);
#endif

#ifdef CONFIG_RTL_PHY_PATCH
		memset(re865x_restartNWayCtrl,0, sizeof(re865x_restartNWayCtrl));
#endif
		#if !defined(CONFIG_RTD_1295_HWNAT)
		atomic_dec(&rtl_devOpened);
		#endif //!defined(CONFIG_RTD_1295_HWNAT)
		free_rx_skb();

		rtk_queue_exit(&rx_skb_queue);

	}

	#if defined(CONFIG_RTL_ETH_NAPI_SUPPORT)
	if (strncmp(dev->name, irqDev->name, IFNAMSIZ)==0) {
		local_irq_save(flags);
		napi_disable(&cp->napi);
		netif_napi_del(&cp->napi);
		cp->napi.poll = NULL;
		local_irq_restore(flags);
	}
	#endif

	memset(&cp->net_stats, '\0', sizeof(struct net_device_stats));
	#if !defined(CONFIG_RTD_1295_HWNAT)
	if (atomic_read(&rtl_devOpened)> 0)
		atomic_dec(&rtl_devOpened);
	#endif //defined(CONFIG_RTD_1295_HWNAT)
	cp->opened = 0;

	#if defined(CONFIG_RTL_MULTIPLE_WAN)
	memset(drvNetif_name,0,MAX_IFNAMESIZE);
	if(rtl_get_ps_drv_netif_mapping_by_psdev_name(dev->name,drvNetif_name) == SUCCESS)
		rtl_disable_advRt_by_netifName(drvNetif_name);

	if(rtl_port_used_by_device(cp->portmask) == FAILED)
	#endif
	{
		rtl865x_disableDevPortForward(dev, cp);
#if !defined(CONFIG_RTL_8196C)
		/*for lan dhcp client to renew ip address*/
		rtl865x_restartDevPHYNway(dev, cp);
#endif
		rtl8186_stop_hw(dev, cp);
	}

#if !defined(CONFIG_RTD_1295_HWNAT)
#if defined(DYNAMIC_ADJUST_TASKLET) || defined(BR_SHORTCUT) || defined(CONFIG_RTL8196C_REVISION_B) || defined(CONFIG_RTL_8198) || defined(CONFIG_RTL_819XD) || defined(CONFIG_RTL_8196E) || defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)
	if (timer_pending(&cp->expire_timer))
		del_timer_sync(&cp->expire_timer);
#endif
#endif //!defined(CONFIG_RTD_1295_HWNAT)

#if defined(CONFIG_TR181_ETH)
	if (dev->name[3] == '0')
		if_change_time[0] = get_uptime_by_sec();
	else
		if_change_time[1] = get_uptime_by_sec();
#endif

#ifdef CONFIG_RTL8196C_GREEN_ETHERNET
	if (atomic_read(&rtl_devOpened)==0)
	{
		if (REG32(REVR) == RTL8196C_REVISION_B) {
			#if 0
				if (timer_pending(&cp->expire_timer2))
						del_timer_sync(&cp->expire_timer2);
			#else
			if (timer_pending(&expire_timer2))
				del_timer_sync(&expire_timer2);
			#endif
		}
	}
#endif

#ifdef BR_SHORTCUT
	if (dev == cached_dev)
		cached_dev=NULL;

	if (dev == cached_dev2)
		cached_dev2=NULL;

	if (dev == cached_dev3)
		cached_dev3=NULL;

	if (dev == cached_dev4)
		cached_dev4=NULL;
#endif
#ifdef CONFIG_RTL_HARDWARE_NAT
	reset_hw_mib_counter(dev);
#endif

	return SUCCESS;
}

#if defined(CONFIG_RTL_STP) || (defined(CONFIG_RTL_CUSTOM_PASSTHRU))
static int re865x_pseudo_open (struct net_device *dev)
{
	struct dev_priv *cp;

	cp = NETDRV_PRIV(dev);
	if (cp->opened)
		return SUCCESS;

	cp->opened = 1;
	netif_start_queue(dev);
	return SUCCESS;
}


static int re865x_pseudo_close (struct net_device *dev)
{
	struct dev_priv *cp;

	cp = NETDRV_PRIV(dev);

	if (!cp->opened)
		return SUCCESS;
	netif_stop_queue(dev);

	memset(&cp->net_stats, '\0', sizeof(struct net_device_stats));
	cp->opened = 0;

#ifdef BR_SHORTCUT
	if (dev == cached_dev)
		cached_dev=NULL;

	if (dev == cached_dev2)
		cached_dev2=NULL;

	if (dev == cached_dev3)
		cached_dev3=NULL;

	if (dev == cached_dev4)
		cached_dev4=NULL;
#endif
	return SUCCESS;
}
#endif

#if 0//defined(CONFIG_RTL_STP)
static int re865x_stp_mapping_init(void)
{
	int i, j, k, totalVlans;
	totalVlans=((sizeof(vlanconfig))/(sizeof(struct rtl865x_vlanConfig)))-1;

	for (i = 0; i < MAX_RE865X_STP_PORT; i++)
	{
		STP_PortDev_Mapping[i] = NO_MAPPING;
	}

	STP_PortDev_Mapping[WLAN_PSEUDO_IF_INDEX] = WLAN_PSEUDO_IF_INDEX;
	#ifdef CONFIG_RTK_MESH
	STP_PortDev_Mapping[WLAN_MESH_PSEUDO_IF_INDEX] = WLAN_MESH_PSEUDO_IF_INDEX;
	#endif

	j = 0;
	for(k=0;k<totalVlans;k++)
	{
		if (vlanconfig[k].isWan == FALSE)
		{
			for(i=0; i< MAX_RE865X_ETH_STP_PORT ; i++)
			{
				if ( (1<<i) & vlanconfig[k].memPort )
				{
					STP_PortDev_Mapping[j] = i;
					j++;
				}
			}

			break;
		}
	}

	return SUCCESS;
}

static int re865x_stp_mapping_reinit(void)
{
	int i, j, k, totalVlans;
	totalVlans=((sizeof(vlanconfig))/(sizeof(struct rtl865x_vlanConfig)))-1;

	for (i = 0; i < MAX_RE865X_STP_PORT; i++)
	{
		STP_PortDev_Mapping[i] = NO_MAPPING;
	}

	STP_PortDev_Mapping[WLAN_PSEUDO_IF_INDEX] = WLAN_PSEUDO_IF_INDEX;
	#ifdef CONFIG_RTK_MESH
	STP_PortDev_Mapping[WLAN_MESH_PSEUDO_IF_INDEX] = WLAN_MESH_PSEUDO_IF_INDEX;
	#endif

	j = 0;
	for(k=0;k<totalVlans;k++)
	{
		if (vlanconfig[k].isWan == FALSE)
		{
			for(i=0; i< MAX_RE865X_ETH_STP_PORT; i++)
			{
				if ( (1<<i) & vlanconfig[k].memPort )
				{
					STP_PortDev_Mapping[j] = i;
					j++;
				}
			}

			break;
		}
	}

	return SUCCESS;
}

static int re865x_stp_get_pseudodevno(uint32 port_num)
{
	int i, dev_no;
	for(i=0; i< MAX_RE865X_STP_PORT-1 ; i++)
	{
		if( STP_PortDev_Mapping[i] == port_num)
		{
			dev_no = i;
			return dev_no;
		}
	}
	return NO_MAPPING;

}

static int getVidByPort(uint32 port_num)
{
	int i, totalVlans, retVid;

	retVid=0;
	totalVlans=((sizeof(vlanconfig))/(sizeof(struct rtl865x_vlanConfig)))-1;

	for(i=0;i<totalVlans;i++)
	{
		if((1<<port_num) & vlanconfig[i].memPort)
		{
			retVid=vlanconfig[i].vid;
			break;
		}
	}

	return retVid;
}
#endif

#if defined(CONFIG_RTL_LOCAL_PUBLIC)
//hyking:this function should move to rtl865x_fdb.c
//implement it at here just for releaae to natami...
//2010-02-22
static int32 rtl865x_getPortlistByMac(const unsigned char *mac,uint32 *portlist)
{
	int32 found = FAILED;
	ether_addr_t *macAddr;
	int32 column;
	rtl865x_tblAsicDrv_l2Param_t	fdbEntry;

	macAddr = (ether_addr_t *)(mac);
	found = rtl865x_Lookup_fdb_entry(0, macAddr, FDB_DYNAMIC, &column, &fdbEntry);
	if(found == SUCCESS)
	{
		if(portlist)
			*portlist = fdbEntry.memberPortMask;
	}

	return found;

}
#endif

#if defined (CONFIG_RTL_IGMP_SNOOPING)
int re865x_setMCastTxInfo(struct sk_buff *skb,struct net_device *dev, rtl_nicTx_info	*nicTx)
{
	int32 ret;
	 struct dev_priv *cp;
	struct iphdr *iph=NULL;
	#if defined (CONFIG_RTL_MLD_SNOOPING)
	struct ipv6hdr *ipv6h=NULL;
	#endif
	unsigned int l4Protocol=0;
	struct rtl_multicastDataInfo multicastDataInfo;
	struct rtl_multicastFwdInfo multicastFwdInfo;

#ifdef CONFIG_RTK_VLAN_WAN_TAG_SUPPORT
	struct sk_buff *skb_wan=NULL;
	rtl_nicTx_info	nicTx_wan;
#endif

#if defined (CONFIG_RTL_VLAN_8021Q)
	int src_port = skb->srcPort;
#endif

	if((skb==NULL) || (dev==NULL) ||(nicTx==NULL))
	{
		return -1;
	}

	if(nicIgmpModuleIndex==0xFFFFFFFF)
	{
		return -1;
	}

	if(igmpsnoopenabled != TRUE && mldSnoopEnabled != TRUE)
	{
		return -1;
	}
	cp = NETDRV_PRIV(dev);
	#ifndef CONFIG_RTL_VLAN_8021Q
	nicTx->portlist=cp->portmask;
	#endif
	if((skb->data[0]==0x01) && (skb->data[1]==0x00) && (skb->data[2]==0x5e))
	{
		if(igmpsnoopenabled!=TRUE)
		{
			return -1;
		}

		iph = re865x_getIpv4Header(skb->data);
		if(iph)
		{
			l4Protocol=iph->protocol;
			if((l4Protocol==IPPROTO_UDP) || (l4Protocol==IPPROTO_TCP) )
			{
				if(cp->portnum<=1)
				{
					return -1;
				}

				/*only process tcp/udp in igmp snooping data plane*/
				multicastDataInfo.ipVersion=4;
				memcpy(multicastDataInfo.sourceIp,&(iph->saddr),4);
				memcpy(multicastDataInfo.groupAddr,&(iph->daddr),4);
				multicastDataInfo.sourceIp[0] = ntohl(multicastDataInfo.sourceIp[0]);
				multicastDataInfo.groupAddr[0] = ntohl(multicastDataInfo.groupAddr[0]);
				/*
				multicastDataInfo.sourceIp[0]=  (uint32)(iph->saddr);
				multicastDataInfo.groupAddr[0]=  (uint32)(iph->daddr);
				*/
				ret= rtl_getMulticastDataFwdInfo(nicIgmpModuleIndex, &multicastDataInfo, &multicastFwdInfo);
				#ifdef  CONFIG_RTL_VLAN_8021Q
				if(linux_vlan_enable)
				{
					nicTx->portlist &= multicastFwdInfo.fwdPortMask & ((1<<RTL8651_MAC_NUMBER)-1) & (~(1<<src_port));
				}
				else
					nicTx->portlist = multicastFwdInfo.fwdPortMask& cp->portmask & ((1<<RTL8651_MAC_NUMBER)-1);
				#else
				nicTx->portlist = multicastFwdInfo.fwdPortMask& cp->portmask & ((1<<RTL8651_MAC_NUMBER)-1);
				#endif
				//printk("Portmask:%x ,%x[%s]:[%d].\n",multicastFwdInfo.fwdPortMask,nicTx->portlist,__FUNCTION__,__LINE__);


			}
			#ifdef CONFIG_RTK_VLAN_WAN_TAG_SUPPORT
			//fix tim; upnp
			if( vlan_bridge_multicast_tag)
			{
				if((cp->id == vlan_bridge_tag) && (nicTx->portlist & RTL_WANPORT_MASK))
				{
					nicTx->portlist &= (~RTL_WANPORT_MASK);
					skb_wan = skb_copy(skb, GFP_ATOMIC);
					if(skb_wan!=NULL)
					{
						nicTx_wan.txIdx=0;
						nicTx_wan.vid = vlan_bridge_multicast_tag;
						nicTx_wan.portlist = RTL_WANPORT_MASK;
						nicTx_wan.srcExtPort = 0;
						nicTx_wan.flags = (PKTHDR_USED|PKT_OUTGOING);
						nicTx_wan.tagport = RTL_WANPORT_MASK;
						RTL_CACHE_WBACK((unsigned long) skb_wan->data, skb_wan->len);
						if (RTL_swNic_send((void *)skb_wan, skb_wan->data, skb_wan->len, &nicTx_wan) < 0)
						{
							dev_kfree_skb_any(skb_wan);
						}
					}
				}
			}//hw-vlan
			#endif
		}
	}
#if defined (CONFIG_RTL_MLD_SNOOPING)
	else if ((skb->data[0]==0x33) && (skb->data[1]==0x33) && (skb->data[2]!=0xff))
	{

		if(mldSnoopEnabled!=TRUE)
		{
			return -1;
		}
		if(cp->portnum<=1)
		{
			return -1;
		}

		ipv6h=re865x_getIpv6Header(skb->data);
		if(ipv6h!=NULL)
		{
			l4Protocol=re865x_getIpv6TransportProtocol(ipv6h);
			/*udp or tcp packet*/
			if((l4Protocol==IPPROTO_UDP) || (l4Protocol==IPPROTO_TCP))
			{
					/*only process tcp/udp in igmp snooping data plane*/
				multicastDataInfo.ipVersion=6;
				memcpy(&multicastDataInfo.sourceIp, &ipv6h->saddr, 16);
				memcpy(&multicastDataInfo.groupAddr, &ipv6h->daddr, 16);
				multicastDataInfo.sourceIp[0] = ntohl(multicastDataInfo.sourceIp[0]);
				multicastDataInfo.sourceIp[1] = ntohl(multicastDataInfo.sourceIp[1]);
				multicastDataInfo.sourceIp[2] = ntohl(multicastDataInfo.sourceIp[2]);
				multicastDataInfo.sourceIp[3] = ntohl(multicastDataInfo.sourceIp[3]);
				multicastDataInfo.groupAddr[0] = ntohl(multicastDataInfo.groupAddr[0]);
				multicastDataInfo.groupAddr[1] = ntohl(multicastDataInfo.groupAddr[1]);
				multicastDataInfo.groupAddr[2] = ntohl(multicastDataInfo.groupAddr[2]);
				multicastDataInfo.groupAddr[3] = ntohl(multicastDataInfo.groupAddr[3]);
				ret= rtl_getMulticastDataFwdInfo(nicIgmpModuleIndex, &multicastDataInfo, &multicastFwdInfo);

				#ifdef  CONFIG_RTL_VLAN_8021Q
				if(linux_vlan_enable)
				{
				#if defined(CONFIG_RTL_CUSTOM_PASSTHRU) && defined (CONFIG_RTL_8021Q_VLAN_SUPPORT_SRC_TAG)
					if((skb->passThruFlag & IS_IP6_MC_PASSTHRU) && (skb->passThruFlag & FLOOD_IN_BRIDGE))
						nicTx->portlist &= ((1<<RTL8651_MAC_NUMBER)-1) & (~(1<<src_port));
					else
						nicTx->portlist &= multicastFwdInfo.fwdPortMask & ((1<<RTL8651_MAC_NUMBER)-1) & (~(1<<src_port));
				#else
					nicTx->portlist &= multicastFwdInfo.fwdPortMask & ((1<<RTL8651_MAC_NUMBER)-1) & (~(1<<src_port));
				#endif
				}
				else
					nicTx->portlist = multicastFwdInfo.fwdPortMask& cp->portmask & ((1<<RTL8651_MAC_NUMBER)-1);
				#else
				nicTx->portlist = multicastFwdInfo.fwdPortMask& cp->portmask & ((1<<RTL8651_MAC_NUMBER)-1);
				#endif
			}

		}


	}
#endif
	return 0;
}
#endif

//#if defined(CONFIG_RTK_VLAN_SUPPORT) && defined(CONFIG_RTK_VLAN_FOR_CABLE_MODEM)
struct net_device* re865x_get_netdev_by_name(const char* name)
{
	int i;
	for(i = 0; i < ETH_INTF_NUM; i++)
	{
		if(_rtl86xx_dev.dev[i] && (strcmp(_rtl86xx_dev.dev[i]->name,name) == 0))
			return _rtl86xx_dev.dev[i];
	}
	return NULL;
}
//#endif

#if 0//defined(CONFIG_RTL_STP)
static inline int rtl_process_stp_tx(rtl_nicTx_info *txInfo)
{
	struct net_device *dev;
	struct sk_buff *skb = NULL;
	struct dev_priv *cp;
	uint8 stpPortNum;

	skb = txInfo->out_skb;
	dev = skb->dev;
	cp = NETDRV_PRIV(dev);
	if(!dev->irq){
		//virtual interfaces have no IRQ assigned.
		//We use device name to identify STP port interfaces(virtual devices).
		if(memcmp((void *)(dev->name), "port", 4)==0)
		{
			if ((skb->data[0]&0x01) && !memcmp(&(skb->data[0]), STPmac, 5) && !(skb->data[5] & 0xF0))
			{
				stpPortNum= dev->name[strlen(dev->name)-1]-'0';
				if (STP_PortDev_Mapping[stpPortNum] != NO_MAPPING)
				{
					cp->id=getVidByPort(stpPortNum);
					cp->portmask = 1<<STP_PortDev_Mapping[stpPortNum];
					cp->portnum = 1;	//Multicast process will check this entry.
				}
				else
				{
					dev_kfree_skb_any(skb);
					return FAILED;
				}
			}
			else
			{
				//To STP port interfaces(virtual devices), drop non bpdu tx pkt.
				dev_kfree_skb_any(skb);
				return FAILED;
			}
		}
	}

	return SUCCESS;
}
#endif

#ifdef CONFIG_RTK_VLAN_WAN_TAG_SUPPORT
static inline int rtl_process_vlan_tag(rtl_nicTx_info *txInfo)
{

	struct sk_buff *skb = NULL;
	struct sk_buff *newskb;
	struct net_device *dev;
	struct dev_priv *cp;

	skb = txInfo->out_skb;
	dev = skb->dev;
	cp = NETDRV_PRIV(dev);

	if(vlan_tag && skb->srcPhyPort == RX_FROM_LOCAL)
	{
		#if defined(CONFIG_RTD_1295_HWNAT)
		if((rtl865x_curOpMode == GATEWAY_MODE && cp->id == vlanconfig[RTL_WAN_IDX].vid )||(rtl865x_curOpMode == BRIDGE_MODE))
		#else //defined(CONFIG_RTD_1295_HWNAT)
		if((rtl865x_curOpMode == GATEWAY_MODE && cp->id == vlanconfig[1].vid )||(rtl865x_curOpMode == BRIDGE_MODE))
		#endif //defined(CONFIG_RTD_1295_HWNAT)
		{

			newskb = NULL;
			if (skb_cloned(skb))
			{
				newskb = skb_copy(skb, GFP_ATOMIC);
				if (newskb == NULL)
				{
					cp->net_stats.tx_dropped++;
					dev_kfree_skb_any(skb);
					return FAILED;
				}
				dev_kfree_skb_any(skb);
				skb = newskb;
				txInfo->out_skb = skb;
			}
			if (*((unsigned short *)(skb->data+ETH_ALEN*2)) != __constant_htons(ETH_P_8021Q))
			{
				if (skb_headroom(skb) < VLAN_HLEN && skb_cow(skb, VLAN_HLEN) !=0 )
				{
					printk("%s-%d: error! (skb_headroom(skb) == %d < %d). Enlarge it!\n",
					__FUNCTION__, __LINE__, skb_headroom(skb), VLAN_HLEN);
					while (1) ;
				}
				skb_push(skb, VLAN_HLEN);
				memmove(skb->data, skb->data + VLAN_HLEN, VLAN_ETH_ALEN<<1);
				*(uint16*)(&(skb->data[12])) = __constant_htons(ETH_P_8021Q);
				*(uint16*)(&(skb->data[14])) = htons(vlan_tag)&0x0fff;// VID
				*(uint8*)(&(skb->data[14])) |= (((uint8)vlan_host_pri)&0x7) << 5;
			}
		}
	}
	return SUCCESS;
}
#endif

//__MIPS16
#if defined(CONFIG_RTL_CUSTOM_PASSTHRU)
static inline int rtl_process_passthru_tx(rtl_nicTx_info *txInfo)
{
	struct net_device *dev;
	struct sk_buff *skb = NULL;
	struct dev_priv *cp;

	if(oldStatus)
	{
		skb = txInfo->out_skb;
		dev = skb->dev;
		if (dev==_rtl86xx_dev.pdev)
		{
			if (SUCCESS==rtl_isPassthruFrame(skb->data))
			{
				cp = NETDRV_PRIV(_rtl86xx_dev.pdev);
				skb->dev=cp->dev;

			}
			else
			{
				dev_kfree_skb_any(skb);
					return FAILED;
			}
		}
	}

	return SUCCESS;
}
#endif

#if defined(CONFIG_RTK_VLAN_SUPPORT)
static inline int rtl_process_rtk_vlan_tx(rtl_nicTx_info *txInfo)
{
	struct net_device *dev;
	struct sk_buff *skb = NULL;
	struct sk_buff *newskb;
	struct dev_priv *cp;
#if	defined CONFIG_PPPOE_VLANTAG
	struct vlan_tag tag, *adding_tag;
	struct vlan_info *info;
	uint8 * data;
#endif

	if(rtk_vlan_support_enable)
	{
		skb = txInfo->out_skb;
		dev = skb->dev;
		cp = NETDRV_PRIV(dev);
		if (cp->vlan_setting.global_vlan)
		{
			newskb = NULL;
			if (skb_cloned(skb))
			{
				#if defined(CONFIG_RTL_ETH_PRIV_SKB)
				newskb = priv_skb_copy(skb);
				#else
				newskb = skb_copy(skb, GFP_ATOMIC);
				#endif
				if (newskb == NULL)
				{
					cp->net_stats.tx_dropped++;
					dev_kfree_skb_any(skb);
					return FAILED;
				}
				dev_kfree_skb_any(skb);
				skb = newskb;
				txInfo->out_skb = skb;
			}

			if (tx_vlan_process(dev, &cp->vlan_setting, skb, 0))
			{
				cp->net_stats.tx_dropped++;
				dev_kfree_skb_any(skb);
				return FAILED;
			}
		}
	}

#if	defined (CONFIG_PPPOE_VLANTAG)

	info =rtl_get_PPPOE_vlanInfo();
	if(info->vlan && info->tag)
	{
		skb = txInfo->out_skb;
		cp = skb->dev->priv;
		if (skb_cloned(skb))
		{
			#if defined(CONFIG_RTL_ETH_PRIV_SKB)
			newskb = priv_skb_copy(skb);
			#else
			newskb = skb_copy(skb, GFP_ATOMIC);
			#endif
			if (newskb == NULL)
			{
				cp->net_stats.tx_dropped++;
				dev_kfree_skb_any(skb);
				return FAILED;
			}
			dev_kfree_skb_any(skb);
			skb = newskb;
			txInfo->out_skb = skb;
		}

		data= skb->data;
		if (((*((uint16*)(data+(ETH_ALEN<<1)))==__constant_htons(ETH_P_PPP_SES))||(*((uint16*)(data+(ETH_ALEN<<1)))==__constant_htons(ETH_P_PPP_DISC))) ||
				((*((uint16*)(data+(ETH_ALEN<<1)))==__constant_htons(ETH_P_8021Q))&&((*((uint16*)(data+(ETH_ALEN<<1)+VLAN_HLEN))==__constant_htons(ETH_P_PPP_SES))||(*((uint16*)(data+(ETH_ALEN<<1)+VLAN_HLEN))==__constant_htons(ETH_P_PPP_DISC)))))
		{

			//add vlan tag
			memcpy(&tag, skb->data+ETH_ALEN*2, VLAN_HLEN);
			if (tag.f.tpid !=  htons(ETH_P_8021Q)) { // tag not existed, insert tag
				if (skb_headroom(skb) < VLAN_HLEN && skb_cow(skb, VLAN_HLEN) !=0 ) {
					printk("%s-%d: error! (skb_headroom(skb) == %d < 4). Enlarge it!\n",
					__FUNCTION__, __LINE__, skb_headroom(skb));
					while (1) ;
				}
				skb_push(skb, VLAN_HLEN);
				memmove(skb->data, skb->data+VLAN_HLEN, ETH_ALEN*2);
			}
			COPY_TAG(tag, info);

			adding_tag = &tag;

			memcpy(skb->data+ETH_ALEN*2, adding_tag, VLAN_HLEN);

		}
	}
#endif

	return SUCCESS;
}
#endif

#if defined(CONFIG_RTL_VLAN_PASSTHROUGH_SUPPORT)
int rtl_process_vlan_passthrough_tx(struct sk_buff **ppskb)
{
	struct sk_buff *skb=NULL, *newskb=NULL;
	struct dev_priv *cp=NULL;

	if(!ppskb)
		return FAILED;

	skb = *ppskb;
	if(!skb)
		return FAILED;
	cp = skb->dev->priv;

	//Tag already existed.
	if (*(uint16*)(&skb->data[ETH_ALEN<<1]) == __constant_htons(ETH_P_8021Q))
		return SUCCESS;

	if (*(uint16*)skb->vlan_passthrough_saved_tag == __constant_htons(ETH_P_8021Q))
	{
		newskb = NULL;

		if (skb_cloned(skb))
		{
			#if defined(CONFIG_RTL_ETH_PRIV_SKB)
			newskb = priv_skb_copy(skb);
			#else
			newskb = skb_copy(skb, GFP_ATOMIC);
			#endif

			if (newskb == NULL)
			{
				cp->net_stats.tx_dropped++;
				dev_kfree_skb_any(skb);
				*ppskb = NULL;
				return FAILED;
			}
			dev_kfree_skb_any(skb);
			skb = newskb;
			*ppskb = newskb;
		}

		if (skb_headroom(skb) < VLAN_HLEN && skb_cow(skb, VLAN_HLEN) !=0 ) {
			printk("%s-%d: error! (skb_headroom(skb) == %d < 4). Enlarge it!\n",
			__FUNCTION__, __LINE__, skb_headroom(skb));
			while (1) ;
		}

		skb_push(skb, VLAN_HLEN);
		memmove(skb->data, skb->data+VLAN_HLEN, ETH_ALEN*2);
		memcpy(skb->data+ETH_ALEN*2, skb->vlan_passthrough_saved_tag, VLAN_HLEN);
	}

	return SUCCESS;
}
#endif

 #if defined(CONFIG_RTL_8021Q_VLAN_SUPPORT_SRC_TAG)
//Skb must be rcved from WAN port.
uint32 rtk_get_vlan_tagmask(uint16 vid);
uint16 rtl_get_linux_vlan_src_tag_vid(struct sk_buff *skb)
{
	struct dev_priv *cp=NULL;
	int member_port_mask=0, tagged_port_mask=0, vid=0;

	if(!skb)
		return RTL_WANVLANID;

	cp = (struct dev_priv *)NETDRV_PRIV(skb->dev);

	//Tag already existed.
	if (*(uint16*)(&skb->data[ETH_ALEN<<1]) == __constant_htons(ETH_P_8021Q))
		return RTL_WANVLANID;

	//Pkt from NAT LAN group.
	if (*(uint16*)skb->linux_vlan_src_tag == __constant_htons(ETH_P_8021Q))
	{
		vid = ntohs(*(short *)(skb->linux_vlan_src_tag+2)) & 0x0fff;
		member_port_mask = rtk_get_vlan_portmask(vid);
		tagged_port_mask = rtk_get_vlan_tagmask(vid);

		if(! (tagged_port_mask & ~(0x01e0|RTL_WANPORT_MASK)))   //untagged group
			return RTL_WANVLANID;
		else
			return vid;
	}
	else //Pkt from DUT itself.
	{
		member_port_mask = rtk_get_vlan_portmask(rtk_vlan_wan_vid);
		tagged_port_mask = rtk_get_vlan_tagmask(rtk_vlan_wan_vid);

		if(!(member_port_mask & RTL_WANPORT_MASK) ||
			!(tagged_port_mask & RTL_WANPORT_MASK) )
			return RTL_WANVLANID;
		else
			return rtk_vlan_wan_vid;
	}
}

//Ingress check for WAN->LAN(NAT, and maybe bridge.)
int rtl_process_linux_vlan_tx_check(struct sk_buff *skb)
{
	uint16 tag_vid=0, src_vid=0;

	if (*(uint16*)skb->linux_vlan_src_tag != __constant_htons(ETH_P_8021Q) ||
		*(uint16*)(skb->data+(ETH_ALEN<<1)) != __constant_htons(ETH_P_8021Q))
		return SUCCESS;

	src_vid = ntohs(*(uint16*)(skb->linux_vlan_src_tag+2)) & 0x0fff;

	tag_vid = ntohs(*(uint16*)(skb->data+(ETH_ALEN<<1)+2)) & 0x0fff;

	if(src_vid == tag_vid)
		return SUCCESS;
	else
		return FAILED;
}
#endif

static inline int rtl_pstProcess_xmit(struct dev_priv *cp, struct net_device *dev, int len)
{
#if !defined(CONFIG_RTL_NIC_HWSTATS)
	cp->net_stats.tx_packets++;
	cp->net_stats.tx_bytes += len;
#endif
	/*jwj: directly use skb->dev may more safer.*/
	//cp->dev->trans_start = jiffies;
	if (dev)
		dev->trans_start = jiffies;

	return SUCCESS;
}

static inline int rtl_preProcess_xmit(rtl_nicTx_info *txInfo)
{
	int retval = SUCCESS;
	#if defined(CONFIG_RTL_8021Q_VLAN_SUPPORT_SRC_TAG)
	struct dev_priv *cp = NULL;
	#endif

	#if 0//defined(CONFIG_RTL_STP)
	retval = rtl_process_stp_tx(txInfo);
	if(FAILED == retval)
		return retval;
	#endif
	#if defined(CONFIG_RTL_CUSTOM_PASSTHRU)
	retval = rtl_process_passthru_tx(txInfo);
	if(FAILED == retval)
		return retval;
	#endif
	#if defined(CONFIG_RTL_HW_VLAN_SUPPORT)
	retval = rtl_process_hw_vlan_tx(txInfo);
	if(FAILED == retval)
		return retval;
	#endif
	#if defined(CONFIG_RTK_VLAN_SUPPORT)
	retval = rtl_process_rtk_vlan_tx(txInfo);
	if(FAILED == retval)
		return retval;
	#endif
	#ifdef CONFIG_RTK_VLAN_WAN_TAG_SUPPORT
	retval = rtl_process_vlan_tag(txInfo);
		return retval;
	#endif
	#if defined(CONFIG_RTL_VLAN_PASSTHROUGH_SUPPORT)
	retval = rtl_process_vlan_passthrough_tx((struct sk_buff **)&txInfo->out_skb);
	if(FAILED == retval)
		return retval;
	#endif
	#if defined(CONFIG_RTL_8021Q_VLAN_SUPPORT_SRC_TAG)
	if(linux_vlan_enable)
	{
		cp = (struct dev_priv *)NETDRV_PRIV(((struct sk_buff *)txInfo->out_skb)->dev);
		if(rtl865x_curOpMode==GATEWAY_MODE && cp->id!=RTL_WANVLANID)
		{
			retval = rtl_process_linux_vlan_tx_check((struct sk_buff *)txInfo->out_skb);
			if(FAILED == retval)
			{
				dev_kfree_skb_any(txInfo->out_skb);
				return retval;
			}
		}
	}
	#endif
	#if defined(CONFIG_RTL_REINIT_SWITCH_CORE)
	if(rtl865x_duringReInitSwtichCore==1) {
		dev_kfree_skb_any(txInfo->out_skb);
		return FAILED;
	}
	#endif

	return retval;
}

#if defined(CONFIG_RTL_ETH_802DOT1X_SUPPORT)
inline void rtl_direct_txInfo(uint32 port_mask,rtl_nicTx_info *txInfo)
#else
static inline void rtl_direct_txInfo(uint32 port_mask,rtl_nicTx_info *txInfo)
#endif
{
	txInfo->portlist = port_mask & 0x3f;
	txInfo->srcExtPort = 0;		//PKTHDR_EXTPORT_LIST_CPU;
	txInfo->flags = (PKTHDR_USED|PKT_OUTGOING);
#ifdef  CONFIG_RTL_VLAN_8021Q
	if(vlan_ctl_p->flag)
		rtk_rm_skb_vid((struct sk_buff *) txInfo->out_skb);
#endif
}
#if defined(CONFIG_RTL_ETH_802DOT1X_SUPPORT)
inline void rtl_hwLookup_txInfo(rtl_nicTx_info *txInfo)
#else
static inline void rtl_hwLookup_txInfo(rtl_nicTx_info *txInfo)
#endif
{
	txInfo->portlist = RTL8651_CPU_PORT;		/* must be set 0x7 */
	txInfo->srcExtPort = PKTHDR_EXTPORT_LIST_CPU;
	txInfo->flags = (PKTHDR_USED|PKTHDR_HWLOOKUP|PKTHDR_BRIDGING|PKT_OUTGOING);
}
#if defined(CONFIG_RTL_EXT_PORT_SUPPORT)
static inline void rtl_direct_txInfo3(uint32 port_mask,rtl_nicTx_info *txInfo)
{
	txInfo->portlist = port_mask & 0x3f;
	txInfo->srcExtPort = PKTHDR_EXTPORT_LIST_CPU;		//PKTHDR_EXTPORT_LIST_CPU;
	txInfo->flags = (PKTHDR_USED|PKT_OUTGOING);
}

static inline void rtl_hwLookup_txInfo3(rtl_nicTx_info *txInfo)
{
	txInfo->portlist = RTL8651_CPU_PORT;		/* must be set 0x7 */
	txInfo->srcExtPort = PKTHDR_EXTPORT_LIST_CPU;//2->7
	//txInfo->flags = (PKTHDR_USED|PKTHDR_HWLOOKUP|PKT_OUTGOING|CSUM_TCPUDP|CSUM_IP);
	txInfo->flags = (PKTHDR_USED|PKTHDR_HWLOOKUP|PKT_OUTGOING);

}
#endif

#ifdef CONFIG_RTK_VLAN_WAN_TAG_SUPPORT
static inline void rtl_hwLookup_txInfo2(rtl_nicTx_info *txInfo)
{
	txInfo->portlist = RTL8651_CPU_PORT;		/* must be set 0x7 */
	txInfo->srcExtPort = PKTHDR_EXTPORT_LIST_P2;//2->7
	txInfo->flags = (PKTHDR_USED|PKTHDR_HWLOOKUP|PKTHDR_BRIDGING|PKT_OUTGOING);
}
#endif
static inline int rtl_ip_option_check(struct sk_buff *skb)
{
	int flag = FALSE;
	if (((*((unsigned short *)(skb->data+ETH_ALEN*2)) == __constant_htons(ETH_P_IP))
		&& ((*((unsigned char*)(skb->data+ETH_ALEN*2+2)) != 0x45))) ||
		((*((unsigned short *)(skb->data+ETH_ALEN*2)) == __constant_htons(ETH_P_8021Q))&&
		(*((unsigned short *)(skb->data+ETH_ALEN*2+2)) == __constant_htons(ETH_P_IP))&&
		((*((unsigned char*)(skb->data+ETH_ALEN*2+4)) != 0x45))))
			flag = TRUE;
	return flag;
}

#if defined(CONFIG_RTL_HW_TX_CSUM)
unsigned char hw_csum_cache_mac[6];
unsigned int hw_csum_cache_port;
#endif

#ifndef CONFIG_RTL_VLAN_8021Q
static inline int rtl_isHwlookup(struct sk_buff *skb, struct dev_priv *cp, uint32 *portlist)
#else
static inline int rtl_isHwlookup(uint16 vid,struct sk_buff *skb, struct dev_priv *cp, uint32 *portlist)
#endif

{
	int flag = FALSE;

#if defined(CONFIG_RTL_HTTP_REDIRECT_LOCAL)
	if(is_skb_http_packet(skb))
		goto assign_portmask;
#endif

//#if defined (CONFIG_RTL_MULTI_LAN_DEV) ||defined (CONFIG_POCKET_ROUTER_SUPPORT)
#if defined (CONFIG_POCKET_ROUTER_SUPPORT)
	goto assign_portmask;
#elif defined(CONFIG_RTK_VLAN_SUPPORT)
	if(rtk_vlan_support_enable ==1) {
		goto assign_portmask;
	}
#endif

#if defined(CONFIG_RTL_HW_TX_CSUM)
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		// L4 checksum is un-complete, need to enable HW Tx checksum feature
		// in 8198C and before IC, it only can use direct Tx mode

		#ifdef CONFIG_RTL_MULTI_LAN_DEV
		goto assign_portmask;
		#else
		if (rtl_isWanDev(cp)!=TRUE) {	// LAN interface
			int portmask;
			extern int getL2MbrByMac(unsigned char *mac);

			if (memcmp(skb->data, hw_csum_cache_mac, 6) == 0) {
				*portlist = hw_csum_cache_port;
			}
			else {
				portmask = getL2MbrByMac(skb->data);
				if (portmask != -1) {
					*portlist = portmask;

					hw_csum_cache_port = portmask;
					memcpy(hw_csum_cache_mac, skb->data, 6);
				}
				else // not found, broadcast it
					*portlist = cp->portmask;
			}
			return FALSE; // direct Tx always

		}
		else
			goto assign_portmask;
		#endif
	}
#endif

#ifdef CONFIG_RTL_VLAN_8021Q
	//if (linux_vlan_enable&&linux_vlan_hw_support_enable)
	if (linux_vlan_enable)
	{
		return rtk_isHwlookup_vlan(vid, skb, portlist);
	}
	#if 0
	else if(linux_vlan_enable&&(!linux_vlan_hw_support_enable))
	{
		if ((!rtl_isWanDev(cp)) ||
			((rtl_isWanDev(cp)) &&(*((uint16*)(skb->data+(ETH_ALEN<<1))) == __constant_htons(ETH_P_8021Q))))
			return true;
	}
	#endif
#endif

	if ((rtl_isWanDev(cp)!=TRUE) && (rtl_ip_option_check(skb) != TRUE)) {
#if defined(CONFIG_RTL_INBAND_CTL_ACL)
		int portNum = -1;
		if(*((uint16*)(skb->data+(ETH_ALEN<<1)))==__constant_htons(inband_ctl_acl_ethernet_type))
			 portNum=rtl865x_getPortNum(skb->data) ;
		if( portNum!= -1 ){
			*portlist = 1<<portNum;
			flag = FALSE;
			return flag;
		}
		else
#endif
		flag = TRUE;
	} else {
		flag = FALSE;
	}

#if defined(CONFIG_RTL_HW_VLAN_SUPPORT)
#ifndef CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT  // new hw vlan , bridge can use HW look due to Brigger vlan will add tag when UC !!
	if((rtk_vlan_support_enable == 0) && ((!memcmp(cp->dev->name, "eth2", 5)) ||
	  (!memcmp(cp->dev->name, "eth3", 5)) ||(!memcmp(cp->dev->name, "eth4", 5))))
		flag = FALSE;
#endif
#endif

#if defined (CONFIG_RTL_LOCAL_PUBLIC)
	//hyking:
	//when hw local public and sw localpublic exist at same time,
	//pkt to sw local public would be trap to cpu by default ACL
	//2010-02-22
	flag = FALSE;
	if(rtl_isWanDev(cp)!=TRUE)
	{
		//hyking: default acl issue, direct tx now...
		rtl865x_getPortlistByMac(skb->data, portlist);
	} else
#endif
	if (flag==FALSE) {
//#if defined(CONFIG_RTL_MULTI_LAN_DEV) ||defined (CONFIG_POCKET_ROUTER_SUPPORT)||defined(CONFIG_RTK_VLAN_SUPPORT)
#if defined (CONFIG_POCKET_ROUTER_SUPPORT)||defined(CONFIG_RTK_VLAN_SUPPORT) || defined(CONFIG_RTL_HW_TX_CSUM)
assign_portmask:
#endif
		*portlist = cp->portmask;
#ifdef CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT
		if(rtl_isWanDev(cp)==TRUE)
			*portlist = (*portlist ) &  RTL_WANPORT_MASK;  //to wan UC only to Wan port
#endif
	}

	return flag;
}
static inline int rtl_fill_txInfo(rtl_nicTx_info *txInfo)
{
	uint32 portlist;
	struct sk_buff *skb = txInfo->out_skb;
	struct dev_priv *cp;
	#if 0//def CONFIG_RTL_VLAN_8021Q
	uint8 portMask;
	uint8 tagMask;
	uint8 untagMask;
	struct sk_buff *pskb = NULL;
	#endif
	cp = NETDRV_PRIV(skb->dev);
	#if 0//def CONFIG_RTL_VLAN_8021Q
	if(linux_vlan_enable)
	{
		txInfo->vid = skb->vlan_tci&0xfff;
		txInfo->portlist= rtk_get_vlan_portmask(skb->vlan_tci&0xfff);  //only 0~5 port
		if (txInfo->vid==0)
		{
			txInfo->vid = cp->id;
			txInfo->portlist=cp->portmask;
		}
	}
	else
	#endif
	#ifdef  CONFIG_RTL_VLAN_8021Q
	if(vlan_ctl_p->flag)
	{
		uint16 skb_vid=0;
		//if packet with tag , then use the vid as nictx vid.
		//if packet without tag  use cp->id as its vlan
		skb_vid =rtk_get_skb_vid(skb);
		if(skb_vid != 0)
		{
			txInfo->vid = skb_vid;
			txInfo->portlist= rtk_get_vlan_portmask(skb_vid);  //only 0~5 port
		}
		else
		{
			#ifdef CONFIG_RTL_8021Q_VLAN_SUPPORT_SRC_TAG
			if(rtl865x_curOpMode==GATEWAY_MODE && cp->id==RTL_WANVLANID)
			{
				txInfo->vid = rtl_get_linux_vlan_src_tag_vid(skb);
				txInfo->portlist = RTL_WANPORT_MASK;
			}
			else
			#endif
			{
				txInfo->vid = cp->id;
				txInfo->portlist=cp->portmask;
			}
		}
	}
	else
	#endif
	txInfo->vid = cp->id;

	#if defined(CONFIG_RTL_ISP_MULTI_WAN_SUPPORT)
	if ((skb->dev->priv_flags&IFF_DOMAIN_WAN) && (skb->vlan_tci&VLAN_VID_MASK)) {
		txInfo->vid = skb->vlan_tci&VLAN_VID_MASK;
	}
	#endif

	#if defined(CONFIG_RTK_VOIP_QOS)
	txInfo->priority  = 0 ;
	#endif
	#if	defined(CONFIG_RTL_HW_QOS_SUPPORT)
	txInfo->priority= rtl_qosGetPriorityByVid(cp->id, skb->mark);
	#endif

	//default output queue is 0
	txInfo->txIdx = 0;

#if (defined(CONFIG_RTL_8367R_SUPPORT) || defined(CONFIG_RTL_8370_SUPPORT))&& !defined(CONFIG_RTL_CPU_TAG)
	rtl_direct_txInfo((0x1 << RTL8197D_RGMII_PORT), txInfo); // always send to 8197D port 0
	return SUCCESS;
#endif

	#ifdef CONFIG_RTK_VLAN_WAN_TAG_SUPPORT
	txInfo->tagport = 0 ;
	#endif

	if((skb->data[0]&0x01)==0)
	{
		#ifndef CONFIG_RTL_VLAN_8021Q
		if(rtl_isHwlookup(skb, cp, &portlist) == TRUE)
		#else
		if(rtl_isHwlookup(txInfo->vid,skb, cp, &portlist) == TRUE)
		#endif
		{
		#ifdef CONFIG_RTK_VLAN_WAN_TAG_SUPPORT
			if ( vlan_bridge_tag && cp->id == vlan_bridge_tag)
			{
				rtl_hwLookup_txInfo2(txInfo);
			}
			else
		#endif
			#if defined(CONFIG_RTL_EXT_PORT_SUPPORT)
			if (txInfo->wFlags == 1) {
				rtl_hwLookup_txInfo3(txInfo);
			}
			else
			#endif
			rtl_hwLookup_txInfo(txInfo);
		}
		else
		{
			#if defined(CONFIG_RTL_EXT_PORT_SUPPORT)
			if (txInfo->wFlags == 1) {
				rtl_direct_txInfo3(portlist,txInfo);
			}
			else
			#endif
			rtl_direct_txInfo(portlist, txInfo);
		}
	}
	else
	{
		/*multicast process*/
		#ifdef CONFIG_RTL_VLAN_8021Q
		if (vlan_ctl_p->flag){
			rtl_direct_txInfo(txInfo->portlist, txInfo);
		}
		else{
			rtl_direct_txInfo(cp->portmask, txInfo);
		}
		#else
		rtl_direct_txInfo(cp->portmask, txInfo);
		#endif
#if defined (CONFIG_RTL_IGMP_SNOOPING)
		/*multicast process*/
		if(	((skb->data[0]==0x01) && (skb->data[1]==0x00) && (skb->data[2]==0x5e))
#if defined (CONFIG_RTL_MLD_SNOOPING)
			||	((skb->data[0]==0x33) && (skb->data[1]==0x33) && (skb->data[2]!=0xFF))
#endif
		)
		{
			re865x_setMCastTxInfo(skb,cp->dev, txInfo);
		}
#endif

#ifdef CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT
		checkMulticatTxInfo(skb,cp->dev, txInfo);
#endif
#if 0//def 	CONFIG_RTL_VLAN_8021Q
				/*Make igmp snoopy Bridge to ethernet work when linux vlan is enable */
			   if(linux_vlan_enable){
			   //if (0){
#if defined (CONFIG_RTL_IGMP_SNOOPING)
							/*multicast process*/
					if( ((skb->data[0]==0x01) && (skb->data[1]==0x00) && (skb->data[2]==0x5e))
#if defined (CONFIG_RTL_MLD_SNOOPING)
					||  ((skb->data[0]==0x33) && (skb->data[1]==0x33) && (skb->data[2]!=0xFF))
#endif
					)
					{
	panic_printk("%s %d  skb->dev->name=%s dmac:0x%02x:%02x:%02x:%02x:%02x:%02x smac:0x%02x:%02x:%02x:%02x:%02x:%02x \
	%02x:%02x:%02x:%02x %02x:%02x:%02x:%02x %02x:%02x:%02x:%02x\n", __FUNCTION__, __LINE__,
	 skb->dev->name,skb->data[0], skb->data[1], skb->data[2], skb->data[3], skb->data[4], skb->data[5]
	, skb->data[6], skb->data[7], skb->data[8], skb->data[9], skb->data[10], skb->data[11]
	, skb->data[12], skb->data[13], skb->data[14], skb->data[15], skb->data[16], skb->data[17],
	skb->data[18], skb->data[19], skb->data[20], skb->data[21], skb->data[22], skb->data[23]);
						txInfo->portlist&=vlan_ctl_p->group[txInfo->vid].memberMask;
						portMask=txInfo->portlist;
						if(portMask){
							tagMask=portMask&vlan_ctl_p->group[txInfo->vid].tagMemberMask;
							untagMask=portMask&(~tagMask);
							if(untagMask){
								if(tagMask){
									/*both tag and untag member exist,old one for tagged out
									and also need to copy a new one for untaged out*/
									pskb=skb_copy(skb,GFP_ATOMIC);
									if (pskb != NULL) {
										/*remove the tag*/
										memmove(pskb->data+VLAN_HLEN, pskb->data, ETH_ALEN<<1);
										skb_pull(pskb,VLAN_HLEN);
										/*set the port mask for untag member*/
										rtl_direct_txInfo(untagMask,txInfo);
										_dma_cache_wback_inv((unsigned long) pskb->data, pskb->len);
										if (RTL_swNic_send((void *)pskb, pskb->data, pskb->len, txInfo) < 0)
										{
											dev_kfree_skb_any(pskb);
										}
									}
								}
								else{
									/*only untag member exist,not need to copy a new one
									but just remove the tag on the old one*/
									if (skb_cloned(skb)){
										/*We need to copy a new one before removing the vlan tag
										if packet is cloned*/
										pskb = skb_copy(skb, GFP_ATOMIC);
										if (pskb == NULL) {
											dev_kfree_skb_any(skb);
											return FAILED;
										}
										dev_kfree_skb_any(skb);
										skb = pskb;
										pskb = NULL;
										/*let txinfo's skb pointer point to the new one*/
										txInfo->out_skb=skb;
									}
									/*remove the tag*/
									memmove(skb->data+VLAN_HLEN, skb->data, ETH_ALEN<<1);
									skb_pull(skb,VLAN_HLEN);
									/*set the port mask for untag member*/
									rtl_direct_txInfo(untagMask,txInfo);
								}
							}

							if(tagMask){
								/*tag member exist, just change the port's masks*/
								rtl_direct_txInfo(tagMask,txInfo);
							}
						}
					}
					else
#endif
					{
						if(rtl_isWanDev(cp) && (*((uint16*)(skb->data+(ETH_ALEN<<1))) != __constant_htons(ETH_P_8021Q)))
						{
							rtl_direct_txInfo(txInfo->portlist,txInfo);
						}
						else
						{
							rtl_hwLookup_txInfo(txInfo);
						}
					}
				}
				else
#endif
#if 0//def 	CONFIG_RTL_VLAN_8021Q

					rtl_direct_txInfo(txInfo->portlist,txInfo);
#endif

	}

	//for WAN Tag
	#ifdef CONFIG_RTK_VLAN_WAN_TAG_SUPPORT
	if((vlan_bridge_tag && cp->id == vlan_bridge_tag) || (vlan_tag && cp->id == vlan_tag))
		txInfo->tagport = RTL_WANPORT_MASK;
	#endif

	if(txInfo->portlist==0)
	{
		dev_kfree_skb_any(skb);
		return FAILED;
	}
	return SUCCESS;
}

#if defined(CONFIG_RTK_BRIDGE_VLAN_SUPPORT)
static int rtl_bridge_wan_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct net_device *wan_dev = NULL;
	if(!memcmp(lan_macaddr,skb->data+6, 6)) {
		dev_kfree_skb_any(skb);
		return 0;
	}
	//printk("[%s][%d]-skb->dev[%s],proto(0x%x)\n", __FUNCTION__, __LINE__, skb->dev->name,skb->protocol);
	wan_dev = rtl_get_wan_from_vlan_info();

	if(wan_dev){
		skb->dev = wan_dev;
		wan_dev->netdev_ops->ndo_start_xmit(skb, wan_dev);
		}
	return 0;
}
#endif

#if (defined(CONFIG_RTL_8367R_SUPPORT) || defined(CONFIG_RTL_8370_SUPPORT))&& !defined(CONFIG_RTL_CPU_TAG)
struct sk_buff * insert_vlan_tag(struct sk_buff *skb, int vid)
{
	struct sk_buff *newskb = NULL;

	if (*((unsigned short *)(skb->data+ETH_ALEN*2)) != __constant_htons(ETH_P_8021Q))
	{
		if (skb_cloned(skb) /* && skb->ppp_cloned == 0 */){
			newskb = skb_copy(skb, GFP_ATOMIC);
			if (newskb == NULL) {
				return NULL;
			}
			dev_kfree_skb_any(skb);
			skb = newskb;
		}
		if (skb_headroom(skb) < VLAN_HLEN)
			skb_cow(skb, VLAN_HLEN);

		//vlan tag
		skb_push(skb, VLAN_HLEN);
		memmove(skb->data, skb->data + VLAN_HLEN, VLAN_ETH_ALEN<<1);

		*(uint16*)(&(skb->data[14])) = htons(vid);			/* VID */
		*(uint16*)(&(skb->data[12])) = __constant_htons(ETH_P_8021Q);		/* TPID */
		*(uint8*)(&(skb->data[14])) &= 0x0f;					/* clear most-significant nibble of byte 14 */
	}
	return skb;
}
#endif

#if  defined (CONFIG_RTL_PPPOE_DIRECT_REPLY)
void extract_magicNum(struct sk_buff * skb);
extern int magicNum;
#endif
#if defined(CONFIG_RTL_ISP_MULTI_WAN_SUPPORT)
static int rtl_insert_vlan_tag(struct sk_buff **skb,unsigned int vid, unsigned int priority)
{
	struct sk_buff *tmpskb;
	unsigned char insert[16];
	u16 *vlan_tci;
	struct ethhdr *eth;
	struct vlan_ethhdr *veth;

	if(skb_cloned(*skb))
	{
		tmpskb = skb_copy(*skb, GFP_ATOMIC);
		dev_kfree_skb_any(*skb);
		if (tmpskb == NULL)
			return 0;
		*skb = tmpskb;
	}

	if (skb_headroom(*skb) < 4) {
		tmpskb = skb_realloc_headroom(*skb, 4);
		dev_kfree_skb_any(*skb);
		if (tmpskb == NULL)
			return 0;
		*skb = tmpskb;
	}

#ifdef CONFIG_E8B
	#ifdef CONFIG_NETFILTER
	if ((*skb)->mark & (1<<16)) { // marked by IPQoS
		insert[0] = (((*skb)->mark&0x7)<<5) | (priority<<5) | (vid>>8);
	}
	else if(((*skb)->mark&0x7)>=1)
	{
		insert[0] = ((((*skb)->mark&0x7)-1)<<5) | (priority<<5) | (vid>>8);
	}
	else
	#endif
		insert[0] = (priority<<5) | (vid>>8);
#else
	#ifdef CONFIG_NETFILTER
		insert[0] = (priority<<5) | (vid>>8);
	#else
	insert[0] = vid>>8;
	#endif
#endif
	insert[1] = vid&0xff;
	vlan_tci = (u16 *)insert;

	eth = (struct ethhdr *)(*skb)->data;
	if (eth->h_proto != htons(ETH_P_8021Q)) {
		veth = (struct vlan_ethhdr *)skb_push(*skb, VLAN_HLEN);
		/* Move the mac addresses to the beginning of the new header. */
		memmove((*skb)->data, (*skb)->data + VLAN_HLEN, 2 * VLAN_ETH_ALEN);
		/* first, the ethernet type */
		veth->h_vlan_proto = htons(ETH_P_8021Q);

		/* now, the TCI */
		veth->h_vlan_TCI = htons(*vlan_tci);
	}
	else {
		veth = (struct vlan_ethhdr *)(*skb)->data;
		/* the TCI */
		veth->h_vlan_TCI = htons(*vlan_tci);
	}

	return 1;
}
#endif
#define	RTL_NIC_TX_RETRY_MAX		(128)
__MIPS16
__IRAM_FWD
static int re865x_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	int retval, tx_retry_cnt;
	struct dev_priv *cp;
	struct sk_buff *tx_skb;
	rtl_nicTx_info	nicTx;
	#if defined(CONFIG_RTD_1295_HWNAT)
	dma_addr_t mapping = 0;
	#endif //defined(CONFIG_RTD_1295_HWNAT)

#if defined(CONFIG_SMP)
	unsigned long flags=0;
	SMP_LOCK_ETH_XMIT(flags);
#endif
#if defined (CONFIG_RTL_PPPOE_DIRECT_REPLY)
	if(magicNum == -1)
		extract_magicNum(skb);
#endif

	memset((void *)&nicTx, 0, sizeof(rtl_nicTx_info));
#if defined(CONFIG_RTL_EXT_PORT_SUPPORT)
	if (dev && (strncmp(dev->name, "wlan", 4)==0))
		nicTx.wFlags = 1;
	else
		nicTx.wFlags = 0;
#endif
	nicTx.out_skb = skb;
	retval = rtl_preProcess_xmit(&nicTx);

	if(FAILED == retval) {
		#if defined(CONFIG_SMP)
		SMP_UNLOCK_ETH_XMIT(flags);
		#endif
		return 0;
	}

	tx_skb = nicTx.out_skb;
	cp = NETDRV_PRIV(tx_skb->dev);

	if (
#ifndef CONFIG_RTL_ISP_MULTI_WAN_SUPPORT
		(cp->id==0) ||
#else
		((tx_skb->dev->priv_flags & IFF_DOMAIN_ELAN) && (cp->id==0)) ||
#endif
		(cp->portmask ==0))
	{
		dev_kfree_skb_any(tx_skb);
		#if defined(CONFIG_SMP)
		SMP_UNLOCK_ETH_XMIT(flags);
		#endif
		return 0;
	}

#if (defined(CONFIG_RTL_8367R_SUPPORT) || defined(CONFIG_RTL_8370_SUPPORT))&& !defined(CONFIG_RTL_CPU_TAG)
	tx_skb = nicTx.out_skb = insert_vlan_tag(skb, cp->id);
	if (tx_skb == NULL) {
		dev_kfree_skb_any(skb);
		#if defined(CONFIG_SMP)
		SMP_UNLOCK_ETH_XMIT(flags);
		#endif
		return 0;
	}
#endif

	retval = rtl_fill_txInfo(&nicTx);
	if(FAILED == retval) {
		#if defined(CONFIG_SMP)
		SMP_UNLOCK_ETH_XMIT(flags);
		#endif
		return 0;
	}

#if defined(CONFIG_RTL_QOS_PATCH) || defined(CONFIG_RTK_VOIP_QOS)
	if(((struct sk_buff *)tx_skb)->srcPhyPort == QOS_PATCH_RX_FROM_LOCAL){
		nicTx.priority = QOS_PATCH_HIGH_QUEUE_PRIO;
		nicTx.txIdx=RTL865X_SWNIC_TXRING_MAX_PKTDESC-1;	//use the highest tx ring index, note: not RTL865X_SWNIC_TXRING_HW_PKTDESC-1
	}
#endif

#if defined(CONFIG_RTL_HW_QOS_SUPPORT_WLAN)
	if (((struct sk_buff *)tx_skb)->srcPhyPort == QOS_PATCH_RX_FROM_WIRELESS) {
		if (strncmp(tx_skb->dev->name, RTL_DRV_WAN0_NETIF_NAME, IFNAMSIZ) == 0) {
			nicTx.priority = rtl_qosGetPriorityByMark(tx_skb->dev->name, skb->mark);
			nicTx.txIdx=RTL865X_SWNIC_TXRING_MAX_PKTDESC-1;	//use the highest tx ring index, note: not RTL865X_SWNIC_TXRING_HW_PKTDESC-1
		}
	}
#endif
	#if defined(CONFIG_RTL_ETH_802DOT1X_SUPPORT)
	if ((dot1x_config.enable)&&(rtl_is802dot1xFrame(skb->data))&&(!dot1x_config.enable_unicastresp))
	{
		rtl_802dot1xFilltxInfo(skb, &nicTx);
	}
	#endif
#if defined(CONFIG_RTL_ISP_MULTI_WAN_SUPPORT)
	if (enable_port_mapping) {
		int port_mask=0;

		if ((tx_skb->from_dev && (tx_skb->from_dev->priv_flags&(IFF_DOMAIN_ELAN|IFF_DOMAIN_WLAN))) &&
			 (tx_skb->dev->priv_flags&IFF_DOMAIN_WAN)) {	//upstream
#if 0
			if (tx_skb->from_dev->priv_flags & IFF_DOMAIN_ELAN) {
						TOKEN_NUM(tx_skb->from_dev->name,&port);
				if (port > 1)
					port -= 1;
			} else if (tx_skb->from_dev->priv_flags & IFF_DOMAIN_WLAN) {
						TOKEN_NUM(tx_skb->from_dev->name,&port);
			}
#else
			port_mask = rtl_get_bind_port_mask_by_dev_name(tx_skb->from_dev->name);
#endif

			if (!(tx_skb->vlan_member & port_mask)) {
				if(net_ratelimit())
				printk("upstream packet[%s -> %s] block by port mapping rule (member 0x%x).\n",
					tx_skb->from_dev->name, tx_skb->dev->name, tx_skb->vlan_member);
				dev_kfree_skb_any(tx_skb);
				#if defined(CONFIG_SMP)
				SMP_UNLOCK_ETH_XMIT(flags);
				#endif

				return 0;
			}
		} else if (tx_skb->from_dev && (tx_skb->from_dev->priv_flags&IFF_DOMAIN_WAN)) {
			if (!rtl_smux_downstream_port_mapping_check(tx_skb)) {
				if(net_ratelimit())
				printk("downstream packet[%s -> %s] block by port mapping rule (member 0x%x).\n",
					tx_skb->from_dev->name, tx_skb->dev->name, tx_skb->vlan_member);
				dev_kfree_skb_any(tx_skb);
				#if defined(CONFIG_SMP)
				SMP_UNLOCK_ETH_XMIT(flags);
				#endif

				return 0;
			}
		}
	}

	if ((RTL_WANVLANID != nicTx.vid) && (RTL_LANVLANID != nicTx.vid) && (RTL_BRIDGE_WANVLANID != nicTx.vid)) {
		if (!rtl_insert_vlan_tag(&tx_skb, nicTx.vid, tx_skb->vlan_tci>>13)) {
			#if defined(CONFIG_SMP)
			SMP_UNLOCK_ETH_XMIT(flags);
			#endif
			return 0;
		}
	} else if((tx_skb->mark & (1<<7)) && ((tx_skb->mark>>20) >0) && (dev->priv_flags & IFF_DOMAIN_WAN )) {
		//nicTx.vid is RTL_WANVLANID  or RTL_LANVLANID or RTL_BRIDGE_WANVLANID
		//means no configured VLAN ID for this WAN interface, so checking mark setting in skb by the QoS/VLAN rules
		//and replace the vid/priority.

		if (!rtl_insert_vlan_tag(&tx_skb, tx_skb->mark>>20, tx_skb->mark&0x7)) {
			#if defined(CONFIG_SMP)
			SMP_UNLOCK_ETH_XMIT(flags);
			#endif
			return 0;
		}
	}
#endif

#if defined(CONFIG_RTD_1295_HWNAT)
#if defined(CONFIG_RTL_ETH_TX_SG) || defined(CONFIG_RTL_TSO)
	if (skb_shinfo(tx_skb)->nr_frags>0)
	{
		uint32 dlen;
		dlen = tx_skb->len - tx_skb->data_len;

		///////////////////////////////////////////////
		/* workaround for HW TSO bug */
		//if ((dlen == 66) && ((output & 3) == 2)) {
		if (dlen == 66) {
			int *src, *dst;
			skb_frag_t *frag = &skb_shinfo(tx_skb)->frags[0];

			//DBG("tx_skb: len %d, data_len %d, dlen %d, nr_frags %d, gso_size %d, physical skb->data 0x%p",
			//	tx_skb->len, tx_skb->data_len, dlen,
			//	skb_shinfo(tx_skb)->nr_frags, skb_shinfo(tx_skb)->gso_size, (void *) virt_to_phys(tx_skb->data));

			/* move 4 bytes from the 1st fragment to skb->data */
			dst = (int *) skb_tail_pointer(tx_skb);
			src = (int *) (((void *) page_address(frag->page.p)) + frag->page_offset);
			*dst = *src;

			/* update pointers and size */
			tx_skb->data_len -= 4;
			frag->page_offset += 4;
			frag->size -=4;
		}

		dlen = tx_skb->len - tx_skb->data_len;
		///////////////////////////////////////////////
		//DBG("TSO: nr_frags %d, gso_size %d, dlen %d", skb_shinfo(tx_skb)->nr_frags, skb_shinfo(tx_skb)->gso_size, dlen);

		if (dlen > 0)
		{
			mapping = dma_map_single(&rtl819x_pdev->dev, tx_skb->data, dlen, DMA_TO_DEVICE);
			//DBG("dma_map_single(dev 0x%p, start 0x%p, len %d, dir %d) = mapping 0x%x", &rtl819x_pdev->dev, tx_skb->data, dlen, DMA_TO_DEVICE, (uint) mapping);
			if (unlikely(dma_mapping_error(&rtl819x_pdev->dev, mapping))) {
				if (net_ratelimit())
					netif_err(cp, drv, tx_skb->dev, "Failed to map TX DMA!\n");
				#if defined(CONFIG_RTL_PROC_DEBUG)||defined(CONFIG_RTL_DEBUG_TOOL)
				tx_drop_cnt++;
				#endif
				dev_kfree_skb_any(tx_skb);
				#if defined(CONFIG_SMP)
				SMP_UNLOCK_ETH_XMIT(flags);
				#endif
				return 0;
			}
		}
	}
	else
	{
		mapping = dma_map_single(&rtl819x_pdev->dev, tx_skb->data, tx_skb->len, DMA_TO_DEVICE);
		//DBG("dma_map_single(dev 0x%p, start 0x%p, len %d, dir %d) = mapping 0x%x", &rtl819x_pdev->dev, tx_skb->data, tx_skb->len, DMA_TO_DEVICE, (uint) mapping);
		if (unlikely(dma_mapping_error(&rtl819x_pdev->dev, mapping))) {
			if (net_ratelimit())
				netif_err(cp, drv, tx_skb->dev, "Failed to map TX DMA!\n");
			#if defined(CONFIG_RTL_PROC_DEBUG)||defined(CONFIG_RTL_DEBUG_TOOL)
			tx_drop_cnt++;
			#endif
			dev_kfree_skb_any(tx_skb);
			#if defined(CONFIG_SMP)
			SMP_UNLOCK_ETH_XMIT(flags);
			#endif
			return 0;
		}
	}
#else
	mapping = dma_map_single(&rtl819x_pdev->dev, tx_skb->data, tx_skb->len, DMA_TO_DEVICE);
	//DBG("dma_map_single(dev 0x%p, start 0x%p, len %d, dir %d) = mapping 0x%x", &rtl819x_pdev->dev, tx_skb->data, tx_skb->len, DMA_TO_DEVICE, (uint) mapping);
	if (unlikely(dma_mapping_error(&rtl819x_pdev->dev, mapping))) {
		if (net_ratelimit())
			netif_err(cp, drv, tx_skb->dev, "Failed to map TX DMA!\n");
		#if defined(CONFIG_RTL_PROC_DEBUG)||defined(CONFIG_RTL_DEBUG_TOOL)
		tx_drop_cnt++;
		#endif
		dev_kfree_skb_any(tx_skb);
		#if defined(CONFIG_SMP)
		SMP_UNLOCK_ETH_XMIT(flags);
		#endif
		return 0;
	}
#endif
#else //!defined(CONFIG_RTD_1295_HWNAT)
#if defined(CONFIG_RTL_ETH_TX_SG) || defined(CONFIG_RTL_TSO)
	{
	if (skb_shinfo(tx_skb)->nr_frags>0)
		RTL_CACHE_WBACK((unsigned long) tx_skb->data, (tx_skb->len - tx_skb->data_len));
	else
		RTL_CACHE_WBACK((unsigned long) tx_skb->data, tx_skb->len);
	}
#else
	RTL_CACHE_WBACK((unsigned long) tx_skb->data, tx_skb->len);
#endif
#endif //defined(CONFIG_RTD_1295_HWNAT)
	tx_retry_cnt = 0;
	while(1)
	{
		// do not suitable to use RTL_swNic_send
#if defined(CONFIG_RTD_1295_HWNAT)
#if defined(CONFIG_RTL_SWITCH_NEW_DESCRIPTOR)
		#ifdef CONFIG_RTL_TSO
		if( (skb_shinfo(tx_skb)->nr_frags>0) || (skb_shinfo(tx_skb)->gso_size>0) )
		{
			retval = _New_swNic_send_tso_sg((void *)tx_skb, (void *)mapping, tx_skb->len, &nicTx);
		}
		else
		{
			retval = _New_swNic_send((void *)tx_skb, (void *)mapping, tx_skb->len, &nicTx);
		}
		#else
		retval = _New_swNic_send((void *)tx_skb, (void *)mapping, tx_skb->len, &nicTx);
		#endif
#else
		#if defined(CONFIG_SMP)
		retval = _swNic_send((void *)tx_skb, (void *)mapping, tx_skb->len, &nicTx);
		#else
		retval = swNic_send((void *)tx_skb, (void *)mapping, tx_skb->len, &nicTx);
		#endif
#endif
#else //defined(CONFIG_RTD_1295_HWNAT)
#if defined(CONFIG_RTL_SWITCH_NEW_DESCRIPTOR)
		#ifdef CONFIG_RTL_TSO
		if( (skb_shinfo(tx_skb)->nr_frags>0) || (skb_shinfo(tx_skb)->gso_size>0) )
		{
			retval = New_swNic_send_tso_sg((void *)tx_skb, tx_skb->data, tx_skb->len, &nicTx);
		}
		else
		{
			retval = New_swNic_send((void *)tx_skb, tx_skb->data, tx_skb->len, &nicTx);
		}
		#else
		retval = New_swNic_send((void *)tx_skb, tx_skb->data, tx_skb->len, &nicTx);
		#endif
#else
		#if defined(CONFIG_SMP)
		retval = _swNic_send((void *)tx_skb, tx_skb->data, tx_skb->len, &nicTx);
		#else
		retval = swNic_send((void *)tx_skb, tx_skb->data, tx_skb->len, &nicTx);
		#endif
#endif
#endif //defined(CONFIG_RTD_1295_HWNAT)

		if (retval >= 0)
			break;

		#if defined(CONFIG_RTL_PROC_DEBUG)||defined(CONFIG_RTL_DEBUG_TOOL)
		tx_ringFull_cnt++;
		#endif
		#if !defined(CONFIG_RTD_1295_HWNAT)
		#if defined(CONFIG_SMP)
		SMP_UNLOCK_ETH_XMIT(flags);
		#endif

		RTL_swNic_txDone(nicTx.txIdx);

		#if defined(CONFIG_SMP)
		SMP_LOCK_ETH_XMIT(flags);
		#endif
		#endif //!defined(CONFIG_RTD_1295_HWNAT)
		if ((tx_retry_cnt++)>RTL_NIC_TX_RETRY_MAX) {
			#if defined(CONFIG_RTL_PROC_DEBUG)||defined(CONFIG_RTL_DEBUG_TOOL)
			tx_drop_cnt++;
			#endif
			dev_kfree_skb_any(tx_skb);
			#if defined(CONFIG_SMP)
			SMP_UNLOCK_ETH_XMIT(flags);
			#endif
			return 0;
		}
	}

	rtl_pstProcess_xmit(cp, tx_skb->dev, tx_skb->len);
	//cp->net_stats.tx_packets++;
	//cp->net_stats.tx_bytes += tx_skb->len;
	#if defined(CONFIG_SMP)
	SMP_UNLOCK_ETH_XMIT(flags);
	#endif

	return 0;
}


static void re865x_tx_timeout (struct net_device *dev)
{
	rtlglue_printf("Tx Timeout!!! Can't send packet\n");
}

#if defined(RTL819X_PRIV_IOCTL_ENABLE)
 int rtl819x_get_port_status(int portnum , struct lan_port_status *port_status)
{
		uint32	regData;
		uint32	data0;

		if( portnum < 0 ||  portnum > CPU)
			return -1;

		regData = READ_MEM32(PSRP0+((portnum)<<2));

		//printk("rtl819x_get_port_status port = %d data=%x\n", portnum,regData); //mark_debug
		data0 = regData & PortStatusLinkUp;
		if (data0)
			port_status->link =1;
		else
			port_status->link =0;

		data0 = regData & PortStatusNWayEnable;
		if (data0)
			port_status->nway=1;
		else
			port_status->nway =0;

		data0 = regData & PortStatusDuplex;
		if (data0)
			port_status->duplex=1;
		else
			port_status->duplex =0;

		data0 = (regData&PortStatusLinkSpeed_MASK)>>PortStatusLinkSpeed_OFFSET;
		port_status->speed = data0 ; // 0 = 10M , 1= 100M , 2=1G ,

		   return 0;
}

 int rtl819x_get_port_stats(int portnum , struct port_statistics *port_stats)
 {

	uint32 addrOffset_fromP0 =0;

	//printk("rtl819x_get_port_stats port = %d \n", portnum); //mark_debug
	if( portnum < 0 ||  portnum > CPU)
			return -1;

	addrOffset_fromP0 = portnum * MIB_ADDROFFSETBYPORT;

	//port_stats->rx_bytes =(uint32) (rtl865xC_returnAsicCounter64( OFFSET_IFINOCTETS_P0 + addrOffset_fromP0 )) ;
	port_stats->rx_bytes =rtl8651_returnAsicCounter( OFFSET_IFINOCTETS_P0 + addrOffset_fromP0 ) ;
	port_stats->rx_unipkts= rtl8651_returnAsicCounter( OFFSET_IFINUCASTPKTS_P0 + addrOffset_fromP0 ) ;
	port_stats->rx_mulpkts= rtl8651_returnAsicCounter( OFFSET_ETHERSTATSMULTICASTPKTS_P0 + addrOffset_fromP0 ) ;
	port_stats->rx_bropkts= rtl8651_returnAsicCounter( OFFSET_ETHERSTATSBROADCASTPKTS_P0 + addrOffset_fromP0 ) ;
	port_stats->rx_discard= rtl8651_returnAsicCounter( OFFSET_DOT1DTPPORTINDISCARDS_P0 + addrOffset_fromP0 ) ;
	port_stats->rx_error= (rtl8651_returnAsicCounter( OFFSET_DOT3STATSFCSERRORS_P0 + addrOffset_fromP0 ) +
						 rtl8651_returnAsicCounter( OFFSET_ETHERSTATSJABBERS_P0 + addrOffset_fromP0 ));

	//port_stats->tx_bytes =(uint32) (rtl865xC_returnAsicCounter64( OFFSET_IFOUTOCTETS_P0 + addrOffset_fromP0 )) ;
	port_stats->tx_bytes =rtl8651_returnAsicCounter( OFFSET_IFOUTOCTETS_P0 + addrOffset_fromP0 ) ;
	port_stats->tx_unipkts= rtl8651_returnAsicCounter( OFFSET_IFOUTUCASTPKTS_P0 + addrOffset_fromP0 ) ;
	port_stats->tx_mulpkts= rtl8651_returnAsicCounter( OFFSET_IFOUTMULTICASTPKTS_P0 + addrOffset_fromP0 ) ;
	port_stats->tx_bropkts= rtl8651_returnAsicCounter( OFFSET_IFOUTBROADCASTPKTS_P0 + addrOffset_fromP0 ) ;
	port_stats->tx_discard= rtl8651_returnAsicCounter( OFFSET_IFOUTDISCARDS + addrOffset_fromP0 ) ;
	port_stats->tx_error= (rtl8651_returnAsicCounter( OFFSET_ETHERSTATSCOLLISIONS_P0 + addrOffset_fromP0 ) +
						 rtl8651_returnAsicCounter( OFFSET_DOT3STATSDEFERREDTRANSMISSIONS_P0 + addrOffset_fromP0 ));

	return 0;
 }

int re865x_priv_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	int32 rc = 0;
	unsigned long *data_32;
	int portnum=0;
	struct lan_port_status port_status;
	struct port_statistics port_stats;

	data_32 = (unsigned long *)rq->ifr_data;
	if (copy_from_user(&portnum, data_32, 1*sizeof(unsigned long)))
	{
		return -EFAULT;
	}

	switch (cmd)
	{
		 case RTL819X_IOCTL_READ_PORT_STATUS:
		 	rc = rtl819x_get_port_status(portnum,&port_status); //portnumber
		 	if(rc != 0) {
				return -EFAULT;
			}
		 	if (copy_to_user((void *)rq->ifr_data, (void *)&port_status, sizeof(struct lan_port_status))) {
				return -EFAULT;
			}
		 	break;
		 case RTL819X_IOCTL_READ_PORT_STATS:
		 	rc = rtl819x_get_port_stats(portnum,&port_stats); //portnumber
		 	if(rc != 0)
				return -EFAULT;
		 	if (copy_to_user((void *)rq->ifr_data, (void *)&port_stats, sizeof(struct port_statistics)))
				return -EFAULT;
		 	break;
		 	break;
		 default :
		 	rc = -EOPNOTSUPP;
			break;
	}
	return SUCCESS;

}
#endif
#ifdef CONFIG_AUTO_DHCP_CHECK
#define MAX_LINK_STATUS_PID		8
struct link_status
{
	uint16 valid;
	int pid;
};
struct link_status link_status_pid[MAX_LINK_STATUS_PID];

int rtl_link_status_pid_init(void)
{
	int ret=0;
	int i=0;

	for(i=0;i<MAX_LINK_STATUS_PID;i++)
	{

		link_status_pid[i].valid=0;
		link_status_pid[i].pid=0;
	}


	return ret;
}

int rtl_set_linkstatus_pid(int pid)
{
	int ret=FAILED;
	int i=0;
	for(i=0;i<MAX_LINK_STATUS_PID;i++)
	{
		if(link_status_pid[i].valid==0)
		{
			link_status_pid[i].valid =1;
			link_status_pid[i].pid =pid;
			ret =SUCCESS;
			return ret;
		}
	}

	return ret;
}

void rtl_signal_linkdetect(void)
{

	struct task_struct *task;
	int i;
	int pid=0;
	int cnt=0;
	for(i=0;i<MAX_LINK_STATUS_PID;i++)
	{
		if(link_status_pid[i].valid)
		{
			pid =link_status_pid[i].pid;
			if(pid)
			{
				read_lock(&tasklist_lock);
				task = find_task_by_vpid(pid);
				read_unlock(&tasklist_lock);
				if(task)
				{
					send_sig(SIGUSR2,task,0);
				}
				else {
				   panic_printk("Path selection daemon pid: %d does not exist\n", pid);
				}
				cnt++;
			}
		}
	}

	if(cnt ==0)
		panic_printk("Path selection daemon pid: %d does not register\n", pid);

	return;
}


#ifdef CONFIG_RTL_PROC_NEW
static int rtk_linkstatus_pid_read(struct seq_file *s, void *v)
{
	seq_printf(s, "%s %d\n", "rtk_auto_dhcp_enable:",0);
	return 0;
}
#else
static int32 rtk_linkstatus_pid_read( char *page, char **start, off_t off, int count, int *eof, void *data )
{
	int len=0;
	int i=0;
	len += sprintf(page, "dump wan link status pid\n");
	for(i=0;i<MAX_LINK_STATUS_PID;i++)
	{

		if(link_status_pid[i].valid)
			len += sprintf(page+len, "[%d]:%d \n", i,link_status_pid[i].pid);

	}
	if (len <= off+count) *eof = 1;
	  *start = page + off;
	  len -= off;
	  if (len>count) len = count;
	  if (len<0) len = 0;
	  return len;


}
#endif
static int32 rtk_linkstatus_pid_write( struct file *filp, const char *buff,unsigned long len, void *data )
{
	char 		tmpbuf[64];
	char		*tokptr,*strptr;
	int index, valid, pid;

	if (buff && !copy_from_user(tmpbuf, buff, len))
	{
		tmpbuf[len] = '\0';
		strptr=tmpbuf;
		tokptr  = strsep(&strptr," ");
		if (tokptr != NULL)
		{
			index =simple_strtol(tokptr, NULL, 0);
			if (index<0 ||index>=MAX_LINK_STATUS_PID)
				goto errout;
			//if(!memcmp(tokptr,"eth1",4))
			{
				tokptr = strsep(&strptr," ");
				if (tokptr==NULL)
					goto errout;
				valid =simple_strtol(tokptr, NULL, 0);
				if(valid)
					valid =1;

				tokptr = strsep(&strptr," ");
				if (tokptr==NULL)
					goto errout;

				pid =simple_strtol(tokptr, NULL, 0);
				printk("%d, %d, %d,[%s]:[%d].\n",index, valid, pid,__FUNCTION__,__LINE__);
				link_status_pid[index].valid =valid;
				link_status_pid[index].pid =pid;

			}



		}

	}

	return len;
errout:
	printk("invaild parameter!\n");
	return -EFAULT;
}
#endif
int re865x_ioctl (struct net_device *dev, struct ifreq *rq, int cmd)
{
	int32 rc = 0;
	unsigned long *data;
	#if defined(CONFIG_RTD_1295_HWNAT)
	uintptr_t args[4];
	#else //defined(CONFIG_RTD_1295_HWNAT)
	int32 args[4];
	#endif //defined(CONFIG_RTD_1295_HWNAT)
	int32  * pRet;
	#if defined(CONFIG_RTL8186_KB)||defined(CONFIG_RTL8186_GR)
	uint32	*pU32;
	#endif

#ifdef SUPPORT_DHCP_PORT_IP_BIND
	uint8 client_mac[ETH_ALEN];
	uint8 *pargs=(uint8 *)&args[1];
	int i;
	uint16 port_id=0;
#endif

#if defined(CONFIG_RTL_8367R_SUPPORT) || defined(CONFIG_RTL_8370_SUPPORT)
	extern int rtk_port_phyStatus_get(uint32 port, uint32 *pLinkStatus, uint32 *pSpeed, uint32 *pDuplex);
	uint32 linkStatus, speed, duplex;
#endif

	if (cmd ==	RTL8651_IOCTL_CLEARBRSHORTCUTENTRY)
	{
		#ifdef BR_SHORTCUT
		cached_dev=NULL;
		cached_dev2=NULL;
		cached_dev3=NULL;
		cached_dev4=NULL;
		#endif
		return 0;
	}
	#if defined(CONFIG_RTL_ETH_802DOT1X_SUPPORT)
	rc = rtl_802dot1xIoctl(dev, rq, cmd);
	if (rc != -EOPNOTSUPP)
	{
		return rc;
	}
	#endif

	if (cmd != SIOCDEVPRIVATE)
	{
		#if defined(RTL819X_PRIV_IOCTL_ENABLE)
		rc = re865x_priv_ioctl(dev,rq,cmd);
		return rc;
		#else
		goto normal;
		#endif
	}

	data = (unsigned long *)rq->ifr_data;

	if (copy_from_user(args, data, 4*sizeof(unsigned long)))
	{
		return -EFAULT;
	}

	switch (args[0])
	{

#ifdef CONFIG_RTL8196_RTL8366
		case RTL8651_IOCTL_GETWANLINKSTATUS:
			{
				uint32 phyNum;
				uint32 linkStatus;

				pRet = (int32 *)args[3];
				*pRet = FAILED;
				rc = SUCCESS;

				phyNum=4;//8366 WAN port
				rc=rtl8366rb_getPHYLinkStatus(phyNum, &linkStatus);

				if(rc==SUCCESS)
				{
					if(linkStatus==1)
					{
						*pRet = SUCCESS;
					}
				}

				break;
			}
#elif defined(CONFIG_RTL_8367R_SUPPORT) || defined(CONFIG_RTL_8370_SUPPORT)
		case RTL8651_IOCTL_GETWANLINKSTATUS:
			{
				pRet = (int32 *)args[3];
				*pRet = FAILED;
				rc = SUCCESS;

				rc=rtk_port_phyStatus_get(RTL83XX_WAN, &linkStatus, &speed, &duplex);

				if(rc==SUCCESS)
				{
					if(linkStatus==1)
					{
						*pRet = SUCCESS;
					}
				}
				break;
			}
#else
		case RTL8651_IOCTL_GETWANLINKSTATUS:
			{
				int i;
				int wanPortMask;
				int32 totalVlans;

				pRet = (int32 *)args[3];
				*pRet = FAILED;
				rc = SUCCESS;

				wanPortMask = 0;
				totalVlans=((sizeof(vlanconfig))/(sizeof(struct rtl865x_vlanConfig)))-1;
				for(i=0;i<totalVlans;i++)
				{
					if(vlanconfig[i].isWan==TRUE)
						wanPortMask = vlanconfig[i].memPort;
				}
				if (wanPortMask==0)
				{
					/* no wan port exist */
					break;
				}

				for(i=0;i<RTL8651_AGGREGATOR_NUMBER;i++)
				{
					if( (1<<i)&wanPortMask )
					{
						if((READ_MEM32(PSRP0+(i<<2))&PortStatusLinkUp)!=0)
						{
							*pRet = SUCCESS;
						}
						break;
					}
				}
				break;
			}
#endif

#if defined(CONFIG_RTL_8367R_SUPPORT) || defined(CONFIG_RTL_8370_SUPPORT)
		case RTL8651_IOCTL_GETWANLINKSPEED:
			{
				pRet = (int32 *)args[3];
				*pRet = FAILED;

				rc=rtk_port_phyStatus_get(RTL83XX_WAN, &linkStatus, &speed, &duplex);

				if(rc==SUCCESS)
				{
					*pRet = speed;
				}
				break;
			}
#else
		case RTL8651_IOCTL_GETWANLINKSPEED:
			{
				int i;
				int wanPortMask;
				int32 totalVlans;

				pRet = (int32 *)args[3];
				*pRet = FAILED;
				rc = FAILED;

				wanPortMask = 0;
				totalVlans=((sizeof(vlanconfig))/(sizeof(struct rtl865x_vlanConfig)))-1;
				for(i=0;i<totalVlans;i++)
				{
					if(vlanconfig[i].isWan==TRUE)
						wanPortMask = vlanconfig[i].memPort;
				}

				if (wanPortMask==0)
				{
					/* no wan port exist */
					break;
				}

				for(i=0;i<RTL8651_AGGREGATOR_NUMBER;i++)
				{
					if( (1<<i)&wanPortMask )
					{
						break;
					}
				}

				switch(READ_MEM32(PSRP0 + (i<<2)) & PortStatusLinkSpeed_MASK)
				{
					case PortStatusLinkSpeed10M:
						*pRet = PortStatusLinkSpeed10M;
						rc = SUCCESS;
						break;
					case PortStatusLinkSpeed100M:
						*pRet = PortStatusLinkSpeed100M;
						rc = SUCCESS;
						break;
					case  PortStatusLinkSpeed1000M:
						*pRet = PortStatusLinkSpeed1000M;
						rc = SUCCESS;
						break;
					default:
						break;
				}
				break;
			}
#endif

#if defined(CONFIG_RTL8186_KB)|| defined(CONFIG_RTL8186_GR)
		case RTL8651_IOCTL_GETLANLINKSTATUS:
			{
				int i;
				int lanPortMask;
				int32 totalVlans;

				pRet = (int32 *)args[3];
				*pRet = FAILED;
				rc = SUCCESS;

				lanPortMask = 0;
				totalVlans=((sizeof(vlanconfig))/(sizeof(struct rtl865x_vlanConfig)))-1;
				for(i=0;i<totalVlans;i++)
				{
					if(vlanconfig[i].isWan==FALSE)
					{
						lanPortMask = vlanconfig[i].memPort;
						if (lanPortMask==0)
						{
							/* no wan port exist */
							continue;
						}

						for(i=0;i<=RTL8651_PHY_NUMBER;i++)
						{
							if( (1<<i)&lanPortMask )
							{
								if((READ_MEM32(PSRP0+(i<<2))&PortStatusLinkUp)!=0)
								{
									//rtlglue_printf("Lan port i=%d\n",i);//Added for test
									*pRet = SUCCESS;
									return rc;
								}
							}
						}
					}
				}

				break;
			}
#if defined(CONFIG_RTL8186_KB)
		case RTL8651_IOCTL_GETWANTHROUGHPUT:
			{
				static unsigned long last_jiffies = 0;
				static unsigned long last_rxtx = 0;
				int i;
				int32 totalVlans;
				struct dev_priv *cp;
				int32 *throughputLevel;
				unsigned long	diff_jiffies;
				pRet = (int32 *)args[3];

				diff_jiffies = (jiffies-last_jiffies);
				if (diff_jiffies>HZ)
				{
					pU32 = (uint32*)args[1];
					throughputLevel = (uint32*)pU32[0];
					rc = SUCCESS;

					cp = NULL;
					totalVlans=((sizeof(vlanconfig))/(sizeof(struct rtl865x_vlanConfig)))-1;
					for(i=0;i<totalVlans;i++)
					{
						if(vlanconfig[i].isWan==TRUE)
						{
							cp = NETDRV_PRIV(_rtl86xx_dev.dev[i]);
							break;
						}
					}

					if (cp==NULL)
					{
						/* no wan port exist */
						rc = FAILED;
					}

					for(i=1;i<20;i++)
					{
						if (diff_jiffies < (HZ<<i))
							break;
					}
					/* get the throughput level */
					*throughputLevel = (((cp->net_stats.rx_bytes + cp->net_stats.tx_bytes)-last_rxtx)>>(17+i));

					last_jiffies = jiffies;
					last_rxtx = (cp->net_stats.rx_bytes + cp->net_stats.tx_bytes);
					*pRet = SUCCESS;
				}
				else
				{
					*pRet = FAILED;
				}
			}
#endif
#endif
#ifdef CONFIG_RTL_LAYERED_DRIVER
#if defined(CONFIG_RTL8186_GR)
		case RTL8651_IOCTL_SETWANLINKSTATUS:
			{
				int i;
				int wanPortMask;
				int32 totalVlans;
				int portStatusToSet;
				int forceMode;
				int forceLink;
				int forceLinkSpeed;
				int forceDuplex;
				uint32 regValue;
				uint32 advCapability;
				#define SPEED10M 	0
				#define SPEED100M 	1
				#define SPEED1000M 	2

				pRet = (int32 *)args[3];
				*pRet = FAILED;
				rc = SUCCESS;

				pU32 = (uint32*)args[1];
				portStatusToSet = *(uint32*)pU32[0];

				wanPortMask = 0;
				totalVlans=((sizeof(vlanconfig))/(sizeof(struct rtl865x_vlanConfig)))-1;
				for(i=0;i<totalVlans;i++)
				{
					if(vlanconfig[i].isWan==TRUE)
						wanPortMask = vlanconfig[i].memPort;
				}

				if (wanPortMask==0)
				{
					/* no wan port exist */
					break;
				}

				for(i=0;i<RTL8651_AGGREGATOR_NUMBER;i++)
				{
					if( (1<<i)&wanPortMask )
					{
						/*write register*/

						if(HALF_DUPLEX_10M == portStatusToSet)
						{
							forceMode=TRUE;
							forceLink=TRUE;
							forceLinkSpeed=SPEED10M;
							forceDuplex=FALSE;
							advCapability=(1<<HALF_DUPLEX_10M);
						}else if(HALF_DUPLEX_100M == portStatusToSet)
						{
							forceMode=TRUE;
							forceLink=TRUE;
							forceLinkSpeed=SPEED100M;
							forceDuplex=FALSE;
							advCapability=(1<<HALF_DUPLEX_100M);
						}else if(HALF_DUPLEX_1000M == portStatusToSet)
						{
							forceMode=TRUE;
							forceLink=TRUE;
							forceLinkSpeed=SPEED1000M;
							forceDuplex=FALSE;
							advCapability=(1<<HALF_DUPLEX_1000M);
						}else if(DUPLEX_10M == portStatusToSet)
						{
							forceMode=TRUE;
							forceLink=TRUE;
							forceLinkSpeed=SPEED10M;
							forceDuplex=TRUE;
							advCapability=(1<<DUPLEX_10M);
						}else if(DUPLEX_100M == portStatusToSet)
						{
							forceMode=TRUE;
							forceLink=TRUE;
							forceLinkSpeed=SPEED100M;
							forceDuplex=TRUE;
							advCapability=(1<<DUPLEX_100M);
						}else if(DUPLEX_1000M == portStatusToSet)
						{
							forceMode=TRUE;
							forceLink=TRUE;
							forceLinkSpeed=SPEED1000M;
							forceDuplex=TRUE;
							advCapability=(1<<DUPLEX_1000M);
						}else if(PORT_AUTO == portStatusToSet)
						{
							forceMode=FALSE;
							forceLink=TRUE;
							/*all capality*/
							advCapability=(1<<PORT_AUTO);

						}else
						{
							forceMode=FALSE;
							forceLink=TRUE;
						}
						rtl865xC_setAsicEthernetForceModeRegs(i, forceMode, forceLink, forceLinkSpeed, forceDuplex);

						/*Set PHY Register*/
						rtl8651_setAsicEthernetPHYSpeed(i,forceLinkSpeed);
						rtl8651_setAsicEthernetPHYDuplex(i,forceDuplex);
						rtl8651_setAsicEthernetPHYAutoNeg(i,TRUE);
						rtl8651_setAsicEthernetPHYAdvCapality(i,advCapability);
						rtl8651_restartAsicEthernetPHYNway(i);
						break;
					}
				}

				break;
			}
		case RTL8651_IOCTL_GETLANPORTLINKSTATUS:
			{
				int i;
							int lanPortMask;
							int32 totalVlans;
				int32 *lanportnum;
				int32 lanPortTypeMask;
				uint32 regVal;
				uint32 portLinkSpeed;

							pRet = (int32 *)args[3];
							*pRet = FAILED;
							rc = SUCCESS;

				pU32 = (uint32*)args[1];
				lanportnum = (uint32*)pU32[0];

								lanPortMask = 0;
								totalVlans=((sizeof(vlanconfig))/(sizeof(struct rtl865x_vlanConfig)))-1;
								for(i=0;i<totalVlans;i++)
								{
										if(vlanconfig[i].isWan==FALSE)
										{
												lanPortMask = vlanconfig[i].memPort;
												if (lanPortMask==0)
												{
														/* no wan port exist */
														continue;
												}

												for(i=0;i<=RTL8651_PHY_NUMBER;i++)
												{
														if( (1<<i)&lanPortMask )
														{
															regVal=READ_MEM32(PSRP0+(i<<2));
																if((regVal&PortStatusLinkUp)!=0)
																{
										if(i==(*lanportnum))
										{
																			*pRet = SUCCESS;

											if((regVal&PortStatusDuplex)!=0)
											{
												lanPortTypeMask=1;
												*pRet |= lanPortTypeMask;
											}

											portLinkSpeed=regVal&PortStatusLinkSpeed_MASK;
											if(PortStatusLinkSpeed100M==portLinkSpeed)
											{
												lanPortTypeMask=4;
												*pRet |= lanPortTypeMask;
											}
											else if(PortStatusLinkSpeed1000M==portLinkSpeed)
											{
												lanPortTypeMask=8;
												*pRet |= lanPortTypeMask;
											}
											else
											{
												lanPortTypeMask=2;
												*pRet |= lanPortTypeMask;
											}

																			return rc;
										}
																}
														}
												}
										}
								}

								break;
			}

		case RTL8651_IOCTL_GETWANPORTLINKSTATUS:
			{
				int i;
				int wanPortMask;
				int32 totalVlans;
				int32 wanPortTypeMask;
				uint32 regVal;
				uint32 portLinkSpeed;

				pRet = (int32 *)args[3];
				*pRet = FAILED;
				rc = SUCCESS;

				wanPortMask = 0;
				totalVlans=((sizeof(vlanconfig))/(sizeof(struct rtl865x_vlanConfig)))-1;
				for(i=0;i<totalVlans;i++)
				{
					if(vlanconfig[i].isWan==TRUE)
						wanPortMask = vlanconfig[i].memPort;
				}

				if (wanPortMask==0)
				{
					/* no wan port exist */
					break;
				}

				for(i=0;i<RTL8651_AGGREGATOR_NUMBER;i++)
				{
					if( (1<<i)&wanPortMask )
					{
						/*check phy status link up or down*/
						rtl8651_getAsicEthernetPHYStatus(i,&regVal);
						if(regVal & (1<<2))
						{
							regVal=READ_MEM32(PSRP0+(i<<2));
							if((regVal&PortStatusLinkUp)!=0)
							{
								*pRet = SUCCESS;

								if((regVal&PortStatusDuplex)!=0)
								{
										wanPortTypeMask=1;
										*pRet |= wanPortTypeMask;
								}

								portLinkSpeed=regVal&PortStatusLinkSpeed_MASK;
								if(PortStatusLinkSpeed100M==portLinkSpeed)
								{
										wanPortTypeMask=4;
										*pRet |= wanPortTypeMask;
								}
								else if(PortStatusLinkSpeed1000M==portLinkSpeed)
								{
										wanPortTypeMask=8;
										*pRet |= wanPortTypeMask;
								}
								else
								{
										wanPortTypeMask=2;
										*pRet |= wanPortTypeMask;
								}
							}
						}
						break;
					}
				}

				break;
			}
#endif
#endif
			#ifdef CONFIG_AUTO_DHCP_CHECK
			case RTL8651_IOCTL_SET_GETLINKSTATUS_PID:
			{
				rc=rtl_set_linkstatus_pid(args[1]);
				break;

			}
	#endif
	#ifdef SUPPORT_DHCP_PORT_IP_BIND
		case RTL8651_IOCTL_GETPORTIDBYCLIENTMAC:
		{
			memcpy(client_mac, pargs, ETH_ALEN);
			for(i=0;i<RTL8651_PHY_NUMBER;i++)
			{
				if(memcmp(port_mac_table[i].client_mac, client_mac, ETH_ALEN)==0)
				{
					port_id=port_mac_table[i].port_id;
				break;
				}
			}
			if(port_id>0)
				copy_to_user(rq->ifr_data, &port_id,sizeof(port_id));
			break;
			}
	#endif
		default:
			rc = SUCCESS;
			break;
	}

	return rc;
#if !defined(RTL819X_PRIV_IOCTL_ENABLE)
normal:
#endif
	if (!netif_running(dev))
		return -EINVAL;
	switch (cmd)
		{
		default:
		rc = -EOPNOTSUPP;
		break;
	}
	return rc;
}

static int rtl865x_set_hwaddr(struct net_device *dev, void *addr)
{
	unsigned long flags=0;
	int i;
	unsigned char *p;
	ps_drv_netif_mapping_t * mapp_entry;
	struct rtl865x_vlanConfig *vlancfg_entry;

	p = ((struct sockaddr *)addr)->sa_data;
	SMP_LOCK_ETH(flags);

	for (i = 0; i<ETHER_ADDR_LEN; ++i) {
		dev->dev_addr[i] = p[i];
	}

	mapp_entry = rtl_get_ps_drv_netif_mapping_by_psdev(dev);
	if(mapp_entry == NULL)
		goto out;

	vlancfg_entry = rtl_get_vlanconfig_by_netif_name(mapp_entry->drvName);
	if(vlancfg_entry == NULL)
		goto out;

	if(vlancfg_entry->vid != 0)
	{
		rtl865x_netif_t netif;
		memcpy(vlancfg_entry->mac.octet,dev->dev_addr,ETHER_ADDR_LEN);
		memcpy(netif.macAddr.octet,vlancfg_entry->mac.octet,ETHER_ADDR_LEN);
		memcpy(netif.name,vlancfg_entry->ifname,MAX_IFNAMESIZE);
		rtl865x_setNetifMac(&netif);
	}

out:
	SMP_UNLOCK_ETH(flags);
	return SUCCESS;
}

#if defined(CONFIG_RTL8186_LINK_CHANGE)
static int rtl865x_set_link(struct net_device *dev, int enable)
{
	int32 i;
	struct dev_priv *cp;
	int32 portmask;
	int32 totalVlans;

	cp = NETDRV_PRIV(dev);
#if defined (CONFIG_RTL_MULTI_LAN_DEV)
	portmask=cp->portmask;
#else
	portmask=0;
	totalVlans=((sizeof(vlanconfig))/(sizeof(struct rtl865x_vlanConfig)))-1;

	for(i=0;i<totalVlans;i++)
	{
		if(vlanconfig[i].vid==cp->id)
		{
			portmask = vlanconfig[i].memPort;
			break;
		}
	}
#endif
	if (portmask)
	{
		if (enable)
		{
			for(i=0;i<RTL8651_PHY_NUMBER;i++)
			{
				if (portmask & (1<<i))
				{
					rtl865xC_setAsicEthernetForceModeRegs(i, FALSE, TRUE, 1, TRUE);
					rtl8651_restartAsicEthernetPHYNway(i);
				}
			}
		}
		else
		{
			for(i=0;i<RTL8651_PHY_NUMBER;i++)
			{
				if (portmask & (1<<i))
					rtl865xC_setAsicEthernetForceModeRegs(i, TRUE, FALSE, 1, TRUE);
			}
		}
	}

	return SUCCESS;
}
#endif

static int rtl865x_set_mtu(struct net_device *dev, int new_mtu)
{
	unsigned long flags=0;
	ps_drv_netif_mapping_t * mapp_entry;
	struct rtl865x_vlanConfig *vlancfg_entry;

	SMP_LOCK_ETH(flags);
	dev->mtu = new_mtu;

	mapp_entry = rtl_get_ps_drv_netif_mapping_by_psdev(dev);
	if(mapp_entry == NULL)
		goto out;

	vlancfg_entry = rtl_get_vlanconfig_by_netif_name(mapp_entry->drvName);
	if(vlancfg_entry == NULL)
		goto out;

	if(vlancfg_entry->vid !=0)
	{
		rtl865x_netif_t netif;
		vlancfg_entry->mtu = new_mtu;
		netif.mtu = new_mtu;
		#if defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)
		netif.mtuV6 = new_mtu;
		#endif
		memcpy(netif.name,vlancfg_entry->ifname,MAX_IFNAMESIZE);

		rtl865x_setNetifMtu(&netif);
	}
	#ifdef CONFIG_HARDWARE_NAT_DEBUG
	/*2007-12-19*/
		rtlglue_printf("%s:%d:new_mtu is %d\n",__FUNCTION__,__LINE__,new_mtu);
	#endif

out:
	SMP_UNLOCK_ETH(flags);

	return SUCCESS;
}

#if defined(CONFIG_COMPAT_NET_DEV_OPS)
#else
static const struct net_device_ops rtl819x_netdev_ops = {
	.ndo_open		= re865x_open,
	.ndo_stop		= re865x_close,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address 	= rtl865x_set_hwaddr,

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0)
	.ndo_set_multicast_list	= re865x_set_rx_mode,
#endif
	.ndo_get_stats		= re865x_get_stats,
	.ndo_do_ioctl		= re865x_ioctl,
	.ndo_start_xmit		= re865x_start_xmit,
	.ndo_tx_timeout		= re865x_tx_timeout,
#if defined(CP_VLAN_TAG_USED)
	.ndo_vlan_rx_register	= cp_vlan_rx_register,
#endif
	.ndo_change_mtu		= rtl865x_set_mtu,

};

#if !defined(CONFIG_COMPAT_NET_DEV_OPS) && defined(CONFIG_RTK_BRIDGE_VLAN_SUPPORT)
static const struct net_device_ops rtl819x_netdev_ops_bridge = {
	.ndo_open		= re865x_open,
	.ndo_stop		= re865x_close,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address 	= rtl865x_set_hwaddr,
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0)
	.ndo_set_multicast_list	= re865x_set_rx_mode,
#endif
	.ndo_get_stats		= re865x_get_stats,
	.ndo_do_ioctl		= re865x_ioctl,
	.ndo_start_xmit		= rtl_bridge_wan_start_xmit,
	.ndo_tx_timeout		= re865x_tx_timeout,
#if defined(CP_VLAN_TAG_USED)
	.ndo_vlan_rx_register	= cp_vlan_rx_register,
#endif
	.ndo_change_mtu		= rtl865x_set_mtu,

};
#endif

#endif

#if (defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)) && defined(CONFIG_RTL_IPV6READYLOGO)
static int rtl865x_addAclForIPV6ReadyLogo(struct rtl865x_vlanConfig* vlanConfig)
{
#if 1
	int i;
	#if defined (CONFIG_RTL_MULTI_LAN_DEV) || defined(CONFIG_RTK_VLAN_SUPPORT) || defined(CONFIG_RTL_VLAN_8021Q)
	static struct rtl865x_vlanConfig tmpVlanConfig[NETIF_NUMBER];
	#endif
	struct rtl865x_vlanConfig *pVlanConfig=NULL;

	rtl865x_AclRule_t	rule;
	int ret=FAILED;

	if(vlanConfig==NULL)
	{
		return FAILED;
	}

#if defined (CONFIG_RTL_MULTI_LAN_DEV) ||defined(CONFIG_RTK_VLAN_SUPPORT)  || defined(CONFIG_RTL_VLAN_8021Q)
	re865x_packVlanConfig(vlanConfig, tmpVlanConfig);
	pVlanConfig=tmpVlanConfig;
#else
	pVlanConfig=vlanConfig;
#endif

	for(i=0; pVlanConfig[i].vid != 0; i++)
	{
		if (IF_ETHER!=pVlanConfig[i].if_type)
		{
			continue;
		}


		if(pVlanConfig[i].isWan==0)/*lan config*/
		{
			rtl865x_regist_aclChain(pVlanConfig[i].ifname, RTL865X_ACL_IPV6_USED, RTL865X_ACL_INGRESS);
			bzero((void*)&rule,sizeof(rtl865x_AclRule_t));
			rule.ruleType_ 		= RTL865X_ACL_IPV6;
			rule.actionType_		= RTL865X_ACL_TOCPU;
			rule.pktOpApp_		= RTL865X_ACL_ALL_LAYER;
			rule.ipv6EntryType_ = 1;
			/*::1/128*/
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[0] =0x00;
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[1] =0x00;
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[2] =0x00;
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[3] =0x00;
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[4] =0x00;
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[5] =0x00;
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[6] =0x00;
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[7] =ntohs(0x01);
			ntohl_array((u32 *)&rule.un_ty.L3V6._dstIpV6Addr.v6_addr32[0], (u32 *)&rule.un_ty.L3V6._dstIpV6Addr.v6_addr32[0], 4);

			rule.un_ty.L3V6._dstIpV6AddrMask.inv6_u.u6_addr32[0] = 0xffffffff;
			rule.un_ty.L3V6._dstIpV6AddrMask.inv6_u.u6_addr32[1] = 0xffffffff;
			rule.un_ty.L3V6._dstIpV6AddrMask.inv6_u.u6_addr32[2] = 0xffffffff;
			rule.un_ty.L3V6._dstIpV6AddrMask.inv6_u.u6_addr32[3] = 0xffffffff;

			ret= rtl865x_add_acl(&rule, pVlanConfig[i].ifname, RTL865X_ACL_IPV6_USED);

			rule.ipv6EntryType_ = 0;
			ret= rtl865x_add_acl(&rule, pVlanConfig[i].ifname, RTL865X_ACL_IPV6_USED);

#if 0
			bzero((void*)&rule,sizeof(rtl865x_AclRule_t));
			rule.ruleType_ 		= RTL865X_ACL_IPV6;
			rule.actionType_		= RTL865X_ACL_TOCPU;
			rule.pktOpApp_		= RTL865X_ACL_ALL_LAYER;
			rule.ipv6EntryType_ = 1;
			/*3ffe:501:ffff:0:200:ff:fe00:100/128*/
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[0] =0x3ffe;
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[1] =0x0501;
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[2] =0xffff;
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[3] =0x0;
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[4] =0x200;
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[5] =0x00ff;
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[6] =0xfe00;
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[7] =0x100;

			rule.un_ty.L3V6._dstIpV6AddrMask.inv6_u.u6_addr32[0] = 0xffffffff;
			rule.un_ty.L3V6._dstIpV6AddrMask.inv6_u.u6_addr32[1] = 0xffffffff;
			rule.un_ty.L3V6._dstIpV6AddrMask.inv6_u.u6_addr32[2] = 0xffffffff;
			rule.un_ty.L3V6._dstIpV6AddrMask.inv6_u.u6_addr32[3] = 0xffffffff;

			ret= rtl865x_add_acl(&rule, pVlanConfig[i].ifname, RTL865X_ACL_IPV6_USED);

			rule.ipv6EntryType_ = 0;
			ret= rtl865x_add_acl(&rule, pVlanConfig[i].ifname, RTL865X_ACL_IPV6_USED);
#endif
		}
		else/*wan config*/
		{
			rtl865x_regist_aclChain(pVlanConfig[i].ifname, RTL865X_ACL_IPV6_USED, RTL865X_ACL_INGRESS);
			bzero((void*)&rule,sizeof(rtl865x_AclRule_t));
			rule.ruleType_ = RTL865X_ACL_IPV6;
			rule.actionType_		= RTL865X_ACL_TOCPU;
			rule.pktOpApp_		= RTL865X_ACL_ALL_LAYER;
			rule.ipv6EntryType_ = 1;
			/*fe80::3a83:45ff:fef3:974*/
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[0] =0x00;
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[1] =0x00;
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[2] =0x00;
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[3] =0x00;
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[4] =0x00;
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[5] =0x00;
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[6] =0x00;
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[7] =ntohs(0x01);
			ntohl_array((u32 *)&rule.un_ty.L3V6._dstIpV6Addr.v6_addr32[0], (u32 *)&rule.un_ty.L3V6._dstIpV6Addr.v6_addr32[0], 4);

			rule.un_ty.L3V6._dstIpV6AddrMask.inv6_u.u6_addr32[0] = 0xffffffff;
			rule.un_ty.L3V6._dstIpV6AddrMask.inv6_u.u6_addr32[1] = 0xffffffff;
			rule.un_ty.L3V6._dstIpV6AddrMask.inv6_u.u6_addr32[2] = 0xffffffff;
			rule.un_ty.L3V6._dstIpV6AddrMask.inv6_u.u6_addr32[3] = 0xffffffff;

			ret= rtl865x_add_acl(&rule, pVlanConfig[i].ifname, RTL865X_ACL_IPV6_USED);

			rule.ipv6EntryType_ = 0;
			ret= rtl865x_add_acl(&rule, pVlanConfig[i].ifname, RTL865X_ACL_IPV6_USED);

			bzero((void*)&rule,sizeof(rtl865x_AclRule_t));
			rule.ruleType_ = RTL865X_ACL_IPV6;
			rule.actionType_		= RTL865X_ACL_TOCPU;
			rule.pktOpApp_		= RTL865X_ACL_ALL_LAYER;
			rule.ipv6EntryType_ = 1;
			/*fe80::/64*/
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[0] =ntohs(0xfe80);
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[1] =0x00;
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[2] =0x00;
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[3] =0x00;
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[4] =0x00;
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[5] =0x00;
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[6] =0x00;
			rule.un_ty.L3V6._dstIpV6Addr.v6_addr16[7] =0x00;
			ntohl_array((u32 *)&rule.un_ty.L3V6._dstIpV6Addr.v6_addr32[0], (u32 *)&rule.un_ty.L3V6._dstIpV6Addr.v6_addr32[0], 4);

			rule.un_ty.L3V6._dstIpV6AddrMask.inv6_u.u6_addr32[0] = 0xffffffff;
			rule.un_ty.L3V6._dstIpV6AddrMask.inv6_u.u6_addr32[1] = 0xffffffff;
			rule.un_ty.L3V6._dstIpV6AddrMask.inv6_u.u6_addr32[2] = 0x0;
			rule.un_ty.L3V6._dstIpV6AddrMask.inv6_u.u6_addr32[3] = 0x0;

			ret= rtl865x_add_acl(&rule, pVlanConfig[i].ifname, RTL865X_ACL_IPV6_USED);

			rule.ipv6EntryType_ = 0;
			ret= rtl865x_add_acl(&rule, pVlanConfig[i].ifname, RTL865X_ACL_IPV6_USED);
		}
	}
#endif
	return SUCCESS;
}
#endif
#if  defined (CONFIG_RTL_HARDWARE_MULTICAST)
static int rtl865x_addAclForIgmpSnooping(struct rtl865x_vlanConfig* vlanConfig)
{
#if  defined (CONFIG_RTL_HARDWARE_MULTICAST)

	int i;
	#if defined (CONFIG_RTL_MULTI_LAN_DEV) || defined(CONFIG_RTK_VLAN_SUPPORT)
	static struct rtl865x_vlanConfig tmpVlanConfig[NETIF_NUMBER];
	#endif
	struct rtl865x_vlanConfig *pVlanConfig=NULL;


	rtl865x_AclRule_t	rule;
	int ret=FAILED;

	if(vlanConfig==NULL)
	{
		return FAILED;
	}

#if defined (CONFIG_RTL_MULTI_LAN_DEV) ||defined(CONFIG_RTK_VLAN_SUPPORT)
	re865x_packVlanConfig(vlanConfig, tmpVlanConfig);
	pVlanConfig=tmpVlanConfig;
#else
	pVlanConfig=vlanConfig;
#endif

	for(i=0; pVlanConfig[i].vid != 0; i++)
	{
		if (IF_ETHER!=pVlanConfig[i].if_type)
		{
			continue;
		}


		if(pVlanConfig[i].isWan==0)/*lan config*/
		{

			//trap igmp packet to cpu enable hw multicast
			rtl865x_regist_aclChain(pVlanConfig[i].ifname, RTL865X_ACL_IGMP_USED, RTL865X_ACL_INGRESS);
			bzero((void*)&rule,sizeof(rtl865x_AclRule_t));
			rule.ruleType_ = RTL865X_ACL_IP;
			rule.actionType_		= RTL865X_ACL_TOCPU;
			rule.pktOpApp_ 		= RTL865X_ACL_ALL_LAYER;
			rule.ipProto_=IPPROTO_IGMP;
			rule.ipProtoMask_ =0xff;

			ret= rtl865x_add_acl(&rule, pVlanConfig[i].ifname, RTL865X_ACL_IGMP_USED);

		}
		else/*wan config*/
		{

			rtl865x_regist_aclChain(pVlanConfig[i].ifname, RTL865X_ACL_IGMP_USED, RTL865X_ACL_INGRESS);
			bzero((void*)&rule,sizeof(rtl865x_AclRule_t));
			rule.ruleType_ = RTL865X_ACL_IP;
			rule.actionType_		= RTL865X_ACL_TOCPU;
			rule.pktOpApp_ 		= RTL865X_ACL_ALL_LAYER;
			rule.ipProto_=IPPROTO_IGMP;
			rule.ipProtoMask_ =0xff;
			ret= rtl865x_add_acl(&rule, pVlanConfig[i].ifname, RTL865X_ACL_IGMP_USED);

		}

		#if defined(CONFIG_RTL_IPTABLES_RULE_2_ACL)
		if(rtl865x_curOpMode == WISP_MODE)
		#endif
			rtl865x_reConfigDefaultAcl(pVlanConfig[i].ifname);


	}
#endif
	return SUCCESS;
}

static int rtl865x_removeAclForIgmpSnooping(struct rtl865x_vlanConfig* vlanConfig)
{
#if defined (CONFIG_RTL_HARDWARE_MULTICAST)
	int i;
	#if defined (CONFIG_RTL_MULTI_LAN_DEV) ||defined(CONFIG_RTK_VLAN_SUPPORT)
	struct rtl865x_vlanConfig tmpVlanConfig[NETIF_NUMBER];
	#endif

	struct rtl865x_vlanConfig *pVlanConfig=NULL;

	if(vlanConfig==NULL)
	{
		return FAILED;
	}
#if defined (CONFIG_RTL_MULTI_LAN_DEV) ||defined(CONFIG_RTK_VLAN_SUPPORT)
	re865x_packVlanConfig(vlanConfig, tmpVlanConfig);
	pVlanConfig=tmpVlanConfig;
#else
	pVlanConfig=vlanConfig;
#endif

	for(i=0; pVlanConfig[i].vid != 0; i++)
	{
		if (IF_ETHER!=pVlanConfig[i].if_type)
		{
			continue;
		}

		if(pVlanConfig[i].isWan==0)/*lan config*/
		{
			rtl865x_unRegist_aclChain(pVlanConfig[i].ifname, RTL865X_ACL_IGMP_USED, RTL865X_ACL_INGRESS);
		}
		else/*wan config*/
		{
			rtl865x_unRegist_aclChain(pVlanConfig[i].ifname, RTL865X_ACL_IGMP_USED, RTL865X_ACL_INGRESS);
		}

	}


#if defined (CONFIG_RTL_IPTABLES_RULE_2_ACL)

#else
#if defined (CONFIG_RTK_VLAN_SUPPORT)
		if(rtk_vlan_support_enable==0)
		{
			rtl865x_setDefACLForAllNetif(RTL865X_ACLTBL_PERMIT_ALL, RTL865X_ACLTBL_PERMIT_ALL, RTL865X_ACLTBL_PERMIT_ALL, RTL865X_ACLTBL_PERMIT_ALL);
		}
		else
		{
			rtl865x_setDefACLForAllNetif(RTL865X_ACLTBL_ALL_TO_CPU, RTL865X_ACLTBL_ALL_TO_CPU, RTL865X_ACLTBL_PERMIT_ALL, RTL865X_ACLTBL_PERMIT_ALL);
		}
#else
		rtl865x_setDefACLForAllNetif(RTL865X_ACLTBL_PERMIT_ALL, RTL865X_ACLTBL_PERMIT_ALL, RTL865X_ACLTBL_PERMIT_ALL, RTL865X_ACLTBL_PERMIT_ALL);
#endif
#endif
#endif

	return SUCCESS;
}


int rtl_processAclForIgmpSnooping(int aclEnabled)
{
	int ret;
	if(aclEnabled)
	{
		#if defined (CONFIG_RTL_MULTI_LAN_DEV)
		ret=rtl865x_addAclForIgmpSnooping(packedVlanConfig);
		#else
		 ret=rtl865x_addAclForIgmpSnooping(vlanconfig);
		#endif
	}
	else
	{
		#if defined (CONFIG_RTL_MULTI_LAN_DEV)
		ret=rtl865x_removeAclForIgmpSnooping(packedVlanConfig);
		#else
		ret=rtl865x_removeAclForIgmpSnooping(vlanconfig);
		#endif
	}

	return ret;
}

#endif
#if defined(CONFIG_RTL_INBAND_CTL_ACL)
static int rtl865x_addAclForInbandCtl(struct rtl865x_vlanConfig* vlanConfig)
{
	int i;
	#if defined (CONFIG_RTL_MULTI_LAN_DEV) || defined(CONFIG_RTK_VLAN_SUPPORT) || defined(CONFIG_RTL_VLAN_8021Q)
	struct rtl865x_vlanConfig tmpVlanConfig[NETIF_NUMBER];
	#endif
	struct rtl865x_vlanConfig *pVlanConfig=NULL;
	rtl865x_AclRule_t	rule;
	int ret=FAILED;

	if(vlanConfig==NULL)
	{
		return FAILED;
	}

#if defined (CONFIG_RTL_MULTI_LAN_DEV) ||defined(CONFIG_RTK_VLAN_SUPPORT) || defined(CONFIG_RTL_VLAN_8021Q)
	re865x_packVlanConfig(vlanConfig, tmpVlanConfig);
	pVlanConfig=tmpVlanConfig;
#else
	pVlanConfig=vlanConfig;
#endif

	for(i=0; pVlanConfig[i].vid != 0; i++)
	{
		if (IF_ETHER!=pVlanConfig[i].if_type)
		{
			continue;
		}


		if(pVlanConfig[i].isWan==0)/*lan config*/
		{
			rtl865x_regist_aclChain(pVlanConfig[i].ifname, RTL865X_ACL_TEST_USED, RTL865X_ACL_INGRESS);
			/*drop all packet*/
			bzero((void*)&rule,sizeof(rtl865x_AclRule_t));
			rule.ruleType_ = RTL865X_ACL_MAC;
			rule.actionType_		= RTL865X_ACL_TOCPU;
			rule.pktOpApp_ 		= RTL865X_ACL_ALL_LAYER;

			rule.typeLen_=0x8899;
			rule.typeLenMask_=0xFFFF;

			ret= rtl865x_add_acl(&rule, pVlanConfig[i].ifname, RTL865X_ACL_TEST_USED);


		}
		else/*wan config*/
		{
			rtl865x_regist_aclChain(pVlanConfig[i].ifname, RTL865X_ACL_TEST_USED, RTL865X_ACL_INGRESS);
			/*drop all packet*/
			bzero((void*)&rule,sizeof(rtl865x_AclRule_t));
			rule.ruleType_ = RTL865X_ACL_MAC;
			rule.actionType_		= RTL865X_ACL_TOCPU;
			rule.pktOpApp_ 		= RTL865X_ACL_ALL_LAYER;
			rule.typeLen_=0x8899;
			rule.typeLenMask_=0xFFFF;

			ret= rtl865x_add_acl(&rule, pVlanConfig[i].ifname, RTL865X_ACL_TEST_USED);
		}

		#if defined(CONFIG_RTL_IPTABLES_RULE_2_ACL)
		if(rtl865x_curOpMode == WISP_MODE)
		#endif
			rtl865x_reConfigDefaultAcl(pVlanConfig[i].ifname);


	}
	return SUCCESS;
}

static int rtl865x_removeAclForInbandCtl(struct rtl865x_vlanConfig* vlanConfig)
{
	return 0;
}

#endif

#if defined (CONFIG_RTL_HTTP_REDIRECT_LOCAL)
static int rtl865x_addAclForHttpRedirect(struct rtl865x_vlanConfig* vlanConfig)
{
	int i;
	#if defined (CONFIG_RTL_MULTI_LAN_DEV) || defined(CONFIG_RTK_VLAN_SUPPORT) || defined(CONFIG_RTL_VLAN_8021Q)
	struct rtl865x_vlanConfig tmpVlanConfig[NETIF_NUMBER];
	#endif
	struct rtl865x_vlanConfig *pVlanConfig=NULL;
	rtl865x_AclRule_t	rule;
	int ret=FAILED;
	if(vlanConfig==NULL)
	{
		return FAILED;
	}

#if defined (CONFIG_RTL_MULTI_LAN_DEV) ||defined(CONFIG_RTK_VLAN_SUPPORT) || defined(CONFIG_RTL_VLAN_8021Q)
	re865x_packVlanConfig(vlanConfig, tmpVlanConfig);
	pVlanConfig=tmpVlanConfig;
#else
	pVlanConfig=vlanConfig;
#endif

	for(i=0; pVlanConfig[i].vid != 0; i++)
	{
		if (IF_ETHER!=pVlanConfig[i].if_type)
		{
			continue;
		}
		if(pVlanConfig[i].isWan==0)/*lan config*/
		{
			rtl865x_regist_aclChain(pVlanConfig[i].ifname, RTL865X_ACL_HTTP_REDIRECT_USED, RTL865X_ACL_INGRESS);
			/*to cpu*/
			bzero((void*)&rule,sizeof(rtl865x_AclRule_t));
			rule.ruleType_ = RTL865X_ACL_TCP;
			rule.actionType_		= RTL865X_ACL_TOCPU;
			rule.pktOpApp_		= RTL865X_ACL_ALL_LAYER;
			rule.tcpDstPortUB_ = 80;
			rule.tcpDstPortLB_ = 80;
			rule.tcpSrcPortUB_ = 65535;
			rule.tcpSrcPortLB_ = 0;
			ret= rtl865x_add_acl(&rule, pVlanConfig[i].ifname, RTL865X_ACL_HTTP_REDIRECT_USED);
		}

		#if defined(CONFIG_RTL_IPTABLES_RULE_2_ACL)
		if(rtl865x_curOpMode == WISP_MODE)
		#endif
			rtl865x_reConfigDefaultAcl(pVlanConfig[i].ifname);
	}
	return SUCCESS;
}
static int rtl865x_removeAclForHttpRedirect(struct rtl865x_vlanConfig* vlanConfig)
{
	int i;
	#if defined (CONFIG_RTL_MULTI_LAN_DEV) ||defined(CONFIG_RTK_VLAN_SUPPORT) || defined(CONFIG_RTL_VLAN_8021Q)
	struct rtl865x_vlanConfig tmpVlanConfig[NETIF_NUMBER];
	#endif

	struct rtl865x_vlanConfig *pVlanConfig=NULL;

	if(vlanConfig==NULL)
	{
		return FAILED;
	}
#if defined (CONFIG_RTL_MULTI_LAN_DEV) ||defined(CONFIG_RTK_VLAN_SUPPORT) || defined(CONFIG_RTL_VLAN_8021Q)
	re865x_packVlanConfig(vlanConfig, tmpVlanConfig);
	pVlanConfig=tmpVlanConfig;
#else
	pVlanConfig=vlanConfig;
#endif

	for(i=0; pVlanConfig[i].vid != 0; i++)
	{
		if (IF_ETHER!=pVlanConfig[i].if_type)
		{
			continue;
		}

		if(pVlanConfig[i].isWan==0)/*lan config*/
		{
			rtl865x_unRegist_aclChain(pVlanConfig[i].ifname, RTL865X_ACL_HTTP_REDIRECT_USED, RTL865X_ACL_INGRESS);
		}
	}
	return SUCCESS;
}

void set_http_redirect_acl(int enable)
{
	if(enable){
		rtl865x_addAclForHttpRedirect(packedVlanConfig);
	}
	else{
		rtl865x_removeAclForHttpRedirect(packedVlanConfig);
	}
}
#endif

#if defined (CONFIG_RTL_MLD_SNOOPING)
static int rtl865x_addAclForMldSnooping(struct rtl865x_vlanConfig* vlanConfig)
{
	int i;
	#if defined (CONFIG_RTL_MULTI_LAN_DEV) || defined(CONFIG_RTK_VLAN_SUPPORT) || defined(CONFIG_RTL_VLAN_8021Q)
	static struct rtl865x_vlanConfig tmpVlanConfig[NETIF_NUMBER];
	#endif
	struct rtl865x_vlanConfig *pVlanConfig=NULL;

#if !((defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)) && defined(CONFIG_RTL_HARDWARE_MULTICAST))
	rtl865x_AclRule_t	rule;
	int ret=FAILED;
#endif
	if(vlanConfig==NULL)
	{
		return FAILED;
	}

#if defined (CONFIG_RTL_MULTI_LAN_DEV) ||defined(CONFIG_RTK_VLAN_SUPPORT) || defined(CONFIG_RTL_VLAN_8021Q)
	re865x_packVlanConfig(vlanConfig, tmpVlanConfig);
	pVlanConfig=tmpVlanConfig;
#else
	pVlanConfig=vlanConfig;
#endif

	for(i=0; pVlanConfig[i].vid != 0; i++)
	{
		if (IF_ETHER!=pVlanConfig[i].if_type)
		{
			continue;
		}

		if(pVlanConfig[i].isWan==0)/*lan config*/
		{
#if !((defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)) && defined(CONFIG_RTL_HARDWARE_MULTICAST))
	//no need acl to trap ipv6 multicast packet to cpu enabel 8198C v6 hw multicast
			rtl865x_regist_aclChain(pVlanConfig[i].ifname, RTL865X_ACL_IPV6_USED, RTL865X_ACL_INGRESS);
			/*ping6 issue*/
			bzero((void*)&rule,sizeof(rtl865x_AclRule_t));
			rule.ruleType_ = RTL865X_ACL_MAC;
			rule.actionType_		= RTL865X_ACL_PERMIT;
			rule.pktOpApp_ 		= RTL865X_ACL_ALL_LAYER;
			rule.dstMac_.octet[0]=0x33;
			rule.dstMac_.octet[1]=0x33;
			rule.dstMac_.octet[2]=0xFF;

			rule.dstMacMask_.octet[0]=0xFF;
			rule.dstMacMask_.octet[1]=0xFF;
			rule.dstMacMask_.octet[2]=0xFF;

			ret= rtl865x_add_acl(&rule, pVlanConfig[i].ifname, RTL865X_ACL_IPV6_USED);

			/*ipv6 multicast data issue*/
			bzero((void*)&rule,sizeof(rtl865x_AclRule_t));

			rule.ruleType_ = RTL865X_ACL_MAC;
			rule.actionType_		= RTL865X_ACL_TOCPU;
			rule.pktOpApp_ 		= RTL865X_ACL_ALL_LAYER;
			rule.dstMac_.octet[0]=0x33;
			rule.dstMac_.octet[1]=0x33;
			rule.dstMac_.octet[2]=0x00;
			rule.dstMac_.octet[3]=0x00;
			rule.dstMac_.octet[4]=0x00;
			rule.dstMac_.octet[5]=0x00;

			rule.dstMacMask_.octet[0]=0xFF;
			rule.dstMacMask_.octet[1]=0xFF;
			ret= rtl865x_add_acl(&rule, pVlanConfig[i].ifname, RTL865X_ACL_IPV6_USED);
#endif

		}
		else/*wan config*/
		{
#if !((defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)) && defined(CONFIG_RTL_HARDWARE_MULTICAST))

			rtl865x_regist_aclChain(pVlanConfig[i].ifname, RTL865X_ACL_IPV6_USED, RTL865X_ACL_INGRESS);
			/*ipv6 multicast data issue*/
			bzero((void*)&rule,sizeof(rtl865x_AclRule_t));
			rule.ruleType_ = RTL865X_ACL_MAC;
			rule.actionType_		= RTL865X_ACL_TOCPU;
			rule.pktOpApp_ 		= RTL865X_ACL_ALL_LAYER;
			rule.dstMac_.octet[0]=0x33;
			rule.dstMac_.octet[1]=0x33;
			rule.dstMac_.octet[2]=0x00;
			rule.dstMac_.octet[3]=0x00;
			rule.dstMac_.octet[4]=0x00;
			rule.dstMac_.octet[5]=0x00;

			rule.dstMacMask_.octet[0]=0xFF;
			rule.dstMacMask_.octet[1]=0xFF;

			ret= rtl865x_add_acl(&rule, pVlanConfig[i].ifname, RTL865X_ACL_IPV6_USED);
#endif
		}

		#if defined(CONFIG_RTL_IPTABLES_RULE_2_ACL)
		if(rtl865x_curOpMode == WISP_MODE)
		#endif
			rtl865x_reConfigDefaultAcl(pVlanConfig[i].ifname);
	}
	return SUCCESS;
}

static int rtl865x_removeAclForMldSnooping(struct rtl865x_vlanConfig* vlanConfig)
{
#if (defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)) && defined(CONFIG_RTL_HARDWARE_MULTICAST)

		//no need acl to trap ipv6 multicast packet to cpu
#else

	int i;
	#if defined (CONFIG_RTL_MULTI_LAN_DEV) ||defined(CONFIG_RTK_VLAN_SUPPORT) || defined(CONFIG_RTL_VLAN_8021Q)
	struct rtl865x_vlanConfig tmpVlanConfig[NETIF_NUMBER];
	#endif

	struct rtl865x_vlanConfig *pVlanConfig=NULL;

	if(vlanConfig==NULL)
	{
		return FAILED;
	}
#if defined (CONFIG_RTL_MULTI_LAN_DEV) ||defined(CONFIG_RTK_VLAN_SUPPORT) || defined(CONFIG_RTL_VLAN_8021Q)
	re865x_packVlanConfig(vlanConfig, tmpVlanConfig);
	pVlanConfig=tmpVlanConfig;
#else
	pVlanConfig=vlanConfig;
#endif

	for(i=0; pVlanConfig[i].vid != 0; i++)
	{
		if (IF_ETHER!=pVlanConfig[i].if_type)
		{
			continue;
		}

		if(pVlanConfig[i].isWan==0)/*lan config*/
		{
			rtl865x_unRegist_aclChain(pVlanConfig[i].ifname, RTL865X_ACL_IPV6_USED, RTL865X_ACL_INGRESS);
		}
		else/*wan config*/
		{
			rtl865x_unRegist_aclChain(pVlanConfig[i].ifname, RTL865X_ACL_IPV6_USED, RTL865X_ACL_INGRESS);
		}

	}
#endif

#if defined (CONFIG_RTL_IPTABLES_RULE_2_ACL)

#else
#if defined (CONFIG_RTK_VLAN_SUPPORT)
		if(rtk_vlan_support_enable==0)
		{
			rtl865x_setDefACLForAllNetif(RTL865X_ACLTBL_PERMIT_ALL, RTL865X_ACLTBL_PERMIT_ALL, RTL865X_ACLTBL_PERMIT_ALL, RTL865X_ACLTBL_PERMIT_ALL);
		}
		else
		{
			rtl865x_setDefACLForAllNetif(RTL865X_ACLTBL_ALL_TO_CPU, RTL865X_ACLTBL_ALL_TO_CPU, RTL865X_ACLTBL_PERMIT_ALL, RTL865X_ACLTBL_PERMIT_ALL);
		}
#else
		rtl865x_setDefACLForAllNetif(RTL865X_ACLTBL_PERMIT_ALL, RTL865X_ACLTBL_PERMIT_ALL, RTL865X_ACLTBL_PERMIT_ALL, RTL865X_ACLTBL_PERMIT_ALL);
#endif
#endif

	return SUCCESS;
}
#endif


#if !defined(CONFIG_COMPAT_NET_DEV_OPS) && (defined(CONFIG_RTL_CUSTOM_PASSTHRU) || defined(CONFIG_RTL_STP))
static const struct net_device_ops rtl819x_pseudodev_ops = {
	.ndo_open		= re865x_pseudo_open,
	.ndo_stop		= re865x_pseudo_close,

	.ndo_get_stats	= re865x_get_stats,
	.ndo_do_ioctl		= re865x_ioctl,
	.ndo_start_xmit	= re865x_start_xmit,
};
#endif

#ifdef CONFIG_RTL_PROC_NEW
extern struct proc_dir_entry proc_root;

#if defined(CONFIG_RTK_VLAN_SUPPORT)
/*mib_vlan*/
int mib_vlan_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, read_proc_vlan, PDE_DATA(inode)));
}
static ssize_t mib_vlan_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return write_proc_vlan(file, userbuf,count, off);
}
struct file_operations mib_vlan_proc_fops= {
		.open           = mib_vlan_single_open,
		.write		    = mib_vlan_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};
#endif

/*up_event*/
int up_event_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, rtk_link_event_read, NULL));
}
static ssize_t up_event_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return rtk_link_event_write(file, userbuf,count, off);
}
struct file_operations up_event_proc_fops= {
		.open           = up_event_single_open,
		.write		    = up_event_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};

/*link_status*/
int link_status_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, rtk_link_status_read, PDE_DATA(inode)));
}
static ssize_t link_status_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return rtk_link_status_write(file, userbuf,count, off);
}
struct file_operations link_status_proc_fops= {
		.open           = link_status_single_open,
		.write		    = link_status_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};


#ifdef CONFIG_AUTO_DHCP_CHECK
#ifdef CONFIG_RTL_PROC_NEW
int auto_dhcp_single_open(struct inode *inode, struct file *file)
{
	return(single_open(file, rtk_linkstatus_pid_read,NULL));
}

static ssize_t auto_dhcp_single_write(struct file * file, const char __user * userbuf, size_t count, loff_t * off)
{
	return rtk_linkstatus_pid_write(file, userbuf,count, off);
}

struct file_operations rtk_auto_dhcp_proc_fops = {
	.open		= auto_dhcp_single_open,
	.write		= auto_dhcp_single_write,
	.read		= seq_read,
	.llseek 	= seq_lseek,
	.release 	= single_release,
};
#endif
#endif


#ifdef CONFIG_RTL_HARDWARE_IPV6_SUPPORT
#ifdef CONFIG_RTL_PROC_NEW

int hwL3v6_single_open(struct inode *inode, struct file *file)
{
	return(single_open(file, rtk_hwL3v6_read,NULL));
}

static ssize_t hwL3v6_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return rtk_hwL3v6_write(file, userbuf,count, off);
}

struct file_operations rtk_hwL3v6_proc_fops = {
	.open		= hwL3v6_single_open,
	.write		= hwL3v6_single_write,
	.read		= seq_read,
	.llseek 		= seq_lseek,
	.release 		= single_release,
};
#endif
#endif

#if defined(CONFIG_RTK_VLAN_SUPPORT)
/*rtk_vlan_support*/
int rtk_vlan_support_single_open(struct inode *inode, struct file *file)
{
	return(single_open(file, rtk_vlan_support_read,NULL));
}
static ssize_t rtk_vlan_support_single_write(struct file * file, const char __user * userbuf,
		     size_t count, loff_t * off)
{
	return rtk_vlan_support_write(file, userbuf,count, off);
}
struct file_operations rtk_vlan_support_proc_fops= {
		.open           = rtk_vlan_support_single_open,
		.write		    = rtk_vlan_support_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};
#endif
#if defined(CONFIG_RTK_BRIDGE_VLAN_SUPPORT)
/*rtk_vlan_management_entry*/
int rtk_vlan_management_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, rtk_vlan_management_read,NULL));
}
static ssize_t rtk_vlan_management_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return rtk_vlan_management_write(file, userbuf,count, off);
}
struct file_operations rtk_vlan_management_proc_fops= {
		.open           = rtk_vlan_management_single_open,
		.write		    = rtk_vlan_management_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};
#endif
#ifdef CONFIG_RTK_VLAN_WAN_TAG_SUPPORT
/*rtk_vlan_wan_tag*/
int rtk_vlan_wan_tag_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, rtk_vlan_wan_tag_support_read,NULL));
}
static ssize_t rtk_vlan_wan_tag_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return rtk_vlan_wan_tag_support_write(file, userbuf,count, off);
}
struct file_operations rtk_vlan_wan_tag_proc_fops= {
		.open           = rtk_vlan_wan_tag_single_open,
		.write		    = rtk_vlan_wan_tag_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};
#endif
#ifdef CONFIG_RTL_INBAND_CTL_ACL
int rtk_inband_ctl_acl_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, inband_ctl_acl_read,NULL));
}
static ssize_t rtk_inband_ctl_acl_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return inband_ctl_acl_write(file, userbuf,count, off);
}
struct file_operations rtk_inband_ctl_acl_proc_fops= {
		.open           = rtk_inband_ctl_acl_single_open,
		.write		    = rtk_inband_ctl_acl_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};

#endif

#if defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)
#if defined(CONFIG_RTL_HW_DSLITE_SUPPORT)
static int dslite_hw_fw_read(struct seq_file *s, void *v)
{
	seq_printf(s, "Ds-lite Hw forward %s\n",(dslite_hw_fw>0)?"enabled":"disabled");
	return 0;
}

static int dslite_hw_fw_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
	uint32 tmpBuf[32];
	uint32 uintVal;

	if (buffer && !copy_from_user(tmpBuf, buffer, count))
	{
		tmpBuf[count-1]=0;
		uintVal=simple_strtol((const char *)tmpBuf, NULL, 0);
		if(uintVal!=0)
			dslite_hw_fw = uintVal;
		else
			dslite_hw_fw = 0 ;

		printk("%s.%d. dslite_hw_fw value(%d)\n",__FUNCTION__,__LINE__,dslite_hw_fw);
		return count;
	}
	return -EFAULT;
}

int rtk_dslite_hw_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, dslite_hw_fw_read,NULL));
}

static ssize_t rtk_dslite_hw_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return dslite_hw_fw_write(file, userbuf,count, off);
}
struct file_operations rtk_dslite_hw_forward_proc_fops= {
		.open           = rtk_dslite_hw_single_open,
		.write		    = rtk_dslite_hw_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};


#endif
#ifdef CONFIG_RTL_HW_6RD_SUPPORT
static int sixrd_hw_fw_read(struct seq_file *s, void *v)
{
	seq_printf(s, "6RD Hw forward %s\n",(sixrd_hw_fw>0)?"enabled":"disabled");
	return 0;
}

static int sixrd_hw_fw_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
	uint32 tmpBuf[32];
	uint32 uintVal;

	if (buffer && !copy_from_user(tmpBuf, buffer, count))
	{
		tmpBuf[count-1]=0;
		uintVal=simple_strtol((const char *)tmpBuf, NULL, 0);
		if(uintVal!=0)
			sixrd_hw_fw = uintVal;
		else
			sixrd_hw_fw = 0 ;

		//printk("%s.%d. sixrd_hw_fw value(%d)\n",__FUNCTION__,__LINE__,sixrd_hw_fw);
		return count;
	}
	return -EFAULT;
}

int rtk_6rd_hw_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, sixrd_hw_fw_read,NULL));
}

static ssize_t rtk_6rd_hw_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return sixrd_hw_fw_write(file, userbuf,count, off);
}

struct file_operations rtk_6rd_hw_forward_proc_fops = {
	.open           = rtk_6rd_hw_single_open,
	.write		    = rtk_6rd_hw_single_write,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};
#endif
#endif


#if defined(CONFIG_RTL_HW_VLAN_SUPPORT)
/*rtl_hw_vlan_support*/
int rtl_hw_vlan_support_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, rtl_hw_vlan_support_read,NULL));
}
static ssize_t rtl_hw_vlan_support_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return rtl_hw_vlan_support_write(file, userbuf,count, off);
}
struct file_operations rtl_hw_vlan_support_proc_fops= {
		.open           = rtl_hw_vlan_support_single_open,
		.write		    = rtl_hw_vlan_support_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};
/*rtl_hw_vlan_tagged_mc*/
int rtl_hw_vlan_tagged_mc_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, rtl_hw_vlan_tagged_bridge_multicast_read,NULL));
}
static ssize_t rtl_hw_vlan_tagged_mc_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return rtl_hw_vlan_tagged_bridge_multicast_write(file, userbuf,count, off);
}
struct file_operations rtl_hw_vlan_tagged_mc_proc_fops= {
		.open           = rtl_hw_vlan_tagged_mc_single_open,
		.write		    = rtl_hw_vlan_tagged_mc_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};

#ifdef CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT
/*rtk_multicast_scream_vid*/
int rtk_multicast_scream_vid_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, rtk_multicast_scream_vid_read,NULL));
}
static ssize_t rtk_multicast_scream_vid_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return rtk_multicast_scream_vid_write(file, userbuf,count, off);
}
struct file_operations rtk_multicast_scream_vid_proc_fops= {
		.open           = rtk_multicast_scream_vid_single_open,
		.write		    = rtk_multicast_scream_vid_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};

/*rtk_query_for_bridge_port*/
int rtk_query_for_bridge_port_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, rtk_query_for_bridge_port_read,NULL));
}
static ssize_t rtk_query_for_bridge_port_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return rtk_query_for_bridge_port_write(file, userbuf,count, off);
}
struct file_operations rtk_query_for_bridge_port_proc_fops= {
		.open           = rtk_query_for_bridge_port_single_open,
		.write		    = rtk_query_for_bridge_port_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};

#endif
#endif

#if defined(CONFIG_RTL_8367R_SUPPORT)
/*rtl_8367r_vlan*/
int rtl_8367r_vlan_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, rtl_8367r_vlan_read,NULL));
}
static ssize_t rtl_8367r_vlan_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return 0;
}
struct file_operations rtl_8367r_vlan_proc_fops= {
		.open           = rtl_8367r_vlan_single_open,
		.write		    = rtl_8367r_vlan_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};
#endif

#if defined(CONFIG_819X_PHY_RW)
/*rtl_phy_status*/
int rtl_phy_status_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, rtl_phy_status_read,NULL));
}
static ssize_t rtl_phy_status_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return rtl_phy_status_write(file, userbuf,count, off);
}
struct file_operations rtl_phy_status_proc_fops= {
		.open           = rtl_phy_status_single_open,
		.write		    = rtl_phy_status_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};
/*port_mibEntry*/
int port_mibEntry_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, port_mibStats_read_proc,PDE_DATA(inode)));
}
static ssize_t port_mibEntry_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return port_mibStats_write_proc(file, userbuf,count, off);
}
struct file_operations port_mibEntry_proc_fops= {
		.open           = port_mibEntry_single_open,
		.write		    = port_mibEntry_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};
#endif
#ifdef CONFIG_TR181_ETH
/*tr181_eth_link*/
int tr181_eth_link_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, tr181_eth_link_read,NULL));
}
static ssize_t tr181_eth_link_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return tr181_eth_link_write(file, userbuf,count, off);
}
struct file_operations tr181_eth_link_proc_fops= {
		.open           = tr181_eth_link_single_open,
		.write		    = tr181_eth_link_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};
/*tr181_eth_if*/
int tr181_eth_if_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, tr181_eth_if_read,NULL));
}
static ssize_t tr181_eth_if_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return tr181_eth_if_write(file, userbuf,count, off);
}
struct file_operations tr181_eth_if_proc_fops= {
		.open           = tr181_eth_if_single_open,
		.write		    = tr181_eth_if_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};
/*tr181_eth_stats*/
int tr181_eth_stats_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, tr181_eth_stats_read,NULL));
}
static ssize_t tr181_eth_stats_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return tr181_eth_stats_write(file, userbuf,count, off);
}
struct file_operations tr181_eth_stats_proc_fops= {
		.open           = tr181_eth_stats_single_open,
		.write		    = tr181_eth_stats_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};
/*tr181_eth_set*/
int tr181_eth_set_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, tr181_eth_set_read,NULL));
}
static ssize_t tr181_eth_set_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return tr181_eth_set_write(file, userbuf,count, off);
}
struct file_operations tr181_eth_set_proc_fops= {
		.open           = tr181_eth_set_single_open,
		.write		    = tr181_eth_set_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};
#endif
static int custom_Passthru_read_proc(struct seq_file *s, void *v);
static int custom_Passthru_write_proc(struct file *file, const char *buffer, unsigned long count, void *data);
/*custom_Passthru*/
int custom_Passthru_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, custom_Passthru_read_proc,NULL));
}
static ssize_t custom_Passthru_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return custom_Passthru_write_proc(file, userbuf,count, off);
}
struct file_operations custom_Passthru_proc_fops= {
		.open           = custom_Passthru_single_open,
		.write		    = custom_Passthru_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};
static int rtl865x_stormCtrlReadProc(struct seq_file *s, void *v);
static int rtl865x_stormCtrlWriteProc(struct file *file, const char *buffer, unsigned long count, void *data);
/*StormCtrl*/
int StormCtrl_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, rtl865x_stormCtrlReadProc,NULL));
}
static ssize_t StormCtrl_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return rtl865x_stormCtrlWriteProc(file, userbuf,count, off);
}
struct file_operations StormCtrl_proc_fops= {
		.open           = StormCtrl_single_open,
		.write		    = StormCtrl_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};
#if 0//defined(CONFIG_RTL_8197F)
static int rtl865x_extendStormCtrlReadProc(struct seq_file *s, void *v);
static int rtl865x_extendStormCtrlWriteProc(struct file *file, const char *buffer, unsigned long count, void *data);
/*ExtendStormCtrl*/
int extendStormCtrl_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, rtl865x_extendStormCtrlReadProc,NULL));
}
static ssize_t extendStormCtrl_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return rtl865x_extendStormCtrlWriteProc(file, userbuf,count, off);
}
struct file_operations extendStormCtrl_proc_fops= {
		.open           = extendStormCtrl_single_open,
		.write		    = extendStormCtrl_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};
#endif

#if defined(CONFIG_RTL_WAN_PORT_SETTING)
static int wan_port_setting_read(struct seq_file *s, void *v);
static int wan_port_setting_write(struct file *file, const char *buffer, unsigned long count, void *data);
int wan_port_setting_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, wan_port_setting_read,NULL));
}
static ssize_t  wan_port_setting_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return wan_port_setting_write(file, userbuf,count, off);
}
struct file_operations wan_port_setting_proc_fops= {
		.open           = wan_port_setting_single_open,
		.write		    = wan_port_setting_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};
#endif
static int proc_phyTest_read(struct seq_file *s, void *v);
static int32 proc_phyTest_write( struct file *filp, const char *buff,unsigned long len1, void *data );
/*phyRegTest*/
int phyRegTest_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, proc_phyTest_read,NULL));
}
static ssize_t phyRegTest_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return proc_phyTest_write(file, userbuf,count, off);
}
struct file_operations phyRegTest_proc_fops= {
		.open           = phyRegTest_single_open,
		.write		    = phyRegTest_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};
#if defined(CONFIG_RTL_MLD_SNOOPING)
static int rtl865x_mldSnoopingReadProc(struct seq_file *s, void *v);
static int rtl865x_mldSnoopingWriteProc(struct file *file, const char *buffer, unsigned long count, void *data);
/*br_mldsnoop*/
int br_mldsnoop_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, rtl865x_mldSnoopingReadProc,NULL));
}
static ssize_t br_mldsnoop_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return rtl865x_mldSnoopingWriteProc(file, userbuf,count, off);
}
struct file_operations br_mldsnoop_proc_fops= {
		.open           = br_mldsnoop_single_open,
		.write		    = br_mldsnoop_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};
#endif
#if defined (CONFIG_RTL_PHY_POWER_CTRL)
static int rtl865x_phyPowerCtrlReadProc(struct seq_file *s, void *v);
static int rtl865x_phyPowerCtrlWriteProc(struct file *file, const char *buffer, unsigned long count, void *data);
/*phyPower*/
int phyPower_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, rtl865x_phyPowerCtrlReadProc,NULL));
}
static ssize_t phyPower_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return rtl865x_phyPowerCtrlWriteProc(file, userbuf,count, off);
}
struct file_operations phyPower_proc_fops= {
		.open           = phyPower_single_open,
		.write		    = phyPower_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};
#endif
#if defined(CONFIG_RTL_LINKSTATE)
static int port_state_read_proc(struct seq_file *s, void *v);
static int32 port_state_write_proc( struct file *filp, const char *buff,unsigned long len, void *data );
/*portState*/
int portState_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, port_state_read_proc,NULL));
}
static ssize_t portState_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return port_state_write_proc(file, userbuf,count, off);
}
struct file_operations portState_proc_fops= {
		.open           = portState_single_open,
		.write		    = portState_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};
#endif
#if defined(CONFIG_RTL_REINIT_SWITCH_CORE)
static int rtl865x_reInitSwitchCoreReadProc(struct seq_file *s, void *v);
static int rtl865x_reInitSwitchCoreWriteProc(struct file *file, const char *buffer, unsigned long count, void *data);
/*reInitSwitchCore*/
int reInitSwitchCore_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, rtl865x_reInitSwitchCoreReadProc,NULL));
}
static ssize_t reInitSwitchCore_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return rtl865x_reInitSwitchCoreWriteProc(file, userbuf,count, off);
}
struct file_operations reInitSwitchCore_proc_fops= {
		.open           = reInitSwitchCore_single_open,
		.write		    = reInitSwitchCore_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};
#if defined(CONFIG_RTL_CHECK_SWITCH_TX_HANGUP)
static int rtl_check_swCore_tx_hang_read_proc(struct seq_file *s, void *v);
static int rtl_check_swCore_tx_hang_write_proc(struct file *file, const char *buffer, unsigned long count, void *data);
int rtl_check_swCore_tx_hang_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, rtl_check_swCore_tx_hang_read_proc,NULL));
}
static ssize_t rtl_check_swCore_tx_hang_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return rtl_check_swCore_tx_hang_write_proc(file, userbuf,count, off);
}
struct file_operations rtl_check_swCore_tx_hang_proc_fops= {
		.open           = rtl_check_swCore_tx_hang_single_open,
		.write		    = rtl_check_swCore_tx_hang_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};
#endif
#endif
#if defined (CONFIG_RTL_SOCK_DEBUG)
static int32 rtl865x_sockDebugReadProc(struct seq_file *s, void *v);
static int rtl865x_sockDebugWriteProc(struct file *file, const char *buffer, unsigned long count, void *data);
/*sockDebug*/
int sockDebug_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, rtl865x_sockDebugReadProc,NULL));
}
static ssize_t sockDebug_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return rtl865x_sockDebugWriteProc(file, userbuf,count, off);
}
struct file_operations sockDebug_proc_fops= {
		.open           = sockDebug_single_open,
		.write		    = sockDebug_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};
#endif
static int eee_read_proc(struct seq_file *s, void *v);
static int eee_write_proc(struct file *file, const char *buffer, unsigned long count, void *data);
/*eee*/
int eee_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, eee_read_proc,NULL));
}
static ssize_t eee_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return eee_write_proc(file, userbuf,count, off);
}
struct file_operations eee_proc_fops= {
		.open           = eee_single_open,
		.write		    = eee_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};
#ifdef CONFIG_RTL_JATE_TEST
static int jate_read_proc(struct seq_file *s, void *v);
static int jate_write_proc(struct file *file, const char *buffer, unsigned long count, void *data);
/*jate*/
int jate_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, jate_read_proc,NULL));
}
static ssize_t jate_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return jate_write_proc(file, userbuf,count, off);
}
struct file_operations jate_proc_fops= {
		.open           = jate_single_open,
		.write		    = jate_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};
#endif
#else
static int eee_read_proc(char *page, char **start, off_t off, int count, int *eof, void *data);
static int eee_write_proc(struct file *file, const char *buffer, unsigned long count, void *data);
#ifdef CONFIG_RTL_JATE_TEST
static int jate_read_proc(char *page, char **start, off_t off, int count, int *eof, void *data);
static int jate_write_proc(struct file *file, const char *buffer, unsigned long count, void *data);
#endif
#endif

extern void FullAndSemiReset( void );
#if defined(CONFIG_RTL_8198) || defined(CONFIG_RTL_819XD) || defined(CONFIG_RTL_8196E) || defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)
extern int32 phyRegTest_init(void);
#endif
#if defined (CONFIG_RTL_MLD_SNOOPING)
extern int32 rtl8651_initMldSnooping(void);
#endif
#if defined (CONFIG_RTL_REINIT_SWITCH_CORE)
extern int  rtl865x_creatReInitSwitchCoreProc(void);
#endif
#if defined(CONFIG_RTL_PROC_DEBUG)||defined(CONFIG_RTL_DEBUG_TOOL)
extern int32 rtl865x_proc_debug_cleanup(void);
#endif
#ifdef CONFIG_RTL_LAYERED_ASIC_DRIVER_L3
extern int32 rtl865x_initAsicL3(void);
#endif

#ifdef CONFIG_RTL_LAYERED_ASIC_DRIVER_L4
extern int32 rtl865x_initAsicL4(void);
#endif

#if defined(CONFIG_RTD_1295_HWNAT)
/* get MAC address from uMAC regs and update it in vlanconfig[] */
#define RTL_UMAC_R8(reg)	readb ((void __iomem *)(((uintptr_t)rtl_umac_mmio) + (reg)))
static int rtl_set_vlanconfig_mac_addr(void)
{
	char *path ="/gmac@98016000";
	struct device_node *dt_node;
	void __iomem *rtl_umac_mmio;
	uint32 i, j;
	uint8 mac[ETH_ALEN], ifname[IFNAMSIZ];
	struct rtl865x_vlanConfig *vlancfg_entry;

	dt_node = of_find_node_by_path(path);
	if (!dt_node) {
		printk(KERN_ERR "Failed to find uMAC device-tree node: %s\n", path);
		return -ENODEV;
	}

	rtl_umac_mmio = of_iomap(dt_node, 0);
	if (!rtl_umac_mmio) {
		 printk(KERN_ERR "HWNAT MMIO : no mmio space for rtl_umac_mmio\n");
		return -EINVAL;
	}

	for (i = 0; i < ETH_ALEN; i++)
		mac[i] = RTL_UMAC_R8(i);
	DBG("MAC address base = %pM", mac);

	for (i = 0; i < ETH_INTF_NUM; i++) {
		if (i == RTL_LAN_IDX)
			sprintf(ifname, RTL_DRV_LAN_NETIF_NAME);
		else
			sprintf(ifname, "eth%d", i);

		vlancfg_entry = rtl_get_vlanconfig_by_netif_name(ifname);

		if (vlancfg_entry) {
			memcpy(vlancfg_entry->mac.octet, mac, ETHER_ADDR_LEN);
			for (j = ETHER_ADDR_LEN - 1; j >= 0; j--) {
				if (mac[j] == 0xFF) {
					mac[j] = 0;
				} else {
					mac[j] += 1;
					break;
				}
			}

		} else {
			printk(KERN_ERR "Failed to get %s vlanconfig entry\n", ifname);
			return -EINVAL;
		}
	}

	return SUCCESS;
}
#endif //defined(CONFIG_RTD_1295_HWNAT)

#if defined(CONFIG_RTD_1295_HWNAT)
static int re865x_probe(struct platform_device *pdev)
#else //defined(CONFIG_RTD_1295_HWNAT)
int  __init re865x_probe (void)
#endif //defined(CONFIG_RTD_1295_HWNAT)
{
/*2007-12-19*/
	int32 i, j;
	int32 totalVlans=((sizeof(vlanconfig))/(sizeof(struct rtl865x_vlanConfig)))-1;
#if defined(CONFIG_BRIDGE_IGMP_SNOOPING)
#if defined(CONFIG_RTL_HARDWARE_MULTICAST)
	rtl865x_mCastConfig_t mCastConfig;
#endif
#endif
#if defined (CONFIG_RTL_IGMP_SNOOPING)
	int32 retVal;
	int32 igmpInitFlag=FAILED;
	struct rtl_mCastSnoopingGlobalConfig mCastSnoopingGlobalConfig;
	#if defined (CONFIG_RTL_HARDWARE_MULTICAST)
	rtl865x_mCastConfig_t mCastConfig;
	#endif
#endif
#if defined(CONFIG_RTL_REPORT_LINK_STATUS)  ||  defined(CONFIG_RTK_VLAN_SUPPORT)
	struct proc_dir_entry *res_stats_root;
#endif
#if defined(CONFIG_RTL_819XD)&&defined(CONFIG_RTL_8211DS_SUPPORT)&&defined(CONFIG_RTL_8197D)
	uint32 reg_tmp=0;
#endif

#ifndef CONFIG_RTL_PROC_NEW
#if defined(CONFIG_RTL_REPORT_LINK_STATUS)
	struct proc_dir_entry *rtk_link_event_entry;
	struct proc_dir_entry *rtk_link_status_entry;
#endif

#if defined(CONFIG_TR181_ETH)
	struct proc_dir_entry *tr181_eth_link, *tr181_eth_if, *tr181_eth_stats, *tr181_eth_set;
#endif
#endif

#ifdef CONFIG_RTL_IPV6READYLOGO
#ifndef CONFIG_RTL_PROC_NEW
	struct proc_dir_entry * hwL3v6;
#endif
#endif

#if defined(CONFIG_RTK_VLAN_SUPPORT)
#ifndef CONFIG_RTL_PROC_NEW
	struct proc_dir_entry *res_stats;
	struct proc_dir_entry *rtk_vlan_support_entry;
#if defined(CONFIG_RTK_BRIDGE_VLAN_SUPPORT)
	struct proc_dir_entry *rtk_vlan_management_entry;
#endif
#endif
#ifdef CONFIG_AUTO_DHCP_CHECK
	struct proc_dir_entry *proc;
#endif
#if defined(CONFIG_819X_PHY_RW)//#if defined(CONFIG_RTK_VLAN_FOR_CABLE_MODEM)
	uint32 portnum;
	char port_mibEntry_name[10];
	struct proc_dir_entry *rtl_phy;
	struct proc_dir_entry *port_mibStats_root;
	struct proc_dir_entry *port_mibStats_entry;
#endif	//#if defined(CONFIG_819X_PHY_RW)
#endif

#if defined(CONFIG_RTL_JUMBO_FRAME)&&!defined(CONFIG_RTL_PROC_NEW)
	struct proc_dir_entry *jumbo_frame_support_entry=NULL;
#endif

#if defined(CONFIG_RTL_HW_VLAN_SUPPORT)
	#ifdef CONFIG_RTL_PROC_NEW
	#else
	struct proc_dir_entry *rtl_hw_vlan_support_entry;
	struct proc_dir_entry *rtl_hw_vlan_tagged_bridge_multicast_entry;
#ifdef CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT
	struct proc_dir_entry *rtk_multicast_scream_vid_entry;
	struct proc_dir_entry *rtk_query_for_bridge_port_entry;
#endif
	memset(hw_vlan_info, 0, PORT_NUMBER*sizeof(struct hw_vlan_port_setting));
#endif
#endif

#if defined(CONFIG_RTL_8367R_SUPPORT) && !defined(CONFIG_RTL_PROC_NEW)
	struct proc_dir_entry *rtl_8367r_vlan;
#endif

#if defined (CONFIG_RTL_LOCAL_PUBLIC)
	struct rtl865x_interface_info ifInfo;
#endif

#if defined(PATCH_GPIO_FOR_LED)
	int port;
#endif

#ifdef CONFIG_RTK_VLAN_WAN_TAG_SUPPORT
#ifndef CONFIG_RTL_PROC_NEW
	struct proc_dir_entry *rtk_vlan_wan_tag_support_entry;
#endif
#endif

	#if defined(CONFIG_RTD_1295_HWNAT)
	int irq;
	int dt_tmp;
	unsigned dt_array[6];

	rtl819x_pdev = pdev;
	_rtl86xx_dev.plat_dev = pdev;
	/* initialize memory map of registers and IRQ */
	printk(KERN_ERR "%s driver loaded\n", MODULENAME);

	irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq\n");
		return -EINVAL;
	}

	DBG("IRQ = %d", irq);

	/*
	if (!pdev->dev.dma_mask)
		pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;
	if (!pdev->dev.coherent_dma_mask)
		pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	*/

	rtl_hwnat_mmio = of_iomap(pdev->dev.of_node, 0);
	if (!rtl_hwnat_mmio) {
		dev_err(&pdev->dev, "HWNAT MMIO : no NAT mmio space\n");
		return -EINVAL;
	}

	rtl_hwnat_clk_mmio = of_iomap(pdev->dev.of_node, 1);
	if (!rtl_hwnat_clk_mmio) {
		dev_err(&pdev->dev, "HWNAT MMIO : no CLK mmio space\n");
		return -EINVAL;
	}

	rtl_hwnat_sata_mmio = of_iomap(pdev->dev.of_node, 2);
	if (!rtl_hwnat_sata_mmio) {
		dev_err(&pdev->dev, "HWNAT MMIO : no SATA mmio space\n");
		return -EINVAL;
	}

	of_property_read_u32_array(pdev->dev.of_node, "reg", dt_array, 6);
	rtl_hwnat_base = dt_array[0];
	//of_property_read_u32(pdev->dev.of_node, "reg", &rtl_hwnat_base);
	//DBG("Chip version ID (CVID) = 0x%x, Project code 0x%x", READ_MEM32(CVIDR), REG16(CVIDR + 0xa));
	DBG("HWNAT start 0x%x, rtl_hwnat_mmio = 0x%p", dt_array[0], rtl_hwnat_mmio);
	//DBG("SYS CLK start 0x%x, rtl_hwnat_clk_mmio = 0x%p", dt_array[2], rtl_hwnat_clk_mmio);
	//DBG("SATA Wrapper start 0x%x, rtl_hwnat_sata_mmio = 0x%p", dt_array[4], rtl_hwnat_sata_mmio);

	of_property_read_u32(pdev->dev.of_node, "offload_enable", &dt_tmp);
	DBG("HW NAT offload_enable = %s", dt_tmp > 0 ? "enable" : "disable");
	rtl_hwnat_enable = dt_tmp;

	of_property_read_u32(pdev->dev.of_node, "rgmii_voltage", &dt_tmp);
	DBG("RGMII voltage = %d (1:1.8V, 2:2.5V, 3:3.3V)", dt_tmp);
	hwnat_rgmii_voltage = dt_tmp;

	of_property_read_u32(pdev->dev.of_node, "mac0_enable", &dt_tmp);
	DBG("mac0_enable = %s", dt_tmp > 0 ? "enable" : "disable");
	hwnat_mac0_enable = dt_tmp;
	//if (hwnat_mac0_enable == 0)
	//	vlanconfig[0].if_type = IF_NONE;

	of_property_read_u32(pdev->dev.of_node, "mac0_mode", &dt_tmp);
	DBG("mac0_mode = %s", dt_tmp > 0 ? "SGMII" : "RGMII");
	hwnat_mac0_mode = dt_tmp;

	of_property_read_u32(pdev->dev.of_node, "mac5_conn_to", &dt_tmp);
	DBG("mac5_conn_to = %s", dt_tmp > 0 ? "MAC" : "PHY");
	hwnat_mac5_conn_to = dt_tmp;

	of_property_read_u32(pdev->dev.of_node, "mac0_phy_id", &dt_tmp);
	DBG("mac0_phy_id = %d", dt_tmp);
	rtl8651AsicEthernetTable[0].phyId = dt_tmp;

	of_property_read_u32(pdev->dev.of_node, "mac4_phy_id", &dt_tmp);
	DBG("mac4_phy_id = %d", dt_tmp);
	rtl8651AsicEthernetTable[4].phyId = dt_tmp;

	of_property_read_u32(pdev->dev.of_node, "mac5_phy_id", &dt_tmp);
	DBG("mac5_phy_id = %d", dt_tmp);
	rtl8651AsicEthernetTable[5].phyId = dt_tmp;

	/* get MAC address from uMAC regs and update it in vlanconfig[] */
	rtl_set_vlanconfig_mac_addr();

	/* Init RGMII/SGMII/PHY clock */
	/* Init ASIC table */
	rtd129x_hwnat_clk_init();

	/* enable interrupt migration
	   timeout = 20 * 512ns = 10ms
	   pkt count = 32 */
	WRITE_MEM32(CPUIMTTR0,  0x01405014);
	WRITE_MEM32(CPUIMTTR1,  0x01405014);
	WRITE_MEM32(CPUIMTTR2,  0x01405014);
	WRITE_MEM32(CPUIMTTR3,  0x00000014);
	WRITE_MEM32(CPUIMPNTR0, 0x20202020);
	WRITE_MEM32(CPUIMPNTR1, 0x00002020);
	WRITE_MEM32(CPUIMPNTR2, 0x20202020);
	WRITE_MEM32(CPUIMCR,    0x00000F3F);
	#endif //defined(CONFIG_RTD_1295_HWNAT)

#if defined(CONFIG_FPGA_V6_HWNAT)
	{
		/* reset FPGA PHY daughter boards */
		uint32 phy_db_addr;

		phy_db_addr = ((uint32) rtl_hwnat_mmio) + HWNAT_CPUIR_OFFSET + 0x9100;
		WRITE_MEM32(phy_db_addr, 0);
		mdelay(50);
		WRITE_MEM32(phy_db_addr, 3);
		mdelay(50);
	}
#endif //defined(CONFIG_FPGA_V6_HWNAT)

#ifdef CONFIG_SMP
	spin_lock_init(&lock_eth_other);
	spin_lock_init(&lock_eth_rx);
	spin_lock_init(&lock_eth_tx);
	spin_lock_init(&lock_eth_buf);
	spin_lock_init(&lock_eth_hw);
#endif

#ifdef CONFIG_RTL_8197F
	// bsp_swcore_init() is called in \arch\xxx\bsp\setup.c originally,
	// move to here for 8197F.
	bsp_swcore_init(8);
#endif

	// initial the global variables in dmem.
	rx_noBuffer_cnt=0;
	tx_ringFull_cnt =0;
	tx_drop_cnt =0;
	_debug_flag = 0;

	rtlglue_printf("\nProbing RTL819X NIC-kenel stack size order[%d]...\n", THREAD_SIZE_ORDER);
	REG32(CPUIIMR) = 0x00;
	REG32(CPUICR) &= ~(TXCMD | RXCMD);
	rxMbufRing=NULL;

#if defined(CONFIG_RTL_819XD)
	if ((REG32(REVR) == 0x8197C000) || (REG32(REVR) == 0x8197C001))
	{
		rtl_port0Refined = 1;
	}
#endif

#if defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)
	memset(sw_pvid, 0, sizeof(uint16)*RTL8651_PORT_NUMBER);
#endif

	#if !defined(CONFIG_RTD_1295_HWNAT)
	/* for RTD1295, we use reg 0x98000000 bit1 in rtd129x_hwnat_clk_init() to reset NAT */
	/*Initial ASIC table*/
	FullAndSemiReset();
	#endif //!defined(CONFIG_RTD_1295_HWNAT)
	{
		rtl8651_tblAsic_InitPara_t para;

		memset(&para, 0, sizeof(rtl8651_tblAsic_InitPara_t));

		/*
			For DEMO board layout, RTL865x platform define corresponding PHY setting and PHYID.
		*/

		rtl865x_wanPortMask = RTL865X_PORTMASK_UNASIGNED;

		INIT_CHECK(rtl865x_initAsicL2(&para));

#ifdef CONFIG_RTL_LAYERED_ASIC_DRIVER_L3
		INIT_CHECK(rtl865x_initAsicL3());
#endif
#if defined(CONFIG_RTL_LAYERED_ASIC_DRIVER_L4)
		INIT_CHECK(rtl865x_initAsicL4());
#endif

		/*
			Re-define the wan port according the wan port detection result.
			NOTE:
				There are a very strong assumption that if port5 was giga port,
				then wan port was port 5.
		*/
		if (RTL865X_PORTMASK_UNASIGNED==rtl865x_wanPortMask)
		{
			/* keep the original mask */
			assert(RTL865X_PORTMASK_UNASIGNED==rtl865x_lanPortMask);
			rtl865x_wanPortMask = RTL_WANPORT_MASK;
			rtl865x_lanPortMask = RTL_LANPORT_MASK;

#if defined(CONFIG_RTL_8881A)
			if (REG32(BOND_OPTION)==0x0a) { 		// Check is RTL8881AM

				REG32(PIN_MUX_SEL) = (REG32(PIN_MUX_SEL)&~(0x7<<7))|(0x3<<7); //Set MUX to GPIO
				REG32(PEFGH_CNR) = REG32(PEFGH_CNR) & ~(0x80); //Set E7 for  Gpio
				REG32(PEFGH_DIR) = REG32(PEFGH_DIR) & ~(0x80); //Set E7 for Input Mode
				if((REG32(PEFGH_DAT)&0x80) == 0x80) { //Pull high
					rtl865x_wanPortMask = 0;
					rtl865x_lanPortMask = 0x110;
					vlanconfig[0].memPort = vlanconfig[0].untagSet = 0x110;
					vlanconfig[1].memPort = vlanconfig[1].untagSet = 0;
					if (totalVlans >= 3)
						vlanconfig[2].memPort = vlanconfig[2].untagSet = 0;
				}
			}
#elif defined(CONFIG_RTL_8197F)
			#if !defined(CONFIG_RTD_1295_HWNAT)
			#if !defined(CONFIG_RTL_8367R_SUPPORT) && !defined(CONFIG_RTL_8211F_SUPPORT)
			{
			extern uint32 rtl819x_bond_option(void);
			if (rtl819x_bond_option() == 3){
				rtl865x_wanPortMask = 0;
				rtl865x_lanPortMask = 0x110;
				for(i=0;i<totalVlans;i++)
				{
					if (vlanconfig[i].isWan == 0) {
						vlanconfig[i].memPort = vlanconfig[i].untagSet = 0x110;
					}
					else {
						vlanconfig[i].memPort = vlanconfig[i].untagSet = 0;
					}
				}
			}
			}
			#endif
			#endif //!defined(CONFIG_RTD_1295_HWNAT)
#endif
		}
		else
		{
			/* redefine wan port mask */
			assert(RTL865X_PORTMASK_UNASIGNED!=rtl865x_lanPortMask);
			for(i=0;i<totalVlans;i++)
			{
				if (TRUE==vlanconfig[i].isWan)
				{
					vlanconfig[i].memPort = vlanconfig[i].untagSet = rtl865x_wanPortMask;
				}
				else
				{
					vlanconfig[i].memPort = vlanconfig[i].untagSet = rtl865x_lanPortMask;
				}
			}
		}
#if 1		/*	10/100 & giga use the same pre-allocated skb number */
		/*
			Re-define the pre-allocated skb number according the wan
			port detection result.
			NOTE:
				There are a very strong assumption that if port1~port4 were
				all giga port, then the sdram was 32M.
		*/
		{
			if (RTL865X_PREALLOC_SKB_UNASIGNED==rtl865x_maxPreAllocRxSkb)
			{
				assert(rtl865x_rxSkbPktHdrDescNum==
					rtl865x_txSkbPktHdrDescNum==
					RTL865X_PREALLOC_SKB_UNASIGNED);

				rtl865x_maxPreAllocRxSkb = MAX_PRE_ALLOC_RX_SKB;
				rtl865x_rxSkbPktHdrDescNum = NUM_RX_PKTHDR_DESC;
				rtl865x_txSkbPktHdrDescNum = NUM_TX_PKTHDR_DESC;
			}
			else
			{
				assert(rtl865x_rxSkbPktHdrDescNum!=RTL865X_PREALLOC_SKB_UNASIGNED);
				assert(rtl865x_txSkbPktHdrDescNum!=RTL865X_PREALLOC_SKB_UNASIGNED);
				/* Assigned value in function of rtl8651_initAsic() */
				rxRingSize[0] = rtl865x_rxSkbPktHdrDescNum;
				txRingSize[0] = rtl865x_txSkbPktHdrDescNum;
			}

			for(i=1;i<RTL865X_SWNIC_RXRING_HW_PKTDESC;i++)
			{
				rtl865x_maxPreAllocRxSkb += rxRingSize[i];
			}
		}
#else
		{
			rtl865x_maxPreAllocRxSkb = MAX_PRE_ALLOC_RX_SKB;
			rtl865x_rxSkbPktHdrDescNum = NUM_RX_PKTHDR_DESC;
			rtl865x_txSkbPktHdrDescNum = NUM_TX_PKTHDR_DESC;
		}
#endif
	}


#ifdef BR_SHORTCUT
	cached_dev=NULL;
	cached_dev2=NULL;
	cached_dev3=NULL;
	cached_dev4=NULL;
#endif

#if !defined(CONFIG_RTD_1295_HWNAT)
	/*init PHY LED style*/
#if defined(CONFIG_RTL_8196C) || defined (CONFIG_RTL_8198)

	/* config LED mode */
#if defined(CONFIG_RTK_VOIP_BOARD)
		WRITE_MEM32(LEDCR, 0x00055500 ); // 15 LED
		//avoiv bad voip quality
		WRITE_MEM32(0xb8000048, REG32(0xb8000048)&0xfffffff3);
#else
	WRITE_MEM32(LEDCR, 0x00000000 ); // 15 LED
#endif
	WRITE_MEM32(SWTAA, PORT5_PHY_CONTROL);
	WRITE_MEM32(TCR0, 0x000002C7); //8651 demo board default: 15 LED boards
	WRITE_MEM32(SWTACR, CMD_FORCE | ACTION_START); // force add
#endif

#endif //!defined(CONFIG_RTD_1295_HWNAT)

/*2007-12-19*/
#if defined(CONFIG_RTK_VLAN_SUPPORT)
		//port based decision
	rtl865xC_setNetDecisionPolicy(NETIF_PORT_BASED);
	WRITE_MEM32(PLITIMR,0);

#endif

#if defined(CONFIG_RTL_VLAN_BASED_NETIF)
	rtl865xC_setNetDecisionPolicy(NETIF_VLAN_BASED);
#elif defined(CONFIG_RTL_MAC_BASED_NETIF)
	rtl865xC_setNetDecisionPolicy(NETIF_MAC_BASED);	/* Net interface Multilayer-Decision-Based Control -- Set to MAC-Based mode. */
#endif

#if defined(CONFIG_RTL_819XD)&&defined(CONFIG_RTL_8211DS_SUPPORT)&&defined(CONFIG_RTL_8197D)
	rtl8651_getAsicEthernetPHYReg(0x6, 0, &reg_tmp);
	rtl_setPortMask(reg_tmp);
#endif

	INIT_CHECK(rtl865x_init());

#if defined (CONFIG_RTL_MULTI_LAN_DEV) || defined(CONFIG_RTK_VLAN_SUPPORT) || defined(CONFIG_RTL_VLAN_8021Q)
	re865x_packVlanConfig(vlanconfig, packedVlanConfig);
	INIT_CHECK(rtl865x_config(packedVlanConfig));
#else
	INIT_CHECK(rtl865x_config(vlanconfig));
#endif

#if !defined(CONFIG_RTD_1295_HWNAT)
#if defined(CONFIG_RTL_8198C)
	INIT_CHECK(rtl865x_init_hw());
	#ifdef PRIV_BUF_CAN_USE_KERNEL_BUF
	//RTL_swNic_freeRxBuf();
	#endif
#endif
#endif //!defined(CONFIG_RTD_1295_HWNAT)

#if defined(CONFIG_RTL_SWITCH_NEW_DESCRIPTOR)
	REG32(CPUICR1) = (REG32(CPUICR1) & ~CF_PKT_HDR_TYPE_MASK) | TX_PKTHDR_SHORTCUT_LSO;
#endif
#ifdef CONFIG_RTL_TSO
	REG32(CPUICR1) |= BIT(CF_TX_GATHER_OFFSET);
#endif

	/* create all default VLANs */
//	rtlglue_printf("	creating eth0~eth%d...\n",totalVlans-1 );
#if defined (CONFIG_RTL_IGMP_SNOOPING)
	memset(&mCastSnoopingGlobalConfig, 0, sizeof(struct rtl_mCastSnoopingGlobalConfig));
	mCastSnoopingGlobalConfig.maxGroupNum=256;
	mCastSnoopingGlobalConfig.maxSourceNum=300;
	mCastSnoopingGlobalConfig.hashTableSize=64;

	mCastSnoopingGlobalConfig.groupMemberAgingTime=260;
	mCastSnoopingGlobalConfig.lastMemberAgingTime=2;
	mCastSnoopingGlobalConfig.querierPresentInterval=260;

	mCastSnoopingGlobalConfig.dvmrpRouterAgingTime=120;
	mCastSnoopingGlobalConfig.mospfRouterAgingTime=120;
	mCastSnoopingGlobalConfig.pimRouterAgingTime=120;

	igmpInitFlag=rtl_initMulticastSnooping(mCastSnoopingGlobalConfig);
#endif

	for(i=0;i<totalVlans;i++)
	{
		struct net_device *dev;
		struct dev_priv	  *dp;
		int rc;

		if (IF_ETHER!=vlanconfig[i].if_type)
		{
			continue;
		}

#if defined(CONFIG_RTL_ISP_MULTI_WAN_SUPPORT)
		if (vlanconfig[i].isWan && (strncmp((vlanconfig[i].ifname), RTL_DRV_WAN0_NETIF_NAME, IFNAMSIZ)))
			continue;
#endif
		dev = alloc_etherdev(sizeof(struct dev_priv));
		if (!dev) {
			printk("failed to allocate dev %d", i);
			return -1;
		}
		SET_MODULE_OWNER(dev);
		dp = NETDRV_PRIV(dev);
		memset(dp,0,sizeof(*dp));
		dp->dev = dev;
		#if defined(CONFIG_RTD_1295_HWNAT)
		SET_NETDEV_DEV(dev, &pdev->dev);
		dp->plat_dev = pdev;
		#endif //defined(CONFIG_RTD_1295_HWNAT)

		dp->id = vlanconfig[i].vid;
		dp->portmask =  vlanconfig[i].memPort;
		dp->portnum  = 0;
		#if defined(CONFIG_RTK_VLAN_SUPPORT)
		dp->vlan_setting.is_lan = (dp->id!=RTL_WANVLANID);
		#endif
		for(j=0;j<RTL8651_AGGREGATOR_NUMBER;j++){
			if(dp->portmask & (1<<j))
				dp->portnum++;
		}

		memcpy((char*)dev->dev_addr,(char*)(&(vlanconfig[i].mac)), ETHER_ADDR_LEN);
#if defined(CONFIG_RTL_ISP_MULTI_WAN_SUPPORT)
		if (vlanconfig[i].isWan == 0) {
			#if defined(CONFIG_RTL_MULTI_LAN_DEV)
			if (strncmp((vlanconfig[i].ifname), RTL_DRV_LAN_P0_NETIF_NAME, IFNAMSIZ) == 0)
				strncpy(dev->name, RTL_DRV_ETHLAN_P0_NETIF_NAME, IFNAMSIZ);
			else
				strncpy(dev->name, vlanconfig[i].ifname, IFNAMSIZ);
			#endif
			dev->priv_flags = IFF_DOMAIN_ELAN;
		} else {
			strncpy(dev->name, vlanconfig[i].ifname, IFNAMSIZ);
			dev->priv_flags = IFF_DOMAIN_WAN;
		}
#endif

#if defined(CONFIG_COMPAT_NET_DEV_OPS)
		dev->open = re865x_open;
		dev->stop = re865x_close;
		dev->set_multicast_list = re865x_set_rx_mode;
		#if defined(CONFIG_RTK_BRIDGE_VLAN_SUPPORT)
		if(memcmp((vlanconfig[i].ifname), RTL_DRV_LAN_P7_NETIF_NAME, 4) == 0){
			memcpy((char*)dev->name, (char*)(&(vlanconfig[i].ifname)), 5);
			dev->hard_start_xmit = rtl_bridge_wan_start_xmit;
		}
		else
		#endif
		dev->hard_start_xmit = re865x_start_xmit;
		dev->get_stats = re865x_get_stats;
		dev->do_ioctl = re865x_ioctl;
		dev->tx_timeout = re865x_tx_timeout;
		dev->set_mac_address = rtl865x_set_hwaddr;
		dev->change_mtu = rtl865x_set_mtu;
#if defined(CONFIG_RTL8186_LINK_CHANGE)
		dev->change_link = rtl865x_set_link;
#endif
#ifdef CP_VLAN_TAG_USED
		dev->features |= NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX;
		dev->vlan_rx_register = cp_vlan_rx_register;
		dev->vlan_rx_kill_vid = cp_vlan_rx_kill_vid;
#endif

#else
		#if defined(CONFIG_RTK_BRIDGE_VLAN_SUPPORT)
			if(memcmp((vlanconfig[i].ifname), RTL_DRV_LAN_P7_NETIF_NAME, 4) == 0){
				memcpy((char*)dev->name, (char*)(&(vlanconfig[i].ifname)), 5);
				dev->netdev_ops = &rtl819x_netdev_ops_bridge;
			}
			else
		#endif
			dev->netdev_ops = &rtl819x_netdev_ops;
#endif
		dev->watchdog_timeo = TX_TIMEOUT;
#if 0
		dev->features |= NETIF_F_SG | NETIF_F_IP_CSUM;
#endif
#ifdef CP_VLAN_TAG_USED
		dev->features |= NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX;
#endif

#ifdef CONFIG_RTL_ETH_TX_SG
		dev->features |= (NETIF_F_SG | NETIF_F_GSO);
#endif

#if defined(CONFIG_RTL_HW_TX_CSUM) && !defined(CONFIG_IPV6)
		dev->features |= NETIF_F_HW_CSUM;
#endif

#ifdef CONFIG_RTL_TSO
		dev->features |= (NETIF_F_GSO | NETIF_F_HW_CSUM | NETIF_F_SG |
						  NETIF_F_TSO | NETIF_F_TSO_ECN);
#endif

		#if defined(CONFIG_RTD_1295_HWNAT)
		dev->irq = irq;
		#else //defined(CONFIG_RTD_1295_HWNAT)
		dev->irq = BSP_SWCORE_IRQ;
		#endif //defined(CONFIG_RTD_1295_HWNAT)
		rc = register_netdev(dev);

		if(!rc){
			_rtl86xx_dev.dev[i]=dev;
			rtl_add_ps_drv_netif_mapping(dev,vlanconfig[i].ifname);
			/*2007-12-19*/
			rtlglue_printf("eth%d added. vid=%d Member port 0x%x...\n", i,vlanconfig[i].vid ,vlanconfig[i].memPort );
		}else
			rtlglue_printf("Failed to allocate eth%d\n", i);

#if defined(CONFIG_RTK_VLAN_SUPPORT) || defined(CONFIG_RTL_REPORT_LINK_STATUS)
#ifdef CONFIG_RTL_PROC_NEW
		res_stats_root = proc_mkdir(dev->name, &proc_root);
#else
		res_stats_root = proc_mkdir(dev->name, NULL);
#endif
		if (res_stats_root == NULL)
		{
			printk("proc_mkdir failed!\n");
		}
#endif

#if defined(CONFIG_RTK_VLAN_SUPPORT)
#ifdef CONFIG_RTL_PROC_NEW
		proc_create_data("mib_vlan",0644,res_stats_root,&mib_vlan_proc_fops,(void *)dev);
#else
		if ((res_stats = create_proc_read_entry("mib_vlan", 0644, res_stats_root,
			read_proc_vlan, (void *)dev)) == NULL)
		{
			printk("create_proc_read_entry failed!\n");
		}
		res_stats->write_proc = write_proc_vlan;
#endif

#endif

#if defined(CONFIG_RTL_REPORT_LINK_STATUS)
		/*FIXME if mutliple-WAN*/
		rtk_linkEvent=0;
#ifdef CONFIG_RTL_PROC_NEW
		proc_create_data("up_event",0,res_stats_root,&up_event_proc_fops,NULL);
		proc_create_data("link_status",0644,res_stats_root,&link_status_proc_fops,(void *)dev);
#else/*CONFIG_RTL_PROC_NEW*/
		rtk_link_event_entry=create_proc_entry("up_event",0,res_stats_root);
		if(rtk_link_event_entry)
		{
			rtk_link_event_entry->read_proc=rtk_link_event_read;
			rtk_link_event_entry->write_proc=rtk_link_event_write;
		}


		if ((rtk_link_status_entry = create_proc_read_entry("link_status", 0644, res_stats_root,
			rtk_link_status_read, (void *)dev)) == NULL)
		{
			printk("create_proc_read_entry failed!\n");
		}
		rtk_link_status_entry->write_proc = rtk_link_status_write;
#endif/*CONFIG_RTL_PROC_NEW*/

#endif


	}

#if	defined (CONFIG_PPPOE_VLANTAG)


	res_stats_root = proc_mkdir("ppp", NULL);
	if (res_stats_root == NULL)
	{
		printk("proc_mkdir failed!\n");
	}



	if ((res_stats = create_proc_entry("mib_vlan", 0644, res_stats_root))
		== NULL)
	{
		printk("create_proc_entry failed!\n");
	}
	res_stats->write_proc = write_PPPOE_proc_vlan;
	res_stats->read_proc = read_PPPOE_proc_vlan;
#endif

#ifdef RTK_MANAGE_VLAN_IF
	rtk_add_manage_vlan_dev(RTL_WANVLANID); //just create for default
#endif


#if defined(CONFIG_RTL_MULTIPLE_WAN)
	rtl_config_multipleWan_netif(RTL_MULTIWAN_ADD);
	//rtl865x_addMultiCastNetif();
	rtl_regist_multipleWan_dev();
	rtl819x_igmp_proxy_wan_dev_proc_init();
#elif defined(CONFIG_RTL_REDIRECT_ACL_SUPPORT_FOR_ISP_MULTI_WAN)
	rtl_init_advRt();
#endif
#if defined(CONFIG_RTL_MULTIPLE_WAN) ||defined(CONFIG_RTL_MAC_BASED_NETIF)
	rtl865x_addMultiCastNetif();
#endif


#if defined(CONFIG_BRIDGE_IGMP_SNOOPING)
#if defined(CONFIG_RTL_HARDWARE_MULTICAST)
	memset(&mCastConfig, 0, sizeof(rtl865x_mCastConfig_t));
	for(i=0;i<totalVlans;i++)
	{
		if (TRUE==vlanconfig[i].isWan)
		{
			mCastConfig.externalPortMask |=vlanconfig[i].memPort;
		}
	}
	rtl865x_initMulticast(&mCastConfig);
#endif
#endif

#if defined (CONFIG_RTL_IGMP_SNOOPING)
	retVal=rtl_registerIgmpSnoopingModule(&nicIgmpModuleIndex);
	#if defined (CONFIG_RTL_HARDWARE_MULTICAST)
	if(retVal==SUCCESS)
	{
		rtl_multicastDeviceInfo_t devInfo;
		memset(&devInfo, 0 , sizeof(rtl_multicastDeviceInfo_t));
		strcpy(devInfo.devName, "eth*");
		for(i=0;i<totalVlans;i++)
		{
			if( vlanconfig[i].if_type==IF_ETHER)
			{
				devInfo.portMask|=vlanconfig[i].memPort;
			}
		}
		devInfo.swPortMask=devInfo.portMask & (~ ((1<<RTL8651_MAC_NUMBER)-1));
		rtl_setIgmpSnoopingModuleDevInfo(nicIgmpModuleIndex, &devInfo);
	}
	#endif
	rtl_setIpv4UnknownMCastFloodMap(nicIgmpModuleIndex, 0xFFFFFFFF);
#if defined(CONFIG_RTL_MLD_SNOOPING)
	rtl_setIpv6UnknownMCastFloodMap(nicIgmpModuleIndex, 0xFFFFFFFF);
#endif
	curLinkPortMask=rtl865x_getPhysicalPortLinkStatus();

	#if defined (CONFIG_RTL_HARDWARE_MULTICAST)
	memset(&mCastConfig, 0, sizeof(rtl865x_mCastConfig_t));
	for(i=0;i<totalVlans;i++)
	{
		if (TRUE==vlanconfig[i].isWan)
		{
			mCastConfig.externalPortMask |=vlanconfig[i].memPort;
		}
	}
	rtl865x_initMulticast(&mCastConfig);

	#endif
#endif

#if 0//def CONFIG_RTL_STP
	printk("Configuration ether driver to process port 0 ~ port %d for Spanning tree process\n",MAX_RE865X_ETH_STP_PORT-1);
	//Initial: disable Realtek Hardware STP
	rtl865x_setSpanningEnable(FALSE);

	for ( i = 0 ; i < MAX_RE865X_ETH_STP_PORT; i ++ )
	{
		struct net_device *dev;
		struct dev_priv *dp;
		int rc;
		struct re865x_priv *rp;

		rp = &_rtl86xx_dev;
		dev = alloc_etherdev(sizeof(struct dev_priv));
		if (!dev){
			rtlglue_printf("failed to allocate dev %d", i);
			return -1;
		}
		strcpy(dev->name, "port%d");
		memcpy((char*)dev->dev_addr,(char*)(&(vlanconfig[0].mac)),ETHER_ADDR_LEN);
		dp = NETDRV_PRIV(dev);
		memset(dp,0,sizeof(*dp));
		dp->dev = dev;
#ifdef CONFIG_COMPAT_NET_DEV_OPS
		dev->open = re865x_pseudo_open;
		dev->stop = re865x_pseudo_close;
		dev->set_multicast_list = NULL;
		dev->hard_start_xmit = re865x_start_xmit;
		dev->get_stats = re865x_get_stats;
		dev->do_ioctl = re865x_ioctl;
		dev->tx_timeout = NULL;
#else
		dev->netdev_ops = &rtl819x_pseudodev_ops;
#endif
		dev->watchdog_timeo = TX_TIMEOUT;
		dev->irq = 0;				/* virtual interfaces has no IRQ allocated */
		rc = register_netdev(dev);
		if (rc == 0)
		{
			_rtl86xx_dev.stp_port[i] = dev;
			printk("=> [stp pseudo port%d] done\n", i);
		} else
		{
			printk("=> Failed to register [stp pseudo port%d]", i);
			return -1;
		}
	}

	re865x_stp_mapping_init();
#endif
#if defined(CONFIG_RTL_HW_STP) || defined(CONFIG_RTL_STP)
	//Initial: disable Realtek Hardware STP
	rtl865x_setSpanningEnable(FALSE);
#endif

	((struct dev_priv*)(NETDRV_PRIV(_rtl86xx_dev.dev[0])))->dev_next = _rtl86xx_dev.dev[1];
	((struct dev_priv*)(NETDRV_PRIV(_rtl86xx_dev.dev[1])))->dev_prev = _rtl86xx_dev.dev[0];

#if defined(CONFIG_RTL_ETH_PRIV_SKB)
	init_priv_eth_skb_buf();
#endif

#if (defined(CONFIG_RTL_CUSTOM_PASSTHRU) && !defined(CONFIG_RTL8196_RTL8366))
	//cary
	rtl8651_customPassthru_init();
#endif

	rtl8651_initStormCtrl();

#if 0//defined(CONFIG_RTL_8197F)
	rtl8651_initExtendStormCtrl();
#endif

#if defined(CONFIG_RTL_WAN_PORT_SETTING)
	rtl819x_initWanPortSetting();
#endif

	rtl819x_eee_proc_init();

#ifdef CONFIG_RTL_JATE_TEST
	rtl819x_jate_proc_init();
#endif

#if defined(CONFIG_RTL_8198) || defined(CONFIG_RTL_819XD) || defined(CONFIG_RTL_8196E) || defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)
	// initial proc for phyRegTest
	phyRegTest_init();
#endif

#ifdef CONFIG_RTL_LAYERED_ASIC_DRIVER_L3
#if defined (CONFIG_RTL_HARDWARE_MULTICAST)
	rtl8651_setAsicMulticastEnable(TRUE);
#else
	rtl8651_setAsicMulticastEnable(FALSE);
#endif
#endif


#ifdef CONFIG_AUTO_DHCP_CHECK
#ifdef CONFIG_RTL_PROC_NEW
	rtl_link_status_pid_init();
	proc_create_data("rtk_linkstatus_pid",0,&proc_root,&rtk_auto_dhcp_proc_fops,NULL);
#else
	rtl_link_status_pid_init();
	proc=create_proc_entry("rtk_linkstatus_pid",0,NULL);
	if (proc)
	{
		proc->read_proc = rtk_linkstatus_pid_read;
		proc->write_proc = rtk_linkstatus_pid_write;
	}
#endif
#endif


#ifdef CONFIG_RTL_HARDWARE_IPV6_SUPPORT
	rtk_hwL3v6_enable = 0;
#ifdef CONFIG_RTL_PROC_NEW
	proc_create_data("hwL3v6",0,&proc_root,&rtk_hwL3v6_proc_fops,NULL);
#else
	hwL3v6 = create_proc_entry("hwL3v6", 0, NULL);
	if(hwL3v6)
	{
		hwL3v6->read_proc = rtk_hwL3v6_read;
		hwL3v6->write_proc = rtk_hwL3v6_write;
	}
#endif
#endif

#if defined(CONFIG_RTK_VLAN_SUPPORT)

	rtk_vlan_support_enable= 0;
#ifdef CONFIG_RTL_PROC_NEW
	proc_create_data("rtk_vlan_support",0,&proc_root,&rtk_vlan_support_proc_fops,NULL);
#else
	rtk_vlan_support_entry=create_proc_entry("rtk_vlan_support",0,NULL);
	if (rtk_vlan_support_entry)
	{
		rtk_vlan_support_entry->read_proc=rtk_vlan_support_read;
		rtk_vlan_support_entry->write_proc=rtk_vlan_support_write;
	}
#endif

#if defined(CONFIG_RTK_BRIDGE_VLAN_SUPPORT)
	memset(&management_vlan, 0, sizeof(struct vlan_info*));
#ifdef CONFIG_RTL_PROC_NEW
	proc_create_data("rtk_vlan_management_entry",0,&proc_root,&rtk_vlan_management_proc_fops,NULL);
#else
	rtk_vlan_management_entry = create_proc_entry("rtk_vlan_management_entry", 0, NULL);
	if(rtk_vlan_management_entry)
	{
		rtk_vlan_management_entry->read_proc=rtk_vlan_management_read;
		rtk_vlan_management_entry->write_proc=rtk_vlan_management_write;
	}
#endif
#endif
#endif

#ifdef CONFIG_RTK_VLAN_WAN_TAG_SUPPORT
#ifdef CONFIG_RTL_PROC_NEW
	proc_create_data("rtk_vlan_wan_tag",0,&proc_root,&rtk_vlan_wan_tag_proc_fops,NULL);
#else
	rtk_vlan_wan_tag_support_entry = create_proc_entry("rtk_vlan_wan_tag",0,NULL);
	if (rtk_vlan_wan_tag_support_entry)
	{
		rtk_vlan_wan_tag_support_entry->read_proc=rtk_vlan_wan_tag_support_read;
		rtk_vlan_wan_tag_support_entry->write_proc=rtk_vlan_wan_tag_support_write;
	}
#endif
#endif

#if defined(CONFIG_RTL_JUMBO_FRAME)
#ifdef CONFIG_RTL_PROC_NEW
		proc_create_data("jumbo_frame_support",0,&proc_root,&jumbo_frame_proc_fops,NULL);
#else
	jumbo_frame_support_entry = create_proc_entry("jumbo_frame_support", 0, NULL);
	if (jumbo_frame_support_entry)
	{
		jumbo_frame_support_entry->read_proc=jumbo_frame_support_read;
		jumbo_frame_support_entry->write_proc=jumbo_frame_support_write;
	}
#endif
#endif

#if defined(CONFIG_RTL_HW_VLAN_SUPPORT)
#ifdef CONFIG_RTL_PROC_NEW
	proc_create_data("rtl_hw_vlan_support",0,&proc_root,&rtl_hw_vlan_support_proc_fops,NULL);
	proc_create_data("rtl_hw_vlan_tagged_mc",0,&proc_root,&rtl_hw_vlan_tagged_mc_proc_fops,NULL);
#ifdef CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT
	proc_create_data("rtk_multicast_scream_vid",0,&proc_root,&rtk_multicast_scream_vid_proc_fops,NULL);
	proc_create_data("rtk_query_for_bridge_port",0,&proc_root,&rtk_query_for_bridge_port_proc_fops,NULL);
#endif
#else
	rtl_hw_vlan_support_entry = create_proc_entry("rtl_hw_vlan_support", 0, NULL);
	if (rtl_hw_vlan_support_entry)
	{
		rtl_hw_vlan_support_entry->read_proc=rtl_hw_vlan_support_read;
		rtl_hw_vlan_support_entry->write_proc=rtl_hw_vlan_support_write;
	}
	rtl_hw_vlan_tagged_bridge_multicast_entry = create_proc_entry("rtl_hw_vlan_tagged_mc", 0, NULL);
	if (rtl_hw_vlan_tagged_bridge_multicast_entry)
	{
		rtl_hw_vlan_tagged_bridge_multicast_entry->read_proc=rtl_hw_vlan_tagged_bridge_multicast_read;
		rtl_hw_vlan_tagged_bridge_multicast_entry->write_proc=rtl_hw_vlan_tagged_bridge_multicast_write;
	}
#ifdef CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT
	rtk_multicast_scream_vid_entry = create_proc_entry("rtk_multicast_scream_vid", 0, NULL);
	if (rtk_multicast_scream_vid_entry)
	{
		rtk_multicast_scream_vid_entry->read_proc=rtk_multicast_scream_vid_read;
		rtk_multicast_scream_vid_entry->write_proc=rtk_multicast_scream_vid_write;
	}

	rtk_query_for_bridge_port_entry = create_proc_entry("rtk_query_for_bridge_port", 0, NULL);
	if (rtk_query_for_bridge_port_entry)
	{
		rtk_query_for_bridge_port_entry->read_proc=rtk_query_for_bridge_port_read;
		rtk_query_for_bridge_port_entry->write_proc=rtk_query_for_bridge_port_write;
	}
#endif
#endif
#endif

#if defined(CONFIG_RTL_8367R_SUPPORT)
#ifdef CONFIG_RTL_PROC_NEW
	proc_create_data("rtl_8367r_vlan",0,&proc_root,&rtl_8367r_vlan_proc_fops,NULL);
#else
	rtl_8367r_vlan = create_proc_entry("rtl_8367r_vlan",0,NULL);
	if (rtl_8367r_vlan)
	{
		rtl_8367r_vlan->read_proc=rtl_8367r_vlan_read;
	}
#endif
#endif

#if defined(CONFIG_819X_PHY_RW)
//#if defined(CONFIG_RTK_VLAN_FOR_CABLE_MODEM)
#ifdef CONFIG_RTL_PROC_NEW
	proc_create_data("rtl_phy_status",0,&proc_root,&rtl_phy_status_proc_fops,NULL);
#else
	rtl_phy = create_proc_entry("rtl_phy_status",0,NULL);
	if(rtl_phy)
	{
		rtl_phy->read_proc = rtl_phy_status_read;
		rtl_phy->write_proc = rtl_phy_status_write;
	}
#endif
#ifdef CONFIG_RTL_PROC_NEW
	port_mibStats_root = proc_mkdir("ethPort_mibStats", &proc_root);
#else
	port_mibStats_root = proc_mkdir("ethPort_mibStats", NULL);
#endif
	if (port_mibStats_root == NULL)
	{
		printk("proc_mkdir failed!\n");
	}
	for(portnum=0; portnum<CPU; portnum++)
	{
		sprintf(&port_mibEntry_name[0], "port%u", portnum);
#ifdef CONFIG_RTL_PROC_NEW
		proc_create_data(port_mibEntry_name,0,port_mibStats_root,&port_mibEntry_proc_fops,(void *)portnum);
#else
		port_mibStats_entry = create_proc_entry(port_mibEntry_name, 0, port_mibStats_root);
		port_mibStats_entry->data = (void *)portnum;
		if(port_mibStats_entry)
		{
			port_mibStats_entry->read_proc = port_mibStats_read_proc;
			port_mibStats_entry->write_proc = port_mibStats_write_proc;
		}
#endif
	}
#endif	//#if defined(CONFIG_819X_PHY_RW)

#if defined(CONFIG_TR181_ETH)
	{
	extern struct proc_dir_entry *rtl865x_proc_dir;

#ifdef CONFIG_RTL_PROC_NEW
	proc_create_data("tr181_eth_link",0,rtl865x_proc_dir,&tr181_eth_link_proc_fops,NULL);
	proc_create_data("tr181_eth_if",0,rtl865x_proc_dir,&tr181_eth_if_proc_fops,NULL);
	proc_create_data("tr181_eth_stats",0,rtl865x_proc_dir,&tr181_eth_stats_proc_fops,NULL);
	proc_create_data("tr181_eth_set",0,rtl865x_proc_dir,&tr181_eth_set_proc_fops,NULL);
#else
	tr181_eth_link=create_proc_entry("tr181_eth_link",0,rtl865x_proc_dir);
	if (tr181_eth_link)
	{
		tr181_eth_link->read_proc = tr181_eth_link_read;
		tr181_eth_link->write_proc = tr181_eth_link_write;
	}
	tr181_eth_if=create_proc_entry("tr181_eth_if",0,rtl865x_proc_dir);
	if (tr181_eth_if)
	{
		tr181_eth_if->read_proc = tr181_eth_if_read;
		tr181_eth_if->write_proc = tr181_eth_if_write;
	}
	tr181_eth_stats=create_proc_entry("tr181_eth_stats",0,rtl865x_proc_dir);
	if (tr181_eth_stats)
	{
		tr181_eth_stats->read_proc = tr181_eth_stats_read;
		tr181_eth_stats->write_proc = tr181_eth_stats_write;
	}
	tr181_eth_set=create_proc_entry("tr181_eth_set",0,rtl865x_proc_dir);
	if (tr181_eth_set)
	{
		tr181_eth_set->read_proc = tr181_eth_set_read;
		tr181_eth_set->write_proc = tr181_eth_set_write;
	}
#endif
	}
#endif

#ifdef CONFIG_RTL_INBAND_CTL_ACL
#ifdef CONFIG_RTL_PROC_NEW
	proc_create_data("inband_ctl_acl",0,&proc_root,&rtk_inband_ctl_acl_proc_fops,NULL);
#else
	inband_ctl_acl = create_proc_entry("inband_ctl_acl",0,NULL);
	if (inband_ctl_acl)
	{
		inband_ctl_acl->read_proc = inband_ctl_acl_read;
		inband_ctl_acl->write_proc = inband_ctl_acl_write;
	}
#endif
#endif

#if defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)
#if defined(CONFIG_RTL_HW_DSLITE_SUPPORT)
#ifdef CONFIG_RTL_PROC_NEW
	proc_create_data("dslite_hw_fw",0,&proc_root,&rtk_dslite_hw_forward_proc_fops,NULL);
#endif
#endif
#if defined(CONFIG_RTL_HW_6RD_SUPPORT)
#ifdef CONFIG_RTL_PROC_NEW
	proc_create_data("6rd_hw_fw",0,&proc_root,&rtk_6rd_hw_forward_proc_fops,NULL);
#endif
#endif
#endif


#if defined (CONFIG_RTL_LOCAL_PUBLIC)
	rtl865x_initLocalPublic(NULL);

	memset(&ifInfo, 0 , sizeof(struct rtl865x_interface_info));
#if defined(CONFIG_RTL_PUBLIC_SSID)
	strcpy(ifInfo.ifname,RTL_GW_WAN_DEVICE_NAME);
#else
	strcpy(ifInfo.ifname, RTL_DRV_WAN0_NETIF_NAME);
#endif
	ifInfo.isWan=1;
	for(i=0;i<totalVlans;i++)
	{
		if ((TRUE==vlanconfig[i].isWan) && (vlanconfig[i].if_type==IF_ETHER))
		{
			ifInfo.memPort |= vlanconfig[i].memPort;
			ifInfo.fid=vlanconfig[i].fid;
		}
	}
	rtl865x_setLpIfInfo(&ifInfo);

	memset(&ifInfo, 0 , sizeof(struct rtl865x_interface_info));
	strcpy(ifInfo.ifname,RTL_DRV_LAN_NETIF_NAME);
	ifInfo.isWan=0;
	for(i=0;i<totalVlans;i++)
	{
		if ((FALSE==vlanconfig[i].isWan) && (vlanconfig[i].if_type==IF_ETHER))
		{
			ifInfo.memPort|=vlanconfig[i].memPort;
			ifInfo.fid=vlanconfig[i].fid;
		}
	}
	rtl865x_setLpIfInfo(&ifInfo);
#endif

	#if defined(CONFIG_RTD_1295_HWNAT)
	atomic_set(&rtl_rxTxDoneCnt, 0);
	#else //defined(CONFIG_RTD_1295_HWNAT)
	rtl_rxTxDoneCnt=0;
	#endif //defined(CONFIG_RTD_1295_HWNAT)
	atomic_set(&rtl_devOpened, 0);

#if defined(PATCH_GPIO_FOR_LED)
	for (port=0; port<RTL8651_PHY_NUMBER; port++)
		init_led_ctrl(port);
#endif

#if defined(CONFIG_RTL_LINKSTATE)
	initPortStateCtrl();
#endif

#if defined (CONFIG_RTL_MLD_SNOOPING)
	rtl8651_initMldSnooping();
#endif

#if defined (CONFIG_RTL_PHY_POWER_CTRL)
	rtl865x_initPhyPowerCtrl();
#endif

#if defined(RTL_CPU_QOS_ENABLED)
	highPrioRxTryCnt = MAX_HIGH_PRIO_TRY;
	highestPriority = 0;
	cpuQosHoldLow = 0;
	totalLowQueueCnt = 0;
	memset(pktQueueByPri, 0, sizeof(rtl_queue_entry)*(RTL865X_SWNIC_RXRING_MAX_PKTDESC));
#endif

	rtl865x_config_callback_for_get_drv_netifName(rtl_get_ps_drv_netif_mapping_by_psdev_name);
#if defined (CONFIG_RTL_REINIT_SWITCH_CORE)
	rtl865x_creatReInitSwitchCoreProc();
#endif

	#if defined(RX_TASKLET)
	rtl_rx_tasklet_running=0;
	#endif
	#if defined(TX_TASKLET)
	rtl_tx_tasklet_running=0;
	#endif
#if defined (CONFIG_RTL_SOCK_DEBUG)
	rtl865x_creatSockDebugProc();
#endif

#if 0//def CONFIG_RTL_ULINKER //led
	REG32(PIN_MUX_SEL_2) = REG32(PIN_MUX_SEL_2) | 0x00003000;
	REG32(PABCD_CNR)     = REG32(PABCD_CNR) & ~(0x00004000);
	REG32(PABCD_DIR)     = REG32(PABCD_DIR) | 0x00004000;
	REG32(PABCD_DAT)     = REG32(PABCD_DAT) & ~(0x00004000);
#endif

	memset(&rx_skb_queue, 0, sizeof(struct ring_que));

#if defined(CONFIG_RTL_ETH_802DOT1X_SUPPORT)
	//extern int init_802dot1x(void);
	rtl_init802dot1x();
#endif

#ifdef SUPPORT_DHCP_PORT_IP_BIND
	memset(port_mac_table, 0, sizeof(DHCP_PORT_MAC)*RTL8651_PHY_NUMBER);
#endif

#if defined(CONFIG_RTL_DNS_TRAP) //&& defined(CONFIG_RTL_AP_PACKAGE)
	re865x_packVlanConfig(vlanconfig, packedVlanConfig);
	rtl_remove_Acl_for_dns_trap(packedVlanConfig);
	rtl_add_Acl_for_dns_trap(packedVlanConfig);
#endif


	return 0;
}

#if defined(CONFIG_RTL_ETH_PRIV_SKB)

//---------------------------------------------------------------------------
static void init_priv_eth_skb_buf(void)
{
	int i;

	DEBUG_ERR("Init priv skb.\n");
	memset(eth_skb_buf, '\0', sizeof(struct priv_skb_buf2)*(MAX_ETH_SKB_NUM));
	INIT_LIST_HEAD(&eth_skbbuf_list);
	eth_skb_free_num=MAX_ETH_SKB_NUM;

	for (i=0; i<MAX_ETH_SKB_NUM; i++)  {
		memcpy(eth_skb_buf[i].magic, ETH_MAGIC_CODE, ETH_MAGIC_LEN);
		eth_skb_buf[i].buf_pointer = (void*)(&eth_skb_buf[i]);
		INIT_LIST_HEAD(&eth_skb_buf[i].list);
		list_add_tail(&eth_skb_buf[i].list, &eth_skbbuf_list);
	}
}

static __inline__ unsigned char *get_buf_from_poll(struct list_head *phead, unsigned int *count)
{
	unsigned long flags=0;
	unsigned char *buf;
	struct list_head *plist;

	SMP_LOCK_ETH_BUF(flags);

	if (list_empty(phead)) {
		SMP_UNLOCK_ETH_BUF(flags);
		DEBUG_ERR("eth_drv: phead=%X buf is empty now!\n", (unsigned int)phead);
		DEBUG_ERR("free count %d\n", *count);
		return NULL;
	}

	if (*count == 1) {
		SMP_UNLOCK_ETH_BUF(flags);
		DEBUG_ERR("eth_drv: phead=%X under-run!\n", (unsigned int)phead);
		return NULL;
	}

	*count = *count - 1;
	plist = phead->next;
	list_del_init(plist);
	buf = (unsigned char *)((unsigned int)plist + sizeof (struct list_head));
	SMP_UNLOCK_ETH_BUF(flags);
	return buf;
}

static __inline__ void release_buf_to_poll(unsigned char *pbuf, struct list_head	*phead, unsigned int *count)
{
	unsigned long flags=0;
	struct list_head *plist;

	SMP_LOCK_ETH_BUF(flags);

	*count = *count + 1;
	plist = (struct list_head *)((unsigned int)pbuf - sizeof(struct list_head));
	list_add_tail(plist, phead);
	SMP_UNLOCK_ETH_BUF(flags);
}

__IRAM_GEN void free_rtl865x_eth_priv_buf(unsigned char *head)
{
	#if defined(DELAY_REFILL_ETH_RX_BUF) || defined(ALLOW_RX_RING_PARTIAL_EMPTY)
	if (((atomic_read(&rtl_devOpened)>0) && (FAILED==return_to_rx_pkthdr_ring(head)))
		|| (atomic_read(&rtl_devOpened)==0))
	#endif
	{
		release_buf_to_poll(head, &eth_skbbuf_list, (unsigned int *)&eth_skb_free_num);
	}
}

__MIPS16
__IRAM_FWD
static struct sk_buff *dev_alloc_skb_priv_eth(unsigned int size)
{
	struct sk_buff *skb;
	unsigned char *data;

	/* first argument is not used */
	if(eth_skb_free_num>0)
	{
		data = get_buf_from_poll(&eth_skbbuf_list, (unsigned int *)&eth_skb_free_num);
		if (data == NULL) {
			DEBUG_ERR("eth_drv: priv_skb buffer empty!\n");
			return NULL;
		}

		skb = dev_alloc_8190_skb(data, size);

		if (skb == NULL) {
			//free_rtl865x_eth_priv_buf(data);
			release_buf_to_poll(data, &eth_skbbuf_list, (unsigned int *)&eth_skb_free_num);
			DEBUG_ERR("alloc linux_skb buff failed!\n");
			return NULL;
		}
		return skb;
	}

	return NULL;
}

__MIPS16
__IRAM_FWD
int is_rtl865x_eth_priv_buf(unsigned char *head)
{
	unsigned long offset = (unsigned long)(&((struct priv_skb_buf2 *)0)->buf);
	struct priv_skb_buf2 *priv_buf = (struct priv_skb_buf2 *)(((unsigned long)head) - offset);

	if ((!memcmp(priv_buf->magic, ETH_MAGIC_CODE, ETH_MAGIC_LEN)) &&
		(priv_buf->buf_pointer==(void*)(priv_buf))) {
		return 1;
	}
	else {
		return 0;
	}
}

#if defined(CONFIG_RTL_ETH_PRIV_SKB) || defined(CONFIG_NET_WIRELESS_AGN) || defined(CONFIG_NET_WIRELESS_AG)
extern void copy_skb_header(struct sk_buff *new, const struct sk_buff *old);
struct sk_buff *priv_skb_copy(struct sk_buff *skb)
{
	struct sk_buff *n;
	unsigned long flags=0;

	if (rx_skb_queue.qlen == 0) {
		n = dev_alloc_skb_priv_eth(CROSS_LAN_MBUF_LEN);
	}
	else {
		#if defined(RTK_QUE)
		SMP_LOCK_ETH_BUF(flags);
		n = rtk_dequeue(&rx_skb_queue);
		SMP_UNLOCK_ETH_BUF(flags);
		#else
		n = __skb_dequeue(&rx_skb_queue);
		#endif
	}

	if (n == NULL) {
		return NULL;
	}

	/* Set the tail pointer and length */
	skb_put(n, skb->len);
	n->csum = skb->csum;
	n->ip_summed = skb->ip_summed;
	memcpy(n->data, skb->data, skb->len);

	copy_skb_header(n, skb);
	return n;
}
EXPORT_SYMBOL(priv_skb_copy);
#endif // defined(CONFIG_NET_WIRELESS_AGN) || defined(CONFIG_NET_WIRELESS_AG)
#endif // CONFIG_RTL_ETH_PRIV_SKB

#if defined(CONFIG_RTD_1295_HWNAT)
static int re865x_exit(struct platform_device *pdev)
#else //defined(CONFIG_RTD_1295_HWNAT)
static void __exit re865x_exit (void)
#endif //defined(CONFIG_RTD_1295_HWNAT)
{

#if defined(CONFIG_RTL_PROC_DEBUG)||defined(CONFIG_RTL_DEBUG_TOOL)

	rtl865x_proc_debug_cleanup();
#endif


#if defined(CONFIG_PROC_FS) && defined(CONFIG_NET_SCHED) && defined(CONFIG_RTL_LAYERED_DRIVER)
#if defined(CONFIG_RTL_HW_QOS_SUPPORT)
	rtl865x_exitOutputQueue();
#endif
#endif

#if defined (CONFIG_RTL_IGMP_SNOOPING)
	rtl_exitMulticastSnooping();
#endif

#if defined(CONFIG_RTL_LINKSTATE)
	exitPortStateCtrl();
#endif
#if defined(CONFIG_RTL_WAN_PORT_SETTING)
	rtl819x_exitWanPortSetting();
#endif
	#if defined(CONFIG_RTD_1295_HWNAT)
	return 0;
	#else //defined(CONFIG_RTD_1295_HWNAT)
	return;
	#endif //defined(CONFIG_RTD_1295_HWNAT)
}

#ifdef CONFIG_RTK_FAKE_ETH

int32 rtl8651_EthernetPowerDown(void)
{
 uint32 statCtrlReg0;
 int i;

 //## from rtl8651_setAllAsicEthernetPHYPowerDown
 for (i=0; i<5; i++) {

  /* read current PHY reg 0 value */
  rtl8651_getAsicEthernetPHYReg( i, 0, &statCtrlReg0 );

  REG32(PCRP0+(i*4)) |= EnForceMode;
  statCtrlReg0 |= POWER_DOWN;

  /* write PHY reg 0 */
  rtl8651_setAsicEthernetPHYReg( i, 0, statCtrlReg0 );
 }
 //#######################################

#if !defined(CONFIG_RTD_1295_HWNAT)
 //then set bit 9 of 0xb800-0010 to 0. deactive switch core

 REG32(SYS_CLK_MAG)&=(~(SYS_SW_CLK_ENABLE));
#endif //!defined(CONFIG_RTD_1295_HWNAT)

 return SUCCESS;
}

int re865x_ioctl_fake (struct net_device *dev, struct ifreq *rq, int cmd)
{
	int32 rc = 0;
	unsigned long *data;
	int32 args[4];
	int32  * pRet;

	data = (unsigned long *)rq->ifr_data;

	if (copy_from_user(args, data, 4*sizeof(unsigned long)))
	{
		return -EFAULT;
	}

	switch (args[0])
	{
		case RTL8651_IOCTL_GETWANLINKSTATUS:
			{
				pRet = (int32 *)args[3];
				*pRet = FAILED;
				rc = SUCCESS;

				break;
			}

		case RTL8651_IOCTL_GETWANLINKSPEED:
			{
				int wanPortMask;

				pRet = (int32 *)args[3];
				*pRet = FAILED;
				rc = SUCCESS;

				wanPortMask = 0;

				/* no wan port exist */
				break;
			}
		default:
			rc = SUCCESS;
			break;
	}

	return rc;

}


static int rtl865x_set_mtu_fake(struct net_device *dev, int new_mtu)
{
	unsigned long flags=0;

	SMP_LOCK_ETH(flags);
	dev->mtu = new_mtu;

	SMP_UNLOCK_ETH(flags);

	return SUCCESS;
}

static int rtl865x_set_hwaddr_fake(struct net_device *dev, void *addr)
{
	unsigned long flags=0;
	int i;
	unsigned char *p;

	p = ((struct sockaddr *)addr)->sa_data;
	SMP_LOCK_ETH(flags);

	for (i = 0; i<ETHER_ADDR_LEN; ++i) {
		dev->dev_addr[i] = p[i];
	}

	SMP_UNLOCK_ETH(flags);
	return SUCCESS;
}

static int re865x_start_xmit_fake(struct sk_buff *skb, struct net_device *dev)
{

	dev_kfree_skb_any(skb);
	return 0;

}

static int re865x_open_fake (struct net_device *dev)
{
	struct dev_priv *cp;

	cp = NETDRV_PRIV(dev);
	if (cp->opened)
		return SUCCESS;

	cp->opened = 1;
	netif_start_queue(dev);
	return SUCCESS;
}


static int re865x_close_fake (struct net_device *dev)
{
	struct dev_priv *cp;

	cp = NETDRV_PRIV(dev);

	if (!cp->opened)
		return SUCCESS;
	netif_stop_queue(dev);

//	memset(&cp->net_stats, '\0', sizeof(struct net_device_stats));
	cp->opened = 0;

#ifdef BR_SHORTCUT
	if (dev == cached_dev)
		cached_dev=NULL;
#endif
	return SUCCESS;
}

#if defined(CONFIG_COMPAT_NET_DEV_OPS)
#else
static const struct net_device_ops rtl819x_netdev_ops_fake = {
	.ndo_open		= re865x_open_fake,
	.ndo_stop		= re865x_close_fake,
//	.ndo_validate_addr	= eth_validate_addr_fake,
	.ndo_set_mac_address 	= rtl865x_set_hwaddr_fake,
//	.ndo_set_multicast_list	= re865x_set_rx_mode_fake,
//	.ndo_get_stats		= re865x_get_stats_fake,
	.ndo_do_ioctl		= re865x_ioctl_fake,
	.ndo_start_xmit		= re865x_start_xmit_fake,
//	.ndo_tx_timeout		= re865x_tx_timeout_fake,
//#if defined(CP_VLAN_TAG_USED)
//	.ndo_vlan_rx_register	= cp_vlan_rx_register,
//endif
	.ndo_change_mtu		= rtl865x_set_mtu_fake,

};
#endif


int  __init re865x_probe_fake (void)
{
	struct net_device *dev;
	struct dev_priv	  *dp;
	int rc;

	dev = alloc_etherdev(sizeof(struct dev_priv));
	if (!dev) {
		printk("failed to allocate dev %d", 0);
		return -1;
	}

	dp = NETDRV_PRIV(dev);
	memset(dp,0,sizeof(*dp));
	dp->dev = dev;

	memcpy((char*)dev->dev_addr,(char*)(&(vlanconfig[0].mac)),ETHER_ADDR_LEN); //mark_FIXME.

#if defined(CONFIG_COMPAT_NET_DEV_OPS)
		dev->open = re865x_open_fake;
		dev->stop = re865x_close_fake;
		//dev->set_multicast_list = re865x_set_rx_mode;
		dev->hard_start_xmit = re865x_start_xmit_fake;
		//dev->get_stats = re865x_get_stats;
		dev->do_ioctl = re865x_ioctl_fake;
		//dev->tx_timeout = re865x_tx_timeout;
		dev->set_mac_address = rtl865x_set_hwaddr_fake;
		dev->change_mtu = rtl865x_set_mtu_fake;
#else
		dev->netdev_ops = &rtl819x_netdev_ops_fake;
#endif

	dev->watchdog_timeo = TX_TIMEOUT;

	rc = register_netdev(dev);

#ifdef BR_SHORTCUT
	cached_dev=NULL;
#endif

	if(rc)
		rtlglue_printf("Failed to allocate eth%d\n", 0);

	rc = rtl8651_EthernetPowerDown();

	return 0;
}
static void __exit re865x_exit_fake (void)
{
	return ;
}
#endif

#ifdef CONFIG_RTK_FAKE_ETH
module_init(re865x_probe_fake);
module_exit(re865x_exit_fake);
#else
#if defined(CONFIG_RTD_1295_HWNAT)
#ifdef CONFIG_PM
void rtl819x_save_hw_tables(void);
void rtd129x_save_regs(void);
void rtd129x_restore_regs(void);
void rtd129x_restore_alecr_regs(void);

static int rtd129x_nat_suspend(struct device *dev)
{
	struct net_device *ndev;
	int i;
	struct dev_priv *cp;

	printk(KERN_ERR "[RTD129X_NAT] Enter %s\n", __func__);

	for (i=0; i< ETH_INTF_NUM; i++) {
		ndev = _rtl86xx_dev.dev[i];
		if (netif_running(ndev)) {
			cp = NETDRV_PRIV(ndev);
			DBG(KERN_ERR "[RTD129X_NAT] stop portmask 0x%x\n", cp->portmask);
			netif_device_detach(ndev);
			netif_stop_queue(ndev);
		}
	}

	if (irqDev) {
		cp = NETDRV_PRIV(irqDev);
		#if defined(CONFIG_RTL_ETH_NAPI_SUPPORT)
		DBG(KERN_ERR "[RTD129X_NAT] stop NAPI of portmask 0x%x\n", cp->portmask);
		napi_disable(&cp->napi);
		#endif //CONFIG_RTL_ETH_NAPI_SUPPORT
	}

	if(RTK_PM_STATE == PM_SUSPEND_STANDBY){
		//For idle mode
		printk(KERN_ERR "[RTD129X_NAT] %s Idle mode\n", __func__);
	}else{
		//For suspend mode
		printk(KERN_ERR "[RTD129X_NAT] %s Suspend mode\n", __func__);
	}
	rtd129x_save_regs();
	rtl819x_save_hw_tables();

	printk(KERN_ERR "[RTD129X_NAT] Exit %s\n", __func__);

	return 0;
}

static int rtd129x_nat_resume(struct device *dev)
{
	struct net_device *ndev;
	int i;
	struct dev_priv *cp;

	printk(KERN_ERR "[RTD129X_NAT] Enter %s\n", __func__);

	if(RTK_PM_STATE == PM_SUSPEND_STANDBY){
		//For idle mode
		printk(KERN_ERR "[RTD129X_NAT] %s Idle mode\n", __func__);
	}else{
		//For suspend mode
		printk(KERN_ERR "[RTD129X_NAT] %s Suspend mode\n", __func__);
	}

	/* Enable clk and release reset module */
	rtd129x_hwnat_clk_init();

	/* reset driver */
	rtd129x_restore_regs();
	rtl865x_reinitSwitchCore();
	/* Workaround:
		MSCR is changed when calling rtl865x_reinitSwitchCore().
		Therefore, we restore ALECR regs again */
	rtd129x_restore_alecr_regs();

	for (i=0; i< ETH_INTF_NUM; i++) {
		ndev = _rtl86xx_dev.dev[i];
		if (netif_running(ndev)) {
			cp = NETDRV_PRIV(ndev);
			DBG(KERN_ERR "[RTD129X_NAT] resume portmask 0x%x\n", cp->portmask);
			netif_device_attach(ndev);
		}
	}

	if (irqDev) {
		cp = NETDRV_PRIV(irqDev);
		#if defined(CONFIG_RTL_ETH_NAPI_SUPPORT)
		DBG(KERN_ERR "[RTD129X_NAT] resume NAPI of portmask 0x%x\n", cp->portmask);
		napi_enable(&cp->napi);
		#endif //CONFIG_RTL_ETH_NAPI_SUPPORT
	}


	printk(KERN_ERR "[RTD129X_NAT] Exit %s\n", __func__);

	return 0;
}

static const struct dev_pm_ops rtd129x_nat_pm_ops = {
	.suspend		= rtd129x_nat_suspend,
	.resume			= rtd129x_nat_resume,
};

#define RTD129X_NAT_PM_OPS	(&rtd129x_nat_pm_ops)

#else /* !CONFIG_PM */
#define RTD129X_NAT_PM_OPS	NULL
#endif /* !CONFIG_PM */

static const struct of_device_id rtd1295_nic_dt_ids[] = {
	{ .compatible = "Realtek,rtd1295-hwnat", },
	{},
};

MODULE_DEVICE_TABLE(of, rtd1295_nic_dt_ids);

static struct platform_driver rtd1295_nic_driver = {
	.probe		= re865x_probe,
	.remove		= re865x_exit,
	.driver = {
		.name		= MODULENAME,
		.owner		= THIS_MODULE,
		.pm			= RTD129X_NAT_PM_OPS,
		.of_match_table = of_match_ptr(rtd1295_nic_dt_ids),
	},
};

module_platform_driver(rtd1295_nic_driver);
#else //defined(CONFIG_RTD_1295_HWNAT)
device_initcall_sync(re865x_probe);
module_exit(re865x_exit);
#endif //defined(CONFIG_RTD_1295_HWNAT)
#endif

#include "./common/rtl865x_eventMgr.h"
extern int32 rtl865x_initEventMgr(rtl865x_eventMgr_param_t *param);
#if defined(CONFIG_RTL_PROC_DEBUG)||defined(CONFIG_RTL_DEBUG_TOOL)
extern int32 rtl865x_proc_debug_init(void);
#endif

/*
@func enum RTL_RESULT | rtl865x_init | Initialize light rome driver and RTL865x ASIC.
@rvalue RTL_SUCCESS | Initial success.
@comm
	Its important to call this API before using the driver. Note taht you can not call this API twice !
*/
int32 rtl865x_init(void)
{
	int32 retval = 0;


	__865X_Config = 0;

#ifdef CONFIG_RTL8196_RTL8366
	/*	configure 8366 */
	{
		int ret;
		int i;
		rtl8366rb_phyAbility_t phy;

		REG32(PEFGHCNR_REG) = REG32(PEFGHCNR_REG)& (~(1<<11) ); //set byte F GPIO3 = gpio
		REG32(PEFGHDIR_REG) = REG32(PEFGHDIR_REG) | (1<<11);  //0 input, 1 output, set F bit 3 output
		REG32(PEFGHDAT_REG) = REG32(PEFGHDAT_REG) |( (1<<11) ); //F3 GPIO
		mdelay(150);

		ret = smi_init(GPIO_PORT_F, 2, 1);
		ret = rtl8366rb_initChip();
		ret = rtl8366rb_initVlan();
		ret = smi_write(0x0f09, 0x0020);
		ret = smi_write(0x0012, 0xe0ff);

		memset(&phy, 0, sizeof(rtl8366rb_phyAbility_t));
		phy.Full_1000 = 1;
		phy.Full_100 = 1;
		phy.Full_10 = 1;
		phy.Half_100 = 1;
		phy.Half_10 = 1;
		phy.FC = 1;
		phy.AsyFC = 1;
		phy.AutoNegotiation = 1;
		for(i=0;i<5;i++)
		{
			ret = rtl8366rb_setEthernetPHY(i,&phy);
		}
	}

	 REG32(0xb8010000)=REG32(0xb8010000)&(0x20000000);
		REG32(0xbb80414c)=0x00037d16;
		REG32(0xbb804100)=1;
		REG32(0xbb804104)=0x00E80367;
#endif

/*common*/
	retval = rtl865x_initNetifTable();
	retval = rtl865x_initVlanTable();
#ifdef CONFIG_RTL_LAYERED_DRIVER_ACL
	retval = rtl865x_init_acl();
#endif
	retval = rtl865x_initEventMgr(NULL);

/*l2*/
 #ifdef CONFIG_RTL_LAYERED_DRIVER_L2
	retval = rtl865x_layer2_init();
 #endif


/*layer3*/
#ifdef CONFIG_RTL_LAYERED_DRIVER_L3
	retval = rtl865x_initIpTable();
	retval = rtl865x_initPppTable();
	retval = rtl865x_initRouteTable();
	retval = rtl865x_initNxtHopTable();
	retval = rtl865x_arp_init();
#endif

/*layer4*/
#if defined(CONFIG_RTL_LAYERED_DRIVER_L4)
	rtl865x_nat_init();
#endif

#if defined(CONFIG_RTL_HARDWARE_IPV6_SUPPORT)
	retval = rtl8198c_initIpv6RouteTable();
	retval = rtl8198c_initIpv6NxtHopTable();
	retval = rtl8198c_ipv6_arp_init();
#if defined(CONFIG_RTL_HW_DSLITE_SUPPORT)
	retval = rtl8198c_initIpv6DsLiteTable();
#endif
#if defined(CONFIG_RTL_HW_6RD_SUPPORT)
	retval = rtl8198c_initIpv66RDTable();
#endif
#endif

	/*queue id & rx ring descriptor mapping*/
	/*queue id & rx ring descriptor mapping*/
	REG32(CPUQDM0)=QUEUEID1_RXRING_MAPPING|(QUEUEID0_RXRING_MAPPING<<16);
	REG32(CPUQDM2)=QUEUEID3_RXRING_MAPPING|(QUEUEID2_RXRING_MAPPING<<16);
	REG32(CPUQDM4)=QUEUEID5_RXRING_MAPPING|(QUEUEID4_RXRING_MAPPING<<16);


	rtl8651_setAsicOutputQueueNumber(CPU, RTL_CPU_RX_RING_NUM);


#if defined(CONFIG_RTL_PROC_DEBUG)||defined(CONFIG_RTL_DEBUG_TOOL)
	rtl865x_proc_debug_init();
#endif

#if defined(PATCH_GPIO_FOR_LED)
	rtl8651_resetAllAsicMIBCounter();
#endif

	rtl_ps_drv_netif_mapping_init();
//#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
#ifdef CONFIG_AUTO_DHCP_CHECK
	if(!(nl_eventd_sk = get_nl_eventd_sk()))
	{
		nl_eventd_sk = rtk_eventd_netlink_init();
	}
#endif
	return SUCCESS;
}

int update_vlanconfig(uint32 vid, uint32 portMask, uint32 untagPortMask)
{
	int i;

	for(i=0; vlanconfig[i].vid != 0; i++)
	{
		if (vlanconfig[i].vid == vid)
		{
			vlanconfig[i].memPort = portMask;
			vlanconfig[i].untagSet = untagPortMask;
			return 0;
		}
	}
	return 0;
}

/*
@func enum RTL_RESULT | rtl865x_config | Configure light rome driver. Create VLAN and Network interface.
@parm struct rtl865x_vlanConfig * | vlanconfig |
@rvlaue RTL_SUCCESS | Sucessful configuration.
@rvalue RTL_INVVID | Invalid VID.
@comm
	struct rtl865x_vlanConfig is defined as follows:

			ifname:		Layer 3 Network Interface name, eg: eth0, eth1, ppp0...etc,. If it is specified, both layer 2 vlan and layer 3
						netwrok interface are created and bound together. It also can be a NULL value. In this case, only a layer 2 VLAN
						is created.
			isWan:		1 for WAN interface and 0 for LAN interface in a layer 4 mode.
			if_type:		IF_ETHER sets a network interface to be ETHER type. Instead, IF_PPPOE sets a netwrok to be PPPoE type.
						This field is meaningful only when the ifname is specified.
			vid:			VLAN ID to create a vlan.
			memPort:		VLAN member port.
			untagSet:	VLAN untag Set.
			mtu:			MTU.
			mac:		MAC address of the VLAN or network interface.
	eg1:

	struct rtl865x_vlanConfig vlanconfig[] = {
		{ 	"eth0",	 1,   IF_ETHER, 	8, 	   1, 	0x01, 		0x01,		1500, 	{ { 0x00, 0x00, 0xda, 0xcc, 0xcc, 0x08 } }	},
		{	"eth1",	 0,   IF_ETHER,	9,	   1,		0x1e,		0x1e,		1500,	{ { 0x00, 0x00, 0xda, 0xcc, 0xcc, 0x09 } }	},

		LRCONFIG_END,
	}
*/
int32 rtl865x_config(struct rtl865x_vlanConfig vlanconfig[])
{
	uint16 pvid;
	int32 i, j;
	int32 retval = 0;
	uint32 valid_port_mask = 0;

	if (!vlanconfig[0].vid)
		return RTL_EINVALIDVLANID;

	INIT_CHECK(rtl8651_setAsicOperationLayer(2));

	for(i=0; vlanconfig[i].vid != 0; i++)
	{
		rtl865x_netif_t netif;

		if(vlanconfig[i].memPort == 0)
			continue;

		valid_port_mask = vlanconfig[i].memPort;
		if(vlanconfig[i].isWan == 0)
			valid_port_mask |= 0x100;

		/*add vlan*/
#if defined(CONFIG_RTL_ISP_MULTI_WAN_SUPPORT)
		if (vlanconfig[i].if_type == IF_ETHER) {
#endif
		retval = rtl865x_addVlan(vlanconfig[i].vid);

		if(retval == SUCCESS)
		{
			rtl865x_addVlanPortMember(vlanconfig[i].vid,vlanconfig[i].memPort & valid_port_mask);

#if (defined(CONFIG_RTL_8367R_SUPPORT) || defined(CONFIG_RTL_8370_SUPPORT))&& !defined(CONFIG_RTL_CPU_TAG)
			// remove untagPortMask in 8197D hardware VLAN table
			rtl865x_setVlanPortTag(vlanconfig[i].vid,vlanconfig[i].memPort, TRUE);
#endif
			rtl865x_setVlanFilterDatabase(vlanconfig[i].vid,vlanconfig[i].fid);
		}
#if defined(CONFIG_RTL_ISP_MULTI_WAN_SUPPORT)
		}
#endif
		/*add network interface*/
		memset(&netif, 0, sizeof(rtl865x_netif_t));
		memcpy(netif.name,vlanconfig[i].ifname,MAX_IFNAMESIZE);
		memcpy(netif.macAddr.octet,vlanconfig[i].mac.octet,ETHER_ADDR_LEN);
		netif.mtu = vlanconfig[i].mtu;
		netif.if_type = vlanconfig[i].if_type;
		netif.vid = vlanconfig[i].vid;
		netif.is_wan = vlanconfig[i].isWan;
		netif.is_slave = vlanconfig[i].is_slave;
		#if defined (CONFIG_RTL_HARDWARE_MULTICAST)
		netif.enableRoute=1;
		#endif

		#if defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)
		//netif.enableRouteV6 =1; 	/*jwj:enable ipv6, then enable v6 router*/
		netif.mtuV6 = vlanconfig[i].mtu;
		#if defined(CONFIG_RTL_HARDWARE_IPV6_SUPPORT) ||(defined (CONFIG_RTL_MLD_SNOOPING)&&defined(CONFIG_RTL_HARDWARE_MULTICAST))
		netif.enableRouteV6 =1;   	/*jwj:enable ipv6, then enable v6 router*/
		#endif
		#endif

		retval = rtl865x_addNetif(&netif);

#ifndef CONFIG_RTL_ISP_MULTI_WAN_SUPPORT
		if(netif.is_slave == 1)
#if defined(CONFIG_RTL_PUBLIC_SSID)
			rtl865x_attachMasterNetif(netif.name,RTL_GW_WAN_DEVICE_NAME);
#else
			rtl865x_attachMasterNetif(netif.name, RTL_DRV_WAN0_NETIF_NAME);
#endif
#endif
#if defined(CONFIG_PROC_FS) && defined(CONFIG_NET_SCHED) && defined(CONFIG_RTL_LAYERED_DRIVER)
		memcpy(&netIfName[i][0], vlanconfig[i].ifname, sizeof(vlanconfig[i].ifname));
#endif

#if defined (CONFIG_RTL_UNKOWN_UNICAST_CONTROL)
		if (vlanconfig[i].isWan==0)
			memcpy(lanIfName, vlanconfig[i].ifname, sizeof(vlanconfig[i].ifname));
#endif
		if(retval != SUCCESS && retval != RTL_EVLANALREADYEXISTS)
			return retval;
	}

#if (defined(CONFIG_RTL_8367R_SUPPORT) || defined(CONFIG_RTL_8370_SUPPORT))&& !defined(CONFIG_RTL_CPU_TAG)
	{
	extern int RTL83XX_vlan_init(void);
	RTL83XX_vlan_init();
	}
#endif

	/*this is a one-shot config*/
	if ((++__865X_Config) == 1)
	{
		for(i=0; i<RTL8651_PORT_NUMBER + 3; i++)
		{
			/* Set each port's PVID */
			for(j=0,pvid=0; vlanconfig[j].vid != 0; j++)
			{
				if ( (1<<i) & vlanconfig[j].memPort )
				{
					pvid = vlanconfig[j].vid;
					break;
				}
			}

			if (pvid!=0)
			{
	#ifdef CONFIG_HARDWARE_NAT_DEBUG
	/*2007-12-19*/
			rtlglue_printf("%s:%d:lrconfig[j].vid is %d,pvid is %d, j is %d,i is %d\n",__FUNCTION__,__LINE__,vlanconfig[j].vid,pvid,j, i);
	#endif

			CONFIG_CHECK(rtl8651_setAsicPvid(i, pvid));
	#if defined(CONFIG_RTK_VLAN_SUPPORT)
			rtl865x_setPortToNetif(vlanconfig[j].ifname,i);
	#endif
			}
		}
	}

	#if defined (CONFIG_RTL_HW_QOS_SUPPORT)
	rtl865x_initOutputQueue((uint8 **)netIfName);
	#endif

	#ifdef CONFIG_RTK_VOIP_QOS
	rtl8651_setAsicOutputQueueNumber(0,QNUM3);
	rtl8651_setAsicOutputQueueNumber(1,QNUM3);
	rtl8651_setAsicOutputQueueNumber(2,QNUM3);
	rtl8651_setAsicOutputQueueNumber(3,QNUM3);
	rtl8651_setAsicOutputQueueNumber(4,QNUM3);
	rtl8651_setAsicOutputQueueNumber(6,QNUM2);
	REG32(CPUQIDMCR0)=0x55550000;
	#if defined(CONFIG_RTL_8197F)
	rtl8651_setAsicPriorityDecision(2, 1, 2, 1, 1, 1);
	#else
	rtl8651_setAsicPriorityDecision(2, 1, 2, 1, 1);
	#endif
	rtl8651_resetAsicOutputQueue();
	#endif


	#if defined (CONFIG_RTL_UNKOWN_UNICAST_CONTROL)
	{
		rtl865x_tblAsicDrv_rateLimitParam_t	asic_rl;
		/*
	  	* Designer said: The time unit used to achieve rate limit is 1.67s (5/3), hence here we change
	  	* the time unit to 1 sec.
	  	*/
		bzero(&asic_rl, sizeof(rtl865x_tblAsicDrv_rateLimitParam_t));
		asic_rl.maxToken			= RTL_MAC_REFILL_TOKEN;
		asic_rl.refill_number		= RTL_MAC_REFILL_TOKEN;
		asic_rl.t_intervalUnit		= 1;
		asic_rl.t_remainUnit		= 1;
		asic_rl.token				= RTL_MAC_REFILL_TOKEN;
		rtl8651_setAsicRateLimitTable(0, &asic_rl);

		macRecordIdx = 0;
		bzero(macRecord, RTL_MAC_RECORD_NUM*sizeof(rtlMacRecord));

		for(i=0;i<RTL_MAC_RECORD_NUM;i++)
		{
			init_timer(&macRecord[i].timer);
			macRecord[i].timer.function = rtl_unkownUnicastTimer;
		}

		WRITE_MEM32(TEACR, (READ_MEM32(TEACR)|EnRateLimitTbAging));
	}
	#endif

	return SUCCESS;
}

#if defined (CONFIG_RTL_UNKOWN_UNICAST_CONTROL)
static void rtl_unkownUnicastTimer(unsigned long data)
{
	rtlMacRecord	*record;
	rtl865x_AclRule_t	rule;

	record = (rtlMacRecord*)data;
	bzero((void*)&rule,sizeof(rtl865x_AclRule_t));
	rule.ruleType_ = RTL865X_ACL_MAC;
	rule.pktOpApp_ = RTL865X_ACL_ONLY_L2;
	rule.actionType_ = RTL865X_ACL_DROP_RATE_EXCEED_PPS;
	memset(rule.dstMacMask_.octet, 0xff, ETHER_ADDR_LEN);
	memcpy(rule.dstMac_.octet, record->mac, ETHER_ADDR_LEN);
	rtl865x_del_acl(&rule, lanIfName, RTL865X_ACL_SYSTEM_USED);

	bzero(record, sizeof(rtlMacRecord));
	init_timer(&record->timer);
	record->timer.function = rtl_unkownUnicastTimer;
}

static void	rtl_unkownUnicastUpdate(uint8 *mac)
{
	int	idx;
	rtl865x_AclRule_t	rule;

	for(idx=0;idx<RTL_MAC_RECORD_NUM;idx++)
	{
		if (macRecord[idx].enable==0||memcmp(mac, macRecord[idx].mac, ETHER_ADDR_LEN))
			continue;

		/*	The mac has already recorded	*/
		if (macRecord[idx].cnt==RTL_MAC_THRESHOLD||++macRecord[idx].cnt<RTL_MAC_THRESHOLD)
			return;

		break;
	}

	/*	add/del the rules at lan side */
	bzero((void*)&rule,sizeof(rtl865x_AclRule_t));
	rule.ruleType_ = RTL865X_ACL_MAC;
	rule.pktOpApp_ = RTL865X_ACL_ONLY_L2;
	rule.actionType_ = RTL865X_ACL_DROP_RATE_EXCEED_PPS;
	memset(rule.dstMacMask_.octet, 0xff, ETHER_ADDR_LEN);

	if (idx==RTL_MAC_RECORD_NUM)
	{
		if (macRecord[macRecordIdx].enable!=0&&macRecord[macRecordIdx].cnt>RTL_MAC_THRESHOLD)
		{
			memcpy(rule.dstMac_.octet, macRecord[macRecordIdx].mac, ETHER_ADDR_LEN);
			rtl865x_del_acl(&rule, lanIfName, RTL865X_ACL_SYSTEM_USED);
			init_timer(&macRecord[macRecordIdx].timer);
			macRecord[macRecordIdx].timer.function = rtl_unkownUnicastTimer;
		}
		else
		{
			macRecord[macRecordIdx].enable = 1;
		}
		macRecord[macRecordIdx].cnt = 0;
		memcpy(macRecord[macRecordIdx].mac, mac, ETHER_ADDR_LEN);
		macRecordIdx = (macRecordIdx+1)&(RTL_MAC_RECORD_NUM-1);
	}
	else
	{
		memcpy(rule.dstMac_.octet, mac, ETHER_ADDR_LEN);
		rtl865x_add_acl(&rule, lanIfName, RTL865X_ACL_SYSTEM_USED);
		macRecord[idx].timer.data = (unsigned long)&(macRecord[idx]);
		mod_timer(&macRecord[idx].timer, jiffies+RTL_MAC_TIMEOUT);
	}
}
#endif

#if defined (CONFIG_RTL_IGMP_SNOOPING)
extern int32 rtl_setIgmpSnoopingModuleStaticRouterPortMask(uint32 moduleIndex,uint32 staticRouterPortMask);
static int re865x_reInitIgmpSetting(int mode)
{
	#if defined (CONFIG_RTL_MULTI_LAN_DEV)
	#else
	#if defined (CONFIG_RTL_HARDWARE_MULTICAST)
	rtl_multicastDeviceInfo_t devInfo;
	uint32 externalPortMask=0;
	#endif
	int32 i;
	int32 totalVlans=((sizeof(vlanconfig))/(sizeof(struct rtl865x_vlanConfig)))-1;

	for(i=0; i<totalVlans; i++)
	{
		#if defined (CONFIG_RTL_HARDWARE_MULTICAST)
		if (TRUE==vlanconfig[i].isWan)
		{
			externalPortMask |=vlanconfig[i].memPort;
		}
		else
		{
			devInfo.portMask|=vlanconfig[i].memPort ;
		}
		#endif

		if(mode==GATEWAY_MODE)
		{
			rtl_setIgmpSnoopingModuleStaticRouterPortMask(nicIgmpModuleIndex, 0);
		}
		else
		{
			rtl_setIgmpSnoopingModuleStaticRouterPortMask(nicIgmpModuleIndex, 0x01);
		}

		//rtl_setIgmpSnoopingModuleUnknownMCastFloodMap(nicIgmpModuleIndex, 0x0);
		rtl_setIpv4UnknownMCastFloodMap(nicIgmpModuleIndex, 0xFFFFFFFF);
#if defined(CONFIG_RTL_MLD_SNOOPING)
		rtl_setIpv6UnknownMCastFloodMap(nicIgmpModuleIndex, 0xFFFFFFFF);
#endif
	}
	#endif

	#if defined (CONFIG_RTL_HARDWARE_MULTICAST)
	rtl865x_reinitMulticast();
#if defined (CONFIG_RTL_8198C) || defined (CONFIG_RTL_8197F)
	rtl8198C_reinitMulticastv6();
#endif
	//rtl865x_setMulticastExternalPortMask(externalPortMask);
	rtl865x_setMulticastExternalPortMask(0);
	#endif
	rtl865x_igmpSyncLinkStatus();
	return SUCCESS;
}
#endif

#if defined (CONFIG_RTL_MULTI_LAN_DEV)
unsigned int rtl865x_getEthDevLinkStatus(struct net_device *dev)
{
	if(dev!=NULL)
	{
		struct dev_priv *cp =NETDRV_PRIV(dev);
		return (cp->portmask & rtl865x_getPhysicalPortLinkStatus());

	}
	else
	{
		return 0;
	}
}
#endif

#ifdef CONFIG_RTL_LAYERED_DRIVER_L2
extern int32 rtl865x_layer2_reinit(void);
#endif

static int32 rtl_reinit_hw_table(void)
{
	/*re-init sequence eventmgr->l4->l3->l2->common is to make sure delete asic entry,
	if not following this sequence,
	some asic entry can't be deleted due to reference count is not zero*/

	/*to-do:in each layer reinit function, memset all software entry to zero,
	and force to clear all asic entry of own module,
	then the re-init sequence can be common->l2->l3->l4 */
	/* FullAndSemiReset should not be called here
	  * it will make switch core action totally wrong
		*/

	/*event management */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
	unsigned long flags = 0;
	SMP_LOCK_ETH(flags);
#endif
	rtl865x_reInitEventMgr();

	/*l4*/
	#if defined(CONFIG_RTL_LAYERED_DRIVER_L4)
	rtl865x_nat_reinit();
	#endif

	/*l3*/
	#ifdef CONFIG_RTL_LAYERED_DRIVER_L3
	rtl865x_reinitRouteTable();
	rtl865x_reinitNxtHopTable();
	rtl865x_reinitIpTable();
	rtl865x_reinitPppTable();
	rtl865x_arp_reinit();
	#endif

	#if defined(CONFIG_RTL_HARDWARE_IPV6_SUPPORT)
	rtl8198c_reinitIpv6RouteTable();
	rtl8198c_reinitIpv6NxtHopTable();
	rtl8198c_ipv6_arp_reinit();
	#endif

	/*l2*/
	#ifdef CONFIG_RTL_LAYERED_DRIVER_L2
	rtl865x_layer2_reinit();
	#endif

	/*common*/
	rtl865x_reinitNetifTable();
	rtl865x_reinitVlantable();
	rtl865x_reinit_acl();

	/*queue id & rx ring descriptor mapping*/
	//Use REG32 instead of REG16 because CPUQDM4 is set to 0 unexpectedly when use REG16
	REG32(CPUQDM0)=QUEUEID1_RXRING_MAPPING|(QUEUEID0_RXRING_MAPPING<<16);
	REG32(CPUQDM2)=QUEUEID3_RXRING_MAPPING|(QUEUEID2_RXRING_MAPPING<<16);
	REG32(CPUQDM4)=QUEUEID5_RXRING_MAPPING|(QUEUEID4_RXRING_MAPPING<<16);

	WRITE_MEM32(PLITIMR,0);

	rtl8651_setAsicOperationLayer(2);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
		SMP_UNLOCK_ETH(flags);
#endif

	return SUCCESS;

}
#if defined(CONFIG_RTK_VLAN_SUPPORT) || defined (CONFIG_RTL_MULTI_LAN_DEV)
static int rtl_config_perport_perdev_vlanconfig(int mode)
{
#if defined(CONFIG_RTD_1295_HWNAT)
	if((mode == BRIDGE_MODE) || (mode== WISP_MODE))
	{
		vlanconfig[0] .vid = RTL_LANVLANID;
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]))->id = RTL_LANVLANID; //eth0
	}
	else
	{
		vlanconfig[0].vid = RTL_WANVLANID;
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]))->id = RTL_WANVLANID; //eth0
	}
	vlanconfig[0].memPort = RTL_WANPORT_MASK;
	vlanconfig[0].untagSet= RTL_WANPORT_MASK;
	((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]))->portmask = RTL_WANPORT_MASK; //eth0
	((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]))->portnum = 1;

	vlanconfig[1].vid = RTL_LANVLANID;
	vlanconfig[1].memPort = RTL_LANPORT_MASK_1;
	vlanconfig[1].untagSet= RTL_LANPORT_MASK_1;
	((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]))->portmask = RTL_LANPORT_MASK_1; //eth1
	((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]))->id = RTL_LANVLANID_1; //eth1
	((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]))->portnum = 1;

	vlanconfig[2] .vid = RTL_LANVLANID;
	vlanconfig[2].memPort = RTL_LANPORT_MASK_5;
	vlanconfig[2].untagSet = RTL_LANPORT_MASK_5;
	((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[2]))->portmask = RTL_LANPORT_MASK_5; //eth2
	((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[2]))->id = RTL_LANVLANID_5; //eth2
	((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[2]))->portnum = 1;
#else //defined(CONFIG_RTD_1295_HWNAT)
	vlanconfig[0].vid = RTL_LANVLANID;
	vlanconfig[0].memPort = RTL_LANPORT_MASK_4;
	vlanconfig[0].untagSet= RTL_LANPORT_MASK_4;
	((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]))->portmask = RTL_LANPORT_MASK_4; //eth0
	((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]))->id = RTL_LANVLANID_1; //eth0
	((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]))->portnum = 1;

	if((mode == BRIDGE_MODE) || (mode== WISP_MODE))
	{
		#if defined(CONFIG_RTL_IVL_SUPPORT)
		vlanconfig[1] .vid = RTL_WANVLANID;
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]))->id = RTL_WANVLANID;
		#else
		vlanconfig[1] .vid = RTL_LANVLANID;
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]))->id = RTL_LANVLANID;
		#endif
	}
	else
	{
		vlanconfig[1] .vid = RTL_WANVLANID;
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]))->id = RTL_WANVLANID;
	}
	#if defined(CONFIG_RTL_AP_PACKAGE)
	vlanconfig[1].memPort = RTL_LANPORT_MASK_0;
	vlanconfig[1].untagSet= RTL_LANPORT_MASK_0;
	#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0))
	((struct dev_priv *)_rtl86xx_dev.dev[1]->priv)->portmask = RTL_LANPORT_MASK_0; //eth1
	#endif
	#else
	vlanconfig[1].memPort = RTL_WANPORT_MASK;
	vlanconfig[1].untagSet= RTL_WANPORT_MASK;
	((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]))->portmask = RTL_WANPORT_MASK; //eth1
	//((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]))->id = RTL_WANVLANID; //eth1
	#endif
	((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]))->portnum = 1;

	vlanconfig[2] .vid = RTL_LANVLANID;
	vlanconfig[2].memPort = RTL_LANPORT_MASK_3;
	vlanconfig[2].untagSet = RTL_LANPORT_MASK_3;
	((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[2]))->portmask = RTL_LANPORT_MASK_3; //eth2
	((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[2]))->id = RTL_LANVLANID_2; //eth2
	((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[2]))->portnum = 1;

	vlanconfig[3] .vid = RTL_LANVLANID;
	vlanconfig[3].memPort = RTL_LANPORT_MASK_2;
	vlanconfig[3].untagSet = RTL_LANPORT_MASK_2;
	((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[3]))->portmask = RTL_LANPORT_MASK_2; //eth3
	((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[3]))->id = RTL_LANVLANID_3; //eth3
	((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[3]))->portnum = 1;

	vlanconfig[4] .vid = RTL_LANVLANID;
	vlanconfig[4].memPort = RTL_LANPORT_MASK_1;
	vlanconfig[4].untagSet = RTL_LANPORT_MASK_1;
	((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[4]))->portmask = RTL_LANPORT_MASK_1; //eth4
	((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[4]))->id = RTL_LANVLANID_4; //eth4
	((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[4]))->portnum = 1;

	#if defined(CONFIG_8198_PORT5_GMII)
	vlanconfig[5] .vid = RTL_LANVLANID;
	vlanconfig[5].memPort = RTL_LANPORT_MASK_5;
	vlanconfig[5].untagSet = RTL_LANPORT_MASK_5;
	((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[5]))->portmask = RTL_LANPORT_MASK_5; //eth5
	((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[5]))->id = RTL_LANVLANID_5; //eth5
	((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[5]))->portnum = 1;
	#endif
#endif //defined(CONFIG_RTD_1295_HWNAT)
	return SUCCESS;
}
#endif

#if !defined (CONFIG_RTL_MULTI_LAN_DEV)
static int rtl_config_lanwan_dev_vlanconfig(int mode)
{
#if !defined(CONFIG_RTD_1295_HWNAT)
#if defined(CONFIG_RTL_8197F)
	#if !defined(CONFIG_RTL_8367R_SUPPORT) && !defined(CONFIG_RTL_8211F_SUPPORT)
	extern uint32 rtl819x_bond_option(void);
	if (rtl819x_bond_option() == 3)
		return SUCCESS;
	#endif
#endif
#endif //!defined(CONFIG_RTD_1295_HWNAT)

	/*lan config*/
#if defined (CONFIG_RTK_VLAN_SUPPORT)
	{
		vlanconfig[2].vid = 0; //eth2
		vlanconfig[3].vid = 0; //eth3
		vlanconfig[4].vid = 0; //eth4
		#if defined(CONFIG_8198_PORT5_GMII)
		vlanconfig[5].vid = 0;
		#endif
	}
#endif

#if defined (CONFIG_RTL_IVL_SUPPORT)
	#if defined(CONFIG_POCKET_ROUTER_SUPPORT)
		if(mode == GATEWAY_MODE)
		{
			vlanconfig[0].memPort = 0;
			vlanconfig[0].vid=RTL_LANVLANID;
			vlanconfig[0].untagSet = 0;
			((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]))->portmask = 0; //eth0
			((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]))->id = RTL_LANVLANID;
			vlanconfig[1].vid = RTL_WANVLANID;
			vlanconfig[1].memPort = RTL_WANPORT_MASK;
			vlanconfig[1].untagSet = RTL_WANPORT_MASK;
			((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]))->portmask = RTL_WANPORT_MASK; //eth1
			((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]))->id = RTL_WANVLANID; //eth1
		}
		else
		{
			vlanconfig[0].memPort = RTL_LANPORT_MASK;
			vlanconfig[0].vid=RTL_LANVLANID;
			vlanconfig[0].untagSet = RTL_LANPORT_MASK;
			((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]))->portmask = RTL_LANPORT_MASK; //eth0
			((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]))->id = RTL_LANVLANID;

			vlanconfig[1].vid = 0;
			vlanconfig[1].memPort = 0;
			vlanconfig[1].untagSet = 0;
			((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]))->portmask = 0; //eth1
			((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]))->id = RTL_WANVLANID; //eth1
		}

	#else //else CONFIG_POCKET_ROUTER_SUPPORT
		vlanconfig[0].memPort = RTL_LANPORT_MASK;
		vlanconfig[0].vid=RTL_LANVLANID;
		vlanconfig[0].untagSet = RTL_LANPORT_MASK;
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]))->portmask = RTL_LANPORT_MASK; //eth0
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]))->id = RTL_LANVLANID;

		vlanconfig[1].vid = RTL_WANVLANID;
		vlanconfig[1].memPort = RTL_WANPORT_MASK;
		vlanconfig[1].untagSet = RTL_WANPORT_MASK;
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]))->portmask = RTL_WANPORT_MASK; //eth1
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]))->id = RTL_WANVLANID; //eth1

	#endif //endif CONFIG_POCKET_ROUTER_SUPPORT

#else
	if(mode == BRIDGE_MODE || mode== WISP_MODE)
	{
		vlanconfig[0].memPort = RTL_LANPORT_MASK |RTL_WANPORT_MASK;
		vlanconfig[0].untagSet = RTL_LANPORT_MASK |RTL_WANPORT_MASK;
		vlanconfig[0].vid=RTL_LANVLANID;
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]))->portmask = RTL_LANPORT_MASK |RTL_WANPORT_MASK; //eth0
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]))->id = RTL_LANVLANID;

		vlanconfig[1].vid = 0;
		vlanconfig[1].memPort=0x0;
		vlanconfig[1].untagSet=0x0;
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]))->portmask = 0x0; //eth1
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]))->id = 0;
	}
	else
	{
		vlanconfig[0].memPort = RTL_LANPORT_MASK;
		vlanconfig[0].vid=RTL_LANVLANID;
		vlanconfig[0].untagSet = RTL_LANPORT_MASK;
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]))->portmask = RTL_LANPORT_MASK; //eth0
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]))->id = RTL_LANVLANID;

		vlanconfig[1].vid = RTL_WANVLANID;
		vlanconfig[1].memPort = RTL_WANPORT_MASK;
		vlanconfig[1].untagSet = RTL_WANPORT_MASK;
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]))->portmask = RTL_WANPORT_MASK; //eth1
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]))->id = RTL_WANVLANID; //eth1
	}
#endif

	return SUCCESS;
}
#endif

#if	defined (CONFIG_RTL_MULTI_LAN_DEV)
static int rtl_config_multi_lan_dev_vlanconfig(int mode)
{
	rtl_config_perport_perdev_vlanconfig(mode);
	return SUCCESS;

}

int rtl_is_lan_dev(struct net_device *dev)
{
	int ret = FALSE;

	if (dev == NULL)
		return ret;

	if (rtl865x_curOpMode == GATEWAY_MODE) {
		if ((strcmp(dev->name, "eth0")==0) ||(strcmp(dev->name, RTL_DRV_LAN_P1_NETIF_NAME)==0) ||
			(strcmp(dev->name, RTL_DRV_LAN_P2_NETIF_NAME)==0) ||
			(strcmp(dev->name, RTL_DRV_LAN_P3_NETIF_NAME)==0))
			ret = TRUE;
	} else if ((rtl865x_curOpMode==WISP_MODE) ||(rtl865x_curOpMode==BRIDGE_MODE)) {
		if ((strcmp(dev->name, "eth0")==0) ||(strcmp(dev->name, RTL_DRV_LAN_P1_NETIF_NAME)==0) ||
			(strcmp(dev->name, RTL_DRV_LAN_P2_NETIF_NAME)==0) ||
			(strcmp(dev->name, RTL_DRV_LAN_P3_NETIF_NAME)==0)
			#if !defined(CONFIG_RTL_IVL_SUPPORT)
			|| (strcmp(dev->name, RTL_DRV_LAN_P4_NETIF_NAME)==0)
			#endif
			)
			ret = TRUE;
	}

	return ret;
}
#endif

//#if (defined(CONFIG_RTK_VLAN_SUPPORT)||defined(CONFIG_RTL_VLAN_8021Q))&& !defined (CONFIG_RTL_MULTI_LAN_DEV)
#if (defined(CONFIG_RTK_VLAN_SUPPORT))&& !defined (CONFIG_RTL_MULTI_LAN_DEV)
static int rtl_config_rtkVlan_vlanconfig(int mode)
{
	if(!rtk_vlan_support_enable)
		rtl_config_lanwan_dev_vlanconfig(mode);
	else if(rtk_vlan_support_enable == 1)
	{
		rtl_config_perport_perdev_vlanconfig(mode);
	}
	else if(rtk_vlan_support_enable == 2)
		rtl_config_lanwan_dev_vlanconfig(mode);

	return SUCCESS;
}
#endif

static int rtl_config_operation_layer(int mode)
{
#if (defined(CONFIG_RTL_8367R_SUPPORT) || defined(CONFIG_RTL_8370_SUPPORT))&& !defined(CONFIG_RTL_CPU_TAG)
	rtl8651_setAsicOperationLayer(1);
	WRITE_MEM32( FFCR, READ_MEM32( FFCR ) | EN_UNUNICAST_TOCPU );
	return SUCCESS;
#endif

#if defined(CONFIG_RTL_LAYERED_DRIVER)
	switch(mode)
	{
		case GATEWAY_MODE:
			rtl8651_setAsicOperationLayer(4);
			break;
		case BRIDGE_MODE:
		case WISP_MODE:
			rtl8651_setAsicOperationLayer(3);
			break;
		default:
			rtl8651_setAsicOperationLayer(2);
	}
#endif
	return SUCCESS;
}

static int rtl_config_vlanconfig(int mode)
{
#if !defined(CONFIG_RTD_1295_HWNAT)
#if defined(CONFIG_RTL_8197F)
	#if !defined(CONFIG_RTL_8367R_SUPPORT) && !defined(CONFIG_RTL_8211F_SUPPORT)
	extern uint32 rtl819x_bond_option(void);
	if (rtl819x_bond_option() == 3)
		return SUCCESS;
	#endif
#endif
#endif //!defined(CONFIG_RTD_1295_HWNAT)

	#if	defined (CONFIG_RTL_MULTI_LAN_DEV)
		rtl_config_multi_lan_dev_vlanconfig(mode);
	#else
		#if defined(CONFIG_RTK_VLAN_SUPPORT)
			rtl_config_rtkVlan_vlanconfig(mode);
		#else
			rtl_config_lanwan_dev_vlanconfig(mode);
		#endif
	#endif

	return SUCCESS;

}

int32 rtl865x_changeOpMode(int mode)
{
#ifdef CONFIG_RTK_FAKE_ETH
	return SUCCESS;
#endif
#if defined (CONFIG_RTL_LOCAL_PUBLIC)
	struct rtl865x_interface_info ifInfo;
#endif

#if (defined(CONFIG_RTL_8367R_SUPPORT) || defined(CONFIG_RTL_8370_SUPPORT))&& !defined(CONFIG_RTL_CPU_TAG)
	{
	extern int RTL83XX_vlan_reinit(int mode);

	rtl8651_setAsicOperationLayer(1);
	WRITE_MEM32( FFCR, READ_MEM32( FFCR ) | EN_UNUNICAST_TOCPU );
	RTL83XX_vlan_reinit(mode);

	/*update current operation mode*/
	rtl865x_curOpMode=mode;
	return SUCCESS;
	}
#endif

#if defined(CONFIG_RTK_VLAN_SUPPORT)
	if(rtk_vlan_support_enable==0)
	{
		if(mode==rtl865x_curOpMode)
		{
			return SUCCESS;
		}
	}
#else
	if(mode==rtl865x_curOpMode)
	{
		return SUCCESS;
	}
#endif

	/*config vlan config*/
	rtl_config_vlanconfig(mode);

	if (!vlanconfig[0].vid && !vlanconfig[1].vid )
		return RTL_EINVALIDVLANID;

#if defined (CONFIG_RTL_LOCAL_PUBLIC)
	if(mode==GATEWAY_MODE)
	{
		memset(&ifInfo, 0 , sizeof(struct rtl865x_interface_info));
#if defined(CONFIG_RTL_PUBLIC_SSID)
		strcpy(ifInfo.ifname,RTL_GW_WAN_DEVICE_NAME);
#else
		strcpy(ifInfo.ifname, RTL_DRV_WAN0_NETIF_NAME);
#endif
		ifInfo.isWan=1;
		for(i=0;vlanconfig[i].vid!=0; i++)
		{
			if ((TRUE==vlanconfig[i].isWan) && (vlanconfig[i].if_type==IF_ETHER))
			{
				ifInfo.memPort|=vlanconfig[i].memPort;
				ifInfo.fid=vlanconfig[i].fid;
			}
		}
		rtl865x_setLpIfInfo(&ifInfo);

		memset(&ifInfo, 0 , sizeof(struct rtl865x_interface_info));
		strcpy(ifInfo.ifname, RTL_DRV_LAN_NETIF_NAME);
		ifInfo.isWan=0;
		for(i=0;vlanconfig[i].vid!=0;i++)
		{
			if ((FALSE==vlanconfig[i].isWan) && (vlanconfig[i].if_type==IF_ETHER))
			{
				ifInfo.memPort|=vlanconfig[i].memPort;
				ifInfo.fid=vlanconfig[i].fid;
			}
		}
		rtl865x_setLpIfInfo(&ifInfo);
	}
	else if(mode==WISP_MODE)
	{
		memset(&ifInfo, 0 , sizeof(struct rtl865x_interface_info));
		strcpy(ifInfo.ifname, RTL_PS_WLAN0_DEV_NAME);
		ifInfo.isWan=1;
		rtl865x_setLpIfInfo(&ifInfo);


		memset(&ifInfo, 0 , sizeof(struct rtl865x_interface_info));
		strcpy(ifInfo.ifname,RTL_DRV_LAN_NETIF_NAME);
		ifInfo.isWan=0;
		for(i=0;vlanconfig[i].vid!=0;i++)
		{
			if ((FALSE==vlanconfig[i].isWan) && (vlanconfig[i].if_type==IF_ETHER))
			{
				ifInfo.memPort|=vlanconfig[i].memPort;
				ifInfo.fid=vlanconfig[i].fid;
			}
		}
		rtl865x_setLpIfInfo(&ifInfo);
	}
	else
	{
		memset(&ifInfo, 0 , sizeof(struct rtl865x_interface_info));
		ifInfo.isWan=1;
		rtl865x_setLpIfInfo(&ifInfo);

		memset(&ifInfo, 0 , sizeof(struct rtl865x_interface_info));
		strcpy(ifInfo.ifname, RTL_DRV_LAN_NETIF_NAME);
		ifInfo.isWan=0;
		for(i=0;vlanconfig[i].vid!=0;i++)
		{
			if ((FALSE==vlanconfig[i].isWan) && (vlanconfig[i].if_type==IF_ETHER))
			{
				ifInfo.memPort|=vlanconfig[i].memPort;
				ifInfo.fid=vlanconfig[i].fid;
			}
		}
		rtl865x_setLpIfInfo(&ifInfo);
	}
#endif

#if defined (CONFIG_RTL_MULTI_LAN_DEV) ||defined(CONFIG_RTK_VLAN_SUPPORT) || defined(CONFIG_RTL_VLAN_8021Q)
	re865x_packVlanConfig(vlanconfig, packedVlanConfig);
#endif

	/*reinit hw tables*/
	rtl_reinit_hw_table();

#if defined (CONFIG_RTL_MULTI_LAN_DEV) ||defined(CONFIG_RTK_VLAN_SUPPORT)
	reinit_vlan_configure(packedVlanConfig);
#else
	reinit_vlan_configure(vlanconfig);
#endif

#if defined (CONFIG_RTL_IGMP_SNOOPING)
	re865x_reInitIgmpSetting(mode);
#if defined (CONFIG_RTL_MLD_SNOOPING)
#if defined(CONFIG_RTK_VLAN_SUPPORT)
	if((mldSnoopEnabled==TRUE)&& (rtk_vlan_support_enable==0))
#else
	if(mldSnoopEnabled==TRUE)
#endif
	{
#if defined (CONFIG_RTL_MULTI_LAN_DEV)
		rtl865x_addAclForMldSnooping(packedVlanConfig);
#else
		rtl865x_addAclForMldSnooping(vlanconfig);
#endif
	}
#endif
#if  defined (CONFIG_RTL_HARDWARE_MULTICAST)
	rtl_processAclForIgmpSnooping(igmpsnoopenabled);
#endif
#endif
#if defined(CONFIG_RTL_HTTP_REDIRECT_LOCAL)
	rtl865x_removeAclForHttpRedirect(packedVlanConfig);
	rtl865x_addAclForHttpRedirect(packedVlanConfig);
#endif

#if (defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)) && defined(CONFIG_RTL_IPV6READYLOGO)
	{
		rtl865x_addAclForIPV6ReadyLogo(packedVlanConfig);
	}
#endif
#if defined(CONFIG_RTL_DNS_TRAP)
	rtl_remove_Acl_for_dns_trap(packedVlanConfig);
	rtl_add_Acl_for_dns_trap(packedVlanConfig);
#endif

#if defined (CONFIG_RTL_IVL_SUPPORT)
	if(mode==GATEWAY_MODE)
	{
		WRITE_MEM32( FFCR, READ_MEM32( FFCR ) & ~EN_UNUNICAST_TOCPU );
	}
	else	if((mode==BRIDGE_MODE) ||(mode==WISP_MODE))
	{
		WRITE_MEM32( FFCR, READ_MEM32( FFCR ) | EN_UNUNICAST_TOCPU );
	}
	else
	{
		WRITE_MEM32( FFCR, READ_MEM32( FFCR ) & ~EN_UNUNICAST_TOCPU );
	}
#endif

	/*update current operation mode*/
	rtl865x_curOpMode=mode;

	//setAsicOperationLayer
	rtl_config_operation_layer(mode);
	#if defined(CONFIG_RTL_EXT_PORT_SUPPORT)
	if(rtl865x_curOpMode != GATEWAY_MODE)
		extPortEnabled =0;
	#endif
	#if defined(CONFIG_RTL_MULTIPLE_WAN)
	if(rtl865x_curOpMode != GATEWAY_MODE)
		rtl865xC_setNetDecisionPolicy(NETIF_PORT_BASED);
	#endif

#ifdef CONFIG_RTL_LAYERED_DRIVER_L3
	//always init the default route...
	if(rtl8651_getAsicOperationLayer() >2)
	{
		rtl865x_addRoute(0,0,0,RTL_DRV_WAN0_NETIF_NAME,0);
	}
#endif

#if defined(CONFIG_RTL_HARDWARE_IPV6_SUPPORT)
	if (rtl8651_getAsicOperationLayer() >2) {
		inv6_addr_t ipAddr, nexthop;
		memset(&ipAddr, 0, sizeof(inv6_addr_t));
		memset(&nexthop, 0, sizeof(inv6_addr_t));

		rtl8198c_addIpv6Route(ipAddr, 0, nexthop, RTL_DRV_WAN0_NETIF_NAME);
	}
#endif


	//checksum control register
	switch(mode)
	{
		case GATEWAY_MODE:
			WRITE_MEM32(CSCR,READ_MEM32(CSCR)&~ALLOW_L2_CHKSUM_ERR);
			WRITE_MEM32(CSCR,READ_MEM32(CSCR)&~ALLOW_L3_CHKSUM_ERR);
			WRITE_MEM32(CSCR,READ_MEM32(CSCR)&~ALLOW_L4_CHKSUM_ERR);
			break;
		case BRIDGE_MODE:
		case WISP_MODE:
			WRITE_MEM32(CSCR,READ_MEM32(CSCR)&~ALLOW_L2_CHKSUM_ERR);
			WRITE_MEM32(CSCR,READ_MEM32(CSCR)|ALLOW_L3_CHKSUM_ERR);
			WRITE_MEM32(CSCR,READ_MEM32(CSCR)|ALLOW_L4_CHKSUM_ERR);
			break;
		default:
			WRITE_MEM32(CSCR,READ_MEM32(CSCR)&~ALLOW_L2_CHKSUM_ERR);
			WRITE_MEM32(CSCR,READ_MEM32(CSCR)&~ALLOW_L3_CHKSUM_ERR);
			WRITE_MEM32(CSCR,READ_MEM32(CSCR)&~ALLOW_L4_CHKSUM_ERR);
	}

	#if defined(CONFIG_RTL_8367R_SUPPORT)
	RTL8367R_vlan_set();
	#endif

	return SUCCESS;
}


int  rtl865x_reChangeOpMode (void)
{
	unsigned long flags=0;
	//SMP_LOCK_ETH(flags);
	local_irq_save(flags);
	if(rtl865x_curOpMode==GATEWAY_MODE)
	{
		rtl865x_changeOpMode(BRIDGE_MODE);
		rtl865x_changeOpMode(GATEWAY_MODE);
	}
	else if (rtl865x_curOpMode==BRIDGE_MODE)
	{
		rtl865x_changeOpMode(GATEWAY_MODE);
		rtl865x_changeOpMode(BRIDGE_MODE);
	}
	else if (rtl865x_curOpMode==WISP_MODE)
	{
		rtl865x_changeOpMode(GATEWAY_MODE);
		rtl865x_changeOpMode(WISP_MODE);
	}
	//SMP_UNLOCK_ETH(flags);
	local_irq_restore(flags);
	return 0;
}


static int32 reinit_vlan_configure(struct rtl865x_vlanConfig new_vlanconfig[])
{
	uint16 pvid;
	int32 i, j;
	uint32 valid_port_mask = 0;
	struct rtl865x_vlanConfig *pvlanconfig = NULL;
	int32 totalVlans = 0;
	pvlanconfig = new_vlanconfig;


	/*get input vlan config entry number*/
	for(i=0; pvlanconfig[i].ifname[0] != '\0' ; i++)
	{
		if(pvlanconfig[i].vid != 0)
			totalVlans++;
	}
	//because the new_vlanconfig should be packedVlanConfig
	totalVlans = totalVlans > NETIF_NUMBER? NETIF_NUMBER:totalVlans;

	for(i=0; i<totalVlans; i++)
	{
		rtl865x_netif_t netif;

		if(pvlanconfig[i].vid == 0)
			continue;

		valid_port_mask = pvlanconfig[i].memPort;
		if(pvlanconfig[i].isWan == 0)
			valid_port_mask |= 0x100;

		/*add vlan*/
		if(pvlanconfig[i].if_type==IF_ETHER)
		{
			rtl865x_addVlan(pvlanconfig[i].vid);
			rtl865x_addVlanPortMember(pvlanconfig[i].vid,pvlanconfig[i].memPort & valid_port_mask);

#if (defined(CONFIG_RTL_8367R_SUPPORT) || defined(CONFIG_RTL_8370_SUPPORT)) && !defined(CONFIG_RTL_CPU_TAG)
			// remove untagPortMask in 8197D hardware VLAN table
			rtl865x_setVlanPortTag(pvlanconfig[i].vid,pvlanconfig[i].memPort, TRUE);
#endif
			rtl865x_setVlanFilterDatabase(pvlanconfig[i].vid,pvlanconfig[i].fid);
		}

		/*add network interface*/
		memset(&netif, 0, sizeof(rtl865x_netif_t));
		memcpy(netif.name,pvlanconfig[i].ifname,MAX_IFNAMESIZE);
		memcpy(netif.macAddr.octet,pvlanconfig[i].mac.octet,ETHER_ADDR_LEN);
		netif.mtu = pvlanconfig[i].mtu;
		netif.if_type = pvlanconfig[i].if_type;
		netif.vid = pvlanconfig[i].vid;
		netif.is_wan = pvlanconfig[i].isWan;
		netif.is_slave = pvlanconfig[i].is_slave;
		#if defined (CONFIG_RTL_HARDWARE_MULTICAST)
		netif.enableRoute=1;
		#endif

		#if defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)
		netif.mtuV6 = pvlanconfig[i].mtu;
		#if defined(CONFIG_RTL_HARDWARE_IPV6_SUPPORT) ||(defined (CONFIG_RTL_MLD_SNOOPING)&&defined(CONFIG_RTL_HARDWARE_MULTICAST))
		netif.enableRouteV6 =1;   	/*jwj:enable ipv6, then enable v6 router*/
		#endif
		#endif

		rtl865x_addNetif(&netif);
#ifndef CONFIG_RTL_ISP_MULTI_WAN_SUPPORT
		if(netif.is_slave == 1)
#if defined(CONFIG_RTL_PUBLIC_SSID)
			rtl865x_attachMasterNetif(netif.name,RTL_GW_WAN_DEVICE_NAME);
#else
			rtl865x_attachMasterNetif(netif.name,RTL_DRV_WAN0_NETIF_NAME);
#endif
#endif
	}

	#if defined(CONFIG_RTL_MULTIPLE_WAN)
	rtl_config_multipleWan_netif(RTL_MULTIWAN_ADD);
	//rtl865x_addMultiCastNetif();
#elif defined(CONFIG_RTL_REDIRECT_ACL_SUPPORT_FOR_ISP_MULTI_WAN)
	rtl_init_advRt();
#endif

#if defined(CONFIG_RTL_MULTIPLE_WAN) ||defined(CONFIG_RTL_MAC_BASED_NETIF)
	rtl865x_addMultiCastNetif();
	#endif

#if defined(CONFIG_RTK_VLAN_SUPPORT)
		if(rtk_vlan_support_enable == 1)
			rtl865x_enable_acl(0);
		else
			rtl865x_enable_acl(1);
#endif


	for(i=0; i<RTL8651_PORT_NUMBER + 3; i++)
	{
		/* Set each port's PVID */
		for(j=0,pvid=0; pvlanconfig[j].vid != 0; j++)
		{
			if ( (1<<i) & pvlanconfig[j].memPort )
			{
				pvid = pvlanconfig[j].vid;
				break;
			}
		}

		if (pvid!=0)
		{
			CONFIG_CHECK(rtl8651_setAsicPvid(i,pvid));
			rtl865x_setPortToNetif(pvlanconfig[j].ifname, i);
		}
	}

#if defined(CONFIG_RTL_HW_VLAN_SUPPORT)
		if(rtl_hw_vlan_enable)
		{
#ifdef CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT
			if(RTK_NAT_WAN_TAGGED){
#else
			if((hw_vlan_info[1].vlan_port_enabled)&&hw_vlan_info[1].vlan_port_tag){
#endif
					 rtl865x_setVlanPortTag(vlanconfig[1].vid,RTL_WANPORT_MASK,1); //eth1 vlan port 1 tag
					 swNic_setVlanPortTag(RTL_WANPORT_MASK);//packet from CPU, HW add tag
			}
		else
			swNic_setVlanPortTag(0);

				if((hw_vlan_info[0].vlan_port_enabled)&&(hw_vlan_info[0].vlan_port_bridge == 1)){
#ifdef CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT
			if(hw_vlan_info[0].vlan_port_tag == 0){
			   	if(RTK_NAT_WAN_TAGGED)
			   	{
					rtl8651_setAsicPvid(RTK_WAN_PORT_IDX,hw_vlan_info[0].vlan_port_vid);
					rtl865x_setVlanPortTag(hw_vlan_info[0].vlan_port_vid,  RTL_CPUPORT_MASK,1); //to idefiy untag from bridge
			   	}
				else // nat and bridge all untag
				{
					rtl865x_addVlanPortMember(vlanconfig[1].vid, RTL_LANPORT_MASK_4);//mark_rus
					//rtl8651_setAsicPvid(0,vlanconfig[1].vid); //set brige port pvid to WAN netif vid ,for HW L2
					rtl8651_setAsicPvid(RTK_MASK_TO_PORT(RTL_LANPORT_MASK_4),vlanconfig[1].vid); //set brige port pvid to WAN netif vid ,for HW L2
				}
			   }
			   else	// tag brigde
				rtl865x_setVlanPortTag(hw_vlan_info[0].vlan_port_vid, RTL_WANPORT_MASK | RTL_CPUPORT_MASK,1); //wan port tag
#else
						rtl865x_addVlan(hw_vlan_info[0].vlan_port_vid);
						rtl865x_addVlanPortMember(hw_vlan_info[0].vlan_port_vid, RTL_LANPORT_MASK_4|RTL_WANPORT_MASK);
						rtl865x_setVlanPortTag(hw_vlan_info[0].vlan_port_vid, RTL_WANPORT_MASK,1); //wan port tag
			  if(hw_vlan_info[0].vlan_port_tag){
					rtl865x_setVlanPortTag(hw_vlan_info[0].vlan_port_vid, RTL_LANPORT_MASK_4,1); //wan port tag
			  }
			  rtl865x_setVlanFilterDatabase(hw_vlan_info[0].vlan_port_vid,0);
						//rtl8651_setAsicPvid(0, hw_vlan_info[0].vlan_port_vid); //port vid
						rtl8651_setAsicPvid(RTK_MASK_TO_PORT(RTL_LANPORT_MASK_4), hw_vlan_info[0].vlan_port_vid); //port vid
#endif
			  rtl865x_setPortForward(RTL_LANPORT_MASK_4, TRUE);
				}
				if((hw_vlan_info[2].vlan_port_enabled)&&(hw_vlan_info[2].vlan_port_bridge == 1)){
#ifdef CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT
			  if(hw_vlan_info[2].vlan_port_tag == 0){
			   	if(RTK_NAT_WAN_TAGGED)
			   	{
					rtl8651_setAsicPvid(RTK_WAN_PORT_IDX,hw_vlan_info[2].vlan_port_vid);
					rtl865x_setVlanPortTag(hw_vlan_info[2].vlan_port_vid,  RTL_CPUPORT_MASK,1); //to idefiy untag from bridge
			   	}
				else
				{
					rtl865x_addVlanPortMember(vlanconfig[1].vid, RTL_LANPORT_MASK_3);//mark_rus
					//rtl8651_setAsicPvid(1,vlanconfig[1].vid); //set brige port pvid to WAN netif vid
					rtl8651_setAsicPvid(RTK_MASK_TO_PORT(RTL_LANPORT_MASK_3),vlanconfig[1].vid); //set brige port pvid to WAN netif vid
				}

			   }
			   else	// tag brigde
				rtl865x_setVlanPortTag(hw_vlan_info[2].vlan_port_vid, RTL_WANPORT_MASK | RTL_CPUPORT_MASK,1); //wan port tag
#else
						rtl865x_addVlan(hw_vlan_info[2].vlan_port_vid);
						rtl865x_addVlanPortMember(hw_vlan_info[2].vlan_port_vid, RTL_LANPORT_MASK_3|RTL_WANPORT_MASK);
						rtl865x_setVlanPortTag(hw_vlan_info[2].vlan_port_vid, RTL_WANPORT_MASK,1); //wan port tag
			  if(hw_vlan_info[2].vlan_port_tag){
					 rtl865x_setVlanPortTag(hw_vlan_info[2].vlan_port_vid, RTL_LANPORT_MASK_3,1); //wan port tag
			  }
			  rtl865x_setVlanFilterDatabase(hw_vlan_info[2].vlan_port_vid,0);
						//rtl8651_setAsicPvid(1, hw_vlan_info[2].vlan_port_vid); //port vid
						rtl8651_setAsicPvid(RTK_MASK_TO_PORT(RTL_LANPORT_MASK_3), hw_vlan_info[2].vlan_port_vid); //port vid
#endif
						rtl865x_setPortForward(RTL_LANPORT_MASK_3, TRUE);
				}
				if((hw_vlan_info[3].vlan_port_enabled)&&(hw_vlan_info[3].vlan_port_bridge == 1)){
#ifdef CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT
			  if(hw_vlan_info[3].vlan_port_tag == 0){
			   	if(RTK_NAT_WAN_TAGGED)
			   	{
					rtl8651_setAsicPvid(RTK_WAN_PORT_IDX,hw_vlan_info[3].vlan_port_vid);
					rtl865x_setVlanPortTag(hw_vlan_info[3].vlan_port_vid,  RTL_CPUPORT_MASK,1); //to idefiy untag from bridge
			   	}
				else
				{
					rtl865x_addVlanPortMember(vlanconfig[1].vid, RTL_LANPORT_MASK_2);//mark_rus
					//rtl8651_setAsicPvid(2,vlanconfig[1].vid); //set brige port pvid to WAN netif vid
					rtl8651_setAsicPvid(RTK_MASK_TO_PORT(RTL_LANPORT_MASK_2),vlanconfig[1].vid); //set brige port pvid to WAN netif vid
				}
			   }
			   else	// tag brigde
				rtl865x_setVlanPortTag(hw_vlan_info[3].vlan_port_vid, RTL_WANPORT_MASK | RTL_CPUPORT_MASK,1); //wan port tag
#else
						rtl865x_addVlan(hw_vlan_info[3].vlan_port_vid);
						rtl865x_addVlanPortMember(hw_vlan_info[3].vlan_port_vid, RTL_LANPORT_MASK_2|RTL_WANPORT_MASK);
						rtl865x_setVlanPortTag(hw_vlan_info[3].vlan_port_vid, RTL_WANPORT_MASK,1); //wan port tag
			  if(hw_vlan_info[3].vlan_port_tag){
				 	rtl865x_setVlanPortTag(hw_vlan_info[3].vlan_port_vid, RTL_LANPORT_MASK_2,1); //wan port tag
			  }
			  rtl865x_setVlanFilterDatabase(hw_vlan_info[3].vlan_port_vid,0);
						//rtl8651_setAsicPvid(2, hw_vlan_info[3].vlan_port_vid); //port vid
						rtl8651_setAsicPvid(RTK_MASK_TO_PORT(RTL_LANPORT_MASK_2), hw_vlan_info[3].vlan_port_vid); //port vid
#endif
						rtl865x_setPortForward(RTL_LANPORT_MASK_2, TRUE);
				}
		 if((hw_vlan_info[4].vlan_port_enabled)&&(hw_vlan_info[4].vlan_port_bridge == 1)){
#ifdef CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT
			 if(hw_vlan_info[4].vlan_port_tag == 0){
			   	if(RTK_NAT_WAN_TAGGED)
			   	{
					rtl8651_setAsicPvid(RTK_WAN_PORT_IDX,hw_vlan_info[4].vlan_port_vid);
					rtl865x_setVlanPortTag(hw_vlan_info[4].vlan_port_vid,  RTL_CPUPORT_MASK,1); //to idefiy untag from bridge
			   	}
				else
				{
					rtl865x_addVlanPortMember(vlanconfig[1].vid, RTL_LANPORT_MASK_1);//mark_rus
					//rtl8651_setAsicPvid(3,vlanconfig[1].vid); //set brige port pvid to WAN netif vid
					rtl8651_setAsicPvid(RTK_MASK_TO_PORT(RTL_LANPORT_MASK_1),vlanconfig[1].vid); //set brige port pvid to WAN netif vid
				}
			   }
			   else	// tag brigde
				rtl865x_setVlanPortTag(hw_vlan_info[4].vlan_port_vid, RTL_WANPORT_MASK | RTL_CPUPORT_MASK,1); //wan port tag
#else
						rtl865x_addVlan(hw_vlan_info[4].vlan_port_vid);
						rtl865x_addVlanPortMember(hw_vlan_info[4].vlan_port_vid, RTL_LANPORT_MASK_1|RTL_WANPORT_MASK);
						rtl865x_setVlanPortTag(hw_vlan_info[4].vlan_port_vid, RTL_WANPORT_MASK,1); //wan port tag
			  if(hw_vlan_info[4].vlan_port_tag){
				 	rtl865x_setVlanPortTag(hw_vlan_info[4].vlan_port_vid, RTL_LANPORT_MASK_1,1); //wan port tag
			  }
			  rtl865x_setVlanFilterDatabase(hw_vlan_info[4].vlan_port_vid,0);
						//rtl8651_setAsicPvid(3, hw_vlan_info[4].vlan_port_vid); //port vid
						rtl8651_setAsicPvid(RTK_MASK_TO_PORT(RTL_LANPORT_MASK_1), hw_vlan_info[4].vlan_port_vid); //port vid
#endif
						rtl865x_setPortForward(RTL_LANPORT_MASK_1, TRUE);
				}
#ifdef RTK_MANAGE_VLAN_IF
		//if manage vlan enable
		 rtk_add_manage_vlan();
#endif
		}
		else{
				swNic_setVlanPortTag(0);
		}
#endif

#if 0//def CONFIG_RTL_STP
	re865x_stp_mapping_reinit();
#endif

	rtl_config_operation_layer(rtl865x_curOpMode);

	#if defined(CONFIG_RTL_MULTIPLE_WAN)
	if(rtl865x_curOpMode != GATEWAY_MODE)
		rtl865xC_setNetDecisionPolicy(NETIF_PORT_BASED);
	#endif
	return SUCCESS;
}
#if defined(CONFIG_RTL_REPORT_LINK_STATUS)
#ifdef CONFIG_RTL_PROC_NEW
static int rtk_link_event_read(struct seq_file *s, void *v)
#else
static int32 rtk_link_event_read( char *page, char **start, off_t off, int count, int *eof, void *data )
#endif
{
#ifdef CONFIG_RTL_PROC_NEW
	seq_printf(s, "%d\n",rtk_linkEvent);
	return 0;
#else
		int len;
	len = sprintf(page, "%d\n",rtk_linkEvent);

	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count)
		len = count;
	if (len<0)
	  	len = 0;

	return len;
#endif
}
static int32 rtk_link_event_write( struct file *filp, const char *buff,unsigned long len, void *data )
{
	char 		tmpbuf[4];
	if (buff && !copy_from_user(tmpbuf, buff, len))
	{
		if(0 == (tmpbuf[0]-'0'))
			rtk_linkEvent = 0;
	}
	return len;
}



#ifdef CONFIG_RTL_PROC_NEW
static int rtk_link_status_read(struct seq_file *s, void *v)
#else
static int32 rtk_link_status_read( char *page, char **start, off_t off, int count, int *eof, void *data )
#endif
{
#ifdef CONFIG_RTL_PROC_NEW
	struct net_device *dev = (struct net_device *)(s->private);
#else
	struct net_device *dev = (struct net_device *)data;
	int len;
#endif
	struct dev_priv *cp=NETDRV_PRIV(dev);
	int rtk_linkStatus=0;
#ifdef CONFIG_AUTO_DHCP_CHECK
	int eth0_portLinkStatus =0;
#endif

#if defined(CONFIG_RTL_8196C) && defined(CONFIG_SUPPORT_RUSSIA_FEATURES)
	static int flag=0;
	if(flag==0)
	{
		curLinkPortMask=rtl865x_getPhysicalPortLinkStatus();

//		panic_printk("####%s:%d curLinkPortMask=%u\n",__FUNCTION__,__LINE__,curLinkPortMask);
		flag++;
	}
#endif

	if(cp && ((cp->portmask & curLinkPortMask & ((1<<RTL8651_MAC_NUMBER)-1))!=0))
	{
	#ifdef CONFIG_AUTO_DHCP_CHECK
		if((memcmp(cp->dev->name,"eth0",4)==0)
			&&(curLinkPortMask&RTL_LANPORT_MASK_4)){
			eth0_portLinkStatus =1;
		}
	#endif
		rtk_linkStatus=1;
	}
	else
	{
		rtk_linkStatus=0;
	}


#ifdef CONFIG_RTL_PROC_NEW
	seq_printf(s, "%d\n",rtk_linkStatus);
	return 0;
#else
	len = sprintf(page, "%d\n",rtk_linkStatus);
#ifdef CONFIG_AUTO_DHCP_CHECK
	len += sprintf(page+len, "%d %x",eth0_portLinkStatus,curLinkPortMask);
	len += sprintf(page+len, "\n");
#endif
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count)
		len = count;
	if (len<0)
	  	len = 0;

	return len;
#endif
}
static int32 rtk_link_status_write( struct file *filp, const char *buff,unsigned long len, void *data )
{
	return len;
}
#endif

#ifdef CONFIG_RTL_JUMBO_FRAME
#ifdef CONFIG_RTL_PROC_NEW
static int32 jumbo_frame_support_read(struct seq_file *s, void *v)
#else
static int32 jumbo_frame_support_read( char *page, char **start, off_t off, int count, int *eof, void *data )
#endif
{
#ifdef CONFIG_RTL_PROC_NEW
	seq_printf(s, "%d %d\n", jumboFrameFlag, jumboFrameSize);
	return 0;
#else
	int len;

	len = sprintf(page, "%d %d\n",jumboFrameFlag,jumboFrameSize);

	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count)
		len = count;
	if (len<0)
	  	len = 0;

	return len;
#endif
}

extern int32 rtl865x_set_jumbo_frame_size(uint32 size, int op_mode);

static int32 jumbo_frame_support_write( struct file *filp, const char *buff,unsigned long len, void *data )
{
	char 		tmpbuf[32];
	int32	size;
	if (buff && !copy_from_user(tmpbuf, buff, len))
	{
		sscanf(tmpbuf,"%d %d",&jumboFrameFlag,&jumboFrameSize);
		if(jumboFrameFlag){
			switch(jumboFrameSize){
				case 1500:
					size = JUMBO_FRAME_SIZE_NORMAL;
					break;
				case 9000:
					size = JUMBO_FRAME_SIZE_9K;
					break;
				case 16000:
					size = JUMBO_FRAME_SIZE_16K;
					break;
				default:
					printk("Wrong Jumbo frame size:%d\n",jumboFrameSize);
					return len;
			}
			rtl865x_set_jumbo_frame_size(size, rtl865x_curOpMode);
		}
		else{
			rtl865x_set_jumbo_frame_size(JUMBO_FRAME_SIZE_NORMAL, rtl865x_curOpMode);
		}
	}

	return len;
}
#endif

#ifdef CONFIG_RTL_HARDWARE_IPV6_SUPPORT
#ifdef CONFIG_RTL_PROC_NEW
static int rtk_hwL3v6_read(struct seq_file *s, void *v)
{
	seq_printf(s, "%s %d\n", "rtk_hwL3v6_enable:",rtk_hwL3v6_enable);
	return 0;
}
#else
static int32 rtk_hwL3v6_read( char *page, char **start, off_t off, int count, int *eof, void *data )
{
	int len;
	len = sprintf(page, "%s %d\n", "rtk_hwL3v6_enable:",rtk_hwL3v6_enable);


	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count)
		len = count;
	if (len<0)
	  	len = 0;

	return len;
}
#endif
static int32 rtk_hwL3v6_write( struct file *filp, const char *buff,unsigned long len, void *data )
{
	char  tmpbuf[32];

	if (buff && !copy_from_user(tmpbuf, buff, len))
	{
		tmpbuf[len] = '\0';

		if(tmpbuf[0] == '1')
		{
			rtk_hwL3v6_enable = 1;
			//enable netif's ipv6 routing
			rtl_set_netif_v6_route(rtk_hwL3v6_enable);
			//rtl_config_operation_layer(rtl865x_curOpMode);
		}
		else if(tmpbuf[0] == '0')
		{
			rtk_hwL3v6_enable = 0;
			//disbale netif's ipv6 routing
			rtl_set_netif_v6_route(rtk_hwL3v6_enable);
			//rtl8651_setAsicOperationLayer(2);
		}
		else
		{
			printk("current support: 0/1\n");
			return len;
		}
	}

	return len;
}
#endif

#if defined(CONFIG_RTK_VLAN_SUPPORT)
#ifdef CONFIG_RTK_VLAN_WAN_TAG_SUPPORT
static int32 rtk_vlan_wan_tag_getportmask(int bridge_port)
{
	int32 port_mask = 0x80;// PKTHDR_EXTPORT_LIST_P2 2=> bit 7

	if(vlan_bridge_port&(1<<3))
		port_mask |= RTL_LANPORT_MASK_1;

	if(vlan_bridge_port&(1<<2))
		port_mask |= RTL_LANPORT_MASK_2;

	if(vlan_bridge_port&(1<<1))
		port_mask |= RTL_LANPORT_MASK_3;

	if(vlan_bridge_port&(1<<0))
		port_mask |= RTL_LANPORT_MASK_4;

	return port_mask;
}
static int32 rtk_vlan_wan_tag_support_write( struct file *filp, const char *buff,unsigned long len, void *data )
{
	char tmpbuf[100];
	int lan_portmask;
	int num ;
	int i=0;
	int j=0;
	struct net_device *dev;
	struct dev_priv	  *dp;
	#ifdef CONFIG_RTL_IGMP_SNOOPING
	int ret = 0;
	rtl_multicastDeviceInfo_t devInfo;
	#endif

	if (buff && !copy_from_user(tmpbuf, buff, len))
	{
		num = sscanf(tmpbuf, "%d %d %d %d %d", &vlan_tag,  &vlan_host_pri, &vlan_bridge_tag, &vlan_bridge_port,&vlan_bridge_multicast_tag);
		if (num !=  5) {
				printk("invalid rtk_vlan_wan_tag_support_write parameter!\n");
				return len;
		}
		if(rtl865x_curOpMode == WISP_MODE) //not support in WISP mode
		{
		vlan_tag = 0;
		vlan_bridge_tag = 0;
		}
		else if(rtl865x_curOpMode == BRIDGE_MODE)
		{
			vlan_bridge_tag = 0;
		}
		if(vlan_bridge_tag == vlan_tag)
			vlan_bridge_tag = 0;

#if defined (CONFIG_RTL_MULTI_LAN_DEV)
		rtl_config_vlanconfig(rtl865x_curOpMode);
#else
		rtl_config_rtkVlan_vlanconfig(rtl865x_curOpMode);
#endif

		//Reset PPPoE vlan id to original
		vlanconfig[VLAN_CONFIG_PPPOE_INDEX].vid = RTL_WANVLANID;


		lan_portmask =  0;

		if(vlan_bridge_tag)
		{
			lan_portmask = rtk_vlan_wan_tag_getportmask(vlan_bridge_port);
			vlanconfig[2] .vid = vlan_bridge_tag;
			vlanconfig[2].memPort = lan_portmask|RTL_WANPORT_MASK;
			vlanconfig[2].untagSet = lan_portmask;

			//need verify
			vlanconfig[2].fid = 1;
			((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[2]))->portmask = lan_portmask|RTL_WANPORT_MASK;
			((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[2]))->id = vlan_bridge_tag; //eth2
			//vlanconfig[0].vid= RTL_LANVLANID;
			vlanconfig[0].memPort = RTL_LANPORT_MASK &(~lan_portmask);
			vlanconfig[0].untagSet = RTL_LANPORT_MASK &(~lan_portmask);
			((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]))->portmask = RTL_LANPORT_MASK &(~lan_portmask); //eth0
			//((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]))->id = RTL_LANVLANID;
		}

		if(rtl865x_curOpMode == GATEWAY_MODE && vlan_tag)
		{
			#if defined(CONFIG_RTD_1295_HWNAT)
			vlanconfig[RTL_WAN_IDX].vid = vlan_tag;
			vlanconfig[RTL_WAN_IDX].memPort = RTL_WANPORT_MASK;
			vlanconfig[RTL_WAN_IDX].untagSet = 0; // need tag
			((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[RTL_WAN_IDX]))->portmask = RTL_WANPORT_MASK; //eth1
			((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[RTL_WAN_IDX]))->id = vlan_tag; //eth1
			#else //defined(CONFIG_RTD_1295_HWNAT)
			vlanconfig[1].vid = vlan_tag;
			vlanconfig[1].memPort = RTL_WANPORT_MASK;
			vlanconfig[1].untagSet = 0; // need tag
			((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]))->portmask = RTL_WANPORT_MASK; //eth1
			((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]))->id = vlan_tag; //eth1
			#endif //defined(CONFIG_RTD_1295_HWNAT)
			//PPPOE
			vlanconfig[VLAN_CONFIG_PPPOE_INDEX].vid = vlan_tag;
		}

		re865x_packVlanConfig(vlanconfig, packedVlanConfig);
			rtl_reinit_hw_table();
		if(vlan_bridge_tag)
		{
			packedVlanConfig[2].memPort &= (~0x100); //eth2 use port 7, not port 8
			packedVlanConfig[2].untagSet &= (~0x100);
		}
		reinit_vlan_configure(packedVlanConfig);

		if(rtl865x_curOpMode == GATEWAY_MODE && vlan_tag)
		{
			rtl865x_setVlanPortTag(vlan_tag, RTL_WANPORT_MASK, true); //eth1 vlan port 1 tag
		}
		if(rtl865x_curOpMode == BRIDGE_MODE && vlan_tag)
				{
			rtl865x_addVlan(vlan_tag);
			rtl865x_addVlanPortMember(vlan_tag,RTL_LANPORT_MASK | RTL_WANPORT_MASK);
			rtl865x_setVlanPortTag(vlan_tag, (RTL_LANPORT_MASK|RTL_WANPORT_MASK)&(~0x100),1);
			rtl865x_setVlanFilterDatabase(vlan_tag,0);
		}

		if(vlan_bridge_tag)
		{
			rtl865x_setVlanPortTag(vlan_bridge_tag, RTL_WANPORT_MASK, true); //eth1 vlan port 1 tag
					if(nicIgmpModuleIndex_2==0xFFFFFFFF)
					{
						ret = rtl_registerIgmpSnoopingModule(&nicIgmpModuleIndex_2);
					#if defined (CONFIG_RTL_HARDWARE_MULTICAST)
						memset(&devInfo, 0, sizeof(rtl_multicastDeviceInfo_t ));
						strcpy(devInfo.devName, RTL_PS_ETH_NAME_ETH2);
						if(ret==0)
							 rtl_setIgmpSnoopingModuleDevInfo(nicIgmpModuleIndex_2,&devInfo);
					#endif
					}
			if(vlan_bridge_multicast_tag)
			{
				if (vlan_bridge_multicast_tag == vlan_bridge_tag)
				{
					vlan_bridge_multicast_tag = 0;
				}
				else
				{
					rtl865x_addVlan(vlan_bridge_multicast_tag);
					rtl865x_addVlanPortMember(vlan_bridge_multicast_tag,RTL_WANPORT_MASK);
					rtl865x_setVlanPortTag(vlan_bridge_multicast_tag,RTL_WANPORT_MASK,1); //wan port tag
					rtl865x_setVlanFilterDatabase(vlan_bridge_multicast_tag,1);
				}
					}
				}
				else
				{
					if(nicIgmpModuleIndex_2!=0xFFFFFFFF)
						rtl_unregisterIgmpSnoopingModule(nicIgmpModuleIndex_2);
				//rtl865x_setVlanPortTag(vlanconfig[1].vid,RTL_WANPORT_MASK,false); //eth1 vlan port 1 tag
			#ifdef CONFIG_RTL_IGMP_SNOOPING
				if(nicIgmpModuleIndex_2!=0xFFFFFFFF)
					rtl_unregisterIgmpSnoopingModule(nicIgmpModuleIndex_2);
			#endif
			}

				/*update dev port number*/
				for(i=0; vlanconfig[i].vid != 0; i++)
				{
					if (IF_ETHER!=vlanconfig[i].if_type)
					{
						continue;
					}

					dev=_rtl86xx_dev.dev[i];
					dp = NETDRV_PRIV(dev);
					dp->portnum  = 0;
					for(j=0;j<RTL8651_AGGREGATOR_NUMBER;j++)
					{
						if(dp->portmask & (1<<j))
							dp->portnum++;
					}

				}

			#if defined (CONFIG_RTL_IGMP_SNOOPING)
				re865x_reInitIgmpSetting(rtl865x_curOpMode);
				#if defined (CONFIG_RTL_MLD_SNOOPING)
				if(mldSnoopEnabled)
				{
					re865x_packVlanConfig(vlanconfig, packedVlanConfig);
					rtl865x_addAclForMldSnooping(packedVlanConfig);
				}
				#endif
				#if  defined (CONFIG_RTL_HARDWARE_MULTICAST)
				if(igmpsnoopenabled)
				{
					rtl_processAclForIgmpSnooping(igmpsnoopenabled);
				}
				#endif
			#endif

				#if defined(CONFIG_RTL_DNS_TRAP)
				re865x_packVlanConfig(vlanconfig, packedVlanConfig);
				rtl_remove_Acl_for_dns_trap(packedVlanConfig);
				rtl_add_Acl_for_dns_trap(packedVlanConfig);
				#endif
				//always init the default route...
				if(rtl8651_getAsicOperationLayer() >2)
				{
#ifdef CONFIG_RTL_LAYERED_DRIVER_L3
					rtl865x_addRoute(0,0,0,RTL_DRV_WAN0_NETIF_NAME,0);
#endif
#if defined(CONFIG_RTL_HARDWARE_IPV6_SUPPORT)
				 	{
						inv6_addr_t ipAddr, nexthop;
						memset(&ipAddr, 0, sizeof(inv6_addr_t));
						memset(&nexthop, 0, sizeof(inv6_addr_t));

						rtl8198c_addIpv6Route(ipAddr, 0, nexthop, RTL_DRV_WAN0_NETIF_NAME);
					}
#endif
				}
		WRITE_MEM32(SWTCR0,(READ_MEM32(SWTCR0)| EnUkVIDtoCPU));


		}

		return len;
}
#ifdef CONFIG_RTL_PROC_NEW
static int32 rtk_vlan_wan_tag_support_read(struct seq_file *s, void *v)
{
		seq_printf(s, "vlan_tag: %d vlan_host_pri: %d \nvlan_bridge_tag: %d, vlan_bridge_port: 0x%x vlan_bridge_stream_tag: %d\n",
		vlan_tag, vlan_host_pri, vlan_bridge_tag, vlan_bridge_port,vlan_bridge_multicast_tag);
		return 0;
}
#else
static int32 rtk_vlan_wan_tag_support_read( char *page, char **start, off_t off, int count, int *eof, void *data )
{
		int len;
		len = sprintf(page, "vlan_tag: %d vlan_host_pri: %d \nvlan_bridge_tag: %d, vlan_bridge_port: 0x%x vlan_bridge_stream_tag: %d\n",
		vlan_tag, vlan_host_pri, vlan_bridge_tag, vlan_bridge_port,vlan_bridge_multicast_tag);
		if (len <= off+count) *eof = 1;
		*start = page + off;
		len -= off;
		if (len>count)
				len = count;
		if (len<0)
				len = 0;
		return len;
}
#endif

#endif
#ifdef CONFIG_RTL_PROC_NEW
static int rtk_vlan_support_read(struct seq_file *s, void *v)
{
	seq_printf(s, "%s %d\n", "rtk_vlan_support_enable:",rtk_vlan_support_enable);
	return 0;
}
#else
static int32 rtk_vlan_support_read( char *page, char **start, off_t off, int count, int *eof, void *data )
{
	int len;
	len = sprintf(page, "%s %d\n", "rtk_vlan_support_enable:",rtk_vlan_support_enable);


	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count)
		len = count;
	if (len<0)
	  	len = 0;

	return len;
}
#endif
static int32 rtk_vlan_support_write( struct file *filp, const char *buff,unsigned long len, void *data )
{
	char 		tmpbuf[32];
	int i=0;
	int j=0;
	struct net_device *dev;
	struct dev_priv	  *dp;

	if (buff && !copy_from_user(tmpbuf, buff, len))
	{
		tmpbuf[len] = '\0';
		#if defined (CONFIG_RTL_IGMP_SNOOPING) && defined (CONFIG_RTL_MLD_SNOOPING)
		if(mldSnoopEnabled)
		{
			rtl865x_removeAclForMldSnooping(vlanconfig);
		}
		#if  defined (CONFIG_RTL_HARDWARE_MULTICAST)
		if(igmpsnoopenabled)
		{
			//remove igmp acl
			rtl_processAclForIgmpSnooping(0);
		}
		#endif
		#endif
		if(tmpbuf[0] == '0')
		{
			rtk_vlan_support_enable = 0;
			#if	defined (CONFIG_RTL_MULTI_LAN_DEV)
			rtl_config_vlanconfig(rtl865x_curOpMode);
			#else
			rtl_config_rtkVlan_vlanconfig(rtl865x_curOpMode);
			#endif
			re865x_packVlanConfig(vlanconfig, packedVlanConfig);
			rtl_reinit_hw_table();
			reinit_vlan_configure(packedVlanConfig);

			#if defined(CONFIG_RTL_8367R_SUPPORT)
			rtl865x_disableRtl8367ToCpuAcl();
			#endif

			//unknow vlan drop
			REG32(SWTCR0) &= ~(1 << 15);

#if defined(CONFIG_RTL_LAYERED_DRIVER_ACL)
			rtl865x_enable_acl(1); //enable acl feature
#endif
		}
		else if(tmpbuf[0] == '1')
		{
			rtk_vlan_support_enable = 1;


#if defined (CONFIG_RTL_MULTI_LAN_DEV)
			rtl_config_vlanconfig(rtl865x_curOpMode);
#else
			rtl_config_rtkVlan_vlanconfig(rtl865x_curOpMode);
#endif

			re865x_packVlanConfig(vlanconfig, packedVlanConfig);
			rtl_reinit_hw_table();
			reinit_vlan_configure(packedVlanConfig);

			#if defined(CONFIG_RTL_8367R_SUPPORT)
			rtl865x_enableRtl8367ToCpuAcl();
			#endif

			//unknow vid to cpu
			REG32(SWTCR0) |= 1 << 15;
#if defined(CONFIG_RTL_LAYERED_DRIVER_ACL)
			rtl865x_enable_acl(0); //disable acl feature
#endif
		}
#if defined(CONFIG_RTK_VLAN_FOR_CABLE_MODEM)
		else if(tmpbuf[0] == '2')
		{
			rtk_vlan_support_enable = 2;
			//unknow vid to cpu
			REG32(SWTCR0) |= 1 << 15;
		}
#endif
		else
		{
			printk("current support: 0/1/2\n");
			return len;
		}

		/*update dev port number*/
		for(i=0; vlanconfig[i].vid != 0; i++)
		{
			if (IF_ETHER!=vlanconfig[i].if_type)
			{
				continue;
			}

			dev=_rtl86xx_dev.dev[i];
			dp = NETDRV_PRIV(dev);
			dp->portnum  = 0;
			for(j=0;j<RTL8651_AGGREGATOR_NUMBER;j++)
			{
				if(dp->portmask & (1<<j))
					dp->portnum++;
			}

		}

	#if defined (CONFIG_RTL_IGMP_SNOOPING)
		re865x_reInitIgmpSetting(rtl865x_curOpMode);
		#if defined (CONFIG_RTL_MLD_SNOOPING)
		if(mldSnoopEnabled && (rtk_vlan_support_enable==0))
		{
			re865x_packVlanConfig(vlanconfig, packedVlanConfig);
			rtl865x_addAclForMldSnooping(packedVlanConfig);
		}
		#endif
		#if  defined (CONFIG_RTL_HARDWARE_MULTICAST)
		if(igmpsnoopenabled)
		{
			rtl_processAclForIgmpSnooping(igmpsnoopenabled);
		}
		#endif
	#endif
#if (defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)) && defined(CONFIG_RTL_IPV6READYLOGO)
		{
			rtl865x_addAclForIPV6ReadyLogo(packedVlanConfig);
		}
#endif

#if defined(CONFIG_RTL_HTTP_REDIRECT_LOCAL)
		re865x_packVlanConfig(vlanconfig, packedVlanConfig);
		rtl865x_removeAclForHttpRedirect(packedVlanConfig);
		rtl865x_addAclForHttpRedirect(packedVlanConfig);
#endif
#if defined(CONFIG_RTL_DNS_TRAP)
		re865x_packVlanConfig(vlanconfig, packedVlanConfig);
		rtl_remove_Acl_for_dns_trap(packedVlanConfig);
		rtl_add_Acl_for_dns_trap(packedVlanConfig);
#endif
		//always init the default route...
		if(rtl8651_getAsicOperationLayer() >2)
		{
#ifdef CONFIG_RTL_LAYERED_DRIVER_L3
			rtl865x_addRoute(0,0,0,RTL_DRV_WAN0_NETIF_NAME,0);
#endif
#if defined(CONFIG_RTL_HARDWARE_IPV6_SUPPORT)
		 	{
				inv6_addr_t ipAddr, nexthop;
				memset(&ipAddr, 0, sizeof(inv6_addr_t));
				memset(&nexthop, 0, sizeof(inv6_addr_t));

				rtl8198c_addIpv6Route(ipAddr, 0, nexthop, RTL_DRV_WAN0_NETIF_NAME);
			}
#endif
		}

	}
	return len;
}

#if defined(CONFIG_819X_PHY_RW) //#if defined(CONFIG_RTK_VLAN_FOR_CABLE_MODEM)
#ifdef CONFIG_RTL_PROC_NEW
static int rtl_phy_status_read(struct seq_file *s, void *v)
#else
static int32 rtl_phy_status_read( char *page, char **start, off_t off, int count, int *eof, void *data )
#endif
{
	int		len;
	uint32	regData;
	uint32	data0;
	int		port;

#ifdef CONFIG_RTL_PROC_NEW
	seq_printf(s, "Port Status:\n");
#else
	len = sprintf(page, "Port Status:\n");
#endif

	for(port=PHY0;port<CPU;port++)
	{
		regData = READ_MEM32(PSRP0+((port)<<2));

#ifdef CONFIG_RTL_PROC_NEW
		seq_printf(s, "Port%d ", port);
#else
		len += sprintf(page+len, "Port%d ", port);
#endif
		data0 = regData & PortStatusLinkUp;

		if (data0)
		{
#ifdef CONFIG_RTL_PROC_NEW
			seq_printf(s, "LinkUp | ");
#else
			len += sprintf(page+len, "LinkUp | ");
#endif
		}
		else
		{
#ifdef CONFIG_RTL_PROC_NEW
			seq_printf(s, "LinkDown\n\n");
#else
			len += sprintf(page+len, "LinkDown\n\n");
#endif
			continue;
		}
		data0 = regData & PortStatusDuplex;
#ifdef CONFIG_RTL_PROC_NEW
		seq_printf(s, "	Duplex %s | ", data0?"Enabled":"Disabled");
#else
		len += sprintf(page+len, "	Duplex %s | ", data0?"Enabled":"Disabled");
#endif
		data0 = (regData&PortStatusLinkSpeed_MASK)>>PortStatusLinkSpeed_OFFSET;
#ifdef CONFIG_RTL_PROC_NEW
		seq_printf(s, "Speed %s\n\n", data0==PortStatusLinkSpeed100M?"100M":
			(data0==PortStatusLinkSpeed1000M?"1G":
				(data0==PortStatusLinkSpeed10M?"10M":"Unkown")));
#else
		len += sprintf(page+len, "Speed %s\n\n", data0==PortStatusLinkSpeed100M?"100M":
			(data0==PortStatusLinkSpeed1000M?"1G":
				(data0==PortStatusLinkSpeed10M?"10M":"Unkown")));
#endif
	}


#ifdef CONFIG_RTL_PROC_NEW
	return 0;
#else
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count)
		len = count;
	if (len<0)
	  	len = 0;

	return len;
#endif
}



static int32 rtl_phy_status_write( struct file *filp, const char *buff,unsigned long len, void *data )
{
	char 		tmpbuf[64];
	uint32	port_mask;
	char		*strptr, *cmd_addr;
	char		*tokptr;
	int		type;
	int 		port;
	int forceMode = 0;
	int forceLink = 0;
	int forceLinkSpeed = 0;
	int forceDuplex = 0;
	uint32 advCapability = 0;
	int forwardEnable = TRUE;

	#define SPEED10M 	0
	#define SPEED100M 	1
	#define SPEED1000M 	2

	if (buff && !copy_from_user(tmpbuf, buff, len)) {
		tmpbuf[len-1] = '\0';
		strptr=tmpbuf;
		cmd_addr = strsep(&strptr," ");
		if (cmd_addr==NULL)
		{
			goto errout;
		}
		tokptr = strsep(&strptr," ");
		if (tokptr==NULL)
		{
			goto errout;
		}

		if (!memcmp(cmd_addr, "port", 4))
		{
			port_mask=simple_strtol(tokptr, NULL, 0);
			tokptr = strsep(&strptr," ");
			if (tokptr==NULL)
			{
				goto errout;
			}

			if(strcmp(tokptr,"10_half") == 0)
				type = HALF_DUPLEX_10M;
			else if(strcmp(tokptr,"100_half") == 0)
				type = HALF_DUPLEX_100M;
			else if(strcmp(tokptr,"1000_half") == 0)
				type = HALF_DUPLEX_1000M;
			else if(strcmp(tokptr,"10_full") == 0)
				type = DUPLEX_10M;
			else if(strcmp(tokptr,"100_full") == 0)
				type = DUPLEX_100M;
			else if(strcmp(tokptr,"1000_full") == 0)
				type = DUPLEX_1000M;
			else
				type = PORT_AUTO;

			tokptr =  strsep(&strptr," ");
			if(tokptr == NULL)
				goto errout;

			forwardEnable = simple_strtol(tokptr,NULL,0);

			switch(type)
			{
				case HALF_DUPLEX_10M:
				{
					forceMode=TRUE;
					forceLink=TRUE;
					forceLinkSpeed=SPEED10M;
					forceDuplex=FALSE;
					advCapability=(1<<HALF_DUPLEX_10M);
					break;
				}
				case HALF_DUPLEX_100M:
				{
					forceMode=TRUE;
					forceLink=TRUE;
					forceLinkSpeed=SPEED100M;
					forceDuplex=FALSE;
					advCapability=(1<<HALF_DUPLEX_100M);
					break;
				}
				case HALF_DUPLEX_1000M:
				{
					forceMode=TRUE;
					forceLink=TRUE;
					forceLinkSpeed=SPEED1000M;
					forceDuplex=FALSE;
					advCapability=(1<<HALF_DUPLEX_1000M);
					break;
				}
				case DUPLEX_10M:
				{
					forceMode=TRUE;
					forceLink=TRUE;
					forceLinkSpeed=SPEED10M;
					forceDuplex=TRUE;
					advCapability=(1<<DUPLEX_10M);
					break;
				}
				case DUPLEX_100M:
				{
					forceMode=TRUE;
					forceLink=TRUE;
					forceLinkSpeed=SPEED100M;
					forceDuplex=TRUE;
					advCapability=(1<<DUPLEX_100M);
					break;
				}
				case DUPLEX_1000M:
				{
					forceMode=TRUE;
					forceLink=TRUE;
					forceLinkSpeed=SPEED1000M;
					forceDuplex=TRUE;
					advCapability=(1<<DUPLEX_1000M);
					break;
				}
				default:
				{
					forceMode=FALSE;
					forceLink=TRUE;
					/*all capality*/
					advCapability=(1<<PORT_AUTO);
				}
			}


			for(port = 0; port < CPU; port++)
			{
				if((1<<port) & port_mask)
				{
					rtl865xC_setAsicEthernetForceModeRegs(port, forceMode, forceLink, forceLinkSpeed, forceDuplex);

					/*Set PHY Register*/
					rtl8651_setAsicEthernetPHYSpeed(port,forceLinkSpeed);
					rtl8651_setAsicEthernetPHYDuplex(port,forceDuplex);
					rtl8651_setAsicEthernetPHYAutoNeg(port,forceMode?FALSE:TRUE);
					rtl8651_setAsicEthernetPHYAdvCapality(port,advCapability);
					rtl8651_restartAsicEthernetPHYNway(port);
					rtl865x_setPortForward(port,forwardEnable);
				}
			}

		}
		else
		{
			goto errout;
		}
	}
	else
	{
errout:
		printk("port status only support \"port\" as the first parameter\n");
		printk("format:	\"port port_mask 10_half/100_half/10_full/100_full/1000_full/auto\"\n");
	}
	return len;
}

//mib counter
 int rtl819x_get_port_mibStats(int port , struct port_mibStatistics *port_stats)

  {
	  uint32 addrOffset_fromP0 =0;

	  if((port>CPU)||(port_stats==NULL) )
	  {
		  return FAILED;
	  }

	  addrOffset_fromP0= port* MIB_ADDROFFSETBYPORT;

	  memset(port_stats,0,sizeof(struct port_mibStatistics));
	 /* update the mib64 counters from 32bit counters*/
	  //rtl8651_updateAdvancedMibCounter((unsigned long)(&rtl865x_updateMib64Param)); //mark_rm

	 port_stats->ifInOctets=rtl8651_returnAsicCounter(OFFSET_IFINOCTETS_P0 + addrOffset_fromP0);
	 //rtl865xC_getAsicCounter64(OFFSET_IFINOCTETS_P0 + addrOffset_fromP0, &(port_stats->ifHCInOctets)); //mark_rm
	 port_stats->ifHCInOctets = port_stats->ifInOctets;

	 port_stats->ifInUcastPkts=rtl8651_returnAsicCounter(OFFSET_IFINUCASTPKTS_P0 + addrOffset_fromP0) ;
	 //port_stats->ifHCInUcastPkts = rtl865x_updateMib64Param[port].ifHCInUcastPkts;
	 port_stats->ifHCInUcastPkts = port_stats->ifInUcastPkts;

	 port_stats->ifInDiscards=rtl8651_returnAsicCounter(OFFSET_DOT1DTPPORTINDISCARDS_P0+ addrOffset_fromP0);//??
	 port_stats->ifInErrors=(rtl8651_returnAsicCounter( OFFSET_DOT3STATSFCSERRORS_P0 + addrOffset_fromP0 ) +
						  rtl8651_returnAsicCounter( OFFSET_ETHERSTATSJABBERS_P0 + addrOffset_fromP0 ));
	 port_stats->ifInMulticastPkts= rtl8651_returnAsicCounter( OFFSET_ETHERSTATSMULTICASTPKTS_P0 + addrOffset_fromP0 ) ;
	 //port_stats->ifHCInMulticastPkts= rtl865x_updateMib64Param[port].ifHCInMulticastPkts;
	 port_stats->ifHCInMulticastPkts = port_stats->ifInMulticastPkts;

	 port_stats->ifInBroadcastPkts= rtl8651_returnAsicCounter( OFFSET_ETHERSTATSBROADCASTPKTS_P0 + addrOffset_fromP0 ) ;
	 //port_stats->ifHCInBroadcastPkts= rtl865x_updateMib64Param[port].ifHCInBroadcastPkts;
	  port_stats->ifHCInBroadcastPkts = port_stats->ifInBroadcastPkts;

	 //rtl865xC_getAsicCounter64(OFFSET_ETHERSTATSOCTETS_P0 + addrOffset_fromP0, &port_stats->etherStatsOctets );
	 port_stats->etherStatsOctets = rtl8651_returnAsicCounter(OFFSET_ETHERSTATSOCTETS_P0 + addrOffset_fromP0); //replace above , mark_rm
	 //port_stats->etherStatsOctets=rtl865xC_returnAsicCounter64(OFFSET_ETHERSTATSOCTETS_P0 + addrOffset_fromP0); //not by mark_rm
	 port_stats->etherStatsUndersizePkts=rtl8651_returnAsicCounter( OFFSET_ETHERSTATSUNDERSIZEPKTS_P0 + addrOffset_fromP0);
	 port_stats->etherStatsFraments=rtl8651_returnAsicCounter( OFFSET_ETHERSTATSFRAGMEMTS_P0 + addrOffset_fromP0);
	 port_stats->etherStatsPkts64Octets=rtl8651_returnAsicCounter( OFFSET_ETHERSTATSPKTS64OCTETS_P0 + addrOffset_fromP0);
	 port_stats->etherStatsPkts65to127Octets=rtl8651_returnAsicCounter( OFFSET_ETHERSTATSPKTS65TO127OCTETS_P0 + addrOffset_fromP0);
	 port_stats->etherStatsPkts128to255Octets=rtl8651_returnAsicCounter( OFFSET_ETHERSTATSPKTS128TO255OCTETS_P0 + addrOffset_fromP0);
	 port_stats->etherStatsPkts256to511Octets=rtl8651_returnAsicCounter( OFFSET_ETHERSTATSPKTS256TO511OCTETS_P0 + addrOffset_fromP0);
	 port_stats->etherStatsPkts512to1023Octets=rtl8651_returnAsicCounter( OFFSET_ETHERSTATSPKTS512TO1023OCTETS_P0 + addrOffset_fromP0);
	 port_stats->etherStatsPkts1024to1518Octets=rtl8651_returnAsicCounter( OFFSET_ETHERSTATSPKTS1024TO1518OCTETS_P0 + addrOffset_fromP0);
	 port_stats->etherStatsOversizePkts=rtl8651_returnAsicCounter( OFFSET_ETHERSTATSOVERSIZEPKTS_P0 + addrOffset_fromP0);
	 port_stats->etherStatsJabbers=rtl8651_returnAsicCounter( OFFSET_ETHERSTATSJABBERS_P0 + addrOffset_fromP0);
	 port_stats->etherStatusDropEvents=rtl8651_returnAsicCounter( OFFSET_ETHERSTATSDROPEVENTS_P0 + addrOffset_fromP0);
	 port_stats->dot3FCSErrors=rtl8651_returnAsicCounter( OFFSET_DOT3STATSFCSERRORS_P0 + addrOffset_fromP0);
	 port_stats->dot3StatsSymbolErrors=rtl8651_returnAsicCounter( OFFSET_DOT3STATSSYMBOLERRORS_P0 + addrOffset_fromP0);
	 port_stats->dot3ControlInUnknownOpcodes=rtl8651_returnAsicCounter( OFFSET_DOT3CONTROLINUNKNOWNOPCODES_P0 + addrOffset_fromP0);
	 port_stats->dot3InPauseFrames=rtl8651_returnAsicCounter( OFFSET_DOT3INPAUSEFRAMES_P0 + addrOffset_fromP0);

	 port_stats->ifOutOctets=rtl8651_returnAsicCounter(OFFSET_IFOUTOCTETS_P0 + addrOffset_fromP0);
	 //rtl865xC_getAsicCounter64(OFFSET_IFOUTOCTETS_P0 + addrOffset_fromP0, &port_stats->ifHCOutOctets);
	  port_stats->ifHCOutOctets = port_stats->ifOutOctets;

	 // port_stats->ifHCOutOctets=rtl865xC_returnAsicCounter64(OFFSET_IFOUTOCTETS_P0 + addrOffset_fromP0);//not by mark_rm
	 port_stats->ifOutUcastPkts=rtl8651_returnAsicCounter(OFFSET_IFOUTUCASTPKTS_P0 + addrOffset_fromP0);
	 //port_stats->ifHCOutUcastPkts = rtl865x_updateMib64Param[port].ifHCOutUcastPkts;
	 port_stats->ifHCOutUcastPkts = port_stats->ifOutUcastPkts;
	 port_stats->ifOutDiscards=rtl8651_returnAsicCounter(OFFSET_IFOUTDISCARDS+ addrOffset_fromP0);
	 port_stats->ifOutErrors=(rtl8651_returnAsicCounter( OFFSET_ETHERSTATSCOLLISIONS_P0 + addrOffset_fromP0 ) +
						  rtl8651_returnAsicCounter( OFFSET_DOT3STATSDEFERREDTRANSMISSIONS_P0 + addrOffset_fromP0 ));
	 port_stats->ifOutMulticastPkts=rtl8651_returnAsicCounter(OFFSET_IFOUTMULTICASTPKTS_P0 + addrOffset_fromP0);
	 port_stats->ifOutBroadcastPkts=rtl8651_returnAsicCounter(OFFSET_IFOUTBROADCASTPKTS_P0 + addrOffset_fromP0);
	 //port_stats->ifHCOutMulticastPkts=rtl865x_updateMib64Param[port].ifHCOutMulticastPkts;
	 port_stats->ifHCOutMulticastPkts = port_stats->ifOutMulticastPkts;
	 //port_stats->ifHCOutBroadcastPkts=rtl865x_updateMib64Param[port].ifHCOutBroadcastPkts;
	 port_stats->ifHCOutBroadcastPkts = port_stats->ifOutBroadcastPkts;

	 port_stats->ifOutDiscards=rtl8651_returnAsicCounter(OFFSET_IFOUTDISCARDS + addrOffset_fromP0);
	 port_stats->dot3StatsSingleCollisionFrames=rtl8651_returnAsicCounter(OFFSET_DOT3STATSSINGLECOLLISIONFRAMES_P0+ addrOffset_fromP0);
	 port_stats->dot3StatsMultipleCollisionFrames=rtl8651_returnAsicCounter(OFFSET_DOT3STATSMULTIPLECOLLISIONFRAMES_P0 + addrOffset_fromP0);
	 port_stats->dot3StatsDefferedTransmissions=rtl8651_returnAsicCounter(OFFSET_DOT3STATSDEFERREDTRANSMISSIONS_P0 + addrOffset_fromP0);
	 port_stats->dot3StatsLateCollisions=rtl8651_returnAsicCounter(OFFSET_DOT3STATSLATECOLLISIONS_P0 + addrOffset_fromP0);
	 port_stats->dot3StatsExcessiveCollisions=rtl8651_returnAsicCounter(OFFSET_DOT3STATSEXCESSIVECOLLISIONS_P0 + addrOffset_fromP0);
	 port_stats->dot3OutPauseFrames=rtl8651_returnAsicCounter(OFFSET_DOT3OUTPAUSEFRAMES_P0 + addrOffset_fromP0);
	 port_stats->dot1dBasePortDelayExceededDiscards=rtl8651_returnAsicCounter(OFFSET_DOT1DBASEPORTDELAYEXCEEDEDDISCARDS_P0 + addrOffset_fromP0);
	 port_stats->etherStatsCollisions=rtl8651_returnAsicCounter(OFFSET_ETHERSTATSCOLLISIONS_P0 + addrOffset_fromP0);


	 //port_stats->ifInUnknownProtos = ethPortInUnknownProtos[port]; //mark_rm
	 port_stats->ifInUnknownProtos = 0;
	 port_stats->dot1dTpLearnedEntryDiscards=rtl8651_returnAsicCounter(MIB_ADDROFFSETBYPORT);
	 port_stats->etherStatsCpuEventPkts=rtl8651_returnAsicCounter(MIB_ADDROFFSETBYPORT);

	  return SUCCESS;
  }

#ifdef CONFIG_RTL_PROC_NEW
static int show_port_mibStats(struct seq_file *s, struct port_mibStatistics *port_stats, int port)
{
	//struct lan_port_status tmp_port_status;
	/*here is in counters  definition*/
	seq_printf(s, "%s%u\n", "ifInOctets=", port_stats->ifInOctets);
	seq_printf(s, "%s%llu\n", "ifHCInOctets=", port_stats->ifHCInOctets);
	seq_printf(s, "%s%u\n", "ifInUcastPkts=", port_stats->ifInUcastPkts);
	seq_printf(s, "%s%llu\n", "ifHCInUcastPkts=", port_stats->ifHCInUcastPkts);
	seq_printf(s, "%s%u\n", "ifInMulticastPkts=", port_stats->ifInMulticastPkts);
	seq_printf(s, "%s%llu\n", "ifHCInMulticastPkts=", port_stats->ifHCInMulticastPkts);
	seq_printf(s, "%s%u\n", "ifInBroadcastPkts=", port_stats->ifInBroadcastPkts);
	seq_printf(s, "%s%llu\n", "ifHCInBroadcastPkts=", port_stats->ifHCInBroadcastPkts);
	seq_printf(s, "%s%u\n", "ifInDiscards=", port_stats->ifInDiscards);
	seq_printf(s, "%s%u\n", "ifInErrors=", port_stats->ifInErrors);
	seq_printf(s, "%s%llu\n", "etherStatsOctets=", port_stats->etherStatsOctets);
	seq_printf(s, "%s%u\n", "etherStatsUndersizePkts=", port_stats->etherStatsUndersizePkts);
	seq_printf(s, "%s%u\n", "etherStatsFraments=", port_stats->etherStatsFraments);
	seq_printf(s, "%s%u\n", "etherStatsPkts64Octets=", port_stats->etherStatsPkts64Octets);
	seq_printf(s, "%s%u\n", "etherStatsPkts65to127Octets=", port_stats->etherStatsPkts65to127Octets);
	seq_printf(s, "%s%u\n", "etherStatsPkts128to255Octets=", port_stats->etherStatsPkts128to255Octets);
	seq_printf(s, "%s%u\n", "etherStatsPkts256to511Octets=", port_stats->etherStatsPkts256to511Octets);
	seq_printf(s, "%s%u\n", "etherStatsPkts512to1023Octets=", port_stats->etherStatsPkts512to1023Octets);
	seq_printf(s, "%s%u\n", "etherStatsPkts1024to1518Octets=", port_stats->etherStatsPkts1024to1518Octets);
	seq_printf(s, "%s%u\n", "etherStatsOversizePkts=", port_stats->etherStatsOversizePkts);
	seq_printf(s, "%s%u\n", "etherStatsJabbers=", port_stats->etherStatsJabbers);
	seq_printf(s, "%s%u\n", "dot1dTpPortInDiscards=", port_stats->dot1dTpPortInDiscards);
	seq_printf(s, "%s%u\n", "etherStatusDropEvents=", port_stats->etherStatusDropEvents);
	seq_printf(s, "%s%u\n", "dot3FCSErrors=", port_stats->dot3FCSErrors);
	seq_printf(s, "%s%u\n", "dot3StatsSymbolErrors=", port_stats->dot3StatsSymbolErrors);
	seq_printf(s, "%s%u\n", "dot3ControlInUnknownOpcodes=", port_stats->dot3ControlInUnknownOpcodes);
	seq_printf(s, "%s%u\n", "dot3InPauseFrames=", port_stats->dot3InPauseFrames);
	/*here is out counters  definition*/
	seq_printf(s, "%s%u\n", "ifOutOctets=", port_stats->ifOutOctets);
	seq_printf(s, "%s%llu\n", "ifHCOutOctets=", port_stats->ifHCOutOctets);
	seq_printf(s, "%s%u\n", "ifOutUcastPkts=", port_stats->ifOutUcastPkts);
	seq_printf(s, "%s%llu\n", "ifHCOutUcastPkts=", port_stats->ifHCOutUcastPkts);
	seq_printf(s, "%s%u\n", "ifOutMulticastPkts=", port_stats->ifOutMulticastPkts);
	seq_printf(s, "%s%llu\n", "ifHCOutMulticastPkts=", port_stats->ifHCOutMulticastPkts);
	seq_printf(s, "%s%u\n", "ifOutBroadcastPkts=", port_stats->ifOutBroadcastPkts);
	seq_printf(s, "%s%llu\n", "ifHCOutBroadcastPkts=", port_stats->ifHCOutBroadcastPkts);
	seq_printf(s, "%s%u\n", "ifOutDiscards=", port_stats->ifOutDiscards);
	seq_printf(s, "%s%u\n", "ifOutErrors=", port_stats->ifOutErrors);
	seq_printf(s, "%s%u\n", "dot3StatsSingleCollisionFrames=", port_stats->dot3StatsSingleCollisionFrames);
	seq_printf(s, "%s%u\n", "dot3StatsMultipleCollisionFrames=", port_stats->dot3StatsMultipleCollisionFrames);
	seq_printf(s, "%s%u\n", "dot3StatsDefferedTransmissions=", port_stats->dot3StatsDefferedTransmissions);
	seq_printf(s, "%s%u\n", "dot3StatsLateCollisions=", port_stats->dot3StatsLateCollisions);
	seq_printf(s, "%s%u\n", "dot3StatsExcessiveCollisions=", port_stats->dot3StatsExcessiveCollisions);
	seq_printf(s, "%s%u\n", "dot3OutPauseFrames=", port_stats->dot3OutPauseFrames);
	seq_printf(s, "%s%u\n", "dot1dBasePortDelayExceededDiscards=", port_stats->dot1dBasePortDelayExceededDiscards);
	seq_printf(s, "%s%u\n", "etherStatsCollisions=", port_stats->etherStatsCollisions);
	/*here is whole system couters definition*/
	//mark_rm seq_printf(s, "%s%u\n", "dot1dTpLearnedEntryDiscards=", port_stats->dot1dTpLearnedEntryDiscards);
	//mark_rm seq_printf(s, "%s%u\n", "etherStatsCpuEventPkts=", port_stats->etherStatsCpuEventPkts);
	//seq_printf(s, "%s%u\n", "ifInUnknownProtos=", port_stats->ifInUnknownProtos); //mark_rm
	//seq_printf(s, "%s%u\n", "ifAdminStatus=", PortAdminStatus[port]);//mark_rm
	//seq_printf(s, "%s%u\n", "ifOperStatus=", PortifOperStatus[port]);//mark_rm
	//seq_printf(s, "%s%u\n", "ifLastChange=", PortLastChange[port]);//mark_rm
	//seq_printf(s, "%s%u\n", "ifSpeed=",port_stats->ifSpeed);
	//seq_printf(s, "%s%u\n", "ifHighSpeed=",port_stats->ifHighSpeed);
	//seq_printf(s, "%s%u\n", "ifCounterDiscontinuityTime=", port_stats->ifCounterDiscontinuityTime);
	/*//mark_rm
	rtl819x_get_port_status(port, &tmp_port_status);
	if(tmp_port_status.link == 1)
		port_stats->ifConnectorPresent = 1;//link
	else
		port_stats->ifConnectorPresent = 2;//unlink
	seq_printf(s, "%s%u\n", "ifConnectorPresent=", port_stats->ifConnectorPresent);
		*/

	return 0;
}
#else
static int show_port_mibStats(struct port_mibStatistics *port_stats, int port, char *page, int off)
{
	int len;
	//struct lan_port_status tmp_port_status;
	len = off;
	/*here is in counters  definition*/
	len += sprintf(page+len, "%s%u\n", "ifInOctets=", port_stats->ifInOctets);
	len += sprintf(page+len, "%s%llu\n", "ifHCInOctets=", port_stats->ifHCInOctets);
	len += sprintf(page+len, "%s%u\n", "ifInUcastPkts=", port_stats->ifInUcastPkts);
	len += sprintf(page+len, "%s%llu\n", "ifHCInUcastPkts=", port_stats->ifHCInUcastPkts);
	len += sprintf(page+len, "%s%u\n", "ifInMulticastPkts=", port_stats->ifInMulticastPkts);
	len += sprintf(page+len, "%s%llu\n", "ifHCInMulticastPkts=", port_stats->ifHCInMulticastPkts);
	len += sprintf(page+len, "%s%u\n", "ifInBroadcastPkts=", port_stats->ifInBroadcastPkts);
	len += sprintf(page+len, "%s%llu\n", "ifHCInBroadcastPkts=", port_stats->ifHCInBroadcastPkts);
	len += sprintf(page+len, "%s%u\n", "ifInDiscards=", port_stats->ifInDiscards);
	len += sprintf(page+len, "%s%u\n", "ifInErrors=", port_stats->ifInErrors);
	len += sprintf(page+len, "%s%llu\n", "etherStatsOctets=", port_stats->etherStatsOctets);
	len += sprintf(page+len, "%s%u\n", "etherStatsUndersizePkts=", port_stats->etherStatsUndersizePkts);
	len += sprintf(page+len, "%s%u\n", "etherStatsFraments=", port_stats->etherStatsFraments);
	len += sprintf(page+len, "%s%u\n", "etherStatsPkts64Octets=", port_stats->etherStatsPkts64Octets);
	len += sprintf(page+len, "%s%u\n", "etherStatsPkts65to127Octets=", port_stats->etherStatsPkts65to127Octets);
	len += sprintf(page+len, "%s%u\n", "etherStatsPkts128to255Octets=", port_stats->etherStatsPkts128to255Octets);
	len += sprintf(page+len, "%s%u\n", "etherStatsPkts256to511Octets=", port_stats->etherStatsPkts256to511Octets);
	len += sprintf(page+len, "%s%u\n", "etherStatsPkts512to1023Octets=", port_stats->etherStatsPkts512to1023Octets);
	len += sprintf(page+len, "%s%u\n", "etherStatsPkts1024to1518Octets=", port_stats->etherStatsPkts1024to1518Octets);
	len += sprintf(page+len, "%s%u\n", "etherStatsOversizePkts=", port_stats->etherStatsOversizePkts);
	len += sprintf(page+len, "%s%u\n", "etherStatsJabbers=", port_stats->etherStatsJabbers);
	len += sprintf(page+len, "%s%u\n", "dot1dTpPortInDiscards=", port_stats->dot1dTpPortInDiscards);
	len += sprintf(page+len, "%s%u\n", "etherStatusDropEvents=", port_stats->etherStatusDropEvents);
	len += sprintf(page+len, "%s%u\n", "dot3FCSErrors=", port_stats->dot3FCSErrors);
	len += sprintf(page+len, "%s%u\n", "dot3StatsSymbolErrors=", port_stats->dot3StatsSymbolErrors);
	len += sprintf(page+len, "%s%u\n", "dot3ControlInUnknownOpcodes=", port_stats->dot3ControlInUnknownOpcodes);
	len += sprintf(page+len, "%s%u\n", "dot3InPauseFrames=", port_stats->dot3InPauseFrames);
	/*here is out counters  definition*/
	len += sprintf(page+len, "%s%u\n", "ifOutOctets=", port_stats->ifOutOctets);
	len += sprintf(page+len, "%s%llu\n", "ifHCOutOctets=", port_stats->ifHCOutOctets);
	len += sprintf(page+len, "%s%u\n", "ifOutUcastPkts=", port_stats->ifOutUcastPkts);
	len += sprintf(page+len, "%s%llu\n", "ifHCOutUcastPkts=", port_stats->ifHCOutUcastPkts);
	len += sprintf(page+len, "%s%u\n", "ifOutMulticastPkts=", port_stats->ifOutMulticastPkts);
	len += sprintf(page+len, "%s%llu\n", "ifHCOutMulticastPkts=", port_stats->ifHCOutMulticastPkts);
	len += sprintf(page+len, "%s%u\n", "ifOutBroadcastPkts=", port_stats->ifOutBroadcastPkts);
	len += sprintf(page+len, "%s%llu\n", "ifHCOutBroadcastPkts=", port_stats->ifHCOutBroadcastPkts);
	len += sprintf(page+len, "%s%u\n", "ifOutDiscards=", port_stats->ifOutDiscards);
	len += sprintf(page+len, "%s%u\n", "ifOutErrors=", port_stats->ifOutErrors);
	len += sprintf(page+len, "%s%u\n", "dot3StatsSingleCollisionFrames=", port_stats->dot3StatsSingleCollisionFrames);
	len += sprintf(page+len, "%s%u\n", "dot3StatsMultipleCollisionFrames=", port_stats->dot3StatsMultipleCollisionFrames);
	len += sprintf(page+len, "%s%u\n", "dot3StatsDefferedTransmissions=", port_stats->dot3StatsDefferedTransmissions);
	len += sprintf(page+len, "%s%u\n", "dot3StatsLateCollisions=", port_stats->dot3StatsLateCollisions);
	len += sprintf(page+len, "%s%u\n", "dot3StatsExcessiveCollisions=", port_stats->dot3StatsExcessiveCollisions);
	len += sprintf(page+len, "%s%u\n", "dot3OutPauseFrames=", port_stats->dot3OutPauseFrames);
	len += sprintf(page+len, "%s%u\n", "dot1dBasePortDelayExceededDiscards=", port_stats->dot1dBasePortDelayExceededDiscards);
	len += sprintf(page+len, "%s%u\n", "etherStatsCollisions=", port_stats->etherStatsCollisions);
	/*here is whole system couters definition*/
	//mark_rm len += sprintf(page+len, "%s%u\n", "dot1dTpLearnedEntryDiscards=", port_stats->dot1dTpLearnedEntryDiscards);
	//mark_rm len += sprintf(page+len, "%s%u\n", "etherStatsCpuEventPkts=", port_stats->etherStatsCpuEventPkts);
	//len += sprintf(page+len, "%s%u\n", "ifInUnknownProtos=", port_stats->ifInUnknownProtos); //mark_rm
	//len += sprintf(page+len, "%s%u\n", "ifAdminStatus=", PortAdminStatus[port]);//mark_rm
	//len += sprintf(page+len, "%s%u\n", "ifOperStatus=", PortifOperStatus[port]);//mark_rm
	//len += sprintf(page+len, "%s%u\n", "ifLastChange=", PortLastChange[port]);//mark_rm
	//len += sprintf(page+len, "%s%u\n", "ifSpeed=",port_stats->ifSpeed);
	//len += sprintf(page+len, "%s%u\n", "ifHighSpeed=",port_stats->ifHighSpeed);
	//len += sprintf(page+len, "%s%u\n", "ifCounterDiscontinuityTime=", port_stats->ifCounterDiscontinuityTime);
	/*//mark_rm
	rtl819x_get_port_status(port, &tmp_port_status);
	if(tmp_port_status.link == 1)
		port_stats->ifConnectorPresent = 1;//link
	else
		port_stats->ifConnectorPresent = 2;//unlink
	len += sprintf(page+len, "%s%u\n", "ifConnectorPresent=", port_stats->ifConnectorPresent);
		*/

	return len;
}
#endif

#ifdef CONFIG_RTL_PROC_NEW
static int port_mibStats_read_proc(struct seq_file *s, void *v)
#else
static int port_mibStats_read_proc(char *page, char **start, off_t off,
			 int count, int *eof, void *data)
#endif
{//TODO
	int len;
	uint32 port;
	struct port_mibStatistics port_stats;
	len = 0;
#ifdef CONFIG_RTL_PROC_NEW
	port = (uint32)(s->private);
#else
	port = (uint32)data;
#endif
	if(port > CPU)
		return 0;
	rtl819x_get_port_mibStats(port , &port_stats);
#ifdef CONFIG_RTL_PROC_NEW
	show_port_mibStats(struct seq_file *s, &port_stats, port);
	return 0;
#else
	len = show_port_mibStats(&port_stats, port, page, len);

	return len;
#endif
}

static int32 port_mibStats_write_proc( struct file *filp, const char *buff,unsigned long len, void *data )
{
	if (len < 2)
		return -EFAULT;

	rtl8651_clearAsicCounter();
	return len;
}


#endif	//#if defined(CONFIG_819X_PHY_RW)

#ifdef CONFIG_RTL_PROC_NEW
static int32 read_proc_vlan(struct seq_file *s, void *v)
{
	struct net_device *dev = (struct net_device *)(s->private);
	struct dev_priv *cp;
	//int len;

	cp = NETDRV_PRIV(dev);
#if defined(CONFIG_RTK_BRIDGE_VLAN_SUPPORT) || defined(CONFIG_RTL_HW_VLAN_SUPPORT)
		seq_printf(s, "gvlan=%d, lan=%d, vlan=%d, tag=%d, vid=%d, priority=%d, cfi=%d, forwarding_rule=%d\n",
#else
		seq_printf(s, "gvlan=%d, lan=%d, vlan=%d, tag=%d, vid=%d, priority=%d, cfi=%d\n",
#endif
		cp->vlan_setting.global_vlan, cp->vlan_setting.is_lan, cp->vlan_setting.vlan, cp->vlan_setting.tag,
		cp->vlan_setting.id, cp->vlan_setting.pri, cp->vlan_setting.cfi
#if defined(CONFIG_RTK_BRIDGE_VLAN_SUPPORT) || defined(CONFIG_RTL_HW_VLAN_SUPPORT)
		,cp->vlan_setting.forwarding_rule
#endif
		);
		return 0;
}
#else/*CONFIG_RTL_PROC_NEW*/
static int read_proc_vlan(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{

	struct net_device *dev = (struct net_device *)data;
	struct dev_priv *cp;
	int len;

	cp = NETDRV_PRIV(dev);
#if defined(CONFIG_RTK_BRIDGE_VLAN_SUPPORT) || defined(CONFIG_RTL_HW_VLAN_SUPPORT)
		len = sprintf(page, "gvlan=%d, lan=%d, vlan=%d, tag=%d, vid=%d, priority=%d, cfi=%d, forwarding_rule=%d\n",
#else
		len = sprintf(page, "gvlan=%d, lan=%d, vlan=%d, tag=%d, vid=%d, priority=%d, cfi=%d\n",
#endif
		cp->vlan_setting.global_vlan, cp->vlan_setting.is_lan, cp->vlan_setting.vlan, cp->vlan_setting.tag,
		cp->vlan_setting.id, cp->vlan_setting.pri, cp->vlan_setting.cfi
#if defined(CONFIG_RTK_BRIDGE_VLAN_SUPPORT) || defined(CONFIG_RTL_HW_VLAN_SUPPORT)
		,cp->vlan_setting.forwarding_rule
#endif
		);

	if (len <= off+count)
		*eof = 1;
	*start = page + off;
	len -= off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;
	return len;
}
#endif/*CONFIG_RTL_PROC_NEW*/

static int write_proc_vlan(struct file *file, const char *buffer,
			  unsigned long count, void *data)
{
	#ifdef CONFIG_RTL_PROC_NEW
	struct net_device *dev = PDE_DATA(file_inode(file));
	#else
	struct net_device *dev = (struct net_device *)data;
	#endif
	struct dev_priv *cp;
	#define	VLAN_MAX_INPUT_LEN	128
	char *tmp;

	tmp = kmalloc(VLAN_MAX_INPUT_LEN, GFP_KERNEL);
	if (count < 2 || tmp==NULL)
		goto out;

	cp = NETDRV_PRIV(dev);
	if(rtk_vlan_support_enable == 0)
		goto out;

	if (buffer && !copy_from_user(tmp, buffer, VLAN_MAX_INPUT_LEN))
	{
#if defined(CONFIG_RTK_BRIDGE_VLAN_SUPPORT) || defined(CONFIG_RTL_HW_VLAN_SUPPORT)
			int num = sscanf(tmp, "%d %d %d %d %d %d %d %d",
#else
			int num = sscanf(tmp, "%d %d %d %d %d %d %d",
#endif
			&cp->vlan_setting.global_vlan, &cp->vlan_setting.is_lan,
			&cp->vlan_setting.vlan, &cp->vlan_setting.tag,
			&cp->vlan_setting.id, &cp->vlan_setting.pri,
			&cp->vlan_setting.cfi
#if defined(CONFIG_RTK_BRIDGE_VLAN_SUPPORT) || defined(CONFIG_RTL_HW_VLAN_SUPPORT)
			, &cp->vlan_setting.forwarding_rule
#endif
			);
#if defined(CONFIG_RTK_BRIDGE_VLAN_SUPPORT) || defined(CONFIG_RTL_HW_VLAN_SUPPORT)
		if (num !=8)
#else
		if (num !=7)
#endif
		{
			printk("invalid vlan parameter!\n");
			goto out;
		}

#if defined(CONFIG_RTK_BRIDGE_VLAN_SUPPORT)
		rtl_add_vlan_info(&cp->vlan_setting, dev);
#endif
		#if 0
		printk("===%s(%d), cp->name(%s),global_vlan(%d),is_lan(%d),vlan(%d),tag(%d),id(%d),pri(%d),cfi(%d)",__FUNCTION__,__LINE__,
			cp->dev->name,cp->vlan_setting.global_vlan,cp->vlan_setting.is_lan,cp->vlan_setting.vlan,cp->vlan_setting.tag,
			cp->vlan_setting.id,cp->vlan_setting.pri,cp->vlan_setting.cfi);
		printk("-------------%s(%d),cp->portmask(%d)\n",__FUNCTION__,__LINE__,cp->portmask);
		#endif
	}
out:
	if(tmp)
		kfree(tmp);

	return count;
}
#endif // CONFIG_RTK_VLAN_SUPPORT

#if defined(CONFIG_RTK_BRIDGE_VLAN_SUPPORT)
#ifdef CONFIG_RTL_PROC_NEW
static int rtk_vlan_management_read(struct seq_file *s, void *v)
{
	seq_printf(s, "Management vlan: vid=%d, priority=%d, cfi=%d\n",
		management_vlan.id, management_vlan.pri, management_vlan.cfi
		);
	return 0;
}
#else
static int rtk_vlan_management_read(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{

	int len;

	len = sprintf(page, "Management vlan: vid=%d, priority=%d, cfi=%d\n",
		management_vlan.id, management_vlan.pri, management_vlan.cfi
		);

	if (len <= off+count)
		*eof = 1;
	*start = page + off;
	len -= off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;
	return len;
}
#endif

static int rtk_vlan_management_write(struct file *file, const char *buffer,
			  unsigned long len, void *data)
{
	char tmpbuf[128];
	int num;

	if (buffer && !copy_from_user(tmpbuf, buffer, len))
	{
		tmpbuf[len] = '\0';

		num = sscanf(tmpbuf, "%d %d %d",
			&management_vlan.id, &management_vlan.pri,
			&management_vlan.cfi);

		if (num !=  3) {
			printk("invalid vlan parameter!\n");
		}

	}

	return len;
}

#endif

#if defined(CONFIG_RTL_HW_VLAN_SUPPORT)

uint32 rtl_hw_vlan_get_tagged_portmask(void)
{
	uint32 portmask = 0;
#ifdef CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT
	//wan port dont add hw mc
	portmask = 1 << RTK_WAN_PORT_IDX;
#else
	int i, temp;

	for(i=0; i<PORT_NUMBER; i++)
	{
		if((hw_vlan_info[i].vlan_port_enabled) && (hw_vlan_info[i].vlan_port_tag))
		{
			temp = (i==0)?0:(i-1);
			portmask |= 1<<temp;
		}
	}
#endif
	return portmask;
}

int rtl_process_hw_vlan_tx(rtl_nicTx_info *txInfo)
{
	int i, temp;
	int flag = 0;
	unsigned short vid;
	struct sk_buff *newskb;
	struct sk_buff *skb = NULL;
	//unsigned short vid;
	struct vlan_tag tag, port_tag;
	struct vlan_tag *adding_tag = NULL;
	struct net_device *dev;
	struct dev_priv *cp;

	memset(&tag, 0, sizeof(struct vlan_tag));
	memset(&port_tag, 0, sizeof(struct vlan_tag));
	skb = txInfo->out_skb;
	dev = skb->dev;
	cp = NETDRV_PRIV(dev);

	newskb = NULL;
	if (skb_cloned(skb))
	{
		if (cp->vlan_setting.is_lan && (cp->vlan_setting.forwarding_rule == 2))
		{
			//if tx to lan port and forwarding rule as nat, not need skb copy.
		}
		else
		{
			#if defined(CONFIG_RTL_ETH_PRIV_SKB)
			newskb = priv_skb_copy(skb);
			#else
			newskb = skb_copy(skb, GFP_ATOMIC);
			#endif
			if (newskb == NULL)
			{
				cp->net_stats.tx_dropped++;
				dev_kfree_skb_any(skb);
				return FAILED;
			}
			dev_kfree_skb_any(skb);
			skb = newskb;
			txInfo->out_skb = skb;
		}

	}
#ifdef CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT
	//go normal sw tx vlan path , FIXME  is real need  skb_cloned(skb) ??
	if (tx_vlan_process(dev, &cp->vlan_setting, skb, 0))
	{
				cp->net_stats.tx_dropped++;
				dev_kfree_skb_any(skb);
				return FAILED;
	}
	return SUCCESS;
#endif

	//if((skb->data[0]&1) ||((skb->data[0]==0x33) &&(skb->data[1]==0x33) && (skb->data[2]!=0xFF)))
	{
		if(skb->tag.f.tpid == htons(ETH_P_8021Q))
		{
			vid = ntohs(skb->tag.f.pci & 0xfff);
			for(i=0; i<PORT_NUMBER; i++)
			{
				temp = (i==0)?i:(i-1);
				if((((struct dev_priv*)NETDRV_PRIV(skb->dev))->portmask & (0x1<<temp))&&(vid == hw_vlan_info[i].vlan_port_vid)&&
					(hw_vlan_info[i].vlan_port_bridge == 1)&&(hw_vlan_info[i].vlan_port_tag)&&(hw_vlan_info[i].vlan_port_enabled))
					{
						flag = 1;
						break;
					}
			}

			if(flag)
			{
				//printk("dev name is %s, portmask is 0x%x, i is %d, vid is %d\n", skb->dev->name, ((struct dev_priv*)NETDRV_PRIV(skb->dev))->portmask, i, vid);
				adding_tag = &skb->tag;
			}
		}else{
			for(i=0; i<PORT_NUMBER; i++)
			{
				if(((struct dev_priv*)NETDRV_PRIV(skb->dev))->portmask & (0x1<<i))
				{
					flag = 1;
					break;
				}
			}
			temp = (i==0)?i:(i+1);
			if((flag)&&(hw_vlan_info[temp].vlan_port_bridge == 1)&& (hw_vlan_info[temp].vlan_port_tag) &&
			   (hw_vlan_info[temp].vlan_port_enabled))
			{
				port_tag.f.tpid =  htons(ETH_P_8021Q);
				port_tag.f.pci = (unsigned short) ((0x3 << 13) |(0x1 << 12) |
							((unsigned short)(hw_vlan_info[temp].vlan_port_vid&0xfff)));
				port_tag.f.pci =  htons(port_tag.f.pci);
				adding_tag = &port_tag;
			}
		}

		if(adding_tag != NULL)
		{
			memcpy(&tag, skb->data+ETH_ALEN*2, VLAN_HLEN);
			if (tag.f.tpid !=  htons(ETH_P_8021Q)) { // tag not existed, insert tag
			if (skb_headroom(skb) < VLAN_HLEN && skb_cow(skb, VLAN_HLEN) !=0 ) {
				printk("%s-%d: error! (skb_headroom(skb) == %d < 4). Enlarge it!\n",
				__FUNCTION__, __LINE__, skb_headroom(skb));
				while (1) ;
			}
			skb_push(skb, VLAN_HLEN);
			memmove(skb->data, skb->data+VLAN_HLEN, ETH_ALEN*2);
			}

			memcpy(skb->data+ETH_ALEN*2, adding_tag, VLAN_HLEN);
		}

	}

	return SUCCESS;

}

#ifdef CONFIG_RTL_PROC_NEW
static int rtl_hw_vlan_support_read(struct seq_file *s, void *v)
{
	int i;
	seq_printf(s, "rtl_hw_vlan_enable: %d\n", rtl_hw_vlan_enable);
	for(i=0; i<PORT_NUMBER; i++)
	{
		if(i == 1)
			continue;

		seq_printf(s, "lan_%d_bridge_enabled: %d, lan_%d_bridge: %d, lan_%d_tag: %d, lan_%d_vid: %d\n",
				((i==0)? i: i-1), hw_vlan_info[i].vlan_port_enabled, ((i==0)? i: i-1), hw_vlan_info[i].vlan_port_bridge, ((i==0)? i: i-1), hw_vlan_info[i].vlan_port_tag,
				((i==0)? i: i-1), hw_vlan_info[i].vlan_port_vid);
	}

	seq_printf(s, "wan_port_enabled: %d, wan_port_tag: %d, wan_port_vid: %d\n", hw_vlan_info[1].vlan_port_enabled, hw_vlan_info[1].vlan_port_tag, hw_vlan_info[1].vlan_port_vid);

	return 0;
}
#else
static int32 rtl_hw_vlan_support_read( char *page, char **start, off_t off, int count, int *eof, void *data )
{
		int len;
	 int i;

	 len = sprintf(page, "rtl_hw_vlan_enable: %d\n", rtl_hw_vlan_enable);
	 for(i=0; i<PORT_NUMBER; i++)
	 {
	 	if(i == 1)
			continue;

		len += sprintf(page + len, "lan_%d_bridge_enabled: %d, lan_%d_bridge: %d, lan_%d_tag: %d, lan_%d_vid: %d\n",
			((i==0)? i: i-1), hw_vlan_info[i].vlan_port_enabled, ((i==0)? i: i-1), hw_vlan_info[i].vlan_port_bridge, ((i==0)? i: i-1), hw_vlan_info[i].vlan_port_tag,
			((i==0)? i: i-1), hw_vlan_info[i].vlan_port_vid);
	 }

	len += sprintf(page + len, "wan_port_enabled: %d, wan_port_tag: %d, wan_port_vid: %d\n", hw_vlan_info[1].vlan_port_enabled, hw_vlan_info[1].vlan_port_tag, hw_vlan_info[1].vlan_port_vid);

	if (len <= off+count) *eof = 1;
		*start = page + off;
		len -= off;
		if (len>count)
				len = count;
		if (len<0)
				len = 0;
		return len;
}
#endif


static int32 rtl_hw_vlan_config( void )
{
	int lan_portmask = 0;
	int i=0;
	int j=0;
	int bridge_num = 0;
	struct net_device *dev;
	struct dev_priv	  *dp;
#ifdef CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT
	int mc_tag=0;
#endif

	#if defined (CONFIG_RTL_MULTI_LAN_DEV)
	rtl_config_vlanconfig(rtl865x_curOpMode);
	#else
	rtl_config_rtkVlan_vlanconfig(rtl865x_curOpMode);
	#endif

	/*get rid of port who bridge with wan from RTL_LANPORT_MASK*/
   if(rtl_hw_vlan_enable && (rtl865x_curOpMode == GATEWAY_MODE))
	{
		lan_portmask =  RTL_LANPORT_MASK;

		if((hw_vlan_info[0].vlan_port_enabled == 1)&&(hw_vlan_info[0].vlan_port_bridge == 1))
			lan_portmask &= ~RTL_LANPORT_MASK_4;  //port0 is bridge, so port0 need to get out of lan_port_mask
		if((hw_vlan_info[2].vlan_port_enabled == 1)&&(hw_vlan_info[2].vlan_port_bridge == 1))
			lan_portmask &= ~RTL_LANPORT_MASK_3;
		if((hw_vlan_info[3].vlan_port_enabled == 1)&&(hw_vlan_info[3].vlan_port_bridge == 1))
			lan_portmask &= ~RTL_LANPORT_MASK_2;
		if((hw_vlan_info[4].vlan_port_enabled == 1)&&(hw_vlan_info[4].vlan_port_bridge == 1))
			lan_portmask &= ~RTL_LANPORT_MASK_1;

		vlanconfig[0].memPort = lan_portmask;
		vlanconfig[0].vid= RTL_LANVLANID;
		vlanconfig[0].untagSet = lan_portmask;
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]))->portmask = lan_portmask; //eth0
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]))->id = RTL_LANVLANID;
#ifdef CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT
		rtl_set_nat_vlan_info(10,0); //reset default nat vid
		for(i=0;i<PORT_NUMBER;i++)
		{
			if(i==1)	/*bypass eth1*/
				continue;
			//find any NAT group then update NAT group vlan info
			if(hw_vlan_info[i].vlan_port_enabled == 1 && hw_vlan_info[i].vlan_port_bridge == FW_NAT_VLAN)
				rtl_set_nat_vlan_info(hw_vlan_info[i].vlan_port_vid,hw_vlan_info[i].vlan_port_tag);
#ifdef  CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT  //mark_hwv
			//check mc vlan is tag or not
			if(hw_vlan_info[i].vlan_port_enabled == 1 && hw_vlan_info[i].vlan_port_bridge == FW_BRIDGE_VLAN)
				if((RTK_MC_VLANID !=0)&& (hw_vlan_info[i].vlan_port_vid == RTK_MC_VLANID) && (hw_vlan_info[i].vlan_port_tag== 1))
					mc_tag =1;
#endif
		}
		rtl_update_lan_cp_vinfo((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]),1,
		 RTK_NAT_VLANID,FW_NAT_VLAN,RTK_NAT_WAN_TAGGED);
#endif
#ifdef CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT
		vlanconfig[1].vid = RTK_NAT_VLANID;
#else
		vlanconfig[1].vid = hw_vlan_info[1].vlan_port_vid;
#endif
		vlanconfig[1].memPort = RTL_WANPORT_MASK;
		vlanconfig[1].untagSet = 0; // need tag
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]))->portmask = RTL_WANPORT_MASK; //eth1
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]))->id = vlanconfig[1].vid; //eth1

#ifdef CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT
		// always sync with NAT froup  WAN/LAN
		rtl_update_wan_cp_vinfo((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]),1,
		 RTK_NAT_VLANID,RTK_NAT_WAN_TAGGED);
		// p4 (or CPU port) setting is chaged to used by Management vlan
		if(hw_vlan_info[1].vlan_port_enabled)
			rtl_update_manage_vlan(hw_vlan_info[1].vlan_port_enabled,hw_vlan_info[1].vlan_port_vid,hw_vlan_info[1].vlan_port_tag);
		else
			rtl_update_manage_vlan(hw_vlan_info[1].vlan_port_enabled,RTK_NAT_VLANID,RTK_NAT_WAN_TAGGED);
#endif
		for(i=0; i<PORT_NUMBER; i++)
		{
			if(i==1)	/*bypass eth1*/
				continue;

#ifdef CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT
			if(i > (PORT_NUMBER-2))
				continue;
#endif
			if((hw_vlan_info[i].vlan_port_enabled == 1)&&(hw_vlan_info[i].vlan_port_bridge == 1))
			{
				if(i == 0)
				{
					#if 0
					vlanconfig[2+bridge_num].memPort = 1<<i;
					#else
					vlanconfig[2+bridge_num].memPort = RTL_LANPORT_MASK_4;
					#endif
					vlanconfig[2+bridge_num].vid = hw_vlan_info[i].vlan_port_vid;
					#if 0
					vlanconfig[2+bridge_num].untagSet = 1<<i;
					#else
					vlanconfig[2+bridge_num].untagSet = RTL_LANPORT_MASK_4;
					#endif
#ifdef CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT
					vlanconfig[2+bridge_num].memPort |= RTL_WANPORT_MASK;
					if(hw_vlan_info[i].vlan_port_tag== 0 )
					{
						//if(!RTK_NAT_WAN_TAGGED) //if nat Wab also untagged , then  set this brigge port to Wan vlan. //mark_rus
						//	 vlanconfig[2+bridge_num].vid = RTK_NAT_VLANID;
						//else if (mc_tag == 0) // untag bridge vlan always belong to MC vlan group
						 if ((RTK_MC_VLANID !=0)&& (mc_tag == 0)) // untag bridge vlan always belong to MC vlan group
						{
							vlanconfig[2+bridge_num].vid = RTK_MC_VLANID;  //mark_hwv
							hw_vlan_info[i].vlan_port_vid = RTK_MC_VLANID;  //update vid if this untag bridge is not MC vlan
						}
					}
					rtl_update_lan_cp_vinfo(((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[2+bridge_num])),1,
					vlanconfig[2+bridge_num].vid,FW_BRIDGE_VLAN,hw_vlan_info[i].vlan_port_tag);
#endif
					((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[2+bridge_num]))->portmask = 1<<i;
					((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[2+bridge_num]))->id = vlanconfig[2+bridge_num].vid ;
					bridge_num ++;
				}else{
					#if 0
					vlanconfig[2+bridge_num].memPort = 1<<(i-1);
					#else
					vlanconfig[2+bridge_num].memPort = ((i==2)?RTL_LANPORT_MASK_3:((i==3)?RTL_LANPORT_MASK_2:((i==4)?RTL_LANPORT_MASK_1:0)));
					#endif
					vlanconfig[2+bridge_num].vid = hw_vlan_info[i].vlan_port_vid;
					#if 0
					vlanconfig[2+bridge_num].untagSet = 1<<(i-1);
					#else
					vlanconfig[2+bridge_num].untagSet = ((i==2)?RTL_LANPORT_MASK_3:((i==3)?RTL_LANPORT_MASK_2:((i==4)?RTL_LANPORT_MASK_1:0)));
					#endif
#ifdef CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT
					vlanconfig[2+bridge_num].memPort |= RTL_WANPORT_MASK;
					if(hw_vlan_info[i].vlan_port_tag== 0 )
					{
						//if(!RTK_NAT_WAN_TAGGED) //if nat Wab also untagged , then  set this brigge port to Wan vlan. //mark_rus
						//	 vlanconfig[2+bridge_num].vid = RTK_NAT_VLANID;
						//else if (mc_tag == 0) // untag bridge vlan always belong to MC vlan group
						if ((RTK_MC_VLANID !=0)&& (mc_tag == 0)) // untag bridge vlan always belong to MC vlan group
						{
							vlanconfig[2+bridge_num].vid = RTK_MC_VLANID;
							hw_vlan_info[i].vlan_port_vid = RTK_MC_VLANID;  //update vid if this untag bridge is not MC vlan
						}
					}
					rtl_update_lan_cp_vinfo(((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[2+bridge_num])),1,
					vlanconfig[2+bridge_num].vid,FW_BRIDGE_VLAN,hw_vlan_info[i].vlan_port_tag);
#endif
					((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[2+bridge_num]))->portmask = 1<<(i-1);
					((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[2+bridge_num]))->id = vlanconfig[2+bridge_num].vid;
					bridge_num ++;
				}
			}
		}
		//PPPOE
		vlanconfig[VLAN_CONFIG_PPPOE_INDEX].vid = vlanconfig[1].vid;  //mark_hwv
		//panic_printk("%s %d vlanconfig[5].vid=%d vlanconfig[5].ifname=%s \n", __FUNCTION__, __LINE__, vlanconfig[5].vid, vlanconfig[5].ifname);
	}


	re865x_packVlanConfig(vlanconfig, packedVlanConfig);

	rtl_reinit_hw_table();
	reinit_vlan_configure(packedVlanConfig);

#ifdef CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT
#ifdef RTK_CPU_FC_DISABLE
	{
	unsigned int val;
	val= REG32(0xbb8045d4);
	//printk("cpu port queue fc register= %x\n",val); //mark_fc
	val = val & ~(0x003f0000);  //16~21 bit to disable cpu queue fc
	REG32(0xbb8045d4) = val;
	printk("new cpu port queue fc register= %x\n",val); //mark_fc
	}
#endif
#ifdef CONFIG_RTL_8367R_SUPPORT //mark_8367
	rtk_vlan_init();
	rtk_stat_global_reset();
	if(!rtl_hw_vlan_enable)
		RTL8367R_vlan_set();
#endif
	//unknow vlan enabled
	if(rtl_hw_vlan_enable && (rtl865x_curOpMode == GATEWAY_MODE))
	{
		int vlan_num=0;
		int32 totalVlans=((sizeof(vlanconfig))/(sizeof(struct rtl865x_vlanConfig)))-2;
		vlan_num = rtl_get_vlan_num(packedVlanConfig);
		//update cp portmask to real vlan portmask
		for(i=0; i< vlan_num ; i++)
		{
			dev=(struct net_device *)_rtl86xx_dev.dev[i];
			if (!dev)
				continue;
			dp = NETDRV_PRIV(dev);
			if (!dp)
				continue;
			dp->id = packedVlanConfig[i].vid ;
			dp->portmask = (packedVlanConfig[i].memPort) & 0x3f;

			printk("cp i = %d , vid =%d, portmask=0x%x vlan_num=%d \n", i, dp->id , dp->portmask , vlan_num);
		}

		REG32(SWTCR0) |= EnUkVIDtoCPU; //important for manage vlan

		  //clear otjer cp
		for(i=vlan_num ;i<totalVlans;i++) //mark_hwv ,  pp case?
		{
			dev=(struct net_device *)_rtl86xx_dev.dev[i];
			if (!dev)
				continue;
			dp = NETDRV_PRIV(dev);
			if (!dp)
				continue;
			dp->id = 0;
			dp->portmask=0;
		}
#ifdef CONFIG_RTL_8367R_SUPPORT //mark_8367
		rtk_8367_vlan_mapping();
#endif
 }

#endif
	//unknow vlan drop
	//REG32(SWTCR0) &= ~(1 << 15);

	/*update dev port number*/
	for(i=0; vlanconfig[i].vid != 0; i++)
	{
		if (IF_ETHER!=vlanconfig[i].if_type)
		{
			continue;
		}

		dev=_rtl86xx_dev.dev[i];
		if (!dev)
		   continue;
		dp = NETDRV_PRIV(dev);
		if (!dp)
		   continue;
		dp->portnum  = 0;
		for(j=0;j<RTL8651_AGGREGATOR_NUMBER;j++)
		{
			if(dp->portmask & (1<<j))
				dp->portnum++;
		}

	}

	#if defined (CONFIG_RTL_IGMP_SNOOPING)
		re865x_reInitIgmpSetting(rtl865x_curOpMode);
	#if defined (CONFIG_RTL_MLD_SNOOPING)
		if(mldSnoopEnabled && (rtk_vlan_support_enable==0))
		{
			re865x_packVlanConfig(vlanconfig, packedVlanConfig);
			rtl865x_addAclForMldSnooping(packedVlanConfig);
		}
	#endif
		#if  defined (CONFIG_RTL_HARDWARE_MULTICAST)
		if(igmpsnoopenabled)
		{
			rtl_processAclForIgmpSnooping(igmpsnoopenabled);
		}
		#endif
	#endif

#if (defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)) && defined(CONFIG_RTL_IPV6READYLOGO)
	{
		rtl865x_addAclForIPV6ReadyLogo(packedVlanConfig);
	}
#endif

#if defined(CONFIG_RTL_HTTP_REDIRECT_LOCAL)
	rtl865x_removeAclForHttpRedirect(packedVlanConfig);
	rtl865x_addAclForHttpRedirect(packedVlanConfig);
#endif

#if defined(CONFIG_RTL_DNS_TRAP)
	rtl_remove_Acl_for_dns_trap(packedVlanConfig);
	rtl_add_Acl_for_dns_trap(packedVlanConfig);
#endif
		//always init the default route...
	if(rtl8651_getAsicOperationLayer() >2)
	{
		rtl865x_addRoute(0,0,0,RTL_DRV_WAN0_NETIF_NAME,0);
#if defined(CONFIG_RTL_HARDWARE_IPV6_SUPPORT)
		{
			inv6_addr_t ipAddr, nexthop;
			memset(&ipAddr, 0, sizeof(inv6_addr_t));
			memset(&nexthop, 0, sizeof(inv6_addr_t));

			rtl8198c_addIpv6Route(ipAddr, 0, nexthop, RTL_DRV_WAN0_NETIF_NAME);
		}
#endif
	}

	   rtl8651_setAsicMulticastEnable(TRUE);

	return 0;
}
static int32 rtl_hw_vlan_support_write( struct file *filp, const char *buff,unsigned long len, void *data )
{
		char tmpbuf[100];
		int num=0 ;

		if (buff && !copy_from_user(tmpbuf, buff, len))
		{
			num = sscanf(tmpbuf, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
			&rtl_hw_vlan_enable, &hw_vlan_info[1].vlan_port_enabled, &hw_vlan_info[1].vlan_port_tag, &hw_vlan_info[1].vlan_port_vid,
			&hw_vlan_info[0].vlan_port_enabled, &hw_vlan_info[0].vlan_port_bridge, &hw_vlan_info[0].vlan_port_tag, &hw_vlan_info[0].vlan_port_vid,
			&hw_vlan_info[2].vlan_port_enabled, &hw_vlan_info[2].vlan_port_bridge, &hw_vlan_info[2].vlan_port_tag, &hw_vlan_info[2].vlan_port_vid,
			&hw_vlan_info[3].vlan_port_enabled, &hw_vlan_info[3].vlan_port_bridge, &hw_vlan_info[3].vlan_port_tag, &hw_vlan_info[3].vlan_port_vid,
			&hw_vlan_info[4].vlan_port_enabled, &hw_vlan_info[4].vlan_port_bridge, &hw_vlan_info[4].vlan_port_tag, &hw_vlan_info[4].vlan_port_vid
			);

			if (num !=  20) {
				printk("invalid rtl_hw_vlan_support_write parameter!\n");
				return len;
			}

			rtl_hw_vlan_config();

		}
		return len;
}


#ifdef CONFIG_RTL_PROC_NEW
static int rtl_hw_vlan_tagged_bridge_multicast_read(struct seq_file *s, void *v)
{
	seq_printf(s, "%s %d\n", "rtl_hw_vlan_tagged_mc:",rtl_hw_vlan_ignore_tagged_mc);
	return 0;
}
#else
static int32 rtl_hw_vlan_tagged_bridge_multicast_read( char *page, char **start, off_t off, int count, int *eof, void *data )
{
	int len;
	len = sprintf(page, "%s %d\n", "rtl_hw_vlan_tagged_mc:",rtl_hw_vlan_ignore_tagged_mc);


	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count)
		len = count;
	if (len<0)
	  	len = 0;

	return len;
}
#endif

static int32 rtl_hw_vlan_tagged_bridge_multicast_write( struct file *filp, const char *buff,unsigned long len, void *data )
{
	char 		tmpbuf[32];

	if (buff && !copy_from_user(tmpbuf, buff, len))
	{
		tmpbuf[len] = '\0';

		rtl_hw_vlan_ignore_tagged_mc = tmpbuf[0] - '0';
	}
	return len;
}

#ifdef  RTK_MANAGE_VLAN_IF
void rtk_add_manage_vlan(void)
{
	struct dev_priv	  *cp;

	if(!wan_management_vlan.vlan) //manage vlan disable
		return ;

	if(!manage_vlan_dev)
	{
		printk("manage_vlan_dev not exist\n");
		return;
	}
	cp = (struct dev_priv *)NETDRV_PRIV(manage_vlan_dev) ;

	if(cp->vlan_setting.vlan)
	{
		   rtl865x_addVlan(cp->vlan_setting.id);
		 //add cpu port and wan port
			  rtl865x_addVlanPortMember(cp->vlan_setting.id, (1 << RTK_WAN_PORT_IDX )|RTL_CPUPORT_MASK);
		if(cp->vlan_setting.tag)
				rtl865x_setVlanPortTag(cp->vlan_setting.id, (1 << RTK_WAN_PORT_IDX )|RTL_CPUPORT_MASK,1);
		else
			  rtl865x_setVlanPortTag(cp->vlan_setting.id, (1 << RTK_WAN_PORT_IDX ),0); //set Wan port untag
		//P4 PVID ? ,mark_manv
		rtl865x_setVlanFilterDatabase(cp->vlan_setting.id,1);	//fid to 1 , wan vlan?
	}
}

struct net_device *rtk_add_vlan_if(uint32 vid ,uint32  memPort, char *name)
{
		struct net_device *dev;
		struct dev_priv	  *dp;
		int rc,j;

		dev = alloc_etherdev(sizeof(struct dev_priv));
		if (!dev) {
			printk("failed to allocate dev (rtk_add_vlan_if)\n");
			return NULL;
		}
		SET_MODULE_OWNER(dev);
		dp = NETDRV_PRIV(dev);
		memset(dp,0,sizeof(*dp));
		dp->dev = dev;
		dp->id = vid;
		dp->portmask =  memPort;
		dp->portnum  = 0;

		for(j=0;j<RTL8651_AGGREGATOR_NUMBER;j++){
			if(dp->portmask & (1<<j))
				dp->portnum++;
		}

		#if defined(CONFIG_RTD_1295_HWNAT)
		memcpy((char*)dev->dev_addr,(char*)(&(vlanconfig[RTL_WAN_IDX].mac)),ETHER_ADDR_LEN);//mark_manv
		#else //defined(CONFIG_RTD_1295_HWNAT)
		memcpy((char*)dev->dev_addr,(char*)(&(vlanconfig[1].mac)),ETHER_ADDR_LEN);//mark_manv
		#endif //defined(CONFIG_RTD_1295_HWNAT)
		strcpy(dev->name, name);
#if defined(CONFIG_COMPAT_NET_DEV_OPS)
		dev->open = re865x_open;
		dev->stop = re865x_close;
		dev->set_multicast_list = re865x_set_rx_mode;
		dev->hard_start_xmit = re865x_start_xmit;
		dev->get_stats = re865x_get_stats;
		dev->do_ioctl = re865x_ioctl;
		dev->tx_timeout = re865x_tx_timeout;
		dev->set_mac_address = rtl865x_set_hwaddr;
		dev->change_mtu = rtl865x_set_mtu;
#else
		 dev->netdev_ops = &rtl819x_netdev_ops;
#endif
		dev->watchdog_timeo = TX_TIMEOUT;

		dev->irq = BSP_SWCORE_IRQ;
		rc = register_netdev(dev);

		if(!rc){
			//_rtl86xx_dev.dev[i]=dev;
			rtl_add_ps_drv_netif_mapping(dev,name);
			return (struct net_device *)dev;
			//rtlglue_printf("eth%d added. vid=%d Member port 0x%x...\n", i,vlanconfig[i].vid ,vlanconfig[i].memPort );
		}else
			rtlglue_printf("Failed to allocate %s", name);

	return NULL;
}

static void  rtk_add_manage_vlan_dev(uint32 vid)
{
	uint32 portmask = 0x3f & (1 << RTK_WAN_PORT_IDX );
	manage_vlan_dev = (struct net_device *)rtk_add_vlan_if(vid, portmask,RTK_MANAGE_IFNAME);
}

#endif

struct net_device *rtl_get_man_dev(void)
{
#ifdef  RTK_MANAGE_VLAN_IF
	if(!wan_management_vlan.vlan) //manage vlan disable
		return NULL;
	if(manage_vlan_dev)
		return (struct net_device *)manage_vlan_dev;
#endif
	return NULL;
}

struct vlan_info *rtl_get_man_vlaninfo(void)
{

 #ifdef  RTK_MANAGE_VLAN_IF
	struct dev_priv	*cp;

	if(!wan_management_vlan.vlan) //manage vlan disable
		return NULL;

	if(manage_vlan_dev)
	{
		cp =((struct dev_priv *)NETDRV_PRIV(manage_vlan_dev));
		return (struct vlan_info *)&cp->vlan_setting;
	}
#endif
	return NULL;
}

#endif


#if (defined(CONFIG_RTL_CUSTOM_PASSTHRU) && !defined(CONFIG_RTL8196_RTL8366))

static unsigned long atoi_dec(char *s)
{
	unsigned long k = 0;

	k = 0;
	while (*s != '\0' && *s >= '0' && *s <= '9') {
		k = 10 * k + (*s - '0');
		s++;
	}
	return k;
}

#ifdef CONFIG_RTL_PROC_NEW
static int custom_Passthru_read_proc(struct seq_file *s, void *v)
{
	seq_printf(s, "%s\n", passThru_flag);
	return 0;
}
#else
static int custom_Passthru_read_proc(char *page, char **start, off_t off,
			 int count, int *eof, void *data)
{
	int len;
	len = sprintf(page, "%s\n", passThru_flag);
	if (len <= off+count)
		*eof = 1;

	*start = page + off;
	len -= off;

	if (len>count)
		len = count;

	if (len<0) len = 0;

	return len;
}
#endif

static int custom_createPseudoDevForPassThru(void)
{
		struct net_device *dev, *wanDev;
		struct dev_priv *dp;
		int rc, i;
		unsigned long		flags=0;
#if defined(CONFIG_RTL_REPORT_LINK_STATUS)  ||  defined(CONFIG_RTK_VLAN_SUPPORT)
#if defined(CONFIG_RTK_VLAN_SUPPORT)
#ifndef CONFIG_RTL_PROC_NEW
		struct proc_dir_entry *res_stats;
#endif
#endif
		struct proc_dir_entry *res_stats_root;
#endif

		wanDev = NULL;
		/*	find wan device first	*/
		for(i=0;i<ETH_INTF_NUM;i++)
		{
			dp = NETDRV_PRIV(_rtl86xx_dev.dev[i]);
			if (rtl_isWanDev(dp)==TRUE)
			{
				wanDev = _rtl86xx_dev.dev[i];
				break;
			}
		}

		/*	can't find any wan device, just return	*/
		if (wanDev==NULL)
			return -1;

		dev = alloc_etherdev(sizeof(struct dev_priv));
		if (!dev){
			rtlglue_printf("failed to allocate passthru pseudo dev.\n");
			return -1;
		}
		strcpy(dev->name, "peth%d");
		/*	default set lan side mac		*/
		memcpy((char*)dev->dev_addr,(char*)(&(vlanconfig[0].mac)),ETHER_ADDR_LEN);
		dp = NETDRV_PRIV(dev);
		memset(dp,0,sizeof(*dp));
		dp->dev = wanDev;
#if defined(CONFIG_COMPAT_NET_DEV_OPS)
		dev->open = re865x_pseudo_open;
		dev->stop = re865x_pseudo_close;
		dev->set_multicast_list = NULL;
		dev->hard_start_xmit = re865x_start_xmit;
		dev->get_stats = re865x_get_stats;
		dev->do_ioctl = re865x_ioctl;
#ifdef CP_VLAN_TAG_USED
		dev->features |= NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX;
		dev->vlan_rx_register = cp_vlan_rx_register;
		dev->vlan_rx_kill_vid = cp_vlan_rx_kill_vid;
#endif

#else
		dev->netdev_ops = &rtl819x_pseudodev_ops;
#endif

#ifdef CP_VLAN_TAG_USED
		dev->features |= NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX;
#endif
		dev->watchdog_timeo = TX_TIMEOUT;

		dev->irq = 0;
		rc = register_netdev(dev);
		if(!rc){
			SMP_LOCK_ETH(flags);
			//spin_lock_irqsave(&_rtl86xx_dev.lock, flags);
			_rtl86xx_dev.pdev = dev;
			//spin_unlock_irqrestore(&_rtl86xx_dev.lock, flags);
			SMP_UNLOCK_ETH(flags);
			rtlglue_printf("[%s] added, mapping to [%s]...\n", dev->name, dp->dev->name);
		}else
			rtlglue_printf("Failed to allocate [%s]\n", dev->name);

#if defined(CONFIG_RTK_VLAN_SUPPORT) || defined(CONFIG_RTL_REPORT_LINK_STATUS)
#ifdef CONFIG_RTL_PROC_NEW
		res_stats_root = proc_mkdir(dev->name, &proc_root);
#else
		res_stats_root = proc_mkdir(dev->name, NULL);
#endif
		if (res_stats_root == NULL)
		{
			printk("proc_mkdir failed!\n");
		}
#endif

#if defined(CONFIG_RTK_VLAN_SUPPORT)
#ifdef CONFIG_RTL_PROC_NEW
		proc_create_data("mib_vlan",0644,res_stats_root,&mib_vlan_proc_fops,(void *)dev);
#else
		if ((res_stats = create_proc_read_entry("mib_vlan", 0644, res_stats_root,
			read_proc_vlan, (void *)dev)) == NULL)
		{
			printk("create_proc_read_entry failed!\n");
		}
		res_stats->write_proc = write_proc_vlan;
#endif

#endif
		return 0;
}
#ifdef CONFIG_RTL_8367R_SUPPORT

extern int rtl8367_setProtocolBasedVLAN(uint32 proto_type,uint32 cvid, int cmdFlag);
#endif

static int custom_Passthru_write_proc(struct file *file, const char *buffer,
			  unsigned long count, void *data)
{
	int flag,i;
#ifdef CONFIG_RTL_8367R_SUPPORT
	int cmdFlag=0;
#endif

#if	!defined(CONFIG_RTL_LAYERED_DRIVER)
	struct rtl865x_vlanConfig  passThruVlanConf = {"passThru",0,IF_ETHER,PASSTHRU_VLAN_ID,0,
		rtl865x_lanPortMask|rtl865x_wanPortMask,
		rtl865x_lanPortMask|rtl865x_wanPortMask,
		1500,{ { 0x00, 0x12, 0x34, 0x56, 0x78, 0x90 } } };
#endif

	if (buffer && !copy_from_user(&passThru_flag, buffer, count))
	{
		flag=(int)atoi_dec(passThru_flag);

		if(flag ^ oldStatus)
		{
			//IPv6 PassThru
			if((flag & IP6_PASSTHRU_MASK) ^ (oldStatus & IP6_PASSTHRU_MASK))
			{
				if(flag & IP6_PASSTHRU_MASK)
				{//add
					for(i=0; i<RTL865XC_PORT_NUMBER; i++)
					{
						rtl8651_setProtocolBasedVLAN(IP6_PASSTHRU_RULEID, i, TRUE, PASSTHRU_VLAN_ID);
					}
				#ifdef CONFIG_RTL_8367R_SUPPORT
					cmdFlag=TRUE;
				#endif
				}
				else
				{//delete
					for(i=0; i<RTL865XC_PORT_NUMBER; i++)
					{
						rtl8651_setProtocolBasedVLAN(IP6_PASSTHRU_RULEID, i, FALSE, PASSTHRU_VLAN_ID);
					}
				#ifdef CONFIG_RTL_8367R_SUPPORT
					cmdFlag=FALSE;
				#endif
				}
			#ifdef CONFIG_RTL_8367R_SUPPORT
				rtl8367_setProtocolBasedVLAN(ETH_P_IPV6,PASSTHRU_VLAN_ID,cmdFlag);
			#endif
			}

			#if defined(CONFIG_RTL_CUSTOM_PASSTHRU_PPPOE)
			//PPPoE PassThru
			if((flag & PPPOE_PASSTHRU_MASK) ^ (oldStatus & PPPOE_PASSTHRU_MASK))
			{
				if(flag & PPPOE_PASSTHRU_MASK)
				{//add
					for(i=0; i<RTL865XC_PORT_NUMBER; i++)
					{
						rtl8651_setProtocolBasedVLAN(RTL8651_PBV_RULE_PPPOE_CONTROL, i, TRUE, PASSTHRU_VLAN_ID);
						rtl8651_setProtocolBasedVLAN(RTL8651_PBV_RULE_PPPOE_SESSION, i, TRUE, PASSTHRU_VLAN_ID);
					}
				#ifdef CONFIG_RTL_8367R_SUPPORT
					cmdFlag=TRUE;
				#endif
				}
				else
				{//delete
					for(i=0; i<RTL865XC_PORT_NUMBER; i++)
					{
						rtl8651_setProtocolBasedVLAN(RTL8651_PBV_RULE_PPPOE_CONTROL, i, FALSE, PASSTHRU_VLAN_ID);
						rtl8651_setProtocolBasedVLAN(RTL8651_PBV_RULE_PPPOE_SESSION, i, FALSE, PASSTHRU_VLAN_ID);
					}
				#ifdef CONFIG_RTL_8367R_SUPPORT
					cmdFlag=FALSE;
				#endif
				}
			#ifdef CONFIG_RTL_8367R_SUPPORT
				rtl8367_setProtocolBasedVLAN(ETH_P_PPP_SES,PASSTHRU_VLAN_ID,cmdFlag);
				rtl8367_setProtocolBasedVLAN(ETH_P_PPP_DISC,PASSTHRU_VLAN_ID,cmdFlag);
			#endif
			}
			#endif
		}

		//passthru vlan
		if(flag==0)
		{
			//To del passthru vlan
			//Do nothing here because change_op_mode reinit vlan already
		}
		else
			{
					//To add passthru vlan
			rtl865x_addVlan(PASSTHRU_VLAN_ID);
			#if defined(CONFIG_POCKET_ROUTER_SUPPORT)
			rtl865x_addVlanPortMember(PASSTHRU_VLAN_ID, rtl865x_lanPortMask|rtl865x_wanPortMask|0x100);
			#else
			rtl865x_addVlanPortMember(PASSTHRU_VLAN_ID, rtl865x_lanPortMask|rtl865x_wanPortMask);
			#endif
			}

		oldStatus=flag;
		return count;
	}
	return -EFAULT;
}

#ifndef CONFIG_RTL_PROC_NEW
static struct proc_dir_entry *res=NULL;
#endif

static int32 rtl8651_customPassthru_init(void)
{
	oldStatus=0;
#ifdef CONFIG_RTL_PROC_NEW
	proc_create_data("custom_Passthru",0,&proc_root,&custom_Passthru_proc_fops,NULL);
#else
	res = create_proc_entry("custom_Passthru", 0, NULL);
	if(res)
	{
		res->read_proc = custom_Passthru_read_proc;
		res->write_proc = custom_Passthru_write_proc;
	}
#endif
	rtl8651_defineProtocolBasedVLAN( IP6_PASSTHRU_RULEID, 0x0, ETH_P_IPV6 );
	#if defined(CONFIG_RTL_CUSTOM_PASSTHRU_PPPOE)
	rtl8651_defineProtocolBasedVLAN( PPPOE_PASSTHRU_RULEID1, 0x0, __constant_htons(ETH_P_PPP_SES) );
	rtl8651_defineProtocolBasedVLAN( PPPOE_PASSTHRU_RULEID2, 0x0, __constant_htons(ETH_P_PPP_DISC) );
	#endif
	custom_createPseudoDevForPassThru();
	return 0;
}

static void __exit rtl8651_customPassthru_exit(void)
{
#ifdef CONFIG_RTL_PROC_NEW
	remove_proc_entry("custom_Passthru", &proc_root);
#else
	if (res) {
		remove_proc_entry("custom_Passthru", res);
		res = NULL;
	}
#endif
}
#endif

#define	MULTICAST_STORM_CONTROL	1
#define	BROADCAST_STORM_CONTROL	2
#if defined(CONFIG_RTL_8197F)
#define UNKNOWN_UNICAST_STORM_CONTROL 1
#define UNKNOWN_MULTICAST_SOTRM_CONTROL 2
#define ARP_STORM_CONTROL 4
#define DHCP_STORM_CONTROL 8
#define IGMP_MLD_STORM_CONTROL 16
#define UNKOWN_UNICAST_COUNTER 0
#define UNKOWN_MULTICAST_COUNTER 1
#define ARP_COUNTER 2
#define DHCP_COUNTER 3
#define IGMP_MLD_COUNTER 3
#endif

#ifdef CONFIG_RTL_PROC_NEW
static int rtl865x_stormCtrlReadProc(struct seq_file *s, void *v)
{
	return 0;
}
#else
static struct proc_dir_entry *stormCtrlProc=NULL;
static int rtl865x_stormCtrlReadProc(char *page, char **start, off_t off,
			 int count, int *eof, void *data)
{
	int len=0;
	if (len <= off+count)
		*eof = 1;

	*start = page + off;
	len -= off;

	if (len>count)
		len = count;

	if (len<0) len = 0;

	return len;
}
#endif

static int rtl865x_stormCtrlWriteProc(struct file *file, const char *buffer,
			  unsigned long count, void *data)
{
	uint32 tmpBuf[32];
	uint32 stormCtrlType=MULTICAST_STORM_CONTROL;
	uint32 enableStormCtrl=FALSE;
	uint32 percentage=0;
	uint32 uintVal;

	if (buffer && !copy_from_user(tmpBuf, buffer, count))
	{
		tmpBuf[count-1]=0;
		uintVal=simple_strtol((const char *)tmpBuf, NULL, 0);
		//printk("%s(%d),tmpBuf=%s,count=%d,uintVal=%d\n",__FUNCTION__,__LINE__,
		//	tmpBuf,count,uintVal);//Added for test
		if(uintVal>100)
		{
			enableStormCtrl=FALSE;
			percentage=0;
		}
		else
		{
			enableStormCtrl=TRUE;
			percentage=uintVal;
		}
		rtl865x_setStormControl(stormCtrlType,enableStormCtrl,percentage);
		return count;
	}
	return -EFAULT;
}

int32 rtl8651_initStormCtrl(void)
{
#ifdef CONFIG_RTL_PROC_NEW
	proc_create_data("StormCtrl",0,&proc_root,&StormCtrl_proc_fops,NULL);
#else
	stormCtrlProc = create_proc_entry("StormCtrl", 0, NULL);
	if(stormCtrlProc)
	{
		stormCtrlProc->read_proc = rtl865x_stormCtrlReadProc;
		stormCtrlProc->write_proc = rtl865x_stormCtrlWriteProc;
	}
#endif

	return 0;
}

void __exit rtl8651_exitStormCtrl(void)
{
#ifdef CONFIG_RTL_PROC_NEW
	remove_proc_entry("StormCtrl", &proc_root);
#else
	if (stormCtrlProc) {
		remove_proc_entry("StormCtrl", stormCtrlProc);
		stormCtrlProc = NULL;
	}
#endif
}

#if 0//defined(CONFIG_RTL_8197F)
#ifdef CONFIG_RTL_PROC_NEW
static int rtl865x_extendStormCtrlReadProc(struct seq_file *s, void *v)
{
	uint32	regData, regDataCounter;
	uint32 i;
	char* str = NULL;

	uint32 timeUnit;

	regDataCounter = READ_MEM32(EXTSTMCR3);
	regData = READ_MEM32(EXTSTMCR4);
	timeUnit = (regDataCounter&TUSSCM0)>>TUSSCM0_OFFSET;

	switch(timeUnit){
		case 0:
			str = "25ms";
			break;
		case 1:
			str = "2.5ms";
			break;
		case 2:
			str = "0.25ms";
			break;
		case 3:
			str = "0.5ms";
			break;
		default:
			break;
	}

	seq_printf(s, "extend storm control setting:\n");
	seq_printf(s,"counter 0, %s drop, timing unit %s, rate(%d)\n", regDataCounter&ENDROPSC0?"enable":"disable", regDataCounter&ENDROPSC0? str: "0",((regData<<16)>>16)*100*4/30360);

	#if 0
	regData = READ_MEM32(EXTSTMCR4);
	seq_printf(s,"counter 1, %s drop, timing unit %s, rate(%d)\n", regDataCounter&ENDROPSC1?"enable":"disable", regDataCounter&ENDROPSC1? str: "0", (regData>>16)*100*4/30360);

	regData = READ_MEM32(EXTSTMCR5);
	seq_printf(s,"counter 2, %s drop, timing unit %s, rate(%d)\n", regDataCounter&ENDROPSC2?"enable":"disable", regDataCounter&ENDROPSC2? str: "0", ((regData<<16)>>16)*100*4/30360);

	regData = READ_MEM32(EXTSTMCR5);
	seq_printf(s,"counter 3, %s drop, timing unit %s, rate(%d)\n\n", regDataCounter&ENDROPSC3?"enable":"disable", regDataCounter&ENDROPSC3? str: "0", (regData>>16)*100*4/30360);
	#endif

	for (i=0; i < RTL8651_PORT_NUMBER; i++){

		regData = READ_MEM32(EXTSTMCR0+(i>>1)*4);

		if(i & 0x1){
			seq_printf(s,"port%d, %s unknown unicast, counter %d\n", i,regData&OUUSCS?"enable":"disable", (regData&OPUUSC)>>OPUUSC_OFFSET);
			#if 0
			seq_printf(s,"port%d, %s unknown multicast, counter %d\n", i,regData&OUMSCS?"enable":"disable", (regData&OPUMSC)>>OPUMSC_OFFSET);
			seq_printf(s,"port%d, %s ARP, counter %d\n", i,regData&OASCS?"enable":"disable", (regData&OPASC)>>OPASC_OFFSET);
			seq_printf(s,"port%d, %s DHCP, counter %d\n", i,regData&ODSCS?"enable":"disable", (regData&OPDSC)>>OPDSC_OFFSET);
			seq_printf(s,"port%d, %s IGMP/MLD, counter %d\n\n", i,regData&OIMSCS?"enable":"disable", (regData&OPIMSC)>>OPIMSC_OFFSET);
			#endif
		}
		else{
			seq_printf(s,"port%d, %s unknown unicast, counter %d\n", i, regData&EUUSCS?"enable":"disable", (regData&EPUUSC)>>EPUUSC_OFFSET);
			#if 0
			seq_printf(s,"port%d, %s unknown multicast, counter %d\n", i, regData&EUMSCS?"enable":"disable", (regData&EPUMSC)>>EPUMSC_OFFSET);
			seq_printf(s,"port%d, %s ARP, counter %d\n", i, regData&EASCS?"enable":"disable", (regData&EPASC)>>EPASC_OFFSET);
			seq_printf(s,"port%d, %s DHCP, counter %d\n", i, regData&EDSCS?"enable":"disable", (regData&EPDSC)>>EPDSC_OFFSET);
			seq_printf(s,"port%d, %s IGMP/MLD, counter %d\n\n", i, regData&EIMSCS?"enable":"disable", (regData&EPIMSC)>>EPIMSC_OFFSET);
			#endif
		}
	}

	return 0;
}
#else
static struct proc_dir_entry *extendStormCtrlProc=NULL;
static int rtl865x_extendStormCtrlReadProc(char *page, char **start, off_t off,
			 int count, int *eof, void *data)
{
	int len=0;
	if (len <= off+count)
		*eof = 1;

	*start = page + off;
	len -= off;

	if (len>count)
		len = count;

	if (len<0) len = 0;

	return len;
}
#endif

static int rtl865x_extendStormCtrlWriteProc(struct file *file, const char *buffer,
			  unsigned long count, void *data)
{
	//uint32 tmpBuf[32];
	char tmpBuf[128];
	uint32 enableStormCtrl=FALSE;
	uint32 percentage=0;
	char *strptr, *tokptr, *endstr = NULL;
	int32 stormCtrlType = -1;
	int32 uintVal = -1;

	if (buffer && !copy_from_user(tmpBuf, buffer, count))
	{
		//tmpBuf[count-1]=0;
		//uintVal=simple_strtol((const char *)tmpBuf, NULL, 0);

		tmpBuf[count-1]='\0';
		//printk("=====buf: %s\n", tmpBuf);

		strptr = tmpBuf;
		tokptr = strsep(&strptr," ");
		//printk("tokptr: %s\n", tokptr);

		if(!tokptr)
			goto ESC_ERROR;

		if(!memcmp(tokptr,"type",4)){
			tokptr = strsep(&strptr," ");
			if(!tokptr)
				goto ESC_ERROR;

			if(strcmp(tokptr, "ukn-unicast") == 0){
				stormCtrlType = UNKNOWN_UNICAST_STORM_CONTROL;
			}
			#if 0
			else if(strcmp(tokptr, "ukn-multicast") == 0){
				stormCtrlType = UNKNOWN_MULTICAST_SOTRM_CONTROL;
			}
			else if(strcmp(tokptr, "arp") == 0){
				stormCtrlType = ARP_STORM_CONTROL ;
			}
			else if(strcmp(tokptr, "dhcp") == 0){
				stormCtrlType = DHCP_STORM_CONTROL ;
			}
			else if(strcmp(tokptr, "igmp/mld") == 0){
				stormCtrlType = IGMP_MLD_STORM_CONTROL ;
			}
			#endif
			else
				goto ESC_ERROR;
		}

		tokptr = strsep(&strptr," ");
		if(!tokptr)
			goto ESC_ERROR;

		if(!memcmp(tokptr,"rate",4)){
			tokptr = strsep(&strptr," ");
			uintVal = simple_strtol(tokptr, &endstr, 0);
			if(strcmp(endstr, "") != 0){
				printk("endstr: %s\n", endstr);
				goto ESC_ERROR;
			}

			if(uintVal>100)
			{
				enableStormCtrl=FALSE;
				percentage=100;
			}
			else
			{
				enableStormCtrl=TRUE;
				percentage=uintVal;
			}
		}

		rtl865x_setExtendStormControl(stormCtrlType,enableStormCtrl,percentage);

		return count;
	}
ESC_ERROR:
	printk("Invalid params. \n");
	return -EFAULT;

}

int32 rtl8651_initExtendStormCtrl(void)
{
#ifdef CONFIG_RTL_PROC_NEW
	proc_create_data("ExtendStormCtrl",0,&proc_root,&extendStormCtrl_proc_fops,NULL);
#else
	extendStormCtrlProc = create_proc_entry("ExtendStormCtrl", 0, NULL);
	if(extendStormCtrlProc)
	{
		extendStormCtrlProc->read_proc = rtl865x_extendStormCtrlReadProc;
		extendStormCtrlProc->write_proc = rtl865x_extendStormCtrlWriteProc;
	}
#endif

	return 0;
}

void __exit rtl8651_exitextendStormCtrl(void)
{
#ifdef CONFIG_RTL_PROC_NEW
	remove_proc_entry("ExtendStormCtrl", &proc_root);
#else
	if (extendStormCtrlProc) {
		remove_proc_entry("ExtendStormCtrl", extendStormCtrlProc);
		extendStormCtrlProc = NULL;
	}
#endif
}
#endif

#if defined(CONFIG_RTL_WAN_PORT_SETTING)
static int32 rtl_reinit_hw_table_2(void)
{
	/*re-init sequence eventmgr->l4->l3->l2->common is to make sure delete asic entry,
	if not following this sequence,
	some asic entry can't be deleted due to reference count is not zero*/

	/*to-do:in each layer reinit function, memset all software entry to zero,
	and force to clear all asic entry of own module,
	then the re-init sequence can be common->l2->l3->l4 */
	/* FullAndSemiReset should not be called here
	  * it will make switch core action totally wrong
		*/

	/*event management */
	rtl865x_reInitEventMgr();

	/*l4*/
	#if defined(CONFIG_RTL_LAYERED_DRIVER_L4)
	rtl865x_nat_reinit();
	#endif

	/*l3*/
	#ifdef CONFIG_RTL_LAYERED_DRIVER_L3
	//rtl865x_reinitRouteTable();
	//rtl865x_reinitNxtHopTable();
	//rtl865x_reinitIpTable();
	//rtl865x_reinitPppTable();
	rtl865x_arp_reinit();
	#endif

	/*l2*/
	#ifdef CONFIG_RTL_LAYERED_DRIVER_L2
	rtl865x_layer2_reinit();
	#endif

	/*common*/
	//rtl865x_reinitNetifTable();
	rtl865x_reinitVlantable();
	rtl865x_reinit_acl();

	/*queue id & rx ring descriptor mapping*/
	//Use REG32 instead of REG16 because CPUQDM4 is set to 0 unexpectedly when use REG16
	REG32(CPUQDM0)=QUEUEID1_RXRING_MAPPING|(QUEUEID0_RXRING_MAPPING<<16);
	REG32(CPUQDM2)=QUEUEID3_RXRING_MAPPING|(QUEUEID2_RXRING_MAPPING<<16);
	REG32(CPUQDM4)=QUEUEID5_RXRING_MAPPING|(QUEUEID4_RXRING_MAPPING<<16);

	WRITE_MEM32(PLITIMR,0);

	//rtl8651_setAsicOperationLayer(2);

	return SUCCESS;

}

#ifdef CONFIG_RTL_PROC_NEW
static int wan_port_setting_read(struct seq_file *s, void *v)
{
	int j;

	seq_printf(s, "  Lan Port ");
	for( j = 0; j < RTL8651_PORT_NUMBER + rtl8651_totalExtPortNum; j++ )
	{
		if ( vlanconfig[0].memPort & ( 1 << j ) )
			seq_printf(s,"%d ", j);
	}
	seq_printf(s, "\n");

	seq_printf(s, "  Wan Port ");
	for( j = 0; j < RTL8651_PORT_NUMBER + rtl8651_totalExtPortNum; j++ )
	{
		if ( vlanconfig[1].memPort & ( 1 << j ) )
			seq_printf(s,"%d ", j);
	}
	seq_printf(s, "\n");

	return 0;
}
#else
static struct proc_dir_entry *wan_port_setting_proc=NULL;
static int wan_port_setting_read(char *page, char **start, off_t off,
			 int count, int *eof, void *data)
{
	int j, len=0;

	len += sprintf(page+len, "  Lan Port ");
	for( j = 0; j < RTL8651_PORT_NUMBER + rtl8651_totalExtPortNum; j++ )
	{
		if ( vlanconfig[0].memPort & ( 1 << j ) )
			len += sprintf(page+len, "%d ", j);
	}
	len += sprintf(page+len, "\n");

	len += sprintf(page+len, "  Wan Port ");
	for( j = 0; j < RTL8651_PORT_NUMBER + rtl8651_totalExtPortNum; j++ )
	{
		if ( vlanconfig[1].memPort & ( 1 << j ) )
			len += sprintf(page+len, "%d ", j);
	}
	len += sprintf(page+len, "\n");

	if (len <= off+count)
		*eof = 1;

	*start = page + off;
	len -= off;

	if (len>count)
		len = count;

	if (len<0) len = 0;

	return len;
}
#endif
static int wan_port_setting_write(struct file *file, const char *buffer,
			  unsigned long count, void *data)
{
	char 		tmpbuf[32];

	if (buffer && !copy_from_user(tmpbuf, buffer, count))
	{
		char		*strptr, *cmd_addr;
		char		*tokptr;
		struct net_device *dev;
		struct dev_priv	  *dp;

		tmpbuf[count -1] = '\0';
		strptr = tmpbuf;
		cmd_addr = strsep(&strptr," ");
		if (cmd_addr==NULL)
		{
			goto errout;
		}
		if(!memcmp(cmd_addr, "portmask", 8))
		{
			int j,portNum = -1;
			tokptr = strsep(&strptr," ");
			if (tokptr==NULL)
			{
				goto errout;
			}

			portNum = simple_strtol(tokptr, NULL, 0);
			if(portNum < 0 || portNum > 31)
				goto errout;

			//remove port form lan
			vlanconfig[0].vid=RTL_LANVLANID;
			vlanconfig[1].vid = RTL_WANVLANID;
			for(j = 0; j < 5;j++)
			{
				if(portNum & (1 << j))
				{
					//remove port form lan
					vlanconfig[0].memPort = vlanconfig[0].memPort & (~(1 << j));
					vlanconfig[0].untagSet = vlanconfig[0].untagSet & (~(1 << j));

					//add port to wan
					vlanconfig[1].memPort = vlanconfig[1].memPort | ( 1 << j);
					vlanconfig[1].untagSet = vlanconfig[1].untagSet | ( 1 << j);
				}
				else
				{
					//add port form lan
					vlanconfig[0].memPort = vlanconfig[0].memPort | (1 << j);
					vlanconfig[0].untagSet = vlanconfig[0].untagSet | (1 << j);

					//remove port to wan
					vlanconfig[1].memPort = vlanconfig[1].memPort & (~(1 << j));
					vlanconfig[1].untagSet = vlanconfig[1].untagSet & (~(1 << j));
				}
			}

			//add CPU port
			vlanconfig[0].memPort = vlanconfig[0].memPort | (1 << 8);
			vlanconfig[0].untagSet = vlanconfig[0].untagSet | (1 << 8);

			memset((void *)&vlanconfig[2], 0, sizeof(struct rtl865x_vlanConfig));

			((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]))->portmask = vlanconfig[0].memPort; //eth0
			((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]))->id = vlanconfig[0].vid;
			((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]))->id = vlanconfig[1].vid; //eth1
			((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]))->portmask = vlanconfig[1].memPort; //eth1

			dev=_rtl86xx_dev.dev[0];
			dp = (struct dev_priv *)NETDRV_PRIV(dev);
			dp->portnum  = 0;
			for(j=0;j<RTL8651_AGGREGATOR_NUMBER;j++)
			{
				if(dp->portmask & (1<<j))
					dp->portnum++;
			}

			dev=_rtl86xx_dev.dev[1];
			//dp = dev->priv;
			dp = (struct dev_priv *)NETDRV_PRIV(dev);
			dp->portnum  = 0;
			for(j=0;j<RTL8651_AGGREGATOR_NUMBER;j++)
			{
				if(dp->portmask & (1<<j))
					dp->portnum++;
			}

			/* relint hw table */
			rtl_reinit_hw_table_2();
			reinit_vlan_configure(vlanconfig);
		}
		return count;
	}

errout:
		printk("\nformat:	port 0 1 2 3 4 as bitmap and input in decimal\n");
		printk("example:	echo \"portmask 12\">/proc/wan_port_setting\n");
		printk("port 2 and 3 will be wan port.\n");
	return count;

}

int32 rtl819x_initWanPortSetting(void)
{
	#ifdef CONFIG_RTL_PROC_NEW
	proc_create_data("wan_port_setting",0,&proc_root,&wan_port_setting_proc_fops,NULL);
#else
	wan_port_setting_proc = create_proc_entry("wan_port_setting", 0, NULL);
	if(wan_port_setting_proc)
	{
		wan_port_setting_proc->read_proc = wan_port_setting_read;
		wan_port_setting_proc->write_proc = wan_port_setting_write;
	}
#endif
	return 0;
}

void __exit rtl819x_exitWanPortSetting(void)
{
#ifdef CONFIG_RTL_PROC_NEW
	remove_proc_entry("wan_port_setting", &proc_root);
#else
	if (wan_port_setting_proc) {
		remove_proc_entry("wan_port_setting", wan_port_setting_proc);
		wan_port_setting_proc = NULL;
	}
#endif
}
#endif

#if defined(CONFIG_RTL_8198) || defined(CONFIG_RTL_819XD) || defined(CONFIG_RTL_8196E) || defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)
#ifdef CONFIG_RTL_PROC_NEW
static int proc_phyTest_read(struct seq_file *s, void *v)
{
	return 0;
}
#else
static int32 proc_phyTest_read( char *page, char **start, off_t off, int count, int *eof, void *data )
{
	return 0;
}
#endif

enum _IOL_TEST
{
	IOL_100_MDI = 0,
	IOL_100_MDIX,
	IOL_100_finish,
	IOL_10_MDI,
	IOL_10_MDIX,
	IOL_10_finish,
	IOL_pktGen_allFF,
	IOL_pktGen_random,
	IOL_pktGen_disable,
	IOL_TEST_MAX,
};

static uint8 iol_test_str[IOL_TEST_MAX][16] =
{
	"100_MDI",
	"100_MDIX",
	"100_finish",
	"10_MDI",
	"10_MDIX",
	"10_finish",
	"pktGen_allFF",
	"pktGen_random",
	"pktGen_disable"
};

static int32 proc_phyTest_write( struct file *filp, const char *buff,unsigned long len1, void *data )
{
	char 	tmpbuf[64];
	uint32	phyId=0, regId=0,iNo=0,iPort=0,len=0,regData=0;
	char	*strptr, *cmd_addr;
	char	*tokptr;
	int32	i=0,port=0;
	int32 	ret=FAILED;
	#if defined(CONFIG_RTD_1295_HWNAT)
	uintptr_t regAddr = 0;
	#endif //defined(CONFIG_RTD_1295_HWNAT)

	if (buff && !copy_from_user(tmpbuf, buff, len1)) {
		tmpbuf[len1-1] = '\0';
		strptr=tmpbuf;
		cmd_addr = strsep(&strptr," ");
		if (cmd_addr==NULL)
		{
			goto errout;
		}

		if (!memcmp(cmd_addr, "read", 4))
		{

			tokptr = strsep(&strptr," ");
			if (tokptr==NULL)
			{
				goto errout;
			}
			regId=simple_strtol(tokptr, NULL, 0);

			ret=rtl8651_getAsicEthernetPHYReg(phyId, regId, &regData);
			if(ret==SUCCESS)
			{
				printk("read phyId(%d), regId(%d),regData:0x%x\n", phyId, regId, regData);
			}
			else
			{
				printk("error input!\n");
//				goto errout;
			}
		}
		else if (!memcmp(cmd_addr, "testIOL", 7))
		{
			printk("\n");
			tokptr = strsep(&strptr," ");
			if (tokptr==NULL)
				goto errout2;

			iPort=simple_strtol(tokptr, NULL, 0);
			if ((iPort<0) || (iPort>4))
				goto errout2;
			if (iPort == 0)
				phyId = 8;
			else
				phyId = iPort;

			tokptr = strsep(&strptr," ");
			if (tokptr==NULL)
				goto errout2;

			iNo = IOL_100_finish;
			if (!memcmp(tokptr, iol_test_str[IOL_100_MDIX], strlen(iol_test_str[IOL_100_MDIX])))
				iNo = IOL_100_MDIX;
			else if (!memcmp(tokptr, iol_test_str[IOL_100_MDI], strlen(iol_test_str[IOL_100_MDI])))
				iNo = IOL_100_MDI;
			else if (!memcmp(tokptr, iol_test_str[IOL_100_finish], strlen(iol_test_str[IOL_100_finish])))
				iNo = IOL_100_finish;
			else if (!memcmp(tokptr, iol_test_str[IOL_10_MDIX], strlen(iol_test_str[IOL_10_MDIX])))
				iNo = IOL_10_MDIX;
			else if (!memcmp(tokptr, iol_test_str[IOL_10_MDI], strlen(iol_test_str[IOL_10_MDI])))
				iNo = IOL_10_MDI;
			else if (!memcmp(tokptr, iol_test_str[IOL_10_finish], strlen(iol_test_str[IOL_10_finish])))
				iNo = IOL_10_finish;
			else if (!memcmp(tokptr, iol_test_str[IOL_pktGen_allFF], strlen(iol_test_str[IOL_pktGen_allFF])))
				iNo = IOL_pktGen_allFF;
			else if (!memcmp(tokptr, iol_test_str[IOL_pktGen_random], strlen(iol_test_str[IOL_pktGen_random])))
				iNo = IOL_pktGen_random;
			else if (!memcmp(tokptr, iol_test_str[IOL_pktGen_disable], strlen(iol_test_str[IOL_pktGen_disable])))
				iNo = IOL_pktGen_disable;
			else
				goto errout2;

			switch (iNo) {
			case IOL_100_MDI:
				REG32(PCRP0+(iPort*4)) |= (EnForceMode);
				rtl8651_setAsicEthernetPHYReg(phyId, 24, 0x0310); // disable ALDPS
				rtl8651_setAsicEthernetPHYReg(phyId, 0,  0x2120); // force 100F + undirection
				rtl8651_setAsicEthernetPHYReg(phyId, 28, 0x40C2); // force MDI mode
				break;
			case IOL_100_MDIX:
				REG32(PCRP0+(iPort*4)) |= (EnForceMode);
				rtl8651_setAsicEthernetPHYReg(phyId, 24, 0x0310); // disable ALDPS
				rtl8651_setAsicEthernetPHYReg(phyId, 0,  0x2120); // force 100F + undirection
				rtl8651_setAsicEthernetPHYReg(phyId, 28, 0x40C0); // force MDIX mode
				break;
			case IOL_100_finish:
				REG32(PCRP0+(iPort*4)) &= ~(EnForceMode);
				rtl8651_setAsicEthernetPHYReg(phyId, 28, 0x40C6); // enable Auto MDI/MDIX
				rtl8651_setAsicEthernetPHYReg(phyId, 0,  0x9200); // reset phy
				break;
			case IOL_10_MDI:
				REG32(PCRP0+(iPort*4)) |= (EnForceMode);
				rtl8651_setAsicEthernetPHYReg(phyId, 24, 0x0310); // disable ALDPS
				rtl8651_setAsicEthernetPHYReg(phyId, 0,  0x0120); // force 10F + undirection
				rtl8651_setAsicEthernetPHYReg(phyId, 28, 0x40C2); // force MDI mode
				rtl8651_getAsicEthernetPHYReg(phyId, 23, &regData);
				rtl8651_setAsicEthernetPHYReg(phyId, 23, regData | 0x8000); // enable force phy Link, to enable PHY receive pkt from MAC
				break;
			case IOL_10_MDIX:
				REG32(PCRP0+(iPort*4)) |= (EnForceMode);
				rtl8651_setAsicEthernetPHYReg(phyId, 24, 0x0310); // disable ALDPS
				rtl8651_setAsicEthernetPHYReg(phyId, 0,  0x0120); // force 10F + undirection
				rtl8651_setAsicEthernetPHYReg(phyId, 28, 0x40C0); // force MDIX mode
				rtl8651_getAsicEthernetPHYReg(phyId, 23, &regData);
				rtl8651_setAsicEthernetPHYReg(phyId, 23, regData | 0x8000); // enable force phy Link, to enable PHY receive pkt from MAC
				break;
			case IOL_10_finish:
				REG32(PCRP0+(iPort*4)) &= ~(EnForceMode);
				rtl8651_setAsicEthernetPHYReg(phyId, 28, 0x40C6); // enable Auto MDI/MDIX
				rtl8651_setAsicEthernetPHYReg(phyId, 0,  0x9200); // reset phy
				rtl8651_getAsicEthernetPHYReg(phyId, 23, &regData);
				rtl8651_setAsicEthernetPHYReg(phyId, 23, regData & (~0x8000)); // disable force phy Link
				break;
			case IOL_pktGen_allFF:
				REG32(PGGCR1) |= BIT(EnSMBMode_OFFSET+iPort); // enable portN pktGen Mode

				#if defined(CONFIG_RTD_1295_HWNAT)
				regAddr = PGFPCR + (0x80 * iPort);
				REG32(regAddr) = (REG32(regAddr) & ~(0x001B07FF)) | ((1<<16) | 1518);
				#else //defined(CONFIG_RTD_1295_HWNAT)
				regData = PGFPCR + (0x80 * iPort);
				REG32(regData) = (REG32(regData) & ~(0x001B07FF)) | ((1<<16) | 1518);
				#endif //defined(CONFIG_RTD_1295_HWNAT)

				REG32(PGDPR + (0x80 * iPort)) |= 0xFF;
				REG32(PGGCR1) = (REG32(PGGCR1) & ~(0x3<<(iPort*4))) | (StopTXcommandPulse<<(iPort*4));
				REG32(PGGCR1) = (REG32(PGGCR1) & ~(0x3<<(iPort*4))) | (StartTXcommandPulse<<(iPort*4));
				break;
			case IOL_pktGen_random:
				REG32(PGGCR1) |= BIT(EnSMBMode_OFFSET+iPort); // enable portN pktGen Mode

				#if defined(CONFIG_RTD_1295_HWNAT)
				regAddr = PGFPCR + (0x80 * iPort);
				REG32(regAddr) = (REG32(regAddr) & ~(0x001B07FF)) | (1518);
				#else //defined(CONFIG_RTD_1295_HWNAT)
				regData = PGFPCR + (0x80 * iPort);
				REG32(regData) = (REG32(regData) & ~(0x001B07FF)) | (1518);
				#endif //defined(CONFIG_RTD_1295_HWNAT)

				REG32(PGGCR1) = (REG32(PGGCR1) & ~(0x3<<(iPort*4))) | (StopTXcommandPulse<<(iPort*4));
				REG32(PGGCR1) = (REG32(PGGCR1) & ~(0x3<<(iPort*4))) | (StartTXcommandPulse<<(iPort*4));
				break;
			case IOL_pktGen_disable:
				REG32(PGGCR1) = (REG32(PGGCR1) & ~(0x3<<(iPort*4))) | (StopTXcommandPulse<<(iPort*4));
				mdelay(500);
				REG32(PGGCR1) &= ~BIT(EnSMBMode_OFFSET+iPort);
				break;
			default:
				break;
			}
			printk("set to the test mode: %s\n",iol_test_str[iNo]);
		}
		else if (!memcmp(cmd_addr, "test", 4))
		{
			printk("\r\n");
			tokptr = strsep(&strptr," ");
			if (tokptr==NULL)
			{
				goto errout;
			}
			iNo=simple_strtol(tokptr, NULL, 0);
			if(iNo==1)//test mode 1
			{
#if defined(CONFIG_RTL_8198C)
				printk("PHY Test Mode #1\n");
				for(len=0; len<5; len++)
					REG32(PCRP0+(len*4)) |= (EnForceMode);

				for(i=0; i<5; i++) {
					port = rtl8651AsicEthernetTable[i].phyId;
					rtl8651_setAsicEthernetPHYReg(port, 0x09, 0x2E00);
					mdelay(50);
				}
#else
				unsigned int default_val_t1[]={
				4,0x1f,0x0002,
				4,0x13,0xAA00,
				4,0x14,0xAA00,
				4,0x15,0xAA00,
				4,0x16,0xFA00,
				4,0x17,0xAF00
				};
				printk("PHY Test 1 Mode: No\n");
				for(i=0; i<5; i++)
				REG32(PCRP0+i*4) |= (EnForceMode);
				len=sizeof(default_val_t1)/sizeof(unsigned int);

				for(i=0;i<len;i=i+3)
				{
				port=default_val_t1[i];
				rtl8651_setAsicEthernetPHYReg(port, default_val_t1[i+1], default_val_t1[i+2]);
				}
				for(i=0; i<5; i++)
				{
					rtl8651_setAsicEthernetPHYReg(i, 0x1f, 0x0000);
					rtl8651_setAsicEthernetPHYReg(i, 0x09, 0x2E00);
				}
#endif
				for(i=0; i<5; i++)
				REG32(PCRP0+i*4) &= ~(EnForceMode);
				printk("Please reset the target after leaving Test Mode\n");
				ret=SUCCESS;
			}
	#if 0
			else if(iNo==2)//test mode 2
			{

				unsigned int default_val_t2[]={
				1, 0x1f, 0x0002,
				1, 0x11, 0x5E00,
				1, 0x1f, 0x0000,
				4, 0x1f, 0x0002,
				4, 0x11, 0x5E00,
				4, 0x1f, 0x0000

				};
				printk("PHY Test 2 Mode: No\n");
				for(i=0; i<5; i++)
				REG32(PCRP0+i*4) |= (EnForceMode);
				len=sizeof(default_val_t2)/sizeof(unsigned int);

				for(i=0;i<len;i=i+3)
				{
				port=default_val_t2[i];
				rtl8651_setAsicEthernetPHYReg(port, default_val_t2[i+1], default_val_t2[i+2]);
				}
				for(i=0; i<5; i++)
				{
					rtl8651_setAsicEthernetPHYReg(i, 0x1f, 0x0000);
					rtl8651_setAsicEthernetPHYReg(i, 0x09, 0x4E00);
				}
				for(i=0; i<5; i++)
				REG32(PCRP0+i*4) &= ~(EnForceMode);
				printk("Please reset the target after leaving Test Mode\n");
				ret=SUCCESS;
			}
			else if(iNo==3)//test mode 3
			{
				unsigned int default_val_t3[]={
				1, 0x1f, 0x0002,
				1, 0x11, 0x4000,
				};
				for(i=0; i<5; i++)
				REG32(PCRP0+i*4) |= (EnForceMode);

				printk("PHY Test 3 Mode: No  Port{0~4} Channel{A,B,C,D}\n");
				tokptr = strsep(&strptr," ");
				if (tokptr==NULL)
				{
					goto errout;
				}
				iPort=simple_strtol(tokptr, NULL, 0);
				tokptr = strsep(&strptr," ");
				if (tokptr==NULL)
				{
					goto errout;
				}
				len=sizeof(default_val_t3)/sizeof(unsigned int);
				for(i=0;i<len;i=i+3)
				{
					port=default_val_t3[i];
					rtl8651_setAsicEthernetPHYReg(port, default_val_t3[i+1], default_val_t3[i+2]);
				}

				switch(*tokptr)
				{

					case 'A':
						rtl8651_setAsicEthernetPHYReg(1, 0x10, 0x1100);
						rtl8651_setAsicEthernetPHYReg(1, 0x1f, 0x0000);
						printk("A channel\n");

					break;
					case 'B':
						rtl8651_setAsicEthernetPHYReg(1, 0x10, 0x1300);
						rtl8651_setAsicEthernetPHYReg(1, 0x1f, 0x0000);
						printk("B channel\n");
					break;
					case 'C':
						rtl8651_setAsicEthernetPHYReg(1, 0x10, 0x1500);
						rtl8651_setAsicEthernetPHYReg(1, 0x1f, 0x0000);
						printk("C channel\n");
					break;
					case 'D':
						rtl8651_setAsicEthernetPHYReg(1, 0x10, 0x1700);
						rtl8651_setAsicEthernetPHYReg(1, 0x1f, 0x0000);
						printk("D channel\n");
					break;
				}
				rtl8651_setAsicEthernetPHYReg(iPort, 0x1f, 0x0000);
				rtl8651_setAsicEthernetPHYReg(iPort, 0x09, 0x6e00);
				for(i=0; i<5; i++)
				REG32(PCRP0+i*4) &= ~(EnForceMode);
				printk("Please reset the target after leaving Test Mode\n");
				ret=SUCCESS;
			}
	#endif
			else if(iNo==4)//test mode 4
			{
#if defined(CONFIG_RTL_8198C)

				for(i=0; i<5; i++)
					REG32(PCRP0+i*4) |= (EnForceMode);

				tokptr = strsep(&strptr," ");
				printk("PHY Test Mode #4 for ");
				if (tokptr != NULL) {
					iPort=simple_strtol(tokptr, NULL, 0);
					printk("port %d\n",iPort);
					if (iPort == 0) iPort = 8;
					rtl8651_setAsicEthernetPHYReg(iPort, 0x09, 0x8E00);
				}
				else {
					printk("all ports\n");
					for(i=0; i<5; i++) {
						phyId = rtl8651AsicEthernetTable[i].phyId;
						rtl8651_setAsicEthernetPHYReg(phyId, 0x09, 0x8E00);
						mdelay(50);
					}
				}
#else
				unsigned int default_val_t4[]={
				0, 0x1f, 0x0002,
				0, 0x07, 0x3678,
				0, 0x1f, 0x0000,
				0, 0x09, 0x8e00
				};

				tokptr = strsep(&strptr," ");
				if (tokptr==NULL)
				{
					goto errout;
				}
				iPort=simple_strtol(tokptr, NULL, 0);
								printk("PHY Test 4 Mode: No:%d  Port:%d \n",iNo,iPort);

				for(i=0; i<5; i++)
					REG32(PCRP0+i*4) |= (EnForceMode);

				len=sizeof(default_val_t4)/sizeof(unsigned int);

				for(i=0;i<len;i=i+3)
				{
					rtl8651_setAsicEthernetPHYReg(iPort, default_val_t4[i+1], default_val_t4[i+2]);
				}
#endif
				for(i=0; i<5; i++)
				REG32(PCRP0+i*4) &= ~(EnForceMode);
				printk("Please reset the target after leaving Test Mode\n");
				ret=SUCCESS;
			}
#if defined(CONFIG_RTL_8198C)
			else if(iNo==0)
			{
				for(i=0; i<5; i++)
					REG32(PCRP0+i*4) |= (EnForceMode);

				for(i=0; i<5; i++) {
					phyId = rtl8651AsicEthernetTable[i].phyId;
					rtl8651_setAsicEthernetPHYReg(phyId, 0x09, 0xE00);
					mdelay(50);
				}
				for(i=0; i<5; i++)
					REG32(PCRP0+i*4) &= ~(EnForceMode);
				printk("set to normal mode\n");
				ret=SUCCESS;
			}
#endif

			if(ret==SUCCESS)
			{
				//printk("Write phyId(%d), regId(%d), regData:0x%x\n", phyId, regId, regData);
			}
			else
			{
				printk("error input!\n");
//				goto errout;
			}
		}
		else
		{
			goto errout;
		}

		return len1;

errout:
		printk("\nTest  format:	\"Test 1~4\"\n");
		printk("PHY Test 1 Mode: No\n");
		//printk("PHY Test 2 Mode: No\n");
		//printk("PHY Test 3 Mode: No  Port{0~4} Channel{A,B,C,D}\n");
		printk("PHY Test 4 Mode: No  Port{0~4} \n");

		return len1;

errout2:
		printk("error input!\n");
		return len1;
	}

	return -EFAULT;
}
#endif

#ifndef CONFIG_RTL_PROC_NEW
static struct proc_dir_entry *phyTest_entry=NULL;
#endif

int32 phyRegTest_init(void)
{
#ifdef CONFIG_RTL_PROC_NEW
	proc_create_data("phyRegTest",0,&proc_root,&phyRegTest_proc_fops,NULL);
#else
	phyTest_entry= create_proc_entry("phyRegTest", 0, NULL);
	if(phyTest_entry != NULL)
	{
		phyTest_entry->read_proc = proc_phyTest_read;
		phyTest_entry->write_proc = proc_phyTest_write;
	}
#endif

	return 0;
}

void __exit phyRegTest_exit(void)
{
#ifdef CONFIG_RTL_PROC_NEW
	remove_proc_entry("phyTest_entry", &proc_root);
#else
	if (phyTest_entry)
	{
		remove_proc_entry("phyTest_entry", phyTest_entry);
		phyTest_entry = NULL;
	}
#endif
}

#if defined (CONFIG_RTL_MLD_SNOOPING)

#ifdef CONFIG_RTL_PROC_NEW
static int rtl865x_mldSnoopingReadProc(struct seq_file *s, void *v)
{
	seq_printf(s, "%s\n", (mldSnoopEnabled==TRUE)?"enable":"disable");
	return 0;
}
#else
static struct proc_dir_entry *mldSnoopingProc=NULL;
static int rtl865x_mldSnoopingReadProc(char *page, char **start, off_t off,
			 int count, int *eof, void *data)
{
	int len;
	len = sprintf(page, "%s\n", (mldSnoopEnabled==TRUE)?"enable":"disable");
	if (len <= off+count)
		*eof = 1;

	*start = page + off;
	len -= off;

	if (len>count)
		len = count;

	if (len<0) len = 0;

	return len;
}
#endif

static int rtl865x_mldSnoopingWriteProc(struct file *file, const char *buffer,
			  unsigned long count, void *data)
{
	uint32 tmpBuf[32];
	uint32 uintVal;

	if (buffer && !copy_from_user(tmpBuf, buffer, count))
	{
		tmpBuf[count-1]=0;
		uintVal=simple_strtol((const char *)tmpBuf, NULL, 0);

		if(uintVal!=0)
		{
			if(mldSnoopEnabled==FALSE)
			{
				rtl865x_removeAclForMldSnooping(vlanconfig);
				rtl865x_addAclForMldSnooping(vlanconfig);

				mldSnoopEnabled=TRUE;
			}
		}
		else
		{
			if(mldSnoopEnabled==TRUE)
			{
				rtl865x_removeAclForMldSnooping(vlanconfig);
				mldSnoopEnabled=FALSE;
			}
		}
		if(mldSnoopEnabled==FALSE)
		{
#if defined (CONFIG_RTL_HARDWARE_MULTICAST)
#if defined (CONFIG_RTL_8198C) || defined (CONFIG_RTL_8197F)
				rtl8198C_reinitMulticastv6();
#endif
#endif
				rtl_flushAllIgmpRecord(FLUSH_MLD_RECORD);
		}
		return count;
	}
	return -EFAULT;
}

int32 rtl8651_initMldSnooping(void)
{
#ifdef CONFIG_RTL_PROC_NEW
	proc_create_data("br_mldsnoop",0,&proc_root,&br_mldsnoop_proc_fops,NULL);
#else
	mldSnoopingProc = create_proc_entry("br_mldsnoop", 0, NULL);
	if(mldSnoopingProc)
	{
		mldSnoopingProc->read_proc = rtl865x_mldSnoopingReadProc;
		mldSnoopingProc->write_proc = rtl865x_mldSnoopingWriteProc;
	}
#endif
	 mldSnoopEnabled=FALSE;
	return 0;
}

void __exit rtl8651_exitMldSnoopingCtrl(void)
{
#ifdef CONFIG_RTL_PROC_NEW
	remove_proc_entry("br_mldsnoop", &proc_root);
#else
	if (mldSnoopingProc) {
		remove_proc_entry("br_mldsnoop", mldSnoopingProc);
		mldSnoopingProc = NULL;
	}
#endif
}
#endif

#if defined (CONFIG_RTL_PHY_POWER_CTRL)
#ifndef CONFIG_RTL_PROC_NEW
static struct proc_dir_entry *phyPowerCtrlProc=NULL;
#endif
#define PHY_POWER_OFF 0
#define PHY_POWER_ON 1
int rtl865x_setPhyPowerOff(unsigned int port)
{
	unsigned int offset = port * 4;
	unsigned int pcr = 0;
	unsigned int regData, phyid;
	unsigned int macLinkStatus;
	unsigned int phyLinkStatus;
	if(port >=RTL8651_PHY_NUMBER)
	{
		return -1;
	}

	/*must set mac force link down first*/
	pcr=READ_MEM32( PCRP0 + offset );
	pcr |= EnForceMode;
	pcr &= ~ForceLink;
	WRITE_MEM32( PCRP0 + offset, pcr );
	TOGGLE_BIT_IN_REG_TWICE(PCRP0 + offset,EnForceMode);

	phyid = rtl8651AsicEthernetTable[port].phyId;
	rtl8651_getAsicEthernetPHYReg(phyid, 0, &regData);
	regData=regData |POWER_DOWN;
	rtl8651_setAsicEthernetPHYReg(phyid, 0, regData);

	/*confirm shutdown phy power successfully*/
	regData = READ_MEM32(PSRP0+offset);
	macLinkStatus = regData & PortStatusLinkUp;

	rtl8651_getAsicEthernetPHYReg(phyid, 0, &regData);
	phyLinkStatus=!(regData & POWER_DOWN);
	while((macLinkStatus) || (phyLinkStatus) )
	{
		//printk("port is %d,macLinkStatus is %d,phyLinkStatus is %d\n",port,macLinkStatus,phyLinkStatus);
		pcr=READ_MEM32( PCRP0 + offset );
		pcr |= EnForceMode;
		pcr &= ~ForceLink;
		WRITE_MEM32( PCRP0 + offset, pcr );
		TOGGLE_BIT_IN_REG_TWICE(PCRP0 + offset,EnForceMode);

		rtl8651_getAsicEthernetPHYReg(phyid, 0, &regData);
		regData=regData |POWER_DOWN;
		rtl8651_setAsicEthernetPHYReg(phyid, 0, regData);

		regData = READ_MEM32(PSRP0+offset);
		macLinkStatus = regData & PortStatusLinkUp;

		rtl8651_getAsicEthernetPHYReg(phyid, 0, &regData);
		phyLinkStatus=!(regData & POWER_DOWN);
	}
	return 0;

}

int rtl865x_setPhyPowerOn(unsigned int port)
{
	unsigned int  offset = port * 4;
	unsigned int pcr = 0;

	unsigned int regData, phyid;

	if(port >=RTL8651_PHY_NUMBER)
	{
		return -1;
	}

	pcr=READ_MEM32( PCRP0 + offset );
	pcr |= EnForceMode;

	WRITE_MEM32( PCRP0 + offset, pcr );
	TOGGLE_BIT_IN_REG_TWICE(PCRP0 + offset,EnForceMode);

	phyid = rtl8651AsicEthernetTable[port].phyId;
	rtl8651_getAsicEthernetPHYReg(phyid, 0, &regData);
	regData=regData &(~POWER_DOWN);
	rtl8651_setAsicEthernetPHYReg(phyid, 0, regData);

	pcr &=~EnForceMode;
	WRITE_MEM32( PCRP0 + offset, pcr);
	TOGGLE_BIT_IN_REG_TWICE(PCRP0 + offset,EnForceMode);
	return 0;

}


#ifdef CONFIG_RTL_PROC_NEW
static int rtl865x_phyPowerCtrlReadProc(struct seq_file *s, void *v)
{
	int port;
	unsigned int regData;
#ifndef CONFIG_RTL_8367R_SUPPORT
	unsigned int phyid;
#endif
	for(port=0;port<RTL8651_PHY_NUMBER;port++)
	{
#ifdef CONFIG_RTL_8367R_SUPPORT
		{
		extern int rtk_port_phyReg_get(int port, int reg, uint32 *pData);
		rtk_port_phyReg_get(port, 0, &regData);
		}
#else
		phyid = rtl8651AsicEthernetTable[port].phyId;
		rtl8651_getAsicEthernetPHYReg(phyid, 0, &regData);
#endif
		if(regData & POWER_DOWN)
		{
			seq_printf(s, "port[%d] phy is power off\n",port );
		}
		else
		{
			seq_printf(s, "port[%d] phy is power on\n",port );
		}
	}
	return 0;
}
#else
static int rtl865x_phyPowerCtrlReadProc(char *page, char **start, off_t off,
			 int count, int *eof, void *data)
{
	int len=0;
	int port;
	unsigned int regData, phyid;
	for(port=0;port<RTL8651_PHY_NUMBER;port++)
	{
#if defined(CONFIG_RTL_8367R_SUPPORT) || defined(CONFIG_RTL_8370_SUPPORT)
		{
		extern int rtk_port_phyReg_get(int port, int reg, uint32 *pData);
		rtk_port_phyReg_get(port, 0, &regData);
		}
#else
		phyid = rtl8651AsicEthernetTable[port].phyId;
		rtl8651_getAsicEthernetPHYReg(phyid, 0, &regData);
#endif
		if(regData & POWER_DOWN)
		{
			len += sprintf(page+len, "port[%d] phy is power off\n",port );
		}
		else
		{
			len += sprintf(page+len, "port[%d] phy is power on\n",port );
		}
	}

	if (len <= off+count)
		*eof = 1;

	*start = page + off;
	len -= off;

	if (len>count)
		len = count;

	if (len<0) len = 0;

	return len;
}
#endif

static int rtl865x_phyPowerCtrlWriteProc(struct file *file, const char *buffer,
			  unsigned long count, void *data)
{
	char tmpBuf[256];
	char		*strptr;
	char		*tokptr;
	unsigned int  portMask;
	unsigned int action;
	int i;
	if (buffer && !copy_from_user(tmpBuf, buffer, count))
	{
		tmpBuf[count-1]=0;
		strptr=tmpBuf;

		tokptr = strsep(&strptr," ");
		if (tokptr==NULL)
		{
			goto errOut;
		}
		portMask=simple_strtol(tokptr, NULL, 0);

		tokptr = strsep(&strptr," ");
		if (tokptr==NULL)
		{
			goto errOut;
		}
		action=simple_strtol(tokptr, NULL, 0);

		for(i=0;i<RTL8651_PHY_NUMBER;i++)
		{
			if((1<<i) &portMask)
			{
#if defined(CONFIG_RTL_8367R_SUPPORT) || defined(CONFIG_RTL_8370_SUPPORT)
				{
				extern int rtk_port_phyReg_get(int port, int reg, uint32 *pData);
				extern int rtk_port_phyReg_set(int port, int reg, uint32 regData);
				uint32 regData;

				rtk_port_phyReg_get(i, 0, &regData);

				if(action==PHY_POWER_OFF)
				{
					regData |= POWER_DOWN;
				}
				else if (action==PHY_POWER_ON)
				{
					regData &= ~POWER_DOWN;
				}
				rtk_port_phyReg_set(i, 0, regData);
				}
#else
				if(action==PHY_POWER_OFF)
				{
					rtl865x_setPhyPowerOff(i);
				}
				else if (action==PHY_POWER_ON)
				{
					rtl865x_setPhyPowerOn(i);
				}
#endif
			}

		}

		return count;
	}

errOut:
	return -EFAULT;
}


static int32 rtl865x_initPhyPowerCtrl(void)
{
#ifdef CONFIG_RTL_PROC_NEW
	proc_create_data("phyPower",0,&proc_root,&phyPower_proc_fops,NULL);
#else
	phyPowerCtrlProc = create_proc_entry("phyPower", 0, NULL);
	if(phyPowerCtrlProc)
	{
		phyPowerCtrlProc->read_proc = rtl865x_phyPowerCtrlReadProc;
		phyPowerCtrlProc->write_proc = rtl865x_phyPowerCtrlWriteProc;
	}
#endif

	return 0;
}

void __exit rtl865x_exitPhyPowerCtrl(void)
{
#ifdef CONFIG_RTL_PROC_NEW
	remove_proc_entry("phyPower", &proc_root);
#else
	if (phyPowerCtrlProc) {
		remove_proc_entry("phyPower", phyPowerCtrlProc);
		phyPowerCtrlProc = NULL;
	}
#endif
}

#endif


#if defined(CONFIG_RTL_LINKSTATE)

static struct proc_dir_entry *portStateProc=NULL;
static unsigned int linkUpTime[RTL8651_PHY_NUMBER] = {0,0,0,0,0};

static void init_linkup_time(void)
{
	init_timer(&s_timer);
	s_timer.function = &linkup_time_handle;
	s_timer.expires = jiffies + HZ;
	add_timer(&s_timer);
}

static void linkup_time_handle(unsigned long arg)
{
	int port;
	uint32	regData;
	uint32	data0;
	mod_timer(&s_timer,jiffies +HZ);
	for(port=PHY0;port<PHY5;port++)
	{
		regData = READ_MEM32(PSRP0+((port)<<2));
		data0 = regData & PortStatusLinkUp;
		if (data0)
		{
			linkUpTime[port]++;
		}
		else
		{
			linkUpTime[port]=0;
		}
	}
}

#ifdef CONFIG_RTL_PROC_NEW
static int port_state_read_proc(struct seq_file *s, void *v)
{
	int		len;
	uint32	regData;
	uint32	data0;
	int		port;

	seq_printf(s, "Port Link State:\n");

	for(port=PHY0;port<PHY5;port++)
	{
		regData = READ_MEM32(PSRP0+((port)<<2));

		//if (port==CPU)
			//seq_printf(s, "CPUPort ");
		//else
		seq_printf(s, "Port[%d]:", port);
		data0 = regData & PortStatusLinkUp;

		if (data0)
		{
			seq_printf(s, "LinkUp|");
		}
		else
		{
			seq_printf(s, "LinkDown\n");
			continue;
		}
		//data0 = regData & PortStatusNWayEnable;
		//seq_printf(s, "NWay Mode %s\n", data0?"Enabled":"Disabled");
		data0 = regData & PortStatusRXPAUSE;
		seq_printf(s, "RXPause:%s|", data0?"Enable":"Disable");
		data0 = regData & PortStatusTXPAUSE;
		seq_printf(s, "TXPause:%s|", data0?"Enable":"Disable");
		data0 = regData & PortStatusDuplex;
		seq_printf(s, "Duplex:%s|", data0?"Enable":"Disable");
		data0 = (regData&PortStatusLinkSpeed_MASK)>>PortStatusLinkSpeed_OFFSET;
		seq_printf(s, "Speed:%s|", data0==PortStatusLinkSpeed100M?"100M":
			(data0==PortStatusLinkSpeed1000M?"1G":
				(data0==PortStatusLinkSpeed10M?"10M":"Unkown")));
		seq_printf(s, "UpTime:%ds\n", linkUpTime[port]);
	}

	return 0;
}
#else
static int32 port_state_read_proc( char *page, char **start, off_t off, int count, int *eof, void *data )
{
	int		len;
	uint32	regData;
	uint32	data0;
	int		port;

	len = sprintf(page, "Port Link State:\n");

	for(port=PHY0;port<PHY5;port++)
	{
		regData = READ_MEM32(PSRP0+((port)<<2));

		//if (port==CPU)
			//len += sprintf(page+len, "CPUPort ");
		//else
		len += sprintf(page+len, "Port[%d]:", port);
		data0 = regData & PortStatusLinkUp;

		if (data0)
		{
			len += sprintf(page+len, "LinkUp|");
		}
		else
		{
			len += sprintf(page+len, "LinkDown\n");
			continue;
		}
		//data0 = regData & PortStatusNWayEnable;
		//len += sprintf(page+len, "NWay Mode %s\n", data0?"Enabled":"Disabled");
		data0 = regData & PortStatusRXPAUSE;
		len += sprintf(page+len, "RXPause:%s|", data0?"Enable":"Disable");
		data0 = regData & PortStatusTXPAUSE;
		len += sprintf(page+len, "TXPause:%s|", data0?"Enable":"Disable");
		data0 = regData & PortStatusDuplex;
		len += sprintf(page+len, "Duplex:%s|", data0?"Enable":"Disable");
		data0 = (regData&PortStatusLinkSpeed_MASK)>>PortStatusLinkSpeed_OFFSET;
		len += sprintf(page+len, "Speed:%s|", data0==PortStatusLinkSpeed100M?"100M":
			(data0==PortStatusLinkSpeed1000M?"1G":
				(data0==PortStatusLinkSpeed10M?"10M":"Unkown")));
		len += sprintf(page+len, "UpTime:%ds\n", linkUpTime[port]);
	}

	return len;
}
#endif

static int32 port_state_write_proc( struct file *filp, const char *buff,unsigned long len, void *data )
{
	return len;
}

static int32 initPortStateCtrl(void)
{
#ifdef CONFIG_RTL_PROC_NEW
	proc_create_data("portState",0,&proc_root,&portState_proc_fops,NULL);
#else
	portStateProc = create_proc_entry("portState", 0, NULL);
	if(portStateProc)
	{
		portStateProc->read_proc = port_state_read_proc;
		portStateProc->write_proc = port_state_write_proc;
	}
#endif
	memset(linkUpTime,0,sizeof(linkUpTime));
	init_linkup_time();

	return 0;
}

static void  exitPortStateCtrl(void)
{
#ifdef CONFIG_RTL_PROC_NEW
	remove_proc_entry("portState", &proc_root);
#else
	if (portStateProc) {
		remove_proc_entry("portState", portStateProc);
		portStateProc = NULL;
	}
#endif
	del_timer(&s_timer);
}
#endif

#if defined(CONFIG_RTL_MULTIPLE_WAN)
static int rtl_port_used_by_device(uint32 portMask)
{
	int i;
	struct dev_priv *cp;
	int retval = FAILED;

	for(i = 0; i < ETH_INTF_NUM; i++)
	{
		cp = ((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[i]));
		if(_rtl86xx_dev.dev[i] && cp && cp->opened && (cp->portmask & portMask))
		{
			retval = SUCCESS;
			break;
		}

	}

	cp = (struct dev_priv *)NETDRV_PRIV(rtl_multiWan_net_dev);
	if(rtl_multiWan_net_dev && cp && cp->opened && (cp->portmask & portMask))
		retval = SUCCESS;

	return retval;
}


static int rtl_regist_multipleWan_dev(void)
{
	int j;
	struct net_device *dev;
	struct dev_priv   *dp;
	int rc;

	dev = alloc_etherdev(sizeof(struct dev_priv));
	if (!dev) {
		printk("====%s(%d) failed to allocate\n",__FUNCTION__,__LINE__);
		return FAILED;
	}
	SET_MODULE_OWNER(dev);
	dp = NETDRV_PRIV(dev);
	memset(dp,0,sizeof(*dp));
	dp->dev = dev;
	dp->id = rtl_multiWan_config.vid;
	dp->portmask =	rtl_multiWan_config.memPort;
	dp->portnum  = 0;
	for(j=0;j<RTL8651_AGGREGATOR_NUMBER;j++){
		if(dp->portmask & (1<<j))
			dp->portnum++;
	}

	memcpy((char*)dev->dev_addr,(char*)(&(rtl_multiWan_config.mac)),ETHER_ADDR_LEN);
#if defined(CONFIG_COMPAT_NET_DEV_OPS)
	dev->open = re865x_open;
	dev->stop = re865x_close;
	dev->set_multicast_list = re865x_set_rx_mode;
	dev->hard_start_xmit = re865x_start_xmit;
	dev->get_stats = re865x_get_stats;
	dev->do_ioctl = re865x_ioctl;
	dev->tx_timeout = re865x_tx_timeout;
	dev->set_mac_address = rtl865x_set_hwaddr;
	dev->change_mtu = rtl865x_set_mtu;
#if defined(CONFIG_RTL8186_LINK_CHANGE)
	dev->change_link = rtl865x_set_link;
#endif
#ifdef CP_VLAN_TAG_USED
	dev->features |= NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX;
	dev->vlan_rx_register = cp_vlan_rx_register;
	dev->vlan_rx_kill_vid = cp_vlan_rx_kill_vid;
#endif

#else
	dev->netdev_ops = &rtl819x_netdev_ops;
#endif
	dev->watchdog_timeo = TX_TIMEOUT;

#ifdef CP_VLAN_TAG_USED
	dev->features |= NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX;
#endif

	dev->irq = 0;
	memcpy((char*)dev->name,rtl_multiWan_config.ifname,MAX_IFNAMESIZE);
	rc = register_netdev(dev);
	if(!rc){
		rtl_multiWan_net_dev = dev;
		rtl_add_ps_drv_netif_mapping(dev,rtl_multiWan_config.ifname);
		printk("==%s(%d) %s added. vid=%d Member port 0x%x...\n",__FUNCTION__,__LINE__,dev->name,rtl_multiWan_config.vid ,rtl_multiWan_config.memPort );
	}else
		printk("===%s(%d) Failed to allocate,rc(%d)\n",__FUNCTION__,__LINE__,rc);

	return SUCCESS;
	//
}
#if 0
static int rtl_unregist_multipleWan_dev(void)
{
	if(rtl_multiWan_net_dev)
		unregister_netdev(rtl_multiWan_net_dev);

	rtl_multiWan_net_dev = NULL;
	return SUCCESS;
}
#endif
static int rtl_config_multipleWan_netif(int32 cmd)
{
	int retval = FAILED;
	switch(cmd)
	{
		case RTL_MULTIWAN_ADD:
			{
				rtl865x_netif_t netif;

				/*add vlan*/
				retval = rtl865x_addVlan(rtl_multiWan_config.vid);
				if(retval == SUCCESS)
				{
					rtl865x_addVlanPortMember(rtl_multiWan_config.vid,rtl_multiWan_config.memPort);
					rtl865x_setVlanFilterDatabase(rtl_multiWan_config.vid,rtl_multiWan_config.fid);

				}
				else if (retval == RTL_EVLANALREADYEXISTS)
				{
					//only add memberport to vlan
					rtl865x_addVlanPortMember(rtl_multiWan_config.vid,rtl_multiWan_config.memPort);
				}

				/*add network interface*/
				memset(&netif, 0, sizeof(rtl865x_netif_t));
				memcpy(netif.name,rtl_multiWan_config.ifname,MAX_IFNAMESIZE);
				memcpy(netif.macAddr.octet, rtl_multiWan_config.mac.octet,ETHER_ADDR_LEN);
				netif.mtu = rtl_multiWan_config.mtu;
				netif.if_type = rtl_multiWan_config.if_type;
				netif.vid = rtl_multiWan_config.vid;
				netif.is_wan = rtl_multiWan_config.isWan;
				netif.is_slave = rtl_multiWan_config.is_slave;
				#if defined (CONFIG_RTL_HARDWARE_MULTICAST)
				netif.enableRoute=1;
				#endif

				#if defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)
				netif.mtuV6 = rtl_multiWan_config.mtu;
				#if defined(CONFIG_RTL_HARDWARE_IPV6_SUPPORT) ||(defined (CONFIG_RTL_MLD_SNOOPING)&&defined(CONFIG_RTL_HARDWARE_MULTICAST))
				netif.enableRouteV6 =1;   	/*jwj:enable ipv6, then enable v6 router*/
				#endif
				#endif

				retval = rtl865x_addNetif(&netif);
				rtl865x_reConfigDefaultAcl(netif.name);

				if(retval != SUCCESS && retval != RTL_EVLANALREADYEXISTS)
				{
					printk("=======%s(%d),retval(%d)\n",__FUNCTION__,__LINE__,retval);
				}
			}
			rtl_init_advRt();
			break;
		case RTL_MULTIWAN_DEL:
			rtl_exit_advRt();
			rtl865x_delNetif(rtl_multiWan_config.ifname);
			rtl865x_delVlanPortMember(rtl_multiWan_config.vid,rtl_multiWan_config.memPort);
		break;
		default:
			retval = FAILED;
	}

	return retval;
}

static int rtl865x_isArpFrame(struct sk_buff *skb, int* ah_offset)
{
	int ret = FALSE;

	if (*((uint16*)(skb->data+(ETH_ALEN<<1))) == __constant_htons(ETH_P_ARP)) {
		*ah_offset = ETH_HLEN;
		ret = TRUE;
	} else if ((*((uint16*)(skb->data+(ETH_ALEN<<1))) == __constant_htons(ETH_P_8021Q))&&
		 		(*((uint16*)(skb->data+(ETH_ALEN<<1)+VLAN_HLEN)) == __constant_htons(ETH_P_ARP))) {
		*ah_offset = ETH_HLEN + VLAN_HLEN;
		ret = TRUE;
	}

	return ret;
}

static inline int rtl865x_decideUcDevice(unsigned char *data, int pid, rtl_nicRx_info *info)
{
	int i;
	struct dev_priv	*cp;

	//mac based decision
	for(i = 0; i < ETH_INTF_NUM; i++)
	{
				cp = (struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[i]);
				if (_rtl86xx_dev.dev[i] && cp && cp->opened && (memcmp(data, cp->dev->dev_addr, 6)==0)) {
						info->priv = cp;
						return SUCCESS;
				}
	   }
	//rtl_multiWan_config
	if (rtl_multiWan_net_dev)
	{
		cp = (struct dev_priv *)NETDRV_PRIV(rtl_multiWan_net_dev);
		if (rtl_multiWan_net_dev && cp && cp->opened && (memcmp(data, cp->dev->dev_addr, 6)==0)) {
				info->priv = cp;
					  return SUCCESS;
			  }
	}

	return FAILED;
}

static inline int rtl865x_decideMcDevice(int pid, rtl_nicRx_info *info)
{
	int i;
	struct dev_priv	*cp;

	for (i = 0; i < ETH_INTF_NUM; i++)
	{
				cp = (struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[i]);
				if (_rtl86xx_dev.dev[i] && cp && cp->opened && (cp->portmask & (1<<pid))) {
					   if (strncmp(cp->dev->name, igmp_proxy_wan_dev, MAX_IFNAMESIZE)==0) {
			   	   info->priv = cp;
							   return SUCCESS;
		   	   }
				}
		  }

	if (rtl_multiWan_net_dev)
	{
		cp = (struct dev_priv *)NETDRV_PRIV(rtl_multiWan_net_dev);
		if (rtl_multiWan_net_dev && cp && cp->opened && (cp->portmask & (1<<pid))) {
				if (strncmp(cp->dev->name, igmp_proxy_wan_dev, MAX_IFNAMESIZE)==0) {
				info->priv = cp;
							return SUCCESS;
				}
			  }
	}

	return FAILED;
}

static inline int rtl865x_decideBcDevice(struct sk_buff *skb, int pid, rtl_nicRx_info *info)
{
	int i;
	struct dev_priv *cp;
	struct arphdr *arp;
	unsigned char *arp_ptr;
	__be32 tip, dev_ip, dev_mask;
	int ah_offset = 0;

	if (rtl865x_isArpFrame(skb, &ah_offset) == TRUE) {
		arp = (struct arphdr *)(skb->data + ah_offset);
		if ((arp->ar_hrd==htons(ARPHRD_ETHER)) && (arp->ar_pro==htons(ETH_P_IP))) {
			arp_ptr = (unsigned char *)(arp + 1);
			arp_ptr += (ETH_ALEN<<1) + 4;
			memcpy(&tip, arp_ptr, 4);

			for (i = 0; i < ETH_INTF_NUM; i++)
			{
				cp = ((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[i]));
				if(_rtl86xx_dev.dev[i] && cp && cp->opened && (cp->portmask & (1<<pid)))
				{
					if ((get_dev_ip_mask(cp->dev->name, &dev_ip, &dev_mask)==0) && (ntohl(tip) == dev_ip)) {
						info->priv = cp;
						return SUCCESS;
					}
				}
			}

			if (rtl_multiWan_net_dev)
			{
				cp = (struct dev_priv *)NETDRV_PRIV(rtl_multiWan_net_dev);
				if (rtl_multiWan_net_dev && cp && cp->opened && (cp->portmask & (1<<pid)))
				{
				   	if ((get_dev_ip_mask(cp->dev->name, &dev_ip, &dev_mask)==0) && (ntohl(tip) == dev_ip)) {
				   		info->priv = cp;
						return SUCCESS;
				   	}
				}
			}
		}
	}

	return FAILED;
}

#if 0
static int rtl865x_delMultiCastNetif(void)
{
	int ret=FAILED;

#if defined(CONFIG_RTK_VLAN_SUPPORT)
	{
		rtl865x_AclRule_t	rule;
		bzero((void*)&rule,sizeof(rtl865x_AclRule_t));
		rule.ruleType_ = RTL865X_ACL_MAC;
		rule.pktOpApp_ = RTL865X_ACL_ALL_LAYER;
		rule.actionType_ = RTL865X_ACL_PERMIT;
		ret=rtl865x_del_acl(&rule, multiCastNetIf, RTL865X_ACL_SYSTEM_USED);
	}
#endif

	ret=rtl865x_delNetif(multiCastNetIf);

	if(ret==SUCCESS)
	{
		ret=rtl865x_delVlan(MULTICAST_NETIF_VLAN_ID);
	}
	return ret;

}

int rtl865x_setMultiCastSrcMac(unsigned char *srcMac)
{

	if(srcMac==NULL)
	{
		return FAILED;
	}

	memcpy(multiCastNetIfMac, srcMac, 6);

	if(_rtl865x_getNetifByName(multiCastNetIf)!=NULL)
	{
		rtl865x_delMCastNetif();
		rtl865x_addMCastNetif();
	}

	return SUCCESS;
}
#endif

#endif

#if defined(CONFIG_RTL_MULTIPLE_WAN) ||defined(CONFIG_RTL_MAC_BASED_NETIF)
static int rtl865x_addMultiCastNetif(void)
{
	rtl865x_netif_t netif;
	int ret=FAILED;

	rtl865x_delVlan(MULTICAST_NETIF_VLAN_ID);
	rtl865x_addVlan(MULTICAST_NETIF_VLAN_ID);
	#if defined(CONFIG_RTL_MAC_BASED_NETIF)
	ret=rtl865x_addVlanPortMember(MULTICAST_NETIF_VLAN_ID, RTL_WANPORT_MASK);
	ret=rtl865x_setVlanFilterDatabase(MULTICAST_NETIF_VLAN_ID, RTL_WAN_FID);
	#else
	ret=rtl865x_addVlanPortMember(MULTICAST_NETIF_VLAN_ID, rtl_multiWan_config.memPort);
	ret=rtl865x_setVlanFilterDatabase(MULTICAST_NETIF_VLAN_ID, rtl_multiWan_config.fid);
	#endif
	if(_rtl865x_getNetifByName(multiCastNetIf)!=NULL)
	{
		return SUCCESS;
	}

	memset(&netif, 0, sizeof(rtl865x_netif_t));
	strcpy(netif.name, multiCastNetIf);
	memcpy(&netif.macAddr, multiCastNetIfMac, 6);
	netif.mtu = 1500;
	netif.if_type = IF_ETHER;
	netif.vid = MULTICAST_NETIF_VLAN_ID;
	netif.is_wan = 1;
	netif.is_slave = 0;
	netif.enableRoute=1;
	netif.forMacBasedMCast=TRUE;
	//printk("%s:%d,entry->lpNetif is %s \n",__FUNCTION__,__LINE__,entry->lpNetif);

	#if defined (CONFIG_RTL_8198C) ||defined(CONFIG_RTL8197F)
	netif.mtuV6 = 1500;
	#if defined(CONFIG_RTL_HARDWARE_IPV6_SUPPORT) ||(defined (CONFIG_RTL_MLD_SNOOPING)&&defined(CONFIG_RTL_HARDWARE_MULTICAST))
	netif.enableRouteV6 = 1;   	/*disable v6 router as this netif is multicast netif.*/
	#endif
	#endif

	ret = rtl865x_addNetif(&netif);
	if(ret!=SUCCESS)
	{
		rtl865x_delVlan(MULTICAST_NETIF_VLAN_ID);
		return FAILED;
	}

#if 1//defined(CONFIG_RTK_VLAN_SUPPORT)
	{
		rtl865x_AclRule_t	rule;
		bzero((void*)&rule,sizeof(rtl865x_AclRule_t));
		rule.ruleType_ = RTL865X_ACL_MAC;
		rule.pktOpApp_ = RTL865X_ACL_ALL_LAYER;
		rule.actionType_ = RTL865X_ACL_PERMIT;
		ret=rtl865x_add_acl(&rule, multiCastNetIf, RTL865X_ACL_SYSTEM_USED);
	}
#endif

	return ret;
}
#endif

#if defined(CONFIG_RTL_MULTIPLE_WAN)
#ifdef CONFIG_RTL_PROC_NEW
static int igmp_proxy_wan_dev_read_proc(struct seq_file *s, void *v)
{

	seq_printf(s, "Igmp proxy wan dev name is %s.\n", igmp_proxy_wan_dev);

	return 0;
}
#else
static int igmp_proxy_wan_dev_read_proc(char *page, char **start, off_t off,
			 int count, int *eof, void *data)
{
	int len=0;

	len += sprintf(page+len, "Igmp proxy wan dev name is %s.\n", igmp_proxy_wan_dev);

	if (len <= off+count)
		*eof = 1;

	*start = page + off;
	len -= off;

	if (len>count)
		len = count;

	if (len<0) len = 0;

	return len;
}
#endif

static int igmp_proxy_wan_dev_write_proc(struct file *file, const char *buffer,
			  unsigned long count, void *data)
{
	char tmpBuf[32];

	if (buffer && !copy_from_user(tmpBuf, buffer, count))
	{
		tmpBuf[count-1] = '\0';
		memset(igmp_proxy_wan_dev, 0, MAX_IFNAMESIZE);
		strncpy(igmp_proxy_wan_dev, tmpBuf, MAX_IFNAMESIZE);

		return count;
	}
	return -EFAULT;
}

#ifdef CONFIG_RTL_PROC_NEW
int igmp_proxy_wan_dev_single_open(struct inode *inode, struct file *file)
{
		return(single_open(file, igmp_proxy_wan_dev_read_proc,NULL));
}
static ssize_t igmp_proxy_wan_dev_single_write(struct file * file, const char __user * userbuf,
			 size_t count, loff_t * off)
{
	return igmp_proxy_wan_dev_write_proc(file, userbuf,count, off);
}
struct file_operations igmp_proxy_wan_dev_proc_fops= {
		.open           = igmp_proxy_wan_dev_single_open,
		.write		    = igmp_proxy_wan_dev_single_write,
		.read           = seq_read,
		.llseek         = seq_lseek,
		.release        = single_release,
};
#endif

static int32 rtl819x_igmp_proxy_wan_dev_proc_init(void)
{
#ifdef CONFIG_RTL_PROC_NEW
		proc_create_data("igmp_proxy_wan_dev",0,&proc_root,&igmp_proxy_wan_dev_proc_fops,NULL);
#else
	res_igmp_proxy_wan_dev = create_proc_entry("igmp_proxy_wan_dev", 0, NULL);
	if(res_igmp_proxy_wan_dev)
	{
		res_igmp_proxy_wan_dev->read_proc = igmp_proxy_wan_dev_read_proc;
		res_igmp_proxy_wan_dev->write_proc = igmp_proxy_wan_dev_write_proc;
	}
#endif
	return 0;
}

void __exit rtl819x_igmp_proxy_wan_dev_proc_exit(void)
{
#ifdef CONFIG_RTL_PROC_NEW
	remove_proc_entry("igmp_proxy_wan_dev", &proc_root);
#else
	if (res_igmp_proxy_wan_dev) {
		remove_proc_entry("igmp_proxy_wan_dev", res_igmp_proxy_wan_dev);
		res_igmp_proxy_wan_dev = NULL;
	}
#endif
}
#endif

#if defined(CONFIG_RTL_REINIT_SWITCH_CORE)
#if defined(CONFIG_RTL_8197F)
uint32 swtcr0_value = 0;
uint32 plitimr_value = 0;
extern int rtl819x_save_inused_route(void);
extern int rtl819x_restore_inused_route(void);
extern void rtl819x_save_hw_arp_table(void);
extern void rtl819x_restore_hw_arp_table(void);
#if defined(RTL_SWNAT)
inline void rtl819x_retore_hw_ip(void) {};
#else //defined(RTL_SWNAT)
extern int rtl819x_retore_hw_ip(void);
#endif //defined(RTL_SWNAT)
extern void rtl819x_save_hw_ppp_table(void);
extern void rtl819x_restore_hw_ppp_table(void);
void rtl819x_save_netif_based_way(void)
{
	swtcr0_value = READ_MEM32(SWTCR0);
	plitimr_value = READ_MEM32(PLITIMR);
}

void rtl819x_retore_netif_based_way(void)
{
	WRITE_MEM32(SWTCR0, swtcr0_value);
	WRITE_MEM32(PLITIMR, plitimr_value);
}
void rtl819x_save_hw_tables(void){
	rtl819x_save_netif_based_way();
	rtl819x_save_hw_arp_table();
	rtl819x_save_hw_ppp_table();
	rtl819x_save_inused_route();
}
void rtl819x_restore_hw_tables(void){
	rtl819x_retore_netif_based_way();
	rtl819x_retore_hw_ip();
	rtl819x_restore_hw_l2_table();
	rtl819x_restore_hw_ppp_table();
	rtl819x_restore_inused_route();
	rtl819x_restore_hw_arp_table();
}

#endif

#if defined(CONFIG_RTD_1295_HWNAT)
/* 981C_0000(CPUIR) */
#define CPUIR_REG_NUM	43
static uint32 cpuir_reg[CPUIR_REG_NUM];

void rtd129x_save_cpuir_regs(void)
{
	int i;

	/*disable switch core interrupt*/
	REG32(CPUIIMR) = 0;
	REG32(CPUIISR) = REG32(CPUIISR);
	REG32(CPUICR) = 0;
	REG32(SIRR) = 0;

	for (i=0; i<CPUIR_REG_NUM; i++) {
		cpuir_reg[i] = REG32(CPUICR + (i * 4));
	}
}

void rtd129x_restore_cpuir_regs(void)
{
	int i;

	for (i=0; i<CPUIR_REG_NUM; i++) {
		REG32(CPUICR + (i * 4)) = cpuir_reg[i];
	}
}

/* 981C_4000(GPCR) */
#define GPCR_REG_NUM	27
static uint32 gpcr_reg[GPCR_REG_NUM];

void rtd129x_save_gpcr_regs(void)
{
	int i;

	for (i=0; i<GPCR_REG_NUM; i++) {
		gpcr_reg[i] = REG32(MACCR + (i * 4));
	}
}

void rtd129x_restore_gpcr_regs(void)
{
	int i;

	for (i=0; i<GPCR_REG_NUM; i++) {
		REG32(MACCR + (i * 4)) = gpcr_reg[i];
	}
}

/* 981C_4100(PITCR) */
#define PITCR_REG_NUM	46
static uint32 pitcr_reg[PITCR_REG_NUM];

void rtd129x_save_pitcr_regs(void)
{
	int i;

	for (i=0; i<PITCR_REG_NUM; i++) {
		pitcr_reg[i] = REG32(PITCR + (i * 4));
	}
}

void rtd129x_restore_pitcr_regs(void)
{
	int i;

	for (i=0; i<PITCR_REG_NUM; i++) {
		REG32(PITCR + (i * 4)) = pitcr_reg[i];
	}
}

/* 981C_4300(LEDCR) */
#define LEDCR_REG_NUM	5
static uint32 ledcr_reg[LEDCR_REG_NUM];

void rtd129x_save_ledcr_regs(void)
{
	int i;

	for (i=0; i<LEDCR_REG_NUM; i++) {
		ledcr_reg[i] = REG32(LEDCR0 + (i * 4));
	}
}

void rtd129x_restore_ledcr_regs(void)
{
	int i;

	for (i=0; i<LEDCR_REG_NUM; i++) {
		REG32(LEDCR0 + (i * 4)) = ledcr_reg[i];
	}
}

/* 981C_4400(ALECR) */
#define ALECR_REG_NUM	44
static uint32 alecr_reg[ALECR_REG_NUM];

void rtd129x_save_alecr_regs(void)
{
	int i;

	for (i=0; i<ALECR_REG_NUM; i++) {
		alecr_reg[i] = REG32(TEACR + (i * 4));
	}
}

void rtd129x_restore_alecr_regs(void)
{
	int i;

	for (i=0; i<ALECR_REG_NUM; i++) {
		REG32(TEACR + (i * 4)) = alecr_reg[i];
	}
}

/* 981C_4500(BMCR) */
#define BMCR_REG_NUM	57
static uint32 bmcr_reg[BMCR_REG_NUM];

void rtd129x_save_bmcr_regs(void)
{
	int i;

	for (i=0; i<BMCR_REG_NUM; i++) {
		bmcr_reg[i] = REG32(SBFCR0 + (i * 4));
	}
}

void rtd129x_restore_bmcr_regs(void)
{
	int i;

	for (i=0; i<BMCR_REG_NUM; i++) {
		REG32(SBFCR0 + (i * 4)) = bmcr_reg[i];
	}
}

/* 981C_4700(QOSFR) */
#define QOSFR_REG_NUM	32
static uint32 qosfr_reg[QOSFR_REG_NUM];

void rtd129x_save_qosfr_regs(void)
{
	int i;

	for (i=0; i<QOSFR_REG_NUM; i++) {
		qosfr_reg[i] = REG32(QOSFCR + (i * 4));
	}
}

void rtd129x_restore_qosfr_regs(void)
{
	int i;

	for (i=0; i<QOSFR_REG_NUM; i++) {
		REG32(QOSFCR + (i * 4)) = qosfr_reg[i];
	}
}

/* 981C_4800(MRGCR) */
#define MRGCR_REG_NUM	65
static uint32 mrgcr_reg[MRGCR_REG_NUM];

void rtd129x_save_mrgcr_regs(void)
{
	int i;

	for (i=0; i<MRGCR_REG_NUM; i++) {
		mrgcr_reg[i] = REG32(P0Q0RGCR + (i * 4));
	}
}

void rtd129x_restore_mrgcr_regs(void)
{
	int i;

	for (i=0; i<MRGCR_REG_NUM; i++) {
		REG32(P0Q0RGCR + (i * 4)) = mrgcr_reg[i];
	}
}

/* 981C_4900(LBPCR) */
#define LBPCR_REG_NUM	4
static uint32 lbpcr_reg[LBPCR_REG_NUM];

void rtd129x_save_lbpcr_regs(void)
{
	int i;

	for (i=0; i<LBPCR_REG_NUM; i++) {
		lbpcr_reg[i] = REG32(ELBPCR + (i * 4));
	}
}

void rtd129x_restore_lbpcr_regs(void)
{
	int i;

	for (i=0; i<LBPCR_REG_NUM; i++) {
		REG32(ELBPCR + (i * 4)) = lbpcr_reg[i];
	}
}

/* 981C_4A00(VCR) */
#define VCR_REG_NUM	39
static uint32 vcr_reg[VCR_REG_NUM];

void rtd129x_save_vcr_regs(void)
{
	int i;

	for (i=0; i<VCR_REG_NUM; i++) {
		vcr_reg[i] = REG32(VCR0 + (i * 4));
	}
}

void rtd129x_restore_vcr_regs(void)
{
	int i;

	for (i=0; i<VCR_REG_NUM; i++) {
		REG32(VCR0 + (i * 4)) = vcr_reg[i];
	}
}

/* 981C_4B00(PBACR) */
#define PBACR_REG_NUM	3
static uint32 pbacr_reg[PBACR_REG_NUM];

void rtd129x_save_pbacr_regs(void)
{
	int i;

	for (i=0; i<PBACR_REG_NUM; i++) {
		pbacr_reg[i] = REG32(DOT1XPORTCR + (i * 4));
	}
}

void rtd129x_restore_pbacr_regs(void)
{
	int i;

	for (i=0; i<PBACR_REG_NUM; i++) {
		REG32(DOT1XPORTCR + (i * 4)) = pbacr_reg[i];
	}
}

/* 981C_4C00(LACR) */
#define LACR_REG_NUM	3
static uint32 lacr_reg[LACR_REG_NUM];

void rtd129x_save_lacr_regs(void)
{
	lacr_reg[0] = REG32(LAGHPMR0);
	lacr_reg[2] = REG32(LAGCR0);
}

void rtd129x_restore_lacr_regs(void)
{
	REG32(LAGHPMR0) = lacr_reg[0];
	REG32(LAGCR0)   = lacr_reg[2];
}

/* 981C_5000(8PQGCR) */
#define PQGCR_REG_NUM	62
static uint32 pqgcr_reg[PQGCR_REG_NUM];

void rtd129x_save_pqgcr_regs(void)
{
	int i;

	for (i=0; i<PQGCR_REG_NUM; i++) {
		pqgcr_reg[i] = REG32(PQGCR8 + (i * 4));
	}
}

void rtd129x_restore_pqgcr_regs(void)
{
	int i;

	for (i=0; i<PQGCR_REG_NUM; i++) {
		REG32(PQGCR8 + (i * 4)) = pqgcr_reg[i];
	}
}

/* 981C_5100(EMACCR) */
#define EMACCR_REG_NUM	17
static uint32 emaccr_reg[EMACCR_REG_NUM];

void rtd129x_save_emaccr_regs(void)
{
	int i;

	for (i=0; i<EMACCR_REG_NUM; i++) {
		emaccr_reg[i] = REG32(MACCTRL1 + (i * 4));
	}
}

void rtd129x_restore_emaccr_regs(void)
{
	int i;

	for (i=0; i<EMACCR_REG_NUM; i++) {
		REG32(MACCTRL1 + (i * 4)) = emaccr_reg[i];
	}
}

/* 981C_5200(IPv6CR) */
#define IPV6CR_REG_NUM	25
static uint32 ipv6cr_reg[IPV6CR_REG_NUM];

void rtd129x_save_ipv6cr_regs(void)
{
	int i;

	for (i=0; i<IPV6CR_REG_NUM; i++) {
		ipv6cr_reg[i] = REG32(IPV6CR0 + (i * 4));
	}
}

void rtd129x_restore_ipv6cr_regs(void)
{
	int i;

	for (i=0; i<IPV6CR_REG_NUM; i++) {
		REG32(IPV6CR0 + (i * 4)) = ipv6cr_reg[i];
	}
}

/* End of regs */
void rtd129x_save_regs(void)
{
	rtd129x_save_cpuir_regs();
	rtd129x_save_gpcr_regs();
	rtd129x_save_pitcr_regs();
	rtd129x_save_ledcr_regs();
	rtd129x_save_alecr_regs();
	rtd129x_save_bmcr_regs();
	rtd129x_save_qosfr_regs();
	rtd129x_save_mrgcr_regs();
	rtd129x_save_lbpcr_regs();
	rtd129x_save_vcr_regs();
	rtd129x_save_lacr_regs();
	rtd129x_save_pqgcr_regs();
	rtd129x_save_emaccr_regs();
	rtd129x_save_ipv6cr_regs();
}

void rtd129x_restore_regs(void)
{
	rtd129x_restore_cpuir_regs();
	rtd129x_restore_gpcr_regs();
	rtd129x_restore_pitcr_regs();
	rtd129x_restore_ledcr_regs();
	rtd129x_restore_alecr_regs();
	rtd129x_restore_bmcr_regs();
	rtd129x_restore_qosfr_regs();
	rtd129x_restore_mrgcr_regs();
	rtd129x_restore_lbpcr_regs();
	rtd129x_restore_vcr_regs();
	rtd129x_restore_lacr_regs();
	rtd129x_restore_pqgcr_regs();
	rtd129x_restore_emaccr_regs();
	rtd129x_restore_ipv6cr_regs();
}

#endif //defined(CONFIG_RTD_1295_HWNAT)

int  re865x_reProbe (void)
{
	rtl8651_tblAsic_InitPara_t para;
	unsigned long flags=0;
#if defined(CONFIG_RTL_819XD)&&defined(CONFIG_RTL_8211DS_SUPPORT)&&defined(CONFIG_RTL_8197D)
	uint32 reg_tmp=0;
#endif

	SMP_LOCK_ETH(flags);
	//WRITE_MEM32(PIN_MUX_SEL_2, 0x7<<21);
	#if !defined(CONFIG_RTD_1295_HWNAT)
	/*Initial ASIC table*/
	FullAndSemiReset();
	#endif //!defined(CONFIG_RTD_1295_HWNAT)

	memset(&para, 0, sizeof(rtl8651_tblAsic_InitPara_t));

	/*
		For DEMO board layout, RTL865x platform define corresponding PHY setting and PHYID.
	*/

	INIT_CHECK(rtl865x_initAsicL2(&para));

#ifdef CONFIG_RTL_LAYERED_ASIC_DRIVER_L3
	INIT_CHECK(rtl865x_initAsicL3());
#endif
#if defined(CONFIG_RTL_LAYERED_ASIC_DRIVER_L4)
	INIT_CHECK(rtl865x_initAsicL4());
#endif


	/*init PHY LED style*/
#if defined(CONFIG_RTL_8196C) || defined (CONFIG_RTL_8198)
	/* config LED mode */
	WRITE_MEM32(LEDCR, 0x00000000 ); // 15 LED
	WRITE_MEM32(SWTAA, PORT5_PHY_CONTROL);
	WRITE_MEM32(TCR0, 0x000002C7); //8651 demo board default: 15 LED boards
	WRITE_MEM32(SWTACR, CMD_FORCE | ACTION_START); // force add
#endif

/*2007-12-19*/
#if defined(CONFIG_RTK_VLAN_SUPPORT)
		//port based decision
	rtl865xC_setNetDecisionPolicy(NETIF_PORT_BASED);
	WRITE_MEM32(PLITIMR,0);
#endif

#if defined(CONFIG_RTL_819XD)&&defined(CONFIG_RTL_8211DS_SUPPORT)&&defined(CONFIG_RTL_8197D)
	rtl8651_getAsicEthernetPHYReg(0x6, 0, &reg_tmp);
	rtl_setPortMask(reg_tmp);
#endif

	/*queue id & rx ring descriptor mapping*/
	REG32(CPUQDM0)=QUEUEID1_RXRING_MAPPING|(QUEUEID0_RXRING_MAPPING<<16);
	REG32(CPUQDM2)=QUEUEID3_RXRING_MAPPING|(QUEUEID2_RXRING_MAPPING<<16);
	REG32(CPUQDM4)=QUEUEID5_RXRING_MAPPING|(QUEUEID4_RXRING_MAPPING<<16);


	rtl8651_setAsicOutputQueueNumber(CPU, RTL_CPU_RX_RING_NUM);

#if defined(CONFIG_RTL_HW_STP)
	//Initial: disable Realtek Hardware STP
	rtl865x_setSpanningEnable(FALSE);
#endif


#ifdef CONFIG_RTL_LAYERED_ASIC_DRIVER_L3
#if defined (CONFIG_RTL_HARDWARE_MULTICAST)
	rtl8651_setAsicMulticastEnable(TRUE);
#else
	rtl8651_setAsicMulticastEnable(FALSE);
#endif
#endif

#ifdef CONFIG_RTL_8198_ESD
	esd3_skip_one = 1;
	one_second_counter = 0;
	first_time_read_reg6 = 1;
	phy_reg30[0] = phy_reg30[1] = phy_reg30[2] = phy_reg30[3] = phy_reg30[4] = 0;
#endif

#ifdef CONFIG_RTL_8198
	{
	extern void disable_phy_power_down(void);
	disable_phy_power_down();
	}
#endif

#if defined(CONFIG_RTL_SWITCH_NEW_DESCRIPTOR)
	REG32(CPUICR1) = (REG32(CPUICR1) & ~CF_PKT_HDR_TYPE_MASK) | TX_PKTHDR_SHORTCUT_LSO;
#endif

	SMP_UNLOCK_ETH(flags);
	return 0;
}

#define BSP_SW_IE           (1 << 15)
int rtl865x_reinitSwitchCore(void)
{
	rtl865x_duringReInitSwtichCore=1;
#if !defined(CONFIG_RTD_1295_HWNAT)
	/*disable switch core interrupt*/
	REG32(CPUIIMR) = 0;
	REG32(CPUIISR) = REG32(CPUIISR);
	REG32(GIMR) &= ~(BSP_SW_IE);
	REG32(CPUICR) = 0;
	REG32(SIRR) = 0;

#if defined(CONFIG_RTL_8197F)
	rtl819x_save_hw_tables();
#endif
#endif //!defined(CONFIG_RTD_1295_HWNAT)

	re865x_reProbe();
	RTL_swNic_reInit();
	rtl865x_reChangeOpMode();

#if defined(CONFIG_RTL_8197F)
	rtl819x_restore_hw_tables();
#endif

	#if defined(CONFIG_RTL_HW_VLAN_SUPPORT_HW_NAT)
	if (rtl_hw_vlan_enable)
		rtl_hw_vlan_config();
	#endif

#if defined(CONFIG_RTL_8197F)
	REG32(DMA_CR0) = (REG32(DMA_CR0) & ~(LowFifoMark_MASK|HiFifoMark_MASK)) | ((0x30<<LowFifoMark_OFFSET)|0xD7);
	#if defined(CONFIG_RTD_1295_HWNAT)
	{
		rtl865xc_tblAsic_aclTable_t tmp_rule;
		memset(&tmp_rule, 0, sizeof(rtl865xc_tblAsic_aclTable_t));

		_rtl8651_readAsicEntry(TYPE_ACL_RULE_TABLE, 0, &tmp_rule);
		_rtl8651_forceAddAsicEntry(TYPE_ACL_RULE_TABLE, 0, &tmp_rule);
	}
	#else //!defined(CONFIG_RTD_1295_HWNAT)
	REG32(0xbb0c0000) = REG32(0xbb0c0000);
	#endif //!defined(CONFIG_RTD_1295_HWNAT)
#endif

	/*enable switch core interrupt*/
	REG32(CPUICR) = TXCMD | RXCMD | BUSBURST_128WORDS | MBUF_2048BYTES;
	REG32(CPUIISR) = REG32(CPUIISR);
	REG32(CPUIIMR) = RX_DONE_IE_ALL | TX_ALL_DONE_IE_ALL | LINK_CHANGE_IE | PKTHDR_DESC_RUNOUT_IE_ALL;
	REG32(SIRR) |= TRXRDY;
	#if !defined(CONFIG_RTD_1295_HWNAT)
	REG32(GIMR) |= (BSP_SW_IE);
	#endif //!defined(CONFIG_RTD_1295_HWNAT)

	rtl865x_duringReInitSwtichCore=0;
	return 0;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)) // added by lynn_pu, 2014-10-16
static struct proc_dir_entry *reInitSwitchCoreProc=NULL;
#endif

#ifdef CONFIG_RTL_PROC_NEW
static int rtl865x_reInitSwitchCoreReadProc(struct seq_file *s, void *v)
{
	return 0;
}
#else
static int rtl865x_reInitSwitchCoreReadProc(char *page, char **start, off_t off,
			 int count, int *eof, void *data)
{
	int len=0;

	if (len <= off+count)
		*eof = 1;

	*start = page + off;
	len -= off;

	if (len>count)
		len = count;

	if (len<0) len = 0;

	return len;
}
#endif

static int rtl865x_reInitSwitchCoreWriteProc(struct file *file, const char *buffer,
			  unsigned long count, void *data)
{
	char tmpBuf[256];
	char		*strptr;
	char		*tokptr;
	unsigned int action;

	if (buffer && !copy_from_user(tmpBuf, buffer, count))
	{
		tmpBuf[count-1]=0;
		strptr=tmpBuf;

		tokptr = strsep(&strptr," ");
		if (tokptr==NULL)
		{
			goto errOut;
		}

		if(rtl865x_duringReInitSwtichCore)
		{
			goto errOut;
		}
		//printk("here to reset switch core\n");
		action=simple_strtol(tokptr, NULL, 0);
		if(action==1)
		{
			rtl865x_reinitSwitchCore();
		}

		return count;
	}

errOut:
	return -EFAULT;
}

#if defined(CONFIG_RTL_CHECK_SWITCH_TX_HANGUP)


#ifdef CONFIG_RTL_PROC_NEW
static int rtl_check_swCore_tx_hang_read_proc(struct seq_file *s, void *v)
{
	seq_printf(s, "check_tx_hang_interval=%d, reinit_swCore_threshold=%d, rtl_reinit_swCore_counter=%d\n",
		rtl_check_swCore_tx_hang_interval, rtl_reinit_swCore_threshold, rtl_reinit_swCore_counter);

	return 0;
}
#else
static struct proc_dir_entry *rtl_check_swCore_tx_hang_proc=NULL;
static int rtl_check_swCore_tx_hang_read_proc(char *page, char **start, off_t off,
			 int count, int *eof, void *data)
{
	int len=0;

	len = sprintf(page, "check_tx_hang_interval=%d, reinit_swCore_threshold=%d, rtl_reinit_swCore_counter=%d\n",
		rtl_check_swCore_tx_hang_interval, rtl_reinit_swCore_threshold, rtl_reinit_swCore_counter);

	if (len <= off+count)
		*eof = 1;

	*start = page + off;
	len -= off;

	if (len>count)
		len = count;

	if (len<0) len = 0;

	return len;
}
#endif

static int rtl_check_swCore_tx_hang_write_proc(struct file *file, const char *buffer,
			  unsigned long count, void *data)
{
	char tmpBuf[64];

	if (buffer && !copy_from_user(tmpBuf, buffer, count))
	{
		int num = sscanf(tmpBuf, "%d %d", &rtl_check_swCore_tx_hang_interval, &rtl_reinit_swCore_threshold);

		if (num != 2) {
			panic_printk("invalid parameter!\n");
			goto errOut;
		}

		return count;
	}

errOut:
	return -EFAULT;
}
#endif

int  rtl865x_creatReInitSwitchCoreProc(void)
{
#ifdef CONFIG_RTL_PROC_NEW
	proc_create_data("reInitSwitchCore",0,&proc_root,&reInitSwitchCore_proc_fops,NULL);
#if defined(CONFIG_RTL_CHECK_SWITCH_TX_HANGUP)
	proc_create_data("check_swCore_tx_hang",0,&proc_root,&rtl_check_swCore_tx_hang_proc_fops,NULL);
#endif
#else
	reInitSwitchCoreProc = create_proc_entry("reInitSwitchCore", 0, NULL);
	if(reInitSwitchCoreProc)
	{
		reInitSwitchCoreProc->read_proc = rtl865x_reInitSwitchCoreReadProc;
		reInitSwitchCoreProc->write_proc = rtl865x_reInitSwitchCoreWriteProc;
	}
#if defined(CONFIG_RTL_CHECK_SWITCH_TX_HANGUP)
	rtl_check_swCore_tx_hang_proc = create_proc_entry("check_swCore_tx_hang", 0, NULL);
	if(rtl_check_swCore_tx_hang_proc)
	{
		rtl_check_swCore_tx_hang_proc->read_proc = rtl_check_swCore_tx_hang_read_proc;
		rtl_check_swCore_tx_hang_proc->write_proc = rtl_check_swCore_tx_hang_write_proc;
	}
#endif
#endif

	return 0;
}

void __exit rtl865x_destroyReInitResetSwitchCore(void)
{
#ifdef CONFIG_RTL_PROC_NEW
	remove_proc_entry("reInitSwitchCore", &proc_root);
#if defined(CONFIG_RTL_CHECK_SWITCH_TX_HANGUP)
	remove_proc_entry("check_swCore_tx_hang", &proc_root);
#endif
#else
	if (reInitSwitchCoreProc) {
		remove_proc_entry("reInitSwitchCore", reInitSwitchCoreProc);
		reInitSwitchCoreProc = NULL;
	}
#if defined(CONFIG_RTL_CHECK_SWITCH_TX_HANGUP)
	if (rtl_check_swCore_tx_hang_proc) {
		remove_proc_entry("check_swCore_tx_hang", rtl_check_swCore_tx_hang_proc);
		rtl_check_swCore_tx_hang_proc = NULL;
	}
#endif
#endif
}
#if defined(CONFIG_RTL_CHECK_SWITCH_TX_HANGUP)
void rtl_check_swCore_tx_hang(void)
{
	int32 tx_done_index_tmp = 0;

	if ((rtl_swCore_tx_hang_cnt>0) ||(((rtl_check_swCore_timer++) % rtl_check_swCore_tx_hang_interval)==0)) {
		if (RTL_check_tx_own(&tx_done_index_tmp) == SUCCESS) {
			if (rtl_last_tx_done_idx != tx_done_index_tmp) {
				rtl_last_tx_done_idx = tx_done_index_tmp;
				rtl_swCore_tx_hang_cnt = 1;
			} else {
				rtl_swCore_tx_hang_cnt ++;
			}
		} else {
			rtl_swCore_tx_hang_cnt = 0;
		}

		if (rtl_swCore_tx_hang_cnt >= rtl_reinit_swCore_threshold) {
			panic_printk("SwCore tx hang is detected!\n");
			rtl_swCore_tx_hang_cnt = 0;

			if (rtl865x_duringReInitSwtichCore == 0) {
				panic_printk("Switch will reinit now!\n");
				rtl_reinit_swCore_counter ++;
				rtl865x_reinitSwitchCore();
			}
		}
	}

	return;
}
#endif

#endif

#ifdef CONFIG_RTK_VOIP_QOS
int wan_port_check(int port)
{
	if(1<<port & RTL_WANPORT_MASK)
		return TRUE;
	else
		return FALSE;
}
#endif

#ifdef CONFIG_RTK_VOIP_PORT_LINK
static int rtnl_fill_ifinfo_voip(struct sk_buff *skb, struct net_device *dev,
							int type, u32 pid, u32 seq, u32 change,
							unsigned int flags)
{
		struct ifinfomsg *ifm;
		struct nlmsghdr *nlh;

		nlh = nlmsg_put(skb, pid, seq, type, sizeof(*ifm), flags);
		if (nlh == NULL)
				return -ENOBUFS;

		ifm = nlmsg_data(nlh);
		ifm->ifi_family = AF_UNSPEC;
		ifm->__ifi_pad = 0;
		ifm->ifi_type = dev->type;
		ifm->ifi_index = dev->ifindex;
		ifm->ifi_flags = dev_get_flags(dev);
		ifm->ifi_change = change;
		return nlmsg_end(skb, nlh);
}
static void rtmsg_ifinfo_voip(int type, struct net_device *dev, unsigned change)
{
		struct net *net = dev_net(dev);
		struct sk_buff *skb;
		int err = -ENOBUFS;

		skb = nlmsg_new(NLMSG_GOODSIZE, GFP_ATOMIC);
		if (skb == NULL)
				goto errout;

		err = rtnl_fill_ifinfo_voip(skb, dev, type, 0, 0, change, 0);
		if (err < 0) {
				/* -EMSGSIZE implies BUG in if_nlmsg_size() */
				WARN_ON(err == -EMSGSIZE);
				kfree_skb(skb);
				goto errout;
		}
		rtnl_notify(skb, net, 0, RTNLGRP_LINK, NULL, GFP_ATOMIC);
		return;
errout:
		if (err < 0)
				rtnl_set_sk_err(net, RTNLGRP_LINK, err);
}
#endif


#if defined (CONFIG_RTL_SOCK_DEBUG)
static struct proc_dir_entry *rtl865x_sockDebugProc=NULL;
extern int dumpRawSockInfo(char * buf);
extern int dumpUdpSockInfo(char * buf);
extern int dumpTcpSockInfo(char * buf);
#ifdef CONFIG_RTL_PROC_NEW
static int32 rtl865x_sockDebugReadProc(struct seq_file *s, void *v)
{
	dumpRawSockInfo();
	dumpUdpSockInfo();
	dumpTcpSockInfo();
	return 0;
}
#else
static int rtl865x_sockDebugReadProc(char *page, char **start, off_t off,
			 int count, int *eof, void *data)
{
	int len=0;

	len+=dumpRawSockInfo(page+len);
	len+=dumpUdpSockInfo(page+len);
	len+=dumpTcpSockInfo(page+len);

	return len;
}
#endif

static int rtl865x_sockDebugWriteProc(struct file *file, const char *buffer,
			  unsigned long count, void *data)
{
	return count;
}


int  rtl865x_creatSockDebugProc(void)
{
#ifdef CONFIG_RTL_PROC_NEW
	proc_create_data("sockDebug",0,&proc_root,&sockDebug_proc_fops,NULL);
#else
	rtl865x_sockDebugProc = create_proc_entry("sockDebug", 0, NULL);
	if(rtl865x_sockDebugProc)
	{
		rtl865x_sockDebugProc->read_proc = rtl865x_sockDebugReadProc;
		rtl865x_sockDebugProc->write_proc = rtl865x_sockDebugWriteProc;
	}
#endif

	return 0;
}

void __exit rtl865x_removeSockDebugProc(void)
{
#ifdef CONFIG_RTL_PROC_NEW
	remove_proc_entry("sockDebug", &proc_root);
#else
	if (rtl865x_sockDebugProc) {
		remove_proc_entry("sockDebug", rtl865x_sockDebugProc);
		rtl865x_sockDebugProc = NULL;
	}
#endif
}
#endif

#ifdef CONFIG_RTL_ULINKER
void eth_led_recover(void)
{
	REG32(PIN_MUX_SEL_2) = REG32(PIN_MUX_SEL_2) & ~(0x00003000);
}
#endif

extern int eee_enabled;
#if defined(RTL8198_EEE_MAC)
extern void eee_phy_enable_98(void);
extern void eee_phy_disable_98(void);
#endif

#ifndef CONFIG_RTL_PROC_NEW
static struct proc_dir_entry *res_eee=NULL;
#endif
extern void enable_EEE(void);
extern void disable_EEE(void);

static int32 rtl819x_eee_proc_init(void)
{
#ifdef CONFIG_RTL_PROC_NEW
	proc_create_data("eee",0,&proc_root,&eee_proc_fops,NULL);
#else
	res_eee = create_proc_entry("eee", 0, NULL);
	if(res_eee)
	{
		res_eee->read_proc = eee_read_proc;
		res_eee->write_proc = eee_write_proc;
	}
#endif
	return 0;
}

void __exit rtl819x_eee_proc_exit(void)
{
#ifdef CONFIG_RTL_PROC_NEW
	remove_proc_entry("eee", &proc_root);
#else
	if (res_eee) {
		remove_proc_entry("eee", res_eee);
		res_eee = NULL;
	}
#endif
}

#ifdef CONFIG_RTL_PROC_NEW
static int eee_read_proc(struct seq_file *s, void *v)
{
#if defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)
	int i;
	seq_printf(s, "EEE setting:\n");

	for(i=0; i<4; i++) {
		seq_printf(s, "port %d: eee %sabled.\n",i, ((REG32(EEECR) & BIT(i*8)) ? "en" : "dis") );
	}
	seq_printf(s, "port 4: eee %sabled.\n",((REG32(EEEABICR1) & BIT(0)) ? "en" : "dis") );

	#ifdef CONFIG_RTL_8198C_8367RB
	{
	uint32 enable;
	int ret, j;
	extern int rtk_eee_portEnable_get(uint32 port, uint32 *enable);
	seq_printf(s, "\nRTL8367RB EEE status:\n");
	for (j=0;j<5;j++) {
		ret = rtk_eee_portEnable_get(j, &enable);
		if (ret != 0)
			seq_printf(s, "  port %d: read 8367RB EEE status error.\n",j);
		else
			seq_printf(s, "  port %d: 8367RB EEE %sabled.\n", j, ((enable == 1) ? "en" : "dis"));
	}
	}
	#endif
#else
	seq_printf(s, "eee %sabled.\n", ((eee_enabled) ? "en" : "dis")  );
#endif
	return 0;
}
#else
static int eee_read_proc(char *page, char **start, off_t off,
			 int count, int *eof, void *data)
{
	int len=0;

	len += sprintf(page+len, "eee %sabled.\n", ((eee_enabled) ? "en" : "dis")  );

	if (len <= off+count)
		*eof = 1;

	*start = page + off;
	len -= off;

	if (len>count)
		len = count;

	if (len<0) len = 0;

	return len;
}
#endif

static int eee_write_proc(struct file *file, const char *buffer,
			  unsigned long count, void *data)
{
	char tmpBuf[32];

	if (buffer && !copy_from_user(tmpBuf, buffer, count))
	{
#if defined(CONFIG_RTL_8198C)
		int mode;
		uint32 port_mask = 0x1f;
		char	*strptr, *tokptr;
		extern void set_8198C_EEE(int mode, uint32 port_mask);

		if ((tmpBuf[0] == '0') || (tmpBuf[0] == '1')) {
			mode = tmpBuf[0] - '0';
			eee_enabled = mode;

			if (count >= 3) {
				tmpBuf[count] = '\0';
				strptr = tmpBuf + 2;

				tokptr = strsep(&strptr," ");
				if (tokptr!=NULL)
				{
					port_mask=simple_strtol(tokptr, NULL, 16);
				}
			}

			set_8198C_EEE(mode, port_mask);

			#ifdef CONFIG_RTL_8198C_8367RB
			{
			int i;
			extern int rtk_eee_portEnable_set(uint32 port, uint32 enable);
			for (i=0;i<5;i++)
				rtk_eee_portEnable_set(i, mode);
			}
			#endif
		}
		else {
			return -EFAULT;
		}
#else
		if (tmpBuf[0] == '0') {
			eee_enabled = FALSE;
			disable_EEE();
		}
		else if (tmpBuf[0] == '1') {
			eee_enabled = TRUE;
			enable_EEE();
		}
		else {
			return -EFAULT;
		}
#endif

#ifdef CONFIG_RTL_8198_ESD
		esd3_skip_one = 1;
#endif
		return count;
	}
	return -EFAULT;
}

#ifdef CONFIG_RTL865X_LANPORT_RESTRICTION
char *rtl_getDevNameByPort(int32 port_num)
{
	int i;
	struct dev_priv *cp;

	for(i = 0; i < ETH_INTF_NUM; i++)
	{
		cp = ((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[i]));
		if(_rtl86xx_dev.dev[i] && cp && cp->opened && (cp->portmask & (1<<port_num)))
			return cp->dev->name;
	}

	return NULL;
}
#endif

#ifdef CONFIG_RTL_JATE_TEST

#include <linux/random.h>

#define PKT_LEN		1500
#define DEF_PKT_INTERVAL 		50		// uint: ms
static const unsigned char eth_da_sa[] = {
  0xff ,0xff ,0xff ,0xff ,0xff ,0xff ,0x00 ,0x12  ,0x34 ,0x56 ,0x78 ,0x9a
};

/*
static const unsigned char pkt02[] = {
  0x01 ,0x00 ,0x5e ,0x00 ,0x00 ,0x01 ,0x00 ,0x11  ,0x85 ,0xae ,0x7f ,0x2c ,0x08 ,0x00 ,0x45 ,0x00,
  0x00 ,0x76 ,0xbb ,0xfb ,0x00 ,0x00 ,0x01 ,0x11  ,0x3b ,0x65 ,0xac ,0x12 ,0x35 ,0x03 ,0xe1 ,0x00,
  0x00 ,0x01 ,0x27 ,0x0f ,0x27 ,0x0f ,0x00 ,0x62  ,0xc6 ,0x39 ,0x54 ,0x19 ,0x98 ,0x04 ,0xf4 ,0x03,
  0x52 ,0x00 ,0x91 ,0x65 ,0x30 ,0x35 ,0x39 ,0x31  ,0x38 ,0x33 ,0x38 ,0x33 ,0x36 ,0x37 ,0x30 ,0x36,
  0x00 ,0x62 ,0x12 ,0x00 ,0x38 ,0x31 ,0x36 ,0x38  ,0x34 ,0x30 ,0x30 ,0x00 ,0x26 ,0x00 ,0xe8 ,0x61,
  0x00 ,0x00 ,0x36 ,0x36 ,0x34 ,0x36 ,0x38 ,0x36  ,0x32 ,0x32 ,0x00 ,0x38 ,0x31 ,0x37 ,0x39 ,0x39,
  0x33 ,0x31 ,0x38 ,0x36 ,0x00 ,0x39 ,0x33 ,0x36  ,0x32 ,0x00 ,0x00 ,0x30 ,0x00 ,0x00 ,0x02 ,0x00,
  0x00 ,0x17 ,0x1e ,0x00 ,0x01 ,0x00 ,0x00 ,0x00  ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00,
  0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00
}; */

#ifndef CONFIG_RTL_PROC_NEW
static struct proc_dir_entry *res_jate=NULL;
#endif

static int32 rtl819x_jate_proc_init(void)
{
#ifdef CONFIG_RTL_PROC_NEW
	proc_create_data("jate",0,&proc_root,&jate_proc_fops,NULL);
#else
	res_jate = create_proc_entry("jate", 0, NULL);
	if(res_jate)
	{
		res_jate->read_proc = jate_read_proc;
		res_jate->write_proc = jate_write_proc;
	}
#endif
	return 0;
}

void __exit rtl819x_jate_proc_exit(void)
{
#ifdef CONFIG_RTL_PROC_NEW
	remove_proc_entry("jate", &proc_root);
#else
	if (res_jate) {
		remove_proc_entry("jate", res_jate);
		res_jate = NULL;
	}
#endif
}

#ifdef CONFIG_RTL_PROC_NEW
static int jate_read_proc(struct seq_file *s, void *v)
{
	return 0;
}
#else
static int jate_read_proc(char *page, char **start, off_t off,
			 int count, int *eof, void *data)
{
	return 0;
}
#endif

static int jate_write_proc(struct file *filp, const char *buff,unsigned long len, void *data)
{
	char 	tmpbuf[64];
	uint32	port, mode, tx_time, tx_cnt, interval = DEF_PKT_INTERVAL;
	int 		i, j;
	char		*strptr, *cmd_addr, *tokptr;
#if defined(CONFIG_RTL_8367R_SUPPORT) || defined(CONFIG_RTL_8370_SUPPORT)
	extern int set_83XX_speed_mode(int port, int type);
#else
	uint32	phy_mode;
#endif

	if(len>64)
	{
		goto errout;
	}
	if (buff && !copy_from_user(tmpbuf, buff, len)) {
		tmpbuf[len] = '\0';
		strptr=tmpbuf;
		cmd_addr = strsep(&strptr," ");
		if (cmd_addr==NULL)
		{
			goto errout;
		}

		tokptr = strsep(&strptr," ");
		if (tokptr==NULL)
		{
			goto errout;
		}

		if (!memcmp(cmd_addr, "port", 4))
		{
			port=(uint32)simple_strtol(tokptr, NULL, 0);
			if (port > 5)
				goto errout;

			tokptr = strsep(&strptr," ");
			if (tokptr==NULL)
			{
				goto errout;
			}

#if defined(CONFIG_RTL_8367R_SUPPORT) || defined(CONFIG_RTL_8370_SUPPORT)
			// refer to port_status_write() in rtl865x_proc_debug.c
			if (!memcmp(tokptr, "10H", 3)) {
				mode = HALF_DUPLEX_10M;
			}
			else if (!memcmp(tokptr, "10F", 3)) {
				mode = DUPLEX_10M;
			}
			else if (!memcmp(tokptr, "100H", 4)) {
				mode = HALF_DUPLEX_100M;
			}
			else if (!memcmp(tokptr, "100F", 4)) {
				mode = DUPLEX_100M;
			}
			else if (!memcmp(tokptr, "1000F", 5)) {
				mode = DUPLEX_1000M;
			}
			else if (!memcmp(tokptr, "Auto", 4)) {
				mode = PORT_AUTO;
			}
			else
				goto errout;

			if (port == 5) {
				for (i=0;i<5;i++) {
					set_83XX_speed_mode(i, mode);
				}
			}
			else {
				set_83XX_speed_mode(port, mode);
			}
#else
			if (!memcmp(tokptr, "10H", 3)) {
				mode = EnForceMode | ForceLink;
				phy_mode = 0;
			}
			else if (!memcmp(tokptr, "10F", 3)) {
				mode = EnForceMode | ForceLink | ForceDuplex;
				phy_mode = SELECT_FULL_DUPLEX;
			}
			else if (!memcmp(tokptr, "100H", 4)) {
				mode = EnForceMode | ForceLink | ForceSpeed100M;
				phy_mode = SPEED_SELECT_100M;
			}
			else if (!memcmp(tokptr, "100F", 4)) {
				mode = EnForceMode | ForceLink | ForceSpeed100M | ForceDuplex;
				phy_mode = SPEED_SELECT_100M | SELECT_FULL_DUPLEX;
			}
			else if (!memcmp(tokptr, "Auto", 4)) {
				mode = NwayAbility10MH | NwayAbility10MF | NwayAbility100MH | NwayAbility100MF | NwayAbility1000MF;
				phy_mode = ENABLE_AUTONEGO | SELECT_FULL_DUPLEX;
			}
			else
				goto errout;

			if (port == 5) {
				for (i=0;i<5;i++) {
					//REG32(PCRP0+(i*4)) |= EnForceMode;
					//rtl8651_setAsicEthernetPHYReg( i, 0, POWER_DOWN );
					//mdelay(1000);
					REG32(PCRP0+(i*4)) = (REG32(PCRP0+(i*4)) & ~(0x03fc0000)) | mode;
					rtl8651_setAsicEthernetPHYReg( i, 0, (phy_mode|RESTART_AUTONEGO) );
					//rtl8651_restartAsicEthernetPHYNway(i);
				}
			}
			else {
				//REG32(PCRP0+(port*4)) |= EnForceMode;
				//rtl8651_setAsicEthernetPHYReg( port, 0, POWER_DOWN );
				//mdelay(1000);
				REG32(PCRP0+(port*4)) = (REG32(PCRP0+(port*4)) & ~(0x03fc0000)) | mode;
				rtl8651_setAsicEthernetPHYReg( port, 0, (phy_mode|RESTART_AUTONEGO) );
				//rtl8651_restartAsicEthernetPHYNway(port);
			}
#endif

			panic_printk("successfully set port %d to %s", port, tokptr);
		}

		else if (!memcmp(cmd_addr, "tx", 2))
		{
			port=(uint32)simple_strtol(tokptr, NULL, 16);	// portmask, port 0 = 1<<0, port 1 = 1<<1, and so on
			if (port < 1 || port > 0x1f)
				goto errout;

			tokptr = strsep(&strptr," ");
			if (tokptr==NULL)
			{
				goto errout;
			}
			mode=simple_strtol(tokptr, NULL, 0); // 0 - random data pattern; 1 - all0 data pattern; 2 - all1 data pattern
			if (mode > 2)
				goto errout;

			// packet output time: valid value is from 1 to 1000
			tokptr = strsep(&strptr," ");
			if (tokptr==NULL)
				goto errout;

			tx_time=simple_strtol(tokptr, NULL, 0); // 1 ~ 1000, unit: second
			if (tx_time < 1 || tx_time > 1000)
				goto errout;

			// packet output interval: valid value is from 1 to 500; optional argument; 50ms by default
			tokptr = strsep(&strptr," ");
			if (tokptr != NULL) {
				interval=simple_strtol(tokptr, NULL, 0); // 1 ~ 500, unit: ms
				if (interval < 1 || interval > 500)
					goto errout;
			}

			{
				struct sk_buff *skb;
				rtl_nicTx_info nicTx;
				//unsigned long wtval;

				#ifdef CONFIG_RTL_WTDOG
				//wtval = *((volatile unsigned long *)WDTCNR);
				//*((volatile unsigned long *)WDTCNR) = (WDSTOP_PATTERN << WDTE_OFFSET);
				#endif

				REG32(CPUIIMR) &= ~RX_DONE_IE_ALL;

				nicTx.txIdx = 0;
				nicTx.vid = RTL_LANVLANID;
				nicTx.portlist = port;
				nicTx.srcExtPort = 0;
				nicTx.flags = (PKTHDR_USED|PKT_OUTGOING);

				tx_cnt = tx_time * 1000 / interval;
				panic_printk("test packet transmitting...");
				for (j=0;j<tx_cnt;j++) // for sending multiple packets
				{
					alloc_rx_buf((void **)&skb, MBUF_LEN);

					memcpy(skb->data, eth_da_sa, sizeof(eth_da_sa));
					*(unsigned short *)(skb->data+12) = PKT_LEN;

					if (mode == 0) {
						for (i=0;i<MBUF_LEN;i+=4)
#if defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)
							*(unsigned int *)(skb->data+14+ i) = prandom_u32(); // skb->data is 2 bytes alignment
#else
							*(unsigned int *)(skb->data+14+ i) = random32(); // skb->data is 2 bytes alignment
#endif
					}
					else if (mode == 1)
						memset(skb->data+14, 0, PKT_LEN);
					else if (mode == 2)
						memset(skb->data+14, 0xff, PKT_LEN);

					//skb->len = sizeof(pkt02);
					skb->len = PKT_LEN+14;
					//memcpy(skb->data, pkt02, skb->len);
#if defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)
					RTL_CACHE_WBACK((unsigned long) skb->data, skb->len);
#else
//					RTL_CACHE_WBACK((unsigned long) skb->data, skb->len);
#endif
					RTL_swNic_send((void *)skb, (void *) skb->data, skb->len, &nicTx);
					mdelay(interval);
					if (j % (1000/interval) == 0)
						panic_printk(".");
				}

					REG32(CPUIIMR) |= RX_DONE_IE_ALL;

				#ifdef CONFIG_RTL_WTDOG
				//*((volatile unsigned long *)WDTCNR) |=  WDTCLR;
				//*((volatile unsigned long *)WDTCNR) = wtval;
				#endif
				panic_printk("\n  ==> %d packet(s) with type %d sent out to portmask 0x%x, packet interval: %d ms.\n", tx_cnt, mode, port, interval);
			}

		}
		else
		{
			goto errout;
		}
	}
	else
	{
errout:
		panic_printk("invalid command.\n");
	}

	return len;
}
#endif


#if defined(CONFIG_TR181_ETH)

#define NUMOFIFENTRY		5
#define NUMOFLINKENTRY		2

typedef struct {
	unsigned long long BytesSent;
	unsigned long long BytesReceived;
	unsigned long long PacketsSent;
	unsigned long long PacketsReceived;
	unsigned int ErrorsSent;
	unsigned int ErrorsReceived;
	unsigned long long UnicastPacketsSent;
	unsigned long long UnicastPacketsReceived;
	unsigned int DiscardPacketsSent;
	unsigned int DiscardPacketsReceived;
	unsigned long long MulticastPacketsSent;
	unsigned long long MulticastPacketsReceived;
	unsigned long long BroadcastPacketsSent;
	unsigned long long BroadcastPacketsReceived;
}	tr181_eth_if_stats;


#ifdef CONFIG_RTL_PROC_NEW
static int tr181_eth_link_read(struct seq_file *s, void *v)
{
	int  i, j;

	for(i = 0; i < NUMOFLINKENTRY; i++)
	{
		if (!(_rtl86xx_dev.dev[i]) || !NETDRV_PRIV(_rtl86xx_dev.dev[i]))
			continue;

		if (((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[i]))->opened)
			seq_printf(s, "eth%d: %s %s ", i, "true", "Up");
		else
			seq_printf(s, "eth%d: %s %s ", i, "false", "Down");

		seq_printf(s, "%s %d ", _rtl86xx_dev.dev[i]->name, get_uptime_by_sec() - if_change_time[i]);

		for(j = 0; j < 5; j++)
			seq_printf(s,"%02x:", _rtl86xx_dev.dev[i]->dev_addr[j]);

		seq_printf(s,"%02x", _rtl86xx_dev.dev[i]->dev_addr[j]);

		seq_printf(s,"\n");
	}
	return 0;
}
#else
static int tr181_eth_link_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len=0, i, j;

	for(i = 0; i < NUMOFLINKENTRY; i++)
	{
		if (!(_rtl86xx_dev.dev[i]) || !NETDRV_PRIV(_rtl86xx_dev.dev[i]))
			continue;

		if (((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[i]))->opened)
			len += sprintf(page+len, "eth%d: %s %s ", i, "true", "Up");
		else
			len += sprintf(page+len, "eth%d: %s %s ", i, "false", "Down");

		len += sprintf(page+len, "%s %d ", _rtl86xx_dev.dev[i]->name, get_uptime_by_sec() - if_change_time[i]);

		for(j = 0; j < 5; j++)
			len += sprintf(page+len,"%02x:", _rtl86xx_dev.dev[i]->dev_addr[j]);

		len += sprintf(page+len,"%02x", _rtl86xx_dev.dev[i]->dev_addr[j]);

		len += sprintf(page+len,"\n");
	}

	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;
	return len;
}
#endif

static int tr181_eth_link_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
	return count;
}

#ifdef CONFIG_RTL_PROC_NEW
static int tr181_eth_if_read(struct seq_file *s, void *v)
{
#if defined(CONFIG_RTL_8367R_SUPPORT)
	int i;
	for(i = 0; i < (NUMOFIFENTRY+3); i++)
	{
		extern int rtk_port_phyStatus_get(uint32 port, uint32 *pLinkStatus, uint32 *pSpeed, uint32 *pDuplex);
		uint32 status=0, speed=PortStatusLinkSpeed10M, duplex=0;

		if (i==5 || i==6)
			continue;

		if (i==7) {
		 	if (port_change_time[6] == 0)
				seq_printf(s, "port%d: %s %s ", i, "false", "Down");
			else
				seq_printf(s, "port%d: %s %s ", i, "true", "Up");

			seq_printf(s, "%d ", get_uptime_by_sec() - port_change_time[i]);
			seq_printf(s, "%s %d %s\n", "false", -1, "Auto");
			continue;
		}
		rtk_port_phyStatus_get(i, &status, &speed, &duplex);

	 	if (status == 0)
			seq_printf(s, "port%d: %s %s ", i, "false", "Down");
		else
			seq_printf(s, "port%d: %s %s ", i, "true", "Up");

		seq_printf(s, "%d ", get_uptime_by_sec() - port_change_time[i]);
		seq_printf(s, "%s ", (i == RTL83XX_WAN) ? "true" : "false");
		seq_printf(s, "%d ", (speed==PortStatusLinkSpeed1000M) ? 1000 : (speed==PortStatusLinkSpeed100M) ? 100 : 10);
		seq_printf(s, "%s\n", (duplex) ? "Full" : "Half");
	}
#else
#endif
	return 0;
}
#else
static int tr181_eth_if_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len=0;

#if defined(CONFIG_RTL_8367R_SUPPORT)
	int i;
	for(i = 0; i < (NUMOFIFENTRY+3); i++)
	{
		extern int rtk_port_phyStatus_get(uint32 port, uint32 *pLinkStatus, uint32 *pSpeed, uint32 *pDuplex);
		uint32 status=0, speed=PortStatusLinkSpeed10M, duplex=0;

		if (i==5 || i==6)
			continue;

		if (i==7) {
		 	if (port_change_time[6] == 0)
				len += sprintf(page+len, "port%d: %s %s ", i, "false", "Down");
			else
				len += sprintf(page+len, "port%d: %s %s ", i, "true", "Up");

			len += sprintf(page+len, "%d ", get_uptime_by_sec() - port_change_time[i]);
			len += sprintf(page+len, "%s %d %s\n", "false", -1, "Auto");
			continue;
		}
		rtk_port_phyStatus_get(i, &status, &speed, &duplex);

	 	if (status == 0)
			len += sprintf(page+len, "port%d: %s %s ", i, "false", "Down");
		else
			len += sprintf(page+len, "port%d: %s %s ", i, "true", "Up");

		len += sprintf(page+len, "%d ", get_uptime_by_sec() - port_change_time[i]);
		len += sprintf(page+len, "%s ", (i == RTL83XX_WAN) ? "true" : "false");
		len += sprintf(page+len, "%d ", (speed==PortStatusLinkSpeed1000M) ? 1000 : (speed==PortStatusLinkSpeed100M) ? 100 : 10);
		len += sprintf(page+len, "%s\n", (duplex) ? "Full" : "Half");
	}
#else
#endif

	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;
	return len;
}
#endif

static int tr181_eth_if_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
	return count;
}

static tr181_eth_if_stats eth0_stats, eth1_stats;

#define ENABLE_8367RB_RGMII2	1

#ifdef CONFIG_RTL_PROC_NEW
static int tr181_eth_stats_read(struct seq_file *s, void *v)
{
#ifdef CONFIG_RTL_8367R_SUPPORT
	int i;
#endif
	uint32 eth0_portmask=0, eth1_portmask=0;

	// refer to net/core/dev.c dev_seq_show() and dev_seq_printf_stats()
	seq_printf(s, "%s%s", "Inter-|   Receive                            ", "                    |  Transmit\n");
	seq_printf(s, "%s%s%s", " face |bytes    packets errs drop fifo frame ",
			      "compressed multicast|bytes    packets errs ", "drop fifo colls carrier compressed");

	seq_printf(s, "%s", "  rx_uc  rx_bc  tx_uc  tx_mc  tx_bc\n");

	if ((_rtl86xx_dev.dev[0]) && NETDRV_PRIV(_rtl86xx_dev.dev[0]))
		eth0_portmask = ((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]))->portmask;

	if ((_rtl86xx_dev.dev[1]) && NETDRV_PRIV(_rtl86xx_dev.dev[1]))
		eth1_portmask = ((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]))->portmask;

	memset(&eth0_stats, 0, sizeof(tr181_eth_if_stats));
	memset(&eth1_stats, 0, sizeof(tr181_eth_if_stats));

#ifdef CONFIG_RTL_8367R_SUPPORT
	#ifdef ENABLE_8367RB_RGMII2
	for(i = 0; i < 8; i++)
	#else
	for(i = 0; i < 5; i++)
	#endif
	{
		#ifdef ENABLE_8367RB_RGMII2
		if (i==5 || i==6)
			continue;
		#endif

		rtk_stat_port_getAll(i, &port_cntrs);

		seq_printf(s, "port%d: %llu %u %u %u  0  0  0  %u  ", i,
			   port_cntrs.ifInOctets, port_cntrs.ifInUcastPkts+port_cntrs.etherStatsMcastPkts+port_cntrs.etherStatsBcastPkts,
			   port_cntrs.dot3StatsFCSErrors + port_cntrs.dot3StatsSymbolErrors, port_cntrs.dot1dTpPortInDiscards,
			   port_cntrs.etherStatsMcastPkts);

		seq_printf(s, "%llu %u %u %u  0  0  0 0  ",
			   port_cntrs.ifOutOctets, port_cntrs.ifOutUcastPkts+port_cntrs.ifOutMulticastPkts+port_cntrs.ifOutBrocastPkts,
			   port_cntrs.etherStatsCollisions, port_cntrs.dot1dBasePortDelayExceededDiscards);

		seq_printf(s, "%u %u %u %u  %u\n",
			   port_cntrs.ifInUcastPkts, port_cntrs.etherStatsBcastPkts, port_cntrs.ifOutUcastPkts,
			   port_cntrs.ifOutMulticastPkts, port_cntrs.ifOutBrocastPkts);

		if (eth0_portmask & (1 << i)) {
			eth0_stats.BytesSent += port_cntrs.ifOutOctets;
			eth0_stats.BytesReceived += port_cntrs.ifInOctets;
			eth0_stats.ErrorsSent += port_cntrs.etherStatsCollisions;
			eth0_stats.ErrorsReceived += port_cntrs.dot3StatsFCSErrors + port_cntrs.dot3StatsSymbolErrors;
			eth0_stats.UnicastPacketsSent += port_cntrs.ifOutUcastPkts;
			eth0_stats.UnicastPacketsReceived += port_cntrs.ifInUcastPkts;
			eth0_stats.DiscardPacketsSent += port_cntrs.dot1dBasePortDelayExceededDiscards;
			eth0_stats.DiscardPacketsReceived += port_cntrs.dot1dTpPortInDiscards;
			eth0_stats.MulticastPacketsSent += port_cntrs.ifOutMulticastPkts;
			eth0_stats.MulticastPacketsReceived += port_cntrs.etherStatsMcastPkts;
			eth0_stats.BroadcastPacketsSent += port_cntrs.ifOutBrocastPkts;
			eth0_stats.BroadcastPacketsReceived += port_cntrs.etherStatsBcastPkts;
			eth0_stats.PacketsSent += port_cntrs.ifOutUcastPkts + port_cntrs.ifOutMulticastPkts + port_cntrs.ifOutBrocastPkts;
			eth0_stats.PacketsReceived += port_cntrs.ifInUcastPkts + port_cntrs.etherStatsMcastPkts+ port_cntrs.etherStatsBcastPkts;
		}
		if (eth1_portmask & (1 << i)) {
			eth1_stats.BytesSent += port_cntrs.ifOutOctets;
			eth1_stats.BytesReceived += port_cntrs.ifInOctets;
			eth1_stats.ErrorsSent += port_cntrs.etherStatsCollisions;
			eth1_stats.ErrorsReceived += port_cntrs.dot3StatsFCSErrors + port_cntrs.dot3StatsSymbolErrors;
			eth1_stats.UnicastPacketsSent += port_cntrs.ifOutUcastPkts;
			eth1_stats.UnicastPacketsReceived += port_cntrs.ifInUcastPkts;
			eth1_stats.DiscardPacketsSent += port_cntrs.dot1dBasePortDelayExceededDiscards;
			eth1_stats.DiscardPacketsReceived += port_cntrs.dot1dTpPortInDiscards;
			eth1_stats.MulticastPacketsSent += port_cntrs.ifOutMulticastPkts;
			eth1_stats.MulticastPacketsReceived += port_cntrs.etherStatsMcastPkts;
			eth1_stats.BroadcastPacketsSent += port_cntrs.ifOutBrocastPkts;
			eth1_stats.BroadcastPacketsReceived += port_cntrs.etherStatsBcastPkts;
			eth1_stats.PacketsSent += port_cntrs.ifOutUcastPkts + port_cntrs.ifOutMulticastPkts + port_cntrs.ifOutBrocastPkts;
			eth1_stats.PacketsReceived += port_cntrs.ifInUcastPkts + port_cntrs.etherStatsMcastPkts+ port_cntrs.etherStatsBcastPkts;
		}
	}

	seq_printf(s, "eth0: %llu %llu %u %u  0  0  0  %llu  ",
			   eth0_stats.BytesReceived, eth0_stats.PacketsReceived,
			   eth0_stats.ErrorsReceived, eth0_stats.DiscardPacketsReceived,
			   eth0_stats.MulticastPacketsReceived);

	seq_printf(s, "%llu %llu %u %u  0  0  0 0  ",
			   eth0_stats.BytesSent, eth0_stats.PacketsSent,
			   eth0_stats.ErrorsSent, eth0_stats.DiscardPacketsSent);

	seq_printf(s, "%llu %llu %llu %llu  %llu\n",
			   eth0_stats.UnicastPacketsReceived, eth0_stats.BroadcastPacketsReceived, eth0_stats.UnicastPacketsSent ,
			   eth0_stats.MulticastPacketsSent, eth0_stats.BroadcastPacketsSent);

	seq_printf(s, "eth1: %llu %llu %u %u  0  0  0  %llu  ",
			   eth1_stats.BytesReceived, eth1_stats.PacketsReceived,
			   eth1_stats.ErrorsReceived, eth1_stats.DiscardPacketsReceived,
			   eth1_stats.MulticastPacketsReceived);

	seq_printf(s, "%llu %llu %u %u  0  0  0 0  ",
			   eth1_stats.BytesSent, eth1_stats.PacketsSent,
			   eth1_stats.ErrorsSent, eth1_stats.DiscardPacketsSent);

	seq_printf(s, "%llu %llu %llu %llu  %llu\n",
			   eth1_stats.UnicastPacketsReceived, eth1_stats.BroadcastPacketsReceived, eth1_stats.UnicastPacketsSent ,
			   eth1_stats.MulticastPacketsSent, eth1_stats.BroadcastPacketsSent);

#else
/*
	for(i = 0; i < 5; i++)
	{
		uint32 addrOffset_fromP0 = i * MIB_ADDROFFSETBYPORT;

		if (portmask & (1 << i)) {
			pStats->BytesSent += rtl865xC_returnAsicCounter64( OFFSET_IFOUTOCTETS_P0 + addrOffset_fromP0 );
			pStats->BytesReceived += rtl865xC_returnAsicCounter64( OFFSET_IFINOCTETS_P0 + addrOffset_fromP0 );
			pStats->ErrorsSent += rtl8651_returnAsicCounter( OFFSET_ETHERSTATSCOLLISIONS_P0 + addrOffset_fromP0 );
			pStats->ErrorsReceived += rtl8651_returnAsicCounter( OFFSET_DOT3STATSFCSERRORS_P0 + addrOffset_fromP0 );
			pStats->UnicastPacketsSent += rtl8651_returnAsicCounter( OFFSET_IFOUTUCASTPKTS_P0 + addrOffset_fromP0 );
			pStats->UnicastPacketsReceived += rtl8651_returnAsicCounter( OFFSET_IFINUCASTPKTS_P0 + addrOffset_fromP0 );
			pStats->DiscardPacketsSent += rtl8651_returnAsicCounter( OFFSET_IFOUTDISCARDS + addrOffset_fromP0 );
			pStats->DiscardPacketsReceived += rtl8651_returnAsicCounter( OFFSET_DOT1DTPPORTINDISCARDS_P0 + addrOffset_fromP0 );
			pStats->MulticastPacketsSent += rtl8651_returnAsicCounter( OFFSET_IFOUTMULTICASTPKTS_P0 + addrOffset_fromP0 );
			pStats->MulticastPacketsReceived += rtl8651_returnAsicCounter( OFFSET_ETHERSTATSMULTICASTPKTS_P0 + addrOffset_fromP0 );
			pStats->BroadcastPacketsSent += rtl8651_returnAsicCounter( OFFSET_IFOUTBROADCASTPKTS_P0 + addrOffset_fromP0 );
			pStats->BroadcastPacketsReceived += rtl8651_returnAsicCounter( OFFSET_ETHERSTATSBROADCASTPKTS_P0 + addrOffset_fromP0 );
			pStats->PacketsSent += pStats->UnicastPacketsSent + pStats->MulticastPacketsSent + pStats->BroadcastPacketsSent;
			pStats->PacketsReceived += pStats->UnicastPacketsReceived + pStats->MulticastPacketsReceived + pStats->BroadcastPacketsReceived;
		}
	}
*/
#endif
	return 0;
}
#else
static int tr181_eth_stats_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len=0;
#ifdef CONFIG_RTL_8367R_SUPPORT
	int i;
#endif
	uint32 eth0_portmask=0, eth1_portmask=0;

	// refer to net/core/dev.c dev_seq_show() and dev_seq_printf_stats()
	len += sprintf(page+len, "%s%s", "Inter-|   Receive                            ", "                    |  Transmit\n");
	len += sprintf(page+len, "%s%s%s", " face |bytes    packets errs drop fifo frame ",
			      "compressed multicast|bytes    packets errs ", "drop fifo colls carrier compressed");

	len += sprintf(page+len, "%s", "  rx_uc  rx_bc  tx_uc  tx_mc  tx_bc\n");

	if ((_rtl86xx_dev.dev[0]) && NETDRV_PRIV(_rtl86xx_dev.dev[0]))
		eth0_portmask = ((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]))->portmask;

	if ((_rtl86xx_dev.dev[1]) && NETDRV_PRIV(_rtl86xx_dev.dev[1]))
		eth1_portmask = ((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]))->portmask;

	memset(&eth0_stats, 0, sizeof(tr181_eth_if_stats));
	memset(&eth1_stats, 0, sizeof(tr181_eth_if_stats));

#ifdef CONFIG_RTL_8367R_SUPPORT
	#ifdef ENABLE_8367RB_RGMII2
	for(i = 0; i < 8; i++)
	#else
	for(i = 0; i < 5; i++)
	#endif
	{
		#ifdef ENABLE_8367RB_RGMII2
		if (i==5 || i==6)
			continue;
		#endif

		rtk_stat_port_getAll(i, &port_cntrs);

		len += sprintf(page+len, "port%d: %llu %u %u %u  0  0  0  %u  ", i,
			   port_cntrs.ifInOctets, port_cntrs.ifInUcastPkts+port_cntrs.etherStatsMcastPkts+port_cntrs.etherStatsBcastPkts,
			   port_cntrs.dot3StatsFCSErrors + port_cntrs.dot3StatsSymbolErrors, port_cntrs.dot1dTpPortInDiscards,
			   port_cntrs.etherStatsMcastPkts);

		len += sprintf(page+len, "%llu %u %u %u  0  0  0 0  ",
			   port_cntrs.ifOutOctets, port_cntrs.ifOutUcastPkts+port_cntrs.ifOutMulticastPkts+port_cntrs.ifOutBrocastPkts,
			   port_cntrs.etherStatsCollisions, port_cntrs.dot1dBasePortDelayExceededDiscards);

		len += sprintf(page+len, "%u %u %u %u  %u\n",
			   port_cntrs.ifInUcastPkts, port_cntrs.etherStatsBcastPkts, port_cntrs.ifOutUcastPkts,
			   port_cntrs.ifOutMulticastPkts, port_cntrs.ifOutBrocastPkts);

		if (eth0_portmask & (1 << i)) {
			eth0_stats.BytesSent += port_cntrs.ifOutOctets;
			eth0_stats.BytesReceived += port_cntrs.ifInOctets;
			eth0_stats.ErrorsSent += port_cntrs.etherStatsCollisions;
			eth0_stats.ErrorsReceived += port_cntrs.dot3StatsFCSErrors + port_cntrs.dot3StatsSymbolErrors;
			eth0_stats.UnicastPacketsSent += port_cntrs.ifOutUcastPkts;
			eth0_stats.UnicastPacketsReceived += port_cntrs.ifInUcastPkts;
			eth0_stats.DiscardPacketsSent += port_cntrs.dot1dBasePortDelayExceededDiscards;
			eth0_stats.DiscardPacketsReceived += port_cntrs.dot1dTpPortInDiscards;
			eth0_stats.MulticastPacketsSent += port_cntrs.ifOutMulticastPkts;
			eth0_stats.MulticastPacketsReceived += port_cntrs.etherStatsMcastPkts;
			eth0_stats.BroadcastPacketsSent += port_cntrs.ifOutBrocastPkts;
			eth0_stats.BroadcastPacketsReceived += port_cntrs.etherStatsBcastPkts;
			eth0_stats.PacketsSent += port_cntrs.ifOutUcastPkts + port_cntrs.ifOutMulticastPkts + port_cntrs.ifOutBrocastPkts;
			eth0_stats.PacketsReceived += port_cntrs.ifInUcastPkts + port_cntrs.etherStatsMcastPkts+ port_cntrs.etherStatsBcastPkts;
		}
		if (eth1_portmask & (1 << i)) {
			eth1_stats.BytesSent += port_cntrs.ifOutOctets;
			eth1_stats.BytesReceived += port_cntrs.ifInOctets;
			eth1_stats.ErrorsSent += port_cntrs.etherStatsCollisions;
			eth1_stats.ErrorsReceived += port_cntrs.dot3StatsFCSErrors + port_cntrs.dot3StatsSymbolErrors;
			eth1_stats.UnicastPacketsSent += port_cntrs.ifOutUcastPkts;
			eth1_stats.UnicastPacketsReceived += port_cntrs.ifInUcastPkts;
			eth1_stats.DiscardPacketsSent += port_cntrs.dot1dBasePortDelayExceededDiscards;
			eth1_stats.DiscardPacketsReceived += port_cntrs.dot1dTpPortInDiscards;
			eth1_stats.MulticastPacketsSent += port_cntrs.ifOutMulticastPkts;
			eth1_stats.MulticastPacketsReceived += port_cntrs.etherStatsMcastPkts;
			eth1_stats.BroadcastPacketsSent += port_cntrs.ifOutBrocastPkts;
			eth1_stats.BroadcastPacketsReceived += port_cntrs.etherStatsBcastPkts;
			eth1_stats.PacketsSent += port_cntrs.ifOutUcastPkts + port_cntrs.ifOutMulticastPkts + port_cntrs.ifOutBrocastPkts;
			eth1_stats.PacketsReceived += port_cntrs.ifInUcastPkts + port_cntrs.etherStatsMcastPkts+ port_cntrs.etherStatsBcastPkts;
		}
	}

	len += sprintf(page+len, "eth0: %llu %llu %u %u  0  0  0  %llu  ",
			   eth0_stats.BytesReceived, eth0_stats.PacketsReceived,
			   eth0_stats.ErrorsReceived, eth0_stats.DiscardPacketsReceived,
			   eth0_stats.MulticastPacketsReceived);

	len += sprintf(page+len, "%llu %llu %u %u  0  0  0 0  ",
			   eth0_stats.BytesSent, eth0_stats.PacketsSent,
			   eth0_stats.ErrorsSent, eth0_stats.DiscardPacketsSent);

	len += sprintf(page+len, "%llu %llu %llu %llu  %llu\n",
			   eth0_stats.UnicastPacketsReceived, eth0_stats.BroadcastPacketsReceived, eth0_stats.UnicastPacketsSent ,
			   eth0_stats.MulticastPacketsSent, eth0_stats.BroadcastPacketsSent);

	len += sprintf(page+len, "eth1: %llu %llu %u %u  0  0  0  %llu  ",
			   eth1_stats.BytesReceived, eth1_stats.PacketsReceived,
			   eth1_stats.ErrorsReceived, eth1_stats.DiscardPacketsReceived,
			   eth1_stats.MulticastPacketsReceived);

	len += sprintf(page+len, "%llu %llu %u %u  0  0  0 0  ",
			   eth1_stats.BytesSent, eth1_stats.PacketsSent,
			   eth1_stats.ErrorsSent, eth1_stats.DiscardPacketsSent);

	len += sprintf(page+len, "%llu %llu %llu %llu  %llu\n",
			   eth1_stats.UnicastPacketsReceived, eth1_stats.BroadcastPacketsReceived, eth1_stats.UnicastPacketsSent ,
			   eth1_stats.MulticastPacketsSent, eth1_stats.BroadcastPacketsSent);

#else
/*
	for(i = 0; i < 5; i++)
	{
		uint32 addrOffset_fromP0 = i * MIB_ADDROFFSETBYPORT;

		if (portmask & (1 << i)) {
			pStats->BytesSent += rtl865xC_returnAsicCounter64( OFFSET_IFOUTOCTETS_P0 + addrOffset_fromP0 );
			pStats->BytesReceived += rtl865xC_returnAsicCounter64( OFFSET_IFINOCTETS_P0 + addrOffset_fromP0 );
			pStats->ErrorsSent += rtl8651_returnAsicCounter( OFFSET_ETHERSTATSCOLLISIONS_P0 + addrOffset_fromP0 );
			pStats->ErrorsReceived += rtl8651_returnAsicCounter( OFFSET_DOT3STATSFCSERRORS_P0 + addrOffset_fromP0 );
			pStats->UnicastPacketsSent += rtl8651_returnAsicCounter( OFFSET_IFOUTUCASTPKTS_P0 + addrOffset_fromP0 );
			pStats->UnicastPacketsReceived += rtl8651_returnAsicCounter( OFFSET_IFINUCASTPKTS_P0 + addrOffset_fromP0 );
			pStats->DiscardPacketsSent += rtl8651_returnAsicCounter( OFFSET_IFOUTDISCARDS + addrOffset_fromP0 );
			pStats->DiscardPacketsReceived += rtl8651_returnAsicCounter( OFFSET_DOT1DTPPORTINDISCARDS_P0 + addrOffset_fromP0 );
			pStats->MulticastPacketsSent += rtl8651_returnAsicCounter( OFFSET_IFOUTMULTICASTPKTS_P0 + addrOffset_fromP0 );
			pStats->MulticastPacketsReceived += rtl8651_returnAsicCounter( OFFSET_ETHERSTATSMULTICASTPKTS_P0 + addrOffset_fromP0 );
			pStats->BroadcastPacketsSent += rtl8651_returnAsicCounter( OFFSET_IFOUTBROADCASTPKTS_P0 + addrOffset_fromP0 );
			pStats->BroadcastPacketsReceived += rtl8651_returnAsicCounter( OFFSET_ETHERSTATSBROADCASTPKTS_P0 + addrOffset_fromP0 );
			pStats->PacketsSent += pStats->UnicastPacketsSent + pStats->MulticastPacketsSent + pStats->BroadcastPacketsSent;
			pStats->PacketsReceived += pStats->UnicastPacketsReceived + pStats->MulticastPacketsReceived + pStats->BroadcastPacketsReceived;
		}
	}
*/
#endif


	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;
	return len;
}
#endif

static int tr181_eth_stats_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
	return count;
}

#undef TRUE
#define TRUE 1

#undef FALSE
#define FALSE 0

typedef struct {
	unsigned int InterfaceNumberOfEntries;
	unsigned int LinkNumberOfEntries;
}	tr181_ethernet;

typedef struct {
	int Enable;
	unsigned char Status[16];
	unsigned int LastChange;
	int Upstream;
	int MaxBitRate;
	unsigned char DuplexMode[6];
}	tr181_eth_interface;

#define TR181_ETH_IF_DUPLEX_HALF	0
#define TR181_ETH_IF_DUPLEX_FULL	1
#define TR181_ETH_IF_DUPLEX_AUTO	2


typedef struct {
	int Enable;
	unsigned char Status[16];
	unsigned char Name[16];
	unsigned char MACAddress[16];
	unsigned int LastChange;
}	tr181_eth_link;

//typedef tr181_eth_if_stats tr181_eth_link_stats;


//========================================================
// ========== get function
//========================================================

int tr181_get_eth(tr181_ethernet *pEth)
{
	pEth->InterfaceNumberOfEntries = NUMOFIFENTRY;
	pEth->LinkNumberOfEntries = NUMOFLINKENTRY;

	return 0;
}

int tr181_get_eth_if(int if_link_num, tr181_eth_interface *pIf)
{
	if (if_link_num <0 || if_link_num >= NUMOFIFENTRY)
		return (-1);

 #if defined(CONFIG_RTL_8367R_SUPPORT)
	{
	extern int rtk_port_phyStatus_get(uint32 port, uint32 *pLinkStatus, uint32 *pSpeed, uint32 *pDuplex);
	uint32 status, speed, duplex;

	if ((rtk_port_phyStatus_get(if_link_num, &status, &speed, &duplex)) == 0) {

			if (status == 0) {
				pIf->Enable = FALSE;
				strcpy(pIf->Status, "Down");
			}
			else {
				pIf->Enable = TRUE;
				strcpy(pIf->Status, "Up");

				if (duplex)
					strcpy(pIf->DuplexMode, "Full");
				else
					strcpy(pIf->DuplexMode, "Half");

				if (speed==PortStatusLinkSpeed100M)
					pIf->MaxBitRate = 100;
				else if (speed==PortStatusLinkSpeed1000M)
					pIf->MaxBitRate = 1000;
				else
					pIf->MaxBitRate = 10;
			}
	}
	if (if_link_num == RTL83XX_WAN)
		pIf->Upstream = TRUE;
	else
		pIf->Upstream = FALSE;

	pIf->LastChange = get_uptime_by_sec() - port_change_time[if_link_num];
	}
#else
#endif

	return 0;
}

#ifdef CONFIG_RTL_8367R_SUPPORT
extern rtk_stat_port_cntr_t  port_cntrs;
#endif

int tr181_get_eth_if_stats(int if_link_num, tr181_eth_if_stats *pStats)
{
	if (if_link_num <0 || if_link_num >= NUMOFIFENTRY)
		return (-1);

#ifdef CONFIG_RTL_8367R_SUPPORT

	rtk_stat_port_getAll(if_link_num, &port_cntrs);

	pStats->BytesSent = port_cntrs.ifOutOctets;
	pStats->BytesReceived = port_cntrs.ifInOctets;
	pStats->ErrorsSent = port_cntrs.etherStatsCollisions;
	pStats->ErrorsReceived = port_cntrs.dot3StatsFCSErrors + port_cntrs.dot3StatsSymbolErrors;
	pStats->UnicastPacketsSent = port_cntrs.ifOutUcastPkts;
	pStats->UnicastPacketsReceived = port_cntrs.ifInUcastPkts;
	pStats->DiscardPacketsSent = port_cntrs.dot1dBasePortDelayExceededDiscards;
	pStats->DiscardPacketsReceived = port_cntrs.dot1dTpPortInDiscards;
	pStats->MulticastPacketsSent = port_cntrs.ifOutMulticastPkts;
	pStats->MulticastPacketsReceived = port_cntrs.etherStatsMcastPkts;
	pStats->BroadcastPacketsSent = port_cntrs.ifOutBrocastPkts;
	pStats->BroadcastPacketsReceived = port_cntrs.etherStatsBcastPkts;
	pStats->PacketsSent = pStats->UnicastPacketsSent + pStats->MulticastPacketsSent + pStats->BroadcastPacketsSent;
	pStats->PacketsReceived = pStats->UnicastPacketsReceived + pStats->MulticastPacketsReceived + pStats->BroadcastPacketsReceived;

#else
	{
	uint32 addrOffset_fromP0 = if_link_num * MIB_ADDROFFSETBYPORT;

	pStats->BytesSent = rtl865xC_returnAsicCounter64( OFFSET_IFOUTOCTETS_P0 + addrOffset_fromP0 );
	pStats->BytesReceived = rtl865xC_returnAsicCounter64( OFFSET_IFINOCTETS_P0 + addrOffset_fromP0 );
	pStats->ErrorsSent = rtl8651_returnAsicCounter( OFFSET_ETHERSTATSCOLLISIONS_P0 + addrOffset_fromP0 );
	pStats->ErrorsReceived = rtl8651_returnAsicCounter( OFFSET_DOT3STATSFCSERRORS_P0 + addrOffset_fromP0 );
	pStats->UnicastPacketsSent = rtl8651_returnAsicCounter( OFFSET_IFOUTUCASTPKTS_P0 + addrOffset_fromP0 );
	pStats->UnicastPacketsReceived = rtl8651_returnAsicCounter( OFFSET_IFINUCASTPKTS_P0 + addrOffset_fromP0 );
	pStats->DiscardPacketsSent = rtl8651_returnAsicCounter( OFFSET_IFOUTDISCARDS + addrOffset_fromP0 );
	pStats->DiscardPacketsReceived = rtl8651_returnAsicCounter( OFFSET_DOT1DTPPORTINDISCARDS_P0 + addrOffset_fromP0 );
	pStats->MulticastPacketsSent = rtl8651_returnAsicCounter( OFFSET_IFOUTMULTICASTPKTS_P0 + addrOffset_fromP0 );
	pStats->MulticastPacketsReceived = rtl8651_returnAsicCounter( OFFSET_ETHERSTATSMULTICASTPKTS_P0 + addrOffset_fromP0 );
	pStats->BroadcastPacketsSent = rtl8651_returnAsicCounter( OFFSET_IFOUTBROADCASTPKTS_P0 + addrOffset_fromP0 );
	pStats->BroadcastPacketsReceived = rtl8651_returnAsicCounter( OFFSET_ETHERSTATSBROADCASTPKTS_P0 + addrOffset_fromP0 );
	pStats->PacketsSent = pStats->UnicastPacketsSent + pStats->MulticastPacketsSent + pStats->BroadcastPacketsSent;
	pStats->PacketsReceived = pStats->UnicastPacketsReceived + pStats->MulticastPacketsReceived + pStats->BroadcastPacketsReceived;
	}
#endif

	return 0;
}

int tr181_get_eth_link(int if_link_num, tr181_eth_link *pLink)
{
	int i;

	if (if_link_num <0 || if_link_num >= NUMOFLINKENTRY)
		return (-1);

	if (!(_rtl86xx_dev.dev[if_link_num]) || !NETDRV_PRIV(_rtl86xx_dev.dev[if_link_num]))
		return (-1);

	pLink->Enable = ((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[if_link_num]))->opened;
	if (pLink->Enable == TRUE)
		strcpy(pLink->Status, "Up");
	else
		strcpy(pLink->Status, "Down");

	strcpy(pLink->Name, _rtl86xx_dev.dev[if_link_num]->name);

	for(i = 0; i < 6; i++)
		sprintf(pLink->MACAddress + (i*2),"%02x", _rtl86xx_dev.dev[if_link_num]->dev_addr[i]);
	pLink->LastChange = get_uptime_by_sec() - if_change_time[if_link_num];

	return 0;
}

int tr181_get_eth_link_stats(int if_link_num, tr181_eth_if_stats *pStats)
{
	uint32 portmask;
	int i;

	if (if_link_num <0 || if_link_num >= NUMOFLINKENTRY)
		return (-1);

	if (!(_rtl86xx_dev.dev[if_link_num]) || !NETDRV_PRIV(_rtl86xx_dev.dev[if_link_num]))
		return (-1);

	portmask = ((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[if_link_num]))->portmask;
	memset(pStats, 0, sizeof(tr181_eth_if_stats));

#ifdef CONFIG_RTL_8367R_SUPPORT
	for(i = 0; i < 5; i++)
	{
		if (portmask & (1 << i)) {
			rtk_stat_port_getAll(i, &port_cntrs);

			pStats->BytesSent += port_cntrs.ifOutOctets;
			pStats->BytesReceived += port_cntrs.ifInOctets;
			pStats->ErrorsSent += port_cntrs.etherStatsCollisions;
			pStats->ErrorsReceived += port_cntrs.dot3StatsFCSErrors + port_cntrs.dot3StatsSymbolErrors;
			pStats->UnicastPacketsSent += port_cntrs.ifOutUcastPkts;
			pStats->UnicastPacketsReceived += port_cntrs.ifInUcastPkts;
			pStats->DiscardPacketsSent += port_cntrs.dot1dBasePortDelayExceededDiscards;
			pStats->DiscardPacketsReceived += port_cntrs.dot1dTpPortInDiscards;
			pStats->MulticastPacketsSent += port_cntrs.ifOutMulticastPkts;
			pStats->MulticastPacketsReceived += port_cntrs.etherStatsMcastPkts;
			pStats->BroadcastPacketsSent += port_cntrs.ifOutBrocastPkts;
			pStats->BroadcastPacketsReceived += port_cntrs.etherStatsBcastPkts;
			pStats->PacketsSent += pStats->UnicastPacketsSent + pStats->MulticastPacketsSent + pStats->BroadcastPacketsSent;
			pStats->PacketsReceived += pStats->UnicastPacketsReceived + pStats->MulticastPacketsReceived + pStats->BroadcastPacketsReceived;
		}
	}
#else
	for(i = 0; i < 5; i++)
	{
		uint32 addrOffset_fromP0 = i * MIB_ADDROFFSETBYPORT;

		if (portmask & (1 << i)) {
			pStats->BytesSent += rtl865xC_returnAsicCounter64( OFFSET_IFOUTOCTETS_P0 + addrOffset_fromP0 );
			pStats->BytesReceived += rtl865xC_returnAsicCounter64( OFFSET_IFINOCTETS_P0 + addrOffset_fromP0 );
			pStats->ErrorsSent += rtl8651_returnAsicCounter( OFFSET_ETHERSTATSCOLLISIONS_P0 + addrOffset_fromP0 );
			pStats->ErrorsReceived += rtl8651_returnAsicCounter( OFFSET_DOT3STATSFCSERRORS_P0 + addrOffset_fromP0 );
			pStats->UnicastPacketsSent += rtl8651_returnAsicCounter( OFFSET_IFOUTUCASTPKTS_P0 + addrOffset_fromP0 );
			pStats->UnicastPacketsReceived += rtl8651_returnAsicCounter( OFFSET_IFINUCASTPKTS_P0 + addrOffset_fromP0 );
			pStats->DiscardPacketsSent += rtl8651_returnAsicCounter( OFFSET_IFOUTDISCARDS + addrOffset_fromP0 );
			pStats->DiscardPacketsReceived += rtl8651_returnAsicCounter( OFFSET_DOT1DTPPORTINDISCARDS_P0 + addrOffset_fromP0 );
			pStats->MulticastPacketsSent += rtl8651_returnAsicCounter( OFFSET_IFOUTMULTICASTPKTS_P0 + addrOffset_fromP0 );
			pStats->MulticastPacketsReceived += rtl8651_returnAsicCounter( OFFSET_ETHERSTATSMULTICASTPKTS_P0 + addrOffset_fromP0 );
			pStats->BroadcastPacketsSent += rtl8651_returnAsicCounter( OFFSET_IFOUTBROADCASTPKTS_P0 + addrOffset_fromP0 );
			pStats->BroadcastPacketsReceived += rtl8651_returnAsicCounter( OFFSET_ETHERSTATSBROADCASTPKTS_P0 + addrOffset_fromP0 );
			pStats->PacketsSent += pStats->UnicastPacketsSent + pStats->MulticastPacketsSent + pStats->BroadcastPacketsSent;
			pStats->PacketsReceived += pStats->UnicastPacketsReceived + pStats->MulticastPacketsReceived + pStats->BroadcastPacketsReceived;
		}
	}
#endif

	return 0;
}

//========================================================
// ========== set function
//========================================================

int tr181_set_eth_if_enable(int if_link_num, int enable)
{
	extern int rtk_rgmii_set(int enable);

	if (if_link_num <0 || if_link_num >= (NUMOFIFENTRY+3))
		return (-1);

#ifdef CONFIG_RTL_8367R_SUPPORT
	if (enable == FALSE) {
		extern int rtk_port_adminEnable_set(uint32 port, uint32 enable);
		if (if_link_num == 7) {
			rtk_rgmii_set(0);
			port_change_time[6] = 0;
			port_change_time[7] = get_uptime_by_sec();
		}
		else
			rtk_port_adminEnable_set(if_link_num, 0);
	}
	else if (enable == TRUE) {
		extern int rtk_port_adminEnable_set(uint32 port, uint32 enable);
		if (if_link_num == 7) {
			rtk_rgmii_set(1);
			port_change_time[6] = 1;
			port_change_time[7] = get_uptime_by_sec();
		}
		else
			rtk_port_adminEnable_set(if_link_num, 1);
	}
#else
#endif

	return 0;
}

typedef struct rtk_port_phy_ability_s
{
	uint32    AutoNegotiation;  /*PHY register 0.12 setting for auto-negotiation process*/
	uint32    Half_10;          /*PHY register 4.5 setting for 10BASE-TX half duplex capable*/
	uint32    Full_10;          /*PHY register 4.6 setting for 10BASE-TX full duplex capable*/
	uint32    Half_100;         /*PHY register 4.7 setting for 100BASE-TX half duplex capable*/
	uint32    Full_100;         /*PHY register 4.8 setting for 100BASE-TX full duplex capable*/
	uint32    Full_1000;        /*PHY register 9.9 setting for 1000BASE-T full duplex capable*/
	uint32    FC;               /*PHY register 4.10 setting for flow control capability*/
	uint32    AsyFC;            /*PHY register 4.11 setting for  asymmetric flow control capability*/
} rtk_port_phy_ability_t;

int tr181_set_eth_lf_MaxBitRate(int if_link_num, int rate)
{
	if (if_link_num <0 || if_link_num >= NUMOFIFENTRY)
		return (-1);

#ifdef CONFIG_RTL_8367R_SUPPORT
	{
		extern int rtk_port_phyAutoNegoAbility_get(uint32 port, rtk_port_phy_ability_t *pAbility);
		extern int rtk_port_phyAutoNegoAbility_set(uint32 port, rtk_port_phy_ability_t *pAbility);
		rtk_port_phy_ability_t Ability;

		rtk_port_phyAutoNegoAbility_get(if_link_num, &Ability);
		if (rate == 10) {
			Ability.Half_10 = 1;
			Ability.Full_10 = 1;
			Ability.Half_100 = 0;
			Ability.Full_100 = 0;
			Ability.Full_1000 = 0;
		}
		else if (rate == 100) {
			Ability.Half_10 = 1;
			Ability.Full_10 = 1;
			Ability.Half_100 = 1;
			Ability.Full_100 = 1;
			Ability.Full_1000 = 0;
		}
		else if (rate == 1000) {
			Ability.Half_10 = 1;
			Ability.Full_10 = 1;
			Ability.Half_100 = 1;
			Ability.Full_100 = 1;
			Ability.Full_1000 = 1;
		}
		else if (rate == -1) {
			Ability.Half_10 = 1;
			Ability.Full_10 = 1;
			Ability.Half_100 = 1;
			Ability.Full_100 = 1;
			Ability.Full_1000 = 1;
		}
		rtk_port_phyAutoNegoAbility_set(if_link_num, &Ability);
	}
#else
#endif

	return 0;
}

int tr181_set_eth_if_DuplexMode(int if_link_num, int mode)
{
	if (if_link_num <0 || if_link_num >= NUMOFIFENTRY)
		return (-1);

#ifdef CONFIG_RTL_8367R_SUPPORT
	{
		extern int rtk_port_phyAutoNegoAbility_get(uint32 port, rtk_port_phy_ability_t *pAbility);
		extern int rtk_port_phyAutoNegoAbility_set(uint32 port, rtk_port_phy_ability_t *pAbility);
		rtk_port_phy_ability_t Ability;

		rtk_port_phyAutoNegoAbility_get(if_link_num, &Ability);
		if (mode == TR181_ETH_IF_DUPLEX_HALF) {
			Ability.Half_10 = 1;
			Ability.Full_10 = 0;
			Ability.Half_100 = 1;
			Ability.Full_100 = 0;
			Ability.Full_1000 = 0;
		}
		else if (mode == TR181_ETH_IF_DUPLEX_FULL) {
			Ability.Half_10 = 0;
			Ability.Full_10 = 1;
			Ability.Half_100 = 0;
			Ability.Full_100 = 1;
			Ability.Full_1000 = 1;
		}
		else if (mode == TR181_ETH_IF_DUPLEX_AUTO) {
			Ability.Half_10 = 1;
			Ability.Full_10 = 1;
			Ability.Half_100 = 1;
			Ability.Full_100 = 1;
			Ability.Full_1000 = 1;
		}
		rtk_port_phyAutoNegoAbility_set(if_link_num, &Ability);
	}
#else
#endif

	return 0;
}

int tr181_set_eth_link_enable(int if_link_num, int enable)
{
	rtlglue_printf("  ==> %s: %d %d\n", __FUNCTION__, if_link_num, enable);
	return 0;
}

#ifdef CONFIG_RTL_PROC_NEW
static int tr181_eth_set_read(struct seq_file *s, void *v)
{
	return 0;
}
#else
static int tr181_eth_set_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	return 0;
}
#endif

static int tr181_eth_set_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
	char tmpBuf[100];
	char	*strptr;
	char	*tokptr;
	int cmd, if_link, if_link_num, rate;

	tr181_ethernet Eth;
	tr181_eth_if_stats Stats;
	tr181_eth_interface If;
	tr181_eth_link Link;

	if (buffer && !copy_from_user(tmpBuf, buffer, count))
	{
		tmpBuf[count]='\0';

		strptr=tmpBuf;
		tokptr = strsep(&strptr," ");
		if (tokptr==NULL)	goto errout;

		if (!memcmp(tokptr, "get", 3)) cmd=1;
		else if (!memcmp(tokptr, "set", 3)) cmd=2;
		else goto errout;

		tokptr = strsep(&strptr," ");
		if (tokptr==NULL)	goto errout;

		if (memcmp(tokptr, "eth", 3))
			goto errout;

		tokptr = strsep(&strptr," ");
		if (tokptr==NULL) {
			if (cmd == 1) {
				tr181_get_eth(&Eth);
				return count;
			}
			else goto errout;
		}


		if (!memcmp(tokptr, "if", 2)) {
			if_link = 1;
			if_link_num = tokptr[2] - 0x30;
		}
		else if (!memcmp(tokptr, "link", 4)) {
			if_link = 2;
			if_link_num = tokptr[4] - 0x30;
		}
		else goto errout;


		tokptr = strsep(&strptr," ");
		if (tokptr==NULL) {
			if (cmd == 1 && if_link == 1) {
				memset(&If, 0, sizeof(tr181_eth_interface));
				tr181_get_eth_if(if_link_num, &If);
				return count;
			}
			else if (cmd == 1 && if_link == 2) {
				memset(&Link, 0, sizeof(tr181_eth_link));
				tr181_get_eth_link(if_link_num, &Link);
				return count;
			}
			else goto errout;
		}


		if (!memcmp(tokptr, "stats", 5)) {
			if (cmd == 1 && if_link == 1) {
				tr181_get_eth_if_stats(if_link_num, &Stats);
			}
			else if (cmd == 1 && if_link == 2) {
				tr181_get_eth_link_stats(if_link_num, &Stats);
			}
			else goto errout;
		}
		else if (!memcmp(tokptr, "Enable", 6)) {
			if (cmd == 1) goto errout;
			tokptr = strsep(&strptr," ");
			if (tokptr==NULL) goto errout;

			if (!memcmp(tokptr, "true", 4)) {
				if (if_link == 1)
					tr181_set_eth_if_enable(if_link_num, 1);
				else if (if_link == 2)
					tr181_set_eth_link_enable(if_link_num, 1);
			}
			else if (!memcmp(tokptr, "false", 5)) {
				if (if_link == 1)
					tr181_set_eth_if_enable(if_link_num, 0);
				else if (if_link == 2)
					tr181_set_eth_link_enable(if_link_num, 0);
			}
			else goto errout;
		}
		else if (!memcmp(tokptr, "MaxBitRate", 10)) {
			if (cmd == 1 || if_link == 2) goto errout;
			tokptr = strsep(&strptr," ");
			if (tokptr==NULL) goto errout;

			rate=simple_strtol(tokptr, NULL, 0);
			if (rate == -1 || rate == 10 || rate == 100 || rate == 1000)
				tr181_set_eth_lf_MaxBitRate(if_link_num, rate);
			else goto errout;
		}
		else if (!memcmp(tokptr, "DuplexMode", 10)) {
			if (cmd == 1 || if_link == 2) goto errout;
			tokptr = strsep(&strptr," ");
			if (tokptr==NULL) goto errout;

			if (!memcmp(tokptr, "Half", 4)) {
				tr181_set_eth_if_DuplexMode(if_link_num, TR181_ETH_IF_DUPLEX_HALF);
			}
			else if (!memcmp(tokptr, "Full", 4)) {
				tr181_set_eth_if_DuplexMode(if_link_num, TR181_ETH_IF_DUPLEX_FULL);
			}
			else if (!memcmp(tokptr, "Auto", 4)) {
				tr181_set_eth_if_DuplexMode(if_link_num, TR181_ETH_IF_DUPLEX_AUTO);
			}
			else goto errout;
		}
		else goto errout;

	}
	else
	{
errout:
		rtlglue_printf("error input!\n");
	}

	return count;
}
#endif

#ifdef CONFIG_RTL_INBAND_CTL_ACL
#ifdef CONFIG_RTL_PROC_NEW
static int inband_ctl_acl_read(struct seq_file *s, void *v)
{
	seq_printf(s, "Inband Control Ethternet Type : 0x%x\n", inband_ctl_acl_ethernet_type);
	return 0;
}
#else
static int inband_ctl_acl_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len;
	len = sprintf(page, "Inband Control Ethternet Type : 0x%x\n", inband_ctl_acl_ethernet_type);
	return len;
}
#endif
static int inband_ctl_acl_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
	uint32 tmpBuf[32];
	uint32 uintVal;

	if (buffer && !copy_from_user(tmpBuf, buffer, count))
	{
		tmpBuf[count-1]=0;
		uintVal=simple_strtol((const char *)tmpBuf, NULL, 0);
		if(uintVal!=0)
		{
			inband_ctl_acl_ethernet_type = uintVal;
			rtl865x_removeAclForInbandCtl(vlanconfig);
			rtl865x_addAclForInbandCtl(vlanconfig);
		}
		else
		{
			rtl865x_removeAclForInbandCtl(vlanconfig);
		}
		return count;
	}
	return -EFAULT;
}
#endif

#if defined (CONFIG_RT_MULTIPLE_BR_SUPPORT)
/* using packVlanConfig store vlan group  */
#define RTL_DRV_GENERAL_NETIF_NAME "eth0"

typedef struct _netif_name_mapping_s
{
	char dev_name[MAX_IFNAMESIZE];//protocol stack device name
	char netif_Name[MAX_IFNAMESIZE];//netif name in driver
	int  valid; /* control which br interface using hw nat */
}netif_name_mapping_t;
//used for eth0.x as bridge port
static netif_name_mapping_t  netif_name_mapping[NETIF_NUMBER];
/*  flag:1 ->brctl addif
 *  flag: 2 ->brctl delif
 */
int rtk_dev_name_netif_mapping(char *br_name, char *br_port_name, int flag)
{
	unsigned char prefix[MAX_IFNAMESIZE];
	int i = 0;

	if (!br_name || !br_port_name)
		return FAILED;

	memset(prefix, '\0', sizeof(prefix));
	sprintf(prefix, "%s.", RTL_DRV_GENERAL_NETIF_NAME);
	//panic_printk("%s %d br_name=%s br_port_name=%s flag=%d \n", __FUNCTION__, __LINE__, br_name, br_port_name, flag);

	switch (flag)
	{
		case 1:
			for (i = 0; i < NETIF_NUMBER; i++)
			{
					//null space
				if (netif_name_mapping[i].netif_Name[0]=='\0')
				{
					memcpy(netif_name_mapping[i].dev_name, br_name, MAX_IFNAMESIZE);
					memcpy(netif_name_mapping[i].netif_Name, br_port_name, MAX_IFNAMESIZE);
					break;
				}

			}
			if (i == NETIF_NUMBER)
				printk("%s %d netif_name_mapping is full!\n", __FUNCTION__, __LINE__);
			break;
		case 2:
			for (i = 0; i < NETIF_NUMBER; i++)
			{
					//find it, clear
				if (!memcmp(netif_name_mapping[i].netif_Name, br_port_name, MAX_IFNAMESIZE))
				{
					memset((void *)&netif_name_mapping[i], '\0', sizeof(netif_name_mapping_t));
					break;
				}

			}
			if (i == NETIF_NUMBER)
				printk("%s %d cannot find br_port_name=%s !\n", __FUNCTION__, __LINE__, br_port_name);
			break;
		default:
			break;
	}

	return SUCCESS;
}
int rtk_get_devname_by_netifname(char *dev_name, char *netif_name)
{
	int i = 0;
	if (!dev_name || !netif_name)
		return 0;


	for (i = 0; i < NETIF_NUMBER; i++)
	{
		if ((!memcmp(netif_name_mapping[i].netif_Name, netif_name, MAX_IFNAMESIZE)))
		{
			memcpy(dev_name, netif_name_mapping[i].dev_name, MAX_IFNAMESIZE);
			return 1;
		}
	}

	return 0;
}

#if 0
int rtk_get_netif_name_by_devname(char *dev_name, char *netif_name)
{
	int i = 0;

	if (!dev_name || !netif_name)
		return FAILED;

	for (i = 0; i < NETIF_NUMBER; i++)
	{
		if ((!memcmp(netif_name_mapping[i].dev_name, dev_name, MAX_IFNAMESIZE)))
		{
			//panic_printk("%s %d dev_name=%s netif_name=%s \n", __FUNCTION__, __LINE__, dev_name, netif_name);
			if ((netif_name_mapping[i].valid))
			{
				/* valid should add to l3 table */
				memcpy(netif_name, netif_name_mapping[i].netif_Name, MAX_IFNAMESIZE);
				return SUCCESS;
			}
			else
			{
				/*  valid=0, means forward by software, add acl for it, donot add to l3 table */
				//panic_printk("%s %d dev_name=%s netif_name_mapping[i].netif_Name=%s netif_name_mapping[i].valid=%d \n", __FUNCTION__, __LINE__, dev_name, netif_name_mapping[i].netif_Name, netif_name_mapping[i].valid);
				rtk_add_acl_for_sw_forward(netif_name_mapping[i].netif_Name, dev_name, netif_name_mapping[i].valid);
				return FAILED;
			}
		}
	}

	return FAILED;
}
#endif
int rtk_get_devname_by_netif_name(char *dev_name, char *netif_Name)
{
#if 1
	int i = 0;

	if (!dev_name || !netif_Name)
		return FAILED;

	for (i = 0; i < NETIF_NUMBER; i++)
	{
		if ((!memcmp(netif_name_mapping[i].netif_Name, netif_Name, MAX_IFNAMESIZE)))
		{
			memcpy(dev_name, netif_name_mapping[i].dev_name, MAX_IFNAMESIZE);
			//panic_printk("%s %d dev_name=%s netif_name=%s \n", __FUNCTION__, __LINE__, dev_name, netif_Name);
			return SUCCESS;
		}
	}

	return FAILED;
#endif

}


extern int rtl_get_brIgmpModuleIndexbyName(char *name, int * index);


unsigned int rtl_get_brIgmpModuleIndexbyVid(unsigned int vid)
{
#if 1
	unsigned char netif_Name[MAX_IFNAMESIZE]={0};
	unsigned char dev_name[MAX_IFNAMESIZE]={0};
	int ret=FAILED;
	int index=0;
	unsigned int igmpModuleIndex=0xFFFFFFFF;
	memset(netif_Name, '\0', sizeof(netif_Name));
	sprintf(netif_Name, "%s.%d", RTL_DRV_GENERAL_NETIF_NAME,vid);

	ret=rtk_get_devname_by_netif_name(dev_name, netif_Name);
	if(ret==SUCCESS)
		igmpModuleIndex=rtl_get_brIgmpModuleIndexbyName(dev_name, &index);
	//printk("vid:%d,netif_Name:%s,dev:%s,igmpModuleIndex:%x,[%s]:[%d].\n",vid,netif_Name,dev_name,igmpModuleIndex,__FUNCTION__,__LINE__);
	return igmpModuleIndex;
#endif

}
#endif

#ifdef CONFIG_RTL_VLAN_8021Q
static struct dev_priv * rtk_get_default_cp(void)
{
	#if defined(CONFIG_RTD_1295_HWNAT)
	return ((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[RTL_LAN_IDX]));
	#else //defined(CONFIG_RTD_1295_HWNAT)
	return ((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]));
	#endif //defined(CONFIG_RTD_1295_HWNAT)
}
static struct dev_priv * rtk_get_default_wan_cp(void)
{
	#if defined(CONFIG_RTD_1295_HWNAT)
	return ((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[RTL_WAN_IDX]));
	#else //defined(CONFIG_RTD_1295_HWNAT)
	return ((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[1]));
	#endif //defined(CONFIG_RTD_1295_HWNAT)
}

static int is_nat_wan_vlan(uint16 vid)
{
   if(rtl865x_curOpMode == GATEWAY_MODE)
		if( vid == rtk_vlan_wan_vid )
			return 1;
   return 0;
}

static inline int rtk_isHwlookup_vlan(uint16 vid, struct sk_buff *skb, uint32 *portlist)
{
	int flag = TRUE;

	#if defined(CONFIG_RTL_8021Q_VLAN_SUPPORT_SRC_TAG)
	struct dev_priv *cp;
	if(rtl865x_curOpMode == GATEWAY_MODE)
	{
		cp = NETDRV_PRIV(skb->dev);
		if(cp->id == RTL_WANVLANID)
		{
			*portlist = RTK_WAN_PORT_MASK;
			flag = FALSE;
		}
		else
			flag = TRUE;
	}
	else
	#endif
	{
		*portlist = rtk_get_vlan_portmask(vid);

		//if the packet is belong to wan nat vlan , then we will not send it with HW lay2 lookup
		//only forwad it with nic direct tx way and set the portmask to only wan port(don;t support mulitable wan for now)
		if(vid == rtk_vlan_wan_vid)
		{
			*portlist = (*portlist ) &  RTK_WAN_PORT_MASK;  //to wan UC only to Wan nat only port
			flag = FALSE;
		}
		//else if(rtl_ip_option_check(skb) != TRUE)	//mark_vc , need this check ?
	}

	return flag;
}

struct dev_priv *rtk_get_cp_by_vid_pid(uint16 vid, int pid, struct sk_buff *skb)
{
	struct dev_priv *dp=NULL;

	if(! vid)
		return NULL;

	if(rtl865x_curOpMode == GATEWAY_MODE)
	{
		#if defined(CONFIG_RTL_8021Q_VLAN_SUPPORT_SRC_TAG)
		if((1 << pid) & RTL_WANPORT_MASK)
		{
			int member_port_mask = rtk_get_vlan_portmask(vid);
			uint8 vlan_type = rtk_get_vlan_type(vid);

			member_port_mask &= ~0x01e0;  //Mask cpu/ext ports.

			dp = rtk_get_default_wan_cp();
			if (dp && dp->opened &&
				memcmp(skb->data, dp->dev->dev_addr, 6)==0)
			{
				//For dut wan group untag case, otherwise it may be decided to eth0.
				dp = dp;
			}
			else if(vlan_type == VLAN_TYPE_BRIDGE)   //LAN-WAN bridge group.
			{
				dp = rtk_get_default_cp();
				//panic_printk("[%s,%d] LAN-WAN bridge, decide eth0!\n", __FUNCTION__, __LINE__);
			}
			else
				dp = rtk_get_default_wan_cp();	 //NAT-LAN/NAT-WAN group.
		}
		else
		{
			dp = rtk_get_default_cp();
		}
		#else
		dp = rtk_get_default_cp();
		#ifdef  RTK_DEDICATE_WAN_IF
		//eth1 is dedicate to wan nat infterface
		if(is_nat_wan_vlan(vid))
	 		dp = rtk_get_default_wan_cp();
		#endif
		#endif
	}
	else //BRIDGE_MODE or WISP_MODE
	{
		#ifdef CONFIG_RTL_IVL_SUPPORT
		if(vid == RTL_WANVLANID)
			dp = rtk_get_default_wan_cp();
		else
			dp = rtk_get_default_cp();
		#else
		dp = rtk_get_default_cp();
		#endif
	}

	return dp;
}
#if defined(CONFIG_RTL_MULTI_LAN_DEV)
struct dev_priv *rtk_get_multi_lan_dev_cp(uint16 vid, int pid)
{
	struct dev_priv *dp=NULL;
	int j = 0;

	if(!vid)
	return NULL;

	// usually , if enable multiple lan dev, we use pid to get cp
	for(j=0;j<ETH_INTF_NUM;j++)
	{
		dp = ((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[j]));
		if(_rtl86xx_dev.dev[j] && dp && dp->opened && (dp->portmask & (1<<pid)))
		{
			break;
		}
	}
	if (j == ETH_INTF_NUM)
		dp = rtk_get_default_cp();

#ifdef  RTK_DEDICATE_WAN_IF
	//if  eth1 is dedicate to wan nat infterface  and it is gatewaymode
	// check vid , then reurn eth1's cp
	if(is_nat_wan_vlan(vid))
	{
		dp = rtk_get_default_wan_cp();
	}
#endif

	return dp ;
}
#endif

uint16 rtk_get_vid_by_port(int pid)
{
	//get pvid
	return (uint16)vlan_ctl_p->pvid[pid];
}

uint16 rtk_get_skb_vid(struct sk_buff *skb)
{
	uint16 vid = 0;

	if(*((unsigned short *)(skb->data+(ETH_ALEN<<1)))== __constant_htons(ETH_P_8021Q))
	{
		vid = ntohs(*(uint16*)(skb->data+(ETH_ALEN<<1)+2)) & 0x0fff;
	}

	return vid;
}
#if 1
static void rtk_rm_skb_vid(struct sk_buff *skb)
{
	//if nic decide to directTx , then we need to remove packet's tag if exist
	//then  use HW auto tag function in swNic.c to decide packet tag or not
	uint16 vid=rtk_get_skb_vid(skb);
	if(vid != 0)
		REMOVE_TAG(skb);
}
#endif
static int rtk_decide_vlan_id(int pid,struct sk_buff *skb,uint16 *vid)
{
	uint16 skb_vid=0;
	int type=VID_FROM_PVID;
	//get packet vid from skb
	skb_vid =  rtk_get_skb_vid(skb);

	if(!skb_vid)
	skb_vid =  rtk_get_vid_by_port(pid);
	else //packet with tag
		type = VID_FROM_PACKET;

	*vid = skb_vid;

	 return type ;
}

static struct dev_priv *rtk_decide_vlan_cp(int pid,struct sk_buff *skb)
{
	struct dev_priv *dp=NULL;
	uint16 skb_vid=0;

	if(!vlan_ctl_p->flag)
	return NULL;

	rtk_decide_vlan_id(pid,skb,&skb_vid);

	#if defined(CONFIG_RTL_MULTI_LAN_DEV)
	dp = (struct dev_priv *)rtk_get_multi_lan_dev_cp(skb_vid, pid);
	#else
	dp = (struct dev_priv *)rtk_get_cp_by_vid_pid(skb_vid, pid, skb);
	#endif

	return dp;
}

uint32 rtk_get_vlan_tagmask(uint16 vid)
{
	uint32 mask;

	if(!vlan_ctl_p->flag)
		return 0;

	mask = (vlan_ctl_p->group[vid].memberMask) & (vlan_ctl_p->group[vid].tagMemberMask) ;
	return mask;
}

uint32 rtk_get_vlan_portmask(uint16 vid)
{
	uint32 mask;
	mask = vlan_ctl_p->group[vid].memberMask ;

	return mask;
}

uint8 rtk_get_vlan_type(uint16 vid)
{
	uint32 vlan_type;
	vlan_type = vlan_ctl_p->group[vid].vlanType;

	return vlan_type;
}

static inline int is_cpu_port_tagged(uint16 vid)
{
	uint32 mask;
	mask = (vlan_ctl_p->group[vid].memberMask) & (vlan_ctl_p->group[vid].tagMemberMask) ;

	if( (mask & RTK_CPU_PORT_MASK) == RTK_CPU_PORT_MASK)
		return 1;
	else
		return 0;
}

static int rtk_process_11q_rx(int port, struct sk_buff *skb)
{
	uint16 vid =0;

	if(!vlan_ctl_p->flag)
		return 0;

	vid = rtk_get_skb_vid(skb);

	if(vid == 0) //if packet without tag, check pvid to add tag
	{
		vid = rtk_get_vid_by_port(port);
		//if(is_cpu_port_tagged(vid))
		//{
			#if defined(CONFIG_RTL_8021Q_VLAN_SUPPORT_SRC_TAG)
			{
				if(rtl865x_curOpMode == GATEWAY_MODE)
				{
					if(((struct dev_priv *)NETDRV_PRIV(skb->dev))->id != RTL_WANVLANID)
						rtk_insert_vlan_tag(vid,skb);
				}
				else
					rtk_insert_vlan_tag(vid, skb);
			}
			#elif defined(RTK_DEDICATE_WAN_IF)
			{
				if(!is_nat_wan_vlan(vid))
					rtk_insert_vlan_tag(vid,skb);
			}
			#else
			{
				rtk_insert_vlan_tag(vid,skb);
			}
			#endif
	 	//}
	}
	else //if packet with tag , mostly don't touch
	{
		//if packet with tag , but it is to wan nat vlan . remove the tag
		#if defined(CONFIG_RTL_8021Q_VLAN_SUPPORT_SRC_TAG)
		if(rtl865x_curOpMode == GATEWAY_MODE &&
			((struct dev_priv *)NETDRV_PRIV(skb->dev))->id == RTL_WANVLANID)
		{
			REMOVE_TAG(skb);
		}
		#elif defined(RTK_DEDICATE_WAN_IF)
		if(is_nat_wan_vlan(vid))
		{
			REMOVE_TAG(skb);
			goto wan_ingress_check;
			//return 1;
		}
		#endif

		if(!is_cpu_port_tagged(vid)) //if cpu port is unatagged
			REMOVE_TAG(skb);
	}

#if defined(CONFIG_RTL_CUSTOM_PASSTHRU)
	if((rtk_get_skb_vid(skb) != 0) && (strncmp(skb->dev->name,"peth", 4)==0))
	{
		REMOVE_TAG(skb);
	}
#endif

#if !defined(CONFIG_RTL_8021Q_VLAN_SUPPORT_SRC_TAG)
wan_ingress_check:
	{
		int vlan_type=0, port_mask=0;

		vlan_type = rtk_get_vlan_type(vid);
		port_mask = rtk_get_vlan_portmask(vid);
		//WAN port ingress check, only WAN vid or bridge vid can pass through.
		if(RTK_WAN_PORT_MASK == 1<<port &&
			vlan_type == VLAN_TYPE_NAT &&
			!(port_mask & RTK_WAN_PORT_MASK))
		{
			//panic_printk("WAN port ingress drop(NAT LAN), vid=%d!\n", vid);
			return 2;
		}
	}
#endif

	if(rtl865x_curOpMode == GATEWAY_MODE)
	{
		*(uint16 *)(skb->linux_vlan_src_tag) = __constant_htons(ETH_P_8021Q);
		*(uint16 *)(skb->linux_vlan_src_tag+2) = __constant_htons(vid);
	}

	return 1;
}

void rtk_init_11q_setting(void)
{

	//HW
	//rtl865xC_setNetDecisionPolicy(NETIF_PORT_BASED);
	// REG32(SWTCR0) |= EnUkVIDtoCPU;

	#if defined(CONFIG_RTL_8021Q_VLAN_SUPPORT_SRC_TAG)
	if(rtl865x_curOpMode == GATEWAY_MODE)
		REG32(SWTCR0) |= EnUkVIDtoCPU;
	else
	#endif
		REG32(SWTCR0) &= ~(1 << 15);

}
#if defined(CONFIG_RTL_LAYERED_DRIVER_ACL)
int32 rtl865x_enable_acl(uint32 enable);
#endif

void rtk_default_eth_setting(void)  // copy from rtk_vlan_support=0 switch setting
{
	int i=0;
	int j=0;
	struct net_device *dev;
	struct dev_priv	  *dp;

#if defined (CONFIG_RTL_IGMP_SNOOPING)
#if defined (CONFIG_RTL_MLD_SNOOPING)
		if(mldSnoopEnabled)
		{
			rtl865x_removeAclForMldSnooping(vlanconfig);
		}
#endif
#if  defined (CONFIG_RTL_HARDWARE_MULTICAST)
		if(igmpsnoopenabled)
		{
			//remove igmp acl
			rtl_processAclForIgmpSnooping(0);
		}
#endif
#endif
		//rtl_config_rtkVlan_vlanconfig(rtl865x_curOpMode); //mark_vc
		//rtl_config_lanwan_dev_vlanconfig(rtl865x_curOpMode);
		#if defined (CONFIG_RTL_MULTI_LAN_DEV)
		rtl_config_vlanconfig(rtl865x_curOpMode);
		#else
		//rtl_config_rtkVlan_vlanconfig(rtl865x_curOpMode);
		rtl_config_lanwan_dev_vlanconfig(rtl865x_curOpMode);
		#endif
		re865x_packVlanConfig(vlanconfig, packVlanConfig);
		rtl_reinit_hw_table();
		reinit_vlan_configure(packVlanConfig);

		#if defined(CONFIG_RTL_8367R_SUPPORT)
		rtk_vlan_init();
		rtk_stat_global_reset();
		RTL8367R_vlan_set();
		rtl865x_disableRtl8367ToCpuAcl();
		#endif

#if defined(CONFIG_RTL_LAYERED_DRIVER_ACL)
		rtl865x_enable_acl(1); //enable acl feature
#endif
/*update dev port number*/
		for(i=0; vlanconfig[i].vid != 0; i++)
		{
			if (IF_ETHER!=vlanconfig[i].if_type)
			{
				continue;
			}

			dev=_rtl86xx_dev.dev[i];
			dp = ((struct dev_priv *)(NETDRV_PRIV(dev)));
			dp->portnum  = 0;
			for(j=0;j<RTL8651_AGGREGATOR_NUMBER;j++)
			{
				if(dp->portmask & (1<<j))
					dp->portnum++;
			}

		}

#if defined (CONFIG_RTL_IGMP_SNOOPING)
		re865x_reInitIgmpSetting(rtl865x_curOpMode);
		#if defined (CONFIG_RTL_MLD_SNOOPING)
		if(mldSnoopEnabled)
		{
			re865x_packVlanConfig(vlanconfig, packVlanConfig);
			rtl865x_addAclForMldSnooping(packVlanConfig);
		}
		#endif
#endif

#if (defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)) && defined(CONFIG_RTL_IPV6READYLOGO)
		{
			rtl865x_addAclForIPV6ReadyLogo(packedVlanConfig);
		}
#endif

#if defined(CONFIG_RTL_HTTP_REDIRECT_LOCAL)
	rtl865x_removeAclForHttpRedirect(packedVlanConfig);
	rtl865x_addAclForHttpRedirect(packedVlanConfig);
#endif
#if defined(CONFIG_RTL_DNS_TRAP)
	rtl_remove_Acl_for_dns_trap(packedVlanConfig);
	rtl_add_Acl_for_dns_trap(packedVlanConfig);
#endif

		//always init the default route...
		if(rtl8651_getAsicOperationLayer() >2)
		{
#ifdef CONFIG_RTL_LAYERED_DRIVER_L3
			rtl865x_addRoute(0,0,0,RTL_DRV_WAN0_NETIF_NAME,0);
#endif
		}
}
#if 0
static void rtk_set_eth_pvid(int port, uint16 pvid)
{
   // 819x
   #ifdef CONFIG_RTL_819X_SWCORE
	if(port == RTK_CPU_PORT_IDX ) //map to real cpu port
		port= RTK_HW_CPU_PORT_IDX;
	rtl8651_setAsicPvid(port, pvid);
   #endif
   // mark_vc, 8367 or other integrated switch

}
#endif
static inline uint32 rtk_hw_port_map(uint16 vid ,uint32 portmask)
{
	uint32 mask= portmask & RTK_PHY_PORT_MASK  ; // 819x hw max

	#ifdef CONFIG_RTL_8021Q_VLAN_SUPPORT_SRC_TAG
	//In source-tag mode, NAT-LAN group can be same with DUT-WAN.
	if(vid == rtk_vlan_wan_vid && !(portmask&RTL_LANPORT_MASK))
		mask = mask & (~RTK_CPU_PORT_MASK);
	#else
	if(vid == rtk_vlan_wan_vid ) //mark_vc , wan vlan no cpu port added , remove it
		mask = mask & (~RTK_CPU_PORT_MASK);
	#endif

	//convert  CPU port define
	if( (mask & RTK_CPU_PORT_MASK) == RTK_CPU_PORT_MASK)
		mask = ((mask) & (~RTK_CPU_PORT_MASK)) | (RTK_HW_CPU_PORT_MASK);

	return mask;
}

static void rtk_update_netif_vlan(int netif_idx,uint16 vid , uint32 portmask)
{
	struct net_device *dev;
	struct dev_priv	  *dp;
	int j;
		// update hw netif table vid , depend on vlanconfig idx
	rtl865x_setNetifVid(vlanconfig[netif_idx].ifname,vid);
	//update wan slave netif(ppp0) vid
	if (netif_idx == 1)
		rtl865x_setNetifVid(RTL_DRV_PPP_NETIF_NAME,vid);

	   //update sw info
	   vlanconfig[netif_idx].vid = vid;
	vlanconfig[netif_idx].memPort = portmask;

	((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[netif_idx]))->id = vid;
	((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[netif_idx]))->portmask = portmask;

	dev=_rtl86xx_dev.dev[netif_idx];
	dp = NETDRV_PRIV(dev);
	dp->portnum  = 0;
	for(j=0;j<RTL8651_AGGREGATOR_NUMBER;j++)
	{
			if(dp->portmask & (1<<j))
				dp->portnum++;
	}

}
#if defined(CONFIG_RTL_MULTI_LAN_DEV)
static void rtk_update_netif_vlan_multi_lan_dev(int netif_idx,uint16 vid , uint32 portmask)
{

	// update hw netif table vid , depend on vlanconfig idx
	if (portmask & RTL_LANPORT_MASK_4)
	{
		/* avoid exchange port mask, store in the fixed position  */
		vlanconfig[0].vid = vid;
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]))->id = vid;
	}
#if defined(CONFIG_RTD_1295_HWNAT)
	else if (portmask & RTL_LANPORT_MASK_5)
	{
		vlanconfig[2].vid = vid;
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[2]))->id = vid;
	}
#else //defined(CONFIG_RTD_1295_HWNAT)
	else if (portmask & RTL_LANPORT_MASK_3)
	{
		vlanconfig[2].vid = vid;
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[2]))->id = vid;
	}
	else if (portmask & RTL_LANPORT_MASK_2)
	{
		vlanconfig[3].vid = vid;
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[3]))->id = vid;
	}
	else if (portmask & RTL_LANPORT_MASK_1)
	{
		vlanconfig[4].vid = vid;
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[4]))->id = vid;
	}
#endif //defined(CONFIG_RTD_1295_HWNAT)
	rtl865x_setNetifVid(vlanconfig[netif_idx].ifname,vid);

	//update wan slave netif(ppp0) vid
	if (netif_idx == 1)
		rtl865x_setNetifVid(RTL_DRV_PPP_NETIF_NAME,vid);

}

static void rtk_update_vlan_multi_lan_dev(uint16 vid , uint32 portmask)
{
	if (portmask & RTL_LANPORT_MASK_4)
	{
		/* avoid exchange port mask, store in the fixed position  */
		vlanconfig[0].vid = vid;
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[0]))->id = vid;
	}
#if defined(CONFIG_RTD_1295_HWNAT)
	else if (portmask & RTL_LANPORT_MASK_5)
	{
		vlanconfig[2].vid = vid;
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[2]))->id = vid;
	}
#else //defined(CONFIG_RTD_1295_HWNAT)
	else if (portmask & RTL_LANPORT_MASK_3)
	{
		vlanconfig[2].vid = vid;
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[2]))->id = vid;
	}
	else if (portmask & RTL_LANPORT_MASK_2)
	{
		vlanconfig[3].vid = vid;
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[3]))->id = vid;
	}
	else if (portmask & RTL_LANPORT_MASK_1)
	{
		vlanconfig[4].vid = vid;
		((struct dev_priv *)NETDRV_PRIV(_rtl86xx_dev.dev[4]))->id = vid;
	}
#endif //defined(CONFIG_RTD_1295_HWNAT)
}

#endif

void rtk_add_eth_vlan(uint16 vid , uint32 portmask,uint32 tagmask)
{
	//819x
#ifdef CONFIG_RTL_819X_SWCORE
	/*add vlan info to switch*/
	rtl865x_addVlan(vid);
	//rtl865x_addVlanPortMember(vid,(portmask &0x1ff)|0x100);
	//convert cpu port mapping

	rtl865x_addVlanPortMember(vid,rtk_hw_port_map(vid,portmask));//mark_vc , need cpu port ?
	rtl865x_setVlanPortTag(vid,rtk_hw_port_map(vid,tagmask), TRUE);

	//if portmask has wan prot then set fid to 1
	if(rtl865x_curOpMode == GATEWAY_MODE)
	{
		if( (portmask & RTK_WAN_PORT_MASK ) == RTK_WAN_PORT_MASK )
		{
			rtl865x_setVlanFilterDatabase(vid, RTL_WAN_FID);

			//if(( (portmask & RTK_CPU_PORT_MASK ) == 0 ))  //if wan vlan but no CPU port , mean WAN NAT vlan group
			if( vid == rtk_vlan_wan_vid ) // if it is nat wan vlan , need to update netif table ,mark_vc
			{
				// update wan nat  netif , vlanconfig and cp info
				rtk_update_netif_vlan(1,vid,rtk_hw_port_map(vid,portmask));  //wan 1
				if(portmask & (~(RTK_WAN_PORT_MASK | RTK_CPU_PORT_MASK)))
				{
					//bridge lan, bridge vid equal nat vid case
					//panic_printk("%s %d vid=%d portmask=0x%x tagmask=0x%x rtk_vlan_wan_vid=%d \n", __FUNCTION__, __LINE__, vid, portmask, tagmask, rtk_vlan_wan_vid);
					#if defined(CONFIG_RTL_MULTI_LAN_DEV)
					rtk_update_vlan_multi_lan_dev(vid , portmask);
					#endif
				}
			}
			else if (portmask & (~(RTK_WAN_PORT_MASK | RTK_CPU_PORT_MASK)))
			{
				//bridge lan, bridge vid not equal nat vid case
				#if defined(CONFIG_RTL_MULTI_LAN_DEV)
				//panic_printk("%s %d vid=%d portmask=0x%x tagmask=0x%x rtk_vlan_wan_vid=%d \n", __FUNCTION__, __LINE__, vid, portmask, tagmask, rtk_vlan_wan_vid);
				rtk_update_vlan_multi_lan_dev(vid , portmask);
				#endif
			}
		}
		else
		{
			rtl865x_setVlanFilterDatabase(vid, RTL_LAN_FID);

			//if  portmask is not include wan port , then also set eth0 netif to this vlan
			//update lan nat  netif , vlanconfig and cp info
			#if !defined(CONFIG_RTL_8021Q_VLAN_SUPPORT_SRC_TAG)
			#if defined(CONFIG_RTL_MULTI_LAN_DEV)
			rtk_update_netif_vlan_multi_lan_dev(0,vid,rtk_hw_port_map(vid,portmask));
			#else
			if(vid == rtk_vlan_hw_nat_lan_vid)
				rtk_update_netif_vlan(0, vid, rtk_hw_port_map(vid,portmask));  //lan 0
			#endif
			#endif
			//rtl865x_setNetifVid(vlanconfig[0].ifname,vid);
		}
	}
	else  //bridge mode no need to deal netif table
	{
		rtl865x_setVlanFilterDatabase(vid, RTL_LAN_FID);
		#if defined(CONFIG_RTL_MULTI_LAN_DEV)
		rtk_update_netif_vlan_multi_lan_dev(0,vid,rtk_hw_port_map(vid,portmask));
		#else
		rtk_update_netif_vlan(0,vid,rtk_hw_port_map(vid,portmask));  //  lan 0
		#endif
	}
#endif

	// mark_vc, 8367 or other integrated switch
	#if defined(CONFIG_RTL_8367R_SUPPORT)
 //extern int rtl_vlan_RTL8367R_set(unsigned short vid, unsigned int tagmask, unsigned int mask);
	if(rtl865x_curOpMode == GATEWAY_MODE)
	{
		if( (portmask & RTK_WAN_PORT_MASK ) == RTK_WAN_PORT_MASK )
		{
			//rtl_vlan_RTL8367R_set(vid, tagmask, portmask, 1);
			//include port0~port3, avoid (wan to lan hw multicast) tx to lan with tag.
			if (is_nat_wan_vlan(vid))
				rtl_vlan_RTL8367R_set(vid, tagmask, portmask|0xf, 1);
			else
				rtl_vlan_RTL8367R_set(vid, tagmask, portmask, 1);
		}
		else
		{
			rtl_vlan_RTL8367R_set(vid, tagmask, portmask, 0);
		}
	}
	else
	{
		rtl_vlan_RTL8367R_set(vid, tagmask, portmask, 0);
	}
	#endif
}

static inline int32 rtl_isWanPort(struct dev_priv *cp, uint16 vid, struct sk_buff *skb, int pid)
{
	struct iphdr *iph=NULL;
	#if defined (CONFIG_RTL_MLD_SNOOPING)
	struct ipv6hdr *ipv6h=NULL;
	#endif
	unsigned int l4Protocol=0;

	if (!cp || !skb)
		return 0;

	//panic_printk("%s %d vid=%d pid=%d cp->portnum=%d\n", __FUNCTION__, __LINE__, vid, pid, cp->portnum);
	if (linux_vlan_enable && linux_vlan_hw_support_enable )
	{
		//bridge vid equal nat vid case
		if ((is_nat_wan_vlan(vid)) && (cp->portnum > 1))
		{
			if ((1<<pid) & RTL_WANPORT_MASK)
			{
				if((skb->data[0]==0x01) && (skb->data[1]==0x00)&& (skb->data[2]==0x5e))
				{
					iph=re865x_getIpv4Header(skb->data);
					if(iph!=NULL)
					{
						l4Protocol=iph->protocol;
						if(l4Protocol==IPPROTO_IGMP)
						{
							return 1;
						}
					}
				}

				#if defined (CONFIG_RTL_MLD_SNOOPING)
				else if ((skb->data[0]==0x33) && (skb->data[1]==0x33) && (skb->data[2]!=0xff))
				{
					ipv6h=re865x_getIpv6Header(skb->data);
					if(ipv6h!=NULL)
					{
						l4Protocol=re865x_getIpv6TransportProtocol(ipv6h);
						if(l4Protocol==IPPROTO_ICMPV6)
						{
							return 1;
						}
					}
				}
				#endif
			}
		}
		else
		{
			//bridge vid not equal nat case
			#ifdef CONFIG_RTL_8021Q_VLAN_SUPPORT_SRC_TAG
			if (cp->id==RTL_WANVLANID)
				return 1;
			#else
			if (cp->id==rtk_vlan_wan_vid)
				return 1;
			#endif

			#if defined(CONFIG_RTL_CUSTOM_PASSTHRU)
			if(cp == NETDRV_PRIV(_rtl86xx_dev.pdev))
				return 1;
			#endif
		}
	}
	else if (linux_vlan_enable && !linux_vlan_hw_support_enable )
	{
		if (cp->id==RTL_WANVLANID )
			return 1;
	}

	return 0;
}
#if 0
static void rtk_del_eth_vlan(uint16 vid)
{
   // 819x
   #ifdef CONFIG_RTL_819X_SWCORE
	   rtl865x_delVlan(vid);
   #endif
   // mark_vc, 8367 or other integrated switch

}
static int32 rtk_vlan_ctl_read( char *page, char **start, off_t off, int count, int *eof, void *data )
{
   int len=0;

	len = sprintf(page, "rtk_vlan_ctl = %d\n", vlan_ctl_p->flag);
   return len;
}

static int32 rtk_vlan_tbl_write( struct file *filp, const char *buff,unsigned long len,void *data)
{
	char tmpbuf[100];
	int num ;
	int index;
	uint32 mask;
	uint32 tagMask;
	uint32 vid;
	linux_vlan_group_t *vlan_group_p=NULL;

	if(!vlan_ctl_p->flag)
	{
		printk("rtk 11q vlan support not enabled!!!\n");
		return len;
	}

	if (buff && !copy_from_user(tmpbuf, buff, len))
	{
		num=sscanf(tmpbuf,"%d %d %x %x",&index,&vid,&mask,&tagMask);

	if(num!=4)
		return len;

	if(index > RTK_MAX_VLAN_GROUP)
		return len;

	if(vid <= 0 || vid > 4095 )
		return len;

	   /*clear the old one and delete old vlan group*/
	if(rtk_vlan_idx[index] !=0)  //remove previous vlan if exist
	{
		rtk_del_eth_vlan(rtk_vlan_idx[index]);
	}

	rtk_vlan_idx[index] = vid;
	vlan_group_p=&vlan_ctl_p->group[vid];

	memset(vlan_group_p,0,sizeof(linux_vlan_group_t));
	/*add new vlan group*/
	vlan_group_p->memberMask=mask;
	vlan_group_p->tagMemberMask=tagMask;
	vlan_group_p->vid=(uint16)vid;

	rtk_add_eth_vlan(vlan_group_p->vid,vlan_group_p->memberMask,vlan_group_p->tagMemberMask);

	}

	return len;
}
#endif
#if defined (CONFIG_RTL_HARDWARE_MULTICAST)
#if defined CONFIG_RTL_MULTICAST_PORT_MAPPING
static int rtl865x_nic_MulticastHardwareAccelerate(unsigned int brFwdPortMask,
												unsigned int srcPort,unsigned int srcVlanId,
												unsigned int srcIpAddr, unsigned int destIpAddr, unsigned int mapPortMask)

#else
static int rtl865x_nic_MulticastHardwareAccelerate(unsigned int brFwdPortMask,
												unsigned int srcPort,unsigned int srcVlanId,
												unsigned int srcIpAddr, unsigned int destIpAddr)
#endif
{
	int ret;
	//int fwdDescCnt;
	//unsigned short port_bitmask=0;

	//unsigned int tagged_portmask=0;


	struct rtl_multicastDataInfo multicastDataInfo;
	struct rtl_multicastFwdInfo  multicastFwdInfo;

	rtl865x_tblDrv_mCast_t * existMulticastEntry;
	rtl865x_mcast_fwd_descriptor_t  fwdDescriptor;

	#if 0
	printk("%s:%d,srcPort is %d,srcVlanId is %d,srcIpAddr is 0x%x,destIpAddr is 0x%x\n",__FUNCTION__,__LINE__,srcPort,srcVlanId,srcIpAddr,destIpAddr);
	#endif

	#if 0
	if(brFwdPortMask & br0SwFwdPortMask)
	{
		return -1;
	}
	#endif
	//printk("%s:%d,destIpAddr is 0x%x, srcIpAddr is 0x%x, srcVlanId is %d, srcPort is %d\n",__FUNCTION__,__LINE__,destIpAddr, srcIpAddr, srcVlanId, srcPort);
	existMulticastEntry=rtl865x_findMCastEntry(destIpAddr, srcIpAddr, (unsigned short)srcVlanId, (unsigned short)srcPort);
	if(existMulticastEntry!=NULL)
	{
		/*it's already in cache */
		return 0;

	}

	if(brFwdPortMask==0)
	{
	#if defined CONFIG_RTL_MULTICAST_PORT_MAPPING
		rtl865x_blockMulticastFlow(srcVlanId, srcPort, srcIpAddr, destIpAddr,mapPortMask);
	#else
		rtl865x_blockMulticastFlow(srcVlanId, srcPort, srcIpAddr, destIpAddr);
	#endif
		return 0;
	}

	multicastDataInfo.ipVersion=4;
	multicastDataInfo.sourceIp[0]=  srcIpAddr;
	multicastDataInfo.groupAddr[0]=  destIpAddr;

	/*add hardware multicast entry*/

	memset(&fwdDescriptor, 0, sizeof(rtl865x_mcast_fwd_descriptor_t ));
	strcpy(fwdDescriptor.netifName,"eth*");
	fwdDescriptor.fwdPortMask=0xFFFFFFFF;

	ret= rtl_getMulticastDataFwdInfo(nicIgmpModuleIndex, &multicastDataInfo, &multicastFwdInfo);

	if(ret!=0)
	{
		return -1;
	}
	else
	{
		if(multicastFwdInfo.cpuFlag)
		{
			fwdDescriptor.toCpu=1;
		}
		fwdDescriptor.fwdPortMask=multicastFwdInfo.fwdPortMask & (~(1<<srcPort));
	}

#if defined CONFIG_RTL_MULTICAST_PORT_MAPPING
	fwdDescriptor.fwdPortMask &= mapPortMask;
#endif

	if(fwdDescriptor.fwdPortMask != 0)
	{
		//printk(" rtl865x_nic_MulticastHardwareAccelerate  , %s:%d,srcPort is %d,srcVlanId is %d,srcIpAddr is 0x%x,destIpAddr is 0x%x ,mask= %02x \n",__FUNCTION__,__LINE__,srcPort,srcVlanId,srcIpAddr,destIpAddr,
		//	fwdDescriptor.fwdPortMask);
		#if defined CONFIG_RTL_MULTICAST_PORT_MAPPING
		ret=rtl865x_addMulticastEntry(destIpAddr, srcIpAddr, (unsigned short)srcVlanId, (unsigned short)srcPort,
							&fwdDescriptor, 1, 0, 0, 0, mapPortMask);
		#else
		ret=rtl865x_addMulticastEntry(destIpAddr, srcIpAddr, (unsigned short)srcVlanId, (unsigned short)srcPort,
							&fwdDescriptor, 1, 0, 0, 0);
		#endif
	}

	return 0;
}

#endif
#endif

#if defined(CONFIG_RTL_ETH_NAPI_SUPPORT)
static int rtl865x_poll(struct napi_struct *napi, int budget)
{
	struct dev_priv *cp = container_of(napi, struct dev_priv, napi);
	int work_done= 0;

	work_done = interrupt_dsr_rx(cp, (u32) budget);

	if (work_done < budget) {
		napi_complete(napi);
		interrupt_dsr_rx_done(&RxIntData);
	}

	return work_done;
}
#endif

#if defined(CONFIG_RTL_8198C) || defined(CONFIG_RTL_8197F)
int rtl819x_setSwEthPvid(uint16 port, uint16 pvid)
{
	if (port >= RTL8651_PORT_NUMBER)
		return FAILED;

	sw_pvid[port] = pvid;
	return SUCCESS;
}

int rtl819x_getSwEthPvid(uint16 port, uint16* pvid)
{
	if (port >= RTL8651_PORT_NUMBER)
		return FAILED;

	*pvid = sw_pvid[port];
	return SUCCESS;
}
#endif
#if defined(CONFIG_RTL_ISP_MULTI_WAN_SUPPORT)
int rtl_get_bind_port_mask_by_dev_name(char *name)
{
	int i;

	for (i=0; i<LAN_DEV_NUM_MAX; i++)
	{
		if (strncmp(name, lan_dev_bind_mask_mapping_drv[i].ifname, IFNAMSIZ) == 0) {
			return lan_dev_bind_mask_mapping_drv[i].bind_mask;
		}
	}

	return 0;
}

static int _rtl_update_vlan_table(int vlan_id)
{
	int i;
	int _the_num_of_this_vid_WAN=0;
	int _has_bridged_WAN=0;

	/*	member ports are always WAN + LAN 	*/
	for (i=0; vlanconfig[i].ifname[0] != '\0'; i++)
	{
		if (vlanconfig[i].isWan && (vlanconfig[i].is_slave==0) && (vlanconfig[i].vid==vlan_id)) {
			_the_num_of_this_vid_WAN++;
			if (vlanconfig[i].protocol == SMUX_PROTO_BRIDGE)
				_has_bridged_WAN = 1;
		}
	}

	if (_the_num_of_this_vid_WAN > 0) {
		uint32		memPort;
		uint32		untagSet;
		int 			ret;

		if (_has_bridged_WAN) {
			memPort = (RTL_LANPORT_MASK | RTL_WANPORT_MASK);
			if (vlan_id == RTL_BRIDGE_WANVLANID)
				untagSet 	= (RTL_LANPORT_MASK | RTL_WANPORT_MASK);
			else
				untagSet 	= RTL_LANPORT_MASK;
		} else {
			memPort 	= RTL_WANPORT_MASK;
			if (vlan_id==RTL_WANVLANID)
				untagSet 	= RTL_WANPORT_MASK;
			else
				untagSet 	= 0;
		}

		ret = rtl865x_addVlan(vlan_id);
		if (ret == SUCCESS) {
			/*  this vlan is the first time to setup , set its fid */
			if (rtl865x_setVlanFilterDatabase(vlan_id,RTL_WAN_FID) != SUCCESS) {
				printk("Leave %s @ %d \n", __FUNCTION__, __LINE__);
				return FAILED;
			}
		} else if (ret != RTL_EVLANALREADYEXISTS) {
			printk("Leave %s @ %d \n", __FUNCTION__, __LINE__);
			return FAILED;
		}

		/*  No matter whether we has setup this vlan table before or not , reset its member/untag ports  */
		if (rtl865x_modVlanPortMember(vlan_id,memPort,untagSet) != SUCCESS) {
			printk("Leave %s @ %d \n", __FUNCTION__, __LINE__);
			return FAILED;
		}
	} else {
		if (rtl865x_delVlan(vlan_id) != SUCCESS) {
			printk("Leave %s @ %d \n", __FUNCTION__, __LINE__);
			return FAILED;
		}
	}

	return SUCCESS;
}

static int32 _rtl_update_pvid(void)
{
	int i,j;
	int bridged_wan_exist = 0;
	int novlantag_bridged_wan_exist = 0;
	int novlantag_routing_wan_exist = 0;

	re865x_packVlanConfig(vlanconfig, packedVlanConfig);

	for (j=0; packedVlanConfig[j].vid != 0; j++)
	{
		 if ( packedVlanConfig[j].isWan) {
			if (packedVlanConfig[j].protocol == SMUX_PROTO_BRIDGE) {
		 		bridged_wan_exist = 1;
				if (packedVlanConfig[j].vid == RTL_BRIDGE_WANVLANID)
					novlantag_bridged_wan_exist=1;
			} else {
				if (packedVlanConfig[j].vid == RTL_WANVLANID)
					novlantag_routing_wan_exist=1;
			}
		 }
	}

	//printk("(%s) bridged_wan_exist:%d   novlantag_bridged_wan_exist:%d  novlantag_routing_wan_exist:%d \n"
	//	,__FUNCTION__,bridged_wan_exist,novlantag_bridged_wan_exist,novlantag_routing_wan_exist);

	for (i=0; i<RTL8651_PORT_NUMBER+3; i++)
	{
		uint16 pvid;
		/* wan port*/
		if ((1<<i) & RTL_WANPORT_MASK) {
			/*
				vlan 7's mbr = WAN + LAN 	(for downstream bridging unicast hwacc)
				vlan 8's mbr = WAN		(for downstream routing unicast hwacc)

				downstream bridging unicast hwacc
					: vid has to be 7
				downstream IPoE unicast hwacc
					: vid could be either 7 or 8
				downstream PPPoE unicast hwacc
					: vid could be either 7 or 8

				downstream bridging multicast hwacc	: vid could be either 7 or 8
				downstream IPoE multicast hwacc	: vid could be either 7 or 8
				downstream PPPoE multicast hwacc	: vid has to be 8 (for DA changes from GMAC to 01:00:5e:xx:xx:xx)

				Hence, Default WAN port's pvid is 8 , but...
					(1) If there existed one bridged WAN, pvid should be 7
					(2) If there existed both bridged and PPPoE WAN, pvid is 7 and set pppoe's pvid = 8
			*/
			if (novlantag_bridged_wan_exist)
				pvid = RTL_BRIDGE_WANVLANID;
			else
				pvid = RTL_WANVLANID;

/* //support untag L2 unicast pppoe passthrough hwacc, but l3 pppoe multicast will no hwacc
			if(novlantag_bridged_wan_exist && novlantag_routing_wan_exist)
				rtl8651_setProtocolBasedVLAN(RTL8651_PBV_RULE_PPPOE_SESSION,i,1,RTL_WANVLANID);
			else
				rtl8651_setProtocolBasedVLAN(RTL8651_PBV_RULE_PPPOE_SESSION,i,0,0);
*/
		} else { /* lan port */
		#if defined(CONFIG_RTL8685) || defined(CONFIG_RTL8685S)
			pvid = RTL_LANVLANID;
		#else
			if (!bridged_wan_exist)
				pvid = RTL_LANVLANID;
			else
			{
				/* if the lan port only  exist in only one bridged WAN, we can set pvid as its vid for upstream hw l2 forwarding*/
				int this_port_exist_in_bridged_wan_num=0;
				int the_first_vid_exist_in_bridged_wan=-1;

				for(j=0; packedVlanConfig[j].vid != 0; j++)
				{
					if ( packedVlanConfig[j].isWan && (packedVlanConfig[j].memPort != RTL_WANPORT_MASK) && (packedVlanConfig[j].memPort&(1<<i)) )
					{
						this_port_exist_in_bridged_wan_num ++;
						if (the_first_vid_exist_in_bridged_wan == -1)
							the_first_vid_exist_in_bridged_wan = packedVlanConfig[j].vid;
					}
				}

				if(this_port_exist_in_bridged_wan_num==1)
					pvid = the_first_vid_exist_in_bridged_wan;
				else
					pvid = RTL_LANVLANID;
			}
		#endif
		}

		/* extport patch*/
		if (i == RTL_CPU_PORT)
			pvid = RTL_LANVLANID;

		//set pvid
		CONFIG_CHECK(rtl8651_setAsicPvid(i, pvid));
		pvid_per_port[i]=pvid;

		//set port to netif
		for (j=0; packedVlanConfig[j].vid != 0; j++)
			if (packedVlanConfig[j].vid == pvid)
				break;

		rtl865x_setPortToNetif(packedVlanConfig[j].ifname, i);
	}

	return SUCCESS;
}

int rtl_set_wanport_vlanconfig(char *ifname, int proto, int vid, int napt)
{
	int idx = -1;
		int vlan_id;
	rtl865x_netif_t netif;
	int i;

	//printk("Enter %s (ifname:%s  protio:%d   vid:%d  napt:%d)\n",__FUNCTION__,ifname,proto,vid,napt);

	/* we has to check whether ifname existed and has not been used yet
		Otherwise, there will exist two same netif_name in "vlanconfig"
	*/
	for (i=0; vlanconfig[i].ifname[0] != '\0' ; i++)
	{
		if (vlanconfig[i].isWan && (vlanconfig[i].is_slave==0) && !strcmp(vlanconfig[i].ifname, ifname)) {
			if (vlanconfig[i].vid == 0) {	/* this wan has not been used */
				idx = i;
				break;
			}
		}
	}

	if (idx == -1) {
		printk("Leave %s @ %d \n", __FUNCTION__, __LINE__);
		return FAILED;
	}

	/* Note.  the fields except for vid,isWan,proto in "vlanconfig" have no change */
	/* update  vid  in "vlanconfig"  */
	if (vid != -1) {
		vlan_id = vid&VLAN_VID_MASK;
	} else {
		if (proto == SMUX_PROTO_BRIDGE)
			vlan_id = RTL_BRIDGE_WANVLANID;
		else {
			vlan_id = RTL_WANVLANID;
			/*jwj: To fix wan2lan hw nat fail when vlan based netif,
			because 2 wan netif point to vid 8.*/
			#if defined(CONFIG_RTL_VLAN_BASED_NETIF)
			rtl865x_setNetifVid(RTL_DRV_WAN0_NETIF_NAME, 1);
			#endif
		}
	}
	vlanconfig[idx].vid = vlan_id;
	//printk("(%s)vid:%d\n", __FUNCTION__, vlan_id);
	/* update  proto  in "vlanconfig"  */
	vlanconfig[idx].protocol = proto;

	/* sanity check , this netifname does not exist in sw_netif  */
	if (rtl865x_netif_exist(ifname)) {
		/* BUG !  vlanconfig & sw_neif no sync ??  */
		printk("Leave %s @ %d \n", __FUNCTION__, __LINE__);
		return FAILED;
	}

	/* Update VLAN table */
	if (_rtl_update_vlan_table(vlan_id)!=SUCCESS) {
		printk("Leave %s @ %d \n", __FUNCTION__, __LINE__);
		return FAILED;
	}

	/* Update netif table */
	memset(&netif, 0, sizeof(rtl865x_netif_t));
	memcpy(netif.name, ifname, MAX_IFNAMESIZE);
	netif.mtu 			= 1500;
	netif.if_type 		= IF_ETHER;
	netif.vid 			= vlan_id;
	if (proto != SMUX_PROTO_BRIDGE ) {
		netif.is_wan 	= napt;
	} else {
		netif.is_wan 	= 0;
#if defined(CONFIG_RTL_MULTI_ETH_WAN_MAC_BASED)
		netif.if_feature |= IF_FEATURE_IS_BRIDGE;
#endif
	}

	netif.is_slave 		= 0;
	netif.enableRoute	= 1;
	if (rtl865x_addNetif(&netif) != SUCCESS) {
		printk("Leave %s @ %d \n", __FUNCTION__, __LINE__);
		return FAILED;
	}

#if defined(CONFIG_RTL8196E_IPCHECKSUM_ERROR_PATCH) && defined(CONFIG_RTL_LAYERED_DRIVER_ACL)
	/* Trap the brdiged packet with both vlan and pppoe packet  */
	if ((proto == SMUX_PROTO_BRIDGE) && (vid != -1))
	{
		rtl865x_AclRule_t rule;

		//printk("(%s) Add ACL to trap the brdiged unicast packet with both vlan and pppoe tag \n", __FUNCTION__);
		memset(&rule, 0,sizeof(rtl865x_AclRule_t));
		rule.ruleType_			= RTL865X_ACL_MAC;
		rule.actionType_			= RTL865X_ACL_TOCPU;
		rule.pktOpApp_ 			= RTL865X_ACL_ALL_LAYER;
		rule.direction_	 			= RTL865X_ACL_INGRESS;
		rule.un_ty.typeLen_		= 0x8864;
		rule.un_ty.typeLenMask_	= 0xffff;
		if (rtl865x_add_acl(&rule, netif.name, RTL865X_ACL_ICBUG_PATCH, 1, 1)!=SUCCESS)
		{
			//printk("Leave %s @ %d \n", __FUNCTION__, __LINE__);
			return FAILED;
		}
	}
#endif

#if defined(CONFIG_RTL_MULTI_ETH_WAN_VLAN_BASED)
	/* For untagged bridge + untagged route WAN and VLAN-Based setting,
	   because WAN port pvid = 7, untagged routing WAN down-stream packet will lookup NETIF of vid=7 to check DMAC==GMAC.
		jwj: If vlan_id == RTL_BRIDGE_WANVLANID, then this netif is used for bridge with lan and wan port.
			  But now we need set wan port pvid as RTL_BRIDGE_WANVLANID, so if now the untag route wan exist,
			  it will also match this netif, so if this netif GMAC is the same with vid8 netif, then
			  the untag route packet can be trap to CPU by vid7 netif as the same as vid8 netif.
	   */
	/* If this vid =7 , use vid 8's mac addr */
	if (vlan_id == RTL_BRIDGE_WANVLANID) {
		char vid_8_netif_name[30];
		//printk("(%s) this netif's vid =7 , check whether vid 8 existed , use vid 8's mac addr ... \n",__FUNCTION__);

		rtl865x_get_master_netif_by_vid(RTL_WANVLANID, vid_8_netif_name);

		if (strcmp(vid_8_netif_name, "")) {
			rtl865x_netif_local_t* netif_vid_8 = _rtl865x_getNetifByName(vid_8_netif_name);
			rtl865x_netif_t netif_set_mac;
			if (netif_vid_8 == NULL) {
				printk("Leave %s @ %d \n", __FUNCTION__, __LINE__);
				return FAILED;
			}

			memcpy(netif_set_mac.macAddr.octet, netif_vid_8->macAddr.octet, ETHER_ADDR_LEN);
			memcpy(netif_set_mac.name, ifname, MAX_IFNAMESIZE);

			if (rtl865x_setNetifMac(&netif_set_mac) != SUCCESS) {
				//printk("Leave %s @ %d \n",__FUNCTION__,__LINE__);
				return FAILED;
			}
		}
	}
#endif

	/* set pvid */
	_rtl_update_pvid();

	return SUCCESS;
}


/*
	In RTL8676 (it cannot change VID by using ACL rules) , after calling rtl_set_wanport_vlanconfig()
		you should call rtl_set_wanport_portmapping() to set each WAN's binding member ports.
		If the LAN ports binding to more than one Bridged WAN, the LAN port's pvid = 9 (cannot hwacc)

	In RTL8685 , we could use acl to change vid , so we do not care about port-binding relationship
*/
int rtl_set_wanport_portmapping(char *ifname, unsigned int member)
{
	/*jwj: Because 8685 can change vid by acl rule.*/
#if !(defined(CONFIG_RTL8685) || defined(CONFIG_RTL8685S))
	int i;
	int idx=-1;
	#if defined(RTL_BRIDGE_WAN_SUPPORT_PORT_MAPPING)
	unsigned int lan_netif_port_mask = 0;
	unsigned int lan_netif_untag_port_mask = 0;
	#endif

	//printk("Enter %s (ifname:%s  member:0x%X)\n", __FUNCTION__, ifname,member);

	for (i=0; vlanconfig[i].ifname[0] != '\0' ; i++)
	{
		if (vlanconfig[i].isWan && (vlanconfig[i].is_slave==0) && (!strcmp(vlanconfig[i].ifname, ifname))) {
			if (vlanconfig[i].vid!=0) {/* we are really using this wan now */
				idx = i;
				break;
			}
		}
	}

	if (idx < 0) {
		//printk("Leave %s @ %d \n", __FUNCTION__, __LINE__);
		return FAILED;
	}

	//printk("(%s) idx = %d  \n", __FUNCTION__, idx);

	vlanconfig[idx].memPort 	= RTL_WANPORT_MASK;
	vlanconfig[idx].untagSet 	= RTL_WANPORT_MASK;

	if (vlanconfig[idx].protocol == SMUX_PROTO_BRIDGE) {
		//set membership
		if (member == 0xFFFFFFFF) {
			vlanconfig[idx].memPort |= (RTL_LANPORT_MASK | RTL_WANPORT_MASK);
			vlanconfig[idx].untagSet |= (RTL_LANPORT_MASK | RTL_WANPORT_MASK);
		} else {
			vlanconfig[idx].memPort |= (member&RTL_LANPORT_MASK);
			vlanconfig[idx].untagSet |= (member&RTL_LANPORT_MASK);
			/*  protocol == bridge, and vid != RTL_BRIDGE_WANVLANID, set wan port as tagged port.
			  *  add cpu port to bridge vlan group too.
			  */
			vlanconfig[idx].memPort |= (1<<RTL_CPU_PORT);
			vlanconfig[idx].untagSet |= (1<<RTL_CPU_PORT);
			if (((vlanconfig[idx].vid != RTL_BRIDGE_WANVLANID))) {
				vlanconfig[idx].untagSet &= (~RTL_WANPORT_MASK);
				vlanconfig[idx].untagSet &= (~(1<<RTL_CPU_PORT));
				//panic_printk("(%s:%d) idx = %d	memport=0x%x untag=0x%x ifname=%s member=0x%x\n", __FUNCTION__,__LINE__, idx, vlanconfig[idx].memPort, vlanconfig[idx].untagSet, ifname, member);
			}
			#if defined(RTL_BRIDGE_WAN_SUPPORT_PORT_MAPPING)
			rtl865x_modVlanPortMember(vlanconfig[idx].vid, vlanconfig[idx].memPort, vlanconfig[idx].untagSet);
			/*jwj: Delete wan bridge bind lan port from br0 port mask.now all lan neitf
			port are untag.*/
			lan_netif_port_mask = rtl865x_getVlanPortMask(RTL_LANVLANID);
			lan_netif_untag_port_mask = rtl865x_getVlanPortMask(RTL_LANVLANID);
			rtl865x_modVlanPortMember(RTL_LANVLANID, lan_netif_port_mask&(~member), lan_netif_untag_port_mask&(~member));
			#endif
		}
	}

	if (((vlanconfig[idx].vid != RTL_BRIDGE_WANVLANID) && (vlanconfig[idx].vid != RTL_WANVLANID))
		#ifdef UNIQUE_MAC_PER_DEV
		&& (vid > 8)
		#endif
		)
	{
		vlanconfig[idx].untagSet &= (~RTL_WANPORT_MASK);
	}

	/* set pvid */
	_rtl_update_pvid();
#endif

	return SUCCESS;

}



int rtl865x_unregisterDev(char *ifname)
{
	int idx=-1;
	int i;
	int vlan_id=-1;

	//printk("Enter %s (ifname:%s)\n",__func__,ifname);

	for(i=0; vlanconfig[i].ifname[0] != '\0' ; i++)
	{
		if( vlanconfig[i].isWan&& vlanconfig[i].is_slave==0 && !strcmp(vlanconfig[i].ifname,ifname))
		{
			if(vlanconfig[i].vid!=0) /* we are really using this wan now */
			{
				idx = i;
				vlan_id = vlanconfig[i].vid;
				break;
			}
		}
	}
	if (idx <0)
	{
		printk("Leave %s @ %d \n",__func__,__LINE__);
		return FAILED;
	}

#if defined(CONFIG_RTL_VLAN_BASED_NETIF)
	/*jwj: When delete untag wan, please set the default wan netif to RTL_WANVLANID.*/
	if (vlan_id == RTL_WANVLANID)
		rtl865x_setNetifVid(RTL_DRV_WAN0_NETIF_NAME, RTL_WANVLANID);
#endif

#if defined(RTL_BRIDGE_WAN_SUPPORT_PORT_MAPPING)
	if ((vlanconfig[idx].protocol==SMUX_PROTO_BRIDGE) && (vlanconfig[idx].memPort != (RTL_LANPORT_MASK |RTL_WANPORT_MASK))) {
		/*jwj: Add wan bridge bind lan port to br0 port mask.now all lan neitf
			port are untag.*/
		unsigned int lan_netif_port_mask = rtl865x_getVlanPortMask(RTL_LANVLANID);
		unsigned int lan_netif_untag_port_mask = rtl865x_getVlanPortMask(RTL_LANVLANID);

		rtl865x_modVlanPortMember(RTL_LANVLANID, lan_netif_port_mask |(vlanconfig[idx].memPort&(~RTL_WANPORT_MASK)),
			lan_netif_untag_port_mask |(vlanconfig[idx].memPort&(~RTL_WANPORT_MASK)));
	}
#endif

	/* Update vlanconfig (reassign vid back to 0) */
	vlanconfig[idx].isWan 		= 1;
	vlanconfig[idx].if_type 	= IF_ETHER;
	vlanconfig[idx].vid 		= 0;
	vlanconfig[idx].fid 		= RTL_WAN_FID;
	vlanconfig[idx].memPort 	= RTL_WANPORT_MASK;
	vlanconfig[idx].untagSet 	= RTL_WANPORT_MASK;
	vlanconfig[idx].is_slave 	= 0;


	/* sanity check , this netifname exists in sw_netif actually */
	if(!rtl865x_netif_exist(ifname))
	{
		/* BUG !  vlanconfig & sw_neif no sync ??  */
		printk("Leave %s @ %d \n",__func__,__LINE__);
		return FAILED;
	}


	/* Update netif table */
	if(rtl865x_delNetif(ifname)!=SUCCESS)
	{
		printk("Leave %s @ %d \n",__func__,__LINE__);
		return FAILED;
	}

	/* Update VLAN table */
	if(_rtl_update_vlan_table(vlan_id)!=SUCCESS)
	{
		printk("Leave %s @ %d \n",__func__,__LINE__);
		return FAILED;
	}

	/* set pvid */
	_rtl_update_pvid();

	return SUCCESS;

}

int rtl865x_setNetifMacAddr(char *ifname, unsigned char *addr)
{
	int vid;
	rtl865x_netif_t netif;

	//printk("Enter %s (ifname:%s   addr:%02X:%02X:%02X:%02X:%02X:%02X:) \n", __FUNCTION__, ifname,
	//	addr[0],addr[1],addr[2],addr[3],addr[4],addr[5]);

	memcpy(netif.macAddr.octet, addr, ETHER_ADDR_LEN);
	memcpy(netif.name, ifname, MAX_IFNAMESIZE);
	if (rtl865x_setNetifMac(&netif) != SUCCESS) {
		printk("Leave %s @ %d \n", __FUNCTION__, __LINE__);
		return FAILED;
	}

	vid=0;
	/*jwj: If update vid8 netif mac, we need update vid7 netif mac, the reason
			please see rtl_set_wanport_vlanconfig.*/
#if defined(CONFIG_RTL_MULTI_ETH_WAN_VLAN_BASED)
	if (rtl865x_getNetifVid(ifname, &vid) == SUCCESS) {
		if (vid == RTL_WANVLANID) {
			char vid_7_netif_name[30];
			//printk("(%s) this netif's vid =8 ,  set vid 7 either... \n",__FUNCTION__);

			rtl865x_get_master_netif_by_vid(RTL_BRIDGE_WANVLANID, vid_7_netif_name);

			if (strcmp(vid_7_netif_name, "")) {
				memcpy(netif.name, vid_7_netif_name, MAX_IFNAMESIZE);
				if (rtl865x_setNetifMac(&netif) != SUCCESS) {
					//printk("Leave %s @ %d \n",__FUNCTION__,__LINE__);
					return FAILED;
				}
			}
		}
	} else {
		//printk("Leave %s @ %d \n",__FUNCTION__,__LINE__);
		return FAILED;
	}
#endif

	return SUCCESS;
}

/*
 *	Function Name:	rtl_register_multi_wan_dev
 *	Description:	set netif to vlanconfig
 *
 *	Input:		ifname		:	network interface name
 *				proto		:	0: pppoe, 1: ipoe, 2: bridge
 *				vid			:	vlan id
 *				napt			:	0: not support napt, 1: support napt
 *
 *	Return:		SUCCESS	:	set hwnat successfully
 * 				FAILED		:	set hwnat failed
 */

int rtl_register_multi_wan_dev(char *ifname, int proto, int vid, int napt)
{
	return rtl_set_wanport_vlanconfig(ifname, proto, vid, napt);
}

/*
 *	Function Name:	rtl_update_port_mapping_multi_wan_dev
 *	Description:	set group member of netif to vlanconfig
 *
 *	Input:		ifname		:	network interface name
 *				member 	:	network interface group member
 *
 *	Return:		SUCCESS	:	set 8676hwnat successfully
 *				FAILED		:	set 8676hwnat failed
 */

int rtl_update_port_mapping_multi_wan_dev(char *ifname,unsigned int member)
{
	return rtl_set_wanport_portmapping(ifname,member);
}

/*
 *	Function Name:	rtl_unregister_multi_wan_dev
 *	Description:	reset vlanconfig & netif/acl in 8676 hwnat switch
 *
 *	Input:		ifname		:	network interface name
 *
 *	Return:		SUCCESS	:	set 8676hwnat successfully
 *				FAILED		:	set 8676hwnat failed
 */

int rtl_unregister_multi_wan_dev(char *ifname)
{
	return rtl865x_unregisterDev(ifname);
}

/*
 *	Function Name:	rtl8676_setNetifMacAddr
 *	Description:	update netif mac address in 8676 hwnat switch
 *
 *	Input:		ifname		:	network interface name
 *				addr			:	mac address
 *
 *	Return:		SUCCESS	:	set 8676hwnat successfully
 *				FAILED		:	set 8676hwnat failed
 */

int rtl_set_multi_wan_netif_mac_addr(char *ifname, char *addr)
{
	return rtl865x_setNetifMacAddr(ifname, addr);
}

#if defined(CONFIG_RTL_HARDWARE_NAT)
/*jwj: ?????*/
int set_port_mapping(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct ifvlan *ifv = rq->ifr_data;

	printk("port mapping %s: dev %s member:0x%x\n", ifv->enable?"enable":"disable", dev->name, ifv->member);
	/*
	 * jwj port mapping mechanism: all packets are trapped to cpu by default, if it pass through firewall and port mapping rule, then
	 * acl rule will be written to asic table and portmapping will take effect.
	 */
	if ((ifv->enable == 0) && enable_port_mapping)
	{
		enable_port_mapping = 0;
		//printk("rtl865x_syncRouteToAsic\n");
		rtl865x_syncRouteToAsic();
	}
	else if ((ifv->enable == 1) && !enable_port_mapping)
	{
		enable_port_mapping = 1;
		/* clear routing asic table. */
				//printk("rtl865x_clearAsicRoutingTable\n");
		rtl865x_clearAsicRoutingTable();
	}

#ifdef CONFIG_RTL867X_IPTABLES_FAST_PATH
	/* reset fastpath */
	rtl867x_clearFastPathEntry();
#endif

	/* reset hw-acc */
	#ifdef CONFIG_RTL8676_Static_ACL
		rtl865x_nat_init();
	#else /* CONFIG_RTL8676_Dynamic_ACL */
		rtl8676_clean_L34Unicast_hwacc();
	#endif

	return 0;
}
#endif

#if defined(CONFIG_RTL_PPPOE_HWACC)
int32 rtl_add_ppp_netif(char *ifname)
{
	struct rtl865x_netif_s netif;
	int idx = -1;
	int i;
	int retval;

	if (strncmp(ifname, "ppp", 3))
		return FAILED;

	for (i=0; vlanconfig[i].ifname[0] != '\0'; i++)
	{
		if (vlanconfig[i].isWan && vlanconfig[i].is_slave && (!strcmp(vlanconfig[i].ifname, ifname))) {
			idx = i;
			break;
		}
	}

	if (-1 == idx) {
		printk("%s: no vlanconfig entry availble for %s!\n", __FUNCTION__, ifname);
		return FAILED;
	}

	/*add network interface*/
	memset(&netif, 0, sizeof(rtl865x_netif_t));
	memcpy(netif.name, vlanconfig[idx].ifname, MAX_IFNAMESIZE);
	memcpy(netif.macAddr.octet, vlanconfig[idx].mac.octet, ETHER_ADDR_LEN);
	netif.mtu = vlanconfig[idx].mtu;
	netif.if_type = vlanconfig[idx].if_type;
	netif.vid = vlanconfig[idx].vid;
	netif.is_wan = vlanconfig[idx].isWan;
	netif.is_slave = vlanconfig[idx].is_slave;
	//#if defined (CONFIG_RTL_HARDWARE_MULTICAST)
	netif.enableRoute=1;
	//#endif
	retval = rtl865x_addNetif(&netif);

	return retval;
}
#endif

#endif
#if	defined (CONFIG_PPPOE_VLANTAG)
static struct vlan_info PPPOE_vlanInfo;

struct vlan_info * rtl_get_PPPOE_vlanInfo(void)
{
	return &PPPOE_vlanInfo;
}
static int read_PPPOE_proc_vlan(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{

	int len;



	len = sprintf(page, "vlan=%d, tag=%d, vid=%d, priority=%d, cfi=%d\n",PPPOE_vlanInfo.vlan,
		PPPOE_vlanInfo.tag,PPPOE_vlanInfo.id, PPPOE_vlanInfo.pri, PPPOE_vlanInfo.cfi);

	if (len <= off+count)
		*eof = 1;
	*start = page + off;
	len -= off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;
	return len;
}

static int write_PPPOE_proc_vlan(struct file *file, const char *buffer,
			  unsigned long count, void *data)
{
	#define	VLAN_MAX_INPUT_LEN	128
	char *tmp;

	tmp = kmalloc(VLAN_MAX_INPUT_LEN, GFP_KERNEL);
	if (count < 2 || tmp==NULL)
		goto out;

	memset(&PPPOE_vlanInfo,0,sizeof(struct vlan_info));
	if (buffer && !copy_from_user(tmp, buffer, VLAN_MAX_INPUT_LEN))
	{

			int num = sscanf(tmp, "%d %d %d %d %d",

			&PPPOE_vlanInfo.vlan,
			&PPPOE_vlanInfo.tag,
			&PPPOE_vlanInfo.id,
			&PPPOE_vlanInfo.pri,
			&PPPOE_vlanInfo.cfi
			);


		if (num !=5)
		{
			printk("invalid vlan parameter!\n");
			goto out;
		}


		#if 0
		printk("===%s(%d), global_vlan(%d),is_lan(%d),vlan(%d),tag(%d),id(%d),pri(%d),cfi(%d)",__FUNCTION__,__LINE__,
			PPPOE_vlanInfo.global_vlan,PPPOE_vlanInfo.is_lan,PPPOE_vlanInfo.vlan,PPPOE_vlanInfo.tag,
			PPPOE_vlanInfo.id,PPPOE_vlanInfo.pri,PPPOE_vlanInfo.cfi);
		#endif
	}
	if(PPPOE_vlanInfo.vlan)
	{
		WRITE_MEM32(SWTCR0,(READ_MEM32(SWTCR0)| EnUkVIDtoCPU));
	}
out:
	if(tmp)
		kfree(tmp);

	return count;
}

#endif

#if defined(CONFIG_RTL_WTDOG)

#if defined(CONFIG_RTL_8198B)
#define _WDTCNR_			BSP_WDTCNTRR
#else
#define _WDTCNR_			WDTCNR
#endif

#define _WDTKICK_			WDTCLR

void rtl_watchdog_kick(void)
{
	*((volatile unsigned long *)_WDTCNR_) |= _WDTKICK_;
}

void rtl_periodic_watchdog_kick(unsigned int count, unsigned int times)
{

	if ((count % times) == 0){
		rtl_watchdog_kick();
	}

}
#endif



