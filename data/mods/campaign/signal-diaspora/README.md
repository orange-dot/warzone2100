# Signal Diaspora

Signal Diaspora is an experimental alternate campaign mod built around relay restoration.

This initial version is a technical slice:

- the mod is separate from the base campaign
- the mission script turns existing map objects into damaged relay sites
- the player must repair three relays in sequence to finish the mission
- the mission uses a local 15 minute timer instead of vanilla campaign win/loss checks
- seed-map factions are passive/allied for V0 while the relay traversal loop is validated
- the `.gam` map is a temporary loadable seed copied from an existing GPL campaign asset
- final map, briefing, and original music assets are still TODO

The intended final mission loop is three relay restorations:

1. relay 1 starts damaged and must be repaired
2. relay 2 becomes the next repair objective
3. relay 3 completes the signal and ends the mission

The current V0 relays reuse existing map labels:

- `resRelay`
- `coaHQ`
- `royRelay`

The old chat commands remain as debug fallbacks only and are not the intended playable path:

- `sd1`
- `sd2`
- `sd3`
- `sdthreat`
- `sdclear`

Mission music is disabled for the playable V0. Music files are expected under `audio/music/` later.
