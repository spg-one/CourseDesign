/*************************************

NAME:EmbedSky_adc.c
COPYRIGHT:www.embedsky.net

*************************************/

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/serio.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <mach/regs-clock.h>
#include <plat/regs-timer.h>
	 
#include <plat/regs-adc.h>
#include <mach/regs-gpio.h>
#include <linux/cdev.h>
#include <linux/miscdevice.h>

#include "tq2440_adc.h"

#undef DEBUG
//#define DEBUG
#ifdef DEBUG
#define DPRINTK(x...) {printk("EmbedSky_adc: " x);}
#else
#define DPRINTK(x...) (void)(0)
#endif

#define DEVICE_NAME	"adc"

#define ch_0 0
#define ch_1 1
#define ch_2 2
#define ch_3 3

static void __iomem *base_addr;

typedef struct
{
	wait_queue_head_t wait;
	int channel;
	int prescale;
}ADC_DEV;

DECLARE_MUTEX(ADC_LOCK);
static int ADC_enable = 0;

static ADC_DEV adcdev;
static volatile int ev_adc = 0;
static int adc_data;

static struct clk	*adc_clock;

#define ADCCON			(*(volatile unsigned long *)(base_addr + S3C2410_ADCCON))	//ADC control
#define ADCTSC			(*(volatile unsigned long *)(base_addr + S3C2410_ADCTSC))	//ADC touch screen control
#define ADCDLY			(*(volatile unsigned long *)(base_addr + S3C2410_ADCDLY))	//ADC start or Interval Delay
#define ADCDAT0		(*(volatile unsigned long *)(base_addr + S3C2410_ADCDAT0))	//ADC conversion data 0
#define ADCDAT1		(*(volatile unsigned long *)(base_addr + S3C2410_ADCDAT1))	//ADC conversion data 1
#define ADCUPDN		(*(volatile unsigned long *)(base_addr + 0x14))			//Stylus Up/Down interrupt status

#define PRESCALE_DIS	(0 << 14)
#define PRESCALE_EN	(1 << 14)
#define PRSCVL(x)		((x) << 6)
#define ADC_INPUT(x)	((x) << 3)
#define ADC_START		(1 << 0)
#define ADC_ENDCVT		(1 << 15)

#define START_ADC_AIN(ch, prescale) \
	do{ 	ADCCON = PRESCALE_EN | PRSCVL(prescale) | ADC_INPUT((ch)) ; \
		ADCCON |= ADC_START; \
	}while(0)

int iSaveADCTSC=0;//��������ADCTSC

/*****************************************
*
*  ��������tq2440_adc_ioctl
*  ���ܣ�����ģ��ת��ͨ����
*  ����1��˵����
*  ����2��˵����
*  ����ֵ���ɹ�����0
*  �޸���ʷ��(��Ӵ���)
* 	ʱ�䣺2011-11-29,���ߣ���ϣ�����汾�ţ�������
*	ԭ��
*
*****************************************/
static int tq2440_adc_ioctl(
	struct inode *inode, 
	struct file *file, 
	unsigned int cmd, 
	unsigned long arg)
{
	if (arg > 4)
	{
		return -EINVAL;
	}

	switch(cmd)
	{
		case ch_0:
			// ����ģ��ת��ͨ��Ϊ0
			adcdev.channel=0;
			return 0;

		case ch_1:
			// ����ģ��ת��ͨ��Ϊ1
			adcdev.channel=1;
			return 0;
		case ch_2:
			// ����ģ��ת��ͨ��Ϊ2
			adcdev.channel=2;
			return 0;
		case ch_3:
			// ����ģ��ת��ͨ��Ϊ3
			adcdev.channel=3;
			return 0;

		default:
			return -EINVAL;
	}
}

static ssize_t tq2440_adc_read(struct file *filp, char *buffer, size_t count, loff_t *ppos)
{
	char str[20];
	int value;
	size_t len;
	if (down_trylock(&ADC_LOCK) == 0)
	{
		ADC_enable = 1;
		START_ADC_AIN(adcdev.channel, adcdev.prescale);

		iSaveADCTSC=ADCTSC;//����Ĵ���
		ADCTSC=ADCTSC&(~0x1<<2);//����adcת��
		ADCCON|=0x01;//��ʼת��
		while(ADCCON&0x1);//��ʼ��
		while(!ADCCON&(0x1<<15));//����ת��
		adc_data = ADCDAT0 & 0x3ff;//ȡ��ģ��ת����ֵ
		ADCTSC=iSaveADCTSC;//�ָ��Ĵ���
		ev_adc = 0;

		DPRINTK("AIN[%d] = 0x%04x, %d\n", adcdev.channel, adc_data, ((ADCCON & 0x80) ? 1:0));

		value = adc_data;
		sprintf(str,"%5d", adc_data);
		copy_to_user(buffer, (char *)&adc_data, sizeof(adc_data));

		ADC_enable = 0;
		up(&ADC_LOCK);
		
		len = sprintf(str, "%d\n", value);
		if (count >= len)
		{
			int r = copy_to_user(buffer, str, len);
			return r ? r : len;
		}
		else
		{
			return -1;
		}
	}
	else
	{
		return -1;//����Ǵ����жϣ����أ�1����ȡ���ֵ
		//value = -1;
	}
}

static int tq2440_adc_open(struct inode *inode, struct file *filp)
{
	init_waitqueue_head(&(adcdev.wait));

	adcdev.channel=2;	
	adcdev.prescale=0xff;

	DPRINTK( "ADC opened\n");

	return 0;
}

static int tq2440_adc_release(struct inode *inode, struct file *filp)
{
	DPRINTK( "ADC closed\n");
	return 0;
}


static struct file_operations dev_fops = {
	.owner		= THIS_MODULE,
	.open		= tq2440_adc_open,
	.ioctl		= tq2440_adc_ioctl,
	.read		= tq2440_adc_read,	
	.release	= tq2440_adc_release,
};

static struct miscdevice misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVICE_NAME,
	.fops = &dev_fops,
};

static int __init dev_init(void)
{
	int ret;

	base_addr=ioremap(S3C2410_PA_ADC,0x20);
	if (base_addr == NULL)
	{
		printk(KERN_ERR "failed to remap register block\n");
		return -ENOMEM;
	}

	adc_clock = clk_get(NULL, "adc");
	if (!adc_clock)
	{
		printk(KERN_ERR "failed to get adc clock source\n");
		return -ENOENT;
	}
	clk_enable(adc_clock);
	
	ADCTSC = 0;

	ret = misc_register(&misc);

	printk (DEVICE_NAME" initialized\n");
	return ret;
}

static void __exit dev_exit(void)
{

	if (adc_clock)
	{
		clk_disable(adc_clock);
		clk_put(adc_clock);
		adc_clock = NULL;
	}

	misc_deregister(&misc);
}

EXPORT_SYMBOL(ADC_LOCK);
module_init(dev_init);
module_exit(dev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("www.embedsky.net");
MODULE_DESCRIPTION("ADC Drivers for EmbedSky SKY2440/TQ2440 Board and support touch");

