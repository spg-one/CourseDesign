/*
 * (C) Copyright 2002
 * Sysgo Real-Time Solutions, GmbH <www.elinos.com>
 * Marius Groeger <mgroeger@sysgo.de>
 *
 * (C) Copyright 2002
 * David Mueller, ELSOFT AG, <d.mueller@elsoft.ch>
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <common.h>
#include <s3c2410.h>

DECLARE_GLOBAL_DATA_PTR;

#if defined(CONFIG_USE_MMU)
#define AP(x)			((x) << 10)
#define DOMAIN(x)		((x) << 5)
#define BUFFER			(1 << 2)
#define CACHE			(1 << 3)
#define SPECIAL			(1 << 4)
#define DESC_SECTION		0x02
	
#define WRITE_NOC_NOB		(AP(3) | DOMAIN(0) | SPECIAL | DESC_SECTION)
#define WRITE_C_NOB 		(AP(3) | DOMAIN(0) | SPECIAL | DESC_SECTION | CACHE)
#define WRITE_NOC_B 		(AP(3) | DOMAIN(0) | SPECIAL | DESC_SECTION | BUFFER)
#define WRITE_C_B		(AP(3) | DOMAIN(0) | SPECIAL | DESC_SECTION | BUFFER | CACHE)
	
void mmu_init(ulong ttb_base);
void mmu_enable(void);
void mmu_map_section(ulong ttbbase, ulong virtaddr, ulong physaddr, ulong flag);


typedef struct {
	ulong	start;
	ulong	end;
	ulong	flag;
} MAP_REG_t;

static const MAP_REG_t map_table[] = {
	{0x000, 0x0FF,	WRITE_NOC_NOB},
	{0x100, 0x1FF,	WRITE_NOC_NOB},
	{0x200, 0x2FF,	WRITE_NOC_NOB},
	{0x300, 0x3FF,	WRITE_NOC_NOB},
	{0x400, 0x4FF,	WRITE_NOC_NOB},
	{0x500, 0x5FF,	WRITE_NOC_NOB},
	{0x600, 0x6FF,	WRITE_NOC_NOB},
	{0x700, 0x7FF,	WRITE_NOC_NOB},
	{0x800, 0x8FF,	WRITE_NOC_NOB},
	{0x900, 0x9FF,	WRITE_NOC_NOB},
	{0xA00, 0xAFF,	WRITE_NOC_NOB},
	{0xB00, 0xBFF,	WRITE_NOC_NOB},
	{0xC00, 0xCFF,	WRITE_NOC_NOB},
	{0xD00, 0xDFF,	WRITE_NOC_NOB},
	{0xE00, 0xEFF,	WRITE_NOC_NOB},
	{0xF00, 0xFFF,	WRITE_NOC_NOB},
};

static ulong mmu_ttb[4096] __attribute__((aligned (0x4000)));	/* 存储一级页表 */
static void s3c24xx_startup_mmu(void)
{
#define SZ_1M			0x100000
#define IRQ_STACK_BASE		IRQ_STACK_START
/* 
 * MMU Level 1 Page Table Constants
 * Section descriptor 
 */ 
#define MMU_FULL_ACCESS		(3 << 10)	/* access permission bits */
#define MMU_DOMAIN		(0 << 5)	/* domain control bits */
#define MMU_SPECIAL		(1 << 4)	/* must be 1 */
#define MMU_CACHEABLE		(1 << 3)	/* cacheable */
#define MMU_BUFFERABLE		(1 << 2)	/* bufferable */
#define MMU_SECTION		(2) /* indicates that this is a section descriptor */
#define MMU_SECDESC		(MMU_FULL_ACCESS | MMU_DOMAIN | \
				MMU_SPECIAL | MMU_SECTION)
#define MMU_SECDESC_WB		(MMU_FULL_ACCESS | MMU_DOMAIN | MMU_SPECIAL | \
				MMU_CACHEABLE | MMU_BUFFERABLE | MMU_SECTION)
#define MMU_SECTION_SIZE	0x00100000

