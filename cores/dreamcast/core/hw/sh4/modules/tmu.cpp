/*
	Lovely timers, its amazing how many times this module was bugged
*/

#include "types.h"
#include "hw/sh4/sh4_sched.h"
#include "hw/sh4/sh4_interrupts.h"
#include "hw/sh4/sh4_mmr.h"
#include "serialize.h"

#define tmu_underflow 0x0100
#define tmu_UNIE      0x0020

TMURegisters tmu;
static u32 tmu_shift[3];
static u32 tmu_mask[3];
static u64 tmu_mask64[3];

static u32 old_mode[3] = { 0xFFFF, 0xFFFF, 0xFFFF};

static const InterruptID tmu_intID[3] = { sh4_TMU0_TUNI0, sh4_TMU1_TUNI1, sh4_TMU2_TUNI2 };
int tmu_sched[3];

#if 0
const u32 tmu_ch_bit[3]={1,2,4};

u32 tmu_prescaler[3];
u32 tmu_prescaler_shift[3];
u32 tmu_prescaler_mask[3];

//Accurate counts for the channel ch
template<u32 ch>
void UpdateTMU_chan(u32 clc)
{
	//if channel is on
	//if ((TMU_TSTR & tmu_ch_bit[ch])!=0)
	//{
		//count :D
		tmu_prescaler[ch]+=clc;
		u32 steps=tmu_prescaler[ch]>>tmu_prescaler_shift[ch];
		
		//remove the full steps from the prescaler counter
		tmu_prescaler[ch]&=tmu_prescaler_mask[ch];

		if (unlikely(steps>TMU_TCNT(ch)))
		{
			//remove the 'extra' steps to overflow
			steps-=TMU_TCNT(ch);
			//refill the counter
			TMU_TCNT(ch) = TMU_TCOR(ch);
			//raise the interrupt
			TMU_TCR(ch) |= tmu_underflow;
			InterruptPend(tmu_intID[ch], true);
			
			//remove the full underflows (possible because we only check every 448 cycles)
			//this can be done with a div, but its very very very rare so this is probably faster
			//THIS can probably be replaced with a verify check on counter setup (haven't seen any game do this)
			while(steps>TMU_TCOR(ch))
				steps-=TMU_TCOR(ch);

			//steps now has the partial steps needed for update, guaranteed it won't cause an overflow
		}
		//count down
		TMU_TCNT(ch)-=steps;
	//}
}

template<u32 chans>
void UpdateTMU_i(u32 Cycles)
{
	if (chans & 1) UpdateTMU_chan<0>(Cycles);
	if (chans & 2) UpdateTMU_chan<1>(Cycles);
	if (chans & 4) UpdateTMU_chan<2>(Cycles);
}
#endif

static u32 tmu_ch_base[3];
static u64 tmu_ch_base64[3];

static u32 read_TMU_TCNTch(u32 ch)
{
	return tmu_ch_base[ch] - ((sh4_sched_now64() >> tmu_shift[ch])&tmu_mask[ch]);
}

static s64 read_TMU_TCNTch64(u32 ch)
{
	return tmu_ch_base64[ch] - ((sh4_sched_now64() >> tmu_shift[ch])&tmu_mask64[ch]);
}

static void sched_chan_tick(int ch)
{
	//schedule next interrupt
	//return TMU_TCOR(ch) << tmu_shift[ch];

	u32 togo = read_TMU_TCNTch(ch);

	if (togo > SH4_MAIN_CLOCK)
		togo = SH4_MAIN_CLOCK;

	u32 cycles = togo << tmu_shift[ch];

	if (cycles > SH4_MAIN_CLOCK)
		cycles = SH4_MAIN_CLOCK;

	if (tmu_mask[ch])
		sh4_sched_request(tmu_sched[ch], cycles);
	else
		sh4_sched_request(tmu_sched[ch], -1);
}

static void write_TMU_TCNTch(u32 ch, u32 data)
{
	//u32 TCNT=read_TMU_TCNTch(ch);
	tmu_ch_base[ch]=data+((sh4_sched_now64()>>tmu_shift[ch])&tmu_mask[ch]);
	tmu_ch_base64[ch] = data + ((sh4_sched_now64() >> tmu_shift[ch])&tmu_mask64[ch]);

	sched_chan_tick(ch);
}

template<u32 ch>
static u32 read_TMU_TCNT(u32 addr)
{
	return read_TMU_TCNTch(ch);
}

