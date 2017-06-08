/*
** $Id: ldo.c,v 2.151 2015/12/16 16:40:07 roberto Exp $
** Stack and Call structure of Lua
** See Copyright Notice in lua.h
*/

#define ldo_c
#define LUA_CORE

#include "lprefix.h"


#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lundump.h"
#include "lvm.h"
#include "lzio.h"



#define errorstatus(s)	((s) > LUA_YIELD)


/*
** {======================================================
** Error-recovery functions
** =======================================================
*/

/*
** LUAI_THROW/LUAI_TRY define how Lua does exception handling. By
** default, Lua handles errors with exceptions when compiling as
** C++ code, with _longjmp/_setjmp when asked to use them, and with
** longjmp/setjmp otherwise.
*/
#if !defined(LUAI_THROW)				/* { */

#if defined(__cplusplus) && !defined(LUA_USE_LONGJMP)	/* { */

/* C++ exceptions */
#define LUAI_THROW(L,c)		throw(c)
#define LUAI_TRY(L,c,a) \
	try { a } catch(...) { if ((c)->status == 0) (c)->status = -1; }
#define luai_jmpbuf		int  /* dummy variable */

#elif defined(LUA_USE_POSIX)				/* }{ */

/* in POSIX, try _longjmp/_setjmp (more efficient) */
#define LUAI_THROW(L,c)		_longjmp((c)->b, 1)
#define LUAI_TRY(L,c,a)		if (_setjmp((c)->b) == 0) { a }
#define luai_jmpbuf		jmp_buf

#else							/* }{ */

/* ISO C handling with long jumps */
/*
#include <setjmp.h>
void longjmp(jmp_buf env, int val);

longjmp()用于恢复由最近一次调用setjmp()时保存到env的状态信息。当它执行完时，
程序就象setjmp()刚刚执行完并返回非0值val那样继续执行。包含setjmp()宏调用的函数
一定不能已经终止。所有可访问的对象的值都与调用longjmp()时相同，唯一的例外是，
那些调用setjmp()宏的函数中的非volatile自动变量如果在调用setjmp()后有了改变，
那么就变成未定义的。
*/
#define LUAI_THROW(L,c)		longjmp((c)->b, 1)
/*
#include <setjmp.h>
int setjmp(jmp_buf env);
setjmp()宏把当前状态信息保存到env中，供以后longjmp()恢复状态信息时使用。
如果是直接调用setjmp()，那么返回值为0；如果是由于调用longjmp()而调用setjmp()，
那么返回值非0。setjmp()只能在某些特定情况下调用，如在if语句、 switch语句及循环语
句的条件测试部分以及一些简单的关系表达式中。
*/
#define LUAI_TRY(L,c,a)		if (setjmp((c)->b) == 0) { a }
#define luai_jmpbuf		jmp_buf

#endif							/* } */

#endif							/* } */



/* chain list of long jump buffers */
struct lua_longjmp {
  struct lua_longjmp *previous;	//链表
  luai_jmpbuf b;					//用于记录当前状态信息的内存空间
  volatile int status;				//错误码
};


static void seterrorobj (lua_State *L, int errcode, StkId oldtop) {
  switch (errcode) {
    case LUA_ERRMEM: {		//内存错误
      setsvalue2s(L, oldtop, G(L)->memerrmsg);		//将预注册的错误信息放在栈上
      break;
    }
    case LUA_ERRERR: {
      setsvalue2s(L, oldtop, luaS_newliteral(L, "error in error handling"));
      break;
    }
    default: {
      setobjs2s(L, oldtop, L->top - 1);			//将错误信息放在栈上
      break;
    }
  }
  L->top = oldtop + 1;
}

//处理异常
l_noret luaD_throw (lua_State *L, int errcode) {
  if (L->errorJmp) {					//有返回点
    L->errorJmp->status = errcode;	//设置错误码
    LUAI_THROW(L, L->errorJmp);			//恢复状态信息
  }
  else {								//没有返回点
    global_State *g = G(L);
    L->status = cast_byte(errcode);		//设置状态机错误码
    if (g->mainthread->errorJmp) {			//主线程有返回点  
      setobjs2s(L, g->mainthread->top++, L->top - 1);	//将错误信息拷贝到主线程的栈顶
      luaD_throw(g->mainthread, errcode);  //抛出异常到主线程
    }
    else {								//主线程没有返回点
      if (g->panic) {					//如果有panic函数
        seterrorobj(L, errcode, L->top);  /* assume EXTRA_STACK */
        if (L->ci->top < L->top)
          L->ci->top = L->top;  /* pushing msg. can break this invariant */
        lua_unlock(L);
        g->panic(L);					//调用panic函数
      }
      abort();							//终止程序
    }
  }
}