#define MMU_TABLE_BASE		0x33D80000//0x30000000//0x33D80000//( - 0x4000)		
	unsigned long pageoffset, sectionNumber;

	/* 4G 虚拟地址映射到相同的物理地址. not cacacheable, not bufferable */
	for (sectionNumber = 0; sectionNumber < 4096; sectionNumber++) {
		pageoffset = (sectionNumber << 20);
		*(mmu_ttb + (pageoffset >> 20)) = pageoffset | MMU_SECDESC;
	}

	/* make sdram cacheable */
	/* SDRAM物理地址0x3000000-0x33ffffff,DRAM_BASE=0x30000000,DRAM_SIZE=64M*/
#ifdef CONFIG_USE_IRQ
	for (pageoffset = IRQ_STACK_BASE & (~(SZ_1M - 1)); pageoffset < (PHYS_SDRAM_1+PHYS_SDRAM_1_SIZE); pageoffset += SZ_1M) {
#else
	for (pageoffset = (~(SZ_1M - 1)); pageoffset < (PHYS_SDRAM_1+PHYS_SDRAM_1_SIZE); pageoffset += SZ_1M) {
#endif
		//DPRINTK(3, "Make DRAM section cacheable: 0x%08lx\n", pageoffset);
		*(mmu_ttb + (pageoffset >> 20)) = pageoffset | MMU_SECDESC | MMU_CACHEABLE; 
	}

	mmu_init((ulong)mmu_ttb);

	mmu_enable();
}
#else
static void s3c24xx_startup_mmu(void)
{
	icache_enable();
	dcache_enable();
}
#endif


/*
 * Miscellaneous platform dependent initialisations
 */
int board_init (void)
{
	S3C24X0_GPIO * const gpio = S3C24X0_GetBase_GPIO();

	s3c24xx_startup_mmu();

	/* set up the I/O ports */
	gpio->GPACON = 0x007FFFFF;
	gpio->GPBCON = 0x00055555;
	gpio->GPBUP = 0x000007FF;
	gpio->GPCCON = 0xAAAAAAAA;
	gpio->GPCUP = 0x0000FFFF;
	gpio->GPDCON = 0xAAAAAAAA;
	gpio->GPDUP = 0x0000FFFF;
	gpio->GPECON = 0xAAAAAAAA;
	gpio->GPEUP = 0x0000FFFF;
	gpio->GPFCON = 0x000055AA;
	gpio->GPFUP = 0x000000FF;
	gpio->GPGCON = 0xFF94FFBA;
	gpio->GPGUP = 0x0000FFEF;
	gpio->GPGDAT = gpio->GPGDAT & ((~(1<<4)) | (1<<4)) ;
	gpio->GPHCON = 0x002AFAAA;
	gpio->GPHUP = 0x000007FF;
	gpio->GPJCON = 0x02aaaaaa;
	gpio->GPJUP = 0x00001fff;

//	S3C24X0_I2S * const i2s = S3C24X0_GetBase_I2S();	//HJ_add 屏蔽IIS,
//	i2s->IISCON = 0x00;					//HJ_add 屏蔽IIS,

	/* arch number of TQ2440-Board */
	gd->bd->bi_arch_number = MACH_TYPE_S3C2440;

	/* adress of boot parameters */
	gd->bd->bi_boot_params = 0x30000100;

	return 0;
}

int dram_init (void)
{
	gd->bd->bi_dram[0].start = PHYS_SDRAM_1;
	gd->bd->bi_dram[0].size = PHYS_SDRAM_1_SIZE;

	return 0;
}

#ifdef CONFIG_SURPORT_WINCE

extern S3C24X0_GPIO * gpioregs;

