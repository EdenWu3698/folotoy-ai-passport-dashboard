#include "passport_protocol.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static bool copy_required(const cJSON *object, const char *key,
                          char *target, size_t capacity, bool allow_empty)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    size_t length;

    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }
    length = strlen(item->valuestring);
    if (length >= capacity || (!allow_empty && length == 0U)) {
        return false;
    }
    memcpy(target, item->valuestring, length + 1U);
    return true;
}

static bool copy_json_string(const cJSON *item, char *target, size_t capacity)
{
    size_t length;

    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }
    length = strlen(item->valuestring);
    if (length == 0U || length >= capacity) {
        return false;
    }
    memcpy(target, item->valuestring, length + 1U);
    return true;
}

static bool unsigned_value(const cJSON *object, const char *key,
                           uint64_t maximum, uint64_t *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    uint64_t parsed;

    if (!cJSON_IsNumber(item) || item->valuedouble < 0.0 ||
        item->valuedouble > (double)maximum) {
        return false;
    }
    parsed = (uint64_t)item->valuedouble;
    if ((double)parsed != item->valuedouble) {
        return false;
    }
    *value = parsed;
    return true;
}

static bool heat_levels(const cJSON *item, char target[PASSPORT_HEAT_DAYS + 1])
{
    size_t index;

    if (!cJSON_IsString(item) || item->valuestring == NULL ||
        strlen(item->valuestring) != PASSPORT_HEAT_DAYS) {
        return false;
    }
    for (index = 0; index < PASSPORT_HEAT_DAYS; ++index) {
        if (item->valuestring[index] < '0' || item->valuestring[index] > '4') {
            return false;
        }
    }
    memcpy(target, item->valuestring, PASSPORT_HEAT_DAYS + 1U);
    return true;
}

static bool parse_profile(const cJSON *root, buddy_event_t *event)
{
    passport_profile_t *profile = &event->passport.profile;
    const cJSON *interests = cJSON_GetObjectItemCaseSensitive(root, "into");
    const cJSON *links = cJSON_GetObjectItemCaseSensitive(root, "links");
    int count;
    int index;

    if (!copy_required(root, "name", profile->name, sizeof(profile->name), false) ||
        !copy_required(root, "tagline", profile->tagline, sizeof(profile->tagline), true) ||
        !copy_required(root, "rev", profile->revision, sizeof(profile->revision), false) ||
        !copy_required(root, "no", profile->number, sizeof(profile->number), false) ||
        !copy_required(root, "issued", profile->issued, sizeof(profile->issued), false) ||
        !cJSON_IsArray(interests) || !cJSON_IsArray(links)) {
        return false;
    }
    count = cJSON_GetArraySize(interests);
    if (count != PASSPORT_INTEREST_COUNT) {
        return false;
    }
    for (index = 0; index < count; ++index) {
        if (!copy_json_string(cJSON_GetArrayItem(interests, index),
                              profile->interests[index],
                              sizeof(profile->interests[index]))) {
            return false;
        }
    }
    count = cJSON_GetArraySize(links);
    if (count < 0 || count > PASSPORT_LINK_MAX) {
        return false;
    }
    for (index = 0; index < count; ++index) {
        const cJSON *link = cJSON_GetArrayItem(links, index);
        if (!cJSON_IsObject(link) ||
            !copy_required(link, "l", profile->links[index].label,
                           sizeof(profile->links[index].label), false) ||
            !copy_required(link, "u", profile->links[index].url,
                           sizeof(profile->links[index].url), false)) {
            return false;
        }
    }
    profile->link_count = (uint8_t)count;
    profile->valid = true;
    event->type = BUDDY_EVENT_PROFILE;
    return true;
}

