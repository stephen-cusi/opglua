/*
** Garry's Mod platform layer: CLuaInterface / ILuaBase ABI and the binary
** module loader.
**
** This file gives the stock LuaJIT VM the object layout and the virtual
** tables the reference engine exposes to binary modules.
** A module does  L->luabase  to reach its ILuaBase, casts it to
** GarrysMod::Lua::ILuaBase* and calls fixed vtable slots.  The slot order is
** the ABI, so every method below is emitted in the shipping order even when
** the body is only a safe stand-in.
**
** Behaviour follows the reference implementation: each method mirrors what
** the reference build does for that slot (the reference is a thin wrapper
** over the Lua JIT C API for most of ILuaBase).  Slots that need the host
** game (console/convar/command creation, std::string sinks) are implemented
** as safe stand-ins that report once and return a benign default; none is a
** null pointer, so a module that calls one cannot crash the VM.
**
** Copyright (C) 2005-2017 Mike Pall. See Copyright Notice in luajit.h
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#define glua_stricmp _stricmp
#else
#include <dlfcn.h>
#include <strings.h>
#define glua_stricmp strcasecmp
#endif

#define lib_gmod_c
#define LUA_LIB

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "lj_obj.h"

/* ------------------------------------------------------------------------ */
/* GMod scalar value layout (GarrysMod::Lua exposes these as 3 floats).     */

typedef struct GLuaVector { float x, y, z; } GLuaVector;
typedef struct GLuaAngle  { float x, y, z; } GLuaAngle;
typedef struct GLuaColor  { unsigned char r, g, b, a; } GLuaColor;

/* UserData prefix the reference writes for every typed userdata:
**   +0  void*  data      (payload pointer)
**   +8  u8     type      (GarrysMod::Lua::Type byte)
*/
typedef struct GLuaUserData {
  void *data;
  unsigned char type;
} GLuaUserData;

/* Realm values (GarrysMod::Lua::State). */
#define GLUA_REALM_CLIENT 0
#define GLUA_REALM_SERVER 1
#define GLUA_REALM_MENU   2

/* GarrysMod::Lua::Type numeric values used directly below. */
#define GT_NONE          (-1)
#define GT_NIL           0
#define GT_BOOL          1
#define GT_LIGHTUSERDATA 2
#define GT_NUMBER        3
#define GT_STRING        4
#define GT_TABLE         5
#define GT_FUNCTION      6
#define GT_USERDATA      7
#define GT_THREAD        8
#define GT_VECTOR        10
#define GT_ANGLE         11
#define GT_TYPE_COUNT    44

struct CLuaInterface;

/* ------------------------------------------------------------------------ */
/* ILuaObject: reference-counted handle to a Lua value.  Our concrete       */
/* object stores a registry reference; the vtable follows the published     */
/* GarrysMod::Lua::ILuaObject declaration order so module calls land on the */
/* right slot.                                                              */

typedef struct GLuaObject {
  const void *vtable;
  struct CLuaInterface *iface;
  int ref;                              /* LUA_NOREF when unbound */
} GLuaObject;

/* ------------------------------------------------------------------------ */
/* ILuaGameCallback default implementation.  The reference forwards console */
/* output and object creation to the host game; without a host we route the */
/* text to stdout and allocate our own object.                              */

typedef struct GLuaGameCallback {
  const void *vtable;
} GLuaGameCallback;

/* ------------------------------------------------------------------------ */
/* CLuaInterface.  Offset 0 is the vtable, offset 8 is the lua_State* that   */
/* the reference keeps as ILuaBase's private state member.  The remaining   */
/* fields are ours and are only reached from this translation unit.         */

typedef struct CLuaReturn {
  GLuaObject *objs[4];
} GLuaReturn;

typedef struct CLuaInterface {
  const void *vtable;                   /* +0  */
  lua_State *state;                     /* +8  (ILuaBase::state) */

  unsigned char realm;                  /* +0x10 realm byte */
  GLuaGameCallback callback;            /* borrowed default callback */
  char pathID[32];                      /* +0x18 path id string */
  char path[512];                       /* current include path */
  int pushedPaths;                      /* path stack depth */

  GLuaReturn returns;                   /* GetReturn slots */
  int tempIndex;                        /* NewTemporaryObject ring cursor */
  GLuaObject *temp[32];                 /* NewTemporaryObject ring */

  GLuaObject *global;                   /* Global() table object */
  int metaIDCounter;
  GLuaObject *metaTables[255];          /* per-type metatable objects */

  void *threadedHead;                   /* simple list of pending calls */
  int  threadedCount;

  int colorMetaID;                      /* metatable id for GLuaColor */
  int savedMetas;                       /* guard for repeated Init */
} CLuaInterface;

typedef void (*glua_vfn)(void);
typedef int (*GLuaCFunc)(lua_State *L);

/* ------------------------------------------------------------------------ */
/* Small helpers                                                            */

#define GLUA_IFACE(self)   ((self)->state)

/* Crash forensics: append a line to a trace file.  Used while bringing the
** VM up inside a real engine, where a bad object pointer otherwise turns
** into a silent access violation.  Enabled only when OPGLUA_TRACE is defined;
** it writes to a fixed path and is a bring-up aid, not a shipping feature. */
#if defined(_WIN32) && defined(OPGLUA_TRACE)
static void glua_tracef(const char *tag, void *a, void *b, void *c)
{
  static FILE *f = NULL;
  static int tried = 0;
  if (!tried) {
    tried = 1;
    f = fopen("D:\\tmp\\glua\\opglua_trace.txt", "a");
  }
  if (f != NULL) {
    fprintf(f, "%s a=%p b=%p c=%p\n", tag, a, b, c);
    fflush(f);
  }
}
#else
#define glua_tracef(t,a,b,c) ((void)0)
#endif

static const char *glua_type_name(int type);

/* The interface vtable, defined near the end; referenced by gobj_push for the
** foreign-object guard. */
extern const glua_vfn glua_interface_vtable[132];

/* GetType semantics: stock Lua types pass through, typed userdata report
** their stored type byte when it is a GMod type (>= 9). */
static int glua_gettype(lua_State *L, int idx)
{
  int t = lua_type(L, idx);
  if (t == GT_USERDATA) {
    GLuaUserData *ud = (GLuaUserData *)lua_touserdata(L, idx);
    if (ud != NULL && ud->type >= 9)
      return ud->type;
    return GT_USERDATA;
  }
  return t;
}

/* The reference name table (44 entries) plus the 'none'/'unknown' bounds. */
static const char *glua_typename_table[GT_TYPE_COUNT] = {
  "nil", "bool", "lightuserdata", "number", "string", "table", "function",
  "userdata", "thread", "entity", "vector", "angle", "physobj", "save",
  "restore", "damageinfo", "effectdata", "movedata", "recipientfilter",
  "usercmd", "vehicle", "material", "panel", "particle", "particleemitter",
  "texture", "usermsg", "convar", "mesh", "matrix", "sound",
  "pixelvishandle", "dlight", "video", "file", "locomotion", "path",
  "navarea", "soundhandle", "navladder", "particlesystem",
  "projectedtexture", "physcollide", "surfaceinfo"
};

static const char *glua_type_name(int type)
{
  if (type < 0) return "none";
  if (type >= GT_TYPE_COUNT) return "unknown";
  return glua_typename_table[type];
}

/* Script-callback pooled strings.  Numeric ids passed across the ABI map to
** these names; the table order is the reference order (index 196 is
** FireAnimationEvent).  Values are read-only. */
static const char *glua_pooled_strings[] = {
    "AcceptInput", "AdjustMouseSensitivity", "AllowPlayerPickup", "CalcMainActivity",
    "CalcView", "CalcViewModelView", "CanExitVehicle", "CanPlayerEnterVehicle",
    "CanPlayerSuicide", "ChatText", "ChatTextChanged", "CheckPassword",
    "ClientSignOnStateChanged", "CreateClientsideRagdoll", "CreateEntityRagdoll", "CreateMove",
    "DoAnimationEvent", "DoPlayerDeath", "DrawMonitors", "DrawPhysgunBeam",
    "EndEntityDriving", "EntityEmitSound", "EntityFireBullets", "PostEntityFireBullets",
    "EntityKeyValue", "EntityNetworkedVarChanged", "EntityRemoved", "EntityTakeDamage",
    "PostEntityTakeDamage", "FindUseEntity", "FinishChat", "FinishMove",
    "GUIMouseDoublePressed", "GUIMousePressed", "GUIMouseReleased", "GetFallDamage",
    "GetGameDescription", "GetMotionBlurValues", "GetPreferredCarryAngles", "GetTeamColor",
    "GravGunOnDropped", "GravGunOnPickedUp", "GravGunPickupAllowed", "GravGunPunt",
    "HUDAmmoPickedUp", "HUDDrawScoreBoard", "HUDItemPickedUp", "HUDPaint",
    "HUDPaintBackground", "DrawOverlay", "CaptureVideo", "HUDShouldDraw",
    "HUDWeaponPickedUp", "InitPostEntity", "InputMouseApply", "KeyPress",
    "KeyRelease", "Move", "NeedsDepthPass", "NetworkEntityCreated",
    "NetworkIDValidated", "NotifyShouldTransmit", "OnAchievementAchieved", "OnChatTab",
    "OnCrazyPhysics", "OnDamagedByExplosion", "OnEntityCreated", "OnEntityWaterLevelChanged",
    "OnNPCKilled", "OnNPCDropItem", "OnPhysgunFreeze", "OnPhysgunReload",
    "OnPlayerChat", "OnPlayerHitGround", "PlayerChangedTeam", "OnViewModelChanged",
    "PhysgunDrop", "PhysgunPickup", "OnPhysgunPickup", "PlayerAuthed",
    "PlayerBindPress", "PlayerButtonDown", "PlayerButtonUp", "PlayerCanHearPlayersVoice",
    "PlayerCanPickupItem", "PlayerCanPickupWeapon", "PlayerCanSeePlayersChat", "PlayerConnect",
    "PlayerDeath", "PlayerDeathSound", "PlayerDeathThink", "PlayerDroppedWeapon",
    "PlayerDisconnected", "PlayerDriveAnimate", "PlayerEndVoice", "PlayerEnteredVehicle",
    "PlayerFootstep", "PlayerHurt", "PlayerInitialSpawn", "PlayerLeaveVehicle",
    "PlayerNoClip", "PlayerPostThink", "PlayerSay", "PlayerSelectSpawn",
    "PlayerShouldTakeDamage", "PlayerShouldTaunt", "PlayerSilentDeath", "PlayerSpawn",
    "PlayerSpray", "PlayerStartTaunt", "PlayerStartVoice", "PlayerStepSoundTime",
    "PlayerSwitchFlashlight", "PlayerSwitchWeapon", "PlayerTick", "PlayerTraceAttack",
    "PlayerUse", "PostCleanupMap", "PostDraw2DSkyBox", "PostDrawEffects",
    "PostDrawHUD", "PostDrawOpaqueRenderables", "PostDrawSkyBox", "PostDrawTranslucentRenderables",
    "PostDrawViewModel", "PostPlayerDeath", "PostPlayerDraw", "PostRender",
    "PostRenderVGUI", "PreCleanupMap", "PreDrawEffects", "PreDrawHUD",
    "PreDrawOpaqueRenderables", "PreDrawSkyBox", "PreDrawTranslucentRenderables", "PreDrawViewModel",
    "PreDrawViewModels", "PrePlayerDraw", "PreRender", "PreventScreenClicks",
    "PropBreak", "RenderScene", "RenderScreenspaceEffects", "Restored",
    "Saved", "ScaleNPCDamage", "ScalePlayerDamage", "ScoreboardHide",
    "ScoreboardShow", "SetupMove", "SetupPlayerVisibility", "SetupSkyboxFog",
    "SetupWorldFog", "ShouldCollide", "ShouldDrawLocalPlayer", "ShowHelp",
    "ShowSpare1", "ShowSpare2", "ShowTeam", "ShutDown",
    "SpawniconGenerated", "StartChat", "StartCommand", "StartEntityDriving",
    "Think", "Tick", "TranslateActivity", "UpdateAnimation",
    "VGUIMousePressAllowed", "VGUIMousePressed", "VehicleMove", "ViewPunch",
    "WeaponEquip", "PlayerAmmoChanged", "PlayerClassChanged", "PlayerHandleAnimEvent",
    "PlayerFireAnimationEvent", "OnPlayerPhysicsPickup", "OnPlayerPhysicsDrop", "OnPlayerJump",
    "OnCloseCaptionEmit", "HandlePlayerArmorReduction", "OnPauseMenuShow", "OnClientLuaError",
    "OnChangeActivity", "BehaveUpdate", "Blocked", "BodyUpdate",
    "Deploy", "DoImpactEffect", "Draw", "DrawTranslucent",
    "EndTouch", "Equip", "EquipAmmo", "ExpressionFinished",
    "FireAnimationEvent", "FreezeMovement", "GetAttackSpread", "GetCapabilities",
    "GetRelationship", "GetRenderMesh", "HandleAnimEvent", "Holster",
    "ImpactTrace", "KeyValue", "OnCondition", "OnContact",
    "OnIgnite", "OnInjured", "OnKilled", "OnTraceAttack",
    "OnLandOnGround", "OnLeaveGround", "OnNavAreaChanged", "OnOtherKilled",
    "OnStuck", "OnTakeDamage", "OnUnStuck", "PassesDamageFilter",
    "PassesFilter", "PassesTriggerFilters", "PhysicsCollide", "PhysicsSimulate",
    "PhysicsUpdate", "RunEngineTask", "StartEngineSchedule", "StartEngineTask",
    "StartTouch", "TestCollision", "Touch", "UpdateTransmitState",
    "Use", "IsJumpLegal", "CalcWeaponProficiency", "GetShadowCastDirection",
    "OnEntitySight", "OnEntitySightLost", "OnMovementFailed", "OnMovementComplete",
    "OnActiveWeaponChanged", "RunAI", "OnTaskFailed", "OverrideMove",
    "OverrideMoveFacing", "TranslateSchedule", "TranslateNavGoal", "OnStateChange",
    "CustomAmmoDisplay", "ResolveCustomFlyCollision", "DrawHUD", "DrawHUDBackground",
    "DrawWorldModel", "DrawWorldModelTranslucent", "DrawWeaponSelection", "DoDrawCrosshair",
    "ShouldDrawViewModel", "GetTracerOrigin", "OnDrop", "OnRemove",
    "PrimaryAttack", "SecondaryAttack", "Reload", "RenderScreen",
    "ViewModelDrawn", "TranslateFOV", "SetWeaponHoldType", "CanBePickedUpByNPCs",
    "NPCShoot_Primary", "NPCShoot_Secondary", "GetNPCRestTimes", "GetNPCBurstSettings",
    "GetNPCBulletSpread", "ShouldDropOnDie", "ActionSignal", "AllowInput",
    "ApplySchemeSettings", "ConsoleMessage", "DoClick", "OnActivate",
    "OnBeginLoadingDocument", "OnCallback", "OnChangeAddressBar", "OnChangeTargetURL",
    "OnChangeTitle", "OnChildViewCreated", "OnCursorEntered", "OnCursorExited",
    "OnCursorMoved", "OnDeactivate", "OnDeletion", "OnDocumentReady",
    "OnFinishLoadingDocument", "OnFocusChanged", "OnGetFocus", "OnKeyCodePressed",
    "OnKeyCodeReleased", "OnKeyCodeTyped", "OnLoseFocus", "OnMousePressed",
    "OnMouseReleased", "OnMouseWheeled", "OnScreenSizeChanged", "OnSizeChanged",
    "OnTextChanged", "OnTextClicked", "PerformLayout", "ResourceLoaded",
    "TestHover", "DTVar_ReceiveProxyGL", "Float", "Int",
    "Bool", "Vector", "Angle", "String",
    "Entity"
};

