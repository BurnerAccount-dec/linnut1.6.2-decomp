// Hand-calculated cases for the collision rules. Run: ctest, or build and run ab_tests.
#include <cmath>
#include <cstdio>

#include "game/collision_damage.h"

using namespace ab;

static int failures = 0;
static void Check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
}
static bool Near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

int main() {
    {   // block vs block: dp = (2*5 - 1*-3, 0) = 13 -> force 1.3
        ContactObject a, b;
        a.name = "a"; a.hasStrength = true; a.strength = 10; a.mass = 2; a.velocity = {5, 0};
        b.name = "b"; b.hasStrength = true; b.strength = 1;  b.mass = 1; b.velocity = {-3, 0};
        ContactResult r = ResolveContact(a, b);
        Check(r.events.size() == 1 && !r.events[0].isBird, "block event");
        Check(Near(r.events[0].force, 1.3f), "block force 1.3");
        Check(r.events[0].wasDamageDone, "block damage done");
        Check(r.strengthUpdates.size() == 2 && Near(r.strengthUpdates[0].strength, 8.7f), "a strength 8.7");
        Check(r.destroyed.size() == 1 && r.destroyed[0] == "b", "b destroyed");
        Check(Near(r.blockScoreAdd, 20.0f), "score 2.3 -> floor 2 -> 20");
    }
    {   // defence blocks a weak hit
        ContactObject a, b;
        a.name = "a"; a.hasStrength = true; a.strength = 10; a.hasDefence = true; a.defence = 5;
        a.mass = 1; a.velocity = {2, 0};
        b.name = "b"; b.mass = 1; b.velocity = {0, 0};
        ContactResult r = ResolveContact(a, b);
        Check(r.strengthUpdates.empty(), "defence: no strength change");
        Check(r.events[0].wasDamageDone, "b has no strength so flag stays true");
        Check(Near(r.blockScoreAdd, 0), "defence: no score");
    }
    DamageFactorSet yellow;
    yellow.damageMultiplier["wood"] = 4 * 0.6f;
    yellow.velocityMultiplier["wood"] = 1.0f;
    {   // bird vs block, new path: |v|=10, m=1.5, x2.4 -> damage 3.6
        ContactObject bird, block;
        bird.name = "bird"; bird.controllable = true; bird.mass = 1.5f; bird.velocity = {8, 6};
        bird.damageFactors = &yellow;
        block.name = "plank"; block.material = "wood"; block.hasStrength = true; block.strength = 2;
        block.hasDefence = true; block.defence = 0.5f;
        ContactResult r = ResolveContact(bird, block);
        Check(Near(r.events[0].force, 3.6f), "bird damage 3.6");
        Check(Near(r.events[0].damage, 2.0f), "dealt = strength 2");
        Check(r.disableContact, "contact disabled");
        Check(r.hasVelocityChange && !r.velocityChange.immediate, "deferred velocity");
        Check(Near(r.velocityChange.velocity.x, 8 * (1.6f / 3.6f)), "vx ratio 0.444");
        Check(r.events[0].object1 == "bird" && r.events[0].object2 == "plank", "argument order");
    }
    {   // same, legacy path: ratio clamps to 1
        ContactObject bird, block;
        bird.name = "bird"; bird.controllable = true; bird.mass = 1.5f; bird.velocity = {8, 6};
        bird.damageFactors = &yellow; bird.useLegacyCollisionPath = true;
        block.name = "plank"; block.material = "wood"; block.hasStrength = true; block.strength = 2;
        block.hasDefence = true; block.defence = 0.5f;
        ContactResult r = ResolveContact(block, bird);   // order swapped on purpose
        Check(r.velocityChange.immediate, "legacy: immediate velocity");
        Check(Near(r.velocityChange.velocity.x, 8.0f), "legacy ratio clamped to 1");
        Check(r.events[0].object1 == "bird", "bird is first even when fixture B");
    }
    {   // bird that does not break the block
        ContactObject bird, block;
        bird.name = "bird"; bird.controllable = true; bird.mass = 1; bird.velocity = {10, 0};
        block.name = "stone"; block.material = "rock"; block.hasStrength = true; block.strength = 5;
        ContactResult r = ResolveContact(bird, block);   // damage 1.0, remaining 4
        Check(!r.disableContact && !r.hasVelocityChange && r.destroyed.empty(), "block survives");
        Check(Near(r.strengthUpdates[0].strength, 4.0f), "strength 4");
        Check(Near(r.events[0].damage, 1.0f), "dealt 1");
    }
    {   // bird vs bird: faster one decides
        ContactObject a, b;
        a.name = "a"; a.controllable = true; a.mass = 1; a.velocity = {5, 0};
        b.name = "b"; b.controllable = true; b.mass = 2; b.velocity = {0, 10};
        ContactResult r = ResolveContact(a, b);
        Check(r.events[0].isBird && Near(r.events[0].force, 2.0f) && r.events[0].damage == 0, "bird-bird");
    }
    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
