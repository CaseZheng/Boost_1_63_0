/*
** $Id: lstring.c,v 2.56 2015/11/23 11:32:51 roberto Exp $
** String table (keeps all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#define lstring_c
#define LUA_CORE

#include "lprefix.h"


#include <string.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"


#define MEMERRMSG       "not enough memory"


/*
** Lua will use at most ~(2^LUAI_HASHLIMIT) bytes from a string to
** compute its hash
*/
#if !defined(LUAI_HASHLIMIT)
#define LUAI_HASHLIMIT		5
#endif


//长字符串的比较
int luaS_eqlngstr (TString *a, TString *b) {
  size_t len = a->u.lnglen;
  lua_assert(a->tt == LUA_TLNGSTR && b->tt == LUA_TLNGSTR);
  return (a == b) ||											//地址相同 是同一个肯定相等
    ((len == b->u.lnglen) &&									//长度相同
     (memcmp(getstr(a), getstr(b), len) == 0));					//对字符串比较
}


unsigned int luaS_hash (const char *str, size_t l, unsigned int seed) {
  unsigned int h = seed ^ cast(unsigned int, l);
  size_t step = (l >> LUAI_HASHLIMIT) + 1;
  for (; l >= step; l -= step)
    h ^= ((h<<5) + (h>>2) + cast_byte(str[l - 1]));
  return h;
}

//得到长字符串的hash值 如果没有则求一下
unsigned int luaS_hashlongstr (TString *ts) {
  lua_assert(ts->tt == LUA_TLNGSTR);
  if (ts->extra == 0) {							//长字符串的extra域用于存放hash值 如果为0 表示还未求过hash值
    ts->hash = luaS_hash(getstr(ts), ts->u.lnglen, ts->hash);	//得到其hash值
    ts->extra = 1;								//标记已有hash值
  }
  return ts->hash;
}


/*
调整字符串表的大小
*/
void luaS_resize (lua_State *L, int newsize) {
  int i;
  stringtable *tb = &G(L)->strt;
  if (newsize > tb->size) {  /* grow table if needed */
    luaM_reallocvector(L, tb->hash, tb->size, newsize, TString *);
    for (i = tb->size; i < newsize; i++)
      tb->hash[i] = NULL;
  }
  for (i = 0; i < tb->size; i++) {  /* rehash */
    TString *p = tb->hash[i];
    tb->hash[i] = NULL;
    while (p) {  /* for each node in the list */
      TString *hnext = p->u.hnext;  /* save next */
      unsigned int h = lmod(p->hash, newsize);  /* new position */
      p->u.hnext = tb->hash[h];  /* chain it */
      tb->hash[h] = p;
      p = hnext;
    }
  }
  if (newsize < tb->size) {  /* shrink table if needed */
    /* vanishing slice should be empty */
    lua_assert(tb->hash[newsize] == NULL && tb->hash[tb->size - 1] == NULL);
    luaM_reallocvector(L, tb->hash, tb->size, newsize, TString *);
  }
  tb->size = newsize;
}


/*
** Clear API string cache. (Entries cannot be empty, so fill them with
** a non-collectable string.)
清除API字符串缓存。 （条目不能为空，因此请使用不可收集的字符串填充。）
*/
void luaS_clearcache (global_State *g) {
  int i, j;
  for (i = 0; i < STRCACHE_N; i++)
    for (j = 0; j < STRCACHE_M; j++) {
    if (iswhite(g->strcache[i][j]))  /* 可以被收集? will entry be collected? */
      g->strcache[i][j] = g->memerrmsg;  /* 用初始的固定字符串重置它 replace it with something fixed */
    }
}


/*
初始化字符串表 和 字符串缓存
*/
void luaS_init (lua_State *L) {
  global_State *g = G(L);
  int i, j;
  luaS_resize(L, MINSTRTABSIZE);		//初始化字符串表的大小
  /* pre-create memory-error message */
  g->memerrmsg = luaS_newliteral(L, MEMERRMSG);		//创建MEMERRMSG字符串
  luaC_fix(L, obj2gco(g->memerrmsg));  /* it should never be collected */
  for (i = 0; i < STRCACHE_N; i++)		//使用有效的字符串填充缓存
    for (j = 0; j < STRCACHE_M; j++)
      g->strcache[i][j] = g->memerrmsg;
}



/*
创建一个新的string object 
l字符串的长度  tag字符串类型（长 短）  h是hash值
*/
static TString *createstrobj (lua_State *L, size_t l, int tag, unsigned int h) {
  TString *ts;
  GCObject *o;
  size_t totalsize;
  //sizelstring(l)   sizeof(union UTString) + ((l) + 1) * sizeof(char)
  totalsize = sizelstring(l);				//总长度 TString+字符串长度
  o = luaC_newobj(L, tag, totalsize);
  ts = gco2ts(o);
  ts->hash = h;								//记录其hash值
  ts->extra = 0;
  getstr(ts)[l] = '\0';						//字符串最后一位置为'\0'  Lua和C语言交互时不必做额外的转化
  return ts;
}


//创建一个长字符串对象
TString *luaS_createlngstrobj (lua_State *L, size_t l) {
  //创建长字符串 hash种子使用全局状态机的
  TString *ts = createstrobj(L, l, LUA_TLNGSTR, G(L)->seed);
  ts->u.lnglen = l;							//设置长字符串长度
  return ts;
}


void luaS_remove (lua_State *L, TString *ts) {
  stringtable *tb = &G(L)->strt;
  TString **p = &tb->hash[lmod(ts->hash, tb->size)];
  while (*p != ts)  /* find previous element */
    p = &(*p)->u.hnext;
  *p = (*p)->u.hnext;  /* remove element from its list */
  tb->nuse--;
}


/*
检查是否存在短字符串并且重新利用或创建一个新的  所有的短字符串全部放在全局的字符串hash表中
*/
static TString *internshrstr (lua_State *L, const char *str, size_t l) {
  TString *ts;
  global_State *g = G(L);
  unsigned int h = luaS_hash(str, l, g->seed);				//得到字符串str的hash值  种子使用全局表中seed
  TString **list = &g->strt.hash[lmod(h, g->strt.size)];	//得到该hash值在hash表中的链表
  lua_assert(str != NULL);  /* otherwise 'memcmp'/'memcpy' are undefined */
  for (ts = *list; ts != NULL; ts = ts->u.hnext) {			//遍历这个链表
    if (l == ts->shrlen &&
        (memcmp(str, getstr(ts), l * sizeof(char)) == 0)) {		//字符串长度相同 并 相等
      if (isdead(g, ts))									//如果是死掉的 复活它
        changewhite(ts);
      return ts;											//返回这个字符串
    }
  }
  //上面在表中没找到这个字符串  或者  没找到对应hash值的链表
  //如果全局表中字符串hash表中元素总个数大于等于hash表的大小 而且字符串hash表的尺寸小于整数的一把 
  if (g->strt.nuse >= g->strt.size && g->strt.size <= MAX_INT/2) {
	luaS_resize(L, g->strt.size * 2);						//扩大hash表的尺寸
    list = &g->strt.hash[lmod(h, g->strt.size)];			//重新获取该hash值的链表
  }
  ts = createstrobj(L, l, LUA_TSHRSTR, h);					//创建一个字符串对象
  memcpy(getstr(ts), str, l * sizeof(char));				//将字符串str存储到UTString后面
  ts->shrlen = cast_byte(l);								//短字符串长度存储
  ts->u.hnext = *list;										//将该字符串加入到hash链表中
  *list = ts;
  g->strt.nuse++;											//hash表元素个数+1
  return ts;
}


/*
创建一个新的字符串   有明确的字符串长度  返回创建的字符串
*/
TString *luaS_newlstr (lua_State *L, const char *str, size_t l) {
  if (l <= LUAI_MAXSHORTLEN)				//短字符串
    return internshrstr(L, str, l);			//创建一个短字符串
  else {
    TString *ts;
    if (l >= (MAX_SIZE - sizeof(TString))/sizeof(char))		//字符串过长
      luaM_toobig(L);
    ts = luaS_createlngstrobj(L, l);				//得到长字符串对象
    memcpy(getstr(ts), str, l * sizeof(char));		//将字符串拷贝到生成的对象中
    return ts;
  }
}


/*
创建一个新的字符串 现在缓存中查找它 如果它存在直接返回 否则去创建它并将其放在索引头部
*/
TString *luaS_new (lua_State *L, const char *str) {
  unsigned int i = point2uint(str) % STRCACHE_N;	//hash 字符串缓存的索引
  int j;
  TString **p = G(L)->strcache[i];
  for (j = 0; j < STRCACHE_M; j++) {
    if (strcmp(str, getstr(p[j])) == 0)				//比较下 如果命中 返回该字符串
      return p[j];
  }
  /* normal route */
  for (j = STRCACHE_M - 1; j > 0; j--)				//该索引的所有的字符串向后移动一位
    p[j] = p[j - 1];
  p[0] = luaS_newlstr(L, str, strlen(str));		//创建一个新的字符串 并放在索引头部
  return p[0];
}

//Userdata
Udata *luaS_newudata (lua_State *L, size_t s) {
  Udata *u;
  GCObject *o;
  if (s > MAX_SIZE - sizeof(Udata))
    luaM_toobig(L);
  o = luaC_newobj(L, LUA_TUSERDATA, sizeludata(s));
  u = gco2u(o);
  u->len = s;
  u->metatable = NULL;
  setuservalue(L, u, luaO_nilobject);
  return u;
}

