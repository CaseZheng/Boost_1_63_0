/*
** $Id: llex.c,v 2.96 2016/05/02 14:02:12 roberto Exp $
** Lexical Analyzer
** See Copyright Notice in lua.h
*/

#define llex_c
#define LUA_CORE

#include "lprefix.h"


#include <locale.h>
#include <string.h>

#include "lua.h"

#include "lctype.h"
#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "llex.h"
#include "lobject.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "lzio.h"


//获得下一个字符
#define next(ls) (ls->current = zgetc(ls->z))



#define currIsNewline(ls)	(ls->current == '\n' || ls->current == '\r')


/*Lua 关键字*/
static const char *const luaX_tokens [] = {
    "and", "break", "do", "else", "elseif",
    "end", "false", "for", "function", "goto", "if",
    "in", "local", "nil", "not", "or", "repeat",
    "return", "then", "true", "until", "while",
    "//", "..", "...", "==", ">=", "<=", "~=",
    "<<", ">>", "::", "<eof>",
    "<number>", "<integer>", "<name>", "<string>"
};


#define save_and_next(ls) (save(ls, ls->current), next(ls))


static l_noret lexerror (LexState *ls, const char *msg, int token);


//将字符c保存到buffer中
static void save (LexState *ls, int c) {
  Mbuffer *b = ls->buff;
  if (luaZ_bufflen(b) + 1 > luaZ_sizebuffer(b)) {
    size_t newsize;
    if (luaZ_sizebuffer(b) >= MAX_SIZE/2)
      lexerror(ls, "lexical element too long", 0);
    newsize = luaZ_sizebuffer(b) * 2;
    luaZ_resizebuffer(ls->L, b, newsize);
  }
  b->buffer[luaZ_bufflen(b)++] = cast(char, c);
}

//初始化词法分析所要使用的关键字串
void luaX_init (lua_State *L) {
  int i;
  TString *e = luaS_newliteral(L, LUA_ENV);		//创建"_ENV"字符串
  luaC_fix(L, obj2gco(e));						//设置e不会被回收
  for (i=0; i<NUM_RESERVED; i++) {
    TString *ts = luaS_new(L, luaX_tokens[i]);		//创建lua关键字字符串
    luaC_fix(L, obj2gco(ts));					//lua关键字 不会被回收
    ts->extra = cast_byte(i+1);					//lua关键字 短字符串 标记为lua关键字
  }
}


const char *luaX_token2str (LexState *ls, int token) {
  if (token < FIRST_RESERVED) {  /* single-byte symbols? */
    lua_assert(token == cast_uchar(token));
    return luaO_pushfstring(ls->L, "'%c'", token);
  }
  else {
    const char *s = luaX_tokens[token - FIRST_RESERVED];
    if (token < TK_EOS)  /* fixed format (symbols and reserved words)? */
      return luaO_pushfstring(ls->L, "'%s'", s);
    else  /* names, strings, and numerals */
      return s;
  }
}


static const char *txtToken (LexState *ls, int token) {
  switch (token) {
    case TK_NAME: case TK_STRING:
    case TK_FLT: case TK_INT:
      save(ls, '\0');
      return luaO_pushfstring(ls->L, "'%s'", luaZ_buffer(ls->buff));
    default:
      return luaX_token2str(ls, token);
  }
}


static l_noret lexerror (LexState *ls, const char *msg, int token) {
  msg = luaG_addinfo(ls->L, msg, ls->source, ls->linenumber);
  if (token)
    luaO_pushfstring(ls->L, "%s near %s", msg, txtToken(ls, token));
  luaD_throw(ls->L, LUA_ERRSYNTAX);
}


l_noret luaX_syntaxerror (LexState *ls, const char *msg) {
  lexerror(ls, msg, ls->t.token);
}


