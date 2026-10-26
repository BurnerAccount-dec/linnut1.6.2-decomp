// The game's physics world, reconstructed from AngryBirds.exe 1.6.2 and Angry Birds Rio.
//
// The world is built on Box2D, and the games do not use the same version: Angry Birds Classic and Seasons use
// Box2D 2.1.0 (third_party/box2d is 2.1.1, close enough) and Rio uses 2.2.0 (third_party/box2d22). Both have the
// exe's changed constants in b2Settings.h. Both are linked into the same executable (the 2.2.0 names carry a
// suffix, see b2Rename.h) and MakePhysicsWorld picks one.
//
// This header has no Box2D in it. PhysicsWorld holds what does not depend on the version (the object table, the
// joint records, every call that goes through a body); physics_world_b2.cpp, compiled once for each version,
// fills in the rest: creating bodies, shapes and joints, stepping, and the contact callbacks.
//
// Original code, for reference:
//   world creation      Game constructor: b2World(gravity (0, 20), doSleep = true), warm starting and
//                       continuous physics on (flags 0x101), filter = Game (0x40c440), listener = Game (0x40b670)
//   createBox/Circle/Polygon  0x411590 / 0x411c20 / 0x412230 (helpers) and the Lua wrappers
//   createJoint         0x40ecd0
//   checkJointLimits, setJointParameters, removeObject, setPosition ... see the methods below
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "game/collision_damage.h"
#include "game/frame_update.h"
#include "lua/platform.h"

namespace ab {

constexpr float kGravityX = 0.0f;
constexpr float kGravityY = 20.0f;       // +y is down in this game

// The Box2D version a world was built with.
enum class Box2DVersion { V21, V22 };

// A body with everything the engine does to one. Implemented once per Box2D version.
class PhysBody : public Body {
public:
    virtual float Angle() const = 0;
    virtual void SetAngularVelocity(float w) = 0;
    virtual void SetAwake(bool awake) = 0;               // false also zeroes the velocities
    virtual void SetStatic(bool isStatic) = 0;           // b2_staticBody or b2_dynamicBody
    virtual void SetTransform(float x, float y, float angle) = 0;
    virtual void WorldPoint(float lx, float ly, float& wx, float& wy) const = 0;
    virtual void LocalPoint(float wx, float wy, float& lx, float& ly) const = 0;
    virtual void ApplyImpulse(float ix, float iy, float px, float py) = 0;
    virtual void ApplyForce(float fx, float fy, float px, float py) = 0;
    virtual void SetSensor(bool on) = 0;                 // every fixture
    virtual bool Bounds(float& x0, float& y0, float& x1, float& y1) const = 0;   // union of the fixtures' boxes; false: no fixture
    virtual float LinearDamping() const = 0;
    virtual float AngularDamping() const = 0;
    virtual void SetAngularDamping(float d) = 0;
    virtual float ShapeRadius() const = 0;               // the first fixture's skin radius (polygons) or radius (circles)
};

// One game object (the exe's 0x90-byte struct).
struct PhysObject : FrameObject {
    std::string name;
    std::unique_ptr<PhysBody> pbody;   // the Box2D body; `body` (from FrameObject) points to the same object
    bool collision = true;       // +0x86  both objects need this set to collide
    bool drawWithBirds = false;  // +0x89  setObjectParameter(n, 1, 1): drawGameNative draws it in the bird pass
    std::string sprite;          // +0x68  setSprite, the name (the exe stores the sprite record)
    std::string texture;         // +0x6c  setTexture, the name (the exe stores the texture id)
    bool scaled = false;         // +0x94  setObjectParameter(n, 6, 1): drawn with scaleX/scaleY
    float scaleX = 1, scaleY = 1;  // +0x98..+0xa4  setObjectParameter(n, 5, v)
    bool flipX = false;          // Angry Birds Rio: setObjectParameter(n, 8, 1) draws the sprite mirrored
    bool disableImmovable = false, disableControllable = false;   // Rio ids 15 and 16: no collisions with immovable blocks / birds
    bool special999 = false;     // +0x8a  circles created with z_order 999 (see ShouldCollide)
    float zOrder = 0;            // +0x80
    // Angry Birds Rio, from the exe's object record: setObjectParameter 6 / 7 (+0xd8 / +0xd4) are the bounce multipliers; an
    // impact starts a wobble of the draw scale (wobActive/wobT/wobAmp0/wobCur = +0xc4/+0xc8/+0xd0/+0xcc), wobDX/wobDY is its
    // current offset from the object's own scale
    float bounceAmp = 0, bounceFreq = 0;
    bool wobActive = false; float wobT = 0, wobAmp0 = 0, wobCur = 0, wobDX = 0, wobDY = 0;
    bool enableFlipping = false, flipBase = false;   // Rio ids 9 and 8: the sprite turns to face the way the object moves
    bool disableRotation = false;                    // Rio id 10 (+0x93): only the drawing ignores the angle, the body still turns
    bool canGetUp = false, gettingUp = false;        // Rio id 14 (+0x96) and the exe's "getting up" state (+0x9a)
    float getUpTimer = 0;                            // +0x9c
    int colliderType = 0;                            // setColliderType (Angry Birds 3.0.1 / Seasons): 0 basic, 1 eagle, 2 static, 3 bird, 4 pig, 5 ghost, 6 sticky
};

// The contact that is being reported, as far as the game code touches it.
class ContactHandle {
public:
    virtual ~ContactHandle() {}
    virtual void SetEnabled(bool on) = 0;
};

// Everything about a body that the tests look at.
struct BodyInfo {
    bool valid = false, isStatic = false, awake = false;
    float posX = 0, posY = 0, angle = 0, velX = 0, velY = 0, angularVelocity = 0;
    float linearDamping = 0, angularDamping = 0, mass = 0, shapeRadius = 0;
};

class PhysicsWorld {
public:
    virtual ~PhysicsWorld() {}

