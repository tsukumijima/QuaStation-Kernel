#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include <linux/miscdevice.h>
#include <linux/platform_device.h>	
#include <linux/device.h>
#include <linux/suspend.h>

#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>

#include "hdmitx.h"
#include "hdmitx_dev.h"
#include "hdmitx_api.h"
#include "hdmitx_rpc.h"
#include "hdmitx_scdc.h"

#include "compat_hdmitx.h"


static int __init rtk_hdmi_init(void);
static void __exit rtk_hdmi_exit(void);

static asoc_hdmi_t hdmi_data;
static hdmitx_device_t tx_dev; 
extern unsigned int tmds_en; 

int hdmitx_open(struct inode *inode, struct file *filp)
{	
	if (nonseekable_open(inode, filp))
		return -ENODEV;
	
	filp->private_data = &tx_dev;
	
	return 0; 	
}

/*------------------------------------------------------------------
 * Func : hdmitx_ioctl
 *
 * Desc : ioctl function of hdmitx miscdev
 *
 * Parm : file  : context of file
 *        cmd   : control command
 *        arg   : arguments
 *         
 * Retn : 0 : success, others fail  
 *------------------------------------------------------------------*/
static long hdmitx_ioctl(struct file* file,unsigned int cmd, unsigned long arg)
{    
   
    hdmitx_device_t* dev = (hdmitx_device_t*) file->private_data;               
	asoc_hdmi_t* tx_data;
		
    if (!dev)
        return -ENODEV;
      
    tx_data = hdmitx_get_drvdata(dev);	
	
	switch (cmd) {	
	case HDMI_GET_SINK_CAPABILITY:	
		return ops_get_sink_cap((void __user *)arg,tx_data);
			
	case HDMI_GET_RAW_EDID:	
		return ops_get_raw_edid((void __user *)arg,tx_data);	
		
	case HDMI_CHECK_LINK_STATUS:
		return ops_get_link_status((void __user *)arg);
				
	case HDMI_GET_VIDEO_CONFIG:
		return ops_get_video_config((void __user *)arg,tx_data);
	
	case HDMI_SEND_AVMUTE:
		return ops_send_AVmute((void __user *)arg, dev -> reg_base);

	case HDMI_CONFIG_TV_SYSTEM:
		return ops_config_tv_system((void __user *)arg);
	
	case HDMI_CONFIG_AVI_INFO:
		return ops_config_avi_info((void __user *)arg);		
	
	case HDMI_SET_FREQUNCY:
		return ops_set_frequency((void __user *)arg);
		
	case HDMI_SEND_AUDIO_MUTE:		
		return ops_set_audio_mute((void __user *)arg);	
	
	case HDMI_SEND_AUDIO_VSDB_DATA:	
		return ops_send_audio_vsdb_data((void __user *)arg);
	
	case HDMI_SEND_AUDIO_EDID2:	
		return ops_send_audio_edid2((void __user *)arg);	

	case HDMI_CHECK_TMDS_SRC:	
		return ops_check_tmds_src((void __user *)arg, dev -> reg_base);	
		
	case HDMI_CHECK_Rx_Sense:	
		return ops_check_rx_sense((void __user *)arg, dev -> reg_base);	

	case HDMI_GET_EXT_BLK_COUNT:
		return ops_get_extension_blk_count((void __user *)arg,tx_data);
		
	case HDMI_GET_EXTENDED_EDID:	
		return ops_get_extended_edid((void __user *)arg,tx_data);	

	case HDMI_QUERY_DISPLAY_STANDARD:	
		return ops_query_display_standard((void __user *)arg);		

	case HDMI_SEND_VOUT_EDID_DATA:
		return ops_send_vout_edid_data((void __user *)arg);

    default:    
        HDMI_DEBUG(" Unknown ioctl cmd %08x", cmd);
        return -EFAULT;          
    }		
}
/*------------------------------------------------------------------
 * hdmi misc device
 *------------------------------------------------------------------*/

static struct file_operations hdmitx_fops = {
        .owner 			= THIS_MODULE,
		.open	    	= hdmitx_open,
        .unlocked_ioctl = hdmitx_ioctl,
        .compat_ioctl	= compat_hdmitx_ioctl,
		.poll	    	= NULL,
};

/*------------------------------------------------------------------
 * Func : register_hdmitx_miscdev
 * Desc : register hdmitx miscdev
 * Parm : hdmitx device : hdmitx miscdev to be registered
 * Retn : 0
 *------------------------------------------------------------------*/
int register_hdmitx_miscdev(hdmitx_device_t* device)
{
    struct miscdevice *miscdev = &device->miscdev;
           
	miscdev->minor = MISC_DYNAMIC_MINOR;
    miscdev->name = "hdmitx";
    miscdev->mode = 0666;
    miscdev->fops = &hdmitx_fops;                  
    
    return misc_register(miscdev);
}

int deregister_hdmitx_miscdev(hdmitx_device_t* device)
{
    return misc_deregister(& device->miscdev);
}

static int rtk_hdmitx_suspend(struct device *dev)
{
	int ret_val;
	HDMI_INFO("Enter %s",__FUNCTION__);

	if(RTK_PM_STATE == PM_SUSPEND_STANDBY)// Idle mode
	{
		ret_val = 0;
		HDMI_INFO("Idle mode, do nothing");
	}
	else
	{
		ret_val = rtk_hdmitx_switch_suspend();
		hdmitx_scdcrr_suspend();
	}

	HDMI_INFO("Exit %s",__FUNCTION__);
	return ret_val;
}
 
 static int rtk_hdmitx_resume(struct device *dev)
{
	int ret_val;
	HDMI_INFO("Enter %s",__FUNCTION__);

	if(RTK_PM_STATE == PM_SUSPEND_STANDBY)// Idle mode
	{
		ret_val = 0;
		HDMI_INFO("Idle mode, do nothing");
	}
	else
	{
		hdmitx_scdcrr_resume();
		ret_val = rtk_hdmitx_switch_resume();
	}

	HDMI_INFO("Exit %s",__FUNCTION__);
	return ret_val;
}

