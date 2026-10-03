// Exercises the production callback code with Defold's real Lua and script libraries.
#include <assert.h>
#include <stdio.h>
#include <string>
#include "webview_common.h"
extern "C" {
#include <lua/lualib.h>
}

// Engine helpers used to create a script-instance fixture and check reference accounting.
namespace dmScript {
int GetLuaRefCount();
uint32_t RegisterUserType(lua_State* L, const char* name, const luaL_reg methods[], const luaL_reg meta[]);
}

static dmWebView::WebViewInfo g_Info;
static int g_Allowed;
static int g_Cancelled;
static int g_ContextRef;
static bool g_InstanceValid = true;

namespace dmWebView {
int Platform_ContinueOpen(lua_State*, int, int request, const char*) { ++g_Allowed; return request; }
int Platform_CancelOpen(lua_State*, int, int request, const char*) { ++g_Cancelled; return request; }
}

static int InstanceValid(lua_State* L) { lua_pushboolean(L, g_InstanceValid); return 1; }
static int ContextRef(lua_State* L) { lua_pushinteger(L, g_ContextRef); return 1; }
static int ScriptId(lua_State* L) { lua_pushinteger(L, 1); return 1; }

static void RunLua(lua_State* L, const char* source)
{
    int result = luaL_dostring(L, source);
    if (result) fprintf(stderr, "%s\n", lua_tostring(L, -1));
    assert(result == 0);
}

static void SetCallback(lua_State* L, const char* body)
{
    std::string source = "return function(self, view, request, event, data) ";
    source += body;
    source += " end";
    RunLua(L, source.c_str());
    dmWebView::CreateWebViewInfo(&g_Info, L, -1);
    assert(g_Info.m_Callback);
    lua_pop(L, 1);
}

static int Destroy(lua_State*) { dmWebView::ClearWebViewInfo(&g_Info); return 0; }
static int Replace(lua_State* L)
{
    dmWebView::ClearWebViewInfo(&g_Info);
    SetCallback(L, "replacement_calls = replacement_calls + 1");
    return 0;
}

static void Dispatch(lua_State* L, dmWebView::CallbackResult type = dmWebView::CALLBACK_RESULT_URL_LOADING,
                     uint64_t generation = 0)
{
    dmWebView::CallbackInfo event;
    event.m_Info = &g_Info;
    event.m_Generation = generation ? generation : g_Info.m_Generation;
    event.m_Type = type;
    event.m_WebViewID = 2;
    event.m_RequestID = 7;
    event.m_Url = "https://example.invalid/";
    event.m_Result = "tick";

    // A pre-existing stack entry must not be mistaken for the callback result.
    int top = lua_gettop(L);
    lua_pushboolean(L, 0);
    dmScript::GetInstance(L);
    int previous = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pushnil(L);
    dmScript::SetInstance(L);
    dmWebView::RunCallback(&event);
    assert(lua_gettop(L) == top + 1);
    dmScript::GetInstance(L);
    assert(lua_isnil(L, -1));
    lua_pop(L, 2);
    lua_rawgeti(L, LUA_REGISTRYINDEX, previous);
    dmScript::SetInstance(L);
    luaL_unref(L, LUA_REGISTRYINDEX, previous);
}

// Verifies the public callback arguments and nil/true/false navigation decisions.
static void TestNavigation(lua_State* L)
{
    const char* returns[] = { "return nil", "return true", "return false" };
    for (int i = 0; i < 3; ++i)
    {
        std::string body = "assert(self ~= nil and view == 2 and request == 7 and event == -3); "
                           "assert(data.url == 'https://example.invalid/' and data.result == 'tick'); ";
        body += returns[i];
        SetCallback(L, body.c_str());
        g_Allowed = g_Cancelled = 0;
        Dispatch(L);
        assert(g_Allowed == (i < 2));
        assert(g_Cancelled == (i == 2));
        dmWebView::ClearWebViewInfo(&g_Info);
    }
}

