/*
	This file is part of Warzone 2100.
	Copyright (C) 2026 Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/

#ifndef __INCLUDED_LIB_SOUND_MISSION_MUSIC_H__
#define __INCLUDED_LIB_SOUND_MISSION_MUSIC_H__

#include <chrono>
#include <string>

class MissionMusicStemId
{
public:
	explicit MissionMusicStemId(std::string value);

	const std::string& str() const { return value_; }
	bool empty() const { return value_.empty(); }

private:
	std::string value_;
};

class MissionMusicStemVolume
{
public:
	static MissionMusicStemVolume fromClamped(double value);

	float value() const { return value_; }

private:
	explicit MissionMusicStemVolume(float value);

	float value_ = 1.0f;
};

struct MissionMusicStemSpec
{
	MissionMusicStemId id;
	std::string filename;
	MissionMusicStemVolume targetVolume;
	std::chrono::milliseconds fadeDuration;
	bool loop = true;
};

bool missionMusic_PlayStem(MissionMusicStemSpec spec);
bool missionMusic_SetStemVolume(MissionMusicStemId id, MissionMusicStemVolume volume, std::chrono::milliseconds fadeDuration);
bool missionMusic_StopStem(MissionMusicStemId id, std::chrono::milliseconds fadeDuration);
void missionMusic_StopAllStems(std::chrono::milliseconds fadeDuration);
bool missionMusic_IsStemPlaying(MissionMusicStemId id);

void missionMusic_Update();
void missionMusic_StopAllNow();

#endif // __INCLUDED_LIB_SOUND_MISSION_MUSIC_H__
