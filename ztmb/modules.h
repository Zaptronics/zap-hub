#pragma once

#include "zaptronics/services/registry.h"
/*
 * When rollback_on_fail is true, initialization stops at the first failure and
 * previously initialized modules are deinitialized where possible.
 *
 * When false, every module is attempted independently. The first non-zero
 * return code is returned after the pass, while successfully initialized
 * modules remain available for console diagnostics and incremental testing.
 */
int modules_init_all(bool rollback_on_fail);
int modules_poll_all(void);
int modules_deinit_all(void);
module_entry_t *modules_find_by_short_name(const char *short_name);
int modules_reinitialize_by_short_name(const char *short_name);
int modules_deinitialize_by_short_name(const char *short_name);

/* Read-only registry inspection for diagnostics and console tooling. */
size_t modules_count(void);
const module_entry_t *modules_get(size_t index);
bool modules_is_initialized(size_t index);
