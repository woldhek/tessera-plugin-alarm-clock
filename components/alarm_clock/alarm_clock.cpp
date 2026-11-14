#include "alarm_clock.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>

#include "esphome/core/log.h"

namespace esphome::alarm_clock {

static const char *const TAG = "alarm_clock";

using tessera::Font;
namespace ui = tessera::ui;

static const char *const DAY_NAMES[7] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};
static const char *const DAY_KEYS[7] = {"day_mon", "day_tue", "day_wed", "day_thu", "day_fri", "day_sat", "day_sun"};
// Icons of Tessera's set: the alarm clock, and the bell that rings.
static constexpr uint32_t ICON_ALARM = 0xF0020, ICON_RING = 0xF009E;
// What the media player plays when "Media to play" is empty: the screen's own beeps, ten minutes long (media/alarm.mp3
// of this repository, served by jsDelivr with the type audio/mpeg that every speaker plays).
static const char *const DEFAULT_MEDIA = "https://cdn.jsdelivr.net/gh/woldhek/tessera-plugin-alarm-clock@main/media/alarm.mp3";
static constexpr uint32_t DEFAULT_MEDIA_MS = 595000;   // started again just before its ten minutes end
// An alarm that nobody stops is snoozed again and again for at most this long after it first went off.
static constexpr uint32_t GIVE_UP_S = 3600;

// ---- Days and moments ----

// The days in a text of Home Assistant or of the glass: "mon,wed,fri", or daily, weekdays, weekend.
static uint8_t parse_days(const std::string &text) {
  std::string s = text;
  for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (s.find("daily") != std::string::npos) return EVERY_DAY;
  if (s.find("weekdays") != std::string::npos) return 31;
  if (s.find("weekend") != std::string::npos) return 96;
  uint8_t days = 0;
  for (int i = 0; i < 7; ++i)
    if (s.find(DAY_NAMES[i]) != std::string::npos) days |= static_cast<uint8_t>(1 << i);
  return days;
}

static std::string days_text(uint8_t days) {
  std::string s;
  for (int i = 0; i < 7; ++i) {
    if (!(days & (1 << i))) continue;
    if (!s.empty()) s += ',';
    s += DAY_NAMES[i];
  }
  return s;
}

// Monday 0 to Sunday 6, from the screen's 1 Sunday to 7 Saturday.
static int day_index(const tessera::LocalTime &t) { return (t.weekday + 5) % 7; }

// The moment of `hour`:`minute` on the screen's own clock, `days` after today. A day with a change of summer time is
// an hour longer or shorter: the second look puts the moment right.
static uint32_t moment(uint32_t now, int days, int hour, int minute) {
  const tessera::LocalTime t = tessera::local_time(now);
  int64_t at = static_cast<int64_t>(now) - (t.hour * 3600 + t.minute * 60 + t.second) + static_cast<int64_t>(days) * 86400 +
               hour * 3600 + minute * 60;
  const tessera::LocalTime c = tessera::local_time(static_cast<uint32_t>(at));
  int off = (hour * 60 + minute) - (c.hour * 60 + c.minute);
  if (off > 720) off -= 1440;
  if (off < -720) off += 1440;
  return static_cast<uint32_t>(at + off * 60);
}

// A moment as Home Assistant reads a timestamp: "2026-11-16T06:00:00+00:00" (Howard Hinnant's civil_from_days).
static std::string iso_utc(uint32_t epoch) {
  const long z = static_cast<long>(epoch / 86400) + 719468;
  const long era = z / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp < 10 ? mp + 3 : mp - 9;
  const long y = static_cast<long>(yoe) + era * 400 + (m <= 2 ? 1 : 0);
  const unsigned s = epoch % 86400;
  char text[32];
  snprintf(text, sizeof text, "%04ld-%02u-%02uT%02u:%02u:%02u+00:00", y, m, d, s / 3600, s / 60 % 60, s % 60);
  return text;
}

// ---- Drawing helpers ----

// A place a finger can tap, in the pixels of its card.
struct Box {
  int x = 0, y = 0, w = 0, h = 0;
  bool has(const tessera::Point &p) const { return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h; }
};

