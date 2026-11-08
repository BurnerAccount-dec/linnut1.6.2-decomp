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
#include "physics/collision_prediction.h"
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

struct PhysObject;

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
    virtual void SetMaxSpeed(float s) { (void)s; }   // v2.4: this body's own limit instead of the world's max translation (0: the world's)
    virtual void Touching(std::vector<PhysObject*>& out) const { (void)out; }   // v2.4: the objects on the other side of this body's touching contacts right now
    virtual bool Bounds(float& x0, float& y0, float& x1, float& y1) const = 0;   // union of the fixtures' boxes; false: no fixture
    virtual float LinearDamping() const = 0;
    virtual float AngularDamping() const = 0;
    virtual void SetAngularDamping(float d) = 0;
    virtual void SetLinearDamping(float d) = 0;
    virtual void SetFixedRotation(bool on) = 0;
    virtual void SetRestitution(float r) { (void)r; }      // Star Wars setRestitution (0x419d10): the value goes into the body's shape data (+0x14 of the record at +100)
    virtual void SetDensity(float d) { (void)d; }          // v2.5: every fixture, then the mass is worked out again
    virtual void SetActive(bool on) = 0;
    virtual float ShapeRadius() const = 0;               // the first fixture's skin radius (polygons) or radius (circles)
    // The collision prediction's view of a body (predictCollision). Defaults describe a body that cannot be asked.
    virtual float Inertia() const { return 0; }          // rotational inertia about the centre of mass (0: it cannot spin, or static)
    virtual void CenterOfMass(float& x, float& y) const { x = PosX(); y = PosY(); }   // world position of the centre of mass
    virtual float Friction() const { return 0.2f; }      // the first non-sensor fixture's friction
    virtual float Restitution() const { return 0; }      // the first non-sensor fixture's restitution
    // Closest points of the two bodies' real shapes (non-sensor fixtures, every pair). (ax, ay) lies on this body's core shape, (bx, by) on the other's;
    // `dist` is the distance between the core shapes and radA / radB the skin radii to add to each; dist 0 means the cores overlap. false: not available.
    // Would this body's first fixture overlap the other body's first fixture if this body stood at (x, y) with its present angle? (0x49df50, what the
    // aiming aid asks of every gravitation sensor.) 1 overlapping, 0 apart, -1 the build cannot tell (Box2D 2.1) or a body has no fixture.
    virtual int OverlapAt(const PhysBody& other, float x, float y) const { (void)other; (void)x; (void)y; return -1; }
    virtual bool ClosestPoints(const PhysBody& other, float& ax, float& ay, float& bx, float& by, float& dist, float& radA, float& radB) const {
        (void)other; (void)ax; (void)ay; (void)bx; (void)by; (void)dist; (void)radA; (void)radB; return false;
    }
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
    bool noHitWobble = false;                        // Star Wars +0x150 (setObjectParameter 11): a bird-bird hit does not start this object's wobble
    float bounceAmp = 0, bounceFreq = 0;
    bool wobActive = false; float wobT = 0, wobAmp0 = 0, wobCur = 0, wobDX = 0, wobDY = 0;
    bool enableFlipping = false, flipBase = false;   // Rio ids 9 and 8: the sprite turns to face the way the object moves
    bool disableRotation = false;                    // Rio id 10 (+0x93): only the drawing ignores the angle, the body still turns
    bool canGetUp = false, gettingUp = false;        // Rio id 14 (+0x96) and the exe's "getting up" state (+0x9a)
    float getUpTimer = 0;                            // +0x9c
    int colliderType = 0;                            // setColliderType (Angry Birds 3.0.1 / Seasons): 0 basic, 1 eagle, 2 static, 3 bird, 4 pig, 5 ghost, 6 sticky
    std::string decoSprite; int decoAmount = 0; float decoIncrement = 0, decoScale = 1;   // Space setDecorationObjects (+0x158): `amount` copies of the sprite about the object, each turned `increment` degrees more
    float spriteAngle = 0;                           // Space setSpriteRotation (+0x114): added to the body's angle when the sprite is drawn
    float pivotOffX = 0, pivotOffY = 0;              // Space setPivotOffset (+0x118, +0x11c): moves the sprite's pivot, the point it turns about
    float textureScale = 1;                          // Space setTextureScale (+0x128): kept for the scripts, the tile draw does not use it yet
    float alpha = 1;                                 // setObjectAlpha (engine addition): drawGameNative draws the object with this transparency, 0 invisible .. 1 solid
    // Angry Birds Space (setObjectParameter 20..27, setSensorGravityMask, setObjectGravityCategory): a gravitation sensor pulls the dynamic objects inside it
    std::vector<CompoundShape> parts;                // a compound body: what each part looks like (drawn about the body)
    bool isSensor = false;                           // setAsSensor: contacts go to enterCollision / exitCollision instead of blockCollision
    bool revertGravity = false, revertMulti = false;   // setRevertGravity / setRevertGravityWithMultiplier (exe +0x18a, +0x18b)
    float revertMult = 0, revertMaxSpeed = 0;           // +0x18c (stored negated), +0x190
    int ownGravityMode = 0;                          // setObjectGravity / setObjectGravityScale: 0 the world's gravity, 1 an own vector, 2 the world's gravity times a factor
    float ownGravityX = 0, ownGravityY = 0;          // the vector of mode 1 (acceleration, world units per second squared)
    float ownGravityScale = 1;                       // the factor of mode 2 (-1 turns the gravity round: the sky becomes the ground)
    float birdGravReduce = -1;                       // Space id 31 (+0x184) timeSinceCollision: grows by 1/30 per physics step for controllable objects; their gravity multiplier is mult - 1.3 * this (never below 1)
    bool stopOnExit = false;                         // Space id 34 (+0x1a5): leaving a flagged sensor sets the linear damping to 100 (0x470380)
    int sensorKind = 0;                              // id 21: 2 gravitation, 3 stream (+0x15c)
    bool gravOn = false;                             // id 22 (+0x19f)
    int gravMask = -1, gravCat = -1;                 // setSensorGravityMask (+0x160) / setObjectGravityCategory (+0x180); -1: no filtering
    float gravExt = 0;                               // id 35 (+0x178): extent of a directional field
    float gravMin = 0, gravMax = 0, gravRadius = -1; // ids 25, 26, 27 (+0x168, +0x16c, +0x170); radius <= -1: a directional field
    bool spaceFlagA = false, spaceActive19e = false; // Space ids 19 (+0x188) and 20 (+0x19e), read by the contact filter at 0x4624e0
    // Angry Birds Star Wars (setObjectParameter 36, native_setBlockCollisionEnabled / setIgnoresScore / setKeepOrientation / setAffectedByForceMultipliers / setIsWater)
    int groupIndex = 0;                              // id 36 (+0xf4): two objects with the same group above 0 never collide
    bool blockCollision = true;                      // +0x154: false = collides only with the materials in objects.world[name].collisionMaterials
    bool ignoresScore = false, keepOrientation = false, affectedByForceMult = false, isWater = false;   // +0x166, +0x164, +0x165, +0x163
    float waterDensity = 0;                          // +0x168
    bool killOn = false; float killTimer = 0;       // +0x15b, +0xec: when set, objects.world[name].strength becomes 0 and the object is dead after killTimer seconds
    bool isCircle = false; float boxW = 0, boxH = 0; // +0x15c, +0xb0, +0xb4: the size the water code reads
    bool hidden = false;                             // Space id 33 (+0x1ab) = 0: isVisible false, not drawn
    float capSpeed = 0, capVx = 0, capVy = 0;        // v2.4 setSpeedCap: 0 = no limit, applied after every physics step
    bool zSet = false;                               // setObjectZOrder was used: the object is drawn by zOrder in every game (Rio always sorts by it)
};

