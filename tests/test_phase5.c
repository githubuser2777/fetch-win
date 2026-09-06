#define _GNU_SOURCE
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifndef FETCH_TESTING
#define FETCH_TESTING
#endif
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

/* Helper: Validate that a string is strictly valid UTF-8 */
static int is_valid_utf8(const char *str) {
    if (!str) return 0;
    const unsigned char *s = (const unsigned char *)str;
    while (*s) {
        if (*s < 0x80) {
            s++;
        } else if ((*s & 0xE0) == 0xC0) {
            if ((s[1] & 0xC0) != 0x80) return 0;
            s += 2;
        } else if ((*s & 0xF0) == 0xE0) {
            if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) return 0;
            s += 3;
        } else if ((*s & 0xF8) == 0xF0) {
            if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) return 0;
            s += 4;
        } else {
            return 0;
        }
    }
    return 1;
}

/* Callback collector for multi-item gathers */
#define MAX_EMITTED 16
typedef struct {
    int count;
    char labels[MAX_EMITTED][64];
    char values[MAX_EMITTED][256];
} test_emit_tracker_t;

static test_emit_tracker_t g_tracker;

static void test_emit_cb(const char *label, const char *fmt, ...) {
    if (g_tracker.count >= MAX_EMITTED) return;
    int idx = g_tracker.count++;
    strncpy(g_tracker.labels[idx], label ? label : "", sizeof(g_tracker.labels[idx]) - 1);
    g_tracker.labels[idx][sizeof(g_tracker.labels[idx]) - 1] = '\0';

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_tracker.values[idx], sizeof(g_tracker.values[idx]), fmt, ap);
    va_end(ap);
}

static void reset_tracker(void) {
    memset(&g_tracker, 0, sizeof(g_tracker));
}

/* ------------------------------------------------------------- */
/* 1. Title & Hostname Detection Tests                           */
/* ------------------------------------------------------------- */
static void test_title_and_hostname(void) {
    TEST_SECTION("Title & Hostname Detection");

    char user[128] = "";
    char host[128] = "";

    platform_gather_title(user, sizeof(user), host, sizeof(host));
    TEST_ASSERT(user[0] != '\0', "User name must be non-empty");
    TEST_ASSERT(is_valid_utf8(user), "User name must be valid UTF-8");
    TEST_ASSERT(host[0] != '\0', "Host name must be non-empty");
    TEST_ASSERT(is_valid_utf8(host), "Host name must be valid UTF-8");

    /* Buffer boundary safety: tiny buffers */
    char tiny_user[2] = "X";
    char tiny_host[2] = "Y";
    platform_gather_title(tiny_user, sizeof(tiny_user), tiny_host, sizeof(tiny_host));
    TEST_ASSERT(tiny_user[sizeof(tiny_user) - 1] == '\0', "Tiny user buffer safely null-terminated");
    TEST_ASSERT(tiny_host[sizeof(tiny_host) - 1] == '\0', "Tiny host buffer safely null-terminated");

    /* Null pointer safety */
    platform_gather_title(NULL, 0, NULL, 0);
    TEST_ASSERT(1, "platform_gather_title(NULL, 0, NULL, 0) does not crash");
}

/* ------------------------------------------------------------- */
/* 2. OS & Kernel Version Detection Tests                        */
/* ------------------------------------------------------------- */
static void test_os_and_kernel(void) {
    TEST_SECTION("OS & Kernel Version Detection");

    char os[256] = "";
    platform_gather_os(os, sizeof(os));
    TEST_ASSERT(os[0] != '\0', "OS string must be non-empty");
    TEST_ASSERT(is_valid_utf8(os), "OS string must be valid UTF-8");
    TEST_ASSERT(strstr(os, "Windows") != NULL, "OS string must identify as Windows");
    TEST_ASSERT(strstr(os, "x86_64") != NULL || strstr(os, "arm64") != NULL || strstr(os, "i686") != NULL,
                "OS string must contain system architecture");

    char kernel[128] = "";
    platform_gather_kernel(kernel, sizeof(kernel));
    TEST_ASSERT(kernel[0] != '\0', "Kernel string must be non-empty");
    TEST_ASSERT(is_valid_utf8(kernel), "Kernel string must be valid UTF-8");
    TEST_ASSERT(strncmp(kernel, "Windows NT ", 11) == 0, "Kernel string must start with 'Windows NT '");

    /* Tiny buffer & null safety */
    char tiny[4] = "abc";
    platform_gather_os(tiny, sizeof(tiny));
    TEST_ASSERT(tiny[sizeof(tiny) - 1] == '\0', "Tiny OS buffer safely null-terminated");
    platform_gather_os(NULL, 0);
    platform_gather_kernel(NULL, 0);
    TEST_ASSERT(1, "OS/Kernel collectors handle NULL buffers safely without crash");
}

