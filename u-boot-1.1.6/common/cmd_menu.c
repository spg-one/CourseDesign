/*
 * tq_668@126.com, www.embedsky.net
 *
 */

#include <common.h>
#include <command.h>
#include <def.h>
#include <nand.h>

#ifdef CONFIG_SURPORT_WINCE
#include "../wince/loader.h"
#endif /* CONFIG_SURPORT_WINCE */

extern char console_buffer[];
extern int readline (const char *const prompt);
extern char awaitkey(unsigned long delay, int* error_p);
extern void download_nkbin_to_flash(void);
extern int boot_zImage(ulong from, size_t size);
extern char bLARGEBLOCK;
extern void ClearFreeMem(void);

void param_menu_usage(void)
{
	printf("\r\n##### Parameter Menu #####\r\n");
	printf("[1] Set NFS boot parameter \r\n");
	printf("[2] Set Yaffs boot parameter \r\n");
	printf("[3] Set parameter \r\n");
	printf("[4] View the parameters\r\n");
	printf("[d] Delete parameter \r\n");
	printf("[s] Save the parameters to Nand Flash \r\n");
	printf("[q] Return main Menu \r\n");
	printf("Enter your selection: ");
}


void param_menu_shell(void)
{
	char c;
	char cmd_buf[256];
	char param_buf1[25];
	char param_buf2[25];
	char param_buf3[25];
	char param_buf4[64];

	while (1)
	{
		param_menu_usage();
		c = awaitkey(-1, NULL);
		printf("%c\n", c);
		switch (c)
		{
			case '1':
			{
				sprintf(cmd_buf, "setenv bootargs ");

				printf("Enter the PC IP address:(xxx.xxx.xxx.xxx)\n");
				readline(NULL);
				strcpy(param_buf1,console_buffer);
				printf("Enter the SKY2440/TQ2440 IP address:(xxx.xxx.xxx.xxx)\n");
				readline(NULL);
				strcpy(param_buf2,console_buffer);
				printf("Enter the Mask IP address:(xxx.xxx.xxx.xxx)\n");
				readline(NULL);
				strcpy(param_buf3,console_buffer);
				printf("Enter NFS directory:(eg: /opt/EmbedSky/root_nfs)\n");
				readline(NULL);
				strcpy(param_buf4,console_buffer);
				sprintf(cmd_buf, "setenv bootargs console=ttySAC0 root=/dev/nfs nfsroot=%s:%s ip=%s:%s:%s:%s:SKY2440.embedsky.net:eth0:off", param_buf1, param_buf4, param_buf2, param_buf1, param_buf2, param_buf3);
				printf("bootargs: console=ttySAC0 root=/dev/nfs nfsroot=%s:%s ip=%s:%s:%s:%s:SKY2440.embedsky.net:eth0:off\n",param_buf1, param_buf4, param_buf2, param_buf1, param_buf2, param_buf3);

				run_command(cmd_buf, 0);
				break;
			}

			case '2':
			{
			#if CONFIG_128MB_SDRAM
				sprintf(cmd_buf, "setenv bootargs noinitrd root=/dev/mtdblock2 init=/linuxrc console=ttySAC0 mem=128M");
				printf("bootargs: noinitrd root=/dev/mtdblock2 init=/linuxrc console=ttySAC0 mem=128M\n");
			#else
				sprintf(cmd_buf, "setenv bootargs noinitrd root=/dev/mtdblock2 init=/linuxrc console=ttySAC0");
				printf("bootargs: noinitrd root=/dev/mtdblock2 init=/linuxrc console=ttySAC0\n");
			#endif

				run_command(cmd_buf, 0);
				break;
			}

			case '3':
			{
				sprintf(cmd_buf, "setenv ");

				printf("Name: ");
				readline(NULL);
				strcat(cmd_buf, console_buffer);

				printf("Value: ");
				readline(NULL);
				strcat(cmd_buf, " ");
				strcat(cmd_buf, console_buffer);
				printf("%s\n",cmd_buf);

				run_command(cmd_buf, 0);
				break;
			}

			case '4':
			{
				strcpy(cmd_buf, "printenv ");
				printf("Name(enter to view all paramters): ");
				readline(NULL);
				strcat(cmd_buf, console_buffer);
				run_command(cmd_buf, 0);
				break;
			}

			case 'D':
			case 'd':
			{
				sprintf(cmd_buf, "setenv ");

				printf("Name: ");
				readline(NULL);
				strcat(cmd_buf, console_buffer);

				run_command(cmd_buf, 0);
				break;
			}

			case 'S':
			case 's':
			{
				sprintf(cmd_buf, "saveenv");
				run_command(cmd_buf, 0);
				break;
			}

			case 'Q':
			case 'q':
			{
				//sprintf(cmd_buf, "saveenv");
				//run_command(cmd_buf, 0);
				return;
				break;
			}
		}
	}
}


