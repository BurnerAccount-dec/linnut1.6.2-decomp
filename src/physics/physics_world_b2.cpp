// The Box2D side of the physics world. This file is compiled twice: for Box2D 2.1.1 (Angry Birds Classic and
// Seasons) and, with AB_BOX2D_V22 defined, for Box2D 2.2.0 (Angry Birds Rio). See physics_world.h.
#include <Box2D/Box2D.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <map>

#include "physics/physics_world.h"

#ifdef AB_BOX2D_V22
#define AB_B2_NAMESPACE box2d_v22
#define AB_B2_VERSION Box2DVersion::V22
#define AB_MAKE_WORLD MakePhysicsWorld22
#else
#define AB_B2_NAMESPACE box2d_v21
#define AB_B2_VERSION Box2DVersion::V21
#define AB_MAKE_WORLD MakePhysicsWorld21
#endif

namespace ab {
namespace AB_B2_NAMESPACE {

namespace {

class B2Body : public PhysBody {
public:
    explicit B2Body(b2Body* b) : b_(b) {}
    b2Body* Get() const { return b_; }
    void Wake(const char* why = "") { if (b_->GetType() != b2_staticBody && !b_->IsAwake()) { if (std::getenv("AB_WAKEDBG")) std::fprintf(stderr, "WAKE %s\n", why); b_->SetAwake(true); } }   // v2.2: a script that moves a body wakes it (levels now start asleep)

    bool IsStatic() const override { return b_->GetType() == b2_staticBody; }
    bool IsAwake() const override { return b_->IsAwake(); }
    void SetMaxSpeed(float s) override { b_->SetMaxSpeed(s); }
    void Touching(std::vector<PhysObject*>& out) const override {
        for (b2ContactEdge* e = b_->GetContactList(); e; e = e->next) {
            if (!e->contact->IsTouching()) continue;
            b2Fixture* fa = e->contact->GetFixtureA();
            b2Fixture* f = fa->GetBody() == b_ ? e->contact->GetFixtureB() : fa;
            PhysObject* o = static_cast<PhysObject*>(f->GetUserData());
            if (o) out.push_back(o);
        }
    }
    bool HasJoints() const override { return b_->GetJointList() != nullptr; }
    float PosX() const override { return b_->GetPosition().x; }
    float PosY() const override { return b_->GetPosition().y; }
#ifdef AB_BOX2D_V22
    float RotCol1X() const override { return b_->GetTransform().q.c; }
    float RotCol1Y() const override { return b_->GetTransform().q.s; }
#else
    float RotCol1X() const override { return b_->GetTransform().R.col1.x; }
    float RotCol1Y() const override { return b_->GetTransform().R.col1.y; }
#endif
    float VelX() const override { return b_->GetLinearVelocity().x; }
    float VelY() const override { return b_->GetLinearVelocity().y; }
    float AngularVelocity() const override { return b_->GetAngularVelocity(); }
    float Mass() const override { return b_->GetMass(); }
    void SetLinearVelocity(float x, float y) override { Wake("SetLinearVelocity"); b_->SetLinearVelocity(b2Vec2(x, y)); }
    void SetBullet(bool on) override { b_->SetBullet(on); }

