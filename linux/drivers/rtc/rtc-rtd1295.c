#include <linux/fs.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/rtc.h>

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include "rtc-rtd1295.h"

#define REG_RTCSEC			0x00
#define REG_RTCMIN			0x04
#define REG_RTCHR			0x08
#define REG_RTCDATE_LOW			0x0C
#define REG_RTCDATE_HIGH		0x10
#define REG_ALARMMIN			0x14
#define REG_ALARMHR			0x18
#define REG_ALARMDATE_LOW		0x1C
#define REG_ALARMDATE_HIGH		0x20
#define REG_RTCSTOP			0x24
#define REG_RTCACR			0x28
#define REG_RTCEN			0x2C
#define REG_RTCCR			0x30

#define REG_ISO_ISR             	0x00
#define REG_ISO_RTC             	0x34

#define LEAPS_THRU_END_OF(y) ((y)/4 - (y)/100 + (y)/400)

static	void __iomem *rtk_rtc_base;
static	void __iomem *rtk_rbus_base;
static	void __iomem *rtk_iso_base;

static	long rtk_base_year;

DEFINE_SPINLOCK(rtk_rtc_lock);

static void rtk_rtc_check_rtcacr(struct device *dev) 
{
	unsigned long flags;
	unsigned int val;

	spin_lock_irqsave(&rtk_rtc_lock, flags);

	val = readl(rtk_rtc_base + REG_RTCACR);
        dev_info(dev, "rtk_rtc rtcacr = 0x%x", val);

	
	if ((val &0x80) == 0x80) {
		spin_unlock_irqrestore(&rtk_rtc_lock, flags);
		return ;
	}

	dev_info(dev, "rtk_rtc set rtcacr");
	writel(0x80, rtk_rtc_base + REG_RTCACR);

	/* we set sefault 0 , 0 , 0 , 0 */
	writel(0x40, rtk_rtc_base + REG_RTCCR);
	writel(0x0, rtk_rtc_base + REG_RTCCR);
	writel(0, rtk_rtc_base + REG_RTCMIN);
	writel(0, rtk_rtc_base + REG_RTCHR);
	writel(0, rtk_rtc_base + REG_RTCDATE_LOW);
	writel(0, rtk_rtc_base + REG_RTCDATE_HIGH);

	dev_info(dev, "rtcacr 2 REG_SEC = 0x%x \n", readl(rtk_rtc_base + REG_RTCSEC));
	dev_info(dev, "rtcacr 2 REG_MIN = 0x%x \n", readl(rtk_rtc_base + REG_RTCMIN));
	dev_info(dev, "rtcacr 2 REG_HR = 0x%x \n", readl(rtk_rtc_base + REG_RTCHR));
	dev_info(dev, "rtcacr 2 REG_DATA_LOW = 0x%x \n", readl(rtk_rtc_base + REG_RTCDATE_LOW));
	dev_info(dev, "rtcacr 2 REG_DATE_HIGH = 0x%x \n", readl(rtk_rtc_base + REG_RTCDATE_HIGH));

	spin_unlock_irqrestore(&rtk_rtc_lock, flags);
}

static void rtk_rtc_enable(struct device *dev, int en)
{
	unsigned long flags;

	if (rtk_rtc_base == NULL)
		return;

	spin_lock_irqsave(&rtk_rtc_lock, flags);

	if (!en) {
		dev_err(dev, "rtk_rtc_disable");
		writel(0x00, rtk_rtc_base + REG_RTCEN);

	} else if ( (readl(rtk_rtc_base + REG_RTCEN) & 0xff) != 0x5A ) {
		dev_err(dev, "rtk_rtc_enable");
		writel(0x5A, rtk_rtc_base + REG_RTCEN);
	} else
		dev_err(dev, "rtk_rtc already enabled ");

	dev_info(dev, "enable REG_SEC = 0x%x \n", readl(rtk_rtc_base + REG_RTCSEC));
	dev_info(dev, "enable REG_MIN = 0x%x \n", readl(rtk_rtc_base + REG_RTCMIN));
	dev_info(dev, "enable REG_HR = 0x%x \n", readl(rtk_rtc_base + REG_RTCHR));
	dev_info(dev, "enable REG_DATA_LOW = 0x%x \n", readl(rtk_rtc_base + REG_RTCDATE_LOW));
	dev_info(dev, "enable REG_DATE_HIGH = 0x%x \n", readl(rtk_rtc_base + REG_RTCDATE_HIGH));

	spin_unlock_irqrestore(&rtk_rtc_lock, flags);
}

