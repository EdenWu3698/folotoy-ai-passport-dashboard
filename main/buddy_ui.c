#include "buddy_ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "buddy_i4.h"
#include "lvgl.h"
#include "passport_avatar.h"
#include "passport_qr.h"

#define UI_W 240
#define UI_H 320
#define I4_PALETTE_BYTES (16U * sizeof(lv_color32_t))
#define COL_BG lv_color_hex(0x031D17)
#define COL_PANEL lv_color_hex(0x07352A)
#define COL_LINE lv_color_hex(0x0C4C3D)
#define COL_MINT lv_color_hex(0x55B88A)
#define COL_INK lv_color_hex(0xD8E7D4)
#define COL_BLACK lv_color_hex(0x070A0B)
#define COL_DIM lv_color_hex(0x71665B)

static const uint32_t s_palette_rgb[16] = {
    0x031D17, 0x07352A, 0x0C4C3D, 0x55B88A,
    0xD8E7D4, 0x070A0B, 0x111719, 0x323333,
    0x7A503D, 0xA87250, 0xD89968, 0xF0B37E,
    0xFFD0A0, 0xA34F43, 0x71665B, 0x161C1E,
};

/* Page-local palette slots used only by the activity grid. */
static const uint32_t s_heat_palette_rgb[5] = {
    0x0D291E, 0x0E4429, 0x006D32, 0x26A641, 0x39D353,
};

static lv_obj_t *s_screen;
static lv_obj_t *s_canvas;
static LV_ATTRIBUTE_MEM_ALIGN uint8_t s_canvas_buffer[
    LV_DRAW_BUF_SIZE(UI_W, UI_H, LV_COLOR_FORMAT_I4)];
static buddy_ui_snapshot_t s_snapshot;
static bool s_have_snapshot;
static uint64_t s_elapsed_ms;

static uint8_t color_index(lv_color_t color)
{
    uint32_t rgb = lv_color_to_int(color);
    uint32_t best_distance = UINT32_MAX;
    uint8_t best = 0;
    uint8_t i;
    for (i = 0; i < 16; ++i) {
        int dr = (int)((rgb >> 16) & 0xffU) - (int)((s_palette_rgb[i] >> 16) & 0xffU);
        int dg = (int)((rgb >> 8) & 0xffU) - (int)((s_palette_rgb[i] >> 8) & 0xffU);
        int db = (int)(rgb & 0xffU) - (int)(s_palette_rgb[i] & 0xffU);
        uint32_t distance = (uint32_t)(dr * dr + dg * dg + db * db);
        if (distance < best_distance) {
            best_distance = distance;
            best = i;
        }
    }
    return best;
}

static void pixel(int x, int y, uint8_t index)
{
    if ((unsigned)x >= UI_W || (unsigned)y >= UI_H) return;
    buddy_i4_set_pixel(s_canvas_buffer + I4_PALETTE_BYTES, UI_W,
                       (uint16_t)x, (uint16_t)y, index);
}

static void box_index(int x, int y, int width, int height, uint8_t index)
{
    int px;
    int py;
    for (py = 0; py < height; ++py) {
        for (px = 0; px < width; ++px) pixel(x + px, y + py, index);
    }
}

static void box(int x, int y, int width, int height, lv_color_t color)
{
    box_index(x, y, width, height, color_index(color));
}

static void frame(int x, int y, int width, int height, lv_color_t color)
{
    box(x, y, width, 1, color);
    box(x, y + height - 1, width, 1, color);
    box(x, y, 1, height, color);
    box(x + width - 1, y, 1, height, color);
}

static int line_width(const lv_font_t *font, const char *value, size_t length, int spacing)
{
    int result = 0;
    size_t index;
    for (index = 0; index < length; ++index) {
        lv_font_glyph_dsc_t glyph_value;
        unsigned char character = (unsigned char)value[index];
        if (character < 0x80U &&
            lv_font_get_glyph_dsc(font, &glyph_value, character, 0)) {
            result += glyph_value.adv_w + spacing;
        }
    }
    return result > 0 ? result - spacing : 0;
}

