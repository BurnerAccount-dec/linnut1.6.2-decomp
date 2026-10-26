// Checks for the Box2D-backed world: creation parameters, the exe's constants, filter, joints,
// and a run through the real frame update. Every check runs against Box2D 2.1.1 and against 2.2.0.
#include "physics/physics_world.h"
#include <memory>

#include <cmath>
#include <cstdio>
#include <string>

using namespace ab;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
static bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }

static BodyDef Def(const char* name, float x, float y, float w, float h, float density) {
    BodyDef d;
    d.name = name; d.sprite = "s";
    d.x = x; d.y = y; d.w = w; d.h = h;
    d.density = density; d.friction = 0.5f; d.restitution = 0.0f;
    return d;
}

// Runs the real UpdateFrame; all Lua and audio parts are no-ops.
struct Host : FrameHost {
    PhysicsWorld& w;
    explicit Host(PhysicsWorld& world) : w(world) {}
    void StepWorld(float dt, int v, int p) override { w.Step(dt, v, p); }
    void ClearForces() override { w.ClearForces(); }
    void CallRemoveBlocks() override {}
    void SetObjectNumber(int, const char*, float) override {}
    void SetObjectBool(int, const char*, bool) override {}
    void SetGameBool(const char*, bool) override {}
    void SetJointNumber(int, const char*, float) override {}
    void MarkBlockDead(int id) override { dead = id; }
    bool LoopActive(LoopSound) override { return false; }
    void StartLoop(LoopSound, float) override {}
    void SetLoopVolume(LoopSound, float) override {}
    void StopLoop(LoopSound) override {}
    void CallUpdate(float, float) override {}
    void UpdateEffects(float) override {}
    int dead = -1;
};

static void Run(PhysicsWorld& w, Host& h, float seconds) {
    w.SetRunning(true);
    int frames = (int)(seconds * 60);
    for (int i = 0; i < frames; i++) UpdateFrame(w.Frame(), h, 1.0f / 60.0f);
}

