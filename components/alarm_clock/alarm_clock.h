#pragma once
#include <cstdint>
#include <string>

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/datetime/time_entity.h"
#include "esphome/components/number/number.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text/text.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/smart_display/plugin_api.h"
#include "esphome/components/smart_display/plugin_sound.h"   // empty on a screen without a speaker

namespace esphome::alarm_clock {

constexpr int ALARMS = 2;
// The days of an alarm as bits: 1 Monday, 2 Tuesday, ... 64 Sunday. 0: it rings once and turns itself off.
constexpr uint8_t EVERY_DAY = 127;

// The plugin: it holds no alarm itself. Each alarm is three ESPHome entities of plugin.yaml (a time, a switch, a text of
// days), kept on the screen and shown in Home Assistant, so the card on the glass and an automation set the same thing.
// The plugin watches the screen's clock, rings, and draws the tile, the card and the popup.
class AlarmClock : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void on_interval(uint32_t now_ms) override;
  bool settings(tessera::SettingsPage &page) override;
  void before_update() override { stop(); }
#ifdef USE_SPEAKER
  // The part parts/sound.yaml, on a screen with a speaker: an alarm rings on it.
  void loop() override { sounds_.loop(); }
  void set_speaker(speaker::Speaker *s) { sounds_.set_speaker(s); }
  void set_sound(tessera::Sound s) { sound_ = s; }
  void set_sound_on(switch_::Switch *s) { sound_on_ = s; }
#endif

  void set_media_player(const char *entity) { media_player_ = entity; }
  void set_ringing(binary_sensor::BinarySensor *s) { ringing_sensor_ = s; }
  void set_next(text_sensor::TextSensor *s) { next_sensor_ = s; }
  void set_snooze(switch_::Switch *s) { snooze_switch_ = s; }
  void set_snooze_minutes(number::Number *n) { snooze_minutes_ = n; }
  void set_ring_minutes(number::Number *n) { ring_minutes_ = n; }
  void set_media(text::Text *t) { media_ = t; }
  void set_media_volume(number::Number *n) { media_volume_ = n; }
  void set_alarm(int index, datetime::TimeEntity *time, switch_::Switch *enabled, text::Text *days) {
    if (index < 0 || index >= ALARMS) return;
    time_[index] = time;
    enabled_[index] = enabled;
    days_[index] = days;
  }

  // An alarm as its entities say it is now, and a change from the glass.
  int hour(int alarm) const { return time_[alarm]->hour; }
  int minute(int alarm) const { return time_[alarm]->minute; }
  bool enabled(int alarm) const { return enabled_[alarm]->state; }
  uint8_t days(int alarm) const;
  void set_time(int alarm, int hour, int minute);
  void set_enabled(int alarm, bool on);
  void set_days(int alarm, uint8_t days);

  // What goes on now.
  bool ringing() const { return ringing_; }
  bool snoozed() const { return !ringing_ && snooze_until_ != 0; }
  uint32_t snooze_until() const { return snooze_until_; }
  bool snooze_on() const { return snooze_switch_ != nullptr && snooze_switch_->state; }
  // When the next alarm goes off (a snooze counts), 0 for none or while the clock is not set.
  uint32_t next_alarm(uint32_t now) const;
  // A time of day as the screen writes it ("07:00", "7:00 AM").
  std::string time_text(int hour, int minute) const;

  // Stop the alarm that rings or is snoozed; snooze the one that rings; ring now (a test, a key of Home Assistant).
  void stop();
  void snooze() { snooze_(true); }
  void ring_now();
  // The popup went by itself (its close key, a tap beside it, an alert).
  void dismissed();

 protected:
  void ring_(int alarm, uint32_t now, bool first);
  void snooze_(bool close);
  void show_();
  void quiet_();
  void start_media_();
  void stop_media_();
  int minutes_(number::Number *n, int fallback) const;

  datetime::TimeEntity *time_[ALARMS]{};
  switch_::Switch *enabled_[ALARMS]{};
  text::Text *days_[ALARMS]{};
  binary_sensor::BinarySensor *ringing_sensor_{};
  text_sensor::TextSensor *next_sensor_{};
  switch_::Switch *snooze_switch_{};
  number::Number *snooze_minutes_{}, *ring_minutes_{}, *media_volume_{};
  text::Text *media_{};
  const char *media_player_ = "";

  bool ringing_ = false, media_started_ = false, published_ = false;
  int ring_alarm_ = -1;          // the alarm that rings or is snoozed, -1 for a test
  uint32_t ms_ = 0;              // millis() of the last interval
  uint32_t rang_ms_ = 0;         // when this ring started
  uint32_t woke_ms_ = 0;         // when the screen was last kept awake for it
  uint32_t first_rang_ = 0;      // the screen's clock when the alarm first went off, across its snoozes
  uint32_t snooze_until_ = 0;    // the screen's clock when it rings again, 0 while not snoozed
  uint32_t checked_minute_ = 0;  // the minute of the clock the alarms were last looked at
  uint32_t looked_ = 0;          // the second the next alarm was last worked out
  uint32_t next_told_ = 1;       // the next alarm Home Assistant was told (1: nothing yet)
#ifdef USE_SPEAKER
  tessera::Sound sound_;
  tessera::SoundPlayer sounds_;
  switch_::Switch *sound_on_{};
#endif
};

}  // namespace esphome::alarm_clock