/*
设置新的jmpbuf，串到链表上，调用函数。调用完成后恢复进入时的状态。如
果想回直接返回到最近的错误恢复点，只需要调用longjmp
*/
int luaD_rawrunprotected (lua_State *L, Pfunc f, void *ud) {
  unsigned short oldnCcalls = L->nCcalls;		//记录嵌套的C调用数量
  struct lua_longjmp lj;
  lj.status = LUA_OK;					//设置一个longjmp的状态
  lj.previous = L->errorJmp;			//将这个longjmp加入到errorJmp
  L->errorJmp = &lj;
  LUAI_TRY(L, &lj,
    (*f)(L, ud);
  );									//保存当前的状态信息并调用f函数
  L->errorJmp = lj.previous;			//将longjmp取出errorJmp链表
  L->nCcalls = oldnCcalls;			//恢复嵌套C调用个数
  return lj.status;						//返回longjmp的状态
}

/* }====================================================== */


/*
** {==================================================================
** Stack reallocation
栈上数据重定位
** ===================================================================
*/
static void correctstack (lua_State *L, TValue *oldstack) {
  CallInfo *ci;
  UpVal *up;
  L->top = (L->top - oldstack) + L->stack;					//栈顶重置
  for (up = L->openupval; up != NULL; up = up->u.open.next)	//遍历upvalue 重定位
    up->v = (up->v - oldstack) + L->stack;
  for (ci = L->ci; ci != NULL; ci = ci->previous) {			//遍历调用栈  调用栈各个结点的数据重定位
    ci->top = (ci->top - oldstack) + L->stack;
    ci->func = (ci->func - oldstack) + L->stack;
    if (isLua(ci))												//当前结点表示Lua函数
      ci->u.l.base = (ci->u.l.base - oldstack) + L->stack;
  }
}


/* some space for error handling */
#define ERRORSTACKSIZE	(LUAI_MAXSTACK + 200)

//调整栈的大小
void luaD_reallocstack (lua_State *L, int newsize) {
  TValue *oldstack = L->stack;											//记录旧栈的位置
  int lim = L->stacksize;												//记录旧的栈的大小
  lua_assert(newsize <= LUAI_MAXSTACK || newsize == ERRORSTACKSIZE);
  lua_assert(L->stack_last - L->stack == L->stacksize - EXTRA_STACK);
  luaM_reallocvector(L, L->stack, L->stacksize, newsize, TValue);		//扩大栈的尺寸
  for (; lim < newsize; lim++)					//遍历增长后增长的那一部分 设为nil
    setnilvalue(L->stack + lim);
  L->stacksize = newsize;						//将栈的尺寸设为增大后的尺寸
  L->stack_last = L->stack + newsize - EXTRA_STACK;		//设置栈正常的最后一个位置
  correctstack(L, oldstack);					//对栈上的数据重新定位
}


//栈增长
void luaD_growstack (lua_State *L, int n) {
  int size = L->stacksize;
  if (size > LUAI_MAXSTACK)				//栈的尺寸
    luaD_throw(L, LUA_ERRERR);
  else {
    int needed = cast_int(L->top - L->stack) + n + EXTRA_STACK;	//需要的尺寸 原大小+新增大小+额外大小
    int newsize = 2 * size;			//扩大为原先的两倍
    if (newsize > LUAI_MAXSTACK) newsize = LUAI_MAXSTACK;		//扩大两倍后超出范围 设为最大值
    if (newsize < needed) newsize = needed;					//如果需要的比扩大两倍还大 就设为需要的大小
    if (newsize > LUAI_MAXSTACK) {		//如果需要的大小超出了范围 栈溢出
      luaD_reallocstack(L, ERRORSTACKSIZE);
      luaG_runerror(L, "stack overflow");
    }
    else
      luaD_reallocstack(L, newsize);	//按新的尺寸调整
  }
}


static int stackinuse (lua_State *L) {
  CallInfo *ci;
  StkId lim = L->top;
  for (ci = L->ci; ci != NULL; ci = ci->previous) {
    lua_assert(ci->top <= L->stack_last);
    if (lim < ci->top) lim = ci->top;
  }
  return cast_int(lim - L->stack) + 1;  /* part of stack in use */
}


void luaD_shrinkstack (lua_State *L) {
  int inuse = stackinuse(L);
  int goodsize = inuse + (inuse / 8) + 2*EXTRA_STACK;
  if (goodsize > LUAI_MAXSTACK) goodsize = LUAI_MAXSTACK;
  if (L->stacksize > LUAI_MAXSTACK)  /* was handling stack overflow? */
    luaE_freeCI(L);  /* free all CIs (list grew because of an error) */
  else
    luaE_shrinkCI(L);  /* shrink list */
  if (inuse <= LUAI_MAXSTACK &&  /* not handling stack overflow? */
      goodsize < L->stacksize)  /* trying to shrink? */
    luaD_reallocstack(L, goodsize);  /* shrink it */
  else
    condmovestack(L,,);  /* don't change stack (change only for debugging) */
}


void luaD_inctop (lua_State *L) {
  luaD_checkstack(L, 1);
  L->top++;
}

/* }================================================================== */


/*
** Call a hook for the given event. Make sure there is a hook to be
** called. (Both 'L->hook' and 'L->hookmask', which triggers this
** function, can be changed asynchronously by signals.)
Lua可以为每个线程设置一个钩子函数，用于调试、统计和其它一些特殊用法
钩子函数是一个畃函数，用内部luaD_hook封装起来。
*/
void luaD_hook (lua_State *L, int event, int line) {
  lua_Hook hook = L->hook;
  if (hook && L->allowhook) {  /* make sure there is a hook */
    CallInfo *ci = L->ci;
    ptrdiff_t top = savestack(L, L->top);
    ptrdiff_t ci_top = savestack(L, ci->top);
    lua_Debug ar;
    ar.event = event;
    ar.currentline = line;
    ar.i_ci = ci;
    luaD_checkstack(L, LUA_MINSTACK);  /* ensure minimum stack size */
    ci->top = L->top + LUA_MINSTACK;
    lua_assert(ci->top <= L->stack_last);
    L->allowhook = 0;  /* cannot call hooks inside a hook */
    ci->callstatus |= CIST_HOOKED;
    lua_unlock(L);
    (*hook)(L, &ar);
    lua_lock(L);
    lua_assert(!L->allowhook);
    L->allowhook = 1;
    ci->top = restorestack(L, ci_top);
    L->top = restorestack(L, top);
    ci->callstatus &= ~CIST_HOOKED;
  }
}


static void callhook (lua_State *L, CallInfo *ci) {
  int hook = LUA_HOOKCALL;
  ci->u.l.savedpc++;  /* hooks assume 'pc' is already incremented */
  if (isLua(ci->previous) &&
      GET_OPCODE(*(ci->previous->u.l.savedpc - 1)) == OP_TAILCALL) {
    ci->callstatus |= CIST_TAIL;
    hook = LUA_HOOKTAILCALL;
  }
  luaD_hook(L, hook, -1);
  ci->u.l.savedpc--;  /* correct 'pc' */
}

//adjust_varargs 将需要个固定参数复制到被调用的函数的新一级数据栈帧上，而变长参数留在原地，即上一个栈帧的末尾。
//p函数原型  actual真实参数个数
static StkId adjust_varargs (lua_State *L, Proto *p, int actual) {
  int i;
  int nfixargs = p->numparams;		//最小参数个数
  StkId base, fixed;
  /* move fixed parameters to final position */
  //移动固定参数到最终位置
  fixed = L->top - actual;		//第一个固定参数
  base = L->top;					//第一个参数的固定位置
  for (i = 0; i < nfixargs && i < actual; i++) {		//将参数移动到栈顶
    setobjs2s(L, L->top++, fixed + i);
    setnilvalue(fixed + i);			//删除已移动参数
  }
  for (; i < nfixargs; i++)		//不足参数最小个数   用nil补全
    setnilvalue(L->top++);
  return base;
}


/*
** Check whether __call metafield of 'func' is a function. If so, put
** it in stack below original 'func' so that 'luaD_precall' can call
** it. Raise an error if __call metafield is not a function.
*/
static void tryfuncTM (lua_State *L, StkId func) {
  const TValue *tm = luaT_gettmbyobj(L, func, TM_CALL);	//从原表中得到__call
  StkId p;
  if (!ttisfunction(tm))					//如果不是函数  错误
    luaG_typeerror(L, func, "call");
  /* Open a hole inside the stack at 'func' */
  //根据Lua定义,通过元方法进行的函数调用和原生的函数调用有所区别。通过元方法进行的函数调用，
  //会将对象自身作为第一个参数传入。这就需要移动数据栈，把对象插到第一个参数的位置。
  for (p = L->top; p > func; p--)			//将所有参数全部移动一位
    setobjs2s(L, p, p-1);
  L->top++;									//栈顶+1
  setobj2s(L, func, tm);					//*func = *tm
}


//得到Lua状态中一个空闲CallInfo结点  如果没有 则扩展一个返回
#define next_ci(L) (L->ci = (L->ci->next ? L->ci->next : luaE_extendCI(L)))


/* macro to check stack size, preserving 'p' */
#define checkstackp(L,n,p)  \
  luaD_checkstackaux(L, n, \
    ptrdiff_t t__ = savestack(L, p);  /* save 'p' */ \
    luaC_checkGC(L),  /* stack grow uses memory */ \
    p = restorestack(L, t__))  /* 'pos' part: restore 'p' */


/*
** Prepares a function call: checks the stack, creates a new CallInfo
** entry, fills in the relevant information, calls hook if needed.
** If function is a C function, does the call, too. (Otherwise, leave
** the execution ('luaV_execute') to the caller, to allow stackless
** calls.) Returns true iff function has been executed (C function).
luaD_precall 执行的是函数调用部分的工作
准备一个函数调用：检查堆栈，创建一个新的CallInfo条目，填充相关信息，如果需要，调用钩子。 
如果函数是C函数，也调用。 （否则，将执行（'luaV_execute'）给调用者，以允许无栈调用。）
如果函数已执行（C函数），则返回true。
*/
int luaD_precall (lua_State *L, StkId func, int nresults) {
  lua_CFunction f;
  CallInfo *ci;
  switch (ttype(func)) {		//要执行函数的类型
	//C函数和C闭包仅仅是在储存上有所不同，处理逻辑是一致的：压入新的CallInfo，把数
	//据栈栈顶设置好。调用C函数，然后luaD_poscall。
    case LUA_TCCL:				//C闭包
      f = clCvalue(func)->f;	//设置C函数
      goto Cfunc;
    case LUA_TLCF:				//C函数
      f = fvalue(func);			//设置C函数
     Cfunc: {
      int n;  /* number of returns */
      checkstackp(L, LUA_MINSTACK, func);  /* ensure minimum stack size */
      ci = next_ci(L);				//得到一个空闲CallInfo结点
      ci->nresults = nresults;	//初始化CallInfo 预期结果数 要执行的函数在数据栈上的位置 调用栈栈顶
      ci->func = func;
      ci->top = L->top + LUA_MINSTACK;
      lua_assert(ci->top <= L->stack_last);
      ci->callstatus = 0;
      if (L->hookmask & LUA_MASKCALL)
        luaD_hook(L, LUA_HOOKCALL, -1);
      lua_unlock(L);
      n = (*f)(L);					//调用f函数
      lua_lock(L);
      api_checknelems(L, n);		//检查堆栈的剩余空间
      luaD_poscall(L, ci, L->top - n, n);		//
      return 1;
    }
    case LUA_TLCL: {			//Lua闭包
	  //先通过传入函数对象在数据栈上的位置和栈顶差，计算出数据栈上的调用参数个数n。
	  //如果Lua函数对输入参数个数有明确的最小要求，这点可以通过查询函数原型的numparams字段获知；
	  //若栈上提供的参数数量不足，就需要把不足的部分补为nuk。当调用函数需要可变参数的时候，还需要进一步处理
	  //使用adjust_varargs
      StkId base;
      Proto *p = clLvalue(func)->p;
      int n = cast_int(L->top - func) - 1;		//真实参数的个数
      int fsize = p->maxstacksize;  /* frame size */
      checkstackp(L, fsize, func);
      if (p->is_vararg != 1) {					//未使用变长参数
        for (; n < p->numparams; n++)			//按照最小参数的大小 给栈上填充nil
          setnilvalue(L->top++);
        base = func + 1;
      }
      else
        base = adjust_varargs(L, p, n);			//变长参数
      ci = next_ci(L);					//得到一个空闲CallInfo结点
      ci->nresults = nresults;		//初始化CallInfo 预期结果数 要执行的函数在数据栈上的位置
      ci->func = func;
      ci->u.l.base = base;				//参数的起始位置
      L->top = ci->top = base + fsize;		//栈顶的设置
      lua_assert(ci->top <= L->stack_last);
      ci->u.l.savedpc = p->code;		//开始时的操作码
      ci->callstatus = CIST_LUA;		//设置该结点允许状态
      if (L->hookmask & LUA_MASKCALL)
        callhook(L, ci);
      return 0;
    }
    default: {						//不是一个函数
      checkstackp(L, 1, func);		// ensure space for metamethod
      tryfuncTM(L, func);			//尝试从原表中得到真正的调用函数 try to get '__call' metamethod
      return luaD_precall(L, func, nresults);		//递归调用自己
    }
  }
}


