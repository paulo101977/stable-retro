/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2022  PCSX2 Dev Team
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

#include "Common.h"

#include <cstring> /* memset/memcpy */

#include "GS.h"
#include "Gif_Unit.h"
#include "Vif_Dma.h"

// A three-way toggle used to determine if the GIF is stalling (transferring) or done (finished).
// Should be a gifstate_t rather then int, but I don't feel like possibly interfering with savestates right now.


alignas(16) GIF_Fifo gif_fifo;
alignas(16) gifStruct gif;

static __fi void GifDMAInt(int cycles)
{
	if (dmacRegs.ctrl.MFD == MFD_GIF)
	{
		if (!(cpuRegs.interrupt & (1 << DMAC_MFIFO_GIF)) || cpuRegs.eCycle[DMAC_MFIFO_GIF] < (u32)cycles)
		{
			CPU_INT(DMAC_MFIFO_GIF, cycles);
		}
	}
	else if (!(cpuRegs.interrupt & (1 << DMAC_GIF)) || cpuRegs.eCycle[DMAC_GIF] < (u32)cycles)
	{
		CPU_INT(DMAC_GIF, cycles);
	}
}

//I suspect this is GS side which should really be handled by GS which also doesn't current have a fifo, but we can guess from our FIFO
static unsigned CalculateFIFOCSR(void)
{
	if (gifRegs.stat.FQC >= 15)
		return CSR_FIFO_FULL;
	else if (gifRegs.stat.FQC == 0)
		return CSR_FIFO_EMPTY;
	return CSR_FIFO_NORMAL;
}

static bool CheckPaths(void)
{
	// Can't do Path 3, so try dma again later...
	if (!gifUnit.CanDoPath3())
	{
		if (!gifUnit.Path3Masked())
			GifDMAInt(128);
		return false;
	}
	return true;
}

void GIF_Fifo::init()
{
	memset(data, 0, sizeof(data));
	fifoSize = 0;
	gifRegs.stat.FQC = 0;

	gif.gifstate = GIF_STATE_READY;
	gif.gspath3done = true;

	gif.gscycles = 0;
	gif.prevcycles = 0;
	gif.mfifocycles = 0;
}


int GIF_Fifo::write_fifo(u32* pMem, int size)
{
	if (fifoSize == 16)
		return 0;

	int transferSize = std::min(size, 16 - (int)fifoSize);

	int writePos = fifoSize * 4;

	memcpy(&data[writePos], pMem, transferSize * 16);

	fifoSize += transferSize;

	gifRegs.stat.FQC = fifoSize;
	CSRreg.FIFO      = CalculateFIFOCSR();

	return transferSize;
}

int GIF_Fifo::read_fifo()
{
	if (!fifoSize || !gifUnit.CanDoPath3())
	{
		gifRegs.stat.FQC = fifoSize;
		CSRreg.FIFO      = CalculateFIFOCSR();
		if (fifoSize)
			GifDMAInt(128);
		return 0;
	}

	int readpos  = 0;
	int sizeRead = gifUnit.TransferGSPacketData(GIF_TRANS_DMA, (u8*)&data, fifoSize * 16) / 16; //returns the size actually read

	if (sizeRead < (int)fifoSize)
	{
		if (sizeRead > 0)
		{
			int copyAmount = fifoSize - sizeRead;
			readpos = sizeRead * 4;

			for (int i = 0; i < copyAmount; i++)
			{
				void *dest      = &data[i * 4];
				const void *src = &data[readpos + (i * 4)];
				CopyQWC(dest, src);
			}

			fifoSize = copyAmount;
		}
	}
	else
		fifoSize = 0;

	gifRegs.stat.FQC = fifoSize;
	CSRreg.FIFO      = CalculateFIFOCSR();

	return sizeRead;
}

static void incGifChAddr(u32 qwc)
{
	if (gifch.chcr.STR)
	{
		gifch.madr += qwc * 16;
		gifch.qwc -= qwc;
		hwDmacSrcTadrInc(gifch);
	}
}

