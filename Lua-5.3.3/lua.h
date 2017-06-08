/*
** $Id: lua.h,v 1.331 2016/05/30 15:53:28 roberto Exp $
** Lua - A Scripting Language
** Lua.org, PUC-Rio, Brazil (http://www.lua.org)
** See Copyright Notice at the end of this file
*/


#ifndef lua_h
#define lua_h

#include <stdarg.h>
#include <stddef.h>


#include "luaconf.h"


#define LUA_VERSION_MAJOR	"5"
#define LUA_VERSION_MINOR	"3"
//Lua的版本号
#define LUA_VERSION_NUM		503
#define LUA_VERSION_RELEASE	"3"

#define LUA_VERSION	"Lua " LUA_VERSION_MAJOR "." LUA_VERSION_MINOR
#define LUA_RELEASE	LUA_VERSION "." LUA_VERSION_RELEASE
#define LUA_COPYRIGHT	LUA_RELEASE "  Copyright (C) 1994-2016 Lua.org, PUC-Rio"
#define LUA_AUTHORS	"R. Ierusalimschy, L. H. de Figueiredo, W. Celes"


/* mark for precompiled code ('<esc>Lua') */
#define LUA_SIGNATURE	"\x1bLua"

/* option for multiple returns in 'lua_pcall' and 'lua_call' */
#define LUA_MULTRET	(-1)


/*
** Pseudo-indices
** (-LUAI_MAXSTACK is the minimum valid index; we keep some free empty
** space after that to help overflow detection)
*/
//LUA_REGISTRYINDEX	用来定位注册表的伪索引
#define LUA_REGISTRYINDEX	(-LUAI_MAXSTACK - 1000)
#define lua_upvalueindex(i)	(LUA_REGISTRYINDEX - (i))


/* thread status */
#define LUA_OK		0
#define LUA_YIELD	1			//  挂起
#define LUA_ERRRUN	2
#define LUA_ERRSYNTAX	3		//	句法错误
#define LUA_ERRMEM	4			//	内存错误
#define LUA_ERRGCMM	5			//  GC错误	__gc
#define LUA_ERRERR	6


typedef struct lua_State lua_State;


/*
** basic types		Lua基本类型
*/
#define LUA_TNONE		(-1)		//none

#define LUA_TNIL		0			//nil
#define LUA_TBOOLEAN		1		//boolean
#define LUA_TLIGHTUSERDATA	2		//lightuserdatea
#define LUA_TNUMBER		3			//number
#define LUA_TSTRING		4			//string
#define LUA_TTABLE		5			//table
#define LUA_TFUNCTION		6		//functon
#define LUA_TUSERDATA		7		//userdata
#define LUA_TTHREAD		8			//线程

#define LUA_NUMTAGS		9			//类型个数



//最小Lua堆栈可用于C函数
#define LUA_MINSTACK	20


//注册表预定义值
//注册表中这个索引下是状态机的主线程。 (主线程和状态机同时被创建出来)
#define LUA_RIDX_MAINTHREAD	1
//注册表的这个索引下是全局环境
#define LUA_RIDX_GLOBALS	2
#define LUA_RIDX_LAST		LUA_RIDX_GLOBALS


//Lua Number类型
typedef LUA_NUMBER lua_Number;


/* type for integer functions */
typedef LUA_INTEGER lua_Integer;

/* unsigned integer type */
typedef LUA_UNSIGNED lua_Unsigned;

/* type for continuation-function contexts 
延续函数上下文参数的类型。 这一定是一个数字类型。 当有 intptr_t 时，被定义为 intptr_t ， 
因此它也可以保存指针。 否则，它被定义为 ptrdiff_t。*/
typedef LUA_KCONTEXT lua_KContext;

/*C 函数的类型。
为了正确的和 Lua 通讯， C 函数必须使用下列协议。 这个协议定义了参数以及返回值传递方法： 
C 函数通过 Lua 中的栈来接受参数， 参数以正序入栈（第一个参数首先入栈）。 因此，当函数开始的时候， 
lua_gettop(L) 可以返回函数收到的参数个数。 第一个参数（如果有的话）在索引 1 的地方， 
而最后一个参数在索引 lua_gettop(L) 处。 当需要向 Lua 返回值的时候，
C 函数只需要把它们以正序压到堆栈上（第一个返回值最先压入）， 然后返回这些返回值的个数。 
在这些返回值之下的，堆栈上的东西都会被 Lua 丢掉。 
和 Lua 函数一样，从 Lua 中调用 C 函数也可以有很多返回值。*/
typedef int (*lua_CFunction) (lua_State *L);

/*
** Type for continuation functions
延续函数的类型
*/
typedef int (*lua_KFunction) (lua_State *L, int status, lua_KContext ctx);


/*
** Type for functions that read/write blocks when loading/dumping Lua chunks
lua_load 用到的读取器函数， 每次它需要一块新的代码块的时候， lua_load 就调用读取器，
每次都会传入一个参数 data 。 读取器需要返回含有新的代码块的一块内存的指针， 并把 size 设
为这块内存的大小。 内存块必须在下一次函数被调用之前一直存在。 读取器可以通过返回 NULL 或设 
size 为 0 来指示代码块结束。 读取器可能返回多个块，每个块可以有任意的大于零的尺寸。
*/
typedef const char * (*lua_Reader) (lua_State *L, void *ud, size_t *sz);

typedef int (*lua_Writer) (lua_State *L, const void *p, size_t sz, void *ud);


/*
** Type for memory-allocation functions
Lua 状态机中使用的内存分配器函数的类型。 内存分配函数必须提供一个功能类似于 realloc 但又不完全相同的函数。
它的参数有 ud ，一个由 lua_newstate 传给它的指针；ptr ，一个指向已分配出来/将被重新分配/要释放的内存块指针； 
osize ，内存块原来的尺寸或是关于什么将被分配出来的代码； nsize ，新内存块的尺寸。
如果 ptr 不是 NULL， osize 是 ptr 指向的内存块的尺寸， 即这个内存块当初被分配或重分配的尺寸。
如果 ptr 是 NULL， osize 是 Lua 即将分配对象类型的编码。 当（且仅当）Lua 创建一个对应类型的新对象时，
osize 是LUA_TSTRING，LUA_TTABLE，LUA_TFUNCTION， LUA_TUSERDATA，或 LUA_TTHREAD 中的一个。
若 osize 是其它类型，Lua 将为其它东西分配内存。
Lua 假定分配器函数会遵循以下行为：
当 nsize 是零时， 分配器必须和 free 行为类似并返回 NULL
当 nsize 不是零时， 分配器必须和 realloc 行为类似。 如果分配器无法完成请求，返回 NULL。 Lua 假定在 
osize >= nsize 成立的条件下， 分配器绝不会失败
*/
typedef void * (*lua_Alloc) (void *ud, void *ptr, size_t osize, size_t nsize);



/*
** generic extra include file
*/
#if defined(LUA_USER_H)
#include LUA_USER_H
#endif


/*
** RCS ident string
*/
extern const char lua_ident[];


/*
** state manipulation
*/
LUA_API lua_State *(lua_newstate) (lua_Alloc f, void *ud);
LUA_API void       (lua_close) (lua_State *L);
LUA_API lua_State *(lua_newthread) (lua_State *L);

LUA_API lua_CFunction (lua_atpanic) (lua_State *L, lua_CFunction panicf);


LUA_API const lua_Number *(lua_version) (lua_State *L);


/*
** basic stack manipulation
*/
LUA_API int   (lua_absindex) (lua_State *L, int idx);
LUA_API int   (lua_gettop) (lua_State *L);
LUA_API void  (lua_settop) (lua_State *L, int idx);
LUA_API void  (lua_pushvalue) (lua_State *L, int idx);
LUA_API void  (lua_rotate) (lua_State *L, int idx, int n);
LUA_API void  (lua_copy) (lua_State *L, int fromidx, int toidx);
LUA_API int   (lua_checkstack) (lua_State *L, int n);

LUA_API void  (lua_xmove) (lua_State *from, lua_State *to, int n);


/*
** access functions (stack -> C)
*/

LUA_API int             (lua_isnumber) (lua_State *L, int idx);
LUA_API int             (lua_isstring) (lua_State *L, int idx);
LUA_API int             (lua_iscfunction) (lua_State *L, int idx);
LUA_API int             (lua_isinteger) (lua_State *L, int idx);
LUA_API int             (lua_isuserdata) (lua_State *L, int idx);
LUA_API int             (lua_type) (lua_State *L, int idx);
LUA_API const char     *(lua_typename) (lua_State *L, int tp);

LUA_API lua_Number      (lua_tonumberx) (lua_State *L, int idx, int *isnum);
LUA_API lua_Integer     (lua_tointegerx) (lua_State *L, int idx, int *isnum);
LUA_API int             (lua_toboolean) (lua_State *L, int idx);
LUA_API const char     *(lua_tolstring) (lua_State *L, int idx, size_t *len);
LUA_API size_t          (lua_rawlen) (lua_State *L, int idx);
LUA_API lua_CFunction   (lua_tocfunction) (lua_State *L, int idx);
LUA_API void	       *(lua_touserdata) (lua_State *L, int idx);
LUA_API lua_State      *(lua_tothread) (lua_State *L, int idx);
LUA_API const void     *(lua_topointer) (lua_State *L, int idx);


/*
比较和算数函数
*/

#define LUA_OPADD	0	// + ORDER TM, ORDER OP
#define LUA_OPSUB	1   // -
#define LUA_OPMUL	2   // *
#define LUA_OPMOD	3   // 
#define LUA_OPPOW	4
#define LUA_OPDIV	5
#define LUA_OPIDIV	6
#define LUA_OPBAND	7	// &
#define LUA_OPBOR	8	// |
#define LUA_OPBXOR	9	// ^
#define LUA_OPSHL	10
#define LUA_OPSHR	11
#define LUA_OPUNM	12	// -
#define LUA_OPBNOT	13	//

LUA_API void  (lua_arith) (lua_State *L, int op);

#define LUA_OPEQ	0
#define LUA_OPLT	1
#define LUA_OPLE	2

LUA_API int   (lua_rawequal) (lua_State *L, int idx1, int idx2);
LUA_API int   (lua_compare) (lua_State *L, int idx1, int idx2, int op);


/*
** push functions (C -> stack)
*/
LUA_API void        (lua_pushnil) (lua_State *L);
LUA_API void        (lua_pushnumber) (lua_State *L, lua_Number n);
LUA_API void        (lua_pushinteger) (lua_State *L, lua_Integer n);
LUA_API const char *(lua_pushlstring) (lua_State *L, const char *s, size_t len);
LUA_API const char *(lua_pushstring) (lua_State *L, const char *s);
LUA_API const char *(lua_pushvfstring) (lua_State *L, const char *fmt,
                                                      va_list argp);
LUA_API const char *(lua_pushfstring) (lua_State *L, const char *fmt, ...);
LUA_API void  (lua_pushcclosure) (lua_State *L, lua_CFunction fn, int n);
LUA_API void  (lua_pushboolean) (lua_State *L, int b);
LUA_API void  (lua_pushlightuserdata) (lua_State *L, void *p);
LUA_API int   (lua_pushthread) (lua_State *L);


/*
** get functions (Lua -> stack)
*/
LUA_API int (lua_getglobal) (lua_State *L, const char *name);
LUA_API int (lua_gettable) (lua_State *L, int idx);
LUA_API int (lua_getfield) (lua_State *L, int idx, const char *k);
LUA_API int (lua_geti) (lua_State *L, int idx, lua_Integer n);
LUA_API int (lua_rawget) (lua_State *L, int idx);
LUA_API int (lua_rawgeti) (lua_State *L, int idx, lua_Integer n);
LUA_API int (lua_rawgetp) (lua_State *L, int idx, const void *p);

LUA_API void  (lua_createtable) (lua_State *L, int narr, int nrec);
LUA_API void *(lua_newuserdata) (lua_State *L, size_t sz);
LUA_API int   (lua_getmetatable) (lua_State *L, int objindex);
LUA_API int  (lua_getuservalue) (lua_State *L, int idx);


/*
** set functions (stack -> Lua)
*/
LUA_API void  (lua_setglobal) (lua_State *L, const char *name);
LUA_API void  (lua_settable) (lua_State *L, int idx);
LUA_API void  (lua_setfield) (lua_State *L, int idx, const char *k);
LUA_API void  (lua_seti) (lua_State *L, int idx, lua_Integer n);
LUA_API void  (lua_rawset) (lua_State *L, int idx);
LUA_API void  (lua_rawseti) (lua_State *L, int idx, lua_Integer n);
LUA_API void  (lua_rawsetp) (lua_State *L, int idx, const void *p);
LUA_API int   (lua_setmetatable) (lua_State *L, int objindex);
LUA_API void  (lua_setuservalue) (lua_State *L, int idx);

