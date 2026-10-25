// angrybirds: the game in a window. SDL2 gives the window, the GL context, the events and the clock.
// Usage: angrybirds --scripts <scripts folder> --data <data folder> [--screenshot <png> <frames>]
// --click <frame> <x> <y> and --drag <frame> <x> <y> <x2> <y2> (repeatable, with --screenshot) press the left button at that frame for testing.
// --screenshot runs that many frames at a fixed 1/30 s step, saves the last one and exits.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <io.h>
#include <dbghelp.h>
#include <crtdbg.h>
#pragma comment(lib, "dbghelp.lib")
#else
#include <unistd.h>
#endif

#include <cmath>
#include <cstdio>
#include <exception>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "engine/game_platform.h"
#include "engine/input.h"
#include "engine/png_decoder.h"
#include "window/gl_backend.h"

using namespace ab;

static int MapKey(SDL_Keycode k) {
    if (k >= SDLK_a && k <= SDLK_z) return KeyIndex(std::string(1, (char)('A' + (k - SDLK_a))));
    if (k >= SDLK_0 && k <= SDLK_9) return KeyIndex(std::string(1, (char)('0' + (k - SDLK_0))));
    if (k >= SDLK_F1 && k <= SDLK_F12) return KeyIndex("F" + std::to_string(k - SDLK_F1 + 1));
    switch (k) {
        case SDLK_PAGEDOWN: return KeyIndex("PAGEDOWN");
        case SDLK_PAGEUP: return KeyIndex("PAGEUP");
        case SDLK_HOME: return KeyIndex("HOME");
        case SDLK_END: return KeyIndex("END");
        case SDLK_INSERT: return KeyIndex("INSERT");
        case SDLK_DELETE: return KeyIndex("DELETE");
        case SDLK_RETURN: return KeyIndex("RETURN");
        case SDLK_LSHIFT: case SDLK_RSHIFT: return KeyIndex("SHIFT");
        case SDLK_LCTRL: case SDLK_RCTRL: return KeyIndex("CONTROL");
        case SDLK_LALT: case SDLK_RALT: return KeyIndex("KEY_ALT");
        case SDLK_TAB: return KeyIndex("TAB");
        case SDLK_SPACE: return KeyIndex("SPACE");
        case SDLK_BACKSPACE: return KeyIndex("BACKSPACE");
        case SDLK_PAUSE: return KeyIndex("PAUSE");
        case SDLK_ESCAPE: return KeyIndex("ESCAPE");
        case SDLK_LEFT: return KeyIndex("LEFT");
        case SDLK_RIGHT: return KeyIndex("RIGHT");
        case SDLK_UP: return KeyIndex("UP");
        case SDLK_DOWN: return KeyIndex("DOWN");
        case SDLK_KP_MULTIPLY: return KeyIndex("MULTIPLY");
        case SDLK_KP_PLUS: return KeyIndex("ADD");
        case SDLK_KP_MINUS: return KeyIndex("SUBTRACT");
        case SDLK_KP_DIVIDE: return KeyIndex("DIVIDE");
        case SDLK_CLEAR: return KeyIndex("CLEAR");
        case SDLK_AC_BACK: return KeyIndex("KEY_BACK");
        case SDLK_MENU: return KeyIndex("KEY_MENU");
        default: return -1;
    }
}

// config.lua (the exe reads it from beside itself): the keys the window uses are name, width, height,
// fullscreen and showCursor.
struct WindowConfig {
    std::string name = "Angry Birds";
    std::string imagePath = "images/pc_build", fontPath = "fonts/pc_build";   // Seasons: "images", "fonts"
    std::string dataPath = "data";                  // config.lua datapath: Seasons uses "data_new"
    std::string product = "Angry Birds";          // names the save folder %APPDATA%\\<publisher>\\<product>
    std::string publisher = "Rovio";
    int width = 1280, height = 720;
    bool fullscreen = false, showCursor = true;
};