#define GLUA_POOLED_COUNT ((int)(sizeof(glua_pooled_strings)/sizeof(glua_pooled_strings[0])))

/* ------------------------------------------------------------------------ */
/* Default game callback (stdout sink).                                     */

static GLuaObject *glua_object_alloc(CLuaInterface *self);

static GLuaObject *gc_CreateLuaObject(GLuaGameCallback *cb)
{
  CLuaInterface *self = (CLuaInterface *)((char *)cb - offsetof(CLuaInterface, callback));
  return glua_object_alloc(self);
}

static void gc_DestroyLuaObject(GLuaGameCallback *cb, GLuaObject *obj)
{
  (void)cb; (void)obj;                    /* freed by the object vtable */
}

static void gc_ErrorPrint(GLuaGameCallback *cb, const char *error, bool print)
{
  (void)cb;
  if (print) { fputs(error, stdout); fflush(stdout); }
}

static void gc_Msg(GLuaGameCallback *cb, const char *msg, bool useless)
{
  (void)cb; (void)useless;
  fputs(msg, stdout); fflush(stdout);
}

static void gc_MsgColour(GLuaGameCallback *cb, const char *msg, const void *color)
{
  (void)cb; (void)color;
  fputs(msg, stdout); fflush(stdout);
}

static void gc_LuaError(GLuaGameCallback *cb, const void *err) { (void)cb; (void)err; }
static void gc_InterfaceCreated(GLuaGameCallback *cb, void *iface) { (void)cb; (void)iface; }

static const glua_vfn glua_callback_vtable[7] = {
  (glua_vfn)gc_CreateLuaObject,
  (glua_vfn)gc_DestroyLuaObject,
  (glua_vfn)gc_ErrorPrint,
  (glua_vfn)gc_Msg,
  (glua_vfn)gc_MsgColour,
  (glua_vfn)gc_LuaError,
  (glua_vfn)gc_InterfaceCreated
};

/* ------------------------------------------------------------------------ */
/* ILuaObject implementation.  A bound object owns a registry reference.    */

static void gobj_bind_stack(CLuaInterface *self, GLuaObject *obj, int stackIndex)
{
  lua_State *L = self->state;
  lua_pushvalue(L, stackIndex);
  if (obj->ref != LUA_NOREF && obj->ref != LUA_REFNIL)
    luaL_unref(L, LUA_REGISTRYINDEX, obj->ref);
  obj->ref = luaL_ref(L, LUA_REGISTRYINDEX);
  obj->iface = self;
}

static void gobj_push(GLuaObject *obj)
{
#ifdef _WIN32
  /* Bringing the VM up inside the real engine showed the engine handing the
  ** object slots a pointer whose interface back-pointer is not one of ours.
  ** Trace it and refuse to dereference, rather than fault. */
  {
    lua_State *L = NULL;
    void *iface = NULL;
    int ref = 0;
    __try {
      if (obj != NULL) {
        iface = obj->iface;
        ref = obj->ref;
        if (iface != NULL && *(void **)iface == (void *)glua_interface_vtable)
          L = (lua_State *)((CLuaInterface *)iface)->state;
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      glua_tracef("gobj_push: AV while reading object", obj, NULL, NULL);
      return;
    }
    if (iface != NULL && L == NULL)
      glua_tracef("gobj_push: foreign/unknown object", obj, iface, (void *)(intptr_t)ref);
    if (L != NULL && ref != LUA_NOREF && ref != LUA_REFNIL) {
      lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
      return;
    }
    if (L != NULL) { lua_pushnil(L); return; }
    return;
  }
#else
  if (obj == NULL || obj->iface == NULL || obj->ref == LUA_NOREF) {
    if (obj != NULL && obj->iface != NULL) lua_pushnil(obj->iface->state);
    return;
  }
  lua_rawgeti(obj->iface->state, LUA_REGISTRYINDEX, obj->ref);
#endif
}

/* Resolve an object's lua_State only when the object really is one of ours
** (its iface points at our interface vtable).  Foreign or corrupt objects
** yield NULL, so callers can no-op instead of dereferencing garbage.  The
** reference ILuaObject is a pure interface reached only through its own
** virtual methods, so a caller-supplied object is not guaranteed to share our
** field layout.
*/
static lua_State *gobj_state(GLuaObject *obj)
{
#ifdef _WIN32
  lua_State *L = NULL;
  __try {
    if (obj != NULL && obj->iface != NULL &&
        *(void **)obj->iface == (void *)glua_interface_vtable)
      L = (lua_State *)((CLuaInterface *)obj->iface)->state;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    L = NULL;
  }
  return L;
#else
  if (obj == NULL || obj->iface == NULL) return NULL;
  if (*(void **)obj->iface != (void *)glua_interface_vtable) return NULL;
  return (lua_State *)((CLuaInterface *)obj->iface)->state;
#endif
}

/* The vtable body for ILuaObject is defined further below. */
extern const glua_vfn glua_object_vtable[93];
extern const glua_vfn glua_interface_vtable[132];

static GLuaObject *glua_object_alloc(CLuaInterface *self)
{
  GLuaObject *obj = (GLuaObject *)malloc(sizeof(GLuaObject));
  if (obj == NULL) return NULL;
  obj->vtable = (const void *)glua_object_vtable;
  obj->iface = self;
  obj->ref = LUA_NOREF;
  return obj;
}

/* ------------------------------------------------------------------------ */
/* ILuaBase slots 0..54                                                     */

static int ci_slot_Top(CLuaInterface *self) { return lua_gettop(self->state); }

static void ci_slot_Push(CLuaInterface *self, int i) { lua_pushvalue(self->state, i); }

static void ci_slot_Pop(CLuaInterface *self, int amt)
{
  lua_State *L = self->state;
  if (amt < 0) amt = 1;
  if (lua_gettop(L) < amt)
    luaL_error(L, "Lua stack over-popped!");
  lua_settop(L, -(amt) - 1);
}

static void ci_slot_GetTable(CLuaInterface *self, int i) { lua_gettable(self->state, i); }
static void ci_slot_GetField(CLuaInterface *self, int i, const char *name)
{ lua_getfield(self->state, i, name); }
static void ci_slot_SetField(CLuaInterface *self, int i, const char *name)
{ lua_setfield(self->state, i, name); }
static void ci_slot_CreateTable(CLuaInterface *self) { lua_createtable(self->state, 0, 0); }
static void ci_slot_SetTable(CLuaInterface *self, int i) { lua_settable(self->state, i); }
static void ci_slot_SetMetaTable(CLuaInterface *self, int i) { lua_setmetatable(self->state, i); }
static int  ci_slot_GetMetaTable(CLuaInterface *self, int i)
{ return lua_getmetatable(self->state, i) != 0; }
static void ci_slot_Call(CLuaInterface *self, int args, int rets)
{ lua_call(self->state, args, rets); }
static int  ci_slot_PCall(CLuaInterface *self, int args, int rets, int errfunc)
{ return lua_pcall(self->state, args, rets, errfunc); }
static int  ci_slot_Equal(CLuaInterface *self, int a, int b) { return lua_equal(self->state, a, b); }
static int  ci_slot_RawEqual(CLuaInterface *self, int a, int b) { return lua_rawequal(self->state, a, b); }
static void ci_slot_Insert(CLuaInterface *self, int i) { lua_insert(self->state, i); }
static void ci_slot_Remove(CLuaInterface *self, int i) { lua_remove(self->state, i); }
static int  ci_slot_Next(CLuaInterface *self, int i) { return lua_next(self->state, i); }
static void *ci_slot_NewUserdata(CLuaInterface *self, unsigned int sz)
{ return lua_newuserdata(self->state, (size_t)sz); }
static void ci_slot_ThrowError(CLuaInterface *self, const char *err)
{ luaL_error(self->state, "%s", err ? err : ""); }
static void ci_slot_CheckType(CLuaInterface *self, int idx, int type)
{
  if (glua_gettype(self->state, idx) != type)
    luaL_typerror(self->state, idx, glua_type_name(type));
}
static void ci_slot_ArgError(CLuaInterface *self, int arg, const char *msg)
{ luaL_argerror(self->state, arg, msg ? msg : ""); }
static void ci_slot_RawGet(CLuaInterface *self, int i) { lua_rawget(self->state, i); }
static void ci_slot_RawSet(CLuaInterface *self, int i) { lua_rawset(self->state, i); }
static const char *ci_slot_GetString(CLuaInterface *self, int idx, unsigned int *outLen)
{
  size_t len = 0;
  const char *s = lua_tolstring(self->state, idx, &len);
  if (outLen) *outLen = (unsigned int)len;
  return s;
}
static double ci_slot_GetNumber(CLuaInterface *self, int idx) { return lua_tonumber(self->state, idx); }
static int ci_slot_GetBool(CLuaInterface *self, int idx) { return lua_toboolean(self->state, idx) != 0; }
static GLuaCFunc ci_slot_GetCFunction(CLuaInterface *self, int idx)
{ return (GLuaCFunc)lua_tocfunction(self->state, idx); }
static void *ci_slot_GetUserdata(CLuaInterface *self, int idx) { return lua_touserdata(self->state, idx); }
static void ci_slot_PushNil(CLuaInterface *self) { lua_pushnil(self->state); }
static void ci_slot_PushString(CLuaInterface *self, const char *val, unsigned int len)
{
  if (len != 0) lua_pushlstring(self->state, val, len);
  else lua_pushstring(self->state, val);
}
static void ci_slot_PushNumber(CLuaInterface *self, double val) { lua_pushnumber(self->state, val); }
static void ci_slot_PushBool(CLuaInterface *self, bool val) { lua_pushboolean(self->state, val); }
static void ci_slot_PushCFunction(CLuaInterface *self, int (*f)(lua_State *))
{ lua_pushcclosure(self->state, f, 0); }
static void ci_slot_PushCClosure(CLuaInterface *self, int (*f)(lua_State *), int n)
{ lua_pushcclosure(self->state, f, n); }
static void ci_slot_PushUserdata(CLuaInterface *self, void *p) { lua_pushlightuserdata(self->state, p); }
static int  ci_slot_ReferenceCreate(CLuaInterface *self) { return luaL_ref(self->state, LUA_REGISTRYINDEX); }
static void ci_slot_ReferenceFree(CLuaInterface *self, int i) { luaL_unref(self->state, LUA_REGISTRYINDEX, i); }
static void ci_slot_ReferencePush(CLuaInterface *self, int i)
{ lua_rawgeti(self->state, LUA_REGISTRYINDEX, i); }
static void ci_slot_PushSpecial(CLuaInterface *self, int type)
{
  lua_State *L = self->state;
  switch (type) {
    case 0: lua_pushvalue(L, LUA_GLOBALSINDEX); break;
    case 1: lua_pushvalue(L, LUA_ENVIRONINDEX); break;
    case 2: lua_pushvalue(L, LUA_REGISTRYINDEX); break;
    default: lua_pushnil(L); break;
  }
}
static int ci_slot_IsType(CLuaInterface *self, int idx, int type)
{
  int t = lua_type(self->state, idx);
  if (t == type) return 1;
  if (t == GT_USERDATA && type > GT_USERDATA) {
    GLuaUserData *ud = (GLuaUserData *)lua_touserdata(self->state, idx);
    return ud != NULL && ud->type == type;
  }
  return 0;
}
static int ci_slot_GetType(CLuaInterface *self, int idx) { return glua_gettype(self->state, idx); }
static const char *ci_slot_GetTypeName(CLuaInterface *self, int type)
{ (void)self; return glua_type_name(type); }

static void ci_slot_CreateMetaTableType(CLuaInterface *self, const char *name, int type)
{
  lua_State *L = self->state;
  if (luaL_newmetatable_type(L, name, type)) {
    if (type >= 0 && type < 255) {
      if (self->metaTables[type] == NULL)
        self->metaTables[type] = glua_object_alloc(self);
      if (self->metaTables[type] != NULL)
        gobj_bind_stack(self, self->metaTables[type], -1);
    }
    lua_pop(L, 1);
  } else {
    lua_pop(L, 1);
  }
}

static const char *ci_slot_CheckString(CLuaInterface *self, int idx)
{ return luaL_checklstring(self->state, idx, NULL); }
static double ci_slot_CheckNumber(CLuaInterface *self, int idx) { return luaL_checknumber(self->state, idx); }
static int ci_slot_ObjLen(CLuaInterface *self, int idx) { return (int)lua_objlen(self->state, idx); }

static const void *ci_slot_GetAngle(CLuaInterface *self, int idx)
{
  static GLuaAngle zero = { 0.0f, 0.0f, 0.0f };
  GLuaUserData *ud = (GLuaUserData *)lua_touserdata(self->state, idx);
  if (ud == NULL || ud->data == NULL || ud->type != GT_ANGLE) return &zero;
  return ud->data;
}
static const void *ci_slot_GetVector(CLuaInterface *self, int idx)
{
  static GLuaVector zero = { 0.0f, 0.0f, 0.0f };
  GLuaUserData *ud = (GLuaUserData *)lua_touserdata(self->state, idx);
  if (ud == NULL || ud->data == NULL || ud->type != GT_VECTOR) return &zero;
  return ud->data;
}

/* Typed value userdata: data points past the 16-byte UserData prefix. */
static void *ci_push_typed(CLuaInterface *self, const void *val, size_t size, int type)
{
  lua_State *L = self->state;
  GLuaUserData *ud = (GLuaUserData *)lua_newuserdata(L, 16 + size);
  void *payload = (char *)ud + 16;
  memcpy(payload, val, size);
  ud->data = payload;
  ud->type = (unsigned char)type;
  if (type >= 0 && type < 255 && self->metaTables[type] != NULL) {
    gobj_push(self->metaTables[type]);  /* value: the type metatable */
    lua_setmetatable(L, -2);            /* pops it onto the userdata */
  }
  return ud;
}

static void ci_slot_PushAngle(CLuaInterface *self, const GLuaAngle *val)
{ ci_push_typed(self, val, sizeof(GLuaAngle), GT_ANGLE); }
static void ci_slot_PushVector(CLuaInterface *self, const GLuaVector *val)
{ ci_push_typed(self, val, sizeof(GLuaVector), GT_VECTOR); }

static void ci_slot_SetState(CLuaInterface *self, lua_State *L) { self->state = L; }

static int ci_slot_CreateMetaTable(CLuaInterface *self, const char *name)
{
  lua_State *L = self->state;
  int id = self->metaIDCounter++;
  if (luaL_newmetatable_type(L, name, id)) {
    lua_pushnumber(L, (double)id);
    lua_setfield(L, -2, "MetaID");
    lua_pushstring(L, name);
    lua_setfield(L, -2, "MetaName");
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, name);
    if (id >= 0 && id < 255) {
      if (self->metaTables[id] == NULL) self->metaTables[id] = glua_object_alloc(self);
      if (self->metaTables[id] != NULL) gobj_bind_stack(self, self->metaTables[id], -1);
    }
    lua_pop(L, 1);
    return id;
  }
  /* Already present: report its stored MetaID. */
  lua_pushstring(L, name);
  lua_gettable(L, LUA_REGISTRYINDEX);
  lua_getfield(L, -1, "MetaID");
  id = (int)lua_tonumber(L, -1);
  lua_pop(L, 2);
  return id;
}

