// angrybirds: the game in a window. SDL2 gives the window, the GL context, the events and the clock.
// Usage: angrybirds --scripts <scripts folder> --data <data folder> [--screenshot <png> <frames>]
// --click <frame> <x> <y> and --drag <frame> <x> <y> <x2> <y2> (repeatable, with --screenshot) press the left button at that frame for testing.
// --screenshot runs that many frames at a fixed 1/30 s step, saves the last one and exits.
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <cmath>
#include <cstdio>
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
    int width = 1280, height = 720;
    bool fullscreen = false, showCursor = true;
};

static void ReadConfigLua(const std::string& path, WindowConfig& out) {
    lua_State* L = luaL_newstate();
    if (luaL_loadfile(L, path.c_str()) == 0 && lua_pcall(L, 0, 0, 0) == 0) {
        lua_getglobal(L, "name"); if (lua_isstring(L, -1)) out.name = lua_tostring(L, -1); lua_pop(L, 1);
        lua_getglobal(L, "width"); if (lua_isnumber(L, -1)) out.width = (int)lua_tonumber(L, -1); lua_pop(L, 1);
        lua_getglobal(L, "height"); if (lua_isnumber(L, -1)) out.height = (int)lua_tonumber(L, -1); lua_pop(L, 1);
        lua_getglobal(L, "fullscreen"); if (lua_isboolean(L, -1)) out.fullscreen = lua_toboolean(L, -1) != 0; lua_pop(L, 1);
        lua_getglobal(L, "showCursor"); if (lua_isboolean(L, -1)) out.showCursor = lua_toboolean(L, -1) != 0; lua_pop(L, 1);
        std::printf("config: %s (window %dx%d, fullscreen %d, showCursor %d)\n", path.c_str(), out.width, out.height, out.fullscreen, out.showCursor);
    }
    lua_close(L);
}

int main(int argc, char** argv) {
    std::string scripts, data, shot, configPath;
    int shotFrames = 0;
    std::string audioOut;                 // test: write the mixed sound of the --screenshot run to this .wav
    struct Click { int frame; float x, y, x2, y2; int length; };   // length 3 is a click, more is a drag to (x2, y2)
    std::vector<Click> clicks;
    std::vector<std::pair<int, int>> wheels;     // test: --wheel <frame> <notches>
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--scripts") && i + 1 < argc) scripts = argv[++i];
        else if (!std::strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
        else if (!std::strcmp(argv[i], "--config") && i + 1 < argc) configPath = argv[++i];
        else if (!std::strcmp(argv[i], "--wheel") && i + 2 < argc) { wheels.push_back({std::atoi(argv[i + 1]), std::atoi(argv[i + 2])}); i += 2; }
        else if (!std::strcmp(argv[i], "--click") && i + 3 < argc) { float x = (float)std::atof(argv[i + 2]), y = (float)std::atof(argv[i + 3]); clicks.push_back({std::atoi(argv[i + 1]), x, y, x, y, 3}); i += 3; }
        else if (!std::strcmp(argv[i], "--drag") && i + 5 < argc) { clicks.push_back({std::atoi(argv[i + 1]), (float)std::atof(argv[i + 2]), (float)std::atof(argv[i + 3]), (float)std::atof(argv[i + 4]), (float)std::atof(argv[i + 5]), 30}); i += 5; }
        else if (!std::strcmp(argv[i], "--audio-out") && i + 1 < argc) audioOut = argv[++i];
        else if (!std::strcmp(argv[i], "--screenshot") && i + 2 < argc) { shot = argv[++i]; shotFrames = std::atoi(argv[++i]); }
    }
    // without arguments the exe works like the original: put it next to the game's data folder
    if (data.empty()) data = "data";
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
    if (shotFrames) wc = WindowConfig();               // test runs use the default window

    BootConfig cfg;
    cfg.scriptPath = scripts;
    cfg.imagePath = "images/pc_build";
    cfg.fontPath = "fonts/pc_build";

    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::printf("SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_Window* window = SDL_CreateWindow(wc.name.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          wc.width, wc.height,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                                          (wc.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (!window) { std::printf("SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
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
            ((GamePlatform*)user)->Audio().Mix((int16_t*)stream, len / 4);
        };
        audioDevice = audioOk ? SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0) : 0;
        if (audioDevice) {
            SDL_PauseAudioDevice(audioDevice, 0);
            std::printf("sound: %s, %d Hz, %d channels\n", SDL_GetCurrentAudioDriver(), have.freq, have.channels);
        }
        else std::printf("no sound: %s\n", SDL_GetError());
    }
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
                case SDL_KEYDOWN: input.KeyDown(MapKey(e.key.keysym.sym)); break;
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

        int rw, rh;
        if (platform.TakeResolutionRequest(rw, rh)) SDL_SetWindowSize(window, rw, rh);
        bool wantFull;
        if (platform.TakeFullScreenRequest(wantFull)) SDL_SetWindowFullscreen(window, wantFull ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        if (platform.ExitRequested()) running = false;
        for (size_t i = 0; i < wheels.size(); i++)
            if (wheels[i].first == frame) { input.Wheel((float)wheels[i].second); platform.Wheel(wheels[i].second); }
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
        platform.RunFrame(dt);
        platform.EndFrame();
        input.EndFrame();
        if (!audioOut.empty()) {                          // test mode: one frame is 1/30 s of sound
            size_t at = audioCapture.size();
            audioCapture.resize(at + 1470 * 2);
            platform.Audio().Mix(&audioCapture[at], 1470);
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
        SDL_GL_SwapWindow(window);
    }
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