// A key: a block with its words in the middle.
struct Key {
  lv_obj_t *block{}, *label{};
  Box box;
  void make(lv_obj_t *parent, const Box &at, Font font) {
    box = at;
    block = ui::block(parent, theme::BUTTON);
    lv_obj_set_pos(block, at.x, at.y);
    lv_obj_set_size(block, at.w, at.h);
    label = ui::label(block, font, theme::INK);
    lv_obj_set_width(label, at.w);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
  }
  // Chosen: the screen's blue with its own ink; else a plain key, its words soft when `soft`.
  void paint(bool chosen, bool soft = false) {
    ui::set_fill(block, tessera::Tone::NORMAL, chosen ? theme::ACCENT : theme::BUTTON);
    ui::set_color(label, chosen ? theme::ON_ACCENT : soft ? theme::MUTED : theme::INK);
  }
};

// The largest of `fonts` whose line is at most `height` and in which `text` is at most `width`; the last when none is.
static Font fitting(const std::string &text, int width, int height, std::initializer_list<Font> fonts) {
  Font face = Font::BODY;
  for (Font f : fonts) {
    face = f;
    if (ui::line_height(f) <= height && ui::text_width(text, f) <= width) break;
  }
  return face;
}

// ---- The tile: the next alarm ----

class AlarmTile : public tessera::Tile {
 public:
  explicit AlarmTile(AlarmClock *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    width_ = c.width;
    height_ = c.height;
    tile_ = c.tile;
    icon_h_ = ui::line_height(Font::ICON);
    icon_ = ui::label(c.parent, Font::ICON, theme::ACCENT);
    lv_obj_set_pos(icon_, 0, 0);
    lv_obj_set_width(icon_, icon_h_);
    ui::set_text(icon_, ui::icon(ICON_ALARM));
    const int x = icon_h_ + ui::px(8);
    when_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_pos(when_, x, (icon_h_ - ui::line_height(Font::BODY)) / 2);
    lv_obj_set_width(when_, std::max(1, width_ - x));
    time_ = ui::label(c.parent, Font::VALUE, theme::INK);
    lv_obj_set_width(time_, width_);
  }

  void on_tick(uint32_t epoch) override {
    std::string when, time;
    tessera::Tone tone = tessera::Tone::ACCENT;
    bool quiet = false;
    if (plugin_->ringing()) {
      when = plugin_->text("ringing");
      time = epoch ? tessera::clock_text(epoch) : "";
      tone = tessera::Tone::ALERT;
    } else if (plugin_->snoozed()) {
      when = plugin_->text("snoozed");
      time = tessera::clock_text(plugin_->snooze_until());
      tone = tessera::Tone::BUSY;
    } else if (const uint32_t at = plugin_->next_alarm(epoch)) {
      const int32_t days = tessera::days_from_today(at);
      when = days == 0 ? plugin_->text("today") : days == 1 ? tessera::days_text(1) : tessera::date_text(at);
      time = tessera::clock_text(at);
    } else if (epoch) {
      time = plugin_->text("no_alarm");
      tone = tessera::Tone::NORMAL;
      quiet = true;
    }
    ui::set_tone(icon_, tone, theme::MUTED);
    ui::set_text(when_, when);
    // The time in the largest font that fits under the icon.
    const Font face = fitting(time, width_, height_ - icon_h_, {Font::VALUE, Font::HEADLINE, Font::TITLE, Font::BODY});
    ui::set_font(time_, face);
    ui::set_color(time_, quiet ? theme::MUTED : theme::INK);
    ui::set_text(time_, time);
    lv_obj_set_pos(time_, 0, height_ - ui::line_height(face));
  }

  void on_theme() override { ui::set_color(when_, theme::MUTED); }
  void on_tap() override { tessera::open_card(plugin_->plugin_id(), "alarm", "", tile_); }

 private:
  AlarmClock *plugin_;
  int width_ = 0, height_ = 0, tile_ = -1, icon_h_ = 0;
  lv_obj_t *icon_{}, *when_{}, *time_{};
};

