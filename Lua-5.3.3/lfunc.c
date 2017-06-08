/*
** $Id: lfunc.c,v 2.45 2014/11/02 19:19:04 roberto Exp $
** Auxiliary functions to manipulate prototypes and closures
** See Copyright Notice in lua.h
*/

#define lfunc_c
#define LUA_CORE

#include "lprefix.h"


#include <stddef.h>

#include "lua.h"

#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"


//构造一个C闭包
CClosure *luaF_newCclosure (lua_State *L, int n) {
  GCObject *o = luaC_newobj(L, LUA_TCCL, sizeCclosure(n));		//申请内存空间
  CClosure *c = gco2ccl(o);										//设置对象类型 得到CClosure指针
  c->nupvalues = cast_byte(n);									//设置upvalue个数
  return c;
}

//构造一个Lua闭包
LClosure *luaF_newLclosure (lua_State *L, int n) {
  GCObject *o = luaC_newobj(L, LUA_TLCL, sizeLclosure(n));		//创建一个Lua闭包对象
  LClosure *c = gco2lcl(o);										//设置对象类型  返回LClosure对象的指针
  c->p = NULL;													//函数原型置空
  c->nupvalues = cast_byte(n);									//UpVal的个数
  while (n--) c->upvals[n] = NULL;								//初始化UpVal
  return c;
}

/*
给Closure填充upvalues值
*/
void luaF_initupvals (lua_State *L, LClosure *cl) {
  int i;
  for (i = 0; i < cl->nupvalues; i++) {			//Closure
    UpVal *uv = luaM_new(L, UpVal);				//构造一个新的UpVal对象
    uv->refcount = 1;							//引用
    uv->v = &uv->u.value;						//让upvalue数据指向自己的数据部分
    setnilvalue(uv->v);							//设置upvalue值为nil
    cl->upvals[i] = uv;							//将这个upvalue加入到closure
  }
}

//把数据栈上的值转化为upvalue
UpVal *luaF_findupval (lua_State *L, StkId level) {
  UpVal **pp = &L->openupval;
  UpVal *p;
  UpVal *uv;
  lua_assert(isintwups(L) || L->openupval == NULL);
  while (*pp != NULL && (p = *pp)->v >= level) {		//遍历openupval
    lua_assert(upisopen(p));
    if (p->v == level)									//找到了这个直接复用
      return p;
    pp = &p->u.open.next;								//遍历下一个
  }
  //没有这个upvalue 构造一个upvalue对象,并串入openupval链表
  uv = luaM_new(L, UpVal);
  uv->refcount = 0;				//引用计数置为0
  uv->u.open.next = *pp;		//将其链入openupval链表
  uv->u.open.touched = 1;
  *pp = uv;
  uv->v = level;			//当前值是在数据栈上存活
  if (!isintwups(L)) {		//线程不在具有upvalues的线程列表中 thread not in list of threads with upvalues?
    L->twups = G(L)->twups;		// 将线程连接到g->twups列表 link it to the list
    G(L)->twups = L;
  }
  return uv;
}

//清除一个函数的upvalue
void luaF_close (lua_State *L, StkId level) {
  UpVal *uv;
  while (L->openupval != NULL && (uv = L->openupval)->v >= level) {
    lua_assert(upisopen(uv));
    L->openupval = uv->u.open.next;		//将uv从openupval链表中删除
    if (uv->refcount == 0)				//该UpValue引用计数为0	释放这个upvalue
      luaM_free(L, uv);
    else {								//这个upvalue还有人在用
      setobj(L, &uv->u.value, uv->v);  //将uv->v指向的数据部分拷贝到uv->u.value
      uv->v = &uv->u.value;				//让uv->v指向uv->u.value
      luaC_upvalbarrier(L, uv);
    }
  }
}


Proto *luaF_newproto (lua_State *L) {
  GCObject *o = luaC_newobj(L, LUA_TPROTO, sizeof(Proto));
  Proto *f = gco2p(o);
  f->k = NULL;
  f->sizek = 0;
  f->p = NULL;
  f->sizep = 0;
  f->code = NULL;
  f->cache = NULL;
  f->sizecode = 0;
  f->lineinfo = NULL;
  f->sizelineinfo = 0;
  f->upvalues = NULL;
  f->sizeupvalues = 0;
  f->numparams = 0;
  f->is_vararg = 0;
  f->maxstacksize = 0;
  f->locvars = NULL;
  f->sizelocvars = 0;
  f->linedefined = 0;
  f->lastlinedefined = 0;
  f->source = NULL;
  return f;
}


void luaF_freeproto (lua_State *L, Proto *f) {
  luaM_freearray(L, f->code, f->sizecode);
  luaM_freearray(L, f->p, f->sizep);
  luaM_freearray(L, f->k, f->sizek);
  luaM_freearray(L, f->lineinfo, f->sizelineinfo);
  luaM_freearray(L, f->locvars, f->sizelocvars);
  luaM_freearray(L, f->upvalues, f->sizeupvalues);
  luaM_free(L, f);
}


/*
** Look for n-th local variable at line 'line' in function 'func'.
** Returns NULL if not found.
*/
const char *luaF_getlocalname (const Proto *f, int local_number, int pc) {
  int i;
  for (i = 0; i<f->sizelocvars && f->locvars[i].startpc <= pc; i++) {
    if (pc < f->locvars[i].endpc) {  /* is variable active? */
      local_number--;
      if (local_number == 0)
        return getstr(f->locvars[i].varname);
    }
  }
  return NULL;  /* not found */
}

