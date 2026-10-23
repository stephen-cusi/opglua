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
  unsigned long long guard[4];
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
  int luaErrorCount;
  int invalidErrorCount;
  char messageText[4096];
} Host;

static Obj *host_create_object(Host *h);
static void host_destroy_object(Host *h, Obj *o);

static void *cb_create(void *self) { return host_create_object((Host *)self); }
static void  cb_destroy(void *self, void *o) { host_destroy_object((Host *)self, (Obj *)o); }
static void  cb_errprint(void *self, const char *t, int f)
{ Host *h = (Host *)self; h->errCount++; printf("ERR|%.300s\n", t ? t : "(null)"); (void)f; }
static void  cb_msg(void *self, const char *t, int f)
{ Host *h = (Host *)self; h->msgCount++; printf("MSG|%.300s\n", t ? t : "(null)");
  if (t) strncat(h->messageText,t,sizeof(h->messageText)-strlen(h->messageText)-1); (void)f; }
static void  cb_msgcol(void *self, const char *t, const void *c)
{ (void)self; (void)t; (void)c; }
typedef struct HostString {
  union { char buffer[16]; const char *pointer; } storage;
  size_t size, capacity;
} HostString;
typedef struct HostError {
  HostString message, side;
  const void *begin, *end, *capacity;
} HostError;
static void cb_luaerr(void *self, const void *e)
{
  Host *h = (Host *)self;
  const HostError *error = (const HostError *)e;
  const char *message, *side;
  ++h->luaErrorCount;
  if (!error || error->message.size > 65536 || error->message.capacity < error->message.size ||
      error->side.size != 6 || error->side.capacity < error->side.size || error->side.capacity > 1024) {
    ++h->invalidErrorCount;
    return;
  }
  message = error->message.capacity < 16 ? error->message.storage.buffer : error->message.storage.pointer;
  side = error->side.capacity < 16 ? error->side.storage.buffer : error->side.storage.pointer;
  if (!message || !side || memcmp(side, "server", 6) || side[6] || message[error->message.size])
    ++h->invalidErrorCount;
}
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

static void *g_obj_vtable[128];

/* Every slot answers something safe: the reference drives objects through a
** wide vtable and a missing entry is a null call, not a graceful failure. */
static long long obj_stub(void *a, ...) { (void)a; return 0; }

static void obj_read_member(Obj *o, const char *name, Obj *out)
{
  obj_push(o);
  ((void (*)(void *, int, const char *))VT(o->iface, S_GETFIELD))(o->iface, -1, name);
  obj_setfromstack(out, -1);
  ((void (*)(void *, int))VT(o->iface, S_POP))(o->iface, 2);
}

