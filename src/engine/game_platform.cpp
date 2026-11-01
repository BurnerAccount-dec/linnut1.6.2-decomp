#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iterator>
#include "engine/game_platform.h"
#include "video/video_audio.h"
#include "engine/level_writer.h"

#include <cmath>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdio>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace ab {

class GamePlatform::Host : public LuaFrameHost {
public:
    Host(ScriptHost& scripts, GamePlatform& platform) : LuaFrameHost(scripts), platform_(platform) {
        objectNameOf = [this](int id) { return platform_.Physics().NameOf(id); };
        jointNameOf = [this](int index) { return platform_.Physics().JointName(index); };
    }
    void StepWorld(float dt, int velIter, int posIter) override {
        platform_.Physics().Step(dt, velIter, posIter);
        static const char* dumpEvery = std::getenv("AB_BODYDUMP");   // debug: print every body each N steps
        static int step = 0;
        if (dumpEvery && step == 0) { float gx, gy; platform_.Physics().GetGravity(gx, gy); std::fprintf(stderr, "BG %g %g ver=%d\n", gx, gy, (int)platform_.Physics().Version()); }
        if (dumpEvery && step == 0) for (const auto& o : platform_.Physics().Objects()) { BodyInfo bi = platform_.Physics().Inspect(o->name); std::fprintf(stderr, "BI %s st=%d aw=%d m=%g ld=%g ad=%g r=%g\n", o->name.c_str(), (int)bi.isStatic, (int)bi.awake, bi.mass, bi.linearDamping, bi.angularDamping, bi.shapeRadius); }
        if (dumpEvery && step == 0) for (const auto& o : platform_.Physics().Objects()) if (o->pbody && o->pbody->IsStatic()) std::fprintf(stderr, "BS %s %.3f %.3f %.3f\n", o->name.c_str(), o->pbody->PosX(), o->pbody->PosY(), o->pbody->Angle());
        if (dumpEvery && ++step % std::atoi(dumpEvery) == 0) {
            for (const auto& o : platform_.Physics().Objects())
                if (o->pbody && !o->pbody->IsStatic())
                    std::fprintf(stderr, "BD %d %s %.3f %.3f %.3f%s\n", step, o->name.c_str(), o->pbody->PosX(), o->pbody->PosY(), o->pbody->Angle(), o->pbody->IsAwake() ? " A" : "");
        }
    }
    void ClearForces() override { platform_.Physics().ClearForces(); }
    // Audio loops and the effects manager are not implemented yet.
    bool LoopActive(LoopSound) override { return false; }
    void StartLoop(LoopSound, float) override {}
    void SetLoopVolume(LoopSound, float) override {}
    void StopLoop(LoopSound) override {}
    void UpdateEffects(float) override {}

private:
    GamePlatform& platform_;
};

namespace {

// Pushes objects.world[name] (nil if there is none) and leaves it on the stack.
void PushWorldEntry(lua_State* L, const std::string& name) {
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_getfield(L, -1, "objects");
    lua_remove(L, -2);
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "world");
        lua_remove(L, -2);
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, name.c_str());
            lua_remove(L, -2);
            return;
        }
    }
    lua_pop(L, 1);
    lua_pushnil(L);
}

bool GetNumber(lua_State* L, int table, const char* key, float& out) {
    lua_getfield(L, table, key);
    bool ok = lua_type(L, -1) == LUA_TNUMBER;
    if (ok) out = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);
    return ok;
}

// A table of material -> number, such as blockTable.damageFactors.X.damageMultiplier
void ReadNumberMap(lua_State* L, int table, const char* key, std::map<std::string, float>& out) {
    lua_getfield(L, table, key);
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            if (lua_type(L, -2) == LUA_TSTRING && lua_type(L, -1) == LUA_TNUMBER)
                out[lua_tostring(L, -2)] = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
}

// Fills the Lua side of a ContactObject from its objects.world entry (at stack index `entry`).
void ReadContactFields(lua_State* L, int entry, ContactObject& c, DamageFactorSet& factors) {
    c.hasStrength = GetNumber(L, entry, "strength", c.strength);
    c.hasDefence = GetNumber(L, entry, "defence", c.defence);
    lua_getfield(L, entry, "material");
    if (lua_type(L, -1) == LUA_TSTRING) c.material = lua_tostring(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, entry, "useLegacyCollisionPath");
    c.useLegacyCollisionPath = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);

    // objects.world[name].damageFactors names a set in blockTable.damageFactors
    lua_getfield(L, entry, "damageFactors");
    if (lua_type(L, -1) == LUA_TSTRING) {
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        lua_getfield(L, -1, "blockTable");
        lua_remove(L, -2);
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "damageFactors");
            lua_remove(L, -2);
            if (lua_istable(L, -1)) {
                lua_pushvalue(L, -2);
                lua_gettable(L, -2);
                lua_remove(L, -2);
                if (lua_istable(L, -1)) {
                    ReadNumberMap(L, lua_gettop(L), "damageMultiplier", factors.damageMultiplier);
                    ReadNumberMap(L, lua_gettop(L), "velocityMultiplier", factors.velocityMultiplier);
                    c.damageFactors = &factors;
                }
            }
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

// scoreTable.blocks (nil when the script has none)
void PushBlockScore(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_getfield(L, -1, "scoreTable");
    lua_remove(L, -2);
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "blocks");
        lua_remove(L, -2);
        return;
    }
    lua_pop(L, 1);
    lua_pushnil(L);
}

}  // namespace

GamePlatform::GamePlatform(const std::string& dataRoot) : dataRoot_(dataRoot) {}

// The physics world is made on first use, when the scripts have told which game this is: Rio uses Box2D 2.2.0, the others 2.1.x.
PhysicsWorld& GamePlatform::Physics() {
    if (physics_) return *physics_;
    Box2DVersion ver = scripts_ && scripts_->IsRio() ? Box2DVersion::V22 : Box2DVersion::V21;
    if (const char* f = std::getenv("AB_BOX2D")) ver = std::string(f) == "21" ? Box2DVersion::V21 : Box2DVersion::V22;  // test override
    if (requestedBox2D_) ver = requestedBox2D_ == 21 ? Box2DVersion::V21 : Box2DVersion::V22;
    physics_ = MakePhysicsWorld(ver);
    physics_->startAsleep = std::getenv("AB_ASLEEPSTART") != nullptr;   // v2.2: the exe creates every body awake (b2BodyDef.awake = 1 at 0x411590 and the other creators); AB_ASLEEPSTART=1 starts blocks asleep for testing
    if (std::getenv("AB_PHYSDBG")) std::fprintf(stderr, "PHYS Box2D %s\n", physics_->Version() == Box2DVersion::V22 ? "2.2.0" : "2.1.1");
    physics_->onBeginContact = [this](PhysObject& a, PhysObject& b, ContactHandle& c) { OnContact(a, b, c); };
    // the contact filter reads objects.world[name].material live
    physics_->materialOf = [this](const PhysObject& o) {
        std::string material;
        if (!scripts_) return material;
        lua_State* L = scripts_->State();
        PushWorldEntry(L, o.name);
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "material");
            if (lua_type(L, -1) == LUA_TSTRING) material = lua_tostring(L, -1);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        return material;
    };
    return *physics_;
}
GamePlatform::~GamePlatform() {}

void GamePlatform::Attach(ScriptHost& scripts) {
    scripts_ = &scripts;
    host_.reset(new Host(scripts, *this));
}

// Game::update (0x408980) starts the sound output by itself whenever settings.audioEnabled is true and
// the output exists but is not running; the scripts only ever call stopAudioOutput.
void GamePlatform::AddSpeed(float step) {
    speed_ += step;
    if (speed_ < 0.25f) speed_ = 0.25f;
    if (speed_ > 8.0f) speed_ = 8.0f;
    Print("speed: " + std::to_string(speed_) + "x");
}

void GamePlatform::RunFrame(float dt) {
    dt *= speed_ * (speedBoost_ ? 2.0f : 1.0f);
    // v2.2: the script globals gameSpeedFactor (the time step of everything: scripts, animations, physics, particles) and
    // physicsSpeedFactor (only the physics steps; the retail 3.2.0+ exes scaled everything with it) are read every frame.
    // Both are plain Lua globals, so scripts and the console (set / lua) can read and write them.
    float physicsScale = 1.0f;
    if (scripts_) {
        lua_State* L = scripts_->State();
        int top = lua_gettop(L);
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        lua_getfield(L, -1, "gameSpeedFactor");
        if (lua_isnumber(L, -1) && lua_tonumber(L, -1) > 0) dt *= (float)lua_tonumber(L, -1);
        lua_getfield(L, -2, "physicsSpeedFactor");
        if (lua_isnumber(L, -1) && lua_tonumber(L, -1) > 0) physicsScale = (float)lua_tonumber(L, -1);
        lua_settop(L, top);
    }
    Physics().Frame().physicsScale = physicsScale;
    std::string audioError = audio_.TakeError();
    if (!audioError.empty()) Print("audio: " + audioError);
    if (scripts_ && audio_.Created() && !audio_.Started()) {
        lua_State* L = scripts_->State();
        int top = lua_gettop(L);
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        lua_getfield(L, -1, "settings");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "audioEnabled");
            if (lua_isboolean(L, -1) && lua_toboolean(L, -1)) audio_.Start();
        }
        lua_settop(L, top);
    }
    UpdateZoom(dt);
    // Seasons restart (R key): the script makes the pause page the current page while the level loads and the
    // page's offset is still 0 from before it was laid out, so it shows next to "Loading..." until the game
    // mode takes over. The page is hidden (animation at 0) then, so keep it off screen for those frames.
    if (scripts_ && !pauseGuard_ && IsSeasonsScripts()) {
        lua_State* L = scripts_->State();
        int top = lua_gettop(L);
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        lua_getfield(L, -1, "drawMenu");
        if (lua_isfunction(L, -1)) {
            pauseGuard_ = true;
            const char* code =
                "local orig = drawMenu\n"
                "drawMenu = function(...)\n"
                "  local pp = pausePage\n"
                "  if pp and currentGameMode == updateLoading and currentMenuPage == pp and pp.offsetX == 0 and (pauseBGw or 0) > 0 then\n"
                "    local a = elementAnimations and elementAnimations.ingamePausePageScroll\n"
                "    if a and (a.percentage or 0) <= 0 then\n"
                "      pp.offsetX = -pauseBGw\n"
                "      if pp.backgroundOverlay then pp.backgroundOverlay.shade = 0 end\n"
                "    end\n"
                "  end\n"
                "  return orig(...)\n"
                "end\n";
            if (luaL_loadstring(L, code) == 0) {
                lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
                lua_setfenv(L, -2);
                if (lua_pcall(L, 0, 0, 0) != 0) std::fprintf(stderr, "pause guard: %s\n", lua_tostring(L, -1));
            }
        }
        lua_settop(L, top);
    }
    if (host_) UpdateFrame(Physics().Frame(), *host_, dt);
    {   // test hook: AB_EXEC_FRAME=n AB_EXEC="lua code" runs a snippet on frame n
        static int execFrame = 0;
        const char* at = std::getenv("AB_EXEC_FRAME");
        const char* at2 = std::getenv("AB_EXEC2_FRAME");
        const char* at3 = std::getenv("AB_EXEC3_FRAME");
        ++execFrame;
        if (scripts_ && at && execFrame == std::atoi(at) && std::getenv("AB_EXEC")) scripts_->RunString(std::getenv("AB_EXEC"));
        if (scripts_ && at2 && execFrame == std::atoi(at2) && std::getenv("AB_EXEC2")) scripts_->RunString(std::getenv("AB_EXEC2"));
        if (scripts_ && at3 && execFrame == std::atoi(at3) && std::getenv("AB_EXEC3")) scripts_->RunString(std::getenv("AB_EXEC3"));
    }
    if (scripts_ && scripts_->IsRio() && dt > 0) {
        if (Physics().Frame().running) UpdateRioGettingUp(dt);
        SelectRioAnimations(dt);
        if (Physics().Frame().running) { UpdateRioBirdFaces(dt); UpdateRioSquash(dt); }
    }
    if (scripts_ && scripts_->IsRio()) scripts_->Draw();      // Rio: the exe calls draw() after update()
    // 0x40a8cb: the particles move only while the physics is enabled
    bool seasons = IsSeasonsScripts();
    if (Physics().Frame().running) UpdateParticles(dt, true, seasons ? -1 : 0);
    // Seasons: the menus switch their own particles on with the global enableMenuParticles
    if (scripts_) {
        lua_State* L = scripts_->State();
        int top = lua_gettop(L);
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        lua_getfield(L, -1, "enableMenuParticles");
        bool menu = lua_toboolean(L, -1) != 0;
        lua_settop(L, top);
        if (menu) UpdateParticles(dt, false);
    }
    // 2.0.0 (0x40a8cb): the menu list always moves
    if (!seasons) UpdateParticles(dt, false, 1);
}

void GamePlatform::Print(const std::string& text) {
    log_.push_back(text);
    std::fputs(text.c_str(), stdout);
    if (text.empty() || text[text.size() - 1] != '\n') std::fputc('\n', stdout);
}

// 0x40d2c0: the old level's bodies are destroyed and the step accumulator restarts
void GamePlatform::BeginLevelLoad() {
    if (physics_ && requestedBox2D_ && physics_->Version() != (requestedBox2D_ == 21 ? Box2DVersion::V21 : Box2DVersion::V22)) {
        physics_.reset();                         // the console asked for the other Box2D: the old level is gone anyway, make the new world
        std::printf("console: physics is now Box2D %s\n", requestedBox2D_ == 21 ? "2.1.1" : "2.2.0");
    }
    // v2.2: every level starts in a brand new world. After a Clear the old world keeps its broadphase tree and contact order, and a restarted
    // stack then settled differently from the first load (Ham 'em High 12-1 / 12-8 caved in on some restarts). AB_KEEPWORLD=1: the old behaviour.
    if (physics_ && std::getenv("AB_KEEPWORLD") == nullptr) {
        float gx = 0, gy = 0;
        physics_->GetGravity(gx, gy);
        physics_->Clear();
        physics_.reset();
        Physics().SetGravity(gx, gy);
    }
    Physics().Clear();
    Physics().SetMaxTranslation(2.0f);          // 0x40d2c0 writes 2.0 and 4.0 to the two translation limits
    // a new level (or a restart) starts without the old one's particles and bird trails
    particles_.clear();
    themeSprites_.clear();
    for (int i = 0; i < 6; i++) { trajDots_[i].clear(); trajPuffs_[i].clear(); }
    birdFaces_.clear();
}

void GamePlatform::CreateThemeSprite(const std::string& name, const std::string& sprite, float x, float y, float scaleX, float scaleY, float angle, float speedX, int layer) {
    themeSprites_.push_back({name, sprite, x, y, scaleX, scaleY, angle, speedX, layer});
}

// Angry Birds Rio (the exe's 0x4182a0 / 0x4185e0 / 0x418470): the angle is in radians, rotateThemeSprites turns it by angleVel a second
void GamePlatform::CreateThemeSpriteRio(const std::string& name, const std::string& sprite, float x, float y, float scaleX, float scaleY, float angle, int layer, float angleVel, bool flipped) {
    static const bool dbg = std::getenv("AB_THEMEDBG") != nullptr;
    if (dbg) std::fprintf(stderr, "THEME create %s sprite=%s x=%g y=%g s=%g,%g a=%g layer=%d vel=%g flip=%d\n", name.c_str(), sprite.c_str(), x, y, scaleX, scaleY, angle, layer, angleVel, (int)flipped);
    ThemeSprite t{name, sprite, x, y, scaleX, scaleY, angle, 0, layer};
    t.angleVel = angleVel; t.flipped = flipped;
    themeSprites_.push_back(t);
}