//这个函数的行为和 lua_call 完全一致，只不过它还允许被调用的函数让出 
LUA_API void  (lua_callk) (lua_State *L, int nargs, int nresults,
                           lua_KContext ctx, lua_KFunction k);
/*调用一个函数。
要调用一个函数请遵循以下协议： 首先，要调用的函数应该被压入栈； 
接着，把需要传递给这个函数的参数按正序压栈； 这是指第一个参数首先压栈。 
最后调用一下 lua_call； nargs 是你压入栈的参数个数。 
当函数调用完毕后，所有的参数以及函数本身都会出栈。 而函数的返回值这时则被压栈。 
返回值的个数将被调整为 nresults 个， 除非 nresults 被设置成 LUA_MULTRET。
在这种情况下，所有的返回值都被压入堆栈中。 Lua 会保证返回值都放入栈空间中。
函数返回值将按正序压栈（第一个返回值首先压栈）， 因此在调用结束后，最后一个返回值将被放在栈顶。
被调用函数内发生的错误将（通过 longjmp ）一直上抛。*/
#define lua_call(L,n,r)		lua_callk(L, (n), (r), 0, NULL)

//这个函数的行为和 lua_pcall 完全一致，只不过它还允许被调用的函数让出
LUA_API int   (lua_pcallk) (lua_State *L, int nargs, int nresults, int errfunc,
                            lua_KContext ctx, lua_KFunction k);
/*
lua_pcall
以保护模式调用一个函数。	n压入参数个数 r返回参数个数 f错误处理函数
nargs 和 nresults 的含义与 lua_call 中的相同。 
如果在调用过程中没有发生错误， lua_pcall 的行为和 lua_call 完全一致。 
但是，如果有错误发生的话， lua_pcall 会捕获它， 然后把唯一的值（错误消息）压栈，然后返回错误码。 
同 lua_call 一样， lua_pcall 总是把函数本身和它的参数从栈上移除。
如果 f 是 0 ， 返回在栈顶的错误消息就和原始错误消息完全一致。 
否则， f 就被当成是 错误处理函数 在栈上的索引位置。 （在当前的实现里，这个索引不能是伪索引。） 
在发生运行时错误时， 这个函数会被调用而参数就是错误消息。 错误处理函数的返回值将被 lua_pcall 作为错误消息返回在堆栈上。
典型的用法中，错误处理函数被用来给错误消息加上更多的调试信息， 比如栈跟踪信息。 这些信息在 lua_pcall 返回后， 由于栈已经展开，所以收集不到了。
lua_pcall 函数会返回下列常数 （定义在 lua.h 内）中的一个：
 LUA_OK (0): 成功。
 LUA_ERRRUN: 运行时错误。
 LUA_ERRMEM: 内存分配错误。对于这种错，Lua 不会调用错误处理函数。
 LUA_ERRERR: 在运行错误处理函数时发生的错误。
   LUA_ERRGCMM: 在运行 __gc 元方法时发生的错误。 （这个错误和被调用的函数无关。）
*/
#define lua_pcall(L,n,r,f)	lua_pcallk(L, (n), (r), (f), 0, NULL)

LUA_API int   (lua_load) (lua_State *L, lua_Reader reader, void *dt,
                          const char *chunkname, const char *mode);

LUA_API int (lua_dump) (lua_State *L, lua_Writer writer, void *data, int strip);


/*
** coroutine functions
*/
LUA_API int  (lua_yieldk)     (lua_State *L, int nresults, lua_KContext ctx,
                               lua_KFunction k);
LUA_API int  (lua_resume)     (lua_State *L, lua_State *from, int narg);
LUA_API int  (lua_status)     (lua_State *L);
LUA_API int (lua_isyieldable) (lua_State *L);

//让出线程	不同的是不提供延续函数。 因此，当线程被延续，线程会继续运行调用 lua_yield 函数的函数
#define lua_yield(L,n)		lua_yieldk(L, (n), 0, NULL)


