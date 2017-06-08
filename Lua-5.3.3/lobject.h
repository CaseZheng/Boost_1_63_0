/*
** $Id: lobject.h,v 2.116 2015/11/03 18:33:10 roberto Exp $
** Type definitions for Lua objects
** See Copyright Notice in lua.h
*/


#ifndef lobject_h
#define lobject_h


#include <stdarg.h>


#include "llimits.h"
#include "lua.h"


/*
** Extra tags for non-values
*/
#define LUA_TPROTO	LUA_NUMTAGS				//函数原型 function prototypes 
#define LUA_TDEADKEY	(LUA_NUMTAGS+1)		//标记这个key是被移除 removed keys in tables

/*
** number of all possible tags (including LUA_TNONE but excluding DEADKEY)
*/
#define LUA_TOTALTAGS	(LUA_TPROTO + 2)


/*
** tags for Tagged Values have the following use of bits:
** bits 0-3: actual tag (a LUA_T* value)
** bits 4-5: variant bits
** bit 6: whether value is collectable
0 - 3 : 大类型标记
4 - 5 : 子类型标记
6 : 可回收标记
*/


/*
** LUA_TFUNCTION variants:
** 0 - Lua function
** 1 - light C function
** 2 - regular C function (closure)
*/

/* Variant tags for functions */
//Lua闭包
#define LUA_TLCL	(LUA_TFUNCTION | (0 << 4))  /* Lua closure */
//C函数
#define LUA_TLCF	(LUA_TFUNCTION | (1 << 4))  /* light C function */
//C闭包
#define LUA_TCCL	(LUA_TFUNCTION | (2 << 4))  /* C closure */


//字符串保存在Lua状态机内有两种内部形式，短字符串及长字符串
//这个小类型区分放在类型字节的高四位，所以为外部 API 所不可见。对于最终用户来说，只见到 LUA_TSTRING 一种类型
#define LUA_TSHRSTR	(LUA_TSTRING | (0 << 4))  /* short strings */
#define LUA_TLNGSTR	(LUA_TSTRING | (1 << 4))  /* long strings */


//数字在lua状态机中保存的两种形式
#define LUA_TNUMFLT	(LUA_TNUMBER | (0 << 4))  /* float numbers */
#define LUA_TNUMINT	(LUA_TNUMBER | (1 << 4))  /* integer numbers */


/* 表示对象可回收的标记Bit mark for collectable types */
#define BIT_ISCOLLECTABLE	(1 << 6)

/* mark a tag as collectable */
//将标记标记为可收集
#define ctb(t)			((t) | BIT_ISCOLLECTABLE)


/*
** Common type for all collectable objects
*/
typedef struct GCObject GCObject;


/*
所有引用对象的头常用的头，以宏的形式包含在其它对象中
*/
#define CommonHeader	GCObject *next; lu_byte tt; lu_byte marked


/*
** Common type has only the common header
*/
struct GCObject {
  CommonHeader;
};

//Lua 使用一个联合union Value 保存数据 
typedef union Value {
  GCObject *gc;    // 引用类型
  void *p;         // userdata
  int b;           // booleans
  lua_CFunction f; // light C functions
  lua_Integer i;   // integer numbers
  lua_Number n;    // float numbers
} Value;


#define TValuefields	Value value_; int tt_


typedef struct lua_TValue {
  TValuefields;			//Value value_; int tt_			tt_用于区分存放在Value联合体中的数据类型
} TValue;



/* macro defining a nil value */
#define NILCONSTANT	{NULL}, LUA_TNIL


#define val_(o)		((o)->value_)


/* TValue的原类型标记 raw type tag of a TValue */
#define rttype(o)	((o)->tt_)

//保留低四位
#define novariant(x)	((x) & 0x0F)

/* 原始值的大类型与小类型 type tag of a TValue (bits 0-3 for tags + variant bits 4-5) */
#define ttype(o)	(rttype(o) & 0x3F)

/* 原始值的大类型  type tag of a TValue with no variants (bits 0-3) */
#define ttnov(o)	(novariant(rttype(o)))


