/*
** $Id: ltable.c,v 2.117 2015/11/19 19:16:22 roberto Exp $
** Lua tables (hash)
** See Copyright Notice in lua.h
*/

#define ltable_c
#define LUA_CORE

#include "lprefix.h"


/*
** Implementation of tables (aka arrays, objects, or hash tables).
** Tables keep its elements in two parts: an array part and a hash part.
** Non-negative integer keys are all candidates to be kept in the array
** part. The actual size of the array is the largest 'n' such that
** more than half the slots between 1 and n are in use.
** Hash uses a mix of chained scatter table with Brent's variation.
** A main invariant of these tables is that, if an element is not
** in its main position (i.e. the 'original' position that its hash gives
** to it), then the colliding element is in its own main position.
** Hence even when the load factor reaches 100%, performance remains good.
*/

#include <math.h>
#include <limits.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "lvm.h"


/*
** Maximum size of array part (MAXASIZE) is 2^MAXABITS. MAXABITS is
** the largest integer such that MAXASIZE fits in an unsigned int.
*/
#define MAXABITS	cast_int(sizeof(int) * CHAR_BIT - 1)
#define MAXASIZE	(1u << MAXABITS)

/*
** Maximum size of hash part is 2^MAXHBITS. MAXHBITS is the largest
** integer such that 2^MAXHBITS fits in a signed int. (Note that the
** maximum number of elements in a table, 2^MAXABITS + 2^MAXHBITS, still
** fits comfortably in an unsigned int.)
*/
#define MAXHBITS	(MAXABITS - 1)


#define hashpow2(t,n)		(gnode(t, lmod((n), sizenode(t))))

#define hashstr(t,str)		hashpow2(t, (str)->hash)
#define hashboolean(t,p)	hashpow2(t, p)
//得到int类型键i在hash表t中的主键
#define hashint(t,i)		hashpow2(t, i)


/*
** for some types, it is better to avoid modulus by power of 2, as
** they tend to have many 2 factors.
*/
#define hashmod(t,n)	(gnode(t, ((n) % ((sizenode(t)-1)|1))))


#define hashpointer(t,p)	hashmod(t, point2uint(p))


#define dummynode		(&dummynode_)

//与全局空Hash结点比较 地址相等为true
#define isdummy(n)		((n) == dummynode)

//一个不可改写的空结点 当hash表被初始化时空表就指向这个dummy节点  由于其只读所以不会引起线程安全问题
static const Node dummynode_ = {
  {NILCONSTANT},  /* value */
  {{NILCONSTANT, 0}}  /* key */
};


/*
** Hash for floating-point numbers.
** The main computation should be just
**     n = frexp(n, &i); return (n * INT_MAX) + i
** but there are some numerical subtleties.
** In a two-complement representation, INT_MAX does not has an exact
** representation as a float, but INT_MIN does; because the absolute
** value of 'frexp' is smaller than 1 (unless 'n' is inf/NaN), the
** absolute value of the product 'frexp * -INT_MIN' is smaller or equal
** to INT_MAX. Next, the use of 'unsigned int' avoids overflows when
** adding 'i'; the use of '~u' (instead of '-u') avoids problems with
** INT_MIN.
*/
#if !defined(l_hashfloat)
static int l_hashfloat (lua_Number n) {
  int i;
  lua_Integer ni;
  n = l_mathop(frexp)(n, &i) * -cast_num(INT_MIN);
  if (!lua_numbertointeger(n, &ni)) {  /* is 'n' inf/-inf/NaN? */
    lua_assert(luai_numisnan(n) || l_mathop(fabs)(n) == cast_num(HUGE_VAL));
    return 0;
  }
  else {  /* normal case */
    unsigned int u = cast(unsigned int, i) + cast(unsigned int, ni);
    return cast_int(u <= cast(unsigned int, INT_MAX) ? u : ~u);
  }
}
#endif


