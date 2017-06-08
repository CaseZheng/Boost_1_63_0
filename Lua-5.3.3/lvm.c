/*
** $Id: lvm.c,v 2.268 2016/02/05 19:59:14 roberto Exp $
** Lua virtual machine
** See Copyright Notice in lua.h
*/

#define lvm_c
#define LUA_CORE

#include "lprefix.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lvm.h"


/* limit for table tag-method chains (to avoid loops) */
#define MAXTAGLOOP	2000



/*
** 'l_intfitsf' checks whether a given integer can be converted to a
** float without rounding. Used in comparisons. Left undefined if
** all integers fit in a float precisely.
*/
#if !defined(l_intfitsf)

/* number of bits in the mantissa of a float */
#define NBM		(l_mathlim(MANT_DIG))

/*
** Check whether some integers may not fit in a float, that is, whether
** (maxinteger >> NBM) > 0 (that implies (1 << NBM) <= maxinteger).
** (The shifts are done in parts to avoid shifting by more than the size
** of an integer. In a worst case, NBM == 113 for long double and
** sizeof(integer) == 32.)
*/
#if ((((LUA_MAXINTEGER >> (NBM / 4)) >> (NBM / 4)) >> (NBM / 4)) \
	>> (NBM - (3 * (NBM / 4))))  >  0

#define l_intfitsf(i)  \
  (-((lua_Integer)1 << NBM) <= (i) && (i) <= ((lua_Integer)1 << NBM))

#endif

#endif



/*
** Try to convert a value to a float. The float case is already handled
** by the macro 'tonumber'.
*/
int luaV_tonumber_ (const TValue *obj, lua_Number *n) {
  TValue v;
  if (ttisinteger(obj)) {
    *n = cast_num(ivalue(obj));
    return 1;
  }
  else if (cvt2num(obj) &&  /* string convertible to number? */
            luaO_str2num(svalue(obj), &v) == vslen(obj) + 1) {
    *n = nvalue(&v);  /* convert result of 'luaO_str2num' to a float */
    return 1;
  }
  else
    return 0;  /* conversion failed */
}


/*
转化一个值为整数 根据mode的模式选择
** try to convert a value to an integer, rounding according to 'mode':
** mode == 0: 输入的数是一个整数	如果不是整数则无法转化	accepts only integral values
** mode == 1: 需要向下取整			takes the floor of the number
** mode == 2: 需要向上取整			takes the ceil of the number
*/
int luaV_tointeger (const TValue *obj, lua_Integer *p, int mode) {
  TValue v;
 again:
  if (ttisfloat(obj)) {				//要转化的是float类型
    lua_Number n = fltvalue(obj);
    lua_Number f = l_floor(n);		//向下取整
    if (n != f) {					//原值和向下取整后的值不想等 输入数不是整数
      if (mode == 0) return 0;		//如果只是转化为整数 既不能向上取整也不能向下取整 无法转化返回0
      else if (mode > 1)			//向上取整
        f += 1;						//加一 相当于向上取整
    }
    return lua_numbertointeger(f, p);
  }
  else if (ttisinteger(obj)) {		//要转化的就是一个int类型
    *p = ivalue(obj);
    return 1;
  }
  else if (cvt2num(obj) &&
            luaO_str2num(svalue(obj), &v) == vslen(obj) + 1) {	//是一个字符串
    obj = &v;
    goto again;  /* convert result from 'luaO_str2num' to an integer */
  }
  return 0;  /* conversion failed */
}


/*
** Try to convert a 'for' limit to an integer, preserving the
** semantics of the loop.
** (The following explanation assumes a non-negative step; it is valid
** for negative steps mutatis mutandis.)
** If the limit can be converted to an integer, rounding down, that is
** it.
** Otherwise, check whether the limit can be converted to a number.  If
** the number is too large, it is OK to set the limit as LUA_MAXINTEGER,
** which means no limit.  If the number is too negative, the loop
** should not run, because any initial integer value is larger than the
** limit. So, it sets the limit to LUA_MININTEGER. 'stopnow' corrects
** the extreme case when the initial value is LUA_MININTEGER, in which
** case the LUA_MININTEGER limit would still run the loop once.
*/
static int forlimit (const TValue *obj, lua_Integer *p, lua_Integer step,
                     int *stopnow) {
  *stopnow = 0;  /* usually, let loops run */
  if (!luaV_tointeger(obj, p, (step < 0 ? 2 : 1))) {  /* not fit in integer? */
    lua_Number n;  /* try to convert to float */
    if (!tonumber(obj, &n)) /* cannot convert to float? */
      return 0;  /* not a number */
    if (luai_numlt(0, n)) {  /* if true, float is larger than max integer */
      *p = LUA_MAXINTEGER;
      if (step < 0) *stopnow = 1;
    }
    else {  /* float is smaller than min integer */
      *p = LUA_MININTEGER;
      if (step >= 0) *stopnow = 1;
    }
  }
  return 1;
}


/*
** Finish the table access 'val = t[key]'.
** if 'slot' is NULL, 't' is not a table; otherwise, 'slot' points to
** t[k] entry (which must be nil).
完成表访问'val = t [key]'。 如果'slot'为NULL，'t'不是表; 否则，'slot'指向t [k]项（必须为nil）。
*/
void luaV_finishget (lua_State *L, const TValue *t, TValue *key, StkId val,
                      const TValue *slot) {
  int loop;				//计数器避免陷入无限循环
  const TValue *tm;		//元表
  //元表的处理最多处理MAXTAGLOOP层 主要是为了避免元表的循环引用导致的死循
  for (loop = 0; loop < MAXTAGLOOP; loop++) {
    if (slot == NULL) {							//solt为空
      lua_assert(!ttistable(t));
      tm = luaT_gettmbyobj(L, t, TM_INDEX);		//得到元表index值
      if (ttisnil(tm))							//如果是为nil 报错
        luaG_typeerror(L, t, "index");
    }
    else {										//slot是一个表
      lua_assert(ttisnil(slot));
      tm = fasttm(L, hvalue(t)->metatable, TM_INDEX);		//得到表t的index元表
      if (tm == NULL) {							//没有元表
        setnilvalue(val);						//返回nil
        return;
      }
    }
    if (ttisfunction(tm)) {						//如果取到的值是个函数 调用它
      luaT_callTM(L, tm, t, key, val, 1);
      return;
    }
    t = tm;										//tm不是函数 可能是表 尝试访问tm[key]
    if (luaV_fastget(L,t,key,slot,luaH_get)) {  /* fast track? */
      setobj2s(L, val, slot);  /* done */
      return;
    }
  }
  //循环超出限定 错误
  luaG_runerror(L, "'__index' chain too long; possible loop");
}


