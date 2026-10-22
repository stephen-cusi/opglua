#include <windows.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include "GarrysMod/Lua/LuaShared.h"

#define main opglua_interface_contract_main
#include "opglua_diff.c"
#undef main

static void *own_slots[100], *base_slots[32];
static struct { void **own; void **base; } filesystem = {own_slots, base_slots};
static std::string mounted_path, mounted_id;
static std::string mount_resolve_scope, canonical_scope;
static int mounted_mode, marked, cursor, closed, walks;
static int failures, handle_mismatches;
typedef intptr_t FindHandle;
static unsigned int find_state;
static const char *entries[] = {".hidden", "packed.lua", "packed.lua", "folder"};
static const char packed_script[] = "return 'packed-script-ok'";
static size_t stream_offset;
static unsigned int file_state;
static int wrong_size_calls, file_closes, packed_reads;
static bool packed_name(const char *name)
{
    if(!name)return false;
    std::string s(name);
    for(char &c:s)if(c=='\\')c='/';
    return s=="opglua_contract_dir/packed.lua";
}
static void *open_file(void *,const char *name,const char *,const char *)
{ if(!packed_name(name))return NULL;stream_offset=0;return &file_state; }
static unsigned int size_name(void *,const char *name,const char *)
{
    if(name==reinterpret_cast<const char *>(&file_state)){++wrong_size_calls;return 0;}
    return packed_name(name)?sizeof(packed_script)-1:0;
}
static unsigned int size_handle(void *,void *handle)
{ return handle==&file_state?sizeof(packed_script)-1:0; }
static int read_file(void *,void *out,int size,void *handle)
{
    if(handle!=&file_state)return 0;
    size_t remaining=sizeof(packed_script)-1-stream_offset;
    size_t n=static_cast<size_t>(size)<remaining?static_cast<size_t>(size):remaining;
    std::memcpy(out,packed_script+stream_offset,n);stream_offset+=n;++packed_reads;
    return static_cast<int>(n);
}
static void close_file(void *,void *handle){if(handle==&file_state)++file_closes;}
static int file_time(void *,const char *name,const char *){return packed_name(name)?777:0;}
static bool file_exists(void *,const char *name,const char *){return packed_name(name);}
static bool relative_name(void *,const char *,const char *scope,char *out,int size)
{canonical_scope=scope?scope:"";std::snprintf(out,size,"workshop/lua/opglua_contract_dir/packed.lua");return true;}
static void copy_path(void *,const char *path,char *out,int size)
{std::snprintf(out,size,"%s",path);}
static int read_file_ex(void *,const char *name,const char *,void **out,bool,bool,int,int,void *)
{
    if(!packed_name(name)){*out=NULL;return 0;}
    *out=std::malloc(sizeof(packed_script));std::memcpy(*out,packed_script,sizeof(packed_script));
    ++packed_reads;return sizeof(packed_script)-1;
}
static void free_file(void *,void *buffer){std::free(buffer);}
static void **table(void *p) { return *(void ***)p; }
static void add_path(void *, const char *path, const char *id, int mode)
{ mounted_path=path; mounted_id=id; mounted_mode=mode; }
static void mark_path(void *, const char *, bool enabled) { marked=enabled; }
static const char *resolve_path(void *, const char *path, const char *scope, char *out, int size, int, void *)
{
    if(!std::strcmp(path,"workshop/lua"))mount_resolve_scope=scope?scope:"";
    if(!std::strcmp(path,"lua") && scope && !std::strcmp(scope,"GAME")) {
        std::snprintf(out,size,"Z:/addon/nvg/lua");return out;
    }
    std::snprintf(out,size,"Z:/mounted/%s",path); return out;
}
static bool is_directory(void *, const char *path, const char *)
{ return !std::strncmp(path,"Z:/mounted/",11) || !std::strncmp(path,"Z:/addon/",9) || !std::strcmp(path,"workshop/lua"); }
static const char *find_first(void *, const char *, const char *, FindHandle *handle)
{ ++walks; cursor=0; *handle=reinterpret_cast<FindHandle>(&find_state); return entries[0]; }
static bool valid_handle(FindHandle handle)
{
    if(handle==reinterpret_cast<FindHandle>(&find_state))return true;
    ++handle_mismatches;
    return false;
}
static const char *find_next(void *, FindHandle handle)
{ return !valid_handle(handle) || ++cursor==4 ? NULL : entries[cursor]; }
static bool find_directory(void *, FindHandle handle)
{ return valid_handle(handle) && cursor==3; }
static void find_close(void *, FindHandle handle)
{ if(valid_handle(handle))++closed; }
static void *factory(const char *name, int *)
{ return !std::strcmp(name,"VFileSystem022") ? &filesystem : NULL; }