void GamePlatform::SetThemeSprite(const std::string& name, const std::string& sprite, int layer) {
    for (ThemeSprite& t : themeSprites_)
        if (t.name == name && t.layer == layer) { t.sprite = sprite; break; }
}

void GamePlatform::RotateThemeSprites(float dt) {
    const float twoPi = 6.2831855f;
    for (ThemeSprite& t : themeSprites_) {
        if (t.angleVel == 0) continue;
        t.angle = std::fmod(t.angle + t.angleVel * dt, twoPi);
        if (t.angle < 0) t.angle += twoPi;
    }
}

// updateBackgroundAndForegroundScrollingNative (0x41a450): every layer's offset grows by autoScroll * dt * 60
void GamePlatform::ScrollThemeLayers(float dt, bool force) {
    (void)force;
    for (BgLayer& l : bgLayers_) l.offset += l.autoScroll * dt * 60.0f;
    for (size_t i = 0; i < fgOffset_.size(); i++) fgOffset_[i] += fgAuto_[i] * dt * 60.0f;
}

void GamePlatform::ModifyThemeSprite(const std::string& name, float x, float y, float scaleX, float scaleY, float angle, int layer) {
    for (ThemeSprite& t : themeSprites_)
        if (t.name == name) { t.x = x; t.y = y; t.scaleX = scaleX; t.scaleY = scaleY; t.angle = angle; t.layer = layer; }
}

void GamePlatform::RemoveThemeSprite(const std::string& name, int layer) {
    for (size_t i = 0; i < themeSprites_.size(); i++)
        if (themeSprites_[i].name == name && themeSprites_[i].layer == layer) { themeSprites_.erase(themeSprites_.begin() + (long)i); break; }
}

// updateThemeSprite (the exe's 0x417550): sprites of the background layers drift by speedX (physics units a second)
// and wrap around the screen, left to right when moving left. The foreground layers are not updated.
// Level paths come from the scripts without the extension (loadLevel adds it, so saveLevel does too).
void GamePlatform::SaveLevel(const std::string& path) {
    std::string file = path;
    if (file.size() < 4 || file.compare(file.size() - 4, 4, ".lua") != 0) file += ".lua";
    std::string error;
    bool absolute = file.size() > 1 && (file[1] == ':' || file[0] == '/');     // v2.2: a full path is used as it is
    std::string text;
    bool written = scripts_ && ab::WriteLevelFile(scripts_->State(), absolute ? file : ResolveDataFile(file), IsSeasonsScripts(), error, &text);
    if (!text.empty()) savedLevels_[file] = text;       // v2.2: kept in memory too, so TAB in the editor still finds its temp copy when the game folder is read-only
    if (!written) Print("saveLevel: " + error);
}

bool GamePlatform::FindSavedLevel(const std::string& path, std::vector<uint8_t>& bytes) {
    std::string file = path;
    if (file.size() < 4 || file.compare(file.size() - 4, 4, ".lua") != 0) file += ".lua";
    auto it = savedLevels_.find(file);
    if (it == savedLevels_.end()) return false;
    bytes.assign(it->second.begin(), it->second.end());
    return true;
}

void GamePlatform::CreateDirectory(const std::string& path) {
    std::string dir = ResolveDataFile(path);
    while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
    ab::MakeDirectory(dir);
}

bool GamePlatform::CheckDirectory(const std::string& path) {
    std::string dir = ResolveDataFile(path);
    while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
    return ab::DirectoryExists(dir);
}

bool GamePlatform::IsSeasonsScripts() {
    if (!scripts_) return false;
    lua_State* L = scripts_->State();
    int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_getfield(L, -1, "enableMenuParticles");
    bool yes = !lua_isnil(L, -1);
    lua_settop(L, top);
    return yes;
}

void GamePlatform::UpdateThemeSprites(float dt) {
    for (ThemeSprite& t : themeSprites_) {
        if (t.speedX == 0 || t.layer >= (int)bgLayers_.size()) continue;
        const Sprite* sp = sprites_.Find(t.sprite);
        if (!sp) continue;
        float shift = bgLayers_[t.layer].speed * camX_;
        if (IsSeasonsScripts()) {
            // Seasons: a sprite goes round only once it is wholly off the screen (its full width, from its pivot)
            // and comes back wholly off the other side, so it never shows up out of nowhere
            float sw = (float)sp->w * t.scaleX, pivot = (float)sp->pivotX * t.scaleX;
            float margin = sw * 0.5f;
            float leftEdge = t.x * 20.0f - pivot, rightEdge = leftEdge + sw;
            float lo = shift, hi = screenWidth_ / worldScale_ + shift;
            t.x += t.speedX * dt;
            leftEdge = t.x * 20.0f - pivot; rightEdge = leftEdge + sw;
            if (t.speedX < 0 && rightEdge < lo - margin) t.x = (hi + margin + pivot) / 20.0f;
            else if (t.speedX > 0 && leftEdge > hi + margin) t.x = (lo - margin - sw + pivot) / 20.0f;
            continue;
        }
        float left = t.x * 20.0f - (float)sp->w * t.scaleX;
        float leftBound = shift - (float)sp->h * t.scaleX * 1.1f;
        float rightBound = screenWidth_ / worldScale_ + shift;
        t.x += t.speedX * dt;
        if (left < leftBound && t.speedX < 0) t.x = ((float)sp->w * t.scaleX + rightBound) / 20.0f;
        else if (left > rightBound && t.speedX > 0) t.x = ((float)sp->w * t.scaleX + leftBound) / 20.0f;
    }
}

// The sprites of one layer, placed in the same world as the physics objects (units * 20) and moved by the
// layer's own camera shift. The size is the world scale times the sprite's own scale.
void GamePlatform::DrawThemeSprites(int layer) {
    RenderState layerState = state_;
    for (const ThemeSprite& t : themeSprites_) {
        if (t.layer != layer) continue;
        const Sprite* sp = sprites_.Find(t.sprite);
        float px = t.x * 20.0f * worldScale_ - themeShift_ * layerState.sx;
        float py = t.y * 20.0f * worldScale_ + layerState.ty * layerState.sy;
        state_ = RenderState();
        state_.sx = worldScale_ * t.scaleX * (t.flipped ? -1.0f : 1.0f); state_.sy = worldScale_ * t.scaleY;
        state_.angle = t.flipped ? -t.angle : t.angle;
        if (sp) { state_.pivotX = (float)sp->pivotX; state_.pivotY = (float)sp->pivotY; }
        ResDrawSprite(t.sprite, px / state_.sx, py / state_.sy, 4, 3, false, 0, 0);
    }
    state_ = layerState;
}

std::string GamePlatform::ResolveDataFile(const std::string& path) {
    std::string full = dataRoot_.empty() ? path : dataRoot_ + "/" + path;
    // the Hatchery (Angry Birds Island HD scripts) asks for its images and fonts in the "pc" profile; its iPad data has only "1024x768"
    size_t pc = full.find("hatchery/");
    if (pc != std::string::npos) {
        size_t profile = full.find("/pc/", pc);
        if (profile != std::string::npos && !std::ifstream(full.c_str())) {
            std::string alt = full.substr(0, profile) + "/1024x768/" + full.substr(profile + 4);
            if (std::ifstream(alt.c_str())) return alt;
        }
    }
    return full;
}

