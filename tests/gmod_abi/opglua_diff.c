/*
** opglua_diff - differential probe host.
**
** Loads a lua_shared implementation at run time and drives it through the
** interface the engine uses, printing one line per observation.  Run it once
** against the shipping module and once against the replacement and diff the
** two reports: every line that differs is a behavioural gap.
**
** The host side is a faithful ILuaGameCallback / ILuaObject implementation:
** objects are backed by registry references, exactly like the engine's own,
** because the runtime creates and drives objects through those virtuals and
** never inspects their fields.
**
** Deliberately independent of the module's own headers: the vtable order is
** spelled out here so a mis-ordered table shows up as a wrong result rather
** than a compile error.
*/

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

typedef struct lua_State lua_State;
typedef struct Iface Iface;

/* --- ILuaObject: registry-reference backed, as the engine does it -------- */

typedef struct Obj {
  void *vtable;
  Iface *iface;
  int ref;          /* registry reference, 0 = none */
} Obj;

#define OBJ_SETFROMS 1    /* +0x08 */
#define OBJ_UNREF    2    /* +0x10 */
#define OBJ_GETTYPE  3    /* +0x18 */
#define OBJ_SETMSTR  16   /* +0x80  SetMember(const char*, const char*) */
#define OBJ_PUSH     33   /* +0x108 */
#define OBJ_ISNIL    34   /* +0x110 */
#define OBJ_ISTABLE  35   /* +0x118 */
#define OBJ_SETMNUM  53   /* +0x1a8 SetMember(const char*, double) */

/* --- the interface vtable slots we drive -------------------------------- */

#define S_TOP            0
#define S_PUSH           1
#define S_POP            2
#define S_GETTABLE       3
#define S_GETFIELD       4
#define S_SETFIELD       5
#define S_CREATETABLE    6
#define S_SETTABLE       7
#define S_GETSTRING     23
#define S_GETNUMBER     24
#define S_GETBOOL       25
#define S_PUSHNIL       28
#define S_PUSHSTRING    29
#define S_PUSHNUMBER    30
#define S_PUSHBOOL      31
#define S_REFCREATE     35
#define S_REFFREE       36
#define S_REFPUSH       37
#define S_ISTYPE        39
#define S_GETTYPE       40
#define S_GETTYPENAME   41
#define S_INIT          55
#define S_SHUTDOWN      56
#define S_CYCLE         57
#define S_SETPATHID     98
#define S_GETPATHID     99
#define S_PUSHPATH     102
#define S_POPPATH      103
#define S_GETPATH      104
#define S_RUNSTRINGEX  111
#define S_REQUIRE      119
#define S_GETPLSTRING  123
#define S_REGMT        130

#define VT(p, slot)  (((void **)(*(void ***)(p)))[slot])

static int   S(void *p, int slot) { return ((int (*)(void *))VT(p, slot))(p); }

/* --- callbacks ---------------------------------------------------------- */

typedef struct Host {
  void *vtable;
  Iface *iface;
  int objCreated;
  int objDestroyed;
  int msgCount;
  int errCount;
} Host;

static Obj *host_create_object(Host *h);
static void host_destroy_object(Host *h, Obj *o);

static void *cb_create(void *self) { return host_create_object((Host *)self); }
static void  cb_destroy(void *self, void *o) { host_destroy_object((Host *)self, (Obj *)o); }
static void  cb_errprint(void *self, const char *t, int f)
{ Host *h = (Host *)self; h->errCount++; printf("ERR|%.300s\n", t ? t : "(null)"); (void)f; }
static void  cb_msg(void *self, const char *t, int f)
{ Host *h = (Host *)self; h->msgCount++; printf("MSG|%.300s\n", t ? t : "(null)"); (void)f; }
static void  cb_msgcol(void *self, const char *t, const void *c)
{ (void)self; (void)t; (void)c; }
static void  cb_luaerr(void *self, const void *e) { (void)self; (void)e; }
static void  cb_iface_created(void *self, void *iface)
{ Host *h = (Host *)self; h->iface = (Iface *)iface; }

static void *g_host_vtable[7] = {
  (void *)cb_create, (void *)cb_destroy, (void *)cb_errprint,
  (void *)cb_msg, (void *)cb_msgcol, (void *)cb_luaerr, (void *)cb_iface_created
};

/* --- object vtable ------------------------------------------------------ */