static void glyph(int x, int y, uint8_t color, const lv_font_t *font,
                  unsigned char character)
{
    lv_font_glyph_dsc_t descriptor;
    const uint8_t *bitmap;
    unsigned row;
    unsigned column;
    if (!lv_font_get_glyph_dsc(font, &descriptor, character, 0) ||
        descriptor.box_w == 0 || descriptor.box_h == 0) return;
    descriptor.req_raw_bitmap = 1;
    bitmap = descriptor.resolved_font->get_glyph_bitmap(&descriptor, NULL);
    if (bitmap == NULL || descriptor.format != LV_FONT_GLYPH_FORMAT_A1) return;
    y += font->line_height - font->base_line - descriptor.box_h - descriptor.ofs_y;
    x += descriptor.ofs_x;
    for (row = 0; row < descriptor.box_h; ++row) {
        for (column = 0; column < descriptor.box_w; ++column) {
            uint32_t bit = row * descriptor.box_w + column;
            if ((bitmap[bit >> 3] & (0x80U >> (bit & 7U))) != 0U) {
                pixel(x + (int)column, y + (int)row, color);
            }
        }
    }
}

static void text(int x, int y, int width, lv_color_t color, const char *value,
                 bool large, lv_text_align_t align)
{
    const lv_font_t *font = large ? &lv_font_unscii_16 : &lv_font_unscii_8;
    int spacing = large ? 2 : 0;
    uint8_t color_value = color_index(color);
    size_t length = strlen(value);
    int pen = x;
    size_t index;
    int measured = line_width(font, value, length, spacing);
    if (align == LV_TEXT_ALIGN_CENTER) pen += (width - measured) / 2;
    else if (align == LV_TEXT_ALIGN_RIGHT) pen += width - measured;
    for (index = 0; index < length; ++index) {
        lv_font_glyph_dsc_t descriptor;
        unsigned char character = (unsigned char)value[index];
        if (character >= 0x80U) continue;
        glyph(pen, y, color_value, font, character);
        if (lv_font_get_glyph_dsc(font, &descriptor, character, 0)) {
            pen += descriptor.adv_w + spacing;
        }
        if (pen >= x + width) break;
    }
}

static void heading(const char *title, unsigned page)
{
    char marker[16];
    text(10, 35, 180, COL_MINT, title, true, LV_TEXT_ALIGN_LEFT);
    snprintf(marker, sizeof(marker), "%u/7", page);
    text(190, 39, 38, COL_MINT, marker, false, LV_TEXT_ALIGN_RIGHT);
    box(10, 61, 218, 1, COL_LINE);
}

static void footer(unsigned selected)
{
    unsigned index;
    for (index = 0; index < 7; ++index) {
        box(70 + (int)index * 15, 301, index == selected ? 9 : 5, 5,
            index == selected ? COL_MINT : COL_LINE);
    }
    text(8, 311, 224, COL_MINT, "UP / DOWN  PAGE", false, LV_TEXT_ALIGN_CENTER);
}

static void draw_status_bar(const buddy_ui_snapshot_t *snapshot)
{
    char right[32];
    uint64_t age_ms = s_elapsed_ms >= snapshot->time_received_ms
                          ? s_elapsed_ms - snapshot->time_received_ms : 0;
    time_t epoch = (time_t)(snapshot->epoch_seconds + snapshot->timezone_offset_seconds +
                            age_ms / 1000U);
    struct tm tm_value;
    text(8, 7, 120, snapshot->ble_encrypted ? COL_MINT : COL_DIM,
         snapshot->ble_encrypted ? "PASSPORT  SYNC" : "PASSPORT  READY",
         false, LV_TEXT_ALIGN_LEFT);
    if (snapshot->epoch_seconds > 0 && gmtime_r(&epoch, &tm_value) != NULL) {
        snprintf(right, sizeof(right), "%02d:%02d", tm_value.tm_hour, tm_value.tm_min);
    } else {
        snprintf(right, sizeof(right), "%s", snapshot->ble_connected ? "BLE" : "OFFLINE");
    }
    text(150, 7, 82, COL_MINT, right, false, LV_TEXT_ALIGN_RIGHT);
    box(8, 25, 224, 1, COL_LINE);
}

static void draw_avatar(int x, int y)
{
    int row;
    int column;
    frame(x - 2, y - 2, PASSPORT_AVATAR_W + 4, PASSPORT_AVATAR_H + 4, COL_MINT);
    for (row = 0; row < PASSPORT_AVATAR_H; ++row) {
        for (column = 0; column < PASSPORT_AVATAR_W; ++column) {
            size_t offset = (size_t)row * PASSPORT_AVATAR_W + (size_t)column;
            uint8_t packed = PASSPORT_AVATAR_I4[offset / 2U];
            uint8_t value = (offset & 1U) == 0U ? packed >> 4 : packed & 0x0fU;
            pixel(x + column, y + row, value);
        }
    }
}