void lcd_menu_usage(void)
{
	printf("\r\n##### LCD Parameters Menu #####\r\n");
	printf("[1] VBPD       - Set VBPD \r\n");
	printf("[2] VFPD       - Set VFPD \r\n");
	printf("[3] VSPW       - Set VSPW \r\n");
	printf("[4] HBPD       - Set HBPD \r\n");
	printf("[5] HFPD       - Set HFPD \r\n");
	printf("[6] HSPW       - Set HSPW \r\n");
	printf("[7] CLKVAL     - Set CLKVAL \r\n");
	printf("[s] Save the parameters to Nand Flash \r\n");
	printf("[q] Return main Menu \r\n");
	printf("Enter your selection: ");
}


void lcd_menu_shell(void)
{
	char c;
	char cmd_buf[256];

	while (1)
	{
		lcd_menu_usage();
		c = awaitkey(-1, NULL);
		printf("%c\n", c);
		switch (c)
		{
			case '1':
			{
				sprintf(cmd_buf, "setenv dwVBPD");

				printf("Please enter VBPD Value: ");
				readline(NULL);
				strcat(cmd_buf, " ");
				strcat(cmd_buf, console_buffer);
				printf("%s\n",cmd_buf);

				run_command(cmd_buf, 0);
				break;
			}

			case '2':
			{
				sprintf(cmd_buf, "setenv dwVFPD");

				printf("Please enter VFPD Value: ");
				readline(NULL);
				strcat(cmd_buf, " ");
				strcat(cmd_buf, console_buffer);
				printf("%s\n",cmd_buf);

				run_command(cmd_buf, 0);
				break;
			}

			case '3':
			{
				sprintf(cmd_buf, "setenv dwVSPW");

				printf("Please enter VSPW Value: ");
				readline(NULL);
				strcat(cmd_buf, " ");
				strcat(cmd_buf, console_buffer);
				printf("%s\n",cmd_buf);

				run_command(cmd_buf, 0);
				break;
			}

			case '4':
			{
				sprintf(cmd_buf, "setenv dwHBPD");

				printf("Please enter HBPD Value: ");
				readline(NULL);
				strcat(cmd_buf, " ");
				strcat(cmd_buf, console_buffer);
				printf("%s\n",cmd_buf);

				run_command(cmd_buf, 0);
				break;
			}

			case '5':
			{
				sprintf(cmd_buf, "setenv dwHFPD");

				printf("Please enter HFPD Value: ");
				readline(NULL);
				strcat(cmd_buf, " ");
				strcat(cmd_buf, console_buffer);
				printf("%s\n",cmd_buf);

				run_command(cmd_buf, 0);
				break;
			}

			case '6':
			{
				sprintf(cmd_buf, "setenv dwHSPW");

				printf("Please enter HSPW Value: ");
				readline(NULL);
				strcat(cmd_buf, " ");
				strcat(cmd_buf, console_buffer);
				printf("%s\n",cmd_buf);

				run_command(cmd_buf, 0);
				break;
			}

			case '7':
			{
				sprintf(cmd_buf, "setenv dwCLKVAL");

				printf("Please enter CLKVAL Value: ");
				readline(NULL);
				strcat(cmd_buf, " ");
				strcat(cmd_buf, console_buffer);
				printf("%s\n",cmd_buf);

				run_command(cmd_buf, 0);
				break;
			}

			case 'S':
			case 's':
			{
				sprintf(cmd_buf, "saveenv");
				run_command(cmd_buf, 0);
				break;
			}

			case 'Q':
			case 'q':
			{
				//sprintf(cmd_buf, "saveenv");
				//run_command(cmd_buf, 0);
				return;
				break;
			}
		}
	}
}