static DEVICE_ATTR(hdmitx_info, S_IRUGO, show_hdmitx_info, NULL);
static DEVICE_ATTR(edid_info, S_IRUGO, show_edid_info, NULL);

static int rtk_hdmi_probe(struct platform_device *pdev)
{
	unsigned int module_en = 0;

	HDMI_INFO("Driver init");

    if(check_hdmi_mhl_mode())
		return 0;
	
    tx_dev.reg_base = of_iomap(pdev->dev.of_node, 0);

	if(!tx_dev.reg_base)
	{
		HDMI_ERROR("Can't map HDMI Tx registers");
		goto end;
	}

	if (register_hdmitx_miscdev(&tx_dev)) {
        HDMI_ERROR("Could not register_hdmitx_miscdev");
        goto end;
    }

    hdmi_data.edid_ptr = NULL;
	hdmitx_reset_sink_capability(&hdmi_data);	
	hdmitx_set_drvdata(&tx_dev, &hdmi_data);
		
	/* Initial hotplug gpio */
	tx_dev.hpd_gpio = of_get_gpio(pdev->dev.of_node, 0);

	if (tx_dev.hpd_gpio < 0) {
		HDMI_ERROR("[%s ] could not get gpio from of ",__FUNCTION__);
		return -ENODEV;
	}
	else
		HDMI_INFO("hotplug gpio(%d)",tx_dev.hpd_gpio);

	if (!gpio_is_valid(tx_dev.hpd_gpio))
		HDMI_ERROR("[%s ] gpio %d is not valid",__FUNCTION__,tx_dev.hpd_gpio);
		
	if(gpio_request(tx_dev.hpd_gpio, pdev->dev.of_node->name))
		HDMI_ERROR("[%s ] Could not request gpio, %d",__FUNCTION__, tx_dev.hpd_gpio);

	/* Get hotplug gpio irq */
	tx_dev.hpd_irq = irq_of_parse_and_map (pdev->dev.of_node, 0);
	if (!tx_dev.hpd_irq)
	{
		HDMI_ERROR("Fail to parse of irq!!!");
		return -ENODEV;
	}
	else
		HDMI_DEBUG("irq_num(%u)\n",tx_dev.hpd_irq);

	if (register_hdmitx_switchdev(&tx_dev)) {
        HDMI_ERROR("Could not register_hdmitx_switchdev");
        goto err_register;
    } 

	// Initial SCDC read request
	register_hdmitx_scdcrr(pdev->dev.of_node);

#ifdef USE_ION_AUDIO_HEAP
	//for RPC
	register_ion_client("hdmitx_driver");  
#endif	

// temporary solution	
	of_property_read_u32(pdev->dev.of_node, "module-enable", &(module_en));
	tmds_en = module_en;
	HDMI_INFO("%s turning off TMDS flow",module_en?"Enable":"Disable");

	device_create_file(&pdev->dev, &dev_attr_hdmitx_info);
	device_create_file(&pdev->dev, &dev_attr_edid_info);

	HDMI_INFO("Driver init done");
	return 0;
	
err_register:
        deregister_hdmitx_miscdev(&tx_dev);	
end:
	return -EFAULT;
	
}

static const struct of_device_id rtk_hdmitx_dt_ids[] = {
     { .compatible = "Realtek,rtk119x-hdmitx", },
     {},
};
MODULE_DEVICE_TABLE(of, rtk_hdmitx_dt_ids);

static const struct dev_pm_ops rtk_hdmitx_pm_ops = {
    .suspend    = rtk_hdmitx_suspend,
    .resume     = rtk_hdmitx_resume,
};

static struct platform_driver rtk_hdmi_driver = {
	.probe = rtk_hdmi_probe,
	.driver = {
		.name = "rtk_hdmi",
		.owner = THIS_MODULE, 
		.of_match_table = of_match_ptr(rtk_hdmitx_dt_ids),
#ifdef CONFIG_PM
        .pm = &rtk_hdmitx_pm_ops,
#endif
	}, 
};

/*-----------------------------------------------------------------------------
 * Function: hdmi_init
 *-----------------------------------------------------------------------------
 */
static int __init rtk_hdmi_init(void)
{									
    if (platform_driver_register(&rtk_hdmi_driver)) {
        HDMI_ERROR("Could not add character driver");
        goto err_register;
    }
						
	return 0;	
		
err_register:
        platform_driver_unregister(&rtk_hdmi_driver);

    return -EFAULT;
}

/*-----------------------------------------------------------------------------
 * Function: hdcp_exit
 *-----------------------------------------------------------------------------
 */
static void __exit rtk_hdmi_exit(void)
{
#ifdef USE_ION_AUDIO_HEAP
	//for RPC
	deregister_ion_client("hdmitx_driver");
#endif	
	deregister_hdmitx_switchdev(&tx_dev);
	deregister_hdmitx_miscdev(&tx_dev);
	platform_driver_unregister(&rtk_hdmi_driver);
}

/*-----------------------------------------------------------------------------
 *-----------------------------------------------------------------------------
 */
module_init(rtk_hdmi_init);
module_exit(rtk_hdmi_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek HDMI kernel module");