__fi void gifCheckPathStatus(bool calledFromGIF)
{
	// If GIF is running on it's own, let it handle its own timing.
	if (calledFromGIF && gifch.chcr.STR)
	{
		if (gif_fifo.fifoSize == 16)
			GifDMAInt(16);
		return;
	}

	// Required for Path3 Masking timing!
	if (gifUnit.gifPath[GIF_PATH_3].state == GIF_PATH_WAIT)
		gifUnit.gifPath[GIF_PATH_3].state = GIF_PATH_IDLE;

	if (gifRegs.stat.APATH == 3)
	{
		gifRegs.stat.APATH = 0;
		gifRegs.stat.OPH = 0;

		if (!calledFromGIF && (gifUnit.gifPath[GIF_PATH_3].state == GIF_PATH_IDLE || gifUnit.gifPath[GIF_PATH_3].state == GIF_PATH_WAIT))
		{
			if (gifUnit.checkPaths(1, 1, 0))
				gifUnit.Execute(false, true);
		}
	}

	// GIF DMA isn't running but VIF might be waiting on PATH3 so resume it here
	if (calledFromGIF && gifUnit.gifPath[GIF_PATH_3].state == GIF_PATH_IDLE)
	{
		if (vif1Regs.stat.VGW)
		{
			// Check if VIF is in a cycle or is currently "idle" waiting for GIF to come back.
			if (!(cpuRegs.interrupt & (1 << DMAC_VIF1)))
				CPU_INT(DMAC_VIF1, 1);

			// Make sure it loops if the GIF packet is empty to prepare for the next packet
			// or end if it was the end of a packet.
			// This must trigger after VIF retriggers as VIf might instantly mask Path3
			if ((!gifUnit.Path3Masked() || gifch.qwc == 0) && (gifch.chcr.STR || gif_fifo.fifoSize))
			{
				GifDMAInt(16);
			}
		}
	}
}

__fi void gifInterrupt(void)
{
	gifCheckPathStatus(false);

	if (gifUnit.gifPath[GIF_PATH_3].state == GIF_PATH_IDLE)
	{
		if (vif1Regs.stat.VGW)
		{
			// Check if VIF is in a cycle or is currently "idle" waiting for GIF to come back.
			if (!(cpuRegs.interrupt & (1 << DMAC_VIF1)))
				CPU_INT(DMAC_VIF1, 1);

			// Make sure it loops if the GIF packet is empty to prepare for the next packet
			// or end if it was the end of a packet.
			// This must trigger after VIF retriggers as VIf might instantly mask Path3
			if (!gifUnit.Path3Masked() || gifch.qwc == 0)
			{
				GifDMAInt(16);
			}
			if (gifUnit.Path3Masked() || !gifUnit.CanDoPath3())
				cpuRegs.dmastall |= 1 << DMAC_GIF;
			else
				cpuRegs.dmastall &= ~(1 << DMAC_GIF);
			return;
		}
	}

	if (dmacRegs.ctrl.MFD == MFD_GIF)
	{ // GIF MFIFO
		gifMFIFOInterrupt();
		return;
	}

	if (gifUnit.gsSIGNAL.queued)
	{
		GifDMAInt(128);
		cpuRegs.dmastall |= 1 << DMAC_GIF;
		if (gif_fifo.fifoSize == 16)
			return;
	}

	// If there's something in the FIFO and we can do PATH3, empty the FIFO.
	if (gif_fifo.fifoSize > 0)
	{
		const int readSize = gif_fifo.read_fifo();

		if (readSize)
			GifDMAInt(readSize * BIAS);

		// The following is quite timing sensitive so we need to pause/resume the DMA in these certain scenarios
		// If the DMA is masked/blocked and the fifo is full, no need to run the DMA
		// If we just read from the fifo, we want to loop and not read more DMA
		// If there is no DMA data waiting and the DMA is active, let the DMA progress until there is
		if ((!CheckPaths() && gif_fifo.fifoSize == 16) || readSize)
		{
			if (gifUnit.Path3Masked() || !gifUnit.CanDoPath3())
				cpuRegs.dmastall |= 1 << DMAC_GIF;
			else
				cpuRegs.dmastall &= ~(1 << DMAC_GIF);
			return;
		}
	}

	if (!(gifch.chcr.STR))
		return;

	if ((gifch.qwc > 0) || (!gif.gspath3done))
	{
		if (!dmacRegs.ctrl.DMAE)
		{
			// Re-raise the int shortly in the future
			GifDMAInt(64);
			cpuRegs.dmastall |= 1 << DMAC_GIF;
			return;
		}
		GIFdma();

		return;
	}

	gif.gscycles = 0;
	gifch.chcr.STR = false;
	gifRegs.stat.FQC = gif_fifo.fifoSize;
	CSRreg.FIFO      = CalculateFIFOCSR();
	hwDmacIrq(DMAC_GIF);

	if (gif_fifo.fifoSize)
		GifDMAInt(8 * BIAS);
}