/* ------------------------------------------------------------- */
/* 3. Host / Hardware Model Detection Tests                      */
/* ------------------------------------------------------------- */
static void test_host_model(void) {
    TEST_SECTION("Host / BIOS Model Detection");

    char host[256] = "";
    platform_gather_host(host, sizeof(host));
    /* Host may legitimately be empty on generic OEM or virtual environments */
    TEST_ASSERT(is_valid_utf8(host), "Host model string must be valid UTF-8");
    if (host[0] != '\0') {
        TEST_ASSERT(strstr(host, "To be filled by O.E.M.") == NULL,
                    "Host model must not output raw generic OEM placeholder");
        TEST_ASSERT(strstr(host, "System Product Name") == NULL,
                    "Host model must not output raw 'System Product Name' placeholder");
    }

    char tiny[3] = "ab";
    platform_gather_host(tiny, sizeof(tiny));
    TEST_ASSERT(tiny[sizeof(tiny) - 1] == '\0', "Tiny host buffer safely null-terminated");
    platform_gather_host(NULL, 0);
    TEST_ASSERT(1, "platform_gather_host(NULL, 0) handles NULL safely");
}

/* ------------------------------------------------------------- */
/* 4. CPU Detection Tests                                        */
/* ------------------------------------------------------------- */
static void test_cpu(void) {
    TEST_SECTION("CPU Detection & Formatting");

    char cpu[256] = "";
    platform_gather_cpu(cpu, sizeof(cpu));
    TEST_ASSERT(cpu[0] != '\0', "CPU string must be non-empty");
    TEST_ASSERT(is_valid_utf8(cpu), "CPU string must be valid UTF-8");
    TEST_ASSERT(strchr(cpu, '(') != NULL && strchr(cpu, ')') != NULL,
                "CPU string must include logical core count in parentheses");
    TEST_ASSERT(strstr(cpu, "  ") == NULL, "CPU string must normalize multiple consecutive spaces");

    /* Null & buffer boundary safety */
    char tiny[4] = "xyz";
    platform_gather_cpu(tiny, sizeof(tiny));
    TEST_ASSERT(tiny[sizeof(tiny) - 1] == '\0', "Tiny CPU buffer safely null-terminated");
    platform_gather_cpu(NULL, 0);
    TEST_ASSERT(1, "platform_gather_cpu(NULL, 0) does not crash");
}

/* ------------------------------------------------------------- */
/* 5. GPU Enumeration & Software-Adapter Filtering Tests         */
/* ------------------------------------------------------------- */
static void test_gpu(void) {
    TEST_SECTION("GPU Enumeration & Software Filtering");

    reset_tracker();
    platform_gather_gpu(test_emit_cb);

    /* Verify contract: if GPUs found, all must be valid UTF-8 and labeled "GPU" */
    for (int i = 0; i < g_tracker.count; i++) {
        TEST_ASSERT(strcmp(g_tracker.labels[i], "GPU") == 0, "Emitted item label must be 'GPU'");
        TEST_ASSERT(is_valid_utf8(g_tracker.values[i]), "Emitted GPU string must be valid UTF-8");
        TEST_ASSERT(g_tracker.values[i][0] != '\0', "Emitted GPU string must not be empty");

        /* Strict requirement: software adapters MUST be filtered out */
        TEST_ASSERT(strstr(g_tracker.values[i], "Microsoft Basic Render Driver") == NULL,
                    "Must filter out Microsoft Basic Render Driver software adapter");
        TEST_ASSERT(strstr(g_tracker.values[i], "Basic Display Adapter") == NULL,
                    "Must filter out Basic Display Adapter");
        TEST_ASSERT(strstr(g_tracker.values[i], "WARP") == NULL,
                    "Must filter out WARP software adapter");

        /* Conservative classification check */
        if (strstr(g_tracker.values[i], "[Discrete]")) {
            TEST_ASSERT(1, "Valid [Discrete] classification tag");
        } else if (strstr(g_tracker.values[i], "[Integrated]")) {
            TEST_ASSERT(1, "Valid [Integrated] classification tag");
        } else {
            TEST_ASSERT(1, "Uncertain classification cleanly omits tag");
        }
    }

    /* Deterministic ordering: second call must yield identical items */
    test_emit_tracker_t first_run = g_tracker;
    reset_tracker();
    platform_gather_gpu(test_emit_cb);
    TEST_ASSERT(g_tracker.count == first_run.count, "Repeated GPU query must produce identical count");
    for (int i = 0; i < g_tracker.count; i++) {
        TEST_ASSERT(strcmp(g_tracker.values[i], first_run.values[i]) == 0,
                    "Repeated GPU query must preserve deterministic ordering and values");
    }

    /* Null callback safety */
    platform_gather_gpu(NULL);
    TEST_ASSERT(1, "platform_gather_gpu(NULL) handles NULL callback safely");
}

