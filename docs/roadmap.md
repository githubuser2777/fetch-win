# Native Windows Port Roadmap & Technical Specification (v2.1)

This document formalizes the target architecture, platform abstraction, terminal lifecycle, system-information semantics, build strategy, and phased implementation roadmap for porting `fetch` to Windows as a native CLI application.

---

## 1. Executive Summary & Core Requirements

1. **Native Windows Application**: Builds natively into `fetch.exe` without WSL, Cygwin, MSYS runtime, or Linux compatibility layers.
2. **Terminal-First CLI**: Preserves standard terminal behavior across Windows Terminal, PowerShell (`conhost`), and CMD (`conhost`).
3. **Preserve Animated 3D Renderer**: Preserves existing rotation, depth, scaling, ASCII/block/sextant shading modes, Blinn-Phong lighting, and color mapping.
4. **Clean Platform Abstraction**: Isolates OS-specific terminal handling and system metrics collection behind a unified interface (`platform.h`), maintaining 100% platform-independent rendering, configuration, and core logic.
5. **Robust Terminal Lifecycle**: Guarantees restoration of terminal modes, code pages, and cursor visibility on normal exit, keypress, and Ctrl+C.
6. **Authentic System Information**: Uses native Win32, Registry, and DXGI APIs. Zero data fabrication.
7. **Zero Render-Loop Hitching**: Strict prohibition against spawning external processes (`popen`), DXGI adapter enumerations, or network calls inside the 20 FPS animation loop.

---

## 2. Target Architecture

```
src/
├── core/
│   ├── common.h         # Common data structures, UTF-8 & ANSI parsing utilities
│   ├── common.c         # Pure string/encoding functions (no OS dependencies)
│   └── main.c           # CLI parsing, execution orchestration, 20 FPS animation loop
├── renderer/
│   ├── renderer.h       # 3D math, heightmapping, point cloud generation, lighting
│   └── renderer.c       # Rendering pipeline (completely platform-independent)
├── config/
│   ├── config.h         # Field enum IDs, configuration structs, default order
│   └── config.c         # Configuration file deserializer, inline hint stripping
├── logo/
│   ├── logo.h           # Logo grid storage, color parser, distro color schemes
│   └── logo.c           # Logo loader, fastfetch invoker, embedded fallback art
└── platform/
    ├── platform.h       # Formal platform abstraction API
    ├── platform_posix.c # Shared POSIX console (termios, ioctl, signals)
    ├── platform_linux.c # Linux system-information gathers (/proc, /sys, DRM ioctls)
    ├── platform_mac.c   # macOS system-information gathers (sysctl, system_profiler, IOKit)
    └── platform_win.c   # Native Windows terminal lifecycle & Win32/DXGI/IPHLPAPI gathers
```

---

## 3. Platform Abstraction Interface (`platform.h`)

The platform layer isolates terminal lifecycle, console I/O, timing, and system data collection:

```c
#ifndef FETCH_PLATFORM_H
#define FETCH_PLATFORM_H

#include <stddef.h>
#include <stdatomic.h>

// --- Terminal Capabilities & Initialization ---
typedef struct {
  int is_tty;
  int supports_vt;
  int supports_mouse;
} platform_term_caps_t;

int  platform_terminal_init(platform_term_caps_t *caps);
void platform_terminal_cleanup(void);
void platform_get_term_size(int *rows, int *cols);
int  platform_check_resize(void);
void platform_sleep_frame(unsigned int usec);

// --- Atomic Frame Output ---
int  platform_write_output(const char *buf, size_t len);

// --- Input & Event Handling ---
typedef enum {
  INPUT_NONE = 0,
  INPUT_EXIT_KEY,    // Normal keypress triggering exit
  INPUT_MOUSE_DRAG,  // Mouse drag event with relative deltas
  INPUT_MOUSE_UP     // Mouse button release
} platform_input_event_t;

typedef struct {
  int btn;           // 0 = left, 32 = drag
  int x, y;          // 1-based column and row coordinates
  int dx, dy;        // relative movement deltas
} platform_mouse_event_t;

platform_input_event_t platform_poll_input(platform_mouse_event_t *mouse_event);
int platform_is_interrupted(void);

// --- Path Resolution ---
void platform_get_config_path(char *out, size_t outsz);
void platform_get_logo_path(char *out, size_t outsz);

// --- OS / Distro Identification ---
int platform_detect_os_id(char *out, size_t outsz);

// --- System Information Gathers ---
typedef void (*platform_emit_info_cb)(const char *label, const char *fmt, ...);

void platform_gather_title(char *out_user, size_t usersz, char *out_host, size_t hostsz);
void platform_gather_os(char *out, size_t outsz);
void platform_gather_host(char *out, size_t outsz);
void platform_gather_kernel(char *out, size_t outsz);
void platform_gather_uptime(char *out, size_t outsz);
void platform_gather_packages(char *out, size_t outsz);
void platform_gather_shell(char *out, size_t outsz);
void platform_gather_display(platform_emit_info_cb emit_cb);
void platform_gather_wm(char *out, size_t outsz);
void platform_gather_displaymanager(char *out, size_t outsz);
void platform_gather_theme(char *out, size_t outsz);
void platform_gather_icons(char *out, size_t outsz);
void platform_gather_font(char *out, size_t outsz);
void platform_gather_cursor(char *out, size_t outsz);
void platform_gather_terminal(char *out, size_t outsz);
void platform_gather_cpu(char *out, size_t outsz);
void platform_gather_gpu(platform_emit_info_cb emit_cb);
void platform_gather_memory(char *out, size_t outsz);
void platform_gather_swap(char *out, size_t outsz);
void platform_gather_disk(const char *path, char *out, size_t outsz);
void platform_gather_ip(platform_emit_info_cb emit_cb);
void platform_gather_battery(char *out_label, size_t labelsz, char *out_val, size_t valsz);
void platform_gather_locale(char *out, size_t outsz);

#endif // FETCH_PLATFORM_H
```

---

## 4. Windows System Information Mapping

