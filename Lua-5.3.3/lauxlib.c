/*
** $Id: lauxlib.c,v 1.286 2016/01/08 15:33:09 roberto Exp $
** Auxiliary functions for building Lua libraries
** See Copyright Notice in lua.h
*/

#define lauxlib_c
#define LUA_LIB

#include "lprefix.h"


#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/*
** This file uses only the official API of Lua.
** Any function declared here could be written as an application function.
*/

#include "lua.h"

#include "lauxlib.h"


/*
** {======================================================
** Traceback
** =======================================================
*/


#define LEVELS1	10	/* size of the first part of the stack */
#define LEVELS2	11	/* size of the second part of the stack */



/*
** search for 'objidx' in table at index -1.
** return 1 + string at top if find a good name.
*/
static int findfield (lua_State *L, int objidx, int level) {
  if (level == 0 || !lua_istable(L, -1))
    return 0;  /* not found */
  lua_pushnil(L);  /* start 'next' loop */
  while (lua_next(L, -2)) {  /* for each pair in table */
    if (lua_type(L, -2) == LUA_TSTRING) {  /* ignore non-string keys */
      if (lua_rawequal(L, objidx, -1)) {  /* found object? */
        lua_pop(L, 1);  /* remove value (but keep name) */
        return 1;
      }
      else if (findfield(L, objidx, level - 1)) {  /* try recursively */
        lua_remove(L, -2);  /* remove table (but keep name) */
        lua_pushliteral(L, ".");
        lua_insert(L, -2);  /* place '.' between the two names */
        lua_concat(L, 3);
        return 1;
      }
    }
    lua_pop(L, 1);  /* remove value */
  }
  return 0;  /* not found */
}


/*
** Search for a name for a function in all loaded modules
** (registry._LOADED).
*/
static int pushglobalfuncname (lua_State *L, lua_Debug *ar) {
  int top = lua_gettop(L);
  lua_getinfo(L, "f", ar);  /* push function */
  lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
  if (findfield(L, top + 1, 2)) {
    const char *name = lua_tostring(L, -1);
    if (strncmp(name, "_G.", 3) == 0) {  /* name start with '_G.'? */
      lua_pushstring(L, name + 3);  /* push name without prefix */
      lua_remove(L, -2);  /* remove original name */
    }
    lua_copy(L, -1, top + 1);  /* move name to proper place */
    lua_pop(L, 2);  /* remove pushed values */
    return 1;
  }
  else {
    lua_settop(L, top);  /* remove function and global table */
    return 0;
  }
}


static void pushfuncname (lua_State *L, lua_Debug *ar) {
  if (pushglobalfuncname(L, ar)) {  /* try first a global name */
    lua_pushfstring(L, "function '%s'", lua_tostring(L, -1));
    lua_remove(L, -2);  /* remove name */
  }
  else if (*ar->namewhat != '\0')  /* is there a name from code? */
    lua_pushfstring(L, "%s '%s'", ar->namewhat, ar->name);  /* use it */
  else if (*ar->what == 'm')  /* main? */
      lua_pushliteral(L, "main chunk");
  else if (*ar->what != 'C')  /* for Lua functions, use <file:line> */
    lua_pushfstring(L, "function <%s:%d>", ar->short_src, ar->linedefined);
  else  /* nothing left... */
    lua_pushliteral(L, "?");
}


static int lastlevel (lua_State *L) {
  lua_Debug ar;
  int li = 1, le = 1;
  /* find an upper bound */
  while (lua_getstack(L, le, &ar)) { li = le; le *= 2; }
  /* do a binary search */
  while (li < le) {
    int m = (li + le)/2;
    if (lua_getstack(L, m, &ar)) li = m + 1;
    else le = m;
  }
  return le - 1;
}


LUALIB_API void luaL_traceback (lua_State *L, lua_State *L1,
                                const char *msg, int level) {
  lua_Debug ar;
  int top = lua_gettop(L);
  int last = lastlevel(L1);
  int n1 = (last - level > LEVELS1 + LEVELS2) ? LEVELS1 : -1;
  if (msg)
    lua_pushfstring(L, "%s\n", msg);
  luaL_checkstack(L, 10, NULL);
  lua_pushliteral(L, "stack traceback:");
  while (lua_getstack(L1, level++, &ar)) {
    if (n1-- == 0) {  /* too many levels? */
      lua_pushliteral(L, "\n\t...");  /* add a '...' */
      level = last - LEVELS2 + 1;  /* and skip to last ones */
    }
    else {
      lua_getinfo(L1, "Slnt", &ar);
      lua_pushfstring(L, "\n\t%s:", ar.short_src);
      if (ar.currentline > 0)
        lua_pushfstring(L, "%d:", ar.currentline);
      lua_pushliteral(L, " in ");
      pushfuncname(L, &ar);
      if (ar.istailcall)
        lua_pushliteral(L, "\n\t(...tail calls...)");
      lua_concat(L, lua_gettop(L) - top);
    }
  }
  lua_concat(L, lua_gettop(L) - top);
}

