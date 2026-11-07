// Everything the Lua scripts can ask of the engine, as one interface.
//
// lua_api.cpp registers every function the original exe registers (see ENGINE_SPEC.md section 5/6),
// checks and converts the arguments the way the originals do, and calls the matching method here.
// Every method has a harmless default so the scripts can be run before the real physics, renderer
// and audio exist; the real modules override the ones they implement.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "physics/collision_prediction.h"

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

// One shape of an Angry Birds Space compound body (addBox/Polygon/Line/CircleToCompoundList): placed relative to the body, in physics units
struct CompoundShape {
    int kind = 0;                 // 0 box, 1 polygon, 2 line, 3 circle
    std::string sprite;
    float x = 0, y = 0, angle = 0, w = 0, h = 0;   // circle: w is the radius
    std::vector<float> verts;     // polygon: x,y pairs
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
    virtual void GetBGColor(float& r, float& g, float& b) { r = g = b = 255; }   // 0..255, as setBGColor stored them
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
    // v2.2: the text of a level the editor saved in this session (its temp copies), read before the file
    virtual bool FindSavedLevel(const std::string& path, std::vector<uint8_t>& bytes) { (void)path; (void)bytes; return false; }
    virtual std::string ResolveUserFile(const std::string& file, bool appData) { (void)appData; return file; }
    virtual void CreateDirectory(const std::string& path) { (void)path; }
    virtual bool CheckDirectory(const std::string& path) { (void)path; return false; }
    virtual bool CheckForLuaFile(const std::string& path) { (void)path; return false; }