    float Angle() const override { return b_->GetAngle(); }
    void SetAngularVelocity(float w) override { Wake("SetAngularVelocity"); b_->SetAngularVelocity(w); }
    void SetAwake(bool awake) override { if (awake && !b_->IsAwake() && std::getenv("AB_WAKEDBG")) std::fprintf(stderr, "WAKE SetAwake\n"); b_->SetAwake(awake); }
    void SetStatic(bool isStatic) override { if (!isStatic && !b_->IsAwake() && std::getenv("AB_WAKEDBG")) std::fprintf(stderr, "WAKE SetStatic\n"); b_->SetType(isStatic ? b2_staticBody : b2_dynamicBody); }
    void SetTransform(float x, float y, float angle) override { b_->SetTransform(b2Vec2(x, y), angle); }
    void WorldPoint(float lx, float ly, float& wx, float& wy) const override {
        b2Vec2 p = b_->GetWorldPoint(b2Vec2(lx, ly));
        wx = p.x; wy = p.y;
    }
    void LocalPoint(float wx, float wy, float& lx, float& ly) const override {
        b2Vec2 p = b_->GetLocalPoint(b2Vec2(wx, wy));
        lx = p.x; ly = p.y;
    }
    void ApplyImpulse(float ix, float iy, float px, float py) override { Wake("ApplyImpulse"); b_->ApplyLinearImpulse(b2Vec2(ix, iy), b2Vec2(px, py)); }
    void ApplyForce(float fx, float fy, float px, float py) override { Wake("ApplyForce"); b_->ApplyForce(b2Vec2(fx, fy), b2Vec2(px, py)); }
    void SetRestitution(float r) override { for (b2Fixture* f = b_->GetFixtureList(); f; f = f->GetNext()) f->SetRestitution(r); }
    void SetDensity(float d) override {
        for (b2Fixture* f = b_->GetFixtureList(); f; f = f->GetNext()) f->SetDensity(d);
        b_->ResetMassData();
    }
    void SetSensor(bool on) override {
        for (b2Fixture* f = b_->GetFixtureList(); f; f = f->GetNext()) f->SetSensor(on);
    }
    bool Bounds(float& x0, float& y0, float& x1, float& y1) const override {
        b2AABB box;
        bool first = true;
        if (!b_->IsActive()) return false;      // an inactive body has no broad-phase proxies, so its fixtures have no AABB
        for (const b2Fixture* f = b_->GetFixtureList(); f; f = f->GetNext()) {
#ifdef AB_BOX2D_V22
            const b2AABB& a = f->GetAABB(0);
#else
            const b2AABB& a = f->GetAABB();
#endif
            if (first) { box = a; first = false; } else box.Combine(box, a);
        }
        if (first) return false;
        x0 = box.lowerBound.x; y0 = box.lowerBound.y; x1 = box.upperBound.x; y1 = box.upperBound.y;
        return true;
    }
    float LinearDamping() const override { return b_->GetLinearDamping(); }
    float AngularDamping() const override { return b_->GetAngularDamping(); }
    void SetAngularDamping(float d) override { b_->SetAngularDamping(d); }
    void SetLinearDamping(float d) override { b_->SetLinearDamping(d); }
    void SetFixedRotation(bool on) override { b_->SetFixedRotation(on); }
    void SetActive(bool on) override { b_->SetActive(on); }
    float ShapeRadius() const override { return b_->GetFixtureList() ? b_->GetFixtureList()->GetShape()->m_radius : 0.0f; }
    float Inertia() const override {
        if (b_->GetType() != b2_dynamicBody) return 0.0f;
        b2Vec2 lc = b_->GetLocalCenter();
        float i = b_->GetInertia() - b_->GetMass() * (lc.x * lc.x + lc.y * lc.y);     // GetInertia is about the body origin
        return i > 0 ? i : 0.0f;
    }
    void CenterOfMass(float& x, float& y) const override { b2Vec2 c = b_->GetWorldCenter(); x = c.x; y = c.y; }
    const b2Fixture* SolidFixture() const { for (const b2Fixture* f = b_->GetFixtureList(); f; f = f->GetNext()) if (!f->IsSensor()) return f; return nullptr; }
    float Friction() const override { const b2Fixture* f = SolidFixture(); return f ? f->GetFriction() : 0.2f; }
    float Restitution() const override { const b2Fixture* f = SolidFixture(); return f ? f->GetRestitution() : 0.0f; }
#ifdef AB_BOX2D_V22
    int OverlapAt(const PhysBody& other, float x, float y) const override {
        const B2Body* o = dynamic_cast<const B2Body*>(&other);
        const b2Fixture* fa = b_->GetFixtureList();
        const b2Fixture* fb = o ? o->b_->GetFixtureList() : nullptr;
        if (!fa || !fb) return -1;
        b2Transform xf = b_->GetTransform();
        xf.p.Set(x, y);
        return b2TestOverlap(fa->GetShape(), 0, fb->GetShape(), 0, xf, o->b_->GetTransform()) ? 1 : 0;
    }
    bool ClosestPoints(const PhysBody& other, float& ax, float& ay, float& bx, float& by, float& dist, float& radA, float& radB) const override {
        const B2Body* o = dynamic_cast<const B2Body*>(&other);
        if (!o) return false;
        bool found = false;
        float best = 0;
        for (const b2Fixture* fa = b_->GetFixtureList(); fa; fa = fa->GetNext()) {
            if (fa->IsSensor()) continue;
            for (const b2Fixture* fb = o->b_->GetFixtureList(); fb; fb = fb->GetNext()) {
                if (fb->IsSensor()) continue;
                b2DistanceInput in;
                in.proxyA.Set(fa->GetShape(), 0); in.proxyB.Set(fb->GetShape(), 0);
                in.transformA = b_->GetTransform(); in.transformB = o->b_->GetTransform();
                in.useRadii = false;                                  // the cores; the skin radii are added back by the caller
                b2SimplexCache cache; cache.count = 0;
                b2DistanceOutput out;
                b2Distance(&out, &cache, &in);
                float surface = out.distance - fa->GetShape()->m_radius - fb->GetShape()->m_radius;
                if (!found || surface < best) {
                    found = true; best = surface;
                    ax = out.pointA.x; ay = out.pointA.y; bx = out.pointB.x; by = out.pointB.y; dist = out.distance;
                    radA = fa->GetShape()->m_radius; radB = fb->GetShape()->m_radius;
                }
            }
        }
        return found;
    }
#endif

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

class ContactRef : public ContactHandle {
public:
    explicit ContactRef(b2Contact* c) : c_(c) {}
    void SetEnabled(bool on) override { c_->SetEnabled(on); }
    void WorldManifold(float& px, float& py, float& nx, float& ny) override {
        b2WorldManifold wm;
        c_->GetWorldManifold(&wm);
        px = wm.points[0].x; py = wm.points[0].y; nx = wm.normal.x; ny = wm.normal.y;
    }

private:
    b2Contact* c_;
};

b2Body* BodyOf(const PhysObject* o) { return static_cast<B2Body*>(o->pbody.get())->Get(); }

// The angle the joint solvers measure: the body's raw sweep angle. Box2D 2.2.0's GetAngle() wraps it to (-pi, pi], so a joint def
// initialised from GetAngle() gets a reference angle that is off by 2*pi for a body whose angle was set to e.g. 4.37 (the scripts
// keep angles in [0, 2*pi)) - the solver then tries to turn the bodies a whole revolution and the structure explodes.
// 2.1.1's GetAngle() is the sweep angle itself.
float RelativeAngle(const b2Body* a, const b2Body* b) {
#ifdef AB_BOX2D_V22
    return b->GetSweepAngle() - a->GetSweepAngle();
#else
    return b->GetAngle() - a->GetAngle();
#endif
}

class Impl : public PhysicsWorld, public b2ContactListener, public b2ContactFilter, public b2DestructionListener {
public:
    Impl() : world_(b2Vec2(kGravityX, kGravityY), true) {
        world_.SetWarmStarting(std::getenv("AB_NOWARM") == nullptr);
        world_.SetContinuousPhysics(std::getenv("AB_NOCCD") == nullptr);   // the exe runs with continuous collision on (world flag at +0x19255 is set to 1 and never cleared); AB_NOCCD=1 turns it off for testing
        world_.SetContactListener(this);
        world_.SetContactFilter(this);
        world_.SetDestructionListener(this);
#ifdef AB_BOX2D_V22
        if (const char* r = std::getenv("AB_POLYRADIUS")) b2_polygonRadiusValue = (float)std::atof(r);   // test override
        if (const char* r = std::getenv("AB_SOLVERSKIN")) b2_solverSkinFraction = (float)std::atof(r);   // test override
#endif
    }