// The contact that is being reported, as far as the game code touches it.
class ContactHandle {
public:
    virtual ~ContactHandle() {}
    virtual void SetEnabled(bool on) = 0;
    // the first world contact point and the normal (b2Contact::GetWorldManifold); all zero when the contact has no points
    virtual void WorldManifold(float& px, float& py, float& nx, float& ny) { px = py = nx = ny = 0; }
};

// Everything about a body that the tests look at.
struct BodyInfo {
    bool valid = false, isStatic = false, awake = false;
    float posX = 0, posY = 0, angle = 0, velX = 0, velY = 0, angularVelocity = 0;
    float linearDamping = 0, angularDamping = 0, mass = 0, shapeRadius = 0;
};

struct RayHit { PhysObject* obj; float px, py, nx, ny, fraction; };

class PhysicsWorld {
public:
    virtual ~PhysicsWorld() {}

    // v2.2: the game starts its levels with every block asleep (a level authored at rest stays still until something touches it);
    // a world used on its own (the tests) creates awake bodies
    bool startAsleep = false;
    // Angry Birds Space has its own setObjectParameter ids (0x47ac60) and its own contact filter (0x4624e0)
    bool spaceRules = false;
    bool ShouldCollide(const PhysObject* a, const PhysObject* b) const { return ShouldCollideObjects(a, b); }   // the contact filter (for tests)
    float waterForceMultiplier = 2.0f, birdWaterDrag = 0.4f, objectWaterDrag = 0, gameWorldScale = 1;   // game +0x3cc, +0x3d0, +0x3d4, +0x160 (defaults of the constructor at 0x43dedc)
    bool starWarsRules = false;                      // Star Wars: the contact filter at 0x410e70 (spaceRules is set too)
    std::function<std::vector<std::string>(const PhysObject&)> collisionMaterialsOf;   // objects.world[name].collisionMaterials, read by the filter for an object with blockCollision false

