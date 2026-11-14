#include "alarm_clock.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>

namespace esphome::alarm_clock {

using tessera::Font;
namespace ui = tessera::ui;

static const char *const DAY_NAMES[7] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};
static const char *const DAY_KEYS[7] = {"day_mon", "day_tue", "day_wed", "day_thu", "day_fri", "day_sat", "day_sun"};
// Icons of Tessera's set: the alarm clock, the bell that rings, the arrows of the time, sleep for a snooze.
static constexpr uint32_t ICON_ALARM = 0xF0020, ICON_RING = 0xF009E, ICON_UP = 0xF0143, ICON_DOWN = 0xF0140,
                          ICON_SLEEP = 0xF04B2;
// An alarm that nobody stops is snoozed again and again for at most this long after it first went off.
static constexpr uint32_t GIVE_UP_S = 3600;

// ---- Days and moments ----

// The days in a text of Home Assistant or of the glass: "mon,wed,fri", or daily, weekdays, weekend.
static uint8_t parse_days(const std::string &text) {
  std::string s = text;
  for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (s.find("daily") != std::string::npos) return EVERY_DAY;
  if (s.find("weekdays") != std::string::npos) return WEEKDAYS;
  if (s.find("weekend") != std::string::npos) return WEEKEND;
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

static lv_obj_t *placed(lv_obj_t *obj, int x, int y, int w, int h) {
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  return obj;
}

// A label whose line is centred in a box of `w` by `h` at x, y.
static lv_obj_t *centred(lv_obj_t *parent, Font font, theme::Role role, int x, int y, int w, int h) {
  lv_obj_t *l = ui::label(parent, font, role);
  lv_obj_set_pos(l, x, y + (h - ui::line_height(font)) / 2);
  lv_obj_set_width(l, w);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  return l;
}

// A key: a block with its words or its icon in the middle. `round`: a pill or a disc.
struct Key {
  lv_obj_t *block{}, *label{};
  Box box;
  void make(lv_obj_t *parent, const Box &at, Font font, bool round = false) {
    box = at;
    block = placed(ui::block(parent, theme::BUTTON), at.x, at.y, at.w, at.h);
    if (round) lv_obj_set_style_radius(block, LV_RADIUS_CIRCLE, 0);
    label = centred(block, font, theme::INK, 0, 0, at.w, at.h);
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

// ---- The tile ----
// 1x1: the next alarm, with its day. Larger: both alarms side by side (a wide tile) or one above the other, each with
// its days. Every part is an icon and a line of words over a large time.

// One alarm, or the next of both, in a part of the tile.
struct Glance {
  lv_obj_t *icon{}, *when{}, *time{};
  int w = 0, h = 0, x = 0, y = 0, icon_h = 0;
  void make(lv_obj_t *parent, int at_x, int at_y, int width, int height) {
    x = at_x;
    y = at_y;
    w = width;
    h = height;
    // The icon a size smaller in a part too low for the large one.
    const Font face = h >= ui::line_height(Font::ICON) + ui::line_height(Font::HEADLINE) ? Font::ICON : Font::ICON_SMALL;
    icon_h = ui::line_height(face);
    icon = ui::label(parent, face, theme::ACCENT);
    lv_obj_set_pos(icon, x, y);
    lv_obj_set_width(icon, icon_h);
    ui::set_text(icon, ui::icon(ICON_ALARM));
    const int wx = x + icon_h + ui::px(6);
    when = ui::label(parent, Font::BODY, theme::MUTED);
    lv_obj_set_pos(when, wx, y + (icon_h - ui::line_height(Font::BODY)) / 2);
    lv_obj_set_width(when, std::max(1, x + w - wx));
    time = ui::label(parent, Font::VALUE, theme::INK);
    lv_obj_set_width(time, w);
  }
  void show(uint32_t icon_code, tessera::Tone tone, const std::string &words, const std::string &big, bool quiet) {
    ui::set_text(icon, ui::icon(icon_code));
    ui::set_tone(icon, tone, theme::MUTED);
    ui::set_text(when, words);
    ui::set_color(when, theme::MUTED);
    const Font face = fitting(big, w, h - icon_h, {Font::VALUE, Font::HEADLINE, Font::TITLE, Font::BODY});
    ui::set_font(time, face);
    ui::set_color(time, quiet ? theme::MUTED : theme::INK);
    ui::set_text(time, big);
    lv_obj_set_pos(time, x, y + h - ui::line_height(face));
  }
};

class AlarmTile : public tessera::Tile {
 public:
  explicit AlarmTile(AlarmClock *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    tile_ = c.tile;
    both_ = c.columns > 1 || c.rows > 1;
    if (!both_) {
      parts_[0].make(c.parent, 0, 0, c.width, c.height);
      return;
    }
    // Side by side on a wide tile, one above the other on a tall or a large one.
    const int gap = ui::px(12);
    if (c.columns > 1 && c.rows == 1) {
      const int half = (c.width - gap) / 2;
      parts_[0].make(c.parent, 0, 0, half, c.height);
      parts_[1].make(c.parent, half + gap, 0, c.width - half - gap, c.height);
    } else {
      const int half = (c.height - gap) / 2;
      parts_[0].make(c.parent, 0, 0, c.width, half);
      parts_[1].make(c.parent, 0, half + gap, c.width, c.height - half - gap);
    }
  }

  void on_tick(uint32_t epoch) override {
    if (!both_) {
      next(parts_[0], epoch);
      return;
    }
    for (int a = 0; a < ALARMS; ++a) alarm(parts_[a], a, epoch);
  }

  void on_theme() override { on_tick(tessera::epoch()); }
  void on_tap() override { tessera::open_card(plugin_->plugin_id(), "alarm", "", tile_); }

 private:
  // What rings or sleeps now, when it is `alarm` (-1: any).
  bool busy(Glance &g, int alarm) {
    if (alarm >= 0 && plugin_->ring_alarm() != alarm) return false;
    if (plugin_->ringing()) {
      g.show(ICON_RING, tessera::Tone::ALERT, plugin_->text("ringing"), tessera::clock_text(tessera::epoch()), false);
      return true;
    }
    if (plugin_->snoozed()) {
      g.show(ICON_SLEEP, tessera::Tone::BUSY, plugin_->text("snoozed"), tessera::clock_text(plugin_->snooze_until()), false);
      return true;
    }
    return false;
  }

  void next(Glance &g, uint32_t epoch) {
    if (busy(g, -1)) return;
    if (const uint32_t at = plugin_->next_alarm(epoch)) {
      const int32_t days = tessera::days_from_today(at);
      const std::string when = days == 0 ? plugin_->text("today") : days == 1 ? tessera::days_text(1) : tessera::date_text(at);
      g.show(ICON_ALARM, tessera::Tone::ACCENT, when, tessera::clock_text(at), false);
    } else {
      g.show(ICON_ALARM, tessera::Tone::NORMAL, "", epoch ? plugin_->text("no_alarm") : "", true);
    }
  }

  void alarm(Glance &g, int a, uint32_t epoch) {
    if (busy(g, a)) return;
    // The alarm's time as the screen writes a time (07:00, or 7:00 AM).
    std::string big;
    if (epoch) {
      big = tessera::clock_text(moment(epoch, 0, plugin_->hour(a), plugin_->minute(a)));
    } else {
      char time[8];
      snprintf(time, sizeof time, "%02d:%02d", plugin_->hour(a), plugin_->minute(a));
      big = time;
    }
    const bool on = plugin_->enabled(a);
    g.show(ICON_ALARM, on ? tessera::Tone::ACCENT : tessera::Tone::NORMAL,
           on ? plugin_->days_words(plugin_->days(a)) : plugin_->text("off"), big, !on);
  }

  AlarmClock *plugin_;
  int tile_ = -1;
  bool both_ = false;
  Glance parts_[ALARMS];
};

// ---- The card: both alarms ----
// From the top: the two alarms as tabs, each with its time; the chosen one's time in large digits with an arrow up
// above and down below the hours and the minutes; its days as round keys; and a switch with when it rings. Every tap
// writes the alarm's entity at once: there is nothing to save.
class AlarmCard : public tessera::Card {
 public:
  explicit AlarmCard(AlarmClock *plugin) : plugin_(plugin) {}

  void open(const tessera::CardContext &c) override {
    const int gap = ui::px(14), small = ui::px(8);
    const int w = std::min(c.width, ui::px(560));
    const int left = (c.width - w) / 2;
    const int row = std::max(1, std::min(ui::mm(12), c.height / 7));
    const int arrow = std::max(1, std::min(ui::mm(9), c.height / 9));
    const int day = std::max(1, std::min(row, (w - 6 * small) / 7));
    twelve_ = plugin_->twelve_hours();

    // The digits take the largest font the rest leaves room for.
    const int rest = c.height - (row + gap + 2 * arrow + 2 * small + gap + day + gap + row);
    digits_ = Font::TITLE;
    for (Font f : {Font::DIGITS, Font::VALUE, Font::HEADLINE, Font::TITLE}) {
      digits_ = f;
      if (ui::line_height(f) <= rest && ui::covers("0123456789", f)) break;
    }
    const int digit_h = ui::line_height(digits_);
    const int total = row + gap + 2 * arrow + 2 * small + digit_h + gap + day + gap + row;
    int y = std::max(0, (c.height - total) / 2);

    // The two alarms: a tab each, its name on the left and its time on the right.
    const int tab = (w - small) / ALARMS;
    for (int i = 0; i < ALARMS; ++i) {
      Tab &t = tabs_[i];
      t.box = {left + i * (tab + small), y, tab, row};
      t.block = placed(ui::block(c.parent, theme::BUTTON), t.box.x, t.box.y, t.box.w, t.box.h);
      const int pad = ui::px(12);
      t.name = ui::label(t.block, Font::BODY, theme::INK);
      lv_obj_set_pos(t.name, pad, (row - ui::line_height(Font::BODY)) / 2);
      lv_obj_set_width(t.name, tab / 2);
      ui::set_text(t.name, tessera::format(plugin_->text("alarm_n"), i + 1));
      t.time = ui::label(t.block, Font::TITLE, theme::INK);
      lv_obj_set_pos(t.time, tab / 2, (row - ui::line_height(Font::TITLE)) / 2);
      lv_obj_set_width(t.time, tab / 2 - pad);
      lv_obj_set_style_text_align(t.time, LV_TEXT_ALIGN_RIGHT, 0);
    }
    y += row + gap;

    // The time: hours, a colon, minutes (and AM or PM), each column with its arrows.
    const int digit_w = std::max(ui::text_width("88", digits_) + ui::px(8), ui::mm(16));
    const Font mark = fitting(":", w, digit_h, {Font::VALUE, Font::HEADLINE, Font::TITLE});
    const int colon_w = ui::text_width(":", mark) + 2 * small;
    const int half_w = twelve_ ? std::max(ui::text_width(plugin_->text("am"), Font::TITLE),
                                          ui::text_width(plugin_->text("pm"), Font::TITLE)) + small : 0;
    int x = left + (w - (2 * digit_w + colon_w + half_w)) / 2;
    const int top = y, middle = y + arrow + small;
    auto column = [&](int index, lv_obj_t *&digits) {
      ups_[index].make(c.parent, {x, top, digit_w, arrow}, Font::ICON_SMALL, true);
      ui::set_text(ups_[index].label, ui::icon(ICON_UP));
      digits = centred(c.parent, digits_, theme::INK, x, middle, digit_w, digit_h);
      downs_[index].make(c.parent, {x, middle + digit_h + small, digit_w, arrow}, Font::ICON_SMALL, true);
      ui::set_text(downs_[index].label, ui::icon(ICON_DOWN));
      x += digit_w;
    };
    column(0, hour_);
    colon_ = centred(c.parent, mark, theme::MUTED, x, middle, colon_w, digit_h);
    ui::set_text(colon_, ":");
    x += colon_w;
    column(1, minute_);
    half_ = centred(c.parent, Font::TITLE, theme::MUTED, x, middle, half_w, digit_h);
    y = middle + digit_h + small + arrow + gap;

    // Its days, Monday first, as round keys.
    const int spread = (w - 7 * day) / 6;
    for (int i = 0; i < 7; ++i) {
      days_[i].make(c.parent, {left + i * (day + spread), y, day, day}, Font::BODY_LARGE, true);
      ui::set_text(days_[i].label, plugin_->text(DAY_KEYS[i]));
    }
    y += day + gap;

    // A switch and when it rings; while it is snoozed, the same row stops it.
    power_ = {left, y, w, row};
    const int knob = row - 2 * ui::px(6);
    const int track_w = 2 * knob + ui::px(16);
    track_ = placed(ui::block(c.parent, theme::BUTTON), left, y + (row - knob - ui::px(8)) / 2, track_w, knob + ui::px(8));
    lv_obj_set_style_radius(track_, LV_RADIUS_CIRCLE, 0);
    knob_ = placed(ui::block(track_, theme::ON_ACCENT), ui::px(4), ui::px(4), knob, knob);
    lv_obj_set_style_radius(knob_, LV_RADIUS_CIRCLE, 0);
    knob_travel_ = track_w - knob - 2 * ui::px(4);
    status_ = ui::label(c.parent, Font::BODY_LARGE, theme::INK);
    lv_obj_set_pos(status_, left + track_w + gap, y + (row - ui::line_height(Font::BODY_LARGE)) / 2);
    lv_obj_set_width(status_, std::max(1, w - track_w - gap));
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
    const int h = plugin_->hour(a), m = plugin_->minute(a);
    if (ups_[0].box.has(p)) plugin_->set_time(a, (h + 1) % 24, m);
    else if (downs_[0].box.has(p)) plugin_->set_time(a, (h + 23) % 24, m);
    // The minutes go in fives: 07:03 goes up to 07:05 and down to 07:00.
    else if (ups_[1].box.has(p)) plugin_->set_time(a, h, (m - m % 5 + 5) % 60);
    else if (downs_[1].box.has(p)) plugin_->set_time(a, h, m % 5 ? m - m % 5 : (m + 55) % 60);
    for (int i = 0; i < 7; ++i)
      if (days_[i].box.has(p)) plugin_->set_days(a, plugin_->days(a) ^ static_cast<uint8_t>(1 << i));
    if (power_.has(p)) {
      if (plugin_->snoozed() && plugin_->ring_alarm() == a) plugin_->stop();
      else plugin_->set_enabled(a, !plugin_->enabled(a));
    }
    paint();
  }

 private:
  struct Tab {
    lv_obj_t *block{}, *name{}, *time{};
    Box box;
  };

  std::string time_of(int a) const {
    const int h = plugin_->hour(a), m = plugin_->minute(a);
    char text[16];
    if (twelve_) snprintf(text, sizeof text, "%d:%02d", h % 12 ? h % 12 : 12, m);
    else snprintf(text, sizeof text, "%02d:%02d", h, m);
    return text;
  }

  void paint() {
    const int a = chosen_;
    const uint32_t now = tessera::epoch();
    for (int i = 0; i < ALARMS; ++i) {
      Tab &t = tabs_[i];
      const bool chosen = i == a, on = plugin_->enabled(i);
      ui::set_fill(t.block, tessera::Tone::NORMAL, chosen ? theme::ACCENT : theme::BUTTON);
      ui::set_color(t.name, chosen ? theme::ON_ACCENT : theme::INK);
      ui::set_color(t.time, chosen ? theme::ON_ACCENT : on ? theme::INK : theme::MUTED);
      ui::set_text(t.time, on ? time_of(i) : plugin_->text("off"));
    }
    const int h = plugin_->hour(a), m = plugin_->minute(a);
    char text[8];
    if (twelve_) snprintf(text, sizeof text, "%d", h % 12 ? h % 12 : 12);
    else snprintf(text, sizeof text, "%02d", h);
    ui::set_text(hour_, text);
    snprintf(text, sizeof text, "%02d", m);
    ui::set_text(minute_, text);
    ui::set_text(half_, twelve_ ? plugin_->text(h < 12 ? "am" : "pm") : "");
    const bool on = plugin_->enabled(a);
    for (lv_obj_t *l : {hour_, minute_}) ui::set_color(l, on ? theme::INK : theme::MUTED);
    ui::set_color(colon_, theme::MUTED);
    ui::set_color(half_, theme::MUTED);
    for (int i = 0; i < 2; ++i) {
      ups_[i].paint(false);
      downs_[i].paint(false);
    }
    const uint8_t days = plugin_->days(a);
    for (int i = 0; i < 7; ++i) days_[i].paint(days & (1 << i));

    // The switch and its words.
    const bool asleep = plugin_->snoozed() && plugin_->ring_alarm() == a;
    ui::set_fill(track_, tessera::Tone::NORMAL, on ? theme::ACCENT : theme::BUTTON);
    lv_obj_set_x(knob_, ui::px(4) + (on ? knob_travel_ : 0));
    std::string words;
    if (asleep) words = tessera::fill(plugin_->text("stop_snooze"), "time", tessera::clock_text(plugin_->snooze_until()));
    else if (!on) words = plugin_->text("off");
    else if (const uint32_t at = plugin_->next_of(a, now)) words = plugin_->until_words(at, now);
    else words = plugin_->text("on");
    if (on && !days && !asleep) words += plugin_->text("once_suffix");
    ui::set_text(status_, words);
    ui::set_color(status_, asleep ? theme::ACCENT : on ? theme::INK : theme::MUTED);
  }

  AlarmClock *plugin_;
  int chosen_ = 0, knob_travel_ = 0;
  bool twelve_ = false;
  Font digits_ = Font::HEADLINE;
  Tab tabs_[ALARMS];
  Key ups_[2], downs_[2], days_[7];
  Box power_;
  lv_obj_t *hour_{}, *minute_{}, *colon_{}, *half_{}, *track_{}, *knob_{}, *status_{};
};

// ---- The popup: an alarm rings ----
// A bell that pulses beside the alarm's name, the time large under it, and the keys Snooze and Stop (Stop alone while
// snoozing is turned off in the settings).
class RingPopup : public tessera::Popup {
 public:
  explicit RingPopup(AlarmClock *plugin) : plugin_(plugin) {}

  void open(const tessera::PopupContext &c) override {
    const int w = c.width, gap = ui::px(14);
    const int disc = c.key > 0 ? c.key : ui::px(54);
    disc_ = placed(ui::block(c.parent, theme::ACCENT), 0, 0, disc, disc);
    lv_obj_set_style_radius(disc_, LV_RADIUS_CIRCLE, 0);
    bell_ = centred(disc_, Font::ICON_SMALL, theme::ON_ACCENT, 0, 0, disc, disc);
    ui::set_text(bell_, ui::icon(ICON_RING));
    ui::animate(disc_, tessera::Motion::PULSE, 1200);

    const int word_x = disc + ui::px(12);
    const int word_w = std::max(1, w - word_x - c.corner);
    const int a = plugin_->ring_alarm();
    const std::string word = a >= 0 ? tessera::format(plugin_->text("alarm_n"), a + 1) : std::string(plugin_->text("ringing"));
    const Font face = fitting(word, word_w, disc, {Font::HEADLINE, Font::TITLE, Font::BODY_LARGE, Font::BODY});
    word_ = ui::label(c.parent, face, theme::INK);
    lv_obj_set_pos(word_, word_x, (disc - ui::line_height(face)) / 2);
    lv_obj_set_width(word_, word_w);
    ui::set_text(word_, word);

    // The keys are a finger wide at least; the time takes the largest font that leaves them room.
    const int key_h = std::max(ui::mm(9), std::min(ui::mm(13), c.height / 4));
    const int room = c.height - disc - key_h - 3 * gap;
    const Font big = fitting("88:88", w, room, {Font::DIGITS, Font::VALUE, Font::HEADLINE, Font::TITLE});
    big_ = ui::covers("0123456789", big) ? big : Font::VALUE;
    time_ = centred(c.parent, big_, theme::INK, 0, disc + gap, w, ui::line_height(big_));
    const int y = disc + gap + ui::line_height(big_) + gap;
    if (plugin_->snooze_on()) {
      const int half = (w - gap) / 2;
      snooze_.make(c.parent, {0, y, half, key_h}, Font::BODY_LARGE, true);
      ui::set_text(snooze_.label, tessera::format(plugin_->text("snooze_n"), plugin_->snooze_minutes()));
      stop_.make(c.parent, {half + gap, y, w - half - gap, key_h}, Font::BODY_LARGE, true);
    } else {
      stop_.make(c.parent, {0, y, w, key_h}, Font::BODY_LARGE, true);
    }
    ui::set_text(stop_.label, plugin_->text("stop"));
    on_theme();
    on_tick(tessera::epoch());
  }

  void on_tick(uint32_t epoch) override {
    // The digits font has digits only: the colon of 07:00 is there, a 12-hour clock's AM is not.
    std::string text = epoch ? tessera::clock_text(epoch) : "";
    ui::set_font(time_, ui::covers(text, big_) ? big_ : Font::VALUE);
    ui::set_text(time_, text);
  }

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
  Font big_ = Font::VALUE;
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

bool AlarmClock::twelve_hours() const {
  const uint32_t now = tessera::epoch();
  return now && tessera::clock_text(moment(now, 0, 13, 0)).find("13") == std::string::npos;
}

std::string AlarmClock::days_words(uint8_t days) const {
  if (days == 0) return text("once");
  if (days == EVERY_DAY) return text("daily");
  if (days == WEEKDAYS) return text("weekdays");
  if (days == WEEKEND) return text("weekend");
  // A run of three days or more as "Mon–Thu", else the days one by one.
  int first = -1, last = -1, count = 0;
  bool run = true;
  for (int i = 0; i < 7; ++i) {
    if (!(days & (1 << i))) continue;
    if (first < 0) first = i;
    else if (i != last + 1) run = false;
    last = i;
    ++count;
  }
  if (run && count >= 3) return std::string(text(DAY_KEYS[first])) + "–" + text(DAY_KEYS[last]);
  std::string s;
  for (int i = 0; i < 7; ++i) {
    if (!(days & (1 << i))) continue;
    if (!s.empty()) s += ' ';
    s += text(DAY_KEYS[i]);
  }
  return s;
}

std::string AlarmClock::until_words(uint32_t at, uint32_t now) const {
  const uint32_t minutes = at > now ? (at - now + 59) / 60 : 0;
  if (minutes >= 24 * 60) {
    const int32_t days = tessera::days_from_today(at);
    const std::string day = days == 1 ? tessera::days_text(1) : tessera::date_text(at);
    return tessera::fill(tessera::fill(text("rings_on"), "day", day).c_str(), "time", tessera::clock_text(at));
  }
  char n[8];
  snprintf(n, sizeof n, "%u", static_cast<unsigned>(minutes % 60));
  if (minutes < 60) return tessera::fill(text("rings_in_m"), "m", n);
  const std::string with_m = tessera::fill(text("rings_in_hm"), "m", n);
  snprintf(n, sizeof n, "%u", static_cast<unsigned>(minutes / 60));
  return tessera::fill(with_m.c_str(), "h", n);
}

uint32_t AlarmClock::next_of(int a, uint32_t now) const {
  if (!now || !enabled(a)) return 0;
  const tessera::LocalTime t = tessera::local_time(now);
  const int today = day_index(t), passed = t.hour * 60 + t.minute;
  const uint8_t on = days(a);
  const int at = hour(a) * 60 + minute(a);
  for (int d = 0; d <= 7; ++d) {
    if (d == 0 && at <= passed) continue;   // today's has gone, or rings this minute
    if (on && !(on & (1 << ((today + d) % 7)))) continue;
    return moment(now, d, hour(a), minute(a));
  }
  return 0;
}

uint32_t AlarmClock::next_alarm(uint32_t now) const {
  if (!now) return 0;
  uint32_t next = snoozed() ? snooze_until_ : 0;
  for (int a = 0; a < ALARMS; ++a) {
    const uint32_t when = next_of(a, now);
    if (when && (!next || when < next)) next = when;
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
  if (!published_) publish_ringing_(ringing_);
}

void AlarmClock::publish_ringing_(bool on) {
  published_ = true;
  if (ringing_sensor_ != nullptr) ringing_sensor_->publish_state(on);
  for (int a = 0; a < ALARMS; ++a)
    if (alarm_ringing_[a] != nullptr) alarm_ringing_[a]->publish_state(on && ring_alarm_ == a);
}

void AlarmClock::ring_(int alarm, uint32_t now, bool first) {
  ringing_ = true;
  ring_alarm_ = alarm;
  rang_ms_ = ms_;
  woke_ms_ = ms_;
  snooze_until_ = 0;
  if (first) first_rang_ = now;
  publish_ringing_(true);
  show_();
  tessera::refresh();
}

void AlarmClock::ring_now() {
  if (!ringing_) ring_(-1, tessera::epoch(), true);
}

void AlarmClock::show_() {
  tessera::show_popup(this, [this]() -> tessera::Popup * { return new RingPopup(this); });
}

// The alarm falls silent: its sensors and its sound.
void AlarmClock::quiet_() {
  ringing_ = false;
  publish_ringing_(false);
#ifdef USE_SPEAKER
  sounds_.stop();
#endif
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
  snooze_until_ = now ? now + static_cast<uint32_t>(snooze_minutes()) * 60u : 0;
  quiet_();
  if (close && tessera::popup_shown(this)) tessera::close_popup(this);
}

void AlarmClock::dismissed() {
  // The popup's close key or a tap beside it snoozes; with snoozing turned off the popup comes back until Stop.
  if (ringing_ && snooze_on()) snooze_(false);
}

bool AlarmClock::settings(tessera::SettingsPage &page) {
  page.icon = "\U000F0020";
  page.card(text("set_open"), "\U000F0020", "alarm");
  page.toggle(text("set_snooze"), [this] { return snooze_on(); },
              [this](bool on) { on ? snooze_switch_->turn_on() : snooze_switch_->turn_off(); });
  page.number(text("set_snooze_minutes"), 1, 30, 1, text("unit_min"), [this] { return snooze_minutes(); },
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