    ~Impl() override {
        // the world is destroyed after this body runs; keep the callbacks from touching freed records
        world_.SetDestructionListener(nullptr);
        world_.SetContactListener(nullptr);
        world_.SetContactFilter(nullptr);
    }

    Box2DVersion Version() const override { return AB_B2_VERSION; }
#ifdef AB_BOX2D_V22
    void SetBox2DFlavour(bool space) override { b2_coincidentCircleNormal = !space; b2_spaceBuild = space; }
#endif
#ifdef AB_BOX2D_V22
    void SetPolygonRadius(float r) override { if (r >= 0) b2_polygonRadiusValue = r; }
    float PolygonRadius() const override { return b2_polygonRadiusValue; }
    void SetSolverSettings(float linearSlop, float timeToSleep, float linearSleepTolerance) override { b2_linearSlopValue = linearSlop; b2_timeToSleepValue = timeToSleep; b2_linearSleepToleranceValue = linearSleepTolerance; }
#endif

    void Step(float dt, int velocityIterations, int positionIterations) override {
        // AngryBirdsRio.exe steps the world with 10 velocity and 10 position iterations (0x4e4480 is called with 1/30, 10, 10 at 0x40e7b3 area);
        // the scripts' own counts are used as they come.
        static const char* vi = std::getenv("AB_VELITERS"); static const char* pi = std::getenv("AB_POSITERS");   // test overrides
        if (vi) velocityIterations = std::atoi(vi);
        if (pi) positionIterations = std::atoi(pi);
#ifdef AB_BOX2D_V22
        b2_velocityThresholdV22 = Tn("solver.velocityThreshold", 1.0f); b2_maxLinearCorrectionV22 = Tn("solver.maxLinearCorrection", 0.2f);
        b2_baumgarteV22 = Tn("solver.baumgarte", 0.2f); b2_maxRotationV22 = Tn("solver.maxRotation", 1.5707964f); b2_angularSleepToleranceV22 = Tn("solver.angularSleepTolerance", 0.034906585f);
#else
        b2_velocityThresholdV21 = Tn("solver.velocityThreshold", 1.0f); b2_maxLinearCorrectionV21 = Tn("solver.maxLinearCorrection", 0.2f);
        b2_contactBaumgarteV21 = Tn("solver.baumgarte", 0.2f); b2_maxRotationV21 = Tn("solver.maxRotation", 1.5707964f); b2_angularSleepToleranceV21 = Tn("solver.angularSleepTolerance", 0.034906585f);
#endif
        world_.Step(dt, velocityIterations, positionIterations);
        for (size_t i = 0; i < joints_.size(); i++)      // v2.2: backAndForth joints turn round at their limits without the scripts having to ask
            if (joints_[i]->backAndForth) CheckJointLimits(joints_[i]->name);
        // debug aid: AB_CONTACTDBG=<part of an object name> lists the contacts of the matching objects after every step
        static const char* watch = std::getenv("AB_CONTACTDBG");
        static int stepNo = 0;
        stepNo++;
        if (watch)
            for (b2Contact* c = world_.GetContactList(); c; c = c->GetNext()) {
                PhysObject* a = static_cast<PhysObject*>(c->GetFixtureA()->GetUserData());
                PhysObject* b = static_cast<PhysObject*>(c->GetFixtureB()->GetUserData());
                if (!a || !b || (a->name.find(watch) == std::string::npos && b->name.find(watch) == std::string::npos)) continue;
                std::fprintf(stderr, "CT %d %s | %s touching=%d enabled=%d points=%d\n", stepNo, a->name.c_str(), b->name.c_str(),
                             c->IsTouching() ? 1 : 0, c->IsEnabled() ? 1 : 0, c->GetManifold()->pointCount);
            }
    }
    // The aiming aid's arc (extension of the retail trail, which is the bird's free flight): the bird alone in a world with the same gravity, launched with
    // the given velocity and stepped like the real world (1/30 s, 10 + 10 iterations, the speed caps after every step). No blocks, no ground.
    bool SimulateFlight(const std::string& bird, float vx, float vy, int steps, int every, float h, std::vector<std::pair<float, float>>& out, const float* start = nullptr) override {
        PhysObject* o = Find(bird);
        if (!o || !o->pbody) return false;
        b2Body* src = BodyOf(o);
        b2World scratch(world_.GetGravity(), true);
        b2BodyDef bd; bd.type = b2_dynamicBody; bd.position = start ? b2Vec2(start[0], start[1]) : src->GetPosition(); bd.angle = src->GetAngle();
        bd.linearDamping = src->GetLinearDamping(); bd.angularDamping = src->GetAngularDamping(); bd.bullet = true;
        b2Body* bb = scratch.CreateBody(&bd);
        for (b2Fixture* f = src->GetFixtureList(); f; f = f->GetNext()) {
            if (f->IsSensor()) continue;
            b2FixtureDef fd; fd.shape = f->GetShape(); fd.friction = f->GetFriction(); fd.restitution = f->GetRestitution(); fd.density = f->GetDensity(); fd.filter = f->GetFilterData();
            bb->CreateFixture(&fd);
        }
        bb->SetLinearVelocity(b2Vec2(vx, vy));
        for (int i = 0; i < steps; i++) {
            scratch.Step(h, 10, 10);
            if (o->capVx > 0 || o->capVy > 0 || o->capSpeed > 0) {
                b2Vec2 n0 = bb->GetLinearVelocity();
                if (o->capVx > 0) n0.x = std::max(-o->capVx, std::min(o->capVx, n0.x));
                if (o->capVy > 0) n0.y = std::max(-o->capVy, std::min(o->capVy, n0.y));
                if (o->capSpeed > 0) { float sp = std::sqrt(n0.x * n0.x + n0.y * n0.y); if (sp > o->capSpeed) { float k = o->capSpeed / sp; n0.x *= k; n0.y *= k; } }
                bb->SetLinearVelocity(n0);
            }
            b2Vec2 p = bb->GetPosition();
            if (every > 0 && (i + 1) % every == 0) out.push_back({p.x, p.y});
        }
        return true;
    }
    void ClearForces() override { world_.ClearForces(); }
    void SetGravity(float x, float y) override { world_.SetGravity(b2Vec2(x, y)); }
    void SetMaxTranslation(float t) override { b2_maxTranslation = t; b2_maxTranslationSquared = t * t; }
    void RayCastAll(float x1, float y1, float x2, float y2, std::vector<RayHit>& out) const override {
        struct Collect : b2RayCastCallback {
            std::vector<RayHit>* hits;
            float32 ReportFixture(b2Fixture* f, const b2Vec2& p, const b2Vec2& n, float32 fraction) override {
                PhysObject* o = static_cast<PhysObject*>(f->GetUserData());
                if (!f->IsSensor() && o) hits->push_back({o, p.x, p.y, n.x, n.y, fraction});
                return 1.0f;
            }
        } cb;
        cb.hits = &out;
        world_.RayCast(&cb, b2Vec2(x1, y1), b2Vec2(x2, y2));
    }
    void GetGravity(float& x, float& y) const override { b2Vec2 g = world_.GetGravity(); x = g.x; y = g.y; }

