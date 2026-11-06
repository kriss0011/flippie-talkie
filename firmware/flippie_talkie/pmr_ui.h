#pragma once
#include <gui/gui.h>
#include <gui/elements.h>
#include <stdio.h>

#define FLIPPIE_TALKIE_VERSION "0.10"

// All geometry is native 128 x 64. No scaled fonts or simulated signal traces.
static inline void pmr_ui_header(Canvas* c, const char* title, const char* badge) {
    canvas_clear(c);
    canvas_set_color(c, ColorBlack);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 3, 10, title);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str_aligned(c, 125, 10, AlignRight, AlignBottom, badge);
    canvas_draw_line(c, 0, 13, 127, 13);
}
// Rows use the full width unless a scrollbar needs the right edge.
static inline void pmr_ui_row(
    Canvas* c,
    uint32_t row,
    bool selected,
    bool scrollbar,
    const char* label,
    const char* value) {
    uint32_t y = 15 + row * 12, width = scrollbar ? 123 : 128;
    if(selected) {
        canvas_draw_rbox(c, 0, y, width, 12, 2);
        canvas_set_color(c, ColorWhite);
    }
    canvas_set_font(c, FontSecondary);
    canvas_draw_str(c, 5, y + 9, label);
    if(value) canvas_draw_str_aligned(c, width - 5, y + 9, AlignRight, AlignBottom, value);
    canvas_set_color(c, ColorBlack);
}
static inline void pmr_ui_scroll(Canvas* c, uint32_t index, uint32_t count) {
    if(count > 4) elements_scrollbar_pos(c, 128, 15, 48, index, count);
}
static inline void pmr_ui_radio(
    Canvas* c,
    const char* title,
    const char* badge,
    uint32_t channel,
    int32_t rssi,
    const char* state,
    const char* detail) {
    char text[40];
    pmr_ui_header(c, title, badge);
    uint32_t hz = 446006250 + (channel - 1) * 12500;
    snprintf(text, sizeof(text), "%lu.%05lu MHz", hz / 1000000, (hz % 1000000) / 10);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str(c, 3, 24, text);
    canvas_draw_rframe(c, 0, 27, 42, 23, 3);
    snprintf(text, sizeof(text), "%02lu", channel);
    canvas_set_font(c, FontBigNumbers);
    canvas_draw_str_aligned(c, 21, 46, AlignCenter, AlignBottom, text);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 47, 36, state);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str(c, 47, 47, detail);
    // Segmented RSSI meter: actual measured dBm, with an explicit no-reading state.
    uint32_t bars = rssi <= -120 ? 0 : rssi >= -55 ? 9 : (uint32_t)(rssi + 120) * 9 / 65;
    for(uint32_t i = 0; i < 9; i++) {
        uint32_t h = 2 + i / 3;
        if(i < bars)
            canvas_draw_box(c, 92 + i * 4, 24 - h, 2, h);
        else
            canvas_draw_dot(c, 92 + i * 4, 23);
    }
}