    // ---- physics
    // The three Create calls return the body mass (the exe writes it to objects.world[name].mass).
    virtual float CreateBox(const BodyDef& d) { (void)d; return 0; }
    virtual float CreateCompound(const BodyDef& d, const std::vector<CompoundShape>& shapes, float damping) { (void)d; (void)shapes; (void)damping; return 0; }
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
    virtual CollisionPrediction PredictCollision(const std::string& a, const std::string& b, float restitution, const float* vel = nullptr) { (void)a; (void)b; (void)restitution; (void)vel; return CollisionPrediction(); }
    virtual bool SetOwnGravityVector(const std::string& n, float gx, float gy) { (void)n; (void)gx; (void)gy; return false; }
    virtual bool SetOwnGravityScale(const std::string& n, float scale) { (void)n; (void)scale; return false; }
    virtual bool ResetOwnGravity(const std::string& n) { (void)n; return false; }
    // Space getRayCastedObjects: the objects a segment crosses (0x46f3f0), with the hit point, the surface normal and the fraction of the segment
    struct RayCastHit { std::string name; float px = 0, py = 0, nx = 0, ny = 0, fraction = 0; };
    virtual void RayCastObjects(float x1, float y1, float x2, float y2, std::vector<RayCastHit>& out) { (void)x1; (void)y1; (void)x2; (void)y2; (void)out; }
    virtual bool GetOwnGravity(const std::string& n, int& mode, float& gx, float& gy, float& scale) { (void)n; mode = 0; gx = gy = 0; scale = 1; return false; }
    virtual void ApplyImpulse(const std::string& n, float ix, float iy, float px, float py) { (void)n; (void)ix; (void)iy; (void)px; (void)py; }
    virtual void ApplyForce(const std::string& n, float fx, float fy, float px, float py) { (void)n; (void)fx; (void)fy; (void)px; (void)py; }
    virtual bool SetPosition(const std::string& n, float x, float y) { (void)n; (void)x; (void)y; return false; }   // false: no such object
    virtual bool SetRotation(const std::string& n, float a, float& normalized) { (void)n; normalized = a; return false; }
    virtual void SetVelocity(const std::string& n, float vx, float vy) { (void)n; (void)vx; (void)vy; }
    virtual void SetAngularVelocity(const std::string& n, float w) { (void)n; (void)w; }
    virtual bool BodyCommand(const std::string& n, int cmd, float a, float& result) { (void)n; (void)cmd; (void)a; (void)result; return false; }
    virtual void SetSleeping(const std::string& n, bool sleeping) { (void)n; (void)sleeping; }
    virtual float GetAngle(const std::string& n) { (void)n; return 0; }
    virtual void GetWorldPoint(const std::string& n, float lx, float ly, float& wx, float& wy) { (void)n; wx = lx; wy = ly; }
    // Angry Birds Rio
    virtual float GetSpeed(const std::string& n) { (void)n; return 0; }
    virtual void SetRecordVelocity(const std::string& n, bool on) { (void)n; (void)on; }
    virtual bool GetVelocity(const std::string& n, float& vx, float& vy) { (void)n; vx = vy = 0; return false; }
    virtual bool HasMovingObjects(float tolerance) { (void)tolerance; return false; }
    virtual void GetLocalPoint(const std::string& n, float wx, float wy, float& lx, float& ly) { (void)n; lx = wx; ly = wy; }
    virtual void SetCollisionEnabled(const std::string& n, bool on) { (void)n; (void)on; }
    // collider types (Angry Birds 3.0.1 / Seasons) and the polygon skin of Angry Birds Rio's physics
    virtual void SetColliderType(const std::string& n, int type) { (void)n; (void)type; }
    virtual int ColliderType(const std::string& n) { (void)n; return -1; }
    virtual float GetSpriteAngle(const std::string& n) { (void)n; return 0; }
    virtual void SetDecoration(const std::string& n, const std::string& sprite, int amount, float increment, float scale) { (void)n; (void)sprite; (void)amount; (void)increment; (void)scale; }
    virtual void SetSpriteLook(const std::string& n, int what, float a, float b) { (void)n; (void)what; (void)a; (void)b; }
    virtual void SetObjectScale(const std::string& n, float sx, float sy) { (void)n; (void)sx; (void)sy; }
    virtual bool GetObjectScale(const std::string& n, float& sx, float& sy) { (void)n; sx = sy = 1; return false; }
    virtual void SetSpeedCap(const std::string& n, float speed, float vx, float vy) { (void)n; (void)speed; (void)vx; (void)vy; }
    virtual bool GetSpeedCap(const std::string& n, float& speed, float& vx, float& vy) { (void)n; speed = vx = vy = 0; return false; }
    virtual std::vector<std::string> TouchingObjects(const std::string& n) { (void)n; return {}; }
    virtual void SetObjectAlpha(const std::string& n, float a) { (void)n; (void)a; }
    virtual float ObjectAlpha(const std::string& n) { (void)n; return -1; }
    virtual void SetObjectZOrder(const std::string& n, float z) { (void)n; (void)z; }
    virtual float ObjectZOrder(const std::string& n) { (void)n; return -1e9f; }
    virtual float MoveObjectZ(const std::string& n, bool front) { (void)n; (void)front; return -1e9f; }   // v2.2.3: bringObjectToFront / sendObjectToBack
    virtual std::vector<std::string> GameDrawOrder() { return {}; }                   // v2.2.3: the objects in the order drawGameNative draws them (back to front)
    virtual float RenderZ() { return 0; }                                              // v2.2.3: the z of the current render state
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
    virtual void SetThemeOffsetY(float) {}   // Space setThemeOffsetY(theme, v) (0x466e40)
    virtual bool IsPhysicsEnabled() { return physicsEnabled_; }
    virtual void SetLevelLimits(float minX, float minY, float maxX, float maxY) { (void)minX; (void)minY; (void)maxX; (void)maxY; }
    virtual void ClearVertices() { vertices_.clear(); }
    virtual void AddVertex(float x, float y) { vertices_.push_back(x); vertices_.push_back(y); }
    const std::vector<float>& Vertices() const { return vertices_; }

    // ---- camera
    virtual void SetTopLeft(float x, float y) { (void)x; (void)y; }
    virtual void SetWorldScale(float s) { (void)s; }
    virtual void SetMaxWorldScale(float s) { (void)s; }
    virtual void ResetMouseWheelScale(float s) { (void)s; }   // Space / Star Wars: sets the wheel zoom on its own (the exe's 0x40d160)
    virtual void SetMaxTranslation(float t) { (void)t; }