| Field | Source / Win32 API | Expected Output | Failure Handling | Timing |
|---|---|---|---|---|
| **Title** | `GetUserNameA` + `GetComputerNameExA(ComputerNamePhysicalDnsHostname)` | `user@hostname` | Fallback to `%USERNAME%` / `%COMPUTERNAME%` | **Static** |
| **OS** | Registry `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion` (`ProductName`, `DisplayVersion`, `CurrentBuildNumber`, `UBR`) + `GetNativeSystemInfo` | `Windows 11 Pro 24H2 (Build 26100.3037) x86_64` | Fallback to `RtlGetVersion` | **Static** |
| **Host** | Registry `HKLM\HARDWARE\DESCRIPTION\System\BIOS` (`SystemManufacturer`, `SystemProductName`, `SystemFamily`) | `ASUSTeK ROG Zephyrus G16` (vendor deduplicated) | Omitted if generic | **Static** |
| **Kernel** | `RtlGetVersion` (`ntdll.dll`) or `CurrentBuildNumber` | `Windows NT 10.0.26100` | Fallback to OS build | **Static** |
| **Uptime** | `GetTickCount64()` | `2 days, 4 hours, 12 mins` | Displays `0 mins` | **Periodic** (~1s) |
| **Packages** | Scoop (`%USERPROFILE%\scoop\apps`) & Chocolatey (`%ChocolateyInstall%\lib`) directories | `14 (scoop), 3 (choco)` | Omitted if none found | **Static** |
| **Shell** | `CreateToolhelp32Snapshot` parent process walk (`pwsh.exe`, `powershell.exe`, `cmd.exe`, etc.) + version query | `PowerShell 7.4.5` or `cmd.exe 10.0.26100` | Fallback to `%COMSPEC%` | **Static** |
| **Display** | `EnumDisplayMonitors` + `EnumDisplaySettingsExA` (`ENUM_CURRENT_SETTINGS`) + `GetMonitorInfoA` | `Display (Internal): 2560x1600 @ 240 Hz` | Fallback to primary desktop resolution | **Static** |
| **WM** | Process scan for `glazewm.exe` or `komorebi.exe`; fallback to `DWM` | `GlazeWM`, `komorebi`, or `DWM` | Defaults to `DWM` | **Static** |
| **Display Manager** | Omitted by default (Windows uses integrated logon session) | Omitted | Omitted | **Static** |
| **Theme** | Registry `HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize` (`AppsUseLightTheme`) | `Dark` or `Light` | Omitted if unreadable | **Static** |
| **Icons** | Windows Default icon theme | `Windows Default` | Omitted | **Static** |
| **Font** | `GetCurrentConsoleFontEx` (`FaceName`, `dwFontSize.Y`) | `Cascadia Mono (12pt)` | Omitted if unreadable | **Static** |
| **Cursor** | Registry `HKCU\Control Panel\Cursors` (`Scheme Source`, cursor size) | `Windows Default (32px)` | Omitted if unreadable | **Static** |
| **Terminal** | `WT_SESSION` env var, `TERM_PROGRAM`, or Toolhelp parent scan (`conhost.exe`, `WindowsTerminal.exe`) | `Windows Terminal 1.21` or `conhost.exe` | Fallback to `Windows Console` | **Static** |
| **CPU** | Registry `HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\0` (`ProcessorNameString`, `~MHz`) + `GetSystemInfo` | `AMD Ryzen 9 7945HX (32) @ 2.50 GHz` | Fallback to `%NUMBER_OF_PROCESSORS%` | **Static** |
| **GPU** | DXGI (`IDXGIFactory::EnumAdapters`). Filters software WARP. Discrete/Integrated labeled only with high confidence. | `NVIDIA GeForce RTX 4080 Laptop GPU [Discrete]` or `AMD Radeon Graphics` | Fallback to Registry display adapter | **Static** |
| **Memory** | `GlobalMemoryStatusEx` (`ullTotalPhys`, `ullAvailPhys`) | `14.20 GiB / 31.86 GiB (44%)` with color coding | Omitted on error | **Periodic** (~1s) |
| **Swap** | `GlobalMemoryStatusEx` (`ullTotalPageFile`, `ullAvailPageFile`) | `2.40 GiB / 34.86 GiB (6%)` (Committed Pagefile) | Omitted if pagefile is 0 | **Periodic** (~1s) |
| **Disk** | `GetDiskFreeSpaceExA` + `GetVolumeInformationA` for `C:\` and configured drives (`disk=D:\`) | `Disk (C:\): 340.20 GiB / 952.50 GiB (35%) - NTFS` | Suppress unmounted drives | **Static** |
| **IP** | `GetAdaptersAddresses` (`iphlpapi.h`). Detects active interface, friendly name, IPv4/mask. | `Wi-Fi (Wi-Fi): 192.168.1.55/24` | Omitted if disconnected | **Static** |
| **Battery** | `GetSystemPowerStatus` (`BatteryLifePercent`, `ACLineStatus`, `BatteryLifeTime`) | `Battery: 82% (2 hours, 45 mins remaining) [Discharging]` | Omitted if desktop (no battery) | **Periodic** (~1s) |
| **Locale** | `GetUserDefaultLocaleName` | `en-US` | Fallback to `%LANG%` | **Static** |
| **Colors** | Standard ANSI 16-color test blocks | ANSI color ramp | Always succeeds | **Static** |

---

## 5. Terminal Lifecycle & Input Architecture

### 5.1 Encapsulated State Management
```c
typedef struct {
  HANDLE hIn;
  HANDLE hOut;
  DWORD orig_in_mode;
  DWORD orig_out_mode;
  UINT orig_in_cp;
  UINT orig_out_cp;
  CONSOLE_CURSOR_INFO orig_cursor_info;
  int is_initialized;
  int is_cleaned_up;
} win_console_state_t;

