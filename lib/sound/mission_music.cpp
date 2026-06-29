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

#include "mission_music.h"

#include "audio.h"
#include "mixer.h"
#include "track.h"
#include "lib/framework/frame.h"
#include "lib/gamelib/gtime.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <physfs.h>

MissionMusicStemId::MissionMusicStemId(std::string value)
	: value_(std::move(value))
{
}

MissionMusicStemVolume::MissionMusicStemVolume(float value)
	: value_(value)
{
}

MissionMusicStemVolume MissionMusicStemVolume::fromClamped(double value)
{
	if (!std::isfinite(value))
	{
		value = 1.0;
	}
	return MissionMusicStemVolume(static_cast<float>(std::clamp(value, 0.0, 1.0)));
}

namespace
{

struct MissionMusicStreamState
{
	AUDIO_STREAM *stream = nullptr;
	bool finished = false;
	bool stopQueued = false;
};

class MissionMusicStreamHandle
{
public:
	MissionMusicStreamHandle() = default;
	~MissionMusicStreamHandle() noexcept
	{
		stop();
	}

	MissionMusicStreamHandle(const MissionMusicStreamHandle&) = delete;
	MissionMusicStreamHandle& operator=(const MissionMusicStreamHandle&) = delete;

	MissionMusicStreamHandle(MissionMusicStreamHandle&& other) noexcept
		: state_(std::move(other.state_))
	{
	}

	MissionMusicStreamHandle& operator=(MissionMusicStreamHandle&& other) noexcept
	{
		if (this != &other)
		{
			stop();
			state_ = std::move(other.state_);
		}
		return *this;
	}

	static MissionMusicStreamHandle open(const std::string& filename, float volume)
	{
		auto state = std::make_shared<MissionMusicStreamState>();
		std::weak_ptr<MissionMusicStreamState> weakState = state;
		AUDIO_STREAM *stream = audio_PlayStream(filename.c_str(), volume, [weakState](const AUDIO_STREAM *finishedStream, const void *) {
			auto lockedState = weakState.lock();
			if (!lockedState)
			{
				return;
			}
			if (lockedState->stream == finishedStream)
			{
				lockedState->stream = nullptr;
				lockedState->finished = true;
			}
		}, nullptr);

		if (stream == nullptr)
		{
			return MissionMusicStreamHandle();
		}

		state->stream = stream;
		return MissionMusicStreamHandle(std::move(state));
	}

	bool valid() const
	{
		return state_ && state_->stream != nullptr && !state_->stopQueued;
	}

	bool finished() const
	{
		return state_ && state_->finished;
	}

	void setVolume(float volume)
	{
		if (!valid())
		{
			return;
		}
		sound_SetStreamVolume(state_->stream, volume);
	}

	void stop() noexcept
	{
		if (!state_ || state_->stream == nullptr || state_->stopQueued)
		{
			return;
		}
		state_->stopQueued = true;
		sound_StopStream(state_->stream);
	}

private:
	explicit MissionMusicStreamHandle(std::shared_ptr<MissionMusicStreamState> state)
		: state_(std::move(state))
	{
	}

	std::shared_ptr<MissionMusicStreamState> state_;
};

struct FadeState
{
	float startVolume = 1.0f;
	float targetVolume = 1.0f;
	UDWORD startTime = 0;
	UDWORD endTime = 0;