#define USE_TFTP_DOWN		1
#define USE_USB_DOWN		2

void main_menu_usage(char menu_type)
{

	if (bBootFrmNORFlash())
		printf("\r\n#####	 Boot for Nor Flash Main Menu	#####\r\n");
	else
		printf("\r\n#####	 Boot for Nand Flash Main Menu	#####\r\n");

	if( menu_type == USE_USB_DOWN)
	{
		printf("#####     EmbedSky USB download mode     #####\r\n\n");
	}
	else if( menu_type == USE_TFTP_DOWN)
	{
		printf("#####     EmbedSky TFTP download mode     #####\r\n\n");
	}
	

	if( menu_type == USE_USB_DOWN)
	{
#ifdef CONFIG_WINCE_NK
		printf("[1] Download u-boot or other bootloader to Nand Flash\r\n");
#else
		printf("[1] Download u-boot or STEPLDR.nb1 or other bootloader to Nand Flash\r\n");
#endif /* CONFIG_WINCE_NK */
	}
	else if( menu_type == USE_TFTP_DOWN)
	{
		printf("[1] Download u-boot.bin to Nand Flash\r\n");
	}
	printf("[2] Download Eboot (eboot.nb0) to Nand Flash\r\n");
	printf("[3] Download Linux Kernel (zImage.bin) to Nand Flash\r\n");
	if( menu_type == USE_USB_DOWN)
	{
#ifdef CONFIG_SURPORT_WINCE
		printf("[4] Download WinCE NK.bin to Nand Flash\r\n");
#endif /* CONFIG_SURPORT_WINCE */
#ifdef CONFIG_WINCE_NK
		printf("[4] Download WinCE NK.bin to Nand Flash\r\n");
#endif /* CONFIG_WINCE_NK */
		printf("[5] Download CRAMFS image to Nand Flash\r\n");
	}
	else if( menu_type == USE_TFTP_DOWN)
	{
#ifdef CONFIG_WINCE_NK
		printf("[4] Download WinCE NK.bin to Nand Flash\r\n");
#else
		printf("[4] Download stepldr.nb1 to Nand Flash\r\n");
#endif /* CONFIG_WINCE_NK */
		printf("[5] Set TFTP parameters(PC IP,TQ2440 IP,Mask IP...)\r\n");
	}
	printf("[6] Download YAFFS image (root.bin) to Nand Flash\r\n");
	printf("[7] Download Program (uCOS-II or TQ2440_Test) to SDRAM and Run it\r\n");

	printf("[8] Boot the system\r\n");
	printf("[9] Format the Nand Flash\r\n");
	printf("[0] Set the boot parameters\r\n");
	printf("[a] Download User Program (eg: uCOS-II or TQ2440_Test)\r\n");
	printf("[b] Download LOGO Picture (.bin) to Nand  Flash \r\n");
	printf("[l] Set LCD Parameters \r\n");
	if( menu_type == USE_USB_DOWN)
	{
		printf("[n] Enter TFTP download mode menu \r\n");
	}

	if (bBootFrmNORFlash())
		printf("[o] Download u-boot to Nor Flash\r\n");
	if( menu_type == USE_TFTP_DOWN)
		printf("[p] Test network (TQ2440 Ping PC's IP) \r\n");

	printf("[r] Reboot u-boot\r\n");
	printf("[t] Test Linux Image (zImage)\r\n");
	if( menu_type == USE_USB_DOWN)
	{
		printf("[q] quit from menu\r\n");
	}
	else if( menu_type == USE_TFTP_DOWN)
	{
		printf("[q] Return main Menu \r\n");
	}

	printf("Enter your selection: ");
}


