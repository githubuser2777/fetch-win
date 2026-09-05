#define _GNU_SOURCE
#ifndef FETCH_TESTING
#define FETCH_TESTING
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "src/platform/platform.h"

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

/* -------------------------------------------------------------
 * Test Suite 1: Terminal Initialization & Idempotent Cleanup
 * ------------------------------------------------------------- */
static void test_terminal_lifecycle(void) {
    TEST_SECTION("Terminal Initialization & Idempotent Cleanup");

    platform_term_caps_t caps;
    memset(&caps, 0xFF, sizeof(caps));

    int res = platform_terminal_init(&caps);
    TEST_ASSERT(res == 0, "platform_terminal_init returns 0 (success)");
    TEST_ASSERT(caps.is_tty == 0 || caps.is_tty == 1, "caps.is_tty is a valid boolean");
    TEST_ASSERT(caps.supports_vt == 0 || caps.supports_vt == 1, "caps.supports_vt is a valid boolean");
    TEST_ASSERT(caps.supports_mouse == 0 || caps.supports_mouse == 1, "caps.supports_mouse is a valid boolean");

    /* Initial cleanup */
    platform_terminal_cleanup();
    TEST_ASSERT(1, "platform_terminal_cleanup completes without crash");

    /* Idempotent cleanup calls */
    platform_terminal_cleanup();
    platform_terminal_cleanup();
    TEST_ASSERT(1, "Multiple consecutive platform_terminal_cleanup calls are idempotent");

    /* Re-initialization cycle */
    res = platform_terminal_init(NULL);
    TEST_ASSERT(res == 0, "platform_terminal_init accepts NULL caps pointer");
    platform_terminal_cleanup();
    TEST_ASSERT(1, "Cleanup after re-initialization completes cleanly");

    /* Test honest capability reporting under different TERM settings */
    char orig_term[128] = "";
    char *cur_term = getenv("TERM");
    if (cur_term) strncpy(orig_term, cur_term, sizeof(orig_term) - 1);

#ifdef _WIN32
    _putenv("TERM=dumb");
#else
    setenv("TERM", "dumb", 1);
#endif
    platform_term_caps_t caps_dumb;
    platform_terminal_init(&caps_dumb);
    TEST_ASSERT(caps_dumb.supports_vt == 0, "TERM=dumb reports supports_vt = 0");
    TEST_ASSERT(caps_dumb.supports_mouse == 0, "TERM=dumb reports supports_mouse = 0");
    platform_terminal_cleanup();

#ifdef _WIN32
    _putenv("TERM=vt100");
#else
    setenv("TERM", "vt100", 1);
#endif
    platform_term_caps_t caps_vt100;
    platform_terminal_init(&caps_vt100);
    TEST_ASSERT(caps_vt100.supports_mouse == 0, "TERM=vt100 reports supports_mouse = 0");
    platform_terminal_cleanup();

    /* Restore original TERM */
    if (orig_term[0]) {
#ifdef _WIN32
        char env_buf[200];
        snprintf(env_buf, sizeof(env_buf), "TERM=%s", orig_term);
        _putenv(env_buf);
#else
        setenv("TERM", orig_term, 1);
#endif
    } else {
#ifdef _WIN32
        _putenv("TERM=");
#else
        unsetenv("TERM");
#endif
    }
}

/* -------------------------------------------------------------
 * Test Suite 2: Terminal Size & Fallback
 * ------------------------------------------------------------- */
static void test_terminal_size(void) {
    TEST_SECTION("Terminal Size & Fallback Queries");

    /* NULL pointers safety check */
    platform_get_term_size(NULL, NULL);
    TEST_ASSERT(1, "platform_get_term_size safely handles NULL pointers");

    int rows = -1, cols = -1;
    platform_get_term_size(&rows, &cols);
    TEST_ASSERT(rows >= 0, "platform_get_term_size returns non-negative rows");
    TEST_ASSERT(cols >= 0, "platform_get_term_size returns non-negative cols");

    int only_rows = -1;
    platform_get_term_size(&only_rows, NULL);
    TEST_ASSERT(only_rows >= 0, "platform_get_term_size handles NULL cols");

    int only_cols = -1;
    platform_get_term_size(NULL, &only_cols);
    TEST_ASSERT(only_cols >= 0, "platform_get_term_size handles NULL rows");
}

