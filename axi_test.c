#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/types.h>

//#include "../Main_MiSTerMSU/file_io.h"
//#include "../Main_MiSTerMSU/user_io.h"
//#include "../Main_MiSTerMSU/menu.h"

// DMA testing stuff. ElectronAsh...
#define DEBUG
#include <sys/mman.h>
//#include "hwlib.h"
//#include "sgdma.h"
//#include "hps_0.h"
//#include "dma.h"

//setting for the HPS2FPGA AXI Bridge
#define ALT_AXI_FPGASLVS_OFST (0xC0000000)	// axi_master
#define HW_FPGA_AXI_SPAN (0x00400000) 		// Bridge span 4MB
#define HW_FPGA_AXI_MASK ( HW_FPGA_AXI_SPAN - 1 )

#define BRIDGE_0_BASE 0x00000
#define BRIDGE_0_SPAN 0xFFFFF
#define BRIDGE_0_END  0xFFFFF

#define HW_FPGA_FB_BASE (0x10000000)
#define HW_FPGA_FB_SPAN (0x00400000) 		// Bridge span 4MB
#define HW_FPGA_FB_MASK ( HW_FPGA_FB_SPAN - 1 )

uint8_t mmap_setup_done = 0;

void *axi_addr;
void *fb_addr;

int mmap_setup() {
	printf("HPS AXI Bridge Setup. ElectronAsh / dentnz\n\n");

	void *axi_virtual_base;
	void *fb_virtual_base;

	int fd;

	printf("Opening /dev/mem, for mmap...\n");
	if ( ( fd = open( "/dev/mem", ( O_RDWR | O_SYNC ) ) ) == -1 ) {
		printf( "ERROR: could not open \"/dev/mem\"...\n\n" );
		return( 1 );
	}
	else {
		printf("/dev/mem opened OK...\n\n");
	}

	axi_virtual_base = mmap( NULL, HW_FPGA_AXI_SPAN, ( PROT_READ | PROT_WRITE ), MAP_SHARED, fd, ALT_AXI_FPGASLVS_OFST );
	printf("Mapping the HPS-to_FPGA bridge for AXI...\n");
	if ( axi_virtual_base == MAP_FAILED ) {
		printf( "ERROR: axi mmap() failed...\n\n" );
		close( fd );
		return( 1 );
	}
	else {
		printf("HPS-to_FPGA bridge (for AXI) mapped OK.\n");
	}
	axi_addr = axi_virtual_base + ( ( unsigned long  )( BRIDGE_0_BASE ) & ( unsigned long)( HW_FPGA_AXI_MASK ) );
	printf("axi_addr: 0x%08X\n\n", (unsigned int)axi_addr );
	
	
	fb_virtual_base = mmap( NULL, HW_FPGA_FB_MASK, ( PROT_READ | PROT_WRITE ), MAP_SHARED, fd, HW_FPGA_FB_BASE );
	printf("Mapping the HPS-to_FPGA bridge for Framebuffer...\n");
	if ( fb_virtual_base == MAP_FAILED ) {
		printf( "ERROR: fb mmap() failed...\n\n" );
		close( fd );
		return( 1 );
	}
	else {
		printf("HPS-to_FPGA bridge (for FB) mapped OK.\n");
	}
	fb_addr = fb_virtual_base + ( ( unsigned long  )( 0x00000000 ) & ( unsigned long)( HW_FPGA_FB_MASK ) );
	printf("fb_addr: 0x%08X\n\n", (unsigned int)fb_addr );

	printf("Closing /dev/mem...\n\n");
	close( fd );

	return 0;
}


