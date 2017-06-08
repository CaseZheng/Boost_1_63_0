/*
** $Id: lapi.c,v 2.259 2016/02/29 14:27:14 roberto Exp $
** Lua API
** See Copyright Notice in lua.h
C语言接口
*/

#define lapi_c
#define LUA_CORE

#include "lprefix.h"


#include <stdarg.h>
#include <string.h>

#include "lua.h"

#include "lapi.h"
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
#include "lundump.h"
#include "lvm.h"



const char lua_ident[] =
  "$LuaVersion: " LUA_COPYRIGHT " $"
  "$LuaAuthors: " LUA_AUTHORS " $";


/* value at a non-valid index */
#define NONVALIDVALUE		cast(TValue *, luaO_nilobject)

/* corresponding test */
#define isvalid(o)	((o) != luaO_nilobject)

/* test for pseudo index */
#define ispseudo(i)		((i) <= LUA_REGISTRYINDEX)

/* test for upvalue */
#define isupvalue(i)		((i) < LUA_REGISTRYINDEX)

/* test for valid but not pseudo index */
#define isstackindex(i, o)	(isvalid(o) && !ispseudo(i))

#define api_checkvalidindex(l,o)  api_check(l, isvalid(o), "invalid index")

#define api_checkstackindex(l, i, o)  \
	api_check(l, isstackindex(i, o), "index not in the stack")


static TValue *index2addr (lua_State *L, int idx) {
  CallInfo *ci = L->ci;
  if (idx > 0) {
    TValue *o = ci->func + idx;
    api_check(L, idx <= ci->top - (ci->func + 1), "unacceptable index");
    if (o >= L->top) return NONVALIDVALUE;
    else return o;
  }
  else if (!ispseudo(idx)) {  /* negative index */
    api_check(L, idx != 0 && -idx <= L->top - (ci->func + 1), "invalid index");
    return L->top + idx;
  }
  else if (idx == LUA_REGISTRYINDEX)
    return &G(L)->l_registry;
  else {  /* upvalues */
    idx = LUA_REGISTRYINDEX - idx;
    api_check(L, idx <= MAXUPVAL + 1, "upvalue index too large");
    if (ttislcf(ci->func))  /* light C function? */
      return NONVALIDVALUE;  /* it has no upvalues */
    else {
      CClosure *func = clCvalue(ci->func);
      return (idx <= func->nupvalues) ? &func->upvalue[idx-1] : NONVALIDVALUE;
    }
  }
}


/*
** to be called by 'lua_checkstack' in protected mode, to grow stack
** capturing memory errors
*/
static void growstack (lua_State *L, void *ud) {
  int size = *(int *)ud;
  luaD_growstack(L, size);
}

//确保堆栈上至少有 n 个额外空位。 如果不能把堆栈扩展到相应的尺寸，函数返回假。
//失败的原因包括将把栈扩展到比固定最大尺寸还大 （至少是几千个元素）或分配内存失败。 
//这个函数永远不会缩小堆栈； 如果堆栈已经比需要的大了，那么就保持原样。
LUA_API int lua_checkstack (lua_State *L, int n) {
  int res;
  CallInfo *ci = L->ci;
  lua_lock(L);
  api_check(L, n >= 0, "negative 'n'");
  if (L->stack_last - L->top > n)		//栈上的空间足够
    res = 1;
  else {								//栈上空间不足 需要增长空间
    int inuse = cast_int(L->top - L->stack) + EXTRA_STACK;
    if (inuse > LUAI_MAXSTACK - n)		//栈溢出
      res = 0;
    else								//尝试增大栈空间
      res = (luaD_rawrunprotected(L, &growstack, &n) == LUA_OK);
  }
  if (res && ci->top < L->top + n)
    ci->top = L->top + n;				//调用栈栈顶调整
  lua_unlock(L);
  return res;
}


LUA_API void lua_xmove (lua_State *from, lua_State *to, int n) {
  int i;
  if (from == to) return;
  lua_lock(to);
  api_checknelems(from, n);
  api_check(from, G(from) == G(to), "moving among independent states");
  api_check(from, to->ci->top - to->top >= n, "stack overflow");
  from->top -= n;
  for (i = 0; i < n; i++) {
    setobj2s(to, to->top, from->top + i);
    to->top++;  /* stack already checked by previous 'api_check' */
  }
  lua_unlock(to);
}

//设置一个新的 panic 函数，并返回之前设置的那个
LUA_API lua_CFunction lua_atpanic (lua_State *L, lua_CFunction panicf) {
  lua_CFunction old;
  lua_lock(L);
  old = G(L)->panic;
  G(L)->panic = panicf;
  lua_unlock(L);
  return old;
}

//获得L状态机的版本 将其地址返回	如果L不为空 返回其自带的version
//用户可以在运行时获得传入参数L中version的地址，与自身链入的Lua虚拟机实现代码中的这个变量，
//知道创建这个L的代码是否和自身的代码版本是否一致。这是比版本号比较更强的检查
LUA_API const lua_Number *lua_version (lua_State *L) {
  static const lua_Number version = LUA_VERSION_NUM;
  if (L == NULL) return &version;
  else return G(L)->version;
}



/*
** basic stack manipulation
*/


/*
** convert an acceptable stack index into an absolute index
将一个可接受的索引 idx 转换为绝对索引 （即，一个不依赖栈顶在哪的值）。
*/
LUA_API int lua_absindex (lua_State *L, int idx) {
  return (idx > 0 || ispseudo(idx))
         ? idx
         : cast_int(L->top - L->ci->func) + idx;
}

/*
返回栈顶元素的索引。 因为索引是从 1 开始编号的， 所以这个结果等于栈上的元素个数； 
特别指出，0 表示栈为空
*/
LUA_API int lua_gettop (lua_State *L) {
  return cast_int(L->top - (L->ci->func + 1));
}


LUA_API void lua_settop (lua_State *L, int idx) {
  StkId func = L->ci->func;
  lua_lock(L);
  if (idx >= 0) {
    api_check(L, idx <= L->stack_last - (func + 1), "new top too large");
    while (L->top < (func + 1) + idx)
      setnilvalue(L->top++);
    L->top = (func + 1) + idx;
  }
  else {
    api_check(L, -(idx+1) <= (L->top - (func + 1)), "invalid new top");
    L->top += idx+1;  /* 'subtract' index (index is negative) */
  }
  lua_unlock(L);
}


