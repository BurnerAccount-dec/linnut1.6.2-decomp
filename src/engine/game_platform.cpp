#include <chrono>
#include <random>
#include <type_traits>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iterator>
#include "engine/game_platform.h"
#include "game/collision_damage.h"
#include "lua/lua_api.h"
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
        platform_.SyncTuning(velIter, posIter);
        EmitHook(platform_.scripts_->State(), "physicsStep", nullptr, nullptr, 1, dt);
        platform_.Physics().ApplyGravitySensors(dt);
        platform_.Physics().Step(dt, velIter, posIter);
        platform_.Physics().ApplySpeedCaps();
        EmitHook(platform_.scripts_->State(), "physicsStepEnd", nullptr, nullptr, 1, dt);
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


// ---- Angry Birds Star Wars contact helpers (0x42f9d0)
bool EntryString(lua_State* L, int entry, const char* key, std::string& out) {
    lua_getfield(L, entry, key);
    bool ok = lua_type(L, -1) == LUA_TSTRING;
    if (ok) out = lua_tostring(L, -1);
    lua_pop(L, 1);
    return ok;
}

// blockTable.damageFactors[entry.damageFactors][field][material], `def` when any step is missing
float DamageFactorOf(lua_State* L, int entry, const char* field, const std::string& material, float def) {
    int top = lua_gettop(L);
    float r = def;
    lua_getfield(L, entry, "damageFactors");
    if (lua_type(L, -1) == LUA_TSTRING) {
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        lua_getfield(L, -1, "blockTable");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "damageFactors");
            if (lua_istable(L, -1)) {
                lua_pushvalue(L, top + 1);
                lua_gettable(L, -2);
                if (lua_istable(L, -1)) {
                    lua_getfield(L, -1, field);
                    if (lua_istable(L, -1)) {
                        lua_getfield(L, -1, material.c_str());
                        if (lua_type(L, -1) == LUA_TNUMBER) r = (float)lua_tonumber(L, -1);
                    }
                }
            }
        }
    }
    lua_settop(L, top);
    return r;
}

// blockTable.blocks[entry.definition][key] as a string / number
bool BlockDefField(lua_State* L, int entry, const char* key, std::string* str, float* num) {
    int top = lua_gettop(L);
    bool ok = false;
    lua_getfield(L, entry, "definition");
    if (lua_type(L, -1) == LUA_TSTRING) {
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        lua_getfield(L, -1, "blockTable");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "blocks");
            if (lua_istable(L, -1)) {
                lua_pushvalue(L, top + 1);
                lua_gettable(L, -2);
                if (lua_istable(L, -1)) {
                    lua_getfield(L, -1, key);
                    if (str && lua_type(L, -1) == LUA_TSTRING) { *str = lua_tostring(L, -1); ok = true; }
                    if (num && lua_type(L, -1) == LUA_TNUMBER) { *num = (float)lua_tonumber(L, -1); ok = true; }
                }
            }
        }
    }
    lua_settop(L, top);
    return ok;
}

// Engine tuning from Lua: the global table `tuning` (see docs/SCRIPTING.md, "Tuning table"). A number there replaces the engine's built-in constant of that name.
float Tune(lua_State* L, const char* key, float def) {
    if (!L) return def;
    int top = lua_gettop(L);
    float r = def;
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "tuning");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, key);
            if (lua_type(L, -1) == LUA_TNUMBER) r = (float)lua_tonumber(L, -1);
        }
    }
    lua_settop(L, top);
    return r;
}

// engine.strengthChange(name, other, oldStrength, newStrength) -> number or nil: a script may replace the strength a block is left with after a hit
float LuaStrengthHook(lua_State* L, const std::string& name, const std::string& other, float oldS, float newS) {
    if (!L) return newS;
    int top = lua_gettop(L);
    float r = newS;
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "engine");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "strengthChange");
            if (lua_isfunction(L, -1)) {
                lua_pushstring(L, name.c_str()); lua_pushstring(L, other.c_str()); lua_pushnumber(L, oldS); lua_pushnumber(L, newS);
                if (lua_pcall(L, 4, 1, 0) == 0) { if (lua_type(L, -1) == LUA_TNUMBER) r = (float)lua_tonumber(L, -1); }
                else std::fprintf(stderr, "engine.strengthChange: %s\n", lua_tostring(L, -1));
            }
        }
    }
    lua_settop(L, top);
    return r;
}

// engine.force(a, b, momentumX, momentumY, force) -> number or nil: a script may replace the force a contact is worked out as (all games)
float LuaForceHook(lua_State* L, const std::string& a, const std::string& b, float mx, float my, float force) {
    if (!L) return force;
    int top = lua_gettop(L);
    float r = force;
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "engine");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "force");
            if (lua_isfunction(L, -1)) {
                lua_pushstring(L, a.c_str()); lua_pushstring(L, b.c_str()); lua_pushnumber(L, mx); lua_pushnumber(L, my); lua_pushnumber(L, force);
                if (lua_pcall(L, 5, 1, 0) == 0) { if (lua_type(L, -1) == LUA_TNUMBER) r = (float)lua_tonumber(L, -1); }
                else std::fprintf(stderr, "engine.force: %s\n", lua_tostring(L, -1));
            }
        }
    }
    lua_settop(L, top);
    return r;
}

// engine.damage(attacker, victim, force, damage) -> number or nil: a script may replace the damage one object deals to another (all games)
float LuaDamageHook(lua_State* L, const std::string& attacker, const std::string& victim, float force, float damage) {
    if (!L) return damage;
    int top = lua_gettop(L);
    float r = damage;
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "engine");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "damage");
            if (lua_isfunction(L, -1)) {
                lua_pushstring(L, attacker.c_str()); lua_pushstring(L, victim.c_str()); lua_pushnumber(L, force); lua_pushnumber(L, damage);
                if (lua_pcall(L, 4, 1, 0) == 0) { if (lua_type(L, -1) == LUA_TNUMBER) r = (float)lua_tonumber(L, -1); }
                else std::fprintf(stderr, "engine.damage: %s\n", lua_tostring(L, -1));
            }
        }
    }
    lua_settop(L, top);
    return r;
}

// A number from the script environment or from worldAttributes (the exe reads its config table at game +0x37c)
float EnvConfigNumber(lua_State* L, const char* key, float def) {
    int top = lua_gettop(L);
    float r = def;
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_getfield(L, -1, key);
    if (lua_type(L, -1) == LUA_TNUMBER) r = (float)lua_tonumber(L, -1);
    else {
        lua_pop(L, 1);
        lua_getfield(L, -1, "worldAttributes");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, key);
            if (lua_type(L, -1) == LUA_TNUMBER) r = (float)lua_tonumber(L, -1);
        }
    }
    lua_settop(L, top);
    return r;
}

}  // namespace

GamePlatform::GamePlatform(const std::string& dataRoot) : dataRoot_(dataRoot) {}

// The physics world is made on first use, when the scripts have told which game this is: Rio uses Box2D 2.2.0, the others 2.1.x.
PhysicsWorld& GamePlatform::Physics() {
    if (physics_) return *physics_;
    Box2DVersion ver = scripts_ && (scripts_->IsRio() || scripts_->IsSpace()) ? Box2DVersion::V22 : Box2DVersion::V21;
    if (const char* f = std::getenv("AB_BOX2D")) ver = std::string(f) == "21" ? Box2DVersion::V21 : Box2DVersion::V22;  // test override
    if (requestedBox2D_) ver = requestedBox2D_ == 21 ? Box2DVersion::V21 : Box2DVersion::V22;
    physics_ = MakePhysicsWorld(ver);
    g_damageHook = [this](const std::string& a, const std::string& v, float f, float d) { return LuaDamageHook(scripts_ ? scripts_->State() : nullptr, a, v, f, d); };
    g_forceHook = [this](const std::string& a, const std::string& b, float mx, float my, float f) { return LuaForceHook(scripts_ ? scripts_->State() : nullptr, a, b, mx, my, f); };
    g_damageTune = [this](const char* k, float d) { return Tune(scripts_ ? scripts_->State() : nullptr, k, d); };
    physics_->luaFilter = [this](const PhysObject* a, const PhysObject* b) -> int {
        if (!scripts_) return -1;
        lua_State* L = scripts_->State();
        int top = lua_gettop(L);
        int res = -1;
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "engine");
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "shouldCollide");
                if (lua_isfunction(L, -1)) {
                    lua_pushstring(L, a->name.c_str()); lua_pushstring(L, b->name.c_str());
                    if (lua_pcall(L, 2, 1, 0) == 0) { if (lua_isboolean(L, -1)) res = lua_toboolean(L, -1) ? 1 : 0; }
                }
            }
        }
        lua_settop(L, top);
        return res;
    };
    physics_->SetBox2DFlavour(scripts_ && scripts_->IsSpace());   // Space (0x4a3220 tree, 0x4adf20 solver manifold) is a newer Box2D than Rio (0x4f1e00, 0x4f2da0)
    if (scripts_ && scripts_->IsSpace()) {
        physics_->spaceRules = true;
        solverBase_[0] = 0.005f; solverBase_[1] = 0.5f; solverBase_[2] = 0.5f; physics_->SetSolverSettings(0.005f, 0.5f, 0.5f);      // AngryBirdsSpace.exe: linearSlop 0x512484, timeToSleep 0x50f6ac, linear sleep tolerance 0.25 squared (0x51270c)
        physics_->SetPolygonRadius(0.01f);                   // 0x50f77c
    } else if (scripts_ && scripts_->IsRio()) physics_->SetSolverSettings(0.05f, 0.25f, 0.1f);
    else physics_->SetSolverSettings(0.05f, 0.25f, 0.1f);
    if (!(scripts_ && scripts_->IsSpace())) { solverBase_[0] = 0.05f; solverBase_[1] = 0.25f; solverBase_[2] = 0.1f; }
    if (scripts_ && scripts_->IsSpace()) physics_->SetGravity(0, 0);   // Space: only the gravitation sensors pull (the scripts never call setWorldGravity)
    physics_->startAsleep = std::getenv("AB_ASLEEPSTART") != nullptr;   // v2.2: the exe creates every body awake (b2BodyDef.awake = 1 at 0x411590 and the other creators); AB_ASLEEPSTART=1 starts blocks asleep for testing
    if (std::getenv("AB_PHYSDBG")) std::fprintf(stderr, "PHYS Box2D %s\n", physics_->Version() == Box2DVersion::V22 ? "2.2.0" : "2.1.1");
    physics_->onBeginContact = [this](PhysObject& a, PhysObject& b, ContactHandle& c) { OnContact(a, b, c); };
    physics_->onEndContact = [this](PhysObject& a, PhysObject& b) {
        if (scripts_) EmitHook(scripts_->State(), "contactEnd", a.name.c_str(), b.name.c_str());
        if (scripts_ && scripts_->IsStarWars()) {          // 0x41f370: both bodies wake up, sensors report a trigger exit, every contact reports exitCollision
            if (a.pbody && !a.pbody->IsStatic()) a.pbody->SetAwake(true);
            if (b.pbody && !b.pbody->IsStatic()) b.pbody->SetAwake(true);
            if (a.isSensor || b.isSensor) { CallCollisionPair("exitTriggerCollision", a.name, b.name); SpaceSensorTouch(a, b, false); }
            CallCollisionPair("exitCollision", a.name, b.name);
            return;
        }
        if (!scripts_ || !(a.isSensor || b.isSensor)) return;
        bool space = scripts_->IsSpace();
        if (space) SpaceSensorTouch(a, b, false);
        if (space || HasScriptFunction("enterCollision") || HasScriptFunction("exitCollision")) CallCollisionPair("exitCollision", a.name, b.name);
    };
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
    physics_->starWarsRules = scripts_ && scripts_->IsStarWars();
    // the contact filter of an object with blockCollision false reads objects.world[name].collisionMaterials live
    physics_->collisionMaterialsOf = [this](const PhysObject& o) {
        std::vector<std::string> list;
        if (!scripts_) return list;
        lua_State* L = scripts_->State();
        PushWorldEntry(L, o.name);
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "collisionMaterials");
            if (lua_istable(L, -1)) for (int k = 1; k <= (int)lua_objlen(L, -1); k++) { lua_rawgeti(L, -1, k); if (lua_type(L, -1) == LUA_TSTRING) list.push_back(lua_tostring(L, -1)); lua_pop(L, 1); }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        return list;
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
        // Angry Birds Star Wars bullet time (Obi-Wan, the Force powers): the exe's delta-time multiplier set by
        // setDeltaTimeMultiplier scales the time step of the whole frame; updateBulletTime divides it back out for its own timers.
        lua_getfield(L, -2, "g_deltaTimeMultiplier");
        if (lua_isnumber(L, -1) && lua_tonumber(L, -1) > 0) dt *= (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, -2, "physicsSpeedFactor");
        if (lua_isnumber(L, -1) && lua_tonumber(L, -1) > 0) physicsScale = (float)lua_tonumber(L, -1);
        lua_settop(L, top);
    }
    Physics().Frame().physicsScale = physicsScale;
    // Angry Birds Space (the exe's 0x482990): every frame each background element moves by its velocity and its scale grows by its scaleSpeed
    if (scripts_ && scripts_->IsSpace() && newTheme_) { UpdateNewThemeLayers(bgLayers_, dt); UpdateNewThemeLayers(ntFg_, dt); }
    if (scripts_ && scripts_->IsSpace()) for (ThemeSprite& t : themeSprites_) {
        t.x += t.velX * dt; t.y += t.velY * dt;
        if (t.wrapW > 0) { if (t.x > t.wrapW) t.x -= 2 * t.wrapW; else if (t.x < -t.wrapW) t.x += 2 * t.wrapW; }
        if (t.wrapH > 0) { if (t.y > t.wrapH) t.y -= 2 * t.wrapH; else if (t.y < -t.wrapH) t.y += 2 * t.wrapH; }
        if (t.scaleSpeed != 0 && t.scaleX + t.scaleSpeed * dt > 0) { t.scaleX += t.scaleSpeed * dt; t.scaleY += t.scaleSpeed * dt; }
    }
    Physics().Frame().starWarsSync = scripts_ && scripts_->IsStarWars();
    Physics().Frame().stepOnEqual = scripts_ && scripts_->IsSpace();
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
    static int execFrame = 0;
    if (const char* od = std::getenv("AB_OBJDUMP")) if (execFrame + 1 == std::atoi(od))
        for (const auto& o : Physics().Objects()) { BodyInfo bi = Physics().Inspect(o->name); std::fprintf(stderr, "OD %s spr=%s st=%d sensor=%d r=%g m=%g scaled=%d sc=%g,%g x=%.1f y=%.1f fr=%g e=%g\n", o->name.c_str(), o->sprite.c_str(), (int)bi.isStatic, (int)o->isSensor, bi.shapeRadius, bi.mass, (int)o->scaled, o->scaleX, o->scaleY, o->interpX, o->interpY, o->pbody ? o->pbody->Friction() : 0.0f, o->pbody ? o->pbody->Restitution() : 0.0f); }
    if (const char* pn = std::getenv("AB_PROBE")) if (Physics().Frame().running) if (Physics().Find(pn)) { BodyInfo bi = Physics().Inspect(pn); std::fprintf(stderr, "PROBE %d %.3f %.3f v=%.2f,%.2f awake=%d scale=%g acc=%g dt=%g\n", execFrame, bi.posX, bi.posY, bi.velX, bi.velY, (int)bi.awake, Physics().Frame().physicsScale, Physics().Frame().accumulator, dt); }
    {   // test hook: AB_EXEC_FRAME=n AB_EXEC="lua code" runs a snippet on frame n
        const char* at = std::getenv("AB_EXEC_FRAME");
        const char* at2 = std::getenv("AB_EXEC2_FRAME");
        const char* at3 = std::getenv("AB_EXEC3_FRAME");
        ++execFrame;
        if (scripts_ && at && execFrame == std::atoi(at) && std::getenv("AB_EXEC")) scripts_->RunString(std::getenv("AB_EXEC"));
        if (scripts_ && at2 && execFrame == std::atoi(at2) && std::getenv("AB_EXEC2")) scripts_->RunString(std::getenv("AB_EXEC2"));
        if (scripts_ && at3 && execFrame == std::atoi(at3) && std::getenv("AB_EXEC3")) scripts_->RunString(std::getenv("AB_EXEC3"));
    }
    // Classic 1.6.2: an extra_scripts.lua next to gamelogic.lua (Star Wars' screen wipe between scenes, see extras/) is loaded once the scripts are up
    if (scripts_ && execFrame == 3 && !scripts_->IsSpace() && !IsSeasonsScripts() && !std::getenv("AB_NOSCREENWIPE"))
        scripts_->RunString(
            "local env = _G.getfenv(1)\n"
            "if env.extraScripts == nil and _G.type(env.loadLuaFile) == 'function' and env.scriptPath and _G.io then\n"
            "  local f = _G.io.open(env.scriptPath .. '/extra_scripts.lua', 'rb')\n"
            "  if f then f:close(); _G.pcall(env.loadLuaFile, env.scriptPath .. '/extra_scripts.lua', '', false, false) end\n"
            "end\n");
    // Space: the scripts' debug keys (fps, background, hit boxes, level skip, ...) sit behind `not releaseBuild`, and options.lua sets releaseBuild
    // (so the retail game has none). They are switched on the way the other games' are: cheats on, and the functions that read the keys run in a
    // view where releaseBuild reads false (the real flag stays true: false would unlock every level). AB_NODEBUGKEYS=1 turns this off.
    if (scripts_ && scripts_->IsSpace() && execFrame % 20 == 0 && !std::getenv("AB_NODEBUGKEYS"))
        scripts_->RunString(
            "local env = _G.getfenv(1)\n"
            "env.cheatsEnabled = true\n"
            "local view = env.__abview\n"
            "if not view then view = _G.setmetatable({releaseBuild = false}, {__index = env, __newindex = env}); env.__abview = view; env.__abwrapped = _G.setmetatable({}, {__mode = 'k'}) end\n"
            "local function wrap(f) if _G.type(f) == 'function' and not env.__abwrapped[f] then env.__abwrapped[f] = true; _G.setfenv(f, view) end end\n"
            "if env.QAPage == nil and _G.type(env.loadLuaFile) == 'function' and env.scriptPath then if not _G.pcall(env.loadLuaFile, env.scriptPath .. '/menus/QAPage.lua', '', false, false) or env.QAPage == nil then _G.pcall(env.loadLuaFile, env.scriptPath .. '/menus_space/QAPage.lua', '', false, false) end end\n"   // the cheat page: the scripts only load it when cheatsEnabled is already true at start-up
            "wrap(env.update) wrap(env.updateGame) wrap(env.updateMenu)\n"
            "local gs = env.GameScene\n"
            "if _G.type(gs) == 'table' then wrap(gs.update) wrap(gs.onEntry) end\n");
    // Space: AB_UNLOCKALL=1 opens every level and episode of the select pages for playtesting (the retail progress rules stay otherwise)
    if (scripts_ && scripts_->IsSpace() && execFrame % 60 == 0 && std::getenv("AB_UNLOCKALL"))
        scripts_->RunString("local e=_G.getfenv(1); local W=e.SettingsWrapper; if W and not W.__abUnlocked then W.__abUnlocked=true; "
                            "W.getLevelProgress=function() return 99999 end; W.isComicWatched=function() return true end; "
                            "W.isEpisodePurchased=function() return true end; W.isEggsteroidLevelUnlocked=function() return true end end");
    if (scripts_ && scripts_->IsSpace() && dt > 0) { UpdateAimingAid(dt); StepThemeAnimations(dt); }
    else if (scripts_ && dt > 0 && (aimEnabled_ || !aimDots_.empty())) UpdateAimingAid(dt);   // Classic: extra_scripts.lua drives the aiming aid
    if (scripts_ && scripts_->IsStarWars() && dt > 0) UpdateKillTimers(dt);
    if (scripts_ && scripts_->IsStarWars() && dt > 0 && Physics().Frame().running) UpdateRioSquash(dt);   // 0x433xxx: the same hit wobble as Rio's
    if (scripts_ && scripts_->IsRio() && dt > 0) {
        if (Physics().Frame().running) UpdateRioGettingUp(dt);
        SelectRioAnimations(dt);
        if (Physics().Frame().running) { UpdateRioBirdFaces(dt); UpdateRioSquash(dt); }
    }
    if (scripts_ && (scripts_->IsRio() || scripts_->IsSpace())) { bool ok = scripts_->Draw(); if (!ok && std::getenv("AB_DRAWDBG")) std::fprintf(stderr, "draw() failed\n"); }      // Rio: the exe calls draw() after update()
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
        bool wasRunning = physics_->Frame().running;
        physics_->Clear();
        physics_.reset();
        Physics().SetGravity(gx, gy);
        // Space: onEntry enables physics before the level file loads, and the flag belongs to the game, not the world; without it the menu
        // levels (level / episode selection) never ran their camera, so the scroll arrows did nothing
        if (scripts_ && scripts_->IsSpace()) Physics().SetRunning(wasRunning);
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