/* }====================================================== */


/*
** {======================================================
** Error-report functions
** =======================================================
*/

LUALIB_API int luaL_argerror (lua_State *L, int arg, const char *extramsg) {
  lua_Debug ar;
  if (!lua_getstack(L, 0, &ar))  /* no stack frame? */
    return luaL_error(L, "bad argument #%d (%s)", arg, extramsg);
  lua_getinfo(L, "n", &ar);
  if (strcmp(ar.namewhat, "method") == 0) {
    arg--;  /* do not count 'self' */
    if (arg == 0)  /* error is in the self argument itself? */
      return luaL_error(L, "calling '%s' on bad self (%s)",
                           ar.name, extramsg);
  }
  if (ar.name == NULL)
    ar.name = (pushglobalfuncname(L, &ar)) ? lua_tostring(L, -1) : "?";
  return luaL_error(L, "bad argument #%d to '%s' (%s)",
                        arg, ar.name, extramsg);
}


static int typeerror (lua_State *L, int arg, const char *tname) {
  const char *msg;
  const char *typearg;  /* name for the type of the actual argument */
  if (luaL_getmetafield(L, arg, "__name") == LUA_TSTRING)
    typearg = lua_tostring(L, -1);  /* use the given type name */
  else if (lua_type(L, arg) == LUA_TLIGHTUSERDATA)
    typearg = "light userdata";  /* special name for messages */
  else
    typearg = luaL_typename(L, arg);  /* standard name */
  msg = lua_pushfstring(L, "%s expected, got %s", tname, typearg);
  return luaL_argerror(L, arg, msg);
}


static void tag_error (lua_State *L, int arg, int tag) {
  typeerror(L, arg, lua_typename(L, tag));
}


/*
** The use of 'lua_pushfstring' ensures this function does not
** need reserved stack space when called.
*/
LUALIB_API void luaL_where (lua_State *L, int level) {
  lua_Debug ar;
  if (lua_getstack(L, level, &ar)) {  /* check function at level */
    lua_getinfo(L, "Sl", &ar);  /* get info about it */
    if (ar.currentline > 0) {  /* is there info? */
      lua_pushfstring(L, "%s:%d: ", ar.short_src, ar.currentline);
      return;
    }
  }
  lua_pushfstring(L, "");  /* else, no information available... */
}


/*
** Again, the use of 'lua_pushvfstring' ensures this function does
** not need reserved stack space when called. (At worst, it generates
** an error with "stack overflow" instead of the given message.)
*/
LUALIB_API int luaL_error (lua_State *L, const char *fmt, ...) {
  va_list argp;
  va_start(argp, fmt);
  luaL_where(L, 1);
  lua_pushvfstring(L, fmt, argp);
  va_end(argp);
  lua_concat(L, 2);
  return lua_error(L);
}


LUALIB_API int luaL_fileresult (lua_State *L, int stat, const char *fname) {
  int en = errno;  /* calls to Lua API may change this value */
  if (stat) {
    lua_pushboolean(L, 1);
    return 1;
  }
  else {
    lua_pushnil(L);
    if (fname)
      lua_pushfstring(L, "%s: %s", fname, strerror(en));
    else
      lua_pushstring(L, strerror(en));
    lua_pushinteger(L, en);
    return 3;
  }
}


#if !defined(l_inspectstat)	/* { */

#if defined(LUA_USE_POSIX)

#include <sys/wait.h>

/*
** use appropriate macros to interpret 'pclose' return status
*/
#define l_inspectstat(stat,what)  \
   if (WIFEXITED(stat)) { stat = WEXITSTATUS(stat); } \
   else if (WIFSIGNALED(stat)) { stat = WTERMSIG(stat); what = "signal"; }

#else

#define l_inspectstat(stat,what)  /* no op */

#endif

#endif				/* } */


LUALIB_API int luaL_execresult (lua_State *L, int stat) {
  const char *what = "exit";  /* type of termination */
  if (stat == -1)  /* error? */
    return luaL_fileresult(L, 0, NULL);
  else {
    l_inspectstat(stat, what);  /* interpret result */
    if (*what == 'e' && stat == 0)  /* successful termination? */
      lua_pushboolean(L, 1);
    else
      lua_pushnil(L);
    lua_pushstring(L, what);
    lua_pushinteger(L, stat);
    return 3;  /* return true/nil,what,code */
  }
}

/* }====================================================== */


/*
** {======================================================
** Userdata's metatable manipulation
** =======================================================
*/

