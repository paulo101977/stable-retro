#include "common.h"

extern Disc* disc;

u32 libGDR_GetTrackNumber(u32 sector, u32& elapsed)
{
	if (disc != nullptr)
	{
		for (size_t i = 0; i < disc->tracks.size(); i++)
			if (disc->tracks[i].StartFAD <= sector && (sector <= disc->tracks[i].EndFAD || disc->tracks[i].EndFAD == 0))
			{
				elapsed = sector - disc->tracks[i].StartFAD;
				return i + 1;
			}
	}
	elapsed = 0;
	return 0xAA;
}

bool libGDR_GetTrack(u32 track_num, u32& start_fad, u32& end_fad)
{
	if (track_num == 0 || disc == nullptr || track_num > disc->tracks.size())
		return false;
	start_fad = disc->tracks[track_num - 1].StartFAD;
	end_fad = disc->tracks[track_num - 1].EndFAD;
	if (end_fad == 0)
	{
		if (track_num == disc->tracks.size())
			end_fad = disc->LeadOut.StartFAD - 1;
		else
			end_fad = disc->tracks[track_num].StartFAD - 1;
	}

	return true;
}

std::string libGDR_GetDiskCatalog()
{
	if (disc != nullptr)
		return disc->catalog;
	else
		return "";
}

std::string libGDR_GetTrackIsrc(u32 trackNum)
{
	if (trackNum == 0 || disc == nullptr || trackNum > disc->tracks.size())
		return "";
	return disc->tracks[trackNum - 1].isrc;
}

void libGDR_GetTrackAdrAndControl(u32 trackNum, u8& adr, u8& ctrl)
{
	if (trackNum == 0 || disc == nullptr || trackNum > disc->tracks.size())
	{
		adr = 0;
		ctrl = 0;
	}
	else
	{
		const Track& track = disc->tracks[trackNum - 1];
		adr = track.ADR | !track.isDataTrack();	// Force subcode Q data type 1 for audio tracks
		ctrl = track.CTRL;
	}
}