/*
** creates a new string and anchors it in scanner's table so that
** it will not be collected until the end of the compilation
** (by that time it should be anchored somewhere)
*/
TString *luaX_newstring (LexState *ls, const char *str, size_t l) {
  lua_State *L = ls->L;
  TValue *o;  /* entry for 'str' */
  TString *ts = luaS_newlstr(L, str, l);  /* create new string */
  setsvalue2s(L, L->top++, ts);  /* temporarily anchor it in stack */
  o = luaH_set(L, ls->h, L->top - 1);
  if (ttisnil(o)) {  /* not in use yet? */
    /* boolean value does not need GC barrier;
       table has no metatable, so it does not need to invalidate cache */
    setbvalue(o, 1);  /* t[string] = true */
    luaC_checkGC(L);
  }
  else {  /* string already present */
    ts = tsvalue(keyfromval(o));  /* re-use value previously stored */
  }
  L->top--;  /* remove string from stack */
  return ts;
}


/*
** increment line number and skips newline sequence (any of
** \n, \r, \n\r, or \r\n)
增加行号 并 跳过换行符
*/
static void inclinenumber (LexState *ls) {
  int old = ls->current;
  lua_assert(currIsNewline(ls));
  next(ls);										//跳过\n 或 \r
  if (currIsNewline(ls) && ls->current != old)	//当前字符是\r或\n 而且这个新的字符与前一个字符不同 跳过
    next(ls);									//跳过'\n\r' 或 '\r\n'
  if (++ls->linenumber >= MAX_INT)				//输入行数+1  如果超出了限制 报错
    lexerror(ls, "chunk has too many lines", 0);
}


void luaX_setinput (lua_State *L, LexState *ls, ZIO *z, TString *source,
                    int firstchar) {
  ls->t.token = 0;
  ls->L = L;							//设置扫描状态机关联的Lua状态机为L
  ls->current = firstchar;				//设置当前字符为第一个字符
  ls->lookahead.token = TK_EOS;			//设置没有前一个标记
  ls->z = z;
  ls->fs = NULL;
  ls->linenumber = 1;
  ls->lastline = 1;
  ls->source = source;								//设置源代码名称
  ls->envn = luaS_newliteral(L, LUA_ENV);				//设置环境变量名
  luaZ_resizebuffer(ls->L, ls->buff, LUA_MINBUFFER);	//初始化缓存
}



/*
** =======================================================
** LEXICAL ANALYZER
** =======================================================
*/


static int check_next1 (LexState *ls, int c) {
  if (ls->current == c) {
    next(ls);
    return 1;
  }
  else return 0;
}


/*
** Check whether current char is in set 'set' (with two chars) and
** saves it
*/
static int check_next2 (LexState *ls, const char *set) {
  lua_assert(set[2] == '\0');
  if (ls->current == set[0] || ls->current == set[1]) {
    save_and_next(ls);
    return 1;
  }
  else return 0;
}


/* LUA_NUMBER */
/*
** this function is quite liberal in what it accepts, as 'luaO_str2num'
** will reject ill-formed numerals.
读取数字
*/
static int read_numeral (LexState *ls, SemInfo *seminfo) {
  TValue obj;
  const char *expo = "Ee";
  int first = ls->current;
  lua_assert(lisdigit(ls->current));
  save_and_next(ls);
  if (first == '0' && check_next2(ls, "xX"))		//16进制
    expo = "Pp";									//用于检查16进制数的指数部分
  for (;;) {
    if (check_next2(ls, expo))						//检查指数部分
      check_next2(ls, "-+");  /* optional exponent sign */
    if (lisxdigit(ls->current))						//16进制数字包括0-9
      save_and_next(ls);
    else if (ls->current == '.')					//'.'
      save_and_next(ls);
    else break;										//既不是数字 也不是'.' 结束数字的读取
  }
  save(ls, '\0');									//给数字后面写入'\0'方便C语言字符串转数字
  if (luaO_str2num(luaZ_buffer(ls->buff), &obj) == 0)  /* format error? */
    lexerror(ls, "malformed number", TK_FLT);
  if (ttisinteger(&obj)) {							//如果是整数 保存 返回类型整数
    seminfo->i = ivalue(&obj);
    return TK_INT;
  }
  else {											//转化之后 不是整数 那就是浮点数 保存 返回类型浮点数
    lua_assert(ttisfloat(&obj));
    seminfo->r = fltvalue(&obj);
    return TK_FLT;
  }
}