	bool active() const
	{
		return endTime > startTime;
	}
};

struct ActiveStem
{
	std::string id;
	std::string filename;
	MissionMusicStreamHandle stream;
	float currentVolume = 1.0f;
	float targetVolume = 1.0f;
	FadeState fade;
	bool loop = true;
	bool stopWhenFadeComplete = false;
};

enum class MissionMusicCommandType
{
	Play,
	SetVolume,
	Stop,
	StopAll,
};

struct MissionMusicCommand
{
	MissionMusicCommandType type;
	MissionMusicStemSpec spec;
	MissionMusicStemId id;
	MissionMusicStemVolume volume;
	std::chrono::milliseconds fadeDuration;
};

class MissionMusicStemManager
{
public:
	bool play(MissionMusicStemSpec spec)
	{
		ASSERT_OR_RETURN(false, !spec.id.empty(), "Mission music stem id must not be empty");
		ASSERT_OR_RETURN(false, !spec.filename.empty(), "Mission music stem filename must not be empty");
		ASSERT_OR_RETURN(false, spec.fadeDuration.count() >= 0, "Mission music stem fade must not be negative");

		if (audio_Disabled())
		{
			debug(LOG_SOUND, "Mission music stem \"%s\" not queued because audio is disabled", spec.id.str().c_str());
			return false;
		}
		if (!PHYSFS_exists(spec.filename.c_str()))
		{
			debug(LOG_ERROR, "Mission music stem \"%s\" missing file: %s", spec.id.str().c_str(), spec.filename.c_str());
			return false;
		}

		enqueue({MissionMusicCommandType::Play, std::move(spec), MissionMusicStemId(""), MissionMusicStemVolume::fromClamped(1.0), std::chrono::milliseconds(0)});
		return true;
	}

	bool setVolume(MissionMusicStemId id, MissionMusicStemVolume volume, std::chrono::milliseconds fadeDuration)
	{
		ASSERT_OR_RETURN(false, !id.empty(), "Mission music stem id must not be empty");
		ASSERT_OR_RETURN(false, fadeDuration.count() >= 0, "Mission music stem fade must not be negative");
		enqueue({MissionMusicCommandType::SetVolume, makeEmptySpec(), std::move(id), volume, fadeDuration});
		return true;
	}

	bool stop(MissionMusicStemId id, std::chrono::milliseconds fadeDuration)
	{
		ASSERT_OR_RETURN(false, !id.empty(), "Mission music stem id must not be empty");
		ASSERT_OR_RETURN(false, fadeDuration.count() >= 0, "Mission music stem fade must not be negative");
		enqueue({MissionMusicCommandType::Stop, makeEmptySpec(), std::move(id), MissionMusicStemVolume::fromClamped(0.0), fadeDuration});
		return true;
	}

	void stopAll(std::chrono::milliseconds fadeDuration)
	{
		ASSERT_OR_RETURN(, fadeDuration.count() >= 0, "Mission music stem fade must not be negative");
		enqueue({MissionMusicCommandType::StopAll, makeEmptySpec(), MissionMusicStemId(""), MissionMusicStemVolume::fromClamped(0.0), fadeDuration});
	}

	bool isPlaying(const MissionMusicStemId& id) const
	{
		if (activeStems_.find(id.str()) != activeStems_.end())
		{
			return true;
		}
		return std::any_of(fadingStems_.begin(), fadingStems_.end(), [&id](const ActiveStem& stem) {
			return stem.id == id.str();
		});
	}

	void update()
	{
		processQueuedCommands();
		updateActiveStemList(activeStems_);
		updateFadingStems();
	}

	void stopAllNow()
	{
		{
			std::lock_guard<std::mutex> guard(commandQueueMutex_);
			commandQueue_.clear();
		}
		activeStems_.clear();
		fadingStems_.clear();
	}

private:
	static MissionMusicStemSpec makeEmptySpec()
	{
		return MissionMusicStemSpec{MissionMusicStemId(""), "", MissionMusicStemVolume::fromClamped(0.0), std::chrono::milliseconds(0), false};
	}

	void enqueue(MissionMusicCommand command)
	{
		std::lock_guard<std::mutex> guard(commandQueueMutex_);
		commandQueue_.push_back(std::move(command));
	}

	void processQueuedCommands()
	{
		std::deque<MissionMusicCommand> commands;
		{
			std::lock_guard<std::mutex> guard(commandQueueMutex_);
			commands.swap(commandQueue_);
		}

		for (auto& command : commands)
		{
			switch (command.type)
			{
			case MissionMusicCommandType::Play:
				playNow(std::move(command.spec));
				break;
			case MissionMusicCommandType::SetVolume:
				setVolumeNow(command.id.str(), command.volume.value(), command.fadeDuration);
				break;
			case MissionMusicCommandType::Stop:
				stopNow(command.id.str(), command.fadeDuration);
				break;
			case MissionMusicCommandType::StopAll:
				stopAllNow(command.fadeDuration);
				break;
			}
		}
	}

