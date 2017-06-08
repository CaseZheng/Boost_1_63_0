/*
** $Id: lgc.c,v 2.212 2016/03/31 19:02:03 roberto Exp $
** Garbage Collector
** See Copyright Notice in lua.h
*/

#define lgc_c
#define LUA_CORE

#include "lprefix.h"

#include <string.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"


/*
** internal state for collector while inside the atomic phase. The
** collector should never be in this state while running regular code.
*/
#define GCSinsideatomic		(GCSpause + 1)

/*
** cost of sweeping one element (the size of a small object divided
** by some adjust for the sweep speed)
*/
#define GCSWEEPCOST	((sizeof(TString) + 4) / 4)

/* maximum number of elements to sweep in each single step */
#define GCSWEEPMAX	(cast_int((GCSTEPSIZE / GCSWEEPCOST) / 4))

/* cost of calling one finalizer */
#define GCFINALIZECOST	GCSWEEPCOST


/*
** macro to adjust 'stepmul': 'stepmul' is actually used like
** 'stepmul / STEPMULADJ' (value chosen by tests)
*/
#define STEPMULADJ		200


/*
** macro to adjust 'pause': 'pause' is actually used like
** 'pause / PAUSEADJ' (value chosen by tests)
*/
#define PAUSEADJ		100


/*
** 'makewhite' erases all color bits then sets only the current white bit
*/
#define maskcolors	(~(bitmask(BLACKBIT) | WHITEBITS))
//makewhite擦除所有颜色位 只设置当前白色位 x的颜色 等于 ( x->marked & (~(bitmask(BLACKBIT) | WHITEBITS)) ) | luaC_white(g)
#define makewhite(g,x)	\
 (x->marked = cast_byte((x->marked & maskcolors) | luaC_white(g)))

//将对象从白色置为灰色
#define white2gray(x)	resetbits(x->marked, WHITEBITS)
//将对象从黑色置为灰色
#define black2gray(x)	resetbit(x->marked, BLACKBIT)

//对象可回收且是白色
#define valiswhite(x)   (iscollectable(x) && iswhite(gcvalue(x)))

#define checkdeadkey(n)	lua_assert(!ttisdeadkey(gkey(n)) || ttisnil(gval(n)))


#define checkconsistency(obj)  \
  lua_longassert(!iscollectable(obj) || righttt(obj))


//对象o如果是可回收的且为白色 调用reallymarkobject
#define markvalue(g,o) { checkconsistency(o); \
  if (valiswhite(o)) reallymarkobject(g,gcvalue(o)); }

//如果对象是白色 标记对象
#define markobject(g,t)	{ if (iswhite(t)) reallymarkobject(g, obj2gco(t)); }

/*
** mark an object that can be NULL (either because it is really optional,
** or it was stripped as debug info, or inside an uncompleted structure)
标记一个可以为NULL的对象
*/
#define markobjectN(g,t)	{ if (t) markobject(g,t); }

static void reallymarkobject (global_State *g, GCObject *o);


/*
** {======================================================
** Generic functions
** =======================================================
*/


/*
** one after last element in a hash array
hash数组最后一个元素
*/
#define gnodelast(h)	gnode(h, cast(size_t, sizenode(h)))


/*
** link collectable object 'o' into list pointed by 'p'
连接可回收对象'o'到'p'指向的列表中
*/
#define linkgclist(o,p)	((o)->gclist = (p), (p) = obj2gco(o))


/*
** If key is not marked, mark its entry as dead. This allows key to be
** collected, but keeps its entry in the table.  A dead node is needed
** when Lua looks up for a key (it may be part of a chain) and when
** traversing a weak table (key might be removed from the table during
** traversal). Other places never manipulate dead keys, because its
** associated nil value is enough to signal that the entry is logically
** empty.
移除一个结点
*/
static void removeentry (Node *n) {
  lua_assert(ttisnil(gval(n)));
  if (valiswhite(gkey(n)))		//结点n的key是可回收且是白色的
    setdeadvalue(wgkey(n));		//未使用而且未标记的key 设置dead 移除 unused and unmarked key; remove it
}


/*
** tells whether a key or value can be cleared from a weak
** table. Non-collectable objects are never removed from weak
** tables. Strings behave as 'values', so are never removed too. for
** other objects: if really collected, cannot keep them; for objects
** being finalized, keep them in keys, but not in values
告诉是否可以从弱表中清除键或值。 不可收集的对象永远不会从弱表中删除。 
字符串表现为“值”，因此也不会被删除。 对于其他对象：如果真的收集，不
能保持他们; 对于正在最终确定的对象，将它们保存在键中，而不是值中
*/
static int iscleared (global_State *g, const TValue *o) {
  if (!iscollectable(o)) return 0;			//不可回收 返回0
  else if (ttisstring(o)) {					//字符串 标记它	返回0
    markobject(g, tsvalue(o));  /* strings are 'values', so are never weak */
    return 0;
  }
  else return iswhite(gcvalue(o));			//别的对象 白色返回 1 否则返回0
}


/*
** barrier that moves collector forward, that is, mark the white object
** being pointed by a black object. (If in sweep phase, clear the black
** object to white [sweep it] to avoid other barrier calls for this
** same object.)
*/
void luaC_barrier_ (lua_State *L, GCObject *o, GCObject *v) {
  global_State *g = G(L);
  lua_assert(isblack(o) && iswhite(v) && !isdead(g, v) && !isdead(g, o));
  if (keepinvariant(g))  //标记阶段 must keep invariant?
    reallymarkobject(g, v);  //标记v restore invariant
  else {  //清理阶段 sweep phase
    lua_assert(issweepphase(g));
    makewhite(g, o);  //将o标记为白色 mark main obj. as white to avoid other barriers 
  }
}


/*
** barrier that moves collector backward, that is, mark the black object
** pointing to a white object as gray again.
*/
void luaC_barrierback_ (lua_State *L, Table *t) {
  global_State *g = G(L);
  lua_assert(isblack(t) && !isdead(g, t));
  black2gray(t);  //将其从黑色置为灰色 make table gray (again)
  linkgclist(t, g->grayagain);		//加入grayagain链表
}