// Angry Birds Rio: every frame the exe calls the script's selectObjectAnimation(name, speed, angle, dt) for each entry of
// animatedObjects (marmosets, Nigel...). The script picks the animation whose trigger ranges contain the speed (physics
// units per second) and the angle (degrees), starts it after its threshold time and, for animations marked destroyJoints
// (a hanging marmoset that is hit hard enough), removes the object's joints.
void GamePlatform::SelectRioAnimations(float dt) {
    lua_State* L = scripts_->State();
    int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_getfield(L, -1, "animatedObjects");
    lua_getfield(L, -2, "selectObjectAnimation");
    if (!lua_istable(L, top + 2) || !lua_isfunction(L, top + 3)) { lua_settop(L, top); return; }
    std::vector<std::string> names;
    lua_pushnil(L);
    while (lua_next(L, top + 2)) {
        if (lua_type(L, -2) == LUA_TSTRING) names.push_back(lua_tostring(L, -2));
        lua_pop(L, 1);
    }
    for (const std::string& name : names) {
        PushWorldEntry(L, name);
        bool alive = lua_istable(L, -1);
        lua_pop(L, 1);
        if (!alive) continue;
        PhysObject* po = Physics().Find(name);
        if (po && po->gettingUp) continue;           // 0x40b6xx: the script is not asked while the marmoset stands up
        // the exe passes the direction the object moves in (atan2 of the velocity, in degrees) and its speed
        float degrees = 0;
        if (po && po->pbody) degrees = std::atan2(po->pbody->VelY(), po->pbody->VelX()) * 57.29578f;
        lua_pushvalue(L, top + 3);
        lua_pushstring(L, name.c_str());
        lua_pushnumber(L, Physics().GetSpeed(name));
        lua_pushnumber(L, degrees);
        lua_pushnumber(L, dt);
        if (lua_pcall(L, 4, 0, 0) != 0) {
            std::fprintf(stderr, "selectObjectAnimation: %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    lua_settop(L, top);
}

// Box2D BeginContact -> the exe's collision damage code (0x40b670). The rules are in ResolveContact;
// this reads the Lua fields it needs, applies the result and calls birdCollision / blockCollision.
// (The two skip flags at Game+0x1e9 / 0x1ea are only ever set to 0 in the exe, so they are left out.)
void GamePlatform::OnContact(PhysObject& pa, PhysObject& pb, ContactHandle& contact) {
    if (scripts_ && scripts_->IsRio()) {         // Rio (0x40daa6): a hit starts the scale wobble of both objects that have a bounce multiplier
        float dpx = pa.pbody->Mass() * pa.pbody->VelX() - pb.pbody->Mass() * pb.pbody->VelX();
        float dpy = pa.pbody->Mass() * pa.pbody->VelY() - pb.pbody->Mass() * pb.pbody->VelY();
        float dpLength = std::sqrt(dpx * dpx + dpy * dpy);
        float wobble = std::min(dpLength / 10.0f * 0.02f, 0.1f);      // the momentum difference over the world scale, as for the damage
        static const bool dbg = std::getenv("AB_WOBDBG") != nullptr;
        if (dbg) std::fprintf(stderr, "DBG contact %s/%s dp=%.3f amps %.2f %.2f\n", pa.name.c_str(), pb.name.c_str(), dpLength, pa.bounceAmp, pb.bounceAmp);
        for (PhysObject* o : {&pa, &pb}) {
            if (o->bounceAmp > 0 && o->wobCur < wobble) { o->wobAmp0 = wobble; o->wobActive = true; }
        }
    }
    if (!scripts_) return;
    lua_State* L = scripts_->State();
    int top = lua_gettop(L);

    // both names must be keys of objects.world
    PushWorldEntry(L, pa.name);
    PushWorldEntry(L, pb.name);
    if (!lua_istable(L, top + 1) || !lua_istable(L, top + 2)) {
        lua_settop(L, top);
        return;
    }
    ContactObject a = PhysicsWorld::MakeContactObject(pa), b = PhysicsWorld::MakeContactObject(pb);
    DamageFactorSet factorsA, factorsB;
    ReadContactFields(L, top + 1, a, factorsA);
    ReadContactFields(L, top + 2, b, factorsB);

    // the score is read before the callback runs and written after it
    float scoreBefore = 0;
    PushBlockScore(L);
    if (lua_istable(L, -1)) GetNumber(L, lua_gettop(L), "score", scoreBefore);
    lua_pop(L, 1);

    ContactResult r = ResolveContact(a, b);
    static const bool dmgDbg = std::getenv("AB_DMGDBG") != nullptr;   // v2.2: AB_DMGDBG=1 prints every contact that damages or destroys a block
    if (dmgDbg && (!r.destroyed.empty() || !r.strengthUpdates.empty())) {
        static int n = 0; n++;
        std::fprintf(stderr, "DMG #%d %s vs %s (v %.2f,%.2f / %.2f,%.2f):", n, pa.name.c_str(), pb.name.c_str(), pa.pbody->VelX(), pa.pbody->VelY(), pb.pbody->VelX(), pb.pbody->VelY());
        for (const auto& u : r.strengthUpdates) std::fprintf(stderr, " %s->%.2f", u.name.c_str(), u.strength);
        for (const auto& d : r.destroyed) std::fprintf(stderr, " DESTROYED %s", d.c_str());
        std::fprintf(stderr, "\n");
    }

    for (size_t i = 0; i < r.strengthUpdates.size(); i++) {
        PushWorldEntry(L, r.strengthUpdates[i].name);
        if (lua_istable(L, -1)) {
            lua_pushnumber(L, r.strengthUpdates[i].strength);
            lua_setfield(L, -2, "strength");
        }
        lua_pop(L, 1);
    }
    for (size_t i = 0; i < r.destroyed.size(); i++) {      // deadBlocks[name] = objects.world[name]
        PushWorldEntry(L, r.destroyed[i]);
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        lua_getfield(L, -1, "deadBlocks");
        lua_remove(L, -2);
        if (lua_istable(L, -1) && lua_istable(L, -2)) {
            lua_pushvalue(L, -2);
            lua_setfield(L, -2, r.destroyed[i].c_str());
        }
        lua_pop(L, 2);
    }
    if (r.hasVelocityChange) Physics().ApplyVelocityChange(r.velocityChange);
    if (r.disableContact) contact.SetEnabled(false);
    lua_settop(L, top);

    for (size_t i = 0; i < r.events.size(); i++) {
        const CollisionEvent& e = r.events[i];
        if (e.isBird) birdCollisions++; else blockCollisions++;
        if (e.isBird) scripts_->BirdCollision(e.object1, e.object2, e.force, e.damage);
        else scripts_->BlockCollision(e.object1, e.object2, e.force, e.wasDamageDone);
    }

    if (r.blockScoreAdd > 0) {
        PushBlockScore(L);
        if (lua_istable(L, -1)) {
            lua_pushnumber(L, scoreBefore + r.blockScoreAdd);
            lua_setfield(L, -2, "score");
        }
        lua_settop(L, top);
    }
}

// ---- drawing

void GamePlatform::SetBackend(RenderBackend* backend, int screenWidth, int screenHeight) {
    backend_ = backend;
    screenWidth_ = screenWidth;
    screenHeight_ = screenHeight;
}

void GamePlatform::Resize(int screenWidth, int screenHeight) {
    bool changed = sizeKnown_ && (screenWidth != screenWidth_ || screenHeight != screenHeight_);
    sizeKnown_ = true;
    screenWidth_ = screenWidth;
    screenHeight_ = screenHeight;
    if (!scripts_) return;
    lua_State* L = scripts_->State();
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_pushnumber(L, screenWidth); lua_setfield(L, -2, "screenWidth");
    lua_pushnumber(L, screenHeight); lua_setfield(L, -2, "screenHeight");
    lua_pop(L, 1);
    // the exe tells the scripts when the window is resized: they lay the cameras and pages out again
    if (changed) scripts_->ResolutionChanged((float)screenWidth, (float)screenHeight);
}

bool GamePlatform::TakeResolutionRequest(int& w, int& h) {
    if (!hasReq_) return false;
    w = reqW_; h = reqH_; hasReq_ = false;
    return true;
}

bool GamePlatform::CheckForLuaFile(const std::string& path) {
    std::ifstream f(ResolveDataFile(path));
    return f.good();
}

void GamePlatform::BeginFrame() {
    cursorDrawn_ = false;
    if (backend_) backend_->BeginFrame(screenWidth_, screenHeight_, bgR_, bgG_, bgB_);
}

void GamePlatform::EndFrame() {
    FlushZ();
    // Rio: once the level is no longer drawn (exited to the menu or the loading screen) its particles are gone, and so are the result-screen stars (menu list) that were still alive
    if (gameDrawnLast_ && !gameDrawn_ && scripts_ && scripts_->IsRio()) {
        static const bool partDbg2 = std::getenv("AB_PARTDBG") != nullptr;
        if (partDbg2) std::fprintf(stderr, "THEME level left: %d particles\n", (int)particles_.size());
        particles_.clear();
    }
    gameDrawnLast_ = gameDrawn_; gameDrawn_ = false;
    // 2.0.0 draws the menu list itself, in screen space, over everything (0x4161c0)
    if (!menuParticlesByScript_ && !IsSeasonsScripts()) DrawParticlesOf(false, 1);
    FlushZ();
    if (!cursors_.empty() && backend_) {
        backend_->SetClip(false, 0, 0, 0, 0);
        RenderState keep = state_;
        std::vector<PendingCursor> list;
        list.swap(cursors_);
        for (const PendingCursor& c : list) {
            state_ = c.st;
            drawingCursor_ = true;
            ResDrawSprite(c.name, c.x, c.y, c.vAnchor, c.hAnchor, c.sized, c.width, c.height);
            drawingCursor_ = false;
        }
        state_ = keep;
        FlushZ();
    }
    if (backend_) backend_->EndFrame();
}

// 0x40c570: each channel is clamped to 0..255 and rounded
void GamePlatform::SetBGColor(float r, float g, float b) {
    auto channel = [](float v) { return std::floor(std::min(255.0f, std::max(0.0f, v)) + 0.5f) / 255.0f; };
    bgR_ = channel(r); bgG_ = channel(g); bgB_ = channel(b);
}

// 0x408630: only the values that were passed are changed
void GamePlatform::SetRenderState(int argc, const float* a) {
    if (argc >= 2) { state_.tx = a[0]; state_.ty = a[1]; }
    if (argc >= 4) { state_.sx = a[2]; state_.sy = a[3]; }
    if (argc >= 5) state_.angle = a[4];
    if (argc >= 7) { state_.pivotX = a[5]; state_.pivotY = a[6]; }
    // setRenderState(tx, ty, sx, sy, angle, pivotX, pivotY, alpha): alpha is 0 (invisible) .. 1 (opaque); it stays until it is set again
    if (argc >= 8) state_.alpha = a[7] < 0 ? 0 : (a[7] > 1 ? 1 : a[7]);
    // v2.2: setRenderState(tx, ty, sx, sy, angle, pivotX, pivotY, alpha, skewX, skewY, bendX, bendY, z [, bendFunction])
    // bendFunction is a tween function like tweenLinear(t, start, change, duration) of gamelogic.lua; it gives the shape of the bend
    // (it is asked for t = 0, 1/8 .. 1 with start 0, change 1, duration 1); without it the bend is an arch. Numbers come from tweens as they are.
    if (argc >= 10) { state_.skewX = a[8]; state_.skewY = a[9]; }
    if (argc >= 12) { state_.bendX = a[10]; state_.bendY = a[11]; }
    if (argc >= 13) state_.z = a[12];
    if (argc >= 22) for (int i = 0; i < 9; i++) state_.bendProfile[i] = a[13 + i];
}

// A quad with the sort kind of what is being drawn: a game object, a sprite with a render state z, or neither.
GamePlatform::QueuedQuad GamePlatform::MakeQuad(int tex, float alpha, float r, float g, float b) {
    QueuedQuad q = {};
    q.tex = tex; q.mask = -1; q.alpha = alpha; q.r = r; q.g = g; q.b = b;
    if (drawingCursor_) q.kind = 0;
    else if (drawingNative_) { q.kind = 1; q.key = nativeKey_; }
    else if (state_.z != 0) { q.kind = 2; q.key = state_.z; }
    return q;
}

// Puts a quad in the frame list. Kind 0 goes to the end. A game object (kind 1) goes to the end too, unless a sprite with a higher z is
// already in the list after the last object: then it goes below that sprite. A sprite (kind 2) goes right after the last object or
// sprite whose z is not higher than its own; with none, before the first one that is higher; with no objects or sprites, at the end.
void GamePlatform::EmitQuad(QueuedQuad& q) {
    size_t pos = zQueue_.size();
    if (q.kind == 1 && spriteCount_ > 0) {
        size_t from = zQueue_.size();
        while (from > 0 && zQueue_[from - 1].kind != 1) from--;      // just after the last object
        for (size_t i = from; i < zQueue_.size(); i++)
            if (zQueue_[i].kind == 2 && zQueue_[i].key > q.key) { pos = i; break; }
    } else if (q.kind == 2 && keyedCount_ > 0) {
        size_t after = (size_t)-1, before = (size_t)-1;
        for (size_t i = 0; i < zQueue_.size(); i++) {
            if (!zQueue_[i].kind) continue;
            if (zQueue_[i].key <= q.key) after = i;
            else if (before == (size_t)-1) before = i;
        }
        if (after != (size_t)-1) pos = after + 1;
        else if (before != (size_t)-1) pos = before;
    }
    if (q.kind) keyedCount_++;
    if (q.kind == 2) spriteCount_++;
    zQueue_.insert(zQueue_.begin() + pos, q);
}

void GamePlatform::FlushZ() {
    if (zQueue_.empty()) return;
    std::vector<QueuedQuad> q;
    q.swap(zQueue_);
    keyedCount_ = spriteCount_ = 0;
    if (!backend_) return;
    for (const QueuedQuad& c : q) {
        if (c.mask >= 0) backend_->DrawMaskedQuad(c.tex, c.mask, c.xy, c.tuv, c.uv, c.alpha);
        else backend_->DrawQuad(c.tex, c.xy, c.uv, c.r, c.g, c.b, c.alpha);
    }
}

void GamePlatform::ResSetClipRect(float x, float y, float w, float h) {
    FlushZ();
    if (backend_) backend_->SetClip(true, x, y, w, h);
}

const GamePlatform::TextureInfo* GamePlatform::TextureFor(const SpriteSheet& sheet) {
    return TextureFile(sheet.directory + "/" + sheet.image, false);
}

const GamePlatform::TextureInfo* GamePlatform::TextureFile(const std::string& path, bool repeat) {
    // a file used both as a sprite sheet image and as a tiling texture needs one texture for each wrap mode
    const std::string key = repeat ? path + "|repeat" : path;
    auto it = textures_.find(key);
    if (it != textures_.end()) return it->second.handle >= 0 ? &it->second : nullptr;
    TextureInfo info;
    Image image;
    std::string error;
    if (LoadPngFile(path, image, &error)) {
        info.handle = backend_->CreateTexture(image, repeat);
        info.width = image.width;
        info.height = image.height;
    } else {
        Print("texture: " + error);
    }
    TextureInfo& stored = textures_[key] = info;
    return stored.handle >= 0 ? &stored : nullptr;
}

// A point of a sprite: rotate by the state's angle about (cx, cy), then translate and scale.
void GamePlatform::Transform(float x, float y, float cx, float cy, float& ox, float& oy) const {
    float c = std::cos(state_.angle), s = std::sin(state_.angle);
    float dx = x - cx, dy = y - cy;
    if (state_.skewX != 0 || state_.skewY != 0) { float sdx = dx + state_.skewX * dy, sdy = dy + state_.skewY * dx; dx = sdx; dy = sdy; }   // v2.2 shear about the pivot
    float rx = c * dx - s * dy, ry = s * dx + c * dy;
    ox = (cx + rx + state_.tx) * state_.sx;
    oy = (cy + ry + state_.ty) * state_.sy;
}

// Sprite_handleEvent (0x43aab0) and drawSprite (0x434b30 / 0x434c60): the anchors move the sprite's
// top left corner away from (x, y) by a part of its own size, the quad is drawn at the sprite's size
// (or the size passed), with the sprite's rectangle of the sheet as texture coordinates.
void GamePlatform::ResDrawSprite(const std::string& name, float x, float y, int vAnchor, int hAnchor, bool sized, float width, float height) {
    if (!backend_) return;
    if (!drawingCursor_ && name.compare(0, 7, "CURSOR_") == 0) {
        // v2.2: the hand cursor is drawn last and on top: the draw order (z), blur, pixelation and transparency of what was drawn before it do not apply to it
        // (an editor that fades its panels left the cursor invisible). Only a cursor sprite that exists counts as drawn, otherwise the system cursor stays.
        if (sprites_.Find(name) || sprites_.FindCompo(name)) cursorDrawn_ = true;
        // the cursor is drawn at the end of the frame (EndFrame), so a background, a panel or a game drawn after the script's cursor call cannot cover it,
        // and with the clip rectangle and the z, blur, pixelation, transparency and tint of the sprites before it taken away
        PendingCursor c{name, x, y, vAnchor, hAnchor, sized, width, height, state_};
        c.st.z = 0; c.st.blur = 0; c.st.pixelate = 0; c.st.alpha = 1; c.st.tintR = c.st.tintG = c.st.tintB = 1; c.st.skewX = c.st.skewY = 0; c.st.bendX = c.st.bendY = 0;
        cursors_.push_back(c);
        return;
    }
    const SpriteSheet* sheet = nullptr;
    const Sprite* sp = sprites_.Find(name, &sheet);
    if (sp) { DrawSpriteAt(*sheet, *sp, x, y, vAnchor, hAnchor, sized, width, height); return; }
    // Sprite_handleAllEvents (0x43ae10): the anchors move the composite's origin, then every part is
    // drawn at its own position with the pivot anchors.
    const CompoSprite* compo = sprites_.FindCompo(name);
    if (!compo) return;
    int w, h, ox, oy;
    sprites_.CompoExtent(*compo, w, h, ox, oy);
    switch (vAnchor) {
        case 0: y += (float)oy; break;
        case 1: y += (float)(oy - (h >> 1)); break;
        case 2: y += (float)(oy - h); break;
        default: break;
    }
    switch (hAnchor) {
        case 0: x += (float)ox; break;
        case 1: x += (float)(ox - (w >> 1)); break;
        case 2: x += (float)(ox - w); break;
        default: break;
    }
    for (const CompoPart& part : compo->parts) {
        const SpriteSheet* partSheet = nullptr;
        const Sprite* ps = sprites_.Find(part.sprite, &partSheet);
        if (ps) DrawSpriteAt(*partSheet, *ps, x + (float)part.x, y + (float)part.y, 4, 3, false, 0, 0);
    }
}

void GamePlatform::ResCreateCompoSpriteSet(const std::string& path) {
    std::string error;
    if (!sprites_.LoadCompoSet(path, ResolveDataFile(path), &error)) Print("createCompoSpriteSet: " + error);
}

// 0x4313d0: left, top, right, bottom of the composite relative to its origin
void GamePlatform::ResGetCompoSpriteBounds(const std::string&, const std::string& name, float& x1, float& y1, float& x2, float& y2) {
    x1 = y1 = x2 = y2 = 0;
    const CompoSprite* compo = sprites_.FindCompo(name);
    if (!compo) return;
    int w, h, ox, oy;
    sprites_.CompoExtent(*compo, w, h, ox, oy);
    x1 = (float)-ox; y1 = (float)-oy; x2 = (float)(w - ox); y2 = (float)(h - oy);
}

void GamePlatform::DrawSpriteAt(const SpriteSheet& sheet, const Sprite& spr, float x, float y, int vAnchor, int hAnchor, bool sized, float width, float height) {
    const Sprite* sp = &spr;
    const TextureInfo* tex = TextureFor(sheet);
    if (!tex) return;

    switch (vAnchor) {
        case 1: y -= (float)(sp->h >> 1); break;
        case 2: y -= (float)sp->h; break;
        case 3: case 4: y -= (float)sp->pivotY; break;
        default: break;
    }
    switch (hAnchor) {
        case 1: x -= (float)(sp->w >> 1); break;
        case 2: x -= (float)sp->w; break;
        case 3: x -= (float)sp->pivotX; break;
        default: break;
    }
    float w = sized ? width : (float)sp->w, h = sized ? height : (float)sp->h;
    float cx = x + state_.pivotX, cy = y + state_.pivotY;
    float xy[8], uv[8];
    const float cornerX[4] = {0, w, w, 0}, cornerY[4] = {0, 0, h, h};
    for (int i = 0; i < 4; i++) Transform(x + cornerX[i], y + cornerY[i], cx, cy, xy[i * 2], xy[i * 2 + 1]);
    float tileUV[8];
    if (tileTexture_) {
        // the tile's uv is the world position (rotated, before the camera) in texture sizes
        float c = std::cos(state_.angle), s = std::sin(state_.angle);
        for (int i = 0; i < 4; i++) {
            float dx = x + cornerX[i] - cx, dy = y + cornerY[i] - cy;
            tileUV[i * 2] = (cx + c * dx - s * dy) / tileTexture_->width;
            tileUV[i * 2 + 1] = (cy + s * dx + c * dy) / tileTexture_->height;
        }
    }
    float u0 = (float)sp->x / tex->width, v0 = (float)sp->y / tex->height;
    float u1 = (float)(sp->x + sp->w) / tex->width, v1 = (float)(sp->y + sp->h) / tex->height;
    // 2.0.0's tutorial sheet is packed tight, so the smooth filter picks up the next sprite's pixels along the
    // edges (thin coloured lines); keep the samples half a texel inside the sprite
    if ((sized || sheet.image.find("TUTORIALS") != std::string::npos) && sp->w > 1 && sp->h > 1) {
        u0 += 0.5f / tex->width; u1 -= 0.5f / tex->width; v0 += 0.5f / tex->height; v1 -= 0.5f / tex->height;
    }
    if (state_.flipX) std::swap(u0, u1);
    const float us[4] = {u0, u1, u1, u0}, vs[4] = {v0, v0, v1, v1};
    for (int i = 0; i < 4; i++) { uv[i * 2] = us[i]; uv[i * 2 + 1] = vs[i]; }
    if (tileTexture_) {
        QueuedQuad q = MakeQuad(tex->handle, state_.alpha, 1, 1, 1);
        q.mask = tex->handle; q.tex = tileTexture_->handle;     // DrawMaskedQuad(tile, sprite, ...): the sprite is the mask
        std::memcpy(q.xy, xy, sizeof xy); std::memcpy(q.uv, uv, sizeof uv); std::memcpy(q.tuv, tileUV, sizeof tileUV);
        EmitQuad(q);
        return;
    }
    // v2.2: a bent sprite is drawn as a grid of 8 x 8 pieces; each vertex is moved by bendX * profile(row) / bendY * profile(column)
    const int n = (state_.bendX != 0 || state_.bendY != 0) ? 8 : 1;
    // v2.2 pixelation: the sprite is cut into blocks of state_.pixelate sprite pixels, each block showing the texture at its middle
    int nx = n, ny = n;
    const bool pixelated = state_.pixelate > 1 && w > 0 && h > 0;
    if (pixelated) { nx = (int)std::ceil(w / state_.pixelate); ny = (int)std::ceil(h / state_.pixelate); if (nx > 64) nx = 64; if (ny > 64) ny = 64; if (nx < 1) nx = 1; if (ny < 1) ny = 1; }
    auto profile = [&](float t) {
        float p = t * 8; int i = p >= 8 ? 7 : (int)p; float f = p - i;
        return state_.bendProfile[i] + (state_.bendProfile[i + 1] - state_.bendProfile[i]) * f;
    };
    auto vertex = [&](int gi, int gj, float* oxy, float* ouv, float uvU, float uvV) {
        float u = (float)gi / nx, v = (float)gj / ny;
        float lx = w * u, ly = h * v;
        if (state_.bendX != 0 || state_.bendY != 0) { lx += state_.bendX * profile(v); ly += state_.bendY * profile(u); }
        Transform(x + lx, y + ly, cx, cy, oxy[0], oxy[1]);
        ouv[0] = us[0] + (us[1] - us[0]) * uvU;
        ouv[1] = vs[0] + (vs[3] - vs[0]) * uvV;
    };
    // v2.2 blur: the sprite is drawn up to 9 times spread over state_.blur pixels; the t-th copy has 1/t of the alpha, which averages the copies
    const int taps = state_.blur > 0 ? 9 : 1;
    for (int tap = 0; tap < taps; tap++) {
        float offX = 0, offY = 0;
        if (tap > 0) { float ang = (tap - 1) * 0.785398f; offX = std::cos(ang) * state_.blur; offY = std::sin(ang) * state_.blur; }
        const float tapAlpha = state_.alpha / (float)(tap + 1);
        for (int gj = 0; gj < ny; gj++)
            for (int gi = 0; gi < nx; gi++) {
                float cxy[8], cuv[8];
                const int di[4] = {0, 1, 1, 0}, dj[4] = {0, 0, 1, 1};
                for (int k = 0; k < 4; k++) {
                    float uvU = pixelated ? (gi + 0.5f) / nx : (float)(gi + di[k]) / nx;
                    float uvV = pixelated ? (gj + 0.5f) / ny : (float)(gj + dj[k]) / ny;
                    vertex(gi + di[k], gj + dj[k], cxy + k * 2, cuv + k * 2, uvU, uvV);
                    cxy[k * 2] += offX; cxy[k * 2 + 1] += offY;
                }
                QueuedQuad q = MakeQuad(tex->handle, tapAlpha, state_.tintR, state_.tintG, state_.tintB);
                std::memcpy(q.xy, cxy, sizeof cxy); std::memcpy(q.uv, cuv, sizeof cuv);
                EmitQuad(q);
            }
    }
}

// 0x40abd0: colour 0..1, corners are whole numbers. Without inWorld the render state is first reset.
void GamePlatform::DrawRect(float r, float g, float b, float a, float x1, float y1, float x2, float y2, bool inWorld) {
    if (!inWorld) state_ = RenderState();
    if (!backend_) return;
    x1 = std::trunc(x1); y1 = std::trunc(y1); x2 = std::trunc(x2); y2 = std::trunc(y2);
    float xy[8], uv[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const float px[4] = {x1, x2, x2, x1}, py[4] = {y1, y1, y2, y2};
    for (int i = 0; i < 4; i++) Transform(px[i], py[i], x1 + state_.pivotX, y1 + state_.pivotY, xy[i * 2], xy[i * 2 + 1]);
    QueuedQuad q = MakeQuad(-1, a, r, g, b);
    std::memcpy(q.xy, xy, sizeof xy); std::memcpy(q.uv, uv, sizeof uv);
    EmitQuad(q);
}

// v2.2: lines and rectangles turn about (first corner + pivot) like sprites do (the retail exes' debug overlays on the 8 key rely on it); before, the pivot was ignored
// 0x415310: whole-number coordinates and width, colour 0..255
void GamePlatform::DrawLine2D(float x1, float y1, float x2, float y2, float width, float r, float g, float b, float a) {
    if (!backend_) return;
    x1 = std::trunc(x1); y1 = std::trunc(y1); x2 = std::trunc(x2); y2 = std::trunc(y2);
    width = std::trunc(width);
    float dx = x2 - x1, dy = y2 - y1, len = std::sqrt(dx * dx + dy * dy);
    if (len == 0 || width <= 0) return;
    float nx = -dy / len * width * 0.5f, ny = dx / len * width * 0.5f;
    float px[4] = {x1 + nx, x2 + nx, x2 - nx, x1 - nx}, py[4] = {y1 + ny, y2 + ny, y2 - ny, y1 - ny};
    float xy[8], uv[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 4; i++) Transform(px[i], py[i], x1 + state_.pivotX, y1 + state_.pivotY, xy[i * 2], xy[i * 2 + 1]);
    QueuedQuad q = MakeQuad(-1, a / 255.0f, r / 255.0f, g / 255.0f, b / 255.0f);
    std::memcpy(q.xy, xy, sizeof xy); std::memcpy(q.uv, uv, sizeof uv);
    EmitQuad(q);
}

void GamePlatform::ResCreateSpriteSheet(const std::string& path) {
    std::string error;
    // debug build: the sheets of Angry Birds Island HD that 2.0.0's scripts ask for (the challenge list, the Hatchery and Challenges buttons of the
    // episode selection, the splash screen) are taken from the Hatchery's image folder when it has them
    static const bool debugBuild = std::getenv("AB_NODEBUGKEYS") == nullptr;
    std::string base = path.substr(path.find_last_of("/\\") == std::string::npos ? 0 : path.find_last_of("/\\") + 1);
    std::string alt = "hatchery/images/1024x768/" + base;
    bool hatcheryOn = debugBuild && !ResolveDataFile("hatchery/hatchery.lua").empty() && std::ifstream(ResolveDataFile("hatchery/hatchery.lua")).good();
    if (hatcheryOn && base == "SPLASHES_SHEET_1.dat") {
        sprites_.Load(path, ResolveDataFile(path), &error);
        std::string altError;
        if (sprites_.Load(alt, ResolveDataFile(alt), &altError)) return;       // the later sheet wins, so the Hatchery's splash is the one drawn
        if (error.empty()) return;
    } else if (sprites_.Load(path, ResolveDataFile(path), &error)) {
        if (hatcheryOn && base == "LEVELSELECTION_SHEET_3.dat") {
            std::string ignored;
            for (const char* extra : {"BUTTONS_HATCHERY_1.dat", "LEVELSELECTION_HATCHERY_1.dat", "LEVELSELECTION_SHEET_4.dat", "MENU_HATCHERY_1.dat", "INGAME_BIRDS_2.dat"}) {
                std::string file = std::string("hatchery/images/1024x768/") + extra;
                sprites_.Load(file, ResolveDataFile(file), &ignored);
            }
            // the episode selection of 2.0.0 asks for H_BTN_HATCHERY, the iPad sheet calls the same button H_BUTTON_HATCHERY
            const SpriteSheet* sheet = nullptr;
            if (!sprites_.Find("H_BTN_HATCHERY") && sprites_.Find("H_BUTTON_HATCHERY", &sheet) && sheet) {
                SpriteSheet alias = *sheet;
                alias.sprites.clear();
                for (const Sprite& sp : sheet->sprites) if (sp.name == "H_BUTTON_HATCHERY") { Sprite renamed = sp; renamed.name = "H_BTN_HATCHERY"; alias.sprites.push_back(renamed); }
                sprites_.Add("alias:H_BTN_HATCHERY", std::move(alias));
            }
        }
        // The exe uploads a sheet's texture when the sheet is created (behind the loading screen). Decoding the png on its first draw
        // instead made the frames after a menu or level opened stutter, the more so the more sheets a game or mod has.
        if (backend_) if (const SpriteSheet* made = sprites_.Sheet(path)) TextureFor(*made);
        return;
    } else if (hatcheryOn) {
        std::string altError;
        if (sprites_.Load(alt, ResolveDataFile(alt), &altError)) return;
    }
    // Seasons lists its composite sprite files with the sheets
    if (error == "no SPRT chunk" && sprites_.LoadCompoSet(path, ResolveDataFile(path), &error)) return;
    Print("createSpriteSheet " + path + ": " + error);
}

void GamePlatform::ResReleaseSpriteSheet(const std::string& path) { sprites_.Release(path); }

void GamePlatform::ResGetSpriteBounds(const std::string&, const std::string& sprite, float& w, float& h) {
    const Sprite* s = sprites_.Find(sprite);
    w = s ? (float)s->w : 0.0f;
    h = s ? (float)s->h : 0.0f;
    // sprites and composite sprites share one name table: a composite's size is its extent
    if (!s) if (const CompoSprite* compo = sprites_.FindCompo(sprite)) {
        int cw, ch, ox, oy;
        sprites_.CompoExtent(*compo, cw, ch, ox, oy);
        w = (float)cw; h = (float)ch;
    }
}

void GamePlatform::ResGetSpritePivot(const std::string&, const std::string& sprite, float& px, float& py) {
    const Sprite* s = sprites_.Find(sprite);
    px = s ? (float)s->pivotX : 0.0f;
    py = s ? (float)s->pivotY : 0.0f;
    if (!s) if (const CompoSprite* compo = sprites_.FindCompo(sprite)) {
        int cw, ch, ox, oy;
        sprites_.CompoExtent(*compo, cw, ch, ox, oy);
        px = (float)ox; py = (float)oy;
    }
}

// ---- fonts and text

namespace {

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& bytes) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return false;
    bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// "dir/FONT_BASIC.dat" -> "FONT_BASIC"
std::string BaseName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

}  // namespace

void GamePlatform::ResCreateBitmapFont(const std::string& path) {
    std::string full = ResolveDataFile(path), error;
    std::vector<uint8_t> bytes;
    BitmapFont font;
    if (!ReadWholeFile(full, bytes)) { Print("createBitmapFont: cannot open " + full); return; }
    if (!ParseBitmapFont(bytes, font, &error)) { Print("createBitmapFont: " + full + ": " + error); return; }
    size_t slash = full.find_last_of("/\\");
    font.directory = slash == std::string::npos ? "." : full.substr(0, slash);
    fonts_[BaseName(path)] = font;
}

const BitmapFont* GamePlatform::CurrentFont() const {
    auto it = fonts_.find(current_);
    return it == fonts_.end() ? nullptr : &it->second;
}

void GamePlatform::ResCreateTextGroupSet(const std::string& path) {
    std::string full = ResolveDataFile(path), error;
    std::vector<uint8_t> bytes;
    TextGroup group;
    if (!ReadWholeFile(full, bytes)) { Print("createTextGroupSet: cannot open " + full); return; }
    if (!ParseTextGroup(bytes, group, &error)) { Print("createTextGroupSet: " + full + ": " + error); return; }
    textGroups_[BaseName(path)] = group;
}

std::string GamePlatform::ResGetString(const std::string& group, const std::string& key) {
    auto g = textGroups_.find(group);
    if (g == textGroups_.end()) return key;
    auto s = g->second.strings.find(key);
    return s == g->second.strings.end() ? key : s->second;
}

float GamePlatform::ResGetStringWidth(const std::string& text) {
    const BitmapFont* f = CurrentFont();
    return f ? f->StringWidth(text) : 0.0f;
}
float GamePlatform::ResGetFontHeight() { const BitmapFont* f = CurrentFont(); return f ? (float)(f->maxAscent + f->maxDescent) : 0.0f; }
float GamePlatform::ResGetFontLeading() { const BitmapFont* f = CurrentFont(); return f ? (float)f->leading : 0.0f; }
float GamePlatform::ResGetFontTracking() { const BitmapFont* f = CurrentFont(); return f ? (float)f->tracking : 0.0f; }
float GamePlatform::ResGetFontMaxAscending() { const BitmapFont* f = CurrentFont(); return f ? (float)f->maxAscent : 0.0f; }
float GamePlatform::ResGetFontMaxDescending() { const BitmapFont* f = CurrentFont(); return f ? (float)f->maxDescent : 0.0f; }

// drawString(group, text, x, y, anchor, anchor) (0x430800, font draw 0x43d260). The two anchor words may come in
// either order; the defaults are TOP and LEFT.
void GamePlatform::ResDrawString(const std::string& group, const std::string& key, float x, float y,
                                 const std::string& a1, const std::string& a2) {
    // a text group name means the text is a key of that group ("MI_PLAY"); an empty group draws the text itself
    const std::string text = group.empty() ? key : ResGetString(group, key);
    const BitmapFont* font = CurrentFont();
    if (!font || !backend_) return;
    int v = 0, h = 0;
    const std::string* words[2] = {&a1, &a2};
    for (int i = 0; i < 2; i++) {
        if (*words[i] == "TOP") v = 0; else if (*words[i] == "VCENTER") v = 1; else if (*words[i] == "BOTTOM") v = 2;
        else if (*words[i] == "BASELINE" || *words[i] == "VPIVOT") v = 3;     // y is the baseline: no move
        else if (*words[i] == "LEFT") h = 0; else if (*words[i] == "HCENTER") h = 1; else if (*words[i] == "RIGHT") h = 2;
    }
    float width = font->StringWidth(text);
    if (v == 0) y += (float)font->maxAscent;
    else if (v == 1) y += (float)(font->maxAscent - (font->maxAscent + font->maxDescent) / 2);
    else if (v == 2) y -= (float)font->maxDescent;
    if (h == 1) x -= (float)((int)width / 2);
    else if (h == 2) x -= width;

    SpriteSheet sheet;
    sheet.image = font->image;
    sheet.directory = font->directory;
    std::vector<uint32_t> chars = DecodeUtf8(text);
    for (size_t i = 0; i < chars.size(); i++) {
        auto g = font->glyphs.find(chars[i]);
        if (g == font->glyphs.end()) continue;
        DrawSpriteAt(sheet, g->second, x, y, 4, 3, false, 0, 0);
        x += (float)(font->tracking + g->second.w);
    }
}

// clipText: breaks the text at spaces so that no line is wider than maxWidth (the exe's exact rules are not
// known yet; this is the usual greedy wrap, and newlines in the text start a new line).
std::vector<std::string> GamePlatform::ClipText(const std::string& group, const std::string& key, float maxWidth, float& widest) {
    const std::string text = group.empty() ? key : ResGetString(group, key);
    std::vector<std::string> lines;
    widest = 0;
    const BitmapFont* font = CurrentFont();
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        std::string paragraph = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        std::string line;
        size_t p = 0;
        while (p <= paragraph.size()) {
            size_t sp = paragraph.find(' ', p);
            std::string word = paragraph.substr(p, sp == std::string::npos ? std::string::npos : sp - p);
            std::string trial = line.empty() ? word : line + " " + word;
            if (!line.empty() && font && maxWidth > 0 && font->StringWidth(trial) > maxWidth) {
                lines.push_back(line);
                line = word;
            } else {
                line = trial;
            }
            if (sp == std::string::npos) break;
            p = sp + 1;
        }
        lines.push_back(line);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    if (font) for (size_t i = 0; i < lines.size(); i++) widest = std::max(widest, font->StringWidth(lines[i]));
    return lines;
}

}  // namespace ab

namespace ab {

// ---- the main game's picture

// setTheme (reads blockTable.themes[name]): colour, groundColor, bgLayers, fgLayers, texture.
void GamePlatform::SetTheme(const std::string& theme) {
    bgLayers_.clear(); fgLayers_.clear(); fgSpeed_.clear(); fgScale_.clear(); fgAuto_.clear(); fgOffset_.clear(); themeTexture_.clear();
    if (!scripts_) return;
    lua_State* L = scripts_->State();
    int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_getfield(L, -1, "blockTable");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "themes");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, theme.c_str());
            if (lua_istable(L, -1)) {
                int t = lua_gettop(L);
                auto number = [&](int table, const char* key, float def) {
                    lua_getfield(L, table, key);
                    float v = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : def;
                    lua_pop(L, 1);
                    return v;
                };
                lua_getfield(L, t, "color");
                if (lua_istable(L, -1)) SetBGColor(number(lua_gettop(L), "r", 0), number(lua_gettop(L), "g", 0), number(lua_gettop(L), "b", 0));
                lua_pop(L, 1);
                lua_getfield(L, t, "groundColor");
                if (lua_istable(L, -1)) {
                    groundR_ = number(lua_gettop(L), "r", 0); groundG_ = number(lua_gettop(L), "g", 0); groundB_ = number(lua_gettop(L), "b", 0);
                }
                lua_pop(L, 1);
                lua_getfield(L, t, "texture");
                if (lua_isstring(L, -1)) themeTexture_ = lua_tostring(L, -1);
                lua_pop(L, 1);
                lua_getfield(L, t, "bgLayers");
                if (lua_istable(L, -1)) {
                    int n = (int)lua_objlen(L, -1);
                    for (int i = 1; i <= n; i++) {
                        lua_rawgeti(L, -1, i);
                        if (lua_istable(L, -1)) {
                            BgLayer layer;
                            lua_rawgeti(L, -1, 2);
                            if (lua_isstring(L, -1)) layer.sprite = lua_tostring(L, -1);
                            lua_pop(L, 1);
                            lua_rawgeti(L, -1, 3);
                            if (lua_isnumber(L, -1)) layer.speed = (float)lua_tonumber(L, -1);
                            lua_pop(L, 1);
                            lua_rawgeti(L, -1, 4);
                            if (lua_isnumber(L, -1)) layer.scale = (float)lua_tonumber(L, -1);
                            lua_pop(L, 1);
                            lua_rawgeti(L, -1, 5);
                            if (lua_isboolean(L, -1)) layer.looping = lua_toboolean(L, -1) != 0;
                            else if (lua_isnumber(L, -1)) layer.autoScroll = (float)lua_tonumber(L, -1);     // Rio: the layer drifts by this many pixels a frame
                            lua_pop(L, 1);
                            bgLayers_.push_back(layer);
                        }
                        lua_pop(L, 1);
                    }
                }
                lua_pop(L, 1);
                lua_getfield(L, t, "fgLayers");
                if (lua_istable(L, -1)) {
                    int n = (int)lua_objlen(L, -1);
                    for (int i = 1; i <= n; i++) {
                        lua_rawgeti(L, -1, i);
                        if (lua_istable(L, -1)) {
                            lua_rawgeti(L, -1, 2);
                            if (lua_isstring(L, -1)) {
                                fgLayers_.push_back(lua_tostring(L, -1));
                                lua_pop(L, 1);
                                lua_rawgeti(L, -1, 3);
                                fgSpeed_.push_back(lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 1.0f);
                                lua_pop(L, 1);
                                lua_rawgeti(L, -1, 4);
                                fgScale_.push_back(lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 1.5f);
                                lua_pop(L, 1);
                                lua_rawgeti(L, -1, 5);
                                fgAuto_.push_back(lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f);
                                fgOffset_.push_back(0.0f);
                            }
                            lua_pop(L, 1);
                        }
                        lua_pop(L, 1);
                    }
                }
                lua_pop(L, 1);
            }
        }
    }
    lua_settop(L, top);
}