    BodyInfo Inspect(const std::string& name) const override {
        BodyInfo i;
        PhysObject* o = Find(name);
        if (!o) return i;
        const PhysBody& b = *o->pbody;
        i.valid = true;
        i.isStatic = b.IsStatic(); i.awake = b.IsAwake();
        i.posX = b.PosX(); i.posY = b.PosY(); i.angle = b.Angle();
        i.velX = b.VelX(); i.velY = b.VelY(); i.angularVelocity = b.AngularVelocity();
        i.linearDamping = b.LinearDamping(); i.angularDamping = b.AngularDamping();
        i.mass = b.Mass(); i.shapeRadius = b.ShapeRadius();
        return i;
    }

    bool InspectJoint(const std::string& name, float& motorSpeed, float& frequency, float& dampingRatio) const override {
        motorSpeed = frequency = dampingRatio = 0;
        for (const auto& r : joints_) {
            if (r->name != name) continue;
            b2Joint* joint = static_cast<b2Joint*>(r->joint);
            if (joint->GetType() == e_revoluteJoint) motorSpeed = static_cast<b2RevoluteJoint*>(joint)->GetMotorSpeed();
            if (joint->GetType() == e_prismaticJoint) motorSpeed = static_cast<b2PrismaticJoint*>(joint)->GetMotorSpeed();
            if (joint->GetType() == e_distanceJoint) {
                frequency = static_cast<b2DistanceJoint*>(joint)->GetFrequency();
                dampingRatio = static_cast<b2DistanceJoint*>(joint)->GetDampingRatio();
            }
            return true;
        }
        return false;
    }