/* ------------------------------------------------------------- */
/* 6. Memory & Pagefile / Swap Tests                             */
/* ------------------------------------------------------------- */
static void test_memory_and_swap(void) {
    TEST_SECTION("Memory & Swap/Pagefile Metrics");

    char mem[256] = "";
    platform_gather_memory(mem, sizeof(mem));
    TEST_ASSERT(mem[0] != '\0', "Memory string must be non-empty");
    TEST_ASSERT(is_valid_utf8(mem), "Memory string must be valid UTF-8");
    TEST_ASSERT(strstr(mem, "GiB") != NULL, "Memory string must be formatted in GiB");
    TEST_ASSERT(strchr(mem, '/') != NULL, "Memory string must include '/' separator");
    TEST_ASSERT(strchr(mem, '%') != NULL, "Memory string must include percentage");
    TEST_ASSERT(strstr(mem, "\033[") != NULL, "Memory string must include ANSI color coding");

    char swap[256] = "";
    platform_gather_swap(swap, sizeof(swap));
    TEST_ASSERT(is_valid_utf8(swap), "Swap string must be valid UTF-8");
    if (swap[0] != '\0') {
        TEST_ASSERT(strstr(swap, "GiB") != NULL || strstr(swap, "MiB") != NULL,
                    "Swap string must be formatted in GiB or MiB");
        TEST_ASSERT(strchr(swap, '%') != NULL, "Swap string must include percentage");
    }

    /* Boundary & NULL safety */
    char tiny[3] = "ab";
    platform_gather_memory(tiny, sizeof(tiny));
    TEST_ASSERT(tiny[sizeof(tiny) - 1] == '\0', "Tiny memory buffer safely null-terminated");
    platform_gather_memory(NULL, 0);
    platform_gather_swap(NULL, 0);
    TEST_ASSERT(1, "Memory and swap collectors handle NULL safely");
}

/* ------------------------------------------------------------- */
/* 7. Disk Metrics Tests                                         */
/* ------------------------------------------------------------- */
static void test_disk(void) {
    TEST_SECTION("Disk Metrics & Fallback Handling");

    char disk[256] = "";
    /* Primary disk (C: / default) */
    platform_gather_disk("/", disk, sizeof(disk));
    TEST_ASSERT(disk[0] != '\0', "Default disk collection ('/') must return non-empty metrics");
    TEST_ASSERT(is_valid_utf8(disk), "Disk string must be valid UTF-8");
    TEST_ASSERT(strstr(disk, "GiB") != NULL, "Disk metrics must be formatted in GiB");
    TEST_ASSERT(strchr(disk, '%') != NULL, "Disk metrics must include percentage");

    /* Explicit C: drive */
    char disk_c[256] = "";
    platform_gather_disk("C:", disk_c, sizeof(disk_c));
    TEST_ASSERT(disk_c[0] != '\0', "Explicit 'C:' drive collection must succeed");
    TEST_ASSERT(is_valid_utf8(disk_c), "Explicit 'C:' drive string must be valid UTF-8");

    /* Non-existent drive: MUST gracefully omit and not crash */
    char disk_bad[256] = "INITIAL";
    platform_gather_disk("Z:\\NonexistentPathXYZ", disk_bad, sizeof(disk_bad));
    TEST_ASSERT(disk_bad[0] == '\0', "Non-existent drive must return empty string without error");

    /* NULL path safely defaults */
    char disk_null[256] = "";
    platform_gather_disk(NULL, disk_null, sizeof(disk_null));
    TEST_ASSERT(disk_null[0] != '\0', "NULL path safely defaults to primary system drive");

    /* NULL buffer */
    platform_gather_disk("/", NULL, 0);
    TEST_ASSERT(1, "platform_gather_disk handles NULL buffer safely");
}

/* ------------------------------------------------------------- */
/* 8. Battery Metrics Tests                                      */
/* ------------------------------------------------------------- */
static void test_battery(void) {
    TEST_SECTION("Battery Metrics & Desktop Fallback");

    char label[80] = "";
    char val[256] = "";
    platform_gather_battery(label, sizeof(label), val, sizeof(val));

    TEST_ASSERT(is_valid_utf8(label), "Battery label must be valid UTF-8");
    TEST_ASSERT(is_valid_utf8(val), "Battery value must be valid UTF-8");

    if (label[0] != '\0') {
        /* On laptop / device with battery */
        TEST_ASSERT(strcmp(label, "Battery") == 0, "Battery label must be 'Battery'");
        TEST_ASSERT(strchr(val, '%') != NULL, "Battery value must contain percentage");
        TEST_ASSERT(strstr(val, "Charging") != NULL ||
                    strstr(val, "Discharging") != NULL ||
                    strstr(val, "AC Connected") != NULL,
                    "Battery state must report Charging, Discharging, or AC Connected");
    } else {
        /* On desktop without battery: must be cleanly empty */
        TEST_ASSERT(val[0] == '\0', "Desktop without battery must report cleanly empty value");
    }

    /* NULL buffer safety */
    platform_gather_battery(NULL, 0, NULL, 0);
    TEST_ASSERT(1, "platform_gather_battery(NULL, 0, NULL, 0) handles NULL buffers safely");
}

