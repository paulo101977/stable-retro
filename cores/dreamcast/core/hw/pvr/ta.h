#pragma once
#include "types.h"
#include "ta_ctx.h"
#include "hw/sh4/sh4_if.h"

constexpr u32 SZ32 = 1;
constexpr u32 SZ64 = 2;

struct TA_context;

void ta_vtx_ListInit(bool continuation);
void ta_vtx_SoftReset();

void DYNACALL ta_vtx_data32(const SQBuffer *data);
void ta_vtx_data(const SQBuffer *data, u32 size);

void ta_parse(TA_context *ctx, bool primRestart);

class TaTypeLut
{
public:
	static constexpr u32 INVALID_TYPE = 0xFFFFFFFF;

	static const TaTypeLut& instance() {
		static TaTypeLut _instance;
		return _instance;
	}
	u32 table[256];

private:
	TaTypeLut();
	u32 poly_data_type_id(PCW pcw);
	u32 poly_header_type_size(PCW pcw);
};
