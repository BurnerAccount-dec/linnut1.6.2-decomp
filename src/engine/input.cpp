#include "engine/input.h"

extern "C" {
#include "lauxlib.h"
}

namespace ab {

// The order of the exe's table (0x5049d8)
const char* const kKeyNames[kKeyCount] = {
    "NONE", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V",
    "W", "X", "Y", "Z", "PAGEDOWN", "PAGEUP", "HOME", "END", "INSERT", "DELETE", "RETURN", "SHIFT", "CONTROL", "TAB",
    "SPACE", "BACKSPACE", "PAUSE", "ESCAPE", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
    "LEFT", "RIGHT", "UP", "DOWN", "LBUTTON", "MBUTTON", "RBUTTON", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "MULTIPLY", "ADD", "SUBTRACT", "DIVIDE", "LEFTSOFT", "RIGHTSOFT", "MIDDLE", "ASTERISK", "HASH", "CLEAR", "EDIT",
    "SEND", "VOLUME_UP", "VOLUME_DOWN", "KEY_GAMING_A", "KEY_GAMING_B", "KEY_BACK", "KEY_MENU", "KEY_ALT"};

int KeyIndex(const std::string& name) {
    for (int i = 0; i < kKeyCount; i++)
        if (name == kKeyNames[i]) return i;
    return -1;
}

void InputBridge::KeyDown(int key) {
    if (key < 0 || key >= kKeyCount || held_[key]) return;
    held_[key] = true;
    pressed_[key] = true;
}

void InputBridge::KeyUp(int key) {
    if (key < 0 || key >= kKeyCount || !held_[key]) return;
    held_[key] = false;
    released_[key] = true;
}

void InputBridge::ReleaseAll() {
    for (int i = 0; i < kKeyCount; i++) KeyUp(i);
}

namespace {

void FillKeyTable(lua_State* L, const char* table, const bool* values) {
    lua_getfield(L, -1, table);
    if (lua_istable(L, -1)) {
        for (int i = 0; i < kKeyCount; i++) {
            lua_pushboolean(L, values[i]);
            lua_setfield(L, -2, kKeyNames[i]);
        }
    }
    lua_pop(L, 1);
}

}  // namespace

void InputBridge::Apply(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    FillKeyTable(L, "keyPressed", pressed_);
    FillKeyTable(L, "keyReleased", released_);
    FillKeyTable(L, "keyHold", held_);

    lua_getfield(L, -1, "cursor");
    if (lua_istable(L, -1)) {
        lua_pushnumber(L, cursorX_); lua_setfield(L, -2, "x");
        lua_pushnumber(L, cursorY_); lua_setfield(L, -2, "y");
        // both fields are reset every frame: 2.0.0's menus only clear wheelTriggered in the editor, so a flag left
        // true kept sending WHEEL events (the credits page, then every page after it, scrolled by itself)
        lua_pushnumber(L, wheel_); lua_setfield(L, -2, "wheel");
        lua_pushboolean(L, wheel_ != 0); lua_setfield(L, -2, "wheelTriggered");   // true for the one frame of the notch
    }
    lua_pop(L, 1);

    // one fake touch while the left button is down
    bool down = held_[KeyIndex("LBUTTON")];
    lua_pushnumber(L, down ? 1 : 0);
    lua_setfield(L, -2, "touchcount");
    lua_getfield(L, -1, "touches");
    if (lua_istable(L, -1)) {
        if (down) {
            lua_newtable(L);
            lua_pushnumber(L, cursorX_); lua_setfield(L, -2, "x");
            lua_pushnumber(L, cursorY_); lua_setfield(L, -2, "y");
            lua_rawseti(L, -2, 1);
        } else {
            lua_pushnil(L);
            lua_rawseti(L, -2, 1);
        }
    }
    lua_pop(L, 2);
    wheel_ = 0;
}

void InputBridge::EndFrame() {
    for (int i = 0; i < kKeyCount; i++) pressed_[i] = released_[i] = false;
}

}  // namespace ab