static void venus_rtc_alarm_aie_enable(int state)
{
	unsigned long flags;

	spin_lock_irqsave(&rtk_rtc_lock, flags);
	if(state) {
		writel(0x2000, rtk_iso_base + REG_ISO_ISR);
		writel(0x1, rtk_iso_base + REG_ISO_RTC);
	} else
		writel(0, rtk_iso_base + REG_ISO_RTC);

	spin_unlock_irqrestore(&rtk_rtc_lock, flags);
}

static int venus_rtc_alarm_aie_state(void)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&rtk_rtc_lock, flags);
	ret = readl(rtk_iso_base + REG_ISO_RTC) && 0x1;
	spin_unlock_irqrestore(&rtk_rtc_lock, flags);
	
	return ret;
}

static void venus_rtc_read_alarm_persistent_clock(struct timespec *ts)
{
	int day, hour, min;
	unsigned long flags;

	if(venus_rtc_alarm_aie_state()) {
		spin_lock_irqsave(&rtk_rtc_lock, flags);
		min = readl(rtk_rtc_base + REG_ALARMMIN);
		hour = readl(rtk_rtc_base + REG_ALARMHR);
		day = readl(rtk_rtc_base + REG_ALARMDATE_LOW);
		day += readl(rtk_rtc_base + REG_ALARMDATE_HIGH) << 8;
		spin_unlock_irqrestore(&rtk_rtc_lock, flags);
	} else {
		min = 0;
		hour = 0;
		day = 0;
	}
	ts->tv_sec = ((day*24 + hour)*60 + min)*60 + mktime(rtk_base_year + 1900, 1, 1, 0, 0, 0);
	ts->tv_nsec = 0;
}

static int venus_rtc_set_alarm_mmss(unsigned long nowtime)
{
	unsigned long flags;
	int day, hour, min, hms;
	unsigned long off_sec, base_sec = mktime(rtk_base_year + 1900, 1, 1, 0, 0, 0);

	off_sec = nowtime - base_sec;
	if(base_sec > nowtime) {
		printk("RTC alarm set time error! The time cannot be set to the date before year %ld.\n", rtk_base_year);
		return -EINVAL;
	}

	day = off_sec / (24*60*60);
	hms = off_sec % (24*60*60);
	hour = hms / 3600;
	min = (hms % 3600) / 60;

	if(day > 16383) {
		printk("RTC alarm day field overflow.\n");
		return -EINVAL;
	}

	/* irq are locally disabled here, but I still like to use spin_lock_irqsave */
	spin_lock_irqsave(&rtk_rtc_lock, flags);
	writel(min, rtk_rtc_base + REG_ALARMMIN);
	writel(hour, rtk_rtc_base + REG_ALARMHR);
	writel(day & 0x00ff, rtk_rtc_base + REG_ALARMDATE_LOW);
	writel((day & 0x3f00) >> 8, rtk_rtc_base + REG_ALARMDATE_HIGH);
	spin_unlock_irqrestore(&rtk_rtc_lock, flags);

	return 0;
}

static void rtk_read_persistent_clock(struct device *dev, struct timespec *ts)
{
	unsigned int retried = 0;
	unsigned int day, hour, min, sec;
	unsigned long flags;

	spin_lock_irqsave(&rtk_rtc_lock, flags);
retry:
	sec = readl(rtk_rtc_base + REG_RTCSEC) >> 1;	// One unit represents half second
	min = readl(rtk_rtc_base + REG_RTCMIN);
	hour = readl(rtk_rtc_base + REG_RTCHR);
	day = readl(rtk_rtc_base + REG_RTCDATE_LOW);
	day += readl(rtk_rtc_base + REG_RTCDATE_HIGH)<<8;
	dev_info(dev,"sec=0x%x , min=0x%x . day=0x%x \n", sec, min, day); //hcy test 

	if (sec == 0 && !retried) {
		retried++;
		goto retry;
	}
	spin_unlock_irqrestore(&rtk_rtc_lock, flags);
	ts->tv_sec = ((day*24 + hour)*60 + min)*60 + sec + mktime(rtk_base_year + 1900, 1, 1, 0, 0, 0);
	ts->tv_nsec = 0;
}

