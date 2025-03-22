/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2010  PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "IPU_Fifo.h"
#include "IPUdma.h"

#include "../Common.h"

#define ipumsk( src ) ( (src) & 0xff )
#define ipucase( src ) case ipumsk(src)

#define IPU_INT_TO( cycles )  if(!(cpuRegs.interrupt & (1<<4))) CPU_INT( DMAC_TO_IPU, cycles )
#define IPU_INT_FROM( cycles )  CPU_INT( DMAC_FROM_IPU, cycles )

#define IPU_INT_PROCESS( cycles ) if(!(cpuRegs.interrupt & (1 << IPU_PROCESS))) CPU_INT( IPU_PROCESS, cycles )

//
// Bitfield Structures
//

union tIPU_CMD
{
	struct
	{
		u32 DATA;
		u32 BUSY;
	};
	u64 _u64;
};

union tIPU_CTRL {
	struct {
		u32 IFC : 4;	// Input FIFO counter
		u32 OFC : 4;	// Output FIFO counter
		u32 CBP : 6;	// Coded block pattern
		u32 ECD : 1;	// Error code pattern
		u32 SCD : 1;	// Start code detected
		u32 IDP : 2;	// Intra DC precision
		u32 resv0 : 2;
		u32 AS : 1;		// Alternate scan
		u32 IVF : 1;	// Intra VLC format
		u32 QST : 1;	// Q scale step
		u32 MP1 : 1;	// MPEG1 bit stream
		u32 PCT : 3;	// Picture Type
		u32 resv1 : 3;
		u32 RST : 1;	// Reset
		u32 BUSY : 1;	// Busy
	};
	u32 _u32;
};

struct alignas(16) tIPU_BP {
	alignas(16) u128 internal_qwc[2];

	u32 BP;		// Bit stream point (0 to 128*2)
	u32 IFC;	// Input FIFO counter (8QWC) (0 to 8)
	u32 FP;		// internal FIFO (2QWC) fill status (0 to 2)

	__fi void Align()
	{
		BP = (BP + 7) & ~7;
		Advance(0);
	}

	__fi void Advance(uint bits)
	{
		FillBuffer(bits);

		BP += bits;

		if (BP >= 128)
		{
			BP -= 128;

			if (FP == 2)
			{
				// when BP is over 128 it means we're reading data from the second quadword.  Shift that one
				// to the front and load the new quadword into the second QWC (its a manualized ringbuffer!)
				void      *dest = &internal_qwc[0];
				const void *src = &internal_qwc[1];
				CopyQWC(dest, src);
				FP = 1;
			}
			else
			{
				// if FP == 1 then the buffer has been completely drained.
				// if FP == 0 then an already-drained buffer is being advanced, and we need to drop a
				// quadword from the IPU FIFO.

				if (ipu_fifo.in.read(&internal_qwc[0]))
					FP = 1;
				else
					FP = 0;
			}
		}
	}

	__fi bool FillBuffer(u32 bits)
	{
		while ((FP * 128) < (BP + bits))
		{
			if (ipu_fifo.in.read(&internal_qwc[FP]) == 0)
			{
				// Here we *try* to fill the entire internal QWC buffer; however that may not necessarily
				// be possible -- so if the fill fails we'll only return 0 if we don't have enough
				// remaining bits in the FIFO to fill the request.
				// Used to do ((FP!=0) && (BP + bits) <= 128) if we get here there's defo not enough data now though

				IPUCoreStatus.WaitingOnIPUTo = true;
				return false;
			}

			++FP;
		}

		return true;
	}
};

union tIPU_CMD_IDEC
{
	struct
	{
		u32 FB  : 6;
		u32 UN2 :10;
		u32 QSC : 5;
		u32 UN1 : 3;
		u32 DTD : 1;
		u32 SGN : 1;
		u32 DTE : 1;
		u32 OFM : 1;
		u32 cmd : 4;
	};
	u32 _u32;
};

union tIPU_CMD_BDEC
{
	struct
	{
		u32 FB  : 6;
		u32 UN2 :10;
		u32 QSC : 5;
		u32 UN1 : 4;
		u32 DT  : 1;
		u32 DCR : 1;
		u32 MBI : 1;
		u32 cmd : 4;
	};
	u32 _u32;
};

union tIPU_CMD_CSC
{
	struct
	{
		u32 MBC :11;
		u32 UN2 :15;
		u32 DTE : 1;
		u32 OFM : 1;
		u32 cmd : 4;
	};
	u32 _u32;
};

enum SCE_IPU
{
	SCE_IPU_BCLR = 0x0
,	SCE_IPU_IDEC
,	SCE_IPU_BDEC
,	SCE_IPU_VDEC
,	SCE_IPU_FDEC
,	SCE_IPU_SETIQ
,	SCE_IPU_SETVQ
,	SCE_IPU_CSC
,	SCE_IPU_PACK
,	SCE_IPU_SETTH
};

struct IPUregisters {
	tIPU_CMD	cmd;
	u32		dummy0[2];

	tIPU_CTRL	ctrl;
	u32		dummy1[3];

	u32		ipubp;
	u32		dummy2[3];

	u32		top;
	u32		topbusy;
	u32		dummy3[2];
};

union tIPU_cmd
{
	struct
	{
		int index;
		int pos[6];
		union {
			struct {
				u32 OPTION : 28;
				u32 CMD : 4;
			};
			u32 current;
		};
	};

	u128 _u128[2];

	void clear();
};

static IPUregisters& ipuRegs = (IPUregisters&)eeHw[0x2000];

extern bool FMVstarted;
extern bool EnableFMV;

alignas(16) extern tIPU_cmd ipu_cmd;
extern uint eecount_on_last_vdec;

extern void ipuReset();

extern u32 ipuRead32(u32 mem);
extern u64 ipuRead64(u32 mem);
extern bool ipuWrite32(u32 mem,u32 value);
extern bool ipuWrite64(u32 mem,u64 value);

extern void IPUCMD_WRITE(u32 val);
extern void ipuSoftReset();
extern void IPUProcessInterrupt();

