#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <plat/wifi_power.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <plat/cpu.h>
#include <mach/cpu.h>
#ifdef CONFIG_OF
#include <linux/slab.h>
#include <linux/of.h>
#include <mach/io.h>
#include <plat/io.h>
#include <mach/register.h>
#include <linux/pinctrl/consumer.h>
#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/of_gpio.h>
#include <mach/pinmux.h>
#endif
#include <linux/reboot.h>
#include <linux/suspend.h>
#include <linux/notifier.h>
#include <uapi/linux/reboot.h>
#include <linux/amlogic/wifi_dt.h>

#define WIFI_POWER_MODULE_NAME	"wifi_power"
#define WIFI_POWER_DRIVER_NAME	"wifi_power"
#define WIFI_POWER_DEVICE_NAME	"wifi_power"
#define WIFI_POWER_CLASS_NAME		"wifi_power"

#define USB_POWER_UP    _IO('m',1)
#define USB_POWER_DOWN  _IO('m',2)
#define SDIO_POWER_UP    _IO('m',3)
#define SDIO_POWER_DOWN  _IO('m',4)

extern  void sdio_reinit(void);

static dev_t wifi_power_devno;
static struct cdev *wifi_power_cdev = NULL;
static struct device *devp=NULL;
struct wifi_power_platform_data *pdata = NULL;

int wifi_power_on_pin2 = 0;
static int suspend_set_power = 0;
static int wifi_power_configed = 0;
static int power_gpio_valid_level = 0; //1: high ; 0: low(default)

static int usb_power = 0;
#define BT_BIT 		0
#define WIFI_BIT	1
static DEFINE_MUTEX(wifi_bt_mutex);
void set_bt_power(int is_power);
void set_wifi_power(int is_power);
static void usb_power_control(int is_power, int shift);    

static int wifi_power_probe(struct platform_device *pdev);
static int wifi_power_remove(struct platform_device *pdev);
static int wifi_power_suspend(struct platform_device *pdev, pm_message_t state);
static int wifi_power_resume(struct platform_device *pdev);
static int wifi_power_open(struct inode *inode,struct file *file);
static int wifi_power_release(struct inode *inode,struct file *file);
static void usb_wifi_power(int is_power);
static long wifi_power_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

static const struct of_device_id amlogic_wifi_power_dt_match[]={
	{	.compatible = "amlogic,wifi_power",
	},
	{},
};

static struct platform_driver wifi_power_driver = {
	.probe = wifi_power_probe,
	.remove = wifi_power_remove,
	.suspend	= wifi_power_suspend,
	.resume		= wifi_power_resume,
	.driver = {
		.name = WIFI_POWER_DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = amlogic_wifi_power_dt_match,
	},
};

static const struct file_operations wifi_power_fops = {
	.unlocked_ioctl = wifi_power_ioctl,
	.open	= wifi_power_open,
	.release	= wifi_power_release,
};

static struct class wifi_power_class = {
	.name = WIFI_POWER_CLASS_NAME,
	.owner = THIS_MODULE,
};

void set_bt_power(int is_power)
{
    struct wifi_power_platform_data *pdata = NULL;

    pdata = (struct wifi_power_platform_data*)devp->platform_data;
    if (pdata == NULL) {
        pr_err("%s platform data is required!\n", __FUNCTION__);
        return;
    }

    amlogic_gpio_request(pdata->power_gpio, WIFI_POWER_MODULE_NAME);

    if (wifi_power_on_pin2) {
        amlogic_gpio_request(pdata->power_gpio2,WIFI_POWER_MODULE_NAME);
    }
    if (is_power == 0)
        usb_power_control(power_gpio_valid_level, BT_BIT);
    else
        usb_power_control(!power_gpio_valid_level, BT_BIT);
}

EXPORT_SYMBOL(set_bt_power);

void set_wifi_power(int is_power)
{
	usb_power_control(is_power, WIFI_BIT);	
}
static void usb_power_control(int is_power, int shift)
{	
	mutex_lock(&wifi_bt_mutex);
	if(is_power == power_gpio_valid_level)
	{
		if(!usb_power)
		{	
			usb_wifi_power(is_power);
			usb_power |= (1 << shift);
		  printk("Set %s power on !\n", (shift? "WiFi":"BT"));
		}
	}
	else
	{
		usb_power &= ~(1 << shift);
		if(!usb_power)
		{	
			usb_wifi_power(is_power);
			printk("Set %s power off !\n", (shift? "WiFi":"BT"));
		}
			  
	}
	mutex_unlock(&wifi_bt_mutex);		
}

