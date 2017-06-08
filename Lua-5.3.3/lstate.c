/*
** $Id: lstate.c,v 2.133 2015/11/13 12:16:51 roberto Exp $
** Global State
** See Copyright Notice in lua.h
*/

#define lstate_c
#define LUA_CORE

#include "lprefix.h"


#include <stddef.h>
#include <string.h>

#include "lua.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "llex.h"
#include "lmem.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"


#if !defined(LUAI_GCPAUSE)
#define LUAI_GCPAUSE	200  /* 200% */
#endif

#if !defined(LUAI_GCMUL)
#define LUAI_GCMUL	200 /* GC runs 'twice the speed' of memory allocation */
#endif


/*
** a macro to help the creation of a unique random seed when a state is
** created; the seed is used to randomize hashes.
*/
#if !defined(luai_makeseed)
#include <time.h>
#define luai_makeseed()		cast(unsigned int, time(NULL))
#endif



/*
Lua线程使用
在lua_State之前留出了大小为LUA_EXTRASPACE字节的空间。面对外部用户操作的指针是L而不
是LX，但L所占据的内存块的前面却是有所保留的。
用户可以在拿到L指针后向前移动指针，取得一些EXTRASPACE中额外的数
据。把这些数据放在前面而不是lua_State结构的后面避免了向用户暴露结构的大小
给L附加一些用户自定义信息在追求性能的环境很有意义。可以在为Lua编写的C模块中，直接偏移
L指针来获取一些附加信息。这比去读取L中的注册表要高效的多。另一方面，在多线程环境下，访问注册
表本身会改变界的状态，是线程不安全的。
*/
typedef struct LX {
  lu_byte extra_[LUA_EXTRASPACE];
  lua_State l;
} LX;


//主线程结合了线程状态和全局状态
//	 |-----------------struct LX----------------|
//   |  lu_byte [LUA_EXTRASPACE]  |  lua_State  |  global_State  |
//   |          extra_            |      l      |       g        |
//   |-----------------------struct LG---------------------------|
typedef struct LG {
  LX l;
  global_State g;
} LG;



#define fromstate(L)	(cast(LX *, cast(lu_byte *, (L)) - offsetof(LX, l)))


/*
** Compute an initial seed as random as possible. Rely on Address Space
** Layout Randomization (if present) to increase randomness..
*/
#define addbuff(b,p,e) \
  { size_t t = cast(size_t, e); \
    memcpy(b + p, &t, sizeof(t)); p += sizeof(t); }

static unsigned int makeseed (lua_State *L) {
  char buff[4 * sizeof(size_t)];
  unsigned int h = luai_makeseed();
  int p = 0;
  addbuff(buff, p, L);  /* heap variable */
  addbuff(buff, p, &h);  /* local variable */
  addbuff(buff, p, luaO_nilobject);  /* global variable */
  addbuff(buff, p, &lua_newstate);  /* public function */
  lua_assert(p == sizeof(buff));
  return luaS_hash(buff, p, h);
}


/*
** set GCdebt to a new value keeping the value (totalbytes + GCdebt)
** invariant (and avoiding underflows in 'totalbytes')
将GCdebt设置为新值，保持值（totalbytes + GCdebt）不变（并避免“totalbytes”向下溢出）
*/
void luaE_setdebt (global_State *g, l_mem debt) {
  l_mem tb = gettotalbytes(g);			//总的内存大小
  lua_assert(tb > 0);
  if (debt < tb - MAX_LMEM)
    debt = tb - MAX_LMEM;  /* will make 'totalbytes == MAX_LMEM' */
  g->totalbytes = tb - debt;			//设置新的总内存大小
  g->GCdebt = debt;						//设置内存负债
}

//扩展CallInfo链表
CallInfo *luaE_extendCI (lua_State *L) {
  CallInfo *ci = luaM_new(L, CallInfo);		//分配一个CallInfo结点
  lua_assert(L->ci->next == NULL);
  L->ci->next = ci;							//将新分配的结点链入调用栈
  ci->previous = L->ci;
  ci->next = NULL;
  L->nci++;									//调用栈结点数+1
  return ci;								//返回这个新分配的CallInfo结点的指针
}


//释放所有的CallInfo节点
void luaE_freeCI (lua_State *L) {
  CallInfo *ci = L->ci;
  CallInfo *next = ci->next;
  ci->next = NULL;
  while ((ci = next) != NULL) {		//遍历整个调用栈
    next = ci->next;
    luaM_free(L, ci);					//释放内存
    L->nci--;							//调用栈链表长度-1
  }
}


/*
** free half of the CallInfo structures not in use by a thread
释放一半没有被线程使用的CallInfo结构体
*/
void luaE_shrinkCI (lua_State *L) {
  CallInfo *ci = L->ci;
  CallInfo *next2;  /* next's next */
  /* while there are two nexts */
  while (ci->next != NULL && (next2 = ci->next->next) != NULL) {
    luaM_free(L, ci->next);  /* free next */
    L->nci--;
    ci->next = next2;  /* remove 'next' from the list */
    next2->previous = ci;
    ci = next2;  /* keep next's next */
  }
}


