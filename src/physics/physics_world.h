// The game's Box2D world, reconstructed from AngryBirds.exe 1.6.2.
//
// Wraps Box2D 2.1.1 (third_party/box2d, with the exe's four changed constants in b2Settings.h) and
// creates bodies, fixtures and joints with exactly the parameters the exe passes. The pieces that
// need Lua (the objects.world tables, the Lua callbacks) are left to hooks and return values, so this
// module has no Lua in it.
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

#include <Box2D/Box2D.h>

#include "game/collision_damage.h"
#include "game/frame_update.h"
#include "lua/platform.h"

namespace ab {

constexpr float kGravityX = 0.0f;
constexpr float kGravityY = 20.0f;       // +y is down in this game

// One game object (the exe's 0x90-byte struct).
struct PhysObject : FrameObject {
    std::string name;
    b2Body* b2 = nullptr;
    bool collision = true;       // +0x86  both objects need this set to collide
    bool drawWithBirds = false;  // +0x89  setObjectParameter(n, 1, 1): drawGameNative draws it in the bird pass
    std::string sprite;          // +0x68  setSprite, the name (the exe stores the sprite record)
    std::string texture;         // +0x6c  setTexture, the name (the exe stores the texture id)
    bool special999 = false;     // +0x8a  circles created with z_order 999 (see ShouldCollide)
    float zOrder = 0;            // +0x80
    std::unique_ptr<Body> adapter;
};

class PhysicsWorld : public b2ContactListener, public b2ContactFilter, public b2DestructionListener {
public:
    PhysicsWorld();
    ~PhysicsWorld() override;

    // ---- creation. Each returns the body mass (the original writes it to objects.world[name].mass).
    float CreateBox(const BodyDef& d);
    float CreateCircle(const BodyDef& d);
    float CreatePolygon(const BodyDef& d, const std::vector<float>& verts);

    // Joints. The caller has already applied the table defaults (see JointDef in platform.h and the
    // list in ENGINE_SPEC.md). Returns false if an end object is missing (the original creates nothing).
    bool CreateJoint(JointDef& d);          // may rewrite x1..y2, see the .cpp
    void SetJointParameters(const JointParams& p);
    void CheckJointLimits(const std::string& name);
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
    // value is 0, dynamic otherwise. Other ids do nothing.
    void SetObjectParameter(const std::string& n, int id, float value);
    // setMaterial (0x4125f0): "wood*" -> 1, "rock*" -> 2, "light*" -> 3 (a prefix test); anything else leaves it unchanged.
    void SetMaterial(const std::string& n, const std::string& material);
    void SetSpriteName(const std::string& n, const std::string& sprite);
    void SetTextureName(const std::string& n, const std::string& texture);
    // setMaterial: rolling sound material (1 wood, 2 rock, 3 light, 0 none), rolling flag and factor
    void SetRolling(const std::string& n, int material, bool rolls, float rollFactor);

    // loadLevel: destroys every body and joint and forgets the objects (0x40d2c0)
    void Clear();
    // The name of the i-th joint in Frame().joints (for writing x1..y2 into objects.joints[name])
    const std::string& JointName(int index) const { return joints_[index]->name; }

    void SetLevelLimits(float minX, float maxX) { frame_.levelMinX = (int)minX; frame_.levelMaxX = (int)maxX; }
    void SetRunning(bool on) { frame_.running = on; }

    // ---- stepping, for the frame update (FrameHost::StepWorld / ClearForces)
    void Step(float dt, int velocityIterations, int positionIterations) { world_.Step(dt, velocityIterations, positionIterations); }
    void ClearForces() { world_.ClearForces(); }

    FrameState& Frame() { return frame_; }
    PhysObject* Find(const std::string& name) const;
    const std::vector<std::unique_ptr<PhysObject>>& Objects() const { return objects_; }   // in creation order
    b2World& World() { return world_; }
    std::string NameOf(int id) const { auto it = idNames_.find(id); return it == idNames_.end() ? std::string() : it->second; }
    int BodyCount() const { return (int)objects_.size(); }

    // ---- hooks
    // Called from BeginContact for every contact whose two fixtures both have an object (the
    // original returns early otherwise). The Lua side decides the rest with ResolveContact.
    std::function<void(PhysObject& a, PhysObject& b, b2Contact* c)> onBeginContact;
    // objects.world[name].material, read live by the filter. Return "" when absent.
    std::function<std::string(const PhysObject&)> materialOf;

    // Fills the physics part of a ContactObject (name, controllable, mass, velocity).
    static ContactObject MakeContactObject(const PhysObject& o);
    // Applies a VelocityChange from ResolveContact: immediate = setVelocity now, else after the next Step.
    void ApplyVelocityChange(const VelocityChange& v);

    // ---- Box2D callbacks
    void BeginContact(b2Contact* contact) override;
    bool ShouldCollide(b2Fixture* a, b2Fixture* b) override;
    void SayGoodbye(b2Joint* joint) override;
    void SayGoodbye(b2Fixture*) override {}

private:
    struct JointRecord {
        std::string name, end1, end2;
        b2Joint* joint = nullptr;
        int coordType = 0;
        std::unique_ptr<JointView> view;
    };
    // type-5 joints: no Box2D joint, just "when end1 is removed, end2 is destroyed after destroyTimer"
    struct Link {
        std::string name, end1, end2;
        float destroyTimer = 1;
    };

    PhysObject* AddObject(const BodyDef& d, float rollFactor, bool circle);
    JointRecord* FindJoint(const std::string& name);
    void RebuildJointViews();

    b2World world_;
    std::vector<std::unique_ptr<PhysObject>> objects_;
    std::map<std::string, PhysObject*> byName_;
    std::vector<std::unique_ptr<JointRecord>> joints_;
    std::vector<Link> links_;
    FrameState frame_;
    int nextId_ = 0;
    std::map<int, std::string> idNames_;
};

}  // namespace ab
