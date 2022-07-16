#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/workqueue.h>


struct askey_gpio_button {
    char *pName;
    unsigned int gpioNumber;
    unsigned int irqNumber;
    unsigned long jiffies;
    struct gpio_desc *pGpioDesc;
    struct tasklet_struct tasklet;
};

static struct kobject *pKobject;
static struct timer_list powerButtonFunction;
static struct timer_list resetButtonFunction;
static struct timer_list importButtonFunction;
static struct askey_gpio_button gpioButtonList[4];  // POWER, RESET, WPS, IMPORT


static void send_uevent(char *pButtonEventName)
{
    char eventString[20];  // max 20 letter (include \0)
    char *envp[] = { eventString, NULL };
    int ret;

    if (pKobject != 0 && pButtonEventName != 0) {

        // set button event name (POWER, RESET, INIT, WPS_DOWN, WPS_UP, IMPORT, UNMOUNT)
        snprintf(eventString, 20, "button=%s", pButtonEventName);

        // send uevent with the button event name
        ret = kobject_uevent_env(pKobject, KOBJ_CHANGE, envp);
        if (ret) {
            printk(KERN_ERR "[gpio_isr] failed to send %s uevent. (ret: %d)\n", pButtonEventName, ret);
        } else {
            printk(KERN_ERR "[gpio_isr] send %s uevent.\n", pButtonEventName);
        }
    }
    return;
}


static void power_longpress_callback(unsigned long dummy)
{
    // send POWER uevent (3 sec)
    send_uevent("POWER");
    return;
}


static void reset_longpress_callback(unsigned long dummy)
{
    // send INIT uevent (10 sec)
    send_uevent("INIT");
    return;
}


static void import_longpress_callback(unsigned long dummy)
{
    // send UNMOUNT uevent (3 sec)
    send_uevent("UNMOUNT");
    return;
}


static irqreturn_t gpio_isr_irq_handler(int irq, void *dev_id)
{
    struct askey_gpio_button *pGpioButton = dev_id;

    if (!pGpioButton || !pGpioButton->pName) {
        printk(KERN_ERR "[gpio_isr] given invalid dev_id.\n");
        return IRQ_NONE;
    }

    // dispatch tasklet
    tasklet_schedule(&pGpioButton->tasklet);
    return IRQ_HANDLED;
}


static void gpio_isr_tasklet_handler(unsigned long pGpioButton_)
{
    struct askey_gpio_button *pGpioButton = (struct askey_gpio_button *) pGpioButton_;
    int gpioRawValue = gpiod_get_raw_value(pGpioButton->pGpioDesc);

    // if pressed down
    if (gpioRawValue == 0) {

        printk(KERN_INFO "[gpio_isr] %s button pressed down\n", pGpioButton->pName);

        // save pressed down time
        pGpioButton->jiffies = jiffies;

        // register POWER uevent timer to run after 3 sec
        if (strcmp(pGpioButton->pName, "POWER") == 0) {
            init_timer(&powerButtonFunction);
            powerButtonFunction.expires = jiffies + (3 * HZ);  // 3 sec
            powerButtonFunction.function = power_longpress_callback;
            add_timer(&powerButtonFunction);
            return;
        }

        // register INIT uevent timer to run after 10 sec
        if (strcmp(pGpioButton->pName, "RESET") == 0) {
            init_timer(&resetButtonFunction);
            resetButtonFunction.expires = jiffies + (10 * HZ);  // 10 sec
            resetButtonFunction.function = reset_longpress_callback;
            add_timer(&resetButtonFunction);
            return;
        }

        // send WPS_DOWN uevent
        if (strcmp(pGpioButton->pName, "WPS") == 0) {
            send_uevent("WPS_DOWN");
            return;
        }

        // register UNMOUNT uevent timer to run after 3 sec.
        if (strcmp(pGpioButton->pName, "IMPORT") == 0) {
            init_timer(&importButtonFunction);
            importButtonFunction.function = import_longpress_callback;
            importButtonFunction.expires = jiffies + (3 * HZ);  // 3 sec
            add_timer(&importButtonFunction);
            return;
        }

    // if pressed up
    } else {

        printk(KERN_INFO "[gpio_isr] %s button pressed up\n", pGpioButton->pName);

        // unregister POWER uevent timer
        if (strcmp(pGpioButton->pName, "POWER") == 0) {
            del_timer(&powerButtonFunction);
            return;
        }

        // unregister INIT uevent timer, and send RESET uevent
        if (strcmp(pGpioButton->pName, "RESET") == 0) {
            del_timer(&resetButtonFunction);
            // if within 2 sec of pressed down, send RESET uevent
            if ((jiffies - pGpioButton->jiffies) <= (2 * HZ)) {
                send_uevent("RESET");
            }
            return;
        }

        // send WPS_UP uevent
        if (strcmp(pGpioButton->pName, "WPS") == 0) {
            send_uevent("WPS_UP");
            return;
        }

        // unregister UNMOUNT uevent timer, and send IMPORT uevent
        if (strcmp(pGpioButton->pName, "IMPORT") == 0) {
            del_timer(&importButtonFunction);
            // if within 2 sec of pressed down, send IMPORT uevent
            if ((jiffies - pGpioButton->jiffies) <= (2 * HZ)) {
                send_uevent("IMPORT");
            }
            return;
        }
    }

    return;
}