    virtual Box2DVersion Version() const = 0;
    // the skin of the boxes and polygons that are created from now on (Box2D 2.2.0 only; 0 for 2.1.1, which has none)
    virtual void SetPolygonRadius(float r) { (void)r; }
    virtual void SetSolverSettings(float linearSlop, float timeToSleep, float linearSleepTolerance) { (void)linearSlop; (void)timeToSleep; (void)linearSleepTolerance; }
    /// Space is built from a newer Box2D than Rio: its dynamic tree (rotations) and the circle case of the position solver differ, see b2_spaceBuild and b2_coincidentCircleNormal
    virtual void SetBox2DFlavour(bool space) { (void)space; }
    virtual float PolygonRadius() const { return 0; }

    // ---- creation. Each returns the body mass (the original writes it to objects.world[name].mass).
    virtual float CreateBox(const BodyDef& d) = 0;
    virtual float CreateCompound(const BodyDef& d, const std::vector<CompoundShape>& shapes, float damping) = 0;
    virtual float CreateCircle(const BodyDef& d) = 0;
    virtual float CreatePolygon(const BodyDef& d, const std::vector<float>& verts) = 0;
    // createLineShape (Star Wars, 0x439640): a body of edge shapes, one for each pair of neighbouring vertices (Box2D 2.2 only; the 2.1 build makes a polygon)
    virtual float CreateLine(const BodyDef& d, const std::vector<float>& verts) { return CreatePolygon(d, verts); }

    // Joints. The caller has already applied the table defaults (see JointDef in platform.h and the
    // list in ENGINE_SPEC.md). Returns false if an end object is missing (the original creates nothing).
    virtual bool CreateJoint(JointDef& d) = 0;          // may rewrite x1..y2, see the .cpp
    virtual void SetJointParameters(const JointParams& p) = 0;
    virtual void CheckJointLimits(const std::string& name, bool stop = false) = 0;   // stop: Star Wars handleJointLimits (0x410810) sets the motor speed to 0 instead of turning it round
    void DestroyJoint(const std::string& name);

    // Returns the names of the type-5 "link" joints that fired (the caller removes joints[name] from Lua).
    std::vector<std::string> RemoveObject(const std::string& name);

