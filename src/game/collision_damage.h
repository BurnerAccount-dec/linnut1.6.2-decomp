// Collision damage rules, reconstructed from AngryBirds.exe 1.6.2
// (original: Game contact listener BeginContact, 0x0040b670, "game_handleCollisionDamage").
//
// This is the pure rules part. The caller (the Box2D contact listener) fills in two ContactObject
// values from the two fixtures' user data and body state, calls ResolveContact, then applies the
// result: write the new strengths into objects.world[name], add destroyed names to deadBlocks,
// add blockScoreAdd to scoreTable.blocks.score, call the Lua callbacks in order, disable the
// contact if asked, and apply the bird velocity change.
//
// The caller must already have done the checks the original does first:
//   * return if either of the two game flags at Game+0x1e9 / Game+0x1ea is set,
//   * return if either fixture has no user data,
//   * return unless both object names are keys of objects.world.
#pragma once
#include <map>
#include <string>
#include <vector>

namespace ab {

struct Vec2 {
    float x = 0, y = 0;
};

// blockTable.damageFactors[<name>]  (see blocks.lua, e.g. "YellowBirdDamageFactors").
// A material that is missing from a map leaves the multiplier at 1.
struct DamageFactorSet {
    std::map<std::string, float> damageMultiplier;
    std::map<std::string, float> velocityMultiplier;
};

// One side of a contact.
struct ContactObject {
    std::string name;                 // key in objects.world
    bool controllable = false;        // true for birds (byte at +0x87 in the original)
    bool hasStrength = false;         // objects.world[name].strength exists
    float strength = 0;
    bool hasDefence = false;          // objects.world[name].defence exists
    float defence = 0;
    std::string material;             // objects.world[name].material
    bool useLegacyCollisionPath = false;
    // For a bird: the set named by objects.world[bird].damageFactors, looked up in
    // blockTable.damageFactors. May be null (all multipliers 1).
    const DamageFactorSet* damageFactors = nullptr;
    float mass = 0;                   // body mass
    Vec2 velocity;                    // body linear velocity at the moment of contact
};

struct CollisionEvent {
    bool isBird = false;              // true: birdCollision(...), false: blockCollision(...)
    std::string object1, object2;
    float force = 0;
    float damage = 0;                 // birdCollision only
    bool wasDamageDone = false;       // blockCollision only
};

struct StrengthUpdate {
    std::string name;
    float strength = 0;               // new value for objects.world[name].strength
};

struct VelocityChange {
    std::string name;
    Vec2 velocity;
    bool immediate = false;           // true: call setVelocity now; false: apply after the step
};

struct ContactResult {
    std::vector<StrengthUpdate> strengthUpdates;
    std::vector<std::string> destroyed;     // add each to deadBlocks[name] = objects.world[name]
    float blockScoreAdd = 0;                // scoreTable.blocks.score += this
    bool disableContact = false;            // contact->SetEnabled(false) (bird goes through)
    bool hasVelocityChange = false;
    VelocityChange velocityChange;
    std::vector<CollisionEvent> events;     // Lua callbacks, in call order
};

ContactResult ResolveContact(const ContactObject& a, const ContactObject& b);

}  // namespace ab