static int gpio_button_probe(struct platform_device *pDevice)
{
    struct device_node *pDeviceNode;
    struct device_node *pDeviceChildNode;
    struct askey_gpio_button *pGpioButton;
    struct property *pGpioButtonProp;
    enum of_gpio_flags gpioFlags;
    int gpioButtonCount = 0;
    int count = 0;
    int err;

    // get my kobject
    printk(KERN_INFO "[gpio_isr] initializing...\n");
    pKobject = &pDevice->dev.kobj;
    if (!pKobject) {
        printk(KERN_ERR "[gpio_isr] init failed.\n");
        return -ENODEV;
    }

    printk(KERN_INFO "[gpio_isr] device name: %s\n", pDevice->name);
    pDeviceNode = pDevice->dev.of_node;
    if (!pDeviceNode) {
        printk(KERN_ERR "[gpio_isr] failed to get pDevice->dev.of_node\n");
        return -ENODEV;
    }

    gpioButtonCount = of_get_child_count(pDeviceNode);
    if (gpioButtonCount != 4) {
        printk(KERN_ERR "[gpio_isr] gpioButtonCount is %d (expected 4).\n", gpioButtonCount);
        return -ENODEV;
    }

    // run for each child node of "gpio-btns" (POWER, RESET, WPS, IMPORT)
    for_each_child_of_node(pDeviceNode, pDeviceChildNode) {

        if (!pDeviceChildNode) {
            printk(KERN_ERR "[gpio_isr] pDeviceChildNode not found.\n");
            return -ENODEV;
        }

        // get "gpios" property from device-tree
        pGpioButtonProp = of_find_property(pDeviceChildNode, "gpios", NULL);
        if (!pGpioButtonProp) {
            printk(KERN_ERR "[gpio_isr] failed to get \"gpios\" prop from device-tree.\n");
            return -ENODEV;
        }

        pGpioButton = &gpioButtonList[count];
        count++;

        pGpioButton->pName = kstrdup(pDeviceChildNode->name, GFP_KERNEL);
        pGpioButton->gpioNumber = of_get_named_gpio_flags(pDeviceChildNode, "gpios", 0, &gpioFlags);

        pGpioButton->pGpioDesc = gpio_to_desc(pGpioButton->gpioNumber);
        pGpioButton->irqNumber = gpiod_to_irq(pGpioButton->pGpioDesc);
        if (pGpioButton->irqNumber < 1) {
            printk(KERN_ERR "[gpio_isr] failed to get irq number.\n");
            return -EPERM;
        }

        // gpio registration
        err = gpio_request(pGpioButton->gpioNumber, pDeviceChildNode->name);
        if (err) {
            printk(KERN_ERR "[gpio_isr] failed to request gpio.\n");
            return -EPERM;
        }

        // request irq (register top half)
        err = request_irq(pGpioButton->irqNumber, gpio_isr_irq_handler, IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING, "gpio_isr_irq", pGpioButton);
        if (err) {
            gpio_free(pGpioButton->gpioNumber);
            printk(KERN_ERR "[gpio_isr] failed to request irq.\n");
            return -EPERM;
        }

        // register bottom half
        tasklet_init(&pGpioButton->tasklet, gpio_isr_tasklet_handler, (unsigned long) pGpioButton);

        printk(KERN_INFO "[gpio_isr] %s button init ok! gpio number: %u\n", pGpioButton->pName, pGpioButton->gpioNumber);
    }

    kobject_uevent(pKobject, KOBJ_ADD);

    printk(KERN_INFO "[gpio_isr] init ok!\n");
    return 0;
}


static int gpio_button_remove(struct platform_device *pDev)
{
    int count;

    printk(KERN_INFO "[gpio_isr] exiting...\n");

    // release button resource
    for (count = 0; count < 4; count++) {
        if (gpioButtonList[count].pName != 0) {
            free_irq(gpioButtonList[count].irqNumber, &gpioButtonList[count]);
            tasklet_kill(&gpioButtonList[count].tasklet);
            gpio_free(gpioButtonList[count].gpioNumber);
            printk(KERN_INFO "[gpio_isr] release memory of %s button.\n", gpioButtonList[count].pName);
        }
    }

    // unregister timer
    del_timer(&powerButtonFunction);
    del_timer(&resetButtonFunction);
    del_timer(&importButtonFunction);

    // remove kobject
    kobject_uevent(pKobject, KOBJ_REMOVE);
    kobject_put(pKobject);

    printk(KERN_INFO "[gpio_isr] exit ok!\n");
    return 0;
}


static const struct of_device_id gpioButtonMatchTable[] = {
    { .compatible = "Askey,gpio-btns", },
    {},
};

static struct platform_driver gpioButtonDriver = {
    .probe    = gpio_button_probe,
    .remove   = gpio_button_remove,
    .driver   = {
        .name = "gpio_isr",
        .of_match_table = gpioButtonMatchTable,
    },
};

module_platform_driver(gpioButtonDriver);

MODULE_AUTHOR("tsukumi <tsukumijima@users.noreply.github.com>");
MODULE_DESCRIPTION("GPIO ISR");
MODULE_LICENSE("GPL");
