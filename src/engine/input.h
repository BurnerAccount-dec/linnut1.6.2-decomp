// Keyboard and mouse state for the scripts (app_handleKeysAndInput 0x402f80 and app_updateMultitouch 0x402810).
//
// The scripts read three tables indexed by key name, keyPressed (true on the frame a key went down),
// keyReleased (true on the frame it came up) and keyHold (true while it is down). The exe fills every
// one of its 89 key names into each table on every frame, then clears the pressed / released flags.
// The mouse is the key LBUTTON / MBUTTON / RBUTTON plus the table cursor {x, y, wheel, wheelTriggered};
// with a mouse the engine fakes one touch (touchcount = 1, touches[1] = {x, y}) while LBUTTON is down.
#pragma once
#include <string>

extern "C" {
#include "lua.h"
}

namespace ab {

constexpr int kKeyCount = 89;
extern const char* const kKeyNames[kKeyCount];
int KeyIndex(const std::string& name);       // -1 if it is not one of the 89

class InputBridge {
public:
    void KeyDown(int key);                    // an index into kKeyNames; repeats are ignored
    void KeyUp(int key);
    void MouseMove(float x, float y) { cursorX_ = x; cursorY_ = y; }
    void Wheel(float amount) { wheel_ += amount; }
    void ReleaseAll();                        // the window lost focus
    // Writes the state into the script's tables; call it right before the frame update.
    void Apply(lua_State* L);
    // Clears the one-frame flags; call it right after the frame update.
    void EndFrame();

private:
    bool pressed_[kKeyCount] = {}, released_[kKeyCount] = {}, held_[kKeyCount] = {};
    float cursorX_ = 0, cursorY_ = 0, wheel_ = 0;
    bool wheelTriggered_ = false;
};

}  // namespace ab
