const SD_FALLBACK_HUMAN_PLAYER = 0;
const SD_MILLISECONDS_IN_SECOND = 1000;
const SD_SECONDS_IN_MINUTE = 60;
const SD_MISSION_MINUTES = 15;
const SD_ENABLE_MUSIC = false;
const SD_REPAIR_START_HEALTH = 20;
const SD_REPAIR_COMPLETE_HEALTH = 90;
const SD_REPAIR_CHECK_MS = 1000;
const SD_REPAIR_RADIUS_TILES = 5;
const SD_REPAIR_STEP = 14;
const SD_COMMAND_RELAY_LIMIT = 10;
const SD_FIELD_TEAM_X = 126;
const SD_FIELD_TEAM_Y = 214;
const SD_START_REVEAL_RADIUS = 1536;
const SD_OBJECTIVE_REVEAL_RADIUS = 1536;
const SD_PASSIVE_SLICE_PLAYERS = [1, 2, 3, 4, 5, 6];

const SD_RELAYS = [
	{
		label: "resRelay",
		name: "Relay 1",
		nextObjective: "Repair Relay 2.",
	},
	{
		label: "coaHQ",
		name: "Relay 2",
		nextObjective: "Repair Relay 3.",
	},
	{
		label: "royRelay",
		name: "Relay 3",
		nextObjective: "Signal complete.",
	},
];

const SD_STEMS = {
	dormant: "sd_dormant_bed",
	pulse: "sd_relay_pulse",
	harmony: "sd_relay_harmony",
	voice: "sd_relay_voice",
	threat: "sd_threat_layer",
};

const SD_FILES = {
	dormant: "audio/music/dormant_bed.ogg",
	pulse: "audio/music/relay_pulse.ogg",
	harmony: "audio/music/relay_harmony.ogg",
	voice: "audio/music/relay_voice.ogg",
	threat: "audio/music/threat_layer.ogg",
};

let sdRelayState = 0;
let sdThreatActive = false;
let sdMissionEnding = false;
let sdRelayRefs = [];
let sdRelayRepairActive = [];
let sdObjectiveSpotter = null;

function sdHumanPlayer()
{
	if (typeof selectedPlayer === "number")
	{
		return selectedPlayer;
	}
	return SD_FALLBACK_HUMAN_PLAYER;
}

function sdSecondsToMilliseconds(seconds)
{
	return seconds * SD_MILLISECONDS_IN_SECOND;
}

function sdMinutesToSeconds(minutes)
{
	return minutes * SD_SECONDS_IN_MINUTE;
}

function sdDebug(message)
{
	debug("SD: " + message);
}

function sdObjectMissing(obj)
{
	return obj === null || typeof obj === "undefined";
}

function sdObjectRef(obj)
{
	return {type: obj.type, player: obj.player, id: obj.id};
}

function sdRememberRelay(index, relay)
{
	sdRelayRefs[index - 1] = sdObjectRef(relay);
}

function sdStem(id, filename, volume, fadeMs)
{
	if (!SD_ENABLE_MUSIC)
	{
		return;
	}

	playMissionMusicStem(id, filename, {volume: volume, fadeMs: fadeMs, loop: true});
}

function sdAddFieldDroid(player, x, y, name, body, propulsion, turret)
{
	const droid = addDroid(player, x, y, name, body, propulsion, "", "", turret);
	if (sdObjectMissing(droid))
	{
		console(_("Signal Diaspora setup error: could not create ") + _(name) + ".");
	}
	return droid;
}

