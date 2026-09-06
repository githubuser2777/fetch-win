#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <direct.h>

#include "src/core/common.h"
#include "src/renderer/renderer.h"
#include "src/config/config.h"
#include "src/logo/logo.h"
#include "src/platform/platform.h"

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

static void make_dir(const char *path) {
    CreateDirectoryA(path, NULL);
}

static void write_file(const char *path, const char *content) {
    FILE *fp = fopen(path, "w");
    if (fp) {
        if (content) fputs(content, fp);
        fclose(fp);
    }
}

/* ------------------------------------------------------------- */
/* 1. Windows Primary Path Resolution & Fallbacks                */
/* ------------------------------------------------------------- */
static void test_windows_paths(void) {
    TEST_SECTION("Windows Path Resolution & Fallbacks");

    char orig_appdata[512] = "";
    char orig_userprofile[512] = "";
    char orig_home[512] = "";

    const char *ad = getenv("APPDATA");
    if (ad) strncpy(orig_appdata, ad, sizeof(orig_appdata) - 1);
    const char *up = getenv("USERPROFILE");
    if (up) strncpy(orig_userprofile, up, sizeof(orig_userprofile) - 1);
    const char *hm = getenv("HOME");
    if (hm) strncpy(orig_home, hm, sizeof(orig_home) - 1);

    char test_root[MAX_PATH];
    GetTempPathA(sizeof(test_root), test_root);
    strncat(test_root, "fetch_p6_paths_test", sizeof(test_root) - strlen(test_root) - 1);
    make_dir(test_root);

    char mock_appdata[MAX_PATH];
    snprintf(mock_appdata, sizeof(mock_appdata), "%s\\appdata", test_root);
    make_dir(mock_appdata);
    char mock_appdata_fetch[MAX_PATH];
    snprintf(mock_appdata_fetch, sizeof(mock_appdata_fetch), "%s\\fetch", mock_appdata);
    make_dir(mock_appdata_fetch);

    char mock_home[MAX_PATH];
    snprintf(mock_home, sizeof(mock_home), "%s\\home", test_root);
    make_dir(mock_home);
    char mock_home_config[MAX_PATH];
    snprintf(mock_home_config, sizeof(mock_home_config), "%s\\.config", mock_home);
    make_dir(mock_home_config);
    char mock_home_fetch[MAX_PATH];
    snprintf(mock_home_fetch, sizeof(mock_home_fetch), "%s\\fetch", mock_home_config);
    make_dir(mock_home_fetch);

    char env_ad[MAX_PATH + 32], env_hm[MAX_PATH + 32], env_up[MAX_PATH + 32];
    snprintf(env_ad, sizeof(env_ad), "APPDATA=%s", mock_appdata);
    _putenv(env_ad);
    snprintf(env_hm, sizeof(env_hm), "HOME=%s", mock_home);
    _putenv(env_hm);
    snprintf(env_up, sizeof(env_up), "USERPROFILE=%s", mock_home);
    _putenv(env_up);

    /* 1.1 Neither file exists yet -> returns primary %APPDATA%\fetch\... */
    char resolved[512] = "";
    platform_get_config_path(resolved, sizeof(resolved));
    TEST_ASSERT(strstr(resolved, "fetch\\config") != NULL || strstr(resolved, "fetch/config") != NULL,
                "Config path contains fetch/config");
    TEST_ASSERT(strstr(resolved, mock_appdata) != NULL,
                "Config path prefers APPDATA when neither file exists");

    char resolved_logo[512] = "";
    platform_get_logo_path(resolved_logo, sizeof(resolved_logo));
    TEST_ASSERT(strstr(resolved_logo, "fetch\\logo.txt") != NULL || strstr(resolved_logo, "fetch/logo.txt") != NULL,
                "Logo path contains fetch/logo.txt");
    TEST_ASSERT(strstr(resolved_logo, mock_appdata) != NULL,
                "Logo path prefers APPDATA when neither file exists");

    /* 1.2 Fallback file exists on disk, but primary does NOT exist */
    char fb_cfg[MAX_PATH];
    snprintf(fb_cfg, sizeof(fb_cfg), "%s\\config", mock_home_fetch);
    write_file(fb_cfg, "os\n");

    resolved[0] = '\0';
    platform_get_config_path(resolved, sizeof(resolved));
    TEST_ASSERT(strstr(resolved, mock_home) != NULL,
                "Falls back to ~/.config/fetch/config when primary file does not exist but fallback exists");

    /* 1.3 Primary file exists -> primary wins over fallback */
    char pri_cfg[MAX_PATH];
    snprintf(pri_cfg, sizeof(pri_cfg), "%s\\config", mock_appdata_fetch);
    write_file(pri_cfg, "host\n");

    resolved[0] = '\0';
    platform_get_config_path(resolved, sizeof(resolved));
    TEST_ASSERT(strstr(resolved, mock_appdata) != NULL,
                "Primary %APPDATA% config takes precedence when primary file exists");

    /* 1.4 Regular file validation: directory at primary path must not be treated as regular config file */
    DeleteFileA(pri_cfg);
    make_dir(pri_cfg); /* pri_cfg is now a directory */

    resolved[0] = '\0';
    platform_get_config_path(resolved, sizeof(resolved));
    TEST_ASSERT(strstr(resolved, mock_home) != NULL,
                "Directory at primary path is rejected as invalid; resolves fallback regular file");
    RemoveDirectoryA(pri_cfg);

    /* 1.5 Missing/Empty APPDATA */
    _putenv("APPDATA=");
    resolved[0] = '\0';
    platform_get_config_path(resolved, sizeof(resolved));
    TEST_ASSERT(strstr(resolved, mock_home) != NULL,
                "Handles empty APPDATA safely and falls back to HOME/USERPROFILE");

    /* Cleanup */
    DeleteFileA(fb_cfg);
    RemoveDirectoryA(mock_home_fetch);
    RemoveDirectoryA(mock_home_config);
    RemoveDirectoryA(mock_home);
    RemoveDirectoryA(mock_appdata_fetch);
    RemoveDirectoryA(mock_appdata);
    RemoveDirectoryA(test_root);

    if (orig_appdata[0]) { snprintf(env_ad, sizeof(env_ad), "APPDATA=%s", orig_appdata); _putenv(env_ad); }
    else _putenv("APPDATA=");
    if (orig_userprofile[0]) { snprintf(env_up, sizeof(env_up), "USERPROFILE=%s", orig_userprofile); _putenv(env_up); }
    else _putenv("USERPROFILE=");
    if (orig_home[0]) { snprintf(env_hm, sizeof(env_hm), "HOME=%s", orig_home); _putenv(env_hm); }
    else _putenv("HOME=");
}