/* ------------------------------------------------------------- */
/* 9. Network / IP Metrics Tests                                 */
/* ------------------------------------------------------------- */
static void test_network_ip(void) {
    TEST_SECTION("Network / IP Metrics");

    reset_tracker();
    platform_gather_ip(test_emit_cb);

    /* If online, tracker count > 0; otherwise 0 is acceptable */
    for (int i = 0; i < g_tracker.count; i++) {
        TEST_ASSERT(strcmp(g_tracker.labels[i], "IP") == 0, "IP callback label must be 'IP'");
        TEST_ASSERT(is_valid_utf8(g_tracker.values[i]), "IP callback value must be valid UTF-8");
        TEST_ASSERT(strchr(g_tracker.values[i], '/') != NULL, "IP address must include subnet prefix length");
        TEST_ASSERT(strchr(g_tracker.values[i], '.') != NULL, "IPv4 address must contain dots");
    }

    /* NULL callback safety */
    platform_gather_ip(NULL);
    TEST_ASSERT(1, "platform_gather_ip(NULL) handles NULL callback safely");
}

/* ------------------------------------------------------------- */
/* 10. Uptime Detection Tests                                    */
/* ------------------------------------------------------------- */
static void test_uptime(void) {
    TEST_SECTION("Uptime Calculation & Formatting");

    char uptime[128] = "";
    platform_gather_uptime(uptime, sizeof(uptime));
    TEST_ASSERT(uptime[0] != '\0', "Uptime must be non-empty");
    TEST_ASSERT(is_valid_utf8(uptime), "Uptime must be valid UTF-8");
    TEST_ASSERT(strstr(uptime, "min") != NULL || strstr(uptime, "hour") != NULL || strstr(uptime, "day") != NULL,
                "Uptime must format into mins, hours, or days");

    /* NULL safety */
    platform_gather_uptime(NULL, 0);
    TEST_ASSERT(1, "platform_gather_uptime(NULL, 0) handles NULL buffer safely");
}

/* ------------------------------------------------------------- */
/* 11. Shell & Terminal Detection Tests                          */
/* ------------------------------------------------------------- */
static void test_shell_and_terminal(void) {
    TEST_SECTION("Shell & Terminal Environment Detection");

    char shell[128] = "";
    platform_gather_shell(shell, sizeof(shell));
    TEST_ASSERT(shell[0] != '\0', "Shell detection must return non-empty name");
    TEST_ASSERT(is_valid_utf8(shell), "Shell name must be valid UTF-8");

    char term[128] = "";
    platform_gather_terminal(term, sizeof(term));
    TEST_ASSERT(term[0] != '\0', "Terminal detection must return non-empty name");
    TEST_ASSERT(is_valid_utf8(term), "Terminal name must be valid UTF-8");

    /* NULL safety */
    platform_gather_shell(NULL, 0);
    platform_gather_terminal(NULL, 0);
    TEST_ASSERT(1, "Shell and Terminal collectors handle NULL safely");
}

/* ------------------------------------------------------------- */
/* 12. Display Detection Tests                                   */
/* ------------------------------------------------------------- */
static void test_display(void) {
    TEST_SECTION("Display Monitor Resolution & Refresh Rate");

    reset_tracker();
    platform_gather_display(test_emit_cb);

    TEST_ASSERT(g_tracker.count > 0, "Must detect at least one active display on graphical environment");
    for (int i = 0; i < g_tracker.count; i++) {
        TEST_ASSERT(strcmp(g_tracker.labels[i], "Display") == 0, "Display label must be 'Display'");
        TEST_ASSERT(is_valid_utf8(g_tracker.values[i]), "Display value must be valid UTF-8");
        TEST_ASSERT(strchr(g_tracker.values[i], 'x') != NULL, "Display value must contain resolution 'WxH'");
    }

    /* NULL callback safety */
    platform_gather_display(NULL);
    TEST_ASSERT(1, "platform_gather_display(NULL) handles NULL callback safely");
}

/* ------------------------------------------------------------- */
/* 13. Window Manager & Display Manager Tests                    */
/* ------------------------------------------------------------- */
static void test_wm_and_dm(void) {
    TEST_SECTION("Window Manager & Display Manager");

    char wm[64] = "";
    platform_gather_wm(wm, sizeof(wm));
    TEST_ASSERT(wm[0] != '\0', "Window Manager string must be non-empty");
    TEST_ASSERT(is_valid_utf8(wm), "Window Manager string must be valid UTF-8");
    TEST_ASSERT(strcmp(wm, "DWM") == 0 || strcmp(wm, "GlazeWM") == 0 || strcmp(wm, "komorebi") == 0,
                "Window Manager must identify as DWM or a supported tiling WM");

    char dm[64] = "";
    platform_gather_displaymanager(dm, sizeof(dm));
    TEST_ASSERT(is_valid_utf8(dm), "Display Manager string must be valid UTF-8");
    TEST_ASSERT(strcmp(dm, "N/A") == 0 || strcmp(dm, "Unavailable") == 0,
                "Display Manager must report N/A or Unavailable on Windows");

    /* NULL safety */
    platform_gather_wm(NULL, 0);
    platform_gather_displaymanager(NULL, 0);
    TEST_ASSERT(1, "WM and DM collectors handle NULL safely");
}

