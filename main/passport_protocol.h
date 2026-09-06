#pragma once

#include <stdbool.h>

#include "cJSON.h"
#include "buddy_types.h"

bool passport_protocol_parse(const cJSON *root, const char *command,
                             buddy_event_t *event);
