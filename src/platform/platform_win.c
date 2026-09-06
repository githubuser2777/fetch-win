#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "src/platform/platform.h"

/* Ensure modern console constants are defined even on older SDK headers */
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif
#ifndef ENABLE_EXTENDED_FLAGS
#define ENABLE_EXTENDED_FLAGS 0x0080
#endif
#ifndef ENABLE_QUICK_EDIT_MODE
#define ENABLE_QUICK_EDIT_MODE 0x0040
#endif

/* --- Terminal State Encapsulation --- */

typedef struct {
  HANDLE hIn;
  HANDLE hOut;
  DWORD orig_in_mode;
  DWORD orig_out_mode;
  UINT orig_in_cp;
  UINT orig_out_cp;
  CONSOLE_CURSOR_INFO orig_cursor_info;
  int is_tty;
  int supports_vt;
  int supports_mouse;
  int in_mode_saved;
  int out_mode_saved;
  int cp_saved;
  int cursor_saved;
  int conin_opened;
  int conout_opened;
  int is_initialized;
  int is_cleaned_up;
} win_console_state_t;

static win_console_state_t g_win_console = {0};
static atomic_int g_interrupted = 0;
static volatile int g_term_resized = 0;
static int g_last_rows = 0;
static int g_last_cols = 0;

/* Native Win32 mouse state tracking */
static int s_win_mouse_dragging = 0;
static int s_win_mouse_last_x = 0;
static int s_win_mouse_last_y = 0;

/* SGR escape parser mouse state tracking */
static int s_sgr_mouse_dragging = 0;
static int s_sgr_mouse_last_x = 0;
static int s_sgr_mouse_last_y = 0;

#ifdef FETCH_TESTING
static int s_test_pending_avail = -1;
#endif

/* --- Control Handler (Ctrl+C / Interruption) --- */

static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
  switch (ctrl_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
      /* Safe guarantee: only set atomic interruption flag, no console I/O or allocation */
      atomic_store(&g_interrupted, 1);
      return TRUE;
    default:
      return FALSE;
  }
}

/* Helper to get or connect console handles (supporting redirected stdio) */
static void ensure_console_handles(void) {
  if (g_win_console.hIn == INVALID_HANDLE_VALUE || g_win_console.hIn == NULL) {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (hIn == INVALID_HANDLE_VALUE || hIn == NULL || !GetConsoleMode(hIn, &mode)) {
      HANDLE hConIn = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  NULL, OPEN_EXISTING, 0, NULL);
      if (hConIn != INVALID_HANDLE_VALUE && GetConsoleMode(hConIn, &mode)) {
        g_win_console.hIn = hConIn;
        g_win_console.conin_opened = 1;
      } else {
        g_win_console.hIn = hIn;
      }
    } else {
      g_win_console.hIn = hIn;
    }
  }

  if (g_win_console.hOut == INVALID_HANDLE_VALUE || g_win_console.hOut == NULL) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (hOut == INVALID_HANDLE_VALUE || hOut == NULL || !GetConsoleMode(hOut, &mode)) {
      HANDLE hConOut = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL, OPEN_EXISTING, 0, NULL);
      if (hConOut != INVALID_HANDLE_VALUE && GetConsoleMode(hConOut, &mode)) {
        g_win_console.hOut = hConOut;
        g_win_console.conout_opened = 1;
      } else {
        g_win_console.hOut = hOut;
      }
    } else {
      g_win_console.hOut = hOut;
    }
  }
}

/* --- Terminal Capabilities & Initialization --- */