/*
** Given 'nres' results at 'firstResult', move 'wanted' of them to 'res'.
** Handle most typical cases (zero results for commands, one result for
** expressions, multiple results for tail calls/single parameters)
** separated.
移动结果 nres返回结果个数  wanted预期的返回值数量 res将结果移动到的位置 firstResult 返回值的起始位置
*/
static int moveresults (lua_State *L, const TValue *firstResult, StkId res,
                                      int nres, int wanted) {
  switch (wanted) {			//预期的返回的参数个数
    case 0: break;  /* nothing to move */
    case 1: {								//需要一个参数
      if (nres == 0)						//返回的参数个数为0
        firstResult = luaO_nilobject;		//指向一个nil
      setobjs2s(L, res, firstResult);	    //拷贝返回值到res
      break;
    }
    case LUA_MULTRET: {
      int i;
      for (i = 0; i < nres; i++)  /* move all results to correct place */
        setobjs2s(L, res + i, firstResult + i);
      L->top = res + nres;
      return 0;  /* wanted == LUA_MULTRET */
    }
    default: {
      int i;
      if (wanted <= nres) {			//返回的参数个数大于等于预期
        for (i = 0; i < wanted; i++)	//只移动预期数量的参数到res
          setobjs2s(L, res + i, firstResult + i);
      }
      else {							//返回的参数个数不足
        for (i = 0; i < nres; i++)		//移动所有的返回参数到res
          setobjs2s(L, res + i, firstResult + i);
        for (; i < wanted; i++)		//不足期望个数的部分用nil补全
          setnilvalue(res + i);
      }
      break;
    }
  }
  L->top = res + wanted;				//设置栈顶位置
  return 1;
}


/*
** Finishes a function call: calls hook if necessary, removes CallInfo,
** moves current number of results to proper place; returns 0 iff call
** wanted multiple (variable number of) results.
luaD_procall的是函数返回的工作。
完成函数调用：如果需要，调用hook，删除CallInfo，将当前结果数移动到正确的位置; 
如果调用需要多个（可变数量）结果，则返回0。
ci 调用栈结点 firstResult 返回值起始位置 nres返回值个数
*/
int luaD_poscall (lua_State *L, CallInfo *ci, StkId firstResult, int nres) {
  StkId res;
  int wanted = ci->nresults;							//预期的返回结果的个数
  if (L->hookmask & (LUA_MASKRET | LUA_MASKLINE)) {
    if (L->hookmask & LUA_MASKRET) {
      ptrdiff_t fr = savestack(L, firstResult);  /* hook may change stack */
      luaD_hook(L, LUA_HOOKRET, -1);
      firstResult = restorestack(L, fr);
    }
    L->oldpc = ci->previous->u.l.savedpc;  /* 'oldpc' for caller function */
  }
  res = ci->func;  //将返回值的起始位置定位到调用栈的起始位置
  L->ci = ci->previous;					//当前调用栈-1
  //移动返回值
  return moveresults(L, firstResult, res, nres, wanted);
}


/*
** Check appropriate error for stack overflow ("regular" overflow or
** overflow while handling stack overflow). If 'nCalls' is larger than
** LUAI_MAXCCALLS (which means it is handling a "regular" overflow) but
** smaller than 9/8 of LUAI_MAXCCALLS, does not report an error (to
** allow overflow handling to work)
*/
static void stackerror (lua_State *L) {
  if (L->nCcalls == LUAI_MAXCCALLS)
    luaG_runerror(L, "C stack overflow");
  else if (L->nCcalls >= (LUAI_MAXCCALLS + (LUAI_MAXCCALLS>>3)))
    luaD_throw(L, LUA_ERRERR);  /* error while handing stack error */
}


/*
** Call a function (C or Lua). The function to be called is at *func.
** The arguments are on the stack, right after the function.
** When returns, all the results are on the stack, starting at the original
** function position.
调用函数（C或Lua）。 要调用的函数是在* func。 参数在堆栈上，紧跟在函数之后。
返回后，所有结果都在堆栈上，从原始函数位置开始。
*/
void luaD_call (lua_State *L, StkId func, int nResults) {
  if (++L->nCcalls >= LUAI_MAXCCALLS)
    stackerror(L);
  if (!luaD_precall(L, func, nResults))  /* is a Lua function? */
    luaV_execute(L);  /* call it */
  L->nCcalls--;
}


/*
** Similar to 'luaD_call', but does not allow yields during the call
类似luaD_call 但不允许在调用的时候 yields
*/
void luaD_callnoyield (lua_State *L, StkId func, int nResults) {
  L->nny++;
  luaD_call(L, func, nResults);
  L->nny--;
}


/*
** Completes the execution of an interrupted C function, calling its
** continuation function.
完成中断的C函数的执行，调用其连续函数。
*/
static void finishCcall (lua_State *L, int status) {
  CallInfo *ci = L->ci;
  int n;
  /* must have a continuation and must be able to call it */
  lua_assert(ci->u.c.k != NULL && L->nny == 0);
  /* error status can only happen in a protected call */
  lua_assert((ci->callstatus & CIST_YPCALL) || status == LUA_YIELD);
  if (ci->callstatus & CIST_YPCALL) {	//在lua_pcallk 或 lua_pcall 进入运行后中断
    ci->callstatus &= ~CIST_YPCALL;		//取消从lua_pcallk进入运行的标记
    L->errfunc = ci->u.c.old_errfunc;	//恢复当前错误处理函数
  }
  /* finish 'lua_callk'/'lua_pcall'; CIST_YPCALL and 'errfunc' already handled */
  //完成lua_callk未完成的工作
  adjustresults(L, ci->nresults);		//根据预期返回的结果数和当前栈顶位置 设置栈顶
  /* call continuation function */
  lua_unlock(L);
  //调用它恢复上下文信息
  n = (*ci->u.c.k)(L, status, ci->u.c.ctx);
  lua_lock(L);
  api_checknelems(L, n);				//检查栈上空闲空间数
  //完成函数返回
  luaD_poscall(L, ci, L->top - n, n);
}


