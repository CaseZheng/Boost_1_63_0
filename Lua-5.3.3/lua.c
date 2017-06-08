/*
** $Id: lua.c,v 1.226 2015/08/14 19:11:20 roberto Exp $
** Lua stand-alone interpreter
** See Copyright Notice in lua.h
*/

#define lua_c

#include "lprefix.h"


#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"


#if !defined(LUA_PROMPT)
#define LUA_PROMPT		"> "
#define LUA_PROMPT2		">> "
#endif

#if !defined(LUA_PROGNAME)
#define LUA_PROGNAME		"lua"
#endif

#if !defined(LUA_MAXINPUT)
#define LUA_MAXINPUT		512
#endif

#if !defined(LUA_INIT_VAR)
#define LUA_INIT_VAR		"LUA_INIT"
#endif

//"LUA_INIT""_""5""_""3"
#define LUA_INITVARVERSION  \
	LUA_INIT_VAR "_" LUA_VERSION_MAJOR "_" LUA_VERSION_MINOR


/*
** lua_stdin_is_tty detects whether the standard input is a 'tty' (that
** is, whether we're running lua interactively).
*/
#if !defined(lua_stdin_is_tty)	/* { */

#if defined(LUA_USE_POSIX)	/* { */

#include <unistd.h>
#define lua_stdin_is_tty()	isatty(0)

#elif defined(LUA_USE_WINDOWS)	/* }{ */

#include <io.h>
#define lua_stdin_is_tty()	_isatty(_fileno(stdin))

#else				/* }{ */

/* ISO C definition */
#define lua_stdin_is_tty()	1  /* assume stdin is a tty */

#endif				/* } */

#endif				/* } */


/*
** lua_readline defines how to show a prompt and then read a line from
** the standard input.
** lua_saveline defines how to "save" a read line in a "history".
** lua_freeline defines how to free a line read by lua_readline.
*/
#if !defined(lua_readline)	/* { */

#if defined(LUA_USE_READLINE)	/* { */

#include <readline/readline.h>
#include <readline/history.h>
#define lua_readline(L,b,p)	((void)L, ((b)=readline(p)) != NULL)
#define lua_saveline(L,line)	((void)L, add_history(line))
#define lua_freeline(L,b)	((void)L, free(b))

#else				/* }{ */

#define lua_readline(L,b,p) \
        ((void)L, fputs(p, stdout), fflush(stdout),  /* show prompt */ \
        fgets(b, LUA_MAXINPUT, stdin) != NULL)  /* get line */
#define lua_saveline(L,line)	{ (void)L; (void)line; }
#define lua_freeline(L,b)	{ (void)L; (void)b; }

#endif				/* } */

#endif				/* } */




static lua_State *globalL = NULL;

static const char *progname = LUA_PROGNAME;


/*
** Hook set by signal function to stop the interpreter.
*/
static void lstop (lua_State *L, lua_Debug *ar) {
  (void)ar;  /* unused arg. */
  lua_sethook(L, NULL, 0, 0);  /* reset hook */
  luaL_error(L, "interrupted!");
}


/*
** Function to be called at a C signal. Because a C signal cannot
** just change a Lua state (as there is no proper synchronization),
** this function only sets a hook that, when called, will stop the
** interpreter.
在C信号处调用的函数。 因为C信号不能只改变一个Lua状态（因为没有适当的同步），
这个函数只设置一个钩子，当被调用时，它将停止解释器。
*/
static void laction (int i) {
  signal(i, SIG_DFL);			//恢复信号i的默认处理方式
  lua_sethook(globalL, lstop, LUA_MASKCALL | LUA_MASKRET | LUA_MASKCOUNT, 1);
}


static void print_usage (const char *badoption) {
  lua_writestringerror("%s: ", progname);
  if (badoption[1] == 'e' || badoption[1] == 'l')
    lua_writestringerror("'%s' needs argument\n", badoption);
  else
    lua_writestringerror("unrecognized option '%s'\n", badoption);
  lua_writestringerror(
  "usage: %s [options] [script [args]]\n"
  "Available options are:\n"
  "  -e stat  execute string 'stat'\n"
  "  -i       enter interactive mode after executing 'script'\n"
  "  -l name  require library 'name'\n"
  "  -v       show version information\n"
  "  -E       ignore environment variables\n"
  "  --       stop handling options\n"
  "  -        stop handling options and execute stdin\n"
  ,
  progname);
}