function sdPrepareHumanAssets()
{
	const humanPlayer = sdHumanPlayer();
	if (enumDroid(humanPlayer).length === 0)
	{
		sdAddFieldDroid(humanPlayer, SD_FIELD_TEAM_X, SD_FIELD_TEAM_Y,
			"Signal Truck 1", "Body1REC", "wheeled01", "Spade1Mk1");
		sdAddFieldDroid(humanPlayer, SD_FIELD_TEAM_X + 1, SD_FIELD_TEAM_Y,
			"Signal Truck 2", "Body1REC", "wheeled01", "Spade1Mk1");
		sdAddFieldDroid(humanPlayer, SD_FIELD_TEAM_X, SD_FIELD_TEAM_Y + 1,
			"Signal Repair 1", "Body1REC", "wheeled01", "LightRepair1");
		sdAddFieldDroid(humanPlayer, SD_FIELD_TEAM_X + 1, SD_FIELD_TEAM_Y + 1,
			"Signal Repair 2", "Body1REC", "wheeled01", "LightRepair1");
		sdAddFieldDroid(humanPlayer, SD_FIELD_TEAM_X + 2, SD_FIELD_TEAM_Y,
			"Signal Escort", "Body1REC", "wheeled01", "MG2Mk1");
	}
	sdDebug("human assets player=" + humanPlayer + " droids=" + enumDroid(humanPlayer).length);
}

function sdFocusFieldTeam()
{
	const humanPlayer = sdHumanPlayer();
	centreView(SD_FIELD_TEAM_X, SD_FIELD_TEAM_Y);
	addSpotter(SD_FIELD_TEAM_X, SD_FIELD_TEAM_Y, humanPlayer, SD_START_REVEAL_RADIUS, false, 0);
	setMiniMap(true);
	sdDebug("camera centered x=" + SD_FIELD_TEAM_X + " y=" + SD_FIELD_TEAM_Y);
}

function sdSetPassiveSliceAlliances()
{
	const humanPlayer = sdHumanPlayer();
	for (let i = 0; i < SD_PASSIVE_SLICE_PLAYERS.length; ++i)
	{
		const player = SD_PASSIVE_SLICE_PLAYERS[i];
		if (player !== humanPlayer)
		{
			setAlliance(humanPlayer, player, true);
		}
	}
	sdDebug("passive slice alliances set for player=" + humanPlayer);
}

function sdRestoreMusicState()
{
	if (!SD_ENABLE_MUSIC)
	{
		return;
	}

	hackStopIngameAudio();
	stopAllMissionMusicStems({fadeMs: 0});

	sdStem(SD_STEMS.dormant, SD_FILES.dormant, 0.65, 0);
	if (sdRelayState >= 1)
	{
		sdStem(SD_STEMS.pulse, SD_FILES.pulse, 0.7, 0);
	}
	if (sdRelayState >= 2)
	{
		sdStem(SD_STEMS.harmony, SD_FILES.harmony, 0.65, 0);
	}
	if (sdRelayState >= 3)
	{
		sdStem(SD_STEMS.voice, SD_FILES.voice, 0.7, 0);
	}
	if (sdThreatActive)
	{
		sdStem(SD_STEMS.threat, SD_FILES.threat, 0.55, 0);
	}
}

function sdRestoreRelay(index)
{
	if (sdRelayState >= index)
	{
		return;
	}

	sdRelayState = index;
	playSound("beep9.ogg");

	if (index === 1)
	{
		sdStem(SD_STEMS.pulse, SD_FILES.pulse, 0.7, 1500);
		console(_("Relay 1 restored. Move the field team to Relay 2."));
		sdMarkRelayObjective(2);
	}
	else if (index === 2)
	{
		sdStem(SD_STEMS.harmony, SD_FILES.harmony, 0.65, 1500);
		console(_("Relay 2 restored. Move the field team to Relay 3."));
		sdMarkRelayObjective(3);
	}
	else if (index === 3)
	{
		sdStem(SD_STEMS.voice, SD_FILES.voice, 0.7, 1800);
		console(_("Relay 3 restored. Signal complete."));
		removeTimer("sdCheckRelayRepairs");
		sdClearObjectiveMarker();
		setMissionTime(-1);
		queue("sdCompleteMission", sdSecondsToMilliseconds(4));
	}
}