    virtual Box2DVersion Version() const = 0;
    // the skin of the boxes and polygons that are created from now on (Box2D 2.2.0 only; 0 for 2.1.1, which has none)
    virtual void SetPolygonRadius(float r) { (void)r; }
    virtual float PolygonRadius() const { return 0; }

    // ---- creation. Each returns the body mass (the original writes it to objects.world[name].mass).
    virtual float CreateBox(const BodyDef& d) = 0;
    virtual float CreateCircle(const BodyDef& d) = 0;
    virtual float CreatePolygon(const BodyDef& d, const std::vector<float>& verts) = 0;

    // Joints. The caller has already applied the table defaults (see JointDef in platform.h and the
    // list in ENGINE_SPEC.md). Returns false if an end object is missing (the original creates nothing).
    virtual bool CreateJoint(JointDef& d) = 0;          // may rewrite x1..y2, see the .cpp
    virtual void SetJointParameters(const JointParams& p) = 0;
    virtual void CheckJointLimits(const std::string& name) = 0;
    void DestroyJoint(const std::string& name);

    // Returns the names of the type-5 "link" joints that fired (the caller removes joints[name] from Lua).
    std::vector<std::string> RemoveObject(const std::string& name);

    void ApplyImpulse(const std::string& n, float ix, float iy, float px, float py);
    void ApplyForce(const std::string& n, float fx, float fy, float px, float py);
    bool SetPosition(const std::string& n, float x, float y);                // false: no such object
    bool SetRotation(const std::string& n, float angle, float& normalizedAngle);
    void SetVelocity(const std::string& n, float vx, float vy);
    void SetAngularVelocity(const std::string& n, float w);
    void SetSleeping(const std::string& n, bool sleeping);
    float GetAngle(const std::string& n) const;                              // interpolated angle
    void GetWorldPoint(const std::string& n, float lx, float ly, float& wx, float& wy) const;
    // setObjectParameter (0x40ec00): id 1 -> drawWithBirds = (value == 1); id 2 -> body type static if
    // value is 0, dynamic otherwise. id 5 -> the draw scale; id 6 -> use that scale (value == 1). Other ids do nothing.
    void SetObjectParameter(const std::string& n, int id, float value);
    // setMaterial (0x4125f0): "wood*" -> 1, "rock*" -> 2, "light*" -> 3 (a prefix test); anything else leaves it unchanged.
    void SetMaterial(const std::string& n, const std::string& material);
    void SetSpriteName(const std::string& n, const std::string& sprite);
    void SetTextureName(const std::string& n, const std::string& texture);
    // setMaterial: rolling sound material (1 wood, 2 rock, 3 light, 0 none), rolling flag and factor
    void SetRolling(const std::string& n, int material, bool rolls, float rollFactor);

    // Angry Birds Rio queries
    float GetSpeed(const std::string& n) const;                              // length of the linear velocity
    bool HasJoints(const std::string& n) const;                              // 0x413500: any joint with this object at one end
    bool HasMovingObjects(float tolerance) const;                            // any dynamic body faster than the tolerance
    void GetLocalPoint(const std::string& n, float wx, float wy, float& lx, float& ly) const;
    void SetCollisionEnabled(const std::string& n, bool on);
    // Collider types: every object has one (0 by default) and colliderRules_[a][b] says whether types a and b collide. All pairs collide,
    // except the ghost type (5), which only meets birds (3) and the eagle (1). Scripts change the pairs with setColliderRule.
    enum { kColliderTypes = 16 };
    void SetColliderType(const std::string& n, int type);
    int ColliderType(const std::string& n) const;
    void SetColliderRule(int a, int b, bool collide);
    bool ColliderRule(int a, int b) const;
    void RemoveJointsFromObject(const std::string& n);
    std::vector<std::string> IntersectingObjects(float x, float y, float left, float right, float down, float up) const;