/*
** Reverse the stack segment from 'from' to 'to'
** (auxiliary to 'lua_rotate')
*/
static void reverse (lua_State *L, StkId from, StkId to) {
  for (; from < to; from++, to--) {
    TValue temp;
    setobj(L, &temp, from);
    setobjs2s(L, from, to);
    setobj2s(L, to, &temp);
  }
}


/*
** Let x = AB, where A is a prefix of length 'n'. Then,
** rotate x n == BA. But BA == (A^r . B^r)^r.
*/
LUA_API void lua_rotate (lua_State *L, int idx, int n) {
  StkId p, t, m;
  lua_lock(L);
  t = L->top - 1;  /* end of stack segment being rotated */
  p = index2addr(L, idx);  /* start of segment */
  api_checkstackindex(L, idx, p);
  api_check(L, (n >= 0 ? n : -n) <= (t - p + 1), "invalid 'n'");
  m = (n >= 0 ? t - n : p - n - 1);  /* end of prefix */
  reverse(L, p, m);  /* reverse the prefix with length 'n' */
  reverse(L, m + 1, t);  /* reverse the suffix */
  reverse(L, p, t);  /* reverse the entire segment */
  lua_unlock(L);
}

/*从索引 fromidx 处复制一个值到一个有效索引 toidx 处，覆盖那里的原有值。
不会影响其它位置的值
*/
LUA_API void lua_copy (lua_State *L, int fromidx, int toidx) {
  TValue *fr, *to;
  lua_lock(L);
  fr = index2addr(L, fromidx);
  to = index2addr(L, toidx);
  api_checkvalidindex(L, to);
  setobj(L, to, fr);
  if (isupvalue(toidx))  /* function upvalue? */
    luaC_barrier(L, clCvalue(L->ci->func), fr);
  /* LUA_REGISTRYINDEX does not need gc barrier
     (collector revisits it before finishing collection) */
  lua_unlock(L);
}


LUA_API void lua_pushvalue (lua_State *L, int idx) {
  lua_lock(L);
  setobj2s(L, L->top, index2addr(L, idx));
  api_incr_top(L);
  lua_unlock(L);
}



/*
** access functions (stack -> C)
*/


LUA_API int lua_type (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  return (isvalid(o) ? ttnov(o) : LUA_TNONE);
}


LUA_API const char *lua_typename (lua_State *L, int t) {
  UNUSED(L);
  api_check(L, LUA_TNONE <= t && t < LUA_NUMTAGS, "invalid tag");
  return ttypename(t);
}


LUA_API int lua_iscfunction (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  return (ttislcf(o) || (ttisCclosure(o)));
}


LUA_API int lua_isinteger (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  return ttisinteger(o);
}


LUA_API int lua_isnumber (lua_State *L, int idx) {
  lua_Number n;
  const TValue *o = index2addr(L, idx);
  return tonumber(o, &n);
}


LUA_API int lua_isstring (lua_State *L, int idx) {
  const TValue *o = index2addr(L, idx);
  return (ttisstring(o) || cvt2str(o));
}


LUA_API int lua_isuserdata (lua_State *L, int idx) {
  const TValue *o = index2addr(L, idx);
  return (ttisfulluserdata(o) || ttislightuserdata(o));
}

/*如果索引 index1 与索引 index2 处的值 本身相等（即不调用元方法），返回 1 。 
否则返回 0 。 当任何一个索引无效时，也返回 0 。*/
LUA_API int lua_rawequal (lua_State *L, int index1, int index2) {
  StkId o1 = index2addr(L, index1);
  StkId o2 = index2addr(L, index2);
  return (isvalid(o1) && isvalid(o2)) ? luaV_rawequalobj(o1, o2) : 0;
}

/*
对栈顶的两个值（或者一个，比如取反）做一次数学或位操作。 其中，栈顶的那个值是第二个操作数。 
它会弹出压入的值，并把结果放在栈顶。 这个函数遵循 Lua 对应的操作符运算规则 （即有可能触发元方法）。
op 的值必须是下列常量中的一个：
 LUA_OPADD: 加法 (+)
 LUA_OPSUB: 减法 (-)
 LUA_OPMUL: 乘法 (*)
 LUA_OPDIV: 浮点除法 (/)
 LUA_OPIDIV: 向下取整的除法 (//)
 LUA_OPMOD: 取模 (%)
 LUA_OPPOW: 乘方 (^)
 LUA_OPUNM: 取负 (一元 -)
 LUA_OPBNOT: 按位取反 (~)
 LUA_OPBAND: 按位与 (&)
 LUA_OPBOR: 按位或 (|)
 LUA_OPBXOR: 按位异或 (~)
 LUA_OPSHL: 左移 (<<)
 LUA_OPSHR: 右移 (>>)
*/
LUA_API void lua_arith (lua_State *L, int op) {
  lua_lock(L);
  if (op != LUA_OPUNM && op != LUA_OPBNOT)
    api_checknelems(L, 2);  /* all other operations expect two operands */
  else {  /* for unary operations, add fake 2nd operand */
    api_checknelems(L, 1);
    setobjs2s(L, L->top, L->top - 1);
    api_incr_top(L);
  }
  /* first operand at top - 2, second at top - 1; result go to top - 2 */
  luaO_arith(L, op, L->top - 2, L->top - 1, L->top - 2);
  L->top--;  /* remove second operand */
  lua_unlock(L);
}

/*
比较两个 Lua 值。 当索引 index1 处的值通过 op 和索引 index2 处的值做比较后条件满足，函数返回 1 
这个函数遵循 Lua 对应的操作规则（即有可能触发元方法）。 反之，函数返回 0。 当任何一个索引无效时，
函数也会返回 0 。
op 值必须是下列常量中的一个：
 LUA_OPEQ: 相等比较 (==)
   LUA_OPLT: 小于比较 (<)
 LUA_OPLE: 小于等于比较 (<=)
*/
LUA_API int lua_compare (lua_State *L, int index1, int index2, int op) {
  StkId o1, o2;
  int i = 0;
  lua_lock(L);  /* may call tag method */
  o1 = index2addr(L, index1);
  o2 = index2addr(L, index2);
  if (isvalid(o1) && isvalid(o2)) {
    switch (op) {
      case LUA_OPEQ: i = luaV_equalobj(L, o1, o2); break;
      case LUA_OPLT: i = luaV_lessthan(L, o1, o2); break;
      case LUA_OPLE: i = luaV_lessequal(L, o1, o2); break;
      default: api_check(L, 0, "invalid option");
    }
  }
  lua_unlock(L);
  return i;
}