/*
garbage-collection function and options
LUA_GCSTOP: 停止垃圾收集器。
*/
#define LUA_GCSTOP		0
//LUA_GCRESTART: 重启垃圾收集器。
#define LUA_GCRESTART		1
//LUA_GCCOLLECT: 发起一次完整的垃圾收集循环。
#define LUA_GCCOLLECT		2
//LUA_GCCOUNT: 返回 Lua 使用的内存总量（以 K 字节为单位）。
#define LUA_GCCOUNT		3
//LUA_GCCOUNTB: 返回当前内存使用量除以 1024 的余数。
#define LUA_GCCOUNTB		4
//LUA_GCSTEP: 发起一步增量垃圾收集。
#define LUA_GCSTEP		5
//LUA_GCSETPAUSE: 把 data 设为 垃圾收集器间歇率，并返回之前设置的值。
#define LUA_GCSETPAUSE		6
//LUA_GCSETSTEPMUL: 把 data 设为 垃圾收集器步进倍率，并返回之前设置的值。
#define LUA_GCSETSTEPMUL	7
//LUA_GCISRUNNING: 返回收集器是否在运行（即没有停止）。
#define LUA_GCISRUNNING		9

LUA_API int (lua_gc) (lua_State *L, int what, int data);


/*
** miscellaneous functions
*/

LUA_API int   (lua_error) (lua_State *L);

LUA_API int   (lua_next) (lua_State *L, int idx);

LUA_API void  (lua_concat) (lua_State *L, int n);
LUA_API void  (lua_len)    (lua_State *L, int idx);

LUA_API size_t   (lua_stringtonumber) (lua_State *L, const char *s);

LUA_API lua_Alloc (lua_getallocf) (lua_State *L, void **ud);
LUA_API void      (lua_setallocf) (lua_State *L, lua_Alloc f, void *ud);



/*
** {==============================================================
** some useful macros
** ===============================================================
*/

#define lua_getextraspace(L)	((void *)((char *)(L) - LUA_EXTRASPACE))

#define lua_tonumber(L,i)	lua_tonumberx(L,(i),NULL)
#define lua_tointeger(L,i)	lua_tointegerx(L,(i),NULL)

#define lua_pop(L,n)		lua_settop(L, -(n)-1)

#define lua_newtable(L)		lua_createtable(L, 0, 0)

#define lua_register(L,n,f) (lua_pushcfunction(L, (f)), lua_setglobal(L, (n)))

#define lua_pushcfunction(L,f)	lua_pushcclosure(L, (f), 0)

#define lua_isfunction(L,n)	(lua_type(L, (n)) == LUA_TFUNCTION)
#define lua_istable(L,n)	(lua_type(L, (n)) == LUA_TTABLE)
#define lua_islightuserdata(L,n)	(lua_type(L, (n)) == LUA_TLIGHTUSERDATA)
#define lua_isnil(L,n)		(lua_type(L, (n)) == LUA_TNIL)
#define lua_isboolean(L,n)	(lua_type(L, (n)) == LUA_TBOOLEAN)
#define lua_isthread(L,n)	(lua_type(L, (n)) == LUA_TTHREAD)
#define lua_isnone(L,n)		(lua_type(L, (n)) == LUA_TNONE)
#define lua_isnoneornil(L, n)	(lua_type(L, (n)) <= 0)

#define lua_pushliteral(L, s)	lua_pushstring(L, "" s)

/*将全局环境压栈。*/
#define lua_pushglobaltable(L)  \
	((void)lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS))

#define lua_tostring(L,i)	lua_tolstring(L, (i), NULL)

/*把栈顶元素移动到指定的有效索引处， 依次移动这个索引之上的元素。 
不要用伪索引来调用这个函数， 因为伪索引没有真正指向栈上的位置。*/
#define lua_insert(L,idx)	lua_rotate(L, (idx), 1)

#define lua_remove(L,idx)	(lua_rotate(L, (idx), -1), lua_pop(L, 1))

#define lua_replace(L,idx)	(lua_copy(L, -1, (idx)), lua_pop(L, 1))

/* }============================================================== */


/*
** {==============================================================
** compatibility macros for unsigned conversions
** ===============================================================
*/
#if defined(LUA_COMPAT_APIINTCASTS)

#define lua_pushunsigned(L,n)	lua_pushinteger(L, (lua_Integer)(n))
#define lua_tounsignedx(L,i,is)	((lua_Unsigned)lua_tointegerx(L,i,is))
#define lua_tounsigned(L,i)	lua_tounsignedx(L,(i),NULL)

#endif
/* }============================================================== */

/*
** {======================================================================
** Debug API
** =======================================================================
*/