/*
** Prints an error message, adding the program name in front of it
** (if present)
*/
static void l_message (const char *pname, const char *msg) {
  if (pname) lua_writestringerror("%s: ", pname);	//存在pname将pname输出到标准错误文件
  lua_writestringerror("%s\n", msg);	//将msg输出到标准错误文件
}


//检查status不是OK 打印错误信息
static int report (lua_State *L, int status) {
  if (status != LUA_OK) {						//L执行的结果不是成功的话  从栈中得到错误消息  然后将错误信息弹出栈
    const char *msg = lua_tostring(L, -1);
    l_message(progname, msg);					//将程序名称和错误信息输出到标准错误输出
    lua_pop(L, 1);								//将错误信息弹出栈
  }
  return status;								//返回状态
}


/*
** Message handler used to run all chunks
*/
static int msghandler (lua_State *L) {
  const char *msg = lua_tostring(L, 1);
  if (msg == NULL) {  /* is error object not a string? */
    if (luaL_callmeta(L, 1, "__tostring") &&  /* does it have a metamethod */
        lua_type(L, -1) == LUA_TSTRING)  /* that produces a string? */
      return 1;  /* that is the message */
    else
      msg = lua_pushfstring(L, "(error object is a %s value)",
                               luaL_typename(L, 1));
  }
  luaL_traceback(L, L, msg, 1);  /* append a standard traceback */
  return 1;  /* return the traceback */
}


//设置message函数 和 C信号处理函数 以便运行所有的代码块	L为lua状态机 narg为压入参数个数 nres为返回参数个数
static int docall (lua_State *L, int narg, int nres) {
  int status;
  int base = lua_gettop(L) - narg;		//找到压入参数的起始位置	
  lua_pushcfunction(L, msghandler);		//压入massage函数
  lua_insert(L, base);					//将massage插入参数前面
  globalL = L;							//使globalL指向 状态机L
  signal(SIGINT, laction);				//设置SIGINT信号处理函数
  status = lua_pcall(L, narg, nres, base);		//以保护模式调用一个函数
  signal(SIGINT, SIG_DFL);				//恢复SIGINT信号处理函数
  lua_remove(L, base);					//从栈中移除massage函数
  return status;						//返回代码块执行结果
}


static void print_version (void) {
  lua_writestring(LUA_COPYRIGHT, strlen(LUA_COPYRIGHT));
  lua_writeline();
}


/*
在运行任何代码前， lua 会将所有命令行传入的参数放到一张全局表 arg 中。 
脚本的名字放在索引 0 的地方， 脚本名后紧跟的第一个参数在索引 1 处，依次类推。
在脚本名前面的任何参数 （即解释器的名字以及各选项） 放在负索引处。
*/
static void createargtable (lua_State *L, char **argv, int argc, int script) {
  int i, narg;
  if (script == argc) script = 0;  /* no script name? */
  narg = argc - (script + 1);  /* number of positive indices */
  lua_createtable(L, narg, script + 1);		//创建一张表arg用于存储命令行参数
  for (i = 0; i < argc; i++) {
    lua_pushstring(L, argv[i]);				//将argv[i]压栈
	//将argv[i]放入arg表中 arg[i-script]=argv[i] 
    lua_rawseti(L, -2, i - script);
  }
  lua_setglobal(L, "arg");					//将arg表加入到全局
}


static int dochunk (lua_State *L, int status) {
  if (status == LUA_OK) status = docall(L, 0, 0);
  return report(L, status);
}

//打开该名字的文件，并执行文件中的 Lua 代码块。 
//不带参数调用时， dofile 执行标准输入的内容（stdin）。 
//返回该代码块的所有返回值。 对于有错误的情况，
//dofile 将错误反馈给调用者 （即，dofile 没有运行在保护模式下）。
static int dofile (lua_State *L, const char *name) {
  return dochunk(L, luaL_loadfile(L, name));
}


static int dostring (lua_State *L, const char *s, const char *name) {
  return dochunk(L, luaL_loadbuffer(L, s, strlen(s), name));
}


/*
** Calls 'require(name)' and stores the result in a global variable
** with the given name.
*/
static int dolibrary (lua_State *L, const char *name) {
  int status;
  lua_getglobal(L, "require");
  lua_pushstring(L, name);
  status = docall(L, 1, 1);  /* call 'require(name)' */
  if (status == LUA_OK)
    lua_setglobal(L, name);  /* global[name] = require return */
  return report(L, status);
}