    // loadLevel: destroys every body and joint and forgets the objects (0x40d2c0)
    void Clear();
    // The name of the i-th joint in Frame().joints (for writing x1..y2 into objects.joints[name])
    const std::string& JointName(int index) const { return joints_[index]->name; }

    void SetLevelLimits(float minX, float maxX) { frame_.levelMinX = (int)minX; frame_.levelMaxX = (int)maxX; }
    void SetRunning(bool on) { frame_.running = on; }

    // ---- stepping, for the frame update (FrameHost::StepWorld / ClearForces)
    virtual void Step(float dt, int velocityIterations, int positionIterations) = 0;
    virtual void ClearForces() = 0;
    virtual void SetGravity(float x, float y) = 0;
    virtual void GetGravity(float& x, float& y) const = 0;
    virtual BodyInfo Inspect(const std::string& name) const = 0;
    // motor speed and distance-joint frequency / damping of a joint, for the tests; false: no such joint
    virtual bool InspectJoint(const std::string& name, float& motorSpeed, float& frequency, float& dampingRatio) const = 0;

    FrameState& Frame() { return frame_; }
    PhysObject* Find(const std::string& name) const;
    const std::vector<std::unique_ptr<PhysObject>>& Objects() const { return objects_; }   // in creation order
    std::string NameOf(int id) const { auto it = idNames_.find(id); return it == idNames_.end() ? std::string() : it->second; }
    int BodyCount() const { return (int)objects_.size(); }

    // ---- hooks
    // Called from BeginContact for every contact whose two fixtures both have an object (the
    // original returns early otherwise). The Lua side decides the rest with ResolveContact.
    std::function<void(PhysObject& a, PhysObject& b, ContactHandle& c)> onBeginContact;
    // objects.world[name].material, read live by the filter. Return "" when absent.
    std::function<std::string(const PhysObject&)> materialOf;

    // Fills the physics part of a ContactObject (name, controllable, mass, velocity).
    static ContactObject MakeContactObject(const PhysObject& o);
    // Applies a VelocityChange from ResolveContact: immediate = setVelocity now, else after the next Step.
    void ApplyVelocityChange(const VelocityChange& v);

protected:
    struct JointRecord {
        std::string name, end1, end2;
        void* joint = nullptr;            // the Box2D joint, only the version's code looks inside
        int coordType = 0;
        std::unique_ptr<JointView> view;
    };
    // type-5 joints: no Box2D joint, just "when end1 is removed, end2 is destroyed after destroyTimer"
    struct Link {
        std::string name, end1, end2;
        float destroyTimer = 1;
    };

    // what the version's code needs from the shared part
    PhysObject* NewObject(const BodyDef& d, float rollFactor, bool circle);   // everything except the body
    void AddObject(PhysObject* o, bool bird);                                 // takes the object (the body is already set)
    JointRecord* FindJoint(const std::string& name);
    void RebuildJointViews();
    void DropJointRecord(void* joint);                                        // the Box2D joint is gone (SayGoodbye)
    bool ShouldCollideObjects(const PhysObject* a, const PhysObject* b) const;   // 0x40c440
    struct ColliderRules {
        bool pair[kColliderTypes][kColliderTypes];
        ColliderRules() {
            for (int a = 0; a < kColliderTypes; a++) for (int b = 0; b < kColliderTypes; b++) pair[a][b] = true;
            for (int t = 0; t < kColliderTypes; t++) if (t != 1 && t != 3) pair[5][t] = pair[t][5] = false;
        }
    } colliderRules_;
    virtual void DestroyBody(PhysObject* o) = 0;                              // also destroys the joints on the body
    virtual void DestroyBox2DJoint(void* joint) = 0;

    std::vector<std::unique_ptr<PhysObject>> objects_;
    std::map<std::string, PhysObject*> byName_;
    std::vector<std::unique_ptr<JointRecord>> joints_;
    std::vector<Link> links_;
    FrameState frame_;
    int nextId_ = 0;
    std::map<int, std::string> idNames_;
};

// Makes the world for a game: Rio uses Box2D 2.2.0, everything else 2.1.x.
std::unique_ptr<PhysicsWorld> MakePhysicsWorld(Box2DVersion version);

// One per Box2D version, defined in physics_world_b2.cpp
std::unique_ptr<PhysicsWorld> MakePhysicsWorld21();
std::unique_ptr<PhysicsWorld> MakePhysicsWorld22();

}  // namespace ab
