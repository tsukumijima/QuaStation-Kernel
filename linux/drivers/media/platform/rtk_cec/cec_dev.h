//Copyright (C) 2007-2013 Realtek Semiconductor Corporation.
#ifndef __CEC_DEV_H__
#define __CEC_DEV_H__

#include <linux/cdev.h>
#include "cm_buff.h"
#include "cec.h"

enum {
    CEC_ENABLE,
    CEC_SET_LOGICAL_ADDRESS,
    CEC_SET_POWER_STATUS,   
    CEC_SEND_MESSAGE,       
    CEC_RCV_MESSAGE,
    CEC_SET_PHYSICAL_ADDRESS,
    CEC_SET_STANDBY_MODE,
    CEC_SET_RX_MASK,
    CEC_SET_CEC_VERSION,
    CEC_SET_DEVICE_VENDOR_ID,
    CEC_SET_RETRY_NUM,
};

typedef struct {       
    unsigned char*      buf;
    unsigned char       len;    
}cec_msg;


typedef struct{        
    struct cdev     cdev;    
    struct device*  device;    
}cec_dev;


extern int  create_cec_dev_node(cec_device* dev);
extern void remove_cec_dev_node(cec_device* dev);
extern int cec_tx_device_check(void);
extern int cec_rx_device_check(void);  

#endif  //__CEC_DEV_H__

