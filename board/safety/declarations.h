#pragma once

#include "opendbc/safety/can.h"
#include "opendbc/safety/declarations.h"

// Panda-local mode IDs, separate from opendbc's CarParams.SafetyModel enum.
#define SAFETY_RICHIE 35U

int set_panda_safety_hooks(uint16_t mode, uint16_t param);
extern const safety_hooks richie_hooks;