/* -------------------------------------------------------------
 * Test Suite 3: Resize Detection
 * ------------------------------------------------------------- */
static void test_resize_detection(void) {
    TEST_SECTION("Resize Detection State Machine");

    /* Clear any stale resize flag */
    platform_check_resize();

    TEST_ASSERT(platform_check_resize() == 0, "Initial platform_check_resize returns 0");

    /* Trigger simulated resize signal */
    platform_set_resized_for_test(1);
    TEST_ASSERT(platform_check_resize() == 1, "platform_check_resize returns 1 after resize trigger");
    TEST_ASSERT(platform_check_resize() == 0, "platform_check_resize automatically resets flag to 0");
    TEST_ASSERT(platform_check_resize() == 0, "Second check remains 0");
}

/* -------------------------------------------------------------
 * Test Suite 4: Interruption State
 * ------------------------------------------------------------- */
static void test_interruption_state(void) {
    TEST_SECTION("Signal / Interruption State");

    platform_set_interrupted_for_test(0);
    TEST_ASSERT(platform_is_interrupted() == 0, "Initial platform_is_interrupted returns 0");

    /* Simulate SIGINT / SIGTERM flag */
    platform_set_interrupted_for_test(1);
    TEST_ASSERT(platform_is_interrupted() == 1, "platform_is_interrupted returns 1 when signal received");

    /* Clear interrupt flag */
    platform_set_interrupted_for_test(0);
    TEST_ASSERT(platform_is_interrupted() == 0, "platform_is_interrupted returns 0 after reset");
}

/* -------------------------------------------------------------
 * Test Suite 5: SGR Mouse Drag & Release Parsing
 * ------------------------------------------------------------- */
static void test_mouse_parsing(void) {
    TEST_SECTION("SGR Mouse Drag / Release Parsing & Delta Calculation");

    platform_reset_input_state_for_test();
    platform_mouse_event_t mev;
    size_t consumed = 0;

    /* Empty buffer */
    platform_input_event_t ev = platform_parse_input_chunk("", 0, &mev, &consumed);
    TEST_ASSERT(ev == INPUT_NONE, "Empty buffer returns INPUT_NONE");

    /* Incomplete sequence */
    ev = platform_parse_input_chunk("\033[<", 3, &mev, &consumed);
    TEST_ASSERT(ev == INPUT_NONE, "Incomplete sequence returns INPUT_NONE without consuming");
    TEST_ASSERT(consumed == 0, "Incomplete sequence leaves consumed as 0");

    /* Mouse Down: \033[<0;20;30M (button 0 down at col 20, row 30) */
    ev = platform_parse_input_chunk("\033[<0;20;30M", 11, &mev, &consumed);
    TEST_ASSERT(ev == INPUT_NONE, "Button down starts dragging without emitting drag event yet");
    TEST_ASSERT(consumed == 11, "Consumed full mouse down escape sequence");

    /* Mouse Drag 1: \033[<32;25;38M (btn 32 drag to col 25, row 38) */
    ev = platform_parse_input_chunk("\033[<32;25;38M", 12, &mev, &consumed);
    TEST_ASSERT(ev == INPUT_MOUSE_DRAG, "Drag event returns INPUT_MOUSE_DRAG");
    TEST_ASSERT(mev.btn == 32, "Drag event button is 32");
    TEST_ASSERT(mev.x == 25, "Drag event coordinate x is 25");
    TEST_ASSERT(mev.y == 38, "Drag event coordinate y is 38");
    TEST_ASSERT(mev.dx == 5, "Drag event relative dx is +5 (25 - 20)");
    TEST_ASSERT(mev.dy == 8, "Drag event relative dy is +8 (38 - 30)");
    TEST_ASSERT(consumed == 12, "Consumed full mouse drag escape sequence");

    /* Mouse Drag 2 (negative deltas): \033[<32;18;35M */
    ev = platform_parse_input_chunk("\033[<32;18;35M", 12, &mev, &consumed);
    TEST_ASSERT(ev == INPUT_MOUSE_DRAG, "Second drag event returns INPUT_MOUSE_DRAG");
    TEST_ASSERT(mev.dx == -7, "Relative dx is -7 (18 - 25)");
    TEST_ASSERT(mev.dy == -3, "Relative dy is -3 (35 - 38)");

    /* Mouse Up / Release: \033[<0;18;35m */
    ev = platform_parse_input_chunk("\033[<0;18;35m", 12, &mev, &consumed);
    TEST_ASSERT(ev == INPUT_MOUSE_UP, "Mouse release returns INPUT_MOUSE_UP");
    TEST_ASSERT(mev.x == 18 && mev.y == 35, "Mouse up retains position");

    /* Mouse drag event received while NOT dragging -> ignored */
    ev = platform_parse_input_chunk("\033[<32;50;50M", 12, &mev, &consumed);
    TEST_ASSERT(ev == INPUT_NONE, "Drag event without prior mouse-down is safely ignored");

    /* Non-mouse ANSI escape sequence: \033[A (arrow key) */
    ev = platform_parse_input_chunk("\033[A", 3, &mev, &consumed);
    TEST_ASSERT(ev == INPUT_NONE, "Other escape sequences do not produce mouse events");
    TEST_ASSERT(consumed == 3, "Other escape sequence is skipped");
}