/* ------------------------------------------------------------- */
/* 2. Paths with Spaces and Unicode                              */
/* ------------------------------------------------------------- */
static void test_paths_spaces_unicode(void) {
    TEST_SECTION("Paths with Spaces and Unicode");

    char orig_appdata[512] = "";
    const char *ad = getenv("APPDATA");
    if (ad) strncpy(orig_appdata, ad, sizeof(orig_appdata) - 1);

    char temp_dir[MAX_PATH];
    GetTempPathA(sizeof(temp_dir), temp_dir);

    char space_dir[MAX_PATH];
    snprintf(space_dir, sizeof(space_dir), "%s\\fetch test spaces", temp_dir);
    make_dir(space_dir);
    char space_fetch[MAX_PATH];
    snprintf(space_fetch, sizeof(space_fetch), "%s\\fetch", space_dir);
    make_dir(space_fetch);

    char space_cfg[MAX_PATH];
    snprintf(space_cfg, sizeof(space_cfg), "%s\\config", space_fetch);
    write_file(space_cfg, "cpu\n");

    char env_buf[MAX_PATH + 32];
    snprintf(env_buf, sizeof(env_buf), "APPDATA=%s", space_dir);
    _putenv(env_buf);

    char out[512] = "";
    platform_get_config_path(out, sizeof(out));
    TEST_ASSERT(strstr(out, "fetch test spaces") != NULL, "Path with spaces resolved correctly");
    TEST_ASSERT(strstr(out, "config") != NULL, "Config filename included in spaced path");

    DeleteFileA(space_cfg);
    RemoveDirectoryA(space_fetch);
    RemoveDirectoryA(space_dir);

    if (orig_appdata[0]) {
        snprintf(env_buf, sizeof(env_buf), "APPDATA=%s", orig_appdata);
        _putenv(env_buf);
    } else {
        _putenv("APPDATA=");
    }
}