LUA_API size_t lua_stringtonumber (lua_State *L, const char *s) {
  size_t sz = luaO_str2num(s, L->top);
  if (sz != 0)
    api_incr_top(L);
  return sz;
}


LUA_API lua_Number lua_tonumberx (lua_State *L, int idx, int *pisnum) {
  lua_Number n;
  const TValue *o = index2addr(L, idx);
  int isnum = tonumber(o, &n);
  if (!isnum)
    n = 0;  /* call to 'tonumber' may change 'n' even if it fails */
  if (pisnum) *pisnum = isnum;
  return n;
}


LUA_API lua_Integer lua_tointegerx (lua_State *L, int idx, int *pisnum) {
  lua_Integer res;
  const TValue *o = index2addr(L, idx);
  int isnum = tointeger(o, &res);
  if (!isnum)
    res = 0;  /* call to 'tointeger' may change 'n' even if it fails */
  if (pisnum) *pisnum = isnum;
  return res;
}


LUA_API int lua_toboolean (lua_State *L, int idx) {
  const TValue *o = index2addr(L, idx);
  return !l_isfalse(o);
}


LUA_API const char *lua_tolstring (lua_State *L, int idx, size_t *len) {
  StkId o = index2addr(L, idx);
  if (!ttisstring(o)) {
    if (!cvt2str(o)) {  /* not convertible? */
      if (len != NULL) *len = 0;
      return NULL;
    }
    lua_lock(L);  /* 'luaO_tostring' may create a new string */
    luaO_tostring(L, o);
    luaC_checkGC(L);
    o = index2addr(L, idx);  /* previous call may reallocate the stack */
    lua_unlock(L);
  }
  if (len != NULL)
    *len = vslen(o);
  return svalue(o);
}

//返回给定索引处值的固有“长度”：
//对于字符串，它指字符串的长度； 
//对于表；它指不触发元方法的情况下取长度操作（'#'）应得到的值； 
//对于用户数据，它指为该用户数据分配的内存块的大小； 
//对于其它值，它为 0 。
LUA_API size_t lua_rawlen (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  switch (ttype(o)) {									//得到元素类型
    case LUA_TSHRSTR: return tsvalue(o)->shrlen;		//短字符串 返回字符串长度
    case LUA_TLNGSTR: return tsvalue(o)->u.lnglen;		//长字符串 返回字符串长度
    case LUA_TUSERDATA: return uvalue(o)->len;			//userdata 返回内存块长度
    case LUA_TTABLE: return luaH_getn(hvalue(o));		//table 返回取长度操作的值
    default: return 0;									//其它返回0
  }
}


LUA_API lua_CFunction lua_tocfunction (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  if (ttislcf(o)) return fvalue(o);
  else if (ttisCclosure(o))
    return clCvalue(o)->f;
  else return NULL;  /* not a C function */
}


LUA_API void *lua_touserdata (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  switch (ttnov(o)) {
    case LUA_TUSERDATA: return getudatamem(uvalue(o));
    case LUA_TLIGHTUSERDATA: return pvalue(o);
    default: return NULL;
  }
}


LUA_API lua_State *lua_tothread (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  return (!ttisthread(o)) ? NULL : thvalue(o);
}


LUA_API const void *lua_topointer (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  switch (ttype(o)) {
    case LUA_TTABLE: return hvalue(o);
    case LUA_TLCL: return clLvalue(o);
    case LUA_TCCL: return clCvalue(o);
    case LUA_TLCF: return cast(void *, cast(size_t, fvalue(o)));
    case LUA_TTHREAD: return thvalue(o);
    case LUA_TUSERDATA: return getudatamem(uvalue(o));
    case LUA_TLIGHTUSERDATA: return pvalue(o);
    default: return NULL;
  }
}



/*
** push functions (C -> stack)
*/


LUA_API void lua_pushnil (lua_State *L) {
  lua_lock(L);
  setnilvalue(L->top);
  api_incr_top(L);
  lua_unlock(L);
}


LUA_API void lua_pushnumber (lua_State *L, lua_Number n) {
  lua_lock(L);
  setfltvalue(L->top, n);
  api_incr_top(L);
  lua_unlock(L);
}


LUA_API void lua_pushinteger (lua_State *L, lua_Integer n) {
  lua_lock(L);
  setivalue(L->top, n);
  api_incr_top(L);
  lua_unlock(L);
}


/*
** Pushes on the stack a string with given length. Avoid using 's' when
** 'len' == 0 (as 's' can be NULL in that case), due to later use of
** 'memcmp' and 'memcpy'.
*/
LUA_API const char *lua_pushlstring (lua_State *L, const char *s, size_t len) {
  TString *ts;
  lua_lock(L);
  ts = (len == 0) ? luaS_new(L, "") : luaS_newlstr(L, s, len);
  setsvalue2s(L, L->top, ts);
  api_incr_top(L);
  luaC_checkGC(L);
  lua_unlock(L);
  return getstr(ts);
}


LUA_API const char *lua_pushstring (lua_State *L, const char *s) {
  lua_lock(L);
  if (s == NULL)
    setnilvalue(L->top);
  else {
    TString *ts;
    ts = luaS_new(L, s);
    setsvalue2s(L, L->top, ts);
    s = getstr(ts);  /* internal copy's address */
  }
  api_incr_top(L);
  luaC_checkGC(L);
  lua_unlock(L);
  return s;
}


LUA_API const char *lua_pushvfstring (lua_State *L, const char *fmt,
                                      va_list argp) {
  const char *ret;
  lua_lock(L);
  ret = luaO_pushvfstring(L, fmt, argp);
  luaC_checkGC(L);
  lua_unlock(L);
  return ret;
}

/*
把一个格式化过的字符串压栈， 然后返回这个字符串的指针。 它和 C 函数 sprintf 比较像，
不过有一些重要的区别：你不需要为结果分配空间： 其结果是一个 Lua 字符串，由 Lua 来关
心其内存分配 （同时通过垃圾收集来释放内存）。这个转换非常的受限。 不支持符号、宽度、
精度。 转换符只支持 '%%' （插入一个字符 '%'）， '%s' （插入一个带零终止符的字符串，没
有长度限制）, '%f' （插入一个 lua_Number）， '%I' （插入一个 lua_Integer）， '%p' 
（插入一个指针或是一个十六进制数）， '%d' （插入一个 int）， '%c' （插入一个用 int 表
示的单字节字符），以及 '%U' （插入一个用 long int 表示的 UTF-8 字）。
*/
LUA_API const char *lua_pushfstring (lua_State *L, const char *fmt, ...) {
  const char *ret;
  va_list argp;				//定义va_list变量
  lua_lock(L);
  va_start(argp, fmt);		//初始化argp
  ret = luaO_pushvfstring(L, fmt, argp);		//将参数组装为字符串放在栈顶
  va_end(argp);				//结束可变参数的获取
  luaC_checkGC(L);
  lua_unlock(L);
  return ret;
}