static int ci_slot_PushMetaTable(CLuaInterface *self, int type)
{
  if (type < 0 || type >= 255 || self->metaTables[type] == NULL) return 0;
  gobj_push(self->metaTables[type]);
  return 1;
}

static void ci_slot_PushUserType(CLuaInterface *self, void *data, int type)
{
  lua_State *L = self->state;
  GLuaUserData *ud = (GLuaUserData *)lua_newuserdata(L, sizeof(GLuaUserData));
  ud->data = data;
  ud->type = (unsigned char)type;
  if (type >= 0 && type < 255 && self->metaTables[type] != NULL) {
    gobj_push(self->metaTables[type]);
    lua_setmetatable(L, -2);
  }
}

static void ci_slot_SetUserType(CLuaInterface *self, int idx, void *data)
{
  GLuaUserData *ud = (GLuaUserData *)lua_touserdata(self->state, idx);
  if (ud != NULL) ud->data = data;
}

/* ------------------------------------------------------------------------ */
/* Forward declarations for slots implemented after the interface is fully  */
/* declared (Init, RunStringEx, ...).                                       */

static int  ci_slot_Init(CLuaInterface *self, void *cb, bool dedicated);
static void ci_slot_Shutdown(CLuaInterface *self);
static void ci_slot_Cycle(CLuaInterface *self);
static GLuaObject *ci_slot_Global(CLuaInterface *self);
static GLuaObject *ci_slot_GetObject(CLuaInterface *self, int index);
static void ci_slot_PushLuaObject(CLuaInterface *self, GLuaObject *obj);
static void ci_slot_PushLuaFunction(CLuaInterface *self, int (*f)(lua_State *));
static void ci_slot_LuaError(CLuaInterface *self, const char *err, int index);
static void ci_slot_TypeError(CLuaInterface *self, const char *name, int index);
static void ci_slot_CallInternal(CLuaInterface *self, int args, int rets);
static void ci_slot_CallInternalNoReturns(CLuaInterface *self, int args);
static int  ci_slot_CallInternalGetBool(CLuaInterface *self, int args);
static const char *ci_slot_CallInternalGetString(CLuaInterface *self, int args);
static int  ci_slot_CallInternalGet(CLuaInterface *self, int args, GLuaObject *obj);
static void ci_slot_NewGlobalTable(CLuaInterface *self, const char *name);
static GLuaObject *ci_slot_NewTemporaryObject(CLuaInterface *self);
static int  ci_slot_isUserData(CLuaInterface *self, int index);
static GLuaObject *ci_slot_GetMetaTableObjectName(CLuaInterface *self, const char *name, int type);
static GLuaObject *ci_slot_GetMetaTableObjectIndex(CLuaInterface *self, int index);
static GLuaObject *ci_slot_GetReturn(CLuaInterface *self, int index);
static int  ci_slot_IsServer(CLuaInterface *self);
static int  ci_slot_IsClient(CLuaInterface *self);
static int  ci_slot_IsMenu(CLuaInterface *self);
static void ci_slot_DestroyObject(CLuaInterface *self, GLuaObject *obj);
static GLuaObject *ci_slot_CreateObject(CLuaInterface *self);
static void ci_slot_SetMemberStrObj(CLuaInterface *self, GLuaObject *t, const char *k, GLuaObject *v);
static void ci_slot_SetMemberStr(CLuaInterface *self, GLuaObject *t, const char *k);
static void ci_slot_SetMemberNumObj(CLuaInterface *self, GLuaObject *t, float k, GLuaObject *v);
static void ci_slot_SetMemberNum(CLuaInterface *self, GLuaObject *t, float k);
static void ci_slot_SetMemberObjObj(CLuaInterface *self, GLuaObject *t, GLuaObject *k, GLuaObject *v);
static GLuaObject *ci_slot_GetNewTable(CLuaInterface *self);
static void ci_slot_SetType(CLuaInterface *self, unsigned char realm);
static void ci_slot_PushLong(CLuaInterface *self, long num);
static int  ci_slot_GetFlags(CLuaInterface *self, int index);
static int  ci_slot_FindOnObjectsMetaTable(CLuaInterface *self, int obj, int key);
static int  ci_slot_FindObjectOnTable(CLuaInterface *self, int tbl, int key);
static void ci_slot_SetMemberFast(CLuaInterface *self, GLuaObject *t, int k, int v);
static int  ci_slot_RunString(CLuaInterface *self, const char *fn, const char *path,
                              const char *str, bool run, bool showErrors);
static int  ci_slot_IsEqual(CLuaInterface *self, GLuaObject *a, GLuaObject *b);
static void ci_slot_Error(CLuaInterface *self, const char *err);
static const char *ci_slot_GetStringOrError(CLuaInterface *self, int index);
static int  ci_slot_RunLuaModule(CLuaInterface *self, const char *name);
static int  ci_slot_FindAndRunScript(CLuaInterface *self, const char *fn, bool run,
                                     bool showErrors, const char *str, bool noReturns);
static void ci_slot_SetPathID(CLuaInterface *self, const char *id);
static const char *ci_slot_GetPathID(CLuaInterface *self);
static void ci_slot_ErrorNoHalt(CLuaInterface *self, const char *fmt, ...);
static void ci_slot_Msg(CLuaInterface *self, const char *fmt, ...);
static void ci_slot_PushPath(CLuaInterface *self, const char *path);
static void ci_slot_PopPath(CLuaInterface *self);
static const char *ci_slot_GetPath(CLuaInterface *self);
static int  ci_slot_GetColor(CLuaInterface *self, int index);
static void *ci_slot_PushColor(CLuaInterface *self, GLuaColor color);
static int  ci_slot_GetStack(CLuaInterface *self, int level, lua_Debug *dbg);
static int  ci_slot_GetInfo(CLuaInterface *self, const char *what, lua_Debug *dbg);
static const char *ci_slot_GetLocal(CLuaInterface *self, lua_Debug *dbg, int n);
static const char *ci_slot_GetUpvalue(CLuaInterface *self, int funcIndex, int n);
static int  ci_slot_RunStringEx(CLuaInterface *self, const char *fn, const char *path,
                                const char *str, bool run, bool printErrors,
                                bool dontPushErrors, bool noReturns);
static size_t ci_slot_GetDataString(CLuaInterface *self, int index, const char **str);
static void ci_slot_ErrorFromLua(CLuaInterface *self, const char *fmt, ...);
static const char *ci_slot_GetCurrentLocation(CLuaInterface *self);
static void ci_slot_MsgColour(CLuaInterface *self, const GLuaColor *col, const char *fmt, ...);
static void ci_slot_GetCurrentFile(CLuaInterface *self, void *outStr);
static int  ci_slot_CompileString(CLuaInterface *self, void *dumper, const void *str);
static int  ci_slot_CallFunctionProtected(CLuaInterface *self, int args, int rets, bool printErr);
static void ci_slot_Require(CLuaInterface *self, const char *name);
static const char *ci_slot_GetActualTypeName(CLuaInterface *self, int type);
static void ci_slot_PreCreateTable(CLuaInterface *self, int arr, int narr);
static void ci_slot_PushPooledString(CLuaInterface *self, int index);
static const char *ci_slot_GetPooledString(CLuaInterface *self, int index);
static int  ci_slot_AddThreadedCall(CLuaInterface *self, void *call);
static void ci_slot_AppendStackTrace(CLuaInterface *self, char *buf, unsigned int size);
static void *ci_slot_CreateConVar(CLuaInterface *self, const char *name, const char *def,
                                  const char *help, int flags);
static void *ci_slot_CreateConCommand(CLuaInterface *self, const char *name, const char *help,
                                      int flags, void *cb, void *completion);
static const char *ci_slot_CheckStringOpt(CLuaInterface *self, int idx, const char *def);
static double ci_slot_CheckNumberOpt(CLuaInterface *self, int idx, double def);
static int  ci_slot_RegisterMetaTable(CLuaInterface *self, const char *name, GLuaObject *tbl);
static void ci_slot_dtor(CLuaInterface *self);

/* ------------------------------------------------------------------------ */
/* The 132-slot shipping vtable, emitted strictly in the reference order.   */