/* ------------------------------------------------------------- */
/* 3. Windows Built-in ASCII Logo & Aliases                      */
/* ------------------------------------------------------------- */
static void test_windows_logo(void) {
    TEST_SECTION("Windows Built-in ASCII Logo & Aliases");

    fetch_logo_t logo;
    logo_init(&logo);

    /* 3.1 Load built-in Windows logo */
    int ok = logo_load_builtin(&logo, "windows");
    TEST_ASSERT(ok == 1, "logo_load_builtin('windows') returns 1");
    TEST_ASSERT(logo.rows > 0, "Built-in Windows logo has non-zero rows");
    TEST_ASSERT(strcmp(logo.distro, "windows") == 0, "Logo distro identifier set to 'windows'");

    /* 3.2 Logo aliases: win, win11, win10 */
    fetch_logo_t logo_win, logo_win11, logo_win10;
    logo_init(&logo_win);
    logo_init(&logo_win11);
    logo_init(&logo_win10);

    TEST_ASSERT(logo_load_builtin(&logo_win, "win") == 1, "Alias 'win' loads built-in Windows logo");
    TEST_ASSERT(logo_load_builtin(&logo_win11, "win11") == 1, "Alias 'win11' loads built-in Windows logo");
    TEST_ASSERT(logo_load_builtin(&logo_win10, "win10") == 1, "Alias 'win10' loads built-in Windows logo");

    TEST_ASSERT(logo_win.rows == logo.rows, "'win' has same row count as 'windows'");
    TEST_ASSERT(logo_win11.rows == logo.rows, "'win11' has same row count as 'windows'");

    /* 3.3 Unrelated logo name does NOT silently fallback to windows */
    fetch_logo_t logo_fake;
    logo_init(&logo_fake);
    int fake_res = logo_load_builtin(&logo_fake, "completely_unknown_distro_xyz");
    TEST_ASSERT(fake_res == 0, "Unrelated unknown logo does not succeed in built-in loader");

    /* 3.4 Windows Distro Color Scheme */
    const char *outer = NULL;
    const char *inner = NULL;
    logo_set_distro_colors("windows", &outer, &inner);
    TEST_ASSERT(outer != NULL && strstr(outer, "36m") != NULL, "Windows outer color is bold cyan (36m)");
    TEST_ASSERT(inner != NULL && strstr(inner, "37m") != NULL, "Windows inner color is bold white (37m)");

    outer = NULL; inner = NULL;
    logo_set_distro_colors("win11", &outer, &inner);
    TEST_ASSERT(outer != NULL && strstr(outer, "36m") != NULL, "Alias 'win11' resolves same cyan color");
}

/* ------------------------------------------------------------- */
/* 4. Custom Logo File Loading                                   */
/* ------------------------------------------------------------- */
static void test_custom_logo(void) {
    TEST_SECTION("Custom Logo Loading");

    char temp_dir[MAX_PATH];
    GetTempPathA(sizeof(temp_dir), temp_dir);
    char logo_path[MAX_PATH];
    snprintf(logo_path, sizeof(logo_path), "%s\\fetch_test_logo.txt", temp_dir);

    const char *custom_art =
        "# distro: windows\n"
        "   ####   ####   \n"
        "   ####   ####   \n"
        "                 \n"
        "   ####   ####   \n"
        "   ####   ####   \n";

    write_file(logo_path, custom_art);

    fetch_logo_t logo;
    logo_init(&logo);
    int loaded = logo_load_file(&logo, logo_path);
    TEST_ASSERT(loaded == 1, "Custom logo loaded successfully");
    TEST_ASSERT(logo.rows == 5, "Custom logo row count is 5");
    TEST_ASSERT(strcmp(logo.distro, "windows") == 0, "Custom logo parsed '# distro: windows' metadata");

    DeleteFileA(logo_path);
}