static int  wifi_power_open(struct inode *inode,struct file *file)
{
  struct cdev * cdevp = inode->i_cdev;
  
    
  file->private_data = cdevp;
    
  return 0;
}

static int  wifi_power_release(struct inode *inode,struct file *file)
{
    return 0;
}

static long wifi_power_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct wifi_power_platform_data *pdata = NULL;
    pdata = (struct wifi_power_platform_data*)devp->platform_data;
    if (pdata == NULL)
    {
        printk("%s platform data is required!\n",__FUNCTION__);
        return -1;
    }

    switch (cmd)
    {
        case USB_POWER_UP:
            wifi_power_control(0);
            mdelay(200);
            wifi_power_control(1);
            break;
        case USB_POWER_DOWN:
            wifi_power_control(0);
            amlogic_gpio_free(pdata->power_gpio,WIFI_POWER_MODULE_NAME);
            if (wifi_power_on_pin2)
                amlogic_gpio_free(pdata->power_gpio2,WIFI_POWER_MODULE_NAME);
            printk(KERN_INFO "Set usb_sdio wifi power down!\n");
            break;
        case SDIO_POWER_UP:
            wifi_setup_dt();
            extern_wifi_set_enable(0);
            mdelay(200);
            extern_wifi_set_enable(1);
            mdelay(200);
            sdio_reinit();
            break;
        case SDIO_POWER_DOWN:
            extern_wifi_set_enable(0);
            wifi_teardown_dt();
            break;
        default:
            pr_err("usb wifi_power_ioctl: default !!!\n");
            return -EINVAL;
    }
    return 0;
}

int wifi_power_control(int power_up)
{
	struct wifi_power_platform_data *pdata = NULL;

	pdata = (struct wifi_power_platform_data*)devp->platform_data;
	if (pdata == NULL) {
		pr_err("%s platform data is required!\n", __FUNCTION__);
		return -1;
	}

	amlogic_gpio_request(pdata->power_gpio, WIFI_POWER_MODULE_NAME);

	if (wifi_power_on_pin2) {
		amlogic_gpio_request(pdata->power_gpio2,WIFI_POWER_MODULE_NAME);
	}

	if (power_up) {
		set_wifi_power(power_gpio_valid_level);
		pr_info("Set usb wifi power up!\n");
	} else {
		set_wifi_power(!power_gpio_valid_level);
		pr_info("Set usb wifi power down!\n");
	}
	return 0;
}
EXPORT_SYMBOL(wifi_power_control);

int wifi_set_power(int val)
{
	struct wifi_power_platform_data *pdata = NULL;
    
    
	pdata = (struct wifi_power_platform_data*)devp->platform_data;
	if(pdata == NULL){
		printk("%s platform data is required!\n",__FUNCTION__);
		return -1;
	}
  
	if(pdata->set_power){
		pdata->set_power(val);
	}
	else{
		printk( "%s:%s No wifi set_power !\n",WIFI_POWER_MODULE_NAME,__FUNCTION__);
		return -EINVAL;
	}

	return 0;
}

EXPORT_SYMBOL(wifi_set_power);

int wifi_set_reset(int val)
{
	struct wifi_power_platform_data *pdata = NULL;
	
	
	pdata = (struct wifi_power_platform_data*)devp->platform_data;
	if(pdata == NULL){
		printk("%s platform data is required!\n",__FUNCTION__);
		return -1;
	}
  
	if(pdata->set_reset){
		pdata->set_reset(val);
	}
	else{
		printk( "%s:%s No wifi set_reset !\n",WIFI_POWER_MODULE_NAME,__FUNCTION__);
		return -EINVAL;
	}
	
	return 0;
}

EXPORT_SYMBOL(wifi_set_reset);

int wifi_set_carddetect(int val)
{
	struct wifi_power_platform_data *pdata = NULL;
	
	
	pdata = (struct wifi_power_platform_data*)devp->platform_data;
	if(pdata == NULL){
		printk("%s platform data is required!\n",__FUNCTION__);
		return -1;
	}
	
	if(pdata->set_carddetect){
		pdata->set_carddetect(val);
	}
	else{
		printk( "%s:%s No wifi set_carddetect !\n",WIFI_POWER_MODULE_NAME,__FUNCTION__);
		return -EINVAL;
	}
	
	return 0;
}

EXPORT_SYMBOL(wifi_set_carddetect);

