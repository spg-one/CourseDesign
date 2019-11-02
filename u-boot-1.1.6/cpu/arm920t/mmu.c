/******************************************************************************
 * mmu.c
 * 
 * Copyright (c) 2010 by www.embedsky.net
 * 
 * DESCRIPTION: - MMU的操作，适用于ARMv4
 * 					清理: 将脏的项内容回写，对write-back有效
 *					清除: 将项无效，并不回写
 *					I-cache 不需要进行清理
 *					D-cache 应该先清理后清除
 * Modification history
 ******************************************************************************/

#include <common.h>
 
#if defined(CONFIG_USE_MMU)
 /* See also ARM920T Technical Reference Manual */
#define C1_MMU			(1<<0)		/* mmu off/on */
#define C1_ALIGN		(1<<1)		/* alignment faults off/on */
#define C1_DC			(1<<2)		/* dcache off/on */
 
#define C1_BIG_ENDIAN		(1<<7)		/* big endian off/on */
#define C1_SYS_PROT		(1<<8)		/* system protection */
#define C1_ROM_PROT		(1<<9)		/* ROM protection */
#define C1_IC			(1<<12)		/* icache off/on */
#define C1_HIGH_VECTORS		(1<<13)		/* location of vectors: low/high addresses */

/* read co-processor 15, register #1 (control register) */
static unsigned long read_p15_c1 (void)
{
	unsigned long value;

	__asm__ __volatile__(
		"mrc	p15, 0, %0, c1, c0, 0   @ read control reg\n"
		: "=r" (value)
		:
		: "memory");

	return value;
}

/* write to co-processor 15, register #1 (control register) */
static void write_p15_c1 (unsigned long value)
{
	__asm__ __volatile__(
		"mcr	p15, 0, %0, c1, c0, 0   @ write it back\n"
		:
		: "r" (value)
		: "memory");
}

static void cp_delay (void)
{
	volatile int i;

	/* copro seems to need some delay between reading and writing */
	for (i = 0; i < 100; i++);
}

/* cache_bit must be either C1_IC or C1_DC */
static void cache_enable(uint32_t cache_bit)
{
	uint32_t reg;

	reg = read_p15_c1();	/* get control reg. */
	cp_delay();
	write_p15_c1(reg | cache_bit);
}

/* cache_bit must be either C1_IC or C1_DC */
static void cache_disable(uint32_t cache_bit)
{
	uint32_t reg;

	reg = read_p15_c1();
	cp_delay();
	write_p15_c1(reg & ~cache_bit);
}

static void mmu_regist_ttb(ulong ttb_base)
{
	/* 注册一级页表的地址 */
	__asm__ volatile (
		"mcr	p15, 0, %0, c2, c0, 0\n"
		:
		: "r" (ttb_base)
		: "memory"
	);
}

static void mmu_set_domain(ulong domain)
{
	__asm__ volatile (
		"mcr	p15, 0, %0, c3, c0, 0\n"
		: 
		: "r" (domain)
		: "memory"
	);
}

static void mmu_invalid_tlb(void)
{
	ulong rx = 0;
	
	__asm__ volatile (
		"mcr	p15, 0, %0, c8, c7, 0\n"
		:
		: "r" (rx)
		: "memory"
	);
}

static void mmu_flush_cache_index(ulong rx)
{
	/* 清除TLB中的索引项 */
	__asm__ volatile (
		"mcr	p15, 0, %0, c7, c14, 2\n"
		:
		: "r" (rx)
		: "memory"
	);
}

static void mmu_flush_D_cache(void)
{
	/* 测试并清理、清除D-cache的内容 */
//	__asm__ volatile (
//		"1:\n"
//		"	mrc	p15, 0, pc, c7, c14, 3\n"
//		"	bne	1b\n"
//	);
}

static void mmu_flush_ID_cache(void)
{
	ulong rx=0;

	/* 测试并清理、清除D-cache的内容 */
	mmu_flush_D_cache();
	
	/* 清除I-cache的内容 */
	__asm__ volatile (
		"mcr	p15, 0, %0, c7, c5, 0\n"
		:
		: "r" (rx)
		: "memory"
	);
}

static void mmu_invalid_ID_cache(void)
{
	ulong rx = 0;
	
	/* 清除I/D-cache的内容 */
	__asm__ volatile (
		"mcr	p15, 0, %0, c7, c7, 0\n"
		"mcr	p15, 0, %0, c7, c5, 0\n"		// invalidate I cache
		"mcr	p15, 0, %0, c7, c10, 4\n"		// drain WB
		: 
		: "r" (rx)
		: "memory"
	);

}

static void mmu_flush_tlb(void)
{
	int i,k, x;
	
	for (i = 0; i < 64; i++) {
		for (k = 0; k < 8; k++) {
			x = (i << 26) | (k << 5);
			mmu_flush_cache_index(x);
		}
	}
}

static void mmu_cache_flush(void)
{
	/* 清理并清除 I/D cache内容 */
	mmu_flush_ID_cache();
	
	/* 清除 TLB 的内容 */
	mmu_flush_tlb();
}