const glua_vfn glua_interface_vtable[132] = {
  /*   0 */ (glua_vfn)ci_slot_Top,
  /*   1 */ (glua_vfn)ci_slot_Push,
  /*   2 */ (glua_vfn)ci_slot_Pop,
  /*   3 */ (glua_vfn)ci_slot_GetTable,
  /*   4 */ (glua_vfn)ci_slot_GetField,
  /*   5 */ (glua_vfn)ci_slot_SetField,
  /*   6 */ (glua_vfn)ci_slot_CreateTable,
  /*   7 */ (glua_vfn)ci_slot_SetTable,
  /*   8 */ (glua_vfn)ci_slot_SetMetaTable,
  /*   9 */ (glua_vfn)ci_slot_GetMetaTable,
  /*  10 */ (glua_vfn)ci_slot_Call,
  /*  11 */ (glua_vfn)ci_slot_PCall,
  /*  12 */ (glua_vfn)ci_slot_Equal,
  /*  13 */ (glua_vfn)ci_slot_RawEqual,
  /*  14 */ (glua_vfn)ci_slot_Insert,
  /*  15 */ (glua_vfn)ci_slot_Remove,
  /*  16 */ (glua_vfn)ci_slot_Next,
  /*  17 */ (glua_vfn)ci_slot_NewUserdata,
  /*  18 */ (glua_vfn)ci_slot_ThrowError,
  /*  19 */ (glua_vfn)ci_slot_CheckType,
  /*  20 */ (glua_vfn)ci_slot_ArgError,
  /*  21 */ (glua_vfn)ci_slot_RawGet,
  /*  22 */ (glua_vfn)ci_slot_RawSet,
  /*  23 */ (glua_vfn)ci_slot_GetString,
  /*  24 */ (glua_vfn)ci_slot_GetNumber,
  /*  25 */ (glua_vfn)ci_slot_GetBool,
  /*  26 */ (glua_vfn)ci_slot_GetCFunction,
  /*  27 */ (glua_vfn)ci_slot_GetUserdata,
  /*  28 */ (glua_vfn)ci_slot_PushNil,
  /*  29 */ (glua_vfn)ci_slot_PushString,
  /*  30 */ (glua_vfn)ci_slot_PushNumber,
  /*  31 */ (glua_vfn)ci_slot_PushBool,
  /*  32 */ (glua_vfn)ci_slot_PushCFunction,
  /*  33 */ (glua_vfn)ci_slot_PushCClosure,
  /*  34 */ (glua_vfn)ci_slot_PushUserdata,
  /*  35 */ (glua_vfn)ci_slot_ReferenceCreate,
  /*  36 */ (glua_vfn)ci_slot_ReferenceFree,
  /*  37 */ (glua_vfn)ci_slot_ReferencePush,
  /*  38 */ (glua_vfn)ci_slot_PushSpecial,
  /*  39 */ (glua_vfn)ci_slot_IsType,
  /*  40 */ (glua_vfn)ci_slot_GetType,
  /*  41 */ (glua_vfn)ci_slot_GetTypeName,
  /*  42 */ (glua_vfn)ci_slot_CreateMetaTableType,
  /*  43 */ (glua_vfn)ci_slot_CheckString,
  /*  44 */ (glua_vfn)ci_slot_CheckNumber,
  /*  45 */ (glua_vfn)ci_slot_ObjLen,
  /*  46 */ (glua_vfn)ci_slot_GetAngle,
  /*  47 */ (glua_vfn)ci_slot_GetVector,
  /*  48 */ (glua_vfn)ci_slot_PushAngle,
  /*  49 */ (glua_vfn)ci_slot_PushVector,
  /*  50 */ (glua_vfn)ci_slot_SetState,
  /*  51 */ (glua_vfn)ci_slot_CreateMetaTable,
  /*  52 */ (glua_vfn)ci_slot_PushMetaTable,
  /*  53 */ (glua_vfn)ci_slot_PushUserType,
  /*  54 */ (glua_vfn)ci_slot_SetUserType,
  /*  55 */ (glua_vfn)ci_slot_Init,
  /*  56 */ (glua_vfn)ci_slot_Shutdown,
  /*  57 */ (glua_vfn)ci_slot_Cycle,
  /*  58 */ (glua_vfn)ci_slot_Global,
  /*  59 */ (glua_vfn)ci_slot_GetObject,
  /*  60 */ (glua_vfn)ci_slot_PushLuaObject,
  /*  61 */ (glua_vfn)ci_slot_PushLuaFunction,
  /*  62 */ (glua_vfn)ci_slot_LuaError,
  /*  63 */ (glua_vfn)ci_slot_TypeError,
  /*  64 */ (glua_vfn)ci_slot_CallInternal,
  /*  65 */ (glua_vfn)ci_slot_CallInternalNoReturns,
  /*  66 */ (glua_vfn)ci_slot_CallInternalGetBool,
  /*  67 */ (glua_vfn)ci_slot_CallInternalGetString,
  /*  68 */ (glua_vfn)ci_slot_CallInternalGet,
  /*  69 */ (glua_vfn)ci_slot_NewGlobalTable,
  /*  70 */ (glua_vfn)ci_slot_NewTemporaryObject,
  /*  71 */ (glua_vfn)ci_slot_isUserData,
  /*  72 */ (glua_vfn)ci_slot_GetMetaTableObjectName,
  /*  73 */ (glua_vfn)ci_slot_GetMetaTableObjectIndex,
  /*  74 */ (glua_vfn)ci_slot_GetReturn,
  /*  75 */ (glua_vfn)ci_slot_IsServer,
  /*  76 */ (glua_vfn)ci_slot_IsClient,
  /*  77 */ (glua_vfn)ci_slot_IsMenu,
  /*  78 */ (glua_vfn)ci_slot_DestroyObject,
  /*  79 */ (glua_vfn)ci_slot_CreateObject,
  /*  80 */ (glua_vfn)ci_slot_SetMemberStrObj,
  /*  81 */ (glua_vfn)ci_slot_SetMemberStr,
  /*  82 */ (glua_vfn)ci_slot_SetMemberNumObj,
  /*  83 */ (glua_vfn)ci_slot_SetMemberNum,
  /*  84 */ (glua_vfn)ci_slot_SetMemberObjObj,
  /*  85 */ (glua_vfn)ci_slot_GetNewTable,
  /*  86 */ (glua_vfn)ci_slot_SetType,
  /*  87 */ (glua_vfn)ci_slot_PushLong,
  /*  88 */ (glua_vfn)ci_slot_GetFlags,
  /*  89 */ (glua_vfn)ci_slot_FindOnObjectsMetaTable,
  /*  90 */ (glua_vfn)ci_slot_FindObjectOnTable,
  /*  91 */ (glua_vfn)ci_slot_SetMemberFast,
  /*  92 */ (glua_vfn)ci_slot_RunString,
  /*  93 */ (glua_vfn)ci_slot_IsEqual,
  /*  94 */ (glua_vfn)ci_slot_Error,
  /*  95 */ (glua_vfn)ci_slot_GetStringOrError,
  /*  96 */ (glua_vfn)ci_slot_RunLuaModule,
  /*  97 */ (glua_vfn)ci_slot_FindAndRunScript,
  /*  98 */ (glua_vfn)ci_slot_SetPathID,
  /*  99 */ (glua_vfn)ci_slot_GetPathID,
  /* 100 */ (glua_vfn)ci_slot_ErrorNoHalt,
  /* 101 */ (glua_vfn)ci_slot_Msg,
  /* 102 */ (glua_vfn)ci_slot_PushPath,
  /* 103 */ (glua_vfn)ci_slot_PopPath,
  /* 104 */ (glua_vfn)ci_slot_GetPath,
  /* 105 */ (glua_vfn)ci_slot_GetColor,
  /* 106 */ (glua_vfn)ci_slot_PushColor,
  /* 107 */ (glua_vfn)ci_slot_GetStack,
  /* 108 */ (glua_vfn)ci_slot_GetInfo,
  /* 109 */ (glua_vfn)ci_slot_GetLocal,
  /* 110 */ (glua_vfn)ci_slot_GetUpvalue,
  /* 111 */ (glua_vfn)ci_slot_RunStringEx,
  /* 112 */ (glua_vfn)ci_slot_GetDataString,
  /* 113 */ (glua_vfn)ci_slot_ErrorFromLua,
  /* 114 */ (glua_vfn)ci_slot_GetCurrentLocation,
  /* 115 */ (glua_vfn)ci_slot_MsgColour,
  /* 116 */ (glua_vfn)ci_slot_GetCurrentFile,
  /* 117 */ (glua_vfn)ci_slot_CompileString,
  /* 118 */ (glua_vfn)ci_slot_CallFunctionProtected,
  /* 119 */ (glua_vfn)ci_slot_Require,
  /* 120 */ (glua_vfn)ci_slot_GetActualTypeName,
  /* 121 */ (glua_vfn)ci_slot_PreCreateTable,
  /* 122 */ (glua_vfn)ci_slot_PushPooledString,
  /* 123 */ (glua_vfn)ci_slot_GetPooledString,
  /* 124 */ (glua_vfn)ci_slot_AddThreadedCall,
  /* 125 */ (glua_vfn)ci_slot_AppendStackTrace,
  /* 126 */ (glua_vfn)ci_slot_CreateConVar,
  /* 127 */ (glua_vfn)ci_slot_CreateConCommand,
  /* 128 */ (glua_vfn)ci_slot_CheckStringOpt,
  /* 129 */ (glua_vfn)ci_slot_CheckNumberOpt,
  /* 130 */ (glua_vfn)ci_slot_RegisterMetaTable,
  /* 131 */ (glua_vfn)ci_slot_dtor
};

/* ------------------------------------------------------------------------ */
/* ILuaObject vtable (93 slots, published declaration order).  Bound-method */
/* entries are implemented; the remainder are safe stand-ins.               */

/* Generic stand-in for ILuaObject slots we do not implement: returns zero /
** NULL so a caller reads a benign default rather than garbage. */
static intptr_t gobj_stub(void) { return 0; }

static void gobj_Set(GLuaObject *obj, GLuaObject *other)
{
  if (gobj_state(obj) != NULL && obj->ref != LUA_NOREF && obj->ref != LUA_REFNIL)
    luaL_unref(obj->iface->state, LUA_REGISTRYINDEX, obj->ref);
  obj->ref = LUA_NOREF;
  if (other == NULL || other->iface == NULL || other->ref == LUA_NOREF) {
    obj->iface = other ? other->iface : obj->iface;
    return;
  }
  obj->iface = other->iface;
  gobj_push(other);
  obj->ref = luaL_ref(obj->iface->state, LUA_REGISTRYINDEX);
}

static void gobj_SetFromStack(GLuaObject *obj, int i)
{
  if (gobj_state(obj) != NULL) gobj_bind_stack(obj->iface, obj, i);
}

static void gobj_UnReference(GLuaObject *obj)
{
  if (gobj_state(obj) != NULL && obj->ref != LUA_NOREF && obj->ref != LUA_REFNIL)
    luaL_unref(obj->iface->state, LUA_REGISTRYINDEX, obj->ref);
  obj->ref = LUA_NOREF;
}

static int gobj_GetType(GLuaObject *obj)
{
  int t;
  if (gobj_state(obj) == NULL) return GT_NIL;
  gobj_push(obj);
  t = glua_gettype(obj->iface->state, -1);
  lua_pop(obj->iface->state, 1);
  return t;
}

static const char *gobj_GetString(GLuaObject *obj)
{
  const char *s;
  if (gobj_state(obj) == NULL) return NULL;
  gobj_push(obj);
  s = lua_tostring(obj->iface->state, -1);
  /* The value stays alive after popping: the object holds a registry
  ** reference to it. */
  lua_pop(obj->iface->state, 1);
  return s;
}

static float gobj_GetFloat(GLuaObject *obj)
{
  double d;
  if (gobj_state(obj) == NULL) return 0.0f;
  gobj_push(obj);
  d = lua_tonumber(obj->iface->state, -1);
  lua_pop(obj->iface->state, 1);
  return (float)d;
}

static int gobj_GetInt(GLuaObject *obj) { return (int)gobj_GetFloat(obj); }
static void *gobj_GetUserData(GLuaObject *obj)
{
  void *p;
  if (gobj_state(obj) == NULL) return NULL;
  gobj_push(obj);
  p = lua_touserdata(obj->iface->state, -1);
  lua_pop(obj->iface->state, 1);
  return p;
}

static void gobj_Push(GLuaObject *obj) { gobj_push(obj); }

static int gobj_isNil(GLuaObject *obj) { return gobj_GetType(obj) == GT_NIL; }
static int gobj_isTable(GLuaObject *obj) { return gobj_GetType(obj) == GT_TABLE; }
static int gobj_isString(GLuaObject *obj) { return gobj_GetType(obj) == GT_STRING; }
static int gobj_isNumber(GLuaObject *obj) { return gobj_GetType(obj) == GT_NUMBER; }
static int gobj_isFunction(GLuaObject *obj) { return gobj_GetType(obj) == GT_FUNCTION; }
static int gobj_isUserData(GLuaObject *obj) { return gobj_GetType(obj) == GT_USERDATA; }

static void gobj_SetMemberStr(GLuaObject *obj, const char *name)
{
  if (gobj_state(obj) == NULL) return;
  gobj_push(obj);
  lua_pushvalue(obj->iface->state, -2);
  lua_setfield(obj->iface->state, -2, name);
  lua_pop(obj->iface->state, 2);
}

static void gobj_SetMemberStrObj(GLuaObject *obj, const char *name, GLuaObject *val)
{
  if (gobj_state(obj) == NULL) return;
  gobj_push(obj);
  if (val) gobj_push(val); else lua_pushnil(obj->iface->state);
  lua_setfield(obj->iface->state, -2, name);
  lua_pop(obj->iface->state, 1);
}

static void gobj_SetMemberStrNum(GLuaObject *obj, const char *name, float val)
{
  if (gobj_state(obj) == NULL) return;
  gobj_push(obj);
  lua_pushnumber(obj->iface->state, val);
  lua_setfield(obj->iface->state, -2, name);
  lua_pop(obj->iface->state, 1);
}

static void gobj_SetMemberStrBool(GLuaObject *obj, const char *name, int val)
{
  if (gobj_state(obj) == NULL) return;
  gobj_push(obj);
  lua_pushboolean(obj->iface->state, val);
  lua_setfield(obj->iface->state, -2, name);
  lua_pop(obj->iface->state, 1);
}

static void gobj_SetMemberStrStr(GLuaObject *obj, const char *name, const char *val)
{
  if (gobj_state(obj) == NULL) return;
  gobj_push(obj);
  lua_pushstring(obj->iface->state, val);
  lua_setfield(obj->iface->state, -2, name);
  lua_pop(obj->iface->state, 1);
}

static void gobj_SetMemberStrFn(GLuaObject *obj, const char *name, int (*f)(lua_State *))
{
  if (gobj_state(obj) == NULL) return;
  gobj_push(obj);
  lua_pushcclosure(obj->iface->state, f, 0);
  lua_setfield(obj->iface->state, -2, name);
  lua_pop(obj->iface->state, 1);
}

static int gobj_GetMemberBool(GLuaObject *obj, const char *name, int def)
{
  int r = def;
  if (gobj_state(obj) == NULL) return def;
  gobj_push(obj);
  lua_getfield(obj->iface->state, -1, name);
  if (lua_type(obj->iface->state, -1) == GT_BOOL) r = lua_toboolean(obj->iface->state, -1);
  lua_pop(obj->iface->state, 2);
  return r;
}

static int gobj_GetMemberInt(GLuaObject *obj, const char *name, int def)
{
  int r = def;
  if (gobj_state(obj) == NULL) return def;
  gobj_push(obj);
  lua_getfield(obj->iface->state, -1, name);
  if (lua_type(obj->iface->state, -1) == GT_NUMBER) r = (int)lua_tonumber(obj->iface->state, -1);
  lua_pop(obj->iface->state, 2);
  return r;
}

static float gobj_GetMemberFloat(GLuaObject *obj, const char *name, float def)
{
  float r = def;
  if (gobj_state(obj) == NULL) return def;
  gobj_push(obj);
  lua_getfield(obj->iface->state, -1, name);
  if (lua_type(obj->iface->state, -1) == GT_NUMBER) r = (float)lua_tonumber(obj->iface->state, -1);
  lua_pop(obj->iface->state, 2);
  return r;
}

static const char *gobj_GetMemberStr(GLuaObject *obj, const char *name, const char *def)
{
  const char *r = def;
  if (gobj_state(obj) == NULL) return def;
  gobj_push(obj);
  lua_getfield(obj->iface->state, -1, name);
  if (lua_type(obj->iface->state, -1) == GT_STRING) r = lua_tostring(obj->iface->state, -1);
  lua_pop(obj->iface->state, 2);
  return r;
}

