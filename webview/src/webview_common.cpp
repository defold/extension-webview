#if defined(DM_PLATFORM_ANDROID) || defined(DM_PLATFORM_IOS) || defined (DM_PLATFORM_OSX)

#include <assert.h>
#include <dmsdk/dlib/array.h>
#include <dmsdk/dlib/log.h>

#include "webview_common.h"

namespace dmWebView
{

static uint32_t g_CallbackInvocationDepth = 0;
static dmArray<dmScript::LuaCallbackInfo*> g_DeferredCallbacks;
// Only accessed on the script thread. Preserve this across extension restarts.
static uint64_t g_NextGeneration = 0;

void CreateWebViewInfo(WebViewInfo* info, lua_State* L, int callback_index)
{
    info->m_Callback = dmScript::CreateCallback(L, callback_index);
    if (++g_NextGeneration == 0)
        ++g_NextGeneration;
    info->m_Generation = g_NextGeneration;
}

static void FlushDeferredCallbacks()
{
    if (g_CallbackInvocationDepth != 0)
        return;

    for (uint32_t i = 0; i != g_DeferredCallbacks.Size(); ++i)
        dmScript::DestroyCallback(g_DeferredCallbacks[i]);
    g_DeferredCallbacks.SetSize(0);
}

void RunCallback(CallbackInfo* cbinfo)
{
    if (cbinfo->m_Generation != cbinfo->m_Info->m_Generation)
        return;

    dmScript::LuaCallbackInfo* callback = cbinfo->m_Info->m_Callback;
    if (!dmScript::IsCallbackValid(callback))
    {
        if (callback && cbinfo->m_Type == CALLBACK_RESULT_URL_LOADING)
            Platform_CancelOpen(0, cbinfo->m_WebViewID, cbinfo->m_RequestID, cbinfo->m_Url);
        return;
    }

    lua_State* L = dmScript::GetCallbackLuaContext(callback);
    int top = lua_gettop(L);

    if (!dmScript::SetupCallback(callback))
    {
        if (cbinfo->m_Type == CALLBACK_RESULT_URL_LOADING)
            Platform_CancelOpen(L, cbinfo->m_WebViewID, cbinfo->m_RequestID, cbinfo->m_Url);
        return;
    }

    lua_pushnumber(L, (lua_Number)cbinfo->m_WebViewID);
    lua_pushnumber(L, (lua_Number)cbinfo->m_RequestID);
    lua_pushnumber(L, (lua_Number)cbinfo->m_Type);

    lua_newtable(L);

    lua_pushstring(L, "url");
    if( cbinfo->m_Url ) {
        lua_pushstring(L, cbinfo->m_Url);
    } else {
        lua_pushnil(L);
    }
    lua_rawset(L, -3);

    lua_pushstring(L, "result");
    if( cbinfo->m_Result ) {
        lua_pushstring(L, cbinfo->m_Result);
    } else {
        lua_pushnil(L);
    }
    lua_rawset(L, -3);


    // Lua may destroy this view (and create another in the same slot). Keep the
    // original callback rooted until its stack and script context are restored.
    ++g_CallbackInvocationDepth;
    int ret = dmScript::PCall(L, 5, 1);
    bool allow_navigation = false;
    if (ret == 0)
    {
        allow_navigation = lua_isnil(L, -1) || lua_toboolean(L, -1);
        lua_pop(L, 1);
    }
    dmScript::TeardownCallback(callback);
    assert(top == lua_gettop(L));

    if (cbinfo->m_Type == CALLBACK_RESULT_URL_LOADING &&
        cbinfo->m_Generation == cbinfo->m_Info->m_Generation &&
        cbinfo->m_Info->m_Callback == callback && dmScript::IsCallbackValid(callback))
    {
        if (ret == 0 && allow_navigation)
            Platform_ContinueOpen(L, cbinfo->m_WebViewID, cbinfo->m_RequestID, cbinfo->m_Url);
        else
            Platform_CancelOpen(L, cbinfo->m_WebViewID, cbinfo->m_RequestID, cbinfo->m_Url);
    }

    --g_CallbackInvocationDepth;
    FlushDeferredCallbacks();
}

void ClearWebViewInfo(WebViewInfo* info)
{
    dmScript::LuaCallbackInfo* callback = info->m_Callback;
    info->m_Callback = 0;
    if (!callback)
        return;

    if (g_CallbackInvocationDepth == 0)
    {
        dmScript::DestroyCallback(callback);
        return;
    }
    if (g_DeferredCallbacks.Full())
        g_DeferredCallbacks.OffsetCapacity(4);
    g_DeferredCallbacks.Push(callback);
}

/** Creates a web view
@param callback callback to use for requests
@return the web view id
*/
static int Create(lua_State* L)
{
    int top = lua_gettop(L);

    WebViewInfo info;
    CreateWebViewInfo(&info, L, 1);

    int webview_id = info.m_Callback ? Platform_Create(L, &info) : -1;
    if (webview_id < 0)
        ClearWebViewInfo(&info);
    lua_pushnumber(L, webview_id);

    assert(top + 1 == lua_gettop(L));
    return 1;
}

/** Closes a web view
@param id the web view id
@return -1 if an error occurred. 0 if it was destroyed
*/
static int Destroy(lua_State* L)
{
    int top = lua_gettop(L);

    const int webview_id = luaL_checknumber(L, 1);

    int result = Platform_Destroy(L, webview_id);
    lua_pushnumber(L, result);

    assert(top + 1 == lua_gettop(L));
    return 1;
}


void ParseHeaders(lua_State* L, int argumentindex, int webview_id)
{
    Platform_ClearHeaders(L, webview_id);
    luaL_checktype(L, argumentindex, LUA_TTABLE);
    lua_pushvalue(L, argumentindex);

    // first key
    lua_pushnil(L);

    // push key-value pair
    while (lua_next(L, -2))
    {
        // push copy of key (ie the header), convert it, pop it
        // note from Lua docs: If the value is a number, then lua_tolstring also
        // changes the actual value in the stack to a string. This change
        // confuses lua_next when lua_tolstring is applied to keys during a
        // table traversal
        lua_pushvalue(L, -2);
        const char* header = lua_tostring(L, -1);
        lua_pop(L, 1);

        // push copy of value, convert it, pop it
        lua_pushvalue(L, -1);
        const char* value = lua_tostring(L, -1);
        lua_pop(L, 1);

        Platform_AddHeader(L, webview_id, header, value);

        // remove value, keep key for lua_next
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}


void ParseOptions(lua_State* L, int argumentindex, int webview_id, RequestInfo* requestinfo)
{
    luaL_checktype(L, argumentindex, LUA_TTABLE);
    lua_pushvalue(L, argumentindex);
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        lua_pushvalue(L, -2);
        const char* attr = lua_tostring(L, -1);
        lua_pop(L, 1);
        if( strcmp(attr, "hidden") == 0 )
        {
            luaL_checktype(L, -1, LUA_TBOOLEAN);
            requestinfo->m_Hidden = lua_toboolean(L, -1);
        }
        else if (strcmp(attr, "headers") == 0)
        {
            ParseHeaders(L, -1, webview_id);
        }
        else if( strcmp(attr, "transparent") == 0 )
        {
            luaL_checktype(L, -1, LUA_TBOOLEAN);
            requestinfo->m_Transparent = lua_toboolean(L, -1);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

static int Open(lua_State* L)
{
    int top = lua_gettop(L);
    const int webview_id = luaL_checknumber(L, 1);
    const char* url = luaL_checkstring(L, 2);

    RequestInfo requestinfo;
    if( top >= 3 && !lua_isnil(L, 3))
    {
        ParseOptions(L, 3, webview_id, &requestinfo);
    }

    int request_id = Platform_Open(L, webview_id, url, &requestinfo);
    lua_pushnumber(L, request_id);

    assert(top + 1 == lua_gettop(L));
    return 1;
}

static int OpenRaw(lua_State* L)
{
    int top = lua_gettop(L);
    const int webview_id = luaL_checknumber(L, 1);
    const char* html = luaL_checkstring(L, 2);

    RequestInfo requestinfo;
    if( top >= 3 && !lua_isnil(L, 3))
    {
        ParseOptions(L, 3, webview_id, &requestinfo);
    }

    int request_id = Platform_OpenRaw(L, webview_id, html, &requestinfo);
    lua_pushnumber(L, request_id);

    assert(top + 1 == lua_gettop(L));
    return 1;
}

static int Eval(lua_State* L)
{
    int top = lua_gettop(L);
    const int webview_id = luaL_checknumber(L, 1);
    const char* code = luaL_checkstring(L, 2);

    int request_id = Platform_Eval(L, webview_id, code);
    lua_pushnumber(L, request_id);

    assert(top + 1 == lua_gettop(L));
    return 1;
}

static int SetVisible(lua_State* L)
{
    int top = lua_gettop(L);
    const int webview_id = luaL_checknumber(L, 1);
    int visible = 0;
    if (lua_type(L, 2) == LUA_TNUMBER)
    {
        visible = luaL_checknumber(L, 2);
    }
    else
    {
        visible = lua_toboolean(L, 2);
    }

    Platform_SetVisible(L, webview_id, visible);

    assert(top == lua_gettop(L));
    return 0;
}

static int SetTransparent(lua_State* L)
{
    int top = lua_gettop(L);
    const int webview_id = luaL_checknumber(L, 1);
    const int transparent = lua_toboolean(L, 2);

    Platform_SetTransparent(L, webview_id, transparent);

    assert(top == lua_gettop(L));
    return 0;
}

static int IsVisible(lua_State* L)
{
    int top = lua_gettop(L);
    const int webview_id = luaL_checknumber(L, 1);

    int visible = Platform_IsVisible(L, webview_id);
    lua_pushnumber(L, visible);

    assert(top + 1 == lua_gettop(L));
    return 1;
}

static int SetPosition(lua_State* L)
{
    int top = lua_gettop(L);
    const int webview_id = luaL_checknumber(L, 1);
    const int x = luaL_checknumber(L, 2);
    const int y = luaL_checknumber(L, 3);
    const int width = luaL_checknumber(L, 4);
    const int height = luaL_checknumber(L, 5);

    Platform_SetPosition(L, webview_id, x, y, width, height);

    assert(top == lua_gettop(L));
    return 0;
}

static const luaL_reg WebView_methods[] =
{
    {"create", Create},
    {"destroy", Destroy},
    {"open", Open},
    {"open_raw", OpenRaw},
    {"eval", Eval},
    {"set_visible", SetVisible},
    {"set_transparent", SetTransparent},
    {"set_position", SetPosition},
    {"is_visible", IsVisible},
    {0, 0}
};

static void LuaInit(lua_State* L)
{
    int top = lua_gettop(L);
    luaL_register(L, "webview", WebView_methods);

#define SETCONSTANT(name) \
        lua_pushnumber(L, (lua_Number) name); \
        lua_setfield(L, -2, #name);\

    SETCONSTANT(CALLBACK_RESULT_URL_OK)
    SETCONSTANT(CALLBACK_RESULT_URL_ERROR)
    SETCONSTANT(CALLBACK_RESULT_EVAL_OK)
    SETCONSTANT(CALLBACK_RESULT_EVAL_ERROR)
    SETCONSTANT(CALLBACK_RESULT_URL_LOADING)

#undef SETCONSTANT

    lua_pop(L, 1);
    assert(top == lua_gettop(L));
}

static dmExtension::Result WebView_AppInitialize(dmExtension::AppParams* params)
{
    return Platform_AppInitialize(params);
}

static dmExtension::Result WebView_AppFinalize(dmExtension::AppParams* params)
{
    return Platform_AppFinalize(params);
}

static dmExtension::Result WebView_Initialize(dmExtension::Params* params)
{
    LuaInit(params->m_L);

    return Platform_Initialize(params);
}

static dmExtension::Result WebView_Finalize(dmExtension::Params* params)
{
    return Platform_Finalize(params);
}

static dmExtension::Result WebView_Update(dmExtension::Params* params)
{
    return Platform_Update(params);
}

DM_DECLARE_EXTENSION(WebViewExternal, "WebView", WebView_AppInitialize, WebView_AppFinalize, WebView_Initialize, WebView_Update, 0, WebView_Finalize)


} // namespace

#endif // DM_PLATFORM_ANDROID || DM_PLATFORM_IOS
