// The engine's Platform: physics and level loading are real, the rest (drawing, audio, input) is
// still the harmless defaults from platform.h.
//
// Usage:
//   GamePlatform platform(dataRoot);
//   ScriptHost scripts(platform, config);
//   platform.Attach(scripts);
//   scripts.LoadScripts(); scripts.CreateStartUpAssets();
//   each frame:  platform.RunFrame(dt);      // = the exe's Game::update (0x408980)
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "lua/lua_frame_host.h"
#include "lua/platform.h"
#include "lua/script_host.h"
#include <map>

#include "audio/audio_mixer.h"
#include "engine/render_backend.h"
#include "engine/font.h"
#include "engine/sprite_sheet.h"
#include "physics/physics_world.h"

namespace ab {

class GamePlatform : public Platform {
public:
    // dataRoot: the folder that contains levels/, such as ".../data". Level paths from the scripts
    // ("levels/pack1/Level1.lua") are looked up under it.
    explicit GamePlatform(const std::string& dataRoot);
    ~GamePlatform() override;

    void Attach(ScriptHost& scripts);
    void RunFrame(float dt);
    // Speed: the time step of every frame is multiplied by the speed (Ctrl+minus / Ctrl+plus change it
    // until the game is closed) and by 2 while Shift is held.
    void SetSpeedBoost(bool on) { speedBoost_ = on; }
    void AddSpeed(float step);

    // Drawing. Without a backend all draw calls are ignored (the tests run that way).
    // BeginFrame clears to the colour the scripts set with setBGColor; call EndFrame after RunFrame.
    void SetBackend(RenderBackend* backend, int screenWidth, int screenHeight);
    // The window changed size: the screen is the window (scripts lay themselves out from screenWidth / screenHeight).
    void Resize(int screenWidth, int screenHeight);
    // requestExit / setFullScreenMode: the window loop reads these
    bool ExitRequested() const { return exitRequested_; }
    bool TakeFullScreenRequest(bool& on) { if (!fullScreenReq_) return false; on = IsFullScreen(); fullScreenReq_ = false; return true; }
    // setResolution(w, h) from a script; the window layer applies it. False when nothing was asked.
    bool TakeResolutionRequest(int& w, int& h);
    void SetResolution(float w, float h) override { reqW_ = (int)w; reqH_ = (int)h; hasReq_ = true; }
    bool CheckForLuaFile(const std::string& path) override;

    // audio: the exe's mixer; the window layer pulls the mixed samples with Audio().Mix()
    AudioMixer& Audio() { return audio_; }
    void ResCreateAudioOutput(float channels, float bits, float rate) override { audio_.CreateOutput((int)channels, (int)bits, (int)rate); }
    void ResStartAudioOutput() override { audio_.Start(); }
    void ResStopAudioOutput() override { audio_.Stop(); }
    void ResCreateAudio(const std::string& path, const std::string& name) override { audio_.AddClip(name, ResolveDataFile(path)); }
    void ResPlayAudio(const std::string& name, float volume, bool loop, int track) override { audio_.Play(name, volume, loop, track); }
    void ResStopAudio(const std::string& name) override { audio_.Stop(name); }
    void ResStopAllAudio() override { audio_.StopAll(); }
    bool ResIsAudioPlaying(const std::string& name) override { return audio_.IsActive(name); }
    void ResSetTrackVolume(float volume, int track) override { audio_.SetTrackVolume(volume, track); }
    float ResGetTrackVolume(int track) override { return audio_.TrackVolume(track); }

    // fonts and text
    void ResCreateBitmapFont(const std::string& path) override;
    // 0x430320: a font name that was never created leaves the current font as it is
    void ResUseFont(const std::string& name) override { if (fonts_.count(name)) current_ = name; }
    void ResCreateTextGroupSet(const std::string& path) override;
    std::string ResGetString(const std::string& group, const std::string& key) override;
    void ResDrawString(const std::string& group, const std::string& text, float x, float y,
                       const std::string& vAlign, const std::string& hAlign) override;
    float ResGetStringWidth(const std::string& text) override;
    float ResGetFontHeight() override;
    float ResGetFontLeading() override;
    float ResGetFontTracking() override;
    float ResGetFontMaxAscending() override;
    float ResGetFontMaxDescending() override;
    std::vector<std::string> ClipText(const std::string& group, const std::string& text, float maxWidth, float& widest) override;
    void BeginFrame();
    void EndFrame();
    const SpriteLibrary& Sprites() const { return sprites_; }
    PhysicsWorld& Physics() { return physics_; }
    int birdCollisions = 0, blockCollisions = 0;     // callbacks made so far (for tests)
    const std::vector<std::string>& Log() const { return log_; }