    // ---- creation
    float CreateBox(const BodyDef& d) override {
        PhysObject* o = MakeObject(d, -1.0f, false);
        b2PolygonShape shape;
        shape.SetAsBox(d.w * 0.5f, d.h * 0.5f);
        MakeFixture(BodyOf(o), o, shape, d);
        AddObject(o, false);
        return o->pbody->Mass();
    }

    // Angry Birds Space createCompoundObject: one body, one fixture per part (a box, a polygon, a line as a thin box, or a circle)
    float CreateCompound(const BodyDef& d, const std::vector<CompoundShape>& shapes, float damping) override {
        PhysObject* o = MakeObject(d, -1.0f, false);
        b2Body* body = BodyOf(o);
        for (const CompoundShape& cs : shapes) {
            if (cs.kind == 3) {
                b2CircleShape c;
                c.m_radius = cs.w;
                c.m_p.Set(cs.x, cs.y);
                MakeFixture(body, o, c, d);
            } else if (cs.kind == 1 && cs.verts.size() >= 6) {
                float ca = std::cos(cs.angle), sa = std::sin(cs.angle);
                std::vector<b2Vec2> pts;
                for (size_t i = 0; i + 1 < cs.verts.size() && pts.size() < 8; i += 2)
                    pts.push_back(b2Vec2(cs.x + ca * cs.verts[i] - sa * cs.verts[i + 1], cs.y + sa * cs.verts[i] + ca * cs.verts[i + 1]));
                b2PolygonShape poly;
                poly.Set(pts.data(), (int)pts.size());
                MakeFixture(body, o, poly, d);
            } else {
                b2PolygonShape box;
                float hh = cs.kind == 2 ? 0.05f : cs.h * 0.5f;
                box.SetAsBox(cs.w * 0.5f, hh, b2Vec2(cs.x, cs.y), cs.angle);
                MakeFixture(body, o, box, d);
            }
        }
        o->parts = shapes;
        o->pbody->SetLinearDamping(damping);
        if (d.controllable) o->pbody->SetAngularDamping(2.0f);
        AddObject(o, d.controllable);
        return o->pbody->Mass();
    }