static void RunAll(Box2DVersion version) {
    std::printf("Box2D %s\n", version == Box2DVersion::V22 ? "2.2.0" : "2.1.1");
    // ---- creation: masses and damping
    {
        std::unique_ptr<PhysicsWorld> wp = MakePhysicsWorld(version);
        PhysicsWorld& w = *wp;
        CHECK(w.Version() == version);
        { float gx, gy; w.GetGravity(gx, gy); CHECK(Near(gy, 20.0f) && Near(gx, 0.0f)); }
        CHECK(Near(w.CreateBox(Def("box", 0, 0, 2, 1, 3)), 6.0f));      // 2 x 1 x density 3
        BodyDef c = Def("ball", 5, 0, 1, 0, 1);                          // radius 1
        c.controllable = true;
        CHECK(Near(w.CreateCircle(c), 3.14159265f, 1e-3f));
        CHECK(Near(w.CreateBox(Def("ground", 0, 10, 20, 1, 0)), 0.0f)); // density 0: static, no mass

        PhysObject* box = w.Find("box");
        PhysObject* ball = w.Find("ball");
        PhysObject* ground = w.Find("ground");
        CHECK(box && ball && ground);
        CHECK(!w.Inspect("box").isStatic);
        CHECK(w.Inspect("ground").isStatic);
        CHECK(Near(w.Inspect("box").angularDamping, 1.0f));
        CHECK(Near(w.Inspect("ball").angularDamping, 2.0f));            // controllable circle
        CHECK(Near(w.Inspect("box").linearDamping, 0.0f));
        CHECK(Near(box->rollFactor, -1.0f));
        CHECK(Near(ball->rollFactor, 1.0f));                            // circles: the radius
        CHECK(w.Frame().birds.size() == 1 && w.Frame().birds[0] == ball);
        CHECK(w.Frame().objects.size() == 3);
        CHECK(Near(ball->interpX, 5.0f) && Near(ball->interpAngle, 0.0f));
        // polygon radius of box shapes: the classic exe's 0.1 (Box2D 2.1.1); Angry Birds Rio's 2.2.0 uses one linearSlop (0.05), see b2Settings.cpp
        CHECK(Near(w.Inspect("box").shapeRadius, version == Box2DVersion::V22 ? 0.05f : 0.1f, 1e-6f));
    }

    // ---- polygon, with the angular damping rule
    {
        std::unique_ptr<PhysicsWorld> wp = MakePhysicsWorld(version);
        PhysicsWorld& w = *wp;
        CHECK(w.Version() == version);
        BodyDef d = Def("tri", 0, 0, 0, 0, 2);
        d.controllable = true;
        std::vector<float> v = {0, 0, 2, 0, 0, 2};      // right triangle, area 2
        CHECK(Near(w.CreatePolygon(d, v), 4.0f));
        CHECK(Near(w.Inspect("tri").angularDamping, 2.0f));
        CHECK(w.Frame().birds.empty());                  // only circles join the bird list
    }

    // ---- a ball falls, lands, goes to sleep; through the real frame update
    {
        std::unique_ptr<PhysicsWorld> wp = MakePhysicsWorld(version);
        PhysicsWorld& w = *wp;
        CHECK(w.Version() == version);
        w.CreateBox(Def("ground", 0, 10, 20, 1, 0));      // top at y = 9.5
        w.CreateCircle(Def("ball", 0, 5, 0.5f, 0, 1));
        Host h(w);
        Run(w, h, 0.5f);                                  // free fall: v = g t, sampled mid-air
        PhysObject* ball = w.Find("ball");
        CHECK(Near(w.Inspect("ball").velY, 20.0f * 0.5f, 0.6f));
        CHECK(w.Inspect("ball").posY > 5.0f && w.Inspect("ball").posY < 8.5f);
        Run(w, h, 4.0f);
        float y = w.Inspect("ball").posY;
        CHECK(y > 8.85f && y < 9.15f);                    // radius 0.5 above the ground top at 9.5
        CHECK(!w.Inspect("ball").awake);                      // slept (0.25 s of stillness)
        CHECK(Near(ball->interpY, y, 0.05f));
        CHECK(!w.Frame().objects.empty());
    }

    // ---- filter: collision = false lets objects pass
    {
        std::unique_ptr<PhysicsWorld> wp = MakePhysicsWorld(version);
        PhysicsWorld& w = *wp;
        CHECK(w.Version() == version);
        BodyDef g = Def("ghost", 0, 10, 20, 1, 0);
        g.collision = false;
        w.CreateBox(g);
        w.CreateCircle(Def("ball", 0, 5, 0.5f, 0, 1));
        Host h(w);
        Run(w, h, 2.0f);
        CHECK(w.Inspect("ball").posY > 15.0f);   // fell through
    }

    // ---- filter: z_order 999 circle against an "immovable" material
    {
        std::unique_ptr<PhysicsWorld> wp = MakePhysicsWorld(version);
        PhysicsWorld& w = *wp;
        CHECK(w.Version() == version);
        w.CreateBox(Def("wall", 0, 10, 20, 1, 0));
        BodyDef b = Def("bird", 0, 5, 0.5f, 0, 1);
        b.zOrder = 999;
        w.CreateCircle(b);
        w.materialOf = [](const PhysObject& o) { return o.name == "wall" ? std::string("immovable") : std::string("wood"); };
        Host h(w);
        Run(w, h, 2.0f);
        CHECK(w.Inspect("bird").posY > 15.0f);
    }

    // ---- contact hook, and ResolveContact input
    {
        std::unique_ptr<PhysicsWorld> wp = MakePhysicsWorld(version);
        PhysicsWorld& w = *wp;
        CHECK(w.Version() == version);
        w.CreateBox(Def("ground", 0, 10, 20, 1, 0));
        BodyDef b = Def("bird", 0, 8, 0.5f, 0, 2);
        b.controllable = true;
        w.CreateCircle(b);
        int calls = 0;
        std::string n1, n2;
        w.onBeginContact = [&](PhysObject& a, PhysObject& c, ContactHandle&) {
            if (calls++ == 0) { n1 = a.name; n2 = c.name; }
        };
        Host h(w);
        Run(w, h, 1.0f);
        CHECK(calls >= 1);
        CHECK((n1 == "bird" && n2 == "ground") || (n1 == "ground" && n2 == "bird"));
        ContactObject co = PhysicsWorld::MakeContactObject(*w.Find("bird"));
        CHECK(co.name == "bird" && co.controllable && Near(co.mass, 2.0f * 3.14159265f * 0.25f, 1e-3f));

        // deferred velocity change is applied after the next step by the frame update
        VelocityChange vc;
        vc.name = "bird"; vc.velocity.x = 3; vc.velocity.y = -4; vc.immediate = false;
        w.ApplyVelocityChange(vc);
        CHECK(w.Find("bird")->pendingVelocity);
        UpdateFrame(w.Frame(), h, 0.05f);
        CHECK(!w.Find("bird")->pendingVelocity);
        CHECK(Near(w.Inspect("bird").velX, 3.0f, 0.5f));
    }

    // ---- weld between bodies the scripts rotated past pi (setRotation keeps angles in [0, 2*pi)): the joint must start out
    // satisfied. Box2D 2.2.0's GetAngle() wraps to (-pi, pi], which once gave the weld a reference angle 2*pi out.
    {
        std::unique_ptr<PhysicsWorld> wp = MakePhysicsWorld(version);
        PhysicsWorld& w = *wp;
        w.CreateBox(Def("p", 10, 0, 2, 1, 1));
        w.CreateBox(Def("q", 14, 0, 2, 1, 1));
        float n;
        w.SetRotation("p", 4.37f, n);
        w.SetRotation("q", 2.0f, n);
        JointDef weld;
        weld.name = "w"; weld.end1 = "p"; weld.end2 = "q"; weld.type = 2; weld.coordType = 2;
        CHECK(w.CreateJoint(weld));
        BodyInfo p0 = w.Inspect("p"), q0 = w.Inspect("q");
        w.Step(1.0f / 30.0f, 10, 10);                // one step: a 2*pi error would already have turned the bodies by now
        BodyInfo p1 = w.Inspect("p"), q1 = w.Inspect("q");
        float d0 = std::hypot(q0.posX - p0.posX, q0.posY - p0.posY), d1 = std::hypot(q1.posX - p1.posX, q1.posY - p1.posY);
        CHECK(std::isfinite(d1) && Near(d0, d1, 0.05f));
        CHECK(Near(std::sin(p1.angle), std::sin(p0.angle), 0.05f) && Near(std::sin(q1.angle), std::sin(q0.angle), 0.05f));
    }

    // ---- joints
    {
        std::unique_ptr<PhysicsWorld> wp = MakePhysicsWorld(version);
        PhysicsWorld& w = *wp;
        CHECK(w.Version() == version);
        w.CreateBox(Def("anchor", 0, 0, 1, 1, 0));
        w.CreateBox(Def("arm", 3, 0, 2, 0.5f, 1));
        w.CreateBox(Def("other", 6, 0, 1, 1, 1));

        JointDef rev;
        rev.name = "hinge"; rev.end1 = "anchor"; rev.end2 = "arm"; rev.type = 3; rev.coordType = 1;
        rev.x1 = 0; rev.y1 = 0;                      // local point on end1
        rev.motor = true; rev.motorSpeed = 1.0f; rev.maxTorque = 10000;
        rev.limit = true; rev.lowerLimit = 0; rev.upperLimit = 3.14159265f;
        CHECK(w.CreateJoint(rev));

        JointDef dist;
        dist.name = "rope"; dist.end1 = "arm"; dist.end2 = "other"; dist.type = 1; dist.coordType = 0;
        dist.frequency = 4; dist.dampingRatio = 0.5f;
        CHECK(w.CreateJoint(dist));

        JointDef local;
        local.name = "loc"; local.end1 = "arm"; local.end2 = "other"; local.type = 1; local.coordType = 2;
        CHECK(w.CreateJoint(local));

        JointDef missing = rev;
        missing.name = "bad"; missing.end2 = "nothing";
        CHECK(!w.CreateJoint(missing));

        CHECK(w.Frame().joints.size() == 3);
        CHECK(w.Frame().joints[0]->PublishesEndpoints());
        CHECK(w.Frame().joints[1]->PublishesEndpoints());
        CHECK(!w.Frame().joints[2]->PublishesEndpoints());   // coordType 2: no endpoints written
        float x1, y1, x2, y2;
        w.Frame().joints[1]->Anchors(x1, y1, x2, y2);
        CHECK(Near(x1, 3.0f) && Near(x2, 6.0f));

        // checkJointLimits: angle 0 is at the lower limit; motor speed is positive, so nothing flips.
        w.CheckJointLimits("hinge");
        // push the lower limit above the current angle and use a negative speed: it must flip back
        JointParams p;
        p.name = "hinge"; p.hasMotorSpeed = true; p.motorSpeed = -2.0f;
        w.SetJointParameters(p);
        w.CheckJointLimits("hinge");            // angle 0 <= lower 0 and speed < 0: flips to +2
        float speed, freq, damping;
        CHECK(w.InspectJoint("hinge", speed, freq, damping) && Near(speed, 2.0f));

        p = JointParams();
        p.name = "rope"; p.hasFrequency = true; p.frequency = 9; p.hasDampingRatio = true; p.dampingRatio = 0.25f;
        w.SetJointParameters(p);
        int changed = 0;
        for (const char* n : {"rope", "loc"})
            if (w.InspectJoint(n, speed, freq, damping) && Near(freq, 9.0f) && Near(damping, 0.25f)) changed++;
        CHECK(changed == 1);                    // only "rope", not "loc"

        w.DestroyJoint("rope");
        CHECK(w.Frame().joints.size() == 2);
    }

    // ---- links (type 5) and removeObject
    {
        std::unique_ptr<PhysicsWorld> wp = MakePhysicsWorld(version);
        PhysicsWorld& w = *wp;
        CHECK(w.Version() == version);
        w.CreateBox(Def("a", 0, 0, 1, 1, 1));
        w.CreateBox(Def("b", 2, 0, 1, 1, 1));
        JointDef link;
        link.name = "chain"; link.end1 = "a"; link.end2 = "b"; link.type = 5; link.destroyTimer = 0.5f;
        CHECK(w.CreateJoint(link));
        CHECK(w.Frame().joints.empty());                  // no Box2D joint, nothing published
        std::vector<std::string> fired = w.RemoveObject("a");
        CHECK(fired.size() == 1 && fired[0] == "chain");
        CHECK(!w.Find("a") && w.Find("b"));
        CHECK(w.Frame().objects.size() == 1);
        CHECK(w.Find("b")->destroyPending && Near(w.Find("b")->destroyTimer, 0.5f));

        // the frame update runs the timer and reports the block dead
        Host h(w);
        Run(w, h, 0.4f);
        CHECK(h.dead < 0);
        Run(w, h, 0.4f);
        CHECK(h.dead >= 0);
        CHECK(!w.Find("b")->destroyPending);
    }

    // ---- setPosition / setRotation / velocities / sleeping
    {
        std::unique_ptr<PhysicsWorld> wp = MakePhysicsWorld(version);
        PhysicsWorld& w = *wp;
        CHECK(w.Version() == version);
        w.CreateBox(Def("b", 1, 2, 1, 1, 1));
        CHECK(w.SetPosition("b", 4, 5));
        PhysObject* o = w.Find("b");
        CHECK(Near(w.Inspect("b").posX, 4) && Near(o->interpX, 4) && Near(o->state[0][1], 5));
        float a;
        CHECK(w.SetRotation("b", -1.5707964f, a));
        CHECK(Near(a, 4.712389f, 1e-4f) && Near(w.GetAngle("b"), 4.712389f, 1e-4f));
        CHECK(Near(w.Inspect("b").angle, 4.712389f, 1e-4f) || Near(w.Inspect("b").angle, -1.5707964f, 1e-4f));   // 2.2.0 reports the angle in (-pi, pi]
        w.SetVelocity("b", 1, 2);
        w.SetAngularVelocity("b", 3);
        CHECK(Near(w.Inspect("b").velX, 1) && Near(w.Inspect("b").angularVelocity, 3));
        w.SetSleeping("b", true);
        CHECK(!w.Inspect("b").awake && Near(w.Inspect("b").velX, 0) && Near(w.Inspect("b").angularVelocity, 0));
        w.SetSleeping("b", false);
        CHECK(w.Inspect("b").awake);
        float wx, wy;
        w.SetRotation("b", 0, a);
        w.GetWorldPoint("b", 1, 0, wx, wy);
        CHECK(Near(wx, 5) && Near(wy, 5));
        CHECK(!w.SetPosition("nobody", 0, 0));

        // static bodies ignore velocity
        w.CreateBox(Def("s", 0, 0, 1, 1, 0));
        w.SetVelocity("s", 5, 5);
        CHECK(Near(w.Inspect("s").velX, 0));
    }

    // ---- setMaterial / setObjectParameter / setSprite / setTexture
    {
        std::unique_ptr<PhysicsWorld> wp = MakePhysicsWorld(version);
        PhysicsWorld& w = *wp;
        CHECK(w.Version() == version);
        w.CreateBox(Def("m", 1, 2, 1, 1, 1));
        PhysObject* o = w.Find("m");
        CHECK(o->rollMaterial == kRollNone);
        w.SetMaterial("m", "woodHeavy");   w.SetMaterial("m", "unknown");
        CHECK(o->rollMaterial == kRollWood);       // an unknown name leaves it as it was
        w.SetMaterial("m", "rock");   CHECK(o->rollMaterial == kRollRock);
        w.SetMaterial("m", "light");  CHECK(o->rollMaterial == kRollLight);
        w.SetObjectParameter("m", 1, 1);   CHECK(o->drawWithBirds);
        w.SetObjectParameter("m", 1, 0);   CHECK(!o->drawWithBirds);
        w.SetObjectParameter("m", 2, 0);   CHECK(w.Inspect("m").isStatic);
        w.SetObjectParameter("m", 2, 1);   CHECK(!w.Inspect("m").isStatic);
        w.SetSpriteName("m", "BLOCK_WOOD_1_6"); w.SetTextureName("m", "tex");
        CHECK(o->sprite == "BLOCK_WOOD_1_6" && o->texture == "tex");
        w.SetObjectParameter("nobody", 2, 0);  // no such object: nothing happens
    }

    // ---- collider types: the default rules, a ghost falling through a static floor, and the rule changed from a script
    {
        std::unique_ptr<PhysicsWorld> wp = MakePhysicsWorld(version);
        PhysicsWorld& w = *wp;
        CHECK(w.ColliderRule(0, 0) && w.ColliderRule(3, 4) && w.ColliderRule(5, 3) && w.ColliderRule(5, 1));
        CHECK(!w.ColliderRule(5, 0) && !w.ColliderRule(2, 5) && !w.ColliderRule(5, 5));
        w.CreateBox(Def("floor", 0, 10, 20, 1, 0));
        w.CreateBox(Def("ghost", 0, 0, 1, 1, 1));
        w.CreateBox(Def("plain", 5, 0, 1, 1, 1));
        w.SetColliderType("ghost", 5);  w.SetColliderType("plain", 0);  w.SetColliderType("floor", 2);
        CHECK(w.ColliderType("ghost") == 5 && w.ColliderType("plain") == 0 && w.ColliderType("nobody") == -1);
        Host h(w);
        Run(w, h, 3.0f);
        CHECK(w.Inspect("ghost").posY > 12.0f);          // fell through the floor
        CHECK(w.Inspect("plain").posY < 10.0f);          // stopped on it
        w.SetColliderRule(5, 2, true);                    // scripts can change a pair
        CHECK(w.ColliderRule(2, 5) && w.ColliderRule(5, 2));
        w.SetColliderRule(99, 2, false);                  // out of range: nothing happens
    }

    // ---- Box2D 2.2.0 skin: blocks that are placed flush (as the Angry Birds Rio levels place them) stay where they are
    {
        std::unique_ptr<PhysicsWorld> wp = MakePhysicsWorld(version);
        PhysicsWorld& w = *wp;
        w.CreateBox(Def("floor", 0, 10, 20, 1, 0));
        w.CreateBox(Def("lower", 0, 9.0f - 0.94f, 0.92f, 1.886f, 0.75f));    // standing on the floor, touching it
        w.CreateBox(Def("upper", 0, 9.0f - 0.94f - 1.886f - 0.005f, 0.92f, 1.886f, 0.75f));
        Host h(w);
        Run(w, h, 2.0f);
        CHECK(Near(w.Inspect("upper").posX, 0.0f, 0.05f) && std::fabs(w.Inspect("upper").angle) < 0.05f);
        CHECK(Near(w.Inspect("lower").posX, 0.0f, 0.05f));
        if (version == Box2DVersion::V22) {
            w.SetPolygonRadius(0.07f);  CHECK(Near(w.PolygonRadius(), 0.07f));
            w.SetPolygonRadius(0.05f);  CHECK(Near(w.PolygonRadius(), 0.05f));
        }
    }

}

int main() {
    RunAll(Box2DVersion::V21);
    RunAll(Box2DVersion::V22);
    if (failures == 0) std::printf("physics_world: all passed\n");
    return failures == 0 ? 0 : 1;
}