static void draw_profile(const buddy_ui_snapshot_t *snapshot)
{
    const passport_profile_t *profile = &snapshot->passport.profile;
    const char *name = profile->valid ? profile->name : "TRAVELER";
    const char *tagline = profile->valid ? profile->tagline : "EXPLORING WITH AI";
    char identity[48];
    unsigned index;
    heading("AI PASSPORT", BUDDY_PAGE_PROFILE + 1U);
    text(14, 78, 154, COL_INK, name, true, LV_TEXT_ALIGN_LEFT);
    text(14, 103, 154, COL_MINT, tagline, false, LV_TEXT_ALIGN_LEFT);
    draw_avatar(178, 73);
    box(14, 136, 212, 1, COL_LINE);
    text(14, 150, 80, COL_MINT, "INTERESTS", false, LV_TEXT_ALIGN_LEFT);
    for (index = 0; index < PASSPORT_INTEREST_COUNT; ++index) {
        const char *interest = profile->valid ? profile->interests[index]
                              : (index == 0 ? "AI" : index == 1 ? "CODE" : "HARDWARE");
        int y = 172 + (int)index * 25;
        box(14, y - 5, 128, 19, COL_PANEL);
        text(22, y, 112, COL_INK, interest, false, LV_TEXT_ALIGN_LEFT);
    }
    text(155, 154, 68, COL_MINT, "IDENTITY", false, LV_TEXT_ALIGN_CENTER);
    snprintf(identity, sizeof(identity), "NO %s", profile->valid ? profile->number : "0001");
    text(155, 179, 68, COL_INK, identity, false, LV_TEXT_ALIGN_CENTER);
    snprintf(identity, sizeof(identity), "REV %s", profile->valid ? profile->revision : "A");
    text(155, 202, 68, COL_INK, identity, false, LV_TEXT_ALIGN_CENTER);
    text(14, 263, 212, COL_DIM, "ISSUED", false, LV_TEXT_ALIGN_LEFT);
    text(74, 263, 152, COL_INK, profile->valid ? profile->issued : "2026-01-01",
         false, LV_TEXT_ALIGN_RIGHT);
    footer(BUDDY_PAGE_PROFILE);
}

static void compact_u64(char *target, size_t size, uint64_t value)
{
    if (value >= UINT64_C(1000000000)) {
        snprintf(target, size, "%llu.%01lluB", (unsigned long long)(value / UINT64_C(1000000000)),
                 (unsigned long long)((value % UINT64_C(1000000000)) / UINT64_C(100000000)));
    } else if (value >= UINT64_C(1000000)) {
        snprintf(target, size, "%llu.%01lluM", (unsigned long long)(value / UINT64_C(1000000)),
                 (unsigned long long)((value % UINT64_C(1000000)) / UINT64_C(100000)));
    } else if (value >= UINT64_C(1000)) {
        snprintf(target, size, "%llu.%01lluK", (unsigned long long)(value / UINT64_C(1000)),
                 (unsigned long long)((value % UINT64_C(1000)) / UINT64_C(100)));
    } else {
        snprintf(target, size, "%llu", (unsigned long long)value);
    }
}

static void local_time(char *target, size_t size, int64_t epoch,
                       int32_t timezone_offset, bool include_date)
{
    time_t adjusted = (time_t)(epoch + timezone_offset);
    struct tm value;
    if (epoch <= 0 || gmtime_r(&adjusted, &value) == NULL) {
        snprintf(target, size, "--");
    } else if (include_date) {
        snprintf(target, size, "%02d/%02d %02d:%02d", value.tm_mon + 1, value.tm_mday,
                 value.tm_hour, value.tm_min);
    } else {
        snprintf(target, size, "%02d:%02d", value.tm_hour, value.tm_min);
    }
}

static void quota_bar(int x, int y, int width, uint8_t used)
{
    int fill = (width - 2) * (int)used / 100;
    frame(x, y, width, 10, COL_LINE);
    if (fill > 0) box(x + 1, y + 1, fill, 8, used >= 90U ? COL_INK : COL_MINT);
}