void tftp_menu_shell(void)
{
	char c;
	char cmd_buf[200];
#ifdef CONFIG_WINCE_NK
	int filesize=0;
	char *s;
#endif /* CONFIG_WINCE_NK */

	while (1)
	{
		main_menu_usage(USE_TFTP_DOWN);
		c = awaitkey(-1, NULL);
		printf("%c\n", c);
		switch (c)
		{
			case '1':
			{
#ifdef CONFIG_WINCE_NK
				strcpy(cmd_buf, "tftp 0x30000000 u-boot.bin");
				run_command(cmd_buf, 0);
				s = getenv("filesize");
				filesize = (int)simple_strtoul(s, NULL, 16);
//				printf("filesize=%d\n", filesize);
				if(filesize > (128*1024))
				{
					strcpy(cmd_buf, "nand erase bios; nand write.jffs2 0x30000000 bios $(filesize)");
				}
				else
				{
					strcpy(cmd_buf, "SCEBOOT 0 1 0x30000000");
				}
				run_command(cmd_buf, 0);
#else
				strcpy(cmd_buf, "tftp 0x30000000 u-boot.bin; nand erase bios; nand write.jffs2 0x30000000 bios $(filesize)");
				run_command(cmd_buf, 0);
#endif /* CONFIG_WINCE_NK */
			break;
			}

			case '2':
			{
#ifdef CONFIG_WINCE_NK
				strcpy(cmd_buf, "tftp 0x30000000 eboot.nb0; SCEBOOT 4 4 0x30000000");
				run_command(cmd_buf, 0);
				if(strcmp(getenv("ostype"), "linux") == 0)
				{
					sprintf(cmd_buf, "setenv ostype wince; saveenv");
					run_command(cmd_buf, 0);
				}
#else
				sprintf(cmd_buf, "tftp 0x30000000 eboot.nb0; nand erase eboot; nand write.jffs2 0x30000000 eboot $(filesize)");
				run_command(cmd_buf, 0);
#endif /* CONFIG_WINCE_NK */
				break;
			}

			case '3':
			{
#ifdef CONFIG_WINCE_NK
				if(strcmp(getenv("ostype"), "wince") == 0)
				{
					sprintf(cmd_buf, "setenv ostype linux; saveenv");
					run_command(cmd_buf, 0);
				}
#endif /* CONFIG_WINCE_NK */

				strcpy(cmd_buf, "tftp 0x30000000 zImage.bin; nand erase kernel; nand write.jffs2 0x30000000 kernel $(filesize)");
				run_command(cmd_buf, 0);
				break;
			}

#ifdef CONFIG_WINCE_NK
			case '4':
			{
				ClearFreeMem();
				strcpy(cmd_buf, "tftp  0x31000000 NK.bin; relocateNK 0x31000000 $(filesize); SWince");
				run_command(cmd_buf, 0);
				break;
			}
#else
			case '4':
			{
				strcpy(cmd_buf, "tftp 0x30000000 stepldr.nb1; nand erase kernel; nand write.jffs2 0x30000000 kernel $(filesize)");
				run_command(cmd_buf, 0);
				break;
			}
#endif /* CONFIG_WINCE_NK */

			case '5':
			{
				char param_buf1[25];
				char param_buf2[25];
				char param_buf3[25];

				printf("Enter the TFTP Server(PC) IP address:(xxx.xxx.xxx.xxx)\n");
				readline(NULL);
				strcpy(param_buf1,console_buffer);
				sprintf(cmd_buf, "setenv serverip %s",param_buf1);
				run_command(cmd_buf, 0);

				printf("Enter the SKY2440/TQ2440 IP address:(xxx.xxx.xxx.xxx)\n");
				readline(NULL);
				strcpy(param_buf2,console_buffer);
				sprintf(cmd_buf, "setenv ipaddr %s",param_buf2);
				run_command(cmd_buf, 0);

				printf("Enter the Mask IP address:(xxx.xxx.xxx.xxx)\n");
				readline(NULL);
				strcpy(param_buf3,console_buffer);
				sprintf(cmd_buf, "setenv netmask %s",param_buf3);
				run_command(cmd_buf, 0);

				printf("Save TFTP IP parameters?(y/n)\n");
				if (getc() == 'y' )
				{
					printf("y");
					getc() == '\r';
					printf("\n");
					sprintf(cmd_buf, "saveenv");
					run_command(cmd_buf, 0);
				}
				else
				{
					printf("Not Save it!!!\n");
				}
				break;
			}

			case '6':
			{
				strcpy(cmd_buf, "tftp 0x30000000 root.bin; nand erase root; nand write.yaffs 0x30000000 root $(filesize)");
				run_command(cmd_buf, 0);
				break;
			}

			case '7':
			{
				char tftpaddress[12];
				char filename[32];

				printf("Enter downloads to SDRAM address:\n");
				readline(NULL);
				strcpy(tftpaddress, console_buffer);

				printf("Enter program name:\n");
				readline(NULL);
				strcpy(filename, console_buffer);

				sprintf(cmd_buf, "tftp %s %s", tftpaddress, filename);
				printf("tftp %s %s\n", tftpaddress, filename);
				run_command(cmd_buf, 0);

				sprintf(cmd_buf, "go %s", tftpaddress);
				run_command(cmd_buf, 0);
				break;
			}

			case '8':
			{
#ifdef CONFIG_EMBEDSKY_LOGO
				embedsky_user_logo();					//user's logo display
#endif /* CONFIG_EMBEDSKY_LOGO */

#ifdef CONFIG_SURPORT_WINCE
				if (!TOC_Read())
				{
					/* Launch wince */
					printf("Start WinCE ...\n");
					strcpy(cmd_buf, "wince");
					run_command(cmd_buf, 0);
				}
				else
#endif /* CONFIG_SURPORT_WINCE */
#ifdef CONFIG_WINCE_NK
#if CONFIG_NO_TOC
				if(strcmp(getenv("ostype"), "wince") == 0)
#else
				if(ReadTOC())
#endif /* CONFIG_NO_TOC */
				{
//					ClearFreeMem();
					printf("Start WinCE ...\n");
					LB_NF_Reset();
					Initialize((LPBYTE)0x30000000);
					ReadOSImageFromBootMedia();
					call_linux(0,0,0x30201000);
					printf("fail to call wince\n");
					while(1);
				}
				else
#endif /* CONFIG_WINCE_NK */
				{
					printf("Start Linux ...\n");
					strcpy(cmd_buf, "boot_zImage");
					run_command(cmd_buf, 0);
				}
				break;
			}

			case '9':
			{
				strcpy(cmd_buf, "nand scrub ");
				run_command(cmd_buf, 0);
//				erase_menu_shell();
				break;
			}

			case '0':
			{
				param_menu_shell();
				break;
			}

			case 'A':
			case 'a':
			{
				char filename[32];

				printf("Enter program name:\n");
				readline(NULL);
				strcpy(filename, console_buffer);

				sprintf(cmd_buf, "tftp 0x30000000 %s; nand erase 0x0 $(filesize+1); nand write.jffs2 0x30000000 0x0 $(filesize+1)", filename);
				run_command(cmd_buf, 0);
				break;
			}

			case 'B':
			case 'b':
			{
#ifdef CONFIG_WINCE_NK
				if(strcmp(getenv("ostype"), "wince") == 0)
				{
					strcpy(cmd_buf, "tftp 0x30000000 logo.bin; SCEBOOT 8 8 0x30000000");
				}
				else
#else
				{
					strcpy(cmd_buf, "tftp 0x30000000 logo.bin; nand erase logo; nand write.jffs2 0x30000000 logo $(filesize)");
				}
#endif /* CONFIG_WINCE_NK */
				run_command(cmd_buf, 0);
				break;
			}

			case 'L':
			case 'l':
			{
				lcd_menu_shell();
				break;
			}

			case 'O':
			case 'o':
			{
				if (bBootFrmNORFlash())
				{
					strcpy(cmd_buf, "tftp 0x30000000 u-boot.bin; protect off all; erase 0 +$(filesize); cp.b 0x30000000 0 $(filesize)");
					run_command(cmd_buf, 0);
				}
				break;
			}

			case 'P':
			case 'p':
			{
				char *serverip;
				serverip=getenv("serverip");
				printf("TQ2440 ping PC IP:ping %s\n", serverip);
				sprintf(cmd_buf, "ping %s", serverip);
				run_command(cmd_buf, 0);
				break;
			}

			case 'R':
			case 'r':
			{
				strcpy(cmd_buf, "reset");
				run_command(cmd_buf, 0);
				break;
			}
			
			case 'T':
			case 't':
			{
				strcpy(cmd_buf, "tftp 0x30008000 zImage.bin; test_zImage");
				run_command(cmd_buf, 0);
				break;
			}

			case 'Q':
			case 'q':
			{
				return;
				break;
			}
		}
	}

}

