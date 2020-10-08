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

#include "sony_logo.h"
#include "mem_card.h"

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

	// GPU Framebuffer size of 1024x512.
	// So 524,288 pixels.
	// 2 pixels per 32-bit word (16BPP), so clear 262,144 words.

	// Clear the GPU framebuffer with a known value before starting each test.
	for (int i=0; i<262144; i++) {
		*((uint32_t *)fb_addr+i) = 0x001F001F;
	}
	
	printf("Resetting the GPU core...\n");
	*((uint32_t *)axi_addr+0x000000002) = 0x00000000; // Write to DMA_ACK / gpu_nrst. Assert gpu_nrst (LOW).
	usleep(20000);
	*((uint32_t *)axi_addr+0x000000002) = 0x40000000; // Write to DMA_ACK / gpu_nrst. Bring gpu_nrst HIGH again.
	usleep(20000);
	
	// Write to GP0...
	*((uint32_t *)axi_addr+0x000000000) = 0xE100020A;	// Texpage.
	*((uint32_t *)axi_addr+0x000000000) = 0xE2000000;	// Texwindow.
	*((uint32_t *)axi_addr+0x000000000) = 0xE3000000;	// DrawAreaX1Y1.
	*((uint32_t *)axi_addr+0x000000000) = 0xE4077E7F;	// DrawAreaX2Y2.
	*((uint32_t *)axi_addr+0x000000000) = 0xE5000000;	// DrawAreaOffset.
	*((uint32_t *)axi_addr+0x000000000) = 0xE6000000;	// MaskBits.
	
	// Write to GP0...
	/*
	*((uint32_t *)axi_addr+0x000000000) = 0x02FFFFFF; // Fill Rect WHITE
	*((uint32_t *)axi_addr+0x000000000) = 0x00000000; // Start pos 0,0
	*((uint32_t *)axi_addr+0x000000000) = 0x00100010; // Size 16x16 pixel.
	*/
	
	/*
	*((uint32_t *)axi_addr+0x000000000) = 0x02FFFFFF; // Fill Rect WHITE
	*((uint32_t *)axi_addr+0x000000000) = 0x00400040; // Start pos 64,64
	*((uint32_t *)axi_addr+0x000000000) = 0x00400040; // Size 64x64 pixel.
	*/
	
	/*
	*((uint32_t *)axi_addr+0x000000000) = 0x02FFFFFF; // Fill Rect WHITE
	*((uint32_t *)axi_addr+0x000000000) = 0x00000000; // Start pos 0,0
	*((uint32_t *)axi_addr+0x000000000) = 0x01ff03ff; // Size 511x1023 pixel.
	*/
	
	usleep(500000);	// 500ms Wait for the rendering to finish! TODO - Do a proper polling check for this.
	
	// Write to GP0...
	// (orange diamond, from the BIOS Logo.)
	/*
	*((uint32_t *)axi_addr+0x000000000) = 0x380000B2;	// Color1+Command.  Shaded four-point polygon, opaque.
	*((uint32_t *)axi_addr+0x000000000) = 0x00F000C0;	// Vertex 1.
	*((uint32_t *)axi_addr+0x000000000) = 0x00008CB2;	// Color2.
	*((uint32_t *)axi_addr+0x000000000) = 0x00700140;	// Vertex 2.
	*((uint32_t *)axi_addr+0x000000000) = 0x00008CB2;	// Color3.
	*((uint32_t *)axi_addr+0x000000000) = 0x01700140;	// Vertex 3.
	*((uint32_t *)axi_addr+0x000000000) = 0x000000B2;	// Color4.
	*((uint32_t *)axi_addr+0x000000000) = 0x00F001C0;	// Vertex4.
	*/
	
	//
	// Crash Bandicoot's right ear.
	/*
	*((uint32_t *)axi_addr+0x000000000) = 0x30000828;	// Color1+Command.  Shaded three-point polygon, opaque.
	*((uint32_t *)axi_addr+0x000000000) = 0x001E0019;	// Vertex 1. (YyyyXxxxh)  Y=30. X=25
	*((uint32_t *)axi_addr+0x000000000) = 0x6000003A;	// Color2.   (00BbGgRrh)  
	*((uint32_t *)axi_addr+0x000000000) = 0x001F0028;	// Vertex 2. (YyyyXxxxh)  Y=31. X=40
	*((uint32_t *)axi_addr+0x000000000) = 0x60000039;	// Color3.   (00BbGgRrh)  
	*((uint32_t *)axi_addr+0x000000000) = 0x00030001;	// Vertex 3. (YyyyXxxxh)  Y=3. X=1
	*/
	
	*((uint32_t *)axi_addr+0x000000000) = 0x30FF0000;	// Color1+Command.  Shaded three-point polygon, opaque.
	*((uint32_t *)axi_addr+0x000000000) = 0x00000000;	// Vertex 1. (YyyyXxxxh)  Y=30. X=25
	*((uint32_t *)axi_addr+0x000000000) = 0x0000FF00;	// Color2.   (00BbGgRrh)  
	*((uint32_t *)axi_addr+0x000000000) = 0x0000000F;	// Vertex 2. (YyyyXxxxh)  Y=31. X=40
	*((uint32_t *)axi_addr+0x000000000) = 0x000000FF;	// Color3.   (00BbGgRrh)  
	*((uint32_t *)axi_addr+0x000000000) = 0x000F000F;	// Vertex 3. (YyyyXxxxh)  Y=3. X=1
	
	/*
	*((uint32_t *)axi_addr+0x000000000) = 0x30000828;	// Color1+Command.  Shaded three-point polygon, opaque.
	*((uint32_t *)axi_addr+0x000000000) = 0x001E0019;	// Vertex 1. (YyyyXxxxh)  Y=30. X=25
	*((uint32_t *)axi_addr+0x000000000) = 0x60000B3A;	// Color2.   (00BbGgRrh)  
	*((uint32_t *)axi_addr+0x000000000) = 0x00280040;	// Vertex 2. (YyyyXxxxh)  Y=40. X=64
	*((uint32_t *)axi_addr+0x000000000) = 0x60000B39;	// Color3.   (00BbGgRrh)  
	*((uint32_t *)axi_addr+0x000000000) = 0x00400032;	// Vertex 3. (YyyyXxxxh)  Y=64. X=50
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
	for (int i=0; i<sizeof(sony_logo)/4; i++) {
		*((uint32_t *)axi_addr+0x000000000) = sony_logo[i];
	}
	usleep(500000);	// 500ms Wait for the rendering to finish! TODO - Do a proper polling check for this.
	*/
	
	/*
	for (int i=0; i<sizeof(mem_card)/4; i++) {
		*((uint32_t *)axi_addr+0x000000000) = mem_card[i];
	}
	usleep(500000);	// 500ms Wait for the rendering to finish! TODO - Do a proper polling check for this.
	*/
	
	/*
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
	*/
	printf("\n");
	
	uint32_t offset = 0x0;
	printf("Displaying FB offset: 0x%08X...\n", offset);
	
	for (int i=0; i<128; i++) {
		if ( (i&7)==0 ) printf("\n");
		reg0 = *((uint32_t *)fb_addr+offset+i); printf("%08X ", reg0);
	}
	printf("\n");
	
	
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


