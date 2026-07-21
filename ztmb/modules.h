#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int (*module_init_fn_t)(void *mod_inst, void *bus_ctx, int8_t addr, int8_t en_pin);
typedef int (*module_poll_fn_t)(void);
typedef int (*module_deinit_fn_t)(void);

typedef struct module_entry {
    const char         *short_name;
    const char         *long_name;
    module_init_fn_t    init;
    void               *bus_ctx;
    void               *mod_inst;
    module_poll_fn_t    poll;
    module_deinit_fn_t  deinit;
    int8_t              addr;
    int8_t              en_pin;
} module_entry_t;

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

/* Read-only registry inspection for diagnostics and console tooling. */
size_t modules_count(void);
const module_entry_t *modules_get(size_t index);
bool modules_is_initialized(size_t index);
