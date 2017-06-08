/*
** $Id: llex.h,v 1.79 2016/05/02 14:02:12 roberto Exp $
** Lexical Analyzer
** See Copyright Notice in lua.h
*/

#ifndef llex_h
#define llex_h

#include "lobject.h"
#include "lzio.h"


#define FIRST_RESERVED	257


#if !defined(LUA_ENV)
#define LUA_ENV		"_ENV"
#endif


/*
* WARNING: if you change the order of this enumeration,
* grep "ORDER RESERVED"
*/
enum RESERVED {
  //用ASICII码 256 以后的数 表示Lua关键字
  TK_AND = FIRST_RESERVED,	//and
  TK_BREAK,					//break
  TK_DO,					//do
  TK_ELSE,					//else
  TK_ELSEIF,				//elseif
  TK_END,					//end
  TK_FALSE,					//false
  TK_FOR,					//for
  TK_FUNCTION,				//function
  TK_GOTO,					//goto
  TK_IF,					//if
  TK_IN,					//in
  TK_LOCAL,					//local
  TK_NIL,					//nil
  TK_NOT,					//not
  TK_OR,					//or
  TK_REPEAT,				//repeat
  TK_RETURN,				//return
  TK_THEN,					//then
  TK_TRUE,					//ture
  TK_UNTIL,					//until
  TK_WHILE,					// while
  //符号
  TK_IDIV,					// "//"
  TK_CONCAT,				// ..
  TK_DOTS,					// ...
  TK_EQ,					// ==
  TK_GE,					// ">="
  TK_LE,					// "<="
  TK_NE,					// "~="
  TK_SHL,					// "<<"
  TK_SHR,					// >>
  TK_DBCOLON,				// ::
  TK_EOS,					// <eof>
  TK_FLT,					// number
  TK_INT,					// integer
  TK_NAME,					// name
  TK_STRING					// string
};

/*
lua关键字
static const char *const luaX_tokens [] = {
    "and", "break", "do", "else", "elseif",
    "end", "false", "for", "function", "goto", "if",
    "in", "local", "nil", "not", "or", "repeat",
    "return", "then", "true", "until", "while",
    "//", "..", "...", "==", ">=", "<=", "~=",
    "<<", ">>", "::", "<eof>",
    "<number>", "<integer>", "<name>", "<string>"
};
*/

/* number of reserved words */
#define NUM_RESERVED	(cast(int, TK_WHILE-FIRST_RESERVED+1))


typedef union {
  lua_Number r;			//浮点数
  lua_Integer i;		//整数
  TString *ts;			//字符串
} SemInfo;  //语义信息

//标记
typedef struct Token {
  int token;			//标记的类型
  SemInfo seminfo;
} Token;


/* state of the lexer plus state of the parser when shared by all
   functions */
/*词法扫描 记录当前的行号、符号、期望的下一个符号、读取字符串或者数字等*/
typedef struct LexState {
  int current;			//当前字符
  int linenumber;		//输入的行数 input line counter */
  int lastline;			//已解析的最后一行
  Token t;				//当前标记
  Token lookahead;		//前一个标记
  struct FuncState *fs;		//指向当前解析的函数
  struct lua_State *L;		//指向Lua状态机
  ZIO *z;				//输入流
  Mbuffer *buff;		//标记缓存区
  Table *h;				//扫描器表 收集字符串 重用
  struct Dyndata *dyd;  //动态结构使用的解析器 dynamic structures used by the parser */
  TString *source;		//当前源代码名称
  TString *envn;		//环境变量名
} LexState;


LUAI_FUNC void luaX_init (lua_State *L);
LUAI_FUNC void luaX_setinput (lua_State *L, LexState *ls, ZIO *z,
                              TString *source, int firstchar);
LUAI_FUNC TString *luaX_newstring (LexState *ls, const char *str, size_t l);
LUAI_FUNC void luaX_next (LexState *ls);
LUAI_FUNC int luaX_lookahead (LexState *ls);
LUAI_FUNC l_noret luaX_syntaxerror (LexState *ls, const char *s);
LUAI_FUNC const char *luaX_token2str (LexState *ls, int token);


#endif