/* Macros to test type */
#define checktag(o,t)		(rttype(o) == (t))
#define checktype(o,t)		(ttnov(o) == (t))
#define ttisnumber(o)		checktype((o), LUA_TNUMBER)
#define ttisfloat(o)		checktag((o), LUA_TNUMFLT)
#define ttisinteger(o)		checktag((o), LUA_TNUMINT)
//判断nil
#define ttisnil(o)		checktag((o), LUA_TNIL)
#define ttisboolean(o)		checktag((o), LUA_TBOOLEAN)
#define ttislightuserdata(o)	checktag((o), LUA_TLIGHTUSERDATA)
#define ttisstring(o)		checktype((o), LUA_TSTRING)
#define ttisshrstring(o)	checktag((o), ctb(LUA_TSHRSTR))
#define ttislngstring(o)	checktag((o), ctb(LUA_TLNGSTR))
#define ttistable(o)		checktag((o), ctb(LUA_TTABLE))
#define ttisfunction(o)		checktype(o, LUA_TFUNCTION)
#define ttisclosure(o)		((rttype(o) & 0x1F) == LUA_TFUNCTION)
#define ttisCclosure(o)		checktag((o), ctb(LUA_TCCL))
#define ttisLclosure(o)		checktag((o), ctb(LUA_TLCL))
#define ttislcf(o)		checktag((o), LUA_TLCF)
#define ttisfulluserdata(o)	checktag((o), ctb(LUA_TUSERDATA))
#define ttisthread(o)		checktag((o), ctb(LUA_TTHREAD))
#define ttisdeadkey(o)		checktag((o), LUA_TDEADKEY)


/* Macros to access values */
#define ivalue(o)	check_exp(ttisinteger(o), val_(o).i)
#define fltvalue(o)	check_exp(ttisfloat(o), val_(o).n)
#define nvalue(o)	check_exp(ttisnumber(o), \
	(ttisinteger(o) ? cast_num(ivalue(o)) : fltvalue(o)))
#define gcvalue(o)	check_exp(iscollectable(o), val_(o).gc)
#define pvalue(o)	check_exp(ttislightuserdata(o), val_(o).p)
#define tsvalue(o)	check_exp(ttisstring(o), gco2ts(val_(o).gc))
//取userdata的地址
#define uvalue(o)	check_exp(ttisfulluserdata(o), gco2u(val_(o).gc))
#define clvalue(o)	check_exp(ttisclosure(o), gco2cl(val_(o).gc))
#define clLvalue(o)	check_exp(ttisLclosure(o), gco2lcl(val_(o).gc))
#define clCvalue(o)	check_exp(ttisCclosure(o), gco2ccl(val_(o).gc))
#define fvalue(o)	check_exp(ttislcf(o), val_(o).f)
#define hvalue(o)	check_exp(ttistable(o), gco2t(val_(o).gc))
//取boolean值
#define bvalue(o)	check_exp(ttisboolean(o), val_(o).b)
#define thvalue(o)	check_exp(ttisthread(o), gco2th(val_(o).gc))
/* a dead value may get the 'gc' field, but cannot access its contents */
#define deadvalue(o)	check_exp(ttisdeadkey(o), cast(void *, val_(o).gc))

#define l_isfalse(o)	(ttisnil(o) || (ttisboolean(o) && bvalue(o) == 0))

//表示对象可回收
#define iscollectable(o)	(rttype(o) & BIT_ISCOLLECTABLE)


/* Macros for internal tests */
#define righttt(obj)		(ttype(obj) == gcvalue(obj)->tt)

#define checkliveness(L,obj) \
	lua_longassert(!iscollectable(obj) || \
		(righttt(obj) && (L == NULL || !isdead(G(L),gcvalue(obj)))))


/* Macros to set values */
#define settt_(o,t)	((o)->tt_=(t))

#define setfltvalue(obj,x) \
  { TValue *io=(obj); val_(io).n=(x); settt_(io, LUA_TNUMFLT); }

#define chgfltvalue(obj,x) \
  { TValue *io=(obj); lua_assert(ttisfloat(io)); val_(io).n=(x); }

//设为整数
#define setivalue(obj,x) \
  { TValue *io=(obj); val_(io).i=(x); settt_(io, LUA_TNUMINT); }

#define chgivalue(obj,x) \
  { TValue *io=(obj); lua_assert(ttisinteger(io)); val_(io).i=(x); }

//设置该对象为nil
#define setnilvalue(obj) settt_(obj, LUA_TNIL)

//设置函数值等于x 设置函数类型
#define setfvalue(obj,x) \
  { TValue *io=(obj); val_(io).f=(x); settt_(io, LUA_TLCF); }