LUA_API void lua_pushcclosure (lua_State *L, lua_CFunction fn, int n) {
  lua_lock(L);
  if (n == 0) {						//upvalue个数为0
    setfvalue(L->top, fn);			//将栈顶值设为fn
  }
  else {							//upvalue个数不为0
    CClosure *cl;
    api_checknelems(L, n);			//栈上元素个数检测
    api_check(L, n <= MAXUPVAL, "upvalue index too large");		//upvalue个数检测
    cl = luaF_newCclosure(L, n);	//创建一个CClosure对象
    cl->f = fn;						//设置closure函数指向
    L->top -= n;					//设置栈顶指针
    while (n--) {
      setobj2n(L, &cl->upvalue[n], L->top + n);			//将栈中数据拷贝到closure的upvalue数据区
      /* does not need barrier because closure is white */
    }
    setclCvalue(L, L->top, cl);		//将Lua状态机L栈顶置为closure
  }
  api_incr_top(L);					//栈顶+1
  luaC_checkGC(L);
  lua_unlock(L);
}


LUA_API void lua_pushboolean (lua_State *L, int b) {
  lua_lock(L);
  setbvalue(L->top, (b != 0));  /* ensure that true is 1 */
  api_incr_top(L);
  lua_unlock(L);
}


LUA_API void lua_pushlightuserdata (lua_State *L, void *p) {
  lua_lock(L);
  setpvalue(L->top, p);
  api_incr_top(L);
  lua_unlock(L);
}


LUA_API int lua_pushthread (lua_State *L) {
  lua_lock(L);
  setthvalue(L, L->top, L);
  api_incr_top(L);
  lua_unlock(L);
  return (G(L)->mainthread == L);
}



/*
** get functions (Lua -> stack)
*/


static int auxgetstr (lua_State *L, const TValue *t, const char *k) {
  const TValue *slot;
  TString *str = luaS_new(L, k);
  if (luaV_fastget(L, t, str, slot, luaH_getstr)) {
    setobj2s(L, L->top, slot);
    api_incr_top(L);
  }
  else {
    setsvalue2s(L, L->top, str);
    api_incr_top(L);
    luaV_finishget(L, t, L->top - 1, L->top - 1, slot);
  }
  lua_unlock(L);
  return ttnov(L->top - 1);
}

//从全局表中找到name 然后将其压入栈 并返回name的类型
LUA_API int lua_getglobal (lua_State *L, const char *name) {
  Table *reg = hvalue(&G(L)->l_registry);
  lua_lock(L);
  return auxgetstr(L, luaH_getint(reg, LUA_RIDX_GLOBALS), name);
}

/*
把 t[k] 的值压栈， 这里的 t 是指索引指向的值， 而 k 则是栈顶放的值。
这个函数会弹出堆栈上的键，把结果放在栈上相同位置。 和在 Lua 中一样， 
这个函数可能触发对应 "index" 事件的元方法
返回压入值的类型
*/
LUA_API int lua_gettable (lua_State *L, int idx) {
  StkId t;
  lua_lock(L);
  t = index2addr(L, idx);
  luaV_gettabl(L, t, L->top - 1, L->top - 1);
  lua_unlock(L);
  return ttnov(L->top - 1);
}

/*把 t[k] 的值压栈， 这里的 t 是索引指向的值。 在 Lua 中，这个函数可能
触发对应 "index" 事件对应的元方法
函数将返回压入值的类型。*/
LUA_API int lua_getfield (lua_State *L, int idx, const char *k) {
  lua_lock(L);
  return auxgetstr(L, index2addr(L, idx), k);
}

/*
把 t[i] 的值压栈， 这里的 t 指给定的索引指代的值。 和在 Lua 里一样，
这个函数可能会触发 "index" 事件的元方法
返回压入值的类型。
*/
LUA_API int lua_geti (lua_State *L, int idx, lua_Integer n) {
  StkId t;
  const TValue *slot;
  lua_lock(L);
  t = index2addr(L, idx);
  if (luaV_fastget(L, t, n, slot, luaH_getint)) {
    setobj2s(L, L->top, slot);
    api_incr_top(L);
  }
  else {
    setivalue(L->top, n);
    api_incr_top(L);
    luaV_finishget(L, t, L->top - 1, L->top - 1, slot);
  }
  lua_unlock(L);
  return ttnov(L->top - 1);
}

/*类似于 lua_gettable ， 但是作一次直接访问（不触发元方法）。*/
LUA_API int lua_rawget (lua_State *L, int idx) {
  StkId t;
  lua_lock(L);
  t = index2addr(L, idx);
  api_check(L, ttistable(t), "table expected");
  setobj2s(L, L->top - 1, luaH_get(hvalue(t), L->top - 1));
  lua_unlock(L);
  return ttnov(L->top - 1);
}

//把 t[n] 的值压栈， 这里的 t 是指给定索引处的表。 
//这是一次直接访问；就是说，它不会触发元方法。
//返回入栈值的类型。
LUA_API int lua_rawgeti (lua_State *L, int idx, lua_Integer n) {
  StkId t;
  lua_lock(L);
  t = index2addr(L, idx);
  api_check(L, ttistable(t), "table expected");
  setobj2s(L, L->top, luaH_getint(hvalue(t), n));
  api_incr_top(L);
  lua_unlock(L);
  return ttnov(L->top - 1);
}

/*把 t[k] 的值压栈， 这里的 t 是指给定索引处的表， k 是指针 p 对应的轻量用户数据。 
这是一次直接访问；就是说，它不会触发元方法。
返回入栈值的类型。*/
LUA_API int lua_rawgetp (lua_State *L, int idx, const void *p) {
  StkId t;
  TValue k;
  lua_lock(L);
  t = index2addr(L, idx);
  api_check(L, ttistable(t), "table expected");
  setpvalue(&k, cast(void *, p));
  setobj2s(L, L->top, luaH_get(hvalue(t), &k));
  api_incr_top(L);
  lua_unlock(L);
  return ttnov(L->top - 1);
}