/*
** Returns the string to be used as a prompt by the interpreter.
*/
static const char *get_prompt (lua_State *L, int firstline) {
  const char *p;
  lua_getglobal(L, firstline ? "_PROMPT" : "_PROMPT2");
  p = lua_tostring(L, -1);
  if (p == NULL) p = (firstline ? LUA_PROMPT : LUA_PROMPT2);
  return p;
}

/* mark in error messages for incomplete statements */
#define EOFMARK		"<eof>"
#define marklen		(sizeof(EOFMARK)/sizeof(char) - 1)


/*
** Check whether 'status' signals a syntax error and the error
** message at the top of the stack ends with the above mark for
** incomplete statements.
*/
static int incomplete (lua_State *L, int status) {
  if (status == LUA_ERRSYNTAX) {
    size_t lmsg;
    const char *msg = lua_tolstring(L, -1, &lmsg);
    if (lmsg >= marklen && strcmp(msg + lmsg - marklen, EOFMARK) == 0) {
      lua_pop(L, 1);
      return 1;
    }
  }
  return 0;  /* else... */
}


/*
** Prompt the user, read a line, and push it into the Lua stack.
*/
static int pushline (lua_State *L, int firstline) {
  char buffer[LUA_MAXINPUT];
  char *b = buffer;
  size_t l;
  const char *prmt = get_prompt(L, firstline);
  int readstatus = lua_readline(L, b, prmt);
  if (readstatus == 0)
    return 0;  /* no input (prompt will be popped by caller) */
  lua_pop(L, 1);  /* remove prompt */
  l = strlen(b);
  if (l > 0 && b[l-1] == '\n')  /* line ends with newline? */
    b[--l] = '\0';  /* remove it */
  if (firstline && b[0] == '=')  /* for compatibility with 5.2, ... */
    lua_pushfstring(L, "return %s", b + 1);  /* change '=' to 'return' */
  else
    lua_pushlstring(L, b, l);
  lua_freeline(L, b);
  return 1;
}


/*
** Try to compile line on the stack as 'return <line>;'; on return, stack
** has either compiled chunk or original line (if compilation failed).
*/
static int addreturn (lua_State *L) {
  const char *line = lua_tostring(L, -1);  /* original line */
  const char *retline = lua_pushfstring(L, "return %s;", line);
  int status = luaL_loadbuffer(L, retline, strlen(retline), "=stdin");
  if (status == LUA_OK) {
    lua_remove(L, -2);  /* remove modified line */
    if (line[0] != '\0')  /* non empty? */
      lua_saveline(L, line);  /* keep history */
  }
  else
    lua_pop(L, 2);  /* pop result from 'luaL_loadbuffer' and modified line */
  return status;
}


/*
** Read multiple lines until a complete Lua statement
*/
static int multiline (lua_State *L) {
  for (;;) {  /* repeat until gets a complete statement */
    size_t len;
    const char *line = lua_tolstring(L, 1, &len);  /* get what it has */
    int status = luaL_loadbuffer(L, line, len, "=stdin");  /* try it */
    if (!incomplete(L, status) || !pushline(L, 0)) {
      lua_saveline(L, line);  /* keep history */
      return status;  /* cannot or should not try to add continuation line */
    }
    lua_pushliteral(L, "\n");  /* add newline... */
    lua_insert(L, -2);  /* ...between the two lines */
    lua_concat(L, 3);  /* join them */
  }
}


/*
** Read a line and try to load (compile) it first as an expression (by
** adding "return " in front of it) and second as a statement. Return
** the final status of load/call with the resulting function (if any)
** in the top of the stack.
*/
static int loadline (lua_State *L) {
  int status;
  lua_settop(L, 0);
  if (!pushline(L, 1))
    return -1;  /* no input */
  if ((status = addreturn(L)) != LUA_OK)  /* 'return ...' did not work? */
    status = multiline(L);  /* try as command, maybe with continuation lines */
  lua_remove(L, 1);  /* remove line from the stack */
  lua_assert(lua_gettop(L) == 1);
  return status;
}


