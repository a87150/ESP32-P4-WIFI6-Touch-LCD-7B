#pragma once

/*
 * LVGL v8 namespace compatibility for the Waveshare apps sources.
 *
 * LVGL v9.2 already handles types and function aliases (lv_img_dsc_t,
 * LV_IMG_DECLARE, lv_img_set_src, ...) via lv_api_map_v8.h.  This header adds
 * the few remaining v8 identifiers that have no v9 counterpart under the same
 * name.  Auto-injected via -include into every apps source file.
 */

#include "lvgl.h"

#ifndef LV_IMG_CF_TRUE_COLOR
#define LV_IMG_CF_TRUE_COLOR        LV_COLOR_FORMAT_RGB565
#endif

#ifndef LV_IMG_CF_TRUE_COLOR_ALPHA
#define LV_IMG_CF_TRUE_COLOR_ALPHA  LV_COLOR_FORMAT_RGB565A8
#endif

#ifndef LV_IMG_PX_SIZE_ALPHA_BYTE
#define LV_IMG_PX_SIZE_ALPHA_BYTE   3
#endif

#ifndef LV_IMG_ZOOM_NONE
#define LV_IMG_ZOOM_NONE            LV_SCALE_NONE
#endif
