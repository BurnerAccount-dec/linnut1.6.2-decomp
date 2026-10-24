// Per-frame update, reconstructed from AngryBirds.exe 1.6.2
// (original: Game::update, 0x00408980, "game_updateFrame").
//
// What the original does each frame, in order:
//   1. Fixed-step physics: accumulator += dt; while accumulator > 1/30 { Step(1/30, 10, 10);
//      accumulator -= 1/30; Lua removeBlocks(); apply deferred bird velocities;
//      if accumulator <= 2/30 { flip the double buffer and snapshot every awake body } }
//   2. ClearForces, then alpha = accumulator / (1/30).
//   3. For every object: interpolate position/angle between the two snapshots (awake bodies only),
//      and count down the destroy timer (block goes into deadBlocks when it runs out).
//   4. For every object that is awake (or was awake last frame): write x, y into its Lua table,
//      mark it "frozen" if it left the level, work out the moving/awake flags, write xVel/yVel for
//      birds, write angle and sleeping, and collect the rolling-sound volumes.
//   5. Publish hasMovingObjects / hasAwakeObjects / hasMovingObjectsAboveTolerance.
//   6. Write x1,y1,x2,y2 of every joint into objects.joints[name].
//   7. Start/stop/adjust the three rolling loops (wood, rock, light).
//   8. Version-check callbacks, then Lua update(dt, dt), then the effects manager update.
//
// This file contains no Box2D and no Lua: the caller supplies both through Body and FrameHost.
#pragma once
#include <vector>

namespace ab {

constexpr float kFixedStep = 0.033333335f;     // 1/30
constexpr float kSnapshotWindow = 0.06666667f; // snapshots only when this much time or less is left

// A Box2D 2.1.0 body, as far as the frame update touches it.
class Body {
public:
    virtual ~Body() {}
    virtual bool IsStatic() const = 0;          // m_type == b2_staticBody (the original tests m_type != 0)
    virtual bool IsAwake() const = 0;           // flags & e_awakeFlag (2)
    virtual bool HasJoints() const = 0;         // m_jointList != 0
    virtual float PosX() const = 0;
    virtual float PosY() const = 0;
    virtual float RotCol1X() const = 0;         // xf.R.col1.x  (cos angle)
    virtual float RotCol1Y() const = 0;         // xf.R.col1.y  (sin angle)
    virtual float VelX() const = 0;
    virtual float VelY() const = 0;
    virtual float AngularVelocity() const = 0;
    virtual float Mass() const = 0;
    virtual void SetLinearVelocity(float x, float y) = 0;
    virtual void SetBullet(bool on) = 0;        // e_bulletFlag (8)
};

// A joint; endpoints are what the two virtual calls on the original joint wrapper return.
class JointView {
public:
    virtual ~JointView() {}
    virtual bool PublishesEndpoints() const = 0;   // original: kind != 2
    virtual void Anchors(float& x1, float& y1, float& x2, float& y2) const = 0;
};

enum RollMaterial { kRollNone = 0, kRollWood = 1, kRollRock = 2, kRollLight = 3 };

// One entry of the game's object table (the original struct, only the fields this code uses).
struct FrameObject {
    int id = 0;                       // host handle for its Lua table (objects.world[name])
    Body* body = nullptr;
    int rollMaterial = kRollNone;     // +0x18
    bool isBird = false;              // +0x87
    bool rolls = false;               // +0x88  makes rolling noise
    float rollFactor = 0;             // +0x70
    // +0x84 .. +0x30: velocity change queued by the collision code, applied after the next Step
    bool pendingVelocity = false;
    float pendingVx = 0, pendingVy = 0;
    bool wasAwake = false;            // +0x85
    bool destroyPending = false;      // +0x8b
    float destroyTimer = 0;           // +0x8c
    // double-buffered state (x, y, angle) at +0x00 and +0x0c, and the interpolated result
    float state[2][3] = {{0, 0, 0}, {0, 0, 0}};
    float interpX = 0, interpY = 0, interpAngle = 0;   // +0x74, +0x78, +0x7c
};

enum LoopSound { kLoopWood = 0, kLoopRock = 1, kLoopLight = 2 };

// Everything outside the physics: Lua, audio, and the engine's other subsystems.
class FrameHost {
public:
    virtual ~FrameHost() {}
    virtual void StepWorld(float dt, int velocityIterations, int positionIterations) = 0;
    virtual void ClearForces() = 0;
    virtual void CallRemoveBlocks() = 0;                       // Lua removeBlocks()
    virtual void LogBirdVelocity(int /*objectId*/, float /*vx*/, float /*vy*/) {}  // "Bird %s new velocity is x: %f, y: %f"

    // Lua table writes. key is the exact string the original uses.
    virtual void SetObjectNumber(int objectId, const char* key, float value) = 0;  // x y xVel yVel angle
    virtual void SetObjectBool(int objectId, const char* key, bool value) = 0;     // frozen sleeping
    virtual void SetGameBool(const char* key, bool value) = 0;                     // hasMovingObjects ...
    virtual void SetJointNumber(int jointIndex, const char* key, float value) = 0; // x1 y1 x2 y2
    // objects.world[name].strength = 0 and objects.deadBlocks[name] = <that table>
    virtual void MarkBlockDead(int objectId) = 0;

    // Rolling loops. Original: isClipActive(handle) / playAudioClip(name, volume, loop=1, 2) /
    // setClipVolume(handle, volume) / stopAudioClip(name).
    virtual bool LoopActive(LoopSound which) = 0;
    virtual void StartLoop(LoopSound which, float volume) = 0;
    virtual void SetLoopVolume(LoopSound which, float volume) = 0;
    virtual void StopLoop(LoopSound which) = 0;

    virtual void LatestVersionNumberReceived() {}   // Game+0x1c set
    virtual void UpdateAvailable(bool /*available*/) {} // Game+0x1d set, value at +0x1e
    virtual void CallUpdate(float dt, float realDt) = 0;  // Lua update(dt, dt)
    virtual void UpdateEffects(float dt) = 0;       // the object at Game+0x60 (0x41cec0), only while running
};

struct FrameState {
    bool running = false;             // Game+0x1f8: a level is loaded and the world exists
    float accumulator = 0;            // Game+0x204
    int bufferIndex = 0;              // Game+0xc8
    int levelMinX = 0, levelMaxX = 0; // Game+0x2a4 / +0x2a8, set by setLevelLimits (args 1 and 3)
    bool versionReceived = false;     // Game+0x1c
    bool updateNoticePending = false; // Game+0x1d
    bool updateNoticeValue = false;   // Game+0x1e
    std::vector<FrameObject*> objects;     // the object hashtable
    std::vector<FrameObject*> birds;       // the array at Game+0x180 checked for pending velocity
    std::vector<JointView*> joints;
};

void UpdateFrame(FrameState& s, FrameHost& host, float dt);

// "zoomLevel" handling at the top of the original: zoom += (current - target) * 0.5 when they differ.
float StepZoom(float zoomLevel, float current, float target);

}  // namespace ab