static u32 WRITERING_DMA(u32* pMem, u32 qwc)
{
	u32 originalQwc = qwc;

	if (gifRegs.stat.IMT)
	{
		// Splitting by 8qw can be really slow, so on bigger packets be less picky.
		// Games seem to be more concerned with other channels finishing before PATH 3 finishes
		// so we can get away with transferring "most" of it when it's a big packet.
		// Use Wallace and Gromit Project Zoo or The Suffering for testing
		if (qwc > 64)
			qwc = qwc * 0.5f;
		else
			qwc = std::min(qwc, 8u);
	}
	// If the packet is larger than 8qw, try to time the packet somewhat so any "finish" signals don't fire way too early and GIF syncs with other units.
	// (Mana Khemia exhibits flickering characters without).
	else if (qwc > 8)
		qwc -= 8;

	uint size;

	if (CheckPaths() == false || ((qwc < 8 || gif_fifo.fifoSize > 0) && CHECK_GIFFIFOHACK))
	{
		if (gif_fifo.fifoSize < 16)
		{
			size = gif_fifo.write_fifo((u32*)pMem, originalQwc); // Use original QWC here, the intermediate mode is for the GIF unit, not DMA
			incGifChAddr(size);
			return size;
		}
		return 4; // Arbitrary value, probably won't schedule a DMA anwyay since the FIFO is full and GIF is paused
	}

	size = gifUnit.TransferGSPacketData(GIF_TRANS_DMA, (u8*)pMem, qwc * 16) / 16;
	incGifChAddr(size);
	return size;
}

static __fi void GIFchain(void)
{
	tDMA_TAG* pMem;

	pMem = dmaGetAddr(gifch.madr, false);
	if (pMem == NULL)
	{
		// Must increment madr and clear qwc, else it loops
		gifch.madr += gifch.qwc * 16;
		gifch.qwc = 0;
		return;
	}

	int transferred = WRITERING_DMA((u32*)pMem, gifch.qwc);
	gif.gscycles += transferred * BIAS;

	if (!gifUnit.Path3Masked() || (gif_fifo.fifoSize < 16))
		GifDMAInt(gif.gscycles);
}

#define checkTieBit(ptag) if (gifch.chcr.TIE && ptag->IRQ) gif.gspath3done = true

static __fi tDMA_TAG* ReadTag(void)
{
	tDMA_TAG* ptag = dmaGetAddr(gifch.tadr, false); // Set memory pointer to TADR

	if (!(gifch.transfer(ptag)))
		return NULL;

	gifch.madr      = ptag[1]._u32; // MADR = ADDR field + SPR
	gif.gscycles   += 2; // Add 1 cycles from the QW read for the tag

	gif.gspath3done = hwDmacSrcChainWithStack(gifch, ptag->ID);
	return ptag;
}