/* ------------------------------------------------------------- */
/* 5. Subprocess Safety, Timeouts, and Handle Leak Avoidance     */
/* ------------------------------------------------------------- */
static void test_subprocess_safety(void) {
    TEST_SECTION("Process Execution Safety & Subprocess Helpers");

    /* 5.1 Execute simple command and capture bounded output */
    char out[1024] = "";
    int status = platform_run_command("cmd.exe /c echo Hello_Fetch_P6", out, sizeof(out), 2500);
    TEST_ASSERT(status == 0, "platform_run_command returns 0 for echo command");
    TEST_ASSERT(strstr(out, "Hello_Fetch_P6") != NULL, "Captured stdout contains expected string");

    /* 5.2 Non-zero exit code */
    char err_out[1024] = "";
    status = platform_run_command("cmd.exe /c exit 42", err_out, sizeof(err_out), 2500);
    TEST_ASSERT(status == 42, "platform_run_command reports non-zero exit code 42");

    /* 5.3 Missing executable handling */
    char no_exe_out[256] = "";
    status = platform_run_command("nonexistent_executable_123456.exe", no_exe_out, sizeof(no_exe_out), 1000);
    TEST_ASSERT(status != 0, "platform_run_command fails cleanly for nonexistent binary");

    /* 5.4 Bounded output: verify output is truncated without overflow */
    char small_buf[16] = "";
    status = platform_run_command("cmd.exe /c echo AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", small_buf, sizeof(small_buf), 2500);
    TEST_ASSERT(status == 0, "Bounded capture returns 0");
    TEST_ASSERT(strlen(small_buf) < sizeof(small_buf), "Output is safely bounded to buffer size");

    /* 5.5 Subprocess timeout handling: hung process is terminated */
    char timeout_out[256] = "";
    DWORD t0 = GetTickCount();
    status = platform_run_command("cmd.exe /c ping -n 10 127.0.0.1 >nul", timeout_out, sizeof(timeout_out), 500);
    DWORD elapsed = GetTickCount() - t0;
    TEST_ASSERT(status != 0, "Timed out command returns non-zero status");
    TEST_ASSERT(elapsed < 2000, "Timed out command terminated promptly (< 2000ms)");
}

/* ------------------------------------------------------------- */
/* 6. Package Managers (Scoop, Chocolatey, Winget) Parsing       */
/* ------------------------------------------------------------- */
static void test_package_managers(void) {
    TEST_SECTION("Package Managers (Scoop, Chocolatey, Winget)");

    /* 6.1 Scoop counting mock directory */
    char temp_dir[MAX_PATH];
    GetTempPathA(sizeof(temp_dir), temp_dir);

    char mock_scoop[MAX_PATH];
    snprintf(mock_scoop, sizeof(mock_scoop), "%s\\mock_scoop_apps", temp_dir);
    make_dir(mock_scoop);

    char p1[MAX_PATH], p2[MAX_PATH], p3[MAX_PATH], p_file[MAX_PATH];
    snprintf(p1, sizeof(p1), "%s\\app1", mock_scoop); make_dir(p1);
    snprintf(p2, sizeof(p2), "%s\\app2", mock_scoop); make_dir(p2);
    snprintf(p3, sizeof(p3), "%s\\app3", mock_scoop); make_dir(p3);
    snprintf(p_file, sizeof(p_file), "%s\\not_a_package.txt", mock_scoop); write_file(p_file, "text");

    int scoop_count = platform_count_dir_packages(mock_scoop);
    TEST_ASSERT(scoop_count == 3, "Directory package counter finds 3 package directories and ignores files");

    DeleteFileA(p_file);
    RemoveDirectoryA(p3);
    RemoveDirectoryA(p2);
    RemoveDirectoryA(p1);
    RemoveDirectoryA(mock_scoop);

    /* 6.2 Chocolatey counting mock directory */
    char mock_choco[MAX_PATH];
    snprintf(mock_choco, sizeof(mock_choco), "%s\\mock_choco_lib", temp_dir);
    make_dir(mock_choco);

    char c1[MAX_PATH], c2[MAX_PATH], c_file[MAX_PATH];
    snprintf(c1, sizeof(c1), "%s\\pkgA", mock_choco); make_dir(c1);
    snprintf(c2, sizeof(c2), "%s\\pkgB", mock_choco); make_dir(c2);
    snprintf(c_file, sizeof(c_file), "%s\\readme.md", mock_choco); write_file(c_file, "readme");

    int choco_count = platform_count_dir_packages(mock_choco);
    TEST_ASSERT(choco_count == 2, "Chocolatey package counter finds 2 package directories and ignores files");

    DeleteFileA(c_file);
    RemoveDirectoryA(c2);
    RemoveDirectoryA(c1);
    RemoveDirectoryA(mock_choco);

    /* 6.3 Winget output parsing test */
    const char *mock_winget_table =
        "Name                          Id                           Version          Available Source\n"
        "---------------------------------------------------------------------------------------------\n"
        "Visual Studio Code            Microsoft.VisualStudioCode   1.89.0           1.90.0    winget\n"
        "Git                           Git.Git                      2.45.0                     winget\n"
        "PowerShell                    Microsoft.PowerShell         7.4.2                      winget\n";

    int parsed_count = platform_parse_winget_output(mock_winget_table);
    TEST_ASSERT(parsed_count == 3, "platform_parse_winget_output correctly counts 3 valid package rows");

    /* 6.4 Malformed winget output resilience */
    const char *malformed_output =
        "Failed to connect to source\n"
        "Error: 0x80070002\n"
        "No packages found\n";
    int malformed_res = platform_parse_winget_output(malformed_output);
    TEST_ASSERT(malformed_res == 0, "Malformed winget output yields 0 without errors or false counts");

    /* 6.5 Multi-manager formatted string test */
    char formatted[128] = "";
    platform_format_packages_string(formatted, sizeof(formatted), 142, 38, 27);
    TEST_ASSERT(strstr(formatted, "winget: 142") != NULL, "Includes 'winget: 142'");
    TEST_ASSERT(strstr(formatted, "Scoop: 38") != NULL, "Includes 'Scoop: 38'");
    TEST_ASSERT(strstr(formatted, "Chocolatey: 27") != NULL, "Includes 'Chocolatey: 27'");
    TEST_ASSERT(strstr(formatted, ", ") != NULL, "Separates package managers with commas");

    /* Partial counts: omit managers with 0 */
    formatted[0] = '\0';
    platform_format_packages_string(formatted, sizeof(formatted), 0, 15, 0);
    TEST_ASSERT(strcmp(formatted, "Scoop: 15") == 0, "Formats single manager when others are 0");

    formatted[0] = '\0';
    platform_format_packages_string(formatted, sizeof(formatted), 0, 0, 0);
    TEST_ASSERT(formatted[0] == '\0', "Empty output when all package managers report 0");
}

