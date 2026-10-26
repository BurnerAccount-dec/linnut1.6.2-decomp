// Everything the Lua scripts can ask of the engine, as one interface.
//
// lua_api.cpp registers every function the original exe registers (see ENGINE_SPEC.md section 5/6),
// checks and converts the arguments the way the originals do, and calls the matching method here.
// Every method has a harmless default so the scripts can be run before the real physics, renderer
// and audio exist; the real modules override the ones they implement.
#pragma once
#include <string>
#include <vector>

namespace ab {

// The table passed to createJoint(...). Missing numbers are 0, missing flags false.
struct JointDef {
    std::string name, end1, end2;
    int type = 0;
    int coordType = 0;
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    float frequency = 0, dampingRatio = 0;
    bool collideConnected = false;
    bool motor = false;
    float motorSpeed = 0, maxTorque = 0;
    bool limit = false;
    float lowerLimit = 0, upperLimit = 0;
    bool backAndForth = false;
    float worldAxisX = 0, worldAxisY = 0;
    float destroyTimer = 1.0f;     // type 5 only
};

// The table passed to setJointParameters{...}: only the fields that were present are set.
struct JointParams {
    std::string name;
    bool hasMotor = false, motor = false;
    bool hasMotorSpeed = false; float motorSpeed = 0;
    bool hasMaxTorque = false; float maxTorque = 0;
    bool hasLimit = false, limit = false;
    bool hasLowerLimit = false; float lowerLimit = 0;
    bool hasUpperLimit = false; float upperLimit = 0;
    bool hasFrequency = false; float frequency = 0;
    bool hasDampingRatio = false; float dampingRatio = 0;
};

struct BodyDef {
    std::string name, sprite;
    float x = 0, y = 0, w = 0, h = 0;      // circle: w is the radius
    float density = 0, friction = 0, restitution = 0;
    bool collision = true;                  // not present for circles
    bool controllable = false;
    float zOrder = 0;
};

class Platform {
public:
    virtual ~Platform() {}

    // ---- window / system
    virtual void RequestExit() {}
    virtual void Print(const std::string& text) { (void)text; }
    virtual void SetBGColor(float r, float g, float b) { (void)r; (void)g; (void)b; }
    virtual void CaptureMouse(bool on) { mouseCaptured_ = on; }
    virtual bool IsMouseCaptured() { return mouseCaptured_; }
    virtual void SetFullScreen(bool on) { fullScreen_ = on; }
    virtual bool IsFullScreen() { return fullScreen_; }
    virtual void SetResolution(float w, float h) { (void)w; (void)h; }
    virtual void SetMouseWheelSimulation(bool on) { (void)on; }
    virtual bool MouseClickSetsTouchCount() { return true; }
    virtual void SetEditing(bool on) { (void)on; }
    virtual void SetGameOn(bool on) { (void)on; }

    // ---- files. appData: true = user-data folder, false = install folder (to confirm)
    // loadLevel(path): the Lua side (running the level file into `loadedObjects`) is in lua_api.cpp.
    // BeginLevelLoad drops the old level's physics and resets the step accumulator; ResolveDataFile
    // turns "levels/pack1/Level1.lua" into a real path; EndLevelLoad is called when the file is loaded.
    virtual void BeginLevelLoad() {}
    virtual void EndLevelLoad() {}
    virtual std::string ResolveDataFile(const std::string& path) { return path; }
    virtual void SaveLevel(const std::string& path) { (void)path; }
    virtual std::string ResolveUserFile(const std::string& file, bool appData) { (void)appData; return file; }
    virtual void CreateDirectory(const std::string& path) { (void)path; }
    virtual bool CheckDirectory(const std::string& path) { (void)path; return false; }
    virtual bool CheckForLuaFile(const std::string& path) { (void)path; return false; }