/*
** barrier for assignments to closed upvalues. Because upvalues are
** shared among closures, it is impossible to know the color of all
** closures pointing to it. So, we assume that the object being assigned
** must be marked.
*/
void luaC_upvalbarrier_ (lua_State *L, UpVal *uv) {
  global_State *g = G(L);
  GCObject *o = gcvalue(uv->v);
  lua_assert(!upisopen(uv));  /* ensured by macro luaC_upvalbarrier */
  if (keepinvariant(g))			//标记阶段
    markobject(g, o);
}

//设置对象o 永远不会被gc回收
void luaC_fix (lua_State *L, GCObject *o) {
  global_State *g = G(L);
  lua_assert(g->allgc == o);	// object must be 1st in 'allgc' list!
  white2gray(o);				//将对象o置为灰色的
  g->allgc = o->next;			//将o节点从allgc链表删除
  //将o节点链入 fixedgc list
  o->next = g->fixedgc;  
  g->fixedgc = o;
}


/*
创建一个新的引用对象 并加入allgc链表
tt 对象类型 sz对象大小
*/
GCObject *luaC_newobj (lua_State *L, int tt, size_t sz) {
  global_State *g = G(L);
  //(GCObject*)(luaM_newobject(L, novariant(tt), sz))		
  GCObject *o = cast(GCObject *, luaM_newobject(L, novariant(tt), sz));
  o->marked = luaC_white(g);	//给对象标记颜色为当前Lua虚拟机的颜色
  o->tt = tt;					//设置对象类型
  //插入allgc链表头
  o->next = g->allgc;
  g->allgc = o;
  return o;
}

/* }====================================================== */



/*
** {======================================================
** Mark functions
** =======================================================
*/


/*
** mark an object. Userdata, strings, and closed upvalues are visited
** and turned black here. Other objects are marked gray and added
** to appropriate list to be visited (and turned black) later. (Open
** upvalues are already linked in 'headuv' list.)
标记一个对象。Userdata，strings和closed upvalues是被访问的 并标记为黑色 
其余对象标记为灰色，并添加到适当的列表中以便稍后访问(并变黑)
(open upvalues 已经链接到headuv列表)
*/
static void reallymarkobject (global_State *g, GCObject *o) {
 reentry:
  white2gray(o);				//对象置为灰色
  switch (o->tt) {
    case LUA_TSHRSTR: {			//短字符串
      gray2black(o);			//置为黑色
      g->GCmemtrav += sizelstring(gco2ts(o)->shrlen);
      break;
    }
    case LUA_TLNGSTR: {			//长字符串
      gray2black(o);			//置为黑色
      g->GCmemtrav += sizelstring(gco2ts(o)->u.lnglen);
      break;
    }
    case LUA_TUSERDATA: {		//userdata
      TValue uvalue;
      markobjectN(g, gco2u(o)->metatable);  //标记它的元表
      gray2black(o);			//置为黑色
      g->GCmemtrav += sizeudata(gco2u(o));
      getuservalue(g->mainthread, gco2u(o), &uvalue);
      if (valiswhite(&uvalue)) {	//userdata数据可被标记
        o = gcvalue(&uvalue);		//重置对象o
        goto reentry;				//标记o
      }
      break;
    }
    case LUA_TLCL: {
      linkgclist(gco2lcl(o), g->gray);
      break;
    }
    case LUA_TCCL: {
      linkgclist(gco2ccl(o), g->gray);
      break;
    }
    case LUA_TTABLE: {
      linkgclist(gco2t(o), g->gray);
      break;
    }
    case LUA_TTHREAD: {
      linkgclist(gco2th(o), g->gray);
      break;
    }
    case LUA_TPROTO: {
      linkgclist(gco2p(o), g->gray);
      break;
    }
    default: lua_assert(0); break;
  }
}


/*
** mark metamethods for basic types
标记基本类型的元方法
*/
static void markmt (global_State *g) {
  int i;
  for (i=0; i < LUA_NUMTAGS; i++)
    markobjectN(g, g->mt[i]);
}


/*
** mark all objects in list of being-finalized
标记列表中的所有对象 userdata
*/
static void markbeingfnz (global_State *g) {
  GCObject *o;
  for (o = g->tobefnz; o != NULL; o = o->next)
    markobject(g, o);
}


/*
** Mark all values stored in marked open upvalues from non-marked threads.
** (Values from marked threads were already marked when traversing the
** thread.) Remove from the list threads that no longer have upvalues and
** not-marked threads.
*/
static void remarkupvals (global_State *g) {
  lua_State *thread;
  lua_State **p = &g->twups;
  while ((thread = *p) != NULL) {		//遍历列表
    lua_assert(!isblack(thread));  /* threads are never black */
    if (isgray(thread) && thread->openupval != NULL)		//线程是灰色 而且其openupval不为NULL
      p = &thread->twups;	//在列表中保持标记线程的upvalues keep marked thread with upvalues in the list
    else {		//线程没有被标记或者没有upvalue thread is not marked or without upvalues
      UpVal *uv;
      *p = thread->twups;  /* remove thread from the list */
      thread->twups = thread;  /* mark that it is out of list */
      for (uv = thread->openupval; uv != NULL; uv = uv->u.open.next) {		//遍历open upvalue
        if (uv->u.open.touched) {
          markvalue(g, uv->v);			//重新标记upvalue remark upvalue's value
          uv->u.open.touched = 0;
        }
      }
    }
  }
}


/*
** mark root set and reset all gray lists, to start a new collection
重新开始垃圾收集
新gc过程的开始。
此时root set中包含：
* lua_State对象
* _G table
* Registry
* metatable数组
本状态将root set中所有old white对象变gray，并至于gray list中。
*/
static void restartcollection (global_State *g) {
  g->gray = g->grayagain = NULL;
  g->weak = g->allweak = g->ephemeron = NULL;
  markobject(g, g->mainthread);						//主线程为白色 则标记为置为灰色加入gray链表
  markvalue(g, &g->l_registry);						//注册表是可回收的 且是白色 将其置为灰色 加入gray链表
  markmt(g);										//元方法 如果是白色 置为灰色 加入gray链表
  markbeingfnz(g);									//遍历userdata列表 如果是白色 标记它
}

