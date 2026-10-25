#include <algorithm>
#include <fstream>
#include <iterator>
#include "engine/game_platform.h"

#include <cmath>
#include <algorithm>
#include <cmath>
#include <cstdio>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace ab {

class GamePlatform::Host : public LuaFrameHost {
public:
    Host(ScriptHost& scripts, PhysicsWorld& physics) : LuaFrameHost(scripts), physics_(physics) {
        objectNameOf = [this](int id) { return physics_.NameOf(id); };
        jointNameOf = [this](int index) { return physics_.JointName(index); };
    }
    void StepWorld(float dt, int velIter, int posIter) override { physics_.Step(dt, velIter, posIter); }
    void ClearForces() override { physics_.ClearForces(); }
    // Audio loops and the effects manager are not implemented yet.
    bool LoopActive(LoopSound) override { return false; }
    void StartLoop(LoopSound, float) override {}
    void SetLoopVolume(LoopSound, float) override {}
    void StopLoop(LoopSound) override {}
    void UpdateEffects(float) override {}

private:
    PhysicsWorld& physics_;
};

namespace {

// Pushes objects.world[name] (nil if there is none) and leaves it on the stack.
void PushWorldEntry(lua_State* L, const std::string& name) {
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_getfield(L, -1, "objects");
    lua_remove(L, -2);
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "world");
        lua_remove(L, -2);
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, name.c_str());
            lua_remove(L, -2);
            return;
        }
    }
    lua_pop(L, 1);
    lua_pushnil(L);
}

bool GetNumber(lua_State* L, int table, const char* key, float& out) {
    lua_getfield(L, table, key);
    bool ok = lua_type(L, -1) == LUA_TNUMBER;
    if (ok) out = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);
    return ok;
}

// A table of material -> number, such as blockTable.damageFactors.X.damageMultiplier
void ReadNumberMap(lua_State* L, int table, const char* key, std::map<std::string, float>& out) {
    lua_getfield(L, table, key);
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            if (lua_type(L, -2) == LUA_TSTRING && lua_type(L, -1) == LUA_TNUMBER)
                out[lua_tostring(L, -2)] = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
}

// Fills the Lua side of a ContactObject from its objects.world entry (at stack index `entry`).
void ReadContactFields(lua_State* L, int entry, ContactObject& c, DamageFactorSet& factors) {
    c.hasStrength = GetNumber(L, entry, "strength", c.strength);
    c.hasDefence = GetNumber(L, entry, "defence", c.defence);
    lua_getfield(L, entry, "material");
    if (lua_type(L, -1) == LUA_TSTRING) c.material = lua_tostring(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, entry, "useLegacyCollisionPath");
    c.useLegacyCollisionPath = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);

    // objects.world[name].damageFactors names a set in blockTable.damageFactors
    lua_getfield(L, entry, "damageFactors");
    if (lua_type(L, -1) == LUA_TSTRING) {
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        lua_getfield(L, -1, "blockTable");
        lua_remove(L, -2);
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "damageFactors");
            lua_remove(L, -2);
            if (lua_istable(L, -1)) {
                lua_pushvalue(L, -2);
                lua_gettable(L, -2);
                lua_remove(L, -2);
                if (lua_istable(L, -1)) {
                    ReadNumberMap(L, lua_gettop(L), "damageMultiplier", factors.damageMultiplier);
                    ReadNumberMap(L, lua_gettop(L), "velocityMultiplier", factors.velocityMultiplier);
                    c.damageFactors = &factors;
                }
            }
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

// scoreTable.blocks (nil when the script has none)
void PushBlockScore(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_getfield(L, -1, "scoreTable");
    lua_remove(L, -2);
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "blocks");
        lua_remove(L, -2);
        return;
    }
    lua_pop(L, 1);
    lua_pushnil(L);
}

}  // namespace

GamePlatform::GamePlatform(const std::string& dataRoot) : dataRoot_(dataRoot) {
    physics_.onBeginContact = [this](PhysObject& a, PhysObject& b, b2Contact* c) { OnContact(a, b, c); };
    // the contact filter reads objects.world[name].material live
    physics_.materialOf = [this](const PhysObject& o) {
        std::string material;
        if (!scripts_) return material;
        lua_State* L = scripts_->State();
        PushWorldEntry(L, o.name);
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "material");
            if (lua_type(L, -1) == LUA_TSTRING) material = lua_tostring(L, -1);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        return material;
    };
}
GamePlatform::~GamePlatform() {}

void GamePlatform::Attach(ScriptHost& scripts) {
    scripts_ = &scripts;
    host_.reset(new Host(scripts, physics_));
}

// Game::update (0x408980) starts the sound output by itself whenever settings.audioEnabled is true and
// the output exists but is not running; the scripts only ever call stopAudioOutput.
void GamePlatform::AddSpeed(float step) {
    speed_ += step;
    if (speed_ < 0.25f) speed_ = 0.25f;
    if (speed_ > 8.0f) speed_ = 8.0f;
    Print("speed: " + std::to_string(speed_) + "x");
}

void GamePlatform::RunFrame(float dt) {
    dt *= speed_ * (speedBoost_ ? 2.0f : 1.0f);
    // later versions (3.2.0 and up) scale the time with the script global physicsSpeedFactor
    if (scripts_) {
        lua_State* L = scripts_->State();
        int top = lua_gettop(L);
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        lua_getfield(L, -1, "physicsSpeedFactor");
        if (lua_isnumber(L, -1) && lua_tonumber(L, -1) > 0) dt *= (float)lua_tonumber(L, -1);
        lua_settop(L, top);
    }
    std::string audioError = audio_.TakeError();
    if (!audioError.empty()) Print("audio: " + audioError);
    if (scripts_ && audio_.Created() && !audio_.Started()) {
        lua_State* L = scripts_->State();
        int top = lua_gettop(L);
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        lua_getfield(L, -1, "settings");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "audioEnabled");
            if (lua_isboolean(L, -1) && lua_toboolean(L, -1)) audio_.Start();
        }
        lua_settop(L, top);
    }
    UpdateZoom(dt);
    // Seasons restart (R key): the script makes the pause page the current page while the level loads and the
    // page's offset is still 0 from before it was laid out, so it shows next to "Loading..." until the game
    // mode takes over. The page is hidden (animation at 0) then, so keep it off screen for those frames.
    if (scripts_ && !pauseGuard_ && IsSeasonsScripts()) {
        lua_State* L = scripts_->State();
        int top = lua_gettop(L);
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        lua_getfield(L, -1, "drawMenu");
        if (lua_isfunction(L, -1)) {
            pauseGuard_ = true;
            const char* code =
                "local orig = drawMenu\n"
                "drawMenu = function(...)\n"
                "  local pp = pausePage\n"
                "  if pp and currentGameMode == updateLoading and currentMenuPage == pp and pp.offsetX == 0 and (pauseBGw or 0) > 0 then\n"
                "    local a = elementAnimations and elementAnimations.ingamePausePageScroll\n"
                "    if a and (a.percentage or 0) <= 0 then\n"
                "      pp.offsetX = -pauseBGw\n"
                "      if pp.backgroundOverlay then pp.backgroundOverlay.shade = 0 end\n"
                "    end\n"
                "  end\n"
                "  return orig(...)\n"
                "end\n";
            if (luaL_loadstring(L, code) == 0) {
                lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
                lua_setfenv(L, -2);
                if (lua_pcall(L, 0, 0, 0) != 0) std::fprintf(stderr, "pause guard: %s\n", lua_tostring(L, -1));
            }
        }
        lua_settop(L, top);
    }
    if (host_) UpdateFrame(physics_.Frame(), *host_, dt);
    // 0x40a8cb: the particles move only while the physics is enabled
    bool seasons = IsSeasonsScripts();
    if (physics_.Frame().running) UpdateParticles(dt, true, seasons ? -1 : 0);
    // Seasons: the menus switch their own particles on with the global enableMenuParticles
    if (scripts_) {
        lua_State* L = scripts_->State();
        int top = lua_gettop(L);
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        lua_getfield(L, -1, "enableMenuParticles");
        bool menu = lua_toboolean(L, -1) != 0;
        lua_settop(L, top);
        if (menu) UpdateParticles(dt, false);
    }
    // 2.0.0 (0x40a8cb): the menu list always moves
    if (!seasons) UpdateParticles(dt, false, 1);
}

