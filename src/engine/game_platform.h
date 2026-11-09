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
#include <list>
#include <array>
#include <memory>
#include <functional>
#include <string>
#include <vector>

#include "lua/lua_frame_host.h"
#include "lua/platform.h"
#include "lua/script_host.h"
#include <cstdlib>
#include <cmath>
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
    void ResSetAudioClipPitch(const std::string& n, float v) override { audio_.SetClipPitch(n, v); }
    void ResSetAudioClipTempo(const std::string& n, float v) override { audio_.SetClipTempo(n, v); }
    void ResSetAudioClipPan(const std::string& n, float v) override { audio_.SetClipPan(n, v); }
    void ResResetAudioClipEffects(const std::string& n) override { audio_.ResetClipEffects(n); }
    bool ResSeekAudioClip(const std::string& n, double s) override { return audio_.SeekClip(n, s); }
    double ResAudioClipTime(const std::string& n) override { return audio_.ClipTime(n); }
    double ResAudioClipLength(const std::string& n) override { return audio_.ClipLength(n); }

    // fonts and text
    void ResCreateBitmapFont(const std::string& path) override;
    // 0x430320: a font name that was never created leaves the current font as it is
    void ResUseFont(const std::string& name) override { if (fonts_.count(name)) current_ = name; }
    void ResCreateTextGroupSet(const std::string& path) override;
    std::string ResGetString(const std::string& group, const std::string& key) override;
    void ResSetString(const std::string& group, const std::string& key, const std::string& text) override { textGroups_[group].strings[key] = text; }
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
    void ResCaptureSprite(const std::string& name) override;
    void ResKeepLastFrame(bool on) override { keepLast_ = on; if (!on) lastFrame_.clear(); }
    bool keepLast_ = false; int quadsThisFrame_ = 0; std::vector<unsigned char> lastFrame_; int lastW_ = 0, lastH_ = 0;
    void SetObjectDensity(const std::string& n, float density) override;
    void SetObjectFriction(const std::string& n, float f) override;
    void ResizeObjectCircle(const std::string& n, float r) override;
    void SetObjectRestitution(const std::string& n, float r) override;
    void AddParticlesMode(const std::string& type, float amount, float x, float y, float w, float h, float angle, int mode) override;
    void DrawParticlesFiltered(int filter) override;
    void ResGetSpriteBounds(const std::string& sheet, const std::string& sprite, float& w, float& h) override;
    void ResGetSpritePivot(const std::string& sheet, const std::string& sprite, float& px, float& py) override;

    float CreateBox(const BodyDef& d) override { return Physics().CreateBox(d); }
    float CreateCircle(const BodyDef& d) override { return Physics().CreateCircle(d); }
    float CreatePolygon(const BodyDef& d, const std::vector<float>& v) override { return Physics().CreatePolygon(d, v); }
    float CreateLine(const BodyDef& d, const std::vector<float>& v) override { return Physics().CreateLine(d, v); }
    float CreateCompound(const BodyDef& d, const std::vector<CompoundShape>& s, float damping) override { return Physics().CreateCompound(d, s, damping); }
    bool CreateJoint(JointDef& d) override { return Physics().CreateJoint(d); }
    void SetJointParameters(const JointParams& p) override { Physics().SetJointParameters(p); }
    void CheckJointLimits(const std::string& n, bool stop = false) override { Physics().CheckJointLimits(n, stop); }
    void DestroyJoint(const std::string& n) override { Physics().DestroyJoint(n); }
    std::vector<std::string> RemoveObject(const std::string& n) override { return Physics().RemoveObject(n); }
    CollisionPrediction PredictCollision(const std::string& a, const std::string& b, float restitution, const float* vel = nullptr) override { return Physics().PredictCollision(a, b, restitution, vel); }
    bool SetOwnGravityVector(const std::string& n, float gx, float gy) override { return Physics().SetOwnGravityVector(n, gx, gy); }
    bool SetOwnGravityScale(const std::string& n, float scale) override { return Physics().SetOwnGravityScale(n, scale); }
    bool ResetOwnGravity(const std::string& n) override { return Physics().ResetOwnGravity(n); }
    void RayCastObjects(float x1, float y1, float x2, float y2, std::vector<RayCastHit>& out) override {
        std::vector<RayHit> hits;
        Physics().RayCastAll(x1, y1, x2, y2, hits);
        for (const RayHit& h : hits) out.push_back({h.obj->name, h.px, h.py, h.nx, h.ny, h.fraction});
    }
    bool GetOwnGravity(const std::string& n, int& mode, float& gx, float& gy, float& scale) override { return Physics().GetOwnGravity(n, mode, gx, gy, scale); }
    void ApplyImpulse(const std::string& n, float ix, float iy, float px, float py) override { Physics().ApplyImpulse(n, ix, iy, px, py); }
    void ApplyForce(const std::string& n, float fx, float fy, float px, float py) override { Physics().ApplyForce(n, fx, fy, px, py); }
    bool SetPosition(const std::string& n, float x, float y) override { return Physics().SetPosition(n, x, y); }
    bool SetRotation(const std::string& n, float a, float& normalized) override { return Physics().SetRotation(n, a, normalized); }
    float GetSpeed(const std::string& n) override { return Physics().GetSpeed(n); }
    void SetRecordVelocity(const std::string& n, bool on) override { Physics().SetRecordVelocity(n, on); }
    bool GetVelocity(const std::string& n, float& vx, float& vy) override { return Physics().GetVelocity(n, vx, vy); }
    bool HasMovingObjects(float tol) override { SyncTuningOnly(); return Physics().HasMovingObjects(tol == 0.5f ? Physics().Tn("moving.tolerance", 0.5f) : tol == 0.001f ? Physics().Tn("moving.zeroTolerance", 0.001f) : tol); }
    void GetLocalPoint(const std::string& n, float wx, float wy, float& lx, float& ly) override { Physics().GetLocalPoint(n, wx, wy, lx, ly); }
    void SetCollisionEnabled(const std::string& n, bool on) override { Physics().SetCollisionEnabled(n, on); }
    void SetColliderType(const std::string& n, int type) override { Physics().SetColliderType(n, type); }
    int ColliderType(const std::string& n) override { return Physics().ColliderType(n); }
    float GetSpriteAngle(const std::string& n) override { PhysObject* o = Physics().Find(n); return o ? o->spriteAngle : 0.0f; }
    void SetDecoration(const std::string& n, const std::string& sprite, int amount, float increment, float scale) override { Physics().SetDecoration(n, sprite, amount, increment, scale); }
    void SetSpriteLook(const std::string& n, int what, float a, float b) override { Physics().SetSpriteLook(n, what, a, b); }
    void SetObjectScale(const std::string& n, float sx, float sy) override { Physics().SetObjectScale(n, sx, sy); }
    bool GetObjectScale(const std::string& n, float& sx, float& sy) override { return Physics().GetObjectScale(n, sx, sy); }
    void SetSpeedCap(const std::string& n, float speed, float vx, float vy) override { Physics().SetSpeedCap(n, speed, vx, vy); }
    bool GetSpeedCap(const std::string& n, float& speed, float& vx, float& vy) override { return Physics().GetSpeedCap(n, speed, vx, vy); }
    std::vector<std::string> TouchingObjects(const std::string& n) override { return Physics().TouchingObjects(n); }
    void SetObjectAlpha(const std::string& n, float a) override { Physics().SetObjectAlpha(n, a); }
    float ObjectAlpha(const std::string& n) override { return Physics().ObjectAlpha(n); }
    void SetObjectZOrder(const std::string& n, float z) override { Physics().SetObjectZOrder(n, z); }
    float ObjectZOrder(const std::string& n) override { return Physics().ObjectZOrder(n); }
    float MoveObjectZ(const std::string& n, bool front) override { return Physics().MoveObjectZ(n, front); }
    std::vector<std::string> GameDrawOrder() override;
    float RenderZ() override { return state_.z; }
    void SyncObjectZ(const std::string& name);              // writes the z_order of an object into objects.world[name]
    std::vector<PhysObject*> DrawGroup(int which);        // v2.2.3: the objects of one of the four groups of DrawGame (0 statics, 1 blocks, 2 birds, 3 z >= 5), in drawing order
    void SetColliderRule(int a, int b, bool collide) override { Physics().SetColliderRule(a, b, collide); }
    bool ColliderRule(int a, int b) override { return Physics().ColliderRule(a, b); }
    void SetPolygonRadius(float r) override { Physics().SetPolygonRadius(r); }
    float PolygonRadius() override { return Physics().PolygonRadius(); }
    void RemoveJointsFromObject(const std::string& n) override { Physics().RemoveJointsFromObject(n); }
    std::vector<std::string> IntersectingObjects(float x, float y, float l, float r, float d, float u) override { return Physics().IntersectingObjects(x, y, l, r, d, u); }
    void SetVelocity(const std::string& n, float vx, float vy) override { Physics().SetVelocity(n, vx, vy); }
    void SetAngularVelocity(const std::string& n, float w) override { Physics().SetAngularVelocity(n, w); }
    bool BodyCommand(const std::string& n, int cmd, float a, float& result) override;   // cmds 20..24: the Space aiming aid
    bool SpriteExists(const std::string& name) override { return sprites_.Find(name) != nullptr; }
    bool AimSimulate(const std::string& bird, float vx, float vy, int points, const float* start = nullptr) override;
    void SetSleeping(const std::string& n, bool s) override { Physics().SetSleeping(n, s); }
    float GetAngle(const std::string& n) override { return Physics().GetAngle(n); }
    void GetWorldPoint(const std::string& n, float lx, float ly, float& wx, float& wy) override { Physics().GetWorldPoint(n, lx, ly, wx, wy); }
    void SetBGColor(float r, float g, float b) override;
    void GetBGColor(float& r, float& g, float& b) override { r = std::floor(bgR_ * 255.0f + 0.5f); g = std::floor(bgG_ * 255.0f + 0.5f); b = std::floor(bgB_ * 255.0f + 0.5f); }
    void RequestExit() override { exitRequested_ = true; }
    void SetFullScreen(bool on) override { Platform::SetFullScreen(on); fullScreenReq_ = true; }
    void SetRenderState(int argc, const float* args) override;
    void GetRenderTranslation(float& tx, float& ty) override { tx = state_.tx; ty = state_.ty; }
    bool levelParticlesByScript_ = false;
    bool pauseGuard_ = false;
    bool editing_ = false;
    bool cursorDrawn_ = false;
    bool gameDrawn_ = false, gameDrawnLast_ = false;   // Rio: drawGameNative ran this / the last frame (the particles of a level end with it)
    bool menuParticlesByScript_ = false;      // 2.0.0 draws the level's particles itself, in two layers (drawLevelParticlesNative)
    void SetScalePivot(bool on, float x, float y) override { state_.hasScalePivot = on; state_.spX = x; state_.spY = y; }
    void SetAlpha(float a) override { state_.alpha = a < 0 ? 0 : (a > 1 ? 1 : a); }
    void SetTint(float r, float g, float b) override { state_.tintR = r < 0 ? 0 : r; state_.tintG = g < 0 ? 0 : g; state_.tintB = b < 0 ? 0 : b; }
    void GetTint(float& r, float& g, float& b) override { r = state_.tintR; g = state_.tintG; b = state_.tintB; }
    void SetRenderExtras(int what, float x, float y) override { if (what == 0) { state_.skewX = x; state_.skewY = y; } else if (what == 1) { state_.bendX = x; state_.bendY = y; } else if (what == 2) state_.z = x; else if (what == 3) state_.blur = x < 0 ? 0 : x; else if (what == 5) state_.angle = x; else state_.pixelate = x < 0 ? 0 : x; }
    void ResDrawSprite(const std::string& sprite, float x, float y, int vAnchor, int hAnchor, bool sized, float width, float height) override;
    void DrawTexturedLine2D(const std::string& sprite, float x1, float y1, float x2, float y2, float width, float r, float g, float b, float a) override;
    void SetWorldGravity(float x, float y) override { Physics().SetGravity(x, y); }
    void GetWorldGravity(float& x, float& y) override { Physics().GetGravity(x, y); }
    void SetMaxTranslation(float t) override { Physics().SetMaxTranslation(t); }
    void ClearParticles() override { particles_.clear(); }
    void ClearMenuParticles() override { for (size_t i = 0; i < particles_.size();) { if (particles_[i].second) particles_.erase(particles_.begin() + i); else i++; } }
    void CreateThemeSprite(const std::string& name, const std::string& sprite, float x, float y, float scaleX, float scaleY, float angle, float speedX, int layer) override;
    void ModifyThemeSprite(const std::string& name, float x, float y, float scaleX, float scaleY, float angle, int layer) override;
    void RemoveThemeSprite(const std::string& name, int layer) override;
    void UpdateThemeSprites(float dt) override;
    void CreateThemeSpriteSpace(const std::string& name, const std::string& sprite, float x, float y, float scaleX, float scaleY, float angle, int layer, float scaleSpeed, bool flipped, float velX, float velY) override;
    void CreateThemeSpriteRio(const std::string& name, const std::string& sprite, float x, float y, float scaleX, float scaleY, float angle, int layer, float angleVel, bool flipped) override;
    void SetThemeSprite(const std::string& name, const std::string& sprite, int layer) override;
    void CreateThemeAnimation(const ThemeAnimation& a) override;
    void RotateThemeSprites(float dt) override;
    void StepThemeAnimations(float dt);
    void ScrollThemeLayers(float dt, bool force) override;
    void ScrollWaterLayers(float dt) override { for (size_t i = 0; i < fgWater_.size() && i < fgOffset_.size() && i < fgAuto_.size(); i++) if (fgWater_[i].water) fgOffset_[i] += fgAuto_[i] * dt * 60.0f; waterTime_ += dt; }
    float waterTime_ = 0;
    bool IsSeasonsScripts();
    std::vector<std::string> extraScriptFiles;     // .lua files from the command line (drag and drop), loaded once the game scripts are up
    void ResSetClipRect(float x, float y, float w, float h) override;
    void CanvasInit(int w, int h) override;
    void CanvasClear(float r, float g, float b, float a) override;
    void CanvasPaint(const std::string& brush, float x, float y, float w, float h) override;
    void CanvasDraw(const std::string& mask) override;
    void CanvasDominant(std::string& first, std::string& second) override;
    void SetupCompoObject(const std::string& object, const std::vector<CompoItem>& items, float scale) override;
    bool CanvasMakeSprite(const std::string& name, int width, int height, int pivotX, int pivotY, bool save, const std::string& file) override;
    int CreateSilhouette(const std::string& name, const std::string& source, float r, float g, float b, float a, int outline) override;
    bool CanvasLoadSprite(const std::string& name, const std::string& file, int pivotX, int pivotY) override;
    void DrawRect(float r, float g, float b, float a, float x1, float y1, float x2, float y2, bool inWorld) override;
    void DrawLine2D(float x1, float y1, float x2, float y2, float width, float r, float g, float b, float a) override;

    // ---- the main game's picture (drawBackgroundNative / drawForegroundNative / drawGameNative)
    void SetTopLeft(float x, float y) override { camX_ = x; camY_ = y; }
    // 0x40c6b0: the scripts' world scale also resets the exe's zoom animation state
    // Space / Star Wars keep the wheel zoom apart from the world scale (setWorldScale 0x40d100 only stores the scale; resetMouseWheelScale 0x40d160 sets the zoom)
    void SetWorldScale(float s) override { worldScale_ = s; if (!(scripts_ && scripts_->IsSpace())) zoom_ = zoomPrev_ = s; }
    void ResetMouseWheelScale(float s) override { zoom_ = zoomPrev_ = s; }
    void SetMaxWorldScale(float s) override { zoomMax_ = s; }
    // 0x403b00: one mouse-wheel step (notches, positive = away from the user)
    void Wheel(int notches);
    void SetWheelFine(bool on) { wheelFine_ = on; }   // Shift held: the wheel zooms in 5% steps (0x403b00 scales its step by 0.05 when its flag at +0x236 is set)
    bool wheelFine_ = false;
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
        // Space: the sprites the script chose for the bird (setNormalTrailSprite / setSpecialTrailSprite) belong to the trajectory that starts now
        if (!pendingNormalTrail_.empty()) for (int i = 0; i < 3; i++) trajCustom_[trajSet_ / 3][i] = pendingNormalTrail_;
        if (!pendingSpecialTrail_.empty()) trajCustom_[trajSet_ / 3][3] = pendingSpecialTrail_;
    }
    void SetTrailSprite(bool special, const std::string& name) override { (special ? pendingSpecialTrail_ : pendingNormalTrail_) = name; }
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
    void AddParticlesOverride(const std::string& type, float amount, float x, float y, float w, float h, float angle, const ParticleOverride& ov) override;
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
    void SetLevelLimits(float minX, float minY, float maxX, float maxY) override { Physics().SetLevelLimits(minX, maxX); Physics().SetLevelLimitsY(minY, maxY); }

