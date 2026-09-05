#ifndef FETCH_CORE_COMMON_H
#define FETCH_CORE_COMMON_H

#include <stddef.h>

// Text alignments
enum v_alignment {
  V_ALIGN_TOP,
  V_ALIGN_CENTER,
  V_ALIGN_BOTTOM
};

enum h_alignment {
  H_ALIGN_LEFT,
  H_ALIGN_CENTER,
  H_ALIGN_RIGHT
};

// Returns byte length of a UTF-8 sequence from its leading byte (1..4)
int utf8_char_len(unsigned char c);

// Skip past an ANSI escape sequence (ESC [ ... letter)
// Returns number of bytes to skip, or 0 if not an escape
int skip_ansi(const char *p);

// Check if an ANSI escape is a cursor movement (not a color/SGR escape)
int is_cursor_escape(const char *p);

// Strip a trailing " (...)" documentation hint, e.g. from a config value
// like "white (red, green, yellow, ...)" -> "white". Also trims any
// trailing whitespace left after the cut.
void strip_inline_hint(char *val);

// Visible columns of a string, ignoring ANSI escapes (codepoint = 1 column)
int visible_width(const char *s);

// Copy s into p clipped to max_cols visible columns (ANSI passes through,
// max_cols < 0 = no limit). Appends a reset if the clip cut a color short.
char *emit_clipped(char *p, char *end, const char *s, int max_cols);

#endif // FETCH_CORE_COMMON_H