/*
** Finish a table assignment 't[key] = val'.
** If 'slot' is NULL, 't' is not a table.  Otherwise, 'slot' points
** to the entry 't[key]', or to 'luaO_nilobject' if there is no such
** entry.  (The value at 'slot' must be nil, otherwise 'luaV_fastset'
** would have done the job.)
*/
void luaV_finishset (lua_State *L, const TValue *t, TValue *key,
                     StkId val, const TValue *slot) {
  int loop;				//计数   防止无限循环
  for (loop = 0; loop < MAXTAGLOOP; loop++) {
    const TValue *tm;				// '__newindex' 元表
    if (slot != NULL) {				// is 't' a table?
      Table *h = hvalue(t);  /* save 't' table */
      lua_assert(ttisnil(slot));  /* old value must be nil */
      tm = fasttm(L, h->metatable, TM_NEWINDEX);  /* get metamethod */
      if (tm == NULL) {  /* no metamethod? */
        if (slot == luaO_nilobject)  /* no previous entry? */
          slot = luaH_newkey(L, h, key);  /* create one */
        /* no metamethod and (now) there is an entry with given key */
        setobj2t(L, cast(TValue *, slot), val);  /* set its new value */
        invalidateTMcache(h);
        luaC_barrierback(L, h, val);
        return;
      }
      /* else will try the metamethod */
    }
    else {  /* not a table; check metamethod */
      if (ttisnil(tm = luaT_gettmbyobj(L, t, TM_NEWINDEX)))
        luaG_typeerror(L, t, "index");
    }
    /* try the metamethod */
    if (ttisfunction(tm)) {
      luaT_callTM(L, tm, t, key, val, 0);
      return;
    }
    t = tm;  /* else repeat assignment over 'tm' */
    if (luaV_fastset(L, t, key, slot, luaH_get, val))
      return;  /* done */
    /* else loop */
  }
  luaG_runerror(L, "'__newindex' chain too long; possible loop");
}


/*
** Compare two strings 'ls' x 'rs', returning an integer smaller-equal-
** -larger than zero if 'ls' is smaller-equal-larger than 'rs'.
** The code is a little tricky because it allows '\0' in the strings
** and it uses 'strcoll' (to respect locales) for each segments
** of the strings.
*/
static int l_strcmp (const TString *ls, const TString *rs) {
  const char *l = getstr(ls);
  size_t ll = tsslen(ls);
  const char *r = getstr(rs);
  size_t lr = tsslen(rs);
  for (;;) {  /* for each segment */
    int temp = strcoll(l, r);
    if (temp != 0)  /* not equal? */
      return temp;  /* done */
    else {  /* strings are equal up to a '\0' */
      size_t len = strlen(l);  /* index of first '\0' in both strings */
      if (len == lr)  /* 'rs' is finished? */
        return (len == ll) ? 0 : 1;  /* check 'ls' */
      else if (len == ll)  /* 'ls' is finished? */
        return -1;  /* 'ls' is smaller than 'rs' ('rs' is not finished) */
      /* both strings longer than 'len'; go on comparing after the '\0' */
      len++;
      l += len; ll -= len; r += len; lr -= len;
    }
  }
}


/*
** Check whether integer 'i' is less than float 'f'. If 'i' has an
** exact representation as a float ('l_intfitsf'), compare numbers as
** floats. Otherwise, if 'f' is outside the range for integers, result
** is trivial. Otherwise, compare them as integers. (When 'i' has no
** float representation, either 'f' is "far away" from 'i' or 'f' has
** no precision left for a fractional part; either way, how 'f' is
** truncated is irrelevant.) When 'f' is NaN, comparisons must result
** in false.
*/
static int LTintfloat (lua_Integer i, lua_Number f) {
#if defined(l_intfitsf)
  if (!l_intfitsf(i)) {
    if (f >= -cast_num(LUA_MININTEGER))  /* -minint == maxint + 1 */
      return 1;  /* f >= maxint + 1 > i */
    else if (f > cast_num(LUA_MININTEGER))  /* minint < f <= maxint ? */
      return (i < cast(lua_Integer, f));  /* compare them as integers */
    else  /* f <= minint <= i (or 'f' is NaN)  -->  not(i < f) */
      return 0;
  }
#endif
  return luai_numlt(cast_num(i), f);  /* compare them as floats */
}


/*
** Check whether integer 'i' is less than or equal to float 'f'.
** See comments on previous function.
检测整数i是否小于或等于float 'f'
*/
static int LEintfloat (lua_Integer i, lua_Number f) {
#if defined(l_intfitsf)
  if (!l_intfitsf(i)) {
    if (f >= -cast_num(LUA_MININTEGER))  /* -minint == maxint + 1 */
      return 1;  /* f >= maxint + 1 > i */
    else if (f >= cast_num(LUA_MININTEGER))  /* minint <= f <= maxint ? */
      return (i <= cast(lua_Integer, f));  /* compare them as integers */
    else  /* f < minint <= i (or 'f' is NaN)  -->  not(i <= f) */
      return 0;
  }
#endif
  return luai_numle(cast_num(i), f);  /* compare them as floats */
}


/*
** Return 'l < r', for numbers.
返回l<r的值 只针对数字
*/
static int LTnum (const TValue *l, const TValue *r) {
  if (ttisinteger(l)) {				//l是int
    lua_Integer li = ivalue(l);
    if (ttisinteger(r))				//r也为int 直接比较
      return li < ivalue(r);
    else							//l是int  r是float
      return LTintfloat(li, fltvalue(r));  /* l < r */
  }
  else {
    lua_Number lf = fltvalue(l);	//l是float
    if (ttisfloat(r))				//r是float 直接比较
      return luai_numlt(lf, fltvalue(r));
    else if (luai_numisnan(lf))		//l是nan 
      return 0;						// NaN < i 必为 false
    else							// r是int l是float 没有 NaN, (l < r)  <-->  not(r <= l)
      return !LEintfloat(ivalue(r), lf);  // not (r <= l)
  }
}


/*
** Return 'l <= r', for numbers.
返回l<=r的值 只针对数字
*/
static int LEnum (const TValue *l, const TValue *r) {
  if (ttisinteger(l)) {
    lua_Integer li = ivalue(l);
    if (ttisinteger(r))
      return li <= ivalue(r);  /* both are integers */
    else  /* 'l' is int and 'r' is float */
      return LEintfloat(li, fltvalue(r));  /* l <= r ? */
  }
  else {
    lua_Number lf = fltvalue(l);  /* 'l' must be float */
    if (ttisfloat(r))
      return luai_numle(lf, fltvalue(r));  /* both are float */
    else if (luai_numisnan(lf))  /* 'r' is int and 'l' is float */
      return 0;  /*  NaN <= i is always false */
    else  /* without NaN, (l <= r)  <-->  not(r < l) */
      return !LTintfloat(ivalue(r), lf);  /* not (r < l) ? */
  }
}


/*
** Main operation less than; return 'l < r'.
主要操作是小于比较 返回 l<r 的值
*/
int luaV_lessthan (lua_State *L, const TValue *l, const TValue *r) {
  int res;
  if (ttisnumber(l) && ttisnumber(r))		//l和r都是number
    return LTnum(l, r);
  else if (ttisstring(l) && ttisstring(r))  /* both are strings? */
    return l_strcmp(tsvalue(l), tsvalue(r)) < 0;
  else if ((res = luaT_callorderTM(L, l, r, TM_LT)) < 0)  /* no metamethod? */
    luaG_ordererror(L, l, r);  /* error */
  return res;
}