static void gobj_SetMemberNum(GLuaObject *obj, float key)
{
  if (gobj_state(obj) == NULL) return;
  gobj_push(obj);
  lua_pushvalue(obj->iface->state, -2);
  lua_pushnumber(obj->iface->state, key);
  lua_settable(obj->iface->state, -3);
  lua_pop(obj->iface->state, 2);
}

static void gobj_SetMemberNumVal(GLuaObject *obj, float key, float val)
{
  if (gobj_state(obj) == NULL) return;
  gobj_push(obj);
  lua_pushnumber(obj->iface->state, key);
  lua_pushnumber(obj->iface->state, val);
  lua_settable(obj->iface->state, -3);
  lua_pop(obj->iface->state, 1);
}

static void gobj_SetMemberNumObj(GLuaObject *obj, float key, GLuaObject *val)
{
  if (gobj_state(obj) == NULL) return;
  gobj_push(obj);
  lua_pushnumber(obj->iface->state, key);
  if (val) gobj_push(val); else lua_pushnil(obj->iface->state);
  lua_settable(obj->iface->state, -3);
  lua_pop(obj->iface->state, 1);
}

static void gobj_SetMemberObjObj(GLuaObject *obj, GLuaObject *k, GLuaObject *v)
{
  if (gobj_state(obj) == NULL) return;
  gobj_push(obj);
  if (k) gobj_push(k); else lua_pushnil(obj->iface->state);
  if (v) gobj_push(v); else lua_pushnil(obj->iface->state);
  lua_settable(obj->iface->state, -3);
  lua_pop(obj->iface->state, 1);
}

static void gobj_SetReference(GLuaObject *obj, int i)
{
  if (obj->iface) obj->ref = i;
}

static void gobj_SetFromGlobal(GLuaObject *obj, const char *name)
{
  if (gobj_state(obj) == NULL) return;
  lua_getglobal(obj->iface->state, name);
  gobj_bind_stack(obj->iface, obj, -1);
  lua_pop(obj->iface->state, 1);
}

static int gobj_Init(GLuaObject *obj)
{
  if (gobj_state(obj) != NULL && obj->ref != LUA_NOREF && obj->ref != LUA_REFNIL)
    luaL_unref(obj->iface->state, LUA_REGISTRYINDEX, obj->ref);
  obj->ref = LUA_NOREF;
  return 0;
}

static float gobj_GetFloat0(GLuaObject *obj) { return gobj_GetFloat(obj); }
static int gobj_GetBool0(GLuaObject *obj) { return gobj_GetType(obj) == GT_BOOL; }
static int gobj_RemoveMe(GLuaObject *obj) { (void)obj; return 0; }

static void gobj_SetFloat(GLuaObject *obj, float v)
{
  (void)obj; (void)v;
}
static void gobj_SetString(GLuaObject *obj, const char *v) { (void)obj; (void)v; }
static double gobj_GetDouble(GLuaObject *obj) { return (double)gobj_GetFloat(obj); }
static int gobj_GetStringLen(GLuaObject *obj, unsigned int *len)
{
  if (!obj->iface) { if (len) *len = 0; return 0; }
  gobj_push(obj);
  if (len) *len = (unsigned int)lua_objlen(obj->iface->state, -1);
  lua_pop(obj->iface->state, 1);
  return 0;
}

const glua_vfn glua_object_vtable[93] = {
  /*  0 */ (glua_vfn)gobj_Set,
  /*  1 */ (glua_vfn)gobj_SetFromStack,
  /*  2 */ (glua_vfn)gobj_UnReference,
  /*  3 */ (glua_vfn)gobj_GetType,
  /*  4 */ (glua_vfn)gobj_GetString,
  /*  5 */ (glua_vfn)gobj_GetFloat,
  /*  6 */ (glua_vfn)gobj_GetInt,
  /*  7 */ (glua_vfn)gobj_GetUserData,
  /*  8 */ (glua_vfn)gobj_SetMemberStr,
  /*  9 */ (glua_vfn)gobj_SetMemberStrObj,
  /* 10 */ (glua_vfn)gobj_SetMemberStrNum,
  /* 11 */ (glua_vfn)gobj_SetMemberStrBool,
  /* 12 */ (glua_vfn)gobj_SetMemberStrStr,
  /* 13 */ (glua_vfn)gobj_SetMemberStrFn,
  /* 14 */ (glua_vfn)gobj_GetMemberBool,
  /* 15 */ (glua_vfn)gobj_GetMemberInt,
  /* 16 */ (glua_vfn)gobj_GetMemberFloat,
  /* 17 */ (glua_vfn)gobj_GetMemberStr,
  /* 18 */ (glua_vfn)gobj_stub,
  /* 19 */ (glua_vfn)gobj_stub,
  /* 20 */ (glua_vfn)gobj_stub,
  /* 21 */ (glua_vfn)gobj_stub,
  /* 22 */ (glua_vfn)gobj_stub,
  /* 23 */ (glua_vfn)gobj_stub,
  /* 24 */ (glua_vfn)gobj_Push,
  /* 25 */ (glua_vfn)gobj_isNil,
  /* 26 */ (glua_vfn)gobj_isTable,
  /* 27 */ (glua_vfn)gobj_isString,
  /* 28 */ (glua_vfn)gobj_isNumber,
  /* 29 */ (glua_vfn)gobj_isFunction,
  /* 30 */ (glua_vfn)gobj_isUserData,
  /* 31 */ (glua_vfn)gobj_stub,
  /* 32 */ (glua_vfn)gobj_stub,
  /* 33 */ (glua_vfn)gobj_SetMemberNum,
  /* 34 */ (glua_vfn)gobj_SetMemberNumObj,
  /* 35 */ (glua_vfn)gobj_SetMemberNumVal,
  /* 36 */ (glua_vfn)gobj_stub,
  /* 37 */ (glua_vfn)gobj_stub,
  /* 38 */ (glua_vfn)gobj_stub,
  /* 39 */ (glua_vfn)gobj_stub,
  /* 40 */ (glua_vfn)gobj_SetMemberObjObj,
  /* 41 */ (glua_vfn)gobj_GetBool0,
  /* 42 */ (glua_vfn)gobj_stub,
  /* 43 */ (glua_vfn)gobj_stub,
  /* 44 */ (glua_vfn)gobj_SetFloat,
  /* 45 */ (glua_vfn)gobj_SetString,
  /* 46 */ (glua_vfn)gobj_GetDouble,
  /* 47 */ (glua_vfn)gobj_stub,
  /* 48 */ (glua_vfn)gobj_stub,
  /* 49 */ (glua_vfn)gobj_stub,
  /* 50 */ (glua_vfn)gobj_stub,
  /* 51 */ (glua_vfn)gobj_stub,
  /* 52 */ (glua_vfn)gobj_stub,
  /* 53 */ (glua_vfn)gobj_stub,
  /* 54 */ (glua_vfn)gobj_stub,
  /* 55 */ (glua_vfn)gobj_stub,
  /* 56 */ (glua_vfn)gobj_RemoveMe,
  /* 57 */ (glua_vfn)gobj_Init,
  /* 58 */ (glua_vfn)gobj_SetFromGlobal,
  /* 59 */ (glua_vfn)gobj_GetStringLen,
  /* 60 */ (glua_vfn)gobj_stub,
  /* 61 */ (glua_vfn)gobj_stub,
  /* 62 */ (glua_vfn)gobj_stub,
  /* 63 */ (glua_vfn)gobj_SetReference,
  /* 64 */ (glua_vfn)gobj_stub,
  /* 65 */ (glua_vfn)gobj_stub,
  /* 66 */ (glua_vfn)gobj_stub,
  /* 67 */ (glua_vfn)gobj_stub,
  /* 68 */ (glua_vfn)gobj_stub,
  /* 69 */ (glua_vfn)gobj_stub,
  /* 70 */ (glua_vfn)gobj_stub,
  /* 71 */ (glua_vfn)gobj_stub,
  /* 72 */ (glua_vfn)gobj_stub,
  /* 73 */ (glua_vfn)gobj_stub,
  /* 74 */ (glua_vfn)gobj_stub,
  /* 75 */ (glua_vfn)gobj_stub,
  /* 76 */ (glua_vfn)gobj_stub,
  /* 77 */ (glua_vfn)gobj_stub,
  /* 78 */ (glua_vfn)gobj_stub,
  /* 79 */ (glua_vfn)gobj_stub,
  /* 80 */ (glua_vfn)gobj_stub,
  /* 81 */ (glua_vfn)gobj_stub,
  /* 82 */ (glua_vfn)gobj_stub,
  /* 83 */ (glua_vfn)gobj_stub,
  /* 84 */ (glua_vfn)gobj_stub,
  /* 85 */ (glua_vfn)gobj_stub,
  /* 86 */ (glua_vfn)gobj_stub,
  /* 87 */ (glua_vfn)gobj_stub,
  /* 88 */ (glua_vfn)gobj_stub,
  /* 89 */ (glua_vfn)gobj_stub,
  /* 90 */ (glua_vfn)gobj_stub,
  /* 91 */ (glua_vfn)gobj_stub,
  /* 92 */ (glua_vfn)gobj_stub
};

/* ------------------------------------------------------------------------ */
/* ILuaInterface slots 55..131                                              */

static void glua_warn_once(const char *what)
{
  /* Small seen-set so alternating stand-ins each report once. */
  static const char *seen[16];
  static int seenCount = 0;
  int i;
  for (i = 0; i < seenCount; i++)
    if (seen[i] == what) return;
  if (seenCount < 16) seen[seenCount++] = what;
  fprintf(stdout, "[opglua] safe stand-in for %s (needs the host game)\n", what);
  fflush(stdout);
}

/* _LOADLIB holder used by the binary module loader. */
typedef struct GLuaLoadLib {
#ifdef _WIN32
  HMODULE hmod;
#else
  void *hmod;
#endif
} GLuaLoadLib;

static int loadlib_close(lua_State *L)
{
  GLuaLoadLib *holder = (GLuaLoadLib *)luaL_checkudata(L, 1, "_LOADLIB");
  if (holder->hmod != NULL) {
#ifdef _WIN32
    FARPROC closefn = GetProcAddress(holder->hmod, "gmod13_close");
    if (closefn != NULL) {
      lua_pushcclosure(L, (lua_CFunction)(void *)closefn, 0);
      lua_call(L, 0, 0);
    }
    FreeLibrary(holder->hmod);
#else
    {
      void *closefn = dlsym(holder->hmod, "gmod13_close");
      if (closefn != NULL) {
        lua_pushcclosure(L, (lua_CFunction)closefn, 0);
        lua_call(L, 0, 0);
      }
      dlclose(holder->hmod);
    }
#endif
    holder->hmod = NULL;
  }
  return 0;
}

/* Load a module and resolve a symbol.  Return values match the reference
** worker: 0 = symbol pushed (or load-only true), 1 = load failure,
** 2 = symbol not found. */
static int glua_open_module(lua_State *L, const char *path, const char *symbol)
{
  GLuaLoadLib *holder;

  lua_pushfstring(L, "LOADLIB: %s", path);
  lua_gettable(L, LUA_REGISTRYINDEX);
  if (lua_type(L, -1) == 0) {          /* not cached yet */
    lua_pop(L, 1);
    holder = (GLuaLoadLib *)lua_newuserdata(L, sizeof(GLuaLoadLib));
    holder->hmod = NULL;
    luaL_setmetatable(L, "_LOADLIB");
    lua_pushfstring(L, "LOADLIB: %s", path);
    lua_pushvalue(L, -2);
    lua_settable(L, LUA_REGISTRYINDEX);
  } else {
    holder = (GLuaLoadLib *)lua_touserdata(L, -1);
  }

  if (holder->hmod == NULL) {
#ifdef _WIN32
    holder->hmod = LoadLibraryExA(path, NULL, 0);
#else
    holder->hmod = dlopen(path, RTLD_NOW);
#endif
    if (holder->hmod == NULL) {
      lua_pushfstring(L, "Couldn't load module library! (%s)", path);
      return 1;      /* load failure */
    }
  }

  if (symbol[0] == '*') {                    /* load-only request */
    lua_pushboolean(L, 1);
    return 0;
  }

#ifdef _WIN32
  {
    FARPROC fn = GetProcAddress(holder->hmod, symbol);
    if (fn != NULL) { lua_pushcclosure(L, (lua_CFunction)(void *)fn, 0); return 0; }
  }
#else
  {
    void *fn = dlsym(holder->hmod, symbol);
    if (fn != NULL) { lua_pushcclosure(L, (lua_CFunction)fn, 0); return 0; }
  }
#endif

  /* The named entry is absent.  The reference falls back to a luaopen_*
  ** derivation (strip up to the last '-', '.' becomes '_') resolved against
  ** the module, then the host process, then the caller's own image, and
  ** loads the result as a raw bytecode chunk. */
  {
    char derived[256];
    const char *base = strrchr(symbol, '-');
    const char *p;
    size_t n = 0;
    void *fn = NULL;
    base = (base != NULL) ? base + 1 : symbol;
    for (p = "luaopen_"; *p != '\0' && n < sizeof(derived) - 1; p++)
      derived[n++] = *p;
    for (p = base; *p != '\0' && n < sizeof(derived) - 1; p++)
      derived[n++] = (*p == '.') ? '_' : *p;
    derived[n] = '\0';
#ifdef _WIN32
    {
      FARPROC proc = NULL;
      HMODULE host = NULL;
      if (holder->hmod != NULL)
        proc = GetProcAddress(holder->hmod, derived);
      if (proc == NULL) {
        host = GetModuleHandleA(NULL);
        if (host != NULL) proc = GetProcAddress(host, derived);
      }
      if (proc == NULL &&
          GetModuleHandleExA(6, (LPCSTR)(const void *)&glua_open_module, &host))
        proc = GetProcAddress(host, derived);
      fn = (void *)proc;
    }
#else
    fn = dlsym(RTLD_DEFAULT, derived);
#endif
    if (fn != NULL) {
      if (luaL_loadbuffer(L, (const char *)fn, 0x7fffff00, symbol) != 0)
        return 3;
      return 0;
    }
  }

  lua_pushfstring(L, "Couldn't find function in library! (%s)", symbol);
  return 2;                                  /* symbol not found */
}