template<u32 ch>
static void write_TMU_TCNT(u32 addr, u32 data)
{
	write_TMU_TCNTch(ch,data);
}

static void turn_on_off_ch(u32 ch, bool on)
{
	u32 TCNT=read_TMU_TCNTch(ch);
	tmu_mask[ch]=on?0xFFFFFFFF:0x00000000;
	tmu_mask64[ch] = on ? 0xFFFFFFFFFFFFFFFF : 0x0000000000000000;
	write_TMU_TCNTch(ch,TCNT);
}

//Update internal counter registers
static void UpdateTMUCounts(u32 reg)
{
	InterruptPend(tmu_intID[reg],TMU_TCR(reg) & tmu_underflow);
	InterruptMask(tmu_intID[reg],TMU_TCR(reg) & tmu_UNIE);

	if (old_mode[reg]==(TMU_TCR(reg) & 0x7))
		return;
	else
		old_mode[reg]=(TMU_TCR(reg) & 0x7);

	u32 TCNT=read_TMU_TCNTch(reg);
	switch(TMU_TCR(reg) & 0x7)
	{
		case 0: //4
			tmu_shift[reg]=2;
			break;

		case 1: //16
			tmu_shift[reg]=4;
			break;

		case 2: //64
			tmu_shift[reg]=6;
			break;

		case 3: //256
			tmu_shift[reg]=8;
			break;

		case 4: //1024
			tmu_shift[reg]=10;
			break;

		case 5: //reserved
			INFO_LOG(SH4, "TMU ch%d - TCR%d mode is reserved (5)",reg,reg);
			break;

		case 6: //RTC
			INFO_LOG(SH4, "TMU ch%d - TCR%d mode is RTC (6), can't be used on Dreamcast",reg,reg);
			break;

		case 7: //external
			INFO_LOG(SH4, "TMU ch%d - TCR%d mode is External (7), can't be used on Dreamcast",reg,reg);
			break;
	}
	tmu_shift[reg]+=2;
	write_TMU_TCNTch(reg,TCNT);
}

//Write to status registers
template<int ch>
static void TMU_TCR_write(u32 addr, u16 data)
{
	if (ch == 2)
		TMU_TCR(ch) = data & 0x03ff;
	else
		TMU_TCR(ch) = data & 0x013f;
	UpdateTMUCounts(ch);
}

//Chan 2 not used functions
static u32 TMU_TCPR2_read(u32 addr)
{
	INFO_LOG(SH4, "Read from TMU_TCPR2 - this register should be not used on Dreamcast according to docs");
	return 0;
}

static void TMU_TCPR2_write(u32 addr, u32 data)
{
	INFO_LOG(SH4, "Write to TMU_TCPR2 - this register should be not used on Dreamcast according to docs, data=%d", data);
}

static void write_TMU_TSTR(u32 addr, u8 data)
{
	TMU_TSTR = data & 7;

	for (int i = 0; i < 3; i++)
		turn_on_off_ch(i, data & (1 << i));
}

static int sched_tmu_cb(int ch, int sch_cycl, int jitter, void *arg)
{
	if (tmu_mask[ch]) {
		
		u32 tcnt = read_TMU_TCNTch(ch);
		
		s64 tcnt64 = (s64)read_TMU_TCNTch64(ch);

		//64 bit maths to differentiate big values from overflows
		if (tcnt64 <= jitter) {
			//raise interrupt, timer counted down
			TMU_TCR(ch) |= tmu_underflow;
			InterruptPend(tmu_intID[ch], true);
			
			//printf("Interrupt for %d, %d cycles\n", ch, sch_cycl);

			//schedule next trigger by writing the TCNT register
			u32 tcor = TMU_TCOR(ch);
			// Don't miss an underflow if tcor is less than -tcnt
			write_TMU_TCNTch(ch, (u32)std::max<s64>((u64)tcor + (s32)tcnt, 0));
		}
		else {
			
			//schedule next trigger by writing the TCNT register
			write_TMU_TCNTch(ch, tcnt);
		}

		return 0;	//has already been scheduled by TCNT write
	}
	else {
		return 0;	//this channel is disabled, no need to schedule next event
	}
}