// ---- The card: both alarms, set with keys ----
// Four rows: which alarm, its time with a - and a + for the hours and for the minutes, its days, and on or off. Every
// tap writes the alarm's entity at once: there is nothing to save.
class AlarmCard : public tessera::Card {
 public:
  explicit AlarmCard(AlarmClock *plugin) : plugin_(plugin) {}

  void open(const tessera::CardContext &c) override {
    const int w = c.width, gap = ui::px(10), small = ui::px(6);
    const int row = std::max(1, std::min(ui::mm(12), (c.height - 3 * gap) / 4));
    int y = std::max(0, (c.height - (4 * row + 3 * gap)) / 2);

    // Which alarm.
    const int tab = (w - gap) / ALARMS;
    for (int i = 0; i < ALARMS; ++i) {
      tabs_[i].make(c.parent, {i * (tab + gap), y, tab, row}, Font::TITLE);
      ui::set_text(tabs_[i].label, tessera::format(plugin_->text("alarm_n"), i + 1));
    }
    y += row + gap;

    // Its time: - 07 +  :  - 00 +. The digits take the largest font the row holds.
    digits_ = Font::HEADLINE;
    for (Font f : {Font::DIGITS, Font::VALUE, Font::HEADLINE}) {
      digits_ = f;
      if (ui::line_height(f) <= row && ui::covers("0123456789", f)) break;
    }
    const Font between = fitting(":", w, row, {Font::VALUE, Font::HEADLINE, Font::TITLE});
    const int digit_w = ui::text_width("88", digits_) + ui::px(10);
    const int colon_w = std::max(ui::text_width(":", between), ui::text_width("PM", Font::BODY_LARGE)) + ui::px(8);
    const int key = std::max(1, std::min(ui::mm(13), (w - 2 * digit_w - colon_w - 6 * small) / 4));
    int x = std::max(0, (w - (4 * key + 2 * digit_w + colon_w + 6 * small)) / 2);
    const char *minus = ui::covers("−", Font::HEADLINE) ? "−" : "-";
    auto step_key = [&](Key &k, const char *sign) {
      k.make(c.parent, {x, y, key, row}, Font::HEADLINE);
      ui::set_text(k.label, sign);
      x += key + small;
    };
    auto shown = [&](lv_obj_t *&label, Font font, int width) {
      label = ui::label(c.parent, font, theme::INK);
      lv_obj_set_pos(label, x, y + (row - ui::line_height(font)) / 2);
      lv_obj_set_width(label, width);
      lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
      x += width + small;
    };
    step_key(steps_[0], minus);
    shown(hour_, digits_, digit_w);
    step_key(steps_[1], "+");
    const int colon_x = x;
    shown(colon_, between, colon_w);
    half_ = ui::label(c.parent, Font::BODY_LARGE, theme::MUTED);   // AM or PM, on a screen that shows 12 hours
    lv_obj_set_pos(half_, colon_x, y + (row - ui::line_height(Font::BODY_LARGE)) / 2);
    lv_obj_set_width(half_, colon_w);
    lv_obj_set_style_text_align(half_, LV_TEXT_ALIGN_CENTER, 0);
    step_key(steps_[2], minus);
    shown(minute_, digits_, digit_w);
    step_key(steps_[3], "+");
    y += row + gap;

    // Its days, Monday first.
    const int day = (w - 6 * small) / 7;
    for (int i = 0; i < 7; ++i) {
      days_[i].make(c.parent, {i * (day + small), y, day, row}, Font::BODY_LARGE);
      ui::set_text(days_[i].label, plugin_->text(DAY_KEYS[i]));
    }
    y += row + gap;

    // On or off; while an alarm is snoozed, the key that stops it.
    power_.make(c.parent, {0, y, w, row}, Font::BODY_LARGE);
    paint();
  }

  void on_tick(uint32_t) override { paint(); }
  void on_theme() override { paint(); }

