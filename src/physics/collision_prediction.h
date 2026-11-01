#pragma once

namespace ab {

// predictCollision: what would happen if two objects met, worked out from where they are and how they move, before anything touches
struct CollisionPrediction {
    bool valid = false;               // false: one of the objects does not exist (every other field is 0)
    bool collides = false;            // the contact filter lets them collide, and they are moving towards each other
    bool filtered = false;            // the contact filter keeps them apart (a sensor, a collision-less object, Space's masks)
    float distance = 0, gap = 0;      // between the centres / between the surfaces (approximate: circles by radius, other shapes by their boxes)
    float nx = 0, ny = 0;             // contact normal, from A to B
    float relVx = 0, relVy = 0;       // velocity of B seen from A
    float closingSpeed = 0;           // speed along the normal, positive when they approach
    float timeToContact = -1;         // seconds at the current speeds (-1: never)
    float impulse = 0;                // size of the normal impulse of the collision
    float restitution = 0;
    float massA = 0, massB = 0;
    float aVx = 0, aVy = 0, bVx = 0, bVy = 0;   // the velocities after the collision
    float energyLost = 0;             // kinetic energy (linear and spin) that the impact removes
    bool exactShapes = false;         // the contact point and normal come from the closest points of the real shapes (false: the line between the centres)
    float contactX = 0, contactY = 0; // the point where they would touch (halfway between the two surfaces)
    float normalSpeed = 0, tangentSpeed = 0;   // speed of B relative to A at that point, along the normal / along the tangent (nx, ny turned by 90 degrees: ny, -nx), before the hit
    float friction = 0;               // the combined friction coefficient, sqrt(frictionA * frictionB)
    float tangentImpulse = 0;         // the friction impulse along the tangent (clamped to friction * impulse)
    float aAngVel = 0, bAngVel = 0;   // angular velocities after the collision (radians per second)
};

}  // namespace ab
