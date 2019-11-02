#include <common.h>
#include <watchdog.h>
#include <command.h>
#include <image.h>
#include <malloc.h>
#include <zlib.h>
#include <bzlib.h>
#include <environment.h>
#include <asm/byteorder.h>

int do_hello()
{
    printf("hello");
    return 0;
}

U_BOOT_CMD(
 	hello,	CFG_MAXARGS,	1,	do_hello,
 	"hello   hhhhhhh\n",
 	"long hello"
);