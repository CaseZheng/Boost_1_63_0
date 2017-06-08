/*
** $Id: ltm.h,v 2.22 2016/02/26 19:20:15 roberto Exp $
** Tag methods
** See Copyright Notice in lua.h
*/

#ifndef ltm_h
#define ltm_h


#include "lobject.h"


/*
* WARNING: if you change the order of this enumeration,
* grep "ORDER TM" and "ORDER OP"
*/
typedef enum {
  /*索引 table[key]。 当 table 不是表或是表 table 中不存在 key 这个键时，这个事件被触发。 此时，
  会读出 table 相应的元方法。尽管名字取成这样， 这个事件的元方法其实可以是一个函数也可以是一张表。 
  如果它是一个函数，则以 table 和 key 作为参数调用它。 如果它是一张表，最终的结果就是以 key 取索
  引这张表的结果。 （这个索引过程是走常规的流程，而不是直接索引， 所以这次索引有可能引发另一次元方法。）*/
  TM_INDEX,
  /*索引赋值 table[key] = value 。 和索引事件类似，它发生在 table 不是表或是表 table 中不存在
  key 这个键的时候。 此时，会读出 table 相应的元方法。同索引过程那样， 这个事件的元方法即可以是函数，
  也可以是一张表。 如果是一个函数， 则以 table、 key、以及 value 为参数传入。 如果是一张表， Lua 对
  这张表做索引赋值操作。 （这个索引过程是走常规的流程，而不是直接索引赋值， 所以这次索引赋值有可能引
  发另一次元方法。）一旦有了 "newindex" 元方法， Lua 就不再做最初的赋值操作。 （如果有必要，在元方
  法内部可以调用 rawset 来做赋值。）*/
  TM_NEWINDEX,
  TM_GC,
  TM_MODE,
  /* # （取长度）操作。 如果对象不是字符串，Lua 会尝试它的元方法。 如果有元方法，则调用它并将对象以参
  数形式传入， 而返回值（被调整为单个）则作为结果。 如果对象是一张表且没有元方法， Lua 使用表的取长
  度操作。 其它情况，均抛出错误。*/
  TM_LEN,
  /* == （等于）操作。 和 "add" 操作行为类似， 不同的是 Lua 仅在两个值都是表或都是完全用户数据 且它们
  不是同一个对象时才尝试元方法。 调用的结果总会被转换为布尔量。*/
  TM_EQ,
  /*add + 如果任何不是数字的值（包括不能转换为数字的字符串）做加法， Lua 就会尝试调用元方法。 
  //首先、Lua 检查第一个操作数（即使它是合法的）， 如果这个操作数没有为 "__add" 事件定义元方法， 
  //Lua 就会接着检查第二个操作数。 一旦 Lua 找到了元方法， 它将把两个操作数作为参数传入元方法， 
  //元方法的结果（调整为单个值）作为这个操作的结果。 如果找不到元方法，将抛出一个错误。*/
  TM_ADD,		
  TM_SUB,		//sub - 行为和 "add" 操作类似。
  TM_MUL,		//mul * 行为和 "add" 操作类似。
  TM_MOD,		//mod % 行为和 "add" 操作类似。
  TM_POW,		//pow 次方^ 行为和 "add" 操作类似。
  TM_DIV,		//div /行为和 "add" 操作类似。
  TM_IDIV,		//idiv 向下取整除法 // 行为和 "add" 操作类似。
  /*按位与 & 行为和 "add" 操作类似， 不同的是 Lua 会在任何一个操作数无法转换为整数时尝试取元方法。*/
  TM_BAND,
  TM_BOR,		//按位或 | 行为和 "band" 操作类似。
  TM_BXOR,		//按位异或 ~ 行为和 "band" 操作类似。
  TM_SHL,		//左移 << 行为和 "band" 操作类似。
  TM_SHR,		//右移 >> 行为和 "band" 操作类似。
  TM_UNM,		//取负 取负 - 行为和 "add" 操作类似。
  TM_BNOT,		//按位非 ~  行为和 "band" 操作类似。
  /*小于 < 和 "add" 操作行为类似， 不同的是 Lua仅在两个值不全为整数也不全为字符串时才尝试元方法。 
  调用的结果总会被转换为布尔量。*/
  TM_LT,		
  /*<= （小于等于）操作。 和其它操作不同， 小于等于操作可能用到两个不同的事件。 首先，像 "lt" 操作的
  行为那样，Lua 在两个操作数中查找 "__le" 元方法。 如果一个元方法都找不到，就会再次查找 "__lt" 事件， 
  它会假设 a <= b 等价于 not (b < a)。 而其它比较操作符类似，其结果会被转换为布尔量。*/
  TM_LE,
  /*连接 ... Lua 在任何操作数即不是一个字符串 也不是数字（数字总能转换为对应的字符串）的情况下尝试元方法。*/
  TM_CONCAT,	
  /*函数调用操作 func(args)。 当 Lua 尝试调用一个非函数的值的时候会触发这个事件 （即 func 不是一个函数）
  查找 func 的元方法， 如果找得到，就调用这个元方法， func 作为第一个参数传入，原来调用的参数（args）后
  依次排在后面。*/
  TM_CALL,
  TM_N		//元方法的数量
} TMS;


//得到元表中的一个项
#define gfasttm(g,et,e) ((et) == NULL ? NULL : \
  ((et)->flags & (1u<<(e))) ? NULL : luaT_gettm(et, e, (g)->tmname[e]))

#define fasttm(l,et,e)	gfasttm(G(l), et, e)

#define ttypename(x)	luaT_typenames_[(x) + 1]

LUAI_DDEC const char *const luaT_typenames_[LUA_TOTALTAGS];


LUAI_FUNC const char *luaT_objtypename (lua_State *L, const TValue *o);

LUAI_FUNC const TValue *luaT_gettm (Table *events, TMS event, TString *ename);
LUAI_FUNC const TValue *luaT_gettmbyobj (lua_State *L, const TValue *o,
                                                       TMS event);
LUAI_FUNC void luaT_init (lua_State *L);

LUAI_FUNC void luaT_callTM (lua_State *L, const TValue *f, const TValue *p1,
                            const TValue *p2, TValue *p3, int hasres);
LUAI_FUNC int luaT_callbinTM (lua_State *L, const TValue *p1, const TValue *p2,
                              StkId res, TMS event);
LUAI_FUNC void luaT_trybinTM (lua_State *L, const TValue *p1, const TValue *p2,
                              StkId res, TMS event);
LUAI_FUNC int luaT_callorderTM (lua_State *L, const TValue *p1,
                                const TValue *p2, TMS event);



#endif
