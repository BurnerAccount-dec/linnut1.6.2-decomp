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
#include <functional>
#include <string>
#include <vector>

#include "lua/lua_frame_host.h"
#include "lua/platform.h"
#include "lua/script_host.h"
#include <cstdlib>
#include <map>

#include "audio/audio_mixer.h"
#include "engine/render_backend.h"
#include "engine/font.h"
#include "engine/sprite_sheet.h"
#include "physics/physics_world.h"
#include "video/video_player.h"

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
    // playVideo / openURL: the window hands them to the system (the default video player, the browser); unset = only a message
    std::function<void(const std::string&)> openExternal;
    // v2.2: the video plays inside the engine (video/video_player.h). playVideo returns when it is over: videoPump (made by the window) runs
    // the frames until then. A click or a key skips it. Sound comes from the Windows AAC decoder.
    void PlayVideo(const std::string& path) override;
    void StopVideo() override;
    bool IsVideoPlaying() override { return video_ != nullptr; }
    void VideoFrame(double dt);          // advances and draws one frame of the video (the caller swaps the buffers)
    std::function<void()> videoPump;
    std::unique_ptr<VideoPlayer> video_;
    int videoTexture_ = -1, videoTextureW_ = 0, videoTextureH_ = 0;
    void ResOpenURL(const std::string& url) override { if (openExternal) openExternal(url); else std::printf("openURL %s\n", url.c_str()); }
    float ResGetTrackVolume(int track) override { return audio_.TrackVolume(track); }
    void ResSetAudioSpeed(float speed, int track) override { audio_.SetSpeed(speed, track); }
    float ResGetAudioSpeed(int track) override { return audio_.Speed(track); }
    void ResPauseAudio(bool paused, int track) override { audio_.Pause(paused, track); }
    bool ResIsAudioPaused(int track) override { return audio_.Paused(track); }
    void ResSetMasterVolume(float v) override { audio_.SetMasterVolume(v); }
    float ResGetMasterVolume() override { return audio_.MasterVolume(); }
    void ResSetAudioClipVolume(const std::string& n, float v) override { audio_.SetClipVolume(n, v); }
    void ResSetAudioClipSpeed(const std::string& n, float s) override { audio_.SetClipSpeed(n, s); }

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
    bool NeedsSystemCursor() const { return editing_ && !cursorDrawn_; }   // v2.2: an editor whose scripts drew no cursor this frame (2.0.0) is given the system cursor
    void EndFrame();
    const SpriteLibrary& Sprites() const { return sprites_; }
    PhysicsWorld& Physics();      // made on first use: the version of Box2D depends on the game
    // The console of the command prompt window: one typed line (a command such as "box2d 21", or Lua code). Called on the main thread.
    void ConsoleCommand(const std::string& line);
    int requestedBox2D_ = 0;      // 21 or 22 from the console; applied when the next level loads (0 = the game's own)
    // v2.2: frame pacing, set from the console (fps N, vsync on|off) or the command line (--fps, --vsync); main.cpp applies it
    int fpsLimit = 0;             // 0 = not limited
    bool vsync = false;           // off by default: the retail exes were not synchronised to the display either
    bool vsyncChanged = true;
    bool TakeVsyncChange(bool& on) { on = vsync; bool c = vsyncChanged; vsyncChanged = false; return c; }
    int birdCollisions = 0, blockCollisions = 0;     // callbacks made so far (for tests)
    const std::vector<std::string>& Log() const { return log_; }

    // ---- Platform overrides
    void Print(const std::string& text) override;
    void BeginLevelLoad() override;
    std::string ResolveDataFile(const std::string& path) override;
    // The level editor: setEditing(true) while a level is edited, saveLevel(file) writes it over the level file, the folders are made on demand.
    void SetEditing(bool on) override { editing_ = on; }
    void SaveLevel(const std::string& path) override;
    bool FindSavedLevel(const std::string& path, std::vector<uint8_t>& bytes) override;
    void CreateDirectory(const std::string& path) override;
    bool CheckDirectory(const std::string& path) override;
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

    float CreateBox(const BodyDef& d) override { return Physics().CreateBox(d); }
    float CreateCircle(const BodyDef& d) override { return Physics().CreateCircle(d); }
    float CreatePolygon(const BodyDef& d, const std::vector<float>& v) override { return Physics().CreatePolygon(d, v); }
    bool CreateJoint(JointDef& d) override { return Physics().CreateJoint(d); }
    void SetJointParameters(const JointParams& p) override { Physics().SetJointParameters(p); }
    void CheckJointLimits(const std::string& n) override { Physics().CheckJointLimits(n); }
    void DestroyJoint(const std::string& n) override { Physics().DestroyJoint(n); }
    std::vector<std::string> RemoveObject(const std::string& n) override { return Physics().RemoveObject(n); }
    void ApplyImpulse(const std::string& n, float ix, float iy, float px, float py) override { Physics().ApplyImpulse(n, ix, iy, px, py); }
    void ApplyForce(const std::string& n, float fx, float fy, float px, float py) override { Physics().ApplyForce(n, fx, fy, px, py); }
    bool SetPosition(const std::string& n, float x, float y) override { return Physics().SetPosition(n, x, y); }
    bool SetRotation(const std::string& n, float a, float& normalized) override { return Physics().SetRotation(n, a, normalized); }
    float GetSpeed(const std::string& n) override { return Physics().GetSpeed(n); }
    bool HasMovingObjects(float tol) override { return Physics().HasMovingObjects(tol); }
    void GetLocalPoint(const std::string& n, float wx, float wy, float& lx, float& ly) override { Physics().GetLocalPoint(n, wx, wy, lx, ly); }
    void SetCollisionEnabled(const std::string& n, bool on) override { Physics().SetCollisionEnabled(n, on); }
    void SetColliderType(const std::string& n, int type) override { Physics().SetColliderType(n, type); }
    int ColliderType(const std::string& n) override { return Physics().ColliderType(n); }
    void SetObjectAlpha(const std::string& n, float a) override { Physics().SetObjectAlpha(n, a); }
    float ObjectAlpha(const std::string& n) override { return Physics().ObjectAlpha(n); }
    void SetObjectZOrder(const std::string& n, float z) override { Physics().SetObjectZOrder(n, z); }
    float ObjectZOrder(const std::string& n) override { return Physics().ObjectZOrder(n); }
    void SetColliderRule(int a, int b, bool collide) override { Physics().SetColliderRule(a, b, collide); }
    bool ColliderRule(int a, int b) override { return Physics().ColliderRule(a, b); }
    void SetPolygonRadius(float r) override { Physics().SetPolygonRadius(r); }
    float PolygonRadius() override { return Physics().PolygonRadius(); }
    void RemoveJointsFromObject(const std::string& n) override { Physics().RemoveJointsFromObject(n); }
    std::vector<std::string> IntersectingObjects(float x, float y, float l, float r, float d, float u) override { return Physics().IntersectingObjects(x, y, l, r, d, u); }
    void SetVelocity(const std::string& n, float vx, float vy) override { Physics().SetVelocity(n, vx, vy); }
    void SetAngularVelocity(const std::string& n, float w) override { Physics().SetAngularVelocity(n, w); }
    void SetSleeping(const std::string& n, bool s) override { Physics().SetSleeping(n, s); }
    float GetAngle(const std::string& n) override { return Physics().GetAngle(n); }
    void GetWorldPoint(const std::string& n, float lx, float ly, float& wx, float& wy) override { Physics().GetWorldPoint(n, lx, ly, wx, wy); }
    void SetBGColor(float r, float g, float b) override;
    void RequestExit() override { exitRequested_ = true; }
    void SetFullScreen(bool on) override { Platform::SetFullScreen(on); fullScreenReq_ = true; }
    void SetRenderState(int argc, const float* args) override;
    bool levelParticlesByScript_ = false;
    bool pauseGuard_ = false;
    bool editing_ = false;
    bool cursorDrawn_ = false;
    bool gameDrawn_ = false, gameDrawnLast_ = false;   // Rio: drawGameNative ran this / the last frame (the particles of a level end with it)
    bool menuParticlesByScript_ = false;      // 2.0.0 draws the level's particles itself, in two layers (drawLevelParticlesNative)
    void SetAlpha(float a) override { state_.alpha = a < 0 ? 0 : (a > 1 ? 1 : a); }
    void SetTint(float r, float g, float b) override { state_.tintR = r < 0 ? 0 : r; state_.tintG = g < 0 ? 0 : g; state_.tintB = b < 0 ? 0 : b; }
    void GetTint(float& r, float& g, float& b) override { r = state_.tintR; g = state_.tintG; b = state_.tintB; }
    void SetRenderExtras(int what, float x, float y) override { if (what == 0) { state_.skewX = x; state_.skewY = y; } else if (what == 1) { state_.bendX = x; state_.bendY = y; } else if (what == 2) state_.z = x; else if (what == 3) state_.blur = x < 0 ? 0 : x; else state_.pixelate = x < 0 ? 0 : x; }
    void ResDrawSprite(const std::string& sprite, float x, float y, int vAnchor, int hAnchor, bool sized, float width, float height) override;
    void SetWorldGravity(float x, float y) override { Physics().SetGravity(x, y); }
    void SetMaxTranslation(float t) override { Physics().SetMaxTranslation(t); }
    void ClearParticles() override { particles_.clear(); }
    void CreateThemeSprite(const std::string& name, const std::string& sprite, float x, float y, float scaleX, float scaleY, float angle, float speedX, int layer) override;
    void ModifyThemeSprite(const std::string& name, float x, float y, float scaleX, float scaleY, float angle, int layer) override;
    void RemoveThemeSprite(const std::string& name, int layer) override;
    void UpdateThemeSprites(float dt) override;
    void CreateThemeSpriteRio(const std::string& name, const std::string& sprite, float x, float y, float scaleX, float scaleY, float angle, int layer, float angleVel, bool flipped) override;
    void SetThemeSprite(const std::string& name, const std::string& sprite, int layer) override;
    void RotateThemeSprites(float dt) override;
    void ScrollThemeLayers(float dt, bool force) override;
    bool IsSeasonsScripts();
    void ResSetClipRect(float x, float y, float w, float h) override;
    void CanvasInit(int w, int h) override;
    void CanvasClear(float r, float g, float b, float a) override;
    void CanvasPaint(const std::string& brush, float x, float y, float w, float h) override;
    void CanvasDraw(const std::string& mask) override;
    void CanvasDominant(std::string& first, std::string& second) override;
    void SetupCompoObject(const std::string& object, const std::vector<CompoItem>& items, float scale) override;
    bool CanvasMakeSprite(const std::string& name, int width, int height, int pivotX, int pivotY, bool save, const std::string& file) override;
    bool CanvasLoadSprite(const std::string& name, const std::string& file, int pivotX, int pivotY) override;
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
        for (std::string& name : trajCustom_[trajSet_ / 3]) name.clear();
    }
    void SetCustomTrajectorySprites(const std::string& a, const std::string& b, const std::string& c, const std::string& d) override {
        trajCustom_[trajSet_ / 3][0] = a; trajCustom_[trajSet_ / 3][1] = b; trajCustom_[trajSet_ / 3][2] = c; trajCustom_[trajSet_ / 3][3] = d;
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

    void SetObjectParameter(const std::string& n, int id, float v) override { Physics().SetObjectParameter(n, id, v); }
    void SetMaterial(const std::string& n, const std::string& m) override { Physics().SetMaterial(n, m); }
    // a Hatchery bird (setupCompoObject) keeps its own sprites when the script gives it its flying, special or dead one
    void SetSprite(const std::string& n, const std::string& s) override {
        PhysObject* o = Physics().Find(n);
        if (o && o->sprite.compare(0, 6, "compo:") == 0 && s.compare(0, 6, "compo:") != 0) return;
        Physics().SetSpriteName(n, s);
    }
    void SetTexture(const std::string& n, const std::string& t) override { Physics().SetTextureName(n, t); }
    void SetPhysicsEnabled(bool on) override { Physics().SetRunning(on); }
    bool IsPhysicsEnabled() override { return Physics().Frame().running; }
    void SetLevelLimits(float minX, float, float maxX, float) override { Physics().SetLevelLimits(minX, maxX); }

private:
    void OnContact(PhysObject& a, PhysObject& b, ContactHandle& contact);
    class Host;                       // FrameHost: world step, Lua writes, (no audio / effects yet)
    // The persistent state set by setRenderState (0x408630): translation, scale, angle, pivot.
    struct RenderState {
        float tx = 0, ty = 0, sx = 1, sy = 1, angle = 0, pivotX = 0, pivotY = 0;
        bool flipX = false;              // Rio: the sprite is drawn mirrored
        float alpha = 1;                 // 2.0.0: setRenderState's 8th value and setAlpha (render state +0x40)
        // v2.2 additions to setRenderState (arguments 9 to 14): shear and bend of what is drawn, and a draw order for sprites
        float skewX = 0, skewY = 0;      // x += skewX * (y - pivot y); y += skewY * (x - pivot x), before the rotation
        float bendX = 0, bendY = 0;      // sprites: the sideways shift (pixels) of the middle of the sprite; bendX moves rows, bendY moves columns
        float bendProfile[9] = {0, 0.383f, 0.707f, 0.924f, 1, 0.924f, 0.707f, 0.383f, 0};   // the shape along the sprite, 9 samples (default: an arch)
        float tintR = 1, tintG = 1, tintB = 1;   // v2.2: sprites are multiplied by this colour (setRenderState arguments 15 to 17, setTint)
        float blur = 0;       // v2.2: sprites are drawn 9 times spread over this many pixels (setRenderState argument 18, setBlur)
        float pixelate = 0;   // v2.2: sprites are drawn in blocks of this many sprite pixels (setRenderState argument 19, setPixelate)
        float z = 0;                     // sprites with z != 0 wait and are drawn sorted by z when the next other drawing starts
    };
    struct QueuedQuad { float z; int seq; int tex; float xy[8], uv[8], alpha, r, g, b; };
    std::vector<QueuedQuad> zQueue_;
    void FlushZ();                       // draws the waiting sprites, lowest z first (equal z: in the order they were drawn)
    struct TextureInfo { int handle = -1, width = 0, height = 0; };
    const TextureInfo* TextureFor(const SpriteSheet& sheet);
    const TextureInfo* TextureFile(const std::string& path, bool repeat);
    void DrawSpriteAt(const SpriteSheet& sheet, const Sprite& sp, float x, float y, int vAnchor, int hAnchor, bool sized, float width, float height);
    const BitmapFont* CurrentFont() const;
    void Transform(float x, float y, float cx, float cy, float& ox, float& oy) const;

    void DrawPhysObject(PhysObject& o);
    void SelectRioAnimations(float dt);          // Rio: the exe asks the script which animation each animated object plays
    void UpdateRioSquash(float dt);              // Rio: the wobble of an object's scale after a hit, and the marmosets' turning
    void UpdateRioGettingUp(float dt);           // Rio: a marmoset lying on its side stands up again
    void UpdateRioBirdFaces(float dt);           // Rio: waiting birds blink, the bird pulled back in the slingshot yells
    struct BirdFace { float wait = 1.0f + 4.0f * (std::rand() % 1000) / 1000.0f, blink = 0.0f; };
    std::map<std::string, BirdFace> birdFaces_;

    struct BgLayer { std::string sprite; float speed = 1, scale = 1; bool looping = true; float autoScroll = 0, offset = 0; };
    struct TrajectoryPoint { float x, y; };
    // What particleTable.particles[type] says (read once per type).
    struct ParticleDef {
        float gravityX = 0, gravityY = 0, minVelX = 0, maxVelX = 0, minVelY = 0, maxVelY = 0, minAngleVel = 0, maxAngleVel = 0;
        // Seasons: the emitter angle in degrees gives the particle's start angle; the birth position may be left exact
        bool seasons = false, randomizeBirth = true;
        bool v2Emitter = false;   // 2.0.0: minAngleEmitter/maxAngleEmitter (degrees) give the direction of the velocity, and the spin start
        bool roundExplosions = false;   // Seasons 2.2.0 (0x428658): minVelX..maxVelX is a speed and the direction is random over the full circle
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
    struct ThemeSprite { std::string name, sprite; float x, y, scaleX, scaleY, angle, speedX; int layer; float angleVel = 0; bool flipped = false; };
    std::vector<ThemeSprite> themeSprites_;
    std::map<std::string, std::string> savedLevels_;   // v2.2: levels saved by the editor this session, by path (see FindSavedLevel)
    void DrawThemeSprites(int layer);
    float themeShift_ = 0;
    std::vector<std::string> fgLayers_;
    std::vector<float> fgSpeed_, fgScale_, fgAuto_, fgOffset_;      // per foreground layer: parallax speed and scale (entries 3 and 4; 1 and 1.5 when absent)
    float groundR_ = 0, groundG_ = 0, groundB_ = 0;
    std::string themeTexture_;
    std::vector<TrajectoryPoint> trajDots_[6], trajPuffs_[6];
    std::string trajCustom_[2][4];      // per set: the three dot sprites and the puff sprite set by setCustomTrajectorySprites (empty = the defaults)
    int trajSet_ = 0;
    const TextureInfo* tileTexture_ = nullptr;          // set while a textured block is drawn

    RenderBackend* backend_ = nullptr;
    int screenWidth_ = 1024, screenHeight_ = 768;
    bool sizeKnown_ = false;
    int reqW_ = 0, reqH_ = 0;
    bool hasReq_ = false;
    float bgR_ = 1, bgG_ = 1, bgB_ = 1;           // 0..1; white until the scripts set it (the first splash is on white)
    RenderState state_;
    struct PendingCursor { std::string name; float x, y; int vAnchor, hAnchor; bool sized; float width, height; RenderState st; };
    bool drawingCursor_ = false;
    std::vector<PendingCursor> cursors_;   // v2.2: the hand cursors of this frame, drawn at the very end so nothing covers them
    std::map<std::string, TextureInfo> textures_;
    ScriptHost* scripts_ = nullptr;
    std::string dataRoot_;
    std::unique_ptr<PhysicsWorld> physics_;
    SpriteLibrary sprites_;
    // the Hatchery's egg painter canvas: colour, which brush colour painted each pixel, the pixels the egg mask shows
    struct PaintCanvas {
        int w = 0, h = 0;
        std::vector<uint8_t> rgba, owner, visible;
        std::vector<std::string> brushes;      // owner value i + 1 is brushes[i] (the colour name from H_PAINT_BRUSH_<COLOR>)
        std::string mask;                      // the mask sprite of the last draw
        float scaleX = 1, scaleY = 1;          // its scale then: canvas pixels per mask pixel
        int texture = -1, texW = 0, texH = 0;
        int revision = 0, analysed = -1, composedRev = -1;
        std::string first, second;
    } canvas_;
    std::map<std::string, Image> brushImages_;   // the pixels of the sheets brushes are cut from
    const Image* SheetImage(const SpriteSheet& sheet);
    bool ComposeCanvas(const std::string& mask, int outW, int outH, float scaleX, float scaleY, Image& out, bool markVisible);
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