/* The exported loader.  Return codes match the reference:
**  -1 load failure, -2 gmod13_open missing, 1 called, 0 fallback. */
LUALIB_API int GMOD_LoadBinaryModule(lua_State *L, const char *path)
{
  int r = glua_open_module(L, path, "gmod13_open");
  if (r == 1) return -1;
  if (r == 2) return -2;
  if (r == 0) {
    lua_pushstring(L, path);
    lua_call(L, 1, 1);
    return 1;
  }
  lua_settop(L, -2);
  return 0;
}

static int ci_slot_Init(CLuaInterface *self, void *cb, bool dedicated)
{
  lua_State *L;
  (void)cb; (void)dedicated;
  if (self->state != NULL) return false;

  L = luaL_newstate();
  if (L == NULL) {
    fputs("Unable to create new Lua State! Couldn't alloc memory?\n", stderr);
    return false;
  }
  self->state = L;
  L->luabase = self;                    /* the module-visible back pointer */

  luaL_openlibs(L);
  lua_atpanic(L, NULL);

  /* Custom metatable ids start after the builtin type range so they can
  ** never collide with a GarrysMod::Lua::Type value. */
  self->metaIDCounter = GT_TYPE_COUNT;

  /* Register the loader's _LOADLIB metatable (handle cache for modules). */
  luaL_newmetatable(L, "_LOADLIB");
  lua_pushcclosure(L, loadlib_close, 0);
  lua_setfield(L, -2, "__gc");
  lua_pop(L, 1);

  /* A small metatable for GLuaColor so PushColor/GetColor round-trip. */
  self->colorMetaID = ci_slot_CreateMetaTable(self, "Color");

  /* Global table object. */
  self->global = glua_object_alloc(self);
  if (self->global != NULL) {
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    gobj_bind_stack(self, self->global, -1);
    lua_pop(L, 1);
  }
  self->callback.vtable = (const void *)glua_callback_vtable;
  self->savedMetas = 1;
  return true;
}

static void ci_slot_Shutdown(CLuaInterface *self)
{
  if (self->state != NULL) {
    lua_close(self->state);
    self->state = NULL;
  }
}

static void ci_slot_Cycle(CLuaInterface *self)
{
  int i;
  for (i = 0; i < 4; i++) self->returns.objs[i] = NULL;
  /* Threaded calls are pumped here in the reference; ours is a no-op list. */
  self->threadedHead = NULL;
  self->threadedCount = 0;
}

static GLuaObject *ci_slot_Global(CLuaInterface *self) { return self->global; }

static GLuaObject *ci_slot_GetObject(CLuaInterface *self, int index)
{
  GLuaObject *obj = glua_object_alloc(self);
  if (obj == NULL) return NULL;
  gobj_bind_stack(self, obj, index);
  return obj;
}

static void ci_slot_PushLuaObject(CLuaInterface *self, GLuaObject *obj)
{
  (void)self;
  if (obj == NULL) { if (self->state) lua_pushnil(self->state); return; }
  gobj_push(obj);
}

static void ci_slot_PushLuaFunction(CLuaInterface *self, int (*f)(lua_State *))
{ lua_pushcclosure(self->state, f, 0); }

static void ci_slot_LuaError(CLuaInterface *self, const char *err, int index)
{
  if (index != -1) luaL_argerror(self->state, index, err);
  else ci_slot_ErrorNoHalt(self, "%s", err);
}

static void ci_slot_TypeError(CLuaInterface *self, const char *name, int index)
{ luaL_typerror(self->state, index, name); }

/* Protected call helper shared by the CallInternal family. */
static int glua_pcall_int(CLuaInterface *self, int args, int rets)
{
  lua_State *L = self->state;
  int base = lua_gettop(L) - args;
  if (base < 1 || lua_type(L, base) != GT_FUNCTION) {
    luaL_error(L, "Lua tried to call a non-function");
    return 0;
  }
  return lua_pcall(L, args, rets, 0);
}

static void ci_slot_CallInternal(CLuaInterface *self, int args, int rets)
{
  int i;
  if (rets > 4) { luaL_error(self->state, "too many returns"); return; }
  for (i = 0; i < 4; i++) self->returns.objs[i] = NULL;
  if (glua_pcall_int(self, args, rets) != 0) return;
  for (i = 0; i < rets; i++) {
    if (self->returns.objs[i] == NULL) self->returns.objs[i] = glua_object_alloc(self);
    if (self->returns.objs[i] != NULL)
      gobj_bind_stack(self, self->returns.objs[i], -(rets - i));
  }
  lua_pop(self->state, rets);
}

static void ci_slot_CallInternalNoReturns(CLuaInterface *self, int args)
{ (void)glua_pcall_int(self, args, 0); }

static int ci_slot_CallInternalGetBool(CLuaInterface *self, int args)
{
  int r = 0;
  if (glua_pcall_int(self, args, 1) == 0) {
    r = lua_toboolean(self->state, -1) != 0;
    lua_pop(self->state, 1);
  }
  return r;
}

static const char *ci_slot_CallInternalGetString(CLuaInterface *self, int args)
{
  const char *r = NULL;
  if (glua_pcall_int(self, args, 1) == 0) {
    if (lua_type(self->state, -1) == GT_STRING) r = lua_tostring(self->state, -1);
    lua_pop(self->state, 1);
  }
  return r;
}

static int ci_slot_CallInternalGet(CLuaInterface *self, int args, GLuaObject *obj)
{
  if (glua_pcall_int(self, args, 1) != 0) return 0;
  if (obj != NULL) {
    gobj_bind_stack(self, obj, -1);
    lua_pop(self->state, 1);
    return 1;
  }
  lua_pop(self->state, 1);
  return 0;
}

static void ci_slot_NewGlobalTable(CLuaInterface *self, const char *name)
{
  lua_State *L = self->state;
  lua_createtable(L, 0, 0);
  lua_setfield(L, LUA_GLOBALSINDEX, name);
}

static GLuaObject *ci_slot_NewTemporaryObject(CLuaInterface *self)
{
  int idx = ++self->tempIndex;
  if (idx > 31) { idx = 0; self->tempIndex = 0; }
  if (self->temp[idx] == NULL) self->temp[idx] = glua_object_alloc(self);
  else if (self->temp[idx]->iface && self->temp[idx]->ref != LUA_NOREF)
    luaL_unref(self->temp[idx]->iface->state, LUA_REGISTRYINDEX, self->temp[idx]->ref),
    self->temp[idx]->ref = LUA_NOREF;
  return self->temp[idx];
}

static int ci_slot_isUserData(CLuaInterface *self, int index)
{ return lua_type(self->state, index) == GT_USERDATA; }

static GLuaObject *ci_slot_GetMetaTableObjectName(CLuaInterface *self, const char *name, int type)
{
  lua_State *L = self->state;
  lua_getfield(L, LUA_REGISTRYINDEX, name);
  if (lua_type(L, -1) != GT_TABLE) {
    lua_pop(L, 1);
    if (type == -1) return NULL;
    ci_slot_CreateMetaTableType(self, name, type);
    /* re-fetch */
    lua_getfield(L, LUA_REGISTRYINDEX, name);
  }
  {
    GLuaObject *obj = glua_object_alloc(self);
    if (obj != NULL) gobj_bind_stack(self, obj, -1);
    lua_pop(L, 1);
    return obj;
  }
}

static GLuaObject *ci_slot_GetMetaTableObjectIndex(CLuaInterface *self, int index)
{
  lua_State *L = self->state;
  if (!lua_getmetatable(L, index)) return NULL;
  {
    GLuaObject *obj = glua_object_alloc(self);
    if (obj != NULL) gobj_bind_stack(self, obj, -1);
    lua_pop(L, 1);
    return obj;
  }
}

static GLuaObject *ci_slot_GetReturn(CLuaInterface *self, int index)
{
  if (index < 0 || index > 3) luaL_error(self->state, "Tried to get return higher than max");
  return self->returns.objs[index];
}

static int ci_slot_IsServer(CLuaInterface *self) { return self->realm == GLUA_REALM_SERVER; }
static int ci_slot_IsClient(CLuaInterface *self) { return self->realm == GLUA_REALM_CLIENT; }
static int ci_slot_IsMenu(CLuaInterface *self) { return self->realm == GLUA_REALM_MENU; }

static void ci_slot_DestroyObject(CLuaInterface *self, GLuaObject *obj)
{
  if (obj == NULL) return;
  gobj_UnReference(obj);
  free(obj);
}

static GLuaObject *ci_slot_CreateObject(CLuaInterface *self) { return glua_object_alloc(self); }

static void ci_slot_SetMemberStrObj(CLuaInterface *self, GLuaObject *t, const char *k, GLuaObject *v)
{
  (void)self;
  if (gobj_state(t) == NULL) return;
  gobj_push(t);
  if (v) gobj_push(v); else lua_pushnil(t->iface->state);
  lua_setfield(t->iface->state, -2, k);
  lua_pop(t->iface->state, 1);
}

static void ci_slot_SetMemberStr(CLuaInterface *self, GLuaObject *t, const char *k)
{
  (void)self;
  if (gobj_state(t) == NULL) return;
  gobj_push(t);
  lua_pushvalue(t->iface->state, -2);
  lua_setfield(t->iface->state, -2, k);
  lua_pop(t->iface->state, 2);
}

static void ci_slot_SetMemberNumObj(CLuaInterface *self, GLuaObject *t, float k, GLuaObject *v)
{
  (void)self;
  if (gobj_state(t) == NULL) return;
  gobj_push(t);
  lua_pushnumber(t->iface->state, k);
  if (v) gobj_push(v); else lua_pushnil(t->iface->state);
  lua_settable(t->iface->state, -3);
  lua_pop(t->iface->state, 1);
}

static void ci_slot_SetMemberNum(CLuaInterface *self, GLuaObject *t, float k)
{
  (void)self;
  if (gobj_state(t) == NULL) return;
  gobj_push(t);
  lua_pushnumber(t->iface->state, k);
  lua_pushvalue(t->iface->state, -3);
  lua_settable(t->iface->state, -3);
  lua_pop(t->iface->state, 2);
}

static void ci_slot_SetMemberObjObj(CLuaInterface *self, GLuaObject *t, GLuaObject *k, GLuaObject *v)
{
  (void)self;
  if (gobj_state(t) == NULL || k == NULL) return;
  gobj_push(t);
  gobj_push(k);
  if (v) gobj_push(v); else lua_pushnil(t->iface->state);
  lua_settable(t->iface->state, -3);
  lua_pop(t->iface->state, 1);
}

static GLuaObject *ci_slot_GetNewTable(CLuaInterface *self)
{
  lua_State *L = self->state;
  GLuaObject *obj;
  lua_createtable(L, 0, 0);
  obj = glua_object_alloc(self);
  if (obj != NULL) gobj_bind_stack(self, obj, -1);
  lua_pop(L, 1);
  return obj;
}

static void ci_slot_SetType(CLuaInterface *self, unsigned char realm) { self->realm = realm; }
static void ci_slot_PushLong(CLuaInterface *self, long num)
{ lua_pushnumber(self->state, (double)num); }

static int ci_slot_GetFlags(CLuaInterface *self, int index)
{ (void)self; (void)index; return 0; }

static int ci_slot_FindOnObjectsMetaTable(CLuaInterface *self, int obj, int key)
{
  lua_State *L = self->state;
  if (!lua_getmetatable(L, obj)) { lua_pushnil(L); return 0; }
  lua_pushvalue(L, key);
  lua_gettable(L, -2);
  lua_remove(L, -2);
  return lua_type(L, -1) != 0;
}

static int ci_slot_FindObjectOnTable(CLuaInterface *self, int tbl, int key)
{
  lua_State *L = self->state;
  lua_pushvalue(L, tbl);
  lua_pushvalue(L, key);
  lua_gettable(L, -2);
  lua_remove(L, -2);
  return lua_type(L, -1) != 0;
}

static void ci_slot_SetMemberFast(CLuaInterface *self, GLuaObject *t, int k, int v)
{
  lua_State *L = self->state;
  if (t == NULL) return;
  gobj_push(t);
  lua_pushvalue(L, k);
  lua_pushvalue(L, v);
  lua_settable(L, -3);
  lua_pop(L, 1);
}

/* Compile + optionally run a string.  Bytecode (leading 0x1b) is refused,
** matching the reference. */
static int glua_run_ex(CLuaInterface *self, const char *filename, const char *path,
                       const char *str, bool run, bool printErrors,
                       bool dontPushErrors, bool noReturns)
{
  lua_State *L = self->state;
  char cn[512];
  const char *chunkname;
  int base;
  (void)path; (void)dontPushErrors;
  if (str == NULL || str[0] == '\0') return 0;
  if (str[0] == '\033') {                /* 0x1b: reject bytecode */
    if (printErrors) ci_slot_ErrorNoHalt(self, "Cannot run byte code!");
    return 0;
  }
  /* Chunk names are prefixed with '@', like the reference. */
  if (filename != NULL && filename[0] != '\0') {
    snprintf(cn, sizeof(cn), "@%s", filename);
    chunkname = cn;
  } else {
    chunkname = "?";
  }
  base = lua_gettop(L);
  if (luaL_loadbufferx(L, str, strlen(str), chunkname, NULL) != 0) {
    if (printErrors) ci_slot_ErrorNoHalt(self, "%s", lua_tostring(L, -1));
    if (dontPushErrors) lua_pop(L, 1);
    return 0;
  }
  if (!run) return 1;
  if (lua_pcall(L, 0, noReturns ? 0 : LUA_MULTRET, 0) != 0) {
    if (printErrors) ci_slot_ErrorNoHalt(self, "%s", lua_tostring(L, -1));
    lua_settop(L, base);
    return 0;
  }
  lua_settop(L, base);
  return 1;
}