/*
** Main operation less than or equal to; return 'l <= r'. If it needs
** a metamethod and there is no '__le', try '__lt', based on
** l <= r iff !(r < l) (assuming a total order). If the metamethod
** yields during this substitution, the continuation has to know
** about it (to negate the result of r<l); bit CIST_LEQ in the call
** status keeps that information.
*/
int luaV_lessequal (lua_State *L, const TValue *l, const TValue *r) {
  int res;
  if (ttisnumber(l) && ttisnumber(r))  /* both operands are numbers? */
    return LEnum(l, r);
  else if (ttisstring(l) && ttisstring(r))  /* both are strings? */
    return l_strcmp(tsvalue(l), tsvalue(r)) <= 0;
  else if ((res = luaT_callorderTM(L, l, r, TM_LE)) >= 0)  /* try 'le' */
    return res;
  else {  /* try 'lt': */
    L->ci->callstatus |= CIST_LEQ;  /* mark it is doing 'lt' for 'le' */
    res = luaT_callorderTM(L, r, l, TM_LT);
    L->ci->callstatus ^= CIST_LEQ;  /* clear mark */
    if (res < 0)
      luaG_ordererror(L, l, r);
    return !res;  /* result is negated */
  }
}


/*
** Main operation for equality of Lua values; return 't1 == t2'.
** L == NULL means raw equality (no metamethods)
主要操作是判断Lua值的相等判断 然后 t1==t2
L == NULl 表示值比较 （没有元方法）
*/
int luaV_equalobj (lua_State *L, const TValue *t1, const TValue *t2) {
  const TValue *tm;
  if (ttype(t1) != ttype(t2)) {  //t1 t2类型不同
    if (ttnov(t1) != ttnov(t2) || ttnov(t1) != LUA_TNUMBER)	 
	  // t1与t2的大类型不同 或者 t1的大类型不是number
      return 0;  //只有数字可以与不同的变量相等
    else {						//两个数字具有不同的小类型
      lua_Integer i1, i2;		//将t1与t2化为整数比较
      return (tointeger(t1, &i1) && tointeger(t2, &i2) && i1 == i2);
    }
  }
  /* values have same type and same variant 值的类型有相同的大类型与小类型*/
  switch (ttype(t1)) {			//得到t1的类型
	//nil int float boolean lightuserdata thread function 类型都是值比较，可以在O(1)下完成。
    case LUA_TNIL: return 1;
    case LUA_TNUMINT: return (ivalue(t1) == ivalue(t2));
    case LUA_TNUMFLT: return luai_numeq(fltvalue(t1), fltvalue(t2));
    case LUA_TBOOLEAN: return bvalue(t1) == bvalue(t2);
    case LUA_TLIGHTUSERDATA: return pvalue(t1) == pvalue(t2);
    case LUA_TLCF: return fvalue(t1) == fvalue(t2);
	//字符串被分为短字符串和长字符串分别处理，短字符串直接比较指针，长字符串则可能触发完整的比较操作。
    case LUA_TSHRSTR: return eqshrstr(tsvalue(t1), tsvalue(t2));			//短字符串比较
    case LUA_TLNGSTR: return luaS_eqlngstr(tsvalue(t1), tsvalue(t2));		//长字符串比较
	//userdata和table都有可能触发比较元方法。按Lua手册中的定义，EQ操作的元方法触发原则是：被
	//比较的两个对象比较有相同的元表，否则认为它们不相等。这个过程由get_equalTM函数保证。
    case LUA_TUSERDATA: {
      if (uvalue(t1) == uvalue(t2)) return 1;		//t1 t2指向同一个地址 相等
      else if (L == NULL) return 0;					//没有元方法 返回0
      tm = fasttm(L, uvalue(t1)->metatable, TM_EQ);		//得到t1元方法
      if (tm == NULL)									//t1元方法不存在 找t2的
        tm = fasttm(L, uvalue(t2)->metatable, TM_EQ);
      break;
    }
    case LUA_TTABLE: {
      if (hvalue(t1) == hvalue(t2)) return 1;
      else if (L == NULL) return 0;
      tm = fasttm(L, hvalue(t1)->metatable, TM_EQ);
      if (tm == NULL)
        tm = fasttm(L, hvalue(t2)->metatable, TM_EQ);
      break;
    }
    default:
      return gcvalue(t1) == gcvalue(t2);
  }
  if (tm == NULL)									//没找到元方法 返回不等
    return 0;
  luaT_callTM(L, tm, t1, t2, L->top, 1);			//调用元方法
  return !l_isfalse(L->top);
}


/* macro used by 'luaV_concat' to ensure that element at 'o' is a string 
“luaV_concat”使用的宏，以确保“o”处的元素是字符串 */
#define tostring(L,o)  \
	(ttisstring(o) || (cvt2str(o) && (luaO_tostring(L, o), 1)))

#define isemptystr(o)	(ttisshrstring(o) && tsvalue(o)->shrlen == 0)

/* copy strings in stack from top - n up to top - 1 to buffer 
将堆栈中的字符串从top-n到top-1复制到缓冲区 */
static void copy2buff (StkId top, int n, char *buff) {
  size_t tl = 0;  /* size already copied */
  do {
    size_t l = vslen(top - n);  /* length of string being copied */
    memcpy(buff + tl, svalue(top - n), l * sizeof(char));
    tl += l;
  } while (--n > 0);
}


/*
** Main operation for concatenation: concat 'total' values in the stack,
** from 'L->top - total' up to 'L->top - 1'.
连接主要操作：在栈中连接total个值 从L->top - total 到 L->top - 1
*/
void luaV_concat (lua_State *L, int total) {
  lua_assert(total >= 2);		//串联的个数大于等于2
  do {
    StkId top = L->top;
    int n = 2;
    if (!(ttisstring(top-2) || cvt2str(top-2)) || !tostring(L, top-1))
	  //两个参数都不是字符串或数字 并且不能转为字符串 尝试元方法
      luaT_trybinTM(L, top-2, top-1, top-2, TM_CONCAT);
    else if (isemptystr(top - 1))			//第二个参数是""
      cast_void(tostring(L, top - 2));		//结果为第一个参数
    else if (isemptystr(top - 2)) {			//第一个参数是""
      setobjs2s(L, top - 2, top - 1);		//结果为第二个参数
    }
    else {
      size_t tl = vslen(top - 1);			//得到第二个参数的长度
      TString *ts;
	  //得到字符串的总长度
      for (n = 1; n < total && tostring(L, top - n - 1); n++) {
        size_t l = vslen(top - n - 1);
        if (l >= (MAX_SIZE/sizeof(char)) - tl)			//字符串长度溢出
          luaG_runerror(L, "string length overflow");
        tl += l;
      }
      if (tl <= LUAI_MAXSHORTLEN) {			//结果是短字符串 is result a short string
        char buff[LUAI_MAXSHORTLEN];
        copy2buff(top, n, buff);			//拷贝栈上n个字符串到buff
        ts = luaS_newlstr(L, buff, tl);	//新建一个字符串 如果原先存在则重用
      }
      else {								//结果是长字符串 将字符串直接复制到最终结果
        ts = luaS_createlngstrobj(L, tl);
        copy2buff(top, n, getstr(ts));		//将栈中n个字符串依次拷贝到长字符串的buff中
      }
      setsvalue2s(L, top - n, ts);			//设置结果
    }
    total -= n-1;  /* got 'n' strings to create 1 new */
    L->top -= n-1;  /* popped 'n' strings and pushed one */
  } while (total > 1);  /* repeat until only 1 result left */
}