/*
** skip a sequence '[=*[' or ']=*]'; if sequence is well formed, return
** its number of '='s; otherwise, return a negative number (-1 iff there
** are no '='s after initial bracket)
*/
static int skip_sep (LexState *ls) {
  int count = 0;
  int s = ls->current;					//记录等号开始前的字符
  lua_assert(s == '[' || s == ']');
  save_and_next(ls);
  while (ls->current == '=') {			//略过--[===[ ]===] 中[[ 或 ]] 中间的'='号
    save_and_next(ls);					//保存当前字符 并 得到下一个字符
    count++;							//‘=’数量+1
  }
  //'='开始前一个字符域'='结束后一个字符一样 返回'='号符号个数 否则返回负数
  return (ls->current == s) ? count : (-count) - 1;
}

//读取长字符串
static void read_long_string (LexState *ls, SemInfo *seminfo, int sep) {
  int line = ls->linenumber;	//保存注释开始的行号
  save_and_next(ls);			//保存第二个'['符号  --[===[注释]===]
  if (currIsNewline(ls))		//新的一行
    inclinenumber(ls);			//行号增加 跳过换行符
  for (;;) {
    switch (ls->current) {
      case EOZ: {				//读到EOZ  出错
        const char *what = (seminfo ? "string" : "comment");
        const char *msg = luaO_pushfstring(ls->L,
                     "unfinished long %s (starting at line %d)", what, line);
        lexerror(ls, msg, TK_EOS);
        break;  /* to avoid warnings */
      }
      case ']': {				//读到']'
        if (skip_sep(ls) == sep) {		//略过'=' 返回'='的个数
          save_and_next(ls);	//保存第二个']'
          goto endloop;		//跳转结束对长注释的
        }
        break;
      }
      case '\n': case '\r': {	//读到换行符
        save(ls, '\n');			//保存它
        inclinenumber(ls);		//新的一行
        if (!seminfo) luaZ_resetbuffer(ls->buff);		//seminfo 不存在 缓存区置空
        break;
      }
      default: {
        if (seminfo) save_and_next(ls);				//有seminfo 保存字符
        else next(ls);									//没有则不保存 直接读取下一个字符
      }
    }
  } endloop:
  if (seminfo)
    seminfo->ts = luaX_newstring(ls, luaZ_buffer(ls->buff) + (2 + sep),
                                     luaZ_bufflen(ls->buff) - 2*(2 + sep));	//将注释转化为字符串保存起来
}


static void esccheck (LexState *ls, int c, const char *msg) {
  if (!c) {
    if (ls->current != EOZ)
      save_and_next(ls);  /* add current to buffer for error message */
    lexerror(ls, msg, TK_STRING);
  }
}


static int gethexa (LexState *ls) {
  save_and_next(ls);
  esccheck (ls, lisxdigit(ls->current), "hexadecimal digit expected");
  return luaO_hexavalue(ls->current);
}


static int readhexaesc (LexState *ls) {
  int r = gethexa(ls);
  r = (r << 4) + gethexa(ls);
  luaZ_buffremove(ls->buff, 2);  /* remove saved chars from buffer */
  return r;
}


static unsigned long readutf8esc (LexState *ls) {
  unsigned long r;
  int i = 4;  /* chars to be removed: '\', 'u', '{', and first digit */
  save_and_next(ls);  /* skip 'u' */
  esccheck(ls, ls->current == '{', "missing '{'");
  r = gethexa(ls);  /* must have at least one digit */
  while ((save_and_next(ls), lisxdigit(ls->current))) {
    i++;
    r = (r << 4) + luaO_hexavalue(ls->current);
    esccheck(ls, r <= 0x10FFFF, "UTF-8 value too large");
  }
  esccheck(ls, ls->current == '}', "missing '}'");
  next(ls);  /* skip '}' */
  luaZ_buffremove(ls->buff, i);  /* remove saved chars from buffer */
  return r;
}


