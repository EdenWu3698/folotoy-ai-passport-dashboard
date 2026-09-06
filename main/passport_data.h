#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PASSPORT_TEXT_MAX 40
#define PASSPORT_TAGLINE_MAX 64
#define PASSPORT_URL_MAX 128
#define PASSPORT_INTEREST_COUNT 3
#define PASSPORT_LINK_MAX 3
#define PASSPORT_TOPIC_MAX 5
#define PASSPORT_HEAT_DAYS 91

typedef struct {
    char label[16];
    char url[PASSPORT_URL_MAX];
} passport_link_t;

typedef struct {
    char name[PASSPORT_TEXT_MAX];
    char tagline[PASSPORT_TAGLINE_MAX];
    char interests[PASSPORT_INTEREST_COUNT][PASSPORT_TEXT_MAX];
    passport_link_t links[PASSPORT_LINK_MAX];
    uint8_t link_count;
    char revision[8];
    char number[16];
    char issued[16];
    bool valid;
} passport_profile_t;

typedef struct {
    char topic[PASSPORT_TEXT_MAX];
    uint8_t sessions;
    uint8_t days;
} passport_focus_item_t;

typedef struct {
    passport_focus_item_t items[PASSPORT_TOPIC_MAX];
    uint8_t count;
    bool valid;
} passport_focus_t;

typedef struct {
    char topic[PASSPORT_TEXT_MAX];
    char levels[PASSPORT_HEAT_DAYS + 1];
} passport_heat_theme_t;

typedef struct {
    uint32_t days;
    uint32_t streak;
    uint64_t tokens;
    uint64_t week;
    uint64_t today;
    uint64_t total;
} passport_heat_summary_t;

typedef struct {
    char start[16];
    char all[PASSPORT_HEAT_DAYS + 1];
    passport_heat_theme_t themes[PASSPORT_TOPIC_MAX];
    uint8_t theme_count;
    passport_heat_summary_t summary;
    bool valid;
} passport_heat_t;

typedef struct {
    char topic[PASSPORT_TEXT_MAX];
    char date[16];
} passport_stamp_t;

typedef struct {
    passport_stamp_t items[PASSPORT_TOPIC_MAX];
    uint8_t count;
    bool valid;
} passport_stamps_t;

typedef struct {
    char provider[24];
    uint32_t five_hour_used;
    uint64_t five_hour_resets_at;
    uint64_t week_resets_at;
    uint32_t week_used;
    int64_t updated_at;
    uint8_t reserved;
    bool valid;
} passport_cc_usage_t;

typedef struct {
    int64_t main_resets_at;
    int64_t spark_5h_resets_at;
    int64_t spark_week_resets_at;
    int64_t updated_at;
    uint32_t main_window_mins;
    uint8_t main_used;
    uint8_t spark_5h_used;
    uint8_t spark_week_used;
    uint8_t reset_credits;
    bool valid;
} passport_codex_quota_t;

typedef struct {
    uint64_t tokens;
    uint64_t output_tokens;
    uint32_t requests;
    bool valid;
} passport_cc_daily_t;

typedef struct {
    passport_profile_t profile;
    passport_focus_t focus;
    passport_heat_t heat;
    passport_stamps_t stamps;
    passport_cc_usage_t cc;
    passport_codex_quota_t codex;
    passport_cc_daily_t cc_daily;
} passport_data_t;