static void ReadConfigLua(const std::string& path, WindowConfig& out) {
    lua_State* L = luaL_newstate();
    if (luaL_loadfile(L, path.c_str()) == 0 && lua_pcall(L, 0, 0, 0) == 0) {
        lua_getglobal(L, "name"); if (lua_isstring(L, -1)) out.name = lua_tostring(L, -1); lua_pop(L, 1);
        lua_getglobal(L, "imagePath"); if (lua_isstring(L, -1)) out.imagePath = lua_tostring(L, -1); lua_pop(L, 1);
        lua_getglobal(L, "fontPath"); if (lua_isstring(L, -1)) out.fontPath = lua_tostring(L, -1); lua_pop(L, 1);
        lua_getglobal(L, "datapath"); if (lua_isstring(L, -1)) out.dataPath = lua_tostring(L, -1); lua_pop(L, 1);
        lua_getglobal(L, "product"); if (lua_isstring(L, -1)) out.product = lua_tostring(L, -1); lua_pop(L, 1);
        lua_getglobal(L, "publisher"); if (lua_isstring(L, -1)) out.publisher = lua_tostring(L, -1); lua_pop(L, 1);
        lua_getglobal(L, "width"); if (lua_isnumber(L, -1)) out.width = (int)lua_tonumber(L, -1); lua_pop(L, 1);
        lua_getglobal(L, "height"); if (lua_isnumber(L, -1)) out.height = (int)lua_tonumber(L, -1); lua_pop(L, 1);
        lua_getglobal(L, "fullscreen"); if (lua_isboolean(L, -1)) out.fullscreen = lua_toboolean(L, -1) != 0; lua_pop(L, 1);
        lua_getglobal(L, "showCursor"); if (lua_isboolean(L, -1)) out.showCursor = lua_toboolean(L, -1) != 0; lua_pop(L, 1);
        std::printf("config: %s (window %dx%d, fullscreen %d, showCursor %d)\n", path.c_str(), out.width, out.height, out.fullscreen, out.showCursor);
    }
    lua_close(L);
}