/* -------------------------------------------------------------
 * Test Suite 6: Keypress Exit & Passthrough Behavior
 * ------------------------------------------------------------- */
static void test_keypress_and_passthrough(void) {
    TEST_SECTION("Normal Keypress Exit & Shell Passthrough");

    platform_reset_input_state_for_test();
    platform_mouse_event_t mev;
    size_t consumed = 0;

    /* Regular character keys trigger exit */
    platform_input_event_t ev = platform_parse_input_chunk("q", 1, &mev, &consumed);
    TEST_ASSERT(ev == INPUT_EXIT_KEY, "Character 'q' returns INPUT_EXIT_KEY");
    TEST_ASSERT(consumed == 1, "Consumed 1 byte for keypress");

    ev = platform_parse_input_chunk(" ", 1, &mev, &consumed);
    TEST_ASSERT(ev == INPUT_EXIT_KEY, "Space returns INPUT_EXIT_KEY");

    ev = platform_parse_input_chunk("\n", 1, &mev, &consumed);
    TEST_ASSERT(ev == INPUT_EXIT_KEY, "Enter returns INPUT_EXIT_KEY");

    /*
     * PASSTHROUGH HARD REQUIREMENT:
     * When a single byte is pending in the tty buffer, platform_poll_input()
     * MUST return INPUT_EXIT_KEY WITHOUT reading or consuming the byte!
     */
    platform_set_pending_bytes_for_test(1);
    ev = platform_poll_input(&mev);
    TEST_ASSERT(ev == INPUT_EXIT_KEY, "Pending single byte in tty triggers INPUT_EXIT_KEY");
    TEST_ASSERT(platform_get_pending_bytes_for_test() == 1,
                "PASSTHROUGH CONFIRMED: Pending byte was NOT consumed from tty buffer");

    /* Reset pending bytes test state */
    platform_set_pending_bytes_for_test(-1);

    /* Zero pending bytes returns INPUT_NONE */
    platform_set_pending_bytes_for_test(0);
    ev = platform_poll_input(&mev);
    TEST_ASSERT(ev == INPUT_NONE, "Zero pending bytes returns INPUT_NONE");
    platform_set_pending_bytes_for_test(-1);
}

/* -------------------------------------------------------------
 * Test Suite 7: Output & Frame Timing Wrappers
 * ------------------------------------------------------------- */