void GamePlatform::Print(const std::string& text) {
    log_.push_back(text);
    std::fputs(text.c_str(), stdout);
    if (text.empty() || text[text.size() - 1] != '\n') std::fputc('\n', stdout);
}

// 0x40d2c0: the old level's bodies are destroyed and the step accumulator restarts
void GamePlatform::BeginLevelLoad() {
    physics_.Clear();
    // a new level (or a restart) starts without the old one's particles and bird trails
    particles_.clear();
    themeSprites_.clear();
    for (int i = 0; i < 6; i++) { trajDots_[i].clear(); trajPuffs_[i].clear(); }
}

void GamePlatform::CreateThemeSprite(const std::string& name, const std::string& sprite, float x, float y, float scaleX, float scaleY, float angle, float speedX, int layer) {
    themeSprites_.push_back({name, sprite, x, y, scaleX, scaleY, angle, speedX, layer});
}

void GamePlatform::ModifyThemeSprite(const std::string& name, float x, float y, float scaleX, float scaleY, float angle, int layer) {
    for (ThemeSprite& t : themeSprites_)
        if (t.name == name) { t.x = x; t.y = y; t.scaleX = scaleX; t.scaleY = scaleY; t.angle = angle; t.layer = layer; }
}

void GamePlatform::RemoveThemeSprite(const std::string& name, int layer) {
    for (size_t i = 0; i < themeSprites_.size(); i++)
        if (themeSprites_[i].name == name && themeSprites_[i].layer == layer) { themeSprites_.erase(themeSprites_.begin() + (long)i); break; }
}

// updateThemeSprite (the exe's 0x417550): sprites of the background layers drift by speedX (physics units a second)
// and wrap around the screen, left to right when moving left. The foreground layers are not updated.
bool GamePlatform::IsSeasonsScripts() {
    if (!scripts_) return false;
    lua_State* L = scripts_->State();
    int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_getfield(L, -1, "enableMenuParticles");
    bool yes = !lua_isnil(L, -1);
    lua_settop(L, top);
    return yes;
}

void GamePlatform::UpdateThemeSprites(float dt) {
    for (ThemeSprite& t : themeSprites_) {
        if (t.speedX == 0 || t.layer >= (int)bgLayers_.size()) continue;
        const Sprite* sp = sprites_.Find(t.sprite);
        if (!sp) continue;
        float shift = bgLayers_[t.layer].speed * camX_;
        if (IsSeasonsScripts()) {
            // Seasons: a sprite goes round only once it is wholly off the screen (its full width, from its pivot)
            // and comes back wholly off the other side, so it never shows up out of nowhere
            float sw = (float)sp->w * t.scaleX, pivot = (float)sp->pivotX * t.scaleX;
            float margin = sw * 0.5f;
            float leftEdge = t.x * 20.0f - pivot, rightEdge = leftEdge + sw;
            float lo = shift, hi = screenWidth_ / worldScale_ + shift;
            t.x += t.speedX * dt;
            leftEdge = t.x * 20.0f - pivot; rightEdge = leftEdge + sw;
            if (t.speedX < 0 && rightEdge < lo - margin) t.x = (hi + margin + pivot) / 20.0f;
            else if (t.speedX > 0 && leftEdge > hi + margin) t.x = (lo - margin - sw + pivot) / 20.0f;
            continue;
        }
        float left = t.x * 20.0f - (float)sp->w * t.scaleX;
        float leftBound = shift - (float)sp->h * t.scaleX * 1.1f;
        float rightBound = screenWidth_ / worldScale_ + shift;
        t.x += t.speedX * dt;
        if (left < leftBound && t.speedX < 0) t.x = ((float)sp->w * t.scaleX + rightBound) / 20.0f;
        else if (left > rightBound && t.speedX > 0) t.x = ((float)sp->w * t.scaleX + leftBound) / 20.0f;
    }
}

// The sprites of one layer, placed in the same world as the physics objects (units * 20) and moved by the
// layer's own camera shift. The size is the world scale times the sprite's own scale.
void GamePlatform::DrawThemeSprites(int layer) {
    RenderState layerState = state_;
    for (const ThemeSprite& t : themeSprites_) {
        if (t.layer != layer) continue;
        const Sprite* sp = sprites_.Find(t.sprite);
        float px = t.x * 20.0f * worldScale_ - themeShift_ * layerState.sx;
        float py = t.y * 20.0f * worldScale_ + layerState.ty * layerState.sy;
        state_ = RenderState();
        state_.sx = worldScale_ * t.scaleX; state_.sy = worldScale_ * t.scaleY;
        state_.angle = t.angle;
        if (sp) { state_.pivotX = (float)sp->pivotX; state_.pivotY = (float)sp->pivotY; }
        ResDrawSprite(t.sprite, px / state_.sx, py / state_.sy, 4, 3, false, 0, 0);
    }
    state_ = layerState;
}

std::string GamePlatform::ResolveDataFile(const std::string& path) {
    if (dataRoot_.empty()) return path;
    return dataRoot_ + "/" + path;
}

// Box2D BeginContact -> the exe's collision damage code (0x40b670). The rules are in ResolveContact;
// this reads the Lua fields it needs, applies the result and calls birdCollision / blockCollision.
// (The two skip flags at Game+0x1e9 / 0x1ea are only ever set to 0 in the exe, so they are left out.)
void GamePlatform::OnContact(PhysObject& pa, PhysObject& pb, b2Contact* contact) {
    if (!scripts_) return;
    lua_State* L = scripts_->State();
    int top = lua_gettop(L);

    // both names must be keys of objects.world
    PushWorldEntry(L, pa.name);
    PushWorldEntry(L, pb.name);
    if (!lua_istable(L, top + 1) || !lua_istable(L, top + 2)) {
        lua_settop(L, top);
        return;
    }
    ContactObject a = PhysicsWorld::MakeContactObject(pa), b = PhysicsWorld::MakeContactObject(pb);
    DamageFactorSet factorsA, factorsB;
    ReadContactFields(L, top + 1, a, factorsA);
    ReadContactFields(L, top + 2, b, factorsB);

    // the score is read before the callback runs and written after it
    float scoreBefore = 0;
    PushBlockScore(L);
    if (lua_istable(L, -1)) GetNumber(L, lua_gettop(L), "score", scoreBefore);
    lua_pop(L, 1);

    ContactResult r = ResolveContact(a, b);

    for (size_t i = 0; i < r.strengthUpdates.size(); i++) {
        PushWorldEntry(L, r.strengthUpdates[i].name);
        if (lua_istable(L, -1)) {
            lua_pushnumber(L, r.strengthUpdates[i].strength);
            lua_setfield(L, -2, "strength");
        }
        lua_pop(L, 1);
    }
    for (size_t i = 0; i < r.destroyed.size(); i++) {      // deadBlocks[name] = objects.world[name]
        PushWorldEntry(L, r.destroyed[i]);
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        lua_getfield(L, -1, "deadBlocks");
        lua_remove(L, -2);
        if (lua_istable(L, -1) && lua_istable(L, -2)) {
            lua_pushvalue(L, -2);
            lua_setfield(L, -2, r.destroyed[i].c_str());
        }
        lua_pop(L, 2);
    }
    if (r.hasVelocityChange) physics_.ApplyVelocityChange(r.velocityChange);
    if (r.disableContact) contact->SetEnabled(false);
    lua_settop(L, top);

    for (size_t i = 0; i < r.events.size(); i++) {
        const CollisionEvent& e = r.events[i];
        if (e.isBird) birdCollisions++; else blockCollisions++;
        if (e.isBird) scripts_->BirdCollision(e.object1, e.object2, e.force, e.damage);
        else scripts_->BlockCollision(e.object1, e.object2, e.force, e.wasDamageDone);
    }

    if (r.blockScoreAdd > 0) {
        PushBlockScore(L);
        if (lua_istable(L, -1)) {
            lua_pushnumber(L, scoreBefore + r.blockScoreAdd);
            lua_setfield(L, -2, "score");
        }
        lua_settop(L, top);
    }
}