void GIFdma(void)
{
	while (gifch.qwc > 0 || !gif.gspath3done)
	{
		tDMA_TAG* ptag;
		gif.gscycles = gif.prevcycles;

		if (gifRegs.ctrl.PSE)
		{ // Temporarily stop
			GifDMAInt(16);
			cpuRegs.dmastall |= 1 << DMAC_GIF;
			return;
		}

		if ((dmacRegs.ctrl.STD == STD_GIF) && (gif.prevcycles != 0))
		{
			if ((gifch.madr + (gifch.qwc * 16)) > dmacRegs.stadr.ADDR)
			{
				GifDMAInt(4);
				cpuRegs.dmastall |= 1 << DMAC_GIF;
				gif.gscycles = 0;
				return;
			}
			gif.prevcycles = 0;
			gifch.qwc = 0;
		}

		if ((gifch.chcr.MOD == CHAIN_MODE) && (!gif.gspath3done) && gifch.qwc == 0) // Chain Mode
		{
			ptag = ReadTag();
			if (ptag == NULL)
				return;
			gifRegs.stat.FQC = std::min((u32)0x10, gifch.qwc);
			CSRreg.FIFO      = CalculateFIFOCSR();

			if (dmacRegs.ctrl.STD == STD_GIF)
			{
				// there are still bugs, need to also check if gifch.madr +16*qwc >= stadr, if not, stall
				if ((ptag->ID == TAG_REFS) && ((gifch.madr + (gifch.qwc * 16)) > dmacRegs.stadr.ADDR))
				{
					// stalled.
					// We really need to test this. Pay attention to prevcycles, as it used to trigger GIFchains in the code above. (rama)
					gif.prevcycles = gif.gscycles;
					gifch.tadr -= 16;
					gifch.qwc = 0;
					hwDmacIrq(DMAC_STALL_SIS);
					GifDMAInt(128);
					gif.gscycles = 0;
					cpuRegs.dmastall |= 1 << DMAC_GIF;
					return;
				}
			}

			checkTieBit(ptag);
		}

		// Transfer Dn_QWC from Dn_MADR to GIF
		if (gifch.qwc > 0) // Normal Mode
		{
			GIFchain(); // Transfers the data set by the switch
			if (gifUnit.Path3Masked() || !gifUnit.CanDoPath3())
				cpuRegs.dmastall |= 1 << DMAC_GIF;
			else
				cpuRegs.dmastall &= ~(1 << DMAC_GIF);
			return;
		}
	}

	gif.prevcycles = 0;
	GifDMAInt(16);
}

void dmaGIF(void)
{
	gif.gspath3done = false; // For some reason this doesn't clear? So when the system starts the thread, we will clear it :)
	cpuRegs.dmastall &= ~(1 << DMAC_GIF);
	if (gifch.chcr.MOD == NORMAL_MODE) // Else it really is a normal transfer and we want to quit, else it gets confused with chains
		gif.gspath3done = true;

	if (gifch.chcr.MOD == CHAIN_MODE && gifch.qwc > 0)
	{
		tDMA_TAG tmp;
		tmp._u32 = gifch.chcr._u32;
		if ((tmp.ID == TAG_REFE) || (tmp.ID == TAG_END) || (tmp.IRQ && gifch.chcr.TIE))
			gif.gspath3done = true;
	}

	gifInterrupt();
}

static u32 QWCinGIFMFIFO(u32 DrainADDR)
{
	u32 ret;

	// Calculate what we have in the fifo.
	if (DrainADDR <= spr0ch.madr)
	{
		// Drain is below the write position, calculate the difference between them
		ret = (spr0ch.madr - DrainADDR) >> 4;
	}
	else
	{
		u32 limit = dmacRegs.rbor.ADDR + dmacRegs.rbsr.RMSK + 16;
		// Drain is higher than SPR so it has looped round,
		// calculate from base to the SPR tag addr and what is left in the top of the ring
		ret = ((spr0ch.madr - dmacRegs.rbor.ADDR) + (limit - DrainADDR)) >> 4;
	}
	if (ret == 0)
		gif.gifstate = GIF_STATE_EMPTY;

	return ret;
}

static __fi bool mfifoGIFrbTransfer(void)
{
	u32 qwc = std::min(QWCinGIFMFIFO(gifch.madr), gifch.qwc);

	if (qwc == 0) // Either gifch.qwc is 0 (shouldn't get here) or the FIFO is empty.
		return true;

	u8* src = (u8*)PSM(gifch.madr);
	if (src == NULL)
		return false;

	u32 MFIFOUntilEnd = ((dmacRegs.rbor.ADDR + dmacRegs.rbsr.RMSK + 16) - gifch.madr) >> 4;
	bool needWrap = MFIFOUntilEnd < qwc;
	u32 firstTransQWC = needWrap ? MFIFOUntilEnd : qwc;
	u32 transferred;

	transferred = WRITERING_DMA((u32*)src, firstTransQWC); // First part

	gifch.madr = dmacRegs.rbor.ADDR + (gifch.madr & dmacRegs.rbsr.RMSK);
	gifch.tadr = dmacRegs.rbor.ADDR + (gifch.tadr & dmacRegs.rbsr.RMSK);

	if (needWrap && transferred == MFIFOUntilEnd)
	{
		src = (u8*)PSM(dmacRegs.rbor.ADDR);
		if (src == NULL)
			return false;

		// Need to do second transfer to wrap around
		const uint secondTransQWC = qwc - MFIFOUntilEnd;
		const u32 transferred2 = WRITERING_DMA((u32*)src, secondTransQWC); // Second part

		gif.mfifocycles += (transferred2 + transferred) * 2;
	}
	else
		gif.mfifocycles += transferred * 2;

	return true;
}