static void utf8esc (LexState *ls) {
  char buff[UTF8BUFFSZ];
  int n = luaO_utf8esc(buff, readutf8esc(ls));
  for (; n > 0; n--)  /* add 'buff' to string */
    save(ls, buff[UTF8BUFFSZ - n]);
}


static int readdecesc (LexState *ls) {
  int i;
  int r = 0;  /* result accumulator */
  for (i = 0; i < 3 && lisdigit(ls->current); i++) {  /* read up to 3 digits */
    r = 10*r + ls->current - '0';
    save_and_next(ls);
  }
  esccheck(ls, r <= UCHAR_MAX, "decimal escape too large");
  luaZ_buffremove(ls->buff, i);  /* remove read digits from buffer */
  return r;
}


static void read_string (LexState *ls, int del, SemInfo *seminfo) {
  save_and_next(ls);  /* keep delimiter (for error messages) */
  while (ls->current != del) {
    switch (ls->current) {
      case EOZ:
        lexerror(ls, "unfinished string", TK_EOS);
        break;  /* to avoid warnings */
      case '\n':
      case '\r':
        lexerror(ls, "unfinished string", TK_STRING);
        break;  /* to avoid warnings */
      case '\\': {  /* escape sequences */
        int c;  /* final character to be saved */
        save_and_next(ls);  /* keep '\\' for error messages */
        switch (ls->current) {
          case 'a': c = '\a'; goto read_save;
          case 'b': c = '\b'; goto read_save;
          case 'f': c = '\f'; goto read_save;
          case 'n': c = '\n'; goto read_save;
          case 'r': c = '\r'; goto read_save;
          case 't': c = '\t'; goto read_save;
          case 'v': c = '\v'; goto read_save;
          case 'x': c = readhexaesc(ls); goto read_save;
          case 'u': utf8esc(ls);  goto no_save;
          case '\n': case '\r':
            inclinenumber(ls); c = '\n'; goto only_save;
          case '\\': case '\"': case '\'':
            c = ls->current; goto read_save;
          case EOZ: goto no_save;  /* will raise an error next loop */
          case 'z': {  /* zap following span of spaces */
            luaZ_buffremove(ls->buff, 1);  /* remove '\\' */
            next(ls);  /* skip the 'z' */
            while (lisspace(ls->current)) {
              if (currIsNewline(ls)) inclinenumber(ls);
              else next(ls);
            }
            goto no_save;
          }
          default: {
            esccheck(ls, lisdigit(ls->current), "invalid escape sequence");
            c = readdecesc(ls);  /* digital escape '\ddd' */
            goto only_save;
          }
        }
       read_save:
         next(ls);
         /* go through */
       only_save:
         luaZ_buffremove(ls->buff, 1);  /* remove '\\' */
         save(ls, c);
         /* go through */
       no_save: break;
      }
      default:
        save_and_next(ls);
    }
  }
  save_and_next(ls);  /* skip delimiter */
  seminfo->ts = luaX_newstring(ls, luaZ_buffer(ls->buff) + 1,
                                   luaZ_bufflen(ls->buff) - 2);
}