    // ---- Platform overrides
    void Print(const std::string& text) override;
    void BeginLevelLoad() override;
    std::string ResolveDataFile(const std::string& path) override;
    // Where settings.lua and highscores.lua live (default: the working folder).
    void SetUserDir(const std::string& dir) { userDir_ = dir; }
    std::string ResolveUserFile(const std::string& file, bool appData) override {
        (void)appData;
        return userDir_.empty() ? file : userDir_ + "/" + file;
    }

    // Sprite sheets: the sprite is looked up by name across every loaded sheet (the sheet argument of
    // getSpriteBounds / getSpritePivot is ignored by the exe); sizes are the raw integers from the file.
    void ResCreateSpriteSheet(const std::string& path) override;
    void ResCreateCompoSpriteSet(const std::string& path) override;
    void ResReleaseCompoSpriteSet(const std::string& path) override { sprites_.ReleaseCompoSet(path); }
    void ResGetCompoSpriteBounds(const std::string& sheet, const std::string& sprite, float& x1, float& y1, float& x2, float& y2) override;
    void ResReleaseSpriteSheet(const std::string& path) override;
    void ResGetSpriteBounds(const std::string& sheet, const std::string& sprite, float& w, float& h) override;
    void ResGetSpritePivot(const std::string& sheet, const std::string& sprite, float& px, float& py) override;

    float CreateBox(const BodyDef& d) override { return physics_.CreateBox(d); }
    float CreateCircle(const BodyDef& d) override { return physics_.CreateCircle(d); }
    float CreatePolygon(const BodyDef& d, const std::vector<float>& v) override { return physics_.CreatePolygon(d, v); }
    bool CreateJoint(JointDef& d) override { return physics_.CreateJoint(d); }
    void SetJointParameters(const JointParams& p) override { physics_.SetJointParameters(p); }
    void CheckJointLimits(const std::string& n) override { physics_.CheckJointLimits(n); }
    void DestroyJoint(const std::string& n) override { physics_.DestroyJoint(n); }
    std::vector<std::string> RemoveObject(const std::string& n) override { return physics_.RemoveObject(n); }
    void ApplyImpulse(const std::string& n, float ix, float iy, float px, float py) override { physics_.ApplyImpulse(n, ix, iy, px, py); }
    void ApplyForce(const std::string& n, float fx, float fy, float px, float py) override { physics_.ApplyForce(n, fx, fy, px, py); }
    bool SetPosition(const std::string& n, float x, float y) override { return physics_.SetPosition(n, x, y); }
    bool SetRotation(const std::string& n, float a, float& normalized) override { return physics_.SetRotation(n, a, normalized); }
    void SetVelocity(const std::string& n, float vx, float vy) override { physics_.SetVelocity(n, vx, vy); }
    void SetAngularVelocity(const std::string& n, float w) override { physics_.SetAngularVelocity(n, w); }
    void SetSleeping(const std::string& n, bool s) override { physics_.SetSleeping(n, s); }
    float GetAngle(const std::string& n) override { return physics_.GetAngle(n); }
    void GetWorldPoint(const std::string& n, float lx, float ly, float& wx, float& wy) override { physics_.GetWorldPoint(n, lx, ly, wx, wy); }
    void SetBGColor(float r, float g, float b) override;
    void RequestExit() override { exitRequested_ = true; }
    void SetFullScreen(bool on) override { Platform::SetFullScreen(on); fullScreenReq_ = true; }
    void SetRenderState(int argc, const float* args) override;
    bool levelParticlesByScript_ = false;
    bool pauseGuard_ = false;
    bool menuParticlesByScript_ = false;      // 2.0.0 draws the level's particles itself, in two layers (drawLevelParticlesNative)
    void SetAlpha(float a) override { state_.alpha = a; }
    void ResDrawSprite(const std::string& sprite, float x, float y, int vAnchor, int hAnchor, bool sized, float width, float height) override;
    void SetWorldGravity(float x, float y) override { physics_.World().SetGravity(b2Vec2(x, y)); }
    void ClearParticles() override { particles_.clear(); }
    void CreateThemeSprite(const std::string& name, const std::string& sprite, float x, float y, float scaleX, float scaleY, float angle, float speedX, int layer) override;
    void ModifyThemeSprite(const std::string& name, float x, float y, float scaleX, float scaleY, float angle, int layer) override;
    void RemoveThemeSprite(const std::string& name, int layer) override;
    void UpdateThemeSprites(float dt) override;
    bool IsSeasonsScripts();
    void ResSetClipRect(float x, float y, float w, float h) override;
    void DrawRect(float r, float g, float b, float a, float x1, float y1, float x2, float y2, bool inWorld) override;
    void DrawLine2D(float x1, float y1, float x2, float y2, float width, float r, float g, float b, float a) override;

