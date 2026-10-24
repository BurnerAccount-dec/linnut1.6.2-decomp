// Hand-calculated checks for the per-frame update.
#include "game/frame_update.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace ab;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
static bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }

struct FakeBody : Body {
    bool isStatic = false, awake = true, joints = false, bullet = false;
    float px = 0, py = 0, angle = 0, vx = 0, vy = 0, w = 0, mass = 1;
    bool IsStatic() const override { return isStatic; }
    bool IsAwake() const override { return awake; }
    bool HasJoints() const override { return joints; }
    float PosX() const override { return px; }
    float PosY() const override { return py; }
    float RotCol1X() const override { return std::cos(angle); }
    float RotCol1Y() const override { return std::sin(angle); }
    float VelX() const override { return vx; }
    float VelY() const override { return vy; }
    float AngularVelocity() const override { return w; }
    float Mass() const override { return mass; }
    void SetLinearVelocity(float x, float y) override { vx = x; vy = y; }
    void SetBullet(bool on) override { bullet = on; }
};

struct FakeJoint : JointView {
    bool publishes = true;
    bool PublishesEndpoints() const override { return publishes; }
    void Anchors(float& x1, float& y1, float& x2, float& y2) const override { x1 = 1; y1 = 2; x2 = 3; y2 = 4; }
};

struct FakeHost : FrameHost {
    int steps = 0, removeBlocks = 0, clearForces = 0, updates = 0, effects = 0, dead = 0;
    float lastDt = 0, lastRealDt = 0;
    std::map<std::string, float> num;    // "id:key"
    std::map<std::string, bool> flag;    // "id:key" or "game:key"
    std::map<std::string, float> joint;
    bool active[3] = {false, false, false};
    float volume[3] = {0, 0, 0};
    int started[3] = {0, 0, 0}, stopped[3] = {0, 0, 0};
    std::vector<std::string> order;
    bool versionCalled = false;

    static std::string K(int id, const char* k) { return std::to_string(id) + ":" + k; }
    void StepWorld(float, int vi, int pi) override { steps++; CHECK(vi == 10 && pi == 10); order.push_back("step"); }
    void ClearForces() override { clearForces++; order.push_back("clear"); }
    void CallRemoveBlocks() override { removeBlocks++; order.push_back("remove"); }
    void SetObjectNumber(int id, const char* k, float v) override { num[K(id, k)] = v; }
    void SetObjectBool(int id, const char* k, bool v) override { flag[K(id, k)] = v; }
    void SetGameBool(const char* k, bool v) override { flag[std::string("game:") + k] = v; }
    void SetJointNumber(int i, const char* k, float v) override { joint[std::to_string(i) + k] = v; }
    void MarkBlockDead(int) override { dead++; }
    bool LoopActive(LoopSound s) override { return active[s]; }
    void StartLoop(LoopSound s, float v) override { active[s] = true; volume[s] = v; started[s]++; }
    void SetLoopVolume(LoopSound s, float v) override { volume[s] = v; }
    void StopLoop(LoopSound s) override { active[s] = false; stopped[s]++; }
    void LatestVersionNumberReceived() override { versionCalled = true; }
    void CallUpdate(float dt, float real) override { updates++; lastDt = dt; lastRealDt = real; order.push_back("update"); }
    void UpdateEffects(float) override { effects++; }
};

static FrameObject MakeObj(int id, FakeBody* b) {
    FrameObject o;
    o.id = id;
    o.body = b;
    return o;
}