//***************************[ PORTS ]****************************************************
void Wince_Port_Init(void)
{
    //CAUTION:Follow the configuration order for setting the ports. 
    // 1) setting value(GPnDAT) 
    // 2) setting control register  (GPnCON)
    // 3) configure pull-up resistor(GPnUP)  

    //32bit data bus configuration  
    //*** PORT A GROUP
    //Ports  : GPA22 GPA21  GPA20 GPA19 GPA18 GPA17 GPA16 GPA15 GPA14 GPA13 GPA12  
    //Signal : nFCE nRSTOUT nFRE   nFWE  ALE   CLE  nGCS5 nGCS4 nGCS3 nGCS2 nGCS1 
    //Binary :  1     1      1  , 1   1   1    1   ,  1     1     1     1
    //Ports  : GPA11   GPA10  GPA9   GPA8   GPA7   GPA6   GPA5   GPA4   GPA3   GPA2   GPA1  GPA0
    //Signal : ADDR26 ADDR25 ADDR24 ADDR23 ADDR22 ADDR21 ADDR20 ADDR19 ADDR18 ADDR17 ADDR16 ADDR0 
    //Binary :  1       1      1      1   , 1       1      1      1   ,  1       1     1      1         
    gpioregs->GPACON = 0x7fffff; 

    //**** PORT B GROUP
    //Ports  : GPB10    GPB9    GPB8    GPB7    GPB6     GPB5    GPB4   GPB3   GPB2     GPB1      GPB0
    //Signal : nXDREQ0 nXDACK0 nXDREQ1 nXDACK1 nSS_KBD nDIS_OFF L3CLOCK L3DATA L3MODE nIrDATXDEN Keyboard
    //Setting: INPUT  OUTPUT   INPUT  OUTPUT   INPUT   OUTPUT   OUTPUT OUTPUT OUTPUT   OUTPUT    OUTPUT 
    //Binary :   00  ,  01       00  ,   01      00   ,  01       01  ,   01     01   ,  01        01  
    gpioregs->GPBCON = 0x044555;
    gpioregs->GPBUP  = 0x7ff;     // The pull up function is disabled GPB[10:0]

    //*** PORT C GROUP
    //Ports  : GPC15 GPC14 GPC13 GPC12 GPC11 GPC10 GPC9 GPC8  GPC7   GPC6   GPC5 GPC4 GPC3  GPC2  GPC1 GPC0
    //Signal : VD7   VD6   VD5   VD4   VD3   VD2   VD1  VD0 LCDVF2 LCDVF1 LCDVF0 VM VFRAME VLINE VCLK LEND  
    //Binary :  10   10  , 10    10  , 10    10  , 10   10  , 10     10  ,  10   10 , 10     10 , 10   10
    gpioregs->GPCCON = 0xaaaaaaaa;       
    gpioregs->GPCUP  = 0xffff;     // The pull up function is disabled GPC[15:0] 

    //*** PORT D GROUP
    //Ports  : GPD15 GPD14 GPD13 GPD12 GPD11 GPD10 GPD9 GPD8 GPD7 GPD6 GPD5 GPD4 GPD3 GPD2 GPD1 GPD0
    //Signal : VD23  VD22  VD21  VD20  VD19  VD18  VD17 VD16 VD15 VD14 VD13 VD12 VD11 VD10 VD9  VD8
    //Binary : 10    10  , 10    10  , 10    10  , 10   10 , 10   10 , 10   10 , 10   10 ,10   10
    //rGPDCON  = 0xaaa9aaaa;   // GPD8: USB_CHG_DIS: Output
    //rGPDUP   = 0xffff;       // The pull up function is disabled GPD[15:0]
    //rGPDDAT &= ~(0x1 << 8);  // Set USB_CHG_DIS Low

    //Ports  : GPD15        GPD14   GPD13   GPD12   GPD11   GPD10   GPD9    GPD8         
    //Signal : USB_PULLUP   NC      NC      NC      NC      nFWP    SELI    USB_CHG_DIS  
    //CON    : 00           00      00      00      00      01      01      01
    //DAT    : 0            0       0       0       0       1       0(100ma)0 (low)
    //UP     : 1            0       0       0       0       1       1       1

    //Ports  : GPD7         GPD6    GPD5    GPD4    GPD3    GPD2    GPD1    GPD0
    //Signal : NC           EL_EN   KEEPACT IR_SD   PWREN2  NC      TXON    NC
    //CON    : 00           01      01      01      01      00      01      00
    //DAT    : 0            0(on)   1(high) 1(on)   0(low)  0       0(low)  0
    //UP     : 0            1       1       1       1       0       1       0
    gpioregs->GPDCON  = 0x00151544;
    gpioregs->GPDDAT  = 0x0430;
    gpioregs->GPDUP   = 0x877A;

    //*** PORT E GROUP
    //Ports  : GPE15  GPE14 GPE13   GPE12   GPE11   GPE10   GPE9    GPE8     GPE7  GPE6  GPE5   GPE4  
    //Signal : IICSDA IICSCL SPICLK SPIMOSI SPIMISO SDDATA3 SDDATA2 SDDATA1 SDDATA0 SDCMD SDCLK I2SSDO 
    //Binary :  10     10  ,  10      10  ,  00      10   ,  10      10   ,   10    10  , 10     10  ,     
    //-------------------------------------------------------------------------------------------------------
    //Ports  :  GPE3   GPE2  GPE1    GPE0    
    //Signal : I2SSDI CDCLK I2SSCLK I2SLRCK     
    //Binary :  10     10  ,  10      10 
    //rGPECON = 0xaaaaaaaa;       
    //rGPEUP  = 0xffff;     // The pull up function is disabled GPE[15:0]
    gpioregs->GPECON = 0xaa2aaaaa;       
    gpioregs->GPEUP  = 0xf7ff;       // GPE11 is NC

    //*** PORT F GROUP
    //Ports  : GPF7   GPF6   GPF5   GPF4      GPF3     GPF2  GPF1   GPF0
    //Signal : nLED_8 nLED_4 nLED_2 nLED_1 nIRQ_PCMCIA EINT2 KBDINT EINT0
    //Setting: Output Output Output Output    EINT3    EINT2 EINT1  EINT0
    //Binary :  01      01 ,  01     01  ,     10       10  , 10     10
    gpioregs->GPFCON = 0x55aa;
    gpioregs->GPFUP  = 0xff;     // The pull up function is disabled GPF[7:0]

    //*** PORT G GROUP
    //Ports  : GPG15 GPG14 GPG13 GPG12  GPG11    GPG10    GPG9     GPG8     GPG7      GPG6    
    //Signal : nYPON  YMON nXPON XMON   NC       SDWP     WHEEL_SW WHEEL_A  WHEEL_B   ONE_WIRE
    //Setting: nYPON  YMON nXPON XMON   IN       EINT18   EINT17   ENT16    EINT15    OUTPUT
    //Binary :   11    11 , 11    11  , 00       01    ,  10       10   ,   10        01
    //UP     :   1     1    1     1     0        1        1        1        1         1
    //-----------------------------------------------------------------------------------------
    //Ports  : GPG5       GPG4      GPG3    GPG2    GPG1     GPG0    
    //Signal : NC         LCD_PWREN NC      nSS_SPI JACK_CLK JACK_DATA
    //Setting: IN         LCD_PWRDN IN      nSS0    EINT9    EINT8
    //Binary : 00         11   ,    00      11  ,   10       10
    //UP     : 0          1         0       1       1        1
// Behavior changed from board rev A to rev D. Don't power LCD down.
    
    //*** PORT H GROUP
    //Ports  :  GPH10    GPH9  GPH8 GPH7  GPH6  GPH5 GPH4 GPH3 GPH2 GPH1  GPH0 
    //Signal : CLKOUT1 CLKOUT0 UCLK nCTS1 nRTS1 RXD1 TXD1 RXD0 TXD0 nRTS0 nCTS0
    //Binary :   01   ,  01     10 , 11    11  , 10   10 , 10   10 , 10    10    
    gpioregs->GPHCON = 0x16faaa;
    gpioregs->GPHUP  = 0x7ff;    // The pull up function is disabled GPH[10:0]

    //External interrupt will be falling edge triggered. 
    gpioregs->EXTINT0 = 0x22222222;    // EINT[7:0]
    gpioregs->EXTINT1 = 0x22222222;    // EINT[15:8]
    gpioregs->EXTINT2 = 0x22222222;    // EINT[23:16]
}
#endif /* CONFIG_SURPORT_WINCE */