    // ---- the main game's picture (drawBackgroundNative / drawForegroundNative / drawGameNative)
    void SetTopLeft(float x, float y) override { camX_ = x; camY_ = y; }
    // 0x40c6b0: the scripts' world scale also resets the exe's zoom animation state
    void SetWorldScale(float s) override { worldScale_ = s; zoom_ = zoomPrev_ = s; }
    void SetMaxWorldScale(float s) override { zoomMax_ = s; }
    // 0x403b00: one mouse-wheel step (notches, positive = away from the user)
    void Wheel(int notches);
    void SetTheme(const std::string& theme) override;
    void DrawBackground() override;
    void DrawForeground() override;
    void DrawGame() override;
    // 0x4153f0 / 0x415450 / 0x4154b0: six lists of dots and six of puffs; bird i (1..3) writes to list i + set,
    // where set alternates between 0 and 3 each time a new trajectory starts (that set's lists are cleared)
    void StartNewTrajectory() override {
        trajSet_ = trajSet_ + 3 > 3 ? 0 : trajSet_ + 3;
        for (int i = 0; i < 3; i++) { trajDots_[trajSet_ + i].clear(); trajPuffs_[trajSet_ + i].clear(); }
    }
    void AddToTrajectory(float i, float x, float y) override { int n = (int)i + trajSet_; if (n >= 1 && n <= 6) trajDots_[n - 1].push_back({x, y}); }
    void AddPuffToTrajectory(float i, float x, float y) override { int n = (int)i + trajSet_; if (n >= 1 && n <= 6) trajPuffs_[n - 1].push_back({x, y}); }

    // ---- particles (the exe's Particles class, 0x41c260)
    void AddParticles(const std::string& type, float amount, float x, float y, float w, float h, float angle, bool ignoreLimits = false, bool second = false) override;
    void SetParticleSoftLimit(float limit, float factor) override { softLimit_ = (int)limit; softFactor_ = factor; }
    void SetParticleHardLimit(float limit) override { hardLimit_ = (int)limit; }
    void DrawParticles(bool world = true) override;
    void DrawLevelParticles(int layer) override;
    void AddLevelParticles(const std::string& type, float amount, float x, float y, float w, float h, float angle, bool ignoreLimits) override;
    void DrawParticlesOf(bool world, int second, int levelLayer = 0);          // second: -1 both lists, 0 the first, 1 the second

    void SetObjectParameter(const std::string& n, int id, float v) override { physics_.SetObjectParameter(n, id, v); }
    void SetMaterial(const std::string& n, const std::string& m) override { physics_.SetMaterial(n, m); }
    void SetSprite(const std::string& n, const std::string& s) override { physics_.SetSpriteName(n, s); }
    void SetTexture(const std::string& n, const std::string& t) override { physics_.SetTextureName(n, t); }
    void SetPhysicsEnabled(bool on) override { physics_.SetRunning(on); }
    bool IsPhysicsEnabled() override { return physics_.Frame().running; }
    void SetLevelLimits(float minX, float, float maxX, float) override { physics_.SetLevelLimits(minX, maxX); }

private:
    void OnContact(PhysObject& a, PhysObject& b, b2Contact* contact);
    class Host;                       // FrameHost: world step, Lua writes, (no audio / effects yet)
    // The persistent state set by setRenderState (0x408630): translation, scale, angle, pivot.
    struct RenderState {
        float tx = 0, ty = 0, sx = 1, sy = 1, angle = 0, pivotX = 0, pivotY = 0;
        float alpha = 1;                 // 2.0.0: setRenderState's 8th value and setAlpha (render state +0x40)
    };
    struct TextureInfo { int handle = -1, width = 0, height = 0; };
    const TextureInfo* TextureFor(const SpriteSheet& sheet);
    const TextureInfo* TextureFile(const std::string& path, bool repeat);
    void DrawSpriteAt(const SpriteSheet& sheet, const Sprite& sp, float x, float y, int vAnchor, int hAnchor, bool sized, float width, float height);
    const BitmapFont* CurrentFont() const;
    void Transform(float x, float y, float cx, float cy, float& ox, float& oy) const;