/* }====================================================== */


/*
** {======================================================
** Traverse functions
** =======================================================
*/

/*
** Traverse a table with weak values and link it to proper list. During
** propagate phase, keep it in 'grayagain' list, to be revisited in the
** atomic phase. In the atomic phase, if table has any white value,
** put it in 'weak' list, to be cleared.
遍历具有弱值的表，并将其链接到正确的列表。 在标记阶段，保持在“grayagain”列表中，
在原子阶段重新访问。 在原子阶段，如果表有任何白色值，将其放在“弱”列表中，以清除。
*/
static void traverseweakvalue (global_State *g, Table *h) {
  Node *n, *limit = gnodelast(h);			//得到hash表最后一个元素
  /* if there is array part, assume it may have white values (it is not
     worth traversing it now just to check) */
  int hasclears = (h->sizearray > 0);		//数组部分存在标志
  for (n = gnode(h, 0); n < limit; n++) {  //遍历hash表部分
    checkdeadkey(n);
    if (ttisnil(gval(n)))					//该结点值是nil
      removeentry(n);						//移除它
    else {
      lua_assert(!ttisnil(gkey(n)));
      markvalue(g, gkey(n));				//标记key值
      if (!hasclears && iscleared(g, gval(n)))  //不存在数组部分 而且可以被清理 is there a white value
        hasclears = 1;						//标记table 可以被清除table will have to be cleared
    }
  }
  if (g->gcstate == GCSpropagate)			//标记阶段
    linkgclist(h, g->grayagain);			//加入grayagain列表 must retraverse it in atomic phase
  else if (hasclears)						//不是标记阶段 表可被清除 
    linkgclist(h, g->weak);					//将表加入weak列表中
}


/*
** Traverse an ephemeron table and link it to proper list. Returns true
** iff any object was marked during this traversal (which implies that
** convergence has to continue). During propagation phase, keep table
** in 'grayagain' list, to be visited again in the atomic phase. In
** the atomic phase, if table has any white->white entry, it has to
** be revisited during ephemeron convergence (as that key may turn
** black). Otherwise, if it has any white key, table has to be cleared
** (in the atomic phase).
遍历一个弱表 其键为弱键 并将它链接到一个正确的列表中 如果任何一个对象在遍历期间被标记返回true
在标记阶段 将表加入gragagain列表中 在原子阶段再次访问 在原子阶段如果表中具有白->白色键值对 则必须在
ephemeron阶段再次重新访问(因为该键可能变黑) 否则如果它有任何白色的key 表将被清除在原则阶段
*/
static int traverseephemeron (global_State *g, Table *h) {
  int marked = 0;			//如果在此遍历中标记对象，则为true 
  int hasclears = 0;		//如果表中有白色的键 置为true
  int hasww = 0;			//如果表中有白色key->白色value 键值对 置为true
  Node *n, *limit = gnodelast(h);		//得到table hash部分的最后一个结点
  unsigned int i;
  /* traverse array part */
  for (i = 0; i < h->sizearray; i++) {	//遍历数组部分
    if (valiswhite(&h->array[i])) {		//数组部分 值为白色
      marked = 1;						//置为true
      reallymarkobject(g, gcvalue(&h->array[i]));	//标记它
    }
  }
  /* traverse hash part */
  for (n = gnode(h, 0); n < limit; n++) {		//遍历hash表部分
    checkdeadkey(n);
    if (ttisnil(gval(n)))						//值为nil
      removeentry(n);							//移除它
    else if (iscleared(g, gkey(n))) {			//键可以被标记 key is not marked (yet)?
      hasclears = 1;							//表可以被清除
      if (valiswhite(gval(n)))					//值可以被标记	
        hasww = 1;								// white-white entry
    }
    else if (valiswhite(gval(n))) {				//值未被标记 value not marked yet?
      marked = 1;
      reallymarkobject(g, gcvalue(gval(n)));	//标记它 mark it now
    }
  }
  /* 将table链接到正确的列表中 link table into proper list */
  if (g->gcstate == GCSpropagate)				//标记阶段 将table加入grayagain列表
    linkgclist(h, g->grayagain);	//重新遍历在原子阶段 must retraverse it in atomic phase
  else if (hasww)								//不是标记阶段 有白色key->白色value键值对
    linkgclist(h, g->ephemeron);				//加入ephemeron列表 have to propagate again
  else if (hasclears)				//不是标记阶段 也没有白色key白色value键值对 但有白色可以
    linkgclist(h, g->allweak);		//加入allweak列表 may have to clean white keys
  return marked;
}

//标记不存在弱键或弱值的表
static void traversestrongtable (global_State *g, Table *h) {
  Node *n, *limit = gnodelast(h);
  unsigned int i;
  for (i = 0; i < h->sizearray; i++)		//遍历数组部分
    markvalue(g, &h->array[i]);				//标记
  for (n = gnode(h, 0); n < limit; n++) {		//遍历hash表部分
    checkdeadkey(n);
    if (ttisnil(gval(n)))					//值为nil
      removeentry(n);  //删除该结点
    else {
      lua_assert(!ttisnil(gkey(n)));
      markvalue(g, gkey(n));				//标记key mark key
      markvalue(g, gval(n));				//标记值 mark value 
    }
  }
}

//标记table表引用的对象
static lu_mem traversetable (global_State *g, Table *h) {
  const char *weakkey, *weakvalue;
  const TValue *mode = gfasttm(g, h->metatable, TM_MODE);
  markobjectN(g, h->metatable);				//标记它的元表
  if (mode && ttisstring(mode) &&			//弱表标记存在
      ((weakkey = strchr(svalue(mode), 'k')),
       (weakvalue = strchr(svalue(mode), 'v')),
       (weakkey || weakvalue))) {			//是弱表
    black2gray(h);							//将表置为灰色
    if (!weakkey)							//不是弱键 即为弱值
      traverseweakvalue(g, h);				//遍历有弱值的表
    else if (!weakvalue)					//不是弱值 即为弱键
      traverseephemeron(g, h);				//遍历有若键的表
    else									//弱值 弱键
      linkgclist(h, g->allweak);			//将弱表加入allweak链表 不需要遍历
  }
  else										//不是弱表
    traversestrongtable(g, h);				//标记非弱表
  return sizeof(Table) + sizeof(TValue) * h->sizearray +
                         sizeof(Node) * cast(size_t, sizenode(h));
}


/*
** Traverse a prototype. (While a prototype is being build, its
** arrays can be larger than needed; the extra slots are filled with
** NULL, so the use of 'markobjectN')
遍历标记函数原型
*/
static int traverseproto (global_State *g, Proto *f) {
  int i;
  if (f->cache && iswhite(f->cache))		//如果函数原型的缓存存在 且为白色 置为NULL
    f->cache = NULL;  /* allow cache to be collected */
  markobjectN(g, f->source);				//标记模块名
  for (i = 0; i < f->sizek; i++)			//标记常量
    markvalue(g, &f->k[i]);
  for (i = 0; i < f->sizeupvalues; i++)		//标记upvalue
    markobjectN(g, f->upvalues[i].name);
  for (i = 0; i < f->sizep; i++)			//标记函数内定义的函数 
    markobjectN(g, f->p[i]);
  for (i = 0; i < f->sizelocvars; i++)		//标记局部变量信息
    markobjectN(g, f->locvars[i].varname);
  return sizeof(Proto) + sizeof(Instruction) * f->sizecode +
                         sizeof(Proto *) * f->sizep +
                         sizeof(TValue) * f->sizek +
                         sizeof(int) * f->sizelineinfo +
                         sizeof(LocVar) * f->sizelocvars +
                         sizeof(Upvaldesc) * f->sizeupvalues;
}


static lu_mem traverseCclosure (global_State *g, CClosure *cl) {
  int i;
  for (i = 0; i < cl->nupvalues; i++)  //标记其upvalues值
    markvalue(g, &cl->upvalue[i]);
  return sizeCclosure(cl->nupvalues);
}

/*
** open upvalues point to values in a thread, so those values should
** be marked when the thread is traversed except in the atomic phase
** (because then the value cannot be changed by the thread and the
** thread may not be traversed again)
*/
static lu_mem traverseLclosure (global_State *g, LClosure *cl) {
  int i;
  markobjectN(g, cl->p);					// 标记closure的函数原型
  for (i = 0; i < cl->nupvalues; i++) {		// 标记closure的upvalues
    UpVal *uv = cl->upvals[i];
    if (uv != NULL) {
      if (upisopen(uv) && g->gcstate != GCSinsideatomic)	//open vpvalues
        uv->u.open.touched = 1;  /* can be marked in 'remarkupvals' */
      else
        markvalue(g, uv->v);				//close upvalues 标记它
    }
  }
  return sizeLclosure(cl->nupvalues);		//返回Lua closure的大小
}


static lu_mem traversethread (global_State *g, lua_State *th) {
  StkId o = th->stack;						//栈的起始位置
  if (o == NULL)
    return 1;  //栈底为NULL 堆栈尚未完全构建
  lua_assert(g->gcstate == GCSinsideatomic ||
             th->openupval == NULL || isintwups(th));
  for (; o < th->top; o++)					//遍历栈
    markvalue(g, o);						//标记
  if (g->gcstate == GCSinsideatomic) {		//最终遍历? final traversal?
    StkId lim = th->stack + th->stacksize;		//得到栈的真正结束位置 real end of stack
    for (; o < lim; o++)  //将栈不用的部分置为nil clear not-marked stack slice
      setnilvalue(o);
    /* 'remarkupvals'可能已经从'twups'列表中删除线程 'remarkupvals' may have removed thread from 'twups' list */ 
    if (!isintwups(th) && th->openupval != NULL) {
      th->twups = g->twups;  /* link it back to the list */
      g->twups = th;
    }
  }
  else if (g->gckind != KGC_EMERGENCY)
    luaD_shrinkstack(th); /* do not change stack in emergency cycle */
  return (sizeof(lua_State) + sizeof(TValue) * th->stacksize +
          sizeof(CallInfo) * th->nci);
}


/*
** traverse one gray object, turning it to black (except for threads,
** which are always gray).
遍历一个灰色对象，将其变为黑色（线程总是灰色除外）。
将gray list中的每个对象标志为black并从gray list中移出，并遍历每个
对象引用关系，将所有仍为old white的对象变成gray并添加到gray list。
当gray list中没有任何对象时，将old white翻转为current white。
*/
static void propagatemark (global_State *g) {
  lu_mem size;
  GCObject *o = g->gray;
  lua_assert(isgray(o));
  gray2black(o);					//将对象o标记为黑色
  switch (o->tt) {
    case LUA_TTABLE: {				//TABLE
      Table *h = gco2t(o);
      g->gray = h->gclist;			//将该对象从gray列表中移除
      size = traversetable(g, h);	//标记表
      break;
    }
    case LUA_TLCL: {				//Lua Closure
      LClosure *cl = gco2lcl(o);
      g->gray = cl->gclist;			//将该对象从gray列表中移除  
      size = traverseLclosure(g, cl);
      break;
    }
    case LUA_TCCL: {				//C Closure
      CClosure *cl = gco2ccl(o);
      g->gray = cl->gclist;			//将该对象从gray列表中移除 
      size = traverseCclosure(g, cl);
      break;
    }
    case LUA_TTHREAD: {				//thread
      lua_State *th = gco2th(o);
      g->gray = th->gclist;			//将该对象从gray列表中移除
      linkgclist(th, g->grayagain);		//将thread加入grayagain链表
      black2gray(o);				//置为灰色
      size = traversethread(g, th);	//标记thread 主要是栈上元素的标记
      break;
    }
    case LUA_TPROTO: {				//函数原型
      Proto *p = gco2p(o);
      g->gray = p->gclist;			//将该对象从gray列表中移除
      size = traverseproto(g, p);
      break;
    }
    default: lua_assert(0); return;
  }
  g->GCmemtrav += size;
}

//如果gray列表不空 循环标记直到 gray列表为NULL
static void propagateall (global_State *g) {
  while (g->gray) propagatemark(g);
}