  void on_tap() override {
    const tessera::Point p = tessera::tap_point();
    if (p.x < 0) return;
    const int a = chosen_;
    for (int i = 0; i < ALARMS; ++i)
      if (tabs_[i].box.has(p)) chosen_ = i;
    int h = plugin_->hour(a), m = plugin_->minute(a);
    if (steps_[0].box.has(p)) plugin_->set_time(a, (h + 23) % 24, m);
    else if (steps_[1].box.has(p)) plugin_->set_time(a, (h + 1) % 24, m);
    // The minutes go in fives: 07:03 goes down to 07:00 and up to 07:05.
    else if (steps_[2].box.has(p)) plugin_->set_time(a, h, m % 5 ? m - m % 5 : (m + 55) % 60);
    else if (steps_[3].box.has(p)) plugin_->set_time(a, h, (m - m % 5 + 5) % 60);
    for (int i = 0; i < 7; ++i)
      if (days_[i].box.has(p)) plugin_->set_days(a, plugin_->days(a) ^ static_cast<uint8_t>(1 << i));
    if (power_.box.has(p)) {
      if (plugin_->snoozed()) plugin_->stop();
      else plugin_->set_enabled(a, !plugin_->enabled(a));
    }
    paint();
  }

 private:
  void paint() {
    const int a = chosen_;
    for (int i = 0; i < ALARMS; ++i) tabs_[i].paint(i == a, !plugin_->enabled(i));
    const int h = plugin_->hour(a), m = plugin_->minute(a);
    // A screen that shows 12 hours writes 13:00 without a 13.
    const std::string sample = plugin_->time_text(13, 0);
    const bool twelve = sample.find("13") == std::string::npos;
    char text[8];
    if (twelve) snprintf(text, sizeof text, "%d", h % 12 ? h % 12 : 12);
    else snprintf(text, sizeof text, "%02d", h);
    ui::set_text(hour_, text);
    snprintf(text, sizeof text, "%02d", m);
    ui::set_text(minute_, text);
    ui::set_text(colon_, twelve ? "" : ":");
    ui::set_text(half_, twelve ? (h < 12 ? "AM" : "PM") : "");
    ui::set_color(hour_, theme::INK);
    ui::set_color(minute_, theme::INK);
    ui::set_color(colon_, theme::INK);
    ui::set_color(half_, theme::MUTED);
    for (auto &k : steps_) k.paint(false);
    const uint8_t days = plugin_->days(a);
    for (int i = 0; i < 7; ++i) days_[i].paint(days & (1 << i));
    if (plugin_->snoozed()) {
      power_.paint(true);
      ui::set_text(power_.label, tessera::fill(plugin_->text("stop_snooze"), "time",
                                               tessera::clock_text(plugin_->snooze_until())));
    } else {
      const bool on = plugin_->enabled(a);
      power_.paint(on);
      ui::set_text(power_.label, plugin_->text(!on ? "off" : days ? "on" : "on_once"));
    }
  }

  AlarmClock *plugin_;
  int chosen_ = 0;
  Font digits_ = Font::HEADLINE;
  Key tabs_[ALARMS], steps_[4], days_[7], power_;
  lv_obj_t *hour_{}, *minute_{}, *colon_{}, *half_{};
};

// ---- The popup: an alarm rings ----
// A bell that pulses beside the word, the time under it, and the keys Snooze and Stop (Stop alone while snoozing is
// turned off in the settings).
class RingPopup : public tessera::Popup {
 public:
  explicit RingPopup(AlarmClock *plugin) : plugin_(plugin) {}