// ---- drawing

void GamePlatform::SetBackend(RenderBackend* backend, int screenWidth, int screenHeight) {
    backend_ = backend;
    screenWidth_ = screenWidth;
    screenHeight_ = screenHeight;
}

void GamePlatform::Resize(int screenWidth, int screenHeight) {
    bool changed = sizeKnown_ && (screenWidth != screenWidth_ || screenHeight != screenHeight_);
    sizeKnown_ = true;
    screenWidth_ = screenWidth;
    screenHeight_ = screenHeight;
    if (!scripts_) return;
    lua_State* L = scripts_->State();
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_pushnumber(L, screenWidth); lua_setfield(L, -2, "screenWidth");
    lua_pushnumber(L, screenHeight); lua_setfield(L, -2, "screenHeight");
    lua_pop(L, 1);
    // the exe tells the scripts when the window is resized: they lay the cameras and pages out again
    if (changed) scripts_->ResolutionChanged((float)screenWidth, (float)screenHeight);
}

bool GamePlatform::TakeResolutionRequest(int& w, int& h) {
    if (!hasReq_) return false;
    w = reqW_; h = reqH_; hasReq_ = false;
    return true;
}

bool GamePlatform::CheckForLuaFile(const std::string& path) {
    std::ifstream f(ResolveDataFile(path));
    return f.good();
}

void GamePlatform::BeginFrame() {
    if (backend_) backend_->BeginFrame(screenWidth_, screenHeight_, bgR_, bgG_, bgB_);
}

void GamePlatform::EndFrame() {
    // 2.0.0 draws the menu list itself, in screen space, over everything (0x4161c0)
    if (!menuParticlesByScript_ && !IsSeasonsScripts()) DrawParticlesOf(false, 1);
    if (backend_) backend_->EndFrame();
}

// 0x40c570: each channel is clamped to 0..255 and rounded
void GamePlatform::SetBGColor(float r, float g, float b) {
    auto channel = [](float v) { return std::floor(std::min(255.0f, std::max(0.0f, v)) + 0.5f) / 255.0f; };
    bgR_ = channel(r); bgG_ = channel(g); bgB_ = channel(b);
}

// 0x408630: only the values that were passed are changed
void GamePlatform::SetRenderState(int argc, const float* a) {
    if (argc >= 2) { state_.tx = a[0]; state_.ty = a[1]; }
    if (argc >= 4) { state_.sx = a[2]; state_.sy = a[3]; }
    if (argc >= 5) state_.angle = a[4];
    if (argc >= 7) { state_.pivotX = a[5]; state_.pivotY = a[6]; }
    if (argc >= 8) state_.alpha = a[7];
}

void GamePlatform::ResSetClipRect(float x, float y, float w, float h) {
    if (backend_) backend_->SetClip(true, x, y, w, h);
}

const GamePlatform::TextureInfo* GamePlatform::TextureFor(const SpriteSheet& sheet) {
    return TextureFile(sheet.directory + "/" + sheet.image, false);
}

const GamePlatform::TextureInfo* GamePlatform::TextureFile(const std::string& path, bool repeat) {
    auto it = textures_.find(path);
    if (it != textures_.end()) return it->second.handle >= 0 ? &it->second : nullptr;
    TextureInfo info;
    Image image;
    std::string error;
    if (LoadPngFile(path, image, &error)) {
        info.handle = backend_->CreateTexture(image, repeat);
        info.width = image.width;
        info.height = image.height;
    } else {
        Print("texture: " + error);
    }
    TextureInfo& stored = textures_[path] = info;
    return stored.handle >= 0 ? &stored : nullptr;
}

// A point of a sprite: rotate by the state's angle about (cx, cy), then translate and scale.
void GamePlatform::Transform(float x, float y, float cx, float cy, float& ox, float& oy) const {
    float c = std::cos(state_.angle), s = std::sin(state_.angle);
    float dx = x - cx, dy = y - cy;
    ox = (cx + c * dx - s * dy + state_.tx) * state_.sx;
    oy = (cy + s * dx + c * dy + state_.ty) * state_.sy;
}

// Sprite_handleEvent (0x43aab0) and drawSprite (0x434b30 / 0x434c60): the anchors move the sprite's
// top left corner away from (x, y) by a part of its own size, the quad is drawn at the sprite's size
// (or the size passed), with the sprite's rectangle of the sheet as texture coordinates.
void GamePlatform::ResDrawSprite(const std::string& name, float x, float y, int vAnchor, int hAnchor, bool sized, float width, float height) {
    if (!backend_) return;
    const SpriteSheet* sheet = nullptr;
    const Sprite* sp = sprites_.Find(name, &sheet);
    if (sp) { DrawSpriteAt(*sheet, *sp, x, y, vAnchor, hAnchor, sized, width, height); return; }
    // Sprite_handleAllEvents (0x43ae10): the anchors move the composite's origin, then every part is
    // drawn at its own position with the pivot anchors.
    const CompoSprite* compo = sprites_.FindCompo(name);
    if (!compo) return;
    int w, h, ox, oy;
    sprites_.CompoExtent(*compo, w, h, ox, oy);
    switch (vAnchor) {
        case 0: y += (float)oy; break;
        case 1: y += (float)(oy - (h >> 1)); break;
        case 2: y += (float)(oy - h); break;
        default: break;
    }
    switch (hAnchor) {
        case 0: x += (float)ox; break;
        case 1: x += (float)(ox - (w >> 1)); break;
        case 2: x += (float)(ox - w); break;
        default: break;
    }
    for (const CompoPart& part : compo->parts) {
        const SpriteSheet* partSheet = nullptr;
        const Sprite* ps = sprites_.Find(part.sprite, &partSheet);
        if (ps) DrawSpriteAt(*partSheet, *ps, x + (float)part.x, y + (float)part.y, 4, 3, false, 0, 0);
    }
}

void GamePlatform::ResCreateCompoSpriteSet(const std::string& path) {
    std::string error;
    if (!sprites_.LoadCompoSet(path, ResolveDataFile(path), &error)) Print("createCompoSpriteSet: " + error);
}

