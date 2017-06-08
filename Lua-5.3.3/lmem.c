/*
** $Id: lmem.c,v 1.91 2015/03/06 19:45:54 roberto Exp $
** Interface to Memory Manager
** See Copyright Notice in lua.h
*/

#define lmem_c
#define LUA_CORE

#include "lprefix.h"


#include <stddef.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"



/*
** About the realloc function:
** void * frealloc (void *ud, void *ptr, size_t osize, size_t nsize);
** ('osize' is the old size, 'nsize' is the new size)
**
** * frealloc(ud, NULL, x, s) creates a new block of size 's' (no
** matter 'x').
**
** * frealloc(ud, p, x, 0) frees the block 'p'
** (in this specific case, frealloc must return NULL);
** particularly, frealloc(ud, NULL, 0, 0) does nothing
** (which is equivalent to free(NULL) in ISO C)
**
** frealloc returns NULL if it cannot create or reallocate the area
** (any reallocation to an equal or smaller size cannot fail!)
*/



#define MINSIZEARRAY	4

//用来管理可变长数组,当数组空间不够时,扩大为原先的两倍
void *luaM_growaux_ (lua_State *L, void *block, int *size, size_t size_elems,
                     int limit, const char *what) {
  void *newblock;
  int newsize;
  if (*size >= limit/2) {		//原内存大于了 限制的一半
    if (*size >= limit)		//原内存已经大于了限制
      luaG_runerror(L, "too many %s (limit is %d)", what, limit);	//抛出异常
    newsize = limit;			//否则使用限制为扩大后内存的大小
  }
  else {
    newsize = (*size)*2;					//将新的大小设为原先的两倍
    if (newsize < MINSIZEARRAY)			//最小也要4个
      newsize = MINSIZEARRAY;
  }
  newblock = luaM_reallocv(L, block, *size, newsize, size_elems);	//分配新的内存
  *size = newsize;							//设置新的内存大小
  return newblock;
}


l_noret luaM_toobig (lua_State *L) {
  luaG_runerror(L, "memory allocation error: block too big");
}



/*
通用的内存分配程序
ud　 ：Lua默认内存管理器并未使用该参数。不过在用户自定义内存管理器中，可以让内存管理在不同的堆上进行。
ptr　：非NULL表示指向一个已分配的内存块指针，NULL表示将分配一块nsize大小的新内存块。
osize：原始内存块大小，默认内存管理器并未使用该参数。Lua的设计强制在调用内存管理器函数时候需要给出原始
内存块的大小信息，如果用户需要自定义一个高效的内存管理器，那么这个参数信息将十分重要。这是因为大多数的
内存管理算法都需要为所管理的内存块加上一个cookie，里面存储了内存块尺寸的信息，以便在释放内存的时候能够
获取到尺寸信息(譬如多级内存池回收内存操作)。而Lua内存管理器刻意在调用内存管理器时提供了这个信息，这样
就不必额外存储这些cookie信息，这样在大量使用小内存块的环境中将可以节省不少的内存。另外在ptr传入NULL时，
osize表示Lua对象类型（LUA_TNIL、LUA_TBOOLEAN、LUA_TTHREAD等等），这样内存管理器就可以知道当前在分配
的对象的类型，从而可以针对它做一些统计或优化的工作。
nsize：新的内存块大小，特别地，在nsize为0时需要提供内存释放的功能。
*/
void *luaM_realloc_ (lua_State *L, void *block, size_t osize, size_t nsize) {
  void *newblock;
  global_State *g = G(L);
  size_t realosize = (block) ? osize : 0;
  lua_assert((realosize == 0) == (block == NULL));
#if defined(HARDMEMTESTS)
  if (nsize > realosize && g->gcrunning)
    luaC_fullgc(L, 1);  /* force a GC whenever possible */
#endif
  newblock = (*g->frealloc)(g->ud, block, osize, nsize);	//调用创建状态机时传入的内存管理函数
  if (newblock == NULL && nsize > 0) {						//申请内存 但 失败了
    lua_assert(nsize > realosize);  /* cannot fail when shrinking a block */
    if (g->version) {  /* is state fully built? */
      luaC_fullgc(L, 1);									//尝试释放一些内存 gc
      newblock = (*g->frealloc)(g->ud, block, osize, nsize);	//再次申请内存
    }
    if (newblock == NULL)									//分配内存失败 抛出异常
      luaD_throw(L, LUA_ERRMEM);
  }
  lua_assert((nsize == 0) == (newblock == NULL));
  g->GCdebt = (g->GCdebt + nsize) - realosize;			//
  return newblock;
}