// ---- mouse-wheel zoom (App::update 0x402f90 animates, 0x403b00 handles the wheel, 0x408b5f feeds the scripts)

// The zoom eases from its start to its target with 1 + (u - 1)^3 over the duration. The scripts see the
// difference between the zoom and the scale they last set, halved, added to the global zoomLevel.
void GamePlatform::UpdateZoom(float dt) {
    if (zoomT_ > -1.0f) {
        zoomT_ += dt;
        float u = zoomT_ / zoomDur_ - 1.0f;
        zoom_ = zoomStart_ + (zoomTarget_ - zoomStart_) * (u * u * u + 1.0f);
        if (zoomT_ > zoomDur_) { zoomT_ = -1.0f; zoom_ = zoomTarget_; }
        if (zoom_ < 0.01f) zoom_ = 0.01f;
        if (zoom_ > 100.0f) zoom_ = 100.0f;
    }
    if (zoom_ != zoomPrev_ && scripts_) {
        lua_State* L = scripts_->State();
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        lua_getfield(L, -1, "zoomLevel");
        float level = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
        lua_pop(L, 1);
        lua_pushnumber(L, level + (zoom_ - zoomPrev_) * 0.5f);
        lua_setfield(L, -2, "zoomLevel");
        lua_pop(L, 1);
    }
}

