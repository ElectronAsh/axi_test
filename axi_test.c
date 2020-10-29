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

#include "tm.h"
#include "sony_logo.h"
#include "comp_ent.h"

char psxBuffer[2 * 1024 * 1024];

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

#define HW_FPGA_FB_OFST (0x10000000)
#define HW_FPGA_FB_SPAN (0x00400000) 		// Bridge span 4MB
#define HW_FPGA_FB_MASK ( HW_FPGA_FB_SPAN - 1 )

void *axi_addr;
void *fb_addr;

/*
ADR +0 = Write / Read to GP0
ADR +4 = Write / Read to GP1

ADR +8 = Bit 31 DMA_ACK (Write)
		 Bit 30 DMA_REQ Read / gpu_nrst (Write). Must keep gpu_nrst bit HIGH when writing to DMA_ACK!
		 Bit 29 IRQRequest Read 
         Bit 28 dbg_canWrite
		 Bit 27:0 myDebugCnt (GPU counter cycle)

ADR +12= Read Data bus (cpuDataOut), without any other CPU signal.
         Write Data bus (cpuDataIn) + DMA_ACK = true (pulse).
*/

void writeRaw(uint32_t data) {
	uint32_t* BASE_GPU = (uint32_t *)axi_addr;
	
	while ( !(BASE_GPU[2] & 1<<28) );	// Wait for dbg_canWrite flag (High) before sending data. If LOW, then stay in while loop. TODO: Add timeout?
	BASE_GPU[0] = data;
}

typedef unsigned short u16;
typedef unsigned int   u32;

class GPUManager {
public:
	GPUManager(u32* gpuBase):BASE_GPU(gpuBase),diff(0),writeIdx(0),readIdx(0) {}
	
	bool canPush      () { return (BASE_GPU[2] & 1<<28);    }
	u32  getGPUCycle  () { return BASE_GPU[2] & 0x0FFFFFFF; }

	void StartGPUReset() { BASE_GPU[2] = 0<<30;                 }
	void EndGPUReset  () { BASE_GPU[2] = 1<<30;                 }
	
	bool canWriteCommand() {
		return (diff < MAX_SIZE-2);
	}
	
	void writeCommand (u32 v) {
		// User must check to know, but I check here to avoid memory overwrite.
		if (canWriteCommand()) {
			diff++;
			buffer[writeIdx++] = v;
			if (writeIdx >= MAX_SIZE) { writeIdx = 0; }
		}
	}
	
	void executeInLoop() {
		if (canPush() && diff) {
			writeGP0(buffer[readIdx++]);
			if (readIdx >= MAX_SIZE) { readIdx = 0; }
			diff--;
		}
	}

	void waitUntilWrite(u32 data) {
		while (!canWriteCommand()) { 
			executeInLoop();
		}
		writeCommand(data);
		executeInLoop();
	}
	
private:
	static const u32 MAX_SIZE = 4096;
	
	void writeGP0(u32 value) { BASE_GPU[0] = value; }
	void writeGP1(u32 value) { BASE_GPU[1] = value; }

	u32* BASE_GPU;
	u32 diff;
	u32 readIdx;
	u32 writeIdx;
	u32 buffer[MAX_SIZE];
};


uint8_t mmap_setup_done = 0;


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
	
	
	fb_virtual_base = mmap( NULL, HW_FPGA_FB_SPAN, ( PROT_READ | PROT_WRITE ), MAP_SHARED, fd, HW_FPGA_FB_OFST );
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

void parser(const char* fileName, u16* psxBuffer, GPUManager& mgr, uint32_t delay) {
	FILE* binSrc = fopen(fileName,"rb");

	//printf("feof flag: %d\n", feof(binSrc) );
	
	// ---------------------------------------------------------------------------------
	// Method 1 ----- Read file directly into DDR VRAM of the PSX ?
	// Load dump from VRAM in memory.
	// With this method, stencil cache is not updated correctly on real HW.
	
    u16* buff16 = (u16*)psxBuffer;
    for (int y=0; y < 524288; y++) {
        fread(&buff16[y],sizeof(u16),1,binSrc);
        //if ((y & 0x3FF) == 0) { printf("Y:%i\n", y >> 10); }
        //usleep(1);
    }
	
	//u16* buff16 = (u16*)psxBuffer;
	//fread(psxBuffer,sizeof(u16),1024*512,binSrc);

	/* In the SIM I update directly the chip stencil with this code
	// ----- Sync Stencil cache and VRAM state.
	for (int y=0; y < 512; y++) {
		for (int x=0; x < 1024; x++) {
			bool bBit    = (buff16[x + (y*1024)] & 0x8000) ? true : false;
			setStencil(mod,x,y,bBit);
		}
	}
	*/
	
	//printf("cmdSetup:");

	// ---------------------------------------------------------------------------------
	// Method 2 ---- Create a REAL UPLOAD COMMAND
	// ---- Setup
	u32 setupCommandCount;
	fread(&setupCommandCount, sizeof(u32), 1, binSrc);
	for (int n=0; n < setupCommandCount; n++) {
		u32 cmdSetup;
		fread(&cmdSetup, sizeof(u32),1, binSrc);
		//printf("cmdSetup: 0x%08X  ", cmdSetup);
		writeRaw(cmdSetup);
		if (delay>0) usleep(delay);
	}

	u32 logCommandCount;
	fread(&logCommandCount, sizeof(u32), 1, binSrc);
	for (int n=0; n < logCommandCount; n++) {
		u32 cmdLengthRAW;
		u32 cmdLength;
		u32 cmdType;
		fread(&cmdLengthRAW, sizeof(u32),1, binSrc);
		cmdLength = cmdLengthRAW & 0xFFFFFF;
		cmdType   = cmdLengthRAW >> 24;
		
		u32 operand;
		if (cmdType == 0) { // GP0
			fread(&operand, sizeof(u32),1, binSrc);

			if ((operand>>24) != 0xC0) {
				writeRaw(operand);
				for (int m=1; m < cmdLength; m++) {
					fread(&operand, sizeof(u32),1, binSrc);

					//printf("operand: 0x%08X\n", operand);
					writeRaw(operand);
					if (delay>0) usleep(delay);
				}
			} else {
				// skip the command...
				fread(&operand, sizeof(u32),1, binSrc);
				fread(&operand, sizeof(u32),1, binSrc);
			}
		} else {
			// GP1 to do later... Skip for now...
			for (int m=0; m < cmdLength; m++) {
				fread(&operand, sizeof(u32),1, binSrc);
			}
		}
	}
	
	fclose(binSrc);
}