// 0x4313d0: left, top, right, bottom of the composite relative to its origin
void GamePlatform::ResGetCompoSpriteBounds(const std::string&, const std::string& name, float& x1, float& y1, float& x2, float& y2) {
    x1 = y1 = x2 = y2 = 0;
    const CompoSprite* compo = sprites_.FindCompo(name);
    if (!compo) return;
    int w, h, ox, oy;
    sprites_.CompoExtent(*compo, w, h, ox, oy);
    x1 = (float)-ox; y1 = (float)-oy; x2 = (float)(w - ox); y2 = (float)(h - oy);
}

void GamePlatform::DrawSpriteAt(const SpriteSheet& sheet, const Sprite& spr, float x, float y, int vAnchor, int hAnchor, bool sized, float width, float height) {
    const Sprite* sp = &spr;
    const TextureInfo* tex = TextureFor(sheet);
    if (!tex) return;

    switch (vAnchor) {
        case 1: y -= (float)(sp->h >> 1); break;
        case 2: y -= (float)sp->h; break;
        case 3: case 4: y -= (float)sp->pivotY; break;
        default: break;
    }
    switch (hAnchor) {
        case 1: x -= (float)(sp->w >> 1); break;
        case 2: x -= (float)sp->w; break;
        case 3: x -= (float)sp->pivotX; break;
        default: break;
    }
    float w = sized ? width : (float)sp->w, h = sized ? height : (float)sp->h;
    float cx = x + state_.pivotX, cy = y + state_.pivotY;
    float xy[8], uv[8];
    const float cornerX[4] = {0, w, w, 0}, cornerY[4] = {0, 0, h, h};
    for (int i = 0; i < 4; i++) Transform(x + cornerX[i], y + cornerY[i], cx, cy, xy[i * 2], xy[i * 2 + 1]);
    float tileUV[8];
    if (tileTexture_) {
        // the tile's uv is the world position (rotated, before the camera) in texture sizes
        float c = std::cos(state_.angle), s = std::sin(state_.angle);
        for (int i = 0; i < 4; i++) {
            float dx = x + cornerX[i] - cx, dy = y + cornerY[i] - cy;
            tileUV[i * 2] = (cx + c * dx - s * dy) / tileTexture_->width;
            tileUV[i * 2 + 1] = (cy + s * dx + c * dy) / tileTexture_->height;
        }
    }
    float u0 = (float)sp->x / tex->width, v0 = (float)sp->y / tex->height;
    float u1 = (float)(sp->x + sp->w) / tex->width, v1 = (float)(sp->y + sp->h) / tex->height;
    // 2.0.0's tutorial sheet is packed tight, so the smooth filter picks up the next sprite's pixels along the
    // edges (thin coloured lines); keep the samples half a texel inside the sprite
    if ((sized || sheet.image.find("TUTORIALS") != std::string::npos) && sp->w > 1 && sp->h > 1) {
        u0 += 0.5f / tex->width; u1 -= 0.5f / tex->width; v0 += 0.5f / tex->height; v1 -= 0.5f / tex->height;
    }
    const float us[4] = {u0, u1, u1, u0}, vs[4] = {v0, v0, v1, v1};
    for (int i = 0; i < 4; i++) { uv[i * 2] = us[i]; uv[i * 2 + 1] = vs[i]; }
    if (tileTexture_) backend_->DrawMaskedQuad(tileTexture_->handle, tex->handle, xy, tileUV, uv);
    else backend_->DrawQuad(tex->handle, xy, uv, 1, 1, 1, state_.alpha);
}

