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

uint8_t axi_setup_done = 0;

void *h2p_rom2_addr;

int axi_setup() {
	printf("HPS AXI Bridge Setup. ElectronAsh / dentnz\n\n");

	void *axi_virtual_base;

	int fd;

	printf("Running mmap...\n");
	if ( ( fd = open( "/dev/mem", ( O_RDWR | O_SYNC ) ) ) == -1 ) {
		printf( "ERROR: could not open \"/dev/mem\"...\n\n" );
		return( 1 );
	}
	else {
		printf("mmap completed OK...\n\n");	
	}

	//HPS-to-FPGA bridge
	axi_virtual_base = mmap( NULL, HW_FPGA_AXI_SPAN, ( PROT_READ | PROT_WRITE ), MAP_SHARED, fd, ALT_AXI_FPGASLVS_OFST );

	printf("Mapping the HPS-to_FPGA bridge...\n");
	if ( axi_virtual_base == MAP_FAILED ) {
		printf( "ERROR: axi mmap() failed...\n\n" );
		close( fd );
		return( 1 );
	}
	else {
		printf("HPS-to_FPGA bridge mapped OK. ");
	}

	h2p_rom2_addr = axi_virtual_base + ( ( unsigned long  )( BRIDGE_0_BASE ) & ( unsigned long)( HW_FPGA_AXI_MASK ) );

	return 0;
}


int main()
{
	uint32_t reg0;

	
	//if (!axi_setup_done) {	// AXI setup NOT done yet!
		//axi_setup_done = 1;
		axi_setup();
	//}
	//else {					// AXI setup done!...
		printf("h2p_rom2_addr: 0x%08X\n", (unsigned int)h2p_rom2_addr );
		//printf("Sending data to AXI bridge...\n");
		//*((uint32_t *)h2p_rom2_addr+0) = 0x0123ABCD;
		//return 0;
	//}
	
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
	
	// Write to GP0...
	*((uint32_t *)h2p_rom2_addr+0x0) = 0x02FFFFFF; // Fill Rect WHITE
	*((uint32_t *)h2p_rom2_addr+0x0) = 0x00000000; // Start pos 0,0
	*((uint32_t *)h2p_rom2_addr+0x0) = 0x00100010; // Size 16x16 pixel.
	
	// Read flags / myDebugCnt.
	reg0 = *((uint32_t *)h2p_rom2_addr+0x00000008);
	printf("myDebugCnt: 0x%08X\n", reg0&0x0FFFFFFF);
	
	// Read flags / myDebugCnt.
	reg0 = *((uint32_t *)h2p_rom2_addr+0x00000008);
	printf("myDebugCnt: 0x%08X\n", reg0&0x0FFFFFFF);
	
	//*((uint32_t *)h2p_rom2_addr+0x4) = 0xDEADBEEF; // Write to GP1.
	//*((uint32_t *)h2p_rom2_addr+0x8) = 0x40000000; // Write to DMA_ACK / gpu_nrst. (keep gpu_nrst HIGH!)

	// Read flags / myDebugCnt.
	//reg0 = *((uint32_t *)h2p_rom2_addr+0x00000008);
	//printf("myDebugCnt: 0x%08X\n", reg0&0x0FFFFFFF);
	
	//reg0 = *((uint32_t *)h2p_rom2_addr+0xC);		 // Read DMA (no gpuSel nor read/write pulse).
	//*((uint32_t *)h2p_rom2_addr+0xC) = 0xCAFEBABE; // Write DMA (no gpuSel nor read/write pulse).
	
	return 0;
}


