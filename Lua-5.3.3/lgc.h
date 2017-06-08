/*
** $Id: lgc.h,v 2.91 2015/12/21 13:02:14 roberto Exp $
** Garbage Collector
** See Copyright Notice in lua.h
*/

#ifndef lgc_h
#define lgc_h


#include "lobject.h"
#include "lstate.h"

/*
** Collectable objects may have one of three colors: white, which
** means the object is not marked; gray, which means the
** object is marked, but its references may be not marked; and
** black, which means that the object and all its references are marked.
** The main invariant of the garbage collector, while marking objects,
** is that a black object can never point to a white one. Moreover,
** any gray object must be in a "gray list" (gray, grayagain, weak,
** allweak, ephemeron) so that it can be visited again before finishing
** the collection cycle. These lists have no meaning when the invariant
** is not being enforced (e.g., sweep phase).
*/



/* how much to allocate before next GC step */
#if !defined(GCSTEPSIZE)
/* ~100 small strings */
#define GCSTEPSIZE	(cast_int(100 * sizeof(TString)))
#endif


/*
** Possible states of the Garbage Collector
GC的状态
GCSpause: 处于两次完整 GC 流程中间的休息状态
GCSpause 到 GCSpropagate : 一次性标记 root set
GCSpropagate: 可以分多次执行，直到 gray 链表处理完，进入 GCSatomic
GCSatoimic: 一次性的处理所有需要回顾一遍的地方, 保证一致性, 然后进入清理阶段 GCSswpallgc
GCSswpallgc: 清理 allgc 链表, 可以分多次执行, 清理完后进入 GCSswpfinobj
GCSswpfinobj: 清理 finobj 链表, 可以分多次执行, 清理完 后进入 GCSswptobefnz
GCSswptobefnz: 清理 tobefnz 链表, 可以分多次执行, 清理完 后进入 GCSswpend
GCSswpend: sweep main thread 然后进入 GCScallfin
GCScallfin: 执行一些 finalizer (__gc) 然后进入 GCSpause, 完成循环
*/
//标记阶段，将gray链表的元素标记为黑色并移除，如果gray链表为空则置换状态为GCSatomic
#define GCSpropagate	0
//标记阶段的原子操作，不可中断
#define GCSatomic	1
//扫描回收常规对象
#define GCSswpallgc	2
//扫描回收被终止器标记的对象
#define GCSswpfinobj	3
//扫描回收带有gc回调的udata对象
#define GCSswptobefnz	4
//扫描回收阶段结束阶段，扫描主线程、缩减字符串池大小
#define GCSswpend	5
//tobefnz不为NULL并且非紧急模式，则调用gc回调（通过GCTM），否则将状态置成GCSpause
#define GCScallfin	6
//gc前的初始化状态，global_State中的相关链表置成NULL，标记主线程的全局表、注册表等
#define GCSpause	7

//判断GC处于扫描回收状态
#define issweepphase(g)  \
	(GCSswpallgc <= (g)->gcstate && (g)->gcstate <= GCSswpend)


/*
** macro to tell when main invariant (white objects cannot point to black
** ones) must be kept. During a collection, the sweep
** phase may break the invariant, as objects turned white may point to
** still-black objects. The invariant is restored when sweep ends and
** all objects are white again.
*/

#define keepinvariant(g)	((g)->gcstate <= GCSatomic)


/*
** some useful bit tricks
*/
#define resetbits(x,m)		((x) &= cast(lu_byte, ~(m)))
#define setbits(x,m)		((x) |= (m))
#define testbits(x,m)		((x) & (m))
#define bitmask(b)		(1<<(b))
#define bit2mask(b1,b2)		(bitmask(b1) | bitmask(b2))
#define l_setbit(x,b)		setbits(x, bitmask(b))
#define resetbit(x,b)		resetbits(x, bitmask(b))
// x & 1<<b
#define testbit(x,b)		testbits(x, bitmask(b))


/* Layout for bit use in 'marked' field: */
#define WHITE0BIT	0  /* 白色 object is white (type 0) */
#define WHITE1BIT	1  /* 白色 object is white (type 1) */
#define BLACKBIT	2  /* 黑色 object is black */
#define FINALIZEDBIT	3  /* 对象已经被标记了拥有终结器(__gc)  object has been marked for finalization */
/* bit 7 is currently used by tests (luaL_checkmemory) */