/*
返回表中元素的主键的位置 即散列表的索引
*/
static Node *mainposition (const Table *t, const TValue *key) {
  switch (ttype(key)) {								//得到key的类型
    case LUA_TNUMINT:								//整型
      return hashint(t, ivalue(key));
    case LUA_TNUMFLT:								//浮点
      return hashmod(t, l_hashfloat(fltvalue(key)));
    case LUA_TSHRSTR:								//短字符串
      return hashstr(t, tsvalue(key));
    case LUA_TLNGSTR:								//如果是长字符串
      return hashpow2(t, luaS_hashlongstr(tsvalue(key)));
    case LUA_TBOOLEAN:								//布尔值
      return hashboolean(t, bvalue(key));
    case LUA_TLIGHTUSERDATA:						//userdata
      return hashpointer(t, pvalue(key));
    case LUA_TLCF:									//C函数
      return hashpointer(t, fvalue(key));
    default:
      lua_assert(!ttisdeadkey(key));
      return hashpointer(t, gcvalue(key));
  }
}


/*
** returns the index for 'key' if 'key' is an appropriate key to live in
** the array part of the table, 0 otherwise.
返回key的索引,如果key是在表的整数部分存在的合适的键 否则返回0
*/
static unsigned int arrayindex (const TValue *key) {
  if (ttisinteger(key)) {							//键必须是整数
    lua_Integer k = ivalue(key);
    if (0 < k && (lua_Unsigned)k <= MAXASIZE)		//键大于0 而且小于MAXASIZE
      return cast(unsigned int, k);					//这个键是合适的键 返回k
  }
  return 0;  //这个键在整数部分是不合适的
}


/*
** returns the index of a 'key' for table traversals. First goes all
** elements in the array part, then elements in the hash part. The
** beginning of a traversal is signaled by 0
返回表遍历的“键”的索引。 首先是数组部分中的所有元素，然后是散列部分中的元素。 
遍历的开始用0表示。
*/
static unsigned int findindex (lua_State *L, Table *t, StkId key) {
  unsigned int i;
  if (ttisnil(key)) return 0;						//如果key是nil 直接返回0
  i = arrayindex(key);								//如果键是整数  并且在数组部分
  if (i != 0 && i <= t->sizearray)					//键是整数部分的 并且合法
    return i;										//返回
  else {
    int nx;
    Node *n = mainposition(t, key);					//得到主键
    for (;;) {										//在链表中查找这个key
      //key相等  或者 结点已经死了 但是 它的next域仍然有效
      if (luaV_rawequalobj(gkey(n), key) ||
            (ttisdeadkey(gkey(n)) && iscollectable(key) &&
             deadvalue(gkey(n)) == gcvalue(key))) {
        i = cast_int(n - gnode(t, 0));				//返回
        return (i + 1) + t->sizearray;				//hash表部分在数组+1后面开始编号
      }
      nx = gnext(n);								//下一个结点的偏移
      if (nx == 0)									//如果找不到 报错
        luaG_runerror(L, "invalid key to 'next'");
      else n += nx;									//设置下一个结点
    }
  }
}

//表的迭代  传入当前键  返回当前键的值 返回下一个键的索引
int luaH_next (lua_State *L, Table *t, StkId key) {
  unsigned int i = findindex(L, t, key);			//得到当前键的索引
  for (; i < t->sizearray; i++) {					//数组部分
    if (!ttisnil(&t->array[i])) {					//如果下标i所在位置不为空
      setivalue(key, i + 1);						//返回i+1
      setobj2s(L, key+1, &t->array[i]);				//返回array[i]
      return 1;
    }
  }
  for (i -= t->sizearray; cast_int(i) < sizenode(t); i++) {		//hash表部分
    if (!ttisnil(gval(gnode(t, i)))) {				//该结点位置的数据不为nil
      setobj2s(L, key, gkey(gnode(t, i)));			//返回下一个键的索引和当前索引的值
      setobj2s(L, key+1, gval(gnode(t, i)));
      return 1;
    }
  }
  return 0;  /* no more elements */
}