// 0x40abd0: colour 0..1, corners are whole numbers. Without inWorld the render state is first reset.
void GamePlatform::DrawRect(float r, float g, float b, float a, float x1, float y1, float x2, float y2, bool inWorld) {
    if (!inWorld) state_ = RenderState();
    if (!backend_) return;
    x1 = std::trunc(x1); y1 = std::trunc(y1); x2 = std::trunc(x2); y2 = std::trunc(y2);
    float xy[8], uv[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const float px[4] = {x1, x2, x2, x1}, py[4] = {y1, y1, y2, y2};
    for (int i = 0; i < 4; i++) Transform(px[i], py[i], px[i], py[i], xy[i * 2], xy[i * 2 + 1]);
    backend_->DrawQuad(-1, xy, uv, r, g, b, a);
}

// 0x415310: whole-number coordinates and width, colour 0..255
void GamePlatform::DrawLine2D(float x1, float y1, float x2, float y2, float width, float r, float g, float b, float a) {
    if (!backend_) return;
    x1 = std::trunc(x1); y1 = std::trunc(y1); x2 = std::trunc(x2); y2 = std::trunc(y2);
    width = std::trunc(width);
    float dx = x2 - x1, dy = y2 - y1, len = std::sqrt(dx * dx + dy * dy);
    if (len == 0 || width <= 0) return;
    float nx = -dy / len * width * 0.5f, ny = dx / len * width * 0.5f;
    float px[4] = {x1 + nx, x2 + nx, x2 - nx, x1 - nx}, py[4] = {y1 + ny, y2 + ny, y2 - ny, y1 - ny};
    float xy[8], uv[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 4; i++) Transform(px[i], py[i], px[i], py[i], xy[i * 2], xy[i * 2 + 1]);
    backend_->DrawQuad(-1, xy, uv, r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

void GamePlatform::ResCreateSpriteSheet(const std::string& path) {
    std::string error;
    if (sprites_.Load(path, ResolveDataFile(path), &error)) return;
    // Seasons lists its composite sprite files with the sheets
    if (error == "no SPRT chunk" && sprites_.LoadCompoSet(path, ResolveDataFile(path), &error)) return;
    Print("createSpriteSheet " + path + ": " + error);
}

void GamePlatform::ResReleaseSpriteSheet(const std::string& path) { sprites_.Release(path); }

void GamePlatform::ResGetSpriteBounds(const std::string&, const std::string& sprite, float& w, float& h) {
    const Sprite* s = sprites_.Find(sprite);
    w = s ? (float)s->w : 0.0f;
    h = s ? (float)s->h : 0.0f;
    // sprites and composite sprites share one name table: a composite's size is its extent
    if (!s) if (const CompoSprite* compo = sprites_.FindCompo(sprite)) {
        int cw, ch, ox, oy;
        sprites_.CompoExtent(*compo, cw, ch, ox, oy);
        w = (float)cw; h = (float)ch;
    }
}

void GamePlatform::ResGetSpritePivot(const std::string&, const std::string& sprite, float& px, float& py) {
    const Sprite* s = sprites_.Find(sprite);
    px = s ? (float)s->pivotX : 0.0f;
    py = s ? (float)s->pivotY : 0.0f;
    if (!s) if (const CompoSprite* compo = sprites_.FindCompo(sprite)) {
        int cw, ch, ox, oy;
        sprites_.CompoExtent(*compo, cw, ch, ox, oy);
        px = (float)ox; py = (float)oy;
    }
}

// ---- fonts and text

namespace {

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& bytes) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return false;
    bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// "dir/FONT_BASIC.dat" -> "FONT_BASIC"
std::string BaseName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

}  // namespace

void GamePlatform::ResCreateBitmapFont(const std::string& path) {
    std::string full = ResolveDataFile(path), error;
    std::vector<uint8_t> bytes;
    BitmapFont font;
    if (!ReadWholeFile(full, bytes)) { Print("createBitmapFont: cannot open " + full); return; }
    if (!ParseBitmapFont(bytes, font, &error)) { Print("createBitmapFont: " + full + ": " + error); return; }
    size_t slash = full.find_last_of("/\\");
    font.directory = slash == std::string::npos ? "." : full.substr(0, slash);
    fonts_[BaseName(path)] = font;
}

const BitmapFont* GamePlatform::CurrentFont() const {
    auto it = fonts_.find(current_);
    return it == fonts_.end() ? nullptr : &it->second;
}

void GamePlatform::ResCreateTextGroupSet(const std::string& path) {
    std::string full = ResolveDataFile(path), error;
    std::vector<uint8_t> bytes;
    TextGroup group;
    if (!ReadWholeFile(full, bytes)) { Print("createTextGroupSet: cannot open " + full); return; }
    if (!ParseTextGroup(bytes, group, &error)) { Print("createTextGroupSet: " + full + ": " + error); return; }
    textGroups_[BaseName(path)] = group;
}

std::string GamePlatform::ResGetString(const std::string& group, const std::string& key) {
    auto g = textGroups_.find(group);
    if (g == textGroups_.end()) return key;
    auto s = g->second.strings.find(key);
    return s == g->second.strings.end() ? key : s->second;
}

float GamePlatform::ResGetStringWidth(const std::string& text) {
    const BitmapFont* f = CurrentFont();
    return f ? f->StringWidth(text) : 0.0f;
}
float GamePlatform::ResGetFontHeight() { const BitmapFont* f = CurrentFont(); return f ? (float)(f->maxAscent + f->maxDescent) : 0.0f; }
float GamePlatform::ResGetFontLeading() { const BitmapFont* f = CurrentFont(); return f ? (float)f->leading : 0.0f; }
float GamePlatform::ResGetFontTracking() { const BitmapFont* f = CurrentFont(); return f ? (float)f->tracking : 0.0f; }
float GamePlatform::ResGetFontMaxAscending() { const BitmapFont* f = CurrentFont(); return f ? (float)f->maxAscent : 0.0f; }
float GamePlatform::ResGetFontMaxDescending() { const BitmapFont* f = CurrentFont(); return f ? (float)f->maxDescent : 0.0f; }

// drawString(group, text, x, y, anchor, anchor) (0x430800, font draw 0x43d260). The two anchor words may come in
// either order; the defaults are TOP and LEFT.
void GamePlatform::ResDrawString(const std::string& group, const std::string& key, float x, float y,
                                 const std::string& a1, const std::string& a2) {
    // a text group name means the text is a key of that group ("MI_PLAY"); an empty group draws the text itself
    const std::string text = group.empty() ? key : ResGetString(group, key);
    const BitmapFont* font = CurrentFont();
    if (!font || !backend_) return;
    int v = 0, h = 0;
    const std::string* words[2] = {&a1, &a2};
    for (int i = 0; i < 2; i++) {
        if (*words[i] == "TOP") v = 0; else if (*words[i] == "VCENTER") v = 1; else if (*words[i] == "BOTTOM") v = 2;
        else if (*words[i] == "BASELINE" || *words[i] == "VPIVOT") v = 3;     // y is the baseline: no move
        else if (*words[i] == "LEFT") h = 0; else if (*words[i] == "HCENTER") h = 1; else if (*words[i] == "RIGHT") h = 2;
    }
    float width = font->StringWidth(text);
    if (v == 0) y += (float)font->maxAscent;
    else if (v == 1) y += (float)(font->maxAscent - (font->maxAscent + font->maxDescent) / 2);
    else if (v == 2) y -= (float)font->maxDescent;
    if (h == 1) x -= (float)((int)width / 2);
    else if (h == 2) x -= width;

    SpriteSheet sheet;
    sheet.image = font->image;
    sheet.directory = font->directory;
    std::vector<uint32_t> chars = DecodeUtf8(text);
    for (size_t i = 0; i < chars.size(); i++) {
        auto g = font->glyphs.find(chars[i]);
        if (g == font->glyphs.end()) continue;
        DrawSpriteAt(sheet, g->second, x, y, 4, 3, false, 0, 0);
        x += (float)(font->tracking + g->second.w);
    }
}

// clipText: breaks the text at spaces so that no line is wider than maxWidth (the exe's exact rules are not
// known yet; this is the usual greedy wrap, and newlines in the text start a new line).
std::vector<std::string> GamePlatform::ClipText(const std::string& group, const std::string& key, float maxWidth, float& widest) {
    const std::string text = group.empty() ? key : ResGetString(group, key);
    std::vector<std::string> lines;
    widest = 0;
    const BitmapFont* font = CurrentFont();
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        std::string paragraph = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        std::string line;
        size_t p = 0;
        while (p <= paragraph.size()) {
            size_t sp = paragraph.find(' ', p);
            std::string word = paragraph.substr(p, sp == std::string::npos ? std::string::npos : sp - p);
            std::string trial = line.empty() ? word : line + " " + word;
            if (!line.empty() && font && maxWidth > 0 && font->StringWidth(trial) > maxWidth) {
                lines.push_back(line);
                line = word;
            } else {
                line = trial;
            }
            if (sp == std::string::npos) break;
            p = sp + 1;
        }
        lines.push_back(line);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    if (font) for (size_t i = 0; i < lines.size(); i++) widest = std::max(widest, font->StringWidth(lines[i]));
    return lines;
}

}  // namespace ab

namespace ab {

// ---- the main game's picture

// setTheme (reads blockTable.themes[name]): colour, groundColor, bgLayers, fgLayers, texture.
void GamePlatform::SetTheme(const std::string& theme) {
    bgLayers_.clear(); fgLayers_.clear(); fgSpeed_.clear(); fgScale_.clear(); themeTexture_.clear();
    if (!scripts_) return;
    lua_State* L = scripts_->State();
    int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_getfield(L, -1, "blockTable");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "themes");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, theme.c_str());
            if (lua_istable(L, -1)) {
                int t = lua_gettop(L);
                auto number = [&](int table, const char* key, float def) {
                    lua_getfield(L, table, key);
                    float v = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : def;
                    lua_pop(L, 1);
                    return v;
                };
                lua_getfield(L, t, "color");
                if (lua_istable(L, -1)) SetBGColor(number(lua_gettop(L), "r", 0), number(lua_gettop(L), "g", 0), number(lua_gettop(L), "b", 0));
                lua_pop(L, 1);
                lua_getfield(L, t, "groundColor");
                if (lua_istable(L, -1)) {
                    groundR_ = number(lua_gettop(L), "r", 0); groundG_ = number(lua_gettop(L), "g", 0); groundB_ = number(lua_gettop(L), "b", 0);
                }
                lua_pop(L, 1);
                lua_getfield(L, t, "texture");
                if (lua_isstring(L, -1)) themeTexture_ = lua_tostring(L, -1);
                lua_pop(L, 1);
                lua_getfield(L, t, "bgLayers");
                if (lua_istable(L, -1)) {
                    int n = (int)lua_objlen(L, -1);
                    for (int i = 1; i <= n; i++) {
                        lua_rawgeti(L, -1, i);
                        if (lua_istable(L, -1)) {
                            BgLayer layer;
                            lua_rawgeti(L, -1, 2);
                            if (lua_isstring(L, -1)) layer.sprite = lua_tostring(L, -1);
                            lua_pop(L, 1);
                            lua_rawgeti(L, -1, 3);
                            if (lua_isnumber(L, -1)) layer.speed = (float)lua_tonumber(L, -1);
                            lua_pop(L, 1);
                            lua_rawgeti(L, -1, 4);
                            if (lua_isnumber(L, -1)) layer.scale = (float)lua_tonumber(L, -1);
                            lua_pop(L, 1);
                            lua_rawgeti(L, -1, 5);
                            if (lua_isboolean(L, -1)) layer.looping = lua_toboolean(L, -1) != 0;
                            lua_pop(L, 1);
                            bgLayers_.push_back(layer);
                        }
                        lua_pop(L, 1);
                    }
                }
                lua_pop(L, 1);
                lua_getfield(L, t, "fgLayers");
                if (lua_istable(L, -1)) {
                    int n = (int)lua_objlen(L, -1);
                    for (int i = 1; i <= n; i++) {
                        lua_rawgeti(L, -1, i);
                        if (lua_istable(L, -1)) {
                            lua_rawgeti(L, -1, 2);
                            if (lua_isstring(L, -1)) {
                                fgLayers_.push_back(lua_tostring(L, -1));
                                lua_pop(L, 1);
                                lua_rawgeti(L, -1, 3);
                                fgSpeed_.push_back(lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 1.0f);
                                lua_pop(L, 1);
                                lua_rawgeti(L, -1, 4);
                                fgScale_.push_back(lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 1.5f);
                            }
                            lua_pop(L, 1);
                        }
                        lua_pop(L, 1);
                    }
                }
                lua_pop(L, 1);
            }
        }
    }
    lua_settop(L, top);
}