static bool parse_focus(const cJSON *root, buddy_event_t *event)
{
    passport_focus_t *focus = &event->passport.focus;
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "items");
    int count;
    int index;

    if (!cJSON_IsArray(items)) {
        return false;
    }
    count = cJSON_GetArraySize(items);
    if (count < 0 || count > PASSPORT_TOPIC_MAX) {
        return false;
    }
    for (index = 0; index < count; ++index) {
        const cJSON *item = cJSON_GetArrayItem(items, index);
        uint64_t sessions;
        uint64_t days;
        if (!cJSON_IsObject(item) ||
            !copy_required(item, "t", focus->items[index].topic,
                           sizeof(focus->items[index].topic), false) ||
            !unsigned_value(item, "c", 15U, &sessions) ||
            !unsigned_value(item, "d", 30U, &days)) {
            return false;
        }
        focus->items[index].sessions = (uint8_t)sessions;
        focus->items[index].days = (uint8_t)days;
    }
    focus->count = (uint8_t)count;
    focus->valid = true;
    event->type = BUDDY_EVENT_FOCUS;
    return true;
}

static bool parse_heat(const cJSON *root, buddy_event_t *event)
{
    passport_heat_t *heat = &event->passport.heat;
    const cJSON *all = cJSON_GetObjectItemCaseSensitive(root, "all");
    const cJSON *themes = cJSON_GetObjectItemCaseSensitive(root, "themes");
    const cJSON *summary = cJSON_GetObjectItemCaseSensitive(root, "sum");
    const cJSON *item;
    uint64_t value;

    if (!copy_required(root, "start", heat->start, sizeof(heat->start), false) ||
        !heat_levels(all, heat->all) || !cJSON_IsObject(themes) ||
        !cJSON_IsObject(summary)) {
        return false;
    }
    cJSON_ArrayForEach(item, themes) {
        passport_heat_theme_t *theme;
        size_t topic_length;
        if (heat->theme_count >= PASSPORT_TOPIC_MAX || item->string == NULL) {
            return false;
        }
        theme = &heat->themes[heat->theme_count];
        topic_length = strlen(item->string);
        if (topic_length == 0U || topic_length >= sizeof(theme->topic) ||
            !heat_levels(item, theme->levels)) {
            return false;
        }
        memcpy(theme->topic, item->string, topic_length + 1U);
        ++heat->theme_count;
    }
    if (!unsigned_value(summary, "days", UINT32_MAX, &value)) return false;
    heat->summary.days = (uint32_t)value;
    if (!unsigned_value(summary, "streak", UINT32_MAX, &value)) return false;
    heat->summary.streak = (uint32_t)value;
    if (!unsigned_value(summary, "tok", UINT64_C(9007199254740991), &heat->summary.tokens) ||
        !unsigned_value(summary, "week", UINT64_C(9007199254740991), &heat->summary.week) ||
        !unsigned_value(summary, "today", UINT64_C(9007199254740991), &heat->summary.today) ||
        !unsigned_value(summary, "total", UINT64_C(9007199254740991), &heat->summary.total)) {
        return false;
    }
    heat->valid = true;
    event->type = BUDDY_EVENT_HEAT;
    return true;
}

static bool parse_stamps(const cJSON *root, buddy_event_t *event)
{
    passport_stamps_t *stamps = &event->passport.stamps;
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "items");
    int count;
    int index;

    if (!cJSON_IsArray(items)) {
        return false;
    }
    count = cJSON_GetArraySize(items);
    if (count < 0 || count > PASSPORT_TOPIC_MAX) {
        return false;
    }
    for (index = 0; index < count; ++index) {
        const cJSON *item = cJSON_GetArrayItem(items, index);
        if (!cJSON_IsObject(item) ||
            !copy_required(item, "t", stamps->items[index].topic,
                           sizeof(stamps->items[index].topic), false) ||
            !copy_required(item, "d", stamps->items[index].date,
                           sizeof(stamps->items[index].date), false)) {
            return false;
        }
    }
    stamps->count = (uint8_t)count;
    stamps->valid = true;
    event->type = BUDDY_EVENT_STAMPS;
    return true;
}