#define setpvalue(obj,x) \
  { TValue *io=(obj); val_(io).p=(x); settt_(io, LUA_TLIGHTUSERDATA); }

#define setbvalue(obj,x) \
  { TValue *io=(obj); val_(io).b=(x); settt_(io, LUA_TBOOLEAN); }

#define setgcovalue(L,obj,x) \
  { TValue *io = (obj); GCObject *i_g=(x); \
    val_(io).gc = i_g; settt_(io, ctb(i_g->tt)); }

//设置obj所指值为x 设置x类型为x原类型
#define setsvalue(L,obj,x) \
  { TValue *io = (obj); TString *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(x_->tt)); \
    checkliveness(L,io); }

//设置obj所指值为x 设置x类型为userdata
#define setuvalue(L,obj,x) \
  { TValue *io = (obj); Udata *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TUSERDATA)); \
    checkliveness(L,io); }

//设置obj所指值为x 设置x类型为thread
#define setthvalue(L,obj,x) \
  { TValue *io = (obj); lua_State *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TTHREAD)); \
    checkliveness(L,io); }

//设置obj所指值为x 设置x类型为Lua closure
#define setclLvalue(L,obj,x) \
  { TValue *io = (obj); LClosure *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TLCL)); \
    checkliveness(L,io); }

//设置obj所指值为x 设置x类型为C closure
#define setclCvalue(L,obj,x) \
  { TValue *io = (obj); CClosure *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TCCL)); \
    checkliveness(L,io); }

//设置obj所指值为x 设置x类型为table
#define sethvalue(L,obj,x) \
  { TValue *io = (obj); Table *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TTABLE)); \
    checkliveness(L,io); }

//标记obj是dead
#define setdeadvalue(obj)	settt_(obj, LUA_TDEADKEY)



#define setobj(L,obj1,obj2) \
	{ TValue *io1=(obj1); *io1 = *(obj2); \
	  (void)L; checkliveness(L,io1); }


/*
** different types of assignments, according to destination
*/

/* from stack to (same) stack */
#define setobjs2s	setobj
/* to stack (not from same stack) */
#define setobj2s	setobj
#define setsvalue2s	setsvalue
#define sethvalue2s	sethvalue
#define setptvalue2s	setptvalue
/* from table to same table */
#define setobjt2t	setobj
/* to new object */
#define setobj2n	setobj
#define setsvalue2n	setsvalue

/* to table (define it as an expression to be used in macros) */
//设置*o1=*o2  检测Lua状态机L和o1的状态
#define setobj2t(L,o1,o2)  ((void)L, *(o1)=*(o2), checkliveness(L,(o1)))




/*
** {======================================================
** types and prototypes
** =======================================================
*/


typedef TValue *StkId;  /* index to stack elements */




/*
字符串一旦创建，则不可被改写。Lua 的值对象若为字符串类型，则以引用方式存在。属于需被垃圾收
集器管理的对象。也就是说，一个字符串一旦没有被任何地方引用就可以回收它
字符串的数据内容并没有被分配独立一块内存来保存，而是直接最加在畔畓畴畲畩畮畧结构的后面。用getstr
这个宏就可以取到实际的畃字符串指针。
所有短字符串均被存放在全局表（global_State）的strt中
长字符串则独立存放，从外部压入一个长字符串时，简单复制一遍字符串，并不立刻计算其hash值
而是标记一下extra域。直到需要对字符串做键匹配时，才惰性计算hash值，加快以后的键比较过程
*/
typedef struct TString {
  CommonHeader;				//用于GC
  lu_byte extra;			//用来记录辅助信息 
							//对于短字符串extra 用来记录这个字符串是否为保留字，这个标记用于词法分析器对保留字的快速判断；
							//对于长字符串，可以用于惰性求哈希值
  lu_byte shrlen;			//由于Lua并不以'\0'结尾来识别字符串的长度，记录其长度  短字符串长度
  unsigned int hash;		//记录字符串的 hash 可以用来加快字符串的匹配和查找；
  union {
    size_t lnglen;			//长字符串长度
    struct TString *hnext;  //hash表 连接列表
  } u;
} TString;


/*
** Ensures that address after this type is always fully aligned.
*/
typedef union UTString {
  L_Umaxalign dummy;			//确保字符串的最大对齐方式
  TString tsv;					//字符串
} UTString;