static void convergeephemerons (global_State *g) {
  int changed;
  do {
    GCObject *w;
    GCObject *next = g->ephemeron;  //得到暂时表列表 get ephemeron list
    g->ephemeron = NULL;  //将暂时表列表置NULL tables may return to this list when traversed
    changed = 0;
    while ((w = next) != NULL) {	//遍历next
      next = gco2t(w)->gclist;
      if (traverseephemeron(g, gco2t(w))) {  //遍历标记一些值 traverse marked some value?
        propagateall(g);	//清空g->gray列表 propagate changes
        changed = 1;		//将必须重新访问所有的暂时表 will have to revisit all ephemeron tables
      }
    }
  } while (changed);
}

/* }====================================================== */


/*
** {======================================================
** Sweep Functions
** =======================================================
*/


/*
** clear entries with unmarked keys from all weaktables in list 'l' up
** to element 'f'
从列表“l”到元素'f'中的所有弱表中清除具有未标记键的条目
*/
static void clearkeys (global_State *g, GCObject *l, GCObject *f) {
  for (; l != f; l = gco2t(l)->gclist) {
    Table *h = gco2t(l);
    Node *n, *limit = gnodelast(h);
    for (n = gnode(h, 0); n < limit; n++) {
      if (!ttisnil(gval(n)) && (iscleared(g, gkey(n)))) {
        setnilvalue(gval(n));  /* remove value ... */
        removeentry(n);  /* and remove entry from table */
      }
    }
  }
}


/*
** clear entries with unmarked values from all weaktables in list 'l' up
** to element 'f'
清除列表'l'到元素'f'中所有弱表的未标记值的条目
*/
static void clearvalues (global_State *g, GCObject *l, GCObject *f) {
  for (; l != f; l = gco2t(l)->gclist) {
    Table *h = gco2t(l);
    Node *n, *limit = gnodelast(h);
    unsigned int i;
    for (i = 0; i < h->sizearray; i++) {		//数组部分
      TValue *o = &h->array[i];
      if (iscleared(g, o))						//判定可被回收 value was collected?
        setnilvalue(o);							//删除它 remove value
    }
    for (n = gnode(h, 0); n < limit; n++) {	//hash表部分
      if (!ttisnil(gval(n)) && iscleared(g, gval(n))) {		//值不为nil 而且可被回收
        setnilvalue(gval(n));					//删除它
        removeentry(n);							//移除结点 and remove entry from table 
      }
    }
  }
}


void luaC_upvdeccount (lua_State *L, UpVal *uv) {
  lua_assert(uv->refcount > 0);
  uv->refcount--;
  if (uv->refcount == 0 && !upisopen(uv))
    luaM_free(L, uv);
}


static void freeLclosure (lua_State *L, LClosure *cl) {
  int i;
  for (i = 0; i < cl->nupvalues; i++) {
    UpVal *uv = cl->upvals[i];
    if (uv)
      luaC_upvdeccount(L, uv);
  }
  luaM_freemem(L, cl, sizeLclosure(cl->nupvalues));
}


static void freeobj (lua_State *L, GCObject *o) {
  switch (o->tt) {
    case LUA_TPROTO: luaF_freeproto(L, gco2p(o)); break;
    case LUA_TLCL: {
      freeLclosure(L, gco2lcl(o));
      break;
    }
    case LUA_TCCL: {
      luaM_freemem(L, o, sizeCclosure(gco2ccl(o)->nupvalues));
      break;
    }
    case LUA_TTABLE: luaH_free(L, gco2t(o)); break;
    case LUA_TTHREAD: luaE_freethread(L, gco2th(o)); break;
    case LUA_TUSERDATA: luaM_freemem(L, o, sizeudata(gco2u(o))); break;
    case LUA_TSHRSTR:
      luaS_remove(L, gco2ts(o));  /* remove it from hash table */
      luaM_freemem(L, o, sizelstring(gco2ts(o)->shrlen));
      break;
    case LUA_TLNGSTR: {
      luaM_freemem(L, o, sizelstring(gco2ts(o)->u.lnglen));
      break;
    }
    default: lua_assert(0);
  }
}


#define sweepwholelist(L,p)	sweeplist(L,p,MAX_LUMEM)
static GCObject **sweeplist (lua_State *L, GCObject **p, lu_mem count);


/*
** sweep at most 'count' elements from a list of GCObjects erasing dead
** objects, where a dead object is one marked with the old (non current)
** white; change all non-dead objects back to white, preparing for next
** collection cycle. Return where to continue the traversal or NULL if
** list is finished.
主要的用途是移除死亡的节点，回收该死亡节点的资源。节点死亡的条件是，当该节点为白色，
并且与虚拟机当前的白色不同。这里要说明一下，lua中用了两个位来表示白色，每次trace-mark完
之后，会切换一下虚拟机当前白色的比特位，具体代码在atomic接口中。在执行sweeplist时，
若节点未死亡，则该节点的颜色会被染为当前的白色。
*/
static GCObject **sweeplist (lua_State *L, GCObject **p, lu_mem count) {
  global_State *g = G(L);
  int ow = otherwhite(g);		//得到和当前Lua虚拟机不同的白色
  int white = luaC_white(g);	//得到Lua虚拟机的当前白色
  while (*p != NULL && count-- > 0) {	//链表不为NULL 遍历count个结点
    GCObject *curr = *p;
    int marked = curr->marked;	//对象的颜色
    if (isdeadm(ow, marked)) {		//死的 清除
      *p = curr->next;				//从链表中删除
      freeobj(L, curr);				//释放curr
    }
    else {							//改变白色的bit位 切换到当前虚拟机的白色
      curr->marked = cast_byte((marked & maskcolors) | white);
      p = &curr->next;
    }
  }
  return (*p == NULL) ? NULL : p;
}


/*
** sweep a list until a live object (or end of list)
*/
static GCObject **sweeptolive (lua_State *L, GCObject **p) {
  GCObject **old = p;
  do {
    p = sweeplist(L, p, 1);
  } while (p == old);
  return p;
}

/* }====================================================== */


/*
** {======================================================
** Finalization
** =======================================================
*/