// A wheel step moves the target by notches * 0.2 (0.1 below a scale of 1). While an animation is already
// running, between 0.6 and the maximum scale, the step is halved and added to the old target.
void GamePlatform::Wheel(int notches) {
    float step = zoom_ >= 1.0f ? 0.2f : 0.1f;
    zoomPrev_ = zoom_;
    if (zoomT_ > -1.0f && zoom_ > 0.6f && zoom_ < zoomMax_) {
        zoomTarget_ = step * 0.5f * (float)notches + zoomTarget_;
        zoomDur_ = 1.0f - zoomT_;
    } else {
        zoomTarget_ = (float)notches * step + zoom_;
        zoomDur_ = 0.5f;
    }
    if (zoomTarget_ < 0.01f) zoomTarget_ = 0.01f;
    if (zoomTarget_ > 100.0f) zoomTarget_ = 100.0f;
    zoomStart_ = zoom_;
    zoomT_ = 0.0f;
}

// drawBackgroundNative: every looping layer is a row of copies of its sprite, shifted by the camera
// times the layer's parallax speed.
void GamePlatform::DrawBackground() {
    if (!backend_) return;
    // The non-looping layers (the cranes) are placed one after another: the first at episode4BGCranes.startX / 16
    // sprite widths, each next one half a sprite width further.
    float startX = 0;
    if (scripts_) {
        lua_State* L = scripts_->State();
        int top = lua_gettop(L);
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        lua_getfield(L, -1, "episode4BGCranes");
        if (lua_istable(L, -1)) GetNumber(L, lua_gettop(L), "startX", startX);
        lua_settop(L, top);
    }
    float place = 0;
    bool firstFixed = true;
    for (const BgLayer& layer : bgLayers_) {
        // a layer is a sprite or a composite sprite (the Seasons hills are composites)
        const Sprite* sp = sprites_.Find(layer.sprite);
        const CompoSprite* compo = sp ? nullptr : sprites_.FindCompo(layer.sprite);
        int cw = 0, ch = 0, ox = 0, oy = 0;
        if (compo) sprites_.CompoExtent(*compo, cw, ch, ox, oy);
        float w = sp ? (float)sp->w : (float)cw, pivotX = sp ? (float)sp->pivotX : (float)ox;
        if (w <= 0) continue;
        float s = layer.scale, inv = 1.0f / s;
        float baseShift = layer.speed * camX_, shift = layer.speed * (camX_ + layer.offset);   // 0x41a6e7: (offset + camera) * parallax
        state_ = RenderState();
        state_.ty = -camY_ * inv;
        state_.sx = state_.sy = s * worldScale_;
        if (layer.looping) {
            float m = std::fmod(shift, w * s);
            float x0 = -(m + w * s) * inv;
            state_.tx = x0 - std::trunc(x0);
            float right = screenWidth_ * inv / worldScale_ + pivotX;
            for (float x = std::trunc(x0); x <= right; x += w) ResDrawSprite(layer.sprite, x, 0, 4, 3, false, 0, 0);
        } else {
            place += firstFixed ? startX * 0.0625f * w : w * 0.5f;
            firstFixed = false;
            state_.tx = place - std::trunc(place);
            ResDrawSprite(layer.sprite, place - shift * inv, 0, 4, 3, false, 0, 0);
        }
        themeShift_ = baseShift * inv;
        DrawThemeSprites((int)(&layer - &bgLayers_[0]));
    }
    state_ = RenderState();
}

// drawForegroundNative: the ground colour below the first foreground sprite, then the foreground
// sprites repeated at 1.5 times the world scale.
void GamePlatform::DrawForeground() {
    if (!backend_) return;
    // a foreground layer is a sprite or a composite sprite (the Ham 'o' Ween menu draws its trees and pumpkins as composites)
    auto extent = [&](const std::string& name, float& w, float& pivotX, float& h, float& pivotY) {
        const Sprite* sp = sprites_.Find(name);
        if (sp) { w = (float)sp->w; pivotX = (float)sp->pivotX; h = (float)sp->h; pivotY = (float)sp->pivotY; return true; }
        const CompoSprite* compo = sprites_.FindCompo(name);
        if (!compo) return false;
        int cw = 0, ch = 0, ox = 0, oy = 0;
        sprites_.CompoExtent(*compo, cw, ch, ox, oy);
        w = (float)cw; pivotX = (float)ox; h = (float)ch; pivotY = (float)oy;
        return true;
    };
    float height = 0;
    if (!fgLayers_.empty()) {
        float w, px, h, py;
        if (extent(fgLayers_[0], w, px, h, py)) height = (h - py) * fgScale_[0] / 1.5f;
    }
    DrawRect(groundR_ / 255.0f, groundG_ / 255.0f, groundB_ / 255.0f, 1, 0, -(camY_ - height * 1.5f) * worldScale_, (float)screenWidth_, (float)screenHeight_ + 1, false);
    int fgIndex = -1;
    for (const std::string& name : fgLayers_) {
        fgIndex++;
        float w, px, h, py;
        if (!extent(name, w, px, h, py) || w <= 0) continue;
        float c = fgScale_[fgIndex], baseShift = fgSpeed_[fgIndex] * camX_, shift = fgSpeed_[fgIndex] * (camX_ + fgOffset_[fgIndex]);
        float m = std::fmod(shift, c * w);
        float x0 = -(m + c * w) / c;
        state_ = RenderState();
        state_.tx = x0 - std::trunc(x0);
        state_.ty = -camY_ / c;
        state_.sx = state_.sy = worldScale_ * c;
        float right = screenWidth_ / (worldScale_ * c) + px;
        for (float x = std::trunc(x0); x <= right; x += w) ResDrawSprite(name, x, 0, 4, 3, false, 0, 0);
        themeShift_ = baseShift / c;
        DrawThemeSprites((int)bgLayers_.size() + fgIndex);
    }
    state_ = RenderState();
}

// One object: its sprite rotated about its pivot at the interpolated position (physics units * 20).
void GamePlatform::DrawPhysObject(PhysObject& o) {
    const SpriteSheet* sheet = nullptr;
    const Sprite* sp = sprites_.Find(o.sprite, &sheet);
    const CompoSprite* compo = sp ? nullptr : sprites_.FindCompo(o.sprite);
    if (!sp && !compo) return;
    float x = o.interpX * 20.0f, y = o.interpY * 20.0f;
    state_.angle = o.interpAngle;
    if (!o.texture.empty() && sp) {
        const TextureInfo* t = TextureFile(sheet->directory + "/" + o.texture + ".png", true);
        if (t) tileTexture_ = t;
    }
    // 0x4165xx: a scaled sprite shrinks about its pivot, the pivot stays where it was on the screen
    state_.alpha = o.alpha;                       // setObjectAlpha; the state is put back by the caller
    RenderState saved = state_;
    if (o.disableRotation) state_.angle = 0;     // 0x41b2c0: setObjectParameter 10 draws the sprite upright
    // the draw scale is the object's own scale plus the wobble after a hit (0x40b2xx: x gets +sin, y gets -sin of the same phase)
    float drawSX = (o.scaled ? o.scaleX : 1.0f) + o.wobDX, drawSY = (o.scaled ? o.scaleY : 1.0f) + o.wobDY;
    if ((o.scaled || o.wobDX != 0 || o.wobDY != 0) && drawSX != 0 && drawSY != 0) {
        state_.sx *= drawSX; state_.sy *= drawSY;
        state_.tx = (x + saved.tx) / drawSX - x;
        state_.ty = (y + saved.ty) / drawSY - y;
    }
    if (sp && !o.flipX) {
        state_.pivotX = (float)sp->pivotX;
        state_.pivotY = (float)sp->pivotY;
        DrawSpriteAt(*sheet, *sp, x, y, 4, 3, false, 0, 0);
    } else {
        // Angry Birds Rio draws its monkeys and caged birds as composite sprites (every part is drawn relative to the
        // object's position and the whole thing turns about it), and some objects are mirrored: a mirrored part sits
        // on the other side of the position
        auto drawPart = [&](const SpriteSheet& sh, const Sprite& s, float left, float top) {
            if (o.flipX) {
                left = -(left + (float)s.w);
                state_.flipX = true;
            }
            state_.pivotX = -left;
            state_.pivotY = -top;
            DrawSpriteAt(sh, s, x + left, y + top, 0, 0, false, 0, 0);
            state_.flipX = false;
        };
        if (sp) {
            drawPart(*sheet, *sp, (float)-sp->pivotX, (float)-sp->pivotY);
        } else {
            for (const CompoPart& part : compo->parts) {
                const SpriteSheet* partSheet = nullptr;
                const Sprite* ps = sprites_.Find(part.sprite, &partSheet);
                if (ps && part.exact) {
                    // a part of a Hatchery bird: its place turns with the object, the part itself turns by its own angle and is scaled about its pivot
                    float c = std::cos(saved.angle), s = std::sin(saved.angle);
                    float ox = o.flipX ? -part.fx : part.fx;
                    float px = x + c * ox - s * part.fy, py = y + s * ox + c * part.fy;
                    RenderState partState = state_;
                    partState.angle = saved.angle + (o.flipX ? -part.angle : part.angle);
                    partState.pivotX = (float)ps->pivotX; partState.pivotY = (float)ps->pivotY;
                    partState.flipX = o.flipX;
                    partState.sx = saved.sx * part.scale; partState.sy = saved.sy * part.scale;
                    partState.tx = (px + saved.tx) / part.scale - px; partState.ty = (py + saved.ty) / part.scale - py;
                    RenderState keep = state_;
                    state_ = partState;
                    DrawSpriteAt(*partSheet, *ps, px - (float)ps->pivotX, py - (float)ps->pivotY, 0, 0, false, 0, 0);   // the sprite's pivot lands on (px, py)
                    state_ = keep;
                    continue;
                }
                if (ps) drawPart(*partSheet, *ps, (float)(part.x - ps->pivotX), (float)(part.y - ps->pivotY));
            }
        }
    }
    state_ = saved;
    tileTexture_ = nullptr;
}

// The four groups of drawGameNative (see DrawGame), each in the order it is drawn: Rio sorts a group by z order (equal z keeps the creation
// order); the earlier games keep the creation order unless setObjectZOrder was used (those objects are then sorted among the others, stable).
std::vector<PhysObject*> GamePlatform::DrawGroup(int which) {
    bool rio = scripts_ && scripts_->IsRio();
    std::vector<PhysObject*> group;
    for (auto& up : Physics().Objects()) {
        PhysObject& o = *up;
        bool isStatic = o.pbody && o.pbody->IsStatic(), bird = o.isBird || o.drawWithBirds;
        bool in = which == 0 ? isStatic && o.zOrder < 5
                : which == 1 ? !o.isBird && !isStatic && !o.drawWithBirds && o.zOrder < 5
                : which == 2 ? bird && o.zOrder < 5
                : o.zOrder >= 5;
        if (in) group.push_back(&o);
    }
    bool anySet = false;
    for (PhysObject* o : group) anySet = anySet || o->zSet;
    if (rio || anySet)
        std::stable_sort(group.begin(), group.end(), [rio](PhysObject* a, PhysObject* b) {
            return (rio || a->zSet ? a->zOrder : 0) < (rio || b->zSet ? b->zOrder : 0); });
    return group;
}

// v2.2.3: back to front, as drawGameNative draws them (the trajectory dots are not objects and are not listed)
std::vector<std::string> GamePlatform::GameDrawOrder() {
    std::vector<std::string> names;
    for (int which = 0; which < 4; which++)
        for (PhysObject* o : DrawGroup(which)) names.push_back(o->name);
    return names;
}