static win_console_state_t g_win_console = {0};
static atomic_bool g_interrupted = false;
```

### 5.2 Safe Threading & Control Handler
- Windows runs `SetConsoleCtrlHandler` callbacks on a separate thread.
- **Safety Guarantee**: The handler does **not** call console I/O, format buffers, or invoke `ExitProcess()`. It atomically sets `atomic_store(&g_interrupted, true)` and returns `TRUE`.
- The main animation thread checks `platform_is_interrupted()` at every iteration, breaks cleanly, and executes `platform_terminal_cleanup()` synchronously on the main thread.

### 5.3 Input Model & Passthrough Realities
- `hOut` enables `ENABLE_VIRTUAL_TERMINAL_PROCESSING` for fast VT frame output.
- `hIn` uses native Win32 `INPUT_RECORD` processing (`ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT`).
- In `conhost` (PowerShell/CMD), non-destructive `PeekConsoleInputA` leaves unconsumed key events in the buffer, allowing the calling shell to receive the keypress upon exit.
- In PTY/ConPTY environments (Windows Terminal, VS Code Terminal), keypress acts reliably as an **exit trigger**, while transparent passthrough to the shell prompt is best-effort due to ConPTY pipeline teardown.

---

## 6. Phased Implementation Roadmap

```text
Phase 0: Baseline & Test Harness (COMPLETED)
   │
   ▼
Phase 1: Core & Renderer Extraction (COMPLETED)
   │
   ▼
Phase 2: Config & Logo Extraction (COMPLETED)
   │
   ▼
Phase 3: Platform Abstraction + POSIX Backend (COMPLETED)
   │
   ▼
Phase 4: Windows Terminal Backend (State & Input)
   │
   ▼
Phase 5: Windows System Information Backend
   │
   ▼
Phase 6: Windows Paths, Logo & Packages
   │
   ▼
Phase 7: CMake Cross-Platform Build System
   │
   ▼
Phase 8: GitHub Actions CI Matrix
   │
   ▼
Phase 9: Quality Control & Performance Verification
   │
   ▼