/*
** Main operation 'ra' = #rb'.
LEN 操作用于取对象长度，根据Lua的定义：对于字符串取串的长度；对于表，在没有定义元方法时，
取数组部分长度；其它情况调用元len方法。
*/
void luaV_objlen (lua_State *L, StkId ra, const TValue *rb) {
  const TValue *tm;
  switch (ttype(rb)) {
    case LUA_TTABLE: {				//table
      Table *h = hvalue(rb);
      tm = fasttm(L, h->metatable, TM_LEN);
      if (tm) break;				//有元方法调用元方法
      setivalue(ra, luaH_getn(h));	//返回数组部分长度
      return;
    }
    case LUA_TSHRSTR: {				//短字符串
      setivalue(ra, tsvalue(rb)->shrlen);
      return;
    }
    case LUA_TLNGSTR: {				//长字符串
      setivalue(ra, tsvalue(rb)->u.lnglen);
      return;
    }
    default: {
      tm = luaT_gettmbyobj(L, rb, TM_LEN);
      if (ttisnil(tm))				//取到的元方法为nil 报错
        luaG_typeerror(L, rb, "get length of");
      break;
    }
  }
  luaT_callTM(L, tm, rb, rb, ra, 1);	//调用取到的元方法
}


/*
** Integer division; return 'm // n', that is, floor(m/n).
** C division truncates its result (rounds towards zero).
** 'floor(q) == trunc(q)' when 'q >= 0' or when 'q' is integer,
** otherwise 'floor(q) == trunc(q) - 1'.
*/
lua_Integer luaV_div (lua_State *L, lua_Integer m, lua_Integer n) {
  if (l_castS2U(n) + 1u <= 1u) {  /* special cases: -1 or 0 */
    if (n == 0)
      luaG_runerror(L, "attempt to divide by zero");
    return intop(-, 0, m);   /* n==-1; avoid overflow with 0x80000...//-1 */
  }
  else {
    lua_Integer q = m / n;  /* perform C division */
    if ((m ^ n) < 0 && m % n != 0)  /* 'm/n' would be negative non-integer? */
      q -= 1;  /* correct result for different rounding */
    return q;
  }
}


/*
** Integer modulus; return 'm % n'. (Assume that C '%' with
** negative operands follows C99 behavior. See previous comment
** about luaV_div.)
*/
lua_Integer luaV_mod (lua_State *L, lua_Integer m, lua_Integer n) {
  if (l_castS2U(n) + 1u <= 1u) {  /* special cases: -1 or 0 */
    if (n == 0)
      luaG_runerror(L, "attempt to perform 'n%%0'");
    return 0;   /* m % -1 == 0; avoid overflow with 0x80000...%-1 */
  }
  else {
    lua_Integer r = m % n;
    if (r != 0 && (m ^ n) < 0)  /* 'm/n' would be non-integer negative? */
      r += n;  /* correct result for different rounding */
    return r;
  }
}


/* number of bits in an integer */
#define NBITS	cast_int(sizeof(lua_Integer) * CHAR_BIT)

/*
** Shift left operation. (Shift right just negates 'y'.)
*/
lua_Integer luaV_shiftl (lua_Integer x, lua_Integer y) {
  if (y < 0) {  /* shift right? */
    if (y <= -NBITS) return 0;
    else return intop(>>, x, -y);
  }
  else {  /* shift left */
    if (y >= NBITS) return 0;
    else return intop(<<, x, y);
  }
}


/*
** check whether cached closure in prototype 'p' may be reused, that is,
** whether there is a cached closure with the same upvalues needed by
** new closure to be created.
检查原型“p”中的缓存闭包是否可以重用，即是否存在具有创建的新闭包所需的相同upvalues的缓存闭包。
*/
static LClosure *getcached (Proto *p, UpVal **encup, StkId base) {
  LClosure *c = p->cache;
  if (c != NULL) {						//closure缓存 存在
    int nup = p->sizeupvalues;
    Upvaldesc *uv = p->upvalues;
    int i;
    for (i = 0; i < nup; i++) {		//遍历所有的upvalues值
      TValue *v = uv[i].instack ? base + uv[i].idx : encup[uv[i].idx]->v;
      if (c->upvals[i]->v != v)			//不相等 返回null 没有缓存
        return NULL;
    }
  }
  return c;  /* return cached closure (or NULL if no cached closure) */
}


/*
** create a new Lua closure, push it in the stack, and initialize
** its upvalues. Note that the closure is not cached if prototype is
** already black (which means that 'cache' was already cleared by the
** GC).
创建一个新的Lua闭包，将其推入堆栈，并初始化它的upvalue。 注意，如果原型已经是黑色的
（这意味着“cache”已经被GC清除），则不会缓存闭包。
*/
static void pushclosure (lua_State *L, Proto *p, UpVal **encup, StkId base,
                         StkId ra) {
  int nup = p->sizeupvalues;
  Upvaldesc *uv = p->upvalues;
  int i;
  LClosure *ncl = luaF_newLclosure(L, nup);
  ncl->p = p;											//设置closure 函数原型的指向
  setclLvalue(L, ra, ncl);								//将closure加入栈中
  for (i = 0; i < nup; i++) {							//填充closure的upvalue值
    if (uv[i].instack)						//upvalue是局部变量
      ncl->upvals[i] = luaF_findupval(L, base + uv[i].idx);	//upvalue值在栈上
    else									//从传入的值中得到upvalue
      ncl->upvals[i] = encup[uv[i].idx];
    ncl->upvals[i]->refcount++;
    /* new closure is white, so we do not need a barrier here */
  }
  if (!isblack(p))							// cache will not break GC invariant? */
    p->cache = ncl;							//缓存新创建的这个closure
}


