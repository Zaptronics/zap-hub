// web_assets.h
#pragma once
#include <stddef.h>
#include <stdint.h>

extern const unsigned char *INDEX_HTML;
extern size_t  INDEX_HTML_LEN;

extern const unsigned char APP_JS[];
extern const size_t  APP_JS_LEN;

void web_assets_init();