/*
从TString中得到真正的字符串			
cast(char *, (ts)) + sizeof(UTString) 展开后 ((char*)(ts))+sizeof(UTString)
*/
#define getstr(ts)  \
  check_exp(sizeof((ts)->extra), cast(char *, (ts)) + sizeof(UTString))


/* get the actual string (array of bytes) from a Lua value */
#define svalue(o)       getstr(tsvalue(o))

/* 得到字符串长度 get string length from 'TString *s' */
#define tsslen(s)	((s)->tt == LUA_TSHRSTR ? (s)->shrlen : (s)->u.lnglen)

/* 得到字符串长度 get string length from 'TValue *o' */
#define vslen(o)	tsslen(tsvalue(o))


/*
** Header for userdata; memory area follows the end of this structure
** (aligned according to 'UUdata'; see next).
*/
typedef struct Udata {
  CommonHeader;
  lu_byte ttuv_;  /* user value's tag */
  struct Table *metatable;	//元表
  size_t len;				//保存userdata内存的大小，就紧随头后面数据内存的大小 number of bytes
  union Value user_;  /* user value */
} Udata;


/*
userdata  数据部分放在后面
** Ensures that address after this type is always fully aligned.
*/
typedef union UUdata {
  L_Umaxalign dummy;  /* ensures maximum alignment for 'local' udata */
  Udata uv;
} UUdata;


/*
**  Get the address of memory block inside 'Udata'.
** (Access to 'ttuv_' ensures that value is really a 'Udata'.)
*/
#define getudatamem(u)  \
  check_exp(sizeof((u)->ttuv_), (cast(char*, (u)) + sizeof(UUdata)))

#define setuservalue(L,u,o) \
	{ const TValue *io=(o); Udata *iu = (u); \
	  iu->user_ = io->value_; iu->ttuv_ = rttype(io); \
	  checkliveness(L,io); }


#define getuservalue(L,u,o) \
	{ TValue *io=(o); const Udata *iu = (u); \
	  io->value_ = iu->user_; settt_(io, iu->ttuv_); \
	  checkliveness(L,io); }


/*
** Description of an upvalue for function prototypes
*/
typedef struct Upvaldesc {
  TString *name;	//upvalue的名称 用于debug调试
  lu_byte instack;  //在堆栈（寄存器）上为1 否则为0
  lu_byte idx;		//upvalue 的索引 栈上 或 函数列表 index of upvalue (in stack or in outer function's list)
} Upvaldesc;


/*
** Description of a local variable for function prototypes
** (used for debug information)
*/
typedef struct LocVar {
  TString *varname;
  int startpc;  /* first point where variable is active */
  int endpc;    /* first point where variable is dead */
} LocVar;


/*
函数原型
*/
typedef struct Proto {
  CommonHeader;
  lu_byte numparams;		//输入参数的明确最小要求
  lu_byte is_vararg;		//变长参数 2: declared vararg; 1: uses vararg */
  lu_byte maxstacksize;		//该函数需要的寄存器的数量
  int sizeupvalues;			//upvalue的数量
  int sizek;				//'k'表的大小
  int sizecode;
  int sizelineinfo;
  int sizep;				//函数内定义函数的数量
  int sizelocvars;
  int linedefined;  /* debug information  */
  int lastlinedefined;  /* debug information  */
  TValue *k;				//函数使用的常量
  Instruction *code;		//字节码数组
  struct Proto **p;			//函数类定义的函数
  int *lineinfo;			//操作码与源代码行号的映射 (调试信息)
  LocVar *locvars;			//局部变量信息 information about local variables (debug information)
  Upvaldesc *upvalues;		//upvalue的信息	数组
  struct LClosure *cache;	//用这个函数原型创建的最后一个closure
  TString  *source;			//模块的名称 用于调试
  GCObject *gclist;
} Proto;



/*
** Lua Upvalues
*/
typedef struct UpVal UpVal;


/*
** Closures
*/

#define ClosureHeader \
	CommonHeader; lu_byte nupvalues; GCObject *gclist

typedef struct CClosure {
  ClosureHeader;
  lua_CFunction f;
  TValue upvalue[1];  /* list of upvalues */
} CClosure;

//Lua 闭包仅仅是原型Proto和UpVal的集合
typedef struct LClosure {
  ClosureHeader;
  struct Proto *p;		//指向函数原型
  UpVal *upvals[1];		//upvalue值列表 list of upvalues
} LClosure;