/*
** If possible, shrink string table
*/
static void checkSizes (lua_State *L, global_State *g) {
  if (g->gckind != KGC_EMERGENCY) {
    l_mem olddebt = g->GCdebt;
    if (g->strt.nuse < g->strt.size / 4)  /* string table too big? */
      luaS_resize(L, g->strt.size / 2);  /* shrink it a little */
    g->GCestimate += g->GCdebt - olddebt;  /* update estimate */
  }
}


static GCObject *udata2finalize (global_State *g) {
  GCObject *o = g->tobefnz;  /* get first element */
  lua_assert(tofinalize(o));
  g->tobefnz = o->next;  /* remove it from 'tobefnz' list */
  o->next = g->allgc;  /* return it to 'allgc' list */
  g->allgc = o;
  resetbit(o->marked, FINALIZEDBIT);  /* object is "normal" again */
  if (issweepphase(g))
    makewhite(g, o);  /* "sweep" object */
  return o;
}


static void dothecall (lua_State *L, void *ud) {
  UNUSED(ud);
  luaD_callnoyield(L, L->top - 2, 0);
}


static void GCTM (lua_State *L, int propagateerrors) {
  global_State *g = G(L);
  const TValue *tm;
  TValue v;
  setgcovalue(L, &v, udata2finalize(g));
  tm = luaT_gettmbyobj(L, &v, TM_GC);
  if (tm != NULL && ttisfunction(tm)) {  /* is there a finalizer? */
    int status;
    lu_byte oldah = L->allowhook;
    int running  = g->gcrunning;
    L->allowhook = 0;  /* stop debug hooks during GC metamethod */
    g->gcrunning = 0;  /* avoid GC steps */
    setobj2s(L, L->top, tm);  /* push finalizer... */
    setobj2s(L, L->top + 1, &v);  /* ... and its argument */
    L->top += 2;  /* and (next line) call the finalizer */
    status = luaD_pcall(L, dothecall, NULL, savestack(L, L->top - 2), 0);
    L->allowhook = oldah;  /* restore hooks */
    g->gcrunning = running;  /* restore state */
    if (status != LUA_OK && propagateerrors) {  /* error while running __gc? */
      if (status == LUA_ERRRUN) {  /* is there an error object? */
        const char *msg = (ttisstring(L->top - 1))
                            ? svalue(L->top - 1)
                            : "no message";
        luaO_pushfstring(L, "error in __gc metamethod (%s)", msg);
        status = LUA_ERRGCMM;  /* error in __gc metamethod */
      }
      luaD_throw(L, status);  /* re-throw error */
    }
  }
}


/*
** call a few (up to 'g->gcfinnum') finalizers
*/
static int runafewfinalizers (lua_State *L) {
  global_State *g = G(L);
  unsigned int i;
  lua_assert(!g->tobefnz || g->gcfinnum > 0);
  for (i = 0; g->tobefnz && i < g->gcfinnum; i++)
    GCTM(L, 1);  /* call one finalizer */
  g->gcfinnum = (!g->tobefnz) ? 0  /* nothing more to finalize? */
                    : g->gcfinnum * 2;  /* else call a few more next time */
  return i;
}


/*
** call all pending finalizers
*/
static void callallpendingfinalizers (lua_State *L) {
  global_State *g = G(L);
  while (g->tobefnz)
    GCTM(L, 0);
}


/*
** find last 'next' field in list 'p' list (to add elements in its end)
在列表'p'列表中找到最后'next'字段（在其末尾添加元素）
*/
static GCObject **findlast (GCObject **p) {
  while (*p != NULL)
    p = &(*p)->next;
  return p;
}


/*
** move all unreachable objects (or 'all' objects) that need
** finalization from list 'finobj' to list 'tobefnz' (to be finalized)
将需要的最终确认的所有不可达对象（或'all'对象）从列表'finobj'移动到列表'tobefnz'（要最终确定）
*/
static void separatetobefnz (global_State *g, int all) {
  GCObject *curr;
  GCObject **p = &g->finobj;
  GCObject **lastnext = findlast(&g->tobefnz);		//找到g->tpbefnz列表的最后一个结点的next域
  while ((curr = *p) != NULL) {  //遍历所有可结束对象 traverse all finalizable objects
    lua_assert(tofinalize(curr));
    if (!(iswhite(curr) || all))  //不能被收集 not being collected?
      p = &curr->next;  // 跳过这个结点 don't bother with it
    else {
      *p = curr->next;				//将curr从finobj列表移除 remove 'curr' from 'finobj' list
      curr->next = *lastnext;		//将curr链接到tobefnz列表 link at the end of 'tobefnz' list
      *lastnext = curr;
      lastnext = &curr->next;
    }
  }
}


/*
** if object 'o' has a finalizer, remove it from 'allgc' list (must
** search the list to find it) and link it in 'finobj' list.
如果对象'o'有终结器，从'allgc'列表中删除它（必须搜索列表来找到它），并链接到'finobj'列表。
*/
void luaC_checkfinalizer (lua_State *L, GCObject *o, Table *mt) {
  global_State *g = G(L);
  if (tofinalize(o) ||					//对象已经被标记为拥有终结器 obj. is already marked... 
      gfasttm(g, mt, TM_GC) == NULL)	//对象没有终结器 or has no finalizer?
    return;								//直接返回 nothing to be done
  else {								//移动对象到finobj列表 move 'o' to 'finobj' list
    GCObject **p;
    if (issweepphase(g)) {				//GC处于扫描回收阶段
      makewhite(g, o);					//将对象o设置白色与当前Lua虚拟机相同 "sweep" object 'o'
      if (g->sweepgc == &o->next)  /* should not remove 'sweepgc' object */
        g->sweepgc = sweeptolive(L, g->sweepgc);  /* change 'sweepgc' */
    }
    /* 在allgc链表中搜索指向o的指针 search for pointer pointing to 'o' */
    for (p = &g->allgc; *p != o; p = &(*p)->next) { /* empty */ }
    *p = o->next;							//删除o从allgc链表 remove 'o' from 'allgc' list
    o->next = g->finobj;					//将o加入finobj链表 link it in 'finobj' list
    g->finobj = o;
    l_setbit(o->marked, FINALIZEDBIT);		//标记其有终结器 mark it as such
  }
}

/* }====================================================== */



/*
** {======================================================
** GC control
** =======================================================
*/


