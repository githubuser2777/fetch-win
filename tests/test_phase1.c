#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "src/core/common.h"
#include "src/renderer/renderer.h"

// Globals required by backward-compatible build_points() bridge
int logo_rows = 0;
int logo_cols = 0;
int logo_has_ansi = 0;
int logo_cell_counts[MAX_LOGO_ROWS];
char logo_cells[MAX_LOGO_ROWS][MAX_LOGO_COLS][5];
int logo_cell_color[MAX_LOGO_ROWS][MAX_LOGO_COLS];
float size_scale = 1.0f;
float config_depth = 1.0f;
int depth_user_set = 0;
int render_height = 36;

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    tests_run++; \
    if (cond) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        fprintf(stderr, "  [FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg); \
    } \
} while(0)

#define TEST_SECTION(title) printf("\n=== %s ===\n", title)

// -------------------------------------------------------------
// Unit Tests: Core Utilities (src/core/common.h)
// -------------------------------------------------------------
static void test_core_common_utilities(void) {
    TEST_SECTION("Unit Tests: Core Utilities (common.h/.c)");

    // 1. utf8_char_len
    TEST_ASSERT(utf8_char_len('x') == 1, "ASCII length is 1");
    TEST_ASSERT(utf8_char_len((unsigned char)'\0') == 1, "NUL length is 1");
    TEST_ASSERT(utf8_char_len((unsigned char)"\xC2\xA2"[0]) == 2, "2-byte UTF-8 lead byte length is 2");
    TEST_ASSERT(utf8_char_len((unsigned char)"\xE2\x82\xAC"[0]) == 3, "3-byte UTF-8 lead byte length is 3");
    TEST_ASSERT(utf8_char_len((unsigned char)"\xF0\x90\x8D\x88"[0]) == 4, "4-byte UTF-8 lead byte length is 4");
    TEST_ASSERT(utf8_char_len((unsigned char)0xFF) == 1, "Invalid UTF-8 lead byte defaults to 1");

    // 2. skip_ansi
    TEST_ASSERT(skip_ansi("\033[0m") == 4, "skip_ansi for reset sequence");
    TEST_ASSERT(skip_ansi("\033[1;31m") == 7, "skip_ansi for bold red sequence");
    TEST_ASSERT(skip_ansi("\033[38;2;12;34;56m") == 16, "skip_ansi for 24-bit sequence");
    TEST_ASSERT(skip_ansi("not_ansi") == 0, "skip_ansi for plain text");
    TEST_ASSERT(skip_ansi("\033") == 0, "skip_ansi for lone ESC");
    TEST_ASSERT(skip_ansi("\033[") == 2, "skip_ansi for unterminated escape prefix");

    // 3. is_cursor_escape
    TEST_ASSERT(is_cursor_escape("\033[H") == 1, "Cursor home is cursor escape");
    TEST_ASSERT(is_cursor_escape("\033[2J") == 1, "Erase display is cursor escape");
    TEST_ASSERT(is_cursor_escape("\033[?25l") == 1, "Cursor hide is cursor escape");
    TEST_ASSERT(is_cursor_escape("\033[10;20H") == 1, "Positioning is cursor escape");
    TEST_ASSERT(is_cursor_escape("\033[31m") == 0, "Color SGR is not cursor escape");
    TEST_ASSERT(is_cursor_escape("\033[0m") == 0, "Reset SGR is not cursor escape");
    TEST_ASSERT(is_cursor_escape("text") == 0, "Plain text is not cursor escape");

    // 4. strip_inline_hint
    char h1[64] = "blue (cyan, green, ...)";
    strip_inline_hint(h1);
    TEST_ASSERT(strcmp(h1, "blue") == 0, "Strips trailing parenthesized documentation hint");

    char h2[64] = "trimmed   \t  ";
    strip_inline_hint(h2);
    TEST_ASSERT(strcmp(h2, "trimmed") == 0, "Trims trailing whitespace");

    char h3[64] = "clean";
    strip_inline_hint(h3);
    TEST_ASSERT(strcmp(h3, "clean") == 0, "Preserves already clean string");

    char h4[64] = "";
    strip_inline_hint(h4);
    TEST_ASSERT(strcmp(h4, "") == 0, "Handles empty string");

    // 5. visible_width
    TEST_ASSERT(visible_width("FetchWin") == 8, "Plain ASCII visible width");
    TEST_ASSERT(visible_width("\033[1;34mFetchWin\033[0m") == 8, "Visible width ignores ANSI escapes");
    TEST_ASSERT(visible_width("█░▒▓") == 4, "Unicode block elements count 1 col each");
    TEST_ASSERT(visible_width("\033[31m█\033[0m\033[32m░\033[0m") == 2, "Mixed ANSI and Unicode block visible width");
    TEST_ASSERT(visible_width("") == 0, "Empty string has 0 visible width");

    // 6. emit_clipped
    char buf[128];
    char *end = buf + sizeof(buf);

    char *p = emit_clipped(buf, end, "Short text", 20);
    *p = '\0';
    TEST_ASSERT(strcmp(buf, "Short text") == 0, "Short string is unclipped");

    p = emit_clipped(buf, end, "1234567890abcdef", 8);
    *p = '\0';
    TEST_ASSERT(visible_width(buf) <= 8, "Clipped string satisfies max_cols");
    TEST_ASSERT(strstr(buf, "...") != NULL, "Clipped string contains ellipsis");

    p = emit_clipped(buf, end, "\033[31mRed text exceeding column budget\033[0m", 10);
    *p = '\0';
    TEST_ASSERT(strstr(buf, "\033[0m") != NULL, "Clipped string restores color reset");

    p = emit_clipped(buf, end, "Unlimited width", -1);
    *p = '\0';
    TEST_ASSERT(strcmp(buf, "Unlimited width") == 0, "max_cols < 0 means no limit");
}

// -------------------------------------------------------------
// Unit Tests: Renderer Module (src/renderer/renderer.h)
// -------------------------------------------------------------
static void test_renderer_math_and_pipeline(void) {
    TEST_SECTION("Unit Tests: Renderer Pipeline & 3D Math");

    // 1. render_compute_half_vector
    float lx = 0.4082f, ly = 0.8165f, lz = -0.4082f;
    float hlx = 0, hly = 0, hlz = 0;
    render_compute_half_vector(lx, ly, lz, &hlx, &hly, &hlz);
    float hlen = sqrtf(hlx * hlx + hly * hly + hlz * hlz);
    TEST_ASSERT(fabsf(hlen - 1.0f) < 0.001f, "Half-vector is properly normalized to unit length");
    TEST_ASSERT(hlz < 0.0f, "Half-vector z-component points towards camera");

    // 2. select_shading & glyphs
    TEST_ASSERT(select_shading("ascii", NULL) == 1, "select_shading accepts ascii");
    TEST_ASSERT(sub_rows == 1 && sub_cols == 1, "ASCII mode sets 1x1 sub-cell grid");

    TEST_ASSERT(select_shading("blocks", NULL) == 1, "select_shading accepts blocks");
    TEST_ASSERT(sub_rows == 2 && sub_cols == 2, "Blocks mode sets 2x2 sub-cell grid");

    TEST_ASSERT(select_shading("sextants", NULL) == 1, "select_shading accepts sextants");
    TEST_ASSERT(sub_rows == 3 && sub_cols == 2, "Sextants mode sets 2x3 sub-cell grid");
    TEST_ASSERT(strcmp(sextant_glyphs[21], "▌") == 0, "Sextant mask 21 is left half block");
    TEST_ASSERT(strcmp(sextant_glyphs[42], "▐") == 0, "Sextant mask 42 is right half block");

    TEST_ASSERT(select_shading("ascii", "@#") == 1, "select_shading with custom ramp");
    TEST_ASSERT(shading_count == 2, "Custom ramp parsed to 2 entries");
    TEST_ASSERT(strcmp(shading_chars[0], "@") == 0, "Ramp char 0 is '@'");
    TEST_ASSERT(strcmp(shading_chars[1], "#") == 0, "Ramp char 1 is '#'");

    TEST_ASSERT(select_shading("unknown", NULL) == 0, "Rejects unknown shading mode");

    // 3. char_weight_utf8
    TEST_ASSERT(char_weight_utf8("M") == 1.0f, "'M' has maximum ASCII weight 1.0");
    TEST_ASSERT(char_weight_utf8(" ") == 0.0f, "Space character has weight 0.0");
    TEST_ASSERT(char_weight_utf8("\xe2\x96\x88") == 1.0f, "Full block U+2588 has weight 1.0");
    TEST_ASSERT(char_weight_utf8("\xe2\x96\x93") == 0.75f, "Dark shade has weight 0.75");
    TEST_ASSERT(char_weight_utf8("\xe2\x96\x92") == 0.50f, "Medium shade has weight 0.50");
    TEST_ASSERT(char_weight_utf8("\xe2\x96\x91") == 0.25f, "Light shade has weight 0.25");
    TEST_ASSERT(char_weight_utf8("\xe2\x95\xad") == 0.20f, "Box-drawing rounded corner has weight 0.20");

    // Braille weight (popcount of continuation byte / 8)
    // U+2800 (blank Braille): E2 A0 80 -> byte 0x80 popcount 1 -> 1/8 = 0.125f
    TEST_ASSERT(fabsf(char_weight_utf8("\xe2\xa0\x80") - 0.125f) < 0.001f, "Blank Braille character has weight 0.125");
    // U+2801 (dot 1): E2 A0 81 -> byte 0x81 popcount 2 -> 2/8 = 0.25f
    TEST_ASSERT(fabsf(char_weight_utf8("\xe2\xa0\x81") - 0.25f) < 0.001f, "1-dot Braille character has weight 0.25");
    // U+28FF (8 dots): E2 A3 BF -> byte 0xBF popcount 7 -> 7/8 = 0.875f
    TEST_ASSERT(fabsf(char_weight_utf8("\xe2\xa3\xbf") - 0.875f) < 0.001f, "8-dot Braille character has weight 0.875");

    // 4. clear_buf & cell_glyph
    render_height = 10;
    sub_rows = 1;
    sub_cols = 1;
    select_shading("ascii", NULL);
    clear_buf();

    int col_out = -1;
    TEST_ASSERT(cell_glyph(0, 0, shading_count - 1, &col_out) == NULL, "Empty buffer cell produces NULL glyph");

    zbuf[0][0] = 0.5f;
    lumbuf[0][0] = 0.9f;
    colorbuf[0][0] = 33; // yellow
    const char *g = cell_glyph(0, 0, shading_count - 1, &col_out);
    TEST_ASSERT(g != NULL, "Populated buffer produces valid glyph");
    TEST_ASSERT(col_out == 33, "Populated buffer extracts correct cell color");

    // 5. Decoupled renderer_build_points
    // Synthetic 3x3 logo
    char test_cells[MAX_LOGO_ROWS][MAX_LOGO_COLS][5] = {
        {"#", "M", "#"},
        {" ", "█", " "},
        {"#", " ", "#"}
    };
    int test_cell_colors[MAX_LOGO_ROWS][MAX_LOGO_COLS] = {
        {31, 32, 33},
        {0,  34, 0},
        {35, 0,  36}
    };
    int test_counts[MAX_LOGO_ROWS] = {3, 3, 3};

    renderer_logo_t logo = {
        .rows = 3,
        .cols = 3,
        .has_ansi = 1,
        .cell_counts = test_counts,
        .cells = (const char (*)[MAX_LOGO_COLS][5])test_cells,
        .cell_color = (const int (*)[MAX_LOGO_COLS])test_cell_colors
    };

    float test_depth = 1.0f;
    renderer_config_t cfg = {
        .size_scale = 1.0f,
        .config_depth = &test_depth,
        .depth_user_set = 1
    };

    renderer_build_points(&logo, &cfg);
    TEST_ASSERT(POINT_COUNT > 0, "renderer_build_points generated 3D points");
    TEST_ASSERT(POINT_COUNT < MAX_POINTS, "POINT_COUNT within maximum limit");

    int all_finite = 1;
    for (int i = 0; i < POINT_COUNT; i++) {
        if (isnan(PX[i]) || isinf(PX[i]) ||
            isnan(PY[i]) || isinf(PY[i]) ||
            isnan(PZ[i]) || isinf(PZ[i]) ||
            isnan(NX[i]) || isinf(NX[i]) ||
            isnan(NY[i]) || isinf(NY[i]) ||
            isnan(NZ[i]) || isinf(NZ[i])) {
            all_finite = 0;
            break;
        }
    }
    TEST_ASSERT(all_finite == 1, "Generated point coordinates and surface normals are finite");

    // 6. render_project_points
    render_height = 20;
    sub_rows = 1;
    sub_cols = 1;
    clear_buf();

    // Manually define 2 test points: one in front, one behind
    POINT_COUNT = 2;
    // Point 0: farther (larger z)
    PX[0] = 0.0f; PY[0] = 0.0f; PZ[0] = 2.0f;
    NX[0] = 0.0f; NY[0] = 0.0f; NZ[0] = 1.0f;
    PCOLOR[0] = 31;

    // Point 1: closer (smaller z)
    PX[1] = 0.0f; PY[1] = 0.0f; PZ[1] = -1.0f;
    NX[1] = 0.0f; NY[1] = 0.0f; NZ[1] = 1.0f;
    PCOLOR[1] = 32;

    float A = 0.0f, B = 0.0f;
    float K1 = 37.0f;
    float K2 = 5.5f;
    float y_center = 10.0f;
    int aw = 40;

    render_project_points(A, B, K1, K2, y_center, aw, render_height,
                          lx, ly, lz, hlx, hly, hlz);

    // Verify depth buffer has depth and color from the closer point (PCOLOR[1] == 32)
    int center_x = aw / 2;
    int center_y = (int)y_center;
    TEST_ASSERT(zbuf[center_y][center_x] > 0.0f, "Projected point landed in buffer");
    TEST_ASSERT(colorbuf[center_y][center_x] == 32, "Depth sorting selected closer point (color 32)");
    TEST_ASSERT(lumbuf[center_y][center_x] >= 0.08f && lumbuf[center_y][center_x] <= 1.0f,
                "Luminance calculated within valid [0.08, 1.0] range");
}

// -------------------------------------------------------------
// Main Test Runner
// -------------------------------------------------------------
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("====================================================\n");
    printf("  FETCH PHASE 1 EXTRACTION UNIT TESTS               \n");
    printf("  Testing isolated src/core and src/renderer modules\n");
    printf("====================================================\n");

    test_core_common_utilities();
    test_renderer_math_and_pipeline();

    printf("\n====================================================\n");
    printf("  PHASE 1 TEST SUMMARY: %d / %d passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf(" (%d FAILED)", tests_failed);
    } else {
        printf(" (100%% SUCCESS)");
    }
    printf("\n====================================================\n");

    return tests_failed == 0 ? 0 : 1;
}