static int rtc_mips_set_mmss(struct device *dev , unsigned long nowtime)
{
	unsigned long flags;
	int day, hour, min, sec, hms;
	unsigned long off_sec, base_sec = mktime(rtk_base_year + 1900, 1, 1, 0, 0, 0);

	off_sec = nowtime - base_sec;
	if(base_sec > nowtime) {
		dev_err(dev, "RTC set time error! The time cannot be set to the date before year %ld.\n", rtk_base_year + 1900);
		return -EINVAL;
	}

	day = off_sec / (24*60*60);
	hms = off_sec % (24*60*60);
	hour = hms / 3600;
	min = (hms % 3600) / 60;
	sec = ((hms % 3600) % 60) * 2;	// One unit represents half second.

	if(day > 16383) {
		dev_err(dev, "RTC day field overflow....\n");
		return -EINVAL;
	}
	rtk_rtc_enable(dev, 0);

	/* irq are locally disabled here, but I still like to use spin_lock_irqsave */
	spin_lock_irqsave(&rtk_rtc_lock, flags);

	writel(sec, rtk_rtc_base + REG_RTCSEC);
	writel(min, rtk_rtc_base + REG_RTCMIN);
	writel(hour, rtk_rtc_base + REG_RTCHR);
	writel((day & 0xFF), rtk_rtc_base + REG_RTCDATE_LOW);
	writel(((day >> 8) & 0x7F), rtk_rtc_base + REG_RTCDATE_HIGH);

	spin_unlock_irqrestore(&rtk_rtc_lock, flags);

	rtk_rtc_enable(dev, 1);

	return 0;
}

/*****************************************************************************************************
 **	rtc_class_ops :
 **
 *****************************************************************************************************/
static int rtk_rtc_gettime(struct device *dev, struct rtc_time *tm)
{
	struct timespec ts;

	rtk_read_persistent_clock(dev, &ts);
	rtc_time_to_tm(ts.tv_sec, tm);
	dev_info(dev,"time read as %04d.%02d.%02d %02d:%02d:%02d",
															   1900+tm->tm_year,
															   tm->tm_mon,
															   tm->tm_mday,
															   tm->tm_hour,
															   tm->tm_min,
															   tm->tm_sec);

	return rtc_valid_tm(tm);
}

static int rtk_rtc_settime(struct device *dev, struct rtc_time *tm)
{
	unsigned long cur_sec;

	dev_info(dev,"set time %04d.%02d.%02d %02d:%02d:%02d", 1900+tm->tm_year, tm->tm_mon, tm->tm_mday,
					tm->tm_hour, tm->tm_min, tm->tm_sec);

	cur_sec = mktime(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	rtc_mips_set_mmss(dev, cur_sec);

	return 0;
}

static int rtk_rtc_getalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct timespec ts;

	venus_rtc_read_alarm_persistent_clock(&ts);
	rtc_time_to_tm(ts.tv_sec, &alrm->time);
	alrm->enabled = venus_rtc_alarm_aie_state();

	return 0;
}

static int rtk_rtc_setalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	unsigned long cur_sec;

	venus_rtc_alarm_aie_enable(0);
	cur_sec = mktime(alrm->time.tm_year + 1900, alrm->time.tm_mon + 1, alrm->time.tm_mday,
			alrm->time.tm_hour, alrm->time.tm_min, alrm->time.tm_sec);
	venus_rtc_set_alarm_mmss(cur_sec);
	if(alrm->enabled)
		venus_rtc_alarm_aie_enable(1);

	return 0;
}

static int rtk_rtc_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case RTC_AIE_ON:
		venus_rtc_alarm_aie_enable(1); 
		break;
	case RTC_AIE_OFF:
		venus_rtc_alarm_aie_enable(0);
		break;
	default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

static const struct rtc_class_ops rtk_rtcops = {
	.read_time = rtk_rtc_gettime,
	.set_time = rtk_rtc_settime,
//	.read_alarm = rtk_rtc_getalarm,
//	.set_alarm = rtk_rtc_setalarm,
	.ioctl	= rtk_rtc_ioctl,
//	.proc = rtk_rtc_proc,
//	.alarm_irq_enable = rtk_rtc_setaie,
};