static int ci_slot_RunString(CLuaInterface *self, const char *fn, const char *path,
                             const char *str, bool run, bool showErrors)
{ return glua_run_ex(self, fn, path, str, run, showErrors, false, false); }

static int ci_slot_RunStringEx(CLuaInterface *self, const char *fn, const char *path,
                               const char *str, bool run, bool printErrors,
                               bool dontPushErrors, bool noReturns)
{ return glua_run_ex(self, fn, path, str, run, printErrors, dontPushErrors, noReturns); }

static int ci_slot_IsEqual(CLuaInterface *self, GLuaObject *a, GLuaObject *b)
{
  lua_State *L = self->state;
  int r;
  if (a == NULL || b == NULL) return 0;
  if (gobj_GetType(a) != gobj_GetType(b)) return 0;
  gobj_push(a);
  gobj_push(b);
  r = lua_equal(L, -1, -2);
  lua_pop(L, 2);
  return r;
}

static void ci_slot_Error(CLuaInterface *self, const char *err)
{ luaL_error(self->state, "%s", err ? err : ""); }

static const char *ci_slot_GetStringOrError(CLuaInterface *self, int index)
{
  const char *s = lua_tostring(self->state, index);
  if (s == NULL) ci_slot_TypeError(self, "string", index);
  return s;
}

/* Load includes/modules/<name>.lua once, caching in _MODULES. */
static int ci_slot_RunLuaModule(CLuaInterface *self, const char *name)
{ return ci_slot_FindAndRunScript(self, name, true, true, "!MODULE", false); }

static int ci_slot_FindAndRunScript(CLuaInterface *self, const char *fn, bool run,
                                    bool showErrors, const char *str, bool noReturns)
{
  lua_State *L = self->state;
  char full[512];
  size_t len = strlen(fn);
  FILE *f;
  char *buf;
  long size;
  int ok;
  (void)str;
  if (len < 4 || glua_stricmp(fn + len - 4, ".lua") != 0) {
    if (showErrors) ci_slot_ErrorNoHalt(self, "Couldn't include file '%s' - Not a .lua file!\n", fn);
    return 0;
  }
  snprintf(full, sizeof(full), "%s", fn);
  {
    size_t i;
    for (i = 0; full[i]; i++) if (full[i] == '\\') full[i] = '/';
  }
  f = fopen(full, "rb");
  if (f == NULL) {
    if (showErrors) ci_slot_ErrorNoHalt(self, "Couldn't include file '%s' - File not found or is empty\n", fn);
    return 0;
  }
  fseek(f, 0, SEEK_END);
  size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size <= 0) {
    fclose(f);
    if (showErrors) ci_slot_ErrorNoHalt(self, "Not running script %s - it's too short.\n", fn);
    return 0;
  }
  buf = (char *)malloc((size_t)size + 1);
  if (buf == NULL) { fclose(f); return 0; }
  if (fread(buf, 1, (size_t)size, f) != (size_t)size) { fclose(f); free(buf); return 0; }
  buf[size] = '\0';
  fclose(f);
  ok = glua_run_ex(self, fn, NULL, buf, run, showErrors, false, noReturns);
  free(buf);
  (void)L;
  return ok;
}

static void ci_slot_SetPathID(CLuaInterface *self, const char *id)
{
  if (id == NULL) id = "";
  strncpy(self->pathID, id, sizeof(self->pathID) - 1);
  self->pathID[sizeof(self->pathID) - 1] = '\0';
}

static const char *ci_slot_GetPathID(CLuaInterface *self) { return self->pathID; }

static void glua_vmsg(CLuaInterface *self, const char *fmt, va_list ap, int errHalt)
{
  char buf[4096];
  vsnprintf(buf, sizeof(buf), fmt, ap);
  if (errHalt) {
    gc_ErrorPrint(&self->callback, buf, true);
  } else {
    gc_Msg(&self->callback, buf, true);
  }
}

static void ci_slot_ErrorNoHalt(CLuaInterface *self, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  glua_vmsg(self, fmt, ap, 1);
  va_end(ap);
}

static void ci_slot_Msg(CLuaInterface *self, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  glua_vmsg(self, fmt, ap, 0);
  va_end(ap);
}

static void ci_slot_PushPath(CLuaInterface *self, const char *path)
{
  if (path != NULL) {
    strncpy(self->path, path, sizeof(self->path) - 1);
    self->path[sizeof(self->path) - 1] = '\0';
  }
  self->pushedPaths++;
}

static void ci_slot_PopPath(CLuaInterface *self)
{
  if (self->pushedPaths <= 0) {
    ci_slot_ErrorNoHalt(self, "CLuaInterface::PopPath - Overpopped!\n");
    self->pushedPaths--;
    return;
  }
  self->pushedPaths--;
}

static const char *ci_slot_GetPath(CLuaInterface *self)
{ return self->pushedPaths > 0 ? self->path : NULL; }

/* GetColor: the reference reads a Color object (a table with r/g/b/a) and
** packs it as a little-endian RGBA int; a non-table yields 0xFFFFFFFF. */
static int ci_slot_GetColor(CLuaInterface *self, int index)
{
  lua_State *L = self->state;
  unsigned char r, g, b, a;
  if (lua_type(L, index) != GT_TABLE) return -1;
  lua_getfield(L, index, "r"); r = (unsigned char)lua_tonumber(L, -1); lua_pop(L, 1);
  lua_getfield(L, index, "g"); g = (unsigned char)lua_tonumber(L, -1); lua_pop(L, 1);
  lua_getfield(L, index, "b"); b = (unsigned char)lua_tonumber(L, -1); lua_pop(L, 1);
  lua_getfield(L, index, "a"); a = (unsigned char)lua_tonumber(L, -1); lua_pop(L, 1);
  return (int)((a << 24) | (b << 16) | (g << 8) | r);
}

static void *ci_slot_PushColor(CLuaInterface *self, GLuaColor color)
{
  lua_State *L = self->state;
  lua_createtable(L, 0, 4);
  lua_pushnumber(L, color.r); lua_setfield(L, -2, "r");
  lua_pushnumber(L, color.g); lua_setfield(L, -2, "g");
  lua_pushnumber(L, color.b); lua_setfield(L, -2, "b");
  lua_pushnumber(L, color.a); lua_setfield(L, -2, "a");
  if (self->metaTables[self->colorMetaID] != NULL) {
    gobj_push(self->metaTables[self->colorMetaID]);
    lua_setmetatable(L, -2);
  }
  return NULL;
}

static int ci_slot_GetStack(CLuaInterface *self, int level, lua_Debug *dbg)
{ return lua_getstack(self->state, level, dbg); }
static int ci_slot_GetInfo(CLuaInterface *self, const char *what, lua_Debug *dbg)
{ return lua_getinfo(self->state, what, dbg); }
static const char *ci_slot_GetLocal(CLuaInterface *self, lua_Debug *dbg, int n)
{ return lua_getlocal(self->state, dbg, n); }
static const char *ci_slot_GetUpvalue(CLuaInterface *self, int funcIndex, int n)
{ return lua_getupvalue(self->state, funcIndex, n); }

static size_t ci_slot_GetDataString(CLuaInterface *self, int index, const char **str)
{
  size_t len = 0;
  const char *s = lua_tolstring(self->state, index, &len);
  if (str) *str = s;
  return s ? len : 0;
}

static void ci_slot_ErrorFromLua(CLuaInterface *self, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  glua_vmsg(self, fmt, ap, 1);
  va_end(ap);
}

static const char *ci_slot_GetCurrentLocation(CLuaInterface *self)
{
  static char buf[512];
  lua_State *L = self->state;
  lua_Debug ar;
  int level;
  snprintf(buf, sizeof(buf), "<nowhere>");
  for (level = 0; level < 2; level++) {
    if (lua_getstack(L, level, &ar) != 0) {
      lua_getinfo(L, "Sl", &ar);
      if (ar.currentline > 0) {
        snprintf(buf, sizeof(buf), "%s (line %d)", ar.short_src, ar.currentline);
        break;
      }
    }
  }
  return buf;
}

static void ci_slot_MsgColour(CLuaInterface *self, const GLuaColor *col, const char *fmt, ...)
{
  char buf[4096];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  gc_MsgColour(&self->callback, buf, col);
}

static void ci_slot_GetCurrentFile(CLuaInterface *self, void *outStr)
{
  (void)self; (void)outStr;
  glua_warn_once("GetCurrentFile(std::string&)");
}

static int ci_slot_CompileString(CLuaInterface *self, void *dumper, const void *str)
{
  (void)self; (void)dumper; (void)str;
  glua_warn_once("CompileString(Bootil::Buffer&)");
  return 0;
}

static int ci_slot_CallFunctionProtected(CLuaInterface *self, int args, int rets, bool printErr)
{
  lua_State *L = self->state;
  int base = lua_gettop(L) - args;
  int r;
  if (base < 1 || lua_type(L, base) != GT_FUNCTION) {
    ci_slot_ErrorNoHalt(self, "CallFunctionProtected was passed a non-function!\n");
    return 0;
  }
  r = lua_pcall(L, args, rets, 0);
  if (r != 0) {
    if (printErr) ci_slot_ErrorNoHalt(self, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
    return 0;
  }
  return 1;
}

/* Binary module naming.  The realm prefix follows IsClient (the reference:
** gmcl_ when the realm is Client, gmsv_ otherwise -- the menu realm uses the
** sv prefix too).  The suffix and extension are compile-time properties of
** this build, exactly as the reference hardcodes them. */
static const char *glua_module_prefix(CLuaInterface *self)
{ return self->realm == GLUA_REALM_CLIENT ? "gmcl_" : "gmsv_"; }

static const char *glua_module_suffix(void)
{
#if defined(_WIN64)
  return "win64";
#elif defined(_WIN32)
  return "win32";
#elif defined(__ANDROID__)
  return "android64";
#elif defined(__aarch64__) || defined(__x86_64__)
  return "linux64";
#elif defined(__APPLE__)
  return "osx64";
#else
  return "linux";
#endif
}

static const char *glua_module_ext(void)
{
#ifdef _WIN32
  return ".dll";
#else
  return ".so";
#endif
}

/* Game directory used to resolve module paths.  The reference resolves via
** the engine file system; a standalone host sets this registry value (or the
** OPGLUA_GAMEDIR environment variable). */
static const char *glua_game_dir(lua_State *L)
{
  const char *dir = NULL;
  lua_getfield(L, LUA_REGISTRYINDEX, "OPGLUA_GAMEDIR");
  if (lua_type(L, -1) == GT_STRING) dir = lua_tostring(L, -1);
  lua_pop(L, 1);
  if (dir == NULL || dir[0] == '\0') {
    dir = getenv("OPGLUA_GAMEDIR");
  }
  return dir ? dir : "";
}

/* Load a binary module file and call its gmod13_open (mirrors the reference
** path builder in the Require slot).  Returns 1 on success. */
static int glua_load_binary_module(CLuaInterface *self, const char *fullpath, const char *entry)
{
  lua_State *L = self->state;
#ifdef _WIN32
  HMODULE h;
#else
  void *h;
#endif
  GLuaCFunc openfn;
#ifdef _WIN32
  h = LoadLibraryExA(fullpath, NULL, 0);
  if (h == NULL) {
    lua_pushfstring(L, "Couldn't load module library! (%s)", fullpath);
    return 0;
  }
  openfn = (GLuaCFunc)(void *)GetProcAddress(h, "gmod13_open");
#else
  h = dlopen(fullpath, RTLD_NOW);
  if (h == NULL) {
    lua_pushfstring(L, "Couldn't load module library! (%s)", fullpath);
    return 0;
  }
  openfn = (GLuaCFunc)dlsym(h, "gmod13_open");
#endif
  if (openfn == NULL) {
    lua_pushfstring(L, "Couldn't find function in library! (%s)", fullpath);
    return 0;
  }
  lua_pushcclosure(L, (lua_CFunction)(void *)openfn, 0);
  lua_pushstring(L, entry);
  if (lua_pcall(L, 1, 0, 0) != 0) return 0;
  return 1;
}

static int glua_file_exists(const char *path)
{
  FILE *f = fopen(path, "rb");
  if (f == NULL) return 0;
  fclose(f);
  return 1;
}

/* Require: try the Lua module file, then fall back to the binary module, the
** same order the reference uses inside this slot. */
static void ci_slot_Require(CLuaInterface *self, const char *name)
{
  lua_State *L = self->state;
  const char *dir;
  char path[1024];
  lua_getglobal(L, "_MODULES");
  if (lua_type(L, -1) != GT_TABLE) {
    lua_pop(L, 1);
    lua_createtable(L, 0, 0);
    lua_pushvalue(L, -1);
    lua_setglobal(L, "_MODULES");
  }
  lua_getfield(L, -1, name);
  if (lua_toboolean(L, -1)) {           /* already loaded */
    lua_pop(L, 2);
    return;
  }
  lua_pop(L, 2);                        /* cached value + _MODULES */

  dir = glua_game_dir(L);
  snprintf(path, sizeof(path), "%s/lua/includes/modules/%s.lua",
           dir, name);
  if (!glua_file_exists(path)) {
    /* The prefix helpers already carry the leading "gm" ("gmcl_"/"gmsv_"). */
    snprintf(path, sizeof(path), "%s/lua/bin/%s%s_%s%s",
             dir, glua_module_prefix(self), name,
             glua_module_suffix(), glua_module_ext());
    if (!glua_file_exists(path)) {
      ci_slot_ErrorNoHalt(self, "Module not found! (%s)\n", name);
      return;
    }
    glua_load_binary_module(self, path, name);
  } else {
    ci_slot_FindAndRunScript(self, path, true, true, NULL, false);
  }

  lua_getglobal(L, "_MODULES");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, name);
  lua_pop(L, 1);
}

static const char *ci_slot_GetActualTypeName(CLuaInterface *self, int type)
{ return lua_typename(self->state, type); }

