#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "libwebsockets.h"
#include <string.h>
#include <assert.h>
#include <stdio.h>

static void stackDump (const char* bla, lua_State *L) {
      int i;
      int top = lua_gettop(L);
      printf("%s ",bla);
      for (i = 1; i <= top; i++) {  /* repeat for each level */
        int t = lua_type(L, i);
        switch (t) {
    
          case LUA_TSTRING:  /* strings */
            printf("`%s'", lua_tostring(L, i));
            break;
    
          case LUA_TBOOLEAN:  /* booleans */
            printf(lua_toboolean(L, i) ? "true" : "false");
            break;
    
          case LUA_TNUMBER:  /* numbers */
            printf("%g", lua_tonumber(L, i));
            break;
    
          default:  /* other values */
            printf("%s %s", lua_typename(L, t), lua_tostring(L,t));
            break;
    
        }
        printf("  ");  /* put a separator */
      }
      printf("\n");  /* end the listing */
    }

#define WS_CONTEXT_META "lws.con"
#define WS_WEBSOCKET_META "lws.ws"
#define MAX_PROTOCOLS 4
#define MAX_EXTENSIONS 4

struct lws_websocket {
  lua_State *L;
  struct libwebsocket *wsi;
};

struct lws_link {
  void *userdata;
  int protocol_index;
};

struct lws_context {
  lua_State *L;
  int protocol_function_refs[MAX_PROTOCOLS];
  struct libwebsocket_context *context;
  int destroyed;
  int protocol_count;
  char protocol_names[MAX_PROTOCOLS][100];
  struct libwebsocket_protocols protocols[MAX_PROTOCOLS];
  struct libwebsocket_extension extensions[MAX_EXTENSIONS];
  struct lws_link links[MAX_PROTOCOLS];
};

static struct lws_context *lws_context_create(lua_State *L) {
  struct lws_context *user = lua_newuserdata(L, sizeof(struct lws_context));;
  memset(user, 0, sizeof(struct lws_context));
  user->L = L;
  luaL_getmetatable(L, WS_CONTEXT_META);
  lua_setmetatable(L, -2);
  return user;
}

static struct lws_websocket *lws_websocket_create(lua_State *L,struct libwebsocket* wsi) {
  struct lws_websocket *user = lua_newuserdata(L, sizeof(struct lws_websocket));;
  memset(user, 0, sizeof(struct lws_websocket));
  user->wsi = wsi;
  user->L = L;
  return user;
}

static int lws_callback(struct libwebsocket_context * context,
			struct libwebsocket *wsi,
			 enum libwebsocket_callback_reasons reason, void *dyn_user,
			void *in, size_t len, void *user) {
  struct lws_link* link = user;
  struct lws_context* lws_user = link->userdata;
  lua_State* L = lws_user->L;
  int argc = 0;
  int res;
  int ws_ref = LUA_REFNIL;
  //  printf("TOP %d %s %d\n",lua_gettop(L),lua_typename(L,lua_type(L,1)),luaL_optint(L,2,-100));
  stackDump("cbIN",L);
 // printf("CALLBACK %d %p %p %d %p\n",reason,dyn_user,in,len,user);
  if(reason == LWS_CALLBACK_ESTABLISHED || reason == LWS_CALLBACK_CLIENT_ESTABLISHED) {
    lws_websocket_create(L, wsi);
    stackDump("cbNEW",L);
    luaL_getmetatable(L, WS_WEBSOCKET_META);
    stackDump("cbNEW3",L);
    lua_setmetatable(L, -2);
    stackDump("cbNEW4",L);
    ws_ref = luaL_ref(L, LUA_REGISTRYINDEX);    
    *(int *)dyn_user = ws_ref;
    stackDump("cbNEW6",L);
  }
  else if(reason == LWS_CALLBACK_CLOSED) {
    printf("CLOSED\n");
    luaL_unref(L, LUA_REGISTRYINDEX,*(int *)dyn_user);
  }
  /* push Lua protocol callback function on stack */
  lua_rawgeti(L, LUA_REGISTRYINDEX, lws_user->protocol_function_refs[link->protocol_index]);  
  /* first arguments is websocket (userdata). may be nil */
  lua_rawgeti(L, LUA_REGISTRYINDEX, ws_ref);
  ++argc;
  /* second argumen arguments is reason as number */
  lua_pushnumber(L,reason);
  ++argc;

  switch(reason) {
  case LWS_CALLBACK_SET_MODE_POLL_FD:
  case LWS_CALLBACK_CLEAR_MODE_POLL_FD:
    /* push fd */
    lua_pushnumber(L,(int)(dyn_user));
    ++argc;

    /* push modification POLLIN or POLLOUT */
    lua_pushnumber(L,len);
    ++argc;
    break;
  case LWS_CALLBACK_ADD_POLL_FD:    
  case LWS_CALLBACK_DEL_POLL_FD:
    /* push fd */
    lua_pushnumber(L,(int)(dyn_user));
    ++argc;
    break;
  case LWS_CALLBACK_RECEIVE:
  case LWS_CALLBACK_CLIENT_RECEIVE:
  case LWS_CALLBACK_HTTP:
    /* push data */
    if(len > 0 && in != NULL) {
      lua_pushlstring(L,in,len);
      ++argc;    
    }
    break;
  }
  lua_call(lws_user->L,argc,1);  
  res = luaL_optint(L,-1,0); /* 0 means ok / continue */

  lua_pop(L,1);
  stackDump("cbOUT",L);
  return res;
}