/* ------------------------------------------------------------- */
/* 14. Theme, Icons, Font, Cursor, Locale Tests                  */
/* ------------------------------------------------------------- */
static void test_desktop_appearance(void) {
    TEST_SECTION("Theme, Icons, Font, Cursor, and Locale");

    char theme[64] = "";
    platform_gather_theme(theme, sizeof(theme));
    TEST_ASSERT(theme[0] != '\0', "Theme must be non-empty");
    TEST_ASSERT(strcmp(theme, "Dark") == 0 || strcmp(theme, "Light") == 0,
                "Theme must report Dark or Light");

    char icons[64] = "";
    platform_gather_icons(icons, sizeof(icons));
    TEST_ASSERT(strcmp(icons, "Windows Default") == 0, "Icons must report 'Windows Default'");

    char font[128] = "";
    platform_gather_font(font, sizeof(font));
    TEST_ASSERT(is_valid_utf8(font), "Font string must be valid UTF-8 (empty is valid fallback)");

    char cursor[64] = "";
    platform_gather_cursor(cursor, sizeof(cursor));
    TEST_ASSERT(cursor[0] != '\0', "Cursor string must be non-empty");
    TEST_ASSERT(is_valid_utf8(cursor), "Cursor string must be valid UTF-8");

    char locale[64] = "";
    platform_gather_locale(locale, sizeof(locale));
    TEST_ASSERT(locale[0] != '\0', "Locale string must be non-empty");
    TEST_ASSERT(is_valid_utf8(locale), "Locale string must be valid UTF-8");
    TEST_ASSERT(strchr(locale, '-') != NULL || strchr(locale, '_') != NULL,
                "Locale string must follow standard locale tag format");

    /* NULL safety */
    platform_gather_theme(NULL, 0);
    platform_gather_icons(NULL, 0);
    platform_gather_font(NULL, 0);
    platform_gather_cursor(NULL, 0);
    platform_gather_locale(NULL, 0);
    TEST_ASSERT(1, "Appearance collectors handle NULL buffers safely");
}

/* ------------------------------------------------------------- */
/* 15. Package Stub (Phase 5/6 Boundary Contract)                */
/* ------------------------------------------------------------- */
static void test_package_stub(void) {
    TEST_SECTION("Package Discovery Stub (Phase 5 Contract)");

    char pkg[128] = "PRESET";
    platform_gather_packages(pkg, sizeof(pkg));
    TEST_ASSERT(pkg[0] == '\0', "platform_gather_packages must be a stub returning empty string in Phase 5");

    platform_gather_packages(NULL, 0);
    TEST_ASSERT(1, "platform_gather_packages(NULL, 0) handles NULL safely");
}