int platform_terminal_init(platform_term_caps_t *caps) {
  g_win_console.is_cleaned_up = 0;
  atomic_store(&g_interrupted, 0);
  g_term_resized = 0;

  /* If handles were previously closed on cleanup, reconnect them */
  ensure_console_handles();

  HANDLE hIn = g_win_console.hIn;
  HANDLE hOut = g_win_console.hOut;

  DWORD in_mode = 0, out_mode = 0;
  int in_is_console = (hIn != INVALID_HANDLE_VALUE && hIn != NULL && GetConsoleMode(hIn, &in_mode));
  int out_is_console = (hOut != INVALID_HANDLE_VALUE && hOut != NULL && GetConsoleMode(hOut, &out_mode));

  int is_tty = in_is_console && out_is_console;
  int supports_vt = 0;
  int supports_mouse = 0;

  if (is_tty) {
    if (!g_win_console.in_mode_saved) {
      g_win_console.orig_in_mode = in_mode;
      g_win_console.in_mode_saved = 1;
    }
    if (!g_win_console.out_mode_saved) {
      g_win_console.orig_out_mode = out_mode;
      g_win_console.out_mode_saved = 1;
    }
    if (!g_win_console.cp_saved) {
      g_win_console.orig_in_cp = GetConsoleCP();
      g_win_console.orig_out_cp = GetConsoleOutputCP();
      g_win_console.cp_saved = 1;
    }
    if (!g_win_console.cursor_saved) {
      if (GetConsoleCursorInfo(hOut, &g_win_console.orig_cursor_info)) {
        g_win_console.cursor_saved = 1;
      }
    }

    /* Switch code pages to UTF-8 */
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    /* Check TERM environment variable for explicit overrides */
    const char *term = getenv("TERM");
    int term_dumb = (term && strcmp(term, "dumb") == 0);
    int term_vt100 = (term && strcmp(term, "vt100") == 0);

    /* Attempt to enable Virtual Terminal Processing on stdout */
    DWORD requested_out_mode = out_mode | ENABLE_PROCESSED_OUTPUT |
                              ENABLE_WRAP_AT_EOL_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!term_dumb && SetConsoleMode(hOut, requested_out_mode)) {
      supports_vt = 1;
      /* Mouse tracking is supported in modern terminals unless TERM specifies legacy vt100/vt52 */
      if (!term_vt100 &&
          (term == NULL ||
           (strcmp(term, "vt52") != 0 &&
            strcmp(term, "vt220") != 0 &&
            strncmp(term, "vanilla", 7) != 0))) {
        supports_mouse = 1;
      }
    } else {
      SetConsoleMode(hOut, out_mode | ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT);
      supports_vt = 0;
      supports_mouse = 0;
    }

    /* Configure raw input mode:
     * Disable line input, echo, processed input, and QuickEdit mode.
     * Enable window resize input and mouse input.
     */
    DWORD requested_in_mode = ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT;
    SetConsoleMode(hIn, requested_in_mode);
  }

  g_win_console.is_tty = is_tty;
  g_win_console.supports_vt = supports_vt;
  g_win_console.supports_mouse = supports_mouse;

  if (caps) {
    caps->is_tty = is_tty;
    caps->supports_vt = supports_vt;
    caps->supports_mouse = supports_mouse;
  }

  /* Register control handler for Ctrl+C / close */
  SetConsoleCtrlHandler(console_ctrl_handler, TRUE);

  if (!g_win_console.is_initialized) {
    atexit(platform_terminal_cleanup);
    g_win_console.is_initialized = 1;
  }

  /* If VT supported, emit VT sequences to hide cursor, enable SGR mouse, clear screen */
  if (is_tty && supports_vt) {
    if (supports_mouse) {
      const char init_seq[] = "\033[?25l\033[?1002h\033[?1006h\033[2J";
      platform_write_output(init_seq, sizeof(init_seq) - 1);
    } else {
      const char init_seq[] = "\033[?25l\033[2J";
      platform_write_output(init_seq, sizeof(init_seq) - 1);
    }
  } else if (is_tty && g_win_console.cursor_saved) {
    /* Non-VT fallback: hide cursor via Win32 cursor info */
    CONSOLE_CURSOR_INFO cinfo = g_win_console.orig_cursor_info;
    cinfo.bVisible = FALSE;
    SetConsoleCursorInfo(hOut, &cinfo);
  }

  /* Cache initial terminal size */
  int rows = 0, cols = 0;
  platform_get_term_size(&rows, &cols);
  g_last_rows = rows;
  g_last_cols = cols;

  return 0;
}

/* --- Safe Terminal Cleanup & Restoration --- */