// Verifies that missing callbacks and deleted script instances never enter Lua.
static void TestInvalidCallbacks(lua_State* L)
{
    Dispatch(L);
    RunLua(L, "invalid_calls = 0");
    SetCallback(L, "invalid_calls = invalid_calls + 1");
    g_InstanceValid = false;
    Dispatch(L, dmWebView::CALLBACK_RESULT_EVAL_OK);
    g_InstanceValid = true;
    dmWebView::ClearWebViewInfo(&g_Info);
    Dispatch(L);
    dmWebView::ClearWebViewInfo(&g_Info);
    RunLua(L, "assert(invalid_calls == 0)");
}

// Verifies teardown remains safe when Lua destroys/replaces the view and forces GC.
static void TestReentrantDestruction(lua_State* L)
{
    const char* actions[] = { "destroy_view()", "replace_view()" };
    for (int i = 0; i < 2; ++i)
    {
        std::string body = actions[i];
        body += "; collectgarbage('collect'); return true";
        SetCallback(L, body.c_str());
        g_Allowed = g_Cancelled = 0;
        Dispatch(L);
        assert(g_Allowed == 0 && g_Cancelled == 0);
        if (i == 1)
        {
            Dispatch(L, dmWebView::CALLBACK_RESULT_EVAL_OK);
            RunLua(L, "assert(replacement_calls == 1)");
        }
        dmWebView::ClearWebViewInfo(&g_Info);
    }
}

// Verifies Lua errors leave a balanced stack and cancel pending navigation.
static void TestCallbackError(lua_State* L)
{
    SetCallback(L, "error('expected callback test error')");
    g_Allowed = g_Cancelled = 0;
    Dispatch(L);
    assert(g_Allowed == 0 && g_Cancelled == 1);
    dmWebView::ClearWebViewInfo(&g_Info);
}

// Verifies old events cannot reach a new callback after the same slot is reused.
static void TestStaleGeneration(lua_State* L)
{
    RunLua(L, "stale_calls = 0");
    SetCallback(L, "stale_calls = stale_calls + 1");
    uint64_t old_generation = g_Info.m_Generation;
    dmWebView::ClearWebViewInfo(&g_Info);
    Dispatch(L, dmWebView::CALLBACK_RESULT_EVAL_OK, old_generation);
    SetCallback(L, "stale_calls = stale_calls + 1");
    assert(old_generation != g_Info.m_Generation);
    // Covers both events queued before destruction and late events from the old producer.
    Dispatch(L, dmWebView::CALLBACK_RESULT_EVAL_OK, old_generation);
    Dispatch(L, dmWebView::CALLBACK_RESULT_URL_LOADING, old_generation);
    RunLua(L, "assert(stale_calls == 0)");
    Dispatch(L, dmWebView::CALLBACK_RESULT_EVAL_OK);
    RunLua(L, "assert(stale_calls == 1)");
    dmWebView::ClearWebViewInfo(&g_Info);
}

int main()
{
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    lua_pushlightuserdata(L, L);
    lua_setglobal(L, "__script_main_thread");
    const luaL_reg methods[] = { { 0, 0 } };
    const luaL_reg meta[] = {
        { "__is_valid", InstanceValid },
        { "__get_instance_context_table_ref", ContextRef },
        { "__get_unique_script_id", ScriptId },
        { 0, 0 }
    };
    dmScript::RegisterUserType(L, "WebViewTestInstance", methods, meta);
    lua_newtable(L);
    g_ContextRef = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_newuserdata(L, 1);
    luaL_getmetatable(L, "WebViewTestInstance");
    lua_setmetatable(L, -2);
    dmScript::SetInstance(L);
    lua_register(L, "destroy_view", Destroy);
    lua_register(L, "replace_view", Replace);
    RunLua(L, "replacement_calls = 0");

    int references = dmScript::GetLuaRefCount();
    TestNavigation(L);
    TestInvalidCallbacks(L);
    TestReentrantDestruction(L);
    TestCallbackError(L);
    TestStaleGeneration(L);
    assert(dmScript::GetLuaRefCount() == references);
    assert(lua_gettop(L) == 0);
    lua_close(L);
    puts("Callback lifecycle tests passed (real Defold Lua/script libraries).");
    return 0;
}