static int lws_context(lua_State *L) {
  int port = 0;
  const char* interf = NULL;
  const char* ssl_cert_filepath = NULL;
  const char* ssl_private_key_filepath = NULL;
  int gid = -1;
  int uid = -1;
  unsigned int options = 0;
  struct lws_context *user = lws_context_create(L);
  int index = 0;
  if( lua_type(L, 1) == LUA_TTABLE ) {
    lua_getfield(L, 1, "port");
    port = luaL_optint(L, -1, 0);    
    lua_pop(L, 1);

    lua_getfield(L, 1, "interf");
    interf = luaL_optstring(L, -1, NULL);    
    lua_pop(L, 1);

    /* push protocols table on top */
    lua_getfield(L, 1, "protocols");    
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushvalue(L, 1);   
    lua_setfenv(L, -3);

    /* nil is top (-1) */
    lua_pushnil(L);
    /* lua_next pushed key at -2 and value at -1 (top)  */
    while(user->protocol_count < MAX_PROTOCOLS && lua_next(L, -2) != 0) {  
      int n = user->protocol_count;
      strcpy(user->protocol_names[n],luaL_checkstring(L,-2));
      user->protocols[n].name = user->protocol_names[n];
      user->protocols[n].callback = lws_callback;
      user->protocols[n].per_session_data_size = sizeof(int); // will hold a luaL_ref to the websocket table
      user->protocol_function_refs[n] = luaL_ref(L, LUA_REGISTRYINDEX);
      ++user->protocol_count;
      user->links[n].userdata = user;
      user->links[n].protocol_index = n;
      user->protocols[n].user = &user->links[n];
    }
    /* pop protocols table on top */
    lua_pop(L, 1);
  }
  user->context = libwebsocket_create_context(port, interf, user->protocols, user->extensions, ssl_cert_filepath, ssl_private_key_filepath, gid, uid, options);
  return 1;
}

static int lws_context_destroy(lua_State *L) {  
  int n = 0;
  struct lws_context *user = (struct lws_context *)luaL_checkudata(L, 1, WS_CONTEXT_META);
  if(!user->destroyed) {
    if(user->context != NULL) {
      libwebsocket_context_destroy(user->context);
    }
    luaL_argcheck(L, user, 1, "websocket context expected");
    while(user->protocol_function_refs[n]) {
      luaL_unref(L, LUA_REGISTRYINDEX, user->protocol_function_refs[n]);
      ++n;
    }
    user->destroyed = 1;
  }
  return 0;
}

static struct lws_context * checked_context(lua_State *L) {
  struct lws_context *user = (struct lws_context *)luaL_checkudata(L, 1, WS_CONTEXT_META);  
  if(user->destroyed) {
    luaL_error(user->L, "websocket context destroyed");
  }  
  return user;
}

static struct lws_websocket * checked_websocket(lua_State *L) {  
  struct lws_websocket *user = (struct lws_websocket *)luaL_checkudata(L, 1, WS_WEBSOCKET_META);  
  return user;
}

static int lws_context_fork_service_loop(lua_State *L) {
  struct lws_context *user = checked_context(L);  
  int n = libwebsockets_fork_service_loop(user->context);
  lua_pushinteger(user->L, n);
  return 1;
}

static int lws_websocket_tostring(lua_State *L) {  
  struct lws_websocket *user = checked_websocket(L);
  lua_pushstring(L, "websocket XX");
  return 1;
}

static int lws_context_service(lua_State *L) {
  struct lws_context *user = checked_context(L);
  int timeout_ms = luaL_optint(L, 2, 0);
  lua_pop(L,1);
  int n = libwebsocket_service(user->context, timeout_ms);
  lua_pushinteger(user->L, n);
  return 1;
}

static const struct luaL_Reg lws_module_methods [] = {
  {"context",lws_context},
  {NULL,NULL}
};

static const struct luaL_Reg lws_context_methods [] = {
  {"destroy",lws_context_destroy},
  {"__gc",lws_context_destroy},
  {"fork_service_loop",lws_context_fork_service_loop},
  {"service",lws_context_service},
  {NULL,NULL}
};

static const struct luaL_Reg lws_websocket_methods [] = {
  {"__tostring",lws_websocket_tostring},
  {NULL,NULL}
};

int luaopen_websockets(lua_State *L) {
  luaL_newmetatable(L, WS_CONTEXT_META);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_register(L, NULL, lws_context_methods);
  luaL_newmetatable(L, WS_WEBSOCKET_META);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_register(L, NULL, lws_websocket_methods);
  luaL_register(L, "websockets", lws_module_methods);
  return 1;
}