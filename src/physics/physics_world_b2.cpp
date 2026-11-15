// The Box2D side of the physics world. This file is compiled twice: for Box2D 2.1.1 (Angry Birds Classic and
// Seasons) and, with AB_BOX2D_V22 defined, for Box2D 2.2.0 (Angry Birds Rio). See physics_world.h.
#include <Box2D/Box2D.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <map>

#include "physics/physics_world.h"
#include "physics/polygon_decompose.h"

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
    void SetKinematic() override { b_->SetType(b2_kinematicBody); }
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
    void SetFriction(float f) override { for (b2Fixture* fx = b_->GetFixtureList(); fx; fx = fx->GetNext()) fx->SetFriction(f); }
    void ResizeCircle(float r) override {
        for (b2Fixture* fx = b_->GetFixtureList(); fx; fx = fx->GetNext())
            if (fx->GetShape()->GetType() == b2Shape::e_circle) const_cast<b2CircleShape*>(static_cast<const b2CircleShape*>(fx->GetShape()))->m_radius = r;
        b_->ResetMassData();
        b_->SetTransform(b_->GetPosition(), b_->GetAngle());     // refreshes the broad-phase boxes
    }
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

#ifdef AB_BOX2D_V22
#define AB_WHEEL 1
#else
#define AB_WHEEL 0
#endif

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
        ProcessGearKills();
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
    bool SimulateFlight(const std::string& bird, float vx, float vy, int steps, int every, float h, std::vector<std::pair<float, float>>& out, const float* start = nullptr, const float* force = nullptr, const float* zones = nullptr, int zoneCount = 0) override {   // zones: 5 numbers each (x, y, radius, gx, gy), round areas where the bird has its own gravity (v3.3); force: a push applied before every step, like a script's applyForce each frame (the Boomerang Bird's lift)
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
        { b2MassData md; src->GetMassData(&md); bb->SetMassData(&md); }      // v3.1: the copy weighs what the real bird weighs (a pushed bird, like the Boomerang, flew with the wrong mass)
        bb->SetLinearVelocity(b2Vec2(vx, vy));
        const float bodyR = o->pbody->ShapeRadius();
        const b2Vec2 worldG = world_.GetGravity();
        for (int i = 0; i < steps; i++) {
            if (force) bb->ApplyForce(b2Vec2(force[0], force[1]), bb->GetWorldCenter());
            // v3.3: inside a gravity area the bird accelerates by the area's gravity instead of the world's (what setObjectGravity does in the real world, as a script's sensor zone uses it)
            for (int z = 0; z < zoneCount; z++) {
                const float* a = zones + z * 5;
                b2Vec2 p0 = bb->GetPosition();
                float dx = p0.x - a[0], dy = p0.y - a[1], reach = a[2] + bodyR;
                if (dx * dx + dy * dy < reach * reach) {
                    float m = bb->GetMass();
                    bb->ApplyForce(b2Vec2(m * (a[3] - worldG.x), m * (a[4] - worldG.y)), bb->GetWorldCenter());
                    break;
                }
            }
            scratch.Step(h, 10, 10);
            scratch.ClearForces();      // forces are not cleared by the step here (as in the exe): the frame loop clears them, so the copy does too
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
    void ShapeCastAll(float x1, float y1, float x2, float y2, const CastShape& cs, const std::string& skip, std::vector<RayHit>& out) const override {
        if (cs.kind == 0) { RayCastAll(x1, y1, x2, y2, out); return; }
        // the swept shape: a circle, or convex pieces of any number of points (a concave outline is split; the distance proxy has no vertex limit)
        b2CircleShape circle;
        std::vector<std::vector<b2Vec2>> pieces;
        std::vector<b2DistanceProxy> proxies;
        float ext = cs.radius;
        if (cs.kind == 1) {
            circle.m_p.SetZero(); circle.m_radius = cs.radius;
            b2DistanceProxy px;
#ifdef AB_BOX2D_V22
            px.Set(&circle, 0);
#else
            px.Set(&circle);
#endif
            proxies.push_back(px);
        } else {
            float c = std::cos(cs.angle), s = std::sin(cs.angle);
            std::vector<polydecomp::Pt> pp;
            for (size_t i = 0; i + 1 < cs.pts.size(); i += 2) pp.push_back({cs.pts[i] * c - cs.pts[i + 1] * s, cs.pts[i] * s + cs.pts[i + 1] * c});
            std::vector<std::vector<polydecomp::Pt>> parts;
            polydecomp::Decompose(pp, 256, parts);
            for (const auto& part : parts) {
                std::vector<b2Vec2> v;
                for (const polydecomp::Pt& q : part) { v.push_back(b2Vec2(q.x, q.y)); ext = std::max(ext, std::sqrt(q.x * q.x + q.y * q.y)); }
                if (v.size() >= 3) pieces.push_back(v);
            }
            if (pieces.empty()) return;
            for (const auto& v : pieces) { b2DistanceProxy px; px.m_vertices = v.data(); px.m_count = (int)v.size(); px.m_radius = 0.0f; proxies.push_back(px); }
        }
        ext += 0.1f;
        b2AABB box; box.lowerBound.Set(std::min(x1, x2) - ext, std::min(y1, y2) - ext); box.upperBound.Set(std::max(x1, x2) + ext, std::max(y1, y2) + ext);
        struct Q : b2QueryCallback { std::vector<b2Fixture*> f; bool ReportFixture(b2Fixture* x) override { f.push_back(x); return true; } } q;
        world_.QueryAABB(&q, box);
        float dx = x2 - x1, dy = y2 - y1, len = std::sqrt(dx * dx + dy * dy);
        for (b2Fixture* f : q.f) {
            PhysObject* o = static_cast<PhysObject*>(f->GetUserData());
            if (f->IsSensor() || !o || (!skip.empty() && o->name == skip)) continue;
            b2Body* body = f->GetBody();
            b2DistanceProxy proxyB;
#ifdef AB_BOX2D_V22
            proxyB.Set(f->GetShape(), 0);
#else
            proxyB.Set(f->GetShape());
#endif
            // every convex piece of the swept shape against this fixture: the earliest touch counts
            float bestT = 2.0f; size_t best = 0;
            for (size_t k = 0; k < proxies.size(); k++) {
                b2TOIInput in; in.proxyA = proxies[k]; in.proxyB = proxyB; in.tMax = 1.0f;
                in.sweepA.localCenter.SetZero(); in.sweepA.c0.Set(x1, y1); in.sweepA.c.Set(x2, y2); in.sweepA.a0 = in.sweepA.a = 0;
                in.sweepB.localCenter = body->GetLocalCenter(); in.sweepB.c0 = in.sweepB.c = body->GetWorldCenter(); in.sweepB.a0 = in.sweepB.a = body->GetAngle();
                b2TOIOutput out2; b2TimeOfImpact(&out2, &in);
                // the bundled 2.1 solver (exe-matched) reports only t and leaves the state unknown: t < 1 is a hit there
                bool hit = out2.state == b2TOIOutput::e_touching || out2.state == b2TOIOutput::e_overlapped || (out2.state == b2TOIOutput::e_unknown && out2.t < 1.0f);
                if (!hit) continue;
                float t = out2.state == b2TOIOutput::e_overlapped ? 0.0f : out2.t;
                if (t < bestT) { bestT = t; best = k; }
            }
            if (bestT > 1.0f) continue;
            float t = bestT;
            float cxp = x1 + dx * t, cyp = y1 + dy * t;
            b2Transform xa; xa.Set(b2Vec2(cxp, cyp), 0);
            b2DistanceInput di; di.proxyA = proxies[best]; di.proxyB = proxyB; di.transformA = xa; di.transformB = body->GetTransform(); di.useRadii = true;
            b2SimplexCache cache; cache.count = 0; b2DistanceOutput dout; b2Distance(&dout, &cache, &di);
            RayHit h; h.obj = o; h.fraction = t; h.cx = cxp; h.cy = cyp;
            b2Vec2 n(dx, dy);
            if (dout.distance > 1e-5f) { n = dout.pointA - dout.pointB; n.Normalize(); h.px = dout.pointB.x; h.py = dout.pointB.y; }
            else { if (len > 1e-6f) n = b2Vec2(-dx / len, -dy / len); h.px = cxp; h.py = cyp; }
            h.nx = n.x; h.ny = n.y;
            out.push_back(h);
        }
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
                for (size_t i = 0; i + 1 < cs.verts.size(); i += 2)
                    pts.push_back(b2Vec2(cs.x + ca * cs.verts[i] - sa * cs.verts[i + 1], cs.y + sa * cs.verts[i] + ca * cs.verts[i + 1]));
                std::vector<polydecomp::Pt> pp;
                for (const b2Vec2& v : pts) pp.push_back({v.x, v.y});
                if (pts.size() <= 8 && (polydecomp::IsConvex(pp) || polydecomp::IsConvex(std::vector<polydecomp::Pt>(pp.rbegin(), pp.rend())))) {
                    b2PolygonShape poly;
                    poly.Set(pts.data(), (int)pts.size());
                    MakeFixture(body, o, poly, d);
                } else AddConcaveFixtures(body, o, pts, d);      // concave, or more points than Box2D takes in one shape
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
        float area2 = 0;     // twice the signed area: Box2D wants the points counter-clockwise, and Stella has shapes drawn the other way round
        for (size_t i = 0; i < pts.size(); i++) { const b2Vec2& p = pts[i]; const b2Vec2& q = pts[(i + 1) % pts.size()]; area2 += p.x * q.y - q.x * p.y; }
        if (area2 < 0) std::reverse(pts.begin(), pts.end());
        if (pts.size() >= 3 && std::fabs(area2) < 2e-5f) pts.resize(2);    // no area: made as the small box below
        bool concave = false;
        if (pts.size() >= 3) {
            std::vector<polydecomp::Pt> pp;
            for (const b2Vec2& v : pts) pp.push_back({v.x, v.y});
            concave = !polydecomp::IsConvex(pp);
        }
        if (concave) {
            // a concave outline (a star, a crescent, an L): convex pieces of at most b2_maxPolygonVertices points, one fixture each; they share the body, so it moves as one
            AddConcaveFixtures(BodyOf(o), o, pts, d);
        } else if (pts.size() >= 3 && (int)pts.size() <= b2_maxPolygonVertices) {
            b2PolygonShape shape;
            shape.Set(pts.data(), (int)pts.size());
            MakeFixture(BodyOf(o), o, shape, d);
        } else if (pts.size() > (size_t)b2_maxPolygonVertices) {
            // Stella's levels have shapes with more points than Box2D takes (8): a fan of triangles from the first point (Box2D asserts, or writes
            // past its array in a release build, on a polygon this large). Triangles too thin to be a shape are left out.
            for (size_t i = 1; i + 1 < pts.size(); i++) {
                b2Vec2 tri[3] = {pts[0], pts[i], pts[i + 1]};
                float area = 0.5f * ((tri[1].x - tri[0].x) * (tri[2].y - tri[0].y) - (tri[2].x - tri[0].x) * (tri[1].y - tri[0].y));
                if (std::fabs(area) < 1e-5f) continue;
                if (area < 0) std::swap(tri[1], tri[2]);   // counter-clockwise, as Box2D wants
                b2PolygonShape shape;
                shape.Set(tri, 3);
                MakeFixture(BodyOf(o), o, shape, d);
            }
        } else {
            b2PolygonShape shape;      // fewer than three points: a small box, so the object still has a body
            shape.SetAsBox(0.05f, 0.05f);
            MakeFixture(BodyOf(o), o, shape, d);
        }
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
        if (d.type == 8) { if (!o1) return false; }                         // mouse: one body, pulled towards a point
        else if (d.type != 7 && (!o1 || !o2)) return false;                 // gear: two joints, no bodies of its own
        if (std::getenv("AB_JOINTDBG") && d.end1.find(std::getenv("AB_JOINTDBG")) != std::string::npos) std::fprintf(stderr, "JT type=%d %s - %s coord=%d freq=%g damp=%g limit=%d lo=%g hi=%g motor=%d coll=%d\n", d.type, d.end1.c_str(), d.end2.c_str(), d.coordType, d.frequency, d.dampingRatio, (int)d.limit, d.lowerLimit, d.upperLimit, (int)d.motor, (int)d.collideConnected);
        b2Body* b1 = o1 ? BodyOf(o1) : nullptr;
        b2Body* b2b = o2 ? BodyOf(o2) : nullptr;
        b2Joint* joint = nullptr;
        b2Joint* gearJ1 = nullptr; b2Joint* gearJ2 = nullptr;

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
        // ---- the joints no editor offers (6-11): the Box2D joint types the engine did not expose before
        case 6: {  // pulley: end1's anchor and end2's anchor (local points) hang from two fixed world points; length1 + ratio * length2 stays constant
            b2PulleyJointDef jd;
            b2Vec2 a = b1->GetWorldPoint(b2Vec2(d.x1, d.y1));
            b2Vec2 b = b2b->GetWorldPoint(b2Vec2(d.x2, d.y2));
            if (std::isnan(d.groundX1) || std::isnan(d.groundY1)) { d.groundX1 = a.x; d.groundY1 = a.y - 3.0f; }   // default: 3 units above the anchor (+y is down)
            if (std::isnan(d.groundX2) || std::isnan(d.groundY2)) { d.groundX2 = b.x; d.groundY2 = b.y - 3.0f; }
            if (!(d.ratio > 0.001f)) d.ratio = 1.0f;
            jd.Initialize(b1, b2b, b2Vec2(d.groundX1, d.groundY1), b2Vec2(d.groundX2, d.groundY2), a, b, d.ratio);
            jd.collideConnected = d.collideConnected;
            joint = world_.CreateJoint(&jd);
            break;
        }
        case 7: {  // gear: couples two joints (revolute or prismatic) that already exist: coordinate1 + ratio * coordinate2 stays constant
            JointRecord* r1 = FindJoint(d.joint1);
            JointRecord* r2 = FindJoint(d.joint2);
            if (!r1 || !r2 || r1 == r2) return false;
            b2Joint* j1 = static_cast<b2Joint*>(r1->joint);
            b2Joint* j2 = static_cast<b2Joint*>(r2->joint);
            auto geared = [](b2Joint* j) { return j->GetType() == e_revoluteJoint || j->GetType() == e_prismaticJoint; };
            if (!geared(j1) || !geared(j2)) return false;
            b2GearJointDef jd;
            jd.bodyA = j1->GetBodyB();
            jd.bodyB = j2->GetBodyB();
            jd.joint1 = j1;
            jd.joint2 = j2;
            jd.ratio = d.ratio;
            jd.collideConnected = d.collideConnected;
            joint = world_.CreateJoint(&jd);
            gearJ1 = j1; gearJ2 = j2;
            d.end1 = r1->end2; d.end2 = r2->end2;        // the bodies the gear acts on
            break;
        }
        case 8: {  // mouse: a soft spring from end1 (at its centre) to the target point
            if (!mouseGround_) { b2BodyDef gd; gd.type = b2_staticBody; mouseGround_ = world_.CreateBody(&gd); }
            b2MouseJointDef jd;
            jd.bodyA = mouseGround_;
            jd.bodyB = b1;
            b2Vec2 p = b1->GetPosition();
            if (std::isnan(d.targetX)) d.targetX = p.x;
            if (std::isnan(d.targetY)) d.targetY = p.y;
            if (d.maxForce < 0) d.maxForce = 1000.0f * b1->GetMass();
            jd.target = p;                  // the joint grabs the body at its centre; the real target is set right after
            jd.maxForce = d.maxForce;
            jd.frequencyHz = d.frequency;
            jd.dampingRatio = d.dampingRatio;
            jd.collideConnected = d.collideConnected;
            joint = world_.CreateJoint(&jd);
            static_cast<b2MouseJoint*>(joint)->SetTarget(b2Vec2(d.targetX, d.targetY));
            d.x1 = p.x; d.y1 = p.y; d.x2 = d.targetX; d.y2 = d.targetY;
            b1->SetAwake(true);
            break;
        }
        case 9: {  // line (2.1): slide along an axis with an optional motor and limits. wheel (2.2): the same with a suspension spring and no limits
            float ax = d.worldAxisX, ay = d.worldAxisY;
            if (ax * ax + ay * ay < 1e-8f) { ax = 0; ay = 1; d.worldAxisX = 0; d.worldAxisY = 1; }
            b2Vec2 anchor = b1->GetWorldPoint(b2Vec2(d.x1, d.y1));
#ifdef AB_BOX2D_V22
            b2WheelJointDef jd;
            jd.Initialize(b1, b2b, anchor, b2Vec2(ax, ay));
            jd.enableMotor = d.motor;
            jd.motorSpeed = d.motorSpeed;
            jd.maxMotorTorque = d.maxTorque;
            jd.frequencyHz = d.frequency;
            jd.dampingRatio = d.dampingRatio;
#else
            b2LineJointDef jd;
            jd.Initialize(b1, b2b, anchor, b2Vec2(ax, ay));
            jd.enableLimit = d.limit;
            jd.lowerTranslation = d.lowerLimit;
            jd.upperTranslation = d.upperLimit;
            jd.enableMotor = d.motor;
            jd.motorSpeed = d.motorSpeed;
            jd.maxMotorForce = d.maxTorque;
#endif
            jd.collideConnected = d.collideConnected;
            joint = world_.CreateJoint(&jd);
            break;
        }
        case 10: {  // friction: top-down friction between the two bodies at the anchor (force and torque limits)
            b2FrictionJointDef jd;
            b2Vec2 anchor = b1->GetWorldPoint(b2Vec2(d.x1, d.y1));
            jd.Initialize(b1, b2b, anchor);
            jd.maxForce = d.maxForce;
            jd.maxTorque = d.maxTorque;
            jd.collideConnected = d.collideConnected;
            joint = world_.CreateJoint(&jd);
            break;
        }
        case 11: {  // rope: the two local anchors may not be further apart than maxLength (they can come closer)
            b2RopeJointDef jd;
            jd.bodyA = b1;
            jd.bodyB = b2b;
            jd.localAnchorA.Set(d.x1, d.y1);
            jd.localAnchorB.Set(d.x2, d.y2);
            if (std::isnan(d.maxLength)) d.maxLength = b2Distance(b1->GetWorldPoint(jd.localAnchorA), b2b->GetWorldPoint(jd.localAnchorB));
            jd.maxLength = d.maxLength;
            jd.collideConnected = d.collideConnected;
            joint = world_.CreateJoint(&jd);
            break;
        }
        default:
            return false;
        }
        if (!joint) return false;
        if (gearJ1) gearDeps_[joint] = std::make_pair(gearJ1, gearJ2);

        std::unique_ptr<JointRecord> r(new JointRecord());
        r->name = d.name; r->end1 = d.end1; r->end2 = d.end2;
        r->joint = joint;
        r->coordType = d.type == 8 ? 1 : d.coordType;
        r->backAndForth = d.backAndForth && d.motor && d.limit && (d.type == 3 || d.type == 4 || (d.type == 9 && !AB_WHEEL));
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
        } else if (joint->GetType() == e_gearJoint) {
            if (p.hasRatio) static_cast<b2GearJoint*>(joint)->SetRatio(p.ratio);
        } else if (joint->GetType() == e_mouseJoint) {
            b2MouseJoint* j = static_cast<b2MouseJoint*>(joint);
            if (p.hasTarget) {
                b2Vec2 t = j->GetTarget();
                if (!std::isnan(p.targetX)) t.x = p.targetX;
                if (!std::isnan(p.targetY)) t.y = p.targetY;
                j->SetTarget(t);
            }
            if (p.hasMaxForce) j->SetMaxForce(p.maxForce);
            if (p.hasFrequency) j->SetFrequency(p.frequency);
            if (p.hasDampingRatio) j->SetDampingRatio(p.dampingRatio);
        } else if (joint->GetType() == e_frictionJoint) {
            b2FrictionJoint* j = static_cast<b2FrictionJoint*>(joint);
            if (p.hasMaxForce) j->SetMaxForce(p.maxForce);
            if (p.hasMaxTorque) j->SetMaxTorque(p.maxTorque);
#ifdef AB_BOX2D_V22
        } else if (joint->GetType() == e_wheelJoint) {
            b2WheelJoint* j = static_cast<b2WheelJoint*>(joint);
            if (p.hasMotor) j->EnableMotor(p.motor);
            if (p.hasMotorSpeed) j->SetMotorSpeed(p.motorSpeed);
            if (p.hasMaxTorque) j->SetMaxMotorTorque(p.maxTorque);
            if (p.hasFrequency) j->SetSpringFrequencyHz(p.frequency);
            if (p.hasDampingRatio) j->SetSpringDampingRatio(p.dampingRatio);
#else
        } else if (joint->GetType() == e_lineJoint) {
            b2LineJoint* j = static_cast<b2LineJoint*>(joint);
            if (p.hasMotor) j->EnableMotor(p.motor);
            if (p.hasMotorSpeed) j->SetMotorSpeed(p.motorSpeed);
            if (p.hasMaxTorque) j->SetMaxMotorForce(p.maxTorque);
            if (p.hasLimit) j->EnableLimit(p.limit);
            if (p.hasLowerLimit) j->SetLimits(p.lowerLimit, j->GetUpperLimit());
            if (p.hasUpperLimit) j->SetLimits(j->GetLowerLimit(), p.upperLimit);
#endif
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
#ifndef AB_BOX2D_V22
        } else if (joint->GetType() == e_lineJoint) {      // the line joint (2.1) turns round at its limits like the prismatic one
            b2LineJoint* j = static_cast<b2LineJoint*>(joint);
            if (!(j->IsMotorEnabled() && j->IsLimitEnabled())) continue;
            float speed = j->GetMotorSpeed();
            float t = j->GetJointTranslation();
            if ((speed > 0 && t >= j->GetUpperLimit()) || (speed < 0 && t <= j->GetLowerLimit()))
                j->SetMotorSpeed(stop ? 0.0f : -speed);
#endif
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
    void SayGoodbye(b2Joint* joint) override {
        gearDeps_.erase(joint);
        gearsToKill_.erase(std::remove(gearsToKill_.begin(), gearsToKill_.end(), joint), gearsToKill_.end());
        KillDependentGears(joint, false);
        DropJointRecord(joint);
    }
    void SayGoodbye(b2Fixture*) override {}

protected:
    void DestroyBody(PhysObject* o) override { world_.DestroyBody(BodyOf(o)); ProcessGearKills(); }
    void DestroyBox2DJoint(void* joint) override {
        b2Joint* j = static_cast<b2Joint*>(joint);
        gearDeps_.erase(j);
        KillDependentGears(j, true);
        world_.DestroyJoint(j);
    }

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

    // fixtures for a concave (or very large) outline: convex pieces of at most b2_maxPolygonVertices points
    static void AddConcaveFixtures(b2Body* body, PhysObject* o, const std::vector<b2Vec2>& pts, const BodyDef& d) {
        std::vector<polydecomp::Pt> pp;
        for (const b2Vec2& v : pts) pp.push_back({v.x, v.y});
        std::vector<std::vector<polydecomp::Pt>> pieces;
        polydecomp::Decompose(pp, (size_t)b2_maxPolygonVertices, pieces);
        for (const auto& pc : pieces) {
            std::vector<b2Vec2> v;
            for (const polydecomp::Pt& q : pc) v.push_back(b2Vec2(q.x, q.y));
            if (v.size() < 3) continue;
            b2PolygonShape shape;
            shape.Set(v.data(), (int)v.size());
            MakeFixture(body, o, shape, d);
        }
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

    // A gear joint holds pointers to the two joints it couples, so it has to go before either of them does.
    // Explicit destruction takes the gears first; when a body (and with it a joint) is removed, the gears are queued and destroyed
    // before the next step.
    void KillDependentGears(b2Joint* j, bool now) {
        std::vector<b2Joint*> gears;
        for (auto& g : gearDeps_) if (g.second.first == j || g.second.second == j) gears.push_back(g.first);
        for (b2Joint* g : gears) {
            if (!now) { if (std::find(gearsToKill_.begin(), gearsToKill_.end(), g) == gearsToKill_.end()) gearsToKill_.push_back(g); continue; }
            gearDeps_.erase(g);
            DropJointRecord(g);
            KillDependentGears(g, true);
            world_.DestroyJoint(g);
        }
    }
    void ProcessGearKills() {
        std::vector<b2Joint*> list;
        list.swap(gearsToKill_);
        for (b2Joint* g : list) {
            if (!gearDeps_.count(g)) continue;       // already gone with its bodies
            gearDeps_.erase(g);
            DropJointRecord(g);
            world_.DestroyJoint(g);
        }
    }

    b2World world_;
    b2Body* mouseGround_ = nullptr;                                          // the fixed body mouse joints pull against (made on first use)
    std::map<b2Joint*, std::pair<b2Joint*, b2Joint*>> gearDeps_;            // gear joint -> the two joints it couples
    std::vector<b2Joint*> gearsToKill_;
};

}  // namespace

}  // namespace AB_B2_NAMESPACE

std::unique_ptr<PhysicsWorld> AB_MAKE_WORLD() { return std::unique_ptr<PhysicsWorld>(new AB_B2_NAMESPACE::Impl()); }

}  // namespace ab