int main() {
    // 1. less than one step of time: no step, still calls update, snapshot untouched
    {
        FakeBody b; FrameObject o = MakeObj(1, &b);
        o.state[0][0] = 10; o.state[1][0] = 20;           // buffer 0 is "current", 1 is "previous"
        FrameState s; s.running = true; s.objects = {&o};
        FakeHost h;
        UpdateFrame(s, h, 0.01f);
        CHECK(h.steps == 0 && h.removeBlocks == 0);
        CHECK(h.clearForces == 1 && h.updates == 1 && h.effects == 1);
        CHECK(Near(h.lastDt, 0.01f) && Near(h.lastRealDt, 0.01f));
        // alpha = 0.01 / (1/30) = 0.3 -> x = 10*0.3 + 20*0.7 = 17
        CHECK(Near(o.interpX, 17.0f));
        CHECK(Near(h.num["1:x"], 17.0f));
    }
    // 2. one step: 0.05 -> one step, 0.016667 left (<= 2/30) so the buffer flips and a snapshot is taken
    {
        FakeBody b; b.px = 5; b.py = 6; b.angle = 0.5f;
        FrameObject o = MakeObj(1, &b);
        FrameState s; s.running = true; s.objects = {&o};
        FakeHost h;
        UpdateFrame(s, h, 0.05f);
        CHECK(h.steps == 1 && h.removeBlocks == 1);
        CHECK(s.bufferIndex == 1);
        CHECK(Near(o.state[1][0], 5) && Near(o.state[1][1], 6) && Near(o.state[1][2], 0.5f));
        CHECK(Near(s.accumulator, 0.05f - kFixedStep));
        // step comes before removeBlocks before update
        CHECK(h.order[0] == "step" && h.order[1] == "remove" && h.order.back() == "update");
    }
    // 3. 0.15: four steps, snapshot only in the last two (0.05, 0.01667 left) -> two flips
    {
        FakeBody b; FrameObject o = MakeObj(1, &b);
        FrameState s; s.running = true; s.objects = {&o};
        FakeHost h;
        UpdateFrame(s, h, 0.15f);
        CHECK(h.steps == 4);
        CHECK(s.bufferIndex == 0);
    }
    // 4. deferred bird velocity: applied after the step, flag cleared; static bodies are left alone
    {
        FakeBody bird; FakeBody wall; wall.isStatic = true;
        FrameObject o = MakeObj(1, &bird); o.isBird = true; o.pendingVelocity = true; o.pendingVx = 3; o.pendingVy = -4;
        FrameObject w = MakeObj(2, &wall); w.pendingVelocity = true; w.pendingVx = 9; w.pendingVy = 9;
        FrameState s; s.running = true; s.objects = {&o, &w}; s.birds = {&o, &w};
        FakeHost h;
        UpdateFrame(s, h, 0.04f);
        CHECK(Near(bird.vx, 3) && Near(bird.vy, -4) && !o.pendingVelocity);
        CHECK(wall.vx == 0 && wall.vy == 0 && !w.pendingVelocity);
        CHECK(bird.bullet);                    // birds with mass get continuous collision
        CHECK(Near(h.num["1:xVel"], 3) && Near(h.num["1:yVel"], -4));
    }
    // 5. angle interpolation wraps the short way round
    {
        FakeBody b; b.angle = -3.0f;   // current snapshot will be taken from this
        FrameObject o = MakeObj(1, &b);
        o.state[0][2] = 3.0f;          // previous (buffer 0), becomes "prev" after the flip
        FrameState s; s.running = true; s.objects = {&o};
        FakeHost h;
        UpdateFrame(s, h, 0.05f);      // 1 step, accumulator 0.016667, alpha 0.5
        // cur = -3, prev = 3: |diff| = 6 >= pi, cur <= prev so prev -> 3 - 2pi = -3.2832; mid = -3.1416
        CHECK(Near(o.interpAngle, -3.1415927f, 1e-3f));
    }
    // 6. leaving the level freezes dynamic objects only
    {
        FakeBody b1; b1.px = -50; b1.mass = 1;
        FakeBody b2; b2.px = 0; b2.py = 25; b2.mass = 1;
        FakeBody b3; b3.px = 100; b3.mass = 1;
        FakeBody b4; b4.px = -50; b4.mass = 0;
        FakeBody b5; b5.px = 5; b5.py = 5; b5.mass = 1;
        FrameObject o1 = MakeObj(1, &b1), o2 = MakeObj(2, &b2), o3 = MakeObj(3, &b3), o4 = MakeObj(4, &b4), o5 = MakeObj(5, &b5);
        FrameObject* all[] = {&o1, &o2, &o3, &o4, &o5};
        FrameState s; s.running = true; s.levelMinX = -10; s.levelMaxX = 50;
        for (FrameObject* o : all) { o->state[0][0] = o->state[1][0] = o->body->PosX(); o->state[0][1] = o->state[1][1] = o->body->PosY(); s.objects.push_back(o); }
        FakeHost h;
        UpdateFrame(s, h, 0.04f);
        CHECK(h.flag["1:frozen"]);   // x < min
        CHECK(h.flag["2:frozen"]);   // y > 20
        CHECK(h.flag["3:frozen"]);   // x > max
        CHECK(!h.flag.count("4:frozen"));  // mass 0
        CHECK(!h.flag.count("5:frozen"));  // inside
    }
    // 7. moving / awake flags and sleeping
    {
        FakeBody fast; fast.vx = 4;                        // speed^2 = 16 > 9
        FakeBody slow; slow.vx = 0.03f;                    // 0.0009 > 0.0005, below 9
        FakeBody asleep; asleep.awake = false;
        FrameObject a = MakeObj(1, &fast), b = MakeObj(2, &slow), c = MakeObj(3, &asleep);
        FrameState s; s.running = true; s.objects = {&a, &b, &c};
        FakeHost h;
        UpdateFrame(s, h, 0.04f);
        CHECK(h.flag["game:hasMovingObjects"]);
        CHECK(h.flag["game:hasAwakeObjects"]);
        CHECK(h.flag["game:hasMovingObjectsAboveTolerance"]);
        CHECK(!h.flag["1:sleeping"] && h.flag["3:sleeping"]);

        FakeBody still; still.vx = 0.01f;                  // 0.0001, below tolerance
        FrameObject d = MakeObj(1, &still);
        FrameState s2; s2.running = true; s2.objects = {&d};
        FakeHost h2;
        UpdateFrame(s2, h2, 0.04f);
        CHECK(!h2.flag["game:hasMovingObjects"] && !h2.flag["game:hasMovingObjectsAboveTolerance"]);
        CHECK(h2.flag["game:hasAwakeObjects"]);
    }
    // 8. an object that just fell asleep is still written once (wasAwake), then not again
    {
        FakeBody b; b.awake = false;
        FrameObject o = MakeObj(1, &b); o.wasAwake = true;
        FrameState s; s.running = true; s.objects = {&o};
        FakeHost h;
        UpdateFrame(s, h, 0.04f);
        CHECK(h.num.count("1:x") == 1);
        CHECK(!o.wasAwake);
        FakeHost h2;
        UpdateFrame(s, h2, 0.04f);
        CHECK(h2.num.count("1:x") == 0);
        CHECK(!h2.flag["game:hasAwakeObjects"]);
    }
    // 9. rolling volume: |w| * factor / 400 * mass, clamped to 1, loudest object per material
    {
        FakeBody a; a.w = -100; a.mass = 2;                // 100 * 2 / 400 * 2 = 1.0 -> clamp 1.0
        FakeBody c; c.w = 40; c.mass = 1;                  // 40 * 2 / 400 * 1 = 0.2
        FakeBody d; d.w = 20; d.mass = 1;                  // 0.1
        FrameObject oa = MakeObj(1, &a), oc = MakeObj(2, &c), od = MakeObj(3, &d);
        oa.rolls = oc.rolls = od.rolls = true;
        oa.rollFactor = oc.rollFactor = od.rollFactor = 2;
        oa.rollMaterial = kRollWood; oc.rollMaterial = kRollWood; od.rollMaterial = kRollLight;
        FrameState s; s.running = true; s.objects = {&oa, &oc, &od};
        FakeHost h;
        UpdateFrame(s, h, 0.04f);
        CHECK(h.active[kLoopWood] && Near(h.volume[kLoopWood], 1.0f));
        CHECK(!h.active[kLoopRock] && h.stopped[kLoopRock] == 1);
        CHECK(h.active[kLoopLight] && Near(h.volume[kLoopLight], 0.1f));
        UpdateFrame(s, h, 0.04f);                          // already playing: only the volume is set
        CHECK(h.started[kLoopWood] == 1);
    }
    // 10. destroy timer
    {
        FakeBody b; FrameObject o = MakeObj(1, &b);
        o.destroyPending = true; o.destroyTimer = 0.05f;
        FrameState s; s.running = true; s.objects = {&o};
        FakeHost h;
        UpdateFrame(s, h, 0.03f);
        CHECK(h.dead == 0 && o.destroyPending);
        UpdateFrame(s, h, 0.03f);
        CHECK(h.dead == 1 && !o.destroyPending);
    }
    // 11. joints
    {
        FakeJoint j1, j2; j2.publishes = false;
        FrameState s; s.running = true; s.joints = {&j1, &j2};
        FakeHost h;
        UpdateFrame(s, h, 0.04f);
        CHECK(Near(h.joint["0x1"], 1) && Near(h.joint["0y1"], 2) && Near(h.joint["0x2"], 3) && Near(h.joint["0y2"], 4));
        CHECK(h.joint.count("1x1") == 0);
    }
    // 12. not running: no physics, all loops stopped, update still called, effects not
    {
        FrameState s; s.running = false;
        FakeHost h;
        UpdateFrame(s, h, 0.1f);
        CHECK(h.steps == 0 && h.clearForces == 0 && h.effects == 0 && h.updates == 1);
        CHECK(h.stopped[0] == 1 && h.stopped[1] == 1 && h.stopped[2] == 1);
        CHECK(s.accumulator == 0);
    }
    // 13. version flags
    {
        FrameState s; s.running = false; s.versionReceived = true;
        FakeHost h;
        UpdateFrame(s, h, 0.01f);
        CHECK(h.versionCalled && !s.versionReceived);
    }
    // 14. zoom
    {
        CHECK(Near(StepZoom(1.0f, 4.0f, 2.0f), 2.0f));
        CHECK(Near(StepZoom(1.0f, 3.0f, 3.0f), 1.0f));
    }

    if (failures == 0) std::printf("frame_update: all passed\n");
    return failures == 0 ? 0 : 1;
}