    float CreateCircle(const BodyDef& d) override {
        PhysObject* o = MakeObject(d, d.w, true);
        b2CircleShape shape;
        shape.m_radius = d.w;
        shape.m_p.Set(0, 0);
        MakeFixture(BodyOf(o), o, shape, d);
        // GUESS: the exe tests the z_order argument here (ftol(...) == 999). Check on a real level.
        o->special999 = (int)d.zOrder == 999;
        if (d.controllable) o->pbody->SetAngularDamping(2.0f);
        AddObject(o, d.controllable);
        return o->pbody->Mass();
    }

    float CreatePolygon(const BodyDef& d, const std::vector<float>& verts) override {
        PhysObject* o = MakeObject(d, -1.0f, false);
        std::vector<b2Vec2> pts;
        for (size_t i = 0; i + 1 < verts.size(); i += 2) pts.push_back(b2Vec2(verts[i], verts[i + 1]));
        b2PolygonShape shape;
        shape.Set(pts.data(), (int)pts.size());
        MakeFixture(BodyOf(o), o, shape, d);
        if (d.controllable) o->pbody->SetAngularDamping(2.0f);
        AddObject(o, false);
        return o->pbody->Mass();
    }

#ifdef AB_BOX2D_V22
    // 0x4254d0: one b2EdgeShape fixture for each pair of neighbouring vertices, created with the object's density, friction and restitution
    float CreateLine(const BodyDef& d, const std::vector<float>& verts) override {
        PhysObject* o = MakeObject(d, -1.0f, false);
        b2Body* body = BodyOf(o);
        for (size_t i = 0; i + 3 < verts.size(); i += 2) {
            b2EdgeShape edge;
            edge.Set(b2Vec2(verts[i], verts[i + 1]), b2Vec2(verts[i + 2], verts[i + 3]));
            MakeFixture(body, o, edge, d);
        }
        AddObject(o, false);
        return o->pbody->Mass();
    }
#endif