/*
** Executes "full continuation" (everything in the stack) of a
** previously interrupted coroutine until the stack is empty (or another
** interruption long-jumps out of the loop). If the coroutine is
** recovering from an error, 'ud' points to the error status, which must
** be passed to the first continuation function (otherwise the default
** status is LUA_YIELD).
*/
static void unroll (lua_State *L, void *ud) {
  if (ud != NULL)  /* error status? */
    finishCcall(L, *(int *)ud);  /* finish 'lua_pcallk' callee */
  while (L->ci != &L->base_ci) {	// 状态机调用栈当前结点不是调用栈底
    if (!isLua(L->ci))				// C 函数
      finishCcall(L, LUA_YIELD);	// 执行中断于一次C函数调用 finishCcall完成剩下C函数的工作
    else {							// Lua 函数
      luaV_finishOp(L);		// finish interrupted instruction */
      luaV_execute(L);		// execute down to higher C 'boundary' */
    }
  }
}


/*
** Try to find a suspended protected call (a "recover point") for the
** given thread.
*/
static CallInfo *findpcall (lua_State *L) {
  CallInfo *ci;
  for (ci = L->ci; ci != NULL; ci = ci->previous) {  /* search for a pcall */
    if (ci->callstatus & CIST_YPCALL)
      return ci;
  }
  return NULL;  /* no pending pcall */
}


/*
** Recovers from an error in a coroutine. Finds a recover point (if
** there is one) and completes the execution of the interrupted
** 'luaD_pcall'. If there is no recover point, returns zero.
*/
static int recover (lua_State *L, int status) {
  StkId oldtop;
  CallInfo *ci = findpcall(L);
  if (ci == NULL) return 0;  /* no recovery point */
  /* "finish" luaD_pcall */
  oldtop = restorestack(L, ci->extra);
  luaF_close(L, oldtop);
  seterrorobj(L, status, oldtop);
  L->ci = ci;
  L->allowhook = getoah(ci->callstatus);  /* restore original 'allowhook' */
  L->nny = 0;  /* should be zero to be yieldable */
  luaD_shrinkstack(L);
  L->errfunc = ci->u.c.old_errfunc;
  return 1;  /* continue running the coroutine */
}


/*
** signal an error in the call to 'resume', not in the execution of the
** coroutine itself. (Such errors should not be handled by any coroutine
** error handler and should not kill the coroutine.)
*/
static l_noret resume_error (lua_State *L, const char *msg, StkId firstArg) {
  L->top = firstArg;  /* remove args from the stack */
  setsvalue2s(L, L->top, luaS_new(L, msg));  /* push error message */
  api_incr_top(L);
  luaD_throw(L, -1);  /* jump back to 'lua_resume' */
}


/*
** Do the work for 'lua_resume' in protected mode. Most of the work
** depends on the status of the coroutine: initial state, suspended
** inside a hook, or regularly suspended (optionally with a continuation
** function), plus erroneous cases: non-suspended coroutine or dead
** coroutine.
*/
static void resume (lua_State *L, void *ud) {
  int nCcalls = L->nCcalls;		//保存嵌套的C调用的数量
  int n = *(cast(int*, ud));		//参数的个数
  StkId firstArg = L->top - n;		//第一个参数在栈中的位置
  CallInfo *ci = L->ci;				//保存调用栈的指针
  if (nCcalls >= LUAI_MAXCCALLS)	//嵌套深度超过限制 错误
    resume_error(L, "C stack overflow", firstArg);
  if (L->status == LUA_OK) {		//状态机状态OK 开始一个线程
    if (ci != &L->base_ci)			//调用栈不是调用栈地 错误
      resume_error(L, "cannot resume non-suspended coroutine", firstArg);
    /* coroutine is in base level; start running it */
    if (!luaD_precall(L, firstArg - 1, LUA_MULTRET))		//如果是一个Lua函数会返回0 调用这个函数
      luaV_execute(L);				//运行
  }
  else if (L->status != LUA_YIELD)	//L状态不是 OK 也不是 YIELD 出错
    resume_error(L, "cannot resume dead coroutine", firstArg);
  else {							//延续一个挂起的线程
    L->status = LUA_OK;			//改变线程状态为运行
    ci->func = restorestack(L, ci->extra);		//得到调用栈函数在栈上的位置
    if (isLua(ci))								//是Lua函数 运行它
      luaV_execute(L);
    else {  /* 'common' yield */
      if (ci->u.c.k != NULL) {		//传入的C函数不为空
        lua_unlock(L);
        n = (*ci->u.c.k)(L, LUA_YIELD, ci->u.c.ctx);	//调用它 恢复上下文信息
        lua_lock(L);
        api_checknelems(L, n);
        firstArg = L->top - n;		//第一个参数的位置
      }
      luaD_poscall(L, ci, firstArg, n);		//调用C函数
    }
    unroll(L, NULL);  /* run continuation */
  }
  lua_assert(nCcalls == L->nCcalls);
}