/*
** Prints (calling the Lua 'print' function) any values on the stack
*/
static void l_print (lua_State *L) {
  int n = lua_gettop(L);
  if (n > 0) {  /* any result to be printed? */
    luaL_checkstack(L, LUA_MINSTACK, "too many results to print");
    lua_getglobal(L, "print");
    lua_insert(L, 1);
    if (lua_pcall(L, n, 0, 0) != LUA_OK)
      l_message(progname, lua_pushfstring(L, "error calling 'print' (%s)",
                                             lua_tostring(L, -1)));
  }
}


/*
** Do the REPL: repeatedly read (load) a line, evaluate (call) it, and
** print any results.
*/
static void doREPL (lua_State *L) {
  int status;
  const char *oldprogname = progname;
  progname = NULL;  /* no 'progname' on errors in interactive mode */
  while ((status = loadline(L)) != -1) {
    if (status == LUA_OK)
      status = docall(L, 0, LUA_MULTRET);
    if (status == LUA_OK) l_print(L);
    else report(L, status);
  }
  lua_settop(L, 0);  /* clear stack */
  lua_writeline();
  progname = oldprogname;
}


//将arg参数压入栈中	返回压入栈的参数的个数
static int pushargs (lua_State *L) {
  int i, n;
  if (lua_getglobal(L, "arg") != LUA_TTABLE)	//得到arg表 压栈  判断返回的类型 必须是个table
    luaL_error(L, "'arg' is not a table");
  n = (int)luaL_len(L, -1);						//得到arg的长度
  luaL_checkstack(L, n + 3, "too many arguments to script");		//检查栈上要有n+3个额外位置没有则扩展 如果无法扩展输出错误信息
  for (i = 1; i <= n; i++)
    lua_rawgeti(L, -i, i);						//将arg表中的参数 依次压入栈中	arg的参数从左到右依次入栈
  lua_remove(L, -i);							//将arg表从栈中删除 
  return n;
}

//解析命令行参数 运行lua脚本文件
static int handle_script (lua_State *L, char **argv) {
  int status;
  const char *fname = argv[0];
  if (strcmp(fname, "-") == 0 && strcmp(argv[-1], "--") != 0)	//fname 等于"-" 而且 argv[-1] 不等于'--' fanme置为空
    fname = NULL;  /* stdin */
  status = luaL_loadfile(L, fname);								//加载Lua代码块
  if (status == LUA_OK) {										//加载成功
    int n = pushargs(L);										//将arg参数压入栈中	n为压入栈中的参数的个数
    status = docall(L, n, LUA_MULTRET);							//执行Lua代码块
  }
  return report(L, status);										//检查执行结果 如果错误打印错误信息
}



/* bits of various argument indicators in 'args' */
#define has_error	1	/* bad option */
#define has_i		2	/* -i */
#define has_v		4	/* -v */
#define has_e		8	/* -e */
#define has_E		16	/* -E */

/*
** Traverses all arguments from 'argv', returning a mask with those
** needed before running any Lua code (or an error code if it finds
** any invalid argument). 'first' returns the first not-handled argument 
** (either the script name or a bad argument in case of error).
*/
static int collectargs (char **argv, int *first) {
  int args = 0;
  int i;
  for (i = 1; argv[i] != NULL; i++) {
    *first = i;
    if (argv[i][0] != '-')			//没有参数			
        return args;
    switch (argv[i][1]) {
	case '-':						//'--'终止对后面选项的处理
        if (argv[i][2] != '\0')
          return has_error;
        *first = i + 1;
        return args;
      case '\0':					//'-'把stdin作为一个文件运行，并终止对后面选项的处理
        return args;
      case 'E':						//忽略环境变量
        if (argv[i][2] != '\0')
          return has_error;
        args |= has_E;
        break;
      case 'i':						//在运行完脚本后，进入交互模式
        args |= has_i;
      case 'v':						//打印版本信息
        if (argv[i][2] != '\0')
          return has_error;
        args |= has_v;
        break;
      case 'e':						//执行一段字符串
        args |= has_e;
	  case 'l':						//请求模式 
        if (argv[i][2] == '\0') {
          i++;
          if (argv[i] == NULL || argv[i][0] == '-')
            return has_error;
        }
        break;
      default:
        return has_error;
    }
  }
  *first = i;						//first记录当前对命令行解析的位置
  return args;
}


