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
// The look: the night in the purple of Tessera's mark, the morning in its amber. Text stays ink: amber is a fill and an
// icon only, since it is too light to read on a light card. Words on amber are SCRIM, dark in both looks.

static constexpr uint32_t ICON_MOON = 0xF0594, ICON_SUN = 0xF0599;
static constexpr uint32_t TWELVE_HOURS = 12 * 3600, LAST_HOUR = 3600;

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

static void hide(lv_obj_t *obj, bool hidden) {
  if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

// A disc of `size` with its middle at cx, cy.
static lv_obj_t *disc(lv_obj_t *parent, theme::Role fill, int cx, int cy, int size) {
  lv_obj_t *d = placed(ui::block(parent, fill), cx - size / 2, cy - size / 2, size, size);
  lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
  return d;
}

static void move_disc(lv_obj_t *d, int cx, int cy) {
  lv_obj_set_pos(d, cx - lv_obj_get_width(d) / 2, cy - lv_obj_get_height(d) / 2);
}

// A label whose line is centred in a box of `w` by `h` at x, y.
static lv_obj_t *centred(lv_obj_t *parent, Font font, theme::Role role, int x, int y, int w, int h) {
  lv_obj_t *l = ui::label(parent, font, role);
  lv_obj_set_pos(l, x, y + (h - ui::line_height(font)) / 2);
  lv_obj_set_width(l, w);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  return l;
}

// The largest of `fonts` whose line is at most `height` and in which `text` is at most `width`; the last when none is.
static Font fitting(const std::string &text, int width, int height, std::initializer_list<Font> fonts) {
  Font face = Font::BODY;
  for (Font f : fonts) {
    face = f;
    if (ui::line_height(f) <= height && ui::text_width(text, f) <= width) break;
  }
  return face;
}

// A point on a circle round cx, cy: `degrees` clockwise from the top.
static void on_circle(int cx, int cy, int radius, float degrees, int &x, int &y) {
  const float a = degrees * 3.14159265f / 180.0f;
  x = cx + static_cast<int>(lroundf(radius * sinf(a)));
  y = cy - static_cast<int>(lroundf(radius * cosf(a)));
}

// A ring: a faint whole circle in the night's purple and a stretch of it drawn full (from `from` to `to` degrees,
// clockwise from the top), with optional dots for the hours round it.
struct Ring {
  lv_obj_t *arc{};
  int cx = 0, cy = 0, size = 0, width = 0;
  void make(lv_obj_t *parent, int at_cx, int at_cy, int diameter, int thickness, int hour_dots) {
    cx = at_cx;
    cy = at_cy;
    size = diameter;
    width = thickness;
    // The hour dots first, so the ring lies over them.
    if (hour_dots > 0) {
      const int dot = std::max(2, width / 3), radius = size / 2 + width;
      for (int i = 0; i < hour_dots; ++i) {
        int x, y;
        on_circle(cx, cy, radius, 360.0f * i / hour_dots, x, y);
        lv_obj_t *d = disc(parent, theme::MARK_PURPLE, x, y, dot);
        lv_obj_set_style_bg_opa(d, LV_OPA_50, 0);
      }
    }
    arc = lv_arc_create(parent);
    lv_obj_remove_style_all(arc);
    placed(arc, cx - size / 2, cy - size / 2, size, size);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, ui::color(theme::MARK_PURPLE), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    span(0, 0, theme::MARK_PURPLE);
  }
  // The middle of the ring's band at `degrees`.
  void point(float degrees, int &x, int &y) const { on_circle(cx, cy, (size - width) / 2, degrees, x, y); }
  void span(int from, int to, theme::Role colour) {
    lv_obj_set_style_arc_color(arc, ui::color(colour), LV_PART_INDICATOR);
    // LVGL draws nothing for a stretch of no length; a whole turn needs 0 to 360.
    if (to <= from) lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    else lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_arc_set_angles(arc, ((from % 360) + 360) % 360, to - from >= 360 ? (from % 360 + 360) % 360 + 360 : ((to % 360) + 360) % 360);
  }
};

// A key: a pill or a disc with its words, or an icon and words side by side, in the middle.
struct Key {
  lv_obj_t *block{}, *icon{}, *label{};
  Box box;
  int label_x = 0, label_y = 0;
  void make(lv_obj_t *parent, const Box &at, Font font, uint32_t icon_code = 0) {
    box = at;
    block = placed(ui::block(parent, theme::BUTTON), at.x, at.y, at.w, at.h);
    lv_obj_set_style_radius(block, LV_RADIUS_CIRCLE, 0);
    label = centred(block, font, theme::INK, 0, 0, at.w, at.h);
    label_y = (at.h - ui::line_height(font)) / 2;
    if (icon_code) {
      icon = ui::label(block, Font::ICON_SMALL, theme::INK);
      ui::set_text(icon, ui::icon(icon_code));
    }
  }
  // An icon beside the words: the two together in the middle of the key.
  void set(const std::string &words) {
    ui::set_text(label, words);
    if (!icon) return;
    const int iw = ui::line_height(Font::ICON_SMALL), gap = ui::px(6);
    const int ww = ui::text_width(words, Font::BODY_LARGE);
    const int x = (box.w - iw - gap - ww) / 2;
    lv_obj_set_pos(icon, x, (box.h - iw) / 2);
    lv_obj_set_width(icon, iw);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    label_x = x + iw + gap;
    lv_obj_set_x(label, label_x);
    lv_obj_set_width(label, ww + 2);
  }
  // A filled key (`fill`, its words in `ink`), or a quiet one: no fill, a hairline round it.
  void paint(theme::Role fill, theme::Role ink, bool filled = true) {
    ui::set_fill(block, tessera::Tone::NORMAL, fill);
    lv_obj_set_style_bg_opa(block, filled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    // A hairline moves the block's children in by its width: the words stay where they were laid out.
    const int line = filled ? 0 : 1;
    lv_obj_set_style_border_width(block, line, 0);
    lv_obj_set_style_border_color(block, ui::color(theme::LINE), 0);
    lv_obj_set_pos(label, label_x - line, label_y - line);
    ui::set_color(label, ink);
    if (icon) ui::set_color(icon, ink);
  }
};

// The time as a large hh:mm with a soft colon (a 12-hour clock as one label: 7:00 AM).
struct BigTime {
  lv_obj_t *hours{}, *colon{}, *minutes{};
  int cx = 0, y = 0, left = -1;   // left >= 0: the time starts there instead of standing round cx
  Font font = Font::VALUE;
  void make(lv_obj_t *parent, Font face, int at_cx, int at_y) {
    font = face;
    cx = at_cx;
    y = at_y;
    hours = ui::label(parent, font, theme::INK);
    colon = ui::label(parent, font, theme::MUTED);
    minutes = ui::label(parent, font, theme::INK);
  }
  int height() const { return ui::line_height(font); }
  void show(const std::string &text, theme::Role ink) {
    const size_t at = text.find(':');
    const bool split = text.size() == 5 && at == 2 && ui::covers(text, font);
    const std::string h = split ? text.substr(0, 2) : text, m = split ? text.substr(3) : "";
    // The digits font has digits and a few signs only: a 12-hour clock's AM steps down to VALUE.
    const Font face = split || ui::covers(text, font) ? font : Font::VALUE;
    for (lv_obj_t *l : {hours, colon, minutes}) ui::set_font(l, face);
    const int hw = ui::text_width(h, face), cw = split ? ui::text_width(":", face) + ui::px(4) : 0,
              mw = split ? ui::text_width(m, face) : 0;
    const int x = left >= 0 ? left : cx - (hw + cw + mw) / 2, top = y + (height() - ui::line_height(face)) / 2;
    placed(hours, x, top, hw + 2, ui::line_height(face));
    placed(colon, x + hw, top, cw + 2, ui::line_height(face));
    lv_obj_set_style_text_align(colon, LV_TEXT_ALIGN_CENTER, 0);
    placed(minutes, x + hw + cw, top, mw + 2, ui::line_height(face));
    ui::set_text(hours, h);
    ui::set_text(colon, split ? ":" : "");
    ui::set_text(minutes, m);
    ui::set_color(hours, ink);
    ui::set_color(minutes, ink);
    ui::set_color(colon, theme::MUTED);
  }
};

// The largest digits font whose "88:88" fits `width` and whose line fits `height`.
static Font digits_for(int width, int height) {
  for (Font f : {Font::DIGITS, Font::VALUE, Font::HEADLINE, Font::TITLE})
    if (ui::line_height(f) <= height && ui::text_width("88:88", f) <= width) return f;
  return Font::BODY;
}

// ---- The tile: a night clock ----
// The time now, in a ring that fills in the twelve hours before the next alarm, towards the sun at its top. The ring
// and the icon turn from the night's purple to the morning's amber in the last hour. Under the time: how long there is
// left to sleep, and the alarm's time. On a small tile the ring stands beside the words; on a large one, and over a
// whole page, the words stand inside it.
class NightTile : public tessera::Tile {
 public:
  explicit NightTile(AlarmClock *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    tile_ = c.tile;
    const int w = c.width, h = c.height;
    inside_ = c.full || std::min(w, h) >= ui::px(170);
    if (inside_) {
      // The ring as large as the tile, with room for its hour dots round it.
      const int thick = std::max(ui::px(6), std::min(w, h) / 34);
      const int size = std::min(w, h) - 4 * thick;
      ring_.make(c.parent, w / 2, h / 2, size, thick, 12);
      const int inner = size - 2 * thick;
      const int line = ui::line_height(Font::BODY), icon_h = ui::line_height(Font::ICON_SMALL);
      const Font face = digits_for(inner * 7 / 10, inner - 2 * (icon_h + 2 * line));
      const int stack = icon_h + ui::line_height(face) + 2 * line + ui::px(6);
      int y = h / 2 - stack / 2;
      icon_ = centred(c.parent, Font::ICON_SMALL, theme::MARK_PURPLE, 0, y, w, icon_h);
      y += icon_h;
      time_.make(c.parent, face, w / 2, y);
      y += time_.height() + ui::px(6);
      first_ = centred(c.parent, Font::BODY, theme::MARK_PURPLE, (w - inner) / 2, y, inner, line);
      room_ = inner * 9 / 10;
      second_ = centred(c.parent, Font::BODY, theme::MUTED, (w - inner) / 2, y + line, inner, line);
    } else {
      // A small ring at the left with the icon in it, the words beside.
      const int thick = std::max(ui::px(5), h / 18);
      const int size = h - 2 * thick;
      ring_.make(c.parent, size / 2 + thick, h / 2, size, thick, 0);
      icon_ = centred(c.parent, Font::ICON, theme::MARK_PURPLE, ring_.cx - size / 2, h / 2 - ui::line_height(Font::ICON) / 2,
                      size, ui::line_height(Font::ICON));
      const int x = size + 2 * thick + ui::px(10), room = std::max(1, w - x);
      const int line = ui::line_height(Font::BODY);
      const Font face = digits_for(room, h - 2 * line);
      const int stack = ui::line_height(face) + 2 * line;
      int y = (h - stack) / 2;
      time_.make(c.parent, face, x + room / 2, y);
      time_.left = x;
      first_ = centred(c.parent, Font::BODY, theme::MARK_PURPLE, x, y + ui::line_height(face), room, line);
      second_ = centred(c.parent, Font::BODY, theme::MUTED, x, y + ui::line_height(face) + line, room, line);
      left_ = x;
      room_ = room;
      lv_obj_set_style_text_align(first_, LV_TEXT_ALIGN_LEFT, 0);
      lv_obj_set_style_text_align(second_, LV_TEXT_ALIGN_LEFT, 0);
    }
    const int sun = ring_.width * 5 / 2;
    int sx, sy;
    ring_.point(0, sx, sy);
    sun_ = disc(c.parent, theme::MARK_AMBER, sx, sy, sun);
    now_ = disc(c.parent, theme::ON_ACCENT, sx, sy, std::max(4, ring_.width));
  }

  void on_tick(uint32_t epoch) override {
    const std::string now_text = epoch ? tessera::clock_text(epoch) : "";
    // The time: in the ring's middle, or at the left of the words beside a small ring.
    time_.show(now_text, theme::INK);
    std::string first, second;
    theme::Role tint = theme::MARK_PURPLE;
    uint32_t icon = ICON_MOON;
    int filled = 0;
    bool sun = false, dot = false;
    if (plugin_->ringing()) {
      icon = ICON_RING;
      tint = theme::MARK_AMBER;
      first = plugin_->text("ring_now");
      filled = 360;
      sun = true;
    } else if (plugin_->snoozed()) {
      icon = ICON_SLEEP;
      tint = theme::MARK_AMBER;
      first = tessera::fill(plugin_->text("snoozed_until"), "time", tessera::clock_text(plugin_->snooze_until()));
      filled = 360;
      sun = true;
    } else if (const uint32_t at = epoch ? plugin_->next_alarm(epoch) : 0) {
      const uint32_t left = at - epoch;
      sun = true;
      if (left <= LAST_HOUR) {
        tint = theme::MARK_AMBER;
        icon = ICON_SUN;
      }
      if (left < TWELVE_HOURS) {
        filled = static_cast<int>(360 - left * 360 / TWELVE_HOURS);
        dot = filled > 0 && filled < 360;
        first = plugin_->sleep_words(left);
        second = tessera::fill(plugin_->text("alarm_at"), "time", tessera::clock_text(at));
        // Beside a small ring: the short words when the long ones do not fit.
        if (ui::text_width(first, Font::BODY) > room_) first = plugin_->sleep_words(left, true);
        if (ui::text_width(second, Font::BODY) > room_)
          second = tessera::fill(plugin_->text("until_time"), "time", tessera::clock_text(at));
      } else {
        const int32_t days = tessera::days_from_today(at);
        const std::string day = days == 0 ? std::string(plugin_->text("today")) : days == 1 ? tessera::days_text(1) : tessera::date_text(at);
        first = tessera::fill(tessera::fill(plugin_->text("next_on"), "day", day).c_str(), "time", tessera::clock_text(at));
      }
    } else if (epoch) {
      first = plugin_->text("no_alarm");
      if (ui::text_width(first, Font::BODY) > room_) first = plugin_->text("off");
      tint = theme::MUTED;
    }
    ring_.span(0, filled, tint);
    hide(sun_, !sun);
    hide(now_, !dot);
    if (dot) {
      int x, y;
      ring_.point(filled, x, y);
      move_disc(now_, x, y);
    }
    ui::set_text(icon_, ui::icon(icon));
    ui::set_color(icon_, tint == theme::MUTED ? theme::MUTED : tint);
    ui::set_text(first_, first);
    // Words stay readable: purple at night, ink in the morning (amber is too light for words on a light card).
    ui::set_color(first_, tint == theme::MARK_PURPLE ? theme::MARK_PURPLE : tint == theme::MUTED ? theme::MUTED : theme::INK);
    ui::set_text(second_, second);
  }

  void on_theme() override { on_tick(tessera::epoch()); }
  void on_tap() override { tessera::open_card(plugin_->plugin_id(), "alarm", "", tile_); }

 private:
  AlarmClock *plugin_;
  int tile_ = -1, left_ = 0, room_ = 0;
  bool inside_ = false;
  Ring ring_;
  BigTime time_;
  lv_obj_t *icon_{}, *first_{}, *second_{}, *sun_{}, *now_{};
};

// ---- The card: set the alarms ----
// From the top: the two alarms as pills; the chosen one's time inside a ring of the day, its sun on the alarm's time
// and the night before it in purple, with arrows above and below the hours and the minutes; its days as round keys;
// and a switch that says when it rings. Every tap writes the alarm's entity at once: there is nothing to save.
class AlarmCard : public tessera::Card {
 public:
  explicit AlarmCard(AlarmClock *plugin) : plugin_(plugin) {}

  void open(const tessera::CardContext &c) override {
    const int gap = ui::px(12), small = ui::px(6);
    const int w = std::min(c.width, ui::px(560));
    const int left = (c.width - w) / 2;
    const int row = std::max(1, std::min(ui::mm(11), c.height / 8));
    const int day = std::max(1, std::min(ui::mm(10), (w - 6 * small) / 7));
    twelve_ = plugin_->twelve_hours();
    int y = 0;

    // The two alarms: a pill each, a sun and its name on the left, its time on the right.
    const int pill = (w - small) / ALARMS;
    for (int i = 0; i < ALARMS; ++i) {
      Pill &p = pills_[i];
      p.box = {left + i * (pill + small), y, pill, row};
      p.block = placed(ui::block(c.parent, theme::BUTTON), p.box.x, p.box.y, p.box.w, p.box.h);
      lv_obj_set_style_radius(p.block, LV_RADIUS_CIRCLE, 0);
      const int pad = row / 2;
      const int icon_h = ui::line_height(Font::ICON_SMALL);
      p.icon = ui::label(p.block, Font::ICON_SMALL, theme::MARK_AMBER);
      lv_obj_set_pos(p.icon, pad - ui::px(4), (row - icon_h) / 2);
      lv_obj_set_width(p.icon, icon_h);
      ui::set_text(p.icon, ui::icon(ICON_SUN));
      p.name = ui::label(p.block, Font::BODY, theme::INK);
      lv_obj_set_pos(p.name, pad + icon_h, (row - ui::line_height(Font::BODY)) / 2);
      lv_obj_set_width(p.name, pill / 2);
      ui::set_text(p.name, tessera::format(plugin_->text("alarm_n"), i + 1));
      p.time = ui::label(p.block, Font::TITLE, theme::INK);
      lv_obj_set_pos(p.time, pill / 2, (row - ui::line_height(Font::TITLE)) / 2);
      lv_obj_set_width(p.time, pill / 2 - pad);
      lv_obj_set_style_text_align(p.time, LV_TEXT_ALIGN_RIGHT, 0);
    }
    y += row + gap;

    // The ring of the day, as large as the room under the pills leaves, over the days and the switch.
    const int below = gap + day + gap + row;
    const int size = std::max(ui::px(120), std::min(w, c.height - y - below));
    const int thick = std::max(ui::px(6), size / 30);
    ring_.make(c.parent, left + w / 2, y + size / 2, size, thick, 0);
    const int inner = size - 2 * thick;
    sun_ = disc(c.parent, theme::MARK_AMBER, ring_.cx, y, thick * 5 / 2);
    // Inside it: arrows, the time, arrows.
    const int arrow = std::max(ui::mm(7), std::min(ui::mm(10), inner / 5));
    const Font face = digits_for(inner * 7 / 10, inner - 2 * arrow - 4 * small);
    const int digit_h = ui::line_height(face);
    const int digit_w = std::max(ui::text_width("88", face) + ui::px(8), ui::mm(14));
    const int colon_w = ui::text_width(":", face) + 2 * small;
    const int stack = 2 * arrow + digit_h + 2 * small;
    const int top = ring_.cy - stack / 2, middle = top + arrow + small;
    const int x0 = ring_.cx - (2 * digit_w + colon_w) / 2;
    for (int i = 0; i < 2; ++i) {
      const int x = x0 + i * (digit_w + colon_w);
      ups_[i].make(c.parent, {x, top, digit_w, arrow}, Font::ICON_SMALL);
      ui::set_text(ups_[i].label, ui::icon(ICON_UP));
      downs_[i].make(c.parent, {x, middle + digit_h + small, digit_w, arrow}, Font::ICON_SMALL);
      ui::set_text(downs_[i].label, ui::icon(ICON_DOWN));
    }
    hours_ = centred(c.parent, face, theme::INK, x0, middle, digit_w, digit_h);
    colon_ = centred(c.parent, face, theme::MUTED, x0 + digit_w, middle, colon_w, digit_h);
    ui::set_text(colon_, ":");
    minutes_ = centred(c.parent, face, theme::INK, x0 + digit_w + colon_w, middle, digit_w, digit_h);
    half_ = centred(c.parent, Font::BODY, theme::MUTED, ring_.cx - inner / 4, middle + digit_h - ui::px(2), inner / 2,
                    ui::line_height(Font::BODY));
    hide(half_, !twelve_);
    y += size + gap;

    // Its days, Monday first, as round keys.
    for (int i = 0; i < 7; ++i) {
      days_[i].make(c.parent, {left + i * (w - day) / 6, y, day, day}, Font::BODY);
      ui::set_text(days_[i].label, plugin_->text(DAY_KEYS[i]));
    }
    y += day + gap;

    // A switch and when it rings; while it is snoozed, the same row stops it.
    power_ = {left, y, w, row};
    const int knob = std::max(ui::px(16), row - 2 * ui::px(10));
    const int track_w = 2 * knob + ui::px(12);
    track_ = placed(ui::block(c.parent, theme::BUTTON), left, y + (row - knob - ui::px(6)) / 2, track_w, knob + ui::px(6));
    lv_obj_set_style_radius(track_, LV_RADIUS_CIRCLE, 0);
    knob_ = disc(track_, theme::ON_ACCENT, ui::px(3) + knob / 2, ui::px(3) + knob / 2, knob);
    knob_travel_ = track_w - knob - 2 * ui::px(3);
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
      if (pills_[i].box.has(p)) chosen_ = i;
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
  struct Pill {
    lv_obj_t *block{}, *icon{}, *name{}, *time{};
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
      Pill &p = pills_[i];
      const bool chosen = i == a, on = plugin_->enabled(i);
      // The chosen alarm's pill is lit: amber when it is on, a key's grey when it is off; the other has a hairline.
      ui::set_fill(p.block, tessera::Tone::NORMAL, chosen && on ? theme::MARK_AMBER : theme::BUTTON);
      lv_obj_set_style_bg_opa(p.block, chosen ? (on ? LV_OPA_30 : LV_OPA_COVER) : LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(p.block, chosen ? 0 : 1, 0);
      lv_obj_set_style_border_color(p.block, ui::color(theme::LINE), 0);
      hide(p.icon, !on);
      ui::set_color(p.name, on ? theme::INK : theme::MUTED);
      ui::set_color(p.time, on ? theme::INK : theme::MUTED);
      ui::set_text(p.time, on ? time_of(i) : plugin_->text("off"));
    }
    const int h = plugin_->hour(a), m = plugin_->minute(a);
    const bool on = plugin_->enabled(a);
    char text[8];
    if (twelve_) snprintf(text, sizeof text, "%d", h % 12 ? h % 12 : 12);
    else snprintf(text, sizeof text, "%02d", h);
    ui::set_text(hours_, text);
    snprintf(text, sizeof text, "%02d", m);
    ui::set_text(minutes_, text);
    ui::set_text(half_, twelve_ ? plugin_->text(h < 12 ? "am" : "pm") : "");
    ui::set_color(hours_, on ? theme::INK : theme::MUTED);
    ui::set_color(minutes_, on ? theme::INK : theme::MUTED);
    ui::set_color(colon_, theme::MUTED);
    for (int i = 0; i < 2; ++i) {
      ups_[i].paint(theme::BUTTON, theme::MUTED, false);
      downs_[i].paint(theme::BUTTON, theme::MUTED, false);
      lv_obj_set_style_border_width(ups_[i].block, 0, 0);
      lv_obj_set_style_border_width(downs_[i].block, 0, 0);
    }
    // The ring of the day: the sun on the alarm's time (midnight at the top), the eight hours before it in purple.
    const int at = (h * 60 + m) * 360 / 1440;
    ring_.span(at - 120, at, on ? theme::MARK_PURPLE : theme::BUTTON);
    int sx, sy;
    ring_.point(at, sx, sy);
    move_disc(sun_, sx, sy);
    ui::set_fill(sun_, tessera::Tone::NORMAL, on ? theme::MARK_AMBER : theme::BUTTON);

    const uint8_t days = plugin_->days(a);
    for (int i = 0; i < 7; ++i) {
      const bool chosen = days & (1 << i);
      days_[i].paint(chosen ? (on ? theme::MARK_AMBER : theme::BUTTON) : theme::BUTTON,
                     chosen ? (on ? theme::SCRIM : theme::INK) : theme::MUTED, chosen);
    }

    // The switch and its words.
    const bool asleep = plugin_->snoozed() && plugin_->ring_alarm() == a;
    ui::set_fill(track_, tessera::Tone::NORMAL, on ? theme::MARK_AMBER : theme::BUTTON);
    lv_obj_set_x(knob_, ui::px(3) + (on ? knob_travel_ : 0));
    std::string words;
    if (asleep) words = tessera::fill(plugin_->text("stop_snooze"), "time", tessera::clock_text(plugin_->snooze_until()));
    else if (!on) words = plugin_->text("off");
    else if (const uint32_t next = plugin_->next_of(a, now)) words = plugin_->until_words(next, now);
    else words = plugin_->text("on");
    if (on && !days && !asleep) words += plugin_->text("once_suffix");
    ui::set_text(status_, words);
    ui::set_color(status_, on || asleep ? theme::INK : theme::MUTED);
  }

  AlarmClock *plugin_;
  int chosen_ = 0, knob_travel_ = 0;
  bool twelve_ = false;
  Pill pills_[ALARMS];
  Ring ring_;
  Key ups_[2], downs_[2], days_[7];
  Box power_;
  lv_obj_t *sun_{}, *hours_{}, *minutes_{}, *colon_{}, *half_{}, *track_{}, *knob_{}, *status_{};
};

// ---- The popup: good morning ----
// A sun that rises over the card's top edge and breathes, a greeting for the time of day, the time, and the keys:
// snooze (a moon and its minutes) and Stop in the morning's amber. Stop alone while snoozing is turned off.
class RingPopup : public tessera::Popup {
 public:
  explicit RingPopup(AlarmClock *plugin) : plugin_(plugin) {}

  void open(const tessera::PopupContext &c) override {
    const int w = c.width, gap = ui::px(12);
    // The sun: a disc and its glow, cut off at their middle by a window, so it stands on the horizon.
    const int glow = std::min(w - 2 * c.corner, std::max(ui::px(80), c.key * 2));
    const int sun = glow * 6 / 10;
    lv_obj_t *window = lv_obj_create(c.parent);
    lv_obj_remove_style_all(window);
    lv_obj_remove_flag(window, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(window, LV_OBJ_FLAG_SCROLLABLE);
    placed(window, (w - glow) / 2, 0, glow, glow / 2);
    glow_ = disc(window, theme::MARK_AMBER, glow / 2, glow / 2, glow);
    lv_obj_set_style_bg_opa(glow_, LV_OPA_30, 0);
    disc(window, theme::MARK_AMBER, glow / 2, glow / 2, sun);
    ui::animate(glow_, tessera::Motion::PULSE, 2400);
    int y = glow / 2 + gap;

    const std::string greeting = plugin_->greeting(tessera::epoch());
    const Font face = fitting(greeting, w, ui::line_height(Font::HEADLINE), {Font::HEADLINE, Font::TITLE, Font::BODY_LARGE});
    word_ = centred(c.parent, face, theme::INK, 0, y, w, ui::line_height(face));
    ui::set_text(word_, greeting);
    y += ui::line_height(face);

    const int key_h = std::max(ui::mm(9), std::min(ui::mm(12), c.height / 5));
    const Font big = digits_for(w, c.height - y - key_h - 2 * gap);
    time_.make(c.parent, big, w / 2, y);
    y += time_.height() + gap;
    if (plugin_->snooze_on()) {
      const int half = (w - gap) / 2;
      snooze_.make(c.parent, {0, y, half, key_h}, Font::BODY_LARGE, ICON_SLEEP);
      snooze_.set(tessera::format(plugin_->text("snooze_short"), plugin_->snooze_minutes()));
      stop_.make(c.parent, {half + gap, y, w - half - gap, key_h}, Font::BODY_LARGE);
    } else {
      stop_.make(c.parent, {0, y, w, key_h}, Font::BODY_LARGE);
    }
    ui::set_text(stop_.label, plugin_->text("stop"));
    on_theme();
  }

  void on_tick(uint32_t epoch) override { time_.show(epoch ? tessera::clock_text(epoch) : "", theme::INK); }

  void on_theme() override {
    ui::set_color(word_, theme::INK);
    if (snooze_.block != nullptr) snooze_.paint(theme::BUTTON, theme::INK);
    stop_.paint(theme::MARK_AMBER, theme::SCRIM);
    on_tick(tessera::epoch());
  }

  void on_tap() override {
    const tessera::Point p = tessera::tap_point();
    if (stop_.box.has(p)) plugin_->stop();
    else if (snooze_.block != nullptr && snooze_.box.has(p)) plugin_->snooze();
  }

  void on_dismiss() override { plugin_->dismissed(); }

 private:
  AlarmClock *plugin_;
  lv_obj_t *glow_{}, *word_{};
  BigTime time_;
  Key snooze_, stop_;
};

// ---- The plugin ----

void AlarmClock::setup() {
  add_tile("alarm", [this]() { return new NightTile(this); });
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

std::string AlarmClock::sleep_words(uint32_t seconds, bool brief) const {
  const uint32_t minutes = (seconds + 59) / 60;
  char n[8];
  snprintf(n, sizeof n, "%u", static_cast<unsigned>(minutes % 60));
  if (minutes < 60) return tessera::fill(text(brief ? "sleep_m_short" : "sleep_m"), "m", n);
  const std::string with_m = tessera::fill(text(brief ? "sleep_hm_short" : "sleep_hm"), "m", n);
  snprintf(n, sizeof n, "%u", static_cast<unsigned>(minutes / 60));
  return tessera::fill(with_m.c_str(), "h", n);
}

std::string AlarmClock::greeting(uint32_t now) const {
  if (!now) return text("greet_night");
  const int h = tessera::local_time(now).hour;
  return text(h >= 5 && h < 12 ? "greet_morning" : h >= 12 && h < 18 ? "greet_afternoon" : h >= 18 ? "greet_evening" : "greet_night");
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