static void draw_cc(const buddy_ui_snapshot_t *snapshot)
{
    const passport_cc_usage_t *usage = &snapshot->passport.cc;
    const passport_cc_daily_t *daily = &snapshot->passport.cc_daily;
    char right[32];
    char reset[32];
    char tokens[24];
    char output[24];
    heading("KIMI QUOTA", BUDDY_PAGE_CC + 1U);
    if (!usage->valid) {
        text(16, 135, 208, COL_DIM, "NO KIMI QUOTA DATA", false, LV_TEXT_ALIGN_CENTER);
        text(16, 158, 208, COL_DIM, "RUN LOCAL SYNC", false, LV_TEXT_ALIGN_CENTER);
        footer(BUDDY_PAGE_CC);
        return;
    }
    text(15, 72, 210, COL_DIM, "CLAUDE CODE PROVIDER", false, LV_TEXT_ALIGN_LEFT);
    box(14, 86, 212, 29, COL_PANEL);
    text(22, 96, 196, COL_INK, usage->provider, false, LV_TEXT_ALIGN_LEFT);

    snprintf(right, sizeof(right), "%lu%% USED", (unsigned long)usage->five_hour_used);
    text(16, 126, 100, COL_INK, "KIMI 5H", false, LV_TEXT_ALIGN_LEFT);
    text(120, 126, 104, COL_MINT, right, false, LV_TEXT_ALIGN_RIGHT);
    quota_bar(16, 142, 208, (uint8_t)usage->five_hour_used);
    local_time(reset, sizeof(reset), (int64_t)usage->five_hour_resets_at,
               snapshot->timezone_offset_seconds, true);
    text(16, 158, 208, COL_DIM, "RESET", false, LV_TEXT_ALIGN_LEFT);
    text(116, 158, 108, COL_DIM, reset, false, LV_TEXT_ALIGN_RIGHT);

    snprintf(right, sizeof(right), "%lu%% USED", (unsigned long)usage->week_used);
    text(16, 185, 100, COL_INK, "KIMI 7D", false, LV_TEXT_ALIGN_LEFT);
    text(120, 185, 104, COL_MINT, right, false, LV_TEXT_ALIGN_RIGHT);
    quota_bar(16, 201, 208, (uint8_t)usage->week_used);
    local_time(reset, sizeof(reset), (int64_t)usage->week_resets_at,
               snapshot->timezone_offset_seconds, true);
    text(16, 217, 208, COL_DIM, "RESET", false, LV_TEXT_ALIGN_LEFT);
    text(116, 217, 108, COL_DIM, reset, false, LV_TEXT_ALIGN_RIGHT);

    box(16, 239, 208, 39, COL_PANEL);
    compact_u64(tokens, sizeof(tokens), daily->tokens);
    compact_u64(output, sizeof(output), daily->output_tokens);
    text(23, 247, 120, COL_INK, "TOKENS TODAY", false, LV_TEXT_ALIGN_LEFT);
    text(146, 247, 70, COL_MINT, tokens, false, LV_TEXT_ALIGN_RIGHT);
    snprintf(right, sizeof(right), "%lu / %s", (unsigned long)daily->requests, output);
    text(23, 263, 120, COL_DIM, "REQUESTS / OUT", false, LV_TEXT_ALIGN_LEFT);
    text(133, 263, 83, COL_MINT, right, false, LV_TEXT_ALIGN_RIGHT);

    local_time(right, sizeof(right), usage->updated_at,
               snapshot->timezone_offset_seconds, false);
    text(18, 284, 204, COL_DIM, "UPDATED", false, LV_TEXT_ALIGN_LEFT);
    text(145, 284, 77, COL_DIM, right, false, LV_TEXT_ALIGN_RIGHT);
    footer(BUDDY_PAGE_CC);
}