static __fi void mfifoGIFchain(void)
{
	// Is QWC = 0? if so there is nothing to transfer
	if (gifch.qwc == 0)
	{
		gif.mfifocycles += 4;
		return;
	}

	if ((gifch.madr & ~dmacRegs.rbsr.RMSK) == dmacRegs.rbor.ADDR)
	{
		if (QWCinGIFMFIFO(gifch.madr) == 0)
		{
			gif.gifstate = GIF_STATE_EMPTY;
			gif.mfifocycles += 4;
			return;
		}

		if (!mfifoGIFrbTransfer())
		{
			gif.mfifocycles += 4;
			gifch.qwc = 0;
			gif.gspath3done = true;
			return;
		}

		// This ends up being done more often but it's safer :P
		// Make sure we wrap the addresses, dont want it being stuck outside the ring when reading from the ring!
		gifch.madr = dmacRegs.rbor.ADDR + (gifch.madr & dmacRegs.rbsr.RMSK);
		gifch.tadr = gifch.madr;
	}
	else
	{
		tDMA_TAG* pMem = dmaGetAddr(gifch.madr, false);
		if (pMem == NULL)
		{
			gif.mfifocycles += 4;
			gifch.qwc = 0;
			gif.gspath3done = true;
			return;
		}

		gif.mfifocycles += WRITERING_DMA((u32*)pMem, gifch.qwc) * 2;
	}

	return;
}

#define QWCTAG(mask) (dmacRegs.rbor.ADDR + ((mask) & dmacRegs.rbsr.RMSK))

static void mfifoGifMaskMem(int id)
{
	switch (id)
	{
		// These five transfer data following the tag, need to check its within the buffer (Front Mission 4)
		case TAG_CNT:
		case TAG_NEXT:
		case TAG_CALL:
		case TAG_RET:
		case TAG_END:
			if (   (gifch.madr < dmacRegs.rbor.ADDR)
			    || (gifch.madr > (dmacRegs.rbor.ADDR + (u32)dmacRegs.rbsr.RMSK)))
				gifch.madr = QWCTAG(gifch.madr);
			break;
		default:
			// Do nothing as the MADR could be outside
			break;
	}
}

void mfifoGIFtransfer(void)
{
	tDMA_TAG* ptag;
	gif.mfifocycles = 0;

	if (gifRegs.ctrl.PSE) // Temporarily stop
	{
		CPU_INT(DMAC_MFIFO_GIF, 16);
		cpuRegs.dmastall |= 1 << DMAC_MFIFO_GIF;
		return;
	}

	if (gifch.qwc == 0)
	{
		gifch.tadr = QWCTAG(gifch.tadr);

		if (QWCinGIFMFIFO(gifch.tadr) == 0)
		{
			gif.gifstate = GIF_STATE_EMPTY;
			GifDMAInt(4);
			cpuRegs.dmastall |= 1 << DMAC_MFIFO_GIF;
			return;
		}

		ptag             = dmaGetAddr(gifch.tadr, false);
		gifch.unsafeTransfer(ptag);
		gifch.madr       = ptag[1]._u32;

		gifRegs.stat.FQC = std::min((u32)0x10, gifch.qwc);
		CSRreg.FIFO      = CalculateFIFOCSR();

		gif.mfifocycles += 2;

		gif.gspath3done  = hwDmacSrcChainWithStack(gifch, ptag->ID);

		mfifoGifMaskMem(ptag->ID);

		gifch.tadr = QWCTAG(gifch.tadr);

		if ((gifch.chcr.TIE) && (ptag->IRQ))
			gif.gspath3done = true;
	}

	mfifoGIFchain();

	GifDMAInt(std::max(gif.mfifocycles, (u32)4));
}