/*
** {=============================================================
** Rehash
** ==============================================================
*/

/*
** Compute the optimal size for the array part of table 't'. 'nums' is a
** "count array" where 'nums[i]' is the number of integers in the table
** between 2^(i - 1) + 1 and 2^i. 'pna' enters with the total number of
** integer keys in the table and leaves with the number of keys that
** will go to the array part; return the optimal size.
*/
static unsigned int computesizes (unsigned int nums[], unsigned int *pna) {
  int i;
  unsigned int twotoi;  /* 2^i (candidate for optimal size) */
  unsigned int a = 0;  /* number of elements smaller than 2^i */
  unsigned int na = 0;  /* number of elements to go to array part */
  unsigned int optimal = 0;  /* optimal size for array part */
  /* loop while keys can fill more than half of total size */
  for (i = 0, twotoi = 1; *pna > twotoi / 2; i++, twotoi *= 2) {
    if (nums[i] > 0) {
      a += nums[i];
      if (a > twotoi/2) {  /* more than half elements present? */
        optimal = twotoi;  /* optimal size (till now) */
        na = a;  /* all elements up to 'optimal' will go to array part */
      }
    }
  }
  lua_assert((optimal == 0 || optimal / 2 < na) && na <= optimal);
  *pna = na;
  return optimal;
}


static int countint (const TValue *key, unsigned int *nums) {
  unsigned int k = arrayindex(key);		//得到key在数组部分是否合适
  if (k != 0) {  //不等于0  这个键是合适的
    nums[luaO_ceillog2(k)]++;  //数目+1
    return 1;
  }
  else			//否则是不合适的
    return 0;
}


/*
** Count keys in array part of table 't': Fill 'nums[i]' with
** number of keys that will go into corresponding slice and return
** total number of non-nil keys.
*/
static unsigned int numusearray (const Table *t, unsigned int *nums) {
  int lg;
  unsigned int ttlg;  /* 2^lg */
  unsigned int ause = 0;  /* summation of 'nums' */
  unsigned int i = 1;  /* count to traverse all array keys */
  /* traverse each slice */
  for (lg = 0, ttlg = 1; lg <= MAXABITS; lg++, ttlg *= 2) {
    unsigned int lc = 0;  /* counter */
    unsigned int lim = ttlg;
    if (lim > t->sizearray) {
      lim = t->sizearray;  /* adjust upper limit */
      if (i > lim)
        break;  /* no more elements to count */
    }
    /* count elements in range (2^(lg - 1), 2^lg] */
    for (; i <= lim; i++) {
      if (!ttisnil(&t->array[i-1]))
        lc++;
    }
    nums[lg] += lc;
    ause += lc;
  }
  return ause;
}


static int numusehash (const Table *t, unsigned int *nums, unsigned int *pna) {
  int totaluse = 0;  /* total number of elements */
  int ause = 0;  /* elements added to 'nums' (can go to array part) */
  int i = sizenode(t);
  while (i--) {
    Node *n = &t->node[i];
    if (!ttisnil(gval(n))) {
      ause += countint(gkey(n), nums);
      totaluse++;
    }
  }
  *pna += ause;
  return totaluse;
}

//调整数组大小为size
static void setarrayvector (lua_State *L, Table *t, unsigned int size) {
  unsigned int i;
  luaM_reallocvector(L, t->array, t->sizearray, size, TValue);		//调整数组大小
  for (i=t->sizearray; i<size; i++)								//将新分配的部分置为nil
     setnilvalue(&t->array[i]);
  t->sizearray = size;												//设置数组部分的大小
}