static void draw_codex(const buddy_ui_snapshot_t *snapshot)
{
    const passport_codex_quota_t *quota = &snapshot->passport.codex;
    char left[32];
    char right[32];
    char reset[32];
    heading("CODEX QUOTA", BUDDY_PAGE_CODEX + 1U);
    if (!quota->valid) {
        text(16, 135, 208, COL_DIM, "NO CODEX QUOTA DATA", false, LV_TEXT_ALIGN_CENTER);
        text(16, 158, 208, COL_DIM, "RUN LOCAL SYNC", false, LV_TEXT_ALIGN_CENTER);
        footer(BUDDY_PAGE_CODEX);
        return;
    }
    if (quota->main_window_mins >= 1440U) {
        snprintf(left, sizeof(left), "MAIN %luD", (unsigned long)(quota->main_window_mins / 1440U));
    } else {
        snprintf(left, sizeof(left), "MAIN %luH", (unsigned long)(quota->main_window_mins / 60U));
    }
    snprintf(right, sizeof(right), "%u%% USED", quota->main_used);
    text(16, 72, 100, COL_INK, left, false, LV_TEXT_ALIGN_LEFT);
    text(120, 72, 104, COL_MINT, right, false, LV_TEXT_ALIGN_RIGHT);
    quota_bar(16, 88, 208, quota->main_used);
    local_time(reset, sizeof(reset), quota->main_resets_at,
               snapshot->timezone_offset_seconds, true);
    text(16, 104, 208, COL_DIM, "RESET", false, LV_TEXT_ALIGN_LEFT);
    text(116, 104, 108, COL_DIM, reset, false, LV_TEXT_ALIGN_RIGHT);

    snprintf(right, sizeof(right), "%u%% USED", quota->spark_5h_used);
    text(16, 130, 100, COL_INK, "SPARK 5H", false, LV_TEXT_ALIGN_LEFT);
    text(120, 130, 104, COL_MINT, right, false, LV_TEXT_ALIGN_RIGHT);
    quota_bar(16, 146, 208, quota->spark_5h_used);
    local_time(reset, sizeof(reset), quota->spark_5h_resets_at,
               snapshot->timezone_offset_seconds, true);
    text(16, 162, 208, COL_DIM, "RESET", false, LV_TEXT_ALIGN_LEFT);
    text(116, 162, 108, COL_DIM, reset, false, LV_TEXT_ALIGN_RIGHT);

    snprintf(right, sizeof(right), "%u%% USED", quota->spark_week_used);
    text(16, 188, 100, COL_INK, "SPARK 7D", false, LV_TEXT_ALIGN_LEFT);
    text(120, 188, 104, COL_MINT, right, false, LV_TEXT_ALIGN_RIGHT);
    quota_bar(16, 204, 208, quota->spark_week_used);
    local_time(reset, sizeof(reset), quota->spark_week_resets_at,
               snapshot->timezone_offset_seconds, true);
    text(16, 220, 208, COL_DIM, "RESET", false, LV_TEXT_ALIGN_LEFT);
    text(116, 220, 108, COL_DIM, reset, false, LV_TEXT_ALIGN_RIGHT);

    box(16, 245, 208, 27, COL_PANEL);
    text(24, 255, 145, COL_INK, "RESET CREDITS", false, LV_TEXT_ALIGN_LEFT);
    snprintf(right, sizeof(right), "%u", quota->reset_credits);
    text(174, 255, 42, COL_MINT, right, false, LV_TEXT_ALIGN_RIGHT);
    local_time(right, sizeof(right), quota->updated_at,
               snapshot->timezone_offset_seconds, false);
    text(16, 280, 208, COL_DIM, "UPDATED", false, LV_TEXT_ALIGN_LEFT);
    text(146, 280, 78, COL_DIM, right, false, LV_TEXT_ALIGN_RIGHT);
    footer(BUDDY_PAGE_CODEX);
}

static void draw_focus(const buddy_ui_snapshot_t *snapshot)
{
    const passport_focus_t *focus = &snapshot->passport.focus;
    unsigned count = focus->valid ? focus->count : 0;
    unsigned index;
    heading("FOCUS", BUDDY_PAGE_FOCUS + 1U);
    text(15, 72, 132, COL_DIM, "TOPIC", false, LV_TEXT_ALIGN_LEFT);
    text(148, 72, 34, COL_DIM, "SESS", false, LV_TEXT_ALIGN_RIGHT);
    text(190, 72, 35, COL_DIM, "DAYS", false, LV_TEXT_ALIGN_RIGHT);
    for (index = 0; index < count; ++index) {
        const passport_focus_item_t *item = &focus->items[index];
        char value[12];
        int y = 99 + (int)index * 37;
        box(13, y - 10, 214, 29, (index & 1U) == 0U ? COL_PANEL : COL_BG);
        text(20, y, 122, COL_INK, item->topic, false, LV_TEXT_ALIGN_LEFT);
        snprintf(value, sizeof(value), "%u", item->sessions);
        text(148, y, 34, COL_MINT, value, false, LV_TEXT_ALIGN_RIGHT);
        snprintf(value, sizeof(value), "%u", item->days);
        text(190, y, 35, COL_MINT, value, false, LV_TEXT_ALIGN_RIGHT);
    }
    if (count == 0U) text(16, 132, 208, COL_DIM, "RUN LOCAL SYNC TO LOAD DATA",
                          false, LV_TEXT_ALIGN_CENTER);
    footer(BUDDY_PAGE_FOCUS);
}