    // ---- physics
    // The three Create calls return the body mass (the exe writes it to objects.world[name].mass).
    virtual float CreateBox(const BodyDef& d) { (void)d; return 0; }
    virtual float CreatePolygon(const BodyDef& d, const std::vector<float>& verts /* x,y pairs */) { (void)d; (void)verts; return 0; }
    virtual float CreateCircle(const BodyDef& d) { (void)d; return 0; }
    // d has the defaults applied; the call may change x1..y2 (coordType 0 writes the body positions).
    // Returns false if an end object does not exist.
    virtual bool CreateJoint(JointDef& d) { (void)d; return true; }
    virtual void SetJointParameters(const JointParams& p) { (void)p; }
    virtual void CheckJointLimits(const std::string& name) { (void)name; }
    virtual void DestroyJoint(const std::string& name) { (void)name; }
    // Returns the names of the links (createJoint type 5) that fired; the caller deletes joints[name].
    virtual std::vector<std::string> RemoveObject(const std::string& name) { (void)name; return std::vector<std::string>(); }
    virtual void ApplyImpulse(const std::string& n, float ix, float iy, float px, float py) { (void)n; (void)ix; (void)iy; (void)px; (void)py; }
    virtual void ApplyForce(const std::string& n, float fx, float fy, float px, float py) { (void)n; (void)fx; (void)fy; (void)px; (void)py; }
    virtual bool SetPosition(const std::string& n, float x, float y) { (void)n; (void)x; (void)y; return false; }   // false: no such object
    virtual bool SetRotation(const std::string& n, float a, float& normalized) { (void)n; normalized = a; return false; }
    virtual void SetVelocity(const std::string& n, float vx, float vy) { (void)n; (void)vx; (void)vy; }
    virtual void SetAngularVelocity(const std::string& n, float w) { (void)n; (void)w; }
    virtual void SetSleeping(const std::string& n, bool sleeping) { (void)n; (void)sleeping; }
    virtual float GetAngle(const std::string& n) { (void)n; return 0; }
    virtual void GetWorldPoint(const std::string& n, float lx, float ly, float& wx, float& wy) { (void)n; wx = lx; wy = ly; }
    // Angry Birds Rio
    virtual float GetSpeed(const std::string& n) { (void)n; return 0; }
    virtual bool HasMovingObjects(float tolerance) { (void)tolerance; return false; }
    virtual void GetLocalPoint(const std::string& n, float wx, float wy, float& lx, float& ly) { (void)n; lx = wx; ly = wy; }
    virtual void SetCollisionEnabled(const std::string& n, bool on) { (void)n; (void)on; }
    // collider types (Angry Birds 3.0.1 / Seasons) and the polygon skin of Angry Birds Rio's physics
    virtual void SetColliderType(const std::string& n, int type) { (void)n; (void)type; }
    virtual int ColliderType(const std::string& n) { (void)n; return -1; }
    virtual void SetColliderRule(int a, int b, bool collide) { (void)a; (void)b; (void)collide; }
    virtual bool ColliderRule(int a, int b) { (void)a; (void)b; return true; }
    virtual void SetPolygonRadius(float r) { (void)r; }
    virtual float PolygonRadius() { return 0; }
    virtual void RemoveJointsFromObject(const std::string& n) { (void)n; }
    virtual std::vector<std::string> IntersectingObjects(float x, float y, float l, float r, float d, float u) {
        (void)x; (void)y; (void)l; (void)r; (void)d; (void)u; return std::vector<std::string>(); }
    virtual void SetObjectParameter(const std::string& n, int id, float value) { (void)n; (void)id; (void)value; }
    virtual void SetPhysicsSimulationScale(float s) { (void)s; }
    virtual void SetWorldGravity(float x, float y) { (void)x; (void)y; }
    virtual void ClearParticles() {}
    virtual void SetPhysicsEnabled(bool on) { physicsEnabled_ = on; }
    virtual bool IsPhysicsEnabled() { return physicsEnabled_; }
    virtual void SetLevelLimits(float minX, float minY, float maxX, float maxY) { (void)minX; (void)minY; (void)maxX; (void)maxY; }
    virtual void ClearVertices() { vertices_.clear(); }
    virtual void AddVertex(float x, float y) { vertices_.push_back(x); vertices_.push_back(y); }
    const std::vector<float>& Vertices() const { return vertices_; }

    // ---- camera
    virtual void SetTopLeft(float x, float y) { (void)x; (void)y; }
    virtual void SetWorldScale(float s) { (void)s; }
    virtual void SetMaxWorldScale(float s) { (void)s; }
    virtual void SetMaxTranslation(float t) { (void)t; }