//调整table hash表部分的尺寸
static void setnodevector (lua_State *L, Table *t, unsigned int size) {
  int lsize;
  if (size == 0) {						//size为0  没有hash表部分
    t->node = cast(Node *, dummynode);  //hash表部分指向 全局只读hash表 dummynode 
    lsize = 0;
  }
  else {
    int i;
    lsize = luaO_ceillog2(size);						//log2(size)
    if (lsize > MAXHBITS)								//hash表的大小限制
      luaG_runerror(L, "table overflow");
    size = twoto(lsize);								//2^lsize
    t->node = luaM_newvector(L, size, Node);			//为table创建一个新的hash表
    for (i = 0; i < (int)size; i++) {					//初始化hash表
      Node *n = gnode(t, i);
      gnext(n) = 0;
      setnilvalue(wgkey(n));
      setnilvalue(gval(n));
    }
  }
  t->lsizenode = cast_byte(lsize);						//重置hash表的大小
  t->lastfree = gnode(t, size);							//记录空闲结点的位置起点
}

//table 内存大小调整 t为要调整内存的表 nasize数组部分大小 nhsize hash表部分大小
void luaH_resize (lua_State *L, Table *t, unsigned int nasize,
                                          unsigned int nhsize) {
  unsigned int i;
  int j;
  unsigned int oldasize = t->sizearray;		//table数组部分的原本大小
  int oldhsize = t->lsizenode;					//table hash表部分的原本大小
  Node *nold = t->node;							//原先hash表头
  if (nasize > oldasize)						//要调整的表的整数部分的尺寸大于旧的大小  必须调整
    setarrayvector(L, t, nasize);				//
  //创建一个新的hash表 用传入的尺寸
  setnodevector(L, t, nhsize);
  if (nasize < oldasize) {					//表的数组部分缩小
    t->sizearray = nasize;						//重新设置表的数组部分的大小
    /* re-insert elements from vanishing slice */
    for (i=nasize; i<oldasize; i++) {			//遍历被舍弃的部分
      if (!ttisnil(&t->array[i]))				//如果该位置非空
        luaH_setint(L, t, i + 1, &t->array[i]);		//设置t[i+1] = t->array[i]  将数组中的值可以放入hash表中
    }
    //缩小数组
    luaM_reallocvector(L, t->array, oldasize, nasize, TValue);
  }
  //重新插入hash表部分的元素
  for (j = twoto(oldhsize) - 1; j >= 0; j--) {
    Node *old = nold + j;						//得到老的hash表的主键链表
    if (!ttisnil(gval(old))) {					//如果不为空
      /* doesn't need barrier/invalidate cache, as entry was already present in the table */
	  //先得到key对于的值的指针 再将旧值复制过去 最后检查有效性
      setobjt2t(L, luaH_set(L, t, gkey(old)), gval(old));		
    }
  }
  if (!isdummy(nold))							//旧的hash表不为空
    luaM_freearray(L, nold, cast(size_t, twoto(oldhsize)));		//释放旧的hash表的控件
}


void luaH_resizearray (lua_State *L, Table *t, unsigned int nasize) {
  int nsize = isdummy(t->node) ? 0 : sizenode(t);
  luaH_resize(L, t, nasize, nsize);
}

/*
** nums[i] = number of keys 'k' where 2^(i - 1) < k <= 2^i
rehash的主要工作是统计当前table中到底有多少有效键值对，以及决定数组部分需要开辟多少空间。
其原则是最终数组部分的利用率需要超过50%。
lua使用一个rehash函数中定义在栈上的nums数组来做这个整数键统计工作。这个数组按2的整数幂
次来分开统计各个区段间的整数键个数。统计过程的实现见numusearray和numusehash函数。
最终，computesizes函数计算出不低于50%利用率下，数组该维持多少空间。同时，还可以得到有多少
有效键将被储存在哈希表里。
根据这些统计数据，rehash函数调用luaH_resize 这个api来重新调整数组部分和哈希部分的大小，并
把不能放在数组里的键值对重新塞入哈希表。
*/
static void rehash (lua_State *L, Table *t, const TValue *ek) {
  unsigned int asize;	//数组部分的最佳大小
  unsigned int na;		//数组部分key的数量
  unsigned int nums[MAXABITS + 1];
  int i;
  int totaluse;
  for (i = 0; i <= MAXABITS; i++) nums[i] = 0;		//初始化
  na = numusearray(t, nums);	//统计数组部分键的数量
  totaluse = na;				//总键数目
  totaluse += numusehash(t, nums, &na);			//统计hash表部分键的数量并加入总数中
  /* count extra key */
  na += countint(ek, nums);						//判断ek是否适合放在数组部分 适合返回1 不适合返回0
  totaluse++;										//总的键数目+1
  //为数组部分计算一个合适的尺寸
  asize = computesizes(nums, &na);
  //调整table的尺寸按照计算出来的
  luaH_resize(L, t, asize, totaluse - na);
}