LUALIB_API int luaL_newmetatable (lua_State *L, const char *tname) {
  if (luaL_getmetatable(L, tname) != LUA_TNIL)  /* name already in use? */
    return 0;  /* leave previous value on top, but return 0 */
  lua_pop(L, 1);
  lua_createtable(L, 0, 2);  /* create metatable */
  lua_pushstring(L, tname);
  lua_setfield(L, -2, "__name");  /* metatable.__name = tname */
  lua_pushvalue(L, -1);
  lua_setfield(L, LUA_REGISTRYINDEX, tname);  /* registry.name = metatable */
  return 1;
}


LUALIB_API void luaL_setmetatable (lua_State *L, const char *tname) {
  luaL_getmetatable(L, tname);
  lua_setmetatable(L, -2);
}


LUALIB_API void *luaL_testudata (lua_State *L, int ud, const char *tname) {
  void *p = lua_touserdata(L, ud);
  if (p != NULL) {  /* value is a userdata? */
    if (lua_getmetatable(L, ud)) {  /* does it have a metatable? */
      luaL_getmetatable(L, tname);  /* get correct metatable */
      if (!lua_rawequal(L, -1, -2))  /* not the same? */
        p = NULL;  /* value is a userdata with wrong metatable */
      lua_pop(L, 2);  /* remove both metatables */
      return p;
    }
  }
  return NULL;  /* value is not a userdata with a metatable */
}


LUALIB_API void *luaL_checkudata (lua_State *L, int ud, const char *tname) {
  void *p = luaL_testudata(L, ud, tname);
  if (p == NULL) typeerror(L, ud, tname);
  return p;
}

/* }====================================================== */


/*
** {======================================================
** Argument check functions
** =======================================================
*/

LUALIB_API int luaL_checkoption (lua_State *L, int arg, const char *def,
                                 const char *const lst[]) {
  const char *name = (def) ? luaL_optstring(L, arg, def) :
                             luaL_checkstring(L, arg);
  int i;
  for (i=0; lst[i]; i++)
    if (strcmp(lst[i], name) == 0)
      return i;
  return luaL_argerror(L, arg,
                       lua_pushfstring(L, "invalid option '%s'", name));
}


//将栈空间扩展到 top + space 个元素。 如果扩展不了，则抛出一个错误。
//msg 是用于错误消息的额外文本 （NULL 表示不需要额外文本）
LUALIB_API void luaL_checkstack (lua_State *L, int space, const char *msg) {
  if (!lua_checkstack(L, space)) {						//检查栈上面有space个额外位置 如果没有就扩展下  如果出错输出错误信息
    if (msg)
      luaL_error(L, "stack overflow (%s)", msg);
    else
      luaL_error(L, "stack overflow");
  }
}


LUALIB_API void luaL_checktype (lua_State *L, int arg, int t) {
  if (lua_type(L, arg) != t)
    tag_error(L, arg, t);
}


LUALIB_API void luaL_checkany (lua_State *L, int arg) {
  if (lua_type(L, arg) == LUA_TNONE)
    luaL_argerror(L, arg, "value expected");
}


LUALIB_API const char *luaL_checklstring (lua_State *L, int arg, size_t *len) {
  const char *s = lua_tolstring(L, arg, len);
  if (!s) tag_error(L, arg, LUA_TSTRING);
  return s;
}


LUALIB_API const char *luaL_optlstring (lua_State *L, int arg,
                                        const char *def, size_t *len) {
  if (lua_isnoneornil(L, arg)) {
    if (len)
      *len = (def ? strlen(def) : 0);
    return def;
  }
  else return luaL_checklstring(L, arg, len);
}


LUALIB_API lua_Number luaL_checknumber (lua_State *L, int arg) {
  int isnum;
  lua_Number d = lua_tonumberx(L, arg, &isnum);
  if (!isnum)
    tag_error(L, arg, LUA_TNUMBER);
  return d;
}


LUALIB_API lua_Number luaL_optnumber (lua_State *L, int arg, lua_Number def) {
  return luaL_opt(L, luaL_checknumber, arg, def);
}


static void interror (lua_State *L, int arg) {
  if (lua_isnumber(L, arg))
    luaL_argerror(L, arg, "number has no integer representation");
  else
    tag_error(L, arg, LUA_TNUMBER);
}


LUALIB_API lua_Integer luaL_checkinteger (lua_State *L, int arg) {
  int isnum;
  lua_Integer d = lua_tointegerx(L, arg, &isnum);
  if (!isnum) {
    interror(L, arg);
  }
  return d;
}


LUALIB_API lua_Integer luaL_optinteger (lua_State *L, int arg,
                                                      lua_Integer def) {
  return luaL_opt(L, luaL_checkinteger, arg, def);
}