void *wifi_mem_prealloc(int section, unsigned long size)
{
	void * ret;
	struct wifi_power_platform_data *pdata = NULL;
    
    
	pdata = (struct wifi_power_platform_data*)devp->platform_data;
	if(pdata == NULL){
		printk("%s platform data is required!\n",__FUNCTION__);
		return NULL;
	}
	
	if(pdata->mem_prealloc){
		ret = pdata->mem_prealloc(section,size);
	}
	else{
		printk( "%s:%s No wifi mem_prealloc !\n",WIFI_POWER_MODULE_NAME,__FUNCTION__);
		return NULL;
	}
	
	return ret;
}

EXPORT_SYMBOL(wifi_mem_prealloc);

int wifi_get_mac_addr(unsigned char *buf)
{
	struct wifi_power_platform_data *pdata = NULL;
	
	
	pdata = (struct wifi_power_platform_data*)devp->platform_data;
	if(pdata == NULL){
		printk("%s platform data is required!\n",__FUNCTION__);
		return -1;
	}
	
	if(pdata->get_mac_addr){
		pdata->get_mac_addr(buf);
	}
	else{
		printk( "%s:%s No wifi get_mac_addr !\n",WIFI_POWER_MODULE_NAME,__FUNCTION__);
		return -EINVAL;
	}
	
	return 0;
}

EXPORT_SYMBOL(wifi_get_mac_addr);

void *wifi_get_country_code(char *ccode)
{
	void * ret;
	struct wifi_power_platform_data *pdata = NULL;
    
     
	pdata = (struct wifi_power_platform_data*)devp->platform_data;
	if(pdata == NULL){
		printk("%s platform data is required!\n",__FUNCTION__);
		return NULL;
	}
	
	if(pdata->get_country_code){
		ret = pdata->get_country_code(ccode);
	}
	else{
		printk( "%s:%s No wifi get_mac_addr !\n",WIFI_POWER_MODULE_NAME,__FUNCTION__);
		return NULL;
	}
	
	return ret;
}

EXPORT_SYMBOL(wifi_get_country_code);


static void usb_wifi_power(int is_power)
{    
#ifdef CONFIG_OF
	if(!pdata){
		printk("%s pdata is not inited!\n",__FUNCTION__);
		return;
	}
	if(pdata->power_gpio > 0)
		amlogic_gpio_direction_output(pdata->power_gpio, is_power, WIFI_POWER_MODULE_NAME); 	  

	if(wifi_power_on_pin2){
	   	if(pdata->power_gpio2 > 0)
	    	amlogic_gpio_direction_output(pdata->power_gpio2, is_power, WIFI_POWER_MODULE_NAME); 	  
	}
	
#else    
	CLEAR_CBUS_REG_MASK(PREG_PAD_GPIO6_EN_N, (1<<11));
	if (is_power)//is_power
		SET_CBUS_REG_MASK(PREG_PAD_GPIO6_O, (1<<11)); // GPIO_E bit 11
	else
		CLEAR_CBUS_REG_MASK(PREG_PAD_GPIO6_O, (1<<11));
#endif
    return;
}