function sdThreatOn()
{
	if (sdThreatActive)
	{
		return;
	}
	sdThreatActive = true;
	sdStem(SD_STEMS.threat, SD_FILES.threat, 0.55, 1000);
	console(_("Threat layer online."));
}

function sdThreatOff()
{
	if (!sdThreatActive)
	{
		return;
	}
	sdThreatActive = false;
	stopMissionMusicStem(SD_STEMS.threat, {fadeMs: 1000});
	console(_("Threat layer cleared."));
}

function sdCompleteMission()
{
	if (sdMissionEnding)
	{
		return;
	}
	sdDebug("complete mission");
	sdMissionEnding = true;
	removeTimer("sdCheckRelayRepairs");
	sdClearObjectiveMarker();
	setMissionTime(-1);
	if (SD_ENABLE_MUSIC)
	{
		stopAllMissionMusicStems({fadeMs: 2500});
	}
	gameOverMessage(true, false);
}

function sdFailMission(message)
{
	if (sdMissionEnding)
	{
		return;
	}
	sdDebug("fail mission: " + message);
	sdMissionEnding = true;
	removeTimer("sdCheckRelayRepairs");
	sdClearObjectiveMarker();
	setMissionTime(-1);
	if (SD_ENABLE_MUSIC)
	{
		stopAllMissionMusicStems({fadeMs: 1000});
	}
	console(_(message));
	gameOverMessage(false, false);
}

function sdRelay(index)
{
	const relayRef = sdRelayRefs[index - 1];
	if (relayRef)
	{
		const relay = getObject(relayRef.type, relayRef.player, relayRef.id);
		if (!sdObjectMissing(relay))
		{
			return relay;
		}
	}

	const labelRelay = getObject(SD_RELAYS[index - 1].label);
	if (!sdObjectMissing(labelRelay))
	{
		sdRememberRelay(index, labelRelay);
	}
	return labelRelay;
}

function sdClearObjectiveMarker()
{
	removeBeacon(sdHumanPlayer());
	if (sdObjectiveSpotter !== null)
	{
		removeSpotter(sdObjectiveSpotter);
		sdObjectiveSpotter = null;
	}
}

function sdApplyRelayStartHealth(index, relay)
{
	const relayInfo = SD_RELAYS[index - 1];
	setHealth(relay, SD_REPAIR_START_HEALTH);
	sdRememberRelay(index, relay);
	sdDebug(relayInfo.name + " prepared player=" + relay.player + " targetHealth=" + SD_REPAIR_START_HEALTH);
}

function sdPrepareRelay(index)
{
	const relayInfo = SD_RELAYS[index - 1];
	let relay = sdRelay(index);

	if (sdObjectMissing(relay))
	{
		console(_("Signal Diaspora setup error: missing ") + relayInfo.name + _(" label."));
		return;
	}

	sdRememberRelay(index, relay);
	sdApplyRelayStartHealth(index, relay);
}

function sdPrepareRelays()
{
	setStructureLimits("A0ComDroidControl", SD_COMMAND_RELAY_LIMIT, sdHumanPlayer());
	for (let i = 1; i <= SD_RELAYS.length; ++i)
	{
		sdPrepareRelay(i);
	}
}

function sdMarkRelayObjective(index)
{
	sdClearObjectiveMarker();

	if (index > SD_RELAYS.length)
	{
		return;
	}

	const relay = sdRelay(index);
	if (sdObjectMissing(relay))
	{
		return;
	}

	addBeacon(relay.x, relay.y, sdHumanPlayer(), SD_RELAYS[index - 1].name);
	sdObjectiveSpotter = addSpotter(relay.x, relay.y, sdHumanPlayer(), SD_OBJECTIVE_REVEAL_RADIUS, false, 0);
	sdDebug(SD_RELAYS[index - 1].name + " objective x=" + relay.x + " y=" + relay.y);
}