// Space createThemeAnimation (0x484cc0): an entry of the layer list with the frames to cycle through
void GamePlatform::CreateThemeAnimation(const ThemeAnimation& a) {
    ThemeSprite t{a.name, a.sprite, a.x, a.y, a.scaleX, a.scaleY, a.angle, 0, a.layer};
    t.frames = a.frames; t.animTimer = a.animTimer; t.animDelay = a.animDelay; t.startDelay = a.startingDelay; t.loop = a.loop;
    if (t.sprite.empty() && !t.frames.empty()) t.sprite = t.frames[0];
    if (std::getenv("AB_ANIMDBG")) std::fprintf(stderr, "THEMEANIM create %s sprite=%s frames=%zu delay=%g start=%g loop=%d layer=%d n=%zu\n", t.name.c_str(), t.sprite.c_str(), t.frames.size(), t.animDelay, t.startDelay, (int)t.loop, t.layer, themeSprites_.size());
    themeSprites_.push_back(t);
}

void GamePlatform::SetThemeSprite(const std::string& name, const std::string& sprite, int layer) {
    for (ThemeSprite& t : themeSprites_)
        if (t.name == name && t.layer == layer) { t.sprite = sprite; break; }
}

// Space and Star Wars: updateThemeSprite(dt) steps the frame animations of createThemeAnimation (their scripts never call rotateThemeSprites)
void GamePlatform::StepThemeAnimations(float dt) {
    for (ThemeSprite& t : themeSprites_) {
        if (t.frames.size() > 1 && t.animDelay > 0) {          // the next frame every animDelay seconds, after the starting delay
            if (t.startDelay > 0) t.startDelay -= dt;
            else {
                t.animTimer += dt;
                while (t.animTimer >= t.animDelay) {
                    t.animTimer -= t.animDelay;
                    if (t.frame + 1 < (int)t.frames.size()) t.frame++;
                    else if (t.loop) t.frame = 0;
                    else { t.finished = true; break; }      // an animation that does not loop (the shooting stars) ends and is taken away; the layer's repopulation makes the next one
                    t.sprite = t.frames[t.frame];
                }
            }
        }
    }
    themeSprites_.erase(std::remove_if(themeSprites_.begin(), themeSprites_.end(), [](const ThemeSprite& t) { return t.finished; }), themeSprites_.end());
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
        if (t.velX != 0 || t.velY != 0) { t.x += t.velX * dt; t.y += t.velY * dt; }
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
// Angry Birds Space: a contact with a sensor goes to the global enterCollision(a, b) / exitCollision(a, b) (the trigger actions), not to blockCollision
bool GamePlatform::HasScriptFunction(const char* fn) {
    lua_State* L = scripts_->State();
    int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_getfield(L, -1, fn);
    bool has = lua_isfunction(L, -1);
    lua_settop(L, top);
    return has;
}

void GamePlatform::CallCollisionPair(const char* fn, const std::string& a, const std::string& b) {
    lua_State* L = scripts_->State();
    int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_getfield(L, -1, fn);
    if (lua_isfunction(L, -1)) {
        lua_pushlstring(L, a.data(), a.size());
        lua_pushlstring(L, b.data(), b.size());
        if (lua_pcall(L, 2, 0, 0) != 0) std::fprintf(stderr, "script error: %s: %s\n", fn, lua_tostring(L, -1));
    }
    lua_settop(L, top);
}

// Space (exe 0x46a500 / 0x470398): touching a gravitation sensor (type 2) sets the Lua field insideGravity on the other object; leaving the last one clears it
void GamePlatform::SpaceSensorTouch(PhysObject& pa, PhysObject& pb, bool enter) {
    PhysObject* pair[2][2] = {{&pa, &pb}, {&pb, &pa}};
    for (auto& pr : pair) {
        PhysObject* sen = pr[0]; PhysObject* other = pr[1];
        if (!sen->isSensor) continue;
        lua_State* L = scripts_->State();
        int top = lua_gettop(L);
        if (enter) {                                                     // 0x46a500: kinds 2 and 3 wake the object and list the sensor; kind 2 sets insideGravity
            if (scripts_->IsStarWars() && sen->sensorKind == 4 && !other->isSensor) { sen->killOn = true; continue; }   // 0x419920: a kind 4 sensor is spent by the first touch
            if (sen->sensorKind != 2 && sen->sensorKind != 3) continue;
            if (other->pbody && !other->pbody->IsStatic()) other->pbody->SetAwake(true);
            physics_->SensorPair(sen, other, true);
            if (sen->sensorKind == 2) {
                PushWorldEntry(L, other->name);
                if (lua_istable(L, -1)) { lua_pushboolean(L, 1); lua_setfield(L, -2, "insideGravity"); }
            }
        } else {                                                         // 0x470380: drop one entry; the last one clears insideGravity
            if (physics_->SensorPair(sen, other, false) == 0) {
                PushWorldEntry(L, other->name);
                if (lua_istable(L, -1)) { lua_pushboolean(L, 0); lua_setfield(L, -2, "insideGravity"); }
            }
            lua_settop(L, top);
            if (sen->spaceFlagA && !other->spaceFlagA) {                 // a sensor with parameter 19: what an object does when it leaves it
                if (other->isBird) {
                    PushWorldEntry(L, other->name);
                    if (lua_istable(L, -1)) { lua_pushboolean(L, 1); lua_setfield(L, -2, "killingVolumeRemovalTime"); }
                } else if (other->pbody && (other->stopOnExit || other->drawWithBirds)) {
                    if (!other->stopOnExit) physics_->SetVelocity(other->name, 0, 0);   // parameter 1 (+0x1a4) also stops the object
                    other->pbody->SetLinearDamping(100.0f);
                    if (other->stopOnExit && scripts_->IsStarWars()) other->killOn = true;   // 0x41f220: and starts its destroy timer
                }
            }
        }
        lua_settop(L, top);
    }
}

// Copies the Lua `tuning` table into the physics world (names the physics reads with Tn) and applies the solver names:
// solver.velocityIterations, solver.positionIterations, solver.linearSlop, solver.timeToSleep, solver.sleepTolerance, solver.gravityScale.
void GamePlatform::SyncTuning(int& velIter, int& posIter) {
    if (!scripts_ || !physics_) return;
    lua_State* L = scripts_->State();
    int top = lua_gettop(L);
    physics_->tuning.clear();
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "tuning");
        if (lua_istable(L, -1)) {
            lua_pushnil(L);
            while (lua_next(L, -2)) {
                if (lua_type(L, -2) == LUA_TSTRING && lua_type(L, -1) == LUA_TNUMBER) physics_->tuning[lua_tostring(L, -2)] = (float)lua_tonumber(L, -1);
                lua_pop(L, 1);
            }
        }
    }
    lua_settop(L, top);
    velIter = (int)physics_->Tn("solver.velocityIterations", (float)velIter);
    posIter = (int)physics_->Tn("solver.positionIterations", (float)posIter);
    if (physics_->tuning.count("solver.linearSlop") || physics_->tuning.count("solver.timeToSleep") || physics_->tuning.count("solver.sleepTolerance"))
        physics_->SetSolverSettings(physics_->Tn("solver.linearSlop", solverBase_[0]), physics_->Tn("solver.timeToSleep", solverBase_[1]), physics_->Tn("solver.sleepTolerance", solverBase_[2]));
}

// Angry Birds Star Wars, the contact handler at 0x42f9d0 (Box2D BeginContact). Differences from the older games' 0x40b670: the force divides by
// game +0x3bc (10) after being scaled by the config's forceDamageMultiplier, a block hit by a bird takes the momentum difference of the two, damaging
// sensors (kinds 5, 6, 7) hurt blocks with their definition's `damage`, objects with ignoresScore deal no score, HOMING birds do a fifth of the damage
// until they have collided, and birdCollision / blockCollision get the contact point and normal as well.
void GamePlatform::OnContactStarWars(PhysObject& pa, PhysObject& pb, ContactHandle& contact) {
    lua_State* L = scripts_->State();
    int top = lua_gettop(L);
    const float fm = EnvConfigNumber(L, "forceDamageMultiplier", 1.0f);
    const float divisor = Tune(L, "sw.forceDivisor", 10.0f);          // game +0x3bc
    PhysObject* A = &pa; PhysObject* B = &pb;
    bool sensorMode = false, sensor57 = false;
    float sensorDamage = 0;
    if (pa.isSensor || pb.isSensor) {
        // 0x419920 for (A, B) and then (B, A): kinds 2 and 3 pull / wake, kinds 5, 6, 7 hurt (unless the other has parameter 20)
        CallCollisionPair("enterCollision", pa.name, pb.name);
        SpaceSensorTouch(pa, pb, true);
        PhysObject* pr[2][2] = {{A, B}, {B, A}};
        bool any = false;
        for (auto& p : pr) {
            PhysObject* sen = p[0]; PhysObject* oth = p[1];
            if (!sen->isSensor) continue;
            const bool hurts = (sen->sensorKind == 5 || sen->sensorKind == 6 || sen->sensorKind == 7) && !oth->spaceActive19e;
            if (!hurts) continue;
            any = true;
            PushWorldEntry(L, sen->name);
            float d = 0;
            if (lua_istable(L, -1)) BlockDefField(L, lua_gettop(L), "damage", nullptr, &d);
            lua_pop(L, 1);
            sensorDamage = d;
            sensor57 = sen->sensorKind == 5 || sen->sensorKind == 7;
        }
        if (!any) { lua_settop(L, top); return; }
        sensorMode = true;
    }
    float wm[4];
    contact.WorldManifold(wm[0], wm[1], wm[2], wm[3]);
    // the hit wobble (min(force * 0.02, 0.1)): an object with a bounce multiplier (parameter 6) is set going when the wobble it has is smaller
    auto wobble = [L](PhysObject* o, float force, bool needMultiplier) {
        const float f = std::min(force * Tune(L, "sw.wobbleFactor", 0.02f), Tune(L, "sw.wobbleMax", 0.1f));
        if ((!needMultiplier || o->bounceAmp > 0) && o->wobCur < f) { o->wobAmp0 = f; o->wobActive = true; }
    };
    PushWorldEntry(L, A->name);
    PushWorldEntry(L, B->name);
    const int ia = top + 1, ib = top + 2;
    if (!lua_istable(L, ia) || !lua_istable(L, ib)) { lua_settop(L, top); return; }
    PhysBody& bA = *A->pbody; PhysBody& bB = *B->pbody;
    const bool cA = A->isBird, cB = B->isBird;

    if (cA && cB) {                                                  // bird against bird: the faster one decides
        const PhysBody& f = (bA.VelX() * bA.VelX() + bA.VelY() * bA.VelY() <= bB.VelX() * bB.VelX() + bB.VelY() * bB.VelY()) ? bB : bA;
        float force = std::sqrt(f.VelX() * f.VelX() + f.VelY() * f.VelY()) * f.Mass() / divisor;
        lua_settop(L, top);
        // 0x42fe61: both birds start the hit wobble (min(force * 0.02, 0.1)) unless parameter 11 is set, when theirs is smaller
        for (PhysObject* o : {A, B}) if (!o->noHitWobble) wobble(o, force, false);
        birdCollisions++;
        scripts_->BirdCollisionSW(A->name, B->name, force, 0, wm);
        return;
    }

    std::string matA, matB;
    EntryString(L, ia, "material", matA); EntryString(L, ib, "material", matB);

    if (cA || cB) {                                                  // a bird against a block
        PhysObject* bird = cA ? A : B; PhysObject* block = cA ? B : A;
        const int ibird = cA ? ia : ib, iblock = cA ? ib : ia;
        const std::string& blockMat = cA ? matB : matA;
        float dm = DamageFactorOf(L, ibird, "damageMultiplier", blockMat, 1.0f);
        float vm = DamageFactorOf(L, ibird, "velocityMultiplier", blockMat, 1.0f);
        lua_getfield(L, ibird, "powerupDamageMultiplier");
        if (lua_type(L, -1) == LUA_TNUMBER) dm *= (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, ibird, "useLegacyCollisionPath");
        const bool legacy = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        PhysBody& pbird = *bird->pbody; PhysBody& pblock = *block->pbody;
        const float bx = fm * pblock.Mass() * pblock.VelX(), by = fm * pblock.Mass() * pblock.VelY();
        const float k = fm * dm * pbird.Mass();
        const float dpx = k * pbird.VelX() - bx, dpy = k * pbird.VelY() - by;
        const float force = LuaForceHook(L, bird->name, block->name, dpx, dpy, std::sqrt(dpx * dpx + dpy * dpy) / divisor);
        float dealt = 0;
        PushWorldEntry(L, block->name);                              // objects.world[block]
        const int iw = lua_gettop(L);
        if (lua_istable(L, iw)) {
            float strength = 0, defence = 0;
            if (GetNumber(L, iw, "strength", strength)) {
                GetNumber(L, iw, "defence", defence);
                float excess = force - defence;
                std::string spec; BlockDefField(L, ibird, "specialty", &spec, nullptr);
                lua_getfield(L, ibird, "hasCollided");
                const bool hasCollided = lua_toboolean(L, -1) != 0;
                lua_pop(L, 1);
                if (spec == "HOMING" && !hasCollided) excess *= Tune(L, "sw.homingDamage", 0.2f);
                excess = LuaDamageHook(L, bird->name, block->name, force, excess);
                dealt = excess;
                if (excess > 0) {
                    float remaining = LuaStrengthHook(L, block->name, bird->name, strength, strength - excess);
                    lua_pushnumber(L, remaining); lua_setfield(L, iw, "strength");
                    if (remaining <= 0) {
                        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
                        lua_getfield(L, -1, "deadBlocks");
                        if (lua_istable(L, -1)) { lua_pushvalue(L, iw); lua_setfield(L, -2, block->name.c_str()); }
                        lua_pop(L, 2);
                        if (legacy) {
                            float ratio = ((-remaining / pbird.Mass()) / force) * divisor * 1.75f;
                            if (ratio > 1.0f) ratio = 1.0f;
                            VelocityChange vc; vc.name = bird->name; vc.velocity = {pbird.VelX() * ratio, pbird.VelY() * ratio}; vc.immediate = true;
                            Physics().ApplyVelocityChange(vc);
                        } else {
                            float ratio = ((force - strength) / force) * vm;
                            if (ratio > 1.0f) ratio = 1.0f;
                            VelocityChange vc; vc.name = bird->name; vc.velocity = {pbird.VelX() * ratio, pbird.VelY() * ratio}; vc.immediate = false;
                            Physics().ApplyVelocityChange(vc);
                        }
                        contact.SetEnabled(false);
                        dealt = strength;
                    }
                }
            }
        }
        lua_settop(L, top);
        wobble(bird, force, false); wobble(block, force, true);
        if (bird->birdGravReduce < 0) bird->birdGravReduce = 0;     // the first collision starts the bird's gravity timer (0x431154)
        birdCollisions++;
        scripts_->BirdCollisionSW(bird->name, block->name, force, std::floor(dealt), wm);
        return;
    }

    // block against block (or a damaging sensor against a block)
    if (sensor57) { lua_settop(L, top); return; }
    float force;
    if (sensorMode) force = sensorDamage;
    else {
        const float dx = fm * (bA.Mass() * bA.VelX() - bB.Mass() * bB.VelX()), dy = fm * (bA.Mass() * bA.VelY() - bB.Mass() * bB.VelY());
        force = LuaForceHook(L, A->name, B->name, dx, dy, std::sqrt(dx * dx + dy * dy) / divisor);
    }
    float scoreBefore = 0;
    PushBlockScore(L);
    if (lua_istable(L, -1)) GetNumber(L, lua_gettop(L), "score", scoreBefore);
    lua_pop(L, 1);
    const float factorA = DamageFactorOf(L, ib, "damageMultiplier", matA, 1.0f);   // B's set, keyed by A's material: what A takes
    const float factorB = DamageFactorOf(L, ia, "damageMultiplier", matB, 1.0f);   // A's set, keyed by B's material: what B takes
    float total = 0, lastExcess = 0;
    bool okA = true, okB = true;
    auto hit = [&](PhysObject* o, int idx, float factor, bool& ok) {
        float strength = 0, defence = 0;
        if (!GetNumber(L, idx, "strength", strength)) return;
        GetNumber(L, idx, "defence", defence);
        if (!(defence > force * factor)) {
            // not blocked by the defence
        } else { ok = false; return; }
        float excess = force * factor - defence;
        excess = LuaDamageHook(L, (o == A ? B : A)->name, o->name, force, excess);   // engine.damage(attacker, victim, force, damage)
        lastExcess = excess;
        float remaining = LuaStrengthHook(L, o->name, (o == A ? B : A)->name, strength, strength - std::floor(excess));   // engine.strengthChange
        lua_pushnumber(L, remaining); lua_setfield(L, idx, "strength");
        float dealt;
        if (remaining <= 0) {
            lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
            lua_getfield(L, -1, "deadBlocks");
            if (lua_istable(L, -1)) { lua_pushvalue(L, idx); lua_setfield(L, -2, o->name.c_str()); }
            lua_pop(L, 2);
            dealt = strength;
        } else dealt = force - defence;
        total += dealt;
        if (o->ignoresScore) total = 0;
    };
    hit(A, ia, factorA, okA);
    hit(B, ib, factorB, okB);
    const bool wasDamageDone = okA || okB;
    wobble(B, force, true); wobble(A, force, true);
    lua_settop(L, top);
    blockCollisions++;
    scripts_->BlockCollisionSW(A->name, B->name, force, wasDamageDone, std::floor(lastExcess), wm);
    if (total > 0) {
        const float add = (float)(int)EnvConfigNumber(L, "scoreDamageMultiplier", 5.0f) * std::floor(total);
        PushBlockScore(L);
        if (lua_istable(L, -1)) { lua_pushnumber(L, scoreBefore + add); lua_setfield(L, -2, "score"); }
        lua_settop(L, top);
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        lua_getfield(L, -1, "addScoreToBird");
        if (lua_isfunction(L, -1)) { lua_pushnumber(L, add); if (lua_pcall(L, 1, 0, 0) != 0) lua_pop(L, 1); }
        lua_settop(L, top);
    }
}