/* }====================================================== */


/*
** {======================================================
** Generic Buffer manipulation
** =======================================================
*/

/* userdata to box arbitrary data */
typedef struct UBox {
  void *box;
  size_t bsize;
} UBox;


static void *resizebox (lua_State *L, int idx, size_t newsize) {
  void *ud;
  lua_Alloc allocf = lua_getallocf(L, &ud);
  UBox *box = (UBox *)lua_touserdata(L, idx);
  void *temp = allocf(ud, box->box, box->bsize, newsize);
  if (temp == NULL && newsize > 0) {  /* allocation error? */
    resizebox(L, idx, 0);  /* free buffer */
    luaL_error(L, "not enough memory for buffer allocation");
  }
  box->box = temp;
  box->bsize = newsize;
  return temp;
}


static int boxgc (lua_State *L) {
  resizebox(L, 1, 0);
  return 0;
}


static void *newbox (lua_State *L, size_t newsize) {
  UBox *box = (UBox *)lua_newuserdata(L, sizeof(UBox));
  box->box = NULL;
  box->bsize = 0;
  if (luaL_newmetatable(L, "LUABOX")) {  /* creating metatable? */
    lua_pushcfunction(L, boxgc);
    lua_setfield(L, -2, "__gc");  /* metatable.__gc = boxgc */
  }
  lua_setmetatable(L, -2);
  return resizebox(L, -1, newsize);
}


/*
** check whether buffer is using a userdata on the stack as a temporary
** buffer
*/
#define buffonstack(B)	((B)->b != (B)->initb)


/*
** returns a pointer to a free area with at least 'sz' bytes
*/
LUALIB_API char *luaL_prepbuffsize (luaL_Buffer *B, size_t sz) {
  lua_State *L = B->L;
  if (B->size - B->n < sz) {  /* not enough space? */
    char *newbuff;
    size_t newsize = B->size * 2;  /* double buffer size */
    if (newsize - B->n < sz)  /* not big enough? */
      newsize = B->n + sz;
    if (newsize < B->n || newsize - B->n < sz)
      luaL_error(L, "buffer too large");
    /* create larger buffer */
    if (buffonstack(B))
      newbuff = (char *)resizebox(L, -1, newsize);
    else {  /* no buffer yet */
      newbuff = (char *)newbox(L, newsize);
      memcpy(newbuff, B->b, B->n * sizeof(char));  /* copy original content */
    }
    B->b = newbuff;
    B->size = newsize;
  }
  return &B->b[B->n];
}


LUALIB_API void luaL_addlstring (luaL_Buffer *B, const char *s, size_t l) {
  if (l > 0) {  /* avoid 'memcpy' when 's' can be NULL */
    char *b = luaL_prepbuffsize(B, l);
    memcpy(b, s, l * sizeof(char));
    luaL_addsize(B, l);
  }
}


LUALIB_API void luaL_addstring (luaL_Buffer *B, const char *s) {
  luaL_addlstring(B, s, strlen(s));
}


LUALIB_API void luaL_pushresult (luaL_Buffer *B) {
  lua_State *L = B->L;
  lua_pushlstring(L, B->b, B->n);
  if (buffonstack(B)) {
    resizebox(L, -2, 0);  /* delete old buffer */
    lua_remove(L, -2);  /* remove its header from the stack */
  }
}


LUALIB_API void luaL_pushresultsize (luaL_Buffer *B, size_t sz) {
  luaL_addsize(B, sz);
  luaL_pushresult(B);
}


LUALIB_API void luaL_addvalue (luaL_Buffer *B) {
  lua_State *L = B->L;
  size_t l;
  const char *s = lua_tolstring(L, -1, &l);
  if (buffonstack(B))
    lua_insert(L, -2);  /* put value below buffer */
  luaL_addlstring(B, s, l);
  lua_remove(L, (buffonstack(B)) ? -2 : -1);  /* remove value */
}


LUALIB_API void luaL_buffinit (lua_State *L, luaL_Buffer *B) {
  B->L = L;
  B->b = B->initb;
  B->n = 0;
  B->size = LUAL_BUFFERSIZE;
}


LUALIB_API char *luaL_buffinitsize (lua_State *L, luaL_Buffer *B, size_t sz) {
  luaL_buffinit(L, B);
  return luaL_prepbuffsize(B, sz);
}

/* }====================================================== */


/*
** {======================================================
** Reference system
** =======================================================
*/

/* index of free-list header */
#define freelist	0