//白色标记 两种白色
#define WHITEBITS	bit2mask(WHITE0BIT, WHITE1BIT)

//white 当前对象还没有被gc访问标记过，新增对象的初始状态，如果一个对象在标记过
//程后仍为白色，则表示该对象没有被系统中的其他对象所引用，可以回收
#define iswhite(x)      testbits((x)->marked, WHITEBITS)
//black 表示该对象已经被gc访问过，并且该对象引用的其他对象也已经被访问过
#define isblack(x)      testbit((x)->marked, BLACKBIT)
//gray 表示该对象已经被gc访问过，但是该对象引用的其他对象还没有被访问到
#define isgray(x)  /* neither white nor black */  \
	(!testbits((x)->marked, WHITEBITS | bitmask(BLACKBIT)))

//对象有终结器（__gc）的判断
#define tofinalize(x)	testbit((x)->marked, FINALIZEDBIT)
//得到和当前虚拟机不同的那个白色
#define otherwhite(g)	((g)->currentwhite ^ WHITEBITS)
//得到和m不同的白色 然后&ow 再求反 即如果m和ow是同一种白色返回true 不同种返回false
#define isdeadm(ow,m)	(!(((m) ^ WHITEBITS) & (ow)))
//v的颜色是白色而且和Lua虚拟机的颜色是同一种白色 返回true 否则返回false
#define isdead(g,v)	isdeadm(otherwhite(g), (v)->marked)

#define changewhite(x)	((x)->marked ^= WHITEBITS)
//标记为黑色
#define gray2black(x)	l_setbit((x)->marked, BLACKBIT)
//当前Lua虚拟机白色
#define luaC_white(g)	cast(lu_byte, (g)->currentwhite & WHITEBITS)


/*
** Does one step of collection when debt becomes positive. 'pre'/'pos'
** allows some adjustments to be done only when needed. macro
** 'condchangemem' is used only for heavy tests (forcing a full
** GC cycle on every opportunity)
条件满足则启动GC g->GCdebt>0
*/
#define luaC_condGC(L,pre,pos) \
	{ if (G(L)->GCdebt > 0) { pre; luaC_step(L); pos;}; \
	  condchangemem(L,pre,pos); }

/* more often than not, 'pre'/'pos' are empty */
#define luaC_checkGC(L)		luaC_condGC(L,(void)0,(void)0)

//p引用对象，v被引用对象 v可被回收 p是黑色的 v->gc是白色的
#define luaC_barrier(L,p,v) (  \
	(iscollectable(v) && isblack(p) && iswhite(gcvalue(v))) ?  \
	luaC_barrier_(L,obj2gco(p),gcvalue(v)) : cast_void(0))

#define luaC_barrierback(L,p,v) (  \
	(iscollectable(v) && isblack(p) && iswhite(gcvalue(v))) ? \
	luaC_barrierback_(L,p) : cast_void(0))

#define luaC_objbarrier(L,p,o) (  \
	(isblack(p) && iswhite(o)) ? \
	luaC_barrier_(L,obj2gco(p),obj2gco(o)) : cast_void(0))

//对close upvalue的处理
#define luaC_upvalbarrier(L,uv) ( \
	(iscollectable((uv)->v) && !upisopen(uv)) ? \
         luaC_upvalbarrier_(L,uv) : cast_void(0))

LUAI_FUNC void luaC_fix (lua_State *L, GCObject *o);
LUAI_FUNC void luaC_freeallobjects (lua_State *L);
LUAI_FUNC void luaC_step (lua_State *L);
LUAI_FUNC void luaC_runtilstate (lua_State *L, int statesmask);
LUAI_FUNC void luaC_fullgc (lua_State *L, int isemergency);
LUAI_FUNC GCObject *luaC_newobj (lua_State *L, int tt, size_t sz);
LUAI_FUNC void luaC_barrier_ (lua_State *L, GCObject *o, GCObject *v);
LUAI_FUNC void luaC_barrierback_ (lua_State *L, Table *o);
LUAI_FUNC void luaC_upvalbarrier_ (lua_State *L, UpVal *uv);
LUAI_FUNC void luaC_checkfinalizer (lua_State *L, GCObject *o, Table *mt);
LUAI_FUNC void luaC_upvdeccount (lua_State *L, UpVal *uv);


#endif
