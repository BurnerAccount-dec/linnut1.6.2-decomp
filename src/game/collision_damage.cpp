#include "collision_damage.h"

#include <cmath>

namespace ab {
namespace {

float Length(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

float Lookup(const std::map<std::string, float>& m, const std::string& key, float fallback) {
    auto it = m.find(key);
    return it == m.end() ? fallback : it->second;
}

// What one block takes from a force. Returns the damage it absorbed.
// absorbed is cleared when the block's defence is higher than the force.
float HitBlock(const ContactObject& o, float force, bool& notBlockedByDefence, ContactResult& r) {
    notBlockedByDefence = true;
    if (!o.hasStrength) return 0;
    float defence = o.hasDefence ? o.defence : 0.0f;
    if (o.hasDefence && force < defence) {
        notBlockedByDefence = false;
        return 0;
    }
    float excess = force - defence;
    float remaining = o.strength - excess;
    r.strengthUpdates.push_back({o.name, remaining});
    if (remaining > 0) return excess;
    r.destroyed.push_back(o.name);
    return o.strength;
}

}  // namespace

ContactResult ResolveContact(const ContactObject& a, const ContactObject& b) {
    ContactResult r;

    if (!a.controllable && !b.controllable) {
        // block against block: force from the difference of the two momenta
        Vec2 dp = {a.mass * a.velocity.x - b.mass * b.velocity.x,
                   a.mass * a.velocity.y - b.mass * b.velocity.y};
        float force = Length(dp) * 0.1f;

        // Rio (0x40ca60): the damage a block takes is scaled by the other block's damageFactors,
        // looked up by the material of the block that is hit (Lua tables with no entry scale by 1).
        // This is what keeps the Mauro boss unharmed by the platforms he jumps onto:
        // DefaultDamageFactors.damageMultiplier.monkeyBoss is 0.
        float forceA = force, forceB = force;
        if (b.damageFactors) forceA *= Lookup(b.damageFactors->damageMultiplier, a.material, 1.0f);
        if (a.damageFactors) forceB *= Lookup(a.damageFactors->damageMultiplier, b.material, 1.0f);

        bool okA, okB;
        float total = HitBlock(a, forceA, okA, r);
        total += HitBlock(b, forceB, okB, r);

        CollisionEvent e;
        e.isBird = false;
        e.object1 = a.name;
        e.object2 = b.name;
        e.force = force;
        e.wasDamageDone = okA || okB;
        r.events.push_back(e);

        if (total > 0) r.blockScoreAdd = std::floor(total) * 10.0f;
        return r;
    }

    if (a.controllable && b.controllable) {
        // bird against bird: the faster bird decides the force
        const ContactObject& fast =
            (a.velocity.x * a.velocity.x + a.velocity.y * a.velocity.y <=
             b.velocity.x * b.velocity.x + b.velocity.y * b.velocity.y) ? b : a;
        CollisionEvent e;
        e.isBird = true;
        e.object1 = a.name;
        e.object2 = b.name;
        e.force = Length(fast.velocity) * fast.mass / 10.0f;
        e.damage = 0;
        r.events.push_back(e);
        return r;
    }

    // bird against block
    const ContactObject& bird = a.controllable ? a : b;
    const ContactObject& block = a.controllable ? b : a;

    float damageMultiplier = 1.0f;
    float velocityMultiplier = 1.0f;
    if (bird.damageFactors) {
        damageMultiplier = Lookup(bird.damageFactors->damageMultiplier, block.material, 1.0f);
        velocityMultiplier = Lookup(bird.damageFactors->velocityMultiplier, block.material, 1.0f);
    }

    float impact = Length(bird.velocity) * bird.mass / 10.0f;
    float damage = impact * damageMultiplier;
    float dealt = 0;

    if (block.hasStrength) {
        float defence = block.hasDefence ? block.defence : 0.0f;
        float excess = damage - defence;
        dealt = excess;  // the original passes this on even when it is zero or negative
        if (excess > 0) {
            float strength = block.strength;
            float remaining = strength - excess;
            r.strengthUpdates.push_back({block.name, remaining});
            if (remaining <= 0) {
                r.destroyed.push_back(block.name);
                r.hasVelocityChange = true;
                r.velocityChange.name = bird.name;
                if (!bird.useLegacyCollisionPath) {
                    float ratio = ((damage - strength) / damage) * velocityMultiplier;
                    if (ratio > 1.0f) ratio = 1.0f;
                    r.velocityChange.velocity = {bird.velocity.x * ratio, bird.velocity.y * ratio};
                    r.velocityChange.immediate = false;
                } else {
                    float ratio = ((-remaining / bird.mass) / damage) * 10.0f * 1.75f;
                    if (ratio > 1.0f) ratio = 1.0f;
                    r.velocityChange.velocity = {bird.velocity.x * ratio, bird.velocity.y * ratio};
                    r.velocityChange.immediate = true;
                }
                r.disableContact = true;
                dealt = strength;
            }
        }
    }

    CollisionEvent e;
    e.isBird = true;
    e.object1 = bird.name;
    e.object2 = block.name;
    e.force = damage;
    e.damage = std::floor(dealt);
    r.events.push_back(e);
    return r;
}

}  // namespace ab