void platform_terminal_cleanup(void) {
  if (!g_win_console.is_initialized || g_win_console.is_cleaned_up) {
    return;
  }
  g_win_console.is_cleaned_up = 1;

  /* If VT is supported, disable mouse tracking and restore cursor visibility */
  if (g_win_console.is_tty && g_win_console.supports_vt) {
    const char cleanup_seq[] = "\033[?1002l\033[?1006l\033[?25h";
    platform_write_output(cleanup_seq, sizeof(cleanup_seq) - 1);
  }

  /* Restore cursor info via Win32 API */
  if (g_win_console.cursor_saved && g_win_console.hOut != INVALID_HANDLE_VALUE && g_win_console.hOut != NULL) {
    SetConsoleCursorInfo(g_win_console.hOut, &g_win_console.orig_cursor_info);
  }

  /* Restore original console input and output modes */
  if (g_win_console.in_mode_saved && g_win_console.hIn != INVALID_HANDLE_VALUE && g_win_console.hIn != NULL) {
    SetConsoleMode(g_win_console.hIn, g_win_console.orig_in_mode);
  }
  if (g_win_console.out_mode_saved && g_win_console.hOut != INVALID_HANDLE_VALUE && g_win_console.hOut != NULL) {
    SetConsoleMode(g_win_console.hOut, g_win_console.orig_out_mode);
  }

  /* Restore original code pages */
  if (g_win_console.cp_saved) {
    if (g_win_console.orig_in_cp != 0) {
      SetConsoleCP(g_win_console.orig_in_cp);
    }
    if (g_win_console.orig_out_cp != 0) {
      SetConsoleOutputCP(g_win_console.orig_out_cp);
    }
  }

  /* If custom CONIN$/CONOUT$ were opened, close them */
  if (g_win_console.conin_opened && g_win_console.hIn != INVALID_HANDLE_VALUE && g_win_console.hIn != NULL) {
    CloseHandle(g_win_console.hIn);
    g_win_console.hIn = INVALID_HANDLE_VALUE;
    g_win_console.conin_opened = 0;
  }
  if (g_win_console.conout_opened && g_win_console.hOut != INVALID_HANDLE_VALUE && g_win_console.hOut != NULL) {
    CloseHandle(g_win_console.hOut);
    g_win_console.hOut = INVALID_HANDLE_VALUE;
    g_win_console.conout_opened = 0;
  }

  g_win_console.in_mode_saved = 0;
  g_win_console.out_mode_saved = 0;
  g_win_console.cp_saved = 0;
  g_win_console.cursor_saved = 0;
}

/* --- Terminal Size & Resize Detection --- */

void platform_get_term_size(int *rows, int *cols) {
  if (rows) *rows = 0;
  if (cols) *cols = 0;

  ensure_console_handles();
  HANDLE hOut = g_win_console.hOut;
  if (hOut == INVALID_HANDLE_VALUE || hOut == NULL) {
    return;
  }

  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (GetConsoleScreenBufferInfo(hOut, &csbi)) {
    int r = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    int c = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    if (rows && r > 0) *rows = r;
    if (cols && c > 0) *cols = c;
  }
}

int platform_check_resize(void) {
  int r = 0, c = 0;
  platform_get_term_size(&r, &c);
  if (g_last_rows > 0 && g_last_cols > 0 && (r > 0 && c > 0)) {
    if (r != g_last_rows || c != g_last_cols) {
      g_term_resized = 1;
      g_last_rows = r;
      g_last_cols = c;
    }
  } else if (r > 0 && c > 0) {
    g_last_rows = r;
    g_last_cols = c;
  }

  if (g_term_resized) {
    g_term_resized = 0;
    return 1;
  }
  return 0;
}

/* --- Output & Sleep Wrappers --- */

int platform_write_output(const char *buf, size_t len) {
  if (!buf || len == 0) return 0;
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  if (hOut != INVALID_HANDLE_VALUE && hOut != NULL) {
    DWORD written = 0;
    if (WriteFile(hOut, buf, (DWORD)len, &written, NULL)) {
      return (int)written;
    }
  }
  ensure_console_handles();
  if (g_win_console.hOut != INVALID_HANDLE_VALUE && g_win_console.hOut != NULL &&
      g_win_console.hOut != hOut) {
    DWORD written = 0;
    if (WriteFile(g_win_console.hOut, buf, (DWORD)len, &written, NULL)) {
      return (int)written;
    }
  }
  int ret = (int)fwrite(buf, 1, len, stdout);
  fflush(stdout);
  return ret;
}