/*
** }=============================================================
*/

//创建新的table
Table *luaH_new (lua_State *L) {
  GCObject *o = luaC_newobj(L, LUA_TTABLE, sizeof(Table));
  Table *t = gco2t(o);		//	(GCUnion*)(o)->h
  t->metatable = NULL;
  t->flags = cast_byte(~0);
  t->array = NULL;
  t->sizearray = 0;
  setnodevector(L, t, 0);	//初始化hash表部分
  return t;
}

//释放一个hash表
void luaH_free (lua_State *L, Table *t) {
  if (!isdummy(t->node))										//如果表t的hash部分不是指向空的hash表 释放它
    luaM_freearray(L, t->node, cast(size_t, sizenode(t)));		//释放hash表部分
  luaM_freearray(L, t->array, t->sizearray);					//释放数组部分
  luaM_free(L, t);												//释放表本身
}

//在表中找一个空闲的主键位置
static Node *getfreepos (Table *t) {
  while (t->lastfree > t->node) {			//如果lastfree大于hash数组 未越界
    t->lastfree--;							//lastfree指针移动一位
    if (ttisnil(gkey(t->lastfree)))			//该位置键是nil
      return t->lastfree;					//返回这个位置
  }
  return NULL;								//返回NULL 在表中未找到空闲的主键位置
}



/*
** inserts a new key into a hash table; first, check whether key's main
** position is free. If not, check whether colliding node is in its main
** position or not: if it is not, move colliding node to an empty place and
** put new key in its main position; otherwise (colliding node is in its main
** position), new key goes to an empty position.
*/
//只负责在哈希表中创建出一个不存在的键，而不关数组部分的工作
TValue *luaH_newkey (lua_State *L, Table *t, const TValue *key) {
  Node *mp;
  TValue aux;
  if (ttisnil(key)) luaG_runerror(L, "table index is nil");		//要创建的键key是个nil  报错
  else if (ttisfloat(key)) {									//是float类型的
    lua_Integer k;
    if (luaV_tointeger(key, &k, 0)) {							//转为整型
      setivalue(&aux, k);										
      key = &aux;												//将key设为转化后的整数
    }
    else if (luai_numisnan(fltvalue(key)))						//如果key是NaN 报错
      luaG_runerror(L, "table index is NaN");
  }
  mp = mainposition(t, key);									//得到key在hash表中主键头节点 即链表头
  if (!ttisnil(gval(mp)) || isdummy(mp)) {						//如果该主键它的值不是nil 或者 指向一个空结点
    Node *othern;
    Node *f = getfreepos(t);									//找一个空闲的结点
    if (f == NULL) {											//表中没有空闲结点了
      rehash(L, t, key);										//调整表的大小
      return luaH_set(L, t, key);								//将key插入到表中
    }
    lua_assert(!isdummy(f));
    othern = mainposition(t, gkey(mp));						//根据头结点的key值得到新的主键
    if (othern != mp) {										//如果两个主键不一致  可能冲突了
	  //新键占据这个位置，而老键更换到新位置并根据它的主键找到属于它的链的那条单向链表中上一个结点，重新链入。
      while (othern + gnext(othern) != mp)  /* find previous */
        othern += gnext(othern);
      gnext(othern) = cast_int(f - othern);					//将新分配的f结点加入到othern链表中
	  //拷贝所有 包括next域
      *f = *mp;
      if (gnext(mp) != 0) {										//如果mp的next不为0  则要将mp的next链在f后面
		//由于使用偏移 所以f的next域并不能指向mp的next本该指向的位置 需要再加上mp和f之间的偏移
        gnext(f) += cast_int(mp - f);
        gnext(mp) = 0;											//将mp的next设置0 mp现在就是一个自由的结点
      }
      setnilvalue(gval(mp));									//将结点mp的值设为nil
    }
    else {														//两个的主键是一样的
      //将新键链入主键的链表中
      if (gnext(mp) != 0)										//链表不为空
        gnext(f) = cast_int((mp + gnext(mp)) - f);				//将f->next指向mp->next
      else lua_assert(gnext(f) == 0);
      gnext(mp) = cast_int(f - mp);								//mp->next指向f
      mp = f;													//将mp指向f 即mp指向新加的结点
    }
  }
  setnodekey(L, &mp->i_key, key);								//设置结点的mp->i_key的值为key
  luaC_barrierback(L, t, key);
  lua_assert(ttisnil(gval(mp)));
  return gval(mp);												//将mp的值的指针返回
}