static void obj_setfromstack(Obj *o, int idx)
{
  Iface *L = o->iface;
  /* Replace whatever the object held with a reference to the given slot. */
  if (o->ref != 0) ((void (*)(void *, int))VT(L, S_REFFREE))(L, o->ref);
  ((void (*)(void *, int))VT(L, S_PUSH))(L, idx);
  o->ref = ((int (*)(void *))VT(L, S_REFCREATE))(L);
}
static void obj_unref(Obj *o)
{
  if (o->ref != 0 && o->iface != NULL) {
    ((void (*)(void *, int))VT(o->iface, S_REFFREE))(o->iface, o->ref);
    o->ref = 0;
  }
}
static int obj_gettype(Obj *o)
{
  if (o->ref == 0) return 0;
  ((void (*)(void *, int))VT(o->iface, S_REFPUSH))(o->iface, o->ref);
  {
    int t = ((int (*)(void *, int))VT(o->iface, S_GETTYPE))(o->iface, -1);
    ((void (*)(void *, int))VT(o->iface, S_POP))(o->iface, 1);
    return t;
  }
}
static void obj_setmember_str(Obj *o, const char *k, const char *v)
{
  Iface *L = o->iface;
  if (o->ref == 0) return;
  ((void (*)(void *, int))VT(L, S_REFPUSH))(L, o->ref);      /* table */
  ((void (*)(void *, const char *, int))VT(L, S_PUSHSTRING))(L, v, 0);
  ((void (*)(void *, int, const char *))VT(L, S_SETFIELD))(L, -2, k);
  ((void (*)(void *, int))VT(L, S_POP))(L, 1);
}
static void obj_setmember_num(Obj *o, const char *k, double v)
{
  Iface *L = o->iface;
  if (o->ref == 0) return;
  ((void (*)(void *, int))VT(L, S_REFPUSH))(L, o->ref);
  ((void (*)(void *, double))VT(L, S_PUSHNUMBER))(L, v);
  ((void (*)(void *, int, const char *))VT(L, S_SETFIELD))(L, -2, k);
  ((void (*)(void *, int))VT(L, S_POP))(L, 1);
}
static void obj_push(Obj *o)
{
  if (o->ref == 0) { ((void (*)(void *))VT(o->iface, S_PUSHNIL))(o->iface); return; }
  ((void (*)(void *, int))VT(o->iface, S_REFPUSH))(o->iface, o->ref);
}
static int obj_isnil(Obj *o) { return o->ref == 0 || obj_gettype(o) == 0; }
static int obj_istable(Obj *o) { return obj_gettype(o) == 5; }

static void *g_obj_vtable[64];

/* Every slot answers something safe: the reference drives objects through a
** wide vtable and a missing entry is a null call, not a graceful failure. */
static long long obj_stub(void *a, ...) { (void)a; return 0; }

static void obj_install_vtable(void)
{
  int i;
  for (i = 0; i < 64; i++) g_obj_vtable[i] = (void *)obj_stub;
  g_obj_vtable[OBJ_SETFROMS] = (void *)obj_setfromstack;
  g_obj_vtable[OBJ_UNREF]    = (void *)obj_unref;
  g_obj_vtable[OBJ_GETTYPE]  = (void *)obj_gettype;
  g_obj_vtable[OBJ_SETMSTR]  = (void *)obj_setmember_str;
  g_obj_vtable[OBJ_PUSH]     = (void *)obj_push;
  g_obj_vtable[OBJ_ISNIL]    = (void *)obj_isnil;
  g_obj_vtable[OBJ_ISTABLE]  = (void *)obj_istable;
  g_obj_vtable[OBJ_SETMNUM]  = (void *)obj_setmember_num;
}

static Host *g_host;

static Obj *host_create_object(Host *h)
{
  Obj *o = (Obj *)calloc(1, sizeof(Obj));
  if (o == NULL) return NULL;
  o->vtable = g_obj_vtable;
  o->iface = h->iface;
  h->objCreated++;
  return o;
}
static void host_destroy_object(Host *h, Obj *o)
{
  if (o == NULL) return;
  obj_unref(o);
  free(o);
  h->objDestroyed++;
}

/* --- probes ------------------------------------------------------------- */

static void *g_iface;
static int g_fail;
static int g_haveFs;

static void emit(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  printf("\n");
  fflush(stdout);
}
static void chk(const char *name, long long got, long long want)
{
  if (want == -1024) emit("%s|%lld", name, got);
  else if (got == want) emit("%s|%lld", name, got);
  else { emit("%s|%lld EXPECTED %lld", name, got, want); g_fail++; }
}