void GamePlatform::OnContact(PhysObject& pa, PhysObject& pb, ContactHandle& contact) {
    if (scripts_) EmitHook(scripts_->State(), "contact", pa.name.c_str(), pb.name.c_str());
    if (scripts_ && scripts_->IsStarWars()) { OnContactStarWars(pa, pb, contact); return; }
    // v2.5: a contact with a sensor goes to the global enterCollision(a, b) / exitCollision(a, b) in every game whose script defines one of them (Space always)
    if (scripts_ && (pa.isSensor || pb.isSensor) && (scripts_->IsSpace() || HasScriptFunction("enterCollision") || HasScriptFunction("exitCollision"))) {
        if (scripts_->IsSpace()) SpaceSensorTouch(pa, pb, true);
        CallCollisionPair("enterCollision", pa.name, pb.name);
        return;
    }
    if (scripts_ && scripts_->IsRio()) {         // Rio (0x40daa6): a hit starts the scale wobble of both objects that have a bounce multiplier
        float dpx = pa.pbody->Mass() * pa.pbody->VelX() - pb.pbody->Mass() * pb.pbody->VelX();
        float dpy = pa.pbody->Mass() * pa.pbody->VelY() - pb.pbody->Mass() * pb.pbody->VelY();
        float dpLength = std::sqrt(dpx * dpx + dpy * dpy);
        float wobble = std::min(dpLength / 10.0f * Tune(scripts_->State(), "rio.wobbleFactor", 0.02f), Tune(scripts_->State(), "rio.wobbleMax", 0.1f));      // the momentum difference over the world scale, as for the damage
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

    for (size_t i = 0; i < r.strengthUpdates.size(); i++) {      // engine.strengthChange can replace the strength a block is left with
        StrengthUpdate& u = r.strengthUpdates[i];
        const std::string& other = (u.name == pa.name) ? pb.name : pa.name;
        const float was = u.strength;
        u.strength = LuaStrengthHook(L, u.name, other, was, was);
        auto it = std::find(r.destroyed.begin(), r.destroyed.end(), u.name);
        if (u.strength > 0 && it != r.destroyed.end()) r.destroyed.erase(it);
        else if (u.strength <= 0 && it == r.destroyed.end()) r.destroyed.push_back(u.name);
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
    if (keepLast_ && backend_) { lastFrame_.clear(); backend_->ReadPixels(lastFrame_, lastW_, lastH_); }
    quadsThisFrame_ = 0;
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
    q.tex = tex; q.mask = -1; q.alpha = alpha; q.r = r; q.g = g; q.b = b; q.blend = state_.blend;
    if (state_.effect >= 1 && state_.effect <= 3 && tex >= 0) q.tex = VariantTexture(tex, state_.effect);
    if (drawingCursor_) q.kind = 0;
    else if (drawingNative_) { q.kind = 1; q.key = nativeKey_; }
    else if (state_.z != 0) { q.kind = 2; q.key = state_.z; }
    return q;
}

// Puts a quad in the frame list. Kind 0 goes to the end. A game object (kind 1) goes to the end too, unless a sprite with a higher z is
// already in the list after the last object: then it goes below that sprite. A sprite (kind 2) goes right after the last object or
// sprite whose z is not higher than its own; with none, before the first one that is higher; with no objects or sprites, at the end.
void GamePlatform::EmitQuad(QueuedQuad& q) {
    quadsThisFrame_++;
    static bool inEffect = false;
    if (!inEffect && (state_.effect == 4 || state_.effect == 5) && q.tex >= 0 && q.mask < 0 && !drawingCursor_) {
        // v2.4 chromatic aberration (red and blue copies pulled apart) and glow (soft additive copies around it); the fixed-function renderer has no shaders
        inEffect = true;
        const float d = state_.effect == 4 ? 2.5f : 3.0f;
        const float dx[4] = {-d, d, 0, 0}, dy[4] = {0, 0, -d, d};
        for (int k = 0; k < (state_.effect == 4 ? 2 : 4); k++) {
            QueuedQuad c = q;
            for (int i = 0; i < 4; i++) { c.xy[i * 2] += dx[k]; c.xy[i * 2 + 1] += dy[k]; }
            c.blend = 1;
            if (state_.effect == 4) { c.alpha = q.alpha * 0.55f; if (k == 0) { c.g = 0; c.b = 0; } else { c.r = 0; c.g = 0; } }
            else c.alpha = q.alpha * 0.22f;
            EmitQuad(c);
        }
        inEffect = false;
    }
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
        backend_->SetBlend(c.blend);
        if (c.mask >= 0) { backend_->SetTileTint(c.r, c.g, c.b); backend_->DrawMaskedQuad(c.tex, c.mask, c.xy, c.tuv, c.uv, c.alpha); }
        else backend_->DrawQuad(c.tex, c.xy, c.uv, c.r, c.g, c.b, c.alpha);
    }
    backend_->SetBlend(0);
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
        handleSource_[info.handle] = {path, repeat};
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
// drawTexturedLine2D(sprite, x1, y1, x2, y2, width, r, g, b, a) (0x469040): the sprite's picture stretched along the line from (x1, y1) to (x2, y2) and
// `width` thick, centred on the line; the colour is 0 to 255 and multiplies the sprite (the slingshot's rubber bands are drawn with it)
void GamePlatform::DrawTexturedLine2D(const std::string& name, float x1, float y1, float x2, float y2, float width, float r, float g, float b, float a) {
    if (!backend_) return;
    const SpriteSheet* sheet = nullptr;
    const Sprite* sp = sprites_.Find(name, &sheet);
    if (!sp) return;
    float dx = x2 - x1, dy = y2 - y1, len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0) return;
    RenderState keep = state_;
    // Star Wars / Space (0x425810): the quad's corners are p1 + n/2, p1 - n/2, p2 + n/2, p2 - n/2 with n the line's normal times the width, and they take the sprite's
    // top left, top right, bottom left and bottom right: the picture's height always runs along the line, its width across it, whatever the sprite looks like
    if ((scripts_ && scripts_->IsSpace()) || (sp->h > sp->w * 2 && sp->pivotY < 0)) {
        // Star Wars' beams (the pink birds' gravity rays) are tall sprites: the picture runs along its height, from the top centre (its pivot) to the far end
        state_.angle = keep.angle + std::atan2(dy, dx) - 1.5707963f;
        state_.pivotX = width * 0.5f; state_.pivotY = 0;
        state_.tintR = keep.tintR * r / 255.0f; state_.tintG = keep.tintG * g / 255.0f; state_.tintB = keep.tintB * b / 255.0f;
        state_.alpha = keep.alpha * a / 255.0f;
        DrawSpriteAt(*sheet, *sp, x1 - width * 0.5f, y1, 0, 0, true, width, len);
        state_ = keep;
        return;
    }
    state_.angle = keep.angle + std::atan2(dy, dx);
    state_.pivotX = 0; state_.pivotY = width * 0.5f;
    state_.tintR = keep.tintR * r / 255.0f; state_.tintG = keep.tintG * g / 255.0f; state_.tintB = keep.tintB * b / 255.0f;
    state_.alpha = keep.alpha * a / 255.0f;
    DrawSpriteAt(*sheet, *sp, x1, y1 - width * 0.5f, 0, 0, true, len, width);
    state_ = keep;
}

void GamePlatform::ResDrawSprite(const std::string& name, float x, float y, int vAnchor, int hAnchor, bool sized, float width, float height) {
    if (!backend_) return;
    // v2.5: the Angry Birds Space splash always sits on a black rectangle over the whole screen (the Rovio logo before it has a white one)
    if (scripts_ && scripts_->IsSpace() && name.compare(0, 12, "SPLASH_SPACE") == 0) {
        RenderState keepState = state_;
        state_ = RenderState();
        DrawRect(0, 0, 0, 1, 0, 0, (float)screenWidth_, (float)screenHeight_, false);
        state_ = keepState;
    }
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
    std::string oldCompo = compoTag_;
    compoTag_ = name;
    for (const CompoPart& part : compo->parts) {
        const SpriteSheet* partSheet = nullptr;
        const Sprite* ps = sprites_.Find(part.sprite, &partSheet);
        if (!ps) continue;
        // the pivot is measured from the composite's top left corner: a part turns about that same point, so its own pivot is moved by where it sits in the composite
        const float keepPX = state_.pivotX, keepPY = state_.pivotY;
        state_.pivotX = keepPX - ((float)(part.x - ps->pivotX) + (float)ox);
        state_.pivotY = keepPY - ((float)(part.y - ps->pivotY) + (float)oy);
        DrawSpriteAt(*partSheet, *ps, x + (float)part.x, y + (float)part.y, 4, 3, false, 0, 0);
        state_.pivotX = keepPX; state_.pivotY = keepPY;
    }
    compoTag_ = oldCompo;
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

// v2.4 shader presets 1..3 (grayscale, sepia, invert) as a converted copy of the texture, made once. The sheets are premultiplied, so the same maths holds.
int GamePlatform::VariantTexture(int handle, int effect) {
    auto src = handleSource_.find(handle);
    if (src == handleSource_.end() || !backend_) return handle;
    long long key = (long long)handle * 8 + effect;
    auto it = textureVariants_.find(key);
    if (it != textureVariants_.end()) return it->second;
    Image image;
    int made = handle;
    if (LoadPngFile(src->second.first, image, nullptr)) {
        for (size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
            float r = image.rgba[i], g = image.rgba[i + 1], b = image.rgba[i + 2], a = image.rgba[i + 3];
            float lum = 0.299f * r + 0.587f * g + 0.114f * b, nr, ng, nb;
            if (effect == 1) nr = ng = nb = lum;
            else if (effect == 2) { nr = std::min(a, lum * 1.07f); ng = lum * 0.74f; nb = lum * 0.43f; }
            else { nr = a - r; ng = a - g; nb = a - b; }
            image.rgba[i] = (unsigned char)std::min(255.0f, std::max(0.0f, nr)); image.rgba[i + 1] = (unsigned char)std::min(255.0f, std::max(0.0f, ng)); image.rgba[i + 2] = (unsigned char)std::min(255.0f, std::max(0.0f, nb));
        }
        made = backend_->CreateTexture(image, src->second.second);
    }
    textureVariants_[key] = made;
    return made;
}

// res.getSpritePixel(sprite, x, y): the pixel (x, y) counted from the sprite's top left corner, 0..255 per channel, straight from the png
bool GamePlatform::ResGetSpritePixel(const std::string& name, int x, int y, int& r, int& g, int& b, int& a) {
    r = g = b = a = 0;
    const SpriteSheet* sheet = nullptr;
    const Sprite* sp = sprites_.Find(name, &sheet);
    if (!sp || x < 0 || y < 0 || x >= sp->w || y >= sp->h) return false;
    std::string path = sheet->directory + "/" + sheet->image;
    auto it = pixelImages_.find(path);
    if (it == pixelImages_.end()) { Image im; if (!LoadPngFile(path, im, nullptr)) return false; it = pixelImages_.emplace(path, std::move(im)).first; }
    const Image& im = it->second;
    int px = sp->x + x, py = sp->y + y;
    if (px >= im.width || py >= im.height) return false;
    const unsigned char* s = &im.rgba[((size_t)py * im.width + px) * 4];
    r = s[0]; g = s[1]; b = s[2]; a = s[3];
    return true;
}

void GamePlatform::SetRenderOverride(const std::string& target, int what, float a, float b, float c) {
    if (target.empty()) return;
    if (what == 11) { overrides_.erase(target); return; }
    RenderOverride& o = overrides_[target];
    switch (what) {
    case 0: o.alpha = true; o.a = a < 0 ? 0 : (a > 1 ? 1 : a); break;
    case 1: o.tint[0] = o.tint[1] = o.tint[2] = true; o.t[0] = a; o.t[1] = b; o.t[2] = c; break;
    case 2: o.skew = true; o.skewX = a; o.skewY = b; break;
    case 3: o.bend = true; o.bendX = a; o.bendY = b; break;
    case 4: o.z = true; o.zv = a; break;
    case 5: o.blur = true; o.blurV = a < 0 ? 0 : a; break;
    case 6: o.pixelate = true; o.pixV = a < 0 ? 0 : a; break;
    case 7: o.blend = true; o.blendV = (int)a; break;
    case 8: o.effect = true; o.effectV = (int)a; break;
    case 9: o.rot = true; o.rotV = a; break;
    case 10: { int ch = (int)a; if (ch >= 0 && ch < 3) { o.tint[ch] = true; o.t[ch] = b; } else if (ch == 3) { o.alpha = true; o.a = b < 0 ? 0 : (b > 1 ? 1 : b); } break; }
    default: break;
    }
}
bool GamePlatform::GetRenderOverride(const std::string& target, int what, float& a, float& b, float& c) {
    auto it = overrides_.find(target);
    a = b = c = 0;
    if (it == overrides_.end()) return false;
    const RenderOverride& o = it->second;
    switch (what) {
    case 0: a = o.a; return o.alpha;
    case 1: a = o.t[0]; b = o.t[1]; c = o.t[2]; return o.tint[0] || o.tint[1] || o.tint[2];
    case 7: a = (float)o.blendV; return o.blend;
    case 8: a = (float)o.effectV; return o.effect;
    case 9: a = o.rotV; return o.rot;
    default: return false;
    }
}

// res.getCompoSprites: a composite sprite by its name, or an object that is drawn as one
bool GamePlatform::ResGetCompoParts(const std::string& name, std::vector<CompoPartInfo>& out) {
    const CompoSprite* compo = sprites_.FindCompo(name);
    if (!compo) { PhysObject* o = Physics().Find(name); if (o) compo = sprites_.FindCompo(o->sprite); }
    if (!compo) return false;
    for (const CompoPart& p : compo->parts) out.push_back({p.sprite, p.exact ? p.fx : (float)p.x, p.exact ? p.fy : (float)p.y, p.exact ? p.scale : 1.0f, p.exact ? p.angle : 0.0f});
    return true;
}

// v2.4: the graphics settings kept for a target are put on the render state for the draw (the group being drawn, then the composite sprite, then the sprite)
void GamePlatform::ApplyOverride(const RenderOverride& o) {
    if (o.alpha) state_.alpha *= o.a;
    if (o.tint[0]) state_.tintR *= o.t[0];
    if (o.tint[1]) state_.tintG *= o.t[1];
    if (o.tint[2]) state_.tintB *= o.t[2];
    if (o.skew) { state_.skewX = o.skewX; state_.skewY = o.skewY; }
    if (o.bend) { state_.bendX = o.bendX; state_.bendY = o.bendY; }
    if (o.z) state_.z = o.zv;
    if (o.blur) state_.blur = o.blurV;
    if (o.pixelate) state_.pixelate = o.pixV;
    if (o.blend) state_.blend = o.blendV;
    if (o.effect) state_.effect = o.effectV;
    if (o.rot) state_.angle += o.rotV;
}
void GamePlatform::DrawSpriteAt(const SpriteSheet& sheet, const Sprite& spr, float x, float y, int vAnchor, int hAnchor, bool sized, float width, float height) {
    // engine.drawSprite(sprite, object, x, y) -> false | table | nil: called for every sprite the game draws. false leaves it undrawn;
    // a table changes how this one sprite is drawn: dx, dy (pixels), angle (radians, added), sx, sy (multiplied), alpha (multiplied), r, g, b (multiplied),
    // flipX, blend (0-3), effect (0-5), skewX, skewY, bendX, bendY, blur, pixelate, z
    static bool inHook = false;
    if (scripts_ && !inHook) {
        lua_State* L = scripts_->State();
        const int top = lua_gettop(L);
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "engine");
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "drawSprite");
                if (lua_isfunction(L, -1)) {
                    inHook = true;
                    RenderState keepHook = state_;
                    lua_pushstring(L, spr.name.c_str()); lua_pushstring(L, drawTag_.c_str()); lua_pushnumber(L, x); lua_pushnumber(L, y);
                    const int rc = lua_pcall(L, 4, 1, 0);
                    state_ = keepHook;
                    inHook = false;
                    if (rc != 0) std::fprintf(stderr, "engine.drawSprite: %s\n", lua_tostring(L, -1));
                    else if (lua_isboolean(L, -1) && !lua_toboolean(L, -1)) { lua_settop(L, top); return; }
                    else if (lua_istable(L, -1)) {
                        const int it = lua_gettop(L);
                        auto num = [&](const char* k, float& out, bool mul) { lua_getfield(L, it, k); if (lua_type(L, -1) == LUA_TNUMBER) { float v = (float)lua_tonumber(L, -1); out = mul ? out * v : out + v; } lua_pop(L, 1); };
                        auto setn = [&](const char* k, float& out) { lua_getfield(L, it, k); if (lua_type(L, -1) == LUA_TNUMBER) out = (float)lua_tonumber(L, -1); lua_pop(L, 1); };
                        RenderState keep2 = state_;
                        float ddx = 0, ddy = 0; num("dx", ddx, false); num("dy", ddy, false);
                        x += ddx; y += ddy;
                        num("angle", state_.angle, false); num("sx", state_.sx, true); num("sy", state_.sy, true); num("alpha", state_.alpha, true);
                        num("r", state_.tintR, true); num("g", state_.tintG, true); num("b", state_.tintB, true);
                        setn("skewX", state_.skewX); setn("skewY", state_.skewY); setn("bendX", state_.bendX); setn("bendY", state_.bendY);
                        setn("blur", state_.blur); setn("pixelate", state_.pixelate); setn("z", state_.z);
                        float tmp = (float)state_.blend; setn("blend", tmp); state_.blend = (int)tmp;
                        tmp = (float)state_.effect; setn("effect", tmp); state_.effect = (int)tmp;
                        lua_getfield(L, it, "flipX"); if (lua_isboolean(L, -1)) state_.flipX = lua_toboolean(L, -1) != 0; lua_pop(L, 1);
                        lua_settop(L, top);
                        inHook = true;   // the draw below must not call the hook again
                        if (overrides_.empty()) DrawSpriteAtRaw(sheet, spr, x, y, vAnchor, hAnchor, sized, width, height);
                        else {
                            const std::string* keys[3] = {&drawTag_, &compoTag_, &spr.name};
                            for (const std::string* k : keys) { if (k->empty()) continue; auto ov = overrides_.find(*k); if (ov != overrides_.end()) ApplyOverride(ov->second); }
                            DrawSpriteAtRaw(sheet, spr, x, y, vAnchor, hAnchor, sized, width, height);
                        }
                        inHook = false;
                        state_ = keep2;
                        return;
                    }
                }
            }
        }
        lua_settop(L, top);
    }
    if (overrides_.empty()) { DrawSpriteAtRaw(sheet, spr, x, y, vAnchor, hAnchor, sized, width, height); return; }
    RenderState keep = state_;
    const std::string* keys[3] = {&drawTag_, &compoTag_, &spr.name};
    for (const std::string* k : keys) { if (k->empty()) continue; auto it = overrides_.find(*k); if (it != overrides_.end()) ApplyOverride(it->second); }
    DrawSpriteAtRaw(sheet, spr, x, y, vAnchor, hAnchor, sized, width, height);
    state_ = keep;
}