/*
整数的搜索函数
*/
const TValue *luaH_getint (Table *t, lua_Integer key) {
  /* (1 <= key && key <= t->sizearray) */
  if (l_castS2U(key) - 1 < t->sizearray)		//key在整数数组的范围内
    return &t->array[key - 1];					//返回所在位置的指针
  else {
    Node *n = hashint(t, key);			//得到该整数的在hash表中的hash值对应的主键 即链表
    for (;;) {							//遍历该链表
      if (ttisinteger(gkey(n)) && ivalue(gkey(n)) == key)	//如果该节点是整数 而且 值相等
        return gval(n);					//返回该节点值的指针
      else {
        int nx = gnext(n);				//下一个结点的偏移
        if (nx == 0) break;				//偏移为0  遍历结束
        n += nx;						//下一个节点 = 当前位置+下一个节点的偏移 
      }
    }
    return luaO_nilobject;				//返回空值
  }
}


/*
在hash表中查找元素 key为短字符串
*/
const TValue *luaH_getshortstr (Table *t, TString *key) {
  Node *n = hashstr(t, key);				//得到存储key的链表头
  lua_assert(key->tt == LUA_TSHRSTR);
  for (;;) {								//检查'key'在链表中
    const TValue *k = gkey(n);
    if (ttisshrstring(k) && eqshrstr(tsvalue(k), key))	//当前节点所存储的也是短字符串 而且 两个对象地址相同
      return gval(n);						//找到了 返回值
    else {
      int nx = gnext(n);					//找下一个节点
      if (nx == 0)							//下一个节点为0
        return luaO_nilobject;				//没找到  返回空对象
      n += nx;
    }
  }
}


/*
hash表 通用的查找函数, (key不能是int 其可以存储在整数部分 对可以转化为浮点数的整数无效其也可以存储在整数部分)
*/
static const TValue *getgeneric (Table *t, const TValue *key) {
  Node *n = mainposition(t, key);					//根据key得到主键
  for (;;) {  /* check whether 'key' is somewhere in the chain */
    if (luaV_rawequalobj(gkey(n), key))				//如果键相等 返回值
      return gval(n);
    else {
      int nx = gnext(n);							//得到下一个结点的偏移
      if (nx == 0)
        return luaO_nilobject;						//遍历完毕 返回nil
      n += nx;										//偏移到下一个结点
    }
  }
}

//查找一个key为字符串的键值对的值
const TValue *luaH_getstr (Table *t, TString *key) {
  if (key->tt == LUA_TSHRSTR)						//短字符串
    return luaH_getshortstr(t, key);
  else {											//长字符串使用通用版本
    TValue ko;
    setsvalue(cast(lua_State *, NULL), &ko, key);
    return getgeneric(t, &ko);
  }
}


