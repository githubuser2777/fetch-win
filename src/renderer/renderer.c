#include "renderer.h"
#include "../core/common.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

int sub_rows = 1;
int sub_cols = 1;

const char *const quadrant_glyphs[16] = {" ", "▘", "▝", "▀", "▖", "▌",
                                        "▞", "▛", "▗", "▚", "▐", "▜",
                                        "▄", "▙", "▟", "█"};

char sextant_glyphs[64][5];

char shading_chars[MAX_SHADING][5];
int shading_count = 0;

float PX[MAX_POINTS], PY[MAX_POINTS], PZ[MAX_POINTS];
float NX[MAX_POINTS], NY[MAX_POINTS], NZ[MAX_POINTS];
int PCOLOR[MAX_POINTS];
int POINT_COUNT = 0;

float zbuf[SUB_H][SUB_W];
float lumbuf[SUB_H][SUB_W];
int colorbuf[SUB_H][SUB_W];

void build_sextant_glyphs(void) {
  for (int mask = 1; mask < 63; mask++) {
    if (mask == 21 || mask == 42) {
      strcpy(sextant_glyphs[mask], mask == 21 ? "▌" : "▐");
      continue;
    }
    unsigned cp = 0x1FB00u + mask - 1 - (mask > 21) - (mask > 42);
    char *g = sextant_glyphs[mask];
    g[0] = (char)(0xF0 | (cp >> 18));
    g[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    g[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    g[3] = (char)(0x80 | (cp & 0x3F));
    g[4] = '\0';
  }
}

void parse_shading(const char *str) {
  shading_count = 0;
  const char *p = str;
  while (*p && shading_count < MAX_SHADING) {
    int len = utf8_char_len((unsigned char)*p);
    if (len > 4)
      len = 4;
    int actual = 0;
    while (actual < len && p[actual])
      actual++;
    memcpy(shading_chars[shading_count], p, actual);
    shading_chars[shading_count][actual] = '\0';
    shading_count++;
    p += actual;
  }
  if (shading_count == 0) {
    strcpy(shading_chars[0], ".");
    shading_count = 1;
  }
}

int select_shading(const char *mode, const char *chars) {
  if (!mode)
    mode = "ascii";
  if (strcmp(mode, "sextants") == 0) {
    sub_cols = 2;
    sub_rows = 3;
    build_sextant_glyphs();
  } else if (strcmp(mode, "blocks") == 0) {
    sub_cols = 2;
    sub_rows = 2;
  } else if (strcmp(mode, "ascii") != 0) {
    return 0;
  }
  if (chars)
    parse_shading(chars);
  else
    parse_shading(strcmp(mode, "ascii") == 0 ? RAMP_ASCII : RAMP_BLOCKS);
  return 1;
}

float char_weight_utf8(const char *ch) {
  // Single-byte ASCII
  if ((unsigned char)ch[0] < 0x80) {
    switch (ch[0]) {
    case 'M':
      return 1.00f;
    case 'N':
      return 0.88f;
    case 'm':
      return 0.76f;
    case 'd':
      return 0.66f;
    case 'h':
      return 0.56f;
    case 'b':
      return 0.56f;
    case 'y':
      return 0.46f;
    case 'o':
      return 0.38f;
    case 'n':
      return 0.38f;
    case 's':
      return 0.30f;
    case '+':
      return 0.22f;
    case ':':
      return 0.18f;
    case '=':
      return 0.22f;
    case '-':
      return 0.14f;
    case '`':
      return 0.08f;
    case '.':
      return 0.10f;
    case '/':
      return 0.12f;
    case '\'':
      return 0.06f;
    case ' ':
      return 0.0f;
    default:
      // Generic: uppercase heavy, lowercase medium, punct light
      if (ch[0] >= 'A' && ch[0] <= 'Z')
        return 0.80f;
      if (ch[0] >= 'a' && ch[0] <= 'z')
        return 0.50f;
      if (ch[0] >= '0' && ch[0] <= '9')
        return 0.40f;
      return 0.15f;
    }
  }

  // Multi-byte UTF-8: compare raw bytes for common block elements
  // Full block U+2588: E2 96 88
  if (memcmp(ch, "\xe2\x96\x88", 3) == 0)
    return 1.00f;
  // Dark shade U+2593: E2 96 93
  if (memcmp(ch, "\xe2\x96\x93", 3) == 0)
    return 0.75f;
  // Medium shade U+2592: E2 96 92
  if (memcmp(ch, "\xe2\x96\x92", 3) == 0)
    return 0.50f;
  // Light shade U+2591: E2 96 91
  if (memcmp(ch, "\xe2\x96\x91", 3) == 0)
    return 0.25f;

  // Half blocks (U+2580-258F)
  // Upper half U+2580: E2 96 80
  if (memcmp(ch, "\xe2\x96\x80", 3) == 0)
    return 0.50f;
  // Lower half U+2584: E2 96 84
  if (memcmp(ch, "\xe2\x96\x84", 3) == 0)
    return 0.50f;
  // Left half U+258C: E2 96 8C
  if (memcmp(ch, "\xe2\x96\x8c", 3) == 0)
    return 0.50f;
  // Right half U+2590: E2 96 90
  if (memcmp(ch, "\xe2\x96\x90", 3) == 0)
    return 0.50f;

  // 3/4 blocks
  // U+259B ▛: E2 96 9B
  if (memcmp(ch, "\xe2\x96\x9b", 3) == 0)
    return 0.75f;
  // U+259C ▜: E2 96 9C
  if (memcmp(ch, "\xe2\x96\x9c", 3) == 0)
    return 0.75f;
  // U+2599 ▙: E2 96 99
  if (memcmp(ch, "\xe2\x96\x99", 3) == 0)
    return 0.75f;
  // U+259F ▟: E2 96 9F
  if (memcmp(ch, "\xe2\x96\x9f", 3) == 0)
    return 0.75f;

  // 1/4 blocks
  // U+2596 ▖: E2 96 96
  if (memcmp(ch, "\xe2\x96\x96", 3) == 0)
    return 0.25f;
  // U+2597 ▗: E2 96 97
  if (memcmp(ch, "\xe2\x96\x97", 3) == 0)
    return 0.25f;
  // U+2598 ▘: E2 96 98
  if (memcmp(ch, "\xe2\x96\x98", 3) == 0)
    return 0.25f;
  // U+259D ▝: E2 96 9D
  if (memcmp(ch, "\xe2\x96\x9d", 3) == 0)
    return 0.25f;

  // Box drawing chars (U+2500-257F): E2 94 xx / E2 95 xx
  if ((unsigned char)ch[0] == 0xe2 &&
      ((unsigned char)ch[1] == 0x94 || (unsigned char)ch[1] == 0x95))
    return 0.20f;

  // Braille (U+2800-28FF): E2 A0-A3 xx
  if ((unsigned char)ch[0] == 0xe2 && (unsigned char)ch[1] >= 0xa0 &&
      (unsigned char)ch[1] <= 0xa3) {
    // Weight by number of dots (popcount of last byte)
    unsigned char b = (unsigned char)ch[2];
    int dots = 0;
    while (b) {
      dots += b & 1;
      b >>= 1;
    }
    return dots / 8.0f;
  }

  // Default for unknown multi-byte: treat as medium fill
  return 0.30f;
}

void clear_buf(void) {
  int n = render_height * sub_rows * SUB_W;
  memset(zbuf, 0, n * sizeof(float));
  memset(lumbuf, 0, n * sizeof(float));
  memset(colorbuf, 0, n * sizeof(int));
}

const char *cell_glyph(int row, int col, int smax, int *color_out) {
  int x0 = col * sub_cols, y0 = row * sub_rows;
  int total = sub_rows * sub_cols;
  int mask = 0, bit = 0, n = 0;
  float lsum = 0.0f, best = 0.0f;
  for (int sr = 0; sr < sub_rows; sr++) {
    for (int sc = 0; sc < sub_cols; sc++, bit++) {
      float z = zbuf[y0 + sr][x0 + sc];
      if (z <= 0.0f)
        continue;
      mask |= 1 << bit;
      lsum += lumbuf[y0 + sr][x0 + sc];
      n++;
      if (z > best) {
        best = z;
        *color_out = colorbuf[y0 + sr][x0 + sc];
      }
    }
  }
  if (!n)
    return NULL;

  float coverage = (float)n / total;
  float ink = lsum / n * coverage;
  int ci = (int)(ink * smax + 0.5f);
  if (ci < 0)
    ci = 0;
  if (ci > smax)
    ci = smax;
  if (mask != (1 << total) - 1 &&
      fabsf(coverage - ink) <= fabsf((ci + 1.0f) / shading_count - ink))
    return sub_rows == 3 ? sextant_glyphs[mask] : quadrant_glyphs[mask];
  return shading_chars[ci];
}

void renderer_build_points(const renderer_logo_t *logo, renderer_config_t *cfg) {
  const float sx = 0.07f;
  const float sy = 0.14f;
  const float cx = (logo->cols - 1) * 0.5f;
  const float cy = (logo->rows - 1) * 0.5f;
  int Z_LAYERS = (int)(6 * cfg->size_scale);
  if (Z_LAYERS < 6)
    Z_LAYERS = 6;

  float(*hmap)[MAX_LOGO_COLS] = malloc(sizeof(float[MAX_LOGO_ROWS][MAX_LOGO_COLS]));
  float(*gnx)[MAX_LOGO_COLS] = malloc(sizeof(float[MAX_LOGO_ROWS][MAX_LOGO_COLS]));
  float(*gny)[MAX_LOGO_COLS] = malloc(sizeof(float[MAX_LOGO_ROWS][MAX_LOGO_COLS]));
  float(*gnz)[MAX_LOGO_COLS] = malloc(sizeof(float[MAX_LOGO_ROWS][MAX_LOGO_COLS]));
  if (!hmap || !gnx || !gny || !gnz) {
    free(hmap); free(gnx); free(gny); free(gnz);
    POINT_COUNT = 0;
    return;
  }

  for (int r = 0; r < logo->rows; r++) {
    for (int c = 0; c < logo->cols; c++) {
      if (c < logo->cell_counts[r])
        hmap[r][c] = char_weight_utf8(logo->cells[r][c]);
      else
        hmap[r][c] = 0.0f;
    }
  }

  // Auto-scale depth when user hasn't set it explicitly.
  // Logos with low height variance look flat — boost depth to compensate.
  if (!cfg->depth_user_set) {
    float sum = 0, sum2 = 0;
    int n = 0;
    for (int r = 0; r < logo->rows; r++)
      for (int c = 0; c < logo->cols; c++)
        if (hmap[r][c] > 0.0f) {
          sum += hmap[r][c];
          sum2 += hmap[r][c] * hmap[r][c];
          n++;
        }
    if (n > 0) {
      float mean = sum / n;
      float variance = sum2 / n - mean * mean;
      float stddev = sqrtf(variance > 0 ? variance : 0);
      if (stddev < 0.25f) {
        float boost = 1.0f + 2.0f * (0.25f - stddev) / 0.25f;
        *cfg->config_depth *= boost;
      }
    }
  }

  const float zmax = 0.18f * (*cfg->config_depth);

  for (int r = 0; r < logo->rows; r++) {
    for (int c = 0; c < logo->cols; c++) {
      if (hmap[r][c] <= 0.0f) {
        gnx[r][c] = gny[r][c] = 0;
        gnz[r][c] = 1;
        continue;
      }
      float dhdx = 0, dhdy = 0;
      if (c > 0 && c < logo->cols - 1)
        dhdx = (hmap[r][c + 1] - hmap[r][c - 1]) * 0.5f;
      else if (c == 0)
        dhdx = hmap[r][c + 1] - hmap[r][c];
      else
        dhdx = hmap[r][c] - hmap[r][c - 1];

      if (r > 0 && r < logo->rows - 1)
        dhdy = (hmap[r + 1][c] - hmap[r - 1][c]) * 0.5f;
      else if (r == 0)
        dhdy = hmap[r + 1][c] - hmap[r][c];
      else
        dhdy = hmap[r][c] - hmap[r - 1][c];

      dhdx /= sx;
      dhdy /= sy;

      float nnx = -dhdx;
      float nny = dhdy;
      float nnz = 1.0f;
      float l = sqrtf(nnx * nnx + nny * nny + nnz * nnz);
      gnx[r][c] = nnx / l;
      gny[r][c] = nny / l;
      gnz[r][c] = nnz / l;
    }
  }

  int subdiv = (int)(cfg->size_scale * sub_rows);
  if (subdiv < sub_rows)
    subdiv = sub_rows;

  int idx = 0;
  for (int row = 0; row < logo->rows; row++) {
    for (int col = 0; col < logo->cols; col++) {
      float h = hmap[row][col];
      if (h <= 0.0f)
        continue;

      for (int sr = 0; sr < subdiv; sr++) {
        for (int sc = 0; sc < subdiv; sc++) {
          float frow = row + (float)sr / subdiv;
          float fcol = col + (float)sc / subdiv;

          // Interpolate height from neighbors
          float ih = h;
          if (sr > 0 || sc > 0) {
            float fr = (float)sr / subdiv;
            float fc = (float)sc / subdiv;
            int nr = row + (sr > 0 ? 1 : 0);
            int nc = col + (sc > 0 ? 1 : 0);
            if (nr >= logo->rows)
              nr = logo->rows - 1;
            if (nc >= logo->cols)
              nc = logo->cols - 1;
            float h00 = hmap[row][col];
            float h10 = hmap[nr][col];
            float h01 = hmap[row][nc];
            float h11 = hmap[nr][nc];
            ih = h00 * (1 - fr) * (1 - fc) + h10 * fr * (1 - fc) +
                 h01 * (1 - fr) * fc + h11 * fr * fc;
            if (ih <= 0.0f)
              continue;
          }

          float ox = (fcol - cx) * sx;
          float oy = (cy - frow) * sy;
          float zr = ih * zmax;

          // Only add side layers for interior cells. Edge cells
          // (adjacent to empty space) only get front + back to avoid
          // "tail" artifacts during rotation.
          int is_edge = 0;
          for (int dr = -1; dr <= 1 && !is_edge; dr++) {
            for (int dc = -1; dc <= 1 && !is_edge; dc++) {
              if (dr == 0 && dc == 0)
                continue;
              int nr = row + dr, nc = col + dc;
              float nh = 0;
              if (nr >= 0 && nr < logo->rows && nc >= 0 && nc < logo->cols)
                nh = hmap[nr][nc];
              if (nh <= 0.0f)
                is_edge = 1;
            }
          }
          int layers = (is_edge || ih < 0.15f) ? 2 : Z_LAYERS;

          for (int k = 0; k < layers; k++) {
            if (idx >= MAX_POINTS)
              break;
            float t = ((float)k / (layers - 1)) - 0.5f;
            PX[idx] = ox;
            PY[idx] = oy;
            PZ[idx] = t * 2.0f * zr;
            PCOLOR[idx] = logo->has_ansi ? logo->cell_color[row][col]
                                        : (k == 0 || k == layers - 1);

            if (k == 0) {
              NX[idx] = gnx[row][col];
              NY[idx] = gny[row][col];
              NZ[idx] = -gnz[row][col];
            } else if (k == layers - 1) {
              NX[idx] = gnx[row][col];
              NY[idx] = gny[row][col];
              NZ[idx] = gnz[row][col];
            } else {
              float ex = 0, ey = 0;
              for (int dr = -1; dr <= 1; dr++) {
                for (int dc = -1; dc <= 1; dc++) {
                  if (dr == 0 && dc == 0)
                    continue;
                  int nr = row + dr, nc = col + dc;
                  float nh = 0;
                  if (nr >= 0 && nr < logo->rows && nc >= 0 && nc < logo->cols)
                    nh = hmap[nr][nc];
                  if (nh < h) {
                    ex += (float)dc;
                    ey += (float)(-dr);
                  }
                }
              }
              float el = sqrtf(ex * ex + ey * ey);
              if (el > 1e-6f) {
                ex /= el;
                ey /= el;
              }
              float tn = ((float)k / (layers - 1)) * 2.0f - 1.0f;
              float side = sqrtf(1.0f - tn * tn);
              NX[idx] = ex * side;
              NY[idx] = ey * side;
              NZ[idx] = tn;
            }
            idx++;
          }
        }
      }
    }
  }
  POINT_COUNT = idx;
  free(hmap);
  free(gnx);
  free(gny);
  free(gnz);
}

void build_points(void) {
  renderer_logo_t logo = {
    .rows = logo_rows,
    .cols = logo_cols,
    .has_ansi = logo_has_ansi,
    .cell_counts = logo_cell_counts,
    .cells = (const char (*)[MAX_LOGO_COLS][5])logo_cells,
    .cell_color = (const int (*)[MAX_LOGO_COLS])logo_cell_color
  };
  renderer_config_t cfg = {
    .size_scale = size_scale,
    .config_depth = &config_depth,
    .depth_user_set = depth_user_set
  };
  renderer_build_points(&logo, &cfg);
}

void render_compute_half_vector(float lx, float ly, float lz,
                                float *hlx, float *hly, float *hlz) {
  const float hx0 = (lx + 0.0f), hy0 = (ly + 0.0f), hz0 = (lz - 1.0f);
  const float hl0 = sqrtf(hx0 * hx0 + hy0 * hy0 + hz0 * hz0);
  *hlx = hx0 / hl0;
  *hly = hy0 / hl0;
  *hlz = hz0 / hl0;
}

void render_project_points(float A, float B, float K1, float K2,
                           float y_center, int aw, int r_height,
                           float lx, float ly, float lz,
                           float hlx, float hly, float hlz) {
  const float cA = cosf(A), sA = sinf(A);
  const float cB = cosf(B), sB = sinf(B);
  const float k1x2 = K1 * 2.0f;
  const float half_aw = (float)aw * 0.5f;

  for (int i = 0; i < POINT_COUNT; i++) {
    float px = PX[i], py = PY[i], pz = PZ[i];
    float nx = NX[i], ny = NY[i], nz = NZ[i];

    float y1 = py * cA - pz * sA;
    float z1 = py * sA + pz * cA;
    float x2 = px * cB + z1 * sB;
    float z2 = -px * sB + z1 * cB;
    float y2 = y1;

    float ny1 = ny * cA - nz * sA;
    float nz1 = ny * sA + nz * cA;
    float nx2 = nx * cB + nz1 * sB;
    float nz2 = -nx * sB + nz1 * cB;
    float ny2 = ny1;

    float zc = z2 + K2;
    if (zc < 0.1f)
      continue;
    float ooz = 1.0f / zc;
    int xs = (int)((half_aw + k1x2 * x2 * ooz) * sub_cols);
    int ys = (int)((y_center - K1 * y2 * ooz) * sub_rows);
    if (xs < 0 || xs >= aw * sub_cols || ys < 0 ||
        ys >= r_height * sub_rows)
      continue;

    if (ooz > zbuf[ys][xs]) {
      float diff = nx2 * lx + ny2 * ly + nz2 * lz;
      if (diff < 0)
        diff = 0;

      float spec_dot = nx2 * hlx + ny2 * hly + nz2 * hlz;
      if (spec_dot < 0)
        spec_dot = 0;
      float spec = spec_dot * spec_dot;
      spec = spec * spec;
      spec = spec * spec;

      float L = 0.08f + 0.62f * diff + 0.30f * spec;
      if (L > 1.0f)
        L = 1.0f;

      zbuf[ys][xs] = ooz;
      lumbuf[ys][xs] = L;
      colorbuf[ys][xs] = PCOLOR[i];
    }
  }
}