//命令行中有-e或-i选项 需要运行Lua代码  运行代码后 如果代码有错误返回0 否则返回1
static int runargs (lua_State *L, char **argv, int n) {
  int i;
  for (i = 1; i < n; i++) {
    int option = argv[i][1];
    lua_assert(argv[i][0] == '-');
    if (option == 'e' || option == 'l') {				//有e 或 l选项
      int status;
      const char *extra = argv[i] + 2;					//看下-e -l后面是不是紧跟着要执行的代码或脚本名 如 -e"print('hello world')"
      if (*extra == '\0') extra = argv[++i];			//如果是'\0' 没有和-e -l紧跟着 如-e "print('hello world')"
      lua_assert(extra != NULL);
      status = (option == 'e')
               ? dostring(L, extra, "=(command line)")
               : dolibrary(L, extra);					//如果是-e选项 运行一段代码   如果是-l选项运行脚本
      if (status != LUA_OK) return 0;					//如果运行脚本返回的状态不对 返回0
    }
  }
  return 1;
}


static int handle_luainit (lua_State *L) {
  const char *name = "=" LUA_INITVARVERSION;	//=LUA_INIT_5_3
  const char *init = getenv(name + 1);			//从环境中取字符串,获取环境变量的值
  if (init == NULL) {							//没取到 改变环境变量的名称再取一次
    name = "=" LUA_INIT_VAR;
    init = getenv(name + 1);					//=LUA_INIT
  }
  if (init == NULL) return LUA_OK;
  else if (init[0] == '@')
    return dofile(L, init+1);
  else
    return dostring(L, init, name);
}


/*
** Main body of stand-alone interpreter (to be called in protected mode).
** Reads the options and handles them all.
*/
static int pmain (lua_State *L) {
  int argc = (int)lua_tointeger(L, 1);				//得到main函数压入栈的命令行参数
  char **argv = (char **)lua_touserdata(L, 2);
  int script;
  int args = collectargs(argv, &script);			//解析命令行参数 script记录对命令行当前解析到的位置
  luaL_checkversion(L);								//检查状态机L和本内核的版本
  if (argv[0] && argv[0][0]) progname = argv[0];	//运行的Lua脚本名称
  if (args == has_error) {							//命令行参数错误
    print_usage(argv[script]);						//输出提醒
    return 0;
  }
  if (args & has_v)									//-v  显示版本信息
    print_version();
  if (args & has_E) {								//-E  忽略环境变量
    lua_pushboolean(L, 1);							//栈中压入true
    lua_setfield(L, LUA_REGISTRYINDEX, "LUA_NOENV");		//注册表["LUA_NOENV"] = true
  }
  luaL_openlibs(L);									//打开所有的标准库
  createargtable(L, argv, argc, script);  /* create table 'arg' */
  if (!(args & has_E)) {							//如果没有-E
    if (handle_luainit(L) != LUA_OK)				//读取环境变量  并运行LUA_INIT
      return 0;
  }
  if (!runargs(L, argv, script))					//处理-i 或 -e选项 运行Lua代码
    return 0;
  if (script < argc && handle_script(L, argv + script) != LUA_OK)	//处理没有-i -e参数时运行Lua脚本的情况  比如 lua main.lua
    return 0;
  if (args & has_i)									//-i 运行完脚本后进入交互模式
    doREPL(L);
  else if (script == argc && !(args & (has_e | has_v))) {  /* no arguments? */
    if (lua_stdin_is_tty()) {  /* running in interactive mode? */
      print_version();
      doREPL(L);  /* do read-eval-print loop */
    }
    else dofile(L, NULL);  /* executes stdin as a file */
  }
  lua_pushboolean(L, 1);  /* signal no errors */
  return 1;
}


int main (int argc, char **argv) {
  int status, result;
  lua_State *L = luaL_newstate();	//创建一个Lua状态机
  if (L == NULL) {
    l_message(argv[0], "cannot create state: not enough memory");	//输出错误信息到标准错误输出
    return EXIT_FAILURE;
  }
  lua_pushcfunction(L, &pmain);		//将pmain函数压入栈
  lua_pushinteger(L, argc);			//将参数argc 和argv压入栈
  lua_pushlightuserdata(L, argv);
  status = lua_pcall(L, 2, 1, 0);	//调用pmain函数  并得到程序执行状态
  result = lua_toboolean(L, -1);	//得到执行结果
  report(L, status);				//如果执行错误输出错误信息
  lua_close(L);						//关闭状态Lua机
  return (result && status == LUA_OK) ? EXIT_SUCCESS : EXIT_FAILURE;	//程序执行有结果并且返回状态正常 返回成功 否则返回失败
}