/*
在table中查找元素
*/
const TValue *luaH_get (Table *t, const TValue *key) {
  switch (ttype(key)) {												//得到key的类型
    case LUA_TSHRSTR: return luaH_getshortstr(t, tsvalue(key));		//短字符串
    case LUA_TNUMINT: return luaH_getint(t, ivalue(key));			//int
    case LUA_TNIL: return luaO_nilobject;							//nil	直接返回一个空对象
    case LUA_TNUMFLT: {												//float
      lua_Integer k;
      if (luaV_tointeger(key, &k, 0))								//没有小数可以转化为int
        return luaH_getint(t, k);									//调用查找key为int的函数
      /* else... */
    }  /* FALLTHROUGH */
    default:
      return getgeneric(t, key);									//在hash表中查找 通用版本
  }
}


/*
** beware: when using this function you probably need to check a GC
** barrier and invalidate the TM cache.
设置一个键返回其值 存在则直接返回它的值  不存在创建一个返回
*/
TValue *luaH_set (lua_State *L, Table *t, const TValue *key) {
  const TValue *p = luaH_get(t, key);				//先查找这个key对于的值
  if (p != luaO_nilobject)							//不为空
    return cast(TValue *, p);						//返回这个值的指针
  else return luaH_newkey(L, t, key);				//创建一个新键 返回其值的指针
}

//设置键为整数的键值对的值 如果不存在该键则创建一个 否则覆盖原先的值
void luaH_setint (lua_State *L, Table *t, lua_Integer key, TValue *value) {
  const TValue *p = luaH_getint(t, key);			//先在表中搜索这个key 得到它的值
  TValue *cell;
  if (p != luaO_nilobject)							//如果值不为nil 存在
    cell = cast(TValue *, p);						//cell指向p
  else {											//不存在这个key
    TValue k;
    setivalue(&k, key);								//设置k的值即类型
    cell = luaH_newkey(L, t, &k);					//得到键k所在位置的值的指针
  }
  setobj2t(L, cell, value);						//*cell = *value
}

//在hash表部分 查找边界  j为整数部分最大下标+1
static int unbound_search (Table *t, unsigned int j) {
  unsigned int i = j;  /* i is zero or a present index */
  j++;
  //查找i和j 找到i存在而j不存在 
  while (!ttisnil(luaH_getint(t, j))) {				//当以j为键的值不为nil 
    i = j;											//i记录j上次的值
    if (j > cast(unsigned int, MAX_INT)/2) {		//j溢出了
      /* table was built with bad purposes: resort to linear search */
      i = 1;
      while (!ttisnil(luaH_getint(t, i))) i++;		//i从1开始查找 直到第一个值为nil退出循环
      return i - 1;									//返回i-1
    }
    j *= 2;											//j*2
  }
  //在i和j之间进行二分查找
  while (j - i > 1) {
    unsigned int m = (i+j)/2;
    if (ttisnil(luaH_getint(t, m))) j = m;
    else i = m;
  }
  return i;
}


/*
** Try to find a boundary in table 't'. A 'boundary' is an integer index
** such that t[i] is non-nil and t[i+1] is nil (and 0 if t[1] is nil).
*/
int luaH_getn (Table *t) {
  unsigned int j = t->sizearray;
  if (j > 0 && ttisnil(&t->array[j - 1])) {		//如果表数组部分的尺寸大于0 而且 数组array[j-1]为nil
	//在数字部分存在边界 使用二分法查找
    unsigned int i = 0;
    while (j - i > 1) {							//i和j未相遇
      unsigned int m = (i+j)/2;
      if (ttisnil(&t->array[m - 1])) j = m;		//如果array[m-1]为nil 右边界收缩
      else i = m;								//否则 左边界收缩
    }
    return i;									//返回i
  }
  //在hash表部分查找一个边界
  else if (isdummy(t->node))					//hash表部分为空
    return j;									//直接返回数组部分的尺寸
  else return unbound_search(t, j);
}



#if defined(LUA_DEBUG)

Node *luaH_mainposition (const Table *t, const TValue *key) {
  return mainposition(t, key);
}

int luaH_isdummy (Node *n) { return isdummy(n); }

#endif