static void test_output_and_timing(void) {
    TEST_SECTION("Output & Frame Timing Wrappers");

    /* Writing 0 bytes or NULL */
    int ret = platform_write_output(NULL, 0);
    TEST_ASSERT(ret == 0, "platform_write_output with NULL buffer returns 0");

    ret = platform_write_output("test", 0);
    TEST_ASSERT(ret == 0, "platform_write_output with len 0 returns 0");

    /* Frame sleep timing */
    clock_t t0 = clock();
    platform_sleep_frame(1000); /* 1 millisecond */
    clock_t t1 = clock();
    TEST_ASSERT(t1 >= t0, "platform_sleep_frame completes forward in time");
}

/* -------------------------------------------------------------
 * Test Suite 8: Platform Paths & OS Identification
 * ------------------------------------------------------------- */
static void test_platform_paths_os(void) {
    TEST_SECTION("Platform Paths & OS Identification");

    char config_path[512] = "";
    platform_get_config_path(config_path, sizeof(config_path));
    TEST_ASSERT(strlen(config_path) > 0, "platform_get_config_path returns non-empty path");
    TEST_ASSERT(strstr(config_path, "config") != NULL, "Config path contains 'config'");

    char logo_path[512] = "";
    platform_get_logo_path(logo_path, sizeof(logo_path));
    TEST_ASSERT(strlen(logo_path) > 0, "platform_get_logo_path returns non-empty path");
    TEST_ASSERT(strstr(logo_path, "logo.txt") != NULL, "Logo path contains 'logo.txt'");

    /* Test with explicit custom HOME */
    char orig_home[256] = "";
    char *cur_home = getenv("HOME");
    if (cur_home) strncpy(orig_home, cur_home, sizeof(orig_home) - 1);

#ifdef _WIN32
    _putenv("HOME=C:\\mock_home");
    _putenv("XDG_CONFIG_HOME=");
#else
    setenv("HOME", "/mock_home", 1);
    unsetenv("XDG_CONFIG_HOME");
#endif

    config_path[0] = '\0';
    platform_get_config_path(config_path, sizeof(config_path));
    TEST_ASSERT(strstr(config_path, "mock_home") != NULL,
                "platform_get_config_path respects custom HOME directory");

    logo_path[0] = '\0';
    platform_get_logo_path(logo_path, sizeof(logo_path));
    TEST_ASSERT(strstr(logo_path, "mock_home") != NULL,
                "platform_get_logo_path respects custom HOME directory");

    /* Restore HOME */
    if (orig_home[0]) {
#ifdef _WIN32
        char env_buf[300];
        snprintf(env_buf, sizeof(env_buf), "HOME=%s", orig_home);
        _putenv(env_buf);
#else
        setenv("HOME", orig_home, 1);
#endif
    }

    /* OS detection */
    char os_id[64] = "";
    int detected = platform_detect_os_id(os_id, sizeof(os_id));
    TEST_ASSERT(detected == 0 || detected == 1, "platform_detect_os_id returns valid status code");
}

/* -------------------------------------------------------------
 * Main Test Runner
 * ------------------------------------------------------------- */
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("====================================================\n");
    printf("  FETCH PHASE 3 PLATFORM ABSTRACTION UNIT TESTS     \n");
    printf("  Testing isolated src/platform (POSIX backend)     \n");
    printf("====================================================\n");

    test_terminal_lifecycle();
    test_terminal_size();
    test_resize_detection();
    test_interruption_state();
    test_mouse_parsing();
    test_keypress_and_passthrough();
    test_output_and_timing();
    test_platform_paths_os();

    printf("\n====================================================\n");
    printf("  PHASE 3 TEST SUMMARY: %d / %d passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf(" (%d FAILED)", tests_failed);
    } else {
        printf(" (100%% SUCCESS)");
    }
    printf("\n====================================================\n");

    return tests_failed == 0 ? 0 : 1;
}