// drawGameNative: static blocks, then the other blocks, the trajectory dots, the birds, and
// finally everything with a z of 5 or more.
void GamePlatform::DrawGame() {
    gameDrawn_ = true;
    if (!backend_) return;
    state_ = RenderState();
    state_.tx = -camX_; state_.ty = -camY_;
    state_.sx = state_.sy = worldScale_;
    bool rio = scripts_ && scripts_->IsRio();
    auto drawGroup = [&](int which) {
        for (PhysObject* o : DrawGroup(which)) {
            drawingNative_ = true;                          // v2.2.1: the object's quads are sorted against render state z sprites by its z_order
            nativeKey_ = (rio || o->zSet) ? o->zOrder : 0;
            DrawPhysObject(*o);
            drawingNative_ = false;
        }
        state_.alpha = 1;                          // setObjectAlpha belongs to one object, not to what is drawn next
    };
    drawGroup(0);
    drawGroup(1);
    // each of the six lists: its dots go through TRAIL_WHITE_1, _2, _3 and start again, then its puffs (BIRD_SPECIAL)
    state_.angle = 0; state_.pivotX = state_.pivotY = 0;
    for (int list = 0; list < 6; list++) {
        const std::string* custom = trajCustom_[list / 3];
        for (size_t i = 0; i < trajDots_[list].size(); i++) {
            const SpriteSheet* sheet = nullptr;
            const Sprite* sp = custom[i % 3].empty() ? nullptr : sprites_.Find(custom[i % 3], &sheet);
            if (!sp) sp = sprites_.Find("TRAIL_WHITE_" + std::to_string(i % 3 + 1), &sheet);
            if (sp) DrawSpriteAt(*sheet, *sp, trajDots_[list][i].x, trajDots_[list][i].y, 4, 3, false, 0, 0);
        }
        for (const TrajectoryPoint& p : trajPuffs_[list]) {
            const SpriteSheet* sheet = nullptr;
            const Sprite* sp = custom[3].empty() ? nullptr : sprites_.Find(custom[3], &sheet);
            if (!sp) sp = sprites_.Find("BIRD_SPECIAL", &sheet);
            if (sp) DrawSpriteAt(*sheet, *sp, p.x, p.y, 4, 3, false, 0, 0);
        }
    }
    drawGroup(2);
    drawGroup(3);
    if (IsSeasonsScripts()) { if (!levelParticlesByScript_) DrawParticles(); }
    else DrawParticlesOf(true, 0);
    state_ = RenderState();
}

// ---- particles (Particles class at 0x41c260; addParticles 0x41c670, update 0x41cec0, draw 0x41d0c0)