/*
在给定线程中启动或延续一条线程 。
要启动一个线程的话， 你需要把主函数以及它需要的参数压入线程栈； 然后调用 lua_resume ， 
把 nargs 设为参数的个数。 这次调用会在线程挂起时或是结束运行后返回。 当函数返回时，堆栈中会有传给
lua_yield 的所有值， 或是主函数的所有返回值。 当线程让出， lua_resume 返回 LUA_YIELD ， 若线程结
束运行且没有任何错误时，返回 0 。 如果有错则返回错误代码（参见 lua_pcall ）。
在发生错误的情况下， 堆栈没有展开， 因此你可以使用调试 API 来处理它。 错误消息放在栈顶在。
要延续一个线程， 你需要清除上次 lua_yield 遗留下的所有结果， 你把需要传给 yield 作结果的值压栈， 
然后调用 lua_resume 。
参数 from 表示线程从哪个线程中来延续 L 的。 如果不存在这样一个线程，这个参数可以是 NULL 。
*/
LUA_API int lua_resume (lua_State *L, lua_State *from, int nargs) {
  int status;
  unsigned short oldnny = L->nny;  //保存线程不能挂起的计数
  lua_lock(L);
  luai_userstateresume(L, nargs);
  L->nCcalls = (from) ? from->nCcalls + 1 : 1;	//当前的C调用数量等于from的C调用数量+1 否则置为1
  L->nny = 0;						//将线程的不能挂起计数置为0
  api_checknelems(L, (L->status == LUA_OK) ? nargs + 1 : nargs);
  status = luaD_rawrunprotected(L, resume, &nargs);
  if (status == -1)  /* error calling 'lua_resume'? */
    status = LUA_ERRRUN;
  else {  /* continue running after recoverable errors */
    while (errorstatus(status) && recover(L, status)) {
      /* unroll continuation */
      status = luaD_rawrunprotected(L, unroll, &status);
    }
    if (errorstatus(status)) {  /* unrecoverable error? */
      L->status = cast_byte(status);  /* mark thread as 'dead' */
      seterrorobj(L, status, L->top);  /* push error message */
      L->ci->top = L->top;
    }
    else lua_assert(status == L->status);  /* normal end or yield */
  }
  L->nny = oldnny;  /* restore 'nny' */
  L->nCcalls--;
  lua_assert(L->nCcalls == ((from) ? from->nCcalls : 0));
  lua_unlock(L);
  return status;
}

/*如果给定的线程可以让出，返回 1 ，否则返回 0*/
LUA_API int lua_isyieldable (lua_State *L) {
  return (L->nny == 0);
}

/*
让出线程(线程)
当 C 函数调用了 lua_yieldk， 当前运行的线程会挂起， 启动这个线程的 lua_resume 调用返回。 
参数 nresults 指栈上需返回给 lua_resume 的返回值的个数。
当线程再次被延续时， Lua 调用延续函数 k 继续运行被挂起的 C 函数。 延续函数会从前一个函数中接收到相同的栈， 
栈中的 n 个返回值被移除而压入了从lua_resume 传入的参数。 此外，延续函数还会收到传给 lua_yieldk 的参数 ctx。
通常，这个函数不会返回； 当线程一次次延续，将从延续函数继续运行。 然而，有一个例外： 
当这个函数从一个逐行运行的钩子函数中调用时，lua_yieldk 不可以提供延续函数。 （也就是类似 lua_yield 的形式），
而此时，钩子函数在调用完让出后将立刻返回。 Lua 会使线程让出，一旦线程再次被延续， 触发钩子的函数会继续正常运行。
当一个线程处于未提供延续函数的 C 调用中，调用它会抛出一个错误。 从并非用延续方式（例如：主线程）启动的线程中调
用它也会这样。
*/
LUA_API int lua_yieldk (lua_State *L, int nresults, lua_KContext ctx,
                        lua_KFunction k) {
  CallInfo *ci = L->ci;						//得到当前运行的函数栈结点
  luai_userstateyield(L, nresults);
  lua_lock(L);
  api_checknelems(L, nresults);
  if (L->nny > 0) {							//当前线程不能被挂起  出错
    if (L != G(L)->mainthread)				//不是主线程
      luaG_runerror(L, "attempt to yield across a C-call boundary");
    else
      luaG_runerror(L, "attempt to yield from outside a coroutine");
  }
  L->status = LUA_YIELD;					//设置线程状态
  ci->extra = savestack(L, ci->func);		//保存当前的func 到栈底的偏移
  if (isLua(ci)) {							//当前允许函数是Lua函数 inside a hook?
    api_check(L, k == NULL, "hooks cannot continue after yielding");
  }
  else {									//当前是一个C函数
    if ((ci->u.c.k = k) != NULL)			//赋值 如果k不为NULL is there a continuation?
      ci->u.c.ctx = ctx;					//保存上下文
    ci->func = L->top - nresults - 1;		//当前结点函数指向返回值下面的一个位置
    luaD_throw(L, LUA_YIELD);
  }
  lua_assert(ci->callstatus & CIST_HOOKED);  /* must be inside a hook */
  lua_unlock(L);
  return 0;  /* return to 'luaD_hook' */
}


