#include "physics/physics_world.h"

#include <cmath>

namespace ab {

namespace {

const float kTwoPi = 6.2831855f;

class B2BodyAdapter : public Body {
public:
    explicit B2BodyAdapter(b2Body* b) : b_(b) {}
    bool IsStatic() const override { return b_->GetType() == b2_staticBody; }
    bool IsAwake() const override { return b_->IsAwake(); }
    bool HasJoints() const override { return b_->GetJointList() != nullptr; }
    float PosX() const override { return b_->GetPosition().x; }
    float PosY() const override { return b_->GetPosition().y; }
    float RotCol1X() const override { return b_->GetTransform().R.col1.x; }
    float RotCol1Y() const override { return b_->GetTransform().R.col1.y; }
    float VelX() const override { return b_->GetLinearVelocity().x; }
    float VelY() const override { return b_->GetLinearVelocity().y; }
    float AngularVelocity() const override { return b_->GetAngularVelocity(); }
    float Mass() const override { return b_->GetMass(); }
    void SetLinearVelocity(float x, float y) override { b_->SetLinearVelocity(b2Vec2(x, y)); }
    void SetBullet(bool on) override { b_->SetBullet(on); }

private:
    b2Body* b_;
};

// The two end points the exe publishes to objects.joints[name].x1..y2 (anchor A, anchor B).
class B2JointView : public JointView {
public:
    B2JointView(b2Joint* j, int coordType) : j_(j), coordType_(coordType) {}
    bool PublishesEndpoints() const override { return coordType_ != 2; }
    void Anchors(float& x1, float& y1, float& x2, float& y2) const override {
        b2Vec2 a = j_->GetAnchorA(), b = j_->GetAnchorB();
        x1 = a.x; y1 = a.y; x2 = b.x; y2 = b.y;
    }

private:
    b2Joint* j_;
    int coordType_;
};

}  // namespace

PhysicsWorld::PhysicsWorld() : world_(b2Vec2(kGravityX, kGravityY), true) {
    world_.SetWarmStarting(true);
    world_.SetContinuousPhysics(true);
    world_.SetContactListener(this);
    world_.SetContactFilter(this);
    world_.SetDestructionListener(this);
}

PhysicsWorld::~PhysicsWorld() {
    // the world is destroyed after this body runs; keep the callbacks from touching freed records
    world_.SetDestructionListener(nullptr);
    world_.SetContactListener(nullptr);
    world_.SetContactFilter(nullptr);
}

PhysObject* PhysicsWorld::Find(const std::string& name) const {
    auto it = byName_.find(name);
    return it == byName_.end() ? nullptr : it->second;
}

PhysObject* PhysicsWorld::AddObject(const BodyDef& d, float rollFactor, bool circle) {
    // An object that already has this name is replaced in the hash table by the original; the old
    // body stays in the world. Scripts never reuse a name, so that case is not reproduced here.
    std::unique_ptr<PhysObject> o(new PhysObject());
    o->name = d.name;
    o->id = nextId_++;
    idNames_[o->id] = d.name;
    o->isBird = d.controllable;
    o->zOrder = d.zOrder;
    o->sprite = d.sprite;          // createBox/Circle/Polygon: the sprite is looked up when the body is created
    o->rollFactor = rollFactor;
    o->collision = circle ? true : d.collision;
    o->state[0][0] = o->state[1][0] = o->interpX = d.x;
    o->state[0][1] = o->state[1][1] = o->interpY = d.y;

    b2BodyDef bd;
    bd.type = d.density != 0.0f ? b2_dynamicBody : b2_staticBody;
    bd.position.Set(d.x, d.y);
    bd.angle = 0;
    bd.linearDamping = 0;
    bd.angularDamping = 1.0f;
    bd.allowSleep = true;
    bd.awake = true;
    bd.active = true;
    bd.userData = o.get();
    o->b2 = world_.CreateBody(&bd);
    o->adapter.reset(new B2BodyAdapter(o->b2));
    o->body = o->adapter.get();
    return o.release();
}

static void MakeFixture(b2Body* body, PhysObject* o, const b2Shape& shape, const BodyDef& d) {
    b2FixtureDef fd;
    fd.shape = &shape;
    fd.friction = d.friction;
    fd.restitution = d.restitution;
    fd.density = d.density;
    fd.filter.categoryBits = 1;
    fd.filter.maskBits = 0xFFFF;
    fd.filter.groupIndex = 0;
    fd.isSensor = false;
    fd.userData = o;
    body->CreateFixture(&fd);
}

float PhysicsWorld::CreateBox(const BodyDef& d) {
    PhysObject* o = AddObject(d, -1.0f, false);
    b2PolygonShape shape;
    shape.SetAsBox(d.w * 0.5f, d.h * 0.5f);
    MakeFixture(o->b2, o, shape, d);
    objects_.emplace_back(o);
    byName_[d.name] = o;
    frame_.objects.push_back(o);
    return o->b2->GetMass();
}

float PhysicsWorld::CreateCircle(const BodyDef& d) {
    PhysObject* o = AddObject(d, d.w, true);
    b2CircleShape shape;
    shape.m_radius = d.w;
    shape.m_p.Set(0, 0);
    MakeFixture(o->b2, o, shape, d);
    // GUESS: the exe tests the z_order argument here (ftol(...) == 999). Check on a real level.
    o->special999 = (int)d.zOrder == 999;
    if (d.controllable) {
        o->b2->SetAngularDamping(2.0f);
        frame_.birds.push_back(o);
    }
    objects_.emplace_back(o);
    byName_[d.name] = o;
    frame_.objects.push_back(o);
    return o->b2->GetMass();
}

float PhysicsWorld::CreatePolygon(const BodyDef& d, const std::vector<float>& verts) {
    PhysObject* o = AddObject(d, -1.0f, false);
    std::vector<b2Vec2> pts;
    for (size_t i = 0; i + 1 < verts.size(); i += 2) pts.push_back(b2Vec2(verts[i], verts[i + 1]));
    b2PolygonShape shape;
    shape.Set(pts.data(), (int)pts.size());
    MakeFixture(o->b2, o, shape, d);
    if (d.controllable) o->b2->SetAngularDamping(2.0f);
    objects_.emplace_back(o);
    byName_[d.name] = o;
    frame_.objects.push_back(o);
    return o->b2->GetMass();
}

// ---- joints

PhysicsWorld::JointRecord* PhysicsWorld::FindJoint(const std::string& name) {
    for (auto& j : joints_)
        if (j->name == name) return j.get();
    return nullptr;
}

void PhysicsWorld::RebuildJointViews() {
    frame_.joints.clear();
    for (auto& j : joints_) frame_.joints.push_back(j->view.get());
}

// 0x40ecd0. The anchors follow the exe literally, including its quirk for distance joints with
// coordType 1: the local anchor is (world point - body position), which ignores the body's rotation.
bool PhysicsWorld::CreateJoint(JointDef& d) {
    if (d.type == 5) {
        Link l;
        l.name = d.name; l.end1 = d.end1; l.end2 = d.end2; l.destroyTimer = d.destroyTimer;
        links_.push_back(l);
        return true;
    }
    PhysObject* o1 = Find(d.end1);
    PhysObject* o2 = Find(d.end2);
    if (!o1 || !o2) return false;
    b2Body* b1 = o1->b2;
    b2Body* b2b = o2->b2;
    b2Joint* joint = nullptr;

    switch (d.type) {
    case 1: {  // distance
        b2DistanceJointDef jd;
        jd.bodyA = b1;
        jd.bodyB = b2b;
        b2Vec2 pa = b1->GetPosition(), pb = b2b->GetPosition();
        if (d.coordType == 0) {
            jd.localAnchorA.Set(0, 0);
            jd.localAnchorB.Set(0, 0);
            jd.length = b2Distance(pa, pb);
            d.x1 = pa.x; d.y1 = pa.y; d.x2 = pb.x; d.y2 = pb.y;     // written back to the Lua table
        } else if (d.coordType == 1) {
            jd.localAnchorA.Set(d.x1 - pa.x, d.y1 - pa.y);
            jd.localAnchorB.Set(d.x2 - pb.x, d.y2 - pb.y);
            jd.length = b2Distance(b2Vec2(d.x1, d.y1), b2Vec2(d.x2, d.y2));
        } else if (d.coordType == 2) {
            jd.localAnchorA.Set(d.x1, d.y1);
            jd.localAnchorB.Set(d.x2, d.y2);
            jd.length = b2Distance(b1->GetWorldPoint(jd.localAnchorA), b2b->GetWorldPoint(jd.localAnchorB));
        } else {
            jd.localAnchorA.Set(0, 0);
            jd.localAnchorB.Set(0, 0);
            jd.length = 1.0f;
        }
        jd.frequencyHz = d.frequency;
        jd.dampingRatio = d.dampingRatio;
        jd.collideConnected = d.collideConnected;
        joint = world_.CreateJoint(&jd);
        break;
    }
    case 2: {  // weld: the anchor is the midpoint of the two transformed local anchors
        b2WeldJointDef jd;
        b2Vec2 a = b1->GetWorldPoint(b2Vec2(d.x1, d.y1));
        b2Vec2 b = b2b->GetWorldPoint(b2Vec2(d.x2, d.y2));
        b2Vec2 mid(a.x + (b.x - a.x) * 0.5f, a.y + (b.y - a.y) * 0.5f);
        jd.Initialize(b1, b2b, mid);
        jd.collideConnected = d.collideConnected;
        joint = world_.CreateJoint(&jd);
        break;
    }
    case 3: {  // revolute
        b2RevoluteJointDef jd;
        b2Vec2 anchor = b1->GetWorldPoint(b2Vec2(d.x1, d.y1));
        jd.Initialize(b1, b2b, anchor);
        jd.enableMotor = d.motor;
        jd.motorSpeed = d.motorSpeed;
        jd.maxMotorTorque = d.maxTorque;
        jd.enableLimit = d.limit;
        jd.lowerAngle = d.lowerLimit;
        jd.upperAngle = d.upperLimit;
        jd.collideConnected = d.collideConnected;
        joint = world_.CreateJoint(&jd);
        break;
    }
    case 4: {  // prismatic
        b2PrismaticJointDef jd;
        b2Vec2 anchor = b1->GetWorldPoint(b2Vec2(d.x1, d.y1));
        jd.Initialize(b1, b2b, anchor, b2Vec2(d.worldAxisX, d.worldAxisY));
        jd.enableLimit = d.limit;
        jd.lowerTranslation = d.lowerLimit;
        jd.upperTranslation = d.upperLimit;
        jd.enableMotor = d.motor;
        jd.motorSpeed = d.motorSpeed;
        jd.maxMotorForce = d.maxTorque;
        jd.collideConnected = d.collideConnected;
        joint = world_.CreateJoint(&jd);
        break;
    }
    default:
        return false;
    }

    std::unique_ptr<JointRecord> r(new JointRecord());
    r->name = d.name; r->end1 = d.end1; r->end2 = d.end2;
    r->joint = joint;
    r->coordType = d.coordType;
    r->view.reset(new B2JointView(joint, d.coordType));
    joints_.push_back(std::move(r));
    RebuildJointViews();
    return true;
}

void PhysicsWorld::Clear() {
    frame_.objects.clear();
    frame_.birds.clear();
    frame_.joints.clear();
    for (auto& o : objects_) world_.DestroyBody(o->b2);   // joints go with their bodies (SayGoodbye)
    objects_.clear();
    byName_.clear();
    idNames_.clear();
    joints_.clear();
    links_.clear();
    frame_.accumulator = 0;
}

void PhysicsWorld::SetJointParameters(const JointParams& p) {
    JointRecord* r = FindJoint(p.name);
    if (!r) return;
    if (r->joint->GetType() == e_revoluteJoint) {
        b2RevoluteJoint* j = static_cast<b2RevoluteJoint*>(r->joint);
        if (p.hasMotor) j->EnableMotor(p.motor);
        if (p.hasMotorSpeed) j->SetMotorSpeed(p.motorSpeed);
        if (p.hasMaxTorque) j->SetMaxMotorTorque(p.maxTorque);
        if (p.hasLimit) j->EnableLimit(p.limit);
        if (p.hasLowerLimit) j->SetLimits(p.lowerLimit, j->GetUpperLimit());
        if (p.hasUpperLimit) j->SetLimits(j->GetLowerLimit(), p.upperLimit);
    } else if (r->joint->GetType() == e_prismaticJoint) {
        b2PrismaticJoint* j = static_cast<b2PrismaticJoint*>(r->joint);
        if (p.hasMotor) j->EnableMotor(p.motor);
        if (p.hasMotorSpeed) j->SetMotorSpeed(p.motorSpeed);
        if (p.hasMaxTorque) j->SetMaxMotorForce(p.maxTorque);
        if (p.hasLimit) j->EnableLimit(p.limit);
        if (p.hasLowerLimit) j->SetLimits(p.lowerLimit, j->GetUpperLimit());
        if (p.hasUpperLimit) j->SetLimits(j->GetLowerLimit(), p.upperLimit);
    } else if (r->joint->GetType() == e_distanceJoint) {
        b2DistanceJoint* j = static_cast<b2DistanceJoint*>(r->joint);
        if (p.hasFrequency) j->SetFrequency(p.frequency);
        if (p.hasDampingRatio) j->SetDampingRatio(p.dampingRatio);
    }
}

void PhysicsWorld::CheckJointLimits(const std::string& name) {
    JointRecord* r = FindJoint(name);
    if (!r) return;
    if (r->joint->GetType() == e_revoluteJoint) {
        b2RevoluteJoint* j = static_cast<b2RevoluteJoint*>(r->joint);
        if (!(j->IsMotorEnabled() && j->IsLimitEnabled())) return;
        float speed = j->GetMotorSpeed();
        float angle = j->GetJointAngle();
        if ((speed > 0 && angle >= j->GetUpperLimit()) || (speed < 0 && angle <= j->GetLowerLimit()))
            j->SetMotorSpeed(-speed);
    } else if (r->joint->GetType() == e_prismaticJoint) {
        b2PrismaticJoint* j = static_cast<b2PrismaticJoint*>(r->joint);
        if (!(j->IsMotorEnabled() && j->IsLimitEnabled())) return;
        float speed = j->GetMotorSpeed();
        float t = j->GetJointTranslation();
        if ((speed > 0 && t >= j->GetUpperLimit()) || (speed < 0 && t <= j->GetLowerLimit()))
            j->SetMotorSpeed(-speed);
    }
}

void PhysicsWorld::DestroyJoint(const std::string& name) {
    for (size_t i = 0; i < links_.size(); i++) {
        if (links_[i].name == name) { links_.erase(links_.begin() + i); return; }
    }
    JointRecord* r = FindJoint(name);
    if (!r) return;
    b2Joint* j = r->joint;
    SayGoodbye(j);                 // Box2D only calls the listener for implicit destruction
    world_.DestroyJoint(j);
}

void PhysicsWorld::SayGoodbye(b2Joint* joint) {
    for (size_t i = 0; i < joints_.size(); i++) {
        if (joints_[i]->joint == joint) { joints_.erase(joints_.begin() + i); break; }
    }
    RebuildJointViews();
}

std::vector<std::string> PhysicsWorld::RemoveObject(const std::string& name) {
    std::vector<std::string> fired;
    PhysObject* o = Find(name);
    if (o) {
        for (size_t i = 0; i < frame_.birds.size(); i++)
            if (frame_.birds[i] == o) { frame_.birds.erase(frame_.birds.begin() + i); break; }
        for (size_t i = 0; i < frame_.objects.size(); i++)
            if (frame_.objects[i] == o) { frame_.objects.erase(frame_.objects.begin() + i); break; }
        world_.DestroyBody(o->b2);        // also destroys the joints on the body
        byName_.erase(name);
        idNames_.erase(o->id);
        for (size_t i = 0; i < objects_.size(); i++)
            if (objects_[i].get() == o) { objects_.erase(objects_.begin() + i); break; }
    }
    for (size_t i = 0; i < links_.size();) {
        if (links_[i].end1 == name) {
            PhysObject* victim = Find(links_[i].end2);
            if (victim) {
                victim->destroyPending = true;
                victim->destroyTimer = links_[i].destroyTimer;
                fired.push_back(links_[i].name);
                links_.erase(links_.begin() + i);
                continue;
            }
        }
        i++;
    }
    return fired;
}

// ---- per-object calls

void PhysicsWorld::ApplyImpulse(const std::string& n, float ix, float iy, float px, float py) {
    PhysObject* o = Find(n);
    if (o) o->b2->ApplyLinearImpulse(b2Vec2(ix, iy), b2Vec2(px, py));
}

void PhysicsWorld::ApplyForce(const std::string& n, float fx, float fy, float px, float py) {
    PhysObject* o = Find(n);
    if (o) o->b2->ApplyForce(b2Vec2(fx, fy), b2Vec2(px, py));
}

bool PhysicsWorld::SetPosition(const std::string& n, float x, float y) {
    PhysObject* o = Find(n);
    if (!o) return false;
    o->b2->SetTransform(b2Vec2(x, y), o->b2->GetAngle());
    o->state[0][0] = o->state[1][0] = o->interpX = x;
    o->state[0][1] = o->state[1][1] = o->interpY = y;
    return true;
}

bool PhysicsWorld::SetRotation(const std::string& n, float angle, float& normalized) {
    PhysObject* o = Find(n);
    if (!o) return false;
    float a = std::fmod(angle, kTwoPi);
    if (a < 0) a += kTwoPi;
    o->b2->SetTransform(o->b2->GetPosition(), a);
    o->state[0][2] = o->state[1][2] = o->interpAngle = a;
    normalized = a;
    return true;
}

void PhysicsWorld::SetVelocity(const std::string& n, float vx, float vy) {
    PhysObject* o = Find(n);
    if (o && o->b2->GetType() != b2_staticBody) o->b2->SetLinearVelocity(b2Vec2(vx, vy));
}

void PhysicsWorld::SetAngularVelocity(const std::string& n, float w) {
    PhysObject* o = Find(n);
    if (o && o->b2->GetType() != b2_staticBody) o->b2->SetAngularVelocity(w);
}

void PhysicsWorld::SetSleeping(const std::string& n, bool sleeping) {
    PhysObject* o = Find(n);
    if (o) o->b2->SetAwake(!sleeping);   // false: also zeroes velocities, force and torque
}

float PhysicsWorld::GetAngle(const std::string& n) const {
    PhysObject* o = Find(n);
    return o ? o->interpAngle : 0.0f;
}

void PhysicsWorld::GetWorldPoint(const std::string& n, float lx, float ly, float& wx, float& wy) const {
    PhysObject* o = Find(n);
    if (!o) { wx = lx; wy = ly; return; }
    b2Vec2 p = o->b2->GetWorldPoint(b2Vec2(lx, ly));
    wx = p.x; wy = p.y;
}

void PhysicsWorld::SetRolling(const std::string& n, int material, bool rolls, float rollFactor) {
    PhysObject* o = Find(n);
    if (!o) return;
    o->rollMaterial = material;
    o->rolls = rolls;
    o->rollFactor = rollFactor;
}

void PhysicsWorld::SetObjectParameter(const std::string& n, int id, float value) {
    PhysObject* o = Find(n);
    if (!o) return;
    if (id == 1) {
        o->drawWithBirds = (int)value == 1;
    } else if (id == 2) {
        o->b2->SetType(value == 0.0f ? b2_staticBody : b2_dynamicBody);
    }
}

void PhysicsWorld::SetMaterial(const std::string& n, const std::string& material) {
    PhysObject* o = Find(n);
    if (!o) return;
    if (material.compare(0, 4, "wood") == 0) o->rollMaterial = kRollWood;
    if (material.compare(0, 4, "rock") == 0) o->rollMaterial = kRollRock;
    if (material.compare(0, 5, "light") == 0) o->rollMaterial = kRollLight;
}

void PhysicsWorld::SetSpriteName(const std::string& n, const std::string& sprite) {
    if (PhysObject* o = Find(n)) o->sprite = sprite;
}

void PhysicsWorld::SetTextureName(const std::string& n, const std::string& texture) {
    if (PhysObject* o = Find(n)) o->texture = texture;
}

// ---- contacts

ContactObject PhysicsWorld::MakeContactObject(const PhysObject& o) {
    ContactObject c;
    c.name = o.name;
    c.controllable = o.isBird;
    c.mass = o.b2->GetMass();
    b2Vec2 v = o.b2->GetLinearVelocity();
    c.velocity.x = v.x;
    c.velocity.y = v.y;
    return c;
}

void PhysicsWorld::ApplyVelocityChange(const VelocityChange& v) {
    PhysObject* o = Find(v.name);
    if (!o) return;
    if (v.immediate) {
        if (o->b2->GetType() != b2_staticBody) o->b2->SetLinearVelocity(b2Vec2(v.velocity.x, v.velocity.y));
    } else {
        o->pendingVelocity = true;
        o->pendingVx = v.velocity.x;
        o->pendingVy = v.velocity.y;
    }
}

void PhysicsWorld::BeginContact(b2Contact* c) {
    PhysObject* a = static_cast<PhysObject*>(c->GetFixtureA()->GetUserData());
    PhysObject* b = static_cast<PhysObject*>(c->GetFixtureB()->GetUserData());
    if (!a || !b) return;
    if (onBeginContact) onBeginContact(*a, *b, c);
}

// 0x40c440. Collide only if both objects have `collision` set, except that a circle created with
// z_order 999 never collides with an object whose material is "immovable".
bool PhysicsWorld::ShouldCollide(b2Fixture* fa, b2Fixture* fb) {
    PhysObject* a = static_cast<PhysObject*>(fa->GetUserData());
    PhysObject* b = static_cast<PhysObject*>(fb->GetUserData());
    if (!a || !b) return true;
    bool collide = a->collision && b->collision;
    // the original compares the first 9 bytes only
    if (a->special999 && materialOf && materialOf(*b).compare(0, 9, "immovable") == 0) collide = false;
    if (b->special999 && materialOf && materialOf(*a).compare(0, 9, "immovable") == 0) collide = false;
    return collide;
}

}  // namespace ab