    // ---- drawing
    // setRenderState(tx, ty, [sx, sy], [angle], [pivotX, pivotY]) updates a persistent state: the exe
    // writes translation when it got at least 2 values, scale with 4, angle with 5, pivot with 7. Values
    // that were not passed keep their previous setting. argc is the number of Lua arguments.
    virtual void SetRenderState(int argc, const float* args) { (void)argc; (void)args; }
    virtual void SetAlpha(float a) { (void)a; }
    virtual void DrawRect(float r, float g, float b, float a, float x1, float y1, float x2, float y2, bool flag) {
        (void)r; (void)g; (void)b; (void)a; (void)x1; (void)y1; (void)x2; (void)y2; (void)flag; }
    virtual void DrawLine2D(float x1, float y1, float x2, float y2, float width, float r, float g, float b, float a) {
        (void)x1; (void)y1; (void)x2; (void)y2; (void)width; (void)r; (void)g; (void)b; (void)a; }
    virtual void DrawGame() {}
    virtual void DrawBackground() {}
    virtual void DrawForeground() {}
    virtual void DrawParticles(bool world = true) { (void)world; }
    virtual void DrawLevelParticles(int layer) { (void)layer; }
    virtual void SetSprite(const std::string& n, const std::string& sprite) { (void)n; (void)sprite; }
    virtual void SetMaterial(const std::string& n, const std::string& material) { (void)n; (void)material; }
    virtual void SetTexture(const std::string& n, const std::string& texture) { (void)n; (void)texture; }
    virtual void SetTheme(const std::string& theme) { (void)theme; }
    virtual void StartNewTrajectory() {}
    virtual void AddToTrajectory(float i, float x, float y) { (void)i; (void)x; (void)y; }
    virtual void AddPuffToTrajectory(float i, float x, float y) { (void)i; (void)x; (void)y; }

    // clipText(group, text, maxWidth): returns the wrapped lines. Default: one line, width 0.
    virtual std::vector<std::string> ClipText(const std::string& group, const std::string& text, float maxWidth, float& widest) {
        (void)group; (void)maxWidth; widest = 0; return std::vector<std::string>(1, text); }