//初始化栈	数据栈和调用栈
static void stack_init (lua_State *L1, lua_State *L) {
  int i; CallInfo *ci;
  /* initialize stack array */
  L1->stack = luaM_newvector(L, BASIC_STACK_SIZE, TValue);		//栈起始地址初始化
  L1->stacksize = BASIC_STACK_SIZE;								//初始化栈的大小
  for (i = 0; i < BASIC_STACK_SIZE; i++)
    setnilvalue(L1->stack + i);									//遍历栈上的所有元素 全部置为nil
  L1->top = L1->stack;											//初始化栈顶 将其指向栈底
  L1->stack_last = L1->stack + L1->stacksize - EXTRA_STACK;		//
  /* initialize first ci */
  ci = &L1->base_ci;											//指向调用栈头结点
  ci->next = ci->previous = NULL;								//初始化其链表前后指针
  ci->callstatus = 0;											//当前结点函数状态
  ci->func = L1->top;											//将调用栈头结点函数指向栈底
  setnilvalue(L1->top++);										//将栈底设为nil 并将栈顶+1
  ci->top = L1->top + LUA_MINSTACK;								//设置调用栈栈顶
  L1->ci = ci;													//将状态机的调用栈指向调用栈头
}

//栈的释放
static void freestack (lua_State *L) {
  if (L->stack == NULL)					//状态机的栈为空 不需要释放了
    return;
  L->ci = &L->base_ci;
  luaE_freeCI(L);								//释放调用栈
  lua_assert(L->nci == 0);
  luaM_freearray(L, L->stack, L->stacksize);	//释放数据栈
}


/*
创建注册表及其预定义值
*/
static void init_registry (lua_State *L, global_State *g) {
  TValue temp;
  /* create registry */
  Table *registry = luaH_new(L);				//创建一个表
  sethvalue(L, &g->l_registry, registry);		//将申请的表设为全局状态状态机的注册表
  luaH_resize(L, registry, LUA_RIDX_LAST, 0);	//调整表的大小
  setthvalue(L, &temp, L);						// temp = L 
  luaH_setint(L, registry, LUA_RIDX_MAINTHREAD, &temp);	//registry[LUA_RIDX_MAINTHREAD] = L
  // registry[LUA_RIDX_GLOBALS] = table of globals
  sethvalue(L, &temp, luaH_new(L));  // temp = new table (global table)
  luaH_setint(L, registry, LUA_RIDX_GLOBALS, &temp);
}


/*
初始化可能导致内存分配错误的部分
*/
static void f_luaopen (lua_State *L, void *ud) {
  global_State *g = G(L);
  UNUSED(ud);
  stack_init(L, L);						//初始化主线程的数据栈和调用栈
  init_registry(L, g);					//初始化注册表
  luaS_init(L);							//初始化字符串池
  luaT_init(L);							//初始化元表用的字符串
  luaX_init(L);							//初始化词法分析用的token串
  g->gcrunning = 1;						//初始化gc正常
  g->version = lua_version(NULL);		//得到当前内核的版本 NULL表示L刚刚开始构建
  luai_userstateopen(L);
}


/*
** preinitialize a thread with consistent values without allocating
** any memory (to avoid errors)
*/
static void preinit_thread (lua_State *L, global_State *g) {
  G(L) = g;				//(L->l_G) = g
  L->stack = NULL;
  L->ci = NULL;
  L->nci = 0;
  L->stacksize = 0;
  L->twups = L;  /* thread has no upvalues */
  L->errorJmp = NULL;
  L->nCcalls = 0;
  L->hook = NULL;
  L->hookmask = 0;
  L->basehookcount = 0;
  L->allowhook = 1;
  resethookcount(L);
  L->openupval = NULL;
  L->nny = 1;
  L->status = LUA_OK;
  L->errfunc = 0;
}


static void close_state (lua_State *L) {
  global_State *g = G(L);
  luaF_close(L, L->stack);		// 清除该线程的upvalue
  luaC_freeallobjects(L);  /* collect all objects */
  if (g->version)  /* closing a fully built state? */
    luai_userstateclose(L);
  luaM_freearray(L, G(L)->strt.hash, G(L)->strt.size);		//释放短字符串
  freestack(L);
  lua_assert(gettotalbytes(g) == sizeof(LG));
  (*g->frealloc)(g->ud, fromstate(L), sizeof(LG), 0);		//释放状态机
}

