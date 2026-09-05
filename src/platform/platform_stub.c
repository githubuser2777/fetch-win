#include "src/platform/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Temporary platform stub for native Windows builds in Phase 3.
 * Full native Windows console lifecycle, Win32 INPUT_RECORD polling,
 * and VT processing will be implemented in src/platform/platform_win.c (Phase 4).
 */

int platform_terminal_init(platform_term_caps_t *caps) {
  if (caps) {
    int stdout_tty = isatty(fileno(stdout));
    int stdin_tty = isatty(fileno(stdin));
    caps->is_tty = stdout_tty && stdin_tty;
    caps->supports_vt = caps->is_tty;
    caps->supports_mouse = 0;
  }
  return 0;
}

void platform_terminal_cleanup(void) {
  /* No-op stub for Phase 3 Windows build */
}

void platform_get_term_size(int *rows, int *cols) {
  if (rows) *rows = 0;
  if (cols) *cols = 0;
}

int platform_check_resize(void) {
  return 0;
}

void platform_sleep_frame(unsigned int usec) {
  usleep(usec);
}

int platform_write_output(const char *buf, size_t len) {
  if (!buf || len == 0) return 0;
  return (int)fwrite(buf, 1, len, stdout);
}

platform_input_event_t platform_poll_input(platform_mouse_event_t *mouse_event) {
  (void)mouse_event;
  return INPUT_NONE;
}

int platform_is_interrupted(void) {
  return 0;
}

void platform_get_config_path(char *out, size_t outsz) {
  if (!out || outsz == 0) return;
  const char *home = getenv("HOME");
  if (home && home[0]) {
    snprintf(out, outsz, "%s/.config/fetch/config", home);
    return;
  }
  const char *userprofile = getenv("USERPROFILE");
  if (userprofile && userprofile[0]) {
    snprintf(out, outsz, "%s/.config/fetch/config", userprofile);
    return;
  }
  snprintf(out, outsz, ".config/fetch/config");
}

void platform_get_logo_path(char *out, size_t outsz) {
  if (!out || outsz == 0) return;
  const char *home = getenv("HOME");
  if (home && home[0]) {
    snprintf(out, outsz, "%s/.config/fetch/logo.txt", home);
    return;
  }
  const char *userprofile = getenv("USERPROFILE");
  if (userprofile && userprofile[0]) {
    snprintf(out, outsz, "%s/.config/fetch/logo.txt", userprofile);
    return;
  }
  snprintf(out, outsz, ".config/fetch/logo.txt");
}

int platform_detect_os_id(char *out, size_t outsz) {
  if (!out || outsz == 0) return 0;
  strncpy(out, "windows", outsz - 1);
  out[outsz - 1] = '\0';
  return 1;
}
