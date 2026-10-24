// The per-frame update writing into real Lua tables.
#include <cstdio>
#include <cmath>

#include "lua/lua_frame_host.h"

extern "C" {
#include "lauxlib.h"
}

using namespace ab;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

struct SimpleBody : Body {
    float x = 0, y = 0, vx = 0, vy = 0;
    bool IsStatic() const override { return false; }
    bool IsAwake() const override { return true; }
    bool HasJoints() const override { return false; }
    float PosX() const override { return x; }
    float PosY() const override { return y; }
    float RotCol1X() const override { return 1; }
    float RotCol1Y() const override { return 0; }
    float VelX() const override { return vx; }
    float VelY() const override { return vy; }
    float AngularVelocity() const override { return 0; }
    float Mass() const override { return 1; }
    void SetLinearVelocity(float a, float b) override { vx = a; vy = b; }
    void SetBullet(bool) override {}
};

struct SimpleJoint : JointView {
    bool PublishesEndpoints() const override { return true; }
    void Anchors(float& x1, float& y1, float& x2, float& y2) const override { x1 = 1; y1 = 2; x2 = 3; y2 = 4; }
};

struct TestHost : LuaFrameHost {
    explicit TestHost(ScriptHost& s) : LuaFrameHost(s) {}
    void StepWorld(float, int, int) override {}
    void ClearForces() override {}
    bool LoopActive(LoopSound) override { return false; }
    void StartLoop(LoopSound, float) override {}
    void SetLoopVolume(LoopSound, float) override {}
    void StopLoop(LoopSound) override {}
    void UpdateEffects(float) override {}
};

static double Num(lua_State* L, const char* chunk) {
    // evaluates `return <chunk>` inside the script environment
    std::string src = std::string("return ") + chunk;
    luaL_loadstring(L, src.c_str());
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_setfenv(L, -2);
    lua_pcall(L, 0, 1, 0);
    double v = lua_isboolean(L, -1) ? (double)lua_toboolean(L, -1) : lua_tonumber(L, -1);
    lua_pop(L, 1);
    return v;
}

int main() {
    Platform platform;
    BootConfig cfg;
    ScriptHost scripts(platform, cfg);
    lua_State* L = scripts.State();

    // a world with one block "a" and one joint "j", and the Lua callbacks the update calls
    const char* setup =
        "objects.world.a = { strength = 5 }\n"
        "objects.joints.j = {}\n"
        "updateCalls = 0\n"
        "function update(dt, real) updateCalls = updateCalls + 1; lastDt = dt end\n"
        "function removeBlocks() removeCalls = (removeCalls or 0) + 1 end\n";
    luaL_loadstring(L, setup);
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_setfenv(L, -2);
    CHECK(lua_pcall(L, 0, 0, 0) == 0);

    TestHost host(scripts);
    SimpleBody body; body.x = 4; body.y = 5; body.vx = 4;
    FrameObject o; o.id = host.AddObjectName("a"); o.body = &body;
    o.state[0][0] = 4; o.state[1][0] = 4; o.state[0][1] = 5; o.state[1][1] = 5;
    o.destroyPending = true; o.destroyTimer = 0.01f;
    SimpleJoint joint; host.AddJointName("j");
    FrameState s; s.running = true; s.levelMinX = -100; s.levelMaxX = 100;
    s.objects.push_back(&o); s.joints.push_back(&joint);

    UpdateFrame(s, host, 0.05f);

    CHECK(Num(L, "objects.world.a.x") == 4);
    CHECK(Num(L, "objects.world.a.y") == 5);
    CHECK(Num(L, "objects.world.a.sleeping") == 0);
    CHECK(Num(L, "objects.world.a.strength") == 0);           // destroy timer ran out
    CHECK(Num(L, "deadBlocks ~= nil and deadBlocks.a == objects.world.a") == 1);
    CHECK(Num(L, "hasAwakeObjects") == 1);
    CHECK(Num(L, "hasMovingObjects") == 1);                    // speed 4 -> 16 > 9
    CHECK(Num(L, "objects.joints.j.x2") == 3 && Num(L, "objects.joints.j.y1") == 2);
    CHECK(Num(L, "removeCalls") == 1);
    CHECK(Num(L, "updateCalls") == 1);
    CHECK(std::fabs(Num(L, "lastDt") - 0.05) < 1e-6);

    if (failures == 0) std::printf("lua_frame_host: all passed\n");
    return failures == 0 ? 0 : 1;
}