    // 0x40ecd0. The anchors follow the exe literally, including its quirk for distance joints with
    // coordType 1: the local anchor is (world point - body position), which ignores the body's rotation.
    bool CreateJoint(JointDef& d) override {
        if (d.type == 5) {
            Link l;
            l.name = d.name; l.end1 = d.end1; l.end2 = d.end2; l.destroyTimer = d.destroyTimer;
            links_.push_back(l);
            return true;
        }
        PhysObject* o1 = Find(d.end1);
        PhysObject* o2 = Find(d.end2);
        if (!o1 || !o2) return false;
        if (std::getenv("AB_JOINTDBG") && d.end1.find(std::getenv("AB_JOINTDBG")) != std::string::npos) std::fprintf(stderr, "JT type=%d %s - %s coord=%d freq=%g damp=%g limit=%d lo=%g hi=%g motor=%d coll=%d\n", d.type, d.end1.c_str(), d.end2.c_str(), d.coordType, d.frequency, d.dampingRatio, (int)d.limit, d.lowerLimit, d.upperLimit, (int)d.motor, (int)d.collideConnected);
        b2Body* b1 = BodyOf(o1);
        b2Body* b2b = BodyOf(o2);
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
            jd.referenceAngle = RelativeAngle(b1, b2b);
            jd.collideConnected = d.collideConnected;
            joint = world_.CreateJoint(&jd);
            break;
        }
        case 3: {  // revolute
            b2RevoluteJointDef jd;
            b2Vec2 anchor = b1->GetWorldPoint(b2Vec2(d.x1, d.y1));
            jd.Initialize(b1, b2b, anchor);
            jd.referenceAngle = RelativeAngle(b1, b2b);
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
            jd.referenceAngle = RelativeAngle(b1, b2b);
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
        r->backAndForth = d.backAndForth && d.motor && d.limit && (d.type == 3 || d.type == 4);
        if (d.motor) { joint->GetBodyA()->SetAwake(true); joint->GetBodyB()->SetAwake(true); }   // a motor drives its bodies from the start
        r->view.reset(new B2JointView(joint, d.coordType));
        joints_.push_back(std::move(r));
        RebuildJointViews();
        return true;
    }

    void SetJointParameters(const JointParams& p) override {
        for (auto& rec : joints_) {      // 0x411a20 walks every joint record and applies the values to each one with this name
        if (rec->name != p.name) continue;
        b2Joint* joint = static_cast<b2Joint*>(rec->joint);
        if (joint->GetType() == e_revoluteJoint) {
            b2RevoluteJoint* j = static_cast<b2RevoluteJoint*>(joint);
            if (p.hasMotor) j->EnableMotor(p.motor);
            if (p.hasMotorSpeed) j->SetMotorSpeed(p.motorSpeed);
            if (p.hasMaxTorque) j->SetMaxMotorTorque(p.maxTorque);
            if (p.hasLimit) j->EnableLimit(p.limit);
            if (p.hasLowerLimit) j->SetLimits(p.lowerLimit, j->GetUpperLimit());
            if (p.hasUpperLimit) j->SetLimits(j->GetLowerLimit(), p.upperLimit);
        } else if (joint->GetType() == e_prismaticJoint) {
            b2PrismaticJoint* j = static_cast<b2PrismaticJoint*>(joint);
            if (p.hasMotor) j->EnableMotor(p.motor);
            if (p.hasMotorSpeed) j->SetMotorSpeed(p.motorSpeed);
            if (p.hasMaxTorque) j->SetMaxMotorForce(p.maxTorque);
            if (p.hasLimit) j->EnableLimit(p.limit);
            if (p.hasLowerLimit) j->SetLimits(p.lowerLimit, j->GetUpperLimit());
            if (p.hasUpperLimit) j->SetLimits(j->GetLowerLimit(), p.upperLimit);
        } else if (joint->GetType() == e_distanceJoint) {
            b2DistanceJoint* j = static_cast<b2DistanceJoint*>(joint);
            if (p.hasFrequency) j->SetFrequency(p.frequency);
            if (p.hasDampingRatio) j->SetDampingRatio(p.dampingRatio);
        }
        }
    }

