# Changelog

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