void gifMFIFOInterrupt(void)
{
	gif.mfifocycles = 0;

	if (dmacRegs.ctrl.MFD != MFD_GIF) // GIF not in MFIFO anymore, come out.
	{
		gifInterrupt();
		cpuRegs.dmastall |= 1 << DMAC_MFIFO_GIF;
		return;
	}

	gifCheckPathStatus(false);

	if (gifUnit.gifPath[GIF_PATH_3].state == GIF_PATH_IDLE)
	{
		if (vif1Regs.stat.VGW)
		{
			// Check if VIF is in a cycle or is currently "idle" waiting for GIF to come back.
			if (!(cpuRegs.interrupt & (1 << DMAC_VIF1)))
				CPU_INT(DMAC_VIF1, 1);

			// Make sure it loops if the GIF packet is empty to prepare for the next packet
			// or end if it was the end of a packet.
			// This must trigger after VIF retriggers as VIf might instantly mask Path3
			if (!gifUnit.Path3Masked() || gifch.qwc == 0)
			{
				GifDMAInt(16);
			}
			if (gifUnit.Path3Masked() || !gifUnit.CanDoPath3())
				cpuRegs.dmastall |= 1 << DMAC_MFIFO_GIF;
			else
				cpuRegs.dmastall &= ~(1 << DMAC_MFIFO_GIF);
			return;
		}
	}

	if (gifUnit.gsSIGNAL.queued)
	{
		GifDMAInt(128);
		cpuRegs.dmastall |= 1 << DMAC_MFIFO_GIF;
		return;
	}

	// If there's something in the FIFO and we can do PATH3, empty the FIFO.
	if (gif_fifo.fifoSize > 0)
	{
		const int readSize = gif_fifo.read_fifo();

		if (readSize)
			GifDMAInt(readSize * BIAS);

		// The following is quite timing sensitive so we need to pause/resume the DMA in these certain scenarios
		// If the DMA is masked/blocked and the fifo is full, no need to run the DMA
		// If we just read from the fifo, we want to loop and not read more DMA
		// If there is no DMA data waiting and the DMA is active, let the DMA progress until there is
		if ((!CheckPaths() && gif_fifo.fifoSize == 16) || readSize)
		{
			if (gifUnit.Path3Masked() || !gifUnit.CanDoPath3())
				cpuRegs.dmastall |= 1 << DMAC_MFIFO_GIF;
			else
				cpuRegs.dmastall &= ~(1 << DMAC_MFIFO_GIF);
			return;
		}
	}

	if (!gifch.chcr.STR)
		return;

	if (spr0ch.madr == gifch.tadr || (gif.gifstate & GIF_STATE_EMPTY))
	{
		gif.gifstate = GIF_STATE_EMPTY; // In case of madr = tadr we need to set it
		FireMFIFOEmpty();

		if (gifch.qwc > 0 || !gif.gspath3done)
		{
			cpuRegs.dmastall |= 1 << DMAC_MFIFO_GIF;
			return;
		}
	}

	if (gifch.qwc > 0 || !gif.gspath3done)
	{
		mfifoGIFtransfer();
		if (gifUnit.Path3Masked() || !gifUnit.CanDoPath3())
			cpuRegs.dmastall |= 1 << DMAC_MFIFO_GIF;
		else
			cpuRegs.dmastall &= ~(1 << DMAC_MFIFO_GIF);
		return;
	}

	gif.gscycles     = 0;

	gifch.chcr.STR   = false;
	gif.gifstate     = GIF_STATE_READY;
	gifRegs.stat.FQC = gif_fifo.fifoSize;
	CSRreg.FIFO      = CalculateFIFOCSR();
	hwDmacIrq(DMAC_GIF);
	cpuRegs.dmastall &= ~(1 << DMAC_MFIFO_GIF);
	if (gif_fifo.fifoSize)
		GifDMAInt(8 * BIAS);
}

bool SaveStateBase::gifDmaFreeze()
{
	// Note: mfifocycles is not a persistent var, so no need to save it here.
	if (!(FreezeTag("GIFdma")))
		return false;

	Freeze(gif);
	Freeze(gif_fifo);

	return IsOkay();
}