void GamePlatform::DrawSpriteAtRaw(const SpriteSheet& sheet, const Sprite& spr, float x, float y, int vAnchor, int hAnchor, bool sized, float width, float height) {
    const Sprite* sp = &spr;
    const TextureInfo* tex = TextureFor(sheet);
    if (!tex) { if (std::getenv("AB_DRAWDBG")) { static int n = 0; if (n++ < 5) std::fprintf(stderr, "NOTEX %s/%s\n", sheet.directory.c_str(), sheet.image.c_str()); } return; }

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
    // v2.2: a bent sprite is drawn as a grid of 8 x 8 pieces; each vertex is moved by bendX * profile(row) / bendY * profile(column)
    // (tiled blocks take part too: the picture is the mask and the tile texture is cut by the same grid, so tint, blur, bend, pixelation and the blend mode apply)
    const int n = (state_.bendX != 0 || state_.bendY != 0) ? 8 : 1;
    // v2.2 pixelation: the sprite is cut into blocks of state_.pixelate sprite pixels, each block showing the texture at its middle
    int nx = n, ny = n;
    const bool pixelated = state_.pixelate > 1 && w > 0 && h > 0;
    if (pixelated) { nx = (int)std::ceil(w / state_.pixelate); ny = (int)std::ceil(h / state_.pixelate); if (nx > 64) nx = 64; if (ny > 64) ny = 64; if (nx < 1) nx = 1; if (ny < 1) ny = 1; }
    auto profile = [&](float t) {
        float p = t * 8; int i = p >= 8 ? 7 : (int)p; float f = p - i;
        return state_.bendProfile[i] + (state_.bendProfile[i + 1] - state_.bendProfile[i]) * f;
    };
    const float tcos = std::cos(state_.angle), tsin = std::sin(state_.angle);
    auto vertex = [&](int gi, int gj, float* oxy, float* ouv, float* otuv, float uvU, float uvV) {
        float u = (float)gi / nx, v = (float)gj / ny;
        float lx = w * u, ly = h * v;
        if (state_.bendX != 0 || state_.bendY != 0) { lx += state_.bendX * profile(v); ly += state_.bendY * profile(u); }
        Transform(x + lx, y + ly, cx, cy, oxy[0], oxy[1]);
        if (tileTexture_) {
            float tdx = x + lx - cx, tdy = y + ly - cy;
            otuv[0] = (cx + tcos * tdx - tsin * tdy) / tileTexture_->width;
            otuv[1] = (cy + tsin * tdx + tcos * tdy) / tileTexture_->height;
        }
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
                float cxy[8], cuv[8], ctuv[8];
                const int di[4] = {0, 1, 1, 0}, dj[4] = {0, 0, 1, 1};
                for (int k = 0; k < 4; k++) {
                    float uvU = pixelated ? (gi + 0.5f) / nx : (float)(gi + di[k]) / nx;
                    float uvV = pixelated ? (gj + 0.5f) / ny : (float)(gj + dj[k]) / ny;
                    vertex(gi + di[k], gj + dj[k], cxy + k * 2, cuv + k * 2, ctuv + k * 2, uvU, uvV);
                    cxy[k * 2] += offX; cxy[k * 2 + 1] += offY;
                }
                QueuedQuad q = MakeQuad(tex->handle, tapAlpha, state_.tintR, state_.tintG, state_.tintB);
                std::memcpy(q.xy, cxy, sizeof cxy); std::memcpy(q.uv, cuv, sizeof cuv);
                if (tileTexture_) {
                    q.mask = tex->handle; q.tex = tileTexture_->handle;     // DrawMaskedQuad(tile, sprite, ...): the sprite is the mask
                    if (state_.effect >= 1 && state_.effect <= 3) q.tex = VariantTexture(q.tex, state_.effect);
                    std::memcpy(q.tuv, ctuv, sizeof ctuv);
                }
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

static void AddRuntimeSprite(SpriteLibrary& lib, const std::string& name, int w, int h, int pivotX, int pivotY);

void GamePlatform::ResReleaseSpriteSheet(const std::string& path) {
    sprites_.Release(path);
    sprites_.Release("canvas:" + path);          // a sprite made at run time (captureSprite)
    textures_.erase("canvas:/" + path);
}

// captureSprite(name) (Star Wars, the screen transitions): the last frame drawn becomes a sprite of that name
void GamePlatform::ResCaptureSprite(const std::string& name) {
    if (!backend_) return;
    std::vector<unsigned char> raw;
    int w = 0, h = 0;
    if (keepLast_ && !lastFrame_.empty()) { raw = lastFrame_; w = lastW_; h = lastH_; }   // Classic screenwipe: the picture is the last finished frame (this frame is only half drawn)
    else if (!backend_->ReadPixels(raw, w, h)) return;
    if (std::getenv("AB_DRAWDBG")) std::fprintf(stderr, "captureSprite %s keep=%d quads=%d last=%zu w=%d h=%d\n", name.c_str(), (int)keepLast_, quadsThisFrame_, lastFrame_.size(), w, h);
    if (w <= 0 || h <= 0) return;
    Image picture;
    picture.width = w; picture.height = h;
    picture.rgba.assign(raw.begin(), raw.end());
    sprites_.Release("canvas:" + name);
    TextureInfo info; info.handle = backend_->CreateTexture(picture); info.width = w; info.height = h;
    textures_["canvas:/" + name] = info;
    AddRuntimeSprite(sprites_, name, w, h, 0, 0);
}

// setMass(name, density) (Star Wars): every fixture of the body takes the density and the mass is worked out again
void GamePlatform::SetObjectDensity(const std::string& n, float density) {
    if (PhysObject* o = Physics().Find(n)) o->pbody->SetDensity(density);
}

void GamePlatform::SetObjectRestitution(const std::string& n, float r) {
    if (PhysObject* o = Physics().Find(n)) o->pbody->SetRestitution(r);
}

// addParticlesWithMode: a notification particle (mode 3) lives in the menu list and is drawn over everything in screen space
void GamePlatform::AddParticlesMode(const std::string& type, float amount, float x, float y, float w, float h, float angle, int mode) {
    size_t before = particles_.size();
    AddParticles(type, amount, x, y, w, h, angle, false, mode == 3);
    for (size_t i = before; i < particles_.size(); i++) particles_[i].mode = mode;
}

// the three particle layers of the Star Wars scenes: behind the blocks, in front of the birds, and the notifications over the menus
void GamePlatform::DrawParticlesFiltered(int filter) {
    menuParticlesByScript_ = true;
    levelParticlesByScript_ = true;
    particleFilter_ = filter;
    DrawParticlesOf(filter != 3, -1);
    particleFilter_ = 0;
}

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
void GamePlatform::SetTheme(const std::string& themeArg) {
    const char* themeEnv = std::getenv("AB_THEME");     // test aid: draw this theme in every level
    const std::string theme = themeEnv ? std::string(themeEnv) : themeArg;
    if (theme != bgTheme_) { bgRef_.have = false; bgRef_.nH0 = 0; bgTheme_ = theme; }
    ntRef_ = decltype(ntRef_)();   // the ThemeSystem is made again with every level, its reference camera is read again   // 0x435f70: the reference camera and the first screen height are dropped only when the theme changes
    swTheme_ = false; swBg_ = false; newTheme_ = false; ntFg_.clear(); fgParticles_.clear(); bgLayers_.clear(); fgLayers_.clear(); fgSpeed_.clear(); fgScale_.clear(); fgAuto_.clear(); fgOffset_.clear(); fgNamed_.clear(); fgDx_.clear(); fgDy_.clear(); fgHidden_.clear(); themeTexture_.clear();
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
                lua_getfield(L, t, "textureScale"); swBg_ = lua_isnumber(L, -1) != 0; lua_pop(L, 1);
                lua_getfield(L, t, "texture");
                if (lua_isstring(L, -1)) themeTexture_ = lua_tostring(L, -1);
                lua_pop(L, 1);
                bool newThemeSystem = false;   // NEW_THEME_SYSTEM (0x166): the layers are Flash-style, a missing xMult is 0 and not 1 (0x43b8xx layer loader)
                lua_getfield(L, t, "NEW_THEME_SYSTEM"); newThemeSystem = lua_toboolean(L, -1) != 0; lua_pop(L, 1); newTheme_ = newThemeSystem;
                lua_getfield(L, t, "bgLayers");
                if (lua_istable(L, -1)) {
                    int n = (int)lua_objlen(L, -1);
                    for (int i = 1; i <= n; i++) {
                        lua_rawgeti(L, -1, i);
                        if (lua_istable(L, -1)) {
                            BgLayer layer;
                            lua_getfield(L, -1, "sprite");
                            const bool spriteIsList = lua_istable(L, -1);   // Star Wars: "sprite" can be a list of rocks that spawnParameters scatters (asteroid field)
                            if (lua_isstring(L, -1) || spriteIsList) {      // Angry Birds Space: named fields; layers without a sprite only carry the scattered elements, but keep their place in the list
                                layer.space = true;
                                std::vector<std::string> spriteList;
                                if (spriteIsList) {
                                    for (int k = 1; k <= (int)lua_objlen(L, -1); k++) { lua_rawgeti(L, -1, k); if (lua_isstring(L, -1)) spriteList.push_back(lua_tostring(L, -1)); lua_pop(L, 1); }
                                    if (!spriteList.empty()) layer.sprite = spriteList[0];   // 0x43b...: the list is the layer's animation frames; without a timeline the first one stays
                                } else layer.sprite = lua_tostring(L, -1);
                                layer.empty = layer.sprite.empty();
                                int lt = lua_gettop(L) - 1;
                                layer.zDist = number(lt, "zDistance", 0);
                                layer.xMult = number(lt, "xMult", newThemeSystem ? 0.0f : 1.0f); layer.yMult = number(lt, "yMult", 1);
                                layer.angleMult = number(lt, "angleMult", 0);
                                layer.speed = number(lt, "parallaxSpeed", 1);
                                layer.scale = number(lt, "scale", 1);
                                layer.posX = number(lt, "posX", 0); layer.posY = number(lt, "posY", 0);
                                layer.offX = number(lt, "offsetX", 0); layer.offY = number(lt, "offsetY", 0); layer.baseOffY = layer.offY; layer.relY = number(lt, "relativeY", 3.4028235e38f);
                                layer.scaleSpeed = number(lt, "scaleSpeed", 1);
                                layer.velX = number(lt, "velX", 0); layer.velY = number(lt, "velY", 0);
                                layer.scaleX = number(lt, "scaleX", layer.scale); layer.scaleY = number(lt, "scaleY", layer.scale);
                                lua_getfield(L, lt, "flags");
                                if (lua_istable(L, -1)) {   // the flag strings of the layer loader (0x43c0xx)
                                    layer.hasFlags = true;
                                    for (int k = 1; k <= (int)lua_objlen(L, -1); k++) {
                                        lua_rawgeti(L, -1, k);
                                        if (lua_isstring(L, -1)) {
                                            std::string fl = lua_tostring(L, -1);
                                            if (fl == "OVERSTRETCH_ANCHOR_V") layer.flags |= 1; else if (fl == "V_REPEAT") layer.flags |= 2; else if (fl == "H_REPEAT") layer.flags |= 4;
                                            else if (fl == "REFRESH_ANIMATION_TIMELINE") layer.flags |= 8; else if (fl == "REFRESH_ANIMATION_COORDINATES") layer.flags |= 0x10; else if (fl == "ANCHOR_V") layer.flags |= 0x20;
                                            else if (fl == "PREVENT_STRETCH_SCALE") layer.flags |= 0x80; else if (fl == "REPEAT_LEFT_ONLY") layer.flags |= 0x100; else if (fl == "REPEAT_RIGHT_ONLY") layer.flags |= 0x200;
                                        }
                                        lua_pop(L, 1);
                                    }
                                }
                                lua_pop(L, 1);
                                // The scattered elements of the layer (themes.lua "elements": amount, scale, variation, w, h, velX, velY, sprites). The retail exe
                                // creates them itself; scripts that scatter their own (they define populateWithRandomThemeSprites*) are left alone.
                                lua_getfield(L, lt, "elements");
                                if (lua_istable(L, -1)) { lua_pushnil(L); layer.elemTable = lua_next(L, -2) != 0; if (layer.elemTable) lua_pop(L, 2); }   // 0xa8..0xac: the layer's own element list
                                if (lua_istable(L, -1) && !HasScriptFunction("populateWithRandomThemeSpritesNew") && !HasScriptFunction("populateWithRandomThemeSprites")) {
                                    int et = lua_gettop(L);
                                    int amount = (int)number(et, "amount", 0);
                                    std::vector<std::string> names;
                                    lua_getfield(L, et, "sprites");
                                    if (lua_istable(L, -1)) for (int k = 1; k <= (int)lua_objlen(L, -1); k++) { lua_rawgeti(L, -1, k); if (lua_isstring(L, -1)) names.push_back(lua_tostring(L, -1)); lua_pop(L, 1); }
                                    lua_pop(L, 1);
                                    if (amount > 0 && !names.empty()) {
                                        static std::mt19937 rng(12345);
                                        auto rnd = [&]() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); };
                                        float ew = number(et, "w", 0.1f) * 768.0f, eh = number(et, "h", 0.1f) * 768.0f;
                                        float esc = number(et, "scale", 1), var = number(et, "variation", 0), evx = number(et, "velX", 0), evy = number(et, "velY", 0);
                                        bool rot = false;
                                        lua_getfield(L, et, "randomRotation"); rot = lua_toboolean(L, -1) != 0; lua_pop(L, 1);
                                        for (int k = 0; k < amount; k++) {
                                            float sc = layer.scale * esc + (0.5f - rnd()) * 2.0f * var;
                                            const std::string& sn = names[(size_t)(rnd() * names.size()) % names.size()];
                                            ThemeSprite t{sn + std::to_string(k + 1), sn, (rnd() * 2 - 1) * ew, (rnd() * 2 - 1) * eh, sc, sc, rot ? rnd() * 6.2831853f : 0.0f, 0, (int)bgLayers_.size()};
                                            t.velX = evx; t.velY = evy; t.wrapW = ew; t.wrapH = eh;
                                            themeSprites_.push_back(t);
                                        }
                                    }
                                }
                                lua_pop(L, 1);
                                // spawnParameters (0x44adb0): kept on the layer, the rocks are scattered by the first draw (0x44a800 needs the level's limits)
                                lua_getfield(L, lt, "spawnParameters");
                                if (lua_istable(L, -1) && !spriteList.empty()) {
                                    int spt = lua_gettop(L);
                                    layer.spawnList = spriteList;
                                    layer.spawnAmount = (int)number(spt, "amount", 0);
                                    layer.spawnXVar = number(spt, "xSpeedVariance", 0);
                                    layer.spawnVelX = number(lt, "velX", 0); layer.spawnVelY = number(lt, "velY", 0);
                                    lua_getfield(L, spt, "area");
                                    if (lua_istable(L, -1)) { int at = lua_gettop(L); layer.spawnSX = number(at, "screenX", 0); layer.spawnSY = number(at, "screenY", 0); layer.spawnSW = number(at, "screenW", 0); layer.spawnSH = number(at, "screenH", 0); layer.spawnWW = number(at, "worldW", 0); layer.spawnWH = number(at, "worldH", 0); }
                                    lua_pop(L, 1);
                                }
                                lua_pop(L, 1);
                                lua_getfield(L, lt, "bLoop");
                                layer.looping = !lua_isboolean(L, -1) || lua_toboolean(L, -1);
                                lua_pop(L, 1);
                                bgLayers_.push_back(layer);
                                lua_pop(L, 2);
                                continue;
                            }
                            lua_pop(L, 1);
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
                lua_getfield(L, t, "NEW_THEME_SYSTEM_FOREGROUND"); swTheme_ = lua_toboolean(L, -1) != 0; lua_pop(L, 1);
                lua_getfield(L, t, "fgLayers");
                if (lua_istable(L, -1)) {
                    int n = (int)lua_objlen(L, -1);
                    for (int i = 1; i <= n; i++) {
                        lua_rawgeti(L, -1, i);
                        if (lua_istable(L, -1)) {
                            if ((scripts_ && scripts_->IsStarWars()) ? swTheme_ : newTheme_) {   // NEW_THEME_SYSTEM(_FOREGROUND): the foreground layers go through the same routine as the background (0x44c1b0, vector 0x264)
                                lua_getfield(L, -1, "sprite");
                                if (lua_isstring(L, -1)) {
                                    int lt = lua_gettop(L) - 1;
                                    BgLayer fl; fl.space = true; fl.sprite = lua_tostring(L, -1);
                                    fl.zDist = number(lt, "zDistance", 0); fl.xMult = number(lt, "xMult", 0); fl.yMult = number(lt, "yMult", 1);
                                    fl.scale = number(lt, "scale", 1); fl.scaleX = number(lt, "scaleX", fl.scale); fl.scaleY = number(lt, "scaleY", fl.scale);
                                    fl.posX = number(lt, "posX", 0); fl.posY = number(lt, "posY", 0); fl.offX = number(lt, "offsetX", 0); fl.offY = number(lt, "offsetY", 0); fl.baseOffY = fl.offY; fl.relY = number(lt, "relativeY", 3.4028235e38f);
                                    lua_getfield(L, lt, "flags");
                                    if (lua_istable(L, -1)) {
                                        fl.hasFlags = true;
                                        for (int k = 1; k <= (int)lua_objlen(L, -1); k++) {
                                            lua_rawgeti(L, -1, k);
                                            if (lua_isstring(L, -1)) {
                                                std::string fs = lua_tostring(L, -1);
                                                if (fs == "OVERSTRETCH_ANCHOR_V") fl.flags |= 1; else if (fs == "V_REPEAT") fl.flags |= 2; else if (fs == "H_REPEAT") fl.flags |= 4;
                                                else if (fs == "ANCHOR_V") fl.flags |= 0x20; else if (fs == "PREVENT_STRETCH_SCALE") fl.flags |= 0x80; else if (fs == "REPEAT_LEFT_ONLY") fl.flags |= 0x100; else if (fs == "REPEAT_RIGHT_ONLY") fl.flags |= 0x200;
                                            }
                                            lua_pop(L, 1);
                                        }
                                    }
                                    lua_pop(L, 1);
                                    lua_getfield(L, lt, "downFillColor");
                                    if (lua_istable(L, -1)) { fl.downFill = true; fl.downR = number(lua_gettop(L), "r", 0); fl.downG = number(lua_gettop(L), "g", 0); fl.downB = number(lua_gettop(L), "b", 0); }
                                    lua_pop(L, 1);
                                    lua_getfield(L, lt, "upFillColor");
                                    if (lua_istable(L, -1)) { fl.upFill = true; fl.upR = number(lua_gettop(L), "r", 0); fl.upG = number(lua_gettop(L), "g", 0); fl.upB = number(lua_gettop(L), "b", 0); }
                                    lua_pop(L, 1);
                                    ntFg_.push_back(fl);
                                }
                                lua_pop(L, 1);
                            }
                            // 0x44b9xx: with NEW_THEME_SYSTEM_FOREGROUND the foreground layers only go to the new-system vector (0x264) and the positional
                            // loop (sprite sheet, sprite, speed, scale, auto-scroll) is skipped; without it only the positional loop runs
                            if (scripts_ && scripts_->IsStarWars() && swTheme_) { lua_pop(L, 1); continue; }
                            // Star Wars' newer themes (Cloud City) name the fields: sprite, scale, zDistance, velX, offsetY, downFillColor
                            lua_getfield(L, -1, "particles");
                            if (lua_isstring(L, -1)) { std::string pn = lua_tostring(L, -1); lua_pop(L, 1); float idf = 0; GetNumber(L, lua_gettop(L), "spawnerId", idf); fgParticles_[(int)idf] = pn; lua_getfield(L, -1, "sprite"); }
                            else { lua_pop(L, 1); lua_getfield(L, -1, "sprite"); }
                            bool hasFill = false;
                            if (lua_isstring(L, -1)) { lua_getfield(L, -2, "downFillColor"); hasFill = lua_istable(L, -1); lua_pop(L, 1); }   // Space's own named layers are not drawn
                            if (lua_isstring(L, -1) && hasFill) {
                                int ft = lua_gettop(L) - 1;
                                fgLayers_.push_back(lua_tostring(L, -1));
                                float sc = 1.5f, zd = 0, vx = 0, oy = 0;
                                GetNumber(L, ft, "scale", sc); GetNumber(L, ft, "zDistance", zd); GetNumber(L, ft, "velX", vx); GetNumber(L, ft, "offsetY", oy);
                                fgSpeed_.push_back(1.0f - zd);
                                fgScale_.push_back(sc);
                                fgAuto_.push_back(-vx / 60.0f);
                                fgOffset_.push_back(0.0f);
                                FgNamed nm; nm.named = true; nm.offY = oy;
                                lua_getfield(L, ft, "downFillColor");
                                if (lua_istable(L, -1)) { nm.fill = true; GetNumber(L, lua_gettop(L), "r", nm.r); GetNumber(L, lua_gettop(L), "g", nm.g); GetNumber(L, lua_gettop(L), "b", nm.b); }
                                lua_pop(L, 1);
                                fgNamed_.resize(fgLayers_.size() - 1); fgNamed_.push_back(nm);
                            }
                            lua_pop(L, 1);
                            lua_rawgeti(L, -1, 2);
                            if (lua_isstring(L, -1)) {
                                fgLayers_.push_back(lua_tostring(L, -1));
                                lua_pop(L, 1);
                                lua_rawgeti(L, -1, 3);
                                fgSpeed_.push_back(lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 1.0f);
                                lua_pop(L, 1);
                                lua_rawgeti(L, -1, 4);
                                fgScale_.push_back(lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : (scripts_ && scripts_->IsStarWars() ? 1.0f : 1.5f));   // 0x44b9xx: index 4 defaults to 1
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
    lua_State* TL = scripts_ ? scripts_->State() : nullptr;
    float step = zoom_ >= 1.0f ? Tune(TL, "zoom.stepIn", 0.2f) : Tune(TL, "zoom.stepOut", 0.1f);
    zoomPrev_ = zoom_;
    if (zoomT_ > -1.0f && zoom_ > 0.6f && zoom_ < zoomMax_) {
        zoomTarget_ = step * 0.5f * (float)notches + zoomTarget_;
        zoomDur_ = 1.0f - zoomT_;
    } else {
        zoomTarget_ = (float)notches * step + zoom_;
        zoomDur_ = Tune(TL, "zoom.duration", 0.5f);
    }
    zoomStart_ = zoom_;
    zoomT_ = 0.0f;
}

// 0x44b200: every layer drifts by its velocity times (1 - zDistance); one that has left the screen in its direction of travel jumps back by whole sprite widths
void GamePlatform::UpdateNewThemeLayers(std::vector<BgLayer>& layers, float dt) {
    if (!scripts_ || !ntRef_.have || dt <= 0) return;
    lua_State* L = scripts_->State();
    int top0 = lua_gettop(L);
    float camX = 0, camY = 0, lim;
    const float ws = worldScale_ > 0 ? worldScale_ : 1.0f, S = ntRef_.s88 > 0 ? ntRef_.s88 : 1.0f;
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    const int env = lua_gettop(L);
    lua_getfield(L, env, "screen");
    if (lua_istable(L, -1)) { GetNumber(L, lua_gettop(L), "x", camX); GetNumber(L, lua_gettop(L), "y", camY); }
    lua_pop(L, 1);
    float left = camX, right = camX + (float)screenWidth_ / ws, topW = camY, bottom = camY + (float)screenHeight_ / ws;
    if (GetNumber(L, env, "leftLimitWorld", lim) && lim < left) left = lim;
    if (GetNumber(L, env, "rightLimitWorld", lim) && lim > right) right = lim;
    if (GetNumber(L, env, "topLimitWorld", lim) && lim < topW) topW = lim;
    if (GetNumber(L, env, "bottomLimitWorld", lim) && lim > bottom) bottom = lim;
    lua_settop(L, top0);
    const float f4 = ws / S;
    for (BgLayer& layer : layers) {
        float k = 1.0f - layer.zDist;
        layer.posX += dt * layer.velX * k; layer.posY += dt * layer.velY * k;
        if ((layer.velX == 0 && layer.velY == 0) || layer.lastW <= 0 || layer.lastH <= 0) continue;
        float f = layer.zDist + f4 * k;
        float halfW = 0.5f * f * layer.scaleX * layer.lastW / ws, halfH = 0.5f * f * layer.scaleY * layer.lastH / ws;
        if (halfW <= 0 || halfH <= 0) continue;
        float shiftX = layer.scaleX * layer.lastW * (1.0f + std::trunc((right - left) / (layer.scaleX * layer.lastW) * S));   // 0x44b200: whole copies of the layer width over the box, times the reference scale
        float shiftY = layer.scaleY * layer.lastH * (1.0f + std::trunc((bottom - topW) / (layer.scaleY * layer.lastH) * S));
        if (layer.lastOutX - halfW > right && layer.velX > 0) layer.posX -= shiftX;
        else if (left > layer.lastOutX + halfW && layer.velX < 0) layer.posX += shiftX;
        else if (topW > layer.lastOutY + halfH && layer.velY < 0) layer.posY += shiftY;
        else if (bottom < layer.lastOutY - halfH && layer.velY > 0) layer.posY -= shiftY;
    }
}

// NEW_THEME_SYSTEM draw (Star Wars; 0x44c1b0 with 0x44a030 size, 0x44a1a0 position, 0x44a320 draw, 0x44bfa0 / 0x44bdf0 repeats, 0x44b820 reference camera).
// A layer is placed in world coordinates and goes through the camera: screen = (world - camera) * worldScale. zDistance z is 1 for a layer that stays on the
// screen and 0 for one that sticks to the world. No reference screen size is used.
void GamePlatform::DrawNewThemeLayers(std::vector<BgLayer>& layers, int layerBase) {
    if (!scripts_) return;
    lua_State* L = scripts_->State();
    const float sw = (float)screenWidth_, sh = (float)screenHeight_;
    float camX = 0, camY = 0;
    const float ws = worldScale_ > 0 ? worldScale_ : 1.0f;
    int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    const int env = lua_gettop(L);
    lua_getfield(L, env, "screen");
    if (lua_istable(L, -1)) { GetNumber(L, lua_gettop(L), "x", camX); GetNumber(L, lua_gettop(L), "y", camY); }
    lua_pop(L, 1);
    auto envBool = [&](const char* k) { lua_getfield(L, env, k); bool b = lua_toboolean(L, -1) != 0; lua_pop(L, 1); return b; };
    // 0x44b820, first call: the reference camera is the level's castleCameraData for this device (px, py, sx); g_useLowerCameraAsThemeReferencePoint takes
    // birdCameraData instead; g_useZeroAsThemeReferencePointX / Y zero it. Without level data the first frame's camera stands in.
    // until the level's own camera data has been seen (it is loaded after the first frames of a level) the camera of the moment stands in; the search stops after 300 frames
    if (!ntRef_.have || (!ntRef_.fromLevel && ntRef_.tries < 300)) {
        ntRef_.have = true; ntRef_.tries++; ntRef_.fromLevel = false; ntRef_.refX = camX; ntRef_.refY = camY; ntRef_.s88 = ws;
        std::string dm;
        lua_getfield(L, env, "deviceModel"); if (lua_isstring(L, -1)) dm = lua_tostring(L, -1); lua_pop(L, 1);
        lua_getfield(L, env, "objects");
        if (lua_istable(L, -1)) {
            int ob = lua_gettop(L);
            auto readCam = [&](const char* tbl) {
                lua_getfield(L, ob, tbl);
                if (lua_istable(L, -1)) {
                    int tt = lua_gettop(L);
                    // the entry of this device; a level saved for other devices only (the retail exe has one for its own) takes the first one with a camera
                    lua_getfield(L, tt, dm.c_str());
                    if (!lua_istable(L, -1)) {
                        lua_pop(L, 1);
                        static const char* alt[] = {"windows", "pc", "PC", "mac", "ipad", "iphone"};
                        bool found = false;
                        for (const char* a : alt) { lua_getfield(L, tt, a); if (lua_istable(L, -1)) { found = true; break; } lua_pop(L, 1); }
                        if (!found) {
                            lua_pushnil(L);
                            while (lua_next(L, tt)) { if (lua_istable(L, -1)) { found = true; break; } lua_pop(L, 1); }
                            if (!found) lua_pushnil(L);
                        }
                    }
                    if (lua_istable(L, -1)) {
                        int ct = lua_gettop(L); float px, py, sx;
                        if (GetNumber(L, ct, "px", px)) ntRef_.refX = px;
                        if (GetNumber(L, ct, "py", py)) ntRef_.refY = py;
                        if (GetNumber(L, ct, "sx", sx) && sx > 0) ntRef_.s88 = sx;
                        ntRef_.fromLevel = true;
                    }
                    lua_pop(L, 1);
                }
                lua_pop(L, 1);
            };
            readCam("castleCameraData");
            if (envBool("g_useLowerCameraAsThemeReferencePoint")) readCam("birdCameraData");
        }
        lua_pop(L, 1);
        if (envBool("g_useZeroAsThemeReferencePointX")) ntRef_.refX = 0;
        if (envBool("g_useZeroAsThemeReferencePointY")) ntRef_.refY = 0;
        if (std::getenv("AB_DRAWDBG") && (ntRef_.fromLevel || ntRef_.tries == 1 || ntRef_.tries == 300)) std::fprintf(stderr, "NTREF dm=%s fromLevel=%d ref=%g,%g s88=%g cam=%g,%g ws=%g screen=%gx%g\n", dm.c_str(), (int)ntRef_.fromLevel, ntRef_.refX, ntRef_.refY, ntRef_.s88, camX, camY, ws, sw, sh);
    }
    const float S = ntRef_.s88 > 0 ? ntRef_.s88 : 1.0f;
    // the level's limits (leftLimitWorld ...) widen the box the rocks are scattered over (0x44a0c0)
    float left = camX, right = camX + sw / ws, topW = camY, bottom = camY + sh / ws, lim;
    if (GetNumber(L, env, "leftLimitWorld", lim) && lim < left) left = lim;
    if (GetNumber(L, env, "rightLimitWorld", lim) && lim > right) right = lim;
    if (GetNumber(L, env, "topLimitWorld", lim) && lim < topW) topW = lim;
    if (GetNumber(L, env, "bottomLimitWorld", lim) && lim > bottom) bottom = lim;
    lua_settop(L, top);
    const float f4 = ws / S;
    // 0x44c1b0: the foreground (mode 0) takes the world y of the screen's top edge as its reference y every frame, so it does not move vertically with the camera
    const float refYuse = (&layers == &ntFg_) ? camY : ntRef_.refY;
    const float stx = camX - ntRef_.refX, sty = camY - refYuse;
    for (size_t i = 0; i < layers.size(); i++) {
        BgLayer& layer = layers[i];
        if (layer.hidden) continue;
        const float z = layer.zDist;
        // 0x44adb0 + 0x44a800: a layer with spawnParameters puts its rock on a random spot of the screen area (screenX/Y/W/H) and then moves it by
        // (random - 0.5) * worldW / worldH of the level's box; "amount" and "xSpeedVariance" are read by the exe but never used
        if (!layer.spawnList.empty() && !layer.spawned) {
            layer.spawned = true;
            auto rnd = []() { return (float)std::rand() / (float)RAND_MAX; };
            layer.offX = layer.spawnSX - layer.spawnSW * 0.5f + layer.spawnSW * rnd();
            layer.offY = layer.spawnSY - layer.spawnSH * 0.5f + layer.spawnSH * rnd();
            if (layer.spawnWW != 0) layer.offX -= (rnd() - 0.5f) * layer.spawnWW * (right - left);
            if (layer.spawnWH != 0) layer.offY -= (rnd() - 0.5f) * layer.spawnWH * (bottom - topW);
        }
        // 0x44a030: the on-screen size factor
        const float f = z + f4 * (1.0f - z);
        // one instance of a sprite whose pivot sits at (posX, posY) in the layer's own coordinates (0x44a1a0)
        auto place = [&](float w, float h, float pivX, float pivY, float posX, float posY, int flags, float& cx, float& cy) {
            float p5 = ((w * 0.5f - pivX) + posX) / S, p6 = ((h * 0.5f - pivY) + posY) / S;
            float shX = stx * z + layer.xMult * stx, shY = sty * z;
            if (flags & 0x40) shX = stx;
            if (flags & 0x20) shY = sty;
            float ox = shX + ntRef_.refX + z * (p5 / f4) + p5 * (1.0f - z);
            float oy = shY + refYuse + p6 * (1.0f - z) + z * (p6 / f4);
            cx = (ox - camX) * ws; cy = (oy - camY) * ws;
        };
        auto drawAt = [&](const std::string& name, float cx, float cy, float szx, float szy) {
            state_ = RenderState();
            state_.sx = szx; state_.sy = szy; state_.tx = (cx + layer.dx) / szx; state_.ty = (cy + layer.dy) / szy;
            ResDrawSprite(name, 0, 0, 1, 1, false, 0, 0);
        };
        const Sprite* sp = layer.empty ? nullptr : sprites_.Find(layer.sprite);
        const CompoSprite* compo = (!sp && !layer.empty) ? sprites_.FindCompo(layer.sprite) : nullptr;
        int cw = 0, ch = 0, cox = 0, coy = 0;
        if (compo) sprites_.CompoExtent(*compo, cw, ch, cox, coy);
        const float w = sp ? (float)sp->w : (float)cw, h = sp ? (float)sp->h : (float)ch;
        const float pivX = sp ? (float)sp->pivotX : (float)cox, pivY = sp ? (float)sp->pivotY : (float)coy;
        const float szx = f * layer.scaleX, szy = f * layer.scaleY;
        float cx = 0, cy = 0;
        // 0x44b820 (first call): a layer with relativeY gets its y offset (+0x7c) set to (H * relativeY - H / 2) / ST4c, replacing its offsetY
        const float offYeff = layer.relY < 3.0e38f ? ((float)screenHeight_ * layer.relY - (float)screenHeight_ * 0.5f) : layer.offY;
        place(w, h, pivX, pivY, layer.posX + layer.offX, layer.posY + offYeff, layer.flags, cx, cy);
        layer.lastOutX = camX + cx / ws; layer.lastOutY = camY + cy / ws; layer.lastW = w; layer.lastH = h;
        if (std::getenv("AB_DRAWDBG")) { static int dn = 0; if (dn++ < 60) std::fprintf(stderr, "NTL %zu %s w=%g h=%g piv=%g,%g z=%g f=%g size=%g,%g pos=%g,%g flags=%x -> screen %g,%g cam=%g,%g ws=%g\n", i, layer.sprite.c_str(), w, h, pivX, pivY, z, f, szx, szy, layer.posX + layer.offX, layer.posY + layer.offY, layer.flags, cx, cy, camX, camY, ws); }
        if ((sp || compo) && szx > 0 && szy > 0 && w > 0 && h > 0) {   // 0x44c1b0: the colour above / below the layer's edge (0x449ea0)
            if (layer.upFill) DrawRect(layer.upR / 255.0f, layer.upG / 255.0f, layer.upB / 255.0f, 1, 0, 0, sw, cy - h * szy * 0.5f + 3.0f, false);
            if (layer.downFill) { float y0 = cy + h * szy * 0.5f - 3.0f; DrawRect(layer.downR / 255.0f, layer.downG / 255.0f, layer.downB / 255.0f, 1, 0, y0, sw, sh + 1.0f, false); }
            const float stepX = w * szx, stepY = h * szy;
            auto drawVertical = [&](float x, float y) {      // 0x44bdf0: V_REPEAT
                if (!(layer.flags & 2)) return;
                float yy = y; for (int n = 0; n < 64 && 0.0f < yy + stepY * 0.5f; n++) { yy -= stepY; drawAt(layer.sprite, x, yy, szx, szy); }
                yy = y; for (int n = 0; n < 64 && yy - stepY * 0.5f < sh; n++) { yy += stepY; drawAt(layer.sprite, x, yy, szx, szy); }
            };
            drawAt(layer.sprite, cx, cy, szx, szy);
            // 0x44bfa0: H_REPEAT, REPEAT_LEFT_ONLY / REPEAT_RIGHT_ONLY
            if ((layer.flags & 4) || (layer.flags & 0x300)) {
                float x = cx;
                if (!(layer.flags & 0x100)) for (int n = 0; n < 64 && x - stepX * 0.5f < sw; n++) { x += stepX; drawAt(layer.sprite, x, cy, szx, szy); drawVertical(x, cy); }
                x = cx;
                if (!(layer.flags & 0x200)) for (int n = 0; n < 64 && 0.0f < x + stepX * 0.5f; n++) { x -= stepX; drawAt(layer.sprite, x, cy, szx, szy); drawVertical(x, cy); }
            }
            drawVertical(cx, cy);
        }
        // the elements of the layer: placed in the layer's own coordinates around its origin
        for (const ThemeSprite& t : themeSprites_) {
            if (t.layer != (int)i + layerBase) continue;
            const Sprite* ep = sprites_.Find(t.sprite);
            if (!ep) continue;
            float ex = 0, ey = 0;
            place((float)ep->w, (float)ep->h, (float)ep->pivotX, (float)ep->pivotY, t.x * (t.flipped ? -1.0f : 1.0f) * 20.0f + layer.posX + layer.offX, t.y * 20.0f + layer.posY + layer.offY, layer.flags, ex, ey);
            float ezx = f * layer.scaleX * t.scaleX, ezy = f * layer.scaleY * t.scaleY;
            if (ezx <= 0 || ezy <= 0) continue;
            drawAt(t.sprite, ex, ey, ezx, ezy);
        }
    }
    state_ = RenderState();
}

void GamePlatform::DrawNewThemeBackground() { DrawNewThemeLayers(bgLayers_, 0); }

// Angry Birds Space drawBackgroundNative (0x467f70). Sizes are in units of 1024x768 screens: nW = width / 1024, nH = height / 768. A
// layer is scaled by its own scale times the larger screen unit (a looping layer: the larger of the first and the current height),
// plus (zDistance + the zoom since the first frame) * scaleSpeed; the camera's move since the first frame, times zDistance and xMult / yMult,
// shifts it, and the angle follows the horizontal move times angleMult.
void GamePlatform::DrawSpaceBackground() {
    if (newTheme_) { DrawNewThemeBackground(); return; }
    float sw = (float)screenWidth_, sh = (float)screenHeight_;
    float nW = sw / 1024.0f, nH = sh / 768.0f;
    float camX = 0, camY = 0;
    if (scripts_) {
        lua_State* L = scripts_->State();
        int top = lua_gettop(L);
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        lua_getfield(L, -1, "screen");
        if (lua_istable(L, -1)) { GetNumber(L, lua_gettop(L), "x", camX); GetNumber(L, lua_gettop(L), "y", camY); }
        lua_settop(L, top);
    }
    if (bgRef_.nH0 <= 0) bgRef_.nH0 = nH;
    if (!bgRef_.have) { bgRef_.have = true; bgRef_.camX = camX; bgRef_.camY = camY; bgRef_.ws = worldScale_; }
    float dx = camX - bgRef_.camX, dy = camY - bgRef_.camY, dws = worldScale_ - bgRef_.ws;
    for (size_t i = 0; i < bgLayers_.size(); i++) {
        BgLayer& layer = bgLayers_[i];
        if (layer.hidden) continue;
        float dxz = dx * layer.zDist, dyz = dy * layer.zDist;
        layer.angle = dxz * 0.05f * layer.angleMult;
        float ext = layer.looping ? std::max(bgRef_.nH0, nH) : std::max(nH, nW);
        static const float swK = std::getenv("AB_BGK") ? (float)std::atof(std::getenv("AB_BGK")) : 0.7f;
        float kk = (swTheme_ || swBg_) ? swK : 1.0f;
        float E = layer.scale * ext * kk;
        float S = (layer.zDist + dws) * layer.scaleSpeed * kk;
        float At = S + E, X = 2 * S + E;
        const Sprite* sp = layer.empty ? nullptr : sprites_.Find(layer.sprite);
        // a layer can be a composite sprite (THEME_1_BG_1 is the base with its stars, the long hills are a stack of tiles): its extent and origin stand for size and pivot
        const CompoSprite* compo = (!sp && !layer.empty) ? sprites_.FindCompo(layer.sprite) : nullptr;
        int cw = 0, ch = 0, cox = 0, coy = 0;
        if (compo) sprites_.CompoExtent(*compo, cw, ch, cox, coy);
        bool drawable = sp || compo;
        float w = sp ? (float)sp->w : compo ? (float)cw : 1024.0f / layer.scale, h = sp ? (float)sp->h : compo ? (float)ch : 1024.0f / layer.scale;
        float pivX = sp ? (float)sp->pivotX : compo ? (float)cox : 0, pivY = sp ? (float)sp->pivotY : compo ? (float)coy : 0;
        if (std::getenv("AB_DRAWDBG")) std::fprintf(stderr, "SPBG %zu %s w=%g h=%g piv=%g,%g E=%g S=%g At=%g loop=%d pos=%g,%g off=%g,%g xm=%g\n", i, layer.sprite.c_str(), w, h, pivX, pivY, E, S, At, (int)layer.looping, layer.posX, layer.posY, layer.offX, layer.offY, layer.xMult);
        if (std::getenv("AB_DRAWDBG")) { static int dn = 0; if (dn++ < 40) std::fprintf(stderr, "SPBG2 %zu cam=%g,%g ref=%g,%g ws=%g refws=%g nW=%g nH=%g nH0=%g dxz=%g dyz=%g offY=%g baseOffY=%g compo=%d sw=%d\n", i, camX, camY, bgRef_.camX, bgRef_.camY, worldScale_, bgRef_.ws, nW, nH, bgRef_.nH0, dxz, dyz, layer.offY, layer.baseOffY, compo ? 1 : 0, swTheme_ ? 1 : 0); }
        if (drawable && At > 0) {
            state_ = RenderState();
            state_.pivotX = pivX; state_.pivotY = pivY;
            state_.angle = layer.angle;
            state_.sx = state_.sy = At;
            if (!layer.looping) {
                float v68 = 2 * layer.posX * At - w * X, v64 = X * h + 2 * layer.posY * At;
                state_.tx = 0.5f * (v68 - layer.xMult * dxz) / At + layer.dx / At;
                state_.ty = 0.5f * (v64 - layer.yMult * dyz) / At + layer.dy / At;
                ResDrawSprite(layer.sprite, layer.offX * nW / At, layer.offY * nH / At, 4, 3, false, 0, 0);
            } else {
                // a row of copies one sprite width apart (32 at most), starting 4.5 widths before the first
                float v68 = 2 * layer.posX * At - w * X, v64 = X * h + 2 * layer.posY * At;
                float x0 = 0.5f * (v68 - 8.0f * w * At - layer.xMult * dxz) / At + layer.dx / At;
                state_.ty = 0.5f * (v64 - layer.yMult * dyz) / At + layer.dy / At;
                float stopAt = 8.0f * w * At + sw / (layer.scale * worldScale_) + pivX;
                float cur = v68 - 8.0f * w * At;
                for (int n = 0; n < 32 && cur < stopAt; n++, cur += 2 * w * At) {
                    state_.tx = x0 + n * w;
                    ResDrawSprite(layer.sprite, layer.offX * nW / At, layer.offY * nH / At, 4, 3, false, 0, 0);
                }
            }
        }
        // the elements scattered on this layer (createThemeSprite)
        for (const ThemeSprite& t : themeSprites_) {
            if (t.layer != (int)i) continue;
            const Sprite* ep = sprites_.Find(t.sprite);
            if (!ep) continue;
            float sxS = t.scaleX * E + S, syS = t.scaleY * E + S;
            if (sxS <= 0 || syS <= 0) continue;
            state_ = RenderState();
            state_.pivotX = (float)ep->pivotX; state_.pivotY = (float)ep->pivotY;
            state_.angle = t.angle + layer.angle;
            state_.sx = sxS; state_.sy = syS;
            state_.tx = 0.5f * (-w * E - layer.xMult * dxz) / sxS + layer.dx / sxS;
            state_.ty = 0.5f * (h * E - layer.yMult * dyz) / syS + layer.dy / syS;
            ResDrawSprite(t.sprite, t.x * (t.flipped ? -1.0f : 1.0f) * 20.0f / sxS, t.y * 20.0f / syS, 4, 3, false, 0, 0);
        }
    }
    state_ = RenderState();
}

// setThemeOffsetY (0x466e40): every layer of the theme moves to its own offsetY plus v; a layer without elements takes v scaled by 768 / screen height first
void GamePlatform::SetThemeOffsetY(float v) {
    if (std::getenv("AB_DRAWDBG")) std::fprintf(stderr, "SETTHEMEOFFSETY v=%g screen=%dx%d\n", v, screenWidth_, screenHeight_);
    for (size_t i = 0; i < bgLayers_.size(); i++) {
        BgLayer& layer = bgLayers_[i];
        bool hasElements = layer.elemTable;
        if (!newTheme_) for (const ThemeSprite& t : themeSprites_) if (t.layer == (int)i) { hasElements = true; break; }
        float add = hasElements || screenHeight_ <= 0 ? v : v * 768.0f / (float)screenHeight_;
        layer.offY = layer.baseOffY + add;
        if (newTheme_) layer.posY = 0;     // 0x412760 writes the layer's +0x7c (posY) itself
        layer.relY = 3.4028235e38f;        // and it replaces what relativeY had put there
    }
}

void GamePlatform::CreateThemeSpriteSpace(const std::string& name, const std::string& sprite, float x, float y, float scaleX, float scaleY, float angle, int layer, float scaleSpeed, bool flipped, float velX, float velY) {
    ThemeSprite t{name, sprite, x, y, scaleX, scaleY, angle, 0, layer};
    t.angleVel = 0; t.flipped = flipped; t.velX = velX; t.velY = velY;
    t.scaleSpeed = scaleSpeed;
    themeSprites_.push_back(t);
}

// drawBackgroundNative: every looping layer is a row of copies of its sprite, shifted by the camera
// times the layer's parallax speed.
bool GamePlatform::GetThemeLayer(int which, int i, ThemeLayerInfo& o) {
    if (which == 2) {   // the old-style foreground strip (Classic, Seasons, Rio): names with parallel arrays
        if (i < 0 || i >= (int)fgLayers_.size()) return false;
        fgDx_.resize(fgLayers_.size(), 0); fgDy_.resize(fgLayers_.size(), 0); fgHidden_.resize(fgLayers_.size(), 0);
        o = ThemeLayerInfo();
        o.sprite = fgLayers_[i]; o.speed = fgSpeed_[i]; o.scale = fgScale_[i]; o.offset = fgOffset_[i]; o.dx = fgDx_[i]; o.dy = fgDy_[i]; o.hidden = fgHidden_[i] != 0;
        return true;
    }
    auto& v = which == 1 ? ntFg_ : bgLayers_;
    if (i < 0 || i >= (int)v.size()) return false;
    const BgLayer& l = v[i];
    o.sprite = l.sprite; o.speed = l.speed; o.scale = l.scale; o.scaleX = l.scaleX; o.scaleY = l.scaleY; o.offset = l.offset; o.offX = l.offX; o.offY = l.offY;
    o.dx = l.dx; o.dy = l.dy; o.velX = l.velX; o.velY = l.velY; o.angle = l.angle; o.looping = l.looping; o.hidden = l.hidden;
    return true;
}
bool GamePlatform::SetThemeLayer(int which, int i, const ThemeLayerInfo& in) {
    if (which == 2) {
        if (i < 0 || i >= (int)fgLayers_.size()) return false;
        fgDx_.resize(fgLayers_.size(), 0); fgDy_.resize(fgLayers_.size(), 0); fgHidden_.resize(fgLayers_.size(), 0);
        fgLayers_[i] = in.sprite; fgSpeed_[i] = in.speed; fgScale_[i] = in.scale; fgOffset_[i] = in.offset; fgDx_[i] = in.dx; fgDy_[i] = in.dy; fgHidden_[i] = in.hidden ? 1 : 0;
        return true;
    }
    auto& v = which == 1 ? ntFg_ : bgLayers_;
    if (i < 0 || i >= (int)v.size()) return false;
    BgLayer& l = v[i];
    l.sprite = in.sprite; l.speed = in.speed; l.scale = in.scale; l.scaleX = in.scaleX; l.scaleY = in.scaleY; l.offset = in.offset; l.offX = in.offX; l.offY = in.offY;
    l.dx = in.dx; l.dy = in.dy; l.velX = in.velX; l.velY = in.velY; l.angle = in.angle; l.looping = in.looping; l.hidden = in.hidden;
    return true;
}

// Raises drawBackground / drawForeground (after the engine's own layers) when the draw function ends, whichever way it returns
template <class F> struct DrawHookGuard {
    F f;
    explicit DrawHookGuard(F fn) : f(fn) {}
    ~DrawHookGuard() { f(); }
};

void GamePlatform::DrawBackground() {
    TagScope tag(this, "background");
    if (scripts_) EmitHook(scripts_->State(), "drawBackgroundBefore");
    auto afterHook = [this]() { if (scripts_) { state_ = RenderState(); EmitHook(scripts_->State(), "drawBackground"); } };
    DrawHookGuard<decltype(afterHook)> bgGuard(afterHook);
    if (Tune(scripts_ ? scripts_->State() : nullptr, "background.skip", 0) != 0) return;
    if (std::getenv("AB_DRAWDBG")) { static int n = 0; if (n++ < 3) std::fprintf(stderr, "DrawBackground layers=%d\n", (int)bgLayers_.size()); }
    if (!backend_) return;
    if (scripts_ && scripts_->IsSpace()) { DrawSpaceBackground(); return; }
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
        if (layer.hidden) continue;
        if (std::getenv("AB_DRAWDBG")) std::fprintf(stderr, "BGLAYER %s scale=%.3f speed=%.3f off=%.1f loop=%d\n", layer.sprite.c_str(), layer.scale, layer.speed, layer.offset, (int)layer.looping);
        // a layer is a sprite or a composite sprite (the Seasons hills are composites)
        const Sprite* sp = sprites_.Find(layer.sprite);
        const CompoSprite* compo = sp ? nullptr : sprites_.FindCompo(layer.sprite);
        int cw = 0, ch = 0, ox = 0, oy = 0;
        if (compo) sprites_.CompoExtent(*compo, cw, ch, ox, oy);
        float w = sp ? (float)sp->w : (float)cw, pivotX = sp ? (float)sp->pivotX : (float)ox;
        if (w <= 0) continue;
        float s = layer.scale, inv = 1.0f / s;
        if (layer.space) {   // Space: screen-space layers (no world scale): pinned ones sit at (offsetX, offsetY), looping ones repeat across the screen
            state_ = RenderState();
            state_.sx = state_.sy = s;
            float shift = layer.speed * camX_ * 0.05f * worldScale_;
            if (layer.looping) {
                float m = std::fmod(shift, w * s);
                for (float x = -(m + w * s) * inv; x * s <= (float)screenWidth_; x += w) ResDrawSprite(layer.sprite, x, layer.offY, 4, 3, false, 0, 0);
            } else ResDrawSprite(layer.sprite, layer.offX - shift * inv, layer.offY, 4, 3, false, 0, 0);
            continue;
        }
        float baseShift = layer.speed * camX_, shift = layer.speed * (camX_ + layer.offset);   // 0x41a6e7: (offset + camera) * parallax
        state_ = RenderState();
        state_.ty = -camY_ * inv + layer.dy / (s * worldScale_);
        state_.sx = state_.sy = s * worldScale_;
        if (layer.looping) {
            float m = std::fmod(shift, w * s);
            float x0 = -(m + w * s) * inv;
            state_.tx = x0 - std::trunc(x0) + layer.dx / (s * worldScale_);
            float right = screenWidth_ * inv / worldScale_ + pivotX;
            for (float x = std::trunc(x0); x <= right; x += w) ResDrawSprite(layer.sprite, x, 0, 4, 3, false, 0, 0);
        } else {
            place += firstFixed ? startX * 0.0625f * w : w * 0.5f;
            firstFixed = false;
            state_.tx = place - std::trunc(place) + layer.dx / (s * worldScale_);
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
    TagScope tag(this, "foreground");
    if (scripts_) EmitHook(scripts_->State(), "drawForegroundBefore");
    auto afterHook = [this]() { if (scripts_) { state_ = RenderState(); EmitHook(scripts_->State(), "drawForeground"); } };
    DrawHookGuard<decltype(afterHook)> fgGuard(afterHook);
    if (Tune(scripts_ ? scripts_->State() : nullptr, "foreground.skip", 0) != 0) return;
    // Angry Birds Space has no ground (its foreground layers use named fields and are not read here); Star Wars keeps the Seasons-style ones
    if (!backend_ || (scripts_ && scripts_->IsSpace() && fgLayers_.empty())) return;
    if (newTheme_ && !ntFg_.empty()) { DrawNewThemeLayers(ntFg_, (int)bgLayers_.size()); return; }
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
    fgNamed_.resize(fgLayers_.size());
    if (!fgNamed_.empty() && fgNamed_[0].named) height = 0;
    else DrawRect(groundR_ / 255.0f, groundG_ / 255.0f, groundB_ / 255.0f, 1, 0, -(camY_ - height * 1.5f) * worldScale_, (float)screenWidth_, (float)screenHeight_ + 1, false);
    int fgIndex = -1;
    for (const std::string& name : fgLayers_) {
        fgIndex++;
        fgDx_.resize(fgLayers_.size(), 0); fgDy_.resize(fgLayers_.size(), 0); fgHidden_.resize(fgLayers_.size(), 0);
        if (fgHidden_[fgIndex]) continue;
        float w, px, h, py;
        if (std::getenv("AB_DRAWDBG")) std::fprintf(stderr, "FGL %d %s found=%d scale=%g speed=%g\n", fgIndex, name.c_str(), (int)extent(name, w, px, h, py), fgScale_[fgIndex], fgSpeed_[fgIndex]);
        if (!extent(name, w, px, h, py) || w <= 0) continue;
        const FgNamed& nm = fgNamed_[fgIndex];
        float c = fgScale_[fgIndex], baseShift = fgSpeed_[fgIndex] * camX_, shift = fgSpeed_[fgIndex] * (camX_ + fgOffset_[fgIndex]);
        if (nm.named && nm.fill) {     // the colour below the layer's bottom edge
            float bottom = (-camY_ + c * (nm.offY + (h - py))) * worldScale_;
            DrawRect(nm.r / 255.0f, nm.g / 255.0f, nm.b / 255.0f, 1, 0, bottom - 1, (float)screenWidth_, (float)screenHeight_ + 1, false);
        }
        float m = std::fmod(shift, c * w);
        float x0 = -(m + c * w) / c;
        state_ = RenderState();
        state_.tx = x0 - std::trunc(x0) + fgDx_[fgIndex] / (worldScale_ * c);
        state_.ty = -camY_ / c + nm.offY + fgDy_[fgIndex] / (worldScale_ * c);
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
    TagScope tag(this, o.name);
    if (scripts_) {   // engine.drawObject(name): return false to leave the object undrawn (draw it yourself with res.drawSprite if you like)
        lua_State* L = scripts_->State();
        int top = lua_gettop(L);
        bool skip = false;
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "engine");
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "drawObject");
                if (lua_isfunction(L, -1)) {
                    lua_pushstring(L, o.name.c_str());
                    RenderState keep = state_;
                    if (lua_pcall(L, 1, 1, 0) == 0) skip = lua_isboolean(L, -1) && !lua_toboolean(L, -1);
                    state_ = keep;
                }
            }
        }
        lua_settop(L, top);
        if (skip) return;
    }
    const SpriteSheet* sheet = nullptr;
    const Sprite* sp = sprites_.Find(o.sprite, &sheet);
    const CompoSprite* compo = sp ? nullptr : sprites_.FindCompo(o.sprite);
    if (std::getenv("AB_DRAWDBG")) { static int n = 0; if (n++ < 3000) std::fprintf(stderr, "DRAW %s sprite=%s found=%d x=%.0f y=%.0f cam=%.0f,%.0f ws=%.3f\n", o.name.c_str(), o.sprite.c_str(), (int)(sp || compo), o.interpX * 20, o.interpY * 20, camX_, camY_, worldScale_); }
    if (!o.parts.empty()) {      // Angry Birds Space compound body: every part is a sprite placed about the body and turning with it
        float bx = o.interpX * 20.0f, by = o.interpY * 20.0f, c = std::cos(o.interpAngle), sn = std::sin(o.interpAngle);
        for (const CompoundShape& part : o.parts) {
            const SpriteSheet* psheet = nullptr;
            const Sprite* ps = part.sprite.empty() ? nullptr : sprites_.Find(part.sprite, &psheet);
            if (!ps) continue;
            float lx = part.x * 20.0f, ly = part.y * 20.0f;
            float px = bx + c * lx - sn * ly, py = by + sn * lx + c * ly;
            RenderState keep = state_;
            state_.angle = o.interpAngle + part.angle;
            state_.alpha = o.alpha;
            state_.pivotX = (float)ps->pivotX; state_.pivotY = (float)ps->pivotY;
            DrawSpriteAt(*psheet, *ps, px - (float)ps->pivotX, py - (float)ps->pivotY, 0, 0, false, 0, 0);
            state_ = keep;
        }
        return;
    }
    if (!sp && !compo) return;
    if (!o.decoSprite.empty() && o.decoAmount > 0 && !o.hidden) {   // 0x48d41b: the copies of the decoration sprite, each turned o.decoIncrement degrees more
        const SpriteSheet* dsheet = nullptr;
        if (const Sprite* ds = sprites_.Find(o.decoSprite, &dsheet)) {
            float dx = o.interpX * 20.0f, dy = o.interpY * 20.0f, ang = o.interpAngle;
            for (int i = 0; i < o.decoAmount; i++) {
                RenderState keep = state_;
                state_.angle = ang; state_.alpha = o.alpha;
                state_.pivotX = (float)ds->pivotX; state_.pivotY = (float)ds->pivotY;
                // 0x48d43b: the copies are drawn at the object's own scale times the decoration's (a planet of scale 1.6 draws its slices 1.6 times as long)
                float dsx = o.decoScale * (Physics().spaceRules ? o.scaleX : 1.0f), dsy = o.decoScale * (Physics().spaceRules ? o.scaleY : 1.0f);
                if (dsx == 0 || dsy == 0) { state_ = keep; continue; }
                state_.sx *= dsx; state_.sy *= dsy;
                state_.tx = (dx + keep.tx) / dsx - dx; state_.ty = (dy + keep.ty) / dsy - dy;
                DrawSpriteAt(*dsheet, *ds, dx, dy, 4, 3, false, 0, 0);
                state_ = keep;
                ang = std::fmod(ang + o.decoIncrement * 3.14159265f / 180.0f, 6.2831853f);
                if (ang < 0) ang += 6.2831853f;
            }
        }
    }
    if (o.hidden) return;                      // Space: setObjectParameter(n, 33, 0)
    if (o.isSensor && o.sensorKind == 2 && Physics().spaceRules) return;   // the gravity area is invisible in retail (its sprite is the blue dot at the planet centre)
    // the other sensors (pig, killing, trigger boxes) carry the same editor-only marker sprites: the blue and green dots are never drawn in a level
    if (Physics().spaceRules && o.isSensor && o.sprite.compare(0, 15, "GRAVITY_SENSOR_") == 0) return;
    float x = o.interpX * 20.0f, y = o.interpY * 20.0f;
    state_.angle = o.interpAngle + o.spriteAngle;   // Space: 0x48d2a9 adds setSpriteRotation's angle
    if (!o.texture.empty() && sp) {
        const TextureInfo* t = TextureFile(sheet->directory + "/" + o.texture + ".png", true);
        if (t) tileTexture_ = t;
    }
    // 0x4165xx: a scaled sprite shrinks about its pivot, the pivot stays where it was on the screen
    state_.alpha = o.alpha;                       // setObjectAlpha; the state is put back by the caller
    RenderState saved = state_;
    if (o.disableRotation) state_.angle = 0;     // 0x41b2c0: setObjectParameter 10 draws the sprite upright
    // the draw scale is the object's own scale plus the wobble after a hit (0x40b2xx: x gets +sin, y gets -sin of the same phase)
    bool useScale = o.scaled || Physics().spaceRules;       // Space always draws the scale (ids 5, 17, 18 and setScale)
    float drawSX = (useScale ? o.scaleX : 1.0f) + o.wobDX, drawSY = (useScale ? o.scaleY : 1.0f) + o.wobDY;
    // Space draws the scale as it is: the sensors and trigger markers are defined with scale = 0 (BLOCK_SENSOR_PIG_A / _B ...), which draws nothing;
    // an unscaled sprite here was the blue dot on the pig's spawn spot and on other sensor bodies
    if (Physics().spaceRules && (drawSX == 0 || drawSY == 0)) { state_ = saved; return; }
    if ((useScale || o.wobDX != 0 || o.wobDY != 0) && drawSX != 0 && drawSY != 0) {
        state_.sx *= drawSX; state_.sy *= drawSY;
        state_.tx = (x + saved.tx) / drawSX - x;
        state_.ty = (y + saved.ty) / drawSY - y;
    }
    if (sp && !o.flipX) {
        state_.pivotX = (float)sp->pivotX + o.pivotOffX;     // 0x48d274: setPivotOffset moves the pivot
        state_.pivotY = (float)sp->pivotY + o.pivotOffY;
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
                if (std::getenv("AB_DRAWDBG") && o.name.compare(0, 3, "PIG") == 0) { static int pn = 0; if (pn++ < 400) std::fprintf(stderr, "COMPOPART alpha=%.2f sx=%.2f flip=%d w=%d h=%d %s part=%s found=%d x=%d y=%d exact=%d scale=%.2f\n", o.alpha, state_.sx, (int)o.flipX, ps ? ps->w : -1, ps ? ps->h : -1, o.name.c_str(), part.sprite.c_str(), (int)(ps != nullptr), part.x, part.y, (int)part.exact, part.scale); }
                if (ps && scripts_ && scripts_->IsSpace()) {
                    // Angry Birds Space: a part's (x, y) is where its sprite's pivot lands, relative to the object's position
                    drawPart(*partSheet, *ps, (float)part.x - (float)ps->pivotX, (float)part.y - (float)ps->pivotY);
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
    if (scripts_ && scripts_->IsSpace()) {
        // Space (drawGameNative 0x48cbd0): one pass over the z layers 0 to 169, lowest first. Every create native files the object under (int)z_order
        // (0x4893b6, ftol) and keeps the names of one layer sorted, so inside a layer the order is the name's. An object outside 0..169 is never drawn.
        if (which != 0) return group;
        for (auto& up : Physics().Objects()) {
            int z = (int)up->zOrder;
            if (z >= 0 && z < 170) group.push_back(up.get());
        }
        std::sort(group.begin(), group.end(), [](PhysObject* a, PhysObject* b) {
            int za = (int)a->zOrder, zb = (int)b->zOrder;
            return za != zb ? za < zb : a->name < b->name; });
        return group;
    }
    if (rio) {
        // Rio (drawGameNative 0x41b660): the create natives file each object's name under (int)z_order (0x41752d, ftol) in a table that is walked from
        // the lowest layer up, one pass for every object; the names of a layer stay in creation order. There are no static / moving / bird groups.
        if (which != 0) return group;
        for (auto& up : Physics().Objects()) group.push_back(up.get());
        std::stable_sort(group.begin(), group.end(), [](PhysObject* a, PhysObject* b) { return (int)a->zOrder < (int)b->zOrder; });
        return group;
    }
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
// Space aiming aid natives (0x461bd0 enableAimingAid, 0x46d200 setAimingAidSprite, 0x4634a0 populateAimingAid, 0x463a60 clearAimingAid, 0x479ab0 updateBirdTrajectoryTable)
bool GamePlatform::BodyCommand(const std::string& n, int cmd, float a, float& result) {
    if (cmd < 20 || cmd > 24) return Physics().BodyCommand(n, cmd, a, result);
    if (cmd == 20) aimEnabled_ = a != 0;
    else if (cmd == 21) { aimSprite_ = n; aimScale_ = a > 0 ? a : 1; }
    else if (cmd == 22) { if (scripts_ && scripts_->IsStarWars()) AimPopulate(); }   // Star Wars populateAimingAid (0x402770): the dots laid out along the stored path
    else if (cmd == 23) { aimDots_.clear(); aimPts_.clear(); aimOn_ = false; }   // 0x45e230
    else if (cmd == 24) {
        std::vector<std::pair<float, float>> pts;
        // 0x479ab0: simulationIterations (50) steps of objects.currentTimeStep * simulationTimeStepMultiplier (3) seconds, a point stored every simulationStorePointsSampler (1)
        // steps. currentTimeStep is 1/90 while a bird is aimed (gamescene.lua sets it), 1/30 otherwise; without it the exe uses 0.0285714 (0x50fb74).
        int iterations = 50, sampler = 1;
        float mult = 3, timeStep = 0.028571429f;
        if (scripts_) {
            lua_State* L = scripts_->State();
            int top = lua_gettop(L);
            lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
            auto num = [&](const char* k, auto& v) { lua_getfield(L, -1, k); if (lua_isnumber(L, -1)) v = (std::remove_reference_t<decltype(v)>)lua_tonumber(L, -1); lua_pop(L, 1); };
            num("simulationIterations", iterations); num("simulationTimeStepMultiplier", mult); num("simulationStorePointsSampler", sampler);
            lua_getfield(L, -1, "objects");
            if (lua_istable(L, -1)) { lua_getfield(L, -1, "currentTimeStep"); if (lua_isnumber(L, -1)) timeStep = (float)lua_tonumber(L, -1); lua_pop(L, 1); }
            lua_settop(L, top);
        }
        if (sampler < 1) sampler = 1;
        Physics().SimulateTrajectory(n.empty() ? "BirdSimulation" : n, iterations, sampler, timeStep * mult, pts);
        if (std::getenv("AB_TRAJDBG")) for (auto& p : pts) std::fprintf(stderr, "TRAJ %.3f %.3f\n", p.first, p.second);
        aimPts_.clear();
        for (auto& p : pts) aimPts_.push_back({p.first, p.second});
        if (std::getenv("AB_DRAWDBG")) std::fprintf(stderr, "AIM %s pts=%d sprite=%s found=%d\n", n.c_str(), (int)aimPts_.size(), aimSprite_.c_str(), (int)(sprites_.Find(aimSprite_) != nullptr));
    }
    return true;
}

// simulateAimingAid(bird, impulseX, impulseY [, points]): the aiming aid's arc for a launch with that impulse: the free flight of the bird, as in the retail trail (see PhysicsWorld::SimulateFlight).
// The points are 1/30 s apart like the retail trail's (simulationTimeStepMultiplier 3 times 1/90); the first one is the bird's own position.
bool GamePlatform::AimSimulate(const std::string& bird, float vx, float vy, int points, const float* start) {
    PhysObject* o = Physics().Find(bird);
    if (!o || !o->pbody) return false;
    std::vector<std::pair<float, float>> pts;
    float mass = o->pbody->Mass();
    if (mass <= 0) return false;
    if (!Physics().SimulateFlight(bird, vx / mass, vy / mass, points, 1, 1.0f / 30.0f, pts, start) || pts.empty()) return false;   // vx, vy are the impulse: the real launch is applyImpulse, and the world steps 1/30 s with 10 + 10 iterations
    aimPts_.clear();
    aimPts_.push_back(start ? TrajectoryPoint{start[0], start[1]} : TrajectoryPoint{o->pbody->PosX(), o->pbody->PosY()});
    for (auto& p : pts) aimPts_.push_back({p.first, p.second});
    if (std::getenv("AB_AIMDBG")) for (size_t i = 0; i < aimPts_.size() && i < 400; i++) std::fprintf(stderr, "AP %d %.3f %.3f\n", (int)i, aimPts_[i].x, aimPts_[i].y);
    return true;
}

void GamePlatform::AimParams(float& speed, float& spawn) {
    speed = 4.0f; spawn = 0.6f;                      // 0x50f644, 0x50f648
    if (!scripts_) return;
    lua_State* L = scripts_->State();
    int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "simulationAimSpeed"); if (lua_isnumber(L, -1)) speed = (float)lua_tonumber(L, -1); lua_pop(L, 1);
        lua_getfield(L, -1, "simulationAimSpawnTime"); if (lua_isnumber(L, -1)) spawn = (float)lua_tonumber(L, -1); lua_pop(L, 1);
    }
    lua_settop(L, top);
}

// 0x45dea0: the dots are laid out along the whole path at spacing speed*spawn; the first spawns after one period
void GamePlatform::AimPopulate() {
    aimDots_.clear();
    float speed, spawn; AimParams(speed, spawn);
    float len = (float)((int)aimPts_.size() - 3);
    int count = (int)(len / (speed * spawn));
    for (int i = 0; i < count; i++) { float x = i * speed * spawn; aimDots_.push_back({x, 0, 1.0f - x / len + 0.2f}); }
    aimTimer_ = spawn;
}

void GamePlatform::AimSetEnabled(bool on) {       // 0x45df70
    if (aimOn_ == on) return;
    aimOn_ = on;
    if (on) AimPopulate();
}

// 0x45dfd0: the dots move along the path and shrink; one is born every spawn time while the aid is on
void GamePlatform::UpdateAimingAid(float dt) {
    if (aimPts_.size() >= 4) AimSetEnabled(aimEnabled_);   // 0x48d757: every frame with a usable table
    float speed, spawn; AimParams(speed, spawn);
    float len = (float)((int)aimPts_.size() - 3);
    for (size_t i = 0; i < aimDots_.size();) {
        AimDot& d = aimDots_[i];
        d.x += speed * dt; d.y += 0;
        d.s = 1.0f - d.x / len + 0.2f;
        if (d.x >= len) aimDots_.erase(aimDots_.begin() + i); else i++;
    }
    aimTimer_ -= dt;
    if (!aimOn_ || aimTimer_ > 0 || len <= 0) return;
    int guard = 0;
    while (aimTimer_ <= 0 && guard++ < 8) { aimTimer_ += spawn; aimDots_.push_back({0, 0, 1.2f}); }
}

GamePlatform::TrajectoryPoint GamePlatform::AimSpline(float x) const {   // 0x45d380, Catmull-Rom between points idx+1 and idx+2
    int n = (int)aimPts_.size();
    int idx = (int)x; if (idx < 0) idx = 0; if (idx > n - 4) idx = n - 4;
    float t = x - std::floor(x), t2 = t * t, t3 = t2 * t;
    const TrajectoryPoint &p0 = aimPts_[idx], &p1 = aimPts_[idx + 1], &p2 = aimPts_[idx + 2], &p3 = aimPts_[idx + 3];
    auto cr = [&](float a, float b, float c, float d) { return 0.5f * (2 * b + (-a + c) * t + (2 * a - 5 * b + 4 * c - d) * t2 + (-a + 3 * b - 3 * c + d) * t3); };
    return {cr(p0.x, p1.x, p2.x, p3.x), cr(p0.y, p1.y, p2.y, p3.y)};
}

void GamePlatform::DrawGame() {
    if (scripts_) { RenderState keepIn = state_; EmitHook(scripts_->State(), "drawGameBefore"); state_ = keepIn; }
    struct GameEnd { GamePlatform* p; ~GameEnd() { if (p->scripts_) { RenderState keep = p->state_; p->state_ = RenderState(); EmitHook(p->scripts_->State(), "drawGame"); p->state_ = keep; } } } gameEnd{this};
    if (std::getenv("AB_DRAWDBG")) { static int n = 0; if (n++ < 3) std::fprintf(stderr, "DrawGame objects=%d backend=%d\n", (int)Physics().Objects().size(), (int)(backend_ != nullptr)); }
    gameDrawn_ = true;
    if (!backend_) return;
    // Star Wars' scripts never call drawForegroundNative: the exe draws the ground colour and the foreground stones itself, behind the objects
    // Hoth's ground blocks (z 1 and 2) are covered by the foreground (snow plane and rocks), everything above them is drawn over it
    bool fgLate = scripts_ && scripts_->IsSpace() && !fgLayers_.empty() && !(fgNamed_.size() > 0 && fgNamed_[0].named) && !std::getenv("AB_FGEARLY");
    bool fgDone = !fgLate;
    if (scripts_ && scripts_->IsSpace() && !fgLayers_.empty() && !fgLate) DrawForeground();
    state_ = RenderState();
    state_.tx = -camX_; state_.ty = -camY_;
    state_.sx = state_.sy = worldScale_;
    bool rio = scripts_ && scripts_->IsRio();
    bool oneGroup = rio || (scripts_ && scripts_->IsSpace());   // Rio and Space draw every object in one pass over the z layers
    bool trajDone = false;
    // each of the six lists: its dots go through TRAIL_WHITE_1, _2, _3 and start again, then its puffs (BIRD_SPECIAL); then Space's aiming aid
    auto drawTrajectory = [&]() {
        trajDone = true;
        state_.angle = 0; state_.pivotX = state_.pivotY = 0;
        for (int list = 0; list < 6; list++) {
            const std::string* custom = trajCustom_[list / 3];
            for (size_t i = 0; i < trajDots_[list].size(); i++) {
                const SpriteSheet* sheet = nullptr;
                const Sprite* sp = custom[i % 3].empty() ? nullptr : sprites_.Find(custom[i % 3], &sheet);
                if (!sp) sp = sprites_.Find("TRAIL_WHITE_" + std::to_string(i % 3 + 1), &sheet);
                if (sp) { TagScope t(this, "trajectory"); DrawSpriteAt(*sheet, *sp, trajDots_[list][i].x, trajDots_[list][i].y, 4, 3, false, 0, 0); }
            }
            for (const TrajectoryPoint& p : trajPuffs_[list]) {
                const SpriteSheet* sheet = nullptr;
                const Sprite* sp = custom[3].empty() ? nullptr : sprites_.Find(custom[3], &sheet);
                if (!sp) sp = sprites_.Find("BIRD_SPECIAL", &sheet);
                if (sp) { TagScope t(this, "trajectory"); DrawSpriteAt(*sheet, *sp, p.x, p.y, 4, 3, false, 0, 0); }
            }
        }
        // 0x45d570: every dot goes through a Catmull-Rom spline of the table (0x45d380) and is drawn scaled by its own value
        if (!aimSprite_.empty() && aimPts_.size() >= 4) {
            const SpriteSheet* sheet = nullptr;
            if (const Sprite* sp = sprites_.Find(aimSprite_, &sheet)) {
                RenderState keep = state_;
                state_.angle = 0; state_.pivotX = state_.pivotY = 0;
                { TagScope t(this, "trajectory");
                  for (const AimDot& d : aimDots_) {
                      TrajectoryPoint p = AimSpline(d.x);
                      { float ds = d.s * aimScale_; DrawSpriteAt(*sheet, *sp, p.x * 20.0f - sp->pivotX * ds, p.y * 20.0f - sp->pivotY * ds, 0, 0, true, sp->w * ds, sp->h * ds); }
                  } }
                state_ = keep;
            }
        }
    };
    auto drawGroup = [&](int which) {
        for (PhysObject* o : DrawGroup(which)) {
            // with one pass over the z layers the dots and puffs would end up above the birds; they are drawn right before the first bird instead
            if (oneGroup && !trajDone && o->isBird) drawTrajectory();
            if (!fgDone && o->zOrder > 9999) { fgDone = true; RenderState keepS = state_; DrawForeground(); state_ = keepS; }
            drawingNative_ = true;                          // v2.2.1: the object's quads are sorted against render state z sprites by its z_order
            nativeKey_ = (rio || o->zSet) ? o->zOrder : 0;
            DrawPhysObject(*o);
            drawingNative_ = false;
        }
        state_.alpha = 1;                          // setObjectAlpha belongs to one object, not to what is drawn next
    };
    drawGroup(0);
    drawGroup(1);
    if (!trajDone) drawTrajectory();               // the earlier games: after the blocks, before the birds
    drawGroup(2);
    drawGroup(3);
    if (!fgDone) { fgDone = true; DrawForeground(); }
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
    if (scripts_->IsSpace()) {          // 0x493e20
        d.space = true; d.seasons = false;
        d.minVelX = d.minVelY = 0; d.maxVelX = d.maxVelY = 0;
        GetNumber(L, t, "minVel", d.minVelX); GetNumber(L, t, "maxVel", d.maxVelX);
        d.angleMinEmitter = d.angleMaxEmitter = 0;
        GetNumber(L, t, "minAngleEmitter", d.angleMinEmitter); GetNumber(L, t, "maxAngleEmitter", d.angleMaxEmitter);
        GetNumber(L, t, "minAngle", d.minAngle); GetNumber(L, t, "maxAngle", d.maxAngle);
        GetNumber(L, t, "emitAreaScaleX", d.areaX); GetNumber(L, t, "emitAreaScaleY", d.areaY);
        lua_getfield(L, t, "useAngleFromSpawner"); d.useSpawnerAngle = lua_toboolean(L, -1) != 0; lua_pop(L, 1);
        lua_getfield(L, t, "ignoreLimits"); d.ignoreLimits = lua_toboolean(L, -1) != 0; lua_pop(L, 1);
        lua_getfield(L, t, "background"); d.background = lua_toboolean(L, -1) != 0; lua_pop(L, 1);
    }
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

// Space addParticlesWithScale / addParticlesWithProperties (0x494900, 0x493280): the definition with the call's scale or speed and emitter angles
void GamePlatform::AddParticlesOverride(const std::string& type, float amount, float x, float y, float w, float h, float angle, const ParticleOverride& ov) {
    const ParticleDef* base = ParticleDefFor(type);
    if (!base) return;
    char key[160];
    std::snprintf(key, sizeof key, "%s|%d|%g|%g|%g|%g", type.c_str(), ov.kind, ov.a, ov.b, ov.c, ov.d);
    auto it = particleOverrideDefs_.find(key);
    if (it == particleOverrideDefs_.end()) {
        ParticleDef d = *base;
        if (ov.kind == 1) { d.minScaleBegin = d.maxScaleBegin = ov.a; d.minScaleEnd = d.maxScaleEnd = ov.b; }
        else { d.minVelX = ov.a; d.maxVelX = ov.b; d.angleMinEmitter = ov.c / 0.017453292f; d.angleMaxEmitter = ov.d / 0.017453292f; }
        it = particleOverrideDefs_.emplace(key, d).first;
    }
    forcedDef_ = &it->second;
    AddParticles(type, amount, x, y, w, h, angle, false, false);
    forcedDef_ = nullptr;
}

void GamePlatform::AddLevelParticles(const std::string& type, float amount, float x, float y, float w, float h, float angle, bool ignoreLimits) {
    size_t before = particles_.size();
    AddParticles(type, amount, x, y, w, h, angle, ignoreLimits, false);
    // 0x42a850 draws a random layer (1 behind the foreground, 2 in front) for each emit
    for (size_t i = before; i < particles_.size(); i++) { particles_[i].level = true; particles_[i].layer = (std::rand() & 1) + 1; }
}

void GamePlatform::AddParticles(const std::string& type, float amount, float x, float y, float w, float h, float angle, bool ignoreLimits, bool second) {
    const ParticleDef* d = forcedDef_ ? forcedDef_ : ParticleDefFor(type);
    lua_State* TL = scripts_ ? scripts_->State() : nullptr;
    amount *= Tune(TL, "particle.amountScale", 1.0f);
    const float pSpeed = Tune(TL, "particle.speedScale", 1.0f), pLife = Tune(TL, "particle.lifeScale", 1.0f), pSize = Tune(TL, "particle.sizeScale", 1.0f);
    // Space (0x494364): the limits are 60 (halved above that) and 225, and the definition decides whether they apply
    bool space = d && d->space;
    if (space) ignoreLimits = d->ignoreLimits;
    if (!ignoreLimits) {
        int count = 0;
        for (const Particle& q : particles_) if (q.second == second && !q.level) count++;
        float soft = space ? 60.0f : (float)softLimit_, hard = space ? 225.0f : (float)hardLimit_;
        if (soft < amount + (float)count) amount *= space ? 0.5f : softFactor_;
        if (hard < (float)count + amount) amount = hard - (float)count;
        if (!(amount > 0)) return;
    }
    static const bool partDbg = std::getenv("AB_PARTDBG") != nullptr;
    if (partDbg) std::fprintf(stderr, "THEME addParticles %s amount=%g def=%d sprites=%d\n", type.c_str(), amount, d ? 1 : 0, d ? (int)d->sprites.size() : -1);
    if (!d || d->sprites.empty()) return;
    float s = std::sin(angle), c = std::cos(angle);
    lua_State* hookL = nullptr;     // set when the script defines engine.particle
    if (TL) {
        int top = lua_gettop(TL);
        lua_getfield(TL, LUA_REGISTRYINDEX, "ab.env");
        if (lua_istable(TL, -1)) { lua_getfield(TL, -1, "engine"); if (lua_istable(TL, -1)) { lua_getfield(TL, -1, "particle"); if (lua_isfunction(TL, -1)) hookL = TL; } }
        lua_settop(TL, top);
    }
    for (int i = 0; (float)i < amount; i++) {
        Particle p;
        p.def = d;
        p.second = second;
        if (d->randomizeBirth) {
            float a = (Rand01() - 0.5f) * w * d->areaX;
            float b = (Rand01() - 0.5f) * h * d->areaY;
            p.x = (x + a * c) - b * s;
            p.y = c * b + s * a + y;
        } else {
            p.x = x; p.y = y;
        }
        if (d->space) {          // 0x4944db: direction and rotation, both turned by the spawner's angle when useAngleFromSpawner is set
            float emit = ((d->angleMaxEmitter - d->angleMinEmitter) * Rand01() + d->angleMinEmitter) * 0.017453292f;
            float speed = (d->maxVelX - d->minVelX) * Rand01() + d->minVelX;
            if (d->useSpawnerAngle) emit += angle;
            p.vx = std::cos(emit) * speed;
            p.vy = std::sin(emit) * speed;
        } else if (d->roundExplosions) {
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
        if (d->space) p.angle = ((d->maxAngle - d->minAngle) * Rand01() + d->minAngle) * 0.017453292f + (d->useSpawnerAngle ? angle : 0.0f);
        else if (d->seasons || d->v2Emitter) p.angle = ((d->angleMaxEmitter - d->angleMinEmitter) * Rand01() + d->angleMinEmitter) * 0.017453292f;
        else p.angle = Rand01() * 3.1415927f + Rand01() * 3.1415927f;   // 1.x: the exe uses one random number twice
        p.angleVel = (d->maxAngleVel - d->minAngleVel) * Rand01() + d->minAngleVel;
        p.scaleBegin = (d->maxScaleBegin - d->minScaleBegin) * Rand01() + d->minScaleBegin;
        p.scaleEnd = (d->maxScaleEnd - d->minScaleEnd) * Rand01() + d->minScaleEnd;
        p.vx *= pSpeed; p.vy *= pSpeed; p.scaleBegin *= pSize; p.scaleEnd *= pSize;
        p.scale = p.scaleBegin;
        p.age = 0;
        p.lifeTime = d->lifeTime * pLife;
        p.frame = 1;
        p.sprite = d->animated ? d->sprites[0] : d->sprites[(size_t)(Rand01() * (float)d->sprites.size())];
        if (hookL) {      // engine.particle(type, t): t has x, y, vx, vy, angle, angleVel, scaleBegin, scaleEnd, lifeTime, sprite; change what you like, return false to drop the particle
            int top = lua_gettop(hookL);
            lua_getfield(hookL, LUA_REGISTRYINDEX, "ab.env"); lua_getfield(hookL, -1, "engine"); lua_getfield(hookL, -1, "particle");
            bool drop = false;
            if (lua_isfunction(hookL, -1)) {
                lua_pushstring(hookL, type.c_str());
                lua_newtable(hookL);
                auto put = [&](const char* k, float v) { lua_pushnumber(hookL, v); lua_setfield(hookL, -2, k); };
                put("x", p.x); put("y", p.y); put("vx", p.vx); put("vy", p.vy); put("angle", p.angle); put("angleVel", p.angleVel);
                put("scaleBegin", p.scaleBegin); put("scaleEnd", p.scaleEnd); put("lifeTime", p.lifeTime);
                lua_pushstring(hookL, p.sprite.c_str()); lua_setfield(hookL, -2, "sprite");
                const int tt = lua_gettop(hookL);
                lua_pushvalue(hookL, tt);
                if (lua_pcall(hookL, 2, 1, 0) == 0) {
                    drop = lua_isboolean(hookL, -1) && !lua_toboolean(hookL, -1);
                    auto get = [&](const char* k, float& v) { lua_getfield(hookL, tt, k); if (lua_isnumber(hookL, -1)) v = (float)lua_tonumber(hookL, -1); lua_pop(hookL, 1); };
                    get("x", p.x); get("y", p.y); get("vx", p.vx); get("vy", p.vy); get("angle", p.angle); get("angleVel", p.angleVel);
                    get("scaleBegin", p.scaleBegin); get("scaleEnd", p.scaleEnd); get("lifeTime", p.lifeTime);
                    p.scale = p.scaleBegin;
                    lua_getfield(hookL, tt, "sprite");
                    if (lua_isstring(hookL, -1)) p.sprite = lua_tostring(hookL, -1);
                    lua_pop(hookL, 1);
                }
            }
            lua_settop(hookL, top);
            if (drop) continue;
        }
        particles_.push_back(p);
    }
}

// Below the ground (y > 0) a particle is removed only in the level; menu particles (enableMenuParticles) stay.
void GamePlatform::UpdateParticles(float dt, bool removeBelowGround, int which) {
    const float pGrav = Tune(scripts_ ? scripts_->State() : nullptr, "particle.gravityScale", 1.0f);
    for (size_t i = 0; i < particles_.size();) {
        Particle& p = particles_[i];
        if (which == 0 && p.second) { i++; continue; }
        if (which == 1 && !p.second) { i++; continue; }
        p.age += dt;
        if (p.lifeTime < p.age || (removeBelowGround && 0.0f < p.y)) { particles_.erase(particles_.begin() + i); continue; }
        const ParticleDef& d = *p.def;
        p.vx += d.gravityX * pGrav * dt;
        p.vy += d.gravityY * pGrav * dt;
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
    TagScope tag(this, "particles");
    if (!backend_) return;
    for (const Particle& p : particles_) {
        if (levelLayer > 0 ? (!p.level || p.layer != levelLayer) : p.level) continue;
        if (second >= 0 && p.second != (second == 1)) continue;
        if (particleFilter_ == 3 ? p.mode != 3 : particleFilter_ > 0 && (p.mode == 3 || (particleFilter_ == 1) != (p.def && p.def->background))) continue;
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
// A bird blinks and squawks by the script's own timers (the same for the bird in the slingshot); this only puts the default sprite back when they are done.
void GamePlatform::UpdateRioBirdFaces(float dt) {
    lua_State* L = scripts_->State();
    int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    int env = top + 1;
    lua_getfield(L, env, "birds");
    lua_getfield(L, env, "getObjectDefinition");
    if (!lua_istable(L, top + 2) || !lua_isfunction(L, top + 3)) { lua_settop(L, top); return; }
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
                    // the script's own timers (blink, squawk) pick the face, the same for the bird in the slingshot as for a waiting one
                    std::string want = o->sprite;
                    if (!showing) want = sp[0];
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
// Star Wars (0x433464): an object with its destroy flag set counts its timer down every frame; at zero its strength is 0 and it is dead
void GamePlatform::UpdateKillTimers(float dt) {
    std::vector<PhysObject*> due;
    const auto& fr = Physics().Frame();
    const double lim = Tune(scripts_ ? scripts_->State() : nullptr, "sw.removeLimit", 0.88f);
    for (auto& o : Physics().Objects()) {
        // 0x433771: sensors of kind 5 and 7 that drift past 0.88 times a level limit are marked for removal at once
        if ((o->sensorKind == 5 || o->sensorKind == 7) &&
            (o->interpY > lim * fr.levelMaxY || o->interpX < lim * fr.levelMinX || o->interpX > lim * fr.levelMaxX || o->interpY < lim * fr.levelMinY)) {
            if (!o->killOn) o->killTimer = 0;
            o->killOn = true;
        }
        if (!o->killOn) continue;
        o->killTimer -= dt;
        if (o->killTimer <= 0) { o->killOn = false; due.push_back(o.get()); }
    }
    lua_State* L = scripts_->State();
    int top = lua_gettop(L);
    for (PhysObject* o : due) {
        PushWorldEntry(L, o->name);
        if (!lua_istable(L, -1)) { lua_settop(L, top); continue; }
        lua_pushnumber(L, 0); lua_setfield(L, -2, "strength");
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        lua_getfield(L, -1, "deadBlocks");
        if (lua_istable(L, -1)) { lua_pushvalue(L, top + 1); lua_setfield(L, -2, o->name.c_str()); }
        lua_settop(L, top);
    }
}

// Rio, the world update at 0x40a8xx / 0x40b8xx: the wobble that a hit starts in an object's draw scale, and the turning
// of the objects that face the way they move
void GamePlatform::UpdateRioSquash(float dt) {
    static const bool dbg = std::getenv("AB_WOBDBG") != nullptr;
    const bool sw = scripts_ && scripts_->IsStarWars();
    std::vector<const PhysObject*> byName;           // Star Wars shifts each object's phase by its place (name order) times a quarter turn
    if (sw) {
        for (auto& o : Physics().Objects()) if (o->wobActive) { byName.clear(); for (auto& q : Physics().Objects()) byName.push_back(q.get()); std::sort(byName.begin(), byName.end(), [](const PhysObject* a, const PhysObject* b) { return a->name < b->name; }); break; }
    }
    lua_State* TL = scripts_ ? scripts_->State() : nullptr;
    const float wobDur = std::max(0.0001f, Tune(TL, "wobble.duration", 1.0f)), wobMax = Tune(TL, "wobble.maxScale", 4.0f), wobCut = Tune(TL, "wobble.cutoff", 0.01f);
    const float wobFreq = Tune(TL, "wobble.frequency", 100.0f), wobBase = Tune(TL, "wobble.baseFrequency", 5.0f), flipSpeed = Tune(TL, "flip.speed", 3.0f);
    for (auto& o : Physics().Objects()) {
        if (dbg && o->wobActive && o->wobT == 0) std::fprintf(stderr, "DBG wobble %s amp0=%.3f mult=%.2f\n", o->name.c_str(), o->wobAmp0, o->bounceAmp);
        if (o->wobActive) {
            o->wobT += dt;
            float cur = (o->wobAmp0 - o->wobT / wobDur * o->wobAmp0) * o->bounceAmp;      // dies out linearly within a second
            if (cur > wobMax) cur = wobMax;
            o->wobCur = cur;
            if (cur > wobCut) {
                float phase = o->wobT * (cur * wobFreq + o->bounceFreq * wobBase);
                if (sw) phase += (float)(std::lower_bound(byName.begin(), byName.end(), o.get(), [](const PhysObject* a, const PhysObject* b) { return a->name < b->name; }) - byName.begin()) * 1.5707964f;
                o->wobDX = std::sin(phase) * cur;
                o->wobDY = std::sin(phase + 3.1415927f) * cur;
            } else {
                o->wobActive = false; o->wobCur = 0; o->wobAmp0 = 0; o->wobT = 0;
            }
        }
        if (o->enableFlipping) {        // Luiz and the marmosets face the way they move (their sprites face left)
            float vx = o->pbody->VelX();
            if (sw) { if (std::fabs(vx) > flipSpeed && o->pbody->IsAwake()) o->flipX = o->flipBase = (vx >= 0.0f); }   // Star Wars (0x433aef) calls setObjectParameter(name, 8, vx >= 0)
            else if (std::fabs(vx) > flipSpeed) o->flipX = o->flipBase != (vx > 0);
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
            "  text <group> <key> <text>  set or add one text (res.getString / drawString read it at once); texts [group] lists the groups or one group's texts\n"
            "  gravity [x y]           show / set the world's gravity; maxtranslation [t] the longest move of a body in one step\n"
            "  scale <name> <sx> [sy]  setScale on an object; speedcap <name> <speed> sets setSpeedCap; blend <name> <mode>; shader <name> <effect>\n"
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
    } else if (cmd == "text") {
        // text <group> <key> <the text>: puts the text in the group (made if it is new); the game reads it the next time it asks
        std::istringstream ss(arg);
        std::string group, key;
        ss >> group >> key;
        std::string value;
        std::getline(ss, value);
        if (!value.empty() && value[0] == ' ') value.erase(0, 1);
        if (group.empty() || key.empty()) std::printf("console: usage: text <group> <key> <text>\n");
        else { textGroups_[group].strings[key] = value; std::printf("console: %s.%s = %s\n", group.c_str(), key.c_str(), value.c_str()); }
    } else if (cmd == "texts") {
        if (arg.empty()) { for (const auto& g : textGroups_) std::printf("  %s (%d texts)\n", g.first.c_str(), (int)g.second.strings.size()); }
        else { auto g = textGroups_.find(arg); if (g == textGroups_.end()) std::printf("console: no text group %s\n", arg.c_str()); else for (const auto& s : g->second.strings) std::printf("  %s = %s\n", s.first.c_str(), s.second.c_str()); }
    } else if (cmd == "gravity" || cmd == "maxtranslation") {
        float a = 0, b = 0; int n = std::sscanf(arg.c_str(), "%f %f", &a, &b);
        if (cmd == "gravity") { if (n >= 2) Physics().SetGravity(a, b); float gx, gy; Physics().GetGravity(gx, gy); std::printf("console: gravity %g %g\n", gx, gy); }
        else if (n >= 1) { Physics().SetMaxTranslation(a); std::printf("console: max translation %g\n", a); }
        else std::printf("console: usage: maxtranslation <t>\n");
    } else if (cmd == "scale" || cmd == "speedcap" || cmd == "blend" || cmd == "shader") {
        std::istringstream ss(arg);
        std::string name, w1, w2;
        ss >> name >> w1 >> w2;
        if (name.empty() || w1.empty()) std::printf("console: usage: %s <object> <value>\n", cmd.c_str());
        else if (cmd == "scale") { float sx = (float)std::atof(w1.c_str()); Physics().SetObjectScale(name, sx, w2.empty() ? sx : (float)std::atof(w2.c_str())); }
        else if (cmd == "speedcap") Physics().SetSpeedCap(name, (float)std::atof(w1.c_str()), 0, 0);
        else {
            int v = std::atoi(w1.c_str());
            if (cmd == "blend") { if (w1 == "add") v = 1; else if (w1 == "multiply") v = 2; else if (w1 == "screen") v = 3; SetRenderOverride(name, 7, (float)v, 0, 0); }
            else { static const char* names[] = {"none", "grayscale", "sepia", "invert", "chromatic", "glow"}; for (int k = 0; k < 6; k++) if (w1 == names[k]) v = k; SetRenderOverride(name, 8, (float)v, 0, 0); }
        }
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

// A sprite with the shape of another one, filled with one colour: every pixel the source draws (alpha > 0) takes the colour and keeps its alpha times
// the colour's. With an outline width only the pixels that close to the edge of the shape are kept. Returns the number of pixels of the shape, -1 when
// the source sprite or its picture cannot be found.
int GamePlatform::CreateSilhouette(const std::string& name, const std::string& source, float r, float g, float b, float a, int outline) {
    if (!backend_) return -1;
    const SpriteSheet* sheet = nullptr;
    const Sprite* sp = sprites_.Find(source, &sheet);
    if (!sp || !sheet) return -1;
    Image whole;
    std::string error;
    if (!LoadPngFile(sheet->directory + "/" + sheet->image, whole, &error)) { Print("createSilhouette: " + error); return -1; }
    const int w = sp->w, h = sp->h;
    if (w <= 0 || h <= 0) return -1;
    auto channel = [](float v) { return (uint8_t)std::lround(std::min(255.0f, std::max(0.0f, v > 1.0f ? v : v * 255.0f))); };
    const uint8_t cr = channel(r), cg = channel(g), cb = channel(b);
    const float ca = a > 1.0f ? std::min(a, 255.0f) / 255.0f : std::max(0.0f, a);
    auto alphaAt = [&](int x, int y) -> int {
        if (x < 0 || y < 0 || x >= w || y >= h) return 0;
        int sx = sp->x + x, sy = sp->y + y;
        if (sx >= whole.width || sy >= whole.height) return 0;
        return whole.rgba[((size_t)sy * whole.width + sx) * 4 + 3];
    };
    Image out;
    out.width = w; out.height = h;
    out.rgba.assign((size_t)w * h * 4, 0);
    int count = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int al = alphaAt(x, y);
            if (al == 0) continue;
            count++;
            if (outline > 0) {                           // keep only pixels with a see-through pixel within `outline` pixels
                bool edge = false;
                for (int dy = -outline; dy <= outline && !edge; dy++)
                    for (int dx = -outline; dx <= outline && !edge; dx++)
                        if (alphaAt(x + dx, y + dy) == 0) edge = true;
                if (!edge) continue;
            }
            uint8_t* px = &out.rgba[((size_t)y * w + x) * 4];
            px[0] = cr; px[1] = cg; px[2] = cb; px[3] = (uint8_t)std::lround(al * ca);
        }
    TextureInfo info; info.handle = backend_->CreateTexture(out); info.width = w; info.height = h;
    textures_["canvas:/" + name] = info;
    AddRuntimeSprite(sprites_, name, w, h, sp->pivotX, sp->pivotY);
    return count;
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

namespace ab {
// themeSystem.spawnFGLayerParticles(id): the foreground particle layer (Endor's ewoks) with that spawnerId puts its particles on the screen
void GamePlatform::SpawnFGLayerParticles(int id) {
    auto it = fgParticles_.find(id);
    if (it == fgParticles_.end()) return;
    AddParticles(it->second, 1.0f, camX_, camY_, 0.0f, 0.0f, 0.0f, false, false);
}
}  // namespace ab