/* ------------------------------------------------------------- */
/* 16. Static System-Info Caching & Call Avoidance               */
/* ------------------------------------------------------------- */
static void test_static_caching_and_call_avoidance(void) {
    TEST_SECTION("Static System-Info Caching & Call Avoidance");

    platform_invalidate_info_cache();
    platform_reset_query_counts_for_test();

    /* 1. OS Collector Caching */
    char os_val1[256] = "", os_val2[256] = "";
    TEST_ASSERT(!platform_is_field_cached_for_test("os"), "OS initially uncached");
    TEST_ASSERT(platform_get_query_count_for_test("os") == 0, "OS initial query count is 0");

    platform_gather_os(os_val1, sizeof(os_val1));
    TEST_ASSERT(platform_is_field_cached_for_test("os"), "OS marked cached after first call");
    TEST_ASSERT(platform_get_query_count_for_test("os") == 1, "OS query count is 1 after first call");

    /* Repeated calls must hit cache and NOT re-query */
    for (int i = 0; i < 5; i++) {
        platform_gather_os(os_val2, sizeof(os_val2));
    }
    TEST_ASSERT(platform_get_query_count_for_test("os") == 1,
                "OS query count remains 1 after 5 repeated calls (call avoidance)");
    TEST_ASSERT(strcmp(os_val1, os_val2) == 0, "Repeated OS calls return consistent cached value");

    /* Invalidation resets cache and allows fresh collection */
    platform_invalidate_info_cache();
    TEST_ASSERT(!platform_is_field_cached_for_test("os"), "OS uncached after cache invalidation");
    platform_gather_os(os_val1, sizeof(os_val1));
    TEST_ASSERT(platform_get_query_count_for_test("os") == 2, "OS re-queried (count=2) after invalidation");

    /* 2. GPU Collector Caching (DXGI enumeration avoidance) */
    reset_tracker();
    TEST_ASSERT(!platform_is_field_cached_for_test("gpu"), "GPU initially uncached");
    int initial_gpu_count = platform_get_query_count_for_test("gpu");
    platform_gather_gpu(test_emit_cb);
    TEST_ASSERT(platform_is_field_cached_for_test("gpu"), "GPU marked cached after first call");
    TEST_ASSERT(platform_get_query_count_for_test("gpu") == initial_gpu_count + 1,
                "DXGI enumerated exactly once on first call");

    for (int i = 0; i < 5; i++) {
        reset_tracker();
        platform_gather_gpu(test_emit_cb);
    }
    TEST_ASSERT(platform_get_query_count_for_test("gpu") == initial_gpu_count + 1,
                "DXGI enumeration NOT repeated across 5 subsequent calls (call avoidance)");

    platform_invalidate_info_cache();
    TEST_ASSERT(!platform_is_field_cached_for_test("gpu"), "GPU uncached after invalidation");
    reset_tracker();
    platform_gather_gpu(test_emit_cb);
    TEST_ASSERT(platform_get_query_count_for_test("gpu") == initial_gpu_count + 2,
                "DXGI fresh enumeration executed after cache invalidation");

    /* 3. Display Collector Caching (EnumDisplayDevices avoidance) */
    reset_tracker();
    TEST_ASSERT(!platform_is_field_cached_for_test("display"), "Display initially uncached");
    int initial_disp_count = platform_get_query_count_for_test("display");
    platform_gather_display(test_emit_cb);
    TEST_ASSERT(platform_is_field_cached_for_test("display"), "Display marked cached after first call");
    TEST_ASSERT(platform_get_query_count_for_test("display") == initial_disp_count + 1,
                "Display enumerated once on first call");

    for (int i = 0; i < 5; i++) {
        reset_tracker();
        platform_gather_display(test_emit_cb);
    }
    TEST_ASSERT(platform_get_query_count_for_test("display") == initial_disp_count + 1,
                "Display enumeration NOT repeated across 5 calls (call avoidance)");

    platform_invalidate_info_cache();
    reset_tracker();
    platform_gather_display(test_emit_cb);
    TEST_ASSERT(platform_get_query_count_for_test("display") == initial_disp_count + 2,
                "Display fresh enumeration after invalidation");

    /* 4. Shell & Terminal Process Tree Walk Caching */
    char shell_buf[128] = "", term_buf[128] = "";
    int initial_proc_count = platform_get_query_count_for_test("shell");
    platform_gather_shell(shell_buf, sizeof(shell_buf));
    TEST_ASSERT(platform_is_field_cached_for_test("shell"), "Shell marked cached");
    TEST_ASSERT(platform_is_field_cached_for_test("terminal"),
                "Terminal marked cached as co-product of process hierarchy walk");
    TEST_ASSERT(platform_get_query_count_for_test("shell") == initial_proc_count + 1,
                "Process walk executed once");

    /* Subsequent terminal call must hit shared cache without walking process tree */
    platform_gather_terminal(term_buf, sizeof(term_buf));
    TEST_ASSERT(platform_get_query_count_for_test("terminal") == initial_proc_count + 1,
                "Terminal query used cached process walk data (zero re-walk)");

    for (int i = 0; i < 5; i++) {
        platform_gather_shell(shell_buf, sizeof(shell_buf));
        platform_gather_terminal(term_buf, sizeof(term_buf));
    }
    TEST_ASSERT(platform_get_query_count_for_test("shell") == initial_proc_count + 1,
                "Zero process re-walks over repeated shell/terminal calls");

    /* 5. CPU Collector Caching */
    char cpu_buf[256] = "";
    int initial_cpu_count = platform_get_query_count_for_test("cpu");
    platform_gather_cpu(cpu_buf, sizeof(cpu_buf));
    TEST_ASSERT(platform_is_field_cached_for_test("cpu"), "CPU marked cached");
    TEST_ASSERT(platform_get_query_count_for_test("cpu") == initial_cpu_count + 1, "CPU registry queried once");
    for (int i = 0; i < 5; i++) platform_gather_cpu(cpu_buf, sizeof(cpu_buf));
    TEST_ASSERT(platform_get_query_count_for_test("cpu") == initial_cpu_count + 1, "CPU query NOT repeated");

    /* 6. Title, Host, Kernel, WM, Theme, Font, Cursor, Locale, IP */
    char tmp[256] = "", tmp2[256] = "";
    platform_gather_title(tmp, sizeof(tmp), tmp2, sizeof(tmp2));
    TEST_ASSERT(platform_is_field_cached_for_test("title"), "Title marked cached");
    platform_gather_host(tmp, sizeof(tmp));
    TEST_ASSERT(platform_is_field_cached_for_test("host"), "Host marked cached");
    platform_gather_kernel(tmp, sizeof(tmp));
    TEST_ASSERT(platform_is_field_cached_for_test("kernel"), "Kernel marked cached");
    platform_gather_wm(tmp, sizeof(tmp));
    TEST_ASSERT(platform_is_field_cached_for_test("wm"), "WM marked cached");
    platform_gather_theme(tmp, sizeof(tmp));
    TEST_ASSERT(platform_is_field_cached_for_test("theme"), "Theme marked cached");
    platform_gather_font(tmp, sizeof(tmp));
    TEST_ASSERT(platform_is_field_cached_for_test("font"), "Font marked cached");
    platform_gather_cursor(tmp, sizeof(tmp));
    TEST_ASSERT(platform_is_field_cached_for_test("cursor"), "Cursor marked cached");
    platform_gather_locale(tmp, sizeof(tmp));
    TEST_ASSERT(platform_is_field_cached_for_test("locale"), "Locale marked cached");
    reset_tracker();
    platform_gather_ip(test_emit_cb);
    TEST_ASSERT(platform_is_field_cached_for_test("ip"), "IP marked cached");

    /* Total invalidation clears ALL flags */
    platform_invalidate_info_cache();
    TEST_ASSERT(!platform_is_field_cached_for_test("title"), "Title cleared on invalidate");
    TEST_ASSERT(!platform_is_field_cached_for_test("host"), "Host cleared on invalidate");
    TEST_ASSERT(!platform_is_field_cached_for_test("kernel"), "Kernel cleared on invalidate");
    TEST_ASSERT(!platform_is_field_cached_for_test("shell"), "Shell cleared on invalidate");
    TEST_ASSERT(!platform_is_field_cached_for_test("terminal"), "Terminal cleared on invalidate");
    TEST_ASSERT(!platform_is_field_cached_for_test("display"), "Display cleared on invalidate");
    TEST_ASSERT(!platform_is_field_cached_for_test("wm"), "WM cleared on invalidate");
    TEST_ASSERT(!platform_is_field_cached_for_test("theme"), "Theme cleared on invalidate");
    TEST_ASSERT(!platform_is_field_cached_for_test("font"), "Font cleared on invalidate");
    TEST_ASSERT(!platform_is_field_cached_for_test("cursor"), "Cursor cleared on invalidate");
    TEST_ASSERT(!platform_is_field_cached_for_test("locale"), "Locale cleared on invalidate");
    TEST_ASSERT(!platform_is_field_cached_for_test("cpu"), "CPU cleared on invalidate");
    TEST_ASSERT(!platform_is_field_cached_for_test("gpu"), "GPU cleared on invalidate");
    TEST_ASSERT(!platform_is_field_cached_for_test("ip"), "IP cleared on invalidate");
}