/*
** finish execution of an opcode interrupted by an yield
完成有yield中断的操作码的执行
*/
void luaV_finishOp (lua_State *L) {
  CallInfo *ci = L->ci;
  StkId base = ci->u.l.base;
  Instruction inst = *(ci->u.l.savedpc - 1);	//中断指令 interrupted instruction
  OpCode op = GET_OPCODE(inst);
  switch (op) {									//完成执行 finish its execution
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_IDIV:
    case OP_BAND: case OP_BOR: case OP_BXOR: case OP_SHL: case OP_SHR:
    case OP_MOD: case OP_POW:
    case OP_UNM: case OP_BNOT: case OP_LEN:
    case OP_GETTABUP: case OP_GETTABLE: case OP_SELF: {
      setobjs2s(L, base + GETARG_A(inst), --L->top);
      break;
    }
    case OP_LE: case OP_LT: case OP_EQ: {
      int res = !l_isfalse(L->top - 1);
      L->top--;
      if (ci->callstatus & CIST_LEQ) {  /* "<=" using "<" instead? */
        lua_assert(op == OP_LE);
        ci->callstatus ^= CIST_LEQ;  /* clear mark */
        res = !res;  /* negate result */
      }
      lua_assert(GET_OPCODE(*ci->u.l.savedpc) == OP_JMP);
      if (res != GETARG_A(inst))  /* condition failed? */
        ci->u.l.savedpc++;  /* skip jump instruction */
      break;
    }
    case OP_CONCAT: {
      StkId top = L->top - 1;  /* top when 'luaT_trybinTM' was called */
      int b = GETARG_B(inst);      /* first element to concatenate */
      int total = cast_int(top - 1 - (base + b));  /* yet to concatenate */
      setobj2s(L, top - 2, top);  /* put TM result in proper position */
      if (total > 1) {  /* are there elements to concat? */
        L->top = top - 1;  /* top is one after last element (at top-2) */
        luaV_concat(L, total);  /* concat them (may yield again) */
      }
      /* move final result to final position */
      setobj2s(L, ci->u.l.base + GETARG_A(inst), L->top - 1);
      L->top = ci->top;  /* restore top */
      break;
    }
    case OP_TFORCALL: {
      lua_assert(GET_OPCODE(*ci->u.l.savedpc) == OP_TFORLOOP);
      L->top = ci->top;  /* correct top */
      break;
    }
    case OP_CALL: {
      if (GETARG_C(inst) - 1 >= 0)  /* nresults >= 0? */
        L->top = ci->top;  /* adjust results */
      break;
    }
    case OP_TAILCALL: case OP_SETTABUP: case OP_SETTABLE:
      break;
    default: lua_assert(0);
  }
}




/*
** {==================================================================
** Function 'luaV_execute': main interpreter loop
** ===================================================================
*/


/*
** some macros for common tasks in 'luaV_execute'
*/


#define RA(i)	(base+GETARG_A(i))
#define RB(i)	check_exp(getBMode(GET_OPCODE(i)) == OpArgR, base+GETARG_B(i))
#define RC(i)	check_exp(getCMode(GET_OPCODE(i)) == OpArgR, base+GETARG_C(i))
#define RKB(i)	check_exp(getBMode(GET_OPCODE(i)) == OpArgK, \
	ISK(GETARG_B(i)) ? k+INDEXK(GETARG_B(i)) : base+GETARG_B(i))
#define RKC(i)	check_exp(getCMode(GET_OPCODE(i)) == OpArgK, \
	ISK(GETARG_C(i)) ? k+INDEXK(GETARG_C(i)) : base+GETARG_C(i))


/* 无条件跳转 dojump除了偏移savedpc以跳转执行流以外，当A大于0时，还会调用luaF_close关闭A层次的upvalue
跳转地址使用的是相对量，负数表示向前跳转，零表示下一条指令，依次类推
execute a jump instruction */
#define dojump(ci,i,e) \
  { int a = GETARG_A(i); \
    if (a != 0) luaF_close(L, ci->u.l.base + a - 1); \
    ci->u.l.savedpc += GETARG_sBx(i) + e; }

/* 条件跳转 读出下一条指令，其必定是JMP。这里并没有立刻递增savedpc的值。而是让随后的
dojunp对savedpc的偏移多加1。由于dojump是用宏实现的，可以认为多传递一个参数略并不会影响效
率。而合并对savedpc的修改执行效率要略微高一点。
for test instructions, execute the jump instruction that follows it */
#define donextjump(ci)	{ i = *ci->u.l.savedpc; dojump(ci, i, 1); }


#define Protect(x)	{ {x;}; base = ci->u.l.base; }

#define checkGC(L,c)  \
	{ luaC_condGC(L, L->top = (c),  /* limit of live values */ \
                         Protect(L->top = ci->top));  /* restore top */ \
           luai_threadyield(L); }


/* fetch an instruction and prepare its execution */
#define vmfetch()	{ \
  i = *(ci->u.l.savedpc++); \
  if (L->hookmask & (LUA_MASKLINE | LUA_MASKCOUNT)) \
    Protect(luaG_traceexec(L)); \
  ra = RA(i); /* WARNING: any stack reallocation invalidates 'ra' */ \
  lua_assert(base == ci->u.l.base); \
  lua_assert(base <= L->top && L->top < L->stack + L->stacksize); \
}

#define vmdispatch(o)	switch(o)
#define vmcase(l)	case l:
#define vmbreak		break


/*
** copy of 'luaV_gettable', but protecting the call to potential
** metamethod (which can reallocate the stack)
'luaV_gettable'的副本，但保护对潜在元方法的调用（这可以重新分配堆栈）
*/
#define gettableProtected(L,t,k,v)  { const TValue *slot; \
  if (luaV_fastget(L,t,k,slot,luaH_get)) { setobj2s(L, v, slot); } \
  else Protect(luaV_finishget(L,t,k,v,slot)); }


/* same for 'luaV_settable' */
#define settableProtected(L,t,k,v) { const TValue *slot; \
  if (!luaV_fastset(L,t,k,slot,luaH_get,v)) \
    Protect(luaV_finishset(L,t,k,v,slot)); }