#ifdef _WIN32
// The title bar and the taskbar show the icon of the game's own exe (AngryBirds.exe / AngryBirdsSeasons.exe
// in the game folder; nothing is shipped with the engine). The icon is built from the exe's icon group at
// the exact size the title bar (small) and the taskbar (big) ask for.
static BOOL CALLBACK FirstIconGroup(HMODULE, LPCSTR, LPSTR name, LONG_PTR param) {
    *(LPSTR*)param = name;
    return FALSE;                                    // the first one is the game's icon
}
static HICON IconOfSize(HMODULE module, LPSTR group, int size) {
    HRSRC g = FindResourceA(module, group, MAKEINTRESOURCEA(14));
    if (!g) return nullptr;
    BYTE* dir = (BYTE*)LockResource(LoadResource(module, g));
    int id = dir ? LookupIconIdFromDirectoryEx(dir, TRUE, size, size, LR_DEFAULTCOLOR) : 0;
    HRSRC r = id ? FindResourceA(module, MAKEINTRESOURCEA(id), MAKEINTRESOURCEA(3)) : nullptr;
    if (!r) return nullptr;
    BYTE* bits = (BYTE*)LockResource(LoadResource(module, r));
    return bits ? CreateIconFromResourceEx(bits, SizeofResource(module, r), TRUE, 0x00030000, size, size, LR_DEFAULTCOLOR) : nullptr;
}
static void SetGameIcon(SDL_Window* window, const std::string& gameName) {
    std::string name;
    for (char c : gameName) if (c != ' ') name += c;
    const std::string candidates[] = {name + ".exe", "AngryBirds.exe", "AngryBirdsSeasons.exe"};
    HICON large = nullptr, small = nullptr;
    for (const std::string& file : candidates) {
        HMODULE module = LoadLibraryExA(file.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
        if (!module) continue;
        LPSTR group = nullptr;
        EnumResourceNamesA(module, MAKEINTRESOURCEA(14), FirstIconGroup, (LONG_PTR)&group);
        if (group) {
            large = IconOfSize(module, group, GetSystemMetrics(SM_CXICON));
            small = IconOfSize(module, group, GetSystemMetrics(SM_CXSMICON));
        }
        FreeLibrary(module);                         // the icons are copies, the module is not needed
        if (!large && !small) ExtractIconExA(file.c_str(), 0, &large, &small, 1);   // the shell's own way as a fallback
        std::printf("icon: %s -> %s\n", file.c_str(), (large || small) ? "found" : "no icon");
        if (large || small) break;
    }
    if (!large && !small) { std::printf("icon: none of the game exes was found in the working folder\n"); return; }
    if (!small) small = large;
    if (!large) large = small;
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info)) { std::printf("icon: no window handle\n"); return; }
    HWND hwnd = info.info.win.window;
    SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)large);
    SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)small);
    SetClassLongPtr(hwnd, GCLP_HICON, (LONG_PTR)large);
    SetClassLongPtr(hwnd, GCLP_HICONSM, (LONG_PTR)small);
    // a window that was created fullscreen can come back to windowed without the system menu, and then
    // the title bar has no place to draw the icon
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    if ((style & WS_CAPTION) == WS_CAPTION) {
        if (!(style & WS_SYSMENU)) SetWindowLongPtr(hwnd, GWL_STYLE, style | WS_SYSMENU);
        // the title bar was laid out before the icon existed (and while the window was fullscreen): lay it out again
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        RedrawWindow(hwnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
    }
    // SDL's own way too: the icon's pixels go through SDL_SetWindowIcon, which sets the window's icons itself
    ICONINFO ii;
    if (GetIconInfo(large, &ii)) {
        BITMAP bm;
        if (ii.hbmColor && GetObject(ii.hbmColor, sizeof bm, &bm) && bm.bmWidth > 0 && bm.bmHeight > 0) {
            std::vector<uint32_t> px((size_t)bm.bmWidth * bm.bmHeight);
            BITMAPINFO bi = {};
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = bm.bmWidth;
            bi.bmiHeader.biHeight = -bm.bmHeight;                 // top-down
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            HDC dc = GetDC(nullptr);
            if (GetDIBits(dc, ii.hbmColor, 0, bm.bmHeight, px.data(), &bi, DIB_RGB_COLORS)) {
                SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(px.data(), bm.bmWidth, bm.bmHeight, 32, bm.bmWidth * 4,
                                                                0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
                if (surface) { SDL_SetWindowIcon(window, surface); SDL_FreeSurface(surface); }
            }
            ReleaseDC(nullptr, dc);
        }
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
        if (ii.hbmMask) DeleteObject(ii.hbmMask);
    }
    std::printf("icon: set (style 0x%08lx, ex style 0x%08lx, small icon %p, big icon %p, read back %p / %p)\n",
                (unsigned long)GetWindowLongPtr(hwnd, GWL_STYLE), (unsigned long)GetWindowLongPtr(hwnd, GWL_EXSTYLE),
                (void*)small, (void*)large, (void*)SendMessageA(hwnd, WM_GETICON, ICON_SMALL, 0), (void*)SendMessageA(hwnd, WM_GETICON, ICON_BIG, 0));
}
#endif

static int GameMain(int argc, char** argv) {
    std::string scripts, data, shot, configPath, userData;
    int shotFrames = 0; int testW = 0, testH = 0;
    std::string audioOut;                 // test: write the mixed sound of the --screenshot run to this .wav
    struct Click { int frame; float x, y, x2, y2; int length; };   // length 3 is a click, more is a drag to (x2, y2)
    std::vector<Click> clicks;
    std::vector<std::pair<int, int>> wheels;     // test: --wheel <frame> <notches>
    std::vector<std::pair<int, std::string>> keys;   // test: --key <frame> <name>, a key press of 3 frames
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--scripts") && i + 1 < argc) scripts = argv[++i];
        else if (!std::strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
        else if (!std::strcmp(argv[i], "--userdata") && i + 1 < argc) userData = argv[++i];
        else if (!std::strcmp(argv[i], "--config") && i + 1 < argc) configPath = argv[++i];
        else if (!std::strcmp(argv[i], "--key") && i + 2 < argc) { keys.push_back({std::atoi(argv[i + 1]), argv[i + 2]}); i += 2; }
        else if (!std::strcmp(argv[i], "--wheel") && i + 2 < argc) { wheels.push_back({std::atoi(argv[i + 1]), std::atoi(argv[i + 2])}); i += 2; }
        else if (!std::strcmp(argv[i], "--click") && i + 3 < argc) { float x = (float)std::atof(argv[i + 2]), y = (float)std::atof(argv[i + 3]); clicks.push_back({std::atoi(argv[i + 1]), x, y, x, y, 3}); i += 3; }
        else if (!std::strcmp(argv[i], "--drag") && i + 5 < argc) { clicks.push_back({std::atoi(argv[i + 1]), (float)std::atof(argv[i + 2]), (float)std::atof(argv[i + 3]), (float)std::atof(argv[i + 4]), (float)std::atof(argv[i + 5]), 30}); i += 5; }
        else if (!std::strcmp(argv[i], "--audio-out") && i + 1 < argc) audioOut = argv[++i];
        else if (!std::strcmp(argv[i], "--screenshot") && i + 2 < argc) { shot = argv[++i]; shotFrames = std::atoi(argv[++i]); }
        else if (!std::strcmp(argv[i], "--size") && i + 2 < argc) { testW = std::atoi(argv[++i]); testH = std::atoi(argv[++i]); }
    }
    // without arguments the exe works like the original: put it next to the game's data folder
    if (data.empty()) {
        WindowConfig beside;                          // the game's own config.lua, next to the exe, names its data folder
        ReadConfigLua("config.lua", beside);
        data = beside.dataPath;
    }
    if (scripts.empty()) scripts = data + "/scripts";
    FILE* probe = std::fopen((scripts + "/gamelogic.lua").c_str(), "rb");
    if (!probe) {
        std::string msg = "Could not find " + scripts + "/gamelogic.lua.\n\nPut this exe next to the game's data folder (beside the original AngryBirds.exe) and start it from there.";
        std::printf("%s\nOr run: %s --scripts <scripts folder> --data <data folder>\n", msg.c_str(), argv[0]);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Angry Birds engine", msg.c_str(), nullptr);
        return 2;
    }
    std::fclose(probe);

    // config.lua: --config, else the folder that holds the data folder (where the original exe sits), else the working folder
    WindowConfig wc;
    if (configPath.empty()) {
        std::string d = data;
        while (!d.empty() && (d.back() == '/' || d.back() == '\\')) d.pop_back();
        size_t cut = d.find_last_of("/\\");
        std::string beside = (cut == std::string::npos ? std::string() : d.substr(0, cut + 1)) + "config.lua";
        FILE* f = std::fopen(beside.c_str(), "r");
        if (f) { std::fclose(f); configPath = beside; } else configPath = "config.lua";
    }
    ReadConfigLua(configPath, wc);
    if (shotFrames) { WindowConfig keep = wc; wc = WindowConfig(); if (testW > 0) { wc.width = testW; wc.height = testH; } wc.imagePath = keep.imagePath; wc.fontPath = keep.fontPath; wc.showCursor = keep.showCursor; }   // test runs use the default window

    BootConfig cfg;
    cfg.scriptPath = scripts;
    cfg.imagePath = wc.imagePath;
    cfg.fontPath = wc.fontPath;
    cfg.showCursor = wc.showCursor;

#ifdef _WIN32
    // like the exes (dpiAware in their manifest): a 125% / 150% desktop gets the real pixel size, not a smaller
    // window that Windows stretches. Per-monitor v2 first (the newest call wins over the compatibility settings), then the old call.
    {
        typedef BOOL (WINAPI *SetCtxFn)(HANDLE);
        SetCtxFn setCtx = (SetCtxFn)GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext");
        bool ok = setCtx && setCtx((HANDLE)-4);            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        if (!ok) ok = SetProcessDPIAware() != 0;
        std::printf("dpi awareness: %s\n", ok ? "on" : "not set (already fixed by the system or the exe's compatibility settings)");
        SDL_SetHint("SDL_WINDOWS_DPI_AWARENESS", "permonitorv2");
        SDL_SetHint("SDL_WINDOWS_DPI_SCALING", "0");
    }
#endif
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::printf("SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_Window* window = SDL_CreateWindow(wc.name.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          wc.width, wc.height,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                                          (wc.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (!window) { std::printf("SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
#ifdef _WIN32
    SetGameIcon(window, wc.name);
#endif
    SDL_SetWindowMinimumSize(window, 1024, 600);
    SDL_ShowCursor(wc.showCursor ? SDL_ENABLE : SDL_DISABLE);   // the game draws its own hand cursor
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) { std::printf("SDL_GL_CreateContext: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetSwapInterval(1);

    GlBackend backend;
    if (!backend.Init()) { std::printf("OpenGL 1.x functions not found\n"); return 1; }
    int pw, ph;
    SDL_GL_GetDrawableSize(window, &pw, &ph);
    backend.SetWindowSize(pw, ph);

    cfg.screenWidth = (float)pw;
    cfg.screenHeight = (float)ph;
    GamePlatform platform(data);
    // --userdata <folder> keeps settings.lua / highscores.lua there; "original" is the folder the game itself
    // uses, %APPDATA%\Rovio\<product>, whose encrypted files are read and written in their own format
    if (userData.empty() && !shotFrames) userData = "original";     // like the game itself; --userdata . keeps saves in the working folder
    if (userData == "original") {
        const char* appData = std::getenv("APPDATA");
        if (appData) userData = std::string(appData) + "\\" + wc.publisher + "\\" + wc.product;
        else { std::printf("--userdata original: APPDATA is not set, using the working folder\n"); userData.clear(); }
    }
    platform.SetUserDir(userData);
    std::printf("saves: %s\n", userData.empty() ? "(working folder)" : userData.c_str());

    // The sound device. The exe mixes in software and plays through DirectSound; here the same mixer
    // (platform.Audio()) is asked for blocks by SDL's audio thread.
    SDL_AudioDeviceID audioDevice = 0;
    if (audioOut.empty()) {
        SDL_AudioSpec want, have;
        bool audioOk = SDL_InitSubSystem(SDL_INIT_AUDIO) == 0;     // a machine without a sound device just plays silently
        SDL_zero(want);
        want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024;
        want.userdata = &platform;
        want.callback = [](void* user, Uint8* stream, int len) {
            ((GamePlatform*)user)->Audio().MixStereo((int16_t*)stream, len / 4, 44100);
        };
        audioDevice = audioOk ? SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0) : 0;
        if (audioDevice) {
            SDL_PauseAudioDevice(audioDevice, 0);
            std::printf("sound: %s, %d Hz, %d channels\n", SDL_GetCurrentAudioDriver(), have.freq, have.channels);
        }
        else std::printf("no sound: %s\n", SDL_GetError());
    }
    // the audio thread uses the platform: the device is closed before the platform goes, on every way out
    struct AudioCloser { SDL_AudioDeviceID id; ~AudioCloser() { if (id) SDL_CloseAudioDevice(id); } } audioCloser{audioDevice};
    std::vector<int16_t> audioCapture;
    platform.SetBackend(&backend, pw, ph);
    ScriptHost host(platform, cfg);
    platform.Attach(host);
    if (!host.LoadScripts()) { std::printf("LOAD FAILED: %s\n", host.LastError().c_str()); return 1; }
    if (!host.CreateStartUpAssets()) { std::printf("createStartUpAssets FAILED: %s\n", host.LastError().c_str()); return 1; }

    InputBridge input;
    bool running = true, inactive = false;
    int titleW = -1, titleH = -1;
    int frame = 0;
    Uint64 last = SDL_GetPerformanceCounter();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            // window pixels -> the game's screen units
            auto toScreen = [&](int x, int y) {
                int ww, wh;                                   // window points -> drawable pixels = screen units
                SDL_GetWindowSize(window, &ww, &wh);
                input.MouseMove(x * (float)pw / ww, y * (float)ph / wh);
            };
            switch (e.type) {
                case SDL_QUIT: running = false; break;
                case SDL_KEYDOWN:
                    // Alt+Enter switches between the window and full screen, like the original exes
                    if (e.key.keysym.sym == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT)) {
                        if (!e.key.repeat) platform.SetFullScreen(!platform.IsFullScreen());
                        break;
                    }
                    // Ctrl+minus / Ctrl+plus slow down / speed up the game until it is closed
                    if (e.key.keysym.mod & KMOD_CTRL) {
                        SDL_Keycode k = e.key.keysym.sym;
                        if (k == SDLK_MINUS || k == SDLK_KP_MINUS) platform.AddSpeed(-0.25f);
                        else if (k == SDLK_EQUALS || k == SDLK_PLUS || k == SDLK_KP_PLUS) platform.AddSpeed(0.25f);
                    }
                    input.KeyDown(MapKey(e.key.keysym.sym));
                    break;
                case SDL_KEYUP: input.KeyUp(MapKey(e.key.keysym.sym)); break;
                case SDL_MOUSEMOTION: toScreen(e.motion.x, e.motion.y); break;
                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP: {
                    toScreen(e.button.x, e.button.y);
                    const char* name = e.button.button == SDL_BUTTON_LEFT ? "LBUTTON"
                                     : e.button.button == SDL_BUTTON_RIGHT ? "RBUTTON" : "MBUTTON";
                    if (e.type == SDL_MOUSEBUTTONDOWN) input.KeyDown(KeyIndex(name)); else input.KeyUp(KeyIndex(name));
                    break;
                }
                case SDL_MOUSEWHEEL: input.Wheel((float)e.wheel.y); platform.Wheel(e.wheel.y); break;
                case SDL_WINDOWEVENT:
                    if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) input.ReleaseAll();
                    // the scripts pause the game when the window is left (gamePaused) and get gameResumed back
                    if ((e.window.event == SDL_WINDOWEVENT_FOCUS_LOST || e.window.event == SDL_WINDOWEVENT_MINIMIZED) && !inactive) {
                        inactive = true;
                        if (audioDevice) SDL_PauseAudioDevice(audioDevice, 1);     // no sound while the window is not the one in use
                        host.GamePaused();
                    } else if ((e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED || e.window.event == SDL_WINDOWEVENT_RESTORED) && inactive) {
                        inactive = false;
                        if (audioDevice) SDL_PauseAudioDevice(audioDevice, 0);
                        host.GameResumed();
                    }
                    break;
                default: break;
            }
        }

        Uint64 now = SDL_GetPerformanceCounter();
        float dt = shotFrames ? 1.0f / 30.0f : (float)((now - last) / (double)SDL_GetPerformanceFrequency());
        last = now;
        if (dt > 0.1f) dt = 0.1f;                    // after a stall, do not jump ahead
        // Frame times from the timer jitter by a millisecond or two even when the display is steady, which shows as
        // stutter in the motion. Short frames are averaged over the last four; a real stall is passed on as it is.
        if (!shotFrames) {
            static float recent[4] = {0, 0, 0, 0};
            static int recentAt = 0, recentCount = 0;
            if (dt < 0.05f) {
                recent[recentAt] = dt; recentAt = (recentAt + 1) % 4; if (recentCount < 4) recentCount++;
                float sum = 0; for (int i = 0; i < recentCount; i++) sum += recent[i];
                dt = sum / recentCount;
            } else recentCount = 0;
        }
        Uint64 frameStart = now;

        int rw, rh;
        if (platform.TakeResolutionRequest(rw, rh)) SDL_SetWindowSize(window, rw, rh);
        bool wantFull;
        if (platform.TakeFullScreenRequest(wantFull)) {
            SDL_SetWindowFullscreen(window, wantFull ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
#ifdef _WIN32
            SetGameIcon(window, wc.name);           // the title bar exists again now: give it its icon
#endif
        }
        if (platform.ExitRequested()) running = false;
        for (size_t i = 0; i < wheels.size(); i++)
            if (wheels[i].first == frame) { input.Wheel((float)wheels[i].second); platform.Wheel(wheels[i].second); }
        for (size_t i = 0; i < keys.size(); i++) {
            if (keys[i].first == frame) input.KeyDown(KeyIndex(keys[i].second));
            if (keys[i].first + 3 == frame) input.KeyUp(KeyIndex(keys[i].second));
        }
        for (size_t i = 0; i < clicks.size(); i++) {
            const Click& c = clicks[i];
            if (c.frame == frame) { input.MouseMove(c.x, c.y); input.KeyDown(KeyIndex("LBUTTON")); }
            if (c.length > 3 && frame > c.frame && frame <= c.frame + 20) {
                float t = (frame - c.frame) / 20.0f;
                input.MouseMove(c.x + (c.x2 - c.x) * t, c.y + (c.y2 - c.y) * t);
            }
            if (c.frame + c.length == frame) input.KeyUp(KeyIndex("LBUTTON"));
        }
        SDL_GL_GetDrawableSize(window, &pw, &ph);
        if (pw != titleW || ph != titleH) {              // the title ends with the size of the game: "Angry Birds (1024x600)"
            titleW = pw; titleH = ph;
            std::string title = wc.name + " (" + std::to_string(pw) + "x" + std::to_string(ph) + ")";
            SDL_SetWindowTitle(window, title.c_str());
        }
        backend.SetWindowSize(pw, ph);
        platform.Resize(pw, ph);                        // the screen is the window
        input.Apply(host.State());
        platform.BeginFrame();
        platform.SetSpeedBoost((SDL_GetModState() & KMOD_SHIFT) != 0);     // holding Shift doubles the speed
        platform.RunFrame(dt);
        platform.EndFrame();
        input.EndFrame();
        if (!audioOut.empty()) {                          // test mode: one frame is 1/30 s of sound
            size_t at = audioCapture.size();
            audioCapture.resize(at + 1470 * 2);
            platform.Audio().MixStereo(&audioCapture[at], 1470, 44100);
        }
        if (!host.LastError().empty()) { std::printf("script error: %s\n", host.LastError().c_str()); return 1; }
        frame++;

        if (shotFrames && frame >= shotFrames) {
            std::vector<unsigned char> pixels;
            int w, h;
            backend.ReadPixels(pixels, w, h);
            WritePngFile(shot.c_str(), w, h, pixels.data());
            std::printf("wrote %s (%dx%d)\n", shot.c_str(), w, h);
            if (!audioOut.empty()) {
                uint32_t bytes = (uint32_t)(audioCapture.size() * 2);
                unsigned char head[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,2,0,0x44,0xAC,0,0,0x10,0xB1,2,0,4,0,16,0,'d','a','t','a'};
                uint32_t riff = bytes + 36;
                std::memcpy(head + 4, &riff, 4); std::memcpy(head + 40, &bytes, 4);
                FILE* wf = std::fopen(audioOut.c_str(), "wb");
                if (wf) { std::fwrite(head, 1, 44, wf); std::fwrite(audioCapture.data(), 1, bytes, wf); std::fclose(wf); }
                std::printf("wrote %s\n", audioOut.c_str());
            }
            break;
        }
        Uint64 beforeSwap = SDL_GetPerformanceCounter();
        SDL_GL_SwapWindow(window);
        if (!shotFrames && std::getenv("AB_SLOWFRAMES")) {       // set AB_SLOWFRAMES=1 to list frames that took long and where the time went
            double f = (double)SDL_GetPerformanceFrequency(), end = (double)SDL_GetPerformanceCounter();
            double total = (end - frameStart) / f * 1000.0;
            if (total > 25.0) std::printf("slow frame %d: %.1f ms (game %.1f, swap %.1f)\n", frame, total, (beforeSwap - frameStart) / f * 1000.0, (end - beforeSwap) / f * 1000.0);
        }
    }
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