/*
针对栈顶的对象，创建并返回一个在索引 t 指向的表中的 引用 （最后会弹出栈顶对象）。
此引用是一个唯一的整数键。 只要你不向表 t 手工添加整数键， luaL_ref 可以保证它返回的键的唯一性。 
你可以通过调用 lua_rawgeti(L, t, r) 来找回由 r 引用的对象。 函数 luaL_unref 用来释放一个引用关联的对象
如果栈顶的对象是 nil， luaL_ref 将返回常量 LUA_REFNIL。 常量 LUA_NOREF 可以保证和 luaL_ref 能返回的其它引用值不同
*/
LUALIB_API int luaL_ref (lua_State *L, int t) {
  int ref;
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);  /* remove from stack */
    return LUA_REFNIL;  /* 'nil' has a unique fixed reference */
  }
  t = lua_absindex(L, t);
  lua_rawgeti(L, t, freelist);  /* get first free element */
  ref = (int)lua_tointeger(L, -1);  /* ref = t[freelist] */
  lua_pop(L, 1);  /* remove it from stack */
  if (ref != 0) {  /* any free element? */
    lua_rawgeti(L, t, ref);  /* remove it from list */
    lua_rawseti(L, t, freelist);  /* (t[freelist] = t[ref]) */
  }
  else  /* no free elements */
    ref = (int)lua_rawlen(L, t) + 1;  /* get a new reference */
  lua_rawseti(L, t, ref);
  return ref;
}


LUALIB_API void luaL_unref (lua_State *L, int t, int ref) {
  if (ref >= 0) {
    t = lua_absindex(L, t);
    lua_rawgeti(L, t, freelist);
    lua_rawseti(L, t, ref);  /* t[ref] = t[freelist] */
    lua_pushinteger(L, ref);
    lua_rawseti(L, t, freelist);  /* t[freelist] = ref */
  }
}

/* }====================================================== */


/*
** {======================================================
** Load functions
** =======================================================
*/

typedef struct LoadF {
  int n;				//读取的缓存中还未返回的数据的大小
  FILE *f;				//文件描述符 句柄
  char buff[BUFSIZ];	//读取的缓存
} LoadF;

//从流中读取数据
static const char *getF (lua_State *L, void *ud, size_t *size) {
  LoadF *lf = (LoadF *)ud;
  (void)L;
  if (lf->n > 0) {		//有预读的数据
    *size = lf->n;		//将数据大小返回
    lf->n = 0;			//预读数据大小置为0
  }
  else {
    if (feof(lf->f)) return NULL;			//检查下流上的文件结束符 如果结束返回NULL
    *size = fread(lf->buff, 1, sizeof(lf->buff), lf->f);	//从文件流读取数据
  }
  return lf->buff;
}


static int errfile (lua_State *L, const char *what, int fnameindex) {
  const char *serr = strerror(errno);
  const char *filename = lua_tostring(L, fnameindex) + 1;
  lua_pushfstring(L, "cannot %s %s: %s", what, filename, serr);
  lua_remove(L, fnameindex);
  return LUA_ERRFILE;
}

//越过utf-8 BOM
static int skipBOM (LoadF *lf) {
  const char *p = "\xEF\xBB\xBF";  /* UTF-8 BOM mark */
  int c;
  lf->n = 0;
  do {
    c = getc(lf->f);				//从文件中读取一个字符
    if (c == EOF || c != *(const unsigned char *)p++) return c;
    lf->buff[lf->n++] = c;  /* to be read by the parser */
  } while (*p != '\0');
  lf->n = 0;  /* prefix matched; discard it */
  return getc(lf->f);  /* return next character */
}


/*
** reads the first character of file 'f' and skips an optional BOM mark
** in its beginning plus its first line if it starts with '#'. Returns
** true if it skipped the first line.  In any case, '*cp' has the
** first "valid" character of the file (after the optional BOM and
** a first-line comment).
读取文件'f'的第一个字符，并在其开头加上其第一行（如果以“＃”开头）
跳过可选的BOM标记。 如果跳过第一行，则返回true。 在任何情况下，'* cp'
都有文件的第一个“有效”字符（在可选BOM和第一行注释之后）
*/
static int skipcomment (LoadF *lf, int *cp) {
  int c = *cp = skipBOM(lf);
  if (c == '#') {  /* first line is a comment (Unix exec. file)? */
    do {  /* skip first line */
      c = getc(lf->f);
    } while (c != EOF && c != '\n');
    *cp = getc(lf->f);  /* skip end-of-line, if present */
    return 1;  /* there was a comment */
  }
  else return 0;  /* no comment */
}