int main()
{
	uint32_t reg0;
	uint32_t reg1;
	uint32_t reg2;
	uint32_t reg3;
	uint32_t reg4;
	uint32_t reg5;
	uint32_t reg6;
	uint32_t reg7;

	mmap_setup();

/*	
ADR +0 = Write / Read to GP0
ADR +4 = Write / Read to GP1
ADR +8 = Bit 0:27 myDebugCnt (GPU counter cycle)
         Bit 28 dbg_canWrite
         Bit 29 IRQ Read 
         Bit 30 DMA_Req Read / gpu_nrst (Write)
         Bit 31 DMA_Ack (Write)
ADR +12= Read Data bus (cpuDataOut), without any other CPU signal.
         Write Data bus (cpuDataIn) + DMA_ACK = true.
*/

	for (int i=0; i<256; i++) {
		*((uint32_t *)fb_addr+i) = 0xCDCDCDCD;
	}
	
	printf("Resetting the GPU core...\n");
	*((uint32_t *)axi_addr+0x000000002) = 0x00000000; // Write to DMA_ACK / gpu_nrst. Assert gpu_nrst (LOW).
	sleep(1);
	*((uint32_t *)axi_addr+0x000000002) = 0x40000000; // Write to DMA_ACK / gpu_nrst. Bring gpu_nrst HIGH again.
	sleep(1);										  // Might need a slight delay before sending commands! ElectronAsh.
	
	
	/*
	*((uint32_t *)axi_addr+0x000000000) = 0x02FFFFFF; // Fill Rect WHITE
	*((uint32_t *)axi_addr+0x000000000) = 0x00000000; // Start pos 0,0
	*((uint32_t *)axi_addr+0x000000000) = 0x00040010; // Size 16x4 pixel.	
	sleep(1);
	*/
	
	// Write to GP0...
	*((uint32_t *)axi_addr+0x000000000) = 0x02FFFFFF; // Fill Rect WHITE
	*((uint32_t *)axi_addr+0x000000000) = 0x00000000; // Start pos 0,0
	*((uint32_t *)axi_addr+0x000000000) = 0x00100010; // Size 16x16 pixel.
	
	//*((uint32_t *)axi_addr+0x000000000) = 0x02FFFFFF; // Fill Rect WHITE
	//*((uint32_t *)axi_addr+0x000000000) = 0x00800080; // Start pos 128,128, probably?
	//*((uint32_t *)axi_addr+0x000000000) = 0x00100010; // Size 16x16 pixel.
	
	/*
	*((uint32_t *)axi_addr+0x000000000) = 0x02FFFFFF; // Fill Rect WHITE
	*((uint32_t *)axi_addr+0x000000000) = 0x00000000; // Start pos 0,0
	*((uint32_t *)axi_addr+0x000000000) = 0x00040010; // Size 16x4 pixel.	
	*/
	
	/*
	*((uint32_t *)axi_addr+0x000000000) = 0x300000FF;	// TriangleGouraud.
	*((uint32_t *)axi_addr+0x000000000) = 0x00000000;
	*((uint32_t *)axi_addr+0x000000000) = 0x0000FF00;
	*((uint32_t *)axi_addr+0x000000000) = 0x0000000F;
	*((uint32_t *)axi_addr+0x000000000) = 0x00FF0000;
	*((uint32_t *)axi_addr+0x000000000) = 0x000F0000;
	*/
	
	/*
	*((uint32_t *)axi_addr+0x000000000) = 0x300000B2;	// TriangleGouraud, from the BIOS logo.
	*((uint32_t *)axi_addr+0x000000000) = 0x010301A0;
	*((uint32_t *)axi_addr+0x000000000) = 0x00008CB2;
	*((uint32_t *)axi_addr+0x000000000) = 0x00A0013D;
	*((uint32_t *)axi_addr+0x000000000) = 0x00008CB2;
	*((uint32_t *)axi_addr+0x000000000) = 0x0166013D;
	*/
	
	/*
	*((uint32_t *)axi_addr+0x000000000) = 0x380000B2;	// QuadGouraud, from the BIOS Logo.
	*((uint32_t *)axi_addr+0x000000000) = 0x00F000C0;
	*((uint32_t *)axi_addr+0x000000000) = 0x00008CB2;
	*((uint32_t *)axi_addr+0x000000000) = 0x00700140;
	*((uint32_t *)axi_addr+0x000000000) = 0x00008CB2;
	*((uint32_t *)axi_addr+0x000000000) = 0x01700140;
	*((uint32_t *)axi_addr+0x000000000) = 0x000000B2;
	*((uint32_t *)axi_addr+0x000000000) = 0x00F001C0;
	*/
	
	sleep(1);
	
	/*
	for (int i=0; i<256; i+=8) {			
		reg0 = *((uint32_t *)fb_addr+i+0);
		reg1 = *((uint32_t *)fb_addr+i+1);
		reg2 = *((uint32_t *)fb_addr+i+2);
		reg3 = *((uint32_t *)fb_addr+i+3);
		reg4 = *((uint32_t *)fb_addr+i+4);
		reg5 = *((uint32_t *)fb_addr+i+5);
		reg6 = *((uint32_t *)fb_addr+i+6);
		reg7 = *((uint32_t *)fb_addr+i+7);
		printf("%08X %08X %08X %08X %08X %08X %08X %08X\n", reg0, reg1, reg2, reg3, reg4, reg5, reg6, reg7);
	}
	*/

	for (int i=0; i<256; i+=8) {			
		reg0 = *((uint32_t *)fb_addr+i+0);	// TESTING!! Skipping every odd 32-bit WORD atm,
		reg1 = *((uint32_t *)fb_addr+i+2);	// as DDRAM is set up as 64-bit wide on the FPGA side, but we're only writing 32 bits. ElectronAsh.
		reg2 = *((uint32_t *)fb_addr+i+4);
		reg3 = *((uint32_t *)fb_addr+i+6);
		reg4 = *((uint32_t *)fb_addr+i+8);
		reg5 = *((uint32_t *)fb_addr+i+10);
		reg6 = *((uint32_t *)fb_addr+i+12);
		reg7 = *((uint32_t *)fb_addr+i+14);
		printf("%08X %08X %08X %08X %08X %08X %08X %08X\n", reg0, reg1, reg2, reg3, reg4, reg5, reg6, reg7);
	}

	
	// Read flags / myDebugCnt.
	reg0 = *((uint32_t *)axi_addr+0x000000002);
	printf("flag bits:  0x%01X\n", (reg0&0xF0000000)>>28 );
	printf("myDebugCnt: 0x%08X\n", reg0&0x0FFFFFFF);
	
	//*((uint32_t *)axi_addr+0x1) = 0xDEADBEEF; // Write to GP1.
	//*((uint32_t *)axi_addr+0x2) = 0x40000000; // Write to DMA_ACK / gpu_nrst. (keep gpu_nrst HIGH!)
	
	//reg0 = *((uint32_t *)axi_addr+0x3);		 // Read DMA (no gpuSel nor read/write pulse).
	//*((uint32_t *)axi_addr+0x3) = 0xCAFEBABE; // Write DMA (no gpuSel nor read/write pulse).
	
	return 0;
}