// A crash must not close the console window before the message can be read: after an unhandled exception,
// an uncaught C++ exception or a failed start the console waits for Enter.
static void WaitForEnter() {
#ifdef _WIN32
    if (!_isatty(_fileno(stdin))) return;                     // output is redirected: nobody is there to press Enter
#else
    if (!isatty(0)) return;
#endif
    std::printf("\nPress Enter to close this window.\n");
    std::fflush(stdout);
    int c;
    while ((c = std::getchar()) != '\n' && c != EOF) {}
}
#ifdef _WIN32
// Prints the calling functions (needs the .pdb next to the exe, which a Debug build has)
static void PrintStack() {
    HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, nullptr, TRUE);
    void* frames[48];
    int n = CaptureStackBackTrace(0, 48, frames, nullptr);
    std::printf("Stack:\n");
    for (int i = 0; i < n; i++) {
        char buf[sizeof(SYMBOL_INFO) + 256] = {};
        SYMBOL_INFO* sym = (SYMBOL_INFO*)buf;
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        DWORD64 addr = (DWORD64)frames[i], disp = 0;
        DWORD lineDisp = 0;
        IMAGEHLP_LINE64 line = {};
        line.SizeOfStruct = sizeof(line);
        if (SymFromAddr(proc, addr, &disp, sym)) {
            if (SymGetLineFromAddr64(proc, addr, &lineDisp, &line)) std::printf("  %s  (%s:%lu)\n", sym->Name, line.FileName, line.LineNumber);
            else std::printf("  %s\n", sym->Name);
        } else std::printf("  %p\n", frames[i]);
    }
    std::fflush(stdout);
}
static int OnCrtReport(int type, char* message, int* returnValue) {
    std::printf("\n%s\n", message);
    PrintStack();
    WaitForEnter();
    std::exit(3);
    (void)type; (void)returnValue;
    return TRUE;
}
static LONG WINAPI OnCrash(EXCEPTION_POINTERS* info) {
    std::printf("\nThe game crashed: exception 0x%08lx at %p\n", (unsigned long)info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress);
    PrintStack();
    WaitForEnter();
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    SetUnhandledExceptionFilter(OnCrash);
#ifdef _DEBUG
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, OnCrtReport);
#endif
#endif
    int result = 1;
    try {
        result = GameMain(argc, argv);
    } catch (const std::exception& e) {
        std::printf("\nThe game stopped: %s\n", e.what());
    } catch (...) {
        std::printf("\nThe game stopped with an unknown error.\n");
    }
    if (result != 0) WaitForEnter();
    return result;
}