//创建一张新的空表压栈。 参数 narray 建议了这张表作为序列使用时会有多少个元素； 
//参数 nrec 建议了这张表可能拥有多少序列之外的元素。 Lua 会使用这些建议来预分配这张新表。
//如果你知道这张表用途的更多信息，预分配可以提高性能。
LUA_API void lua_createtable (lua_State *L, int narray, int nrec) {
  Table *t;
  lua_lock(L);
  t = luaH_new(L);
  sethvalue(L, L->top, t);
  api_incr_top(L);
  if (narray > 0 || nrec > 0)
    luaH_resize(L, t, narray, nrec);
  luaC_checkGC(L);
  lua_unlock(L);
}

/*如果该索引处的值有元表，则将其元表压栈，返回 1 。 否则不会将任何东西入栈，返回 0 。*/
LUA_API int lua_getmetatable (lua_State *L, int objindex) {
  const TValue *obj;
  Table *mt;
  int res = 0;
  lua_lock(L);
  obj = index2addr(L, objindex);
  switch (ttnov(obj)) {
    case LUA_TTABLE:
      mt = hvalue(obj)->metatable;
      break;
    case LUA_TUSERDATA:
      mt = uvalue(obj)->metatable;
      break;
    default:
      mt = G(L)->mt[ttnov(obj)];
      break;
  }
  if (mt != NULL) {
    sethvalue(L, L->top, mt);
    api_incr_top(L);
    res = 1;
  }
  lua_unlock(L);
  return res;
}

/*
将给定索引处的用户数据所关联的 Lua 值压栈。
返回压入值的类型。
*/
LUA_API int lua_getuservalue (lua_State *L, int idx) {
  StkId o;
  lua_lock(L);
  o = index2addr(L, idx);
  api_check(L, ttisfulluserdata(o), "full userdata expected");
  getuservalue(L, uvalue(o), L->top);
  api_incr_top(L);
  lua_unlock(L);
  return ttnov(L->top - 1);
}


/*
** set functions (stack -> Lua)
*/

/*
** t[k] = value at the top of the stack (where 'k' is a string)
*/
static void auxsetstr (lua_State *L, const TValue *t, const char *k) {
  const TValue *slot;
  TString *str = luaS_new(L, k);
  api_checknelems(L, 1);
  if (luaV_fastset(L, t, str, slot, luaH_getstr, L->top - 1))
    L->top--;  /* pop value */
  else {
    setsvalue2s(L, L->top, str);  /* push 'str' (to make it a TValue) */
    api_incr_top(L);
    luaV_finishset(L, t, L->top - 1, L->top - 2, slot);
    L->top -= 2;  /* pop value and key */
  }
  lua_unlock(L);  /* lock done by caller */
}

//从堆栈上弹出一个值，并将其设为全局变量 name 的新值。
LUA_API void lua_setglobal (lua_State *L, const char *name) {
  Table *reg = hvalue(&G(L)->l_registry);
  lua_lock(L);  /* unlock done in 'auxsetstr' */
  auxsetstr(L, luaH_getint(reg, LUA_RIDX_GLOBALS), name);
}


LUA_API void lua_settable (lua_State *L, int idx) {
  StkId t;
  lua_lock(L);
  api_checknelems(L, 2);
  t = index2addr(L, idx);
  luaV_settable(L, t, L->top - 2, L->top - 1);
  L->top -= 2;  /* pop index and value */
  lua_unlock(L);
}

//做一个等价于 t[k] = v 的操作， 这里 t 是给出的索引处的值， 而 v 是栈顶的那个值。
//这个函数将把这个值弹出栈。 跟在 Lua 中一样，这个函数可能触发一个 "newindex" 事件的元方法
LUA_API void lua_setfield (lua_State *L, int idx, const char *k) {
  lua_lock(L);  /* unlock done in 'auxsetstr' */
  auxsetstr(L, index2addr(L, idx), k);
}


LUA_API void lua_seti (lua_State *L, int idx, lua_Integer n) {
  StkId t;
  const TValue *slot;
  lua_lock(L);
  api_checknelems(L, 1);
  t = index2addr(L, idx);
  if (luaV_fastset(L, t, n, slot, luaH_getint, L->top - 1))
    L->top--;  /* pop value */
  else {
    setivalue(L->top, n);
    api_incr_top(L);
    luaV_finishset(L, t, L->top - 1, L->top - 2, slot);
    L->top -= 2;  /* pop value and key */
  }
  lua_unlock(L);
}


LUA_API void lua_rawset (lua_State *L, int idx) {
  StkId o;
  TValue *slot;
  lua_lock(L);
  api_checknelems(L, 2);
  o = index2addr(L, idx);
  api_check(L, ttistable(o), "table expected");
  slot = luaH_set(L, hvalue(o), L->top - 2);
  setobj2t(L, slot, L->top - 1);
  invalidateTMcache(hvalue(o));
  luaC_barrierback(L, hvalue(o), L->top-1);
  L->top -= 2;
  lua_unlock(L);
}

//等价于 t[n] = v ， 这里的 t 是指给定索引处的表， 而 v 是栈顶的值。
//这个函数会将值弹出栈。 赋值是直接的；即不会触发元方法。
LUA_API void lua_rawseti (lua_State *L, int idx, lua_Integer n) {
  StkId o;
  lua_lock(L);
  api_checknelems(L, 1);
  o = index2addr(L, idx);
  api_check(L, ttistable(o), "table expected");
  luaH_setint(L, hvalue(o), n, L->top - 1);
  luaC_barrierback(L, hvalue(o), L->top-1);
  L->top--;
  lua_unlock(L);
}


LUA_API void lua_rawsetp (lua_State *L, int idx, const void *p) {
  StkId o;
  TValue k, *slot;
  lua_lock(L);
  api_checknelems(L, 1);
  o = index2addr(L, idx);
  api_check(L, ttistable(o), "table expected");
  setpvalue(&k, cast(void *, p));
  slot = luaH_set(L, hvalue(o), &k);
  setobj2t(L, slot, L->top - 1);
  luaC_barrierback(L, hvalue(o), L->top - 1);
  L->top--;
  lua_unlock(L);
}