Phase 10: Documentation & Release
```

### Phase Details & Progress Status

#### Phase 0: Baseline & Test Harness
- **Status**: **COMPLETED** (Commit [`3774bfe`](file:///C:/Users/Admin/Documents/code_workspace/fetch-win/tests/test_baseline.c))
- **Deliverables**:
  - `tests/test_baseline.c`: 98 automated assertions covering UTF-8, ANSI width, 3D math, layout sizing, config parsing, CLI flags, box wrapping, and custom logos.
  - `tests/include/*.h`: Minimal POSIX shims allowing native Windows compilation of unmodified `fetch.c`.
  - **Results**: 98 / 98 passed (100% success) against unmodified upstream code.

#### Phase 1: Core Utilities & Renderer Extraction
- **Status**: **COMPLETED** (Commit [`1d287c3`](https://github.com/githubuser2777/fetch-win/commit/1d287c3))
- **Files**: `src/core/common.h`, `src/core/common.c`, `src/renderer/renderer.h`, `src/renderer/renderer.c`, `tests/test_phase1.c`
- **Deliverables**:
  - `src/core/common.h/.c`: Extracted platform-independent UTF-8, ANSI escape decoding, cursor escape checks, string width, clipping, and alignment enums.
  - `src/renderer/renderer.h/.c`: Extracted 3D point cloud generation, heightmapping, Blinn-Phong lighting, sub-cell depth buffering, and projection pipeline.
  - `tests/test_phase1.c`: 67 automated assertions testing isolated `src/core` and `src/renderer` modules.
  - `Makefile`: Updated modular source list and test targets.
- **Results**:
  - Phase 0 baseline regression tests: 98 / 98 passed (100% success).
  - Phase 1 isolated unit tests: 67 / 67 passed (100% success).
  - Clean native build on Windows with MinGW-w64.
- **Acceptance**: Frame rendering, math, and string semantics match Phase 0 baseline with zero behavioral difference.

#### Phase 2: Config & Logo Module Extraction
- **Status**: **COMPLETED**
- **Files**: `src/config/config.h`, `src/config/config.c`, `src/logo/logo.h`, `src/logo/logo.c`, `tests/test_phase2.c`
- **Deliverables**:
  - `src/config/config.h/.c`: Extracted plain-text config deserializer, inline hint stripping, settings clamping, and field ordering.
  - `src/logo/logo.h/.c`: Extracted fastfetch logo invoker, custom logo loader (`# distro:` metadata), fallback art, codepoint cell splitting, and distro color scheme mapping.
  - `fetch.c`: Stripped duplicate config and logo implementations while maintaining existing CLI/config precedence.
  - `tests/test_phase2.c`: 130 automated unit assertions testing isolated config and logo modules.
  - `Makefile`: Updated modular sources and added `test_phase2` test target.
- **Results**:
  - Phase 0 baseline regression tests: 98 / 98 passed (100% success).
  - Phase 1 isolated unit tests: 67 / 67 passed (100% success).
  - Phase 2 isolated unit tests: 130 / 130 passed (100% success).
  - Clean native build of `fetch.exe` on Windows with MinGW-w64.
- **Acceptance**: Config parsing, appearance options, and logo behaviors match Phase 0 baseline with zero regression.

#### Phase 3: Platform Abstraction & POSIX Backend
- **Status**: **COMPLETED**
- **Files**: `src/platform/platform.h`, `src/platform/platform_posix.c`, `src/platform/platform_stub.c`, `fetch.c`, `tests/test_phase3.c`, `Makefile`
- **Deliverables**:
  - `src/platform/platform.h`: Platform-neutral abstraction covering terminal lifecycle, terminal sizing, resize detection, input polling & events, interruption states, sleep timing, atomic frame output, and platform path resolution. Test injection controls guarded behind `FETCH_TESTING`.
  - `src/platform/platform_posix.c`: Shared POSIX console backend implementing raw mode via termios, ioctl sizing, signal-safe flags for SIGINT/SIGTERM/SIGWINCH, idempotent terminal cleanup, honest VT/mouse capability reporting, unconsumed keypress shell passthrough, and SGR mouse drag/release delta tracking.
  - `src/platform/platform_stub.c`: Temporary Phase 3 platform shim preserving clean native Windows compilation without compiling POSIX code on Windows.
  - `fetch.c`: Stripped direct POSIX terminal/runtime dependencies; refactored main loop to drive output, events, and lifecycle exclusively through the platform abstraction interface.
  - `tests/test_phase3.c`: 60 automated unit assertions verifying terminal lifecycle idempotency, honest capability detection, dimension queries, resize handling, signal-safe interruption states, mouse drag delta math, keypress passthrough, timing/output wrappers, and path resolution.
  - `Makefile`: Updated modular sources with conditional platform source selection (`platform_stub.c` on Windows, `platform_posix.c` on POSIX) and added `test_phase3` test target with `-DFETCH_TESTING`.
- **Results**:
  - Phase 0 baseline regression tests: 98 / 98 passed (100% success).
  - Phase 1 isolated unit tests: 67 / 67 passed (100% success).
  - Phase 2 isolated unit tests: 130 / 130 passed (100% success).
  - Phase 3 platform unit tests: 60 / 60 passed (100% success).
  - Clean native build and CLI smoke test verification on Windows with MinGW-w64.
- **Acceptance**: Terminal lifecycle, signal safety, keypress passthrough, and animation behaviors match Phase 0 baseline with zero regression.

#### Phase 4: Native Windows Terminal Backend
- **Status**: Pending
- **Files**: `src/platform/platform_win.c`
- **Scope**:
  - Implement console state save and restore (`orig_in_mode`, `orig_out_mode`, `orig_cp`, cursor).
  - Enable UTF-8 code page and `ENABLE_VIRTUAL_TERMINAL_PROCESSING`.
  - Implement `platform_write_output()` via `WriteFile()`.
  - Implement atomic interrupt handling and input polling (`PeekConsoleInputA`).
- **Acceptance**: Windows console enters raw VT mode, mouse drag works, keypress exits cleanly, terminal state is 100% restored.

#### Phase 5: Native Windows System Information
- **Status**: Pending
- **Files**: `src/platform/platform_win.c`
- **Scope**:
  - Implement OS, Kernel, Host via Registry and `RtlGetVersion`.
  - Implement CPU via Registry and `GetSystemInfo`.
  - Implement DXGI GPU enumeration with software adapter filtering and conservative classification.
  - Implement Memory and Pagefile via `GlobalMemoryStatusEx`.
  - Implement Uptime via `GetTickCount64()`.
  - Implement Display, Disk, IP, Battery, Shell, Terminal, and Theme.
- **Acceptance**: All supported fields output authentic Windows system data without crashes or fabrication.

#### Phase 6: Windows Paths, Logo & Package Managers
- **Status**: Pending
- **Files**: `src/config/config.c`, `src/logo/logo.c`, `src/platform/platform_win.c`
- **Scope**:
  - Resolve `%APPDATA%\fetch\config` and `%APPDATA%\fetch\logo.txt` with fallbacks.
  - Add built-in Windows ASCII logo art and color schemes.
  - Implement Scoop and Chocolatey directory detection.
- **Acceptance**: `fetch.exe` without arguments displays the Windows ASCII logo and native stats.

#### Phase 7: Cross-Platform CMake Build System
- **Status**: Pending
- **Files**: `CMakeLists.txt`, `Makefile`
- **Scope**:
  - Configure `CMakeLists.txt` supporting Windows (MSVC & MinGW-w64), Linux, and macOS.
  - Synchronize `Makefile` with the exact modular source list.
  - Build `fetch.exe` using CMake and Ninja/MSVC.
- **Acceptance**: Native `fetch.exe` builds cleanly from source on Windows.

#### Phase 8: GitHub Actions CI Matrix
- **Status**: Pending
- **Files**: `.github/workflows/ci.yml`
- **Scope**:
  - Create CI workflow covering Windows (MSVC & MinGW), Ubuntu (GCC & Clang), and macOS.
  - Run build, unit tests, and smoke tests across all targets.
- **Acceptance**: CI pipeline passes green across all platforms.

#### Phase 9: Quality Control & Performance Verification
- **Status**: Pending
- **Files**: `tests/`, `src/`
- **Scope**:
  - Execute performance benchmarks (verify zero heavy calls during animation).
  - Test across Windows Terminal, PowerShell (conhost), and CMD.
  - Test terminal resize, mouse drag, and Ctrl+C rapid cancellation.
- **Acceptance**: Stable operation verified across all Tier 1 Windows environments.

#### Phase 10: Documentation & Release Preparation
- **Status**: Pending
- **Files**: `README.md`, `docs/configuration.md`, `docs/custom-logos.md`
- **Scope**:
  - Document Windows build instructions (CMake + MSVC/MinGW).
  - Document `%APPDATA%\fetch\config` and `%APPDATA%\fetch\logo.txt`.
  - Document Windows-specific field semantics, package managers, and terminal capabilities.
- **Acceptance**: Documentation is comprehensive, accurate, and verified against implementation.

---

## 7. Compatibility Tiers

| Tier | Environment | Target Level | Notes |
|---|---|---|---|
| **Tier 1 (Targeted)** | Windows Terminal | Full Support | 24-bit color, VT sequences, SGR mouse drag, Unicode blocks. |
| **Tier 1 (Targeted)** | PowerShell in `conhost` | Full Support | Native VT processing, keypress passthrough, clean restore. |
| **Tier 1 (Targeted)** | CMD in `conhost` | Full Support | Native VT processing, UTF-8 output, clean restore. |
| **Tier 2 (Best-Effort)** | VS Code Terminal | Compatible | VT rendering, mouse drag; keypress passthrough best-effort. |
| **Tier 2 (Best-Effort)** | WezTerm / Alacritty | Compatible | Full VT and legacy computing symbols. |
| **Tier 3 (Unsupported)** | Legacy Win 7 / 8 `conhost` | Unsupported | Lacks native Virtual Terminal Processing support. |
| **Tier 3 (Unsupported)** | Non-interactive redirected pipe | Unsupported | Animation loop requires an interactive console handle. |