static int wifi_power_probe(struct platform_device *pdev)
{
    int ret;
    const char *str;
    

    pdata = kzalloc(sizeof(struct wifi_power_platform_data), GFP_KERNEL);
    if(!pdata)
    {
        printk("Error: can not alloc memory ------%s\n",__func__);
        return -1;
    }
    pdata->usb_set_power = &usb_wifi_power;
    
    if(pdev->dev.of_node)
    {
    	wifi_power_configed = 1;
    	ret = of_property_read_string(pdev->dev.of_node, "valid", &str);
			if(ret)
			{
				printk("Error: Didn't get power valid value --- %s %d\n",__func__,__LINE__);
			} else {
				if(!strncmp(str,"low",3))
				{	
					power_gpio_valid_level = 0;
					printk("mcli: power_gpio_valid_level valid is low!!! \n");
				}
				else
				{
					power_gpio_valid_level = 1;
					printk("mcli: power_gpio_valid_level valid is high !!! \n");
				}
			}
			
			ret = of_property_read_string(pdev->dev.of_node, "suspend_pull_power", &str);
			if(ret)
			{
				printk("Error: Didn't get suspend_power_off value --- %s %d\n",__func__,__LINE__);
				suspend_set_power = 1;
			} else {
				if(!strncmp(str,"false",5))
					suspend_set_power = 0;
				else
					suspend_set_power = 1;
			}
			
      ret = of_property_read_string(pdev->dev.of_node, "power_gpio", &str);
	    if(ret)
	    {
	    	printk("Error: can not get power_gpio name------%s %d\n",__func__,__LINE__);
	      return -1;
	    } else {
	    	pdata->power_gpio = amlogic_gpio_name_map_num(str);
	    	printk("wifi_power power_gpio is %d\n",pdata->power_gpio); 
	    }	     	    
		 
	  	if(!(ret = of_property_read_string(pdev->dev.of_node, "power_gpio2", &str)))
				wifi_power_on_pin2 = 1;
	  	else{
				printk("wifi_dev_probe : there is no wifi_power_on_pin2 setup in DTS file!\n");
	  	}
	   
	  	if(wifi_power_on_pin2){
		  	if(ret)
		    {
		    	printk("Error: can not get power_gpio2 name------%s %d\n",__func__,__LINE__);
		      return -1;
		     }else{
					pdata->power_gpio2 = amlogic_gpio_name_map_num(str);
		     	printk("wifi_power power_gpio2 is %d\n",pdata->power_gpio2);
		     }
	  	}
		 
		}
    pdev->dev.platform_data = pdata;
    
    ret = alloc_chrdev_region(&wifi_power_devno, 0, 1, WIFI_POWER_DRIVER_NAME);
    if (ret < 0) {
    	printk(KERN_ERR "%s:%s failed to allocate major number\n",WIFI_POWER_MODULE_NAME,__FUNCTION__);
    	ret = -ENODEV;
    	goto out;
    }
    ret = class_register(&wifi_power_class);
    if (ret < 0) {
    	printk(KERN_ERR "%s:%s  failed to register class\n",WIFI_POWER_MODULE_NAME,__FUNCTION__);
    	goto error1;
    }
    wifi_power_cdev = cdev_alloc();
    if(!wifi_power_cdev){
    	printk(KERN_ERR "%s:%s failed to allocate memory\n",WIFI_POWER_MODULE_NAME,__FUNCTION__);
    	goto error2;
    }
    cdev_init(wifi_power_cdev,&wifi_power_fops);
    wifi_power_cdev->owner = THIS_MODULE;
    ret = cdev_add(wifi_power_cdev,wifi_power_devno,1);
    if(ret){
    	printk(KERN_ERR "%s:%s failed to add device\n",WIFI_POWER_MODULE_NAME,__FUNCTION__);
    	goto error3;
    }
    devp = device_create(&wifi_power_class,NULL,wifi_power_devno,NULL,WIFI_POWER_DEVICE_NAME);
    if(IS_ERR(devp)){
    	printk(KERN_ERR "%s:%s failed to create device node\n",WIFI_POWER_MODULE_NAME,__FUNCTION__);
    	ret = PTR_ERR(devp);
    	goto error3;
    }
    devp->platform_data = pdata;
    return 0;
    
error3:
    cdev_del(wifi_power_cdev);
error2:
    class_unregister(&wifi_power_class);
error1:
    unregister_chrdev_region(wifi_power_devno,1);
out:
    return ret;
}

static int wifi_power_remove(struct platform_device *pdev)
{
	unregister_chrdev_region(wifi_power_devno,1);
	class_unregister(&wifi_power_class);
	device_destroy(NULL, wifi_power_devno);
	cdev_del(wifi_power_cdev);
	
	return 0;
}

static int wifi_power_suspend(struct platform_device *pdev, pm_message_t state)
{	
  if(wifi_power_configed && suspend_set_power)
    usb_wifi_power(!power_gpio_valid_level);
   
	return 0;
}

static int wifi_power_resume(struct platform_device *pdev)
{
	if(wifi_power_configed && suspend_set_power)
    usb_wifi_power(power_gpio_valid_level);   

	return 0;
}

static int wifi_power_reboot_notify(struct notifier_block *nb, unsigned long event,void *dummy)
{
	if(wifi_power_configed)
		usb_wifi_power(!power_gpio_valid_level);

	return NOTIFY_OK;
}

static struct notifier_block wifi_power_reboot_notifier = {
	.notifier_call = wifi_power_reboot_notify,
};

static int __init init_wifi(void)
{
	int ret = -1;
	
  ret = platform_driver_register(&wifi_power_driver);
  if (ret != 0) {
        printk(KERN_ERR "failed to register wifi power module, error %d\n", ret);
    return -ENODEV;
  }
    
	register_reboot_notifier(&wifi_power_reboot_notifier);
        
  return ret;
}

module_init(init_wifi);

static void __exit unload_wifi(void)
{
	platform_driver_unregister(&wifi_power_driver);
	unregister_reboot_notifier(&wifi_power_reboot_notifier);
}
module_exit(unload_wifi);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AMLOGIC");
MODULE_DESCRIPTION("WIFI power driver");
