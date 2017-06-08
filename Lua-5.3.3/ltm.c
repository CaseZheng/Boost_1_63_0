/*
** $Id: ltm.c,v 2.37 2016/02/26 19:20:15 roberto Exp $
** Tag methods
** See Copyright Notice in lua.h
*/

#define ltm_c
#define LUA_CORE

#include "lprefix.h"


#include <string.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h" 
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lvm.h"


static const char udatatypename[] = "userdata";

LUAI_DDEF const char *const luaT_typenames_[LUA_TOTALTAGS] = {
  "no value",
  "nil", "boolean", udatatypename, "number",
  "string", "table", "function", udatatypename, "thread",
  "proto" /* this last case is used for tests only */
};

//元表初始化 主要初始化元方法所要用的字符串对象
void luaT_init (lua_State *L) {
  static const char *const luaT_eventname[] = {  /* ORDER TM */
    "__index", "__newindex",
    "__gc", "__mode", "__len", "__eq",
    "__add", "__sub", "__mul", "__mod", "__pow",
    "__div", "__idiv",
    "__band", "__bor", "__bxor", "__shl", "__shr",
    "__unm", "__bnot", "__lt", "__le",
    "__concat", "__call"
  };
  int i;
  for (i=0; i<TM_N; i++) {
    G(L)->tmname[i] = luaS_new(L, luaT_eventname[i]);		//初始化元方法所使用的字符串
    luaC_fix(L, obj2gco(G(L)->tmname[i]));					//设置这些字符串不会被gc回收
  }
}


/*
** function to be used with macro "fasttm": optimized for absence of
** tag methods
*/
const TValue *luaT_gettm (Table *events, TMS event, TString *ename) {
  const TValue *tm = luaH_getshortstr(events, ename);
  lua_assert(event <= TM_EQ);
  if (ttisnil(tm)) {  /* no tag method? */
    events->flags |= cast_byte(1u<<event);  /* cache this fact */
    return NULL;
  }
  else return tm;
}

//
const TValue *luaT_gettmbyobj (lua_State *L, const TValue *o, TMS event) {
  Table *mt;
  switch (ttnov(o)) {
    case LUA_TTABLE:				//table
      mt = hvalue(o)->metatable;
      break;
    case LUA_TUSERDATA:				//userdata
      mt = uvalue(o)->metatable;
      break;
    default:
      mt = G(L)->mt[ttnov(o)];
  }
  return (mt ? luaH_getshortstr(mt, G(L)->tmname[event]) : luaO_nilobject);
}


/*
** Return the name of the type of an object. For tables and userdata
** with metatable, use their '__name' metafield, if present.
*/
const char *luaT_objtypename (lua_State *L, const TValue *o) {
  Table *mt;
  if ((ttistable(o) && (mt = hvalue(o)->metatable) != NULL) ||
      (ttisfulluserdata(o) && (mt = uvalue(o)->metatable) != NULL)) {
    const TValue *name = luaH_getshortstr(mt, luaS_new(L, "__name"));
    if (ttisstring(name))  /* is '__name' a string? */
      return getstr(tsvalue(name));  /* use it as type name */
  }
  return ttypename(ttnov(o));  /* else use standard type name */
}


void luaT_callTM (lua_State *L, const TValue *f, const TValue *p1,
                  const TValue *p2, TValue *p3, int hasres) {
  ptrdiff_t result = savestack(L, p3);
  StkId func = L->top;
  setobj2s(L, func, f);			//将函数f压栈
  setobj2s(L, func + 1, p1);	//将参数p1压栈
  setobj2s(L, func + 2, p2);	//将参数p2压栈
  L->top += 3;					//栈顶调整
  if (!hasres)					//没有输出 p3为输入参数 压栈
    setobj2s(L, L->top++, p3);
  /* metamethod may yield only when called from Lua code */
  if (isLua(L->ci))				//Lua函数
    luaD_call(L, func, hasres);	//调用函数
  else
    luaD_callnoyield(L, func, hasres);
  if (hasres) {					//有输出参数
    p3 = restorestack(L, result);
    setobjs2s(L, p3, --L->top);
  }
}


int luaT_callbinTM (lua_State *L, const TValue *p1, const TValue *p2,
                    StkId res, TMS event) {
  const TValue *tm = luaT_gettmbyobj(L, p1, event);		//在第一个对象中看下有没有需要的元方法
  if (ttisnil(tm))										//第一个对象中没找到
    tm = luaT_gettmbyobj(L, p2, event);					//在第二个对象中找需要的元方法
  if (ttisnil(tm)) return 0;							//都没找到 返回0
  luaT_callTM(L, tm, p1, p2, res, 1);					//调用这个元方法
  return 1;
}


void luaT_trybinTM (lua_State *L, const TValue *p1, const TValue *p2,
                    StkId res, TMS event) {
  if (!luaT_callbinTM(L, p1, p2, res, event)) {
    switch (event) {
      case TM_CONCAT:
        luaG_concaterror(L, p1, p2);
      /* call never returns, but to avoid warnings: *//* FALLTHROUGH */
      case TM_BAND: case TM_BOR: case TM_BXOR:
      case TM_SHL: case TM_SHR: case TM_BNOT: {
        lua_Number dummy;
        if (tonumber(p1, &dummy) && tonumber(p2, &dummy))
          luaG_tointerror(L, p1, p2);
        else
          luaG_opinterror(L, p1, p2, "perform bitwise operation on");
      }
      /* calls never return, but to avoid warnings: *//* FALLTHROUGH */
      default:
        luaG_opinterror(L, p1, p2, "perform arithmetic on");
    }
  }
}


int luaT_callorderTM (lua_State *L, const TValue *p1, const TValue *p2,
                      TMS event) {
  if (!luaT_callbinTM(L, p1, p2, L->top, event))
    return -1;  /* no metamethod */
  else
    return !l_isfalse(L->top);
}

