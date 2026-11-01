// The part of the physics world that does not depend on the Box2D version. See physics_world.h.
#include "physics/physics_world.h"

#include <algorithm>
#include <cmath>

namespace ab {

namespace {
const float kTwoPi = 6.2831855f;
}

std::unique_ptr<PhysicsWorld> MakePhysicsWorld(Box2DVersion version) {
    return version == Box2DVersion::V22 ? MakePhysicsWorld22() : MakePhysicsWorld21();
}

PhysObject* PhysicsWorld::Find(const std::string& name) const {
    auto it = byName_.find(name);
    return it == byName_.end() ? nullptr : it->second;
}

PhysObject* PhysicsWorld::NewObject(const BodyDef& d, float rollFactor, bool circle) {
    // An object that already has this name is replaced in the hash table by the original; the old
    // body stays in the world. Scripts never reuse a name, so that case is not reproduced here.
    PhysObject* o = new PhysObject();
    o->name = d.name;
    o->id = nextId_++;
    idNames_[o->id] = d.name;
    o->isBird = d.controllable;
    o->recordVelocity = !spaceRules;   // Space: only after setRecordVelocity
    o->zOrder = d.zOrder;
    o->sprite = d.sprite;          // createBox/Circle/Polygon: the sprite is looked up when the body is created
    o->rollFactor = rollFactor;
    o->collision = (circle && !spaceRules) ? true : d.collision;
    o->state[0][0] = o->state[1][0] = o->interpX = d.x;
    o->state[0][1] = o->state[1][1] = o->interpY = d.y;
    return o;
}

void PhysicsWorld::AddObject(PhysObject* o, bool bird) {
    o->body = o->pbody.get();
    objects_.emplace_back(o);
    byName_[o->name] = o;
    frame_.objects.push_back(o);
    if (bird) frame_.birds.push_back(o);
    if (bird && spaceRules && o->pbody) o->pbody->SetActive(false);   // 0x4899d2: a controllable object is created inactive (b2Body::SetActive 0x496ad0); the launch script activates it
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

void PhysicsWorld::DropJointRecord(void* joint) {
    for (size_t i = 0; i < joints_.size(); i++) {
        if (joints_[i]->joint == joint) { joints_.erase(joints_.begin() + i); break; }
    }
    RebuildJointViews();
}

void PhysicsWorld::DestroyJoint(const std::string& name) {
    for (size_t i = 0; i < links_.size(); i++) {
        if (links_[i].name == name) { links_.erase(links_.begin() + i); return; }
    }
    JointRecord* r = FindJoint(name);
    if (!r) return;
    void* j = r->joint;
    DropJointRecord(j);            // Box2D only calls the listener for implicit destruction
    DestroyBox2DJoint(j);
}

void PhysicsWorld::Clear() {
    frame_.objects.clear();
    frame_.birds.clear();
    frame_.joints.clear();
    for (auto& o : objects_) DestroyBody(o.get());   // joints go with their bodies (SayGoodbye)
    objects_.clear();
    byName_.clear();
    idNames_.clear();
    joints_.clear();
    links_.clear();
    frame_.accumulator = 0;
}

std::vector<std::string> PhysicsWorld::RemoveObject(const std::string& name) {
    std::vector<std::string> fired;
    PhysObject* o = Find(name);
    if (o) {
        for (size_t i = 0; i < frame_.birds.size(); i++)
            if (frame_.birds[i] == o) { frame_.birds.erase(frame_.birds.begin() + i); break; }
        for (size_t i = 0; i < frame_.objects.size(); i++)
            if (frame_.objects[i] == o) { frame_.objects.erase(frame_.objects.begin() + i); break; }
        for (size_t i = 0; i < sensorPairs_.size();) if (sensorPairs_[i].first == o || sensorPairs_[i].second == o) sensorPairs_.erase(sensorPairs_.begin() + i); else i++;
        DestroyBody(o);        // also destroys the joints on the body
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
    if (o) o->pbody->ApplyImpulse(ix, iy, px, py);
}

void PhysicsWorld::ApplyForce(const std::string& n, float fx, float fy, float px, float py) {
    PhysObject* o = Find(n);
    if (o) o->pbody->ApplyForce(fx, fy, px, py);
}

bool PhysicsWorld::SetPosition(const std::string& n, float x, float y) {
    PhysObject* o = Find(n);
    if (!o) return false;
    o->pbody->SetTransform(x, y, o->pbody->Angle());
    o->state[0][0] = o->state[1][0] = o->interpX = x;
    o->state[0][1] = o->state[1][1] = o->interpY = y;
    return true;
}

bool PhysicsWorld::SetRotation(const std::string& n, float angle, float& normalized) {
    PhysObject* o = Find(n);
    if (!o) return false;
    float a = std::fmod(angle, kTwoPi);
    if (a < 0) a += kTwoPi;
    o->pbody->SetTransform(o->pbody->PosX(), o->pbody->PosY(), a);
    o->state[0][2] = o->state[1][2] = o->interpAngle = a;
    normalized = a;
    return true;
}

void PhysicsWorld::SetVelocity(const std::string& n, float vx, float vy) {
    PhysObject* o = Find(n);
    if (o && !o->pbody->IsStatic()) o->pbody->SetLinearVelocity(vx, vy);
}

void PhysicsWorld::SetAngularVelocity(const std::string& n, float w) {
    PhysObject* o = Find(n);
    if (o && !o->pbody->IsStatic()) o->pbody->SetAngularVelocity(w);
}

bool PhysicsWorld::BodyCommand(const std::string& n, int cmd, float a, float& result) {
    if (cmd == 8) { gravityForceMultiplier = a; return true; }
    if (cmd == 9) { result = gravityForceMultiplier; return true; }
    PhysObject* o = Find(n);
    if (!o) return false;
    PhysBody& b = *o->pbody;
    if (cmd == 10) { o->gravMask = (int)a; return true; }
    if (cmd == 11) { o->gravCat = (int)a; return true; }
    if (cmd == 12) { o->gravMin = a; return true; }
    if (cmd == 13) { o->gravMax = a; return true; }
    if (cmd == 14) { o->revertGravity = a != 0; return true; }        // setRevertGravity 0x47a660 (+0x18a)
    if (cmd == 15) { o->revertMulti = a != 0; return true; }          // setRevertGravityWithMultiplier 0x47a6d0 (+0x18b; the multiplier is stored negated at +0x18c, the speed limit at +0x190)
    if (cmd == 16) { o->revertMult = -a; return true; }
    if (cmd == 17) { o->revertMaxSpeed = a; return true; }
    switch (cmd) {
    case 1: b.SetLinearDamping(a); break;
    case 2: b.SetAngularDamping(a); break;
    case 3: b.SetFixedRotation(a != 0); break;
    case 4: b.SetActive(a != 0); break;
    case 5: if (!b.IsStatic()) b.SetLinearVelocity(b.VelX() * a, b.VelY() * a); break;
    case 6: result = b.AngularVelocity(); break;
    case 7: b.SetSensor(a != 0); o->isSensor = a != 0; break;
    default: return false;
    }
    return true;
}

void PhysicsWorld::SetSleeping(const std::string& n, bool sleeping) {
    PhysObject* o = Find(n);
    if (o) o->pbody->SetAwake(!sleeping);   // false: also zeroes velocities, force and torque
}

float PhysicsWorld::GetAngle(const std::string& n) const {
    PhysObject* o = Find(n);
    return o ? o->interpAngle : 0.0f;
}

void PhysicsWorld::GetWorldPoint(const std::string& n, float lx, float ly, float& wx, float& wy) const {
    PhysObject* o = Find(n);
    if (!o) { wx = lx; wy = ly; return; }
    o->pbody->WorldPoint(lx, ly, wx, wy);
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
    if (spaceRules) {
        // 0x47ac60 (jump table at 0x47afe0). Scale is always drawn in Space (ids 5, 17, 18); 6/7 are floats the draw code does not read here,
        // 16 is ignoreClassicGravity (the world has no gravity) and 33 is isVisible
        switch (id) {
        case 1: o->drawWithBirds = (int)value == 1; break;
        case 2: o->pbody->SetStatic(value == 0.0f); break;
        case 5: o->scaleX = o->scaleY = value; o->wobDX = o->wobDY = 0; break;
        case 17: o->scaleX = value; break;
        case 18: o->scaleY = value; break;
        case 8: o->flipX = o->flipBase = (int)value == 1; break;
        case 9: o->enableFlipping = (int)value == 1; break;
        case 10: o->disableRotation = (int)value == 1; break;
        case 15: o->disableImmovable = (int)value == 1; break;
        case 19: o->spaceFlagA = (int)value == 1; break;
        case 20: o->spaceActive19e = (int)value == 1; break;
        case 21: o->sensorKind = (int)value; break;
        case 22: o->gravOn = (int)value == 1; break;
        case 25: o->gravMin = value; break;
        case 26: o->gravMax = value; break;
        case 27: o->gravRadius = value; break;
        case 35: o->gravExt = value; break; // +0x178 height = field extent
        case 24: case 28: case 29: break; // isBox +0x164, width +0x174, forceAngle +0x17c: stored only, not read by gravity
        case 31: o->birdGravReduce = value; break;
        case 34: o->stopOnExit = (int)value == 1; break;   // +0x1a5
        case 33: o->hidden = (int)value != 1; break;
        default: break;
        }
        return;
    }
    if (id == 1) {
        o->drawWithBirds = (int)value == 1;
    } else if (id == 2) {
        o->pbody->SetStatic(value == 0.0f);
    } else if (id == 5) {
        o->scaleX = o->scaleY = value;
        o->wobDX = o->wobDY = 0;        // the exe sets the current scale together with the base scale
    } else if (id == 6) {
        o->scaled = (int)value == 1;
    } else if (id == 106) {
        o->bounceAmp = value;
    } else if (id == 107) {
        o->bounceFreq = value;
    } else if (id == 8) {
        o->flipX = o->flipBase = (int)value == 1;
    } else if (id == 9) {
        o->enableFlipping = (int)value == 1;
    } else if (id == 10) {
        o->disableRotation = (int)value == 1;      // 0x41b2c0 draws the sprite upright; the physics is untouched
    } else if (id == 14) {
        o->canGetUp = (int)value == 1;
    } else if (id == 15) {
        o->disableImmovable = (int)value == 1;
    } else if (id == 16) {
        o->disableControllable = (int)value == 1;
    } else if (id == 21) {
        o->sensorKind = (int)value;
    } else if (id == 22) {
        o->gravOn = (int)value == 1;
    } else if (id == 25) {
        o->gravMin = value;
    } else if (id == 26) {
        o->gravMax = value;
    } else if (id == 27) {
        o->gravRadius = value;
    }
}

// 0x46f720: a gravitation sensor (kind 2) pulls every dynamic object it touches towards its centre with a force that falls off
// linearly from gravMax at the centre to gravMin at the radius (F = max - |d| / radius * (max - min)), scaled by the object's mass
// and 0.1 and by the game's gravity force multiplier. A sensor without a radius is a field: the force points along its angle.
float PhysicsWorld::BirdGravityFactor(float timer) const {
    if (timer < 0) return gravityForceMultiplier;                     // 0x46f900: before any collision the timer is -1 and the full multiplier applies
    float m = gravityForceMultiplier - timer * 1.3f;
    return m > 1 ? m : 1;
}

// The force one gravitation sensor puts on one object (the body of 0x46f720, shared by the real physics and by the aim trail so that the two can never
// disagree). `s` is the sensor, `t` the pulled object, (tx, ty) / (tvx, tvy) its position and velocity, `timer` its bird gravity timer (-1: no
// collision yet) and `mass` its mass. Returns false when the sensor does not pull it (it does not take part in the gravity category, the speed limit
// of setRevertGravityWithMultiplier is reached, the centre of a radial field is hit exactly); otherwise (fx, fy) is the force to apply at the
// object's position. Radial sensors (a radius was set) pull towards their centre with max - distance / radius * (max - min); sensors without a radius are
// directional fields pulling along the axis (-sin, cos) of their angle, measured from the point half an extent along that axis.
bool PhysicsWorld::GravityPull(const PhysObject& s, const PhysObject& t, float tx, float ty, float tvx, float tvy, float timer, float mass, float& fx, float& fy) const {
    if (s.gravMask > -1 && t.gravCat > -1 && (s.gravMask & t.gravCat) == 0) return false;
    float F, ux, uy;
    if (s.gravRadius > -1) {
        float dx = s.pbody->PosX() - tx, dy = s.pbody->PosY() - ty;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len <= 1e-6f) return false;
        F = s.gravMax - len / s.gravRadius * (s.gravMax - s.gravMin);
        ux = dx / len; uy = dy / len;
    } else {
        ux = -s.pbody->RotCol1Y(); uy = s.pbody->RotCol1X();
        float h = s.gravExt * 0.5f;
        float px = s.pbody->PosX() + ux * h - tx, py = s.pbody->PosY() + uy * h - ty;
        float d = std::sqrt(px * px + py * py);
        F = s.gravExt > 0 ? s.gravMax - d / s.gravExt * (s.gravMax - s.gravMin) : s.gravMax;
    }
    float mag = F * mass * 0.1f;
    if (t.isBird) mag *= BirdGravityFactor(timer);
    else if (t.revertGravity) mag *= -0.1f;
    else if (t.revertMulti) {
        if (std::sqrt(tvx * tvx + tvy * tvy) >= t.revertMaxSpeed) return false;
        mag *= t.revertMult;
    }
    fx = ux * mag; fy = uy * mag;
    return true;
}

int PhysicsWorld::SensorPair(PhysObject* sen, PhysObject* other, bool enter) {
    if (enter) sensorPairs_.push_back({sen, other});
    else {
        bool found = false;
        for (size_t i = 0; i < sensorPairs_.size(); i++)
            if (sensorPairs_[i].first == sen && sensorPairs_[i].second == other) { sensorPairs_.erase(sensorPairs_.begin() + i); found = true; break; }
        if (!found) return -1;
    }
    int left = 0;
    for (const auto& pr : sensorPairs_) if (pr.second == other) left++;
    return left;
}

// 0x474250 / 0x46f720: every awake object applies the force of each gravitation sensor it currently touches (the contact list, not a distance scan)
void PhysicsWorld::ApplyGravitySensors(float dt) {
    ApplyOwnGravity();
    // 0x483500 goes through the objects and, for each one whose body is awake, 0x474250 walks that object's own list of touching sensors from the last
    // entry to the first (the order matters for the rounding of the summed force). A sleeping object is not pulled at all, whatever its sensors do, and
    // an object of kind 5 or 7 is skipped. 0x46f720 only tests the sensor kind (+0x15c == 2): the flag set by parameter 22 (+0x19f) is stored but never read.
    for (size_t i = sensorPairs_.size(); i-- > 0;) {
        PhysObject& s = *sensorPairs_[i].first; PhysObject& t = *sensorPairs_[i].second;
        if (!s.pbody || !t.pbody || s.sensorKind != 2 || t.sensorKind == 5 || t.sensorKind == 7 || !t.pbody->IsAwake()) continue;
        if (t.pbody->IsStatic()) continue;
        if (s.gravMask > -1 && t.gravCat > -1 && (s.gravMask & t.gravCat) == 0) continue;
        float fx, fy;
        if (GravityPull(s, t, t.pbody->PosX(), t.pbody->PosY(), t.pbody->VelX(), t.pbody->VelY(), t.birdGravReduce, t.pbody->Mass(), fx, fy))
            t.pbody->ApplyForce(fx, fy, t.pbody->PosX(), t.pbody->PosY());
    }
    // 0x483537: the exe goes through its objects in name order and advances an object's timer right after that object's own turn, so the sensors
    // (names like BLOCK_SENSOR_...) have pulled the bird with the old value before it runs by one fixed step
    for (const auto& bo : objects_)
        if (bo->isBird && bo->birdGravReduce >= 0) bo->birdGravReduce += dt;
}

// ---- gravity of one object. Box2D 2.2 applies the world's gravity to every body (b2Body::SetGravityScale only exists from 2.3), so an object
// with a gravity of its own gets the difference as a force: m * (own - world). Together with the world's gravity the body then accelerates by
// exactly its own value. Only awake dynamic bodies are pushed (a body that sleeps is woken when the gravity is set); the force goes through the
// centre of mass, so the body does not start to spin.
bool PhysicsWorld::SetOwnGravityVector(const std::string& n, float gx, float gy) {
    PhysObject* o = Find(n);
    if (!o) return false;
    o->ownGravityMode = 1; o->ownGravityX = gx; o->ownGravityY = gy;
    if (o->pbody && !o->pbody->IsStatic()) o->pbody->SetAwake(true);
    return true;
}

bool PhysicsWorld::SetOwnGravityScale(const std::string& n, float scale) {
    PhysObject* o = Find(n);
    if (!o) return false;
    o->ownGravityMode = 2; o->ownGravityScale = scale;
    if (o->pbody && !o->pbody->IsStatic()) o->pbody->SetAwake(true);
    return true;
}

bool PhysicsWorld::ResetOwnGravity(const std::string& n) {
    PhysObject* o = Find(n);
    if (!o) return false;
    o->ownGravityMode = 0;
    return true;
}

bool PhysicsWorld::GetOwnGravity(const std::string& n, int& mode, float& gx, float& gy, float& scale) const {
    for (const auto& o : objects_)
        if (o->name == n) {
            float wx, wy; GetGravity(wx, wy);
            mode = o->ownGravityMode; scale = o->ownGravityScale;
            if (mode == 1) { gx = o->ownGravityX; gy = o->ownGravityY; }
            else if (mode == 2) { gx = wx * scale; gy = wy * scale; }
            else { gx = wx; gy = wy; scale = 1; }
            return true;
        }
    return false;
}

void PhysicsWorld::ApplyOwnGravity() {
    float wx, wy;
    GetGravity(wx, wy);
    for (const auto& o : objects_) {
        if (o->ownGravityMode == 0 || !o->pbody || o->pbody->IsStatic() || !o->pbody->IsAwake()) continue;
        float gx = o->ownGravityMode == 1 ? o->ownGravityX : wx * o->ownGravityScale;
        float gy = o->ownGravityMode == 1 ? o->ownGravityY : wy * o->ownGravityScale;
        float m = o->pbody->Mass();
        o->pbody->ApplyForce(m * (gx - wx), m * (gy - wy), o->pbody->PosX(), o->pbody->PosY());
    }
}

// predictCollision: what a hit between two objects would do. The contact comes from the real shapes (the closest points of every fixture pair, so a box
// corner or a polygon edge is found where it really is); the hit is the one Box2D's contact solver computes for a single contact point: a normal impulse
// that gives the bounce, a friction impulse along the tangent clamped by the Coulomb cone, both applied at the contact point so that the bodies' rotation
// (their inertia and spin) takes part. `restitution` < 0 means "as the fixtures say" (the larger of the two, like Box2D), and Box2D's velocity threshold
// of 1 applies (a slow touch does not bounce).
CollisionPrediction PhysicsWorld::PredictCollision(const std::string& an, const std::string& bn, float restitution, const float* vel) const {
    CollisionPrediction r;
    const PhysObject* a = nullptr; const PhysObject* b = nullptr;
    for (const auto& o : objects_) { if (o->name == an) a = o.get(); if (o->name == bn) b = o.get(); }
    if (!a || !b || !a->pbody || !b->pbody || a == b) return r;                 // the safe exit: valid stays false
    r.valid = true;
    r.filtered = !ShouldCollideObjects(a, b) || a->isSensor || b->isSensor;
    const PhysBody& pa = *a->pbody; const PhysBody& pb = *b->pbody;
    const bool sa = pa.IsStatic(), sb = pb.IsStatic();
    float cax, cay, cbx, cby;                                                   // centres of mass
    pa.CenterOfMass(cax, cay); pb.CenterOfMass(cbx, cby);
    float dx = cbx - cax, dy = cby - cay;
    r.distance = std::sqrt(dx * dx + dy * dy);
    // The contact: closest points of the real shapes when the body can tell, otherwise the line between the centres
    float px, py, ax, ay, bx, by, core = 0, ra = 0, rb = 0;
    if (pa.ClosestPoints(pb, ax, ay, bx, by, core, ra, rb)) {
        r.exactShapes = true;
        if (core > 1e-6f) {
            r.nx = (bx - ax) / core; r.ny = (by - ay) / core;
            r.gap = core - ra - rb;
            px = 0.5f * ((ax + r.nx * ra) + (bx - r.nx * rb)); py = 0.5f * ((ay + r.ny * ra) + (by - r.ny * rb));
        } else {                                                                // the cores overlap: push along the line between the centres
            if (r.distance > 1e-6f) { r.nx = dx / r.distance; r.ny = dy / r.distance; } else { r.nx = 0; r.ny = 1; }
            r.gap = -(ra + rb);
            px = 0.5f * (ax + bx); py = 0.5f * (ay + by);
        }
    } else {
        if (r.distance > 1e-6f) { r.nx = dx / r.distance; r.ny = dy / r.distance; } else { r.nx = 0; r.ny = 1; }
        auto reach = [&](const PhysObject* o) {                                 // fallback: circles by radius, other shapes by their box
            BodyInfo bi = Inspect(o->name);
            if (bi.shapeRadius > 0) return bi.shapeRadius;
            float x0, y0, x1, y1;
            if (!o->pbody->Bounds(x0, y0, x1, y1)) return 0.0f;
            return 0.5f * (x1 - x0) * std::fabs(r.nx) + 0.5f * (y1 - y0) * std::fabs(r.ny);
        };
        r.gap = r.distance - reach(a) - reach(b);
        px = 0.5f * (cax + cbx); py = 0.5f * (cay + cby);
    }
    r.contactX = px; r.contactY = py;
    // Lever arms from each centre of mass to the contact point (zero without real shapes, so there is no spin then)
    const float rax = r.exactShapes ? px - cax : 0, ray = r.exactShapes ? py - cay : 0, rbx = r.exactShapes ? px - cbx : 0, rby = r.exactShapes ? py - cby : 0;
    // A velocity given by the caller replaces the body's own (a static body stays at rest); a NaN entry keeps the body's velocity
    auto pick = [&](int i, float own) { return vel && vel[i] == vel[i] ? vel[i] : own; };
    const float avx = sa ? 0 : pick(0, pa.VelX()), avy = sa ? 0 : pick(1, pa.VelY()), bvx = sb ? 0 : pick(2, pb.VelX()), bvy = sb ? 0 : pick(3, pb.VelY());
    const float aw = sa ? 0 : pa.AngularVelocity(), bw = sb ? 0 : pb.AngularVelocity();
    r.massA = sa ? 0 : pa.Mass(); r.massB = sb ? 0 : pb.Mass();
    const float ia = r.massA > 0 ? 1.0f / r.massA : 0, ib = r.massB > 0 ? 1.0f / r.massB : 0;
    const float iA = sa || pa.Inertia() <= 0 ? 0 : 1.0f / pa.Inertia(), iB = sb || pb.Inertia() <= 0 ? 0 : 1.0f / pb.Inertia();
    // Velocity of each body at the contact point (linear velocity plus spin), and of B seen from A
    const float apx = avx - aw * ray, apy = avy + aw * rax, bpx = bvx - bw * rby, bpy = bvy + bw * rbx;
    r.relVx = bpx - apx; r.relVy = bpy - apy;
    const float tx = r.ny, ty = -r.nx;                                          // the tangent: the normal turned by 90 degrees
    const float vn = r.relVx * r.nx + r.relVy * r.ny, vt = r.relVx * tx + r.relVy * ty;
    r.closingSpeed = -vn; r.normalSpeed = vn; r.tangentSpeed = vt;
    r.friction = std::sqrt(std::max(0.0f, pa.Friction()) * std::max(0.0f, pb.Friction()));
    const float eFix = std::max(pa.Restitution(), pb.Restitution());
    r.restitution = restitution >= 0 ? restitution : eFix;
    r.aVx = avx; r.aVy = avy; r.bVx = bvx; r.bVy = bvy; r.aAngVel = aw; r.bAngVel = bw;
    if (r.closingSpeed > 1e-6f) r.timeToContact = r.gap > 0 ? r.gap / r.closingSpeed : 0;
    r.collides = !r.filtered && r.closingSpeed > 1e-6f && (ia + ib) > 0;
    if (r.collides) {
        auto cross = [](float x0, float y0, float x1, float y1) { return x0 * y1 - y0 * x1; };
        const float rnA = cross(rax, ray, r.nx, r.ny), rnB = cross(rbx, rby, r.nx, r.ny);
        const float kn = ia + ib + iA * rnA * rnA + iB * rnB * rnB;
        const float e = vn < -1.0f ? r.restitution : 0.0f;                      // Box2D's velocity threshold: slow contacts do not bounce
        r.impulse = kn > 0 ? -(1.0f + e) * vn / kn : 0.0f;
        const float rtA = cross(rax, ray, tx, ty), rtB = cross(rbx, rby, tx, ty);
        const float kt = ia + ib + iA * rtA * rtA + iB * rtB * rtB;
        float jt = kt > 0 ? -vt / kt : 0.0f;
        const float limit = r.friction * r.impulse;
        jt = std::max(-limit, std::min(limit, jt));                            // Coulomb friction: no more than friction * normal impulse
        r.tangentImpulse = jt;
        const float Px = r.impulse * r.nx + jt * tx, Py = r.impulse * r.ny + jt * ty;
        r.aVx = avx - ia * Px; r.aVy = avy - ia * Py; r.aAngVel = aw - iA * cross(rax, ray, Px, Py);
        r.bVx = bvx + ib * Px; r.bVy = bvy + ib * Py; r.bAngVel = bw + iB * cross(rbx, rby, Px, Py);
        auto energy = [&](float m, float i, float vx, float vy, float w) { return 0.5f * m * (vx * vx + vy * vy) + (i > 0 ? 0.5f * w * w / i : 0.0f); };
        const float before = energy(r.massA, iA, avx, avy, aw) + energy(r.massB, iB, bvx, bvy, bw);
        const float after = energy(r.massA, iA, r.aVx, r.aVy, r.aAngVel) + energy(r.massB, iB, r.bVx, r.bVy, r.bAngVel);
        r.energyLost = before - after;
    }
    return r;
}

// The aim trail (updateBirdTrajectoryTable, 0x479ab0). Retail does not run a formula: it takes the dummy bird, and for `steps` fixed steps lets every
// gravitation sensor whose shape overlaps the bird push it through the very function the real physics uses (0x46f720), integrates the bird on its
// own (`steps` of `h` seconds: simulationIterations steps of objects.currentTimeStep * simulationTimeStepMultiplier, 0x479b1a) with b2World's free-flight integrator (0x497d50: velocity += h * (gravity + invMass * force), damping clamp(1 - h * d, 0, 1), the
// maximum translation of 2 per step, position += h * velocity; no collisions), clears the forces and stores the position every `every` steps (simulationStorePointsSampler). The bird's gravity timer does not run inside the trail (0x479ab0 never advances it). This
// reproduces that loop on copies of the bird's position and velocity, so the real bird is never touched. A radial sensor overlaps the bird while
// the centre distance is within the sensor's radius plus the bird's radius, a field while the bird's box touches the sensor's box.
void PhysicsWorld::SimulateTrajectory(const std::string& n, int steps, int every, float h, std::vector<std::pair<float, float>>& out) {
    PhysObject* o = Find(n);
    if (!o || !o->pbody) return;
    float x = o->pbody->PosX(), y = o->pbody->PosY(), vx = o->pbody->VelX(), vy = o->pbody->VelY();
    float damp = o->pbody->LinearDamping(), mass = o->pbody->Mass(), invMass = mass > 0 ? 1.0f / mass : 0.0f, r = o->pbody->ShapeRadius();
    float wgx = 0, wgy = 0; GetGravity(wgx, wgy);
    float timer = o->birdGravReduce;
    for (int i = 0; i < steps; i++) {
        float fx = 0, fy = 0;
        for (const auto& so : objects_) {                                // 0x479bf0: the sensors in their list order
            const PhysObject& s = *so;
            if (s.sensorKind != 2 || !s.pbody || &s == o) continue;
            int hit = o->pbody->OverlapAt(*s.pbody, x, y);               // 0x479c05: b2TestOverlap of the two first fixtures' shapes
            if (hit == 0) continue;
            if (hit < 0) {                                               // a build that cannot ask (Box2D 2.1): the radius / box approximation
                float x0, y0, x1, y1;
                if (s.gravRadius > -1) {
                    float dx = s.pbody->PosX() - x, dy = s.pbody->PosY() - y, reach = s.gravRadius + r;
                    if (dx * dx + dy * dy > reach * reach) continue;
                } else if (!s.pbody->Bounds(x0, y0, x1, y1) || x + r < x0 || x - r > x1 || y + r < y0 || y - r > y1) continue;
            }
            float px, py;
            if (GravityPull(s, *o, x, y, vx, vy, timer, mass, px, py)) { fx += px; fy += py; }
        }
        vx += h * (wgx + invMass * fx); vy += h * (wgy + invMass * fy);  // 0x497d87: the world's gravity plus the force times the inverse mass
        float k = 1.0f - h * damp; k = k < 0 ? 0 : (k > 1 ? 1 : k);      // 0x497dea: the 2.3 damping clamp
        vx *= k; vy *= k;
        float tx = h * vx, ty = h * vy;
        if (tx * tx + ty * ty > 4.0f) { float q = 2.0f / std::sqrt(tx * tx + ty * ty); vx *= q; vy *= q; }   // 0x497e9c: b2_maxTranslation
        x += h * vx; y += h * vy;
        if (every > 0 && (i + 1) % every == 0) out.push_back({x, y});
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
    c.mass = o.pbody->Mass();
    c.velocity.x = o.pbody->VelX();
    c.velocity.y = o.pbody->VelY();
    return c;
}

void PhysicsWorld::ApplyVelocityChange(const VelocityChange& v) {
    PhysObject* o = Find(v.name);
    if (!o) return;
    if (v.immediate) {
        if (!o->pbody->IsStatic()) o->pbody->SetLinearVelocity(v.velocity.x, v.velocity.y);
    } else {
        o->pendingVelocity = true;
        o->pendingVx = v.velocity.x;
        o->pendingVy = v.velocity.y;
    }
}

// 0x40c440. Collide only if both objects have `collision` set, except that a circle created with
// z_order 999 never collides with an object whose material is "immovable".
bool PhysicsWorld::ShouldCollideObjects(const PhysObject* a, const PhysObject* b) const {
    if (!a || !b) return true;
    if (spaceRules) {
        // Angry Birds Space, 0x4624e0 (T is the object's +0x15c type: 5, 6 and 7 are special; ctl is the controllable byte +0x19c)
        auto t57 = [](const PhysObject* o) { return o->sensorKind == 5 || o->sensorKind == 7; };
        bool anyA = a->spaceFlagA || b->spaceFlagA;
        if (anyA && (t57(a) || t57(b))) return true;
        if (!a->collision || !b->collision) return false;
        if (a->sensorKind == 7 && b->isBird) return false;
        if (b->sensorKind == 7 && a->isBird) return false;
        if (a->spaceFlagA) return false;
        if (b->spaceFlagA && (a->spaceActive19e || b->spaceActive19e)) return false;
        if (t57(a) && t57(b)) return false;
        int ta = a->sensorKind, tb = b->sensorKind;
        if (ta == 6 || tb == 6) { if (a->isBird || b->isBird) return false; }
        bool aImm = a->disableImmovable, bImm = b->disableImmovable;
        if (aImm && materialOf && materialOf(*b).compare(0, 9, "immovable") == 0) return false;
        if (bImm && materialOf && materialOf(*a).compare(0, 9, "immovable") == 0) return false;
        if (!a->isBird && !b->isBird && ta != 6 && tb != 6) {
            if (ta == 5 && tb != ta) return false;
            if (tb == 5 && ta != tb) return false;
        }
        return true;
    }
    bool collide = a->collision && b->collision;
    // the original compares the first 9 bytes only
    if (a->special999 && materialOf && materialOf(*b).compare(0, 9, "immovable") == 0) collide = false;
    if (b->special999 && materialOf && materialOf(*a).compare(0, 9, "immovable") == 0) collide = false;
    if (a->disableImmovable && materialOf && materialOf(*b).compare(0, 9, "immovable") == 0) collide = false;
    if (b->disableImmovable && materialOf && materialOf(*a).compare(0, 9, "immovable") == 0) collide = false;
    if (a->disableControllable && b->isBird) collide = false;
    if (b->disableControllable && a->isBird) collide = false;
    if (!colliderRules_.pair[a->colliderType & (kColliderTypes - 1)][b->colliderType & (kColliderTypes - 1)]) collide = false;
    return collide;
}

// ---- Angry Birds Rio queries

void PhysicsWorld::SetRecordVelocity(const std::string& n, bool on) {
    if (PhysObject* o = Find(n)) { o->recordVelocity = on; o->recordAngle = on; }
}

bool PhysicsWorld::GetVelocity(const std::string& n, float& vx, float& vy) const {
    PhysObject* o = Find(n);
    if (!o || !o->pbody) { vx = vy = 0; return false; }
    vx = o->pbody->VelX(); vy = o->pbody->VelY();
    return true;
}

float PhysicsWorld::GetSpeed(const std::string& n) const {
    PhysObject* o = Find(n);
    return o ? std::sqrt(o->pbody->VelX() * o->pbody->VelX() + o->pbody->VelY() * o->pbody->VelY()) : 0.0f;
}

bool PhysicsWorld::HasMovingObjects(float tolerance) const {
    for (const auto& o : objects_) {
        if (o->pbody->IsStatic() || !o->pbody->IsAwake()) continue;
        float speed = std::sqrt(o->pbody->VelX() * o->pbody->VelX() + o->pbody->VelY() * o->pbody->VelY());
        if (speed > tolerance || std::fabs(o->pbody->AngularVelocity()) > tolerance) return true;
    }
    return false;
}

void PhysicsWorld::GetLocalPoint(const std::string& n, float wx, float wy, float& lx, float& ly) const {
    PhysObject* o = Find(n);
    if (!o) { lx = wx; ly = wy; return; }
    o->pbody->LocalPoint(wx, wy, lx, ly);
}

void PhysicsWorld::SetColliderType(const std::string& n, int type) {
    PhysObject* o = Find(n);
    if (o && type >= 0 && type < kColliderTypes) o->colliderType = type;
}
int PhysicsWorld::ColliderType(const std::string& n) const {
    PhysObject* o = Find(n);
    return o ? o->colliderType : -1;
}
void PhysicsWorld::SetDecoration(const std::string& n, const std::string& sprite, int amount, float increment, float scale) {
    if (PhysObject* o = Find(n)) { o->decoSprite = sprite; o->decoAmount = amount; o->decoIncrement = increment; o->decoScale = scale; }
}
void PhysicsWorld::SetSpriteLook(const std::string& n, int what, float a, float b) {
    PhysObject* o = Find(n);
    if (!o) return;
    if (what == 0) {                       // 0x47a1e0: the angle is wrapped into 0 .. 2 pi
        const float twoPi = 6.2831853f;
        a = std::fmod(a, twoPi);
        if (a < 0) a += twoPi;
        o->spriteAngle = a;
    } else if (what == 1) { o->pivotOffX = a; o->pivotOffY = b; }
    else o->textureScale = a;
}
void PhysicsWorld::SetObjectAlpha(const std::string& n, float a) {
    PhysObject* o = Find(n);
    if (o) o->alpha = a < 0 ? 0 : (a > 1 ? 1 : a);
}
float PhysicsWorld::ObjectAlpha(const std::string& n) const {
    PhysObject* o = Find(n);
    return o ? o->alpha : -1.0f;
}
void PhysicsWorld::SetObjectZOrder(const std::string& n, float z) {
    PhysObject* o = Find(n);
    if (o) { o->zOrder = z; o->zSet = true; }
}
float PhysicsWorld::ObjectZOrder(const std::string& n) const {
    PhysObject* o = Find(n);
    return o ? o->zOrder : -1e9f;
}
// front: one above the highest z, and at least 5 so that the object is in the group that is drawn last in every game;
// back: one below the lowest z, which makes it the first of its group
float PhysicsWorld::MoveObjectZ(const std::string& n, bool front) {
    PhysObject* o = Find(n);
    if (!o) return -1e9f;
    float hi = -1e9f, lo = 1e9f;
    for (auto& p : objects_) {
        if (p.get() == o) continue;
        hi = std::max(hi, p->zOrder);
        lo = std::min(lo, p->zOrder);
    }
    if (hi < lo) { hi = lo = o->zOrder; }                       // the only object
    o->zOrder = front ? std::max(hi + 1.0f, 5.0f) : lo - 1.0f;
    o->zSet = true;
    return o->zOrder;
}
void PhysicsWorld::SetColliderRule(int a, int b, bool collide) {
    if (a < 0 || b < 0 || a >= kColliderTypes || b >= kColliderTypes) return;
    colliderRules_.pair[a][b] = colliderRules_.pair[b][a] = collide;
}
bool PhysicsWorld::ColliderRule(int a, int b) const {
    return a >= 0 && b >= 0 && a < kColliderTypes && b < kColliderTypes && colliderRules_.pair[a][b];
}

void PhysicsWorld::SetCollisionEnabled(const std::string& n, bool on) {
    PhysObject* o = Find(n);
    if (!o) return;
    o->collision = on;
    o->pbody->SetSensor(!on);
}

void PhysicsWorld::RemoveJointsFromObject(const std::string& n) {
    for (size_t i = 0; i < links_.size();) {
        if (links_[i].end1 == n || links_[i].end2 == n) links_.erase(links_.begin() + i); else i++;
    }
    std::vector<std::string> names;
    for (auto& j : joints_)
        if (j->end1 == n || j->end2 == n) names.push_back(j->name);
    for (const std::string& name : names) DestroyJoint(name);
}

bool PhysicsWorld::HasJoints(const std::string& n) const {
    for (const auto& j : joints_)
        if (j->end1 == n || j->end2 == n) return true;
    for (const Link& l : links_)
        if (l.end1 == n || l.end2 == n) return true;
    return false;
}

std::vector<std::string> PhysicsWorld::IntersectingObjects(float x, float y, float left, float right, float down, float up) const {
    std::vector<std::string> out;
    float x0 = x - left, x1 = x + right, y0 = y - down, y1 = y + up;
    if (y0 > y1) std::swap(y0, y1);
    for (const auto& o : objects_) {
        float bx0, by0, bx1, by1;
        if (!o->pbody->Bounds(bx0, by0, bx1, by1)) continue;
        if (bx1 < x0 || bx0 > x1 || by1 < y0 || by0 > y1) continue;
        out.push_back(o->name);
    }
    return out;
}

}  // namespace ab