    void DrawPhysObject(PhysObject& o);

    struct BgLayer { std::string sprite; float speed = 1, scale = 1; bool looping = true; };
    struct TrajectoryPoint { float x, y; };
    // What particleTable.particles[type] says (read once per type).
    struct ParticleDef {
        float gravityX = 0, gravityY = 0, minVelX = 0, maxVelX = 0, minVelY = 0, maxVelY = 0, minAngleVel = 0, maxAngleVel = 0;
        // Seasons: the emitter angle in degrees gives the particle's start angle; the birth position may be left exact
        bool seasons = false, randomizeBirth = true;
        bool v2Emitter = false;   // 2.0.0: minAngleEmitter/maxAngleEmitter (degrees) give the direction of the velocity, and the spin start
        float angleMinEmitter = 0, angleMaxEmitter = 0;
        float minScaleBegin = 0, maxScaleBegin = 0, minScaleEnd = 0, maxScaleEnd = 0, lifeTime = 0;
        bool animated = false;
        std::vector<std::string> sprites;
    };
    struct Particle {
        const ParticleDef* def = nullptr;
        std::string sprite;
        float x = 0, y = 0, vx = 0, vy = 0, angle = 0, angleVel = 0;
        float scaleBegin = 0, scaleEnd = 0, scale = 0, age = 0, lifeTime = 0;
        int frame = 1;
        bool second = false;                    // the menu list (0x24)
        bool level = false;                     // the level list (0x30), drawn by drawLevelParticlesNative(layer)
        int layer = 1;
    };
    const ParticleDef* ParticleDefFor(const std::string& type);
    void UpdateParticles(float dt, bool removeBelowGround = true, int which = -1);   // which: -1 all, 0 world+level lists, 1 menu list
    std::map<std::string, ParticleDef> particleDefs_;
    std::vector<Particle> particles_;
    int softLimit_ = 40, hardLimit_ = 125;     // constructor defaults (0x41c2fb)
    float softFactor_ = 0.5f;
    float camX_ = 0, camY_ = 0, worldScale_ = 1;
    // the exe's zoom animation (App +0x280..0x298): a wheel step eases from zoomStart_ to zoomTarget_
    void UpdateZoom(float dt);
    float zoom_ = 1, zoomPrev_ = 1, zoomMax_ = 1, zoomStart_ = 1, zoomTarget_ = 1, zoomT_ = -1, zoomDur_ = 0.5f;
    bool exitRequested_ = false, fullScreenReq_ = false;
    std::vector<BgLayer> bgLayers_;
    struct ThemeSprite { std::string name, sprite; float x, y, scaleX, scaleY, angle, speedX; int layer; };
    std::vector<ThemeSprite> themeSprites_;
    void DrawThemeSprites(int layer);
    float themeShift_ = 0;
    std::vector<std::string> fgLayers_;
    float groundR_ = 0, groundG_ = 0, groundB_ = 0;
    std::string themeTexture_;
    std::vector<TrajectoryPoint> trajDots_[6], trajPuffs_[6];
    int trajSet_ = 0;
    const TextureInfo* tileTexture_ = nullptr;          // set while a textured block is drawn

    RenderBackend* backend_ = nullptr;
    int screenWidth_ = 1024, screenHeight_ = 768;
    int reqW_ = 0, reqH_ = 0;
    bool hasReq_ = false;
    float bgR_ = 1, bgG_ = 1, bgB_ = 1;           // 0..1; white until the scripts set it (the first splash is on white)
    RenderState state_;
    std::map<std::string, TextureInfo> textures_;
    ScriptHost* scripts_ = nullptr;
    std::string dataRoot_;
    PhysicsWorld physics_;
    SpriteLibrary sprites_;
    AudioMixer audio_;
    std::map<std::string, BitmapFont> fonts_;          // by file name without folder and extension
    std::map<std::string, TextGroup> textGroups_;
    std::string current_;                               // useFont
    std::unique_ptr<Host> host_;
    std::vector<std::string> log_;
    std::string userDir_;
    float speed_ = 1.0f;
    bool speedBoost_ = false;
};

}  // namespace ab
