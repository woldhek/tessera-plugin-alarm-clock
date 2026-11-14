# Changelog

## 0.3.0 - 2026-11-14
- The tile is a night clock: the time in a ring that fills in towards the alarm's sun, how long there is left to
  sleep, purple at night and amber in the last hour. It can take a whole page.
- The card sets the time inside a ring of the day, with amber day keys; the popup is a rising sun with a greeting.
- Needs Tessera's dev branch (plugin API 0.25) until its next release.

## 0.2.0 - 2026-11-14
- A new look: the time with arrows up and down, round day keys, a switch that says when the alarm rings, and both
  alarms on a tile of 2x1 or larger.
- Each alarm has its own sensor that is on while it rings, for automations; the popup names the alarm.
- The media player is gone from the plugin: play any sound on any speaker from an automation on the ringing sensor
  (see the README). Remove the input **Media player** and the file under My media if you set them up.

## 0.1.3 - 2026-11-14
- The media player's alarm is one cycle of beeps (a few kB, not ten minutes), played again every three seconds
  while the alarm rings, on any media player. Upload the new `media/tessera-alarm-clock.mp3` under My media.

## 0.1.2 - 2026-11-14
- The media player's alarm comes from Home Assistant's own media folder, not from the internet: upload
  `media/tessera-alarm-clock.mp3` under My media once.

## 0.1.1 - 2026-11-14
- A media player chosen for the screen now plays the alarm's own beeps when **Media to play** is empty; before, it
  played nothing. The screen's log says what it sent.

## 0.1.0 - 2026-11-14
- First version: two alarms set on the screen, each with a time, its days and an on/off key.
- A popup with Snooze and Stop when an alarm goes off; a sound on a screen with a speaker, and on a media player
  of Home Assistant when you choose one.
- Every alarm is an entity of the screen in Home Assistant, with a sensor for the next alarm and one that is on while
  it rings.