static void draw_heat(const buddy_ui_snapshot_t *snapshot)
{
    const passport_heat_t *heat = &snapshot->passport.heat;
    const char *levels = heat->valid ? heat->all : NULL;
    char value[64];
    unsigned index;
    heading("13 WEEKS", BUDDY_PAGE_HEAT + 1U);
    text(15, 72, 78, COL_DIM, heat->valid ? heat->start : "NO DATA", false,
         LV_TEXT_ALIGN_LEFT);
    text(137, 72, 88, COL_DIM, "ACTIVITY", false, LV_TEXT_ALIGN_RIGHT);
    for (index = 0; index < PASSPORT_HEAT_DAYS; ++index) {
        unsigned column = index / 7U;
        unsigned row = index % 7U;
        unsigned level = levels != NULL ? (unsigned)(levels[index] - '0') : 0U;
        box_index(29 + (int)column * 14, 91 + (int)row * 14, 10, 10,
                  (uint8_t)(8U + (level <= 4U ? level : 0U)));
    }
    box(14, 198, 212, 1, COL_LINE);
    snprintf(value, sizeof(value), "ACTIVE DAYS   %lu", (unsigned long)heat->summary.days);
    text(18, 214, 204, COL_INK, value, false, LV_TEXT_ALIGN_LEFT);
    snprintf(value, sizeof(value), "STREAK        %lu", (unsigned long)heat->summary.streak);
    text(18, 235, 204, COL_INK, value, false, LV_TEXT_ALIGN_LEFT);
    snprintf(value, sizeof(value), "TOKENS        %llu", (unsigned long long)heat->summary.tokens);
    text(18, 256, 204, COL_MINT, value, false, LV_TEXT_ALIGN_LEFT);
    footer(BUDDY_PAGE_HEAT);
}

static void draw_stamps(const buddy_ui_snapshot_t *snapshot)
{
    const passport_stamps_t *stamps = &snapshot->passport.stamps;
    unsigned count = stamps->valid ? stamps->count : 0;
    unsigned index;
    heading("STAMPS", BUDDY_PAGE_STAMPS + 1U);
    for (index = 0; index < count; ++index) {
        const passport_stamp_t *stamp = &stamps->items[index];
        int y = 82 + (int)index * 42;
        frame(14, y, 212, 32, index == 0U ? COL_MINT : COL_LINE);
        box(20, y + 7, 10, 18, index == 0U ? COL_MINT : COL_LINE);
        text(40, y + 11, 104, COL_INK, stamp->topic, false, LV_TEXT_ALIGN_LEFT);
        text(146, y + 11, 72, COL_DIM, stamp->date, false, LV_TEXT_ALIGN_RIGHT);
    }
    if (count == 0U) text(16, 132, 208, COL_DIM, "NO STAMPS YET", false,
                          LV_TEXT_ALIGN_CENTER);
    footer(BUDDY_PAGE_STAMPS);
}

static void draw_qr(int x, int y, int scale)
{
    const int quiet = 4;
    int size = (PASSPORT_QR_SIZE + quiet * 2) * scale;
    int row;
    int column;
    /* True dark-mode QR: bright modules float on the page background. */
    box(x, y, size, size, COL_BG);
    for (row = 0; row < PASSPORT_QR_SIZE; ++row) {
        for (column = 0; column < PASSPORT_QR_SIZE; ++column) {
            uint64_t mask = UINT64_C(1) << (PASSPORT_QR_SIZE - 1 - column);
            if ((passport_qr_rows[row] & mask) != 0U) {
                box(x + (column + quiet) * scale, y + (row + quiet) * scale,
                    scale, scale, COL_MINT);
            }
        }
    }
}