static int rtk_rtc_probe(struct platform_device *pdev)
{
	struct rtc_device *rtc;
	int ret;
	const u32 *prop;

	dev_info(&pdev->dev, "%s %d",__FUNCTION__, __LINE__);

	prop = of_get_property(pdev->dev.of_node, "rtc-base-year", NULL);
	if (prop)
		rtk_base_year = of_read_number(prop, 1);
	else 
		rtk_base_year = 1900; 

	dev_info(&pdev->dev,"early rtk_base_year = %ld \n", rtk_base_year );//hcy test
	rtk_base_year -= 1900;

	rtk_rtc_base = of_iomap(pdev->dev.of_node, 0);
	rtk_rbus_base = of_iomap(pdev->dev.of_node, 1);
	rtk_iso_base = of_iomap(pdev->dev.of_node, 2);
	dev_info(&pdev->dev,"early rtk_rtc_base = 0x%llx \n", (u64)rtk_rtc_base );//hcy test
	dev_info(&pdev->dev,"early rtk_iso_base = 0x%llx \n", (u64)rtk_iso_base );//hcy test

	writel(readl(rtk_rbus_base + 0x4) & ~BIT(20), rtk_rbus_base + 0x4);
	writel(readl(rtk_rbus_base + 0x10) | BIT(10), rtk_rbus_base + 0x10);
	
	rtk_rtc_check_rtcacr(&pdev->dev); 
	rtk_rtc_enable(&pdev->dev, 1);

	dev_info(&pdev->dev, "reset bit 0x%x \n", readl(rtk_rbus_base + 0x4));
	dev_info(&pdev->dev, "clock bit 0x%x \n", readl(rtk_rbus_base + 0x10));

/******* test below ******/
#if 0    
	{
		struct rtc_time tm ;
		tm.tm_year = 115;  
		tm.tm_mon = 2;
		tm.tm_mday = 28;			
		tm.tm_hour = 12;
		tm.tm_min = 0;
		tm.tm_sec = 1;

		rtk_rtc_settime(&pdev->dev, &tm);

	}

	{
		struct rtc_wkalrm alrm;
		alrm.time.tm_year = 116;  
		alrm.time.tm_mon = 2;
		alrm.time.tm_mday = 29;			
		alrm.time.tm_hour = 13;
		alrm.time.tm_min = 1;
		alrm.time.tm_sec = 2;

		dev_err(&pdev->dev,"alrm set as %04d.%02d.%02d %02d:%02d:%02d", 1900+alrm.time.tm_year, alrm.time.tm_mon, alrm.time.tm_mday,
			alrm.time.tm_hour, alrm.time.tm_min, alrm.time.tm_sec);

		rtk_rtc_setalarm(&pdev->dev, &alrm);


	}

	{
		struct rtc_wkalrm alrm;
		rtk_rtc_getalarm(&pdev->dev, &alrm);

		dev_err(&pdev->dev,"alrm read as %04d.%02d.%02d %02d:%02d:%02d", 1900+alrm.time.tm_year, alrm.time.tm_mon, alrm.time.tm_mday,
			alrm.time.tm_hour, alrm.time.tm_min, alrm.time.tm_sec);


	}
#endif
/************/

	device_init_wakeup(&pdev->dev, true);

	rtc = devm_rtc_device_register(&pdev->dev, "rtc", &rtk_rtcops, THIS_MODULE);
	if (IS_ERR(rtc)) {
		pr_err("cannot attach rtc");
		ret = PTR_ERR(rtc);
		goto err_nortc;
	}

	platform_set_drvdata(pdev, rtc);
	
	return 0;

err_nortc:
	return ret;
}

static int  rtk_rtc_remove(struct platform_device *pdev)
{
	struct rtc_device *rtc = platform_get_drvdata(pdev);

	dev_info(&pdev->dev,"%s %s",__FILE__, __func__);
	devm_rtc_device_unregister(&pdev->dev, rtc);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id rtk_rtc_ids[] = {
	{ .compatible = "Realtek,rtk-rtc" },
	{},
};
MODULE_DEVICE_TABLE(of, rtd1295_watchdog_match);

static struct platform_driver rtk_rtc_driver = {
	.probe = rtk_rtc_probe,
	.remove = rtk_rtc_remove,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table	= rtk_rtc_ids,
	},
};

module_platform_driver(rtk_rtc_driver);