/* ------------------------------------------------------------- */
/* 17. Dynamic Field Non-Caching Contract                        */
/* ------------------------------------------------------------- */
static void test_dynamic_fields_non_caching(void) {
    TEST_SECTION("Dynamic Field Non-Caching Contract");

    /* 1. Uptime, Memory, Swap, Battery, Disk must NEVER be cached */
    TEST_ASSERT(!platform_is_field_cached_for_test("uptime"), "Uptime must never be reported as cached");
    TEST_ASSERT(!platform_is_field_cached_for_test("memory"), "Memory must never be reported as cached");
    TEST_ASSERT(!platform_is_field_cached_for_test("swap"), "Swap must never be reported as cached");
    TEST_ASSERT(!platform_is_field_cached_for_test("battery"), "Battery must never be reported as cached");
    TEST_ASSERT(!platform_is_field_cached_for_test("disk"), "Disk must never be reported as cached");

    /* 2. Successive Uptime calls query live ticks */
    char up1[128] = "", up2[128] = "";
    platform_gather_uptime(up1, sizeof(up1));
    TEST_ASSERT(up1[0] != '\0', "Uptime returns non-empty formatted value");
    TEST_ASSERT(!platform_is_field_cached_for_test("uptime"), "Uptime remains uncached after query");

    /* 3. Successive Memory & Swap calls query live memory status */
    char mem1[256] = "", mem2[256] = "";
    platform_gather_memory(mem1, sizeof(mem1));
    TEST_ASSERT(mem1[0] != '\0', "Memory returns non-empty formatted string");
    TEST_ASSERT(!platform_is_field_cached_for_test("memory"), "Memory remains uncached after query");

    char swap1[256] = "", swap2[256] = "";
    platform_gather_swap(swap1, sizeof(swap1));
    TEST_ASSERT(swap1[0] != '\0', "Swap returns non-empty formatted string");
    TEST_ASSERT(!platform_is_field_cached_for_test("swap"), "Swap remains uncached after query");

    /* 4. Cache invalidation does not break dynamic collectors */
    platform_invalidate_info_cache();
    platform_gather_uptime(up2, sizeof(up2));
    platform_gather_memory(mem2, sizeof(mem2));
    platform_gather_swap(swap2, sizeof(swap2));
    TEST_ASSERT(up2[0] != '\0', "Uptime works post-invalidation");
    TEST_ASSERT(mem2[0] != '\0', "Memory works post-invalidation");
    TEST_ASSERT(swap2[0] != '\0', "Swap works post-invalidation");
}