static void *hook_slots[6];
static struct { void **vtable; } find_hook = {hook_slots};
static bool hook_fallback, hook_produces_results;
static int hook_calls, hook_mode = -1, fallback_calls;
static void hook_find(void *, const char *, std::vector<GarrysMod::Lua::LuaFindResult> &out,
                      bool mode)
{
    ++hook_calls;
    hook_mode = mode;
    if(hook_produces_results) {
        GarrysMod::Lua::LuaFindResult r;
        r.fileName="z_client.lua"; r.isFolder=false; out.push_back(r);
        r.fileName="a_client.lua"; out.push_back(r);
    }
}
static bool should_fallback(void *) { ++fallback_calls; return hook_fallback; }

static void check(bool ok, const char *name)
{ std::printf("%s %s\n",ok?"PASS":"FAIL",name); if(!ok)++failures; }

int main(int argc, char **argv)
{
    std::setvbuf(stdout,NULL,_IONBF,0);
    if(argc<3) return 2;
    own_slots[11]=(void *)add_path;
    own_slots[15]=(void *)mark_path;
    own_slots[16]=(void *)resolve_path;
    own_slots[22]=(void *)is_directory;
    own_slots[32]=(void *)find_next;
    own_slots[33]=(void *)find_directory;
    own_slots[34]=(void *)find_close;
    own_slots[35]=(void *)find_first;
    own_slots[37]=(void *)copy_path;
    own_slots[75]=(void *)read_file_ex;
    own_slots[90]=(void *)free_file;
    own_slots[93]=(void *)relative_name;
    base_slots[0]=(void *)read_file;
    base_slots[2]=(void *)open_file;
    base_slots[3]=(void *)close_file;
    base_slots[6]=(void *)size_name;
    base_slots[7]=(void *)size_handle;
    base_slots[10]=(void *)file_exists;
    base_slots[13]=(void *)file_time;
    HMODULE module=LoadLibraryA(argv[1]);
    if(!module) return 3;
    auto create=(void *(*)(const char *,int *))GetProcAddress(module,"CreateInterface");
    void *shared=create("LUASHARED003",NULL);
    if(!std::strcmp(argv[2],"reference")) {
        void **exported=(void **)GetProcAddress(module,"?g_pFullFileSystem@@3PEAVIFileSystem@@EA");
        if(!exported)return 4;
        *exported=&filesystem;
    } else {
        ((void (*)(void *,void *,bool,void *,void *))table(shared)[1])(shared,(void *)factory,false,NULL,NULL);
    }
    ((void (*)(void *,const char *,const char *))table(shared)[10])(shared,"workshop/lua","lsv");
    std::printf("mounted|%s|%s|%x\n",mounted_path.c_str(),mounted_id.c_str(),mounted_mode);
    check(mounted_path=="Z:/mounted/workshop/lua","mount uses filesystem-resolved path");
    check(mount_resolve_scope=="MOD","mount resolution uses MOD without selecting an addon GAME root");
    check(mounted_id=="lsv" && mounted_mode==0x105 && marked,"workshop mount flags and request-only id");
    ((void (*)(void *,const char *,const char *))table(shared)[10])(shared,"lua","lsv");
    check(mounted_path=="Z:/mounted/lua" && mounted_mode==5,
          "stock Lua root stays mounted when an addon also supplies a lua directory");
    // Keep the result allocated: the reference module owns its allocation rules.
    auto *results=new std::vector<GarrysMod::Lua::LuaFindResult>;
    std::string pattern="opglua_contract_dir/*", id="lsv";
    ((void (*)(void *,const void *,const void *,void *))table(shared)[14])(shared,&pattern,&id,results);
    check((reinterpret_cast<uintptr_t>(&find_state)>>32)!=0 && handle_mismatches==0,
          "find handle preserves pointer width through every filesystem call");
    check(results->size()==2,"packed enumeration skips hidden entries and duplicates");
    bool file=false,folder=false;
    for(const auto &r:*results){file|=r.fileName=="packed.lua" && !r.isFolder;folder|=r.fileName=="folder" && r.isFolder;}
    check(file && folder && closed==1,"packed file and folder types and search handle cleanup");
    std::string script_path="opglua_contract_dir/packed.lua";
    void *loaded=((void *(*)(void *,const void *,const void *,bool,bool))table(shared)[7])
        (shared,&script_path,&id,false,true);
    const std::string *contents=loaded?reinterpret_cast<const std::string *>(static_cast<char *>(loaded)+0x48):NULL;
    check(contents && *contents==packed_script && packed_reads>0 && wrong_size_calls==0,
          "enumerated packed script loads its real bytes without using filename Size for a handle");
    const std::string *stored_name=loaded?reinterpret_cast<const std::string *>(static_cast<char *>(loaded)+8):NULL;
    std::string canonical_name="lua/opglua_contract_dir/packed.lua";
    check(stored_name && *stored_name==canonical_name,
          "stored script identity keeps the canonical MOD-relative lua prefix");
    check(canonical_scope=="MOD","script canonical identity uses the MOD path domain");
    void *cached=((void *(*)(void *,const void *))table(shared)[8])(shared,&canonical_name);
    check(cached==loaded && cached!=NULL,
          "canonical client file-table name resolves the same shared cache entry");
    id="lcl";
    int walks_before=walks;
    auto *no_hook=new std::vector<GarrysMod::Lua::LuaFindResult>;
    ((void (*)(void *,const void *,const void *,void *))table(shared)[14])(shared,&pattern,&id,no_hook);
    check(no_hook->empty() && walks==walks_before,"client without a hook does not search server roots");
    hook_slots[3]=(void *)hook_find;
    hook_slots[5]=(void *)should_fallback;
    ((void (*)(void *,void *))table(shared)[13])(shared,&find_hook);
    auto *blocked=new std::vector<GarrysMod::Lua::LuaFindResult>;
    ((void (*)(void *,const void *,const void *,void *))table(shared)[14])(shared,&pattern,&id,blocked);
    check(blocked->empty() && walks==walks_before && hook_calls==1 && fallback_calls==1 && hook_mode==0,
          "client hook receives false mode and may block filesystem fallback");
    hook_produces_results=true;
    auto *hook_results=new std::vector<GarrysMod::Lua::LuaFindResult>;
    ((void (*)(void *,const void *,const void *,void *))table(shared)[14])(shared,&pattern,&id,hook_results);
    check(hook_results->size()==2 && (*hook_results)[0].fileName=="a_client.lua" &&
          walks==walks_before && fallback_calls==1,
          "client hook results are sorted without requesting filesystem fallback");
    hook_produces_results=false;
    hook_fallback=true;
    auto *fallback_results=new std::vector<GarrysMod::Lua::LuaFindResult>;
    ((void (*)(void *,const void *,const void *,void *))table(shared)[14])(shared,&pattern,&id,fallback_results);
    check(fallback_results->size()==2 && walks==walks_before+1 && handle_mismatches==0,
          "client hook permits packed filesystem fallback with intact handles");
    obj_install_vtable();
    Host host={};
    host.vtable=g_host_vtable;
    g_host=&host;
    void *iface=((void *(*)(void *,unsigned char,bool))table(shared)[4])(shared,1,false);
    host.iface=static_cast<Iface *>(iface);
    check(((bool (*)(void *,void *,bool))table(iface)[55])(iface,&host,false),
          "packed execution host initializes the real Lua interface");
    g_iface=iface;
    // This script has already entered the shared cache through LoadFile above.
    check(((bool (*)(void *,const char *,bool,bool,const char *,bool))table(iface)[97])
              (iface,script_path.c_str(),true,true,"packed-contract",false),
          "FindAndRunScript executes the packed script through the real interface");
    const char *result=Top()>0?GetStr(-1):NULL;
    check(result && !std::strcmp(result,"packed-script-ok"),
          "packed script execution produces its expected Lua return value");
    if(Top()>0)Pop(Top());
    ((void (*)(void *))table(iface)[56])(iface);
    std::printf("failures|%d\n",failures);
    return failures?1:0;
}
