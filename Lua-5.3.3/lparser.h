/*
** $Id: lparser.h,v 1.76 2015/12/30 18:16:13 roberto Exp $
** Lua Parser
** See Copyright Notice in lua.h
*/

#ifndef lparser_h
#define lparser_h

#include "llimits.h"
#include "lobject.h"
#include "lzio.h"


/*
** Expression and variable descriptor.
** Code generation for variables and expressions can be delayed to allow
** optimizations; An 'expdesc' structure describes a potentially-delayed
** variable/expression. It has a description of its "main" value plus a
** list of conditional jumps that can also produce its value (generated
** by short-circuit operators 'and'/'or').
*/

//变量/表达式的种类
typedef enum {
  VVOID,  /* when 'expdesc' describes the last expression a list,
             this kind means an empty list (so, no expression) */
  VNIL,			//常数nil
  VTRUE,		//常数true
  VFALSE,		//常数false
  VK,			//常数 'k' info = index of constant in 'k' 
  VKFLT,		//浮点常量 nval = 浮点数常量的值
  VKINT,		//整数常量 nval = 整数常量的值
  VNONRELOC,	//表达式在固定寄存器中具有其值; info = result register
  VLOCAL,		//局部变量 info = 局部寄存器
  VUPVAL,		//upvalue 变量 info = upvalue值在upvalues中的索引
  VINDEXED,  /* indexed variable;
                ind.vt = whether 't' is register or upvalue;
                ind.t = table register or upvalue;
                ind.idx = key's R/K index */
  VJMP,			//表达式是是一个比较 info = pc of corresponding jump instruction
  VRELOCABLE,	//表达式可以将结果放在任何寄存器中 expression can put result in any register; info = instruction pc
  VCALL,		//表达式是一个函数调用 info = instruction pc
  VVARARG		//不定参数表达式 vararg expression; info = instruction pc */
} expkind;


#define vkisvar(k)	(VLOCAL <= (k) && (k) <= VINDEXED)
#define vkisinreg(k)	((k) == VNONRELOC || (k) == VLOCAL)

//表达式
typedef struct expdesc {
  expkind k;		//表达式种类
  union {
    lua_Integer ival;		// VKINT
    lua_Number nval;		// VKFLT
    int info;  /* for generic use */
    struct {  /* for indexed variables (VINDEXED) */
      short idx;  /* index (R/K) */
      lu_byte t;  /* table (register or upvalue) */
      lu_byte vt;  /* whether 't' is register (VLOCAL) or upvalue (VUPVAL) */
    } ind;
  } u;
  int t;  /* patch list of 'exit when true' */
  int f;  /* patch list of 'exit when false' */
} expdesc;


/* description of active local variable */
typedef struct Vardesc {
  short idx;  /* variable index in stack */
} Vardesc;


/* description of pending goto statements and label statements */
typedef struct Labeldesc {
  TString *name;  /* label identifier */
  int pc;  /* position in code */
  int line;  /* line where it appeared */
  lu_byte nactvar;  /* local level where it appears in current block */
} Labeldesc;


/* list of labels or gotos */
typedef struct Labellist {
  Labeldesc *arr;  /* array */
  int n;  /* number of entries in use */
  int size;  /* array size */
} Labellist;


//被解析器使用的动态机构
typedef struct Dyndata {
  struct {  /* list of active local variables */
    Vardesc *arr;
    int n;
    int size;
  } actvar;
  Labellist gt;  /* list of pending gotos */
  Labellist label;   /* list of active labels */
} Dyndata;


/* control of blocks */
struct BlockCnt;  /* defined in lparser.c */


//函数状态机
typedef struct FuncState {
  Proto *f;  /* current function header */
  struct FuncState *prev;  /* enclosing function */
  struct LexState *ls;		//指向词法状态机
  struct BlockCnt *bl;  /* chain of current blocks */
  int pc;  /* next position to code (equivalent to 'ncode') */
  int lasttarget;   /* 'label' of last 'jump label' */
  int jpc;				//等待跳转到'pc'的列表 list of pending jumps to 'pc' */
  int nk;				//'k'表中常量的个数
  int np;  /* number of elements in 'p' */
  int firstlocal;  /* index of first local var (in Dyndata array) */
  short nlocvars;  /* number of elements in 'f->locvars' */
  lu_byte nactvar;  /* number of active local variables */
  lu_byte nups;			//upvalue的个数
  lu_byte freereg;  /* first free register */
} FuncState;


LUAI_FUNC LClosure *luaY_parser (lua_State *L, ZIO *z, Mbuffer *buff,
                                 Dyndata *dyd, const char *name, int firstchar);


#endif
