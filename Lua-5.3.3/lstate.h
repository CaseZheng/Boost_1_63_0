/*
** $Id: lstate.h,v 2.130 2015/12/16 16:39:38 roberto Exp $
** Global State
** See Copyright Notice in lua.h
*/

#ifndef lstate_h
#define lstate_h

#include "lua.h"

#include "lobject.h"
#include "ltm.h"
#include "lzio.h"


/*

** Some notes about garbage-collected objects: All objects in Lua must
** be kept somehow accessible until being freed, so all objects always
** belong to one (and only one) of these lists, using field 'next' of
** the 'CommonHeader' for the link:
**
** 'allgc': all objects not marked for finalization;
** 'finobj': all objects marked for finalization;
** 'tobefnz': all objects ready to be finalized; 
** 'fixedgc': all objects that are not to be collected (currently
** only small strings, such as reserved words).

*/


struct lua_longjmp;  /* defined in ldo.c */


/*
** Atomic type (relative to signals) to better ensure that 'lua_sethook' 
** is thread safe
*/
#if !defined(l_signalT)
#include <signal.h>
#define l_signalT	sig_atomic_t
#endif


/* extra stack space to handle TM calls and some other extras */
#define EXTRA_STACK   5


#define BASIC_STACK_SIZE        (2*LUA_MINSTACK)


/* GC种类 kinds of Garbage Collection */
#define KGC_NORMAL	0
#define KGC_EMERGENCY	1	/* gc was forced by an allocation failure */

//字符串hash表
//相同的短字符串在同一个Lua State中只存在唯一一份，这被称为字符串的内部化
typedef struct stringtable {
  TString **hash;
  int nuse;		//hash表中元素的个数
  int size;		//hash表的大小
} stringtable;


//Lua把调用栈和数据栈分开保存。调用栈放在一个叫做 CallInfo 的结构中，以双向链表的形式储存在线程对象里。
//CallInfo 保存着正在调用的函数的运行状态。状态标示存放在畣畡畬畬畳畴畡畴畵畳中。部分数据和函数的类型有
//关，以联合形式存放。C 函数与 Lua 函数的结构不完全相同。callstatus 中保存了一位标志用来区分是 C 函
//数还是 Lua 函数。
//CallInfo 是一个标准的双向链表结构，不直接被GC模块管理。这个双向链表表达的是一个逻辑上的栈，
//在运行过程中，并不是每次调入更深层次的函数，就立刻构造出一个CallInfo节点。整个CallInfo链表会在
//运行中被反复复用。直到GC的时候才清理那些比当前调用层次更深的无用节点
//调用者只需要把 CallInfo 链表当成一个无限长的堆栈使用即可。当调用层次返回，之前分配
//的节点可以被后续调用行为复用。在GC的时候只需要调用luaE_freeCI就可以释放过长的链表
typedef struct CallInfo {
  StkId func;	//指向正在执行的函数在数据栈上的位置 需要记录这个信息，是因为如果当前是一个Lua函数，
				//且传入的参数个数不定的时候，需要用这个位置和当前数据栈底的位置相减，获得不定参数的准确数量
  StkId	top;	//调用栈栈顶
  struct CallInfo *previous, *next;			//双向链表 前后指针
  union {
    struct {  /* only for Lua functions */
      StkId base;					//当前函数的数据栈指针 调用时函数在栈上的起始位置
      const Instruction *savedpc;	//保存指向当前指令的指针  操作码
    } l;					//Lua函数使用
    struct {  /* only for C functions */
      lua_KFunction k;				//上下文恢复函数
      ptrdiff_t old_errfunc;		//保存旧的当前错误处理函数
      lua_KContext ctx;				//上下文信息 线程挂起的时候
    } c;					//C函数
  } u;
  ptrdiff_t extra;			//保存当前func到栈底的偏移
  short nresults;			//func执行后预期返回的结果的数目
  lu_byte callstatus;		//标志当前调用的函数状态	CIST_LUA CIST_HOOKED 等
} CallInfo;


//用于表示CallInfo的状态
#define CIST_OAH	(1<<0)	/* original value of 'allowhook' */
#define CIST_LUA	(1<<1)	// 调用运行Lua函数
#define CIST_HOOKED	(1<<2)	// 调用运行调试钩子
#define CIST_FRESH	(1<<3)	/* call is running on a fresh invocation
                                   of luaV_execute */
#define CIST_YPCALL	(1<<4)	//标记从lua_pcallk或lua_pcall进入运行 call is a yieldable protected call */
#define CIST_TAIL	(1<<5)	//尾调用
#define CIST_HOOKYIELD	(1<<6)	/* last hook called yielded */
#define CIST_LEQ	(1<<7)  /* using __lt for __le */
//用于判断 C 函数还是 Lua 函数
#define isLua(ci)	((ci)->callstatus & CIST_LUA)

/* assume that CIST_OAH has offset 0 and that 'v' is strictly 0/1 */
#define setoah(st,v)	((st) = ((st) & ~CIST_OAH) | (v))
#define getoah(st)	((st) & CIST_OAH)


/*
** 'global state', shared by all threads of this state
同一Lua虚拟机中的所有执行线程，共享了一块全局数据global_State
*/
typedef struct global_State {
  lua_Alloc frealloc;	//虚拟机内存分配管理器 在调用lua_newstate时指定参数，修改该策略，
						//或者调用luaL_newstate函数使用默认的内存分配策略
						//可以通过函数    lua_setallocf：来设置内存分配策略
  void *ud;         /* auxiliary data to 'frealloc' */
  l_mem totalbytes;		//当前分配的字节数 - GCdebt
  l_mem GCdebt;			//分配的尚未由GC补偿的字节 bytes allocated not yet compensated by the collector
  lu_mem GCmemtrav;		//GC遍历的内存 memory traversed by the GC
  lu_mem GCestimate;	//所使用的非垃圾内存的估计 an estimate of the non-garbage memory in use
  stringtable strt;		//所有的短字符串被存放在全局表的strt域中，是一个hash表 使得整个虚拟机中短字符串只有一份实例
  TValue l_registry;	//注册表 保存全局的注册表，注册表就是一个全局的table（即整个虚拟机中只有
						//一个注册表），它只能被C代码访问，通常，它用来保存那些需要在几个模块中
						//共享的数据。比如通过luaL_newmetatable创建的元表就是放在全局的注册表中。
  unsigned int seed;	//hash表的随机种子
  lu_byte currentwhite;	//Lua GC有两种白色 每次trace-mark之后切换虚拟器当前白色
  lu_byte gcstate;		//GC的状态
  lu_byte gckind;		//GC运行类型 kind of GC running
  lu_byte gcrunning;	//标记内存分派是正常的
  GCObject *allgc;		//由gc分配的对象链表
  GCObject **sweepgc;	//列表中当前扫描位置 current position of sweep in list
  GCObject *finobj;		//具有finalizers(终结器)的可收集对象的列表 list of collectable objects with finalizers
  GCObject *gray;				//灰色对象列表
  GCObject *grayagain;			//以原子方式遍历的对象列表 list of objects to be traversed atomically
  GCObject *weak;				//具有弱值的表列表 list of tables with weak values
  GCObject *ephemeron;			//暂时表列表 list of ephemeron tables (weak keys)
  GCObject *allweak;			//所有弱表的列表   list of all-weak tables
  GCObject *tobefnz;			//userdata列表 list of userdata to be GC
  GCObject *fixedgc;			//不允许被gc回收内存的对象的列表
  struct lua_State *twups;		//列表中当前扫描位置
  unsigned int gcfinnum;	//在每个GC步骤中调用的终结器的数量 number of finalizers to call in each GC step
  int gcpause;			/*垃圾收集器间歇率控制着收集器需要在开启新的
						//循环前要等待多久。 增大这个值会减少收集器的积极性。 当这个值
						//比 100 小的时候，收集器在开启新的循环前不会有等待。 设置这个
						//值为 200 就会让收集器等到总内存使用量达到 之前的两倍时才开始新的循环。*/
  int gcstepmul;		/*垃圾收集器步进倍率控制着收集器运作速度相对于内存分配速度的倍率。 增大
						//这个值不仅会让收集器更加积极，还会增加每个增量步骤的长度。 不要把这个
						//值设得小于 100 ， 那样的话收集器就工作的太慢了以至于永远都干不完一个循
						//环。 默认值是 200 ，这表示收集器以内存分配的“两倍”速工作。*/
  lua_CFunction panic;				//当没有错误处理函数时调用
  struct lua_State *mainthread;		//指向主lua_State，或者说是主线程、主执行栈。Lua虚拟机在调用
									//函数lua_newstate初始化全局状态global_State时也会创建一个主线程，
									//当然根据需要也可以调用lua_newthread来创建新的线程，但是整个虚拟
									//机，只有一个全局的状态global_State
  const lua_Number *version;		//指向版本号
  TString *memerrmsg;				//内存错误信息
  TString *tmname[TM_N];			//原表元方法的名称 字符串
  struct Table *mt[LUA_NUMTAGS];	//保存基本类型的元表，注意table和userdata都有自己的元表
  TString *strcache[STRCACHE_N][STRCACHE_M];  //字符串缓存 API
} global_State;


/*指向一条线程并间接（通过该线程）引用了整个 Lua 解释器的状态。 Lua 库是完全可重入的： 它没有任何全局变量。 状态机所有的信息都可以通过这个结构访问到。
这个结构的指针必须作为第一个参数传递给每一个库函数。 lua_newstate 是一个例外， 这个函数会从头创建一个 Lua 状态机。
lua_State是暴露给用户的数据类型。从名字上看，它想表示一个Lua程序的执行状态，在官方文档中，
它指代Lua的一个线程。每个线程拥有独立的数据栈以及函数调用链，还有独立的调试钩子和错误处理设
施。所以我们不应当简单的把lua_State看成一个静态的数据集，它是一组Lua程序的执行状态机。所有的
Lua C API 都是围绕这个状态机，改变其状态的：或把数据压入堆栈，或取出，或执行栈顶的函数，或继续
上次被中断的执行过程.
*/
struct lua_State {
  CommonHeader;			//GCObject *next; lu_byte tt; lu_byte marked		COmmonHeader 宏展开后
  unsigned short nci;	//CallInfo 链表长度
  lu_byte status;		//状态机的状态
  StkId top;			//栈顶位置
  global_State *l_G;	//指向全局状态
  CallInfo *ci;			//调用栈 保存正在运行的函数的运行状态
  const Instruction *oldpc;  /* last pc traced */
  StkId stack_last;				//记录正常栈可用的最大值 即不包含额外分配的
  StkId stack;					//栈起始位置
  UpVal *openupval;		//放在堆栈上的 UpValue  链表  open upvalues
  GCObject *gclist;
  struct lua_State *twups;		//具有open upvalues的线程列表 list of threads with open upvalues
  //longjmp返回点链表 每次运行一段受保护的Lua代码,都会生成一个新的错误返回点,链入这个链表
  struct lua_longjmp *errorJmp;
  CallInfo base_ci;			//调用栈
  volatile lua_Hook hook;
  ptrdiff_t errfunc;		//当前错误处理函数 记录堆栈索引
  int stacksize;			//数据栈栈大小
  int basehookcount;
  int hookcount;
  unsigned short nny;		//调用栈上不能被挂起的次数 number of non-yieldable calls in stack
  unsigned short nCcalls;	//嵌套的C调用数量
  l_signalT hookmask;
  lu_byte allowhook;
};


#define G(L)	(L->l_G)


/*
** Union of all collectable objects (only for conversions)
*/
union GCUnion {
  GCObject gc;  /* common header */
  struct TString ts;
  struct Udata u;
  union Closure cl;
  struct Table h;
  struct Proto p;
  struct lua_State th;  /* thread */
};


#define cast_u(o)	cast(union GCUnion *, (o))

/* macros to convert a GCObject into a specific value */
#define gco2ts(o)  \
	check_exp(novariant((o)->tt) == LUA_TSTRING, &((cast_u(o))->ts))

//设置userdata类型	返回usrdata
#define gco2u(o)  check_exp((o)->tt == LUA_TUSERDATA, &((cast_u(o))->u))
//设置Lua闭包的类型	返回LClosure
#define gco2lcl(o)  check_exp((o)->tt == LUA_TLCL, &((cast_u(o))->cl.l))
//设置C闭包的类型 返回CClosure
#define gco2ccl(o)  check_exp((o)->tt == LUA_TCCL, &((cast_u(o))->cl.c))
#define gco2cl(o)  \
	check_exp(novariant((o)->tt) == LUA_TFUNCTION, &((cast_u(o))->cl))
//(GCUnion*)(o)->h
#define gco2t(o)  check_exp((o)->tt == LUA_TTABLE, &((cast_u(o))->h))
#define gco2p(o)  check_exp((o)->tt == LUA_TPROTO, &((cast_u(o))->p))
#define gco2th(o)  check_exp((o)->tt == LUA_TTHREAD, &((cast_u(o))->th))


//宏将一个Lua对象转化为GCObject对象
#define obj2gco(v) \
	check_exp(novariant((v)->tt) < LUA_TDEADKEY, (&(cast_u(v)->gc)))


/* 实际分配的总字节数 actual number of total bytes allocated */
#define gettotalbytes(g)	cast(lu_mem, (g)->totalbytes + (g)->GCdebt)

LUAI_FUNC void luaE_setdebt (global_State *g, l_mem debt);
LUAI_FUNC void luaE_freethread (lua_State *L, lua_State *L1);
LUAI_FUNC CallInfo *luaE_extendCI (lua_State *L);
LUAI_FUNC void luaE_freeCI (lua_State *L);
LUAI_FUNC void luaE_shrinkCI (lua_State *L);


#endif