//Lua闭包  使用联合体
typedef union Closure {
  CClosure c;		//C语言实现的闭包
  LClosure l;		//Lua语言实现的闭包
} Closure;

/*
union Closure {
	struct CClosure {
		GCObject *next;
		lu_byte tt;
		lu_byte marked;

		lu_byte nupvalues;
		GCObject *gclist;

		lua_CFunction f;
		TValue upvalue[1];
	} c;
	struct LClosure {
		GCObject *next;
		lu_byte tt;
		lu_byte marked;
		
		lu_byte nupvalues;
		GCObject *gclist;
		
		struct Proto *p;
		UpVal *upvalue[1];
	} l;
}
*/

#define isLfunction(o)	ttisLclosure(o)

#define getproto(o)	(clLvalue(o)->p)


/*
hash表的键
*/
typedef union TKey {
  struct {
    TValuefields;
    int next;			//用于链接链表 表示下一个节点的偏移
  } nk;
  TValue tvk;			//键的值
} TKey;


//设置key的值为obj 并检测有效性  对key的next域忽略
#define setnodekey(L,key,obj) \
	{ TKey *k_=(key); const TValue *io_=(obj); \
	  k_->nk.value_ = io_->value_; k_->nk.tt_ = io_->tt_; \
	  (void)L; checkliveness(L,io_); }


typedef struct Node {
  TValue i_val;			//值
  TKey i_key;			//键
} Node;

/*
struct Node{
	struct TValue{
		Value value_;
		int tt_;
	}i_val;
	union {
		struct {
			Value value_;			//
			int tt_;				//
			int next;
		}nk;
		struct TValue{
			Value value_;			//
			int tt_;				//
		}tvk;
	}i_key;
}
*/

//table 的储存分为数组部分和哈希表部分。数组部分从由开始作整数数字索引。这可以提供紧凑且高效的随
//机访问。而不能被储存在数组部分的数据全部放在哈希表中，唯一不能做哈希键值的是nil，这个限制可以
//帮助我们发现许多运行期错误。lua的哈希表有一个高效的实现，几乎可以认为操作哈希表的时间复杂度为O(1)。
typedef struct Table {
  CommonHeader;
  lu_byte flags;			// 1<<p 标记元方法p不存在
  lu_byte lsizenode;		//hash表的大小 由于hash表的大小一定是2的整数次幂，所以lsizenode表示幂次 而非实际大小
  unsigned int sizearray;	//表数组部分的长度信息
  TValue *array;			//表数组部分 表的整数部分存放在array中
  Node *node;				//表hash表存储位置
  Node *lastfree;			//上次记录的表的空闲位置 any free position is before this position
  struct Table *metatable;	//table的元表
  GCObject *gclist;
} Table;



/*
** 'module' operation for hashing (size is always a power of 2)
cast(int, (s) & ((size)-1))	 展开 (int)((s)&(size-1))
*/
#define lmod(s,size) \
	(check_exp((size&(size-1))==0, (cast(int, (s) & ((size)-1)))))

//2^x
#define twoto(x)	(1<<(x))
//得到hash表的大小
#define sizenode(t)	(twoto((t)->lsizenode))


/*
** (address of) a fixed nil value
*/
#define luaO_nilobject		(&luaO_nilobject_)


LUAI_DDEC const TValue luaO_nilobject_;

/* size of buffer for 'luaO_utf8esc' function */
#define UTF8BUFFSZ	8

LUAI_FUNC int luaO_int2fb (unsigned int x);
LUAI_FUNC int luaO_fb2int (int x);
LUAI_FUNC int luaO_utf8esc (char *buff, unsigned long x);
LUAI_FUNC int luaO_ceillog2 (unsigned int x);
LUAI_FUNC void luaO_arith (lua_State *L, int op, const TValue *p1,
                           const TValue *p2, TValue *res);
LUAI_FUNC size_t luaO_str2num (const char *s, TValue *o);
LUAI_FUNC int luaO_hexavalue (int c);
LUAI_FUNC void luaO_tostring (lua_State *L, StkId obj);
LUAI_FUNC const char *luaO_pushvfstring (lua_State *L, const char *fmt,
                                                       va_list argp);
LUAI_FUNC const char *luaO_pushfstring (lua_State *L, const char *fmt, ...);
LUAI_FUNC void luaO_chunkid (char *out, const char *source, size_t len);


#endif