static bool parse_cc(const cJSON *root, buddy_event_t *event)
{
    passport_cc_usage_t *usage = &event->passport.cc;
    passport_cc_daily_t *daily = &event->passport.cc_daily;
    const cJSON *five = cJSON_GetObjectItemCaseSensitive(root, "five");
    const cJSON *week = cJSON_GetObjectItemCaseSensitive(root, "week");
    const cJSON *today = cJSON_GetObjectItemCaseSensitive(root, "today");
    uint64_t value;

    if (!copy_required(root, "provider", usage->provider, sizeof(usage->provider), false) ||
        !cJSON_IsObject(five) || !cJSON_IsObject(week) || !cJSON_IsObject(today) ||
        !unsigned_value(five, "used", 100U, &value)) {
        return false;
    }
    usage->five_hour_used = (uint32_t)value;
    if (!unsigned_value(five, "reset", INT64_MAX, &value)) return false;
    usage->five_hour_resets_at = value;
    if (!unsigned_value(week, "used", 100U, &value)) return false;
    usage->week_used = (uint32_t)value;
    if (!unsigned_value(week, "reset", INT64_MAX, &value)) return false;
    usage->week_resets_at = value;
    if (!unsigned_value(today, "tok", UINT64_C(9007199254740991), &daily->tokens) ||
        !unsigned_value(today, "out", UINT64_C(9007199254740991), &daily->output_tokens) ||
        !unsigned_value(today, "req", UINT32_MAX, &value)) {
        return false;
    }
    daily->requests = (uint32_t)value;
    daily->valid = true;
    if (!unsigned_value(root, "updated", INT64_MAX, &value)) return false;
    usage->updated_at = (int64_t)value;
    if (!unsigned_value(root, "ok", 1U, &value)) return false;
    usage->valid = value != 0U;
    event->type = BUDDY_EVENT_CC;
    return true;
}

static bool parse_codex(const cJSON *root, buddy_event_t *event)
{
    passport_codex_quota_t *quota = &event->passport.codex;
    const cJSON *main = cJSON_GetObjectItemCaseSensitive(root, "main");
    const cJSON *spark = cJSON_GetObjectItemCaseSensitive(root, "spark");
    uint64_t value;

    if (!cJSON_IsObject(main) || !cJSON_IsObject(spark) ||
        !unsigned_value(main, "used", 100U, &value)) {
        return false;
    }
    quota->main_used = (uint8_t)value;
    if (!unsigned_value(main, "mins", UINT32_MAX, &value)) return false;
    quota->main_window_mins = (uint32_t)value;
    if (!unsigned_value(main, "reset", INT64_MAX, &value)) return false;
    quota->main_resets_at = (int64_t)value;
    if (!unsigned_value(spark, "used", 100U, &value)) return false;
    quota->spark_5h_used = (uint8_t)value;
    if (!unsigned_value(spark, "reset", INT64_MAX, &value)) return false;
    quota->spark_5h_resets_at = (int64_t)value;
    if (!unsigned_value(spark, "week_used", 100U, &value)) return false;
    quota->spark_week_used = (uint8_t)value;
    if (!unsigned_value(spark, "week_reset", INT64_MAX, &value)) return false;
    quota->spark_week_resets_at = (int64_t)value;
    if (!unsigned_value(root, "credits", UINT8_MAX, &value)) return false;
    quota->reset_credits = (uint8_t)value;
    if (!unsigned_value(root, "updated", INT64_MAX, &value)) return false;
    quota->updated_at = (int64_t)value;
    if (!unsigned_value(root, "ok", 1U, &value)) return false;
    quota->valid = value != 0U;
    event->type = BUDDY_EVENT_CODEX;
    return true;
}

bool passport_protocol_parse(const cJSON *root, const char *command,
                             buddy_event_t *event)
{
    if (strcmp(command, "profile") == 0) return parse_profile(root, event);
    if (strcmp(command, "focus") == 0) return parse_focus(root, event);
    if (strcmp(command, "heat") == 0) return parse_heat(root, event);
    if (strcmp(command, "stamps") == 0) return parse_stamps(root, event);
    if (strcmp(command, "cc") == 0) return parse_cc(root, event);
    if (strcmp(command, "codex") == 0) return parse_codex(root, event);
    return false;
}