void mmu_map_section(ulong ttbbase, ulong virtaddr, ulong physaddr, ulong flag)
{
	ulong item;

	flag = flag & 0x00000DFC; /* bit9,bit1,bit0 = 0, AP,domain,C,B valid */
	flag |= 0x02;	/* bit[1:0] = 10b, section descritptor */
	item = (physaddr & 0xFFF00000) | flag;
	*(ulong *)(ttbbase + (virtaddr >> 20) * 4) = item;	
}

void mmu_d_cache_flush(void)
{
	/* 清除 D-Cache */
	mmu_flush_D_cache();
	
	/* 清除 TLB 的内容 */
	mmu_flush_tlb();
}

void mmu_init(ulong ttb_base)
{
	/* 禁止MMU I/D-cache */
	cache_disable(C1_IC | C1_DC | C1_MMU);

	/* 清理并清除 cache 内容 */
	mmu_cache_flush();
	
	/* 使无效整个cache */
	mmu_invalid_ID_cache();
	
	/* 使无效整个TLB */
	mmu_invalid_tlb();

	/* 设置mmu的domain，16个全部是管理者权限 */
	mmu_set_domain(0xffffffff);

	/* 设置转换表地址 */
	mmu_regist_ttb(ttb_base);
}

void mmu_enable(void)
{
	cache_enable(C1_IC | C1_DC | C1_MMU);	/* icache & dcache enable */
}

void mmu_disable(void)
{
	mmu_cache_flush();
	cache_disable(C1_IC | C1_DC | C1_MMU);
}

//#endif /* CONFIG_USE_MMU */
#elif defined(CONFIG_TQ2440_MMU)

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
static unsigned long *mmu_tlb_base = (unsigned long *) MMU_TABLE_BASE;

static inline void arm920_setup(void)
{
	unsigned long ttb = MMU_TABLE_BASE;

__asm__(
	/* Invalidate caches */
	"mov	r0, #0\n"
	"mcr	p15, 0, r0, c7, c7, 0\n"	/* invalidate I,D caches on v4 */
	"mcr	p15, 0, r0, c7, c10, 4\n"	/* drain write buffer on v4 */
	"mcr	p15, 0, r0, c8, c7, 0\n"	/* invalidate I,D TLBs on v4 */
#if 0
	/* Load page table pointer */
	"mov	r4, %0\n"
	"mcr	p15, 0, r4, c2, c0, 0\n"	/* load page table pointer */
	/* Write domain id (cp15_r3) */
	"mvn	r0, #0\n"			/* Domains 0, 1 = client */
	"mcr	p15, 0, r0, c3, c0, 0\n"	/* load domain access register */
	/* Set control register v4 */
	"mrc	p15, 0, r0, c1, c0, 0\n"	/* get control register v4 */
	/* Clear out 'unwanted' bits (then put them in if we need them) */
						/* .RVI ..RS B... .CAM */ 
	"bic	r0, r0, #0x3000\n"		/* ..11 .... .... .... */
	"bic	r0, r0, #0x0300\n"		/* .... ..11 .... .... */
	"bic	r0, r0, #0x0087\n"		/* .... .... 1... .111 */
	/* Turn on what we want */
	/* Fault checking enabled */
	"orr	r0, r0, #0x0002\n"		/* .... .... .... ..1. */
	"orr	r0, r0, #0x0004\n"		/* .... .... .... .1.. */
	"orr	r0, r0, #0x1000\n"		/* ...1 .... .... .... */
	/* MMU enabled */
	"orr	r0, r0, #0x0001\n"		/* .... .... .... ...1 */
	"mcr	p15, 0, r0, c1, c0, 0\n"	/* write control register */
	: /* no outputs */
	: "r" (ttb) 
#endif
	"mrc	p15, 0, r0, c1, c0, 0\n"	/* get control register v4 */
	"and	r0, r0, #0xC0000000\n"
	"bic	r0, r0, #0x0079\n"		/* .... .... 1... .111 */
	"orr	r0, r0, #0x4000\n"		/* .... .... .... ..1. */
	"orr	r0, r0, #0x1000\n"		/* ...1 .... .... .... */
	"orr	r0, r0, #0x0004\n"		/* .... .... .... .1.. */
	/* MMU enabled */
	"mcr	p15, 0, r0, c1, c0, 0\n"	/* write control register */
	);
}

void tq2440_mmu_init(void)
{
#if 0
	arm920_setup();
#else
	unsigned long ttb = 0x30000000;
	__asm__(
		"mov	r0, #0\n"
		"mcr	p15, 0, r0, c7, c7, 0\n"
		"mcr	p15, 0, r0, c7, c10, 4\n"
		"mcr	p15, 0, r0, c8, c7, 0\n"
//		"mov	r4, %0\n"
		"mcr	p15, 0, %0, c2, c0, 0\n"
		"mvn	r0, #0\n"
		"mcr	p15, 0, r0, c3, c0, 0\n"
		"mrc	p15, 0, r0, c1, c0, 0\n"
		"bic	r0, r0, #0x3000\n"
		"bic	r0, r0, #0x0300\n"
		"bic	r0, r0, #0x0087\n"
		"orr	r0, r0, #0x0002\n"
		"orr	r0, r0, #0x1000\n"
		"orr	r0, r0, #0x0001\n"
		"mcr	p15, 0, r0, c1, c0, 0\n"
		:
		:"r"(ttb)
		: "memory"
		);
#endif
}

