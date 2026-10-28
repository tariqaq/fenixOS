/*******************************************************************************
 * Size: 10 px
 * Bpp: 4
 * Opts: --font design/fonts/phosphor/Phosphor-Bold.ttf -r 0xe138,0xe13a,0xe182,0xe3d4,0xe4f6 --size 10 --bpp 4 --format lvgl --no-compress --lv-include lvgl.h --lv-font-name fx_icon_bold_10 -o board/components/fenix_ui/fonts/fx_icon_bold_10.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef FX_ICON_BOLD_10
#define FX_ICON_BOLD_10 1
#endif

#if FX_ICON_BOLD_10

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+E138 "" */
    0x0, 0x6, 0x90, 0x6, 0xd1, 0x6, 0xd1, 0x3,
    0xf1, 0x0, 0xa, 0xa0, 0x0, 0xa, 0xa0, 0x0,
    0xa, 0x80, 0x0, 0x1,

    /* U+E13A "" */
    0x96, 0x0, 0x1, 0xd6, 0x0, 0x1, 0xd6, 0x0,
    0x1, 0xf3, 0x0, 0xaa, 0x0, 0xaa, 0x0, 0x8a,
    0x0, 0x1, 0x0, 0x0,

    /* U+E182 "" */
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1,
    0xd1, 0x0, 0x0, 0x1, 0xd5, 0x1, 0x0, 0x1,
    0xd5, 0x0, 0xb7, 0x1, 0xd5, 0x0, 0x0, 0xc8,
    0xd5, 0x0, 0x0, 0x0, 0xb5, 0x0, 0x0, 0x0,

    /* U+E3D4 "" */
    0x0, 0x0, 0x0, 0x0, 0x0, 0x7, 0x70, 0x0,
    0x0, 0x7, 0x70, 0x0, 0x0, 0x7, 0x70, 0x0,
    0x8b, 0xbd, 0xdb, 0xb8, 0x23, 0x39, 0x93, 0x32,
    0x0, 0x7, 0x70, 0x0, 0x0, 0x7, 0x70, 0x0,
    0x0, 0x3, 0x30, 0x0,

    /* U+E4F6 "" */
    0x17, 0x0, 0x0, 0x62, 0xb, 0x90, 0x7, 0xc0,
    0x0, 0xb9, 0x7c, 0x0, 0x0, 0xd, 0xe0, 0x0,
    0x0, 0x7c, 0xb9, 0x0, 0x7, 0xc0, 0xb, 0x90,
    0x1b, 0x0, 0x0, 0xa3
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 160, .box_w = 5, .box_h = 8, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 20, .adv_w = 160, .box_w = 5, .box_h = 8, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 40, .adv_w = 160, .box_w = 9, .box_h = 7, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 72, .adv_w = 160, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 108, .adv_w = 160, .box_w = 8, .box_h = 7, .ofs_x = 1, .ofs_y = 1}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0x2, 0x4a, 0x29c, 0x3be
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 57656, .range_length = 959, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 5, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t fx_icon_bold_10 = {
#else
lv_font_t fx_icon_bold_10 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 9,          /*The maximum line height required by the font*/
    .base_line = 0,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = 0,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if FX_ICON_BOLD_10*/