function sdRelayRestorers(relay)
{
	const humanPlayer = sdHumanPlayer();
	const droids = enumDroid(humanPlayer, DROID_CONSTRUCT).concat(enumDroid(humanPlayer, DROID_REPAIR));
	return droids.filter((droid) => (
		distBetweenTwoPoints(relay.x, relay.y, droid.x, droid.y) <= SD_REPAIR_RADIUS_TILES
	));
}

function sdAdvanceRelayRepair(index, relay)
{
	const restorers = sdRelayRestorers(relay);
	if (restorers.length === 0)
	{
		return;
	}

	const relayInfo = SD_RELAYS[index - 1];
	if (!sdRelayRepairActive[index - 1])
	{
		console(_(relayInfo.name) + _(" restoration in progress."));
		sdDebug(relayInfo.name + " restoration active restorers=" + restorers.length);
		sdRelayRepairActive[index - 1] = true;
	}

	const nextHealth = Math.min(SD_REPAIR_COMPLETE_HEALTH, relay.health + SD_REPAIR_STEP);
	setHealth(relay, nextHealth);
	if (nextHealth >= SD_REPAIR_COMPLETE_HEALTH)
	{
		sdRestoreRelay(index);
	}
}

function sdCheckRelayRepairs()
{
	const nextRelayIndex = sdRelayState + 1;
	if (nextRelayIndex > SD_RELAYS.length)
	{
		removeTimer("sdCheckRelayRepairs");
		return;
	}

	const relayInfo = SD_RELAYS[nextRelayIndex - 1];
	const relay = sdRelay(nextRelayIndex);
	if (sdObjectMissing(relay))
	{
		sdFailMission(relayInfo.name + _(" was destroyed before restoration."));
		return;
	}

	if (relay.health >= SD_REPAIR_COMPLETE_HEALTH)
	{
		sdRestoreRelay(nextRelayIndex);
		return;
	}

	sdAdvanceRelayRepair(nextRelayIndex, relay);
}

function eventStartLevel()
{
	sdRelayRefs = [];
	sdRelayRepairActive = [];
	sdObjectiveSpotter = null;
	sdMissionEnding = false;
	setMissionTime(sdMinutesToSeconds(SD_MISSION_MINUTES));
	sdDebug("start level selectedPlayer=" + sdHumanPlayer() + " missionTime=" + getMissionTime());
	sdSetPassiveSliceAlliances();
	sdPrepareHumanAssets();
	sdFocusFieldTeam();
	sdPrepareRelays();
	sdRestoreMusicState();
	console(_("Signal Diaspora loaded. Move a repair or construction unit to Relay 1."));
	sdMarkRelayObjective(1);
	removeTimer("sdCheckRelayRepairs");
	setTimer("sdCheckRelayRepairs", SD_REPAIR_CHECK_MS);
}

function eventGameLoaded()
{
	sdSetPassiveSliceAlliances();
	sdRestoreMusicState();
	removeTimer("sdCheckRelayRepairs");
	if (!sdMissionEnding)
	{
		sdMarkRelayObjective(sdRelayState + 1);
		setTimer("sdCheckRelayRepairs", SD_REPAIR_CHECK_MS);
	}
}

function eventMissionTimeout()
{
	sdDebug("mission timeout missionTime=" + getMissionTime());
	sdFailMission("Signal window expired.");
}

function eventGameInit()
{
}

function eventStructureBuilt(struct, droid)
{
}

function eventObjectTransfer(obj, from)
{
}

function eventResearched(research, structure, player)
{
}

function eventAttacked(victim, attacker)
{
}

function eventDestroyed(obj)
{
}

function eventSelectionChanged(selected)
{
}

function eventAllianceAccepted(from, to)
{
}

function eventChat(from, to, message)
{
	if (message === "sd1")
	{
		sdRestoreRelay(1);
	}
	else if (message === "sd2")
	{
		sdRestoreRelay(2);
	}
	else if (message === "sd3")
	{
		sdRestoreRelay(3);
	}
	else if (message === "sdthreat")
	{
		sdThreatOn();
	}
	else if (message === "sdclear")
	{
		sdThreatOff();
	}
}