/*
luaV_execute是Lua虚拟机执行一段字节码的入口。如果把Lua虚拟机看成一个状态机，它就是从当
前调用栈上次运行点开始解释字节码指令，直到下一个C边界跳出点。所谓C边界跳出点，可以是函数执
行完毕，也可以是一次线程yicld操作。
*/
void luaV_execute (lua_State *L) {
  CallInfo *ci = L->ci;
  LClosure *cl;					//放置调用栈中当前函数对象
  TValue *k;					//这个函数的指令序列
  StkId base;					//当前数据栈底的位置
  ci->callstatus |= CIST_FRESH;  /* fresh invocation of 'luaV_execute" */
 //跳转标签 函数调用OP_CALL OP_TAILCALL OP_RETURN都会回到这里更新栈帧继续运行
 newframe:							
  lua_assert(ci == L->ci);
  cl = clLvalue(ci->func);  /* local reference to function's closure */
  k = cl->p->k;  /* local reference to function's constant table */
  base = ci->u.l.base;  /* local copy of function's base */
  /* main loop of interpreter */
  for (;;) {
    Instruction i;
    StkId ra;
    vmfetch();										//宏 展开
	/*
    i = *(ci->u.l.savedpc++);						//得到当前指令 指令指向下一条
    if (L->hookmask & (LUA_MASKLINE | LUA_MASKCOUNT))
      Protect(luaG_traceexec(L));
	//所有的指令都会操作寄存器A 对Lua虚拟机而言，寄存器即栈上的变量，所以可以将寄存器A
	//所指变量预先取出放到局部变量ra中
    ra = RA(i);
	*/
    vmdispatch (GET_OPCODE(i)) {					//switch		得到操作码
      vmcase(OP_MOVE) {								//寄存器赋值 值的来源 局部变量 其它寄存器
        setobjs2s(L, ra, RB(i));
        vmbreak;
      }
      vmcase(OP_LOADK) {
		//寄存器赋值 值的来源常量 对于数字或字符串 值不可能编码进指令中 Lua为每个函数原型保留一个常量表
		//引用常量时 只需要给出常量表中的索引 OP_LOADK就可以把Bx参数索引的常量加载到A寄存器中 如果常量表
		//过大  索引号超过了Bx可以表达的范围 就使用OP_LOADKX
        TValue *rb = k + GETARG_Bx(i);
        setobj2s(L, ra, rb);
        vmbreak;
      }
      vmcase(OP_LOADKX) {
		//索引号超过OP_LOADK Bx表达的范围 使用OP_LOADKX 索引号放在下一条指令OP_EXTRAARG中
        TValue *rb;
        lua_assert(GET_OPCODE(*ci->u.l.savedpc) == OP_EXTRAARG);
        rb = k + GETARG_Ax(*ci->u.l.savedpc++);
        setobj2s(L, ra, rb);
        vmbreak;
      }
      vmcase(OP_LOADBOOL) {
		//寄存器赋值 值的来源常量 bool类型的数据比较短可以通过指令直接加载 不需要通过常量表
        setbvalue(ra, GETARG_B(i));
        if (GETARG_C(i)) ci->u.l.savedpc++;  /* skip next instruction (if C) */
        vmbreak;
      }
      vmcase(OP_LOADNIL) {
		//寄存器赋值 值的来源常量 nil类型的数据比较短可以通过指令直接加载 不需要通过常量表
		//OP_LOADNIL可以把多个变量同时初始化nil
        int b = GETARG_B(i);
        do {
          setnilvalue(ra++);
        } while (b--);
        vmbreak;
      }
      vmcase(OP_GETUPVAL) {						//读当前的upvalue
        int b = GETARG_B(i);
        setobj2s(L, ra, cl->upvals[b]->v);
        vmbreak;
      }
      vmcase(OP_GETTABUP) {						//读upvalue所指的表中的条目
        TValue *upval = cl->upvals[GETARG_B(i)]->v;
        TValue *rc = RKC(i);
        gettableProtected(L, upval, rc, ra);
        vmbreak;
      }
      vmcase(OP_GETTABLE) {						//读寄存器所指的表中索引所指的值
        StkId rb = RB(i);
        TValue *rc = RKC(i);
        gettableProtected(L, rb, rc, ra);
        vmbreak;
      }
      vmcase(OP_SETTABUP) {						//写upvalue所指的表中的条目
        TValue *upval = cl->upvals[GETARG_A(i)]->v;
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        settableProtected(L, upval, rb, rc);
        vmbreak;
      }
      vmcase(OP_SETUPVAL) {						//写当前的upvalue
        UpVal *uv = cl->upvals[GETARG_B(i)];
        setobj(L, uv->v, ra);
        luaC_upvalbarrier(L, uv);
        vmbreak;
      }
      vmcase(OP_SETTABLE) {						//写寄存器所指的表中索引所指的值
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        settableProtected(L, ra, rb, rc);
        vmbreak;
      }
      vmcase(OP_NEWTABLE) {
        int b = GETARG_B(i);
        int c = GETARG_C(i);
        Table *t = luaH_new(L);
        sethvalue(L, ra, t);
        if (b != 0 || c != 0)
          luaH_resize(L, t, luaO_fb2int(b), luaO_fb2int(c));
        checkGC(L, ra + 1);
        vmbreak;
      }
      vmcase(OP_SELF) {							//语法糖
        const TValue *aux;
        StkId rb = RB(i);
        TValue *rc = RKC(i);
        TString *key = tsvalue(rc);  /* key must be a string */
        setobjs2s(L, ra + 1, rb);
        if (luaV_fastget(L, rb, key, aux, luaH_getstr)) {
          setobj2s(L, ra, aux);
        }
        else Protect(luaV_finishget(L, rb, rc, ra, aux));
        vmbreak;
      }
      vmcase(OP_ADD) {							//'+'加
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        lua_Number nb; lua_Number nc;
        if (ttisinteger(rb) && ttisinteger(rc)) {		//两个相加的数都为int
          lua_Integer ib = ivalue(rb); lua_Integer ic = ivalue(rc);
          setivalue(ra, intop(+, ib, ic));
        }
        else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {		//两个相加的数都可转化为float
          setfltvalue(ra, luai_numadd(L, nb, nc));
        }
        else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_ADD)); }
        vmbreak;
      }
      vmcase(OP_SUB) {							//'-' 减
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        lua_Number nb; lua_Number nc;
        if (ttisinteger(rb) && ttisinteger(rc)) {
          lua_Integer ib = ivalue(rb); lua_Integer ic = ivalue(rc);
          setivalue(ra, intop(-, ib, ic));
        }
        else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {
          setfltvalue(ra, luai_numsub(L, nb, nc));
        }
        else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_SUB)); }
        vmbreak;
      }
      vmcase(OP_MUL) {							//'*' 乘
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        lua_Number nb; lua_Number nc;
        if (ttisinteger(rb) && ttisinteger(rc)) {
          lua_Integer ib = ivalue(rb); lua_Integer ic = ivalue(rc);
          setivalue(ra, intop(*, ib, ic));
        }
        else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {
          setfltvalue(ra, luai_nummul(L, nb, nc));
        }
        else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_MUL)); }
        vmbreak;
      }
      vmcase(OP_DIV) {							//'/' 除
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        lua_Number nb; lua_Number nc;
        if (tonumber(rb, &nb) && tonumber(rc, &nc)) {
          setfltvalue(ra, luai_numdiv(L, nb, nc));
        }
        else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_DIV)); }
        vmbreak;
      }
      vmcase(OP_BAND) {							//'&' 按位与
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        lua_Integer ib; lua_Integer ic;
        if (tointeger(rb, &ib) && tointeger(rc, &ic)) {
          setivalue(ra, intop(&, ib, ic));
        }
        else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_BAND)); }
        vmbreak;
      }
      vmcase(OP_BOR) {							//'|' 按位或
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        lua_Integer ib; lua_Integer ic;
        if (tointeger(rb, &ib) && tointeger(rc, &ic)) {
          setivalue(ra, intop(|, ib, ic));
        }
        else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_BOR)); }
        vmbreak;
      }
      vmcase(OP_BXOR) {							//'~' 按位异或
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        lua_Integer ib; lua_Integer ic;
        if (tointeger(rb, &ib) && tointeger(rc, &ic)) {
          setivalue(ra, intop(^, ib, ic));
        }
        else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_BXOR)); }
        vmbreak;
      }
      vmcase(OP_SHL) {							//'<<' 左移
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        lua_Integer ib; lua_Integer ic;
        if (tointeger(rb, &ib) && tointeger(rc, &ic)) {
          setivalue(ra, luaV_shiftl(ib, ic));
        }
        else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_SHL)); }
        vmbreak;
      }
      vmcase(OP_SHR) {							//'>>' 右移
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        lua_Integer ib; lua_Integer ic;
        if (tointeger(rb, &ib) && tointeger(rc, &ic)) {
          setivalue(ra, luaV_shiftl(ib, -ic));
        }
        else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_SHR)); }
        vmbreak;
      }
      vmcase(OP_MOD) {							//'%' 求余
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        lua_Number nb; lua_Number nc;
        if (ttisinteger(rb) && ttisinteger(rc)) {
          lua_Integer ib = ivalue(rb); lua_Integer ic = ivalue(rc);
          setivalue(ra, luaV_mod(L, ib, ic));
        }
        else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {
          lua_Number m;
          luai_nummod(L, nb, nc, m);
          setfltvalue(ra, m);
        }
        else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_MOD)); }
        vmbreak;
      }
      vmcase(OP_IDIV) {							//'//' 向下取整除法
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        lua_Number nb; lua_Number nc;
        if (ttisinteger(rb) && ttisinteger(rc)) {
          lua_Integer ib = ivalue(rb); lua_Integer ic = ivalue(rc);
          setivalue(ra, luaV_div(L, ib, ic));
        }
        else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {
          setfltvalue(ra, luai_numidiv(L, nb, nc));
        }
        else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_IDIV)); }
        vmbreak;
      }
      vmcase(OP_POW) {							//'^' 次方
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        lua_Number nb; lua_Number nc;
        if (tonumber(rb, &nb) && tonumber(rc, &nc)) {
          setfltvalue(ra, luai_numpow(L, nb, nc));
        }
        else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_POW)); }
        vmbreak;
      }
      vmcase(OP_UNM) {							//'-' 取负
        TValue *rb = RB(i);
        lua_Number nb;
        if (ttisinteger(rb)) {
          lua_Integer ib = ivalue(rb);
          setivalue(ra, intop(-, 0, ib));
        }
        else if (tonumber(rb, &nb)) {
          setfltvalue(ra, luai_numunm(L, nb));
        }
        else {
          Protect(luaT_trybinTM(L, rb, rb, ra, TM_UNM));
        }
        vmbreak;
      }
      vmcase(OP_BNOT) {							//'~' 按位非
        TValue *rb = RB(i);
        lua_Integer ib;
        if (tointeger(rb, &ib)) {
          setivalue(ra, intop(^, ~l_castS2U(0), ib));
        }
        else {
          Protect(luaT_trybinTM(L, rb, rb, ra, TM_BNOT));
        }
        vmbreak;
      }
      vmcase(OP_NOT) {							//not
        TValue *rb = RB(i);
        int res = l_isfalse(rb);  /* next assignment may change this value */
        setbvalue(ra, res);
        vmbreak;
      }
      vmcase(OP_LEN) {							//'#' 求长度
        Protect(luaV_objlen(L, ra, RB(i)));
        vmbreak;
      }
      vmcase(OP_CONCAT) {						//'...' 连接字符串
        int b = GETARG_B(i);
        int c = GETARG_C(i);
        StkId rb;
        L->top = base + c + 1;  /* mark the end of concat operands */
        Protect(luaV_concat(L, c - b + 1));		//连接字符串
        ra = RA(i);  /* 'luaV_concat' may invoke TMs and move the stack */
        rb = base + b;
        setobjs2s(L, ra, rb);
        checkGC(L, (ra >= rb ? ra + 1 : rb));
        L->top = ci->top;  /* restore top */
        vmbreak;
      }
      vmcase(OP_JMP) {							//跳转
        dojump(ci, i, 0);						//无条件跳转
        vmbreak;
      }
      vmcase(OP_EQ) {							//''
        TValue *rb = RKB(i);
        TValue *rc = RKC(i);
        Protect(
          if (luaV_equalobj(L, rb, rc) != GETARG_A(i))		//条件成立 继续执行
            ci->u.l.savedpc++;
          else
            donextjump(ci);									//条件不成立 跳转
        )
        vmbreak;
      }
      vmcase(OP_LT) {							//
        Protect(
          if (luaV_lessthan(L, RKB(i), RKC(i)) != GETARG_A(i))
            ci->u.l.savedpc++;
          else
            donextjump(ci);
        )
        vmbreak;
      }
      vmcase(OP_LE) {
        Protect(
          if (luaV_lessequal(L, RKB(i), RKC(i)) != GETARG_A(i))
            ci->u.l.savedpc++;
          else
            donextjump(ci);
        )
        vmbreak;
      }
      vmcase(OP_TEST) {
        if (GETARG_C(i) ? l_isfalse(ra) : !l_isfalse(ra))
            ci->u.l.savedpc++;
          else
          donextjump(ci);
        vmbreak;
      }
      vmcase(OP_TESTSET) {
        TValue *rb = RB(i);
        if (GETARG_C(i) ? l_isfalse(rb) : !l_isfalse(rb))
          ci->u.l.savedpc++;
        else {
          setobjs2s(L, ra, rb);
          donextjump(ci);
        }
        vmbreak;
      }
      vmcase(OP_CALL) {								//普通函数调用
        int b = GETARG_B(i);						//取到参数
        int nresults = GETARG_C(i) - 1;			//取返回值个数
        if (b != 0) L->top = ra+b;			//b不为0 参数个数为b-1 需要调整栈顶指针 b为0不定参数
        if (luaD_precall(L, ra, nresults)) {		//函数执行前准备 如果是C函数直接执行
          if (nresults >= 0)
            L->top = ci->top;						//调整栈顶 有返回的结果
          Protect((void)0);							//更新base
        }
        else {									//Lua函数 重新开始
          ci = L->ci;
          goto newframe;						//通过新的Lua函数重新开始 luaV_execute
        }
        vmbreak;
      }
      vmcase(OP_TAILCALL) {							//尾调用
        int b = GETARG_B(i);
        if (b != 0) L->top = ra+b;
        lua_assert(GETARG_C(i) - 1 == LUA_MULTRET);
        if (luaD_precall(L, ra, LUA_MULTRET)) {		//函数执行前准备 如果是C函数直接执行
          Protect((void)0);							//更新base
        }
        else {
          /* tail call: put called frame (n) in place of caller one (o) */
          CallInfo *nci = L->ci;  /* called frame */
          CallInfo *oci = nci->previous;  /* caller frame */
          StkId nfunc = nci->func;  /* called function */
          StkId ofunc = oci->func;  /* caller function */
          /* last stack slot filled by 'precall' */
          StkId lim = nci->u.l.base + getproto(nfunc)->numparams;
          int aux;
          /* close all upvalues from previous call 
		  关闭当前栈帧上的upvalue，原本这个步骤应该由return来完成的。但因为发生尾调用时，当前栈帧上的
		  变量已经结束了它们的生命期，并将被新的函数复用空间，所以luaF_close这个操作是需要提前做的。*/
          if (cl->p->sizep > 0) luaF_close(L, oci->u.l.base);
          /* move new frame into old one 
		  将luaD_precall为新一层函数调用生成的调用栈，以及在新一层数据栈上准备好的参数，都复制到当前
		  栈帧上*/
          for (aux = 0; nfunc + aux < lim; aux++)
            setobjs2s(L, ofunc + aux, nfunc + aux);
          oci->u.l.base = ofunc + (nci->u.l.base - nfunc);  /* correct base */
          oci->top = L->top = ofunc + (L->top - nfunc);  /* correct top */
          oci->u.l.savedpc = nci->u.l.savedpc;
          oci->callstatus |= CIST_TAIL;			//标记函数调用类型
          ci = L->ci = oci;					//将ci设置回当前栈帧
          lua_assert(L->top == oci->u.l.base + getproto(ofunc)->maxstacksize);
          goto newframe;						//通过新的Lua函数重新开始 luaV_execute
        }
        vmbreak;
      }
      vmcase(OP_RETURN) {						//return 只针对Lua函数和C函数无关
        int b = GETARG_B(i);
        if (cl->p->sizep > 0) luaF_close(L, base);		//关闭open upvalue
        b = luaD_poscall(L, ci, ra, (b != 0 ? b - 1 : cast_int(L->top - ra)));
        if (ci->callstatus & CIST_FRESH)		//检查标记 如果没有表示该Lua函数从C函数中调用 直接返回
          return;
        else {									//否则 该函数由Lua函数调用 继续执行
          ci = L->ci;
          if (b) L->top = ci->top;				//参数个数明确 重置数据栈顶
          lua_assert(isLua(ci));
          lua_assert(GET_OPCODE(*((ci)->u.l.savedpc - 1)) == OP_CALL);
          goto newframe;						//通过新的Lua函数重新开始 luaV_execute
        }
      }
      vmcase(OP_FORLOOP) {
        if (ttisinteger(ra)) {  /* integer loop? */
          lua_Integer step = ivalue(ra + 2);
          lua_Integer idx = intop(+, ivalue(ra), step); /* increment index */
          lua_Integer limit = ivalue(ra + 1);
          if ((0 < step) ? (idx <= limit) : (limit <= idx)) {
            ci->u.l.savedpc += GETARG_sBx(i);  /* jump back */
            chgivalue(ra, idx);  /* update internal index... */
            setivalue(ra + 3, idx);  /* ...and external index */
          }
        }
        else {  /* floating loop */
          lua_Number step = fltvalue(ra + 2);
          lua_Number idx = luai_numadd(L, fltvalue(ra), step); /* inc. index */
          lua_Number limit = fltvalue(ra + 1);
          if (luai_numlt(0, step) ? luai_numle(idx, limit)
                                  : luai_numle(limit, idx)) {
            ci->u.l.savedpc += GETARG_sBx(i);  /* jump back */
            chgfltvalue(ra, idx);  /* update internal index... */
            setfltvalue(ra + 3, idx);  /* ...and external index */
          }
        }
        vmbreak;
      }
      vmcase(OP_FORPREP) {
        TValue *init = ra;
        TValue *plimit = ra + 1;
        TValue *pstep = ra + 2;
        lua_Integer ilimit;
        int stopnow;
        if (ttisinteger(init) && ttisinteger(pstep) &&
            forlimit(plimit, &ilimit, ivalue(pstep), &stopnow)) {
          /* all values are integer */
          lua_Integer initv = (stopnow ? 0 : ivalue(init));
          setivalue(plimit, ilimit);
          setivalue(init, intop(-, initv, ivalue(pstep)));
        }
        else {  /* try making all values floats */
          lua_Number ninit; lua_Number nlimit; lua_Number nstep;
          if (!tonumber(plimit, &nlimit))
            luaG_runerror(L, "'for' limit must be a number");
          setfltvalue(plimit, nlimit);
          if (!tonumber(pstep, &nstep))
            luaG_runerror(L, "'for' step must be a number");
          setfltvalue(pstep, nstep);
          if (!tonumber(init, &ninit))
            luaG_runerror(L, "'for' initial value must be a number");
          setfltvalue(init, luai_numsub(L, ninit, nstep));
        }
        ci->u.l.savedpc += GETARG_sBx(i);
        vmbreak;
      }
      vmcase(OP_TFORCALL) {
        StkId cb = ra + 3;  /* call base */
        setobjs2s(L, cb+2, ra+2);
        setobjs2s(L, cb+1, ra+1);
        setobjs2s(L, cb, ra);
        L->top = cb + 3;  /* func. + 2 args (state and index) */
        Protect(luaD_call(L, cb, GETARG_C(i)));
        L->top = ci->top;
        i = *(ci->u.l.savedpc++);  /* go to next instruction */
        ra = RA(i);
        lua_assert(GET_OPCODE(i) == OP_TFORLOOP);
        goto l_tforloop;
      }
      vmcase(OP_TFORLOOP) {
        l_tforloop:
        if (!ttisnil(ra + 1)) {  /* continue loop? */
          setobjs2s(L, ra, ra + 1);  /* save control variable */
           ci->u.l.savedpc += GETARG_sBx(i);  /* jump back */
        }
        vmbreak;
      }
      vmcase(OP_SETLIST) {
        int n = GETARG_B(i);
        int c = GETARG_C(i);
        unsigned int last;
        Table *h;
        if (n == 0) n = cast_int(L->top - ra) - 1;
        if (c == 0) {
          lua_assert(GET_OPCODE(*ci->u.l.savedpc) == OP_EXTRAARG);
          c = GETARG_Ax(*ci->u.l.savedpc++);
        }
        h = hvalue(ra);
        last = ((c-1)*LFIELDS_PER_FLUSH) + n;
        if (last > h->sizearray)  /* needs more space? */
          luaH_resizearray(L, h, last);  /* preallocate it at once */
        for (; n > 0; n--) {
          TValue *val = ra+n;
          luaH_setint(L, h, last--, val);
          luaC_barrierback(L, h, val);
        }
        L->top = ci->top;  /* correct top (in case of previous open call) */
        vmbreak;
      }
      vmcase(OP_CLOSURE) {										//Lua closure的创建
        Proto *p = cl->p->p[GETARG_Bx(i)];
        LClosure *ncl = getcached(p, cl->upvals, base);		//查看缓存  有则重用
        if (ncl == NULL)										//没有则创建一个
          pushclosure(L, p, cl->upvals, base, ra);
        else
          setclLvalue(L, ra, ncl);								//将闭包压入栈
        checkGC(L, ra + 1);
        vmbreak;
      }
      vmcase(OP_VARARG) {								//把...参数中的若干个复制到当前栈帧
        int b = GETARG_B(i) - 1;  /* required results */
        int j;
        int n = cast_int(base - ci->func) - cl->p->numparams - 1;		//不定参数的个数
        if (n < 0)
          n = 0;										//没有不定参数
        if (b < 0) {  /* B == 0? */
          b = n;										//得到所有的参数
          Protect(luaD_checkstack(L, n));
          ra = RA(i);									//前一个调用可能会改变堆栈
          L->top = ra + n;
        }
        for (j = 0; j < b && j < n; j++)				//复制参数
          setobjs2s(L, ra + j, base - n + j);
        for (; j < b; j++)								//将剩余返回结果的栈置为nil
          setnilvalue(ra + j);
        vmbreak;
      }
      vmcase(OP_EXTRAARG) {
        lua_assert(0);
        vmbreak;
      }
    }
  }
}

/* }================================================================== */