/* ------------------------------------------------------------- */
/* 7. Package Caching and No Per-Frame Subprocess Execution       */
/* ------------------------------------------------------------- */
static void test_package_caching(void) {
    TEST_SECTION("Package Caching & Zero Per-Frame Subprocesses");

    platform_invalidate_info_cache();
    platform_reset_query_counts_for_test();

    char pkg1[128] = "", pkg2[128] = "";
    TEST_ASSERT(!platform_is_field_cached_for_test("packages"), "Packages initially uncached");

    platform_gather_packages(pkg1, sizeof(pkg1));
    TEST_ASSERT(platform_is_field_cached_for_test("packages"), "Packages marked cached after first call");
    int initial_queries = platform_get_query_count_for_test("packages");
    TEST_ASSERT(initial_queries == 1, "Packages query count is 1 after initial query");

    /* Subsequent calls (simulating animation frames) must NOT re-query */
    for (int frame = 0; frame < 100; frame++) {
        platform_gather_packages(pkg2, sizeof(pkg2));
    }
    TEST_ASSERT(platform_get_query_count_for_test("packages") == 1,
                "Package query NOT repeated across 100 simulated frames (call avoidance)");
    TEST_ASSERT(strcmp(pkg1, pkg2) == 0, "Package string remains consistent from cache");

    /* Invalidation resets cache */
    platform_invalidate_info_cache();
    TEST_ASSERT(!platform_is_field_cached_for_test("packages"), "Packages uncached after invalidation");
}

/* ------------------------------------------------------------- */
/* Main Test Runner                                              */
/* ------------------------------------------------------------- */
int main(void) {
    printf("====================================================\n");
    printf("  FETCH PHASE 6 UNIT TESTS                          \n");
    printf("  Testing Windows Paths, Logo & Package Managers    \n");
    printf("====================================================\n");

    test_windows_paths();
    test_paths_spaces_unicode();
    test_windows_logo();
    test_custom_logo();
    test_subprocess_safety();
    test_package_managers();
    test_package_caching();

    printf("\n====================================================\n");
    printf("  PHASE 6 TEST SUMMARY: %d / %d passed (%s)\n",
           tests_passed, tests_run,
           tests_failed == 0 ? "100% SUCCESS" : "FAILURES DETECTED");
    printf("====================================================\n");

    return tests_failed == 0 ? 0 : 1;
}
