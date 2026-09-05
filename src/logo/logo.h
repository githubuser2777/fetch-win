#ifndef FETCH_LOGO_H
#define FETCH_LOGO_H

#include <stddef.h>
#include "src/renderer/renderer.h"

// Encapsulated logo object
typedef struct {
  char data[MAX_LOGO_ROWS][512];
  char cells[MAX_LOGO_ROWS][MAX_LOGO_COLS][5];
  int cell_color[MAX_LOGO_ROWS][MAX_LOGO_COLS];
  int cell_counts[MAX_LOGO_ROWS];
  int rows;
  int cols;
  int has_ansi;
  char distro[64];
  char distro_id_like[64];
} fetch_logo_t;

// Modular Logo API
void logo_init(fetch_logo_t *logo);
void logo_load_default(fetch_logo_t *logo);
int logo_load_file(fetch_logo_t *logo, const char *path);
int logo_load_fastfetch(fetch_logo_t *logo, const char *name);
void logo_process(fetch_logo_t *logo);
void logo_process_row(fetch_logo_t *logo, int row);
void logo_set_distro_colors(const char *distro, const char **color_outer, const char **color_inner);
int logo_detect_distro(char *out, size_t maxlen);
void logo_get_default_path(char *out, size_t outsz);

// Global logo instance and sync functions
extern fetch_logo_t g_logo;
void logo_sync_to_globals(const fetch_logo_t *logo);
void logo_sync_from_globals(fetch_logo_t *logo);

// Backward-compatibility global variables for fetch.c, renderer.c, and Phase 0 baseline tests
extern char logo_data[MAX_LOGO_ROWS][512];
extern char logo_cells[MAX_LOGO_ROWS][MAX_LOGO_COLS][5];
extern int logo_cell_color[MAX_LOGO_ROWS][MAX_LOGO_COLS];
extern int logo_cell_counts[MAX_LOGO_ROWS];
extern int logo_rows;
extern int logo_cols;
extern int logo_has_ansi;
extern char file_distro[64];
extern char distro_id_like[64];
extern const char *color_outer;
extern const char *color_inner;

// Backward-compatibility functions
void load_default_logo(void);
int load_logo_file(void);
int load_logo_fastfetch(const char *name);
void process_logo_row(int row);
void process_logo(void);
void set_distro_colors(const char *distro);
int detect_distro(char *out, int maxlen);

#endif // FETCH_LOGO_H