static int Top(void) { return S(g_iface, S_TOP); }
static void Pop(int n) { ((void (*)(void *, int))VT(g_iface, S_POP))(g_iface, n); }
static int GetType(int i) { return ((int (*)(void *, int))VT(g_iface, S_GETTYPE))(g_iface, i); }
/* GetString takes an optional size out-pointer in the third register: passing
** an unspecified value there makes the implementation write through it. */
static const char *GetStr(int i)
{ return ((const char *(*)(void *, int, void *))VT(g_iface, S_GETSTRING))(g_iface, i, NULL); }
static double GetNum(int i) { return ((double (*)(void *, int))VT(g_iface, S_GETNUMBER))(g_iface, i); }
static void PushStr(const char *s) { ((void (*)(void *, const char *, int))VT(g_iface, S_PUSHSTRING))(g_iface, s, 0); }
static void PushNum(double n) { ((void (*)(void *, double))VT(g_iface, S_PUSHNUMBER))(g_iface, n); }
static void PushNil(void) { ((void (*)(void *))VT(g_iface, S_PUSHNIL))(g_iface); }
static void GetField(int i, const char *k) { ((void (*)(void *, int, const char *))VT(g_iface, S_GETFIELD))(g_iface, i, k); }

static int Run(const char *code)
{
  return ((int (*)(void *, const char *, const char *, const char *, int, int, int, int))
          VT(g_iface, S_RUNSTRINGEX))(g_iface, "@probe", "", code, 1, 1, 1, 0);
}