LUA_API int lua_setmetatable (lua_State *L, int objindex) {
  TValue *obj;
  Table *mt;
  lua_lock(L);
  api_checknelems(L, 1);
  obj = index2addr(L, objindex);
  if (ttisnil(L->top - 1))
    mt = NULL;
  else {
    api_check(L, ttistable(L->top - 1), "table expected");
    mt = hvalue(L->top - 1);
  }
  switch (ttnov(obj)) {
    case LUA_TTABLE: {
      hvalue(obj)->metatable = mt;
      if (mt) {
        luaC_objbarrier(L, gcvalue(obj), mt);
        luaC_checkfinalizer(L, gcvalue(obj), mt);
      }
      break;
    }
    case LUA_TUSERDATA: {
      uvalue(obj)->metatable = mt;
      if (mt) {
        luaC_objbarrier(L, uvalue(obj), mt);
        luaC_checkfinalizer(L, gcvalue(obj), mt);
      }
      break;
    }
    default: {
      G(L)->mt[ttnov(obj)] = mt;
      break;
    }
  }
  L->top--;
  lua_unlock(L);
  return 1;
}


LUA_API void lua_setuservalue (lua_State *L, int idx) {
  StkId o;
  lua_lock(L);
  api_checknelems(L, 1);
  o = index2addr(L, idx);
  api_check(L, ttisfulluserdata(o), "full userdata expected");
  setuservalue(L, uvalue(o), L->top - 1);
  luaC_barrier(L, gcvalue(o), L->top - 1);
  L->top--;
  lua_unlock(L);
}


/*
** 'load' and 'call' functions (run Lua code)
*/


#define checkresults(L,na,nr) \
     api_check(L, (nr) == LUA_MULTRET || (L->ci->top - L->top >= (nr) - (na)), \
	"results from function overflow current stack size")

/*调用一个函数。
要调用一个函数请遵循以下协议： 首先，要调用的函数应该被压入栈； 
接着，把需要传递给这个函数的参数按正序压栈； 这是指第一个参数首先压栈。 
最后调用一下 lua_call； nargs 是你压入栈的参数个数。 
当函数调用完毕后，所有的参数以及函数本身都会出栈。 而函数的返回值这时则被压栈。 
返回值的个数将被调整为 nresults 个， 除非 nresults 被设置成 LUA_MULTRET。
在这种情况下，所有的返回值都被压入堆栈中。 Lua 会保证返回值都放入栈空间中。
函数返回值将按正序压栈（第一个返回值首先压栈）， 因此在调用结束后，最后一个返回值将被放在栈顶。
被调用函数内发生的错误将（通过 longjmp ）一直上抛。*/
LUA_API void lua_callk (lua_State *L, int nargs, int nresults,
                        lua_KContext ctx, lua_KFunction k) {
  StkId func;
  lua_lock(L);
  api_check(L, k == NULL || !isLua(L->ci),
    "cannot use continuations inside hooks");
  api_checknelems(L, nargs+1);
  api_check(L, L->status == LUA_OK, "cannot do calls on non-normal thread");
  checkresults(L, nargs, nresults);
  func = L->top - (nargs+1);		//找到函数在栈上的位置
  if (k != NULL && L->nny == 0) {	//如果恢复上下文信息的函数不为空 且 L调用C函数的递归深度为0
    L->ci->u.c.k = k;				//保存恢复上下文信息的函数
    L->ci->u.c.ctx = ctx;			//恢复上下文函数
    luaD_call(L, func, nresults);	//调用函数
  }
  else								//不需要延续 或 挂起
    luaD_callnoyield(L, func, nresults);	//运行函数
  adjustresults(L, nresults);		//根据预期返回的结果数和当前栈顶位置 设置栈顶
  lua_unlock(L);
}



/*
** Execute a protected call.
*/
struct CallS {  /* data to 'f_call' */
  StkId func;
  int nresults;
};


static void f_call (lua_State *L, void *ud) {
  struct CallS *c = cast(struct CallS *, ud);
  luaD_callnoyield(L, c->func, c->nresults);
}

//这个函数的行为和 lua_pcall 完全一致，只不过它还允许被调用的函数让出
LUA_API int lua_pcallk (lua_State *L, int nargs, int nresults, int errfunc,
                        lua_KContext ctx, lua_KFunction k) {
  struct CallS c;
  int status;
  ptrdiff_t func;
  lua_lock(L);
  api_check(L, k == NULL || !isLua(L->ci),
    "cannot use continuations inside hooks");
  api_checknelems(L, nargs+1);
  api_check(L, L->status == LUA_OK, "cannot do calls on non-normal thread");
  checkresults(L, nargs, nresults);
  if (errfunc == 0)									//传入的错误处理函数为空
    func = 0;
  else {
    StkId o = index2addr(L, errfunc);					//
    api_checkstackindex(L, errfunc, o);
    func = savestack(L, o);								//得到o在栈上的偏移
  }
  c.func = L->top - (nargs+1);							//保存要执行函数在栈上的位置
  if (k == NULL || L->nny > 0) {						//没有上下文恢复函数 或者 C函数调用深度大于0
    c.nresults = nresults;							//记录返回结果的个数
    status = luaD_pcall(L, f_call, &c, savestack(L, c.func), func);	//运行函数
  }
  else {  /* prepare continuation (call is already protected by 'resume') */
    CallInfo *ci = L->ci;
    ci->u.c.k = k;							//保存上下文恢复函数
    ci->u.c.ctx = ctx;						//保存上下文信息
    /* save information for error recovery */
    ci->extra = savestack(L, c.func);		//保存当前运行函数到栈底的偏移
    ci->u.c.old_errfunc = L->errfunc;		//将状态机当前错误处理函数记录下来
    L->errfunc = func;						//设置新的错误处理函数
    setoah(ci->callstatus, L->allowhook);  /* save value of 'allowhook' */
    ci->callstatus |= CIST_YPCALL;		//标记函数调用从lua_pcallk或lua_pcall进入
    luaD_call(L, c.func, nresults);	//运行函数
    ci->callstatus &= ~CIST_YPCALL;		//取消标记
    L->errfunc = ci->u.c.old_errfunc;	//恢复当前错误处理函数
    status = LUA_OK;					//设置虚拟机状态OK
  }
  adjustresults(L, nresults);
  lua_unlock(L);
  return status;						//返回执行状态
}