	void playNow(MissionMusicStemSpec spec)
	{
		removeFadingStem(spec.id.str());

		auto existing = activeStems_.find(spec.id.str());
		if (existing != activeStems_.end())
		{
			if (spec.fadeDuration.count() > 0)
			{
				beginStopFade(existing->second, spec.fadeDuration);
				fadingStems_.push_back(std::move(existing->second));
			}
			activeStems_.erase(existing);
		}

		float initialVolume = spec.fadeDuration.count() > 0 ? 0.0f : spec.targetVolume.value();
		auto stream = MissionMusicStreamHandle::open(spec.filename, initialVolume * sound_GetMusicVolume());
		if (!stream.valid())
		{
			debug(LOG_ERROR, "Failed to open mission music stem \"%s\" from %s", spec.id.str().c_str(), spec.filename.c_str());
			return;
		}

		ActiveStem stem;
		stem.id = spec.id.str();
		stem.filename = std::move(spec.filename);
		stem.stream = std::move(stream);
		stem.currentVolume = initialVolume;
		stem.targetVolume = spec.targetVolume.value();
		stem.loop = spec.loop;
		if (spec.fadeDuration.count() > 0)
		{
			beginFade(stem, 0.0f, spec.targetVolume.value(), spec.fadeDuration);
		}
		auto stemId = stem.id;
		activeStems_.emplace(std::move(stemId), std::move(stem));
	}

	void setVolumeNow(const std::string& id, float volume, std::chrono::milliseconds fadeDuration)
	{
		auto existing = activeStems_.find(id);
		if (existing == activeStems_.end())
		{
			debug(LOG_SOUND, "Mission music stem \"%s\" volume change ignored; stem is not active", id.c_str());
			return;
		}
		if (fadeDuration.count() > 0)
		{
			beginFade(existing->second, existing->second.currentVolume, volume, fadeDuration);
		}
		else
		{
			existing->second.currentVolume = volume;
			existing->second.targetVolume = volume;
			existing->second.fade = {};
		}
	}

	void stopNow(const std::string& id, std::chrono::milliseconds fadeDuration)
	{
		auto existing = activeStems_.find(id);
		if (existing != activeStems_.end())
		{
			if (fadeDuration.count() > 0)
			{
				beginStopFade(existing->second, fadeDuration);
				fadingStems_.push_back(std::move(existing->second));
			}
			activeStems_.erase(existing);
		}

		for (auto& stem : fadingStems_)
		{
			if (stem.id == id)
			{
				if (fadeDuration.count() > 0)
				{
					beginStopFade(stem, fadeDuration);
				}
				else
				{
					stem.currentVolume = 0.0f;
					stem.stopWhenFadeComplete = true;
				}
			}
		}
		removeFinishedFadingStems();
	}

	void stopAllNow(std::chrono::milliseconds fadeDuration)
	{
		if (fadeDuration.count() <= 0)
		{
			activeStems_.clear();
			fadingStems_.clear();
			return;
		}

		for (auto& stem : activeStems_)
		{
			beginStopFade(stem.second, fadeDuration);
			fadingStems_.push_back(std::move(stem.second));
		}
		activeStems_.clear();

		for (auto& stem : fadingStems_)
		{
			beginStopFade(stem, fadeDuration);
		}
	}

	static void beginFade(ActiveStem& stem, float startVolume, float targetVolume, std::chrono::milliseconds fadeDuration)
	{
		stem.fade.startVolume = startVolume;
		stem.fade.targetVolume = targetVolume;
		stem.fade.startTime = realTime;
		stem.fade.endTime = realTime + static_cast<UDWORD>(std::max<int64_t>(0, fadeDuration.count()));
		stem.targetVolume = targetVolume;
	}

	static void beginStopFade(ActiveStem& stem, std::chrono::milliseconds fadeDuration)
	{
		beginFade(stem, stem.currentVolume, 0.0f, fadeDuration);
		stem.stopWhenFadeComplete = true;
	}