    // ---- res.* (resources, fonts, text, audio)
    virtual void ResSetPath(const std::string& p) { (void)p; }
    virtual void ResCreateSpriteSheet(const std::string& path) { (void)path; }
    virtual void ResReleaseSpriteSheet(const std::string& path) { (void)path; }
    virtual void ResCreateCompoSpriteSet(const std::string& path) { (void)path; }
    virtual void ResReleaseCompoSpriteSet(const std::string& path) { (void)path; }
    virtual void ResCreateBitmapFont(const std::string& path) { (void)path; }
    virtual void ResUseFont(const std::string& name) { (void)name; }
    virtual void ResCreateTextGroupSet(const std::string& path) { (void)path; }
    virtual void ResLoadLocale(const std::string& group, const std::string& locale) { (void)group; (void)locale; }
    virtual void ResUseLocale(const std::string& locale) { (void)locale; }
    virtual std::string ResGetString(const std::string& group, const std::string& key) { (void)group; return key; }
    // drawSprite(name, x, y, [anchor], [anchor], [width, height]). Anchors (see Anchor in lua_api.cpp):
    // vertical 0 TOP, 1 VCENTER, 2 BOTTOM, 3 BASELINE, 4 VPIVOT; horizontal 0 LEFT, 1 HCENTER, 2 RIGHT, 3 HPIVOT.
    // The default is the pivot in both directions. width/height stretch the sprite when `sized` is set.
    // Seasons: decorative sprites tied to a background or foreground layer (layer numbers count the
    // background layers first, then the foreground ones). x and y are in the layer's own units.
    virtual void CreateThemeSprite(const std::string& name, const std::string& sprite, float x, float y, float scaleX, float scaleY, float angle, float speedX, int layer) {
        (void)name; (void)sprite; (void)x; (void)y; (void)scaleX; (void)scaleY; (void)angle; (void)speedX; (void)layer; }
    virtual void ModifyThemeSprite(const std::string& name, float x, float y, float scaleX, float scaleY, float angle, int layer) {
        (void)name; (void)x; (void)y; (void)scaleX; (void)scaleY; (void)angle; (void)layer; }
    virtual void RemoveThemeSprite(const std::string& name, int layer) { (void)name; (void)layer; }
    virtual void UpdateThemeSprites(float dt) { (void)dt; }
    // Angry Birds Rio: createThemeSprite(name, sprite, x, y, scaleX, scaleY, angle, layer, angleVel, flipped), setThemeSprite,
    // rotateThemeSprites(dt) and updateBackgroundAndForegroundScrollingNative(dt, force)
    virtual void CreateThemeSpriteRio(const std::string& name, const std::string& sprite, float x, float y, float scaleX, float scaleY, float angle, int layer, float angleVel, bool flipped) {
        (void)name; (void)sprite; (void)x; (void)y; (void)scaleX; (void)scaleY; (void)angle; (void)layer; (void)angleVel; (void)flipped; }
    virtual void SetThemeSprite(const std::string& name, const std::string& sprite, int layer) { (void)name; (void)sprite; (void)layer; }
    virtual void RotateThemeSprites(float dt) { (void)dt; }
    virtual void ScrollThemeLayers(float dt, bool force) { (void)dt; (void)force; }
    virtual void ResDrawSprite(const std::string& sprite, float x, float y, int vAnchor, int hAnchor, bool sized, float width, float height) {
        (void)sprite; (void)x; (void)y; (void)vAnchor; (void)hAnchor; (void)sized; (void)width; (void)height; }
        // vAlign "TOP"/"BOTTOM"/..., hAlign "LEFT"/"HCENTER"/...; group "" = current font
    virtual void ResDrawString(const std::string& group, const std::string& text, float x, float y,
                               const std::string& vAlign, const std::string& hAlign) {
        (void)group; (void)text; (void)x; (void)y; (void)vAlign; (void)hAlign; }
    virtual void ResSetClipRect(float x, float y, float w, float h) { (void)x; (void)y; (void)w; (void)h; }
    virtual void ResGetSpriteBounds(const std::string& sheet, const std::string& sprite, float& w, float& h) { (void)sheet; (void)sprite; w = h = 0; }
    virtual void ResGetSpritePivot(const std::string& sheet, const std::string& sprite, float& px, float& py) { (void)sheet; (void)sprite; px = py = 0; }
    virtual void ResGetCompoSpriteBounds(const std::string& sheet, const std::string& sprite, float& x1, float& y1, float& x2, float& y2) {
        (void)sheet; (void)sprite; x1 = y1 = x2 = y2 = 0; }
    virtual float ResGetStringWidth(const std::string& text) { (void)text; return 0; }
    virtual float ResGetFontHeight() { return 0; }
    virtual float ResGetFontLeading() { return 0; }
    virtual float ResGetFontTracking() { return 0; }
    virtual float ResGetFontMaxAscending() { return 0; }
    virtual float ResGetFontMaxDescending() { return 0; }
    virtual void ResCreateAudioOutput(float channels, float bits, float rate) { (void)channels; (void)bits; (void)rate; }
    virtual void ResStartAudioOutput() {}
    virtual void ResStopAudioOutput() {}
    virtual void ResCreateAudio(const std::string& path, const std::string& name) { (void)path; (void)name; }
    // playAudio(name, volume = 1, loop = false, track = 0)
    virtual void ResPlayAudio(const std::string& name, float volume, bool loop, int track) { (void)name; (void)volume; (void)loop; (void)track; }
    virtual void ResStopAudio(const std::string& name) { (void)name; }
    virtual void ResStopAllAudio() {}
    virtual bool ResIsAudioPlaying(const std::string& name) { (void)name; return false; }
    virtual void ResSetTrackVolume(float volume, int track) { if (track >= 0 && track < 16) trackVolume_[track] = volume; }
    virtual float ResGetTrackVolume(int track) { return (track >= 0 && track < 16) ? trackVolume_[track] : 0; }
    virtual void ResOpenURL(const std::string& url) { (void)url; }

    // ---- particles.*
    // addParticles(type, amount, x, y, w, h, angle): type is the key in particleTable.particles
    virtual void AddLevelParticles(const std::string& type, float amount, float x, float y, float w, float h, float angle, bool ignoreLimits) { (void)type; (void)amount; (void)x; (void)y; (void)w; (void)h; (void)angle; (void)ignoreLimits; }
    virtual void AddParticles(const std::string& type, float amount, float x, float y, float w, float h, float angle, bool ignoreLimits = false, bool second = false) {
        (void)type; (void)amount; (void)x; (void)y; (void)w; (void)h; (void)angle; }
    virtual void SetParticleSoftLimit(float a, float b) { (void)a; (void)b; }
    virtual void SetParticleHardLimit(float n) { (void)n; }

private:
    bool mouseCaptured_ = false, fullScreen_ = false, physicsEnabled_ = true;
    std::vector<float> vertices_;
    float trackVolume_[16] = {0};
};

}  // namespace ab