    // 0x410810 (handleJointLimits in Star Wars, the same body as checkJointLimits): a motor with limits that has reached the limit its speed points at turns round, or stops when `stop` is set.
    // The exe's prismatic test reads (motor and limit on and speed > 0 and upper reached) or (speed < 0 and lower reached): the lower side does not look at the two flags.
    void CheckJointLimits(const std::string& name, bool stop = false) override {
        for (auto& rec : joints_) {      // every joint with this name (0x410810 loops over all records)
        if (rec->name != name) continue;
        b2Joint* joint = static_cast<b2Joint*>(rec->joint);
        if (joint->GetType() == e_revoluteJoint) {
            b2RevoluteJoint* j = static_cast<b2RevoluteJoint*>(joint);
            if (!(j->IsMotorEnabled() && j->IsLimitEnabled())) continue;
            float speed = j->GetMotorSpeed();
            float angle = j->GetJointAngle();
            if ((speed > 0 && angle >= j->GetUpperLimit()) || (speed < 0 && angle <= j->GetLowerLimit()))
                j->SetMotorSpeed(stop ? 0.0f : -speed);
        } else if (joint->GetType() == e_prismaticJoint) {
            b2PrismaticJoint* j = static_cast<b2PrismaticJoint*>(joint);
            float speed = j->GetMotorSpeed();
            float t = j->GetJointTranslation();
            if ((j->IsMotorEnabled() && j->IsLimitEnabled() && speed > 0 && t >= j->GetUpperLimit()) || (speed < 0 && t <= j->GetLowerLimit()))
                j->SetMotorSpeed(stop ? 0.0f : -speed);
        }
        }
    }

    // ---- Box2D callbacks
    void BeginContact(b2Contact* c) override {
        PhysObject* a = static_cast<PhysObject*>(c->GetFixtureA()->GetUserData());
        PhysObject* b = static_cast<PhysObject*>(c->GetFixtureB()->GetUserData());
        if (!a || !b) return;
        ContactRef ref(c);
        if (onBeginContact) onBeginContact(*a, *b, ref);
    }
    void EndContact(b2Contact* c) override {
        PhysObject* a = static_cast<PhysObject*>(c->GetFixtureA()->GetUserData());
        PhysObject* b = static_cast<PhysObject*>(c->GetFixtureB()->GetUserData());
        if (a && b && onEndContact) onEndContact(*a, *b);
    }
    bool ShouldCollide(b2Fixture* fa, b2Fixture* fb) override {
        return ShouldCollideObjects(static_cast<PhysObject*>(fa->GetUserData()), static_cast<PhysObject*>(fb->GetUserData()));
    }
    void SayGoodbye(b2Joint* joint) override { DropJointRecord(joint); }
    void SayGoodbye(b2Fixture*) override {}

protected:
    void DestroyBody(PhysObject* o) override { world_.DestroyBody(BodyOf(o)); }
    void DestroyBox2DJoint(void* joint) override { world_.DestroyJoint(static_cast<b2Joint*>(joint)); }

private:
    // the object and its Box2D body (b2BodyDef as the exe fills it)
    PhysObject* MakeObject(const BodyDef& d, float rollFactor, bool circle) {
        PhysObject* o = NewObject(d, rollFactor, circle);
        o->isCircle = circle; o->boxW = d.w; o->boxH = d.h;
        b2BodyDef bd;
        bd.type = d.density != 0.0f ? b2_dynamicBody : b2_staticBody;
        bd.position.Set(d.x, d.y);
        bd.angle = 0;
        bd.linearDamping = 0;
        bd.angularDamping = 1.0f;
        bd.allowSleep = true;
        bd.awake = d.density == 0.0f || !startAsleep;
        bd.active = true;
        bd.userData = o;
        o->pbody.reset(new B2Body(world_.CreateBody(&bd)));
        return o;
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

    b2World world_;
};

}  // namespace

}  // namespace AB_B2_NAMESPACE

std::unique_ptr<PhysicsWorld> AB_MAKE_WORLD() { return std::unique_ptr<PhysicsWorld>(new AB_B2_NAMESPACE::Impl()); }

}  // namespace ab