/*
 * cpu_arm920_cache_clean_invalidate_all()
 *
 * clean and invalidate all cache lines
 *
 */
static inline void cpu_arm920_cache_clean_invalidate_all(void)
{
__asm__(
	"	mov	r1, #0\n"
	"	mov	r1, #7 << 5\n"		  /* 8 segments */
	"1:	orr	r3, r1, #63 << 26\n"	  /* 64 entries */
	"2:	mcr	p15, 0, r3, c7, c14, 2\n" /* clean & invalidate D index */
	"	subs	r3, r3, #1 << 26\n"
	"	bcs	2b\n"			  /* entries 64 to 0 */
	"	subs	r1, r1, #1 << 5\n"
	"	bcs	1b\n"			  /* segments 7 to 0 */
	"	mcr	p15, 0, r1, c7, c5, 0\n"  /* invalidate I cache */
	"	mcr	p15, 0, r1, c7, c10, 4\n" /* drain WB */
	);
}

/*
 * cpu_arm920_tlb_invalidate_all()
 *
 * Invalidate all TLB entries
 */
static inline void cpu_arm920_tlb_invalidate_all(void)
{
	__asm__(
		"mov	r0, #0\n"
		"mcr	p15, 0, r0, c7, c10, 4\n"	/* drain WB */
		"mcr	p15, 0, r0, c8, c7, 0\n"	/* invalidate I & D TLBs */
		);
}

/*********************************************************************
* 段页表项entry:[31:20]段基址，[11:10]为AP(控制访问权限)，[8:5]域，
*   [3:2]=CP(decide cached&buffered)，[1:0]=0b10-->页表为段
* MMU_SECDESC:
*   AP=0b11
*   DOMAIN=0
*   [1:0]=0b10--->页表为段
* MMU_CACHEABLE:
*   C=1(bit[3])
*
* 本函数先将4G虚拟空间万全映射到相同的物理地址上:Section,not cacacheable, not bufferable
* 再修改虚拟地址0x30000000到0x33f00000,映射到SDRAM的64M物理地址:Section, cacacheable, not bufferable(即write-through mode,写通)
**********************************************************************/
static inline void mem_mapping_linear(void)
{
	unsigned long pageoffset, sectionNumber;

#if 0
	/* 4G 虚拟地址映射到相同的物理地址. not cacacheable, not bufferable */
	for (sectionNumber = 0; sectionNumber < 4096; sectionNumber++) {
		pageoffset = (sectionNumber << 20);
		*(mmu_tlb_base + (pageoffset >> 20)) = pageoffset | MMU_SECDESC;
	}

	/* make sdram cacheable */
	/* SDRAM物理地址0x3000000-0x33ffffff,DRAM_BASE=0x30000000,DRAM_SIZE=64M*/
	for (pageoffset = IRQ_STACK_BASE & (~(SZ_1M - 1)); pageoffset < (PHYS_SDRAM_1+PHYS_SDRAM_1_SIZE); pageoffset += SZ_1M) {
		//DPRINTK(3, "Make DRAM section cacheable: 0x%08lx\n", pageoffset);
		*(mmu_tlb_base + (pageoffset >> 20)) = pageoffset | MMU_SECDESC | MMU_CACHEABLE; 
	}
#else
	*(mmu_tlb_base + (0x0 >> 20)) = (0x0) | MMU_SECDESC_WB;
	*(mmu_tlb_base + (0x30000000 >> 20)) = (0x30000000) | MMU_SECDESC_WB;
#endif
}

void tq2440_mem_map_init(void)
{
#if 0
	mem_mapping_linear();
	cpu_arm920_cache_clean_invalidate_all();
	cpu_arm920_tlb_invalidate_all();
#else
	unsigned long virtuladdr, physicaladdr;
	unsigned long *mmu_tlb_base = (unsigned long *)MMU_TABLE_BASE;

	virtuladdr = 0;
	physicaladdr = 0;
	*(mmu_tlb_base + (virtuladdr >> 20)) = (physicaladdr & 0xFFF00000) | MMU_SECDESC_WB;

/*	virtuladdr = 0x5600000;
	physicaladdr = 0x56000000;
	*(mmu_tlb_base + (virtuladdr >> 20)) = (physicaladdr & 0xFFF00000) | MMU_SECDESC;
*/
	virtuladdr = 0xB0000000;
	physicaladdr = 0x30000000;
	while(virtuladdr < 0xB4000000)
	{
		*(mmu_tlb_base + (virtuladdr >> 20)) = (physicaladdr & 0xFFF00000) | MMU_SECDESC_WB;
		virtuladdr += 0x100000;
		physicaladdr += 0x100000;
	}
	
#endif
}
#endif /* CONFIG_TQ2440_MMU */