/*
** Set a reasonable "time" to wait before starting a new GC cycle; cycle
** will start when memory use hits threshold. (Division by 'estimate'
** should be OK: it cannot be zero (because Lua cannot even start with
** less than PAUSEADJ bytes).
在开始新的GC循环之前设置合理的“时间”等待; 循环将在内存使用命中阈值时启动
*/
static void setpause (global_State *g) {
  l_mem threshold, debt;
  l_mem estimate = g->GCestimate / PAUSEADJ;  //调整当前使用的非垃圾内存的估计值 adjust 'estimate'
  lua_assert(estimate > 0);
  threshold = (g->gcpause < MAX_LMEM / estimate)
            ? estimate * g->gcpause			//得到当前内存的最大限制 no overflow
            : MAX_LMEM;				//溢出了，将内存最大值作为限制 overflow; truncate to maximum
  debt = gettotalbytes(g) - threshold;		//总的内存大小-阀值 等于当前GC的负债
  luaE_setdebt(g, debt);						//设置新的GC内存负债
}


/*
** Enter first sweep phase.
** The call to 'sweeplist' tries to make pointer point to an object
** inside the list (instead of to the header), so that the real sweep do
** not need to skip objects created between "now" and the start of the
** real sweep.
该接口通过调用sweeplist接口，回收allgc链表上的节点
*/
static void entersweep (lua_State *L) {
  global_State *g = G(L);
  g->gcstate = GCSswpallgc;	
  lua_assert(g->sweepgc == NULL);
  g->sweepgc = sweeplist(L, &g->allgc, 1);		//表示当前扫描链表的位置
}


void luaC_freeallobjects (lua_State *L) {
  global_State *g = G(L);
  separatetobefnz(g, 1);  /* separate all objects with finalizers */
  lua_assert(g->finobj == NULL);
  callallpendingfinalizers(L);
  lua_assert(g->tobefnz == NULL);
  g->currentwhite = WHITEBITS; /* this "white" makes all objects look dead */
  g->gckind = KGC_NORMAL;
  sweepwholelist(L, &g->finobj);
  sweepwholelist(L, &g->allgc);
  sweepwholelist(L, &g->fixedgc);  /* collect fixed objects */
  lua_assert(g->strt.nuse == 0);
}


static l_mem atomic (lua_State *L) {
  global_State *g = G(L);
  l_mem work;
  GCObject *origweak, *origall;
  GCObject *grayagain = g->grayagain;  //保存grayagain原列表
  lua_assert(g->ephemeron == NULL && g->weak == NULL);
  lua_assert(!iswhite(g->mainthread));
  g->gcstate = GCSinsideatomic;
  g->GCmemtrav = 0;		//开始计数工作 start counting work
  markobject(g, L);		//标记线程
  /*注册表和全局元表可以由API更改 registry and global metatables may be changed by API */
  markvalue(g, &g->l_registry);		//标记注册表
  markmt(g);						//标记全局元表 mark global metatables 
  /* 重新标记临时的upvalues(也许)对死亡线程 remark occasional upvalues of (maybe) dead threads */
  remarkupvals(g);					//重新标记upvalue
  propagateall(g);					//标记变化的部分 propagate changes
  work = g->GCmemtrav;				//停止计数(对grayagain不重新计数) stop counting (do not recount 'grayagain')
  g->gray = grayagain;				//将gray指向grayagain
  propagateall(g);					//重新标记gray		traverse 'grayagain' list
  g->GCmemtrav = 0;					//重新开始计数 restart counting
  convergeephemerons(g);			//标记暂时表
  //在这一点上，所有强烈可访问的对象被标记。at this point, all strongly accessible objects are marked.
  //在检查终结器之前，从弱表中清除值 Clear values from weak tables, before checking finalizers
  clearvalues(g, g->weak, NULL);		//清除存在弱值的弱表
  clearvalues(g, g->allweak, NULL);		//清除弱表
  origweak = g->weak; origall = g->allweak;
  work += g->GCmemtrav;				//停止计数 stop counting (objects being finalized)
  separatetobefnz(g, 0);			//__gc separate objects to be finalized */
  g->gcfinnum = 1;		//可能存在要最终确定的对象 there may be objects to be finalized
  markbeingfnz(g);		//标记userdata   标记为将最终确定的对象mark objects that will be finalized
  propagateall(g);		//重新标记 清空g->gray列表 remark, to propagate 'resurrection'
  g->GCmemtrav = 0;					//重新开始计数 restart counting
  convergeephemerons(g);			//标记暂时表
  /*此时，所有复活的对象都被标记。 at this point, all resurrected objects are marked. */
  /*从弱表中删除死对象 remove dead objects from weak tables */
  clearkeys(g, g->ephemeron, NULL);		//清理keys从所有的暂时表 clear keys from all ephemeron tables
  clearkeys(g, g->allweak, NULL);		//清理keys从所有的weak表 clear keys from all 'allweak' tables
  /* 清除复活的弱表的值 clear values from resurrected weak tables */
  clearvalues(g, g->weak, origweak);
  clearvalues(g, g->allweak, origall);
  luaS_clearcache(g);					//从g->strcache中清除会被删除的对象
  g->currentwhite = cast_byte(otherwhite(g));  //反转Lua虚拟机当前白色 flip current white
  work += g->GCmemtrav;					//完成计数
  return work;  /* estimate of memory marked by 'atomic' */
}


static lu_mem sweepstep (lua_State *L, global_State *g,
                         int nextstate, GCObject **nextlist) {
  if (g->sweepgc) {
    l_mem olddebt = g->GCdebt;
    g->sweepgc = sweeplist(L, g->sweepgc, GCSWEEPMAX);	//遍历sweepgc列表 清除死亡结点
    g->GCestimate += g->GCdebt - olddebt;  /* update estimate */
    if (g->sweepgc)  /* is there still something to sweep? */
      return (GCSWEEPMAX * GCSWEEPCOST);
  }
  //sweepgc清除结束 将其指向一下个列表 状态置为下一个状态
  g->gcstate = nextstate;
  g->sweepgc = nextlist;
  return 0;
}