/*
加载一段 Lua 代码块，但不运行它。 如果没有错误， lua_load 把一个编译好的代码块作为一个 Lua 函数压到栈顶。 否则，压入错误消息。
lua_load 的返回值可以是：
 LUA_OK: 没有错误；
 LUA_ERRSYNTAX: 在预编译时碰到语法错误；
 LUA_ERRMEM: 内存分配错误：；
   LUA_ERRGCMM: 在运行 __gc 元方法时出错了。 （这个错误和代码块加载过程无关，它是由垃圾收集器引发的。）
lua_load 函数使用一个用户提供的 reader 函数来读取代码块。 data 参数会被传入 reader 函数。
chunkname 这个参数可以赋予代码块一个名字， 这个名字被用于出错信息和调试信息。
lua_load 会自动检测代码块是文本的还是二进制的， 然后做对应的加载操作。 
字符串 mode 用于控制代码块是文本还是二进制（即预编译代码块） 
它可以是字符串 "b" （只能是二进制代码块）， "t" （只能是文本代码块）， 
或 "bt" （可以是二进制也可以是文本）。 默认值为 "bt"
lua_load 的内部会使用栈， 因此 reader 函数必须永远在每次返回时保留栈的原样。
如果返回的函数有上值， 第一个上值会被设置为 保存在注册表 LUA_RIDX_GLOBALS 索引处的全局环境。
在加载主代码块时，这个上值是 _ENV 变量。 其它上值均被初始化为 nil。
*/
LUA_API int lua_load (lua_State *L, lua_Reader reader, void *data,
                      const char *chunkname, const char *mode) {
  ZIO z;
  int status;
  lua_lock(L);
  if (!chunkname) chunkname = "?";						//代码块名称
  luaZ_init(L, &z, reader, data);							//初始化ZIO
  status = luaD_protectedparser(L, &z, chunkname, mode);	//保护模式解析代码
  if (status == LUA_OK) {								//代码解析正确
    LClosure *f = clLvalue(L->top - 1);					//得到新创建的函数
    if (f->nupvalues >= 1) {							//该函数有upvalue值
      /* get global table from registry */
      Table *reg = hvalue(&G(L)->l_registry);
      const TValue *gt = luaH_getint(reg, LUA_RIDX_GLOBALS);
      /* set global table as 1st upvalue of 'f' (may be LUA_ENV) */
      setobj(L, f->upvals[0]->v, gt);
      luaC_upvalbarrier(L, f->upvals[0]);
    }
  }
  lua_unlock(L);
  return status;
}

/*把函数导出成二进制代码块 。 函数接收栈顶的 Lua 函数做参数， 然后生成它的二进制代码块。 
若被导出的东西被再次加载， 加载的结果就相当于原来的函数。 当它在产生代码块的时候， 
lua_dump 通过调用函数 writer （参见 lua_Writer ） 来写入数据，后面的 data 参数会被传入 writer 。
如果 strip 为真， 二进制代码块将不包含该函数的调试信息。
最后一次由 writer 的返回值将作为这个函数的返回值返回； 0 表示没有错误。
该函数不会把 Lua 函数弹出堆栈*/
LUA_API int lua_dump (lua_State *L, lua_Writer writer, void *data, int strip) {
  int status;
  TValue *o;
  lua_lock(L);
  api_checknelems(L, 1);
  o = L->top - 1;
  if (isLfunction(o))
    status = luaU_dump(L, getproto(o), writer, data, strip);
  else
    status = 1;
  lua_unlock(L);
  return status;
}


LUA_API int lua_status (lua_State *L) {
  return L->status;
}


/*
** Garbage-collection function
控制垃圾收集器
*/

LUA_API int lua_gc (lua_State *L, int what, int data) {
  int res = 0;
  global_State *g;
  lua_lock(L);
  g = G(L);
  switch (what) {
    case LUA_GCSTOP: {
      g->gcrunning = 0;
      break;
    }
    case LUA_GCRESTART: {
      luaE_setdebt(g, 0);
      g->gcrunning = 1;
      break;
    }
    case LUA_GCCOLLECT: {
      luaC_fullgc(L, 0);
      break;
    }
    case LUA_GCCOUNT: {
      //得到GC使用的内存总量 GC values are expressed in Kbytes: #bytes/2^10 */
      res = cast_int(gettotalbytes(g) >> 10);
      break;
    }
    case LUA_GCCOUNTB: {
      res = cast_int(gettotalbytes(g) & 0x3ff);
      break;
    }
    case LUA_GCSTEP: {
      l_mem debt = 1;  /* =1 to signal that it did an actual step */
      lu_byte oldrunning = g->gcrunning;
      g->gcrunning = 1;  /* allow GC to run */
      if (data == 0) {
        luaE_setdebt(g, -GCSTEPSIZE);  /* to do a "small" step */
        luaC_step(L);
      }
      else {  /* add 'data' to total debt */
        debt = cast(l_mem, data) * 1024 + g->GCdebt;
        luaE_setdebt(g, debt);
        luaC_checkGC(L);
      }
      g->gcrunning = oldrunning;  /* restore previous state */
      if (debt > 0 && g->gcstate == GCSpause)  /* end of cycle? */
        res = 1;  /* signal it */
      break;
    }
    case LUA_GCSETPAUSE: {
      res = g->gcpause;
      g->gcpause = data;
      break;
    }
    case LUA_GCSETSTEPMUL: {
      res = g->gcstepmul;
      if (data < 40) data = 40;  /* avoid ridiculous low values (and 0) */
      g->gcstepmul = data;
      break;
    }
    case LUA_GCISRUNNING: {
      res = g->gcrunning;
      break;
    }
    default: res = -1;  /* invalid option */
  }
  lua_unlock(L);
  return res;
}



/*以栈顶的值作为错误对象，抛出一个 Lua 错误。 这个函数将做一次长跳转，所以一定不会返回*/
LUA_API int lua_error (lua_State *L) {
  lua_lock(L);
  api_checknelems(L, 1);
  luaG_errormsg(L);
  /* code unreachable; will unlock when control actually leaves the kernel */
  return 0;  /* to avoid warnings */
}

/*从栈顶弹出一个键， 然后把索引指定的表中的一个键值对压栈 （弹出的键之后的 “下一” 对）。
如果表中以无更多元素， 那么 lua_next 将返回 0 （什么也不压栈）。
在遍历一张表的时候， 不要直接对键调用 lua_tolstring ， 除非你知道这个键一定是一个字符串。 
调用 lua_tolstring 有可能改变给定索引位置的值； 这会对下一次调用 lua_next 造成影响。
关于迭代过程中修改被迭代的表的注意事项参见 next 函数。*/

