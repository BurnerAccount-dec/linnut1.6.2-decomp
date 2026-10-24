// The Lua side of FrameHost: writes the per-frame results into objects.world[name], objects.joints[name]
// and the script environment, and calls the Lua callbacks. Stepping the world, audio loops and the
// effects manager stay abstract: the engine supplies them.
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "game/frame_update.h"
#include "lua/script_host.h"

namespace ab {

class LuaFrameHost : public FrameHost {
public:
    explicit LuaFrameHost(ScriptHost& scripts) : scripts_(scripts) {}

    // Object ids used in FrameObject::id are indexes into this list; joint index i is the i-th name.
    int AddObjectName(const std::string& name) { objectNames_.push_back(name); return (int)objectNames_.size() - 1; }
    int AddJointName(const std::string& name) { jointNames_.push_back(name); return (int)jointNames_.size() - 1; }
    // Optional: when set, these resolve ids to names instead of the lists above (the physics world
    // removes objects and joints, so its ids are not list indexes).
    std::function<std::string(int)> objectNameOf, jointNameOf;

    void CallRemoveBlocks() override { scripts_.RemoveBlocks(); }
    void SetObjectNumber(int id, const char* key, float value) override;
    void SetObjectBool(int id, const char* key, bool value) override;
    void SetGameBool(const char* key, bool value) override;
    void SetJointNumber(int jointIndex, const char* key, float value) override;
    void MarkBlockDead(int id) override;
    void CallUpdate(float dt, float realDt) override { scripts_.Update(dt, realDt); }
    void LogBirdVelocity(int id, float vx, float vy) override;

private:
    std::string ObjectName(int id) const { return objectNameOf ? objectNameOf(id) : objectNames_[id]; }
    std::string JointName(int index) const { return jointNameOf ? jointNameOf(index) : jointNames_[index]; }
    // pushes objects.<which>[name] (a table) or nil
    void PushObjectsEntry(const char* which, const std::string& name);

    ScriptHost& scripts_;
    std::vector<std::string> objectNames_, jointNames_;
};

}  // namespace ab