/*
** Event codes
*/
#define LUA_HOOKCALL	0
#define LUA_HOOKRET	1
#define LUA_HOOKLINE	2
#define LUA_HOOKCOUNT	3
#define LUA_HOOKTAILCALL 4


/*
** Event masks
*/
#define LUA_MASKCALL	(1 << LUA_HOOKCALL)
#define LUA_MASKRET	(1 << LUA_HOOKRET)
#define LUA_MASKLINE	(1 << LUA_HOOKLINE)
#define LUA_MASKCOUNT	(1 << LUA_HOOKCOUNT)

typedef struct lua_Debug lua_Debug;  /* activation record */


/* Functions to be called by the debugger in specific events */
typedef void (*lua_Hook) (lua_State *L, lua_Debug *ar);


LUA_API int (lua_getstack) (lua_State *L, int level, lua_Debug *ar);
LUA_API int (lua_getinfo) (lua_State *L, const char *what, lua_Debug *ar);
LUA_API const char *(lua_getlocal) (lua_State *L, const lua_Debug *ar, int n);
LUA_API const char *(lua_setlocal) (lua_State *L, const lua_Debug *ar, int n);
LUA_API const char *(lua_getupvalue) (lua_State *L, int funcindex, int n);
LUA_API const char *(lua_setupvalue) (lua_State *L, int funcindex, int n);

LUA_API void *(lua_upvalueid) (lua_State *L, int fidx, int n);
LUA_API void  (lua_upvaluejoin) (lua_State *L, int fidx1, int n1,
                                               int fidx2, int n2);

LUA_API void (lua_sethook) (lua_State *L, lua_Hook func, int mask, int count);
LUA_API lua_Hook (lua_gethook) (lua_State *L);
LUA_API int (lua_gethookmask) (lua_State *L);
LUA_API int (lua_gethookcount) (lua_State *L);

/*携带有有关函数或活动记录的各种信息的结构*/
struct lua_Debug {
  int event;
  const char *name;			/* (n) 给定函数的一个合理的名字。 因为 Lua 中的函数是一等公民， 所以它们没有固定的名字： 
							一些函数可能是全局复合变量的值， 另一些可能仅仅只是被保存在一张表的某个域中。 lua_getinfo 
							函数会检查函数是怎样被调用的， 以此来找到一个适合的名字。 如果它找不到名字， name 就被设置为 NULL 。*/
  const char *namewhat;		/* (n) 用于解释 name 域。 namewhat 的值可以是 "global", "local", "method", "field", "upvalue",
							或是 "" （空串）。 这取决于函数怎样被调用。 （Lua 用空串表示其它选项都不符合。）
							'global', 'local', 'field', 'method' */
  const char *what;			/* (S) 如果函数是一个 Lua 函数，则为一个字符串 "Lua" ； 
							如果是一个 C 函数，则为 "C"； 如果它是一个代码块的主体部分，
							则为 "main"。		'Lua', 'C', 'main', 'tail' */
  const char *source;		/* (S) 创建这个函数的代码块的名字。 如果 source 以 '@' 打头， 指这个函数定义在一
							个文件中，而 '@' 之后的部分就是文件名。 若 source 以 '=' 打头， 剩余的部分由用户
							行为来决定如何表示源码。 其它的情况下，这个函数定义在一个字符串中， 而 source 正是那个字符串*/
  int currentline;			/* (l) 给定函数正在执行的那一行。 当提供不了行号信息的时候， currentline 被设为 -1*/
  int linedefined;				/* (S) 函数定义开始处的行号*/
  int lastlinedefined;			/* (S) 函数定义结束处的行号*/
  unsigned char nups;			// (u) upvalues的数量
  unsigned char nparams;		// (u) 函数固定形参个数 （对于 C 函数永远是 0 ）。
  char isvararg;				/* (u) 如果函数是一个可变参数函数则为真 （对于 C 函数永远为真）。*/
  char istailcall;				/* (t) 如果函数以尾调用形式调用，这个值就为真。 在这种情况下，当层的调用者不在栈中。*/
  char short_src[LUA_IDSIZE];	/* (S) 一个“可打印版本”的 source ，用于出错信息 */
  /* private part */
  struct CallInfo *i_ci;  /* active function */
};

/* }====================================================================== */


/******************************************************************************
* Copyright (C) 1994-2016 Lua.org, PUC-Rio.
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
******************************************************************************/


#endif