static void draw_links(void)
{
    heading("LINKS", BUDDY_PAGE_LINKS + 1U);
    draw_qr(27, 70, 5);
    text(12, 272, 216, COL_MINT, "PROJECT REPOSITORY", false, LV_TEXT_ALIGN_CENTER);
    text(12, 286, 216, COL_DIM, "SCAN TO OPEN", false, LV_TEXT_ALIGN_CENTER);
    footer(BUDDY_PAGE_LINKS);
}

static void draw_pairing(const buddy_ui_snapshot_t *snapshot)
{
    char passkey[32];
    int x;
    int y;
    for (y = 28; y < 299; ++y) {
        for (x = y & 1; x < UI_W; x += 2) pixel(x, y, 15);
    }
    box(18, 82, 204, 148, COL_PANEL);
    frame(18, 82, 204, 148, COL_MINT);
    text(28, 101, 184, COL_MINT, "BLUETOOTH PAIRING", true, LV_TEXT_ALIGN_CENTER);
    text(28, 139, 184, COL_INK, "ENTER THIS CODE ON YOUR MAC", false,
         LV_TEXT_ALIGN_CENTER);
    snprintf(passkey, sizeof(passkey), "%06lu", (unsigned long)snapshot->passkey);
    text(28, 166, 184, COL_INK, passkey, true, LV_TEXT_ALIGN_CENTER);
    text(28, 205, 184, COL_DIM, "KEEP THIS SCREEN OPEN", false,
         LV_TEXT_ALIGN_CENTER);
}

static void redraw(void)
{
    unsigned index;
    if (s_canvas == NULL || !s_have_snapshot) return;
    for (index = 0; index < 16; ++index) {
        uint32_t rgb = s_snapshot.page == BUDDY_PAGE_HEAT && index >= 8U && index <= 12U
                           ? s_heat_palette_rgb[index - 8U]
                           : s_palette_rgb[index];
        lv_canvas_set_palette(s_canvas, index,
                              lv_color_to_32(lv_color_hex(rgb), LV_OPA_COVER));
    }
    memset(s_canvas_buffer + I4_PALETTE_BYTES, 0,
           sizeof(s_canvas_buffer) - I4_PALETTE_BYTES);
    draw_status_bar(&s_snapshot);
    switch (s_snapshot.page) {
    case BUDDY_PAGE_CC: draw_cc(&s_snapshot); break;
    case BUDDY_PAGE_CODEX: draw_codex(&s_snapshot); break;
    case BUDDY_PAGE_FOCUS: draw_focus(&s_snapshot); break;
    case BUDDY_PAGE_HEAT: draw_heat(&s_snapshot); break;
    case BUDDY_PAGE_STAMPS: draw_stamps(&s_snapshot); break;
    case BUDDY_PAGE_LINKS: draw_links(); break;
    default: draw_profile(&s_snapshot); break;
    }
    if (s_snapshot.passkey_visible) draw_pairing(&s_snapshot);
    lv_obj_invalidate(s_canvas);
}

void buddy_ui_init(void)
{
    unsigned index;
    if (s_screen != NULL) return;
    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, UI_W, UI_H);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_bg_color(s_screen, COL_BG, 0);
    s_canvas = lv_canvas_create(s_screen);
    lv_canvas_set_buffer(s_canvas, s_canvas_buffer, UI_W, UI_H, LV_COLOR_FORMAT_I4);
    lv_obj_set_pos(s_canvas, 0, 0);
    for (index = 0; index < 16; ++index) {
        lv_canvas_set_palette(s_canvas, index,
                              lv_color_to_32(lv_color_hex(s_palette_rgb[index]),
                                             LV_OPA_COVER));
    }
    lv_screen_load(s_screen);
}

void buddy_ui_render(const buddy_ui_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;
    buddy_ui_init();
    s_snapshot = *snapshot;
    s_have_snapshot = true;
    redraw();
}

void buddy_ui_show_passkey(uint32_t passkey)
{
    buddy_ui_snapshot_t snapshot = {.passkey_visible = true, .passkey = passkey};
    buddy_ui_render(&snapshot);
}

void buddy_ui_tick(uint64_t elapsed_ms)
{
    s_elapsed_ms = elapsed_ms;
    redraw();
}

void buddy_ui_scroll(int delta)
{
    (void)delta;
}