LUA_API int lua_next (lua_State *L, int idx) {
  StkId t;
  int more;
  lua_lock(L);
  t = index2addr(L, idx);
  api_check(L, ttistable(t), "table expected");
  more = luaH_next(L, hvalue(t), L->top - 1);
  if (more) {
    api_incr_top(L);
  }
  else  /* no more elements */
    L->top -= 1;  /* remove key */
  lua_unlock(L);
  return more;
}

/*连接栈顶的 n 个值， 然后将这些值出栈，并把结果放在栈顶。 如果 n 为 1 ，
结果就是那个值放在栈上（即，函数什么都不做）； 如果 n 为 0 ，结果是一个空串。 
连接依照 Lua 中通常语义完成
*/
LUA_API void lua_concat (lua_State *L, int n) {
  lua_lock(L);
  api_checknelems(L, n);
  if (n >= 2) {
    luaV_concat(L, n);
  }
  else if (n == 0) {	//创建一个空字符串
    setsvalue2s(L, L->top, luaS_newlstr(L, "", 0));
    api_incr_top(L);
  }
  //n为1 什么都不需要做
  luaC_checkGC(L);
  lua_unlock(L);
}

/*返回给定索引的值的长度。 它等价于 Lua 中的 '#' 操作符。 
它有可能触发 "length" 事件对应的元方法。 结果压栈。*/
LUA_API void lua_len (lua_State *L, int idx) {
  StkId t;
  lua_lock(L);
  t = index2addr(L, idx);
  luaV_objlen(L, L->top, t);
  api_incr_top(L);
  lua_unlock(L);
}

/*返回给定状态机的内存分配器函数。 如果 ud 不是 NULL ， 
Lua 把设置内存分配函数时设置的那个指针置入 *ud*/
LUA_API lua_Alloc lua_getallocf (lua_State *L, void **ud) {
  lua_Alloc f;
  lua_lock(L);
  if (ud) *ud = G(L)->ud;
  f = G(L)->frealloc;
  lua_unlock(L);
  return f;
}


LUA_API void lua_setallocf (lua_State *L, lua_Alloc f, void *ud) {
  lua_lock(L);
  G(L)->ud = ud;
  G(L)->frealloc = f;
  lua_unlock(L);
}

/*
这个函数分配一块指定大小的内存块， 把内存块地址作为一个完全用户数据压栈， 并返回这个地址。 宿主程序可以随意使用这块内存
*/
LUA_API void *lua_newuserdata (lua_State *L, size_t size) {
  Udata *u;
  lua_lock(L);
  u = luaS_newudata(L, size);
  setuvalue(L, L->top, u);
  api_incr_top(L);
  luaC_checkGC(L);
  lua_unlock(L);
  return getudatamem(u);
}



static const char *aux_upvalue (StkId fi, int n, TValue **val,
                                CClosure **owner, UpVal **uv) {
  switch (ttype(fi)) {
    case LUA_TCCL: {  /* C closure */
      CClosure *f = clCvalue(fi);
      if (!(1 <= n && n <= f->nupvalues)) return NULL;
      *val = &f->upvalue[n-1];
      if (owner) *owner = f;
      return "";
    }
    case LUA_TLCL: {  /* Lua closure */
      LClosure *f = clLvalue(fi);
      TString *name;
      Proto *p = f->p;
      if (!(1 <= n && n <= p->sizeupvalues)) return NULL;
      *val = f->upvals[n-1]->v;
      if (uv) *uv = f->upvals[n - 1];
      name = p->upvalues[n-1].name;
      return (name == NULL) ? "(*no name)" : getstr(name);
    }
    default: return NULL;  /* not a closure */
  }
}


LUA_API const char *lua_getupvalue (lua_State *L, int funcindex, int n) {
  const char *name;
  TValue *val = NULL;  /* to avoid warnings */
  lua_lock(L);
  name = aux_upvalue(index2addr(L, funcindex), n, &val, NULL, NULL);
  if (name) {
    setobj2s(L, L->top, val);
    api_incr_top(L);
  }
  lua_unlock(L);
  return name;
}


LUA_API const char *lua_setupvalue (lua_State *L, int funcindex, int n) {
  const char *name;
  TValue *val = NULL;  /* to avoid warnings */
  CClosure *owner = NULL;
  UpVal *uv = NULL;
  StkId fi;
  lua_lock(L);
  fi = index2addr(L, funcindex);
  api_checknelems(L, 1);
  name = aux_upvalue(fi, n, &val, &owner, &uv);
  if (name) {
    L->top--;
    setobj(L, val, L->top);
    if (owner) { luaC_barrier(L, owner, L->top); }
    else if (uv) { luaC_upvalbarrier(L, uv); }
  }
  lua_unlock(L);
  return name;
}


static UpVal **getupvalref (lua_State *L, int fidx, int n, LClosure **pf) {
  LClosure *f;
  StkId fi = index2addr(L, fidx);
  api_check(L, ttisLclosure(fi), "Lua function expected");
  f = clLvalue(fi);
  api_check(L, (1 <= n && n <= f->p->sizeupvalues), "invalid upvalue index");
  if (pf) *pf = f;
  return &f->upvals[n - 1];  /* get its upvalue pointer */
}


LUA_API void *lua_upvalueid (lua_State *L, int fidx, int n) {
  StkId fi = index2addr(L, fidx);
  switch (ttype(fi)) {
    case LUA_TLCL: {  /* lua closure */
      return *getupvalref(L, fidx, n, NULL);
    }
    case LUA_TCCL: {  /* C closure */
      CClosure *f = clCvalue(fi);
      api_check(L, 1 <= n && n <= f->nupvalues, "invalid upvalue index");
      return &f->upvalue[n - 1];
    }
    default: {
      api_check(L, 0, "closure expected");
      return NULL;
    }
  }
}


LUA_API void lua_upvaluejoin (lua_State *L, int fidx1, int n1,
                                            int fidx2, int n2) {
  LClosure *f1;
  UpVal **up1 = getupvalref(L, fidx1, n1, &f1);
  UpVal **up2 = getupvalref(L, fidx2, n2, NULL);
  luaC_upvdeccount(L, *up1);
  *up1 = *up2;
  (*up1)->refcount++;
  if (upisopen(*up1)) (*up1)->u.open.touched = 1;
  luaC_upvalbarrier(L, *up1);
}