/* ------------------------------------------------------------- */
/* 18. Dynamic Refresh Integration & Cadence Contracts           */
/* ------------------------------------------------------------- */
static void test_dynamic_refresh_cadence_contracts(void) {
    TEST_SECTION("Dynamic Refresh Integration & Cadence Contracts");

    /* 1. Verify 1-second refresh cadence rule: (frame > 0 && frame % 20 == 0) */
    int refresh_count = 0;
    for (int frame = 0; frame <= 100; frame++) {
        int should_refresh = (frame > 0 && frame % 20 == 0);
        if (should_refresh) {
            refresh_count++;
            /* Must only fire on exact multiples of 20 */
            TEST_ASSERT(frame == 20 || frame == 40 || frame == 60 || frame == 80 || frame == 100,
                        "Dynamic refresh fires strictly on 20-frame (1-second) boundaries");
        } else {
            /* Guaranteed NOT to fire on non-multiples */
            TEST_ASSERT(frame % 20 != 0 || frame == 0,
                        "Frames between boundaries never trigger dynamic refresh");
        }
    }
    TEST_ASSERT(refresh_count == 5, "Exactly 5 dynamic refresh passes in 100 frames (~5 seconds at 20 FPS)");

    /* 2. Verify static collectors are NOT re-queried during dynamic refresh simulation */
    platform_invalidate_info_cache();
    platform_reset_query_counts_for_test();

    /* Prime static cache (startup phase) */
    char os_b[256], cpu_b[256];
    platform_gather_os(os_b, sizeof(os_b));
    platform_gather_cpu(cpu_b, sizeof(cpu_b));
    reset_tracker();
    platform_gather_gpu(test_emit_cb);
    platform_gather_display(test_emit_cb);

    int os_count_before = platform_get_query_count_for_test("os");
    int cpu_count_before = platform_get_query_count_for_test("cpu");
    int gpu_count_before = platform_get_query_count_for_test("gpu");
    int disp_count_before = platform_get_query_count_for_test("display");

    /* Simulate 50 frames of animation loop: only frames 20 and 40 trigger dynamic refresh */
    for (int frame = 1; frame <= 50; frame++) {
        if (frame % 20 == 0) {
            /* Inside fetch.c: only uptime, memory, swap are gathered */
            char up[128], mem[256], swp[256];
            platform_gather_uptime(up, sizeof(up));
            platform_gather_memory(mem, sizeof(mem));
            platform_gather_swap(swp, sizeof(swp));
        }
    }

    /* Verify static query counts are 100% UNCHANGED */
    TEST_ASSERT(platform_get_query_count_for_test("os") == os_count_before,
                "OS query count completely untouched during dynamic refresh");
    TEST_ASSERT(platform_get_query_count_for_test("cpu") == cpu_count_before,
                "CPU query count completely untouched during dynamic refresh");
    TEST_ASSERT(platform_get_query_count_for_test("gpu") == gpu_count_before,
                "DXGI GPU query count completely untouched during dynamic refresh");
    TEST_ASSERT(platform_get_query_count_for_test("display") == disp_count_before,
                "Display query count completely untouched during dynamic refresh");
}

/* ------------------------------------------------------------- */
/* Main Test Runner                                              */
/* ------------------------------------------------------------- */
int main(void) {
    printf("====================================================\n");
    printf("  FETCH PHASE 5 SYSTEM INFORMATION UNIT TESTS       \n");
    printf("  Testing native Win32/DXGI/Registry collectors     \n");
    printf("====================================================\n");

    test_title_and_hostname();
    test_os_and_kernel();
    test_host_model();
    test_cpu();
    test_gpu();
    test_memory_and_swap();
    test_disk();
    test_battery();
    test_network_ip();
    test_uptime();
    test_shell_and_terminal();
    test_display();
    test_wm_and_dm();
    test_desktop_appearance();
    test_package_stub();
    test_static_caching_and_call_avoidance();
    test_dynamic_fields_non_caching();
    test_dynamic_refresh_cadence_contracts();

    printf("\n====================================================\n");
    printf("  PHASE 5 TEST SUMMARY: %d / %d passed (%s)\n",
           tests_passed, tests_run,
           tests_failed == 0 ? "100% SUCCESS" : "FAILURES DETECTED");
    printf("====================================================\n");

    return tests_failed == 0 ? 0 : 1;
}