    // ---- drawing
    // setRenderState(tx, ty, [sx, sy], [angle], [pivotX, pivotY]) updates a persistent state: the exe
    // writes translation when it got at least 2 values, scale with 4, angle with 5, pivot with 7. Values
    // that were not passed keep their previous setting. argc is the number of Lua arguments.
    virtual void SetRenderState(int argc, const float* args) { (void)argc; (void)args; }
    virtual void GetRenderTranslation(float& tx, float& ty) { tx = ty = 0; }   // where the scripts last moved the render state (drawUITextNative keeps it, as the exe does)
    virtual void SetAlpha(float a) { (void)a; }
    // v2.2: the colour that sprites are multiplied by (1, 1, 1: no tint), setRenderState arguments 15 to 17 and setTint
    virtual void SetTint(float r, float g, float b) { (void)r; (void)g; (void)b; }
    virtual void GetTint(float& r, float& g, float& b) { r = g = b = 1; }
    // v2.4: a graphics setting for one target instead of the render state. target: an object name, a sprite or composite sprite name, or "background" /
    // "foreground" / "trajectory" / "particles" (what the engine draws itself). what: 0 alpha, 1 tint (a, b, c), 2 skew, 3 bend, 4 z, 5 blur, 6 pixelate,
    // 7 blend mode, 8 shader, 9 visual rotation, 10 one colour channel (a = 0..3 for r g b a, b = value), 11 clear everything of that target
    virtual void SetRenderOverride(const std::string& target, int what, float a, float b, float c) { (void)target; (void)what; (void)a; (void)b; (void)c; }
    virtual bool GetRenderOverride(const std::string& target, int what, float& a, float& b, float& c) { (void)target; (void)what; a = b = c = 0; return false; }
    virtual bool HasObject(const std::string& name) { (void)name; return false; }
    // v2.4: SetBlendMode / SetShader on the render state itself (setRenderState's arguments 20 and 21)
    virtual void SetBlendMode(int mode) { (void)mode; }
    virtual void SetShaderEffect(int effect) { (void)effect; }
    // v2.4: res.getSpritePixel(sprite, x, y): the colour of one pixel of a sprite's picture, false if there is no such sprite or pixel
    struct CompoPartInfo { std::string sprite; float x = 0, y = 0, scale = 1, angle = 0; };
    virtual bool ResGetCompoParts(const std::string& name, std::vector<CompoPartInfo>& out) { (void)name; (void)out; return false; }
    virtual bool ResGetSpritePixel(const std::string& sprite, int x, int y, int& r, int& g, int& b, int& a) { (void)sprite; (void)x; (void)y; r = g = b = a = 0; return false; }
    virtual void SetRenderExtras(int what, float x, float y) { (void)what; (void)x; (void)y; }   // 0 skew, 1 bend, 2 z, 3 blur, 4 pixelate
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
    virtual void SetTrailSprite(bool special, const std::string& name) { (void)special; (void)name; }
    virtual void StartNewTrajectory() {}
    virtual void AddToTrajectory(float i, float x, float y) { (void)i; (void)x; (void)y; }
    virtual void AddPuffToTrajectory(float i, float x, float y) { (void)i; (void)x; (void)y; }
    // the iPad's setCustomTrajectorySprites(dot1, dot2, dot3, puff): the trajectory being recorded is drawn with these sprites
    virtual void SetCustomTrajectorySprites(const std::string& a, const std::string& b, const std::string& c, const std::string& d) { (void)a; (void)b; (void)c; (void)d; }

    // clipText(group, text, maxWidth): returns the wrapped lines. Default: one line, width 0.
    virtual std::vector<std::string> ClipText(const std::string& group, const std::string& text, float maxWidth, float& widest) {
        (void)group; (void)maxWidth; widest = 0; return std::vector<std::string>(1, text); }