//把一个文件加载为 Lua 代码块。 这个函数使用 lua_load 加载文件中的数据。 
//代码块的名字被命名为 filename。 如果 filename 为 NULL， 它从标准输入加载。
//如果文件的第一行以 # 打头，则忽略这一行。mode 字符串的作用同函数 lua_load。
//此函数的返回值和 lua_load 相同， 不过它还可能产生一个叫做 LUA_ERRFILE 的出错码。
//这种错误发生于无法打开或读入文件时，或是文件的模式错误。
//和 lua_load 一样，这个函数仅加载代码块不运行。
LUALIB_API int luaL_loadfilex (lua_State *L, const char *filename,
                                             const char *mode) {
  LoadF lf;
  int status, readstatus;
  int c;
  int fnameindex = lua_gettop(L) + 1;		//记录栈顶+1
  if (filename == NULL) {					//文件名为null
    lua_pushliteral(L, "=stdin");			//将" =stdin"压入栈
    lf.f = stdin;							//设置文件描述符为标准输入
  }
  else {
    lua_pushfstring(L, "@%s", filename);	//将文件名压入栈中  压入后字符串类似 @main.lua
    lf.f = fopen(filename, "r");			//只读打开文件  保存文件描述符
    if (lf.f == NULL) return errfile(L, "open", fnameindex);
  }
  if (skipcomment(&lf, &c))  /* read initial portion */
    lf.buff[lf.n++] = '\n';  /* add line to correct line numbers */
  if (c == LUA_SIGNATURE[0] && filename) {  /* binary file? */
    lf.f = freopen(filename, "rb", lf.f);  /* reopen in binary mode */
    if (lf.f == NULL) return errfile(L, "reopen", fnameindex);
    skipcomment(&lf, &c);  /* re-read initial portion */
  }
  if (c != EOF)
    lf.buff[lf.n++] = c;  /* 'c' is the first character of the stream */
  status = lua_load(L, getF, &lf, lua_tostring(L, -1), mode);
  readstatus = ferror(lf.f);
  if (filename) fclose(lf.f);  /* close file (even in case of errors) */
  if (readstatus) {
    lua_settop(L, fnameindex);  /* ignore results from 'lua_load' */
    return errfile(L, "read", fnameindex);
  }
  lua_remove(L, fnameindex);
  return status;
}


typedef struct LoadS {
  const char *s;
  size_t size;
} LoadS;


static const char *getS (lua_State *L, void *ud, size_t *size) {
  LoadS *ls = (LoadS *)ud;
  (void)L;  /* not used */
  if (ls->size == 0) return NULL;
  *size = ls->size;
  ls->size = 0;
  return ls->s;
}

//把一段缓存加载为一个 Lua 代码块。 这个函数使用 lua_load 来加载 buff 指向的长度为 sz 的内存区。
//这个函数和 lua_load 返回值相同。 name 作为代码块的名字，用于调试信息和错误消息。 
//mode 字符串的作用同函数 lua_load。
LUALIB_API int luaL_loadbufferx (lua_State *L, const char *buff, size_t size,
                                 const char *name, const char *mode) {
  LoadS ls;
  ls.s = buff;
  ls.size = size;
  return lua_load(L, getS, &ls, name, mode);
}

//将一个字符串加载为 Lua 代码块。 这个函数使用 lua_load 加载一个零结尾的字符串 s。
//此函数的返回值和 lua_load 相同。
//也和 lua_load 一样，这个函数仅加载代码块不运行。
LUALIB_API int luaL_loadstring (lua_State *L, const char *s) {
  return luaL_loadbuffer(L, s, strlen(s), s);
}

/* }====================================================== */



LUALIB_API int luaL_getmetafield (lua_State *L, int obj, const char *event) {
  if (!lua_getmetatable(L, obj))  /* no metatable? */
    return LUA_TNIL;
  else {
    int tt;
    lua_pushstring(L, event);
    tt = lua_rawget(L, -2);
    if (tt == LUA_TNIL)  /* is metafield nil? */
      lua_pop(L, 2);  /* remove metatable and metafield */
    else
      lua_remove(L, -2);  /* remove only metatable */
    return tt;  /* return metafield type */
  }
}


LUALIB_API int luaL_callmeta (lua_State *L, int obj, const char *event) {
  obj = lua_absindex(L, obj);
  if (luaL_getmetafield(L, obj, event) == LUA_TNIL)  /* no metafield? */
    return 0;
  lua_pushvalue(L, obj);
  lua_call(L, 1, 1);
  return 1;
}

//以数字形式返回给定索引处值的“长度”； 它等价于在 Lua 中调用 '#' 的操作。 
//如果操作结果不是一个整数，则抛出一个错误。 （这种情况只发生在触发元方法时。）
LUALIB_API lua_Integer luaL_len (lua_State *L, int idx) {
  lua_Integer l;
  int isnum;
  lua_len(L, idx);
  l = lua_tointegerx(L, -1, &isnum);
  if (!isnum)
    luaL_error(L, "object length is not an integer");
  lua_pop(L, 1);  /* remove object */
  return l;
}