static lu_mem singlestep (lua_State *L) {
  global_State *g = G(L);
  switch (g->gcstate) {
    case GCSpause: {
      g->GCmemtrav = g->strt.size * sizeof(GCObject*);
      restartcollection(g);			//重新开始GC 设置遍历的root 重置需要用到的列表
      g->gcstate = GCSpropagate;
      return g->GCmemtrav;
    }
    case GCSpropagate: {
      g->GCmemtrav = 0;
      lua_assert(g->gray);
      propagatemark(g);				//将灰色对象标记为黑色
       if (g->gray == NULL)			//没有更多的灰色对象
        g->gcstate = GCSatomic;
      return g->GCmemtrav;			//返回在这一步中遍历的内存大小
    }
    case GCSatomic: {
      lu_mem work;
      propagateall(g);				//确保gray列表为NULL
      work = atomic(L);				//遍历工作是原子的
      entersweep(L);				//回收finobj链表和allgc链表上的节点
      g->GCestimate = gettotalbytes(g);  /* first estimate */;
      return work;
    }
    case GCSswpallgc: {			// sweep "regular" objects
      return sweepstep(L, g, GCSswpfinobj, &g->finobj);
    }
    case GCSswpfinobj: {  /* sweep objects with finalizers */
      return sweepstep(L, g, GCSswptobefnz, &g->tobefnz);
    }
    case GCSswptobefnz: {  /* sweep objects to be finalized */
      return sweepstep(L, g, GCSswpend, NULL);
    }
    case GCSswpend: {  /* finish sweeps */
      makewhite(g, g->mainthread);			//扫描主线程 将其设置当前Lua虚拟机的白色
      checkSizes(L, g);
      g->gcstate = GCScallfin;
      return 0;
    }
    case GCScallfin: {  /* call remaining finalizers */
      if (g->tobefnz && g->gckind != KGC_EMERGENCY) {
        int n = runafewfinalizers(L);
        return (n * GCFINALIZECOST);
      }
      else {  /* emergency mode or no more finalizers */
        g->gcstate = GCSpause;  /* finish collection */
        return 0;
      }
    }
    default: lua_assert(0); return 0;
  }
}


/*
** advances the garbage collector until it reaches a state allowed
** by 'statemask'
每次调用singlestep，实现单步回收，直到出现某个状态时停止回收
*/
void luaC_runtilstate (lua_State *L, int statesmask) {
  global_State *g = G(L);
  while (!testbit(statesmask, g->gcstate))		// !(statesmask & (1<<g->gcstate))
    singlestep(L);
}


/*
** get GC debt and convert it from Kb to 'work units' (avoid zero debt
** and overflows)
*/
static l_mem getdebt (global_State *g) {
  l_mem debt = g->GCdebt;
  int stepmul = g->gcstepmul;
  if (debt <= 0) return 0;  /* minimal debt */
  else {
    debt = (debt / STEPMULADJ) + 1;
    debt = (debt < MAX_LMEM / stepmul) ? debt * stepmul : MAX_LMEM;
    return debt;
  }
}

/*
** performs a basic GC step when collector is running
在GC运行时执行基本GC步骤
*/
void luaC_step (lua_State *L) {
  global_State *g = G(L);
  l_mem debt = getdebt(g);		//得到GC的负载 GC deficit (be paid now)
  if (!g->gcrunning) {					//GC没有运行 not running?
    luaE_setdebt(g, -GCSTEPSIZE * 10);  //避免被频繁调用 avoid being called too often
    return;
  }
  do {  /* repeat until pause or enough "credit" (negative debt) */
    lu_mem work = singlestep(L);	//执行一个单步 perform one single step
    debt -= work;					//执行一个单步后 更新负债
  } while (debt > -GCSTEPSIZE && g->gcstate != GCSpause);
  if (g->gcstate == GCSpause)
    setpause(g);	//设置GC在下一次开始前的等待"时间"即调整其负债
  else {
    debt = (debt / g->gcstepmul) * STEPMULADJ;  //根据gcstepmul得到新的GC负债值 设置下一个步骤的等待"时间"
    luaE_setdebt(g, debt);
    runafewfinalizers(L);
  }
}


/*
** Performs a full GC cycle; if 'isemergency', set a flag to avoid
** some operations which could change the interpreter state in some
** unexpected ways (running finalizers and shrinking some structures).
** Before running the collection, check 'keepinvariant'; if it is true,
** there may be some objects marked as black, so the collector has
** to sweep all objects to turn them back to white (as white has not
** changed, nothing will be collected).
执行完整的GC循环; 如果'isemergency'，设置一个标志，以避免一些操作，可能会以一
些意想不到的方式改变解释器状态（运行终结器和收缩一些结构）。在运行收集之前，检查
'keepinvariant'。 如果它是真的，可能有一些对象标记为黑色，所以收集器必须扫描所
有的对象，将它们变回白色（因为白色没有改变，什么都不会收集）。

LUA_TSTRING, LUA_TTABLE, LUA_TFUNCTION, LUA_TUSERDATA, LUA_TTHREAD 五种对象可以被GC
*/
void luaC_fullgc (lua_State *L, int isemergency) {
  global_State *g = G(L);
  lua_assert(g->gckind == KGC_NORMAL);
  if (isemergency) g->gckind = KGC_EMERGENCY;		//设置GC运行状态为紧急模式
  if (keepinvariant(g)) {	//判断当前的gc阶段状态，如果仍处于标记阶段或初始状态
    entersweep(L);			//进入扫描初级阶段
  }
  //完成任何待定的扫描阶段以开始新的循环
  luaC_runtilstate(L, bitmask(GCSpause));		//直到状态到初始状态GCSpause为止
  luaC_runtilstate(L, ~bitmask(GCSpause));		//直到状态不是GCSpause为止 即开始新的垃圾回收
  luaC_runtilstate(L, bitmask(GCScallfin));		//运行到终止状态
  /* estimate must be correct after a full GC cycle */
  lua_assert(g->GCestimate == gettotalbytes(g));
  luaC_runtilstate(L, bitmask(GCSpause));		//运行到初始状态
  g->gckind = KGC_NORMAL;						//GC运行类型置为普通模式
  setpause(g);									//设置GC在下一次开始前的等待"时间"即调整其负债
}

/* }====================================================== */