static void probes(void)
{
  int i;
  char key[96];

  emit("== stack ==");
  chk("top.initial", Top(), 0);
  PushNil(); PushStr("abc"); PushNum(42.5);
  chk("top.after3", Top(), 3);
  chk("type.nil", GetType(1), 0);
  chk("type.string", GetType(2), 4);
  chk("type.number", GetType(3), 3);
  emit("getstring|%s", GetStr(2) ? GetStr(2) : "(null)");
  emit("getnumber|%.17g", GetNum(3));
  Pop(3);
  chk("top.after_pop", Top(), 0);

  emit("== typenames ==");
  for (i = -1; i <= 12; i++) {
    const char *n = ((const char *(*)(void *, int))VT(g_iface, S_GETTYPENAME))(g_iface, i);
    snprintf(key, sizeof key, "typename.%d", i);
    emit("%s|%s", key, n ? n : "(null)");
  }
  {
    static const int picks[] = { 40, 42, 43, 44, 50, -5 };
    for (i = 0; i < (int)(sizeof picks / sizeof picks[0]); i++) {
      const char *n = ((const char *(*)(void *, int))VT(g_iface, S_GETTYPENAME))(g_iface, picks[i]);
      snprintf(key, sizeof key, "typename.%d", picks[i]);
      emit("%s|%s", key, n ? n : "(null)");
    }
  }

  emit("== pooled ==");
  {
    static const int picks[] = { 0, 1, 25, 100, 126, 127, 196, 200, 300, 316, 317, 400 };
    for (i = 0; i < (int)(sizeof picks / sizeof picks[0]); i++) {
      const char *n = ((const char *(*)(void *, int))VT(g_iface, S_GETPLSTRING))(g_iface, picks[i]);
      snprintf(key, sizeof key, "pooled.%d", picks[i]);
      emit("%s|%s", key, n ? n : "(null)");
    }
  }

  emit("== realm ==");
  chk("isserver", (int)(unsigned char)((int (*)(void *))VT(g_iface, 75))(g_iface), 1);
  chk("isclient", (int)(unsigned char)((int (*)(void *))VT(g_iface, 76))(g_iface), 0);
  chk("ismenu", (int)(unsigned char)((int (*)(void *))VT(g_iface, 77))(g_iface), 0);

  emit("== path ==");
  if (!g_haveFs) { emit("path.skipped|no file system"); }
  else {
  ((void (*)(void *, const char *))VT(g_iface, S_SETPATHID))(g_iface, "probeid");
  emit("getpathid|%s", ((const char *(*)(void *))VT(g_iface, S_GETPATHID))(g_iface));
  {
    const char *p = ((const char *(*)(void *))VT(g_iface, S_GETPATH))(g_iface);
    emit("getpath.empty|%s", p ? p : "(null)");
  }
  ((void (*)(void *, const char *))VT(g_iface, S_PUSHPATH))(g_iface, "lua/includes/probe.lua");
  {
    const char *p = ((const char *(*)(void *))VT(g_iface, S_GETPATH))(g_iface);
    emit("getpath.pushed|%s", p ? p : "(null)");
  }
  ((void (*)(void *))VT(g_iface, S_POPPATH))(g_iface);
  {
    const char *p = ((const char *(*)(void *))VT(g_iface, S_GETPATH))(g_iface);
    emit("getpath.popped|%s", p ? p : "(null)");
  }
  }

  emit("== globals ==");
  GetField(-10002, "VERSION");
  chk("global.VERSION.type", GetType(-1), 3);
  emit("global.VERSION.value|%.17g", GetNum(-1));
  Pop(1);
  GetField(-10002, "BRANCH");
  emit("global.BRANCH|%s", GetType(-1) == 4 ? GetStr(-1) : "(nil)");
  Pop(1);
  {
    static const char *const libs[] = { "print", "pairs", "string", "table", "math",
                                        "os", "io", "debug", "coroutine", "jit", "bit",
                                        "hook", "util", "file", "net", "timer", "draw",
                                        "concommand", "list", "scripted_ents", "undo" };
    for (i = 0; i < (int)(sizeof libs / sizeof libs[0]); i++) {
      GetField(-10002, libs[i]);
      snprintf(key, sizeof key, "global.%s.type", libs[i]);
      chk(key, GetType(-1), -1024);
      Pop(1);
    }
  }
  GetField(-10002, "debug");
  if (GetType(-1) == 5) {
    static const char *const gone[] = { "setlocal", "setupvalue", "upvalueid", "upvaluejoin" };
    for (i = 0; i < 4; i++) {
      GetField(-1, gone[i]);
      snprintf(key, sizeof key, "debug.%s.type", gone[i]);
      chk(key, GetType(-1), -1024);
      Pop(1);
    }
  }
  Pop(1);

  emit("== lang ==");
  {
    static const struct { const char *n; const char *c; } cs[] = {
      { "arith",       "p_a = 2 + 3 * 4" },
      { "continue",    "p_b = 0 for i=1,5 do if i==3 then continue end p_b = p_b + i end" },
      { "andand",      "p_c = (1 == 1) && (2 == 2)" },
      { "oror",        "p_d = (1 == 2) || (2 == 2)" },
      { "bang",        "p_e = !false" },
      { "ne",          "p_f = (1 != 2)" },
      { "linecom",     "p_g = 1 // c" },
      { "blockcom",    "p_h = 1 /* c */ + 1" },
      { "tinsert",     "p_i = table.insert({}, 'x')" },
      { "strfind",     "p_j = string.find('hello','ll')" },
      { "tconcat",     "p_k = table.concat({'a','b'},'-')" },
      { "pcall",       "p_l = pcall(function() error('boom') end)" },
      { "vec",         "p_m = Vector(1,2,3):Length()" },
      { "mathrand",    "math.randomseed(1) p_n = type(math.Rand(1,2))" },
      { "stringfmt",   "p_o = string.format('%d-%s', 7, 'z')" },
      { "goto_break",  "p_p = 0 for i=1,3 do p_p = p_p + i if i==2 then break end end" },
      { "metatable",   "local t=setmetatable({},{__index=function() return 9 end}) p_q = t.x" },
      { "coroutine",   "local co=coroutine.create(function() coroutine.yield(5) end) local ok,v=coroutine.resume(co) p_r = v" },
    };
    for (i = 0; i < (int)(sizeof cs / sizeof cs[0]); i++) {
      int ok = Run(cs[i].c);
      emit("lang.%s|ok=%d", cs[i].n, ok);
    }
    static const char *const vars[] = { "p_a","p_b","p_c","p_d","p_e","p_f","p_g","p_h",
                                        "p_i","p_j","p_k","p_l","p_m","p_n","p_o","p_p",
                                        "p_q","p_r" };
    for (i = 0; i < (int)(sizeof vars / sizeof vars[0]); i++) {
      GetField(-10002, vars[i]);
      snprintf(key, sizeof key, "lang.%s.value", vars[i]);
      if (GetType(-1) == 3) emit("%s|%.17g", key, GetNum(-1));
      else if (GetType(-1) == 4) emit("%s|%s", key, GetStr(-1));
      else if (GetType(-1) == 1) emit("%s|bool", key);
      else emit("%s|type=%d", key, GetType(-1));
      Pop(1);
    }
  }

  emit("== refs ==");
  {
    int r1, r2;
    PushStr("ref-a");
    r1 = ((int (*)(void *))VT(g_iface, S_REFCREATE))(g_iface);
    PushStr("ref-b");
    r2 = ((int (*)(void *))VT(g_iface, S_REFCREATE))(g_iface);
    chk("ref.first_positive", r1 > 0, 1);
    chk("ref.distinct", r1 != r2, 1);
    ((void (*)(void *, int))VT(g_iface, S_REFPUSH))(g_iface, r1);
    emit("ref.push_value|%s", GetStr(-1) ? GetStr(-1) : "(null)");
    Pop(1);
    ((void (*)(void *, int))VT(g_iface, S_REFFREE))(g_iface, r2);
  }

  emit("== errors ==");
  emit("errors.runtime|ok=%d", Run("local t=nil return t.f"));
  emit("errors.compile|ok=%d", Run("this is not lua !!!"));
  emit("errors.explicit|ok=%d", Run("error('explicit')"));
  chk("top.after_errors", Top(), 0);
}

