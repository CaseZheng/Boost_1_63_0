/*
** $Id: lapi.h,v 2.9 2015/03/06 19:49:50 roberto Exp $
** Auxiliary functions from Lua API
** See Copyright Notice in lua.h
*/

#ifndef lapi_h
#define lapi_h


#include "llimits.h"
#include "lstate.h"

//栈顶加1 并检查栈顶溢出
#define api_incr_top(L)   {L->top++; api_check(L, L->top <= L->ci->top, \
	"stack overflow"); }

//根据预期返回的结果数和当前栈顶位置 设置栈顶
#define adjustresults(L,nres) \
    { if ((nres) == LUA_MULTRET && L->ci->top < L->top) L->ci->top = L->top; }

//检查栈上的空间剩余大于n个
#define api_checknelems(L,n)	api_check(L, (n) < (L->top - L->ci->func), \
				  "not enough elements in the stack")


#endif
