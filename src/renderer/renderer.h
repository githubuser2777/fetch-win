#ifndef FETCH_RENDERER_H
#define FETCH_RENDERER_H

#include <stddef.h>

#ifndef PI
#define PI 3.14159265f
#endif

#define ANIM_WIDTH 60
#define MAX_HEIGHT 200

#define RAMP_ASCII ".,-~:;=!*#$@"
#define RAMP_BLOCKS "░▒▓█"

#define MAX_SUB_ROWS 3
#define MAX_SUB_COLS 2

#define MAX_SHADING 64

// Sub-cell render grid dimensions
#define SUB_H (MAX_HEIGHT * MAX_SUB_ROWS)
#define SUB_W (ANIM_WIDTH * MAX_SUB_COLS)

// Maximum 3D points
#define MAX_POINTS 200000

// Logo grid bounds (used for heightmap & point generation)
#define MAX_LOGO_ROWS 64
#define MAX_LOGO_COLS 128

// Sub-cell configuration
extern int sub_rows;
extern int sub_cols;

// Shading glyphs
extern const char *const quadrant_glyphs[16];
extern char sextant_glyphs[64][5];
extern char shading_chars[MAX_SHADING][5];
extern int shading_count;

// Point cloud storage
extern float PX[MAX_POINTS], PY[MAX_POINTS], PZ[MAX_POINTS];
extern float NX[MAX_POINTS], NY[MAX_POINTS], NZ[MAX_POINTS];
extern int PCOLOR[MAX_POINTS];
extern int POINT_COUNT;

// Render buffers (sub-cell grid)
extern float zbuf[SUB_H][SUB_W];
extern float lumbuf[SUB_H][SUB_W];
extern int colorbuf[SUB_H][SUB_W];

// External logo and layout state consumed by build_points() and clear_buf()
// (Until Phase 2 extraction of logo and config modules)
extern int logo_rows;
extern int logo_cols;
extern int logo_has_ansi;
extern int logo_cell_counts[MAX_LOGO_ROWS];
extern char logo_cells[MAX_LOGO_ROWS][MAX_LOGO_COLS][5];
extern int logo_cell_color[MAX_LOGO_ROWS][MAX_LOGO_COLS];

extern float size_scale;
extern float config_depth;
extern int depth_user_set;
extern int render_height;

// Logo struct for clean/decoupled renderer calls
typedef struct {
  int rows;
  int cols;
  int has_ansi;
  const int *cell_counts;
  const char (*cells)[MAX_LOGO_COLS][5];
  const int (*cell_color)[MAX_LOGO_COLS];
} renderer_logo_t;

// Configuration struct for clean/decoupled renderer calls
typedef struct {
  float size_scale;
  float *config_depth;
  int depth_user_set;
} renderer_config_t;

// --- Function Declarations ---

// Generate sextant UTF-8 glyphs
void build_sextant_glyphs(void);

// Parse custom shading ramp string into codepoints
void parse_shading(const char *str);

// Select shading mode ("ascii", "blocks", "sextants") and optional custom ramp chars
int select_shading(const char *mode, const char *chars);

// Heightmap weight for a UTF-8 character / block element
float char_weight_utf8(const char *ch);

// Clear z-buffer, luminance, and color buffers for current render_height
void clear_buf(void);

// Collapse one cell's sub-samples into a glyph
const char *cell_glyph(int row, int col, int smax, int *color_out);

// Build 3D point cloud using decoupled logo and config descriptors
void renderer_build_points(const renderer_logo_t *logo, renderer_config_t *cfg);

// Build 3D point cloud from global logo and config state (backward-compatible)
void build_points(void);

// Pre-compute Blinn-Phong half-vector (view direction is constant (0,0,-1))
void render_compute_half_vector(float lx, float ly, float lz,
                                float *hlx, float *hly, float *hlz);

// Project 3D points, compute Blinn-Phong lighting, and update depth/luminance/color buffers
void render_project_points(float A, float B, float K1, float K2,
                           float y_center, int aw, int r_height,
                           float lx, float ly, float lz,
                           float hlx, float hly, float hlz);

#endif // FETCH_RENDERER_H