// ---- mouse-wheel zoom (App::update 0x402f90 animates, 0x403b00 handles the wheel, 0x408b5f feeds the scripts)

// The zoom eases from its start to its target with 1 + (u - 1)^3 over the duration. The scripts see the
// difference between the zoom and the scale they last set, halved, added to the global zoomLevel.
void GamePlatform::UpdateZoom(float dt) {
    if (zoomT_ > -1.0f) {
        zoomT_ += dt;
        float u = zoomT_ / zoomDur_ - 1.0f;
        zoom_ = zoomStart_ + (zoomTarget_ - zoomStart_) * (u * u * u + 1.0f);
        if (zoomT_ > zoomDur_) { zoomT_ = -1.0f; zoom_ = zoomTarget_; }
        if (zoom_ < 0.01f) zoom_ = 0.01f;
        if (zoom_ > 100.0f) zoom_ = 100.0f;
    }
    if (zoom_ != zoomPrev_ && scripts_) {
        lua_State* L = scripts_->State();
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        lua_getfield(L, -1, "zoomLevel");
        float level = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
        lua_pop(L, 1);
        lua_pushnumber(L, level + (zoom_ - zoomPrev_) * 0.5f);
        lua_setfield(L, -2, "zoomLevel");
        lua_pop(L, 1);
    }
}

// A wheel step moves the target by notches * 0.2 (0.1 below a scale of 1). While an animation is already
// running, between 0.6 and the maximum scale, the step is halved and added to the old target.
void GamePlatform::Wheel(int notches) {
    float step = zoom_ >= 1.0f ? 0.2f : 0.1f;
    zoomPrev_ = zoom_;
    if (zoomT_ > -1.0f && zoom_ > 0.6f && zoom_ < zoomMax_) {
        zoomTarget_ = step * 0.5f * (float)notches + zoomTarget_;
        zoomDur_ = 1.0f - zoomT_;
    } else {
        zoomTarget_ = (float)notches * step + zoom_;
        zoomDur_ = 0.5f;
    }
    if (zoomTarget_ < 0.01f) zoomTarget_ = 0.01f;
    if (zoomTarget_ > 100.0f) zoomTarget_ = 100.0f;
    zoomStart_ = zoom_;
    zoomT_ = 0.0f;
}

// drawBackgroundNative: every looping layer is a row of copies of its sprite, shifted by the camera
// times the layer's parallax speed.
void GamePlatform::DrawBackground() {
    if (!backend_) return;
    // The non-looping layers (the cranes) are placed one after another: the first at episode4BGCranes.startX / 16
    // sprite widths, each next one half a sprite width further.
    float startX = 0;
    if (scripts_) {
        lua_State* L = scripts_->State();
        int top = lua_gettop(L);
        lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
        lua_getfield(L, -1, "episode4BGCranes");
        if (lua_istable(L, -1)) GetNumber(L, lua_gettop(L), "startX", startX);
        lua_settop(L, top);
    }
    float place = 0;
    bool firstFixed = true;
    for (const BgLayer& layer : bgLayers_) {
        // a layer is a sprite or a composite sprite (the Seasons hills are composites)
        const Sprite* sp = sprites_.Find(layer.sprite);
        const CompoSprite* compo = sp ? nullptr : sprites_.FindCompo(layer.sprite);
        int cw = 0, ch = 0, ox = 0, oy = 0;
        if (compo) sprites_.CompoExtent(*compo, cw, ch, ox, oy);
        float w = sp ? (float)sp->w : (float)cw, pivotX = sp ? (float)sp->pivotX : (float)ox;
        if (w <= 0) continue;
        float s = layer.scale, inv = 1.0f / s;
        float shift = layer.speed * camX_;
        state_ = RenderState();
        state_.ty = -camY_ * inv;
        state_.sx = state_.sy = s * worldScale_;
        if (layer.looping) {
            float m = std::fmod(shift, w * s);
            float x0 = -(m + w * s) * inv;
            state_.tx = x0 - std::trunc(x0);
            float right = screenWidth_ * inv / worldScale_ + pivotX;
            for (float x = std::trunc(x0); x <= right; x += w) ResDrawSprite(layer.sprite, x, 0, 4, 3, false, 0, 0);
        } else {
            place += firstFixed ? startX * 0.0625f * w : w * 0.5f;
            firstFixed = false;
            state_.tx = place - std::trunc(place);
            ResDrawSprite(layer.sprite, place - shift * inv, 0, 4, 3, false, 0, 0);
        }
        themeShift_ = shift * inv;
        DrawThemeSprites((int)(&layer - &bgLayers_[0]));
    }
    state_ = RenderState();
}

// drawForegroundNative: the ground colour below the first foreground sprite, then the foreground
// sprites repeated at 1.5 times the world scale.
void GamePlatform::DrawForeground() {
    if (!backend_) return;
    // a foreground layer is a sprite or a composite sprite (the Ham 'o' Ween menu draws its trees and pumpkins as composites)
    auto extent = [&](const std::string& name, float& w, float& pivotX, float& h, float& pivotY) {
        const Sprite* sp = sprites_.Find(name);
        if (sp) { w = (float)sp->w; pivotX = (float)sp->pivotX; h = (float)sp->h; pivotY = (float)sp->pivotY; return true; }
        const CompoSprite* compo = sprites_.FindCompo(name);
        if (!compo) return false;
        int cw = 0, ch = 0, ox = 0, oy = 0;
        sprites_.CompoExtent(*compo, cw, ch, ox, oy);
        w = (float)cw; pivotX = (float)ox; h = (float)ch; pivotY = (float)oy;
        return true;
    };
    float height = 0;
    if (!fgLayers_.empty()) {
        float w, px, h, py;
        if (extent(fgLayers_[0], w, px, h, py)) height = (h - py) * fgScale_[0] / 1.5f;
    }
    DrawRect(groundR_ / 255.0f, groundG_ / 255.0f, groundB_ / 255.0f, 1, 0, -(camY_ - height * 1.5f) * worldScale_, (float)screenWidth_, (float)screenHeight_ + 1, false);
    int fgIndex = -1;
    for (const std::string& name : fgLayers_) {
        fgIndex++;
        float w, px, h, py;
        if (!extent(name, w, px, h, py) || w <= 0) continue;
        float c = fgScale_[fgIndex], shift = fgSpeed_[fgIndex] * camX_;
        float m = std::fmod(shift, c * w);
        float x0 = -(m + c * w) / c;
        state_ = RenderState();
        state_.tx = x0 - std::trunc(x0);
        state_.ty = -camY_ / c;
        state_.sx = state_.sy = worldScale_ * c;
        float right = screenWidth_ / (worldScale_ * c) + px;
        for (float x = std::trunc(x0); x <= right; x += w) ResDrawSprite(name, x, 0, 4, 3, false, 0, 0);
        themeShift_ = shift / c;
        DrawThemeSprites((int)bgLayers_.size() + fgIndex);
    }
    state_ = RenderState();
}