static void ci_slot_PreCreateTable(CLuaInterface *self, int arr, int narr)
{ lua_createtable(self->state, arr, narr); }

static void ci_slot_PushPooledString(CLuaInterface *self, int index)
{
  lua_State *L = self->state;
  if (index >= 0 && index < GLUA_POOLED_COUNT)
    lua_pushstring(L, glua_pooled_strings[index]);
  else
    lua_pushnil(L);
}

static const char *ci_slot_GetPooledString(CLuaInterface *self, int index)
{
  (void)self;
  if (index >= 0 && index < GLUA_POOLED_COUNT) return glua_pooled_strings[index];
  return NULL;
}

static int ci_slot_AddThreadedCall(CLuaInterface *self, void *call)
{ (void)call; self->threadedCount++; return self->threadedCount; }

static void ci_slot_AppendStackTrace(CLuaInterface *self, char *buf, unsigned int size)
{
  lua_State *L = self->state;
  lua_Debug ar;
  int level = 0;
  if (buf == NULL || size == 0) return;
  buf[0] = '\0';
  if (L == NULL) { strncat(buf, "   Lua State = NULL\n\n", size - strlen(buf) - 1); return; }
  while (level < 17 && lua_getstack(L, level, &ar) != 0) {
    char line[256];
    int i;
    lua_getinfo(L, "Slnu", &ar);
    snprintf(line, sizeof(line), "%s (%s) line %d\n",
             ar.short_src, ar.name ? ar.name : "?", ar.currentline);
    for (i = 0; i < level + 2 && strlen(buf) + 2 < size; i++) strcat(buf, "  ");
    strncat(buf, line, size - strlen(buf) - 1);
    level++;
  }
  strncat(buf, "\n", size - strlen(buf) - 1);
}

static void *ci_slot_CreateConVar(CLuaInterface *self, const char *name, const char *def,
                                  const char *help, int flags)
{
  (void)self; (void)name; (void)def; (void)help; (void)flags;
  glua_warn_once("CreateConVar");
  return NULL;
}

static void *ci_slot_CreateConCommand(CLuaInterface *self, const char *name, const char *help,
                                      int flags, void *cb, void *completion)
{
  (void)self; (void)name; (void)help; (void)flags; (void)cb; (void)completion;
  glua_warn_once("CreateConCommand");
  return NULL;
}

static const char *ci_slot_CheckStringOpt(CLuaInterface *self, int idx, const char *def)
{ return luaL_optlstring(self->state, idx, def, NULL); }

static double ci_slot_CheckNumberOpt(CLuaInterface *self, int idx, double def)
{ return luaL_optnumber(self->state, idx, def); }

static int ci_slot_RegisterMetaTable(CLuaInterface *self, const char *name, GLuaObject *tbl)
{
  lua_State *L = self->state;
  int id;
  lua_getfield(L, LUA_REGISTRYINDEX, name);
  if (lua_type(L, -1) == GT_TABLE) {    /* already registered: return its id */
    lua_getfield(L, -1, "MetaID");
    id = (int)lua_tonumber(L, -1);
    lua_pop(L, 2);
    return id;
  }
  lua_pop(L, 1);
  id = self->metaIDCounter++;
  if (tbl != NULL) gobj_push(tbl); else lua_newtable(L);
  lua_pushnumber(L, (double)id);
  lua_setfield(L, -2, "MetaID");
  lua_pushstring(L, name);
  lua_setfield(L, -2, "MetaName");
  lua_pushvalue(L, -1);
  lua_setfield(L, LUA_REGISTRYINDEX, name);
  if (id >= 0 && id < 255) {
    if (self->metaTables[id] == NULL) self->metaTables[id] = glua_object_alloc(self);
    if (self->metaTables[id] != NULL) gobj_bind_stack(self, self->metaTables[id], -1);
  }
  lua_pop(L, 1);
  return id;
}

static void ci_slot_dtor(CLuaInterface *self)
{
  if (self == NULL) return;
  if (self->state != NULL) {
    lua_close(self->state);
    self->state = NULL;
  }
}

/* ------------------------------------------------------------------------ */
/* CreateInterface: the reference interface factory.  The engine does
** CreateInterface("LUASHARED003") to get the ILuaShared object and
** CreateInterface("LUACONVARS001") for ILuaConVars.  Unknown names return
** NULL with the standard return code, like the reference list walk. */

typedef struct CLuaShared {
  const void *vtable;
  void *factory;                        /* engine interface factory (Init) */
  bool dedicated;
  CLuaInterface *client;
  CLuaInterface *server;
  CLuaInterface *menu;
} CLuaShared;

static void cs_dtor(CLuaShared *s) { (void)s; }

/* Init(interfaceFactory, magicBool, context, pGet): the engine hands over
** its CreateInterface so lua_shared can reach VFileSystem022 /
** VEngineCvar007 etc.  Stored for later use; nothing is requested yet. */
static void cs_Init(CLuaShared *s, void *f, bool b, void *ctx, void *get)
{ s->factory = f; s->dedicated = b; (void)ctx; (void)get; }

static void cs_Shutdown(CLuaShared *s)
{
  if (s->client) { ci_slot_Shutdown(s->client); free(s->client); s->client = NULL; }
  if (s->server) { ci_slot_Shutdown(s->server); free(s->server); s->server = NULL; }
  if (s->menu)   { ci_slot_Shutdown(s->menu);   free(s->menu);   s->menu = NULL; }
}
static void cs_DumpStats(CLuaShared *s) { (void)s; }

static CLuaInterface *cs_CreateLuaInterface(CLuaShared *s, unsigned char realm, bool unknown)
{
  CLuaInterface *iface = (CLuaInterface *)calloc(1, sizeof(CLuaInterface));
  (void)unknown;
  if (iface == NULL) return NULL;
  iface->vtable = (const void *)glua_interface_vtable;
  iface->realm = realm;
  if (!ci_slot_Init(iface, NULL, true)) { free(iface); return NULL; }
  switch (realm) {
    case GLUA_REALM_SERVER: s->server = iface; break;
    case GLUA_REALM_MENU:   s->menu = iface; break;
    default:                s->client = iface; break;
  }
  return iface;
}

static void cs_CloseLuaInterface(CLuaShared *s, CLuaInterface *iface)
{
  if (iface == NULL) return;
  if (s->client == iface) s->client = NULL;
  if (s->server == iface) s->server = NULL;
  if (s->menu == iface) s->menu = NULL;
  ci_slot_Shutdown(iface);
  free(iface);
}

static CLuaInterface *cs_GetLuaInterface(CLuaShared *s, unsigned char realm)
{
  switch (realm) {
    case GLUA_REALM_SERVER: return s->server;
    case GLUA_REALM_MENU:   return s->menu;
    default:                return s->client;
  }
}

/* ILuaShared tail (slots 7..17).  File-mounting and script enumeration go
** through the engine file system, which the standalone build does not have;
** they are safe stand-ins.  ScriptExists answers from the host filesystem so
** Require-style probes still work. */
static void *cs_LoadFile(CLuaShared *s, const void *path, const void *pathId,
                         bool fromDatatable, bool fromFile)
{ (void)s; (void)path; (void)pathId; (void)fromDatatable; (void)fromFile; return NULL; }
static void cs_MountLua(CLuaShared *s, const char *pathID) { (void)s; (void)pathID; }
static void cs_MountLuaAdd(CLuaShared *s, const char *file, const char *pathID)
{ (void)s; (void)file; (void)pathID; }
static void cs_UnMountLua(CLuaShared *s, const char *pathID) { (void)s; (void)pathID; }
static void cs_SetFileContents(CLuaShared *s, const char *a, const char *b) { (void)s; (void)a; (void)b; }
static void cs_SetLuaFindHook(CLuaShared *s, void *hook) { (void)s; (void)hook; }
static void cs_FindScripts(CLuaShared *s, const void *path, const void *pathID, void *out)
{ (void)s; (void)path; (void)pathID; (void)out; }
static const char *cs_GetStackTraces(CLuaShared *s) { (void)s; return ""; }
static void cs_InvalidateCache(CLuaShared *s, const void *unknown) { (void)s; (void)unknown; }
static void cs_EmptyCache(CLuaShared *s) { (void)s; }
static bool cs_ScriptExists(CLuaShared *s, const void *file, const void *path, bool unknown)
{
  const char *f = (const char *)file;
  (void)s; (void)path; (void)unknown;
  return f != NULL && glua_file_exists(f);
}

static const glua_vfn glua_shared_vtable[18] = {
  /*  0 */ (glua_vfn)cs_dtor,
  /*  1 */ (glua_vfn)cs_Init,
  /*  2 */ (glua_vfn)cs_Shutdown,
  /*  3 */ (glua_vfn)cs_DumpStats,
  /*  4 */ (glua_vfn)cs_CreateLuaInterface,
  /*  5 */ (glua_vfn)cs_CloseLuaInterface,
  /*  6 */ (glua_vfn)cs_GetLuaInterface,
  /*  7 */ (glua_vfn)cs_LoadFile,
  /*  8 */ (glua_vfn)cs_MountLua,
  /*  9 */ (glua_vfn)cs_MountLuaAdd,
  /* 10 */ (glua_vfn)cs_UnMountLua,
  /* 11 */ (glua_vfn)cs_SetFileContents,
  /* 12 */ (glua_vfn)cs_SetLuaFindHook,
  /* 13 */ (glua_vfn)cs_FindScripts,
  /* 14 */ (glua_vfn)cs_GetStackTraces,
  /* 15 */ (glua_vfn)cs_InvalidateCache,
  /* 16 */ (glua_vfn)cs_EmptyCache,
  /* 17 */ (glua_vfn)cs_ScriptExists
};

/* ILuaConVars: the engine asks for this too.  CreateConVar/CreateConCommand
** need the engine cvar system, which is not linked here, so they return NULL
** (failures are recoverable) rather than crash. */
typedef struct CLuaConVars {
  const void *vtable;
} CLuaConVars;

static void cv_dtor(CLuaConVars *c) { (void)c; }
static void cv_Init(CLuaConVars *c) { (void)c; }
static void *cv_CreateConVar(CLuaConVars *c, const char *name, const char *def,
                             const char *help, int flags)
{ (void)c; (void)name; (void)def; (void)help; (void)flags; return NULL; }
static void *cv_CreateConCommand(CLuaConVars *c, const char *name, const char *help,
                                 int flags, void *cb, void *completion)
{ (void)c; (void)name; (void)help; (void)flags; (void)cb; (void)completion; return NULL; }
static void cv_DestroyManaged(CLuaConVars *c) { (void)c; }
static void cv_Cache(CLuaConVars *c, const char *key, const char *value) { (void)c; (void)key; (void)value; }
static void cv_ClearCache(CLuaConVars *c) { (void)c; }
static void cv_SaveManaged(CLuaConVars *c) { (void)c; }

static const glua_vfn glua_convars_vtable[8] = {
  (glua_vfn)cv_dtor,
  (glua_vfn)cv_Init,
  (glua_vfn)cv_CreateConVar,
  (glua_vfn)cv_CreateConCommand,
  (glua_vfn)cv_DestroyManaged,
  (glua_vfn)cv_Cache,
  (glua_vfn)cv_ClearCache,
  (glua_vfn)cv_SaveManaged
};

static int ns_equals(const char *a, const char *b) { return glua_stricmp(a, b) == 0; }

LUALIB_API void *CreateInterface(const char *name, int *pReturnCode)
{
  if (name == NULL) { if (pReturnCode) *pReturnCode = 1; return NULL; }
  if (ns_equals(name, "LUASHARED003") || ns_equals(name, "LUASHARED")) {
    CLuaShared *shared = (CLuaShared *)calloc(1, sizeof(CLuaShared));
    if (shared == NULL) { if (pReturnCode) *pReturnCode = 2; return NULL; }
    shared->vtable = (const void *)glua_shared_vtable;
    if (pReturnCode) *pReturnCode = 0;
    return shared;
  }
  if (ns_equals(name, "LUACONVARS001") || ns_equals(name, "LUACONVARS")) {
    CLuaConVars *cv = (CLuaConVars *)calloc(1, sizeof(CLuaConVars));
    if (cv == NULL) { if (pReturnCode) *pReturnCode = 2; return NULL; }
    cv->vtable = (const void *)glua_convars_vtable;
    if (pReturnCode) *pReturnCode = 0;
    return cv;
  }
  /* Unknown interface: the reference walks its list and returns NULL. */
  if (pReturnCode) *pReturnCode = 1;
  return NULL;
}

/* ------------------------------------------------------------------------ */
/* A tiny self-test entry so the host chain can be exercised without an     */
/* external harness: opglua_selftest() builds an interface, loads a module  */
/* and reports whether the module's global marker is visible.               */

LUALIB_API int opglua_selftest(const char *modulePath)
{
  CLuaShared shared;
  CLuaInterface *iface;
  lua_State *L;
  int rc;

  memset(&shared, 0, sizeof(shared));
  shared.vtable = (const void *)glua_shared_vtable;
  iface = cs_CreateLuaInterface(&shared, GLUA_REALM_SERVER, true);
  if (iface == NULL) { fputs("selftest: interface creation failed\n", stderr); return 1; }
  L = iface->state;
  if (L == NULL || L->luabase != iface) {
    fputs("selftest: luabase back pointer not set\n", stderr);
    return 2;
  }

  rc = GMOD_LoadBinaryModule(L, modulePath);
  printf("selftest: GMOD_LoadBinaryModule(%s) = %d\n", modulePath, rc);

  lua_getglobal(L, "opglua_test_marker");
  if (lua_type(L, -1) == GT_STRING) {
    printf("selftest: marker = '%s'\n", lua_tostring(L, -1));
    lua_pop(L, 1);
    cs_CloseLuaInterface(&shared, iface);
    return rc == 1 ? 0 : 3;
  }
  printf("selftest: marker missing (type %d)\n", lua_type(L, -1));
  lua_pop(L, 1);
  cs_CloseLuaInterface(&shared, iface);
  return 4;
}
