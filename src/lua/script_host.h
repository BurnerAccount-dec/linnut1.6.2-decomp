// Owns the Lua state: sets up the globals and tables the scripts expect, loads the scripts in the
// order the exe does, and calls the engine-to-script callbacks (ENGINE_SPEC.md section 3).
#pragma once
#include <string>
#include <vector>

extern "C" {
#include "lua.h"
}
#include "lua/platform.h"

namespace ab {

struct BootConfig {
    std::string scriptPath = "scripts";       // folder with gamelogic.lua, blocks.lua, ...
    std::string imagePath = "images/1024X768";   // the script takes the part after the first "/" as the asset profile (keys of loadlist.lua)
    std::string fontPath = "fonts";
    std::string audioPath = "audio";
    std::string localizationPath = "localization";
    std::string levelPath = "levels";
    std::string userPath = ".";               // where settings.lua and highscores.lua live
    std::string deviceModel = "windows";
    float screenWidth = 1024, screenHeight = 768;
    bool isLiteVersion = false;
    bool isBetaVersion = false;
    bool showCursor = true;                    // config.lua: false means the game draws its own cursor (Seasons 2.2.0 reads the global showCursor)
};

class ScriptHost {
public:
    ScriptHost(Platform& platform, const BootConfig& config);
    ~ScriptHost();
    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    // Loads starLimits, loadlist, gamelogic, blocks, particles. False (and lastError) on a Lua error.
    bool LoadScripts();
    // Development aid: remember every global that a script reads and finds nil.
    void TraceMissingGlobals(bool on);
    const std::vector<std::string>& MissingGlobals() const { return missing_; }

    // Callbacks into the scripts. A callback that does not exist is skipped.
    bool CreateStartUpAssets() { return Call("createStartUpAssets"); }
    bool Initialize() { return Call("initialize"); }
    bool Update(float dt, float realDt);
    bool IsRio() const { return rio_; }        // Angry Birds Rio: data in many files, and the exe calls draw() after update()
    bool Draw() { return Call("draw"); }
    void RunString(const char* code);             // test hook: runs a snippet among the scripts' globals
    bool BirdCollision(const std::string& a, const std::string& b, float force, float damage);
    bool BlockCollision(const std::string& a, const std::string& b, float force, bool wasDamageDone);
    bool RemoveBlocks() { return Call("removeBlocks"); }
    bool GamePaused() { return Call("gamePaused"); }
    bool GameResumed() { return Call("gameResumed"); }
    bool ResolutionChanged(float w, float h);
    bool UpdateValues() { return Call("updateValues"); }
    bool CalculatePlaytime() { return Call("calculatePlaytime"); }

    lua_State* State() { return L_; }
    const std::string& LastError() const { return error_; }

private:
    bool rio_ = false;
    bool Call(const char* name);
    bool DrawEditorOverlay();
    bool CallWithArgs(const char* name, int nargs);   // args already pushed after the function slot
    bool RunFile(const std::string& path);
    void SetString(const char* name, const std::string& v);
    void SetEnvField(const char* name);
    void GetEnvField(const char* name);
    int LoadInEnv(const std::string& path);
    void NewGlobalTable(const char* name);
    void LoadPersisted(const char* tableName, const char* fileName);

    Platform& platform_;
    BootConfig config_;
    lua_State* L_;
    std::string error_;
    std::vector<std::string> missing_;
};

}  // namespace ab