//保护模式下调用函数
int luaD_pcall (lua_State *L, Pfunc func, void *u,
                ptrdiff_t old_top, ptrdiff_t ef) {
  int status;
  CallInfo *old_ci = L->ci;					//保存旧的调用栈
  lu_byte old_allowhooks = L->allowhook;
  unsigned short old_nny = L->nny;
  ptrdiff_t old_errfunc = L->errfunc;
  L->errfunc = ef;
  status = luaD_rawrunprotected(L, func, u);	//
  if (status != LUA_OK) {						//未正常返回 
    StkId oldtop = restorestack(L, old_top);	//重新定位栈顶  找到进入前正在调用的闭包/函数
    luaF_close(L, oldtop);						//清除闭包的upvalue
    seterrorobj(L, status, oldtop);			//设置错误信息
    L->ci = old_ci;							//将调用栈恢复到调用func函数之前的状态
    L->allowhook = old_allowhooks;
    L->nny = old_nny;
    luaD_shrinkstack(L);
  }
  L->errfunc = old_errfunc;
  return status;
}



/*
执行受保护的解析器
*/
struct SParser {  /* data to 'f_parser' */
  ZIO *z;						//指向ZIO结构体
  Mbuffer buff;					//被扫描器使用的动态结构
  Dyndata dyd;					//被解析器使用的动态结构
  const char *mode;				//代码块格式 b 二进制代码块  t文本代码块 bt可以是二进制也可以是文本
  const char *name;				//解析的代码块的名称
};

//判断代码块的格式与输入格式  不一致抛出错误
static void checkmode (lua_State *L, const char *mode, const char *x) {
  if (mode && strchr(mode, x[0]) == NULL) {
    luaO_pushfstring(L,
       "attempt to load a %s chunk (mode is '%s')", x, mode);
    luaD_throw(L, LUA_ERRSYNTAX);
  }
}

//解析代码块
static void f_parser (lua_State *L, void *ud) {
  LClosure *cl;
  struct SParser *p = cast(struct SParser *, ud);
  int c = zgetc(p->z);				//读取第一个字符
  if (c == LUA_SIGNATURE[0]) {		//如果是二进制的预编译数据
    checkmode(L, p->mode, "binary");		//检查格式
    cl = luaU_undump(L, p->z, p->name);
  }
  else {							//否则是文本数据
    checkmode(L, p->mode, "text");
    cl = luaY_parser(L, p->z, &p->buff, &p->dyd, p->name, c);			//解析代码
  }
  lua_assert(cl->nupvalues == cl->p->sizeupvalues);
  luaF_initupvals(L, cl);
}


//保护模式下解析代码块
int luaD_protectedparser (lua_State *L, ZIO *z, const char *name,
                                        const char *mode) {
  struct SParser p;
  int status;
  L->nny++;				//解析代码的时候不能挂起
  p.z = z; 
  p.name = name;		//代码块名称
  p.mode = mode;		//代码块格式b t bt
  p.dyd.actvar.arr = NULL; 
  p.dyd.actvar.size = 0;
  p.dyd.gt.arr = NULL; 
  p.dyd.gt.size = 0;
  p.dyd.label.arr = NULL; 
  p.dyd.label.size = 0;
  luaZ_initbuffer(L, &p.buff);
  status = luaD_pcall(L, f_parser, &p, savestack(L, L->top), L->errfunc);	//在保护模式下调用f_parser
  luaZ_freebuffer(L, &p.buff);
  luaM_freearray(L, p.dyd.actvar.arr, p.dyd.actvar.size);
  luaM_freearray(L, p.dyd.gt.arr, p.dyd.gt.size);
  luaM_freearray(L, p.dyd.label.arr, p.dyd.label.size);
  L->nny--;				//不能挂起的次数-1
  return status;
}