#include <s3c2410.h>
void menu_shell(void)
{
	char c;
	char cmd_buf[200];
#ifdef CONFIG_WINCE_NK
	int filesize=0;
	char *s;
#endif /* CONFIG_WINCE_NK */

	while (1)
	{
		main_menu_usage(USE_USB_DOWN);
		c = awaitkey(-1, NULL);
		printf("%c\n", c);
		switch (c)
		{
			case '1':
			{
#ifdef CONFIG_WINCE_NK
				strcpy(cmd_buf, "usbslave 1 0x30000000");
				run_command(cmd_buf, 0);
				s = getenv("filesize");
				filesize = (int)simple_strtoul(s, NULL, 16);
//				printf("filesize=%d\n", filesize);
				if(filesize > (8192))
				{
					strcpy(cmd_buf, "nand erase bios; nand write.jffs2 0x30000000 bios $(filesize)");
				}
				else
				{
					strcpy(cmd_buf, "SCEBOOT 0 1 0x30000000");
				}
				run_command(cmd_buf, 0);
#else
				strcpy(cmd_buf, "usbslave 1 0x30000000; nand erase bios; nand write.jffs2 0x30000000 bios $(filesize)");
				run_command(cmd_buf, 0);
#endif /* CONFIG_SURPORT_WINCE */
				break;
			}

			case '2':
			{
#ifdef CONFIG_WINCE_NK
				strcpy(cmd_buf, "usbslave 1 0x30000000; SCEBOOT 4 2 0x30000000");
				run_command(cmd_buf, 0);
				if(strcmp(getenv("ostype"), "linux") == 0)
				{
					sprintf(cmd_buf, "setenv ostype wince; saveenv");
					run_command(cmd_buf, 0);
				}
#else
				sprintf(cmd_buf, "usbslave 1 0x30000000; nand erase eboot; nand write.jffs2 0x30000000 eboot $(filesize)");
				run_command(cmd_buf, 0);
#endif /* CONFIG_SURPORT_WINCE */
				break;
			}

			case '3':
			{
#ifdef CONFIG_WINCE_NK
				if(strcmp(getenv("ostype"), "wince") == 0)
				{
					sprintf(cmd_buf, "setenv ostype linux; saveenv");
					run_command(cmd_buf, 0);
				}
#endif /* CONFIG_WINCE_NK */

				strcpy(cmd_buf, "usbslave 1 0x30000000; nand erase kernel; nand write.jffs2 0x30000000 kernel $(filesize)");
				run_command(cmd_buf, 0);
#ifdef CONFIG_SURPORT_WINCE
				if (!TOC_Read())
					TOC_Erase();				
#endif /* CONFIG_SURPORT_WINCE */
				break;
			}

#ifdef CONFIG_SURPORT_WINCE
			case '4':
			{
				download_nkbin_to_flash();
				break;
			}
#endif /* CONFIG_SURPORT_WINCE */
#ifdef CONFIG_WINCE_NK
			case '4':
			{
				ClearFreeMem();
				strcpy(cmd_buf, "usbslave 1 0x31000000; relocateNK 0x31000000 $(filesize); SWince");
				run_command(cmd_buf, 0);
				break;
			}
#endif /* CONFIG_WINCE_NK */

			case '5':
			{
				strcpy(cmd_buf, "usbslave 1 0x30000000; nand erase root; nand write.jffs2 0x30000000 root $(filesize)");
				run_command(cmd_buf, 0);
				break;
			}

			case '6':
			{
				strcpy(cmd_buf, "loadYaffs 0x30000000 root");
				run_command(cmd_buf, 0);
				break;
			}

			case '7':
			{
				extern volatile U32 downloadAddress;
				extern int download_run;
				
				download_run = 1;
				strcpy(cmd_buf, "usbslave 1");
				run_command(cmd_buf, 0);
				download_run = 0;
				sprintf(cmd_buf, "go %x", downloadAddress);
				run_command(cmd_buf, 0);
				break;
			}

			case '8':
			{
#ifdef CONFIG_EMBEDSKY_LOGO
				embedsky_user_logo();					//user's logo display
#endif /* CONFIG_EMBEDSKY_LOGO */

#ifdef CONFIG_SURPORT_WINCE
				if (!TOC_Read())
				{
					/* Launch wince */
					printf("Start WinCE ...\n");
					strcpy(cmd_buf, "wince");
					run_command(cmd_buf, 0);
				}
				else
#endif /* CONFIG_SURPORT_WINCE */
#ifdef CONFIG_WINCE_NK
#if CONFIG_NO_TOC
				if(strcmp(getenv("ostype"), "wince") == 0)
#else
				if(ReadTOC())
#endif /* CONFIG_NO_TOC */
				{
//					ClearFreeMem();
					printf("Start WinCE ...\n");
					LB_NF_Reset();
					Initialize((LPBYTE)0x30000000);
					ReadOSImageFromBootMedia();
					call_linux(0,0,0x30201000);
					printf("fail to call wince\n");
					while(1);
				}
				else
#endif /* CONFIG_WINCE_NK */
				{
					printf("Start Linux ...\n");
					strcpy(cmd_buf, "boot_zImage");
					run_command(cmd_buf, 0);
				}
				break;
			}

			case '9':
			{
				strcpy(cmd_buf, "nand scrub ");
				run_command(cmd_buf, 0);
//				erase_menu_shell();
				break;
			}

			case '0':
			{
				param_menu_shell();
				break;
			}

			case 'A':
			case 'a':
			{
				strcpy(cmd_buf, "usbslave 1 0x30000000; nand erase 0x0 $(filesize+1); nand write.jffs2 0x30000000 0x0 $(filesize+1)");
				run_command(cmd_buf, 0);
				break;
			}

			case 'B':
			case 'b':
			{
#ifdef CONFIG_WINCE_NK
				if(strcmp(getenv("ostype"), "wince") == 0)
				{
					strcpy(cmd_buf, "usbslave 1 0x30000000; SCEBOOT 8 8 0x30000000");
				}
				else
#endif /* CONFIG_WINCE_NK */
				{
					strcpy(cmd_buf, "usbslave 1 0x30000000; nand erase logo; nand write.jffs2 0x30000000 logo $(filesize)");
				}
				run_command(cmd_buf, 0);
				break;
			}

			case 'L':
			case 'l':
			{
				lcd_menu_shell();
				break;
			}

			case 'N':
			case 'n':
			{
				tftp_menu_shell();
				break;
			}

			case 'O':
			case 'o':
			{
				if (bBootFrmNORFlash())
				{
					strcpy(cmd_buf, "usbslave 1 0x30000000; protect off all; erase 0 +$(filesize); cp.b 0x30000000 0 $(filesize)");
					run_command(cmd_buf, 0);
				}
				break;
			}

			case 'R':
			case 'r':
			{
				strcpy(cmd_buf, "reset");
				run_command(cmd_buf, 0);
				break;
			}
			
			case 'T':
			case 't':
			{
				strcpy(cmd_buf, "usbslave 1 0x30008000; test_zImage");
				run_command(cmd_buf, 0);
				break;
			}
			
			case 'Q':
			case 'q':
			{
				return;	
				break;
			}

		}
				
	}
}

int do_menu (cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
	menu_shell();
	return 0;
}

U_BOOT_CMD(
	menu,	3,	0,	do_menu,
	"menu - display a menu, to select the items to do something\n",
	" - display a menu, to select the items to do something"
);