	void updateStem(ActiveStem& stem, bool& removeStem)
	{
		removeStem = false;

		if (stem.stream.finished())
		{
			if (stem.loop && !stem.stopWhenFadeComplete)
			{
				stem.stream = MissionMusicStreamHandle::open(stem.filename, stem.currentVolume * sound_GetMusicVolume());
				if (!stem.stream.valid())
				{
					debug(LOG_ERROR, "Failed to restart mission music stem \"%s\" from %s", stem.id.c_str(), stem.filename.c_str());
					removeStem = true;
					return;
				}
			}
			else
			{
				removeStem = true;
				return;
			}
		}

		if (stem.fade.active())
		{
			if (realTime >= stem.fade.endTime)
			{
				stem.currentVolume = stem.fade.targetVolume;
				stem.fade = {};
			}
			else
			{
				const float progress = static_cast<float>(realTime - stem.fade.startTime) / static_cast<float>(stem.fade.endTime - stem.fade.startTime);
				stem.currentVolume = stem.fade.startVolume + (stem.fade.targetVolume - stem.fade.startVolume) * std::clamp(progress, 0.0f, 1.0f);
			}
		}

		stem.stream.setVolume(stem.currentVolume * sound_GetMusicVolume());

		if (stem.stopWhenFadeComplete && !stem.fade.active() && stem.currentVolume <= 0.0f)
		{
			removeStem = true;
		}
	}

	void updateActiveStemList(std::unordered_map<std::string, ActiveStem>& stems)
	{
		for (auto it = stems.begin(); it != stems.end();)
		{
			bool removeStem = false;
			updateStem(it->second, removeStem);
			if (removeStem)
			{
				it = stems.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	void updateFadingStems()
	{
		for (auto& stem : fadingStems_)
		{
			bool removeStem = false;
			updateStem(stem, removeStem);
			if (removeStem)
			{
				stem.currentVolume = 0.0f;
				stem.stopWhenFadeComplete = true;
			}
		}
		removeFinishedFadingStems();
	}

	void removeFinishedFadingStems()
	{
		fadingStems_.erase(std::remove_if(fadingStems_.begin(), fadingStems_.end(), [](const ActiveStem& stem) {
			return stem.stopWhenFadeComplete && !stem.fade.active() && stem.currentVolume <= 0.0f;
		}), fadingStems_.end());
	}

	void removeFadingStem(const std::string& id)
	{
		fadingStems_.erase(std::remove_if(fadingStems_.begin(), fadingStems_.end(), [&id](const ActiveStem& stem) {
			return stem.id == id;
		}), fadingStems_.end());
	}

	mutable std::mutex commandQueueMutex_;
	std::deque<MissionMusicCommand> commandQueue_;
	std::unordered_map<std::string, ActiveStem> activeStems_;
	std::vector<ActiveStem> fadingStems_;
};

MissionMusicStemManager& missionMusicManager()
{
	static MissionMusicStemManager manager;
	return manager;
}

} // namespace

bool missionMusic_PlayStem(MissionMusicStemSpec spec)
{
	return missionMusicManager().play(std::move(spec));
}

bool missionMusic_SetStemVolume(MissionMusicStemId id, MissionMusicStemVolume volume, std::chrono::milliseconds fadeDuration)
{
	return missionMusicManager().setVolume(std::move(id), volume, fadeDuration);
}

bool missionMusic_StopStem(MissionMusicStemId id, std::chrono::milliseconds fadeDuration)
{
	return missionMusicManager().stop(std::move(id), fadeDuration);
}

void missionMusic_StopAllStems(std::chrono::milliseconds fadeDuration)
{
	missionMusicManager().stopAll(fadeDuration);
}

bool missionMusic_IsStemPlaying(MissionMusicStemId id)
{
	return missionMusicManager().isPlaying(id);
}

void missionMusic_Update()
{
	missionMusicManager().update();
}

void missionMusic_StopAllNow()
{
	missionMusicManager().stopAllNow();
}