LUALIB_API const char *luaL_tolstring (lua_State *L, int idx, size_t *len) {
  if (!luaL_callmeta(L, idx, "__tostring")) {  /* no metafield? */
    switch (lua_type(L, idx)) {
      case LUA_TNUMBER: {
        if (lua_isinteger(L, idx))
          lua_pushfstring(L, "%I", lua_tointeger(L, idx));
        else
          lua_pushfstring(L, "%f", lua_tonumber(L, idx));
        break;
      }
      case LUA_TSTRING:
        lua_pushvalue(L, idx);
        break;
      case LUA_TBOOLEAN:
        lua_pushstring(L, (lua_toboolean(L, idx) ? "true" : "false"));
        break;
      case LUA_TNIL:
        lua_pushliteral(L, "nil");
        break;
      default:
        lua_pushfstring(L, "%s: %p", luaL_typename(L, idx),
                                            lua_topointer(L, idx));
        break;
    }
  }
  return lua_tolstring(L, -1, len);
}


/*
** {======================================================
** Compatibility with 5.1 module functions
** =======================================================
*/
#if defined(LUA_COMPAT_MODULE)

static const char *luaL_findtable (lua_State *L, int idx,
                                   const char *fname, int szhint) {
  const char *e;
  if (idx) lua_pushvalue(L, idx);
  do {
    e = strchr(fname, '.');
    if (e == NULL) e = fname + strlen(fname);
    lua_pushlstring(L, fname, e - fname);
    if (lua_rawget(L, -2) == LUA_TNIL) {  /* no such field? */
      lua_pop(L, 1);  /* remove this nil */
      lua_createtable(L, 0, (*e == '.' ? 1 : szhint)); /* new table for field */
      lua_pushlstring(L, fname, e - fname);
      lua_pushvalue(L, -2);
      lua_settable(L, -4);  /* set new table into field */
    }
    else if (!lua_istable(L, -1)) {  /* field has a non-table value? */
      lua_pop(L, 2);  /* remove table and value */
      return fname;  /* return problematic part of the name */
    }
    lua_remove(L, -2);  /* remove previous table */
    fname = e + 1;
  } while (*e == '.');
  return NULL;
}


/*
** Count number of elements in a luaL_Reg list.
*/
static int libsize (const luaL_Reg *l) {
  int size = 0;
  for (; l && l->name; l++) size++;
  return size;
}


/*
** Find or create a module table with a given name. The function
** first looks at the _LOADED table and, if that fails, try a
** global variable with that name. In any case, leaves on the stack
** the module table.
*/
LUALIB_API void luaL_pushmodule (lua_State *L, const char *modname,
                                 int sizehint) {
  luaL_findtable(L, LUA_REGISTRYINDEX, "_LOADED", 1);  /* get _LOADED table */
  if (lua_getfield(L, -1, modname) != LUA_TTABLE) {  /* no _LOADED[modname]? */
    lua_pop(L, 1);  /* remove previous result */
    /* try global variable (and create one if it does not exist) */
    lua_pushglobaltable(L);
    if (luaL_findtable(L, 0, modname, sizehint) != NULL)
      luaL_error(L, "name conflict for module '%s'", modname);
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, modname);  /* _LOADED[modname] = new table */
  }
  lua_remove(L, -2);  /* remove _LOADED table */
}


LUALIB_API void luaL_openlib (lua_State *L, const char *libname,
                               const luaL_Reg *l, int nup) {
  luaL_checkversion(L);
  if (libname) {
    luaL_pushmodule(L, libname, libsize(l));  /* get/create library table */
    lua_insert(L, -(nup + 1));  /* move library table to below upvalues */
  }
  if (l)
    luaL_setfuncs(L, l, nup);
  else
    lua_pop(L, nup);  /* remove upvalues */
}

#endif
/* }====================================================== */

/*
** set functions from list 'l' into table at top - 'nup'; each
** function gets the 'nup' elements at the top as upvalues.
** Returns with only the table at the stack.
把数组 l 中的所有函数注册到栈顶的表中。
若 nup 不为零， 所有的函数都共享 nup 个上值。 这些值必须在调用之前，压在表之上。
这些值在注册完毕后都会从栈弹出。
*/
LUALIB_API void luaL_setfuncs (lua_State *L, const luaL_Reg *l, int nup) {
  luaL_checkstack(L, nup, "too many upvalues");
  for (; l->name != NULL; l++) {			//遍历数组l 将每个函数都注册下
    int i;
    for (i = 0; i < nup; i++)				//将upvalue值压入栈
      lua_pushvalue(L, -nup);
    lua_pushcclosure(L, l->func, nup);		//将func压入栈中 如果有nup，组建一个cclosure压入栈中
    lua_setfield(L, -(nup + 2), l->name);		//将函数注册到表中 t[l->name] = l->func
  }
  lua_pop(L, nup);				//将压入栈中的元素清理下
}