    void ApplyImpulse(const std::string& n, float ix, float iy, float px, float py);
    void ApplyForce(const std::string& n, float fx, float fy, float px, float py);
    bool SetPosition(const std::string& n, float x, float y);                // false: no such object
    bool SetRotation(const std::string& n, float angle, float& normalizedAngle);
    void SetVelocity(const std::string& n, float vx, float vy);
    void SetAngularVelocity(const std::string& n, float w);
    // Angry Birds Space natives that go through a body: cmd 1 linear damping, 2 angular damping, 3 fixed rotation, 4 active, 5 multiply the velocity by a,
    // 6 angular velocity (result), 7 sensor (a != 0: the body no longer collides, it only reports)
    // the gravitation sensors (0x46f720): called before every step
    void ApplyGravitySensors(float dt);
    // gravity of one object, independent of the world's (called from ApplyGravitySensors, before every step)
    void ApplyOwnGravity();
    // 0x41e440: the force and drag of water (a kind 2 sensor with setIsWater) on an object inside it
    void ApplyWater(PhysObject& w, PhysObject& o);
    bool SetOwnGravityVector(const std::string& n, float gx, float gy);
    bool SetOwnGravityScale(const std::string& n, float scale);
    bool ResetOwnGravity(const std::string& n);
    bool GetOwnGravity(const std::string& n, int& mode, float& gx, float& gy, float& scale) const;
    // vel: nullptr, or {aVx, aVy, bVx, bVy} where a NaN entry means "use the body's own velocity"
    CollisionPrediction PredictCollision(const std::string& a, const std::string& b, float restitution, const float* vel = nullptr) const;
    // The exe's list at obj+0x34: one entry per touching fixture pair (0x469ed0 is a plain push_back, 0x470380 erases the first match). Returns the
    // number of entries `other` has left, or -1 when an exit found no entry.
    int SensorPair(PhysObject* sen, PhysObject* other, bool enter);
    float BirdGravityFactor(float timer) const;
    bool GravityPull(const PhysObject& s, const PhysObject& t, float tx, float ty, float tvx, float tvy, float timer, float mass, float& fx, float& fy) const;
    // the aiming aid (updateBirdTrajectoryTable): the object flown on from its position and velocity under the gravitation sensors and its damping, one point every `every` steps
    void SimulateTrajectory(const std::string& n, int steps, int every, float h, std::vector<std::pair<float, float>>& out);
    // extension of the retail trail (which is the bird's free flight): the bird alone in a world with the world's gravity, launched with this velocity and stepped like the real world; no blocks, no ground
    virtual bool SimulateFlight(const std::string& bird, float vx, float vy, int steps, int every, float h, std::vector<std::pair<float, float>>& out, const float* start = nullptr) { (void)bird; (void)vx; (void)vy; (void)steps; (void)every; (void)h; (void)out; (void)start; return false; }
    float gravityForceMultiplier = 4;        // setGravityForceMultiplier (game +0x368)
    bool BodyCommand(const std::string& n, int cmd, float a, float& result);
    void SetSleeping(const std::string& n, bool sleeping);
    void SetRecordVelocity(const std::string& n, bool on);                  // Space setRecordVelocity (0x47a5f0): publish xVel / yVel / angle for this object
    bool GetVelocity(const std::string& n, float& vx, float& vy) const;      // the body's linear velocity; false (0, 0) for an unknown object
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
    void SetDecoration(const std::string& n, const std::string& sprite, int amount, float increment, float scale);
    void SetSpriteLook(const std::string& n, int what, float a, float b);   // 0 sprite angle, 1 pivot offset, 2 texture scale (Space natives)
    void SetObjectScale(const std::string& n, float sx, float sy);            // v2.4 setScale: the scale every game draws (turns scaled drawing on)
    bool GetObjectScale(const std::string& n, float& sx, float& sy) const;
    void SetSpeedCap(const std::string& n, float speed, float vx, float vy);  // v2.4 setSpeedCap
    bool GetSpeedCap(const std::string& n, float& speed, float& vx, float& vy) const;
    void ApplySpeedCaps();                                                     // after every Step
    std::vector<std::string> TouchingObjects(const std::string& n) const;      // v2.4 getTouchingObjects
    void SetObjectAlpha(const std::string& n, float a);       // 0..1, clamped
    float ObjectAlpha(const std::string& n) const;            // -1 if there is no such object
    void SetObjectZOrder(const std::string& n, float z);     // draw order: higher is drawn later (in front)
    float ObjectZOrder(const std::string& n) const;          // -1e9 if there is no such object
    float MoveObjectZ(const std::string& n, bool front);     // v2.2.3: in front of / behind every other object; returns the new z (-1e9: no such object)
    void SetColliderRule(int a, int b, bool collide);
    bool ColliderRule(int a, int b) const;
    void RemoveJointsFromObject(const std::string& n);
    std::vector<std::string> IntersectingObjects(float x, float y, float left, float right, float down, float up) const;

    // loadLevel: destroys every body and joint and forgets the objects (0x40d2c0)
    void Clear();
    // The name of the i-th joint in Frame().joints (for writing x1..y2 into objects.joints[name])
    const std::string& JointName(int index) const { return joints_[index]->name; }

    void SetLevelLimits(float minX, float maxX) { frame_.levelMinX = (int)minX; frame_.levelMaxX = (int)maxX; }
    void SetLevelLimitsY(float minY, float maxY) { frame_.levelMinY = (int)minY; frame_.levelMaxY = (int)maxY; }   // Space (0x462b10): all four limits are kept, truncated
    void SetRunning(bool on) { frame_.running = on; }

    // ---- stepping, for the frame update (FrameHost::StepWorld / ClearForces)
    virtual void Step(float dt, int velocityIterations, int positionIterations) = 0;
    virtual void ClearForces() = 0;
    virtual void SetGravity(float x, float y) = 0;
    // the scripts' setMaxTranslation: the longest distance a body moves in one step (2.0 when a level loads)
    virtual void SetMaxTranslation(float t) { (void)t; }
    virtual void GetGravity(float& x, float& y) const = 0;
    // Every non-sensor fixture the segment (x1, y1) - (x2, y2) crosses, in the order Box2D reports them (the callback at 0x495760 keeps going)
    virtual void RayCastAll(float x1, float y1, float x2, float y2, std::vector<RayHit>& out) const { (void)x1; (void)y1; (void)x2; (void)y2; (void)out; }
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
    std::function<void(PhysObject& a, PhysObject& b)> onEndContact;
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
        bool backAndForth = false;        // v2.2: a motor with limits that reverses by itself at each limit (the exe only has checkJointLimits for Lua to call)
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
    std::vector<std::pair<PhysObject*, PhysObject*>> sensorPairs_;   // Space: (gravitation sensor, touching object) contacts
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