int main()
{
	uint32_t reg0, reg1, reg2, reg3, reg4, reg5, reg6, reg7;

	mmap_setup();

	GPUManager mgr( ((uint32_t *)axi_addr) );

	/*
	char mybyte0;
	char mybyte1;
	char mybyte2;
	char mybyte3;
	
			
	fread(&mybyte0, 1, 1, fd);
	fread(&mybyte1, 1, 1, fd);
	fread(&mybyte2, 1, 1, fd);
	fread(&mybyte3, 1, 1, fd);
	
	uint32_t myword;
	fread(&myword, 1, 4, fd);
	*((uint32_t *)fb_addr+(i*4)) = myword;
	*/
	
	/*
	*((uint32_t *)fb_addr+0x0) = 0xFC00801F;
	*((uint32_t *)fb_addr+0x1) = 0x8000FFFF;
	*((uint32_t *)fb_addr+0x200) = 0xFC1FFFFF;
	*((uint32_t *)fb_addr+0x201) = 0x8000FFFF;
	*((uint32_t *)fb_addr+0x400) = 0xFFFFFFFF;
	*((uint32_t *)fb_addr+0x401) = 0xFFF0FFF0;
	*((uint32_t *)fb_addr+0x600) = 0xFFFFFFFF;
	*((uint32_t *)fb_addr+0x601) = 0xFFF1FFF1;
	*/
	
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
		*((uint32_t *)fb_addr+i) = 0x00000000; // Black.
		//*((uint32_t *)fb_addr+i) = 0x42104210; // Grey.
	}
	usleep(40000);	// 20ms delay. Just to see the rendering update.
	
	printf("Resetting the GPU core...\n\n");

	mgr.StartGPUReset(); // First thing to do in the morning, brush your teeth.
	mgr.EndGPUReset();   // Toilet, then breakfast.
	//usleep(10000);		// 10ms delay, just to be sure. Probably don't need this?

	/*
	writeRaw(0xA0000000); // Copy rect from CPU to VRAM
	writeRaw(0x00000000); // to 0,0
	writeRaw(0x00040004); // Size 4,4
	writeRaw(0xFC00801F); // 01
	writeRaw(0x8000FFFF); // 23
	writeRaw(0xFC1FFFFF); // 45
	writeRaw(0x8000FFFF); // 67
	writeRaw(0xFFFFFFFF); // 89
	writeRaw(0xFFF0FFF0); // AB
	writeRaw(0xFFFFFFFF); // CD
	writeRaw(0xFFF1FFF1); // EF
		
	writeRaw(0x25FFFFFF);
	writeRaw(0x00100010);
	writeRaw(0xFFF30000);
	writeRaw(0x00100110);
	writeRaw((( 0 | (0<<4) | (0<<5) | (2<<7) | (0<<9) | (0<<11) )<<16 ) | 0x0004);		// [15:8,7:0] Texture [4,0]
	writeRaw(0x01100110);
	writeRaw(0x00000404);		// Texture [4,4]
	*/
	
	/*
	writeRaw(0xE6000001);
		
	writeRaw(0x30FF0000);
	writeRaw(0x00000000);
	writeRaw(0x0000FF00);
	writeRaw(0x00000008);
	writeRaw(0x000000FF);
	writeRaw(0x00080000);

	//---------------
	//   Tri, textured
	//---------------
	writeRaw(0x25AABBCC);			// Polygon, 3 pts, opaque, raw texture
	// Vertex 1
	writeRaw(0x00100010);			// [15:0] XCoordinate, [31:16] Y Coordinate (VERTEX 0)
	writeRaw(0xFFF30000);			// [31:16]Color LUT : NONE, value ignored
															// [15:8,7:0] Texture [0,0]
	// Vertex 2
	writeRaw(0x00100110);			// [15:0] XCoordinate, [31:16] Y Coordinate (VERTEX 1)
	writeRaw((( 0 | (0<<4) | (0<<5) | (2<<7) | (0<<9) | (0<<11) )<<16 ) | 0x0004);	// [15:8,7:0] Texture [4,0]
	// Vertex 3
	writeRaw(0x01100110);			// [15:0] XCoordinate, [31:16] Y Coordinate
	writeRaw(0x00000404);			// Texture [4,4]
	*/
	
	/*
	writeRaw(0xA0000000); // Copy rect from CPU to VRAM
	writeRaw(0x00000000); // to 0,0
	writeRaw(0x00040001); // Size 4,1

	writeRaw(0x83E083E0); // 01
	writeRaw(0x83E083E0); // 23

	writeRaw(0x30FF0000); // Triangle
	writeRaw(0x00100010);
	writeRaw(0x0000FF00);
	writeRaw(0x00100110);
	writeRaw(0x000000FF);
	writeRaw(0x01100110);
	*/
	
	/*
	FILE *fd = fopen("/media/fat/FF7Fight", "rb");
	fread(fb_addr, 1024*512*2, 1, fd);
	
	//uint8_t my_buf [1101824];
	//fread(my_buf,1024*512,2,fd);
	//memcpy(fb_addr, my_buf, 1101824);
	
	//for (int y=0; y < 512; y++) {
	   //fread(&((unsigned int * )fb_addr)[y*512 + 128], 512 * 2, 1, fd); // y*512 because fb_addr is a u32 type, isnt it ?
	//}
	fclose(fd);
	*/
	
	/*
	writeRaw(0x02FFFFFF);	// GP0(02h) FillVram / Colour.
	writeRaw(0x00000000);
	writeRaw(0x00040010);	// xpos.bit0-3=0Fh=bugged  xpos.bit0-3=ignored.
	*/

	// Write to GP0...
	/*
	writeRaw(0xE100020A);	// Texpage.
	writeRaw(0xE2000000);	// Texwindow.
	writeRaw(0xE3000000);	// DrawAreaX1Y1.
	writeRaw(0xE4077E7F);	// DrawAreaX2Y2.
	writeRaw(0xE5000000);	// DrawAreaOffset.
	writeRaw(0xE6000000);	// MaskBits.
	*/
	
	//parser( "/media/fat/DumpSet/RRFlag.gpudrawlist", (u16*)fb_addr, mgr, 0);
	//parser( "/media/fat/DumpSet/RROpening.gpudrawlist", (u16*)fb_addr, mgr, 0);
	//parser( "/media/fat/DumpSet/RRChase.gpudrawlist", (u16*)fb_addr, mgr, 0);
	//parser( "/media/fat/DumpSet/RRChase2.gpudrawlist", (u16*)fb_addr, mgr, 0);
	//parser( "/media/fat/DumpSet/RRChase3.gpudrawlist", (u16*)fb_addr, mgr, 0);
	
	parser( "/media/fat/DumpSet/FF7_2.gpudrawlist", (u16*)fb_addr, mgr, 0);
	parser( "/media/fat/DumpSet/FF7_3.gpudrawlist", (u16*)fb_addr, mgr, 0);
	
	
	/*
	parser( "/media/fat/DumpSet/BootMenu", (u16*)fb_addr, mgr, 0);
	sleep(3); mgr.StartGPUReset(); mgr.EndGPUReset();
	parser( "/media/fat/DumpSet/FF7Fight", (u16*)fb_addr, mgr, 0);
	sleep(3); mgr.StartGPUReset(); mgr.EndGPUReset();
	parser( "/media/fat/DumpSet/FF7Station", (u16*)fb_addr, mgr, 0);
	sleep(3); mgr.StartGPUReset(); mgr.EndGPUReset();
	parser( "/media/fat/DumpSet/FF7Station2", (u16*)fb_addr, mgr, 0);
	sleep(3); mgr.StartGPUReset(); mgr.EndGPUReset();
	//parser( "/media/fat/DumpSet/FF7Station2_export", (u16*)fb_addr, mgr, 0);
	//sleep(3); mgr.StartGPUReset(); mgr.EndGPUReset();
	parser( "/media/fat/DumpSet/JumpingFlashMenu", (u16*)fb_addr, mgr, 0);
	sleep(3); mgr.StartGPUReset(); mgr.EndGPUReset();
	//parser( "/media/fat/DumpSet/Lines", (u16*)fb_addr, mgr, 0);
	//sleep(3); mgr.StartGPUReset(); mgr.EndGPUReset();
	parser( "/media/fat/DumpSet/LoaderRidge", (u16*)fb_addr, mgr, 0);
	sleep(3); mgr.StartGPUReset(); mgr.EndGPUReset();
	parser( "/media/fat/DumpSet/Megaman Scr3", (u16*)fb_addr, mgr, 0);
	sleep(3); mgr.StartGPUReset(); mgr.EndGPUReset();
	parser( "/media/fat/DumpSet/Megaman1", (u16*)fb_addr, mgr, 0);
	sleep(3); mgr.StartGPUReset(); mgr.EndGPUReset();
	parser( "/media/fat/DumpSet/Megaman_Menu", (u16*)fb_addr, mgr, 0);
	sleep(3); mgr.StartGPUReset(); mgr.EndGPUReset();
	parser( "/media/fat/DumpSet/MegamanInGame", (u16*)fb_addr, mgr, 0);
	sleep(3); mgr.StartGPUReset(); mgr.EndGPUReset();
	//parser( "/media/fat/DumpSet/MenuFF7", (u16*)fb_addr, mgr, 0);
	//sleep(3); mgr.StartGPUReset(); mgr.EndGPUReset();
	//parser( "/media/fat/DumpSet/MenuPolygon", (u16*)fb_addr, mgr, 0);
	//sleep(3); mgr.StartGPUReset(); mgr.EndGPUReset();
	parser( "/media/fat/DumpSet/RidgeRacerGame", (u16*)fb_addr, mgr, 0);
	sleep(3); mgr.StartGPUReset(); mgr.EndGPUReset();
	parser( "/media/fat/DumpSet/RidgeRacerMenu", (u16*)fb_addr, mgr, 0);
	sleep(3); mgr.StartGPUReset(); mgr.EndGPUReset();
	parser( "/media/fat/DumpSet/RidgeScore", (u16*)fb_addr, mgr, 0);
	sleep(3); mgr.StartGPUReset(); mgr.EndGPUReset();
	parser( "/media/fat/DumpSet/StarOceanMenu", (u16*)fb_addr, mgr, 0);
	sleep(3); mgr.StartGPUReset(); mgr.EndGPUReset();
	parser( "/media/fat/DumpSet/TestLines", (u16*)fb_addr, mgr, 0);
	sleep(3); mgr.StartGPUReset(); mgr.EndGPUReset();
	parser( "/media/fat/DumpSet/TexOverflow", (u16*)fb_addr, mgr, 0);
	sleep(3); mgr.StartGPUReset(); mgr.EndGPUReset();
	parser( "/media/fat/DumpSet/TexTrueColorStarOcean", (u16*)fb_addr, mgr, 0);
	sleep(3); mgr.StartGPUReset(); mgr.EndGPUReset();
	parser( "/media/fat/DumpSet/Transparency", (u16*)fb_addr, mgr, 0);
	sleep(3);
	*/
	
	// Test poly RGB.
	/*
	writeRaw(0x30FF0000);	// (CcBbGgRrh)  Color1+Command.  (blue) Shaded three-point poly. 
	writeRaw(0x00000000);	// (YyyyXxxxh)  Vertex 1.  Y=0. X=0
	writeRaw(0x0000FF00);	// (00BbGgRrh)  Color2. (green)
	writeRaw(0x0000000F);	// (YyyyXxxxh)  Vertex 2.  Y=0. X=15
	writeRaw(0x000000FF);	// (00BbGgRrh)  Color3. (red)
	writeRaw(0x000F000F);	// (YyyyXxxxh)  Vertex 3.  Y=15. X=15
	*/
	
	// Test poly white.
	/*
	writeRaw(0x30FFFFFF);	// (CcBbGgRrh)  Color1+Command.  (blue) Shaded three-point poly. 
	writeRaw(0x00000000);	// (YyyyXxxxh)  Vertex 1.  Y=0. X=0
	writeRaw(0x00FFFFFF);	// (00BbGgRrh)  Color2. (green)
	writeRaw(0x0000000F);	// (YyyyXxxxh)  Vertex 2.  Y=0. X=15
	writeRaw(0x00FFFFFF);	// (00BbGgRrh)  Color3. (red)
	writeRaw(0x000F000F);	// (YyyyXxxxh)  Vertex 3.  Y=15. X=15
	*/
	
	// Test poly red.
	/*
    writeRaw(0x300000FF);    // (CcBbGgRrh)  Color1+Command. Shaded three-point poly. 
    writeRaw(0x00000000);    // (YyyyXxxxh)  Vertex 1.  Y=0. X=0
    writeRaw(0x000000FF);    // (00BbGgRrh)  Color2.
    writeRaw(0x0000000F);    // (YyyyXxxxh)  Vertex 2.  Y=0. X=15
    writeRaw(0x000000FF);    // (00BbGgRrh)  Color3.
    writeRaw(0x000F000F);    // (YyyyXxxxh)  Vertex 3.  Y=15. X=15
	*/
	
	// Test poly green.
	/*
	writeRaw(0x3000FF00);	// (CcBbGgRrh)  Color1+Command. Shaded three-point poly. 
	writeRaw(0x00000000);	// (YyyyXxxxh)  Vertex 1.  Y=0. X=0
	writeRaw(0x0000FF00);	// (00BbGgRrh)  Color2.
	writeRaw(0x0000000F);	// (YyyyXxxxh)  Vertex 2.  Y=0. X=15
	writeRaw(0x0000FF00);	// (00BbGgRrh)  Color3.
	writeRaw(0x000F000F);	// (YyyyXxxxh)  Vertex 3.  Y=15. X=15
	*/
	
	// Test poly blue.
	/*
	writeRaw(0x30FF0000);	// (CcBbGgRrh)  Color1+Command. Shaded three-point poly. 
	writeRaw(0x00000000);	// (YyyyXxxxh)  Vertex 1.  Y=0. X=0
	writeRaw(0x00FF0000);	// (00BbGgRrh)  Color2.
	writeRaw(0x0000000F);	// (YyyyXxxxh)  Vertex 2.  Y=0. X=15
	writeRaw(0x00FF0000);	// (00BbGgRrh)  Color3.
	writeRaw(0x000F000F);	// (YyyyXxxxh)  Vertex 3.  Y=15. X=15
	*/

	
	// Write to GP0...
	/*
	writeRaw(0x02FFFFFF); // Fill Rect WHITE
	writeRaw(0x00000000); // Start pos 0,0
	writeRaw(0x00000280); // Size 16x16 pixel.
	*/
	
	/*
	writeRaw(0x02FFFFFF); // Fill Rect WHITE
	writeRaw(0x00400040); // Start pos 64,64
	writeRaw(0x00400040); // Size 64x64 pixel.
	*/
	
	/*
	writeRaw(0x02FFFFFF); // Fill Rect WHITE
	writeRaw(0x00000000); // Start pos 0,0
	writeRaw(0x01ff03ff); // Size 511x1023 pixel.
	*/


	/*
	writeRaw(0x020000FF); // Fill Rect RED.
	writeRaw(0x00000000); // Start pos 0,0
	writeRaw(0x00400040); // Size 64x64 pixel.
	
	writeRaw(0x0200FF00); // Fill Rect GREEN
	writeRaw(0x00400040); // Start pos 64,64
	writeRaw(0x00400040); // Size 64x64 pixel.
	
	writeRaw(0x02FF0000); // Fill Rect BLUE.
	writeRaw(0x00800080); // Start pos 128,128
	writeRaw(0x00400040); // Size 64x64 pixel.
	*/
	
	//usleep(500000);	// 500ms Wait for the rendering to finish! TODO - Do a proper polling check for this.
	
	// Write to GP0...
	// (orange diamond, from the BIOS Logo.)
	/*
	writeRaw(0x380000B2);	// Color1+Command.  Shaded four-point polygon, opaque.
	writeRaw(0x00F000C0);	// Vertex 1. y=240. x=192.
	writeRaw(0x00008CB2);	// Color2.
	writeRaw(0x00700140);	// Vertex 2. y=112. x=320.
	writeRaw(0x00008CB2);	// Color3.
	writeRaw(0x01700140);	// Vertex 3. y=368. x=320.
	writeRaw(0x000000B2);	// Color4.
	writeRaw(0x00F001C0);	// Vertex 4. y=240. x=448.
	*/
	
	//
	// Crash Bandicoot's right ear.
	/*
	writeRaw(0x30000828);	// Color1+Command.  Shaded three-point polygon, opaque.
	writeRaw(0x001E0019);	// Vertex 1. (YyyyXxxxh)  Y=30. X=25
	writeRaw(0x60000B3A);	// Color2.   (00BbGgRrh)  
	writeRaw(0x001F0028);	// Vertex 2. (YyyyXxxxh)  Y=31. X=40
	writeRaw(0x60000B39);	// Color3.   (00BbGgRrh)  
	writeRaw(0x00030001);	// Vertex 3. (YyyyXxxxh)  Y=3. X=1
	*/

	/*
	writeRaw(0x30800000);	// Color1+Command.  Shaded three-point polygon, opaque.
	writeRaw(0x00000000);	// Vertex 1. (YyyyXxxxh)  Y=30. X=25
	writeRaw(0x00008000);	// Color2.   (00BbGgRrh)  
	writeRaw(0x0000000F);	// Vertex 2. (YyyyXxxxh)  Y=31. X=40
	writeRaw(0x00000080);	// Color3.   (00BbGgRrh)  
	writeRaw(0x000F000F);	// Vertex 3. (YyyyXxxxh)  Y=3. X=1
	*/

	/*
	writeRaw(0x30800000);	// Color1+Command.  Shaded three-point polygon, opaque.
	writeRaw(0x00000000);	// Vertex 1. (YyyyXxxxh)  Y=0. X=0
	writeRaw(0x00008000);	// Color2.   (00BbGgRrh)  
	writeRaw(0x00000040);	// Vertex 2. (YyyyXxxxh)  Y=0. X=64
	writeRaw(0x00000080);	// Color3.   (00BbGgRrh)  
	writeRaw(0x00400000);	// Vertex 3. (YyyyXxxxh)  Y=64. X=0
	*/
	
	/*
    writeRaw(0x30FF0000);    // Color1+Command.  Shaded three-point polygon, opaque.
    writeRaw(0x00000000);    // Vertex 1. (YyyyXxxxh)  Y=0. X=0
    writeRaw(0x0000FF00);    // Color2.   (00BbGgRrh)  
    writeRaw(0x0000000F);    // Vertex 2. (YyyyXxxxh)  Y=0. X=64
    writeRaw(0x000000FF);    // Color3.   (00BbGgRrh)  
    writeRaw(0x000F0000);    // Vertex 3. (YyyyXxxxh)  Y=64. X=64
	*/
	
	/*
	writeRaw(0x3000FF00);	// (CcBbGgRrh)  Color1+Command. Shaded three-point poly. 
	writeRaw(0x00000000);	// (YyyyXxxxh)  Vertex 1.  Y=0. X=0
	writeRaw(0x0000FF00);	// (00BbGgRrh)  Color2.
	writeRaw(0x0000007F);	// (YyyyXxxxh)  Vertex 2.  Y=0. X=15
	writeRaw(0x0000FF00);	// (00BbGgRrh)  Color3.
	writeRaw(0x007F0000);	// (YyyyXxxxh)  Vertex 3.  Y=15. X=15
	*/
	
	// Test poly, for VCD compare...
	/*
    writeRaw(0x32FF0000);    // Color1+Command.  Shaded three-point polygon, opaque.
    writeRaw(0x00000020);    // Vertex 1. (YyyyXxxxh)  Y=0. X=0
    writeRaw(0x0000FF00);    // Color2.   (00BbGgRrh)  
    writeRaw(0x0000009F);    // Vertex 2. (YyyyXxxxh)  Y=0. X=64
    writeRaw(0x000000FF);    // Color3.   (00BbGgRrh)  
    writeRaw(0x007F0020);    // Vertex 3. (YyyyXxxxh)  Y=64. X=64
	*/

	/*
	writeRaw(0x30000828);	// Color1+Command.  Shaded three-point polygon, opaque.
	writeRaw(0x001E0019);	// Vertex 1. (YyyyXxxxh)  Y=30. X=25
	writeRaw(0x60000B3A);	// Color2.   (00BbGgRrh)  
	writeRaw(0x00280040);	// Vertex 2. (YyyyXxxxh)  Y=40. X=64
	writeRaw(0x60000B39);	// Color3.   (00BbGgRrh)  
	writeRaw(0x00400032);	// Vertex 3. (YyyyXxxxh)  Y=64. X=50
	*/

	/*
	writeRaw(0x300000FF);	// TriangleGouraud.
	writeRaw(0x00000000);	// Vertex 1. (YyyyXxxxh)  Y=0. X=0
	writeRaw(0x0000FF00);	// Color2.   (00BbGgRrh)  
	writeRaw(0x0000000F);	// Vertex 2. (YyyyXxxxh)  Y=0. X=15
	writeRaw(0x00FF0000);	// Color3.   (00BbGgRrh)  
	writeRaw(0x000F0000);	// Vertex 3. (YyyyXxxxh)  Y=15. X=0
	*/
	
	/*
	writeRaw(0x300000FF);	// TriangleGouraud.
	writeRaw(0x00000000);	// Vertex 1. (YyyyXxxxh)  Y=0. X=0
	writeRaw(0x0000FF00);	// Color2.   (00BbGgRrh)  
	writeRaw(0x00000040);	// Vertex 2. (YyyyXxxxh)  Y=0. X=64
	writeRaw(0x00FF0000);	// Color3.   (00BbGgRrh)  
	writeRaw(0x00400000);	// Vertex 3. (YyyyXxxxh)  Y=64. X=0
	*/
	
	/*
	writeRaw(0x300000B2);	// TriangleGouraud, from the BIOS logo.
	writeRaw(0x010301A0);
	writeRaw(0x00008CB2);
	writeRaw(0x00A0013D);
	writeRaw(0x00008CB2);
	writeRaw(0x0166013D);
	*/
	
	/*
	writeRaw(0xE100020A);	// Texpage.
	writeRaw(0xE2000000);	// Texwindow.
	writeRaw(0xE3000000);	// DrawAreaX1Y1.
	writeRaw(0xE4077E7F);	// DrawAreaX2Y2.
	writeRaw(0xE5000000);	// DrawAreaOffset.
	writeRaw(0xE6000000);	// MaskBits.
	*/
	
	/*
	writeRaw(0x28B4B4B4);	// Quad, for clearing background to grey.
	writeRaw(0x00000000);
	writeRaw(0x00000280);
	writeRaw(0x01E00000);
	writeRaw(0x01E00280);
	*/
	
	/*
	writeRaw(0x01000000);	// Flush texture cache.
	printf("sizeof sony logo: %d BYTES / %d WORDS\n", sizeof(sony_logo), sizeof(sony_logo)/4 );
	for (int i=0; i<sizeof(sony_logo)/4; i++) {		// sizeof gives bytes, but logo is in uint32_t.
		writeRaw(sony_logo[i]);
	}
	
	writeRaw(0x01000000);	// Flush texture cache.
	printf("sizeof tm: %d BYTES / %d WORDS\n", sizeof(tm), sizeof(tm)/4 );
	for (int i=0; i<sizeof(tm)/4; i++) {			// sizeof gives bytes, but logo is in uint32_t.
		writeRaw(tm[i]);
	}
	
	writeRaw(0x01000000);	// Flush texture cache.
	printf("sizeof comp ent: %d BYTES / %d WORDS\n\n", sizeof(comp_ent), sizeof(comp_ent)/4 );
	for (int i=0; i<sizeof(comp_ent)/4; i++) {		// sizeof gives bytes, but logo is in uint32_t.
		writeRaw(comp_ent[i]);
	}
	
	writeRaw(0x01000000);	// Flush texture cache.
	writeRaw(0xA0000000);	// 16x1 "texture", for 1st palette.
	writeRaw(0x01E00000);
	writeRaw(0x00010010);
	writeRaw(0x4E510000);
	writeRaw(0x396841AB);
	writeRaw(0x24803525);
	writeRaw(0x45CC2CE2);
	writeRaw(0x45EE5693);
	writeRaw(0x3D893104);
	writeRaw(0x28C14A2F);
	writeRaw(0x000056B5);
	
	writeRaw(0x01000000);	// Flush texture cache.
	writeRaw(0xA0000000);	// 16x1 "texture", for 2nd palette.
	writeRaw(0x01E00040);
	writeRaw(0x00010010);
	writeRaw(0x56B40000);
	writeRaw(0x52725293);
	writeRaw(0x4E524E72);
	writeRaw(0x56945293);
	writeRaw(0x4E7256B5);
	writeRaw(0x56B556B5);
	writeRaw(0x52725273);
	writeRaw(0x00005694);

	writeRaw(0x01000000);	// Flush texture cache.
	writeRaw(0xA0000000);	// 16x1 "texture", for 3rd palette.
	writeRaw(0x01E00080);
	writeRaw(0x00010010);
	writeRaw(0x5EF75EF7);
	writeRaw(0x52935EF6);
	writeRaw(0x52725AD5);
	writeRaw(0x56B45272);
	writeRaw(0x4E725272);
	writeRaw(0x5AB54E52);
	writeRaw(0x5AD656B5);
	writeRaw(0x00005694);
	
	writeRaw(0x01000000);	// Flush texture cache.
	writeRaw(0xA0000000);	// 16x1 "texture", for 4th palette.
	writeRaw(0x01E000C0);
	writeRaw(0x00010010);
	writeRaw(0x4E510000);
	writeRaw(0x396841AB);
	writeRaw(0x24803525);
	writeRaw(0x45CC2CE2);
	writeRaw(0x45EE5693);
	writeRaw(0x3D893104);
	writeRaw(0x28C14A2F);
	writeRaw(0x000056B5);

	writeRaw(0x01000000);	// Flush texture cache.
	writeRaw(0xA0000000);	// 16x1 "texture", for 5th palette.
	writeRaw(0x01E00100);
	writeRaw(0x00010010);
	writeRaw(0x4A2F0000);
	writeRaw(0x35253D89);
	writeRaw(0x248028C1);
	writeRaw(0x45EE41AB);
	writeRaw(0x2CE24E51);
	writeRaw(0x56B55693);
	writeRaw(0x31043968);
	writeRaw(0x000045CC);

	writeRaw(0x01000000);	// Flush texture cache.
	writeRaw(0xA0000000);	// 16x1 "texture", for 6th palette.
	writeRaw(0x01E00140);
	writeRaw(0x00010010);
	writeRaw(0x7FFE7FFF);
	writeRaw(0x41CA77BC);
	writeRaw(0x354562F5);
	writeRaw(0x524F3124);
	writeRaw(0x28C13103);
	writeRaw(0x5EB324A0);
	writeRaw(0x6B385A92);
	writeRaw(0x000049EC);
	*/
	
	/*
	writeRaw(0x32FFFFFF);    // Color1+Command.  Shaded three-point polygon, semi-transparent.
	//writeRaw(0x30FF0000);    // Color1+Command.  Shaded three-point polygon, opaque.
	writeRaw(0x00000000);    // Vertex 1. (YyyyXxxxh)
	writeRaw(0x00FFFFFF);    // Color2.   (00BbGgRrh)  
	writeRaw(0x00000015);    // Vertex 2. (YyyyXxxxh)
	writeRaw(0x00FFFFFF);    // Color3.   (00BbGgRrh)  
	writeRaw(0x00090010);    // Vertex 3. (YyyyXxxxh)
	*/
	
	/*
	writeRaw(0x32FFFFFF);    // Color1+Command.  Shaded three-point polygon, semi-transparent.
	//writeRaw(0x30FF0000);    // Color1+Command.  Shaded three-point polygon, opaque.
	writeRaw(0x00000000);    // Vertex 1. (YyyyXxxxh)
	writeRaw(0x00FFFFFF);    // Color2.   (00BbGgRrh)  
	writeRaw(0x0000002f);    // Vertex 2. (YyyyXxxxh)
	writeRaw(0x00FFFFFF);    // Color3.   (00BbGgRrh)  
	writeRaw(0x002f0008);    // Vertex 3. (YyyyXxxxh)
	*/
	
	/*
	usleep(5000);
	
	writeRaw(0x65000000); // Color+Command. GP0(65h) - Textured Rectangle, variable size, opaque, raw-texture
	writeRaw(0x003800C8); // Vertex. Upper-left coord of the rectangle.
	writeRaw(0x780C0000); // Texcoord+Palette
	writeRaw(0x00400040); // (variable size only) (max 1023x511)
	*/
		
		
	/*
	writeRaw(0x2C808080);	// Quad textured, for Sony logo.  Color+Command
	writeRaw(0x003800C8);	// Vertex1           (YyyyXxxxh)
	writeRaw(0x780C0000);	// Texcoord1+Palette (ClutYyXxh)
	writeRaw(0x003801B8);	// Vertex2           (YyyyXxxxh)
	writeRaw(0x000D00EF);	// Texcoord2+Texpage (PageYyXxh)
	writeRaw(0x006800C8);	// Vertex3           (YyyyXxxxh)
	writeRaw(0x00002F00);	// Texcoord3         (0000YyXxh)
	writeRaw(0x006801B8);	// Vertex4           (YyyyXxxxh) (if any)
	writeRaw(0x00002FEF);	// Texcoord4         (0000YyXxh) (if any)

	writeRaw(0x2C808080);	// Quad textured, for "tm".
	writeRaw(0x01670154);
	writeRaw(0x78140000);
	writeRaw(0x0167016C);
	writeRaw(0x000F0017);
	writeRaw(0x01730154);
	writeRaw(0x00000B00);
	writeRaw(0x0173016C);
	writeRaw(0x00000B17);

	writeRaw(0x2C808080);	// Quad textured, for "Computer Entertainment" logo.
	writeRaw(0x017E00C8);
	writeRaw(0x78100000);
	writeRaw(0x017E01B8);
	writeRaw(0x000E00EF);
	writeRaw(0x01BA00C8);
	writeRaw(0x00003B00);
	writeRaw(0x01BA01B8);
	writeRaw(0x00003BEF);

	writeRaw(0x380000B2);	// Color1+Command.  Quad Gouraud for logo diamond.
	writeRaw(0x00F000C0);	// Vertex 1. y=240. x=192.
	writeRaw(0x00008CB2);	// Color2.
	writeRaw(0x00700140);	// Vertex 2. y=112. x=320.
	writeRaw(0x00008CB2);	// Color3.
	writeRaw(0x01700140);	// Vertex 3. y=368. x=320.
	writeRaw(0x000000B2);	// Color4.
	writeRaw(0x00F001C0);	// Vertex 4. y=240. x=448.

	writeRaw(0x300000B2);	// Lower orange triangle (Gouraud) on logo.
	writeRaw(0x01200170);	// 
	writeRaw(0x00008CB2);	// 
	writeRaw(0x00E60136);	// 
	writeRaw(0x00008CB2);	// 
	writeRaw(0x015A0136);	// 
	
	writeRaw(0x300000B2);	// Upper orange triangle (Gouraud) on logo.
	writeRaw(0x00C00110);	// 
	writeRaw(0x00008CB2);	// 
	writeRaw(0x0086014A);	// 
	writeRaw(0x00008CB2);	// 
	writeRaw(0x00FA014A);	// 
	*/
	
	
	usleep(50000);	// 500ms Wait for the rendering to finish! TODO - Do a proper polling check for this.
	

	uint32_t performance = mgr.getGPUCycle();
	printf("\nmydebugCnt: %d\n\n", performance);

	/*
	uint32_t offset = 0x0;
	printf("Displaying FB offset: 0x%08X...\n", offset);
	for (int i=0; i<4096; i++) {
		if ( (i&7)==0 ) printf("\n");
		reg0 = *((uint32_t *)fb_addr+offset+i); printf("%08X ", reg0);
	}
	printf("\n");
	*/
	
	printf("printing lines 0-31...\n");
	for (int line=0; line<32; line++) {
		for (int word=0; word<16; word++) { reg0 = *((uint32_t *)fb_addr+word+(line*512) ); printf("%08X ", reg0); }	// 32-bit word address for fb_addr, 2 pixels per word, so 1024/2.
		printf("\n");
	}
	
	
	// Read flags / myDebugCnt.
	/*
	reg0 = *((uint32_t *)axi_addr+0x000000002);
	printf("flag bits:  0x%01X\n", (reg0&0xF0000000)>>28 );
	printf("myDebugCnt: 0x%08X\n", reg0&0x0FFFFFFF);
	*/
	
	//*((uint32_t *)axi_addr+0x1) = 0xDEADBEEF; // Write to GP1.
	//*((uint32_t *)axi_addr+0x2) = 0x40000000; // Write to DMA_ACK / gpu_nrst. (keep gpu_nrst HIGH!)
	
	//reg0 = *((uint32_t *)axi_addr+0x3);		 // Read DMA (no gpuSel nor read/write pulse).
	//*((uint32_t *)axi_addr+0x3) = 0xCAFEBABE; // Write DMA (no gpuSel nor read/write pulse).
	
	return 0;
}