void platform_sleep_frame(unsigned int usec) {
  /* Round up microseconds to milliseconds so non-zero usec always yields */
  DWORD ms = (DWORD)((usec + 999) / 1000);
  Sleep(ms);
}

int platform_is_interrupted(void) {
  return atomic_load(&g_interrupted) != 0;
}

/* --- SGR Mouse Escape Sequence Parser --- */

platform_input_event_t platform_parse_input_chunk(const char *buf, size_t len,
                                                  platform_mouse_event_t *mouse_event,
                                                  size_t *consumed) {
  if (consumed) *consumed = 0;
  if (!buf || len == 0) return INPUT_NONE;

  /* Non-escape character is treated as an exit keypress */
  if (buf[0] != '\033') {
    if (consumed) *consumed = 1;
    return INPUT_EXIT_KEY;
  }

  /* Must have at least "\033[<" (3 bytes) to be an SGR mouse sequence */
  if (len < 3) {
    return INPUT_NONE;
  }

  if (buf[1] == '[' && buf[2] == '<') {
    size_t j = 3;
    int btn = 0, mx = 0, my = 0;

    while (j < len && buf[j] >= '0' && buf[j] <= '9') {
      btn = btn * 10 + (buf[j++] - '0');
    }
    if (j >= len) return INPUT_NONE;
    if (buf[j] == ';') j++;

    while (j < len && buf[j] >= '0' && buf[j] <= '9') {
      mx = mx * 10 + (buf[j++] - '0');
    }
    if (j >= len) return INPUT_NONE;
    if (buf[j] == ';') j++;

    while (j < len && buf[j] >= '0' && buf[j] <= '9') {
      my = my * 10 + (buf[j++] - '0');
    }
    if (j >= len) return INPUT_NONE;

    char trail = buf[j++];
    if (trail != 'M' && trail != 'm') {
      if (consumed) *consumed = j;
      return INPUT_NONE;
    }

    if (consumed) *consumed = j;

    if (btn == 0 && trail == 'M') {
      s_sgr_mouse_dragging = 1;
      s_sgr_mouse_last_x = mx;
      s_sgr_mouse_last_y = my;
      if (mouse_event) {
        mouse_event->btn = 0;
        mouse_event->x = mx;
        mouse_event->y = my;
        mouse_event->dx = 0;
        mouse_event->dy = 0;
      }
      return INPUT_NONE;
    } else if (btn == 32 && trail == 'M' && s_sgr_mouse_dragging) {
      int dx = mx - s_sgr_mouse_last_x;
      int dy = my - s_sgr_mouse_last_y;
      s_sgr_mouse_last_x = mx;
      s_sgr_mouse_last_y = my;
      if (mouse_event) {
        mouse_event->btn = 32;
        mouse_event->x = mx;
        mouse_event->y = my;
        mouse_event->dx = dx;
        mouse_event->dy = dy;
      }
      return INPUT_MOUSE_DRAG;
    } else if (btn == 0 && trail == 'm') {
      s_sgr_mouse_dragging = 0;
      if (mouse_event) {
        mouse_event->btn = 0;
        mouse_event->x = mx;
        mouse_event->y = my;
        mouse_event->dx = 0;
        mouse_event->dy = 0;
      }
      return INPUT_MOUSE_UP;
    }
    return INPUT_NONE;
  }

  /* Unrecognized escape sequence - skip past terminal character */
  size_t i = 1;
  if (len > 1 && buf[1] == '[') {
    i = 2;
    while (i < len && (buf[i] < 0x40 || buf[i] > 0x7E)) i++;
    if (i < len) i++;
  } else {
    while (i < len && buf[i] < 0x40) i++;
    if (i < len) i++;
  }
  if (consumed) *consumed = i;
  return INPUT_NONE;
}

/* --- Input Polling & Event Handling --- */

