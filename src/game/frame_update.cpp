#include "game/frame_update.h"

#include <cmath>

namespace ab {

namespace {

const float kPi = 3.1415927f;
const float kTwoPi = 6.2831855f;

float Angle(const Body& b) { return std::atan2(b.RotCol1Y(), b.RotCol1X()); }

void StopOrAdjust(FrameHost& host, LoopSound which, float volume) {
    if (volume <= 0.0f) {
        host.StopLoop(which);
    } else if (host.LoopActive(which)) {
        host.SetLoopVolume(which, volume);
    } else {
        host.StartLoop(which, volume);
    }
}

}  // namespace

float StepZoom(float zoomLevel, float current, float target) {
    if (target != current) zoomLevel = (current - target) * 0.5f + zoomLevel;
    return zoomLevel;
}

void UpdateFrame(FrameState& s, FrameHost& host, float dt) {
    float woodVolume = 0, rockVolume = 0, lightVolume = 0;

    if (s.running) {
        // 1. fixed steps
        s.accumulator += dt;
        while (s.accumulator > kFixedStep) {
            host.StepWorld(kFixedStep, 10, 10);
            s.accumulator -= kFixedStep;
            host.CallRemoveBlocks();

            for (size_t i = 0; i < s.birds.size(); i++) {
                FrameObject* o = s.birds[i];
                if (!o->pendingVelocity) continue;
                host.LogBirdVelocity(o->id, o->pendingVx, o->pendingVy);
                if (!o->body->IsStatic()) o->body->SetLinearVelocity(o->pendingVx, o->pendingVy);
                o->pendingVelocity = false;
            }

            if (s.accumulator <= kSnapshotWindow) {
                s.bufferIndex = (s.bufferIndex == 0) ? 1 : 0;
                for (size_t i = 0; i < s.objects.size(); i++) {
                    FrameObject* o = s.objects[i];
                    if (!o->body->IsAwake()) continue;
                    float* st = o->state[s.bufferIndex];
                    st[0] = o->body->PosX();
                    st[1] = o->body->PosY();
                    st[2] = Angle(*o->body);
                    if (o->isBird && o->body->Mass() != 0.0f) o->body->SetBullet(true);
                }
            }
        }

        // 2. interpolation factor
        host.ClearForces();
        float alpha = s.accumulator / kFixedStep;
        float oneMinusAlpha = 1.0f - alpha;
        int cur = s.bufferIndex;
        int prev = (s.bufferIndex == 0) ? 1 : 0;

        // 3. interpolate, and run the destroy timers
        for (size_t i = 0; i < s.objects.size(); i++) {
            FrameObject* o = s.objects[i];
            if (o->body->IsAwake()) {
                o->interpX = o->state[cur][0] * alpha + o->state[prev][0] * oneMinusAlpha;
                o->interpY = o->state[cur][1] * alpha + o->state[prev][1] * oneMinusAlpha;

                float a1 = o->state[cur][2];
                float a0 = o->state[prev][2];
                float diff = a1 - a0;
                if (diff < 0.0f) diff = -diff;
                if (diff >= kPi) {
                    // the angle wrapped between the two snapshots: move the old one by a full turn
                    a0 = (a1 <= a0) ? a0 - kTwoPi : a0 + kTwoPi;
                }
                o->interpAngle = alpha * a1 + a0 * oneMinusAlpha;
            }
            if (o->destroyPending) {
                o->destroyTimer -= dt;
                if (o->destroyTimer <= 0.0f) {
                    host.MarkBlockDead(o->id);
                    o->destroyPending = false;
                }
            }
        }

        // 4. publish to Lua
        bool hasMoving = false, hasAwake = false, hasMovingAboveTolerance = false;
        if (!s.objects.empty()) {
            for (size_t i = 0; i < s.objects.size(); i++) {
                FrameObject* o = s.objects[i];
                Body& b = *o->body;
                if (o->wasAwake || b.IsAwake()) {
                    hasAwake = true;
                    host.SetObjectNumber(o->id, "x", o->interpX);
                    host.SetObjectNumber(o->id, "y", o->interpY);

                    if (b.Mass() > 0.0f) {
                        // anything that leaves the level is frozen
                        if (o->interpY > 20.0f) host.SetObjectBool(o->id, "frozen", true);
                        if (o->interpX < (float)s.levelMinX) host.SetObjectBool(o->id, "frozen", true);
                        if ((float)s.levelMaxX < o->interpX) host.SetObjectBool(o->id, "frozen", true);
                    }

                    float vx = b.VelX(), vy = b.VelY();
                    float speedSq = vy * vy + vx * vx;
                    if (speedSq > 0.0005f && !o->isBird && !b.HasJoints()) hasMovingAboveTolerance = true;
                    if (speedSq > 9.0f || (b.AngularVelocity() > 0.1f && !b.HasJoints())) hasMoving = true;

                    bool done = false;
                    if (o->isBird) {
                        host.SetObjectNumber(o->id, "xVel", vx);
                        host.SetObjectNumber(o->id, "yVel", vy);
                        host.SetObjectNumber(o->id, "angle", o->interpAngle);
                        done = true;
                    }
                    if (!done && o->rolls) {
                        float w = b.AngularVelocity();
                        if (w < 0.0f) w = -w;
                        float volume = ((o->rollFactor * w) / 400.0f) * b.Mass();
                        if (volume > 1.0f) volume = 1.0f;
                        if (o->rollMaterial == kRollWood && woodVolume < volume) woodVolume = volume;
                        if (o->rollMaterial == kRollRock && rockVolume < volume) rockVolume = volume;
                        if (o->rollMaterial == kRollLight && lightVolume < volume) lightVolume = volume;
                    }
                }
                // runs for every object, awake or not
                o->wasAwake = b.IsAwake();
                host.SetObjectNumber(o->id, "angle", Angle(b));
                host.SetObjectBool(o->id, "sleeping", !b.IsAwake());
            }
        }

        // 5. flags
        host.SetGameBool("hasMovingObjects", hasMoving);
        host.SetGameBool("hasAwakeObjects", hasAwake);
        host.SetGameBool("hasMovingObjectsAboveTolerance", hasMovingAboveTolerance);

        // 6. joints
        for (size_t i = 0; i < s.joints.size(); i++) {
            if (!s.joints[i]->PublishesEndpoints()) continue;
            float x1, y1, x2, y2;
            s.joints[i]->Anchors(x1, y1, x2, y2);
            host.SetJointNumber((int)i, "x1", x1);
            host.SetJointNumber((int)i, "y1", y1);
            host.SetJointNumber((int)i, "x2", x2);
            host.SetJointNumber((int)i, "y2", y2);
        }
    }

    // 7. rolling loops (when not running all three volumes are 0, so all three are stopped)
    StopOrAdjust(host, kLoopWood, woodVolume);
    StopOrAdjust(host, kLoopRock, rockVolume);
    StopOrAdjust(host, kLoopLight, lightVolume);

    // 8. callbacks
    if (s.versionReceived) {
        host.LatestVersionNumberReceived();
        s.versionReceived = false;
    }
    if (s.updateNoticePending) host.UpdateAvailable(s.updateNoticeValue);
    host.CallUpdate(dt, dt);
    if (s.running) host.UpdateEffects(dt);
}

}  // namespace ab
