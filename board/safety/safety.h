#pragma once

// Keep the upstream implementation available behind the local selector.
#define set_safety_hooks opendbc_set_safety_hooks
#include "opendbc/safety/safety.h"
// cppcheck-suppress misra-c2012-20.5 ; end the temporary upstream function alias
#undef set_safety_hooks
#include "board/safety/declarations.h"
#include "board/safety/modes/richie.h"

int set_panda_safety_hooks(uint16_t mode, uint16_t param) {
  int status;
  // Only expose Richie and passive upstream modes; treat all other modes as unknown.
  if (mode == SAFETY_RICHIE) {
    // Reuse upstream's complete state reset before installing local hooks.
    // safety.h is included in this translation unit, so its static hook pointer
    // is accessible without modifying the pinned opendbc dependency.
    status = opendbc_set_safety_hooks(SAFETY_NOOUTPUT, 0U);
    if (status == 0) {
      current_hooks = &richie_hooks;
      current_safety_config = richie_hooks.init(param);
      current_safety_mode = mode;
      current_safety_param = param;
    }
  } else if ((mode == SAFETY_NOOUTPUT) ||
             (mode == SAFETY_SILENT) ||
             (mode == SAFETY_ELM327)) {
    status = opendbc_set_safety_hooks(mode, param);
  } else {
    status = -1;
  }
  return status;
}

// Preserve the existing application call sites without sharing a C identifier
// with the macro used to rename the upstream implementation.
#define set_safety_hooks set_panda_safety_hooks