static int llex (LexState *ls, SemInfo *seminfo) {
  luaZ_resetbuffer(ls->buff);
  for (;;) {
    switch (ls->current) {			//判断当前字符
      case '\n': case '\r': {		//当前行结束
        inclinenumber(ls);			//增加行号 跳过换行符
        break;
      }
      case ' ': case '\f': case '\t': case '\v': {	//空白字符
        next(ls);
        break;
      }
      case '-': {								// '-' 或 '--' (注释)
        next(ls);								//下一个字符
        if (ls->current != '-') return '-';		//下一个字符不为'-' 返回'-' 否则是注释
        next(ls);
		//长注释
        if (ls->current == '[') {				
          int sep = skip_sep(ls);
          luaZ_resetbuffer(ls->buff);			//重置缓存
          if (sep >= 0) {
            read_long_string(ls, NULL, sep);	//略过注释
            luaZ_resetbuffer(ls->buff);			//重置缓存
            break;
          }
        }
        //短注释
        while (!currIsNewline(ls) && ls->current != EOZ)
          next(ls);								//读取玩当前行 或者到输入数据结束
        break;
      }
      case '[': {								//一个符串或者一个'['字符
        int sep = skip_sep(ls);					//检查下'['后面'='和'['的情况
        if (sep >= 0) {							//字符串
          read_long_string(ls, seminfo, sep);	//读取这个字符串
          return TK_STRING;						//返回类型
        }
        else if (sep != -1)						//'['后面有'='但没有第二个'['  例如：[==fefe
          lexerror(ls, "invalid long string delimiter", TK_STRING);
        return '[';								//返回'['字符
      }
      case '=': {								//'='或"=="
        next(ls);
        if (check_next1(ls, '=')) return TK_EQ;	//"==" 等于判断
        else return '=';						//"="赋值
      }
      case '<': {
        next(ls);
        if (check_next1(ls, '=')) return TK_LE;
        else if (check_next1(ls, '<')) return TK_SHL;
        else return '<';
      }
      case '>': {
        next(ls);
        if (check_next1(ls, '=')) return TK_GE;
        else if (check_next1(ls, '>')) return TK_SHR;
        else return '>';
      }
      case '/': {
        next(ls);
        if (check_next1(ls, '/')) return TK_IDIV;
        else return '/';
      }
      case '~': {
        next(ls);
        if (check_next1(ls, '=')) return TK_NE;
        else return '~';
      }
      case ':': {
        next(ls);
        if (check_next1(ls, ':')) return TK_DBCOLON;	//"::"
        else return ':';
      }
      case '"': case '\'': {				//字符串
        read_string(ls, ls->current, seminfo);
        return TK_STRING;
      }
      case '.': {							// '.', '..', '...', 或者 number
        save_and_next(ls);
        if (check_next1(ls, '.')) {
          if (check_next1(ls, '.'))
            return TK_DOTS;					//'...'不定参数
          else return TK_CONCAT;			//'..'字符串连接
        }
        else if (!lisdigit(ls->current)) return '.';	//'.'后不是数字 返回'.'符号
        else return read_numeral(ls, seminfo);			//读取数字
      }
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9': {
        return read_numeral(ls, seminfo);			//读取数字
      }
      case EOZ: {									//输入结束
        return TK_EOS;
      }
      default: {
        if (lislalpha(ls->current)) {		//标识符或保留字
          TString *ts;
          do {
            save_and_next(ls);
          } while (lislalnum(ls->current));
          ts = luaX_newstring(ls, luaZ_buffer(ls->buff),
                                  luaZ_bufflen(ls->buff));
          seminfo->ts = ts;
          if (isreserved(ts))				//保留字
            return ts->extra - 1 + FIRST_RESERVED;	//返回保留字的编号
          else {
            return TK_NAME;					//不算保留字 则为标识符
          }
        }
        else {								//单字符标记 (+ - / ...)
          int c = ls->current;
          next(ls);
          return c;
        }
      }
    }
  }
}

//读取下一个标记
void luaX_next (LexState *ls) {
  ls->lastline = ls->linenumber;
  if (ls->lookahead.token != TK_EOS) {			//有前一个标记
    ls->t = ls->lookahead;  /* use this one */
    ls->lookahead.token = TK_EOS;  /* and discharge it */
  }
  else
    ls->t.token = llex(ls, &ls->t.seminfo);  //读取下一个标记
}


int luaX_lookahead (LexState *ls) {
  lua_assert(ls->lookahead.token == TK_EOS);
  ls->lookahead.token = llex(ls, &ls->lookahead.seminfo);
  return ls->lookahead.token;
}