// One object: its sprite rotated about its pivot at the interpolated position (physics units * 20).
void GamePlatform::DrawPhysObject(PhysObject& o) {
    const SpriteSheet* sheet = nullptr;
    const Sprite* sp = sprites_.Find(o.sprite, &sheet);
    if (!sp) return;
    state_.angle = o.interpAngle;
    state_.pivotX = (float)sp->pivotX;
    state_.pivotY = (float)sp->pivotY;
    if (!o.texture.empty()) {
        const TextureInfo* t = TextureFile(sheet->directory + "/" + o.texture + ".png", true);
        if (t) tileTexture_ = t;
    }
    if (o.scaled && o.scaleX != 0 && o.scaleY != 0) {
        // 0x4165xx: the sprite shrinks about its pivot, the pivot stays where it was on the screen
        float x = o.interpX * 20.0f, y = o.interpY * 20.0f;
        RenderState saved = state_;
        state_.sx *= o.scaleX; state_.sy *= o.scaleY;
        state_.tx = (x + saved.tx) / o.scaleX - x;
        state_.ty = (y + saved.ty) / o.scaleY - y;
        DrawSpriteAt(*sheet, *sp, x, y, 4, 3, false, 0, 0);
        state_ = saved;
    } else {
        DrawSpriteAt(*sheet, *sp, o.interpX * 20.0f, o.interpY * 20.0f, 4, 3, false, 0, 0);
    }
    tileTexture_ = nullptr;
}

// drawGameNative: static blocks, then the other blocks, the trajectory dots, the birds, and
// finally everything with a z of 5 or more.
void GamePlatform::DrawGame() {
    if (!backend_) return;
    state_ = RenderState();
    state_.tx = -camX_; state_.ty = -camY_;
    state_.sx = state_.sy = worldScale_;
    const auto& objects = physics_.Objects();
    auto isStatic = [](PhysObject& o) { return o.b2 && o.b2->GetType() == b2_staticBody; };
    for (auto& o : objects) if (isStatic(*o) && o->zOrder < 5) DrawPhysObject(*o);
    for (auto& o : objects) if (!o->isBird && !isStatic(*o) && !o->drawWithBirds && o->zOrder < 5) DrawPhysObject(*o);
    // each of the six lists: its dots go through TRAIL_WHITE_1, _2, _3 and start again, then its puffs (BIRD_SPECIAL)
    state_.angle = 0; state_.pivotX = state_.pivotY = 0;
    for (int list = 0; list < 6; list++) {
        for (size_t i = 0; i < trajDots_[list].size(); i++) {
            const SpriteSheet* sheet = nullptr;
            const Sprite* sp = sprites_.Find("TRAIL_WHITE_" + std::to_string(i % 3 + 1), &sheet);
            if (sp) DrawSpriteAt(*sheet, *sp, trajDots_[list][i].x, trajDots_[list][i].y, 4, 3, false, 0, 0);
        }
        for (const TrajectoryPoint& p : trajPuffs_[list]) {
            const SpriteSheet* sheet = nullptr;
            const Sprite* sp = sprites_.Find("BIRD_SPECIAL", &sheet);
            if (sp) DrawSpriteAt(*sheet, *sp, p.x, p.y, 4, 3, false, 0, 0);
        }
    }
    for (auto& o : objects) if ((o->isBird || o->drawWithBirds) && o->zOrder < 5) DrawPhysObject(*o);
    for (auto& o : objects) if (o->zOrder >= 5) DrawPhysObject(*o);
    if (IsSeasonsScripts()) { if (!levelParticlesByScript_) DrawParticles(); }
    else DrawParticlesOf(true, 0);
    state_ = RenderState();
}

// ---- particles (Particles class at 0x41c260; addParticles 0x41c670, update 0x41cec0, draw 0x41d0c0)