  void open(const tessera::PopupContext &c) override {
    const int w = c.width, gap = ui::px(12);
    const int disc = c.key > 0 ? c.key : ui::px(54);
    disc_ = ui::block(c.parent, theme::ACCENT);
    lv_obj_set_pos(disc_, 0, 0);
    lv_obj_set_size(disc_, disc, disc);
    lv_obj_set_style_radius(disc_, LV_RADIUS_CIRCLE, 0);
    bell_ = ui::label(disc_, Font::ICON_SMALL, theme::ON_ACCENT);
    lv_obj_set_width(bell_, disc);
    lv_obj_set_style_text_align(bell_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(bell_, LV_ALIGN_CENTER, 0, 0);
    ui::set_text(bell_, ui::icon(ICON_RING));
    ui::animate(disc_, tessera::Motion::PULSE, 1200);

    const int word_x = disc + gap;
    const int word_w = std::max(1, w - word_x - c.corner);
    const std::string word = plugin_->text("ringing");
    const Font face = fitting(word, word_w, disc, {Font::HEADLINE, Font::TITLE, Font::BODY_LARGE, Font::BODY});
    word_ = ui::label(c.parent, face, theme::INK);
    lv_obj_set_pos(word_, word_x, (disc - ui::line_height(face)) / 2);
    lv_obj_set_width(word_, word_w);
    ui::set_text(word_, word);

    // The keys are a finger wide at least; the time takes the largest font that leaves them room.
    const int key_h = std::max(ui::mm(8), std::min(ui::mm(12), c.height / 4));
    const int room = c.height - disc - key_h - 2 * gap;
    const Font big = fitting("88:88 PM", w, room, {Font::VALUE, Font::HEADLINE, Font::TITLE});
    time_ = ui::label(c.parent, big, theme::INK);
    lv_obj_set_pos(time_, 0, disc + gap);
    lv_obj_set_width(time_, w);
    lv_obj_set_style_text_align(time_, LV_TEXT_ALIGN_CENTER, 0);
    const int y = disc + gap + ui::line_height(big) + gap;
    if (plugin_->snooze_on()) {
      const int half = (w - gap) / 2;
      snooze_.make(c.parent, {0, y, half, key_h}, Font::BODY_LARGE);
      ui::set_text(snooze_.label, plugin_->text("snooze"));
      stop_.make(c.parent, {half + gap, y, w - half - gap, key_h}, Font::BODY_LARGE);
    } else {
      stop_.make(c.parent, {0, y, w, key_h}, Font::BODY_LARGE);
    }
    ui::set_text(stop_.label, plugin_->text("stop"));
    on_theme();
    on_tick(tessera::epoch());
  }

  void on_tick(uint32_t epoch) override { ui::set_text(time_, epoch ? tessera::clock_text(epoch) : ""); }

  void on_theme() override {
    ui::set_color(word_, theme::INK);
    ui::set_color(time_, theme::INK);
    if (snooze_.block != nullptr) snooze_.paint(false);
    stop_.paint(true);
  }

  void on_tap() override {
    const tessera::Point p = tessera::tap_point();
    if (stop_.box.has(p)) plugin_->stop();
    else if (snooze_.block != nullptr && snooze_.box.has(p)) plugin_->snooze();
  }

  void on_dismiss() override { plugin_->dismissed(); }

 private:
  AlarmClock *plugin_;
  lv_obj_t *disc_{}, *bell_{}, *word_{}, *time_{};
  Key snooze_, stop_;
};

// ---- The plugin ----

void AlarmClock::setup() {
  add_tile("alarm", [this]() { return new AlarmTile(this); });
  add_card("alarm", [this]() { return new AlarmCard(this); });
}

uint8_t AlarmClock::days(int alarm) const { return parse_days(days_[alarm]->state); }

void AlarmClock::set_time(int alarm, int hour, int minute) {
  auto call = time_[alarm]->make_call();
  call.set_time(static_cast<uint8_t>(hour), static_cast<uint8_t>(minute), 0);
  call.perform();
}

void AlarmClock::set_enabled(int alarm, bool on) {
  if (on) enabled_[alarm]->turn_on();
  else enabled_[alarm]->turn_off();
  // An alarm turned off no longer comes back from its snooze.
  if (!on && snoozed() && ring_alarm_ == alarm) stop();
}

void AlarmClock::set_days(int alarm, uint8_t days) {
  auto call = days_[alarm]->make_call();
  call.set_value(days_text(days & EVERY_DAY));
  call.perform();
}

int AlarmClock::minutes_(number::Number *n, int fallback) const {
  if (n == nullptr || std::isnan(n->state)) return fallback;
  return std::max(1, static_cast<int>(n->state));
}

std::string AlarmClock::time_text(int hour, int minute) const {
  const uint32_t now = tessera::epoch();
  if (now) return tessera::clock_text(moment(now, 0, hour, minute));
  char text[8];
  snprintf(text, sizeof text, "%02d:%02d", hour, minute);
  return text;
}

uint32_t AlarmClock::next_alarm(uint32_t now) const {
  if (!now) return 0;
  uint32_t next = snoozed() ? snooze_until_ : 0;
  const tessera::LocalTime t = tessera::local_time(now);
  const int today = day_index(t), passed = t.hour * 60 + t.minute;
  for (int a = 0; a < ALARMS; ++a) {
    if (!enabled(a)) continue;
    const uint8_t on = days(a);
    const int at = hour(a) * 60 + minute(a);
    for (int d = 0; d <= 7; ++d) {
      if (d == 0 && at <= passed) continue;                       // today's has gone, or rings this minute
      if (on && !(on & (1 << ((today + d) % 7)))) continue;
      const uint32_t when = moment(now, d, hour(a), minute(a));
      if (!next || when < next) next = when;
      break;
    }
  }
  return next;
}

void AlarmClock::on_interval(uint32_t now_ms) {
  ms_ = now_ms;
  const uint32_t now = tessera::epoch();
  if (ringing_) {
    if (now_ms - rang_ms_ >= static_cast<uint32_t>(minutes_(ring_minutes_, 10)) * 60000u) {
      stop();   // nobody came: it stops by itself
    } else {
      // The popup is the way to stop it: it comes back when something took it away (an alert, a screen put out).
      if (!tessera::popup_shown(this)) show_();
      // The built-in sound on the media player lasts ten minutes: again, for an alarm that rings longer.
      if (media_started_ && media_default_ && now_ms - media_ms_ >= DEFAULT_MEDIA_MS) start_media_();
      // And the screen stays lit while it rings.
      if (now_ms - woke_ms_ >= 5000) {
        woke_ms_ = now_ms;
        tessera::wake(this);
      }
    }
  } else if (snooze_until_ && now >= snooze_until_) {
    if (now - first_rang_ > GIVE_UP_S) stop();
    else ring_(ring_alarm_, now, false);
  }
#ifdef USE_SPEAKER
  const bool sounding = ringing_ && (sound_on_ == nullptr || sound_on_->state);
  if (sounding && !sounds_.playing()) sounds_.play(sound_);        // the sound ends in a pause: again and again
  else if (!sounding && sounds_.playing()) sounds_.stop();
#endif
  if (!now) return;
  // Once in every minute of the clock: is this an alarm's minute?
  const uint32_t minute = now / 60;
  if (minute != checked_minute_) {
    checked_minute_ = minute;
    const tessera::LocalTime t = tessera::local_time(now);
    for (int a = 0; a < ALARMS; ++a) {
      if (!enabled(a) || hour(a) != t.hour || this->minute(a) != t.minute) continue;
      const uint8_t on = days(a);
      if (on && !(on & (1 << day_index(t)))) continue;
      if (!on) enabled_[a]->turn_off();                             // an alarm without days rings once
      if (!ringing_) ring_(a, now, true);
      break;
    }
  }
  // Tell Home Assistant when the next one goes off, when that changed.
  if (now != looked_ && next_sensor_ != nullptr) {
    looked_ = now;
    const uint32_t next = next_alarm(now);
    if (next != next_told_) {
      next_told_ = next;
      next_sensor_->publish_state(next ? iso_utc(next) : std::string());
    }
  }
  if (ringing_sensor_ != nullptr && !published_) {
    published_ = true;
    ringing_sensor_->publish_state(ringing_);
  }
}

void AlarmClock::ring_(int alarm, uint32_t now, bool first) {
  ringing_ = true;
  ring_alarm_ = alarm;
  rang_ms_ = ms_;
  woke_ms_ = ms_;
  snooze_until_ = 0;
  if (first) first_rang_ = now;
  if (ringing_sensor_ != nullptr) ringing_sensor_->publish_state(true);
  published_ = true;
  start_media_();
  show_();
  tessera::refresh();
}

void AlarmClock::ring_now() {
  if (!ringing_) ring_(-1, tessera::epoch(), true);
}

void AlarmClock::show_() {
  tessera::show_popup(this, [this]() -> tessera::Popup * { return new RingPopup(this); });
}

// The alarm falls silent: its sensor, its sound, the media player.
void AlarmClock::quiet_() {
  ringing_ = false;
  if (ringing_sensor_ != nullptr) ringing_sensor_->publish_state(false);
#ifdef USE_SPEAKER
  sounds_.stop();
#endif
  stop_media_();
  tessera::refresh();
}

void AlarmClock::stop() {
  if (!ringing_ && !snooze_until_) return;
  snooze_until_ = 0;
  quiet_();
  if (tessera::popup_shown(this)) tessera::close_popup(this);
}

void AlarmClock::snooze_(bool close) {
  if (!ringing_) return;
  const uint32_t now = tessera::epoch();
  // Without a clock there is no later: it stops.
  snooze_until_ = now ? now + static_cast<uint32_t>(minutes_(snooze_minutes_, 5)) * 60u : 0;
  quiet_();
  if (close && tessera::popup_shown(this)) tessera::close_popup(this);
}

void AlarmClock::dismissed() {
  // The popup's close key or a tap beside it snoozes; with snoozing turned off the popup comes back until Stop.
  if (ringing_ && snooze_on()) snooze_(false);
}

void AlarmClock::start_media_() {
  if (media_player_[0] == 0) return;
  const std::string media = media_ == nullptr || media_->state.empty() ? std::string(DEFAULT_MEDIA) : media_->state;
  media_default_ = media == DEFAULT_MEDIA;
  media_ms_ = ms_;
  const int volume = media_volume_ == nullptr || std::isnan(media_volume_->state) ? 0 : static_cast<int>(media_volume_->state);
  if (volume > 0) {
    JsonDocument level;
    level["volume_level"] = volume / 100.0;
    if (!tessera::action("media_player.volume_set", media_player_, level.as<JsonVariantConst>()))
      ESP_LOGW(TAG, "Not sent: media_player.volume_set to %s", media_player_);
  }
  JsonDocument play;
  play["media_content_id"] = media;
  play["media_content_type"] = "music";
  media_started_ = tessera::action("media_player.play_media", media_player_, play.as<JsonVariantConst>());
  if (media_started_) ESP_LOGI(TAG, "Asked %s to play %s", media_player_, media.c_str());
  else ESP_LOGW(TAG, "Not sent: media_player.play_media to %s (is Home Assistant connected?)", media_player_);
}

void AlarmClock::stop_media_() {
  if (!media_started_) return;
  media_started_ = false;
  tessera::action("media_player.media_stop", media_player_);
}

bool AlarmClock::settings(tessera::SettingsPage &page) {
  page.icon = "\U000F0020";
  page.card(text("set_open"), "\U000F0020", "alarm");
  page.toggle(text("set_snooze"), [this] { return snooze_on(); },
              [this](bool on) { on ? snooze_switch_->turn_on() : snooze_switch_->turn_off(); });
  page.number(text("set_snooze_minutes"), 1, 30, 1, text("unit_min"), [this] { return minutes_(snooze_minutes_, 5); },
              [this](int v) { auto call = snooze_minutes_->make_call(); call.set_value(v); call.perform(); });
  page.number(text("set_ring_minutes"), 1, 60, 1, text("unit_min"), [this] { return minutes_(ring_minutes_, 10); },
              [this](int v) { auto call = ring_minutes_->make_call(); call.set_value(v); call.perform(); });
#ifdef USE_SPEAKER
  if (sound_on_ != nullptr)
    page.toggle(text("set_sound"), [this] { return sound_on_->state; },
                [this](bool on) { on ? sound_on_->turn_on() : sound_on_->turn_off(); });
#endif
  page.action(text("set_test"), "\U000F009E", [this] { ringing_ ? stop() : ring_now(); }, nullptr,
              [this]() -> std::string { return ringing_ ? text("testing") : ""; })
      .active([this] { return ringing_; });
  return true;
}

}  // namespace esphome::alarm_clock