/*
** ensure that stack[idx][fname] has a table and push that table
** into the stack
*/
LUALIB_API int luaL_getsubtable (lua_State *L, int idx, const char *fname) {
  if (lua_getfield(L, idx, fname) == LUA_TTABLE)
    return 1;  /* table already there */
  else {
    lua_pop(L, 1);  /* remove previous result */
    idx = lua_absindex(L, idx);
    lua_newtable(L);
    lua_pushvalue(L, -1);  /* copy to be left at top */
    lua_setfield(L, idx, fname);  /* assign new table to field */
    return 0;  /* false, because did not find table there */
  }
}


//如果 modname 不在 package.loaded 中， 则调用函数 openf ，并传入字符串 modname。 
//将其返回值置入 package.loaded[modname]。 这个行为好似该函数通过 require 调用过一样。
//如果 glb 为真， 同时也讲模块设到全局变量 modname 里。
//在栈上留下该模块的副本。
LUALIB_API void luaL_requiref (lua_State *L, const char *modname,
                               lua_CFunction openf, int glb) {
  luaL_getsubtable(L, LUA_REGISTRYINDEX, "_LOADED");
  lua_getfield(L, -1, modname);  /* _LOADED[modname] */
  if (!lua_toboolean(L, -1)) {  /* package not already loaded? */
    lua_pop(L, 1);  /* remove field */
    lua_pushcfunction(L, openf);
    lua_pushstring(L, modname);  /* argument to open function */
    lua_call(L, 1, 1);  /* call 'openf' to open module */
    lua_pushvalue(L, -1);  /* make copy of module (call result) */
    lua_setfield(L, -3, modname);  /* _LOADED[modname] = module */
  }
  lua_remove(L, -2);  /* remove _LOADED table */
  if (glb) {
    lua_pushvalue(L, -1);  /* copy of module */
    lua_setglobal(L, modname);  /* _G[modname] = module */
  }
}


LUALIB_API const char *luaL_gsub (lua_State *L, const char *s, const char *p,
                                                               const char *r) {
  const char *wild;
  size_t l = strlen(p);
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  while ((wild = strstr(s, p)) != NULL) {
    luaL_addlstring(&b, s, wild - s);  /* push prefix */
    luaL_addstring(&b, r);  /* push replacement in place of pattern */
    s = wild + l;  /* continue after 'p' */
  }
  luaL_addstring(&b, s);  /* push last suffix */
  luaL_pushresult(&b);
  return lua_tostring(L, -1);
}


static void *l_alloc (void *ud, void *ptr, size_t osize, size_t nsize) {
  (void)ud; (void)osize;	/* not used */
  if (nsize == 0) {			//nsize为0表示释放ptr指向的内存
    free(ptr);
    return NULL;
  }
  else
	//否则表示申请nsize大小的内存		如果ptr为NULL则重新分配 否则从ptr后面扩大 
	//如果不过也重新申请并将原ptr指向的数据拷贝到新申请的内存中  返回扩大后的内存地址
    return realloc(ptr, nsize);		
}

//默认错误处理函数
static int panic (lua_State *L) {
  lua_writestringerror("PANIC: unprotected error in call to Lua API (%s)\n",
                        lua_tostring(L, -1));		//将栈顶的错误信息写到标准错误输出
  return 0;  /* return to Lua to abort */
}


/*创建一个新的 Lua 状态机。 它以一个基于标准 C 的 realloc 函数实现的内存分配器 调用 lua_newstate 。 
并把可打印一些出错信息到标准错误输出的 panic 函数设置好，用于处理致命错误。
返回新的状态机。 如果内存分配失败，则返回 NULL 。*/
LUALIB_API lua_State *luaL_newstate (void) {
  lua_State *L = lua_newstate(l_alloc, NULL);
  if (L) lua_atpanic(L, &panic);			//设置panic函数 错误处理函数
  return L;
}


LUALIB_API void luaL_checkversion_ (lua_State *L, lua_Number ver, size_t sz) {
  const lua_Number *v = lua_version(L);			//得到状态机L的版本的指针
  if (sz != LUAL_NUMSIZES)						//整型和浮点型与当前内核不同 错误
	luaL_error(L, "core and library have incompatible numeric types");
  if (v != lua_version(NULL))					//判断状态机L和当前运行内核是不是同一个 根据内存指针
    luaL_error(L, "multiple Lua VMs detected");
  else if (*v != ver)							//判断状态机L的版本号与当前内核的版本号
    luaL_error(L, "version mismatch: app. needs %f, Lua core provides %f",
                  ver, *v);
}

