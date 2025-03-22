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

#include "Common.h"

#include "R5900OpcodeTables.h"
#include "VUmicro.h"

#define CP2COND (((vuRegs[0].VI[REG_VPU_STAT].US[0] >> 8) & 1))

//Run the FINISH either side of the VCALL's as we have no control over it past here.
void VCALLMS(void)
{
	_vu0FinishMicro();
	vu0ExecMicro(((cpuRegs.code >> 6) & 0x7FFF));
}

void VCALLMSR(void)
{
	_vu0FinishMicro();
	vu0ExecMicro(vuRegs[0].VI[REG_CMSAR0].US[0]);
}

void BC2F(void)
{
	if (CP2COND == 0)
		intDoBranch(_BranchTarget_);
}

void BC2T(void)
{
	if (CP2COND == 1)
		intDoBranch(_BranchTarget_);
}

void BC2FL(void)
{
	if (CP2COND == 0)
		intDoBranch(_BranchTarget_);
	else
		cpuRegs.pc+= 4;
}

void BC2TL(void)
{
	if (CP2COND == 1)
		intDoBranch(_BranchTarget_);
	else
		cpuRegs.pc+= 4;
}