// The definition is read from particleTable.particles[type] the first time the type is used.
const GamePlatform::ParticleDef* GamePlatform::ParticleDefFor(const std::string& type) {
    auto found = particleDefs_.find(type);
    if (found != particleDefs_.end()) return &found->second;
    if (!scripts_) return nullptr;
    lua_State* L = scripts_->State();
    int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, "ab.env");
    lua_getfield(L, -1, "particleTable");
    if (!lua_istable(L, -1)) { lua_settop(L, top); return nullptr; }
    lua_getfield(L, -1, "particles");
    if (!lua_istable(L, -1)) { lua_settop(L, top); return nullptr; }
    lua_getfield(L, -1, type.c_str());
    if (!lua_istable(L, -1)) { lua_settop(L, top); return nullptr; }
    int t = lua_gettop(L);
    ParticleDef d;
    GetNumber(L, t, "gravityX", d.gravityX);
    GetNumber(L, t, "gravityY", d.gravityY);
    // 1.x has one speed range for both axes, Seasons has one for each
    d.seasons = GetNumber(L, t, "minVelX", d.minVelX);
    GetNumber(L, t, "maxVelX", d.maxVelX);
    GetNumber(L, t, "minVelY", d.minVelY);
    GetNumber(L, t, "maxVelY", d.maxVelY);
    if (!d.seasons) {
        GetNumber(L, t, "minVel", d.minVelX);
        GetNumber(L, t, "maxVel", d.maxVelX);
        d.minVelY = d.minVelX; d.maxVelY = d.maxVelX;
    }
    lua_getfield(L, t, "randomizeBirthPosition");
    if (lua_isboolean(L, -1)) d.randomizeBirth = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    if (!GetNumber(L, t, "angleMinEmitter", d.angleMinEmitter) || !GetNumber(L, t, "angleMaxEmitter", d.angleMaxEmitter))
        d.angleMinEmitter = d.angleMaxEmitter = 0;
    // 0x42ad10 (2.0.0) reads minAngleEmitter / maxAngleEmitter; without them the angle range is 0..360
    if (!d.seasons && GetNumber(L, t, "minAngleEmitter", d.angleMinEmitter) && GetNumber(L, t, "maxAngleEmitter", d.angleMaxEmitter)) d.v2Emitter = true;
    lua_getfield(L, t, "roundExplosions");
    if (lua_isboolean(L, -1)) d.roundExplosions = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    GetNumber(L, t, "minAngleVel", d.minAngleVel);
    GetNumber(L, t, "maxAngleVel", d.maxAngleVel);
    GetNumber(L, t, "minScaleBegin", d.minScaleBegin);
    GetNumber(L, t, "maxScaleBegin", d.maxScaleBegin);
    GetNumber(L, t, "minScaleEnd", d.minScaleEnd);
    GetNumber(L, t, "maxScaleEnd", d.maxScaleEnd);
    GetNumber(L, t, "lifeTime", d.lifeTime);
    lua_getfield(L, t, "animation");
    if (lua_type(L, -1) == LUA_TSTRING) d.animated = std::string(lua_tostring(L, -1)) == "lifeTime";
    lua_pop(L, 1);
    lua_getfield(L, t, "sprites");
    if (lua_istable(L, -1)) {
        int n = (int)lua_objlen(L, -1);
        for (int i = 1; i <= n; i++) {
            lua_rawgeti(L, -1, i);
            if (lua_type(L, -1) == LUA_TSTRING) d.sprites.push_back(lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    lua_settop(L, top);
    return &(particleDefs_[type] = d);
}

// MSVC's rand() gives 0..32767
static float Rand01() { return (float)(std::rand() & 0x7fff) * 3.0517578e-05f; }

void GamePlatform::AddLevelParticles(const std::string& type, float amount, float x, float y, float w, float h, float angle, bool ignoreLimits) {
    size_t before = particles_.size();
    AddParticles(type, amount, x, y, w, h, angle, ignoreLimits, false);
    // 0x42a850 draws a random layer (1 behind the foreground, 2 in front) for each emit
    for (size_t i = before; i < particles_.size(); i++) { particles_[i].level = true; particles_[i].layer = (std::rand() & 1) + 1; }
}

void GamePlatform::AddParticles(const std::string& type, float amount, float x, float y, float w, float h, float angle, bool ignoreLimits, bool second) {
    if (!ignoreLimits) {
        int count = 0;
        for (const Particle& q : particles_) if (q.second == second && !q.level) count++;
        if ((float)softLimit_ < amount + (float)count) amount *= softFactor_;
        if ((float)hardLimit_ < (float)count + amount) amount = (float)(hardLimit_ - count);
        if (!(amount > 0)) return;
    }
    const ParticleDef* d = ParticleDefFor(type);
    if (!d || d->sprites.empty()) return;
    float s = std::sin(angle), c = std::cos(angle);
    for (int i = 0; (float)i < amount; i++) {
        Particle p;
        p.def = d;
        p.second = second;
        if (d->randomizeBirth) {
            float a = (Rand01() - 0.5f) * w;
            float b = (Rand01() - 0.5f) * h;
            p.x = (x + a * c) - b * s;
            p.y = c * b + s * a + y;
        } else {
            p.x = x; p.y = y;
        }
        if (d->roundExplosions) {
            float speed = (d->maxVelX - d->minVelX) * Rand01() + d->minVelX;
            float a = Rand01() * 6.2831855f;
            p.vx = std::cos(a) * speed;
            p.vy = std::sin(a) * speed;
        } else if (d->v2Emitter) {
            float speed = (d->maxVelX - d->minVelX) * Rand01() + d->minVelX;
            float a = ((d->angleMaxEmitter - d->angleMinEmitter) * Rand01() + d->angleMinEmitter) * 0.017453292f;
            p.vx = std::cos(a) * speed;
            p.vy = std::sin(a) * speed;
        } else {
            p.vx = (d->maxVelX - d->minVelX) * Rand01() + d->minVelX;
            p.vy = (d->maxVelY - d->minVelY) * Rand01() + d->minVelY;
        }
        if (d->seasons || d->v2Emitter) p.angle = ((d->angleMaxEmitter - d->angleMinEmitter) * Rand01() + d->angleMinEmitter) * 0.017453292f;
        else p.angle = Rand01() * 3.1415927f + Rand01() * 3.1415927f;   // 1.x: the exe uses one random number twice
        p.angleVel = (d->maxAngleVel - d->minAngleVel) * Rand01() + d->minAngleVel;
        p.scaleBegin = (d->maxScaleBegin - d->minScaleBegin) * Rand01() + d->minScaleBegin;
        p.scaleEnd = (d->maxScaleEnd - d->minScaleEnd) * Rand01() + d->minScaleEnd;
        p.scale = p.scaleBegin;
        p.age = 0;
        p.lifeTime = d->lifeTime;
        p.frame = 1;
        p.sprite = d->animated ? d->sprites[0] : d->sprites[(size_t)(Rand01() * (float)d->sprites.size())];
        particles_.push_back(p);
    }
}

// Below the ground (y > 0) a particle is removed only in the level; menu particles (enableMenuParticles) stay.
void GamePlatform::UpdateParticles(float dt, bool removeBelowGround, int which) {
    for (size_t i = 0; i < particles_.size();) {
        Particle& p = particles_[i];
        if (which == 0 && p.second) { i++; continue; }
        if (which == 1 && !p.second) { i++; continue; }
        p.age += dt;
        if (p.lifeTime < p.age || (removeBelowGround && 0.0f < p.y)) { particles_.erase(particles_.begin() + i); continue; }
        const ParticleDef& d = *p.def;
        p.vx += d.gravityX * dt;
        p.vy += d.gravityY * dt;
        p.x += dt * p.vx;
        p.y += p.vy * dt;
        p.angle += p.angleVel * dt;
        float t = p.age / p.lifeTime;
        p.scale = (p.scaleEnd - p.scaleBegin) * t + p.scaleBegin;
        if (d.animated) {
            int n = (int)d.sprites.size();
            int frame = (int)std::ceil(t * (float)n);
            if (frame < 1) frame = 1;
            if (frame > n) frame = n;
            if (frame != p.frame) { p.sprite = d.sprites[frame - 1]; p.frame = frame; }
        }
        i++;
    }
}

// Each particle: a sprite at (x, y) / scale, scaled by scale * worldScale, rotated about the
// sprite's pivot, positioned like the blocks (anchors 4 and 3).
// world = false is the menu call (drawParticlesNative): the particles sit in screen coordinates, no camera
// 0x42a9f0: layer 1 is the first list of particles (behind the foreground), layer 2 the second
void GamePlatform::DrawLevelParticles(int layer) {
    levelParticlesByScript_ = true;
    if (IsSeasonsScripts()) DrawParticlesOf(true, layer == 2);
    else DrawParticlesOf(true, -1, layer);
}

void GamePlatform::DrawParticles(bool world) {
    if (!world) menuParticlesByScript_ = true;
    DrawParticlesOf(world, -1);
}

void GamePlatform::DrawParticlesOf(bool world, int second, int levelLayer) {
    if (!backend_) return;
    for (const Particle& p : particles_) {
        if (levelLayer > 0 ? (!p.level || p.layer != levelLayer) : p.level) continue;
        if (second >= 0 && p.second != (second == 1)) continue;
        const SpriteSheet* sheet = nullptr;
        const Sprite* sp = sprites_.Find(p.sprite, &sheet);
        if (!sp) continue;
        state_ = RenderState();
        float cx = world ? camX_ : 0, cy = world ? camY_ : 0, ws = world ? worldScale_ : 1;
        state_.tx = -cx / p.scale; state_.ty = -cy / p.scale;
        state_.sx = state_.sy = p.scale * ws;
        state_.angle = p.angle;
        state_.pivotX = (float)sp->pivotX;
        state_.pivotY = (float)sp->pivotY;
        if (levelLayer > 0) {
            // 0x42a9f0 (2.0.0): the level particles keep their position, so the camera moves them by
            // (position * scale - camera): a smaller particle trails the camera, and layer 2 is twice as near
            state_.sx = state_.sy = p.scale * ws * (float)levelLayer;
            DrawSpriteAt(*sheet, *sp, p.x, p.y, 4, 3, false, 0, 0);
        } else {
            DrawSpriteAt(*sheet, *sp, p.x / p.scale, p.y / p.scale, 4, 3, false, 0, 0);
        }
    }
    state_ = RenderState();
}

}  // namespace ab
