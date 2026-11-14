# Alarm clock

> New and not yet tried on a real screen. It lights the screen and rings when an alarm goes off, also at night.

Two alarms on a Tessera screen, set on the screen itself: no phone and no Home Assistant needed to change a wake-up
time. Each alarm has a time, the days it rings on, and an on/off key. When one goes off, the screen lights up and shows
a popup with **Snooze** and **Stop**. A screen with a speaker rings too, and a speaker of Home Assistant can play a
sound.

Every alarm is also an entity of the screen in Home Assistant, so an automation can read an alarm, set it, and act
when it rings.

## Set up

1. In Tessera, open Plugins, choose **Add with a link** and paste
   `https://github.com/woldhek/tessera-plugin-alarm-clock`.
2. Tick the screen and apply. **Media player** is optional: choose a speaker of Home Assistant in the same room, or
   leave it empty. The screen builds once.
3. In Layout, place the **Alarm clock** tile from the library's Plugins group, at any size from 1x1 to 2x2.
4. On the screen, tap the tile. Choose **Alarm 1** or **Alarm 2**, set the time with **-** and **+**, tap the days,
   and tap the bottom key to turn it **On**.

## On the screen

- **The tile** shows when the next alarm goes off: the day and the time. A tap opens the card.
- **The card** has four rows: which alarm, its time (hours and minutes each with a - and a +; the minutes go in
  fives), its days from Monday to Sunday, and On or Off. Every tap is kept at once: there is nothing to save.
- **No days chosen**: the alarm rings once, at its next time, and turns itself off.
- **When an alarm goes off** the screen lights up, also from standby and at night, and shows the popup. **Stop** ends
  it. **Snooze** makes it ring again a few minutes later; the popup's close key and a tap beside the popup snooze too.
  While it is snoozed, the tile says so, and the card's bottom key stops it.
- **Nobody there**: an alarm stops by itself after ten minutes (a setting), and an alarm that is snoozed again and
  again gives up an hour after it first went off.

## Settings

On the screen: hold the top bar, Settings, Plugins, Alarm clock. In Tessera: the screen's Plugins tab, the plugin's
details, **Settings**. The same values are entities of the screen in Home Assistant.

- **Snooze**: show the Snooze key when an alarm goes off (on by default). Off: the popup has Stop alone, and it comes
  back until someone taps Stop.
- **Snooze time**: 1 to 30 minutes, 5 by default.
- **Stops by itself after**: 1 to 60 minutes, 10 by default.
- **Alarm sound**: ring on the screen's own speaker. Only on a screen with a speaker (below).
- **Media to play** and **Media volume**: what the media player plays and how loud (below).
- **Test the alarm**: makes the screen ring now.

## Sound

- **The screen's own speaker.** On a screen with a speaker, from its board (the Guition P4 panels, for example) or
  from another plugin, the part **Ring on the screen's speaker** is on by default: four short beeps a second, at the
  screen's volume, until the alarm stops. A screen without a speaker shows the popup without a sound. On the Guition 4-inch, the plugin
  [Guition speaker](https://github.com/woldhek/tessera-plugin-guition-speaker) makes its speaker output the screen's
  speaker. To change the
  sound, replace `components/alarm_clock/sounds/alarm.wav` in a copy of this plugin with a WAV file of 16-bit samples.
- **A media player of Home Assistant.** Choose it when you add the plugin (**Media player**), and put the alarm's
  sound in Home Assistant once:
  1. Download [`media/tessera-alarm-clock.mp3`](media/tessera-alarm-clock.mp3) from this repository (one cycle of the
     alarm's own beeps, a few kB).
  2. In Home Assistant, open **Media**, **My media**, and upload it there, keeping its name.

  When an alarm goes off the speaker plays it from Home Assistant, in your own network: nothing comes from the
  internet. Every media player can play a file once, few can repeat one, so the screen asks the speaker to play it
  again every three seconds while the alarm rings: it works the same on any speaker Home Assistant can play on. To play something else, fill in **Media to
  play**: another file under My media (`media-source://media_source/local/wake.mp3`), an address, or a radio stream.
  The screen calls `media_player.play_media` on that speaker, after `media_player.volume_set` when **Media volume** is
  above 0, and `media_player.media_stop` on Stop and Snooze. Your own file plays once, so take one that lasts. This
  needs Home Assistant to be reachable when the alarm goes off.
- **Nothing on the media player?** Press **Ring alarm** in Home Assistant and read the screen's log (ESPHome, Logs):
  `Asked media_player... to play ...` means the screen sent it, and the problem is between Home Assistant and the
  speaker (Home Assistant's own log says why; a file missing from My media is the most likely). `Not sent` means the screen could not reach Home Assistant. The screen
  must also be allowed to perform Home Assistant actions (Settings, Devices & services, ESPHome, the screen,
  Configure), as every control of Tessera needs.

## In Home Assistant

The screen's device gets these entities (`<screen>` is the screen's name):

| Entity | What |
|---|---|
| `time.<screen>_alarm_1_time`, `time.<screen>_alarm_2_time` | The alarm's time. Set it with the action `time.set_value`. |
| `switch.<screen>_alarm_1`, `switch.<screen>_alarm_2` | The alarm on or off. |
| `text.<screen>_alarm_1_days`, `text.<screen>_alarm_2_days` | Its days: any of `mon,tue,wed,thu,fri,sat,sun` with commas between, or `daily`, `weekdays`, `weekend`. Empty: it rings once. |
| `binary_sensor.<screen>_alarm_ringing` | On while an alarm rings, off while it is snoozed. |
| `sensor.<screen>_next_alarm` | When the next alarm goes off, as a timestamp. |
| `button.<screen>_stop_alarm`, `button.<screen>_snooze_alarm` | Stop or snooze the alarm that rings. |
| `button.<screen>_ring_alarm` | Make the screen ring now. |

Another integration sets the wake-up time:

```yaml
action:
  - action: time.set_value
    target: { entity_id: time.bedroom_alarm_1_time }
    data: { time: "06:45:00" }
  - action: switch.turn_on
    target: { entity_id: switch.bedroom_alarm_1 }
```

A notification, lights or anything else when the alarm rings:

```yaml
trigger:
  - trigger: state
    entity_id: binary_sensor.bedroom_alarm_ringing
    to: "on"
action:
  - action: notify.mobile_app_my_phone
    data: { message: "The alarm in the bedroom rings." }
```

## Good to know

- The alarms are kept on the screen and ring without Home Assistant, as long as the screen has its clock. Home
  Assistant sets that clock after a start: a screen that starts while Home Assistant is away rings nothing until it
  is back.
- An alarm rings in the minute of its time. A screen that is restarting in exactly that minute misses it.
- Each screen has its own two alarms.
- A plugin cannot lock the card: anyone at the screen can change an alarm.
- Checked with the plugins repository's `check.py` and a compile against the headers of Tessera's release and its dev
  branch, and drawn on a small test rig, not on Tessera's preview bench. Not yet run on a real screen.