platform_input_event_t platform_poll_input(platform_mouse_event_t *mouse_event) {
#ifdef FETCH_TESTING
  if (s_test_pending_avail == 1) {
    /* Simulated single pending byte / key - do not consume */
    return INPUT_EXIT_KEY;
  } else if (s_test_pending_avail == 0) {
    return INPUT_NONE;
  }
#endif

  if (atomic_load(&g_interrupted)) {
    return INPUT_NONE;
  }

  ensure_console_handles();
  HANDLE hIn = g_win_console.hIn;
  if (hIn == INVALID_HANDLE_VALUE || hIn == NULL) {
    return INPUT_NONE;
  }

  while (1) {
    if (atomic_load(&g_interrupted)) {
      return INPUT_NONE;
    }

    DWORD num_events = 0;
    if (!GetNumberOfConsoleInputEvents(hIn, &num_events) || num_events == 0) {
      return INPUT_NONE;
    }

    INPUT_RECORD ir;
    DWORD num_read = 0;
    if (!PeekConsoleInputA(hIn, &ir, 1, &num_read) || num_read == 0) {
      return INPUT_NONE;
    }

    /* Window buffer resize event */
    if (ir.EventType == WINDOW_BUFFER_SIZE_EVENT) {
      INPUT_RECORD discard;
      ReadConsoleInputA(hIn, &discard, 1, &num_read);
      g_term_resized = 1;
      continue;
    }

    /* Focus and menu events: consume and discard */
    if (ir.EventType == FOCUS_EVENT || ir.EventType == MENU_EVENT) {
      INPUT_RECORD discard;
      ReadConsoleInputA(hIn, &discard, 1, &num_read);
      continue;
    }

    /* Native Win32 mouse event */
    if (ir.EventType == MOUSE_EVENT) {
      INPUT_RECORD mev_record;
      if (!ReadConsoleInputA(hIn, &mev_record, 1, &num_read) || num_read == 0) {
        continue;
      }

      MOUSE_EVENT_RECORD *m = &mev_record.Event.MouseEvent;
      int cur_x = (int)m->dwMousePosition.X + 1; /* 1-based coordinate */
      int cur_y = (int)m->dwMousePosition.Y + 1; /* 1-based coordinate */
      int left_pressed = (m->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) != 0;

      /* Mouse drag event: button held down and mouse moved */
      if (m->dwEventFlags == MOUSE_MOVED && left_pressed && s_win_mouse_dragging) {
        int dx = cur_x - s_win_mouse_last_x;
        int dy = cur_y - s_win_mouse_last_y;
        s_win_mouse_last_x = cur_x;
        s_win_mouse_last_y = cur_y;

        if (mouse_event) {
          mouse_event->btn = 32;
          mouse_event->x = cur_x;
          mouse_event->y = cur_y;
          mouse_event->dx = dx;
          mouse_event->dy = dy;
        }
        return INPUT_MOUSE_DRAG;
      }

      /* Mouse button down: start dragging baseline */
      if (left_pressed && !s_win_mouse_dragging &&
          (m->dwEventFlags == 0 || m->dwEventFlags == DOUBLE_CLICK)) {
        s_win_mouse_dragging = 1;
        s_win_mouse_last_x = cur_x;
        s_win_mouse_last_y = cur_y;
        if (mouse_event) {
          mouse_event->btn = 0;
          mouse_event->x = cur_x;
          mouse_event->y = cur_y;
          mouse_event->dx = 0;
          mouse_event->dy = 0;
        }
        continue;
      }

      /* Mouse button release */
      if (!left_pressed && s_win_mouse_dragging &&
          (m->dwEventFlags == 0 || m->dwEventFlags == MOUSE_MOVED)) {
        s_win_mouse_dragging = 0;
        if (mouse_event) {
          mouse_event->btn = 0;
          mouse_event->x = cur_x;
          mouse_event->y = cur_y;
          mouse_event->dx = 0;
          mouse_event->dy = 0;
        }
        return INPUT_MOUSE_UP;
      }

      /* Other mouse events (unpressed movement, wheel, right click): ignore */
      continue;
    }

    /* Key event handling */
    if (ir.EventType == KEY_EVENT) {
      KEY_EVENT_RECORD *k = &ir.Event.KeyEvent;

      /* Key release: consume and discard */
      if (!k->bKeyDown) {
        INPUT_RECORD discard;
        ReadConsoleInputA(hIn, &discard, 1, &num_read);
        continue;
      }

      /* Standalone modifier keys (Shift, Ctrl, Alt, CapsLock, Win keys): consume and ignore */
      WORD vk = k->wVirtualKeyCode;
      if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU ||
          vk == VK_CAPITAL || vk == VK_LWIN || vk == VK_RWIN) {
        INPUT_RECORD discard;
        ReadConsoleInputA(hIn, &discard, 1, &num_read);
        continue;
      }

      /*
       * SGR mouse escape sequence check:
       * When streamed through ConPTY/terminal as VT key events,
       * an SGR sequence starts with '\033[<'.
       */
      if (k->uChar.AsciiChar == '\033' && num_events >= 3) {
        INPUT_RECORD lookahead[16];
        DWORD lookahead_count = num_events < 16 ? num_events : 16;
        DWORD lookahead_read = 0;
        if (PeekConsoleInputA(hIn, lookahead, lookahead_count, &lookahead_read) && lookahead_read >= 3) {
          if (lookahead[0].EventType == KEY_EVENT && lookahead[0].Event.KeyEvent.uChar.AsciiChar == '\033' &&
              lookahead[1].EventType == KEY_EVENT && lookahead[1].Event.KeyEvent.uChar.AsciiChar == '[' &&
              lookahead[2].EventType == KEY_EVENT && lookahead[2].Event.KeyEvent.uChar.AsciiChar == '<') {
            char sgr_buf[64];
            size_t sgr_len = 0;
            while (sgr_len < sizeof(sgr_buf) - 1) {
              INPUT_RECORD char_rec;
              DWORD cr = 0;
              if (!PeekConsoleInputA(hIn, &char_rec, 1, &cr) || cr == 0) break;
              if (char_rec.EventType != KEY_EVENT) break;
              ReadConsoleInputA(hIn, &char_rec, 1, &cr);
              if (!char_rec.Event.KeyEvent.bKeyDown) continue;
              char c = char_rec.Event.KeyEvent.uChar.AsciiChar;
              sgr_buf[sgr_len++] = c;
              if (c == 'M' || c == 'm') break;
            }
            sgr_buf[sgr_len] = '\0';
            size_t consumed = 0;
            platform_input_event_t sgr_ev = platform_parse_input_chunk(sgr_buf, sgr_len, mouse_event, &consumed);
            if (sgr_ev != INPUT_NONE) {
              return sgr_ev;
            }
            continue;
          }
        }
      }

      /*
       * HARD REQUIREMENT: KEYPRESS PASSTHROUGH
       * A normal key-down event is NOT consumed from the console input buffer!
       * Return INPUT_EXIT_KEY, leaving the record in the console buffer
       * so that the calling shell (PowerShell/CMD) receives the keypress upon exit.
       */
      return INPUT_EXIT_KEY;
    }

    /* Any unrecognized event: consume and discard */
    INPUT_RECORD discard;
    ReadConsoleInputA(hIn, &discard, 1, &num_read);
  }

  return INPUT_NONE;
}

/* --- Platform Paths & OS Identification --- */

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

#ifdef FETCH_TESTING
/* --- Test Control Helpers --- */

void platform_reset_input_state_for_test(void) {
  s_win_mouse_dragging = 0;
  s_win_mouse_last_x = 0;
  s_win_mouse_last_y = 0;
  s_sgr_mouse_dragging = 0;
  s_sgr_mouse_last_x = 0;
  s_sgr_mouse_last_y = 0;
}

void platform_set_interrupted_for_test(int val) {
  atomic_store(&g_interrupted, val);
}

void platform_set_resized_for_test(int val) {
  g_term_resized = val;
}

void platform_set_pending_bytes_for_test(int count) {
  s_test_pending_avail = count;
}

int platform_get_pending_bytes_for_test(void) {
  return s_test_pending_avail;
}
#endif