    // ---- res.* (resources, fonts, text, audio)
    virtual void ResSetPath(const std::string& p) { (void)p; }
    virtual void ResCreateSpriteSheet(const std::string& path) { (void)path; }
    virtual void ResReleaseSpriteSheet(const std::string& path) { (void)path; }
    virtual void SpawnFGLayerParticles(int id) { (void)id; }
    virtual void ResKeepLastFrame(bool on) { (void)on; }                                    // Classic screenwipe: captureSprite before anything is drawn this frame takes the last finished frame
    virtual void ResCaptureSprite(const std::string& name) { (void)name; }                  // v2.5 (Star Wars): the last frame becomes a sprite of that name
    virtual void SetObjectDensity(const std::string& n, float density) { (void)n; (void)density; }   // v2.5 (Star Wars setMass)
    virtual void AddParticlesMode(const std::string& type, float amount, float x, float y, float w, float h, float angle, int mode) { AddParticles(type, amount, x, y, w, h, angle); (void)mode; }
    virtual void DrawParticlesFiltered(int filter) { (void)filter; }                         // 1 background, 2 foreground, 3 notification
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
    virtual void CreateThemeSpriteSpace(const std::string& name, const std::string& sprite, float x, float y, float scaleX, float scaleY, float angle, int layer, float scaleSpeed, bool flipped, float velX, float velY) { CreateThemeSpriteRio(name, sprite, x, y, scaleX, scaleY, angle, layer, scaleSpeed, flipped); }
    virtual void CreateThemeSpriteRio(const std::string& name, const std::string& sprite, float x, float y, float scaleX, float scaleY, float angle, int layer, float angleVel, bool flipped) {
        (void)name; (void)sprite; (void)x; (void)y; (void)scaleX; (void)scaleY; (void)angle; (void)layer; (void)angleVel; (void)flipped; }
    // Angry Birds Space: createThemeAnimation(table) (0x484cc0), a theme sprite whose picture changes through `frames` every animDelay seconds
    struct ThemeAnimation {
        std::string name, sprite; std::vector<std::string> frames;
        float x = 0, y = 0, scaleX = 1, scaleY = 1, angle = 0, velX = 0, velY = 0, scaleSpeed = 0;
        float animTimer = 0, animDelay = 0, startingDelay = 0; bool loop = false; int layer = 0;
    };
    virtual void CreateThemeAnimation(const ThemeAnimation& a) { (void)a; }
    virtual void SetThemeSprite(const std::string& name, const std::string& sprite, int layer) { (void)name; (void)sprite; (void)layer; }
    virtual void RotateThemeSprites(float dt) { (void)dt; }
    virtual void ScrollThemeLayers(float dt, bool force) { (void)dt; (void)force; }
    virtual void DrawTexturedLine2D(const std::string& sprite, float x1, float y1, float x2, float y2, float width, float r, float g, float b, float a) { (void)sprite; (void)x1; (void)y1; (void)x2; (void)y2; (void)width; (void)r; (void)g; (void)b; (void)a; }
    virtual void ResDrawSprite(const std::string& sprite, float x, float y, int vAnchor, int hAnchor, bool sized, float width, float height) {
        (void)sprite; (void)x; (void)y; (void)vAnchor; (void)hAnchor; (void)sized; (void)width; (void)height; }
        // vAlign "TOP"/"BOTTOM"/..., hAlign "LEFT"/"HCENTER"/...; group "" = current font
    virtual void ResDrawString(const std::string& group, const std::string& text, float x, float y,
                               const std::string& vAlign, const std::string& hAlign) {
        (void)group; (void)text; (void)x; (void)y; (void)vAlign; (void)hAlign; }
    virtual void ResSetClipRect(float x, float y, float w, float h) { (void)x; (void)y; (void)w; (void)h; }
    // Hatchery (iPad) egg painter: a paint canvas the brushes are stamped on, shown through the egg-shaped alpha mask of a sprite
    virtual void CanvasInit(int w, int h) { (void)w; (void)h; }
    virtual void CanvasClear(float r, float g, float b, float a) { (void)r; (void)g; (void)b; (void)a; }
    virtual void CanvasPaint(const std::string& brush, float x, float y, float w, float h) { (void)brush; (void)x; (void)y; (void)w; (void)h; }
    virtual void CanvasDraw(const std::string& mask) { (void)mask; }
    virtual void CanvasDominant(std::string& first, std::string& second) { first.clear(); second.clear(); }
    struct CompoItem { std::string sprite; float x = 0, y = 0, scale = 1, angle = 0; };
    virtual void SetupCompoObject(const std::string& object, const std::vector<CompoItem>& items, float scale) { (void)object; (void)items; (void)scale; }
    virtual bool CanvasMakeSprite(const std::string& name, int width, int height, int pivotX, int pivotY, bool save, const std::string& file) { (void)name; (void)width; (void)height; (void)pivotX; (void)pivotY; (void)save; (void)file; return false; }
    // createSilhouette(newName, sourceSprite, r, g, b, a, outlineWidth): a new sprite with the shape of the source filled with one colour (outlineWidth > 0: only that many pixels along the edge)
    virtual int CreateSilhouette(const std::string& name, const std::string& source, float r, float g, float b, float a, int outline) { (void)name; (void)source; (void)r; (void)g; (void)b; (void)a; (void)outline; return -1; }
    virtual bool CanvasLoadSprite(const std::string& name, const std::string& file, int pivotX, int pivotY) { (void)name; (void)file; (void)pivotX; (void)pivotY; return false; }
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
    // v2.2 audio controls (res.setAudioSpeed / pauseAudio / resumeAudio / isAudioPaused / setMasterVolume / setAudioClipVolume / setAudioClipSpeed); track -1 = all
    virtual void ResSetAudioSpeed(float speed, int track) { (void)speed; (void)track; }
    virtual float ResGetAudioSpeed(int track) { (void)track; return 1; }
    virtual void ResPauseAudio(bool paused, int track) { (void)paused; (void)track; }
    virtual bool ResIsAudioPaused(int track) { (void)track; return false; }
    virtual void ResSetMasterVolume(float v) { (void)v; }
    virtual float ResGetMasterVolume() { return 1; }
    virtual void ResSetAudioClipVolume(const std::string& n, float v) { (void)n; (void)v; }
    virtual void ResSetAudioClipSpeed(const std::string& n, float s) { (void)n; (void)s; }
    // v2.4: per-clip pitch / tempo / pan, seek and tell (res.setAudioClipPitch ...)
    virtual void ResSetAudioClipPitch(const std::string& n, float v) { (void)n; (void)v; }
    virtual void ResSetAudioClipTempo(const std::string& n, float v) { (void)n; (void)v; }
    virtual void ResSetAudioClipPan(const std::string& n, float v) { (void)n; (void)v; }
    virtual void ResResetAudioClipEffects(const std::string& n) { (void)n; }
    virtual bool ResSeekAudioClip(const std::string& n, double s) { (void)n; (void)s; return false; }
    virtual double ResAudioClipTime(const std::string& n) { (void)n; return -1; }
    virtual double ResAudioClipLength(const std::string& n) { (void)n; return -1; }
    virtual void ResSetString(const std::string& group, const std::string& key, const std::string& text) { (void)group; (void)key; (void)text; }   // v2.4: res.setString
    virtual void ResOpenURL(const std::string& url) { (void)url; }
    virtual void PlayVideo(const std::string& path) { (void)path; }   // playVideo("videos/x.m4v")
    virtual void StopVideo() {}                      // v2.2: stopVideo()
    virtual bool IsVideoPlaying() { return false; }  // v2.2: isVideoPlaying()

    // ---- particles.*
    // addParticles(type, amount, x, y, w, h, angle): type is the key in particleTable.particles
    // Angry Birds Space: addParticlesWithScale (kind 1: scale begin a, scale end b) and addParticlesWithProperties (kind 2: min speed a, max speed b,
    // min emitter angle c, max emitter angle d in radians) replace those values of the definition for one call
    struct ParticleOverride { int kind = 0; float a = 0, b = 0, c = 0, d = 0; };
    virtual void AddParticlesOverride(const std::string& type, float amount, float x, float y, float w, float h, float angle, const ParticleOverride& ov) { (void)type; (void)amount; (void)x; (void)y; (void)w; (void)h; (void)angle; (void)ov; }
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