//创建一个线程
LUA_API lua_State *lua_newthread (lua_State *L) {
  global_State *g = G(L);
  lua_State *L1;
  lua_lock(L);
  luaC_checkGC(L);
  /* create new thread */
  //申请内存 类型LX L1指向LX中lua_State对象
  L1 = &cast(LX *, luaM_newobject(L, LUA_TTHREAD, sizeof(LX)))->l;	
  L1->marked = luaC_white(g);
  L1->tt = LUA_TTHREAD;				//设置对象类型为 线程
  /* link it on list 'allgc' */
  L1->next = g->allgc;				//将对象链入allgc链表
  g->allgc = obj2gco(L1);
  /* anchor it on L stack */
  setthvalue(L, L->top, L1);
  api_incr_top(L);
  preinit_thread(L1, g);
  L1->hookmask = L->hookmask;
  L1->basehookcount = L->basehookcount;
  L1->hook = L->hook;
  resethookcount(L1);
  /* initialize L1 extra space */
  memcpy(lua_getextraspace(L1), lua_getextraspace(g->mainthread),
         LUA_EXTRASPACE);
  luai_userstatethread(L, L1);
  stack_init(L1, L);  /* init stack */
  lua_unlock(L);
  return L1;
}


void luaE_freethread (lua_State *L, lua_State *L1) {
  LX *l = fromstate(L1);
  luaF_close(L1, L1->stack);  /* close all upvalues for this thread */
  lua_assert(L1->openupval == NULL);
  luai_userstatefree(L, L1);
  freestack(L1);
  luaM_free(L, l);
}

//创建一个运行在新的独立的状态机中的线程。 如果无法创建线程或状态机（由于内存有限）则返回 NULL。 参数 f 是一个分配器函数； Lua
//将通过这个函数做状态机内所有的内存分配操作。 第二个参数 ud ，这个指针将在每次调用分配器时被转入。
//初始化所有global_State中将引用的数据
LUA_API lua_State *lua_newstate (lua_Alloc f, void *ud) {
  int i;
  lua_State *L;
  global_State *g;
  //cast这个宏展开后为 (LG*)((*f)(ud, NULL, LUA_TTHREAD, sizeof(LG)))	使用指定的内存分配器分配LG的内存空间	f 为l_alloc  ud 为空的时候 (LG*)(l_alloc(NULL, NULL, LUA_TIHREAD, sizeof(LG)))
  LG *l = cast(LG *, (*f)(ud, NULL, LUA_TTHREAD, sizeof(LG)));
  if (l == NULL) return NULL;			//失败了直接返回空
  L = &l->l.l;							//指向LG中的Lua状态机
  g = &l->g;							//指向LG中的Lua全局状态
  L->next = NULL;						//设置next为空
  L->tt = LUA_TTHREAD;					//设置变量类型为线程
  g->currentwhite = bitmask(WHITE0BIT);
  L->marked = luaC_white(g);
  preinit_thread(L, g);
  g->frealloc = f;						//设置内存分配函数
  g->ud = ud;							//用于内存管理
  g->mainthread = L;					//将全局表的主线程置为L
  g->seed = makeseed(L);				//生成一个随机种子 用于哈希值的计算
  g->gcrunning = 0;  /* no GC while building state */
  g->GCestimate = 0;
  g->strt.size = g->strt.nuse = 0;
  g->strt.hash = NULL;
  setnilvalue(&g->l_registry);
  g->panic = NULL;
  g->version = NULL;
  g->gcstate = GCSpause;
  g->gckind = KGC_NORMAL;
  g->allgc = g->finobj = g->tobefnz = g->fixedgc = NULL;
  g->sweepgc = NULL;
  g->gray = g->grayagain = NULL;
  g->weak = g->ephemeron = g->allweak = NULL;
  g->twups = NULL;
  g->totalbytes = sizeof(LG);
  g->GCdebt = 0;
  g->gcfinnum = 0;
  g->gcpause = LUAI_GCPAUSE;
  g->gcstepmul = LUAI_GCMUL;
  for (i=0; i < LUA_NUMTAGS; i++) g->mt[i] = NULL;
  //由于内存管理器是外部传入的，不可能保证它的返回结果。到底有多少内存可供使用也是未知数。为了
  //保证Lua虚拟机的健壮性，需要检查所有的内存分配结果。Lua自身有完整的异常处理机制可以处理这些错
  //误。所以Lua的初始化过程是分两步进行的，首先初始化不需要额外分配内存的部分，把异常处理机制先建
  //立起来，然后去调用可能引用内存分配失败导致错误的初始化代码。
  if (luaD_rawrunprotected(L, f_luaopen, NULL) != LUA_OK) {
    close_state(L);
    L = NULL;
  }
  return L;
}

/*销毁指定 Lua 状态机中的所有对象 （如果有垃圾收集相关的元方法的话，会调用它们）， 并且释放状态机
中使用的所有动态内存。 在一些平台上，你可以不必调用这个函数， 因为当宿主程序结束的时候，所有的资源
就自然被释放掉了。 另一方面，长期运行的程序，比如一个后台程序或是一个网站服务器， 会创建出多个 Lua
状态机。那么就应该在不需要时赶紧关闭它们。
*/
LUA_API void lua_close (lua_State *L) {
  L = G(L)->mainthread;  /* only the main thread can be closed */
  lua_lock(L);
  close_state(L);
}