// The definition is read from particleTable.particles[type] the first time the type is used.
const GamePlatform::ParticleDef* GamePlatform::ParticleDefFor(const std::string& type) {
    auto found = particleDefs_.find(type);
    if (found != particleDefs_.end()) return &found->second;
    if (!scripts_) return nullptr;
    lua_State* L = scripts_->State();
    int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_getfield(L, -1, "particleTable");
    if (!lua_istable(L, -1)) { lua_settop(L, top); return nullptr; }
    lua_getfield(L, -1, "particles");
    if (!lua_istable(L, -1)) { lua_settop(L, top); return nullptr; }
    lua_getfield(L, -1, type.c_str());
    if (!lua_istable(L, -1)) { lua_settop(L, top); return nullptr; }
    int t = lua_gettop(L);
    ParticleDef d;
    GetNumber(L, t, "gravityX", d.gravityX);
    GetNumber(L, t, "gravityY", d.gravityY);
    // 1.x has one speed range for both axes, Seasons has one for each
    d.seasons = GetNumber(L, t, "minVelX", d.minVelX);
    GetNumber(L, t, "maxVelX", d.maxVelX);
    GetNumber(L, t, "minVelY", d.minVelY);
    GetNumber(L, t, "maxVelY", d.maxVelY);
    if (!d.seasons) {
        GetNumber(L, t, "minVel", d.minVelX);
        GetNumber(L, t, "maxVel", d.maxVelX);
        d.minVelY = d.minVelX; d.maxVelY = d.maxVelX;
    }
    lua_getfield(L, t, "randomizeBirthPosition");
    if (lua_isboolean(L, -1)) d.randomizeBirth = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    if (!GetNumber(L, t, "angleMinEmitter", d.angleMinEmitter) || !GetNumber(L, t, "angleMaxEmitter", d.angleMaxEmitter))
        d.angleMinEmitter = d.angleMaxEmitter = 0;
    // 0x42ad10 (2.0.0) reads minAngleEmitter / maxAngleEmitter; without them the angle range is 0..360
    if (!d.seasons && GetNumber(L, t, "minAngleEmitter", d.angleMinEmitter) && GetNumber(L, t, "maxAngleEmitter", d.angleMaxEmitter)) d.v2Emitter = true;
    lua_getfield(L, t, "roundExplosions");
    if (lua_isboolean(L, -1)) d.roundExplosions = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    GetNumber(L, t, "minAngleVel", d.minAngleVel);
    GetNumber(L, t, "maxAngleVel", d.maxAngleVel);
    GetNumber(L, t, "minScaleBegin", d.minScaleBegin);
    GetNumber(L, t, "maxScaleBegin", d.maxScaleBegin);
    GetNumber(L, t, "minScaleEnd", d.minScaleEnd);
    GetNumber(L, t, "maxScaleEnd", d.maxScaleEnd);
    GetNumber(L, t, "lifeTime", d.lifeTime);
    lua_getfield(L, t, "animation");
    if (lua_type(L, -1) == LUA_TSTRING) d.animated = std::string(lua_tostring(L, -1)) == "lifeTime";
    lua_pop(L, 1);
    lua_getfield(L, t, "sprites");
    if (lua_istable(L, -1)) {
        int n = (int)lua_objlen(L, -1);
        for (int i = 1; i <= n; i++) {
            lua_rawgeti(L, -1, i);
            if (lua_type(L, -1) == LUA_TSTRING) d.sprites.push_back(lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    lua_settop(L, top);
    return &(particleDefs_[type] = d);
}

// MSVC's rand() gives 0..32767
static float Rand01() { return (float)(std::rand() & 0x7fff) * 3.0517578e-05f; }

void GamePlatform::AddLevelParticles(const std::string& type, float amount, float x, float y, float w, float h, float angle, bool ignoreLimits) {
    size_t before = particles_.size();
    AddParticles(type, amount, x, y, w, h, angle, ignoreLimits, false);
    // 0x42a850 draws a random layer (1 behind the foreground, 2 in front) for each emit
    for (size_t i = before; i < particles_.size(); i++) { particles_[i].level = true; particles_[i].layer = (std::rand() & 1) + 1; }
}

void GamePlatform::AddParticles(const std::string& type, float amount, float x, float y, float w, float h, float angle, bool ignoreLimits, bool second) {
    if (!ignoreLimits) {
        int count = 0;
        for (const Particle& q : particles_) if (q.second == second && !q.level) count++;
        if ((float)softLimit_ < amount + (float)count) amount *= softFactor_;
        if ((float)hardLimit_ < (float)count + amount) amount = (float)(hardLimit_ - count);
        if (!(amount > 0)) return;
    }
    const ParticleDef* d = ParticleDefFor(type);
    static const bool partDbg = std::getenv("AB_PARTDBG") != nullptr;
    if (partDbg) std::fprintf(stderr, "THEME addParticles %s amount=%g def=%d sprites=%d\n", type.c_str(), amount, d ? 1 : 0, d ? (int)d->sprites.size() : -1);
    if (!d || d->sprites.empty()) return;
    float s = std::sin(angle), c = std::cos(angle);
    for (int i = 0; (float)i < amount; i++) {
        Particle p;
        p.def = d;
        p.second = second;
        if (d->randomizeBirth) {
            float a = (Rand01() - 0.5f) * w;
            float b = (Rand01() - 0.5f) * h;
            p.x = (x + a * c) - b * s;
            p.y = c * b + s * a + y;
        } else {
            p.x = x; p.y = y;
        }
        if (d->roundExplosions) {
            float speed = (d->maxVelX - d->minVelX) * Rand01() + d->minVelX;
            float a = Rand01() * 6.2831855f;
            p.vx = std::cos(a) * speed;
            p.vy = std::sin(a) * speed;
        } else if (d->v2Emitter) {
            float speed = (d->maxVelX - d->minVelX) * Rand01() + d->minVelX;
            float a = ((d->angleMaxEmitter - d->angleMinEmitter) * Rand01() + d->angleMinEmitter) * 0.017453292f;
            p.vx = std::cos(a) * speed;
            p.vy = std::sin(a) * speed;
        } else {
            p.vx = (d->maxVelX - d->minVelX) * Rand01() + d->minVelX;
            p.vy = (d->maxVelY - d->minVelY) * Rand01() + d->minVelY;
        }
        if (d->seasons || d->v2Emitter) p.angle = ((d->angleMaxEmitter - d->angleMinEmitter) * Rand01() + d->angleMinEmitter) * 0.017453292f;
        else p.angle = Rand01() * 3.1415927f + Rand01() * 3.1415927f;   // 1.x: the exe uses one random number twice
        p.angleVel = (d->maxAngleVel - d->minAngleVel) * Rand01() + d->minAngleVel;
        p.scaleBegin = (d->maxScaleBegin - d->minScaleBegin) * Rand01() + d->minScaleBegin;
        p.scaleEnd = (d->maxScaleEnd - d->minScaleEnd) * Rand01() + d->minScaleEnd;
        p.scale = p.scaleBegin;
        p.age = 0;
        p.lifeTime = d->lifeTime;
        p.frame = 1;
        p.sprite = d->animated ? d->sprites[0] : d->sprites[(size_t)(Rand01() * (float)d->sprites.size())];
        particles_.push_back(p);
    }
}

// Below the ground (y > 0) a particle is removed only in the level; menu particles (enableMenuParticles) stay.
void GamePlatform::UpdateParticles(float dt, bool removeBelowGround, int which) {
    for (size_t i = 0; i < particles_.size();) {
        Particle& p = particles_[i];
        if (which == 0 && p.second) { i++; continue; }
        if (which == 1 && !p.second) { i++; continue; }
        p.age += dt;
        if (p.lifeTime < p.age || (removeBelowGround && 0.0f < p.y)) { particles_.erase(particles_.begin() + i); continue; }
        const ParticleDef& d = *p.def;
        p.vx += d.gravityX * dt;
        p.vy += d.gravityY * dt;
        p.x += dt * p.vx;
        p.y += p.vy * dt;
        p.angle += p.angleVel * dt;
        float t = p.age / p.lifeTime;
        p.scale = (p.scaleEnd - p.scaleBegin) * t + p.scaleBegin;
        if (d.animated) {
            int n = (int)d.sprites.size();
            int frame = (int)std::ceil(t * (float)n);
            if (frame < 1) frame = 1;
            if (frame > n) frame = n;
            if (frame != p.frame) { p.sprite = d.sprites[frame - 1]; p.frame = frame; }
        }
        i++;
    }
}

// Each particle: a sprite at (x, y) / scale, scaled by scale * worldScale, rotated about the
// sprite's pivot, positioned like the blocks (anchors 4 and 3).
// world = false is the menu call (drawParticlesNative): the particles sit in screen coordinates, no camera
// 0x42a9f0: layer 1 is the first list of particles (behind the foreground), layer 2 the second
void GamePlatform::DrawLevelParticles(int layer) {
    levelParticlesByScript_ = true;
    if (IsSeasonsScripts()) DrawParticlesOf(true, layer == 2);
    else DrawParticlesOf(true, -1, layer);
}

void GamePlatform::DrawParticles(bool world) {
    if (!world) menuParticlesByScript_ = true;
    DrawParticlesOf(world, -1);
}

void GamePlatform::DrawParticlesOf(bool world, int second, int levelLayer) {
    if (!backend_) return;
    for (const Particle& p : particles_) {
        if (levelLayer > 0 ? (!p.level || p.layer != levelLayer) : p.level) continue;
        if (second >= 0 && p.second != (second == 1)) continue;
        const SpriteSheet* sheet = nullptr;
        const Sprite* sp = sprites_.Find(p.sprite, &sheet);
        if (!sp) continue;
        state_ = RenderState();
        float cx = world ? camX_ : 0, cy = world ? camY_ : 0, ws = world ? worldScale_ : 1;
        state_.tx = -cx / p.scale; state_.ty = -cy / p.scale;
        state_.sx = state_.sy = p.scale * ws;
        state_.angle = p.angle;
        state_.pivotX = (float)sp->pivotX;
        state_.pivotY = (float)sp->pivotY;
        if (levelLayer > 0) {
            // 0x42a9f0 (2.0.0): the level particles keep their position, so the camera moves them by
            // (position * scale - camera): a smaller particle trails the camera, and layer 2 is twice as near
            state_.sx = state_.sy = p.scale * ws * (float)levelLayer;
            DrawSpriteAt(*sheet, *sp, p.x, p.y, 4, 3, false, 0, 0);
        } else {
            DrawSpriteAt(*sheet, *sp, p.x / p.scale, p.y / p.scale, 4, 3, false, 0, 0);
        }
    }
    state_ = RenderState();
}


// Rio: the exe swaps the bird sprites that the script does not (def.sprites.blink / yell).
// A bird waiting on the ground blinks now and then; the bird the player has grabbed yells until it is launched.
void GamePlatform::UpdateRioBirdFaces(float dt) {
    lua_State* L = scripts_->State();
    int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    int env = top + 1;
    lua_getfield(L, env, "birds");
    lua_getfield(L, env, "getObjectDefinition");
    if (!lua_istable(L, top + 2) || !lua_isfunction(L, top + 3)) { lua_settop(L, top); return; }
    std::string grabbed;
    lua_getfield(L, env, "selectedBird");
    if (lua_istable(L, -1)) { lua_getfield(L, -1, "name"); if (lua_isstring(L, -1)) grabbed = lua_tostring(L, -1); lua_pop(L, 1); }
    lua_pop(L, 1);
    lua_getfield(L, env, "flyingBird");
    if (lua_istable(L, -1)) grabbed.clear();
    lua_pop(L, 1);
    std::vector<std::string> names;
    lua_pushnil(L);
    while (lua_next(L, top + 2)) {
        if (lua_type(L, -2) == LUA_TSTRING) names.push_back(lua_tostring(L, -2));
        lua_pop(L, 1);
    }
    for (const std::string& name : names) {
        PhysObject* o = Physics().Find(name);
        if (!o) continue;
        lua_pushvalue(L, top + 3);
        lua_pushstring(L, name.c_str());
        if (lua_pcall(L, 1, 1, 0) != 0) { lua_pop(L, 1); continue; }
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "sprites");
            if (lua_istable(L, -1)) {
                std::string sp[3];
                const char* keys[3] = {"default", "blink", "yell"};
                for (int i = 0; i < 3; i++) {
                    lua_getfield(L, -1, keys[i]);
                    if (lua_isstring(L, -1)) sp[i] = lua_tostring(L, -1);
                    lua_pop(L, 1);
                }
                std::string flyingSprite;
                lua_getfield(L, -1, "flying");
                if (lua_isstring(L, -1)) flyingSprite = lua_tostring(L, -1);
                lua_pop(L, 1);
                if (sp[1] == flyingSprite) sp[1].clear();     // Blu & Jewel: the blink sprite is her flying pose, so she does not blink
                if (sp[2] == flyingSprite) sp[2].clear();
                // The gamelogic script runs the face timers itself (animTimer / animOn: a face for 0.1-0.2 s, a yell one time in four,
                // with the bird_misc sound) but it looks the sprites up as spriteYell / spriteBlink of the definition, a name
                // that Rio's definitions do not have; give it those names from sprites.yell / sprites.blink.
                const char* extra[2] = {"spriteYell", "spriteBlink"};
                for (int i = 0; i < 2; i++) {
                    const std::string& face = sp[i == 0 ? 2 : 1];
                    lua_getfield(L, -2, extra[i]);
                    bool missing = lua_isnil(L, -1);
                    lua_pop(L, 1);
                    if (missing && !face.empty()) { lua_pushstring(L, face.c_str()); lua_setfield(L, -3, extra[i]); }
                }
                // the script never puts the default sprite back: that is when its animOn flag goes false again
                bool showing = false;
                lua_getfield(L, top + 2, name.c_str());
                if (lua_istable(L, -1)) { lua_getfield(L, -1, "animOn"); showing = lua_toboolean(L, -1) != 0; lua_pop(L, 1); }
                lua_pop(L, 1);
                if (!sp[0].empty() && (o->sprite == sp[0] || o->sprite == sp[1] || o->sprite == sp[2])) {
                    // the bird being dragged in the slingshot shows its yell sprite
                    std::string want = o->sprite;
                    if (name == grabbed && !sp[2].empty()) want = sp[2];
                    else if (!showing) want = sp[0];
                    if (o->sprite != want) { if (std::getenv("AB_FACEDBG")) std::fprintf(stderr, "face %s: %s -> %s\n", name.c_str(), o->sprite.c_str(), want.c_str()); o->sprite = want; }
                }
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    lua_settop(L, top);
}

}  // namespace ab

namespace ab {
// Rio, the world update at 0x40a8xx / 0x40b8xx: the wobble that a hit starts in an object's draw scale, and the turning
// of the objects that face the way they move
void GamePlatform::UpdateRioSquash(float dt) {
    static const bool dbg = std::getenv("AB_WOBDBG") != nullptr;
    for (auto& o : Physics().Objects()) {
        if (dbg && o->wobActive && o->wobT == 0) std::fprintf(stderr, "DBG wobble %s amp0=%.3f mult=%.2f\n", o->name.c_str(), o->wobAmp0, o->bounceAmp);
        if (o->wobActive) {
            o->wobT += dt;
            float cur = (o->wobAmp0 - o->wobT * o->wobAmp0) * o->bounceAmp;      // dies out linearly within a second
            if (cur > 4.0f) cur = 4.0f;
            o->wobCur = cur;
            if (cur > 0.01f) {
                float phase = o->wobT * (cur * 100.0f + o->bounceFreq * 5.0f);
                o->wobDX = std::sin(phase) * cur;
                o->wobDY = std::sin(phase + 3.1415927f) * cur;
            } else {
                o->wobActive = false; o->wobCur = 0; o->wobAmp0 = 0; o->wobT = 0;
            }
        }
        if (o->enableFlipping) {        // Luiz and the marmosets face the way they move (their sprites face left)
            float vx = o->pbody->VelX();
            if (std::fabs(vx) > 3.0f) o->flipX = o->flipBase != (vx > 0);
        }
    }
}

// Rio, 0x40b1xx: a marmoset (setObjectParameter 14) that is not hanging on a joint and lies still on its side stands up again.
// The exe starts the "getting_up" animation, then turns the body upright at 2 rad/s the short way round.
void GamePlatform::UpdateRioGettingUp(float dt) {
    const float kPi = 3.1415927f, kTwoPi = 6.2831855f;
    std::vector<std::string> started;
    for (auto& o : Physics().Objects()) {
        if (!o->canGetUp || !o->pbody) continue;
        PhysBody* body = o->pbody.get();
        float angle = std::fmod(body->Angle(), kTwoPi);
        if (angle < 0) angle += kTwoPi;
        float w = std::fabs(body->AngularVelocity());
        if (!o->gettingUp) {
            if (o->getUpTimer > 0 || body->IsStatic()) continue;
            if (body->VelX() * body->VelX() + body->VelY() * body->VelY() >= 4.0f || w >= 0.1f) continue;
            if (Physics().HasJoints(o->name) || o->disableRotation) continue;
            if (angle > 0.7853982f && angle < 5.4977875f) {
                o->getUpTimer = 3.0f; o->gettingUp = true;
                started.push_back(o->name);
                if (std::getenv("AB_WOBDBG")) std::fprintf(stderr, "DBG getup start %s angle=%.2f\n", o->name.c_str(), angle);
            }
            continue;
        }
        o->getUpTimer -= dt;
        bool done = false;
        if (o->getUpTimer <= 0) {
            done = w > 0.1f;
        } else if (angle < 0.1f || angle > 6.183f) {
            done = true;
        } else if (w < 2.0f) {
            body->SetAwake(true);
            body->SetAngularVelocity(angle > kPi ? 2.0f : -2.0f);
        }
        if (done) {
            if (std::getenv("AB_WOBDBG")) std::fprintf(stderr, "DBG getup done %s angle=%.2f\n", o->name.c_str(), angle);
            o->getUpTimer = 0; o->gettingUp = false; body->SetAngularVelocity(0);
        }
    }
    if (started.empty() || !scripts_) return;
    lua_State* L = scripts_->State();
    int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    for (const std::string& name : started) {
        lua_getfield(L, top + 1, "setAnimationByName");
        if (!lua_isfunction(L, -1)) { lua_pop(L, 1); continue; }
        lua_pushstring(L, name.c_str());
        lua_pushstring(L, "getting_up");
        if (lua_pcall(L, 2, 0, 0) != 0) {
            std::fprintf(stderr, "setAnimationByName: %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    lua_settop(L, top);
}
}  // namespace ab

namespace ab {
// What the command prompt window accepts (main.cpp reads the lines on a thread and hands them over here once per frame).
void GamePlatform::ConsoleCommand(const std::string& raw) {
    std::string line = raw;
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
    size_t s = line.find_first_not_of(' ');
    if (s == std::string::npos) return;
    line = line.substr(s);
    std::string cmd = line.substr(0, line.find(' '));
    std::string arg = line.size() > cmd.size() ? line.substr(cmd.size() + 1) : "";
    if (cmd == "help" || cmd == "?") {
        std::printf(
            "Console commands (anything else is run as Lua code among the game's scripts):\n"
            "  help                    this list\n"
            "  box2d [21|22]           show / choose the physics library; it changes when the next level loads (restart the level)\n"
            "  zorder [name [z]]       list the objects back to front / show or set one object's z_order; zfront <name>, zback <name>\n"
            "  polyradius [r]          show / set the collision skin of boxes and polygons made from now on (Box2D 2.2)\n"
            "  fps [n]                 limit the frame rate (0 = unlimited, the default); vsync [on|off] synchronises to the display\n"
            "  gamespeed [x]           gameSpeedFactor: the time step of everything (default 1)\n"
            "  physicsspeed [x]        physicsSpeedFactor: the time step of the physics only (default 1)\n"
            "  set <name> <value>      set any Lua global (set gameSpeedFactor 0.5); get <name> shows it\n"
            "  audiospeed [x [track]]  audio playback speed (pitch follows); audiopause / audioresume [track]; mastervolume [v]\n"
            "  lua <code>              run Lua code, for example: lua print(levelName)\n");
    } else if (cmd == "box2d") {
        if (arg == "21" || arg == "22") {
            requestedBox2D_ = std::atoi(arg.c_str());
            std::printf("console: Box2D %s is used from the next level load (restart the level to apply it)\n", arg == "21" ? "2.1.1" : "2.2.0");
        } else {
            std::printf("console: physics is Box2D %s%s\n", physics_ ? (physics_->Version() == Box2DVersion::V22 ? "2.2.0" : "2.1.1") : "not started",
                        requestedBox2D_ ? (requestedBox2D_ == 21 ? " (2.1.1 requested)" : " (2.2.0 requested)") : "");
        }
    } else if (cmd == "fps") {
        if (!arg.empty()) fpsLimit = std::max(0, std::atoi(arg.c_str()));
        if (fpsLimit) std::printf("console: frame rate limited to %d fps\n", fpsLimit); else std::printf("console: frame rate not limited\n");
    } else if (cmd == "vsync") {
        if (arg == "on" || arg == "1") { vsync = true; vsyncChanged = true; }
        else if (arg == "off" || arg == "0") { vsync = false; vsyncChanged = true; }
        std::printf("console: vsync %s\n", vsync ? "on" : "off");
    } else if (cmd == "gamespeed" || cmd == "physicsspeed" || cmd == "set" || cmd == "get") {
        // gamespeed / physicsspeed are the Lua globals gameSpeedFactor / physicsSpeedFactor; set / get reach any numeric Lua global
        std::string name = cmd == "gamespeed" ? "gameSpeedFactor" : cmd == "physicsspeed" ? "physicsSpeedFactor" : "";
        std::string value = arg;
        if (name.empty()) {
            size_t sp = arg.find(' ');
            name = arg.substr(0, sp);
            value = sp == std::string::npos ? "" : arg.substr(sp + 1);
        }
        if (scripts_ && !name.empty()) {
            if (!value.empty()) scripts_->RunString((name + " = " + value).c_str());
            scripts_->RunString(("_G.print('console: " + name + " = ' .. _G.tostring(" + name + "))").c_str());
        } else std::printf("console: usage: %s%s\n", cmd.c_str(), cmd == "set" ? " <name> <value>" : cmd == "get" ? " <name>" : " [factor]");
    } else if (cmd == "audiospeed" || cmd == "audiopause" || cmd == "audioresume" || cmd == "mastervolume") {
        // audiospeed [x [track]] (pitch follows the speed), audiopause [track], audioresume [track], mastervolume [v]
        float a = 0, b = -1; int n = std::sscanf(arg.c_str(), "%f %f", &a, &b);
        if (cmd == "audiospeed") { if (n >= 1) audio_.SetSpeed(a, n >= 2 ? (int)b : -1); std::printf("console: audio speed %g\n", audio_.Speed(n >= 2 ? (int)b : -1)); }
        else if (cmd == "mastervolume") { if (n >= 1) audio_.SetMasterVolume(a); std::printf("console: master volume %g\n", audio_.MasterVolume()); }
        else { int t = n >= 1 ? (int)a : -1; audio_.Pause(cmd == "audiopause", t); std::printf("console: audio %s\n", cmd == "audiopause" ? "paused" : "resumed"); }
    } else if (cmd == "zorder" || cmd == "zfront" || cmd == "zback") {
        // v2.2.3: zorder (list, back to front) | zorder <name> (show) | zorder <name> <z> (set); zfront <name> / zback <name>
        std::istringstream ss(arg);
        std::string name, zs;
        ss >> name >> zs;
        if (cmd == "zorder" && name.empty()) {
            std::vector<std::string> order = GameDrawOrder();
            int i = 0;
            for (const std::string& n : order) std::printf("console: %3d  z_order %-6g %s\n", ++i, Physics().ObjectZOrder(n), n.c_str());
            std::printf("console: %d objects, back to front (drawGameNative's order)\n", i);
        } else if (name.empty()) {
            std::printf("console: %s <name>\n", cmd.c_str());
        } else if (Physics().ObjectZOrder(name) <= -1e8f) {
            std::printf("console: no object called %s\n", name.c_str());
        } else {
            if (cmd == "zfront" || cmd == "zback") Physics().MoveObjectZ(name, cmd == "zfront");
            else if (!zs.empty()) Physics().SetObjectZOrder(name, (float)std::atof(zs.c_str()));
            SyncObjectZ(name);
            std::vector<std::string> order = GameDrawOrder();
            size_t at = 0;
            while (at < order.size() && order[at] != name) at++;
            std::printf("console: %s z_order %g, drawn %zu of %zu (1 = at the back)\n", name.c_str(), Physics().ObjectZOrder(name), at + 1, order.size());
        }
    } else if (cmd == "polyradius") {
        if (!arg.empty()) Physics().SetPolygonRadius((float)std::atof(arg.c_str()));
        if (Physics().Version() == Box2DVersion::V22) std::printf("console: polygon radius %g (applies to shapes made afterwards)\n", Physics().PolygonRadius());
        else std::printf("console: Box2D 2.1 has a fixed polygon radius of 0.1\n");
    } else if (scripts_) {
        scripts_->RunString((cmd == "lua" ? arg : line).c_str());
    }
    std::fflush(stdout);
}
}  // namespace ab

namespace ab {
// playVideo("videos/seasons_teaser_v01.m4v"): the Angry Birds Free / Lite / Seasons Free scripts play their teaser videos from the iOS
// "videos" folder. v2.2 plays them inside the engine: the file is looked for in the game's data folder (data/videos/<name>), next to it and
// in the exe's folder. The videos are not part of the PC game; they are in the iOS .ipa (a zip file: Payload/<app>/data_iphone/videos).
void GamePlatform::PlayVideo(const std::string& path) {
    auto exists = [](const std::string& p) { if (std::FILE* f = std::fopen(p.c_str(), "rb")) { std::fclose(f); return true; } return false; };
    std::string name = path.substr(path.find_last_of("/\\") == std::string::npos ? 0 : path.find_last_of("/\\") + 1);
    std::string stem = name.substr(0, name.find_last_of('.') == std::string::npos ? name.size() : name.find_last_of('.'));
    std::vector<std::string> bases = {"", dataRoot_ + "/", dataRoot_ + "/videos/", dataRoot_ + "/../", dataRoot_ + "/../videos/", "./", "./videos/"};
    std::vector<std::string> names = {path, name};
    for (const char* ext : {".m4v", ".mp4", ".mov"}) names.push_back(stem + ext);
    for (const std::string& b : bases)
        for (const std::string& n : names) {
            std::string full = b + n;
            if (!exists(full)) continue;
            StopVideo();
            std::unique_ptr<VideoPlayer> player(new VideoPlayer);
            std::string error;
            if (!player->Open(full, &error)) { std::printf("playVideo: %s: %s\n", full.c_str(), error.c_str()); std::fflush(stdout); return; }
            video_ = std::move(player);
            std::printf("playVideo %s (%dx%d, %.1f s)\n", full.c_str(), video_->Width(), video_->Height(), video_->Duration());
            std::fflush(stdout);
            std::vector<int16_t> pcm;
            int rate = 0, channels = 0;
            if (audio_.Created() && DecodeVideoAudio(full, pcm, rate, channels, &error)) {
                audio_.AddPcmClip("__video", rate, channels, pcm);
                audio_.Start();
                audio_.Play("__video", 1.0f, false, kAudioTracks - 1);
            } else if (!error.empty()) { std::printf("playVideo: %s\n", error.c_str()); std::fflush(stdout); }
            if (videoPump) videoPump();            // the call returns when the video is over (a click or a key ends it)
            return;
        }
    std::printf("playVideo: %s not found (put it in %s/videos; the iOS videos are inside the .ipa, a zip file)\n", path.c_str(), dataRoot_.c_str());
}

void GamePlatform::StopVideo() {
    if (!video_) return;
    audio_.Stop("__video");
    video_.reset();
}

void GamePlatform::VideoFrame(double dt) {
    if (!video_) return;
    bool changed = video_->Advance(dt);
    if (backend_) {
        backend_->BeginFrame(screenWidth_, screenHeight_, 0, 0, 0);
        const std::vector<uint8_t>& px = video_->Rgba();
        int vw = video_->Width(), vh = video_->Height();
        if (!px.empty() && vw > 0 && vh > 0) {
            if (videoTexture_ < 0 || vw != videoTextureW_ || vh != videoTextureH_ || changed) {
                Image im;
                im.width = vw; im.height = vh; im.rgba = px;
                if (videoTexture_ < 0 || vw != videoTextureW_ || vh != videoTextureH_) { videoTexture_ = backend_->CreateTexture(im); videoTextureW_ = vw; videoTextureH_ = vh; }
                else backend_->UpdateTexture(videoTexture_, im);
            }
            float scale = std::min(screenWidth_ / (float)vw, screenHeight_ / (float)vh);
            float w = vw * scale, h = vh * scale, x0 = (screenWidth_ - w) * 0.5f, y0 = (screenHeight_ - h) * 0.5f;
            const float xy[8] = {x0, y0, x0 + w, y0, x0 + w, y0 + h, x0, y0 + h}, uv[8] = {0, 0, 1, 0, 1, 1, 0, 1};
            backend_->DrawQuad(videoTexture_, xy, uv, 1, 1, 1, 1);
        }
        backend_->EndFrame();
    }
    if (video_->Finished()) StopVideo();
}

// ---- Hatchery egg painter (the iPad version's native canvas) ---------------------------------------------------
// The scripts paint with brush sprites (H_PAINT_BRUSH_<COLOR>) at positions that are fractions of the canvas, and show the canvas through the
// alpha of an egg-shaped sprite: the mask is centred on the canvas, one mask pixel being `scale` canvas pixels (the scale it is drawn at).
const Image* GamePlatform::SheetImage(const SpriteSheet& sheet) {
    std::string path = sheet.directory + "/" + sheet.image;
    auto it = brushImages_.find(path);
    if (it != brushImages_.end()) return it->second.width > 0 ? &it->second : nullptr;
    Image& img = brushImages_[path];
    std::string error;
    if (!LoadPngFile(path, img, &error)) { Print("texture: " + error); img = Image(); return nullptr; }
    return &img;
}

void GamePlatform::CanvasInit(int w, int h) {
    if (w < 1 || h < 1 || w > 2048 || h > 2048) return;
    if (canvas_.w == w && canvas_.h == h) return;
    canvas_.w = w; canvas_.h = h;
    canvas_.rgba.assign((size_t)w * h * 4, 255);
    canvas_.owner.assign((size_t)w * h, 0);
    canvas_.visible.assign((size_t)w * h, 0);
    canvas_.revision++;
}

void GamePlatform::CanvasClear(float r, float g, float b, float a) {
    auto channel = [](float v) { return (uint8_t)std::floor(std::min(255.0f, std::max(0.0f, v * 255.0f)) + 0.5f); };
    for (size_t i = 0; i + 3 < canvas_.rgba.size(); i += 4) {
        canvas_.rgba[i] = channel(r); canvas_.rgba[i + 1] = channel(g); canvas_.rgba[i + 2] = channel(b); canvas_.rgba[i + 3] = channel(a);
    }
    std::fill(canvas_.owner.begin(), canvas_.owner.end(), 0);
    canvas_.revision++;
}

void GamePlatform::CanvasPaint(const std::string& brush, float x, float y, float w, float h) {
    if (canvas_.w < 1) return;
    const SpriteSheet* sheet = nullptr;
    const Sprite* sp = sprites_.Find(brush, &sheet);
    if (!sp || sp->w < 1 || sp->h < 1) return;
    const Image* img = SheetImage(*sheet);
    if (!img) return;
    std::string color = brush.compare(0, 14, "H_PAINT_BRUSH_") == 0 ? brush.substr(14) : brush;
    size_t id = 0;
    for (; id < canvas_.brushes.size(); id++) if (canvas_.brushes[id] == color) break;
    if (id == canvas_.brushes.size()) canvas_.brushes.push_back(color);
    // w and h are screen pixels of the canvas area (the scripts scale the sprite by screen size / canvas size)
    float destW = w * canvas_.w / (float)screenWidth_, destH = h * canvas_.h / (float)screenHeight_;
    if (destW < 1 || destH < 1) return;
    float cx = x * canvas_.w, cy = (1.0f - y) * canvas_.h;
    float sxScale = destW / sp->w, syScale = destH / sp->h;
    float left = cx - sp->pivotX * sxScale, top = cy - sp->pivotY * syScale;
    int x0 = std::max(0, (int)std::floor(left)), x1 = std::min(canvas_.w, (int)std::ceil(left + destW));
    int y0 = std::max(0, (int)std::floor(top)), y1 = std::min(canvas_.h, (int)std::ceil(top + destH));
    for (int py = y0; py < y1; py++)
        for (int px = x0; px < x1; px++) {
            float u = (px + 0.5f - left) / sxScale - 0.5f, v = (py + 0.5f - top) / syScale - 0.5f;
            int ix = (int)std::floor(u), iy = (int)std::floor(v);
            float fx = u - ix, fy = v - iy;
            float col[4] = {0, 0, 0, 0};
            for (int k = 0; k < 4; k++) {
                int tx = std::min(sp->w - 1, std::max(0, ix + (k & 1))), ty = std::min(sp->h - 1, std::max(0, iy + (k >> 1)));
                float wgt = ((k & 1) ? fx : 1 - fx) * ((k >> 1) ? fy : 1 - fy);
                const uint8_t* p = &img->rgba[((size_t)(sp->y + ty) * img->width + sp->x + tx) * 4];
                float pa = p[3] / 255.0f * wgt;
                col[0] += p[0] * pa; col[1] += p[1] * pa; col[2] += p[2] * pa; col[3] += pa;       // straight colours: weigh them by their alpha (the edges of the brushes are dark where they are clear)
            }
            if (col[3] <= 0.001f) continue;
            float a = std::min(1.0f, col[3]);
            uint8_t* d = &canvas_.rgba[((size_t)py * canvas_.w + px) * 4];
            for (int c = 0; c < 3; c++) d[c] = (uint8_t)std::min(255.0f, col[c] + d[c] * (1.0f - a) + 0.5f);
            d[3] = (uint8_t)std::min(255.0f, a * 255.0f + d[3] * (1.0f - a) + 0.5f);
            if (a > 0.4f) canvas_.owner[(size_t)py * canvas_.w + px] = (uint8_t)(id + 1);
        }
    canvas_.revision++;
}

// The canvas seen through the alpha of a mask sprite. outW x outH is the picture's size, `scale` the canvas pixels per mask pixel.
bool GamePlatform::ComposeCanvas(const std::string& mask, int outW, int outH, float scaleX, float scaleY, Image& out, bool markVisible) {
    const SpriteSheet* sheet = nullptr;
    const Sprite* sp = sprites_.Find(mask, &sheet);
    if (!sp || canvas_.w < 1 || outW < 1 || outH < 1) return false;
    const Image* img = SheetImage(*sheet);
    if (!img) return false;
    auto maskAlpha = [&](float mx, float my) {
        int ix = std::min(sp->w - 1, std::max(0, (int)std::floor(mx))), iy = std::min(sp->h - 1, std::max(0, (int)std::floor(my)));
        return img->rgba[((size_t)(sp->y + iy) * img->width + sp->x + ix) * 4 + 3];
    };
    float offX = (canvas_.w - sp->w * scaleX) * 0.5f, offY = (canvas_.h - sp->h * scaleY) * 0.5f;
    out.width = outW; out.height = outH;
    out.rgba.assign((size_t)outW * outH * 4, 0);
    for (int v = 0; v < outH; v++)
        for (int u = 0; u < outW; u++) {
            float mx = (u + 0.5f) / outW * sp->w, my = (v + 0.5f) / outH * sp->h;
            uint8_t ma = maskAlpha(mx, my);
            if (ma == 0) continue;
            int cx = std::min(canvas_.w - 1, std::max(0, (int)std::floor(mx * scaleX + offX)));
            int cy = std::min(canvas_.h - 1, std::max(0, (int)std::floor(my * scaleY + offY)));
            const uint8_t* c = &canvas_.rgba[((size_t)cy * canvas_.w + cx) * 4];
            uint8_t* o = &out.rgba[((size_t)v * outW + u) * 4];
            o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = (uint8_t)(ma * c[3] / 255);
        }
    if (markVisible) {
        for (int cy = 0; cy < canvas_.h; cy++)
            for (int cx = 0; cx < canvas_.w; cx++) {
                float mx = (cx + 0.5f - offX) / scaleX, my = (cy + 0.5f - offY) / scaleY;
                canvas_.visible[(size_t)cy * canvas_.w + cx] = (mx >= 0 && my >= 0 && mx < sp->w && my < sp->h && maskAlpha(mx, my) > 128) ? 1 : 0;
            }
    }
    return true;
}

void GamePlatform::CanvasDraw(const std::string& mask) {
    if (!backend_ || canvas_.w < 1) return;
    const SpriteSheet* sheet = nullptr;
    const Sprite* sp = sprites_.Find(mask, &sheet);
    if (!sp || state_.sx <= 0 || state_.sy <= 0) return;
    int outW = std::min(1536, std::max(1, (int)std::ceil(sp->w * state_.sx))), outH = std::min(1536, std::max(1, (int)std::ceil(sp->h * state_.sy)));
    bool changed = canvas_.composedRev != canvas_.revision || canvas_.mask != mask || canvas_.scaleX != state_.sx || canvas_.scaleY != state_.sy ||
                   canvas_.texW != outW || canvas_.texH != outH;
    if (changed) {
        canvas_.mask = mask; canvas_.scaleX = state_.sx; canvas_.scaleY = state_.sy;
        Image picture;
        if (!ComposeCanvas(mask, outW, outH, state_.sx, state_.sy, picture, true)) return;
        if (canvas_.texture >= 0 && canvas_.texW == outW && canvas_.texH == outH) backend_->UpdateTexture(canvas_.texture, picture);
        else canvas_.texture = backend_->CreateTexture(picture);
        canvas_.texW = outW; canvas_.texH = outH;
        canvas_.composedRev = canvas_.revision;
    }
    if (canvas_.texture < 0) return;
    float x = -(float)sp->pivotX, y = -(float)sp->pivotY, w = (float)sp->w, h = (float)sp->h;
    float cx = x + state_.pivotX, cy = y + state_.pivotY;
    const float px[4] = {x, x + w, x + w, x}, py[4] = {y, y, y + h, y + h};
    const float uv[8] = {0, 0, 1, 0, 1, 1, 0, 1};
    float xy[8];
    for (int i = 0; i < 4; i++) Transform(px[i], py[i], cx, cy, xy[i * 2], xy[i * 2 + 1]);
    QueuedQuad q = MakeQuad(canvas_.texture, state_.alpha, 1, 1, 1);
    std::memcpy(q.xy, xy, sizeof xy); std::memcpy(q.uv, uv, sizeof uv);
    EmitQuad(q);
}

// the two brush colours that cover most of the egg (empty when there is hardly any paint, or only one colour)
void GamePlatform::CanvasDominant(std::string& first, std::string& second) {
    if (canvas_.analysed != canvas_.revision) {
        canvas_.analysed = canvas_.revision;
        std::vector<long> count(canvas_.brushes.size() + 1, 0);
        long total = 0;
        bool haveMask = canvas_.composedRev >= 0;
        for (size_t i = 0; i < canvas_.owner.size(); i++) {
            if (haveMask && !canvas_.visible[i]) continue;
            total++;
            count[canvas_.owner[i]]++;
        }
        int a = -1, b = -1;
        for (size_t i = 1; i < count.size(); i++) {
            if (a < 0 || count[i] > count[a]) { b = a; a = (int)i; }
            else if (b < 0 || count[i] > count[b]) b = (int)i;
        }
        canvas_.first.clear(); canvas_.second.clear();
        if (total > 0) {
            if (a >= 1 && count[a] >= total * 0.02) canvas_.first = canvas_.brushes[a - 1];
            if (b >= 1 && count[b] >= total * 0.05 && !canvas_.first.empty()) canvas_.second = canvas_.brushes[b - 1];
        }
    }
    first = canvas_.first; second = canvas_.second;
}

// A sprite from an image made at run time: its own one-sprite sheet whose picture is a texture of the backend
static void AddRuntimeSprite(SpriteLibrary& lib, const std::string& name, int w, int h, int pivotX, int pivotY) {
    SpriteSheet sheet;
    sheet.image = name;
    sheet.directory = "canvas:";
    Sprite sp;
    sp.name = name; sp.x = 0; sp.y = 0; sp.w = w; sp.h = h; sp.pivotX = pivotX; sp.pivotY = pivotY;
    sheet.sprites.push_back(sp);
    lib.Add("canvas:" + name, std::move(sheet));
}

// setupCompoObject: the object is drawn from the given sprites, each placed at (x, y) from the object's position with its own scale and angle, the whole thing at `scale`
void GamePlatform::SetupCompoObject(const std::string& object, const std::vector<CompoItem>& items, float scale) {
    CompoSprite compo;
    compo.name = "compo:" + object;
    for (const CompoItem& item : items) {
        CompoPart part;
        part.sprite = item.sprite;
        part.fx = item.x * scale; part.fy = item.y * scale;
        part.scale = item.scale * scale; part.angle = item.angle;
        part.exact = true;
        compo.parts.push_back(part);
    }
    sprites_.AddCompo(compo);
    Physics().SetSpriteName(object, compo.name);
}

bool GamePlatform::CanvasMakeSprite(const std::string& name, int width, int height, int pivotX, int pivotY, bool save, const std::string& file) {
    if (!backend_ || canvas_.mask.empty()) return false;
    const Sprite* sp = sprites_.Find(canvas_.mask);
    if (!sp) return false;
    Image full;
    if (!ComposeCanvas(canvas_.mask, sp->w, sp->h, canvas_.scaleX, canvas_.scaleY, full, false)) return false;
    // the picture is width x height: the egg fills the height and is centred, so it is as tall as the plain egg sprite once it is scaled down
    width = std::max(1, width); height = std::max(1, height);
    Image picture;
    picture.width = width; picture.height = height;
    picture.rgba.assign((size_t)width * height * 4, 0);
    float k = (float)height / full.height;
    float left = (width - full.width * k) * 0.5f;
    for (int v = 0; v < height; v++)
        for (int u = 0; u < width; u++) {
            float fx = (u + 0.5f - left) / k - 0.5f, fy = (v + 0.5f) / k - 0.5f;
            if (fx < -0.5f || fy < -0.5f || fx > full.width - 0.5f || fy > full.height - 0.5f) continue;
            int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
            float tx = fx - x0, ty = fy - y0;
            float acc[4] = {0, 0, 0, 0};
            for (int j = 0; j < 2; j++)
                for (int i = 0; i < 2; i++) {
                    int xx = std::min(full.width - 1, std::max(0, x0 + i)), yy = std::min(full.height - 1, std::max(0, y0 + j));
                    const uint8_t* c = &full.rgba[((size_t)yy * full.width + xx) * 4];
                    float wgt = (i ? tx : 1 - tx) * (j ? ty : 1 - ty), a = c[3] / 255.0f * wgt;
                    acc[0] += c[0] * a; acc[1] += c[1] * a; acc[2] += c[2] * a; acc[3] += a;
                }
            uint8_t* o = &picture.rgba[((size_t)v * width + u) * 4];
            if (acc[3] > 0.0f) { o[0] = (uint8_t)(acc[0] / acc[3] + 0.5f); o[1] = (uint8_t)(acc[1] / acc[3] + 0.5f); o[2] = (uint8_t)(acc[2] / acc[3] + 0.5f); o[3] = (uint8_t)(std::min(1.0f, acc[3]) * 255.0f + 0.5f); }
        }
    if (save) WritePngFile(ResolveUserFile(file, true).c_str(), picture.width, picture.height, picture.rgba.data());
    TextureInfo info; info.handle = backend_->CreateTexture(picture); info.width = picture.width; info.height = picture.height;
    textures_["canvas:/" + name] = info;
    AddRuntimeSprite(sprites_, name, picture.width, picture.height, pivotX, pivotY);
    return true;
}

bool GamePlatform::CanvasLoadSprite(const std::string& name, const std::string& file, int pivotX, int pivotY) {
    if (!backend_) return false;
    Image picture;
    std::string error;
    if (!LoadPngFile(ResolveUserFile(file, true), picture, &error)) { Print("loadCanvasSprite: " + error); return false; }
    TextureInfo info; info.handle = backend_->CreateTexture(picture); info.width = picture.width; info.height = picture.height;
    textures_["canvas:/" + name] = info;
    AddRuntimeSprite(sprites_, name, picture.width, picture.height, pivotX, pivotY);
    return true;
}

}  // namespace ab

// v2.2.3: the z_order in objects.world[name] follows the object (console zorder / zfront / zback)
namespace ab {
void SyncWorldZ(lua_State* L, const std::string& name, float z);
void GamePlatform::SyncObjectZ(const std::string& name) {
    if (scripts_) SyncWorldZ(scripts_->State(), name, Physics().ObjectZOrder(name));
}
}  // namespace ab