//Init/Res/Term
void TMURegisters::init()
{
	super::init();

	//TMU TOCR 0xFFD80000 0x1FD80000 8 0x00 0x00 Held Held Pclk
	setRW<TMU_TOCR_addr, u8, 1>();

	//TMU TSTR 0xFFD80004 0x1FD80004 8 0x00 0x00 Held 0x00 Pclk
	setWriteHandler<TMU_TSTR_addr>(write_TMU_TSTR);

	//TMU TCOR0 0xFFD80008 0x1FD80008 32 0xFFFFFFFF 0xFFFFFFFF Held Held Pclk
	setRW<TMU_TCOR0_addr>();

	//TMU TCNT0 0xFFD8000C 0x1FD8000C 32 0xFFFFFFFF 0xFFFFFFFF Held Held Pclk
	setHandlers<TMU_TCNT0_addr>(read_TMU_TCNT<0>, write_TMU_TCNT<0>);

	//TMU TCR0 0xFFD80010 0x1FD80010 16 0x0000 0x0000 Held Held Pclk
	setWriteHandler<TMU_TCR0_addr>(TMU_TCR_write<0>);

	//TMU TCOR1 0xFFD80014 0x1FD80014 32 0xFFFFFFFF 0xFFFFFFFF Held Held Pclk
	setRW<TMU_TCOR1_addr>();

	//TMU TCNT1 0xFFD80018 0x1FD80018 32 0xFFFFFFFF 0xFFFFFFFF Held Held Pclk
	setHandlers<TMU_TCNT1_addr>(read_TMU_TCNT<1>, write_TMU_TCNT<1>);

	//TMU TCR1 0xFFD8001C 0x1FD8001C 16 0x0000 0x0000 Held Held Pclk
	setWriteHandler<TMU_TCR1_addr>(TMU_TCR_write<1>);

	//TMU TCOR2 0xFFD80020 0x1FD80020 32 0xFFFFFFFF 0xFFFFFFFF Held Held Pclk
	setRW<TMU_TCOR2_addr>();

	//TMU TCNT2 0xFFD80024 0x1FD80024 32 0xFFFFFFFF 0xFFFFFFFF Held Held Pclk
	setHandlers<TMU_TCNT2_addr>(read_TMU_TCNT<2>, write_TMU_TCNT<2>);
	
	//TMU TCR2 0xFFD80028 0x1FD80028 16 0x0000 0x0000 Held Held Pclk
	setWriteHandler<TMU_TCR2_addr>(TMU_TCR_write<2>);

	//TMU TCPR2 0xFFD8002C 0x1FD8002C 32 Held Held Held Held Pclk
	setHandlers<TMU_TCPR2_addr>(TMU_TCPR2_read, TMU_TCPR2_write);

	for (std::size_t i = 0; i < std::size(tmu_sched); i++)
		tmu_sched[i] = sh4_sched_register(i, &sched_tmu_cb);

	reset();
}


void TMURegisters::reset()
{
	super::reset();

	memset(tmu_shift, 0, sizeof(tmu_shift));
	memset(tmu_mask, 0, sizeof(tmu_mask));
	memset(tmu_mask64, 0, sizeof(tmu_mask64));
	memset(old_mode, 0xFF, sizeof(old_mode));
	memset(tmu_ch_base, 0, sizeof(tmu_ch_base));
	memset(tmu_ch_base64, 0, sizeof(tmu_ch_base64));

	TMU_TCOR(0) = TMU_TCOR(1) = TMU_TCOR(2) = 0xffffffff;

	UpdateTMUCounts(0);
	UpdateTMUCounts(1);
	UpdateTMUCounts(2);

	write_TMU_TSTR(0, 0);

	for (int i = 0; i < 3; i++)
		write_TMU_TCNTch(i, 0xffffffff);
}

void TMURegisters::term()
{
	super::term();
	for (int& sched_id : tmu_sched)
	{
		sh4_sched_unregister(sched_id);
		sched_id = -1;
	}
}

void TMURegisters::serialize(Serializer& ser)
{
	ser << tmu_shift;
	ser << tmu_mask;
	ser << tmu_mask64;
	ser << old_mode;
	ser << tmu_ch_base;
	ser << tmu_ch_base64;
}

void TMURegisters::deserialize(Deserializer& deser)
{
	deser >> tmu_shift;
	deser >> tmu_mask;
	deser >> tmu_mask64;
	deser >> old_mode;
	deser >> tmu_ch_base;
	deser >> tmu_ch_base64;
}