private:
    void OnContact(PhysObject& a, PhysObject& b, ContactHandle& contact);
    void UpdateKillTimers(float dt);
    void SyncTuning(int& velIter, int& posIter);
    // Lua access to the theme layers and the camera (getCamera, getThemeLayers, setThemeLayer)
    void GroundColor(bool set, float& r, float& g, float& b) override { if (set) { groundR_ = r; groundG_ = g; groundB_ = b; } else { r = groundR_; g = groundG_; b = groundB_; } }
    void ClearParticleCache() override { oldParticleDefs_.push_back(std::move(particleDefs_)); particleDefs_.clear(); }   // live particles keep pointing at their old definition
    void GetCamera(float& x, float& y, float& ws, int& w, int& h) override { x = camX_; y = camY_; ws = worldScale_; w = screenWidth_; h = screenHeight_; }
    int ThemeLayerCount(int which) override { return which == 2 ? (int)fgLayers_.size() : (int)(which == 1 ? ntFg_ : bgLayers_).size(); }
    bool GetThemeLayer(int which, int i, ThemeLayerInfo& o) override;
    bool SetThemeLayer(int which, int i, const ThemeLayerInfo& in) override;
    void SyncTuningOnly() { int a = 0, b = 0; SyncTuning(a, b); }
    void OnContactStarWars(PhysObject& a, PhysObject& b, ContactHandle& contact);   // 0x42f9d0
    class Host;                       // FrameHost: world step, Lua writes, (no audio / effects yet)
    // The persistent state set by setRenderState (0x408630): translation, scale, angle, pivot.
    struct RenderState {
        float tx = 0, ty = 0, sx = 1, sy = 1, angle = 0, pivotX = 0, pivotY = 0;
        bool hasScalePivot = false; float spX = 0, spY = 0;   // the pivot argument of a drawing call: the scale grows from here
        bool flipX = false;              // Rio: the sprite is drawn mirrored
        float alpha = 1;                 // 2.0.0: setRenderState's 8th value and setAlpha (render state +0x40)
        // v2.2 additions to setRenderState (arguments 9 to 14): shear and bend of what is drawn, and a draw order for sprites
        float skewX = 0, skewY = 0;      // x += skewX * (y - pivot y); y += skewY * (x - pivot x), before the rotation
        float bendX = 0, bendY = 0;      // sprites: the sideways shift (pixels) of the middle of the sprite; bendX moves rows, bendY moves columns
        float bendProfile[9] = {0, 0.383f, 0.707f, 0.924f, 1, 0.924f, 0.707f, 0.383f, 0};   // the shape along the sprite, 9 samples (default: an arch)
        float tintR = 1, tintG = 1, tintB = 1;   // v2.2: sprites are multiplied by this colour (setRenderState arguments 15 to 17, setTint)
        float blur = 0;       // v2.2: sprites are drawn 9 times spread over this many pixels (setRenderState argument 18, setBlur)
        float pixelate = 0;   // v2.2: sprites are drawn in blocks of this many sprite pixels (setRenderState argument 19, setPixelate)
        int blend = 0;                   // v2.4: 0 normal, 1 additive, 2 multiply, 3 screen (setBlendMode, setRenderState argument 20)
        int effect = 0;                  // v2.4: 0 none, 1 grayscale, 2 sepia, 3 invert, 4 chromatic, 5 glow (setShader, setRenderState argument 21)
        float z = 0;                     // setRenderState argument 13: the sprite is placed among the game's objects by z (see EmitQuad); 0 = drawn in call order
    };
    // v2.2.1: everything drawn in a frame waits in one list and is sent to the backend by FlushZ (clip changes and the end of the frame).
    // kind 0 = drawn in call order; 1 = a game object (drawGameNative), key = its z_order; 2 = a sprite drawn with a render state z, key = z.
    // Kind 1 and 2 are put in z order against each other (equal z: the order they were drawn); kind 1 keeps its own order, kind 0 never moves.
    struct QueuedQuad { int kind; float key; int tex, mask; float xy[8], uv[8], tuv[8], alpha, r, g, b; int blend; };
    std::vector<QueuedQuad> zQueue_;
    int keyedCount_ = 0, spriteCount_ = 0;   // kind 1 + 2, and kind 2, in zQueue_
    bool drawingNative_ = false;             // DrawGame is drawing an object
    float nativeKey_ = 0;                    // its z_order
    QueuedQuad MakeQuad(int tex, float alpha, float r, float g, float b);
    void EmitQuad(QueuedQuad& q);
    void FlushZ();                       // draws the waiting quads in list order
    struct TextureInfo { int handle = -1, width = 0, height = 0; };
    const TextureInfo* TextureFor(const SpriteSheet& sheet);
    const TextureInfo* TextureFile(const std::string& path, bool repeat);
    void DrawSpriteAt(const SpriteSheet& sheet, const Sprite& sp, float x, float y, int vAnchor, int hAnchor, bool sized, float width, float height);
    void DrawSpriteAtRaw(const SpriteSheet& sheet, const Sprite& sp, float x, float y, int vAnchor, int hAnchor, bool sized, float width, float height);
    // v2.4: graphics settings of one target (see Platform::SetRenderOverride). Applied on top of the render state when a sprite is drawn for that target.
    struct RenderOverride {
        bool alpha = false, tint[3] = {false, false, false}, skew = false, bend = false, z = false, blur = false, pixelate = false, blend = false, effect = false, rot = false;
        float a = 1, t[3] = {1, 1, 1}, skewX = 0, skewY = 0, bendX = 0, bendY = 0, zv = 0, blurV = 0, pixV = 0, rotV = 0;
        int blendV = 0, effectV = 0;
    };
    std::map<std::string, RenderOverride> overrides_;
    std::string drawTag_, compoTag_;     // what is being drawn: "background", an object's name ...; the composite sprite whose parts are drawn
    struct TagScope { GamePlatform* g; std::string old; TagScope(GamePlatform* p, const std::string& t) : g(p), old(p->drawTag_) { g->drawTag_ = t; } ~TagScope() { g->drawTag_ = old; } };
    void ApplyOverride(const RenderOverride& o);
    std::map<int, std::pair<std::string, bool>> handleSource_;     // texture handle -> the png it came from (for the shader variants)
    std::map<long long, int> textureVariants_;                     // handle * 8 + effect -> handle of the converted copy
    int VariantTexture(int handle, int effect);
    std::map<std::string, Image> pixelImages_;
    bool ResGetSpritePixel(const std::string& sprite, int x, int y, int& r, int& g, int& b, int& a) override;
    bool ResGetCompoParts(const std::string& name, std::vector<CompoPartInfo>& out) override;
    void SetRenderOverride(const std::string& target, int what, float a, float b, float c) override;
    bool GetRenderOverride(const std::string& target, int what, float& a, float& b, float& c) override;
    bool HasObject(const std::string& name) override { return Physics().Find(name) != nullptr; }
    void SetBlendMode(int mode) override { state_.blend = mode < 0 || mode > 3 ? 0 : mode; }
    void SetShaderEffect(int effect) override { state_.effect = effect < 0 || effect > 5 ? 0 : effect; }
    const BitmapFont* CurrentFont() const;
    void Transform(float x, float y, float cx, float cy, float& ox, float& oy) const;

    void DrawPhysObject(PhysObject& o);
    void SelectRioAnimations(float dt);          // Rio: the exe asks the script which animation each animated object plays
    void UpdateRioSquash(float dt);              // Rio: the wobble of an object's scale after a hit, and the marmosets' turning
    void UpdateRioGettingUp(float dt);           // Rio: a marmoset lying on its side stands up again
    void UpdateRioBirdFaces(float dt);           // Rio: waiting birds blink, the bird pulled back in the slingshot yells
    struct BirdFace { float wait = 1.0f + 4.0f * (std::rand() % 1000) / 1000.0f, blink = 0.0f; };
    std::map<std::string, BirdFace> birdFaces_;

    struct BgLayer { float dx = 0, dy = 0; bool hidden = false;   // Lua: extra offset in screen pixels, skip the layer
                     std::string sprite; float speed = 1, scale = 1; bool looping = true, hasFixedX = false; float autoScroll = 0, offset = 0; bool space = false; float offX = 0, offY = 0, baseOffY = 0;
                     // Space (0x48bdd0 / 0x467f70): the layer's own numbers
                     bool empty = false; float zDist = 0, xMult = 1, yMult = 1, angleMult = 0, posX = 0, posY = 0, scaleSpeed = 1, angle = 0;
                     // NEW_THEME_SYSTEM (0x44c1b0): scaleX / scaleY (0xbc / 0xc0), the flag bits (0xa4: 1 OVERSTRETCH_ANCHOR_V, 2 V_REPEAT, 4 H_REPEAT, 0x20 ANCHOR_V, 0x40, 0x100 / 0x200 REPEAT_LEFT_ONLY / REPEAT_RIGHT_ONLY) and the rocks spawnParameters scatters
                     float scaleX = 1, scaleY = 1; int flags = 0; bool hasFlags = false; float relY = 3.4028235e38f; bool elemTable = false; float velX = 0, velY = 0, lastOutX = 0, lastOutY = 0, lastW = 0, lastH = 0; bool upFill = false, downFill = false; float upR = 0, upG = 0, upB = 0, downR = 0, downG = 0, downB = 0;
                     std::vector<std::string> spawnList; int spawnAmount = 0; float spawnVelX = 0, spawnVelY = 0, spawnXVar = 0, spawnSX = 0, spawnSY = 0, spawnSW = 0, spawnSH = 0, spawnWW = 0, spawnWH = 0; bool spawned = false; std::vector<std::array<float,3>> inst; };
    struct { bool have = false; float camX = 0, camY = 0, ws = 1, nH0 = 0; } bgRef_;   // 0x467f70: the camera and screen the parallax is measured from
    void DrawSpaceBackground();
    void UpdateNewThemeLayers(std::vector<BgLayer>& layers, float dt);
    void DrawNewThemeBackground();
    void DrawNewThemeLayers(std::vector<BgLayer>& layers, int layerBase);
    std::vector<BgLayer> ntFg_;                    // the fgLayers of a NEW_THEME_SYSTEM theme (drawn by the same routine, vector 0x264)                 // 0x44c1b0 and its helpers: the NEW_THEME_SYSTEM draw
    bool newTheme_ = false;                        // the theme table has NEW_THEME_SYSTEM (+0x166)
    struct { bool have = false, fromLevel = false; int tries = 0; float refX = 0, refY = 0, s88 = 1; } ntRef_;   // ThemeSystem +0x1c / +0x20 (reference camera) and +0x88 (its scale), read once (0x44b820)
    void SetThemeOffsetY(float v) override;
    void SetStartingCameraValue(bool b) override { bgRef_.have = b; }
    std::string bgTheme_;                          // 0x1a4: the theme the reference belongs to (0x435f70 clears it only when the theme changes)
    struct TrajectoryPoint { float x, y; };
    void CallCollisionPair(const char* fn, const std::string& a, const std::string& b);
    bool HasScriptFunction(const char* fn);
    void SpaceSensorTouch(PhysObject& pa, PhysObject& pb, bool enter);
    bool aimEnabled_ = false;                      // enableAimingAid
    std::string aimSprite_;                        // setAimingAidSprite
    // The retail AimingAid (0x45dd00 constructor, 0x45dea0 populate, 0x45dfd0 update, 0x45d570 draw)
    struct AimDot { float x, y, s; };              // x: distance along the path in points, y: its spin, s: scale
    float aimScale_ = 1;                           // setAimingAidSprite(name, scale): dot size factor (extension)
    std::vector<TrajectoryPoint> aimPts_;          // the trajectory table in metres (updateBirdTrajectoryTable)
    std::vector<AimDot> aimDots_;
    bool aimOn_ = false;                           // AimingAid +0x38
    float aimTimer_ = 0;                           // +0x2c
    void AimParams(float& speed, float& spawn);    // simulationAimSpeed / simulationAimSpawnTime
    void AimPopulate();
    void AimSetEnabled(bool on);
    void UpdateAimingAid(float dt);
    TrajectoryPoint AimSpline(float x) const;
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
        // Angry Birds Space (0x493e20): the emitter angle range, the particle's own rotation range (minAngle / maxAngle, degrees), the emit area scale,
        // `useAngleFromSpawner` (the angle of the emitting object is added to the direction and the rotation), the def's own ignoreLimits and `background`
        bool space = false, useSpawnerAngle = false, ignoreLimits = false, background = false;
        float minAngle = 0, maxAngle = 0, areaX = 1, areaY = 1;
        std::vector<std::string> sprites;
    };
    std::map<std::string, ParticleDef> particleOverrideDefs_;   // addParticlesWithScale / addParticlesWithProperties: the def with the call's values
    const ParticleDef* forcedDef_ = nullptr;
    struct Particle {
        const ParticleDef* def = nullptr;
        std::string sprite;
        float x = 0, y = 0, vx = 0, vy = 0, angle = 0, angleVel = 0;
        float scaleBegin = 0, scaleEnd = 0, scale = 0, age = 0, lifeTime = 0;
        int frame = 1;
        bool second = false;                    // the menu list (0x24)
        bool level = false;                     // the level list (0x30), drawn by drawLevelParticlesNative(layer)
        int layer = 1;
        int mode = -1;                          // Star Wars addParticlesWithMode: 3 is the notification layer
    };
    int particleFilter_ = 0;                    // DrawParticlesFiltered: 1 background, 2 foreground, 3 notification, 0 all
    const ParticleDef* ParticleDefFor(const std::string& type);
    void UpdateParticles(float dt, bool removeBelowGround = true, int which = -1);   // which: -1 all, 0 world+level lists, 1 menu list
    std::map<std::string, ParticleDef> particleDefs_;
    std::list<std::map<std::string, ParticleDef>> oldParticleDefs_;   // definitions replaced by clearParticleCache (particles still use them)
    std::vector<Particle> particles_;
    int softLimit_ = 40, hardLimit_ = 125;     // constructor defaults (0x41c2fb)
    float softFactor_ = 0.5f;
    float camX_ = 0, camY_ = 0, worldScale_ = 1;
    // the exe's zoom animation (App +0x280..0x298): a wheel step eases from zoomStart_ to zoomTarget_
    void UpdateZoom(float dt);
    float zoom_ = 1, zoomPrev_ = 1, zoomMax_ = 1, zoomStart_ = 1, zoomTarget_ = 1, zoomT_ = -1, zoomDur_ = 0.5f;
    bool exitRequested_ = false, fullScreenReq_ = false;
    std::vector<BgLayer> bgLayers_;
    std::vector<float> fgDx_, fgDy_; std::vector<char> fgHidden_;   // Lua: per-layer offset (pixels) and visibility of the old-style foreground strip (which = 2)
    struct ThemeSprite { std::string name, sprite; float x, y, scaleX, scaleY, angle, speedX; int layer; float angleVel = 0; bool flipped = false; float velX = 0, velY = 0, scaleSpeed = 0;
                        std::vector<std::string> frames; int frame = 0; float animTimer = 0, animDelay = 0, startDelay = 0; bool loop = false, finished = false; float wrapW = 0, wrapH = 0; };   // frames: createThemeAnimation
    std::vector<ThemeSprite> themeSprites_;
    std::map<std::string, std::string> savedLevels_;   // v2.2: levels saved by the editor this session, by path (see FindSavedLevel)
    void DrawThemeSprites(int layer);
    float themeShift_ = 0;
    std::vector<std::string> fgLayers_;
    struct FgNamed { bool named = false, fill = false; float offY = 0, r = 0, g = 0, b = 0; };
    std::vector<FgNamed> fgNamed_;
    struct FgWater { bool water = false; float wobbleY = 0; };      // Piglantis: a foreground layer with water = true sits on the water surface (objects.waterLevel) instead of the ground
    std::vector<FgWater> fgWater_;
    std::map<int, std::string> fgParticles_;
    bool swTheme_ = false;
    bool swBg_ = false;                            // a Star Wars theme (its table has textureScale): backgrounds are drawn at the retail Star Wars scale
    void SpawnFGLayerParticles(int id) override;      // Star Wars' foreground layers with named fields (offsetY, downFillColor)
    std::vector<float> fgSpeed_, fgScale_, fgAuto_, fgOffset_;      // per foreground layer: parallax speed and scale (entries 3 and 4; 1 and 1.5 when absent)
    float groundR_ = 0, groundG_ = 0, groundB_ = 0;
    std::string themeTexture_;
    std::vector<TrajectoryPoint> trajDots_[6], trajPuffs_[6];
    std::string pendingNormalTrail_, pendingSpecialTrail_;   // Space setNormalTrailSprite (0x4705f0) / setSpecialTrailSprite (0x470660)
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
    float solverBase_[3] = {0.05f, 0.25f, 0.1f};   // the solver settings the game started with (tuning replaces single ones)
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
