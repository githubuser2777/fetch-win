#include "common.h"
#include <string.h>

int utf8_char_len(unsigned char c) {
  if (c < 0x80)
    return 1;
  if ((c & 0xE0) == 0xC0)
    return 2;
  if ((c & 0xF0) == 0xE0)
    return 3;
  if ((c & 0xF8) == 0xF0)
    return 4;
  return 1; // invalid, treat as 1
}

int skip_ansi(const char *p) {
  if (p[0] != '\033' || p[1] != '[')
    return 0;
  int i = 2;
  while (p[i] && ((p[i] >= '0' && p[i] <= '9') || p[i] == ';'))
    i++;
  if (p[i])
    i++; // skip the final letter
  return i;
}

int is_cursor_escape(const char *p) {
  if (p[0] != '\033' || p[1] != '[')
    return 0;
  int i = 2;
  while (p[i] && ((p[i] >= '0' && p[i] <= '9') || p[i] == ';'))
    i++;
  return (p[i] && p[i] != 'm');
}

void strip_inline_hint(char *val) {
  char *paren = strstr(val, " (");
  if (paren)
    *paren = '\0';
  int len = (int)strlen(val);
  while (len > 0 && (val[len - 1] == ' ' || val[len - 1] == '\t')) {
    val[len - 1] = '\0';
    len--;
  }
}

int visible_width(const char *s) {
  int w = 0;
  while (*s) {
    int a = skip_ansi(s);
    if (a) {
      s += a;
      continue;
    }
    int len = utf8_char_len((unsigned char)*s);
    while (len > 0 && *s) {
      s++;
      len--;
    }
    w++;
  }
  return w;
}

char *emit_clipped(char *p, char *end, const char *s, int max_cols) {
  // first pass: measure visible width to know if we need to clip
  int total_w = 0;
  const char *t = s;
  while (*t) {
    int a = skip_ansi(t);
    if (a) { t += a; continue; }
    t += utf8_char_len((unsigned char)*t);
    total_w++;
  }
  int need_clip = (max_cols >= 0 && total_w > max_cols);
  int limit = max_cols;
  if (need_clip && max_cols >= 6)
    limit = max_cols - 3; // leave room for "..."

  int w = 0, had_ansi = 0;
  while (*s && p + 8 < end) {
    int a = skip_ansi(s);
    if (a) {
      if (p + a + 8 >= end)
        break;
      memcpy(p, s, a);
      p += a;
      s += a;
      had_ansi = 1;
      continue;
    }
    if (limit >= 0 && w >= limit)
      break;
    int len = utf8_char_len((unsigned char)*s);
    int actual = 0;
    while (actual < len && s[actual])
      actual++;
    memcpy(p, s, actual);
    p += actual;
    s += actual;
    w++;
  }
  if (need_clip && max_cols >= 6 && p + 3 < end) {
    memcpy(p, "...", 3);
    p += 3;
  }
  if (had_ansi && need_clip && p + 4 < end) {
    memcpy(p, "\033[0m", 4);
    p += 4;
  }
  return p;
}