static void obj_install_vtable(void)
{
  int i;
  for (i = 0; i < 128; i++) g_obj_vtable[i] = (void *)obj_stub;
  g_obj_vtable[OBJ_SETFROMS] = (void *)obj_setfromstack;
  g_obj_vtable[OBJ_UNREF]    = (void *)obj_unref;
  g_obj_vtable[OBJ_GETTYPE]  = (void *)obj_gettype;
  g_obj_vtable[30] = (void *)obj_read_member;
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
  chk("global.VERSION.type", GetType(-1), -1024);
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

typedef struct PendingCall { void **vtable; int ready, runs, requeue; } PendingCall;
static int pending_ready(PendingCall *p) { return p->ready; }
static void pending_run(PendingCall *p, void *iface)
{
  p->runs++;
  if (p->requeue) { p->requeue = 0; ((int (*)(void *, void *))VT(iface, 124))(iface, p); }
}
static void *pending_table[3] = {NULL, (void *)pending_ready, (void *)pending_run};

static void error_contract_probes(void)
{
  int runtime, print_errors, discard_errors, no_returns;
  emit("== structured errors ==");
  for (runtime = 0; runtime != 2; ++runtime)
    for (print_errors = 0; print_errors != 2; ++print_errors)
      for (discard_errors = 0; discard_errors != 2; ++discard_errors)
        for (no_returns = 0; no_returns != 2; ++no_returns) {
          int previous_callbacks = g_host->luaErrorCount;
          int expected_top = 1 + (!discard_errors && (!runtime || !no_returns));
          PushStr("preserved-stack-value");
          chk("error_contract.failed", ((int (*)(void *, const char *, const char *, const char *, int, int, int, int))
              VT(g_iface, S_RUNSTRINGEX))(g_iface, "@error_contract", "",
              runtime ? "error('native-contract-runtime')" : "this is not lua !!!",
              1, print_errors, discard_errors, no_returns) != 0, 0);
          chk("error_contract.top", Top(), expected_top);
          chk("error_contract.callback", g_host->luaErrorCount - previous_callbacks, print_errors);
          chk("error_contract.object", g_host->invalidErrorCount, 0);
          chk("error_contract.preserved", !strcmp(GetStr(1), "preserved-stack-value"), 1);
          Pop(Top());
        }
  chk("error_contract.recovery", Run("assert(1+1==2)"), 1);
  chk("error_contract.final_top", Top(), 0);
  for (runtime = 0; runtime != 3; ++runtime)
    for (print_errors = 0; print_errors != 2; ++print_errors) {
      int previous_callbacks = g_host->luaErrorCount;
      const char *code = runtime == 0 ? "return 11,22,33" :
                         runtime == 1 ? "error('native-contract-runtime')" : "this is not lua !!!";
      PushStr("preserved-stack-value");
      chk("runstring_contract.status", ((int (*)(void *, const char *, const char *, const char *, int, int))
          VT(g_iface, 92))(g_iface, "@error_contract", "", code, 1, print_errors) != 0, runtime == 0);
      chk("runstring_contract.top", Top(), 1);
      chk("runstring_contract.callback", g_host->luaErrorCount - previous_callbacks, runtime ? print_errors : 0);
      chk("runstring_contract.object", g_host->invalidErrorCount, 0);
      Pop(Top());
    }
}

static void extra_probes(void)
{
  PendingCall pending[257];
  int i;
  Obj *global, *library, *result;
  emit("== host contracts ==");
  chk("metatable.setup", Run("contract_uid=function() return 'contract-player' end"), 1);
  ((void (*)(void *, const char *, int))VT(g_iface, 42))(g_iface, "ContractPlayer", 9);
  ((void (*)(void *, int))VT(g_iface, S_PUSH))(g_iface, -1);
  ((void (*)(void *, int, const char *))VT(g_iface, S_SETFIELD))(g_iface, -2, "__index");
  GetField(-10002, "contract_uid");
  ((void (*)(void *, int, const char *))VT(g_iface, S_SETFIELD))(g_iface, -2, "UniqueID");
  Pop(1);
  ((void (*)(void *, void *, int))VT(g_iface, 53))(g_iface, (void *)1234, 9);
  ((void (*)(void *, int, const char *))VT(g_iface, S_SETFIELD))(g_iface, -10002, "contract_player");
  chk("metatable.player_method", Run("assert(contract_player:UniqueID()=='contract-player')"), 1);
  chk("metatable.lookup_top", Top(), 0);
  chk("proxy.setup", Run("matproxy={ShouldOverrideProxy=function(n) return n=='SkyPaint',false end,Init=function() proxy_ready=true end,Call=function() assert(proxy_ready) proxy_calls=(proxy_calls or 0)+1 end}"), 1);
  global = ((Obj *(*)(void *))VT(g_iface, 58))(g_iface);
  library = ((Obj *(*)(void *))VT(g_iface, 79))(g_iface);
  ((void (*)(void *, const char *, void *))VT(global, 30))(global, "matproxy", library);
  chk("proxy.global_object", obj_istable(library), 1);
  obj_push(library); GetField(-1, "ShouldOverrideProxy");
  ((void (*)(void *, int))VT(g_iface, 15))(g_iface, -2);
  PushStr("SkyPaint");
  ((void (*)(void *, int, int))VT(g_iface, 64))(g_iface, 1, 2);
  result=((Obj *(*)(void *,int))VT(g_iface,74))(g_iface,0); obj_push(result);
  chk("proxy.override.first", ((int (*)(void *,int))VT(g_iface,S_GETBOOL))(g_iface,-1)!=0, 0); Pop(1);
  result=((Obj *(*)(void *,int))VT(g_iface,74))(g_iface,1); obj_push(result);
  chk("proxy.override.second", ((int (*)(void *,int))VT(g_iface,S_GETBOOL))(g_iface,-1)!=0, 1); Pop(1);
  chk("proxy.init_bind", Run("matproxy.Init() matproxy.Call() assert(proxy_calls==1)"), 1);
  chk("return.setup",Run("contract_returns=function() return 11,22,33,44 end"),1);
  GetField(-10002,"contract_returns");
  ((void (*)(void *,int,int))VT(g_iface,64))(g_iface,0,4);
  for (i=0;i<4;i++) {
    result=((Obj *(*)(void *,int))VT(g_iface,74))(g_iface,i);
    obj_push(result);
    chk("return.reverse_slot",(int)GetNum(-1),44-i*11);
    Pop(1);
  }
  memset(pending,0,sizeof(pending));
  for(i=0;i<257;i++) { pending[i].vtable=pending_table; pending[i].ready=i!=0; ((int (*)(void *,void *))VT(g_iface,124))(g_iface,&pending[i]); }

  ((void (*)(void *))VT(g_iface,S_CYCLE))(g_iface);
  for(i=1;i<257;i++) chk("queue.first_run",pending[i].runs,1);
  chk("queue.delayed",pending[0].runs,0);
  pending[0].ready=1;
  ((void (*)(void *))VT(g_iface,S_CYCLE))(g_iface);
  chk("queue.delayed_run",pending[0].runs,1);

  chk("contracts.final_top",Top(),0);
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
  ((void (*)(void *,unsigned char))VT(iface,86))(iface,1);
  Run("print('opglua-print-contract',123,true,nil)");
  emit("print.host_text|%s",host.messageText);
  chk("print.host_output", !strcmp(host.messageText,"opglua-print-contract\t123\ttrue\tnil\n"),1);

  {
    void *state = *(void **)((char *)iface + 8);
    emit("state|nonnull=%d", state != NULL);
    if (state != NULL) emit("state.luabase|matches=%d", *(void **)((char *)state + 0x78) == iface);
  }

  probes();
  extra_probes();
  error_contract_probes();

  if(argc>3) {
    void *main_state = *(void **)((char *)iface+8);
    int (*load_module)(void *,const char *)=(int (*)(void *,const char *))GetProcAddress(mod,"GMOD_LoadBinaryModule");
    int top_before=Top();
    chk("binary.export",load_module!=NULL,1);
    if(load_module) {
      chk("binary.load",load_module(*(void **)((char *)iface+8),argv[3]),1);
      emit("binary.stack_delta|%d",Top()-top_before);
      chk("binary.stack_result",Top()-top_before,1);
      Pop(Top()-top_before);
      chk("binary.echo",Run("local s=string.char(65,0,66) local a,b,c=opglua_native_contract.echo(s) assert(a==s and b==22 and c==true)"),1);
      chk("binary.error",Run("local ok,e=pcall(opglua_native_contract.fail) assert(not ok and string.find(e,'opglua_expected_native_error',1,true)) assert(opglua_native_contract.echo('after')=='after')"),1);
      chk("binary.coroutine",Run("local co=coroutine.create(function() local a,b,c=opglua_native_contract.echo('coroutine') assert(a=='coroutine' and b==22 and c==true) end) local ok,e=coroutine.resume(co) assert(ok,e) print('native coroutine complete')"),1);
      chk("binary.coroutine_state_restored", *(void **)((char *)iface+8)==main_state,1);
      chk("binary.coroutine_yields",Run("local co=coroutine.create(function() for i=1,3 do assert(opglua_native_contract.echo('yield')=='yield') coroutine.yield(i) end end) for i=1,3 do local ok,v=coroutine.resume(co) assert(ok and v==i) end assert(coroutine.resume(co))"),1);
      chk("binary.yield_state_restored", *(void **)((char *)iface+8)==main_state,1);
      chk("binary.coroutine_nested",Run("local outer=coroutine.create(function() local inner=coroutine.create(function() opglua_native_contract.echo('inner') coroutine.yield() error('nested failure') end) assert(coroutine.resume(inner)) assert(not opglua_native_contract.current_state()) assert(opglua_native_contract.echo('outer')=='outer') local ok,e=coroutine.resume(inner) assert(not ok and string.find(e,'nested failure',1,true)) coroutine.yield() end) assert(coroutine.resume(outer)) assert(coroutine.resume(outer))"),1);
      chk("binary.nested_state_restored", *(void **)((char *)iface+8)==main_state,1);
      chk("binary.coroutine_wrap",Run("local f=coroutine.wrap(function() opglua_native_contract.echo('wrapped') coroutine.yield(7) opglua_native_contract.echo('wrapped again') error('wrapped failure') end) assert(f()==7) local ok,e=pcall(f) assert(not ok and string.find(e,'wrapped failure',1,true))"),1);
      chk("binary.wrap_state_restored", *(void **)((char *)iface+8)==main_state,1);
      chk("binary.behaviour_setup",Run("contract_behaviour=coroutine.create(function() for i=1,3 do opglua_native_contract.echo('behaviour') coroutine.yield(i) end end)"),1);
      chk("binary.behaviour_first_tick",Run("local ok,n=coroutine.resume(contract_behaviour) assert(ok and n==1)"),1);
      chk("binary.behaviour_next_tick",Run("local ok,n=coroutine.resume(contract_behaviour) assert(ok and n==2)"),1);
      chk("binary.behaviour_last_tick",Run("local ok,n=coroutine.resume(contract_behaviour) assert(ok and n==3) contract_behaviour=nil"),1);
      chk("binary.recovery",Run("assert(opglua_native_contract.echo('main')=='main')"),1);
      chk("binary.final_top",Top(),0);
    }
  }

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
  return g_fail ? 1 : 0;
}
