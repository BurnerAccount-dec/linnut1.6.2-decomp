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
    o->zOrder = d.zOrder;
    o->sprite = d.sprite;          // createBox/Circle/Polygon: the sprite is looked up when the body is created
    o->rollFactor = rollFactor;
    o->collision = circle ? true : d.collision;
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
void PhysicsWorld::SetObjectAlpha(const std::string& n, float a) {
    PhysObject* o = Find(n);
    if (o) o->alpha = a < 0 ? 0 : (a > 1 ? 1 : a);
}
float PhysicsWorld::ObjectAlpha(const std::string& n) const {
    PhysObject* o = Find(n);
    return o ? o->alpha : -1.0f;
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