/* --- driver ------------------------------------------------------------- */

int main(int argc, char **argv)
{
  const char *dllPath = (argc > 1) ? argv[1] : "";
  int skipShared = (argc > 2 && strcmp(argv[2], "skip-shared-init") == 0);
  HMODULE mod;
  void *(*createInterface)(const char *, int *);
  int rc = 0;
  void *shared, *iface;
  Host host;

  if (dllPath[0] == '\0') { fprintf(stderr, "usage: opglua_diff <dll> [skip-shared-init]\n"); return 2; }
  mod = LoadLibraryA(dllPath);
  if (mod == NULL) { fprintf(stderr, "LoadLibrary failed (%lu)\n", GetLastError()); return 2; }
  createInterface = (void *(*)(const char *, int *))GetProcAddress(mod, "CreateInterface");
  if (createInterface == NULL) { fprintf(stderr, "no CreateInterface\n"); return 2; }
  emit("module|%s", dllPath);

  obj_install_vtable();
  memset(&host, 0, sizeof host);
  host.vtable = g_host_vtable;
  g_host = &host;

  shared = createInterface("LUASHARED003", &rc);
  emit("create.shared|rc=%d nonnull=%d", rc, shared != NULL);
  if (shared == NULL) return 3;

  if (!skipShared) {
    ((void (*)(void *, void *, int, void *, void *))VT(shared, 1))(shared, NULL, 0, NULL, NULL);
    emit("shared.init|done");
  } else {
    emit("shared.init|skipped");
  }

  emit("getiface.before|nonnull=%d",
       ((void *(*)(void *, unsigned char))VT(shared, 6))(shared, 1) != NULL);

  iface = ((void *(*)(void *, unsigned char, int))VT(shared, 4))(shared, 1, 0);
  emit("create.iface|nonnull=%d", iface != NULL);
  if (iface == NULL) return 4;
  emit("getiface.after|matches=%d",
       ((void *(*)(void *, unsigned char))VT(shared, 6))(shared, 1) == iface);

  /* The host's callback is per realm and knows its interface before Init runs:
  ** Init asks for objects through it while the interface is still booting. */
  host.iface = (Iface *)iface;
  {
    int ok = ((int (*)(void *, void *, int))VT(iface, S_INIT))(iface, &host, 0);
    emit("iface.init|ok=%d created=%d objs=%d", (int)(unsigned char)ok, host.iface == iface, host.objCreated);
  }
  g_iface = iface;

  {
    void *state = *(void **)((char *)iface + 8);
    emit("state|nonnull=%d", state != NULL);
    if (state != NULL) emit("state.luabase|matches=%d", *(void **)((char *)state + 0x78) == iface);
  }

  probes();

  {
    int i;
    for (i = 0; i < 3; i++) ((void (*)(void *))VT(iface, S_CYCLE))(iface);
    chk("cycle.top_after", Top(), 0);
  }

  ((void (*)(void *))VT(iface, S_SHUTDOWN))(iface);
  emit("shutdown|done");
  emit("host.objects|created=%d destroyed=%d msgs=%d errs=%d",
       host.objCreated, host.objDestroyed, host.msgCount, host.errCount);
  emit("failures|%d", g_fail);
  FreeLibrary(mod);
  return 0;
}
