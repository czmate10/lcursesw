/* */

/***
Curses attributed string buffers.

An array of characters, plus associated curses attributes and
colors at each position.

Although marginally useful alone, the constants used to set colors
and attributes in `chstr` buffers are not defined until **after**
`curses.initscr ()` has been called.

@classmod curses.chstr
*/

#ifndef LCURSES_CHSTR_C
#define LCURSES_CHSTR_C 1

#include "_helpers.c"


static const char *CHSTR_META = "curses:chstr";

typedef struct chstr {
  unsigned int len; // element num
  unsigned int size; // buffer size
  cchar_t str[1];
} chstr;

#define CHSTR_SIZE(len) (sizeof(chstr) + len * sizeof(cchar_t))


static chstr *
chstr_new_by_size(size_t len) {
  if (len < 1) return NULL;
  chstr * cs = malloc(CHSTR_SIZE(len));
  if (!cs) return NULL;
  cs->size = len;
  cs->len = len;
  for (unsigned int i = 0; i < len; i++) {
    cs->str[i].attr = A_NORMAL;
    cs->str[i].chars[0] = ' ';
    cs->str[i].chars[1] = '\0';
  }
  return cs;
}

static chstr *
chstr_new(const char * str, size_t len, int attr) {
  if (!str || len < 1) return NULL;

  chstr * cs = malloc(CHSTR_SIZE(len));
  if (!cs) return NULL;

  cs->size = len;

  int i = 0;

  for (const char * str_end = str + len; str < str_end; i++) {
    int code;
    str = utf8_decode(str, &code);

    // str is not valid utf-8 byte sequence
    if (str == NULL) return free(cs), NULL;

    cs->str[i].attr     = attr;
    cs->str[i].chars[0] = code;
    cs->str[i].chars[1] = '\0';
  }

  cs->len = i;

  return cs;
}

static void
delete_chwstr(chstr * cs) {
  free(cs);
}

/* get chstr from lua (convert if needed) */
static chstr **
checkchstr(lua_State *L, int narg) {
  chstr **cs = (chstr**)luaL_checkudata(L, narg, CHSTR_META);
  luaL_argcheck(L, cs, narg, "bad curses chstr");
  return cs;
}


/***
Change the contents of the chstr.
@function set_str
@int o offset to start of change
@string s characters to insert into *cs* at *o*
@int[opt=A_NORMAL] attr attributes for changed elements
@int[opt=1] rep repeat count
@usage
  cs = curses.chstr (10)
  cs:set_str(0, "0123456789", curses.A_BOLD)
*/
static int
Cset_str(lua_State *L) {
  chstr **pcs = checkchstr(L, 1);

  int offset = checkint(L, 2);
  luaL_argcheck(L, 0 < offset && offset <= (int)(*pcs)->len, 2, "bad index");

  --offset;

  size_t len;
  const char *str = luaL_checklstring(L, 3, &len);
  int attr = optint(L, 4, A_NORMAL);
  int rep = optint(L, 5, 1);
  luaL_argcheck(L, rep > 0, 5, "rep should > 0");

  int *code_array = malloc(sizeof(int) * len);
  if (!code_array) return luaL_error(L, "malloc failed");

  int utf8_str_len = 0;
  for (const char * str_end = str + len; str < str_end; utf8_str_len++) {
    str = utf8_decode(str, code_array++);
    if (str == NULL) {
      free(code_array);
      return luaL_argerror(L, 3, "bad utf8 byte sequence");
    }
  }

  if (utf8_str_len < 1) {
    free(code_array);
    return luaL_argerror(L, 3, "empty string");
  }

  code_array -= utf8_str_len;

  size_t new_size = utf8_str_len * rep + offset;

  if (new_size > (*pcs)->size) {
    chstr *new_cs = realloc(*pcs, CHSTR_SIZE(new_size));
    if (!new_cs) {
      free(code_array);
      return luaL_error(L, "realloc failed");
    }
    new_cs->size = new_size;
    *pcs = new_cs; // update userdata
  }

  chstr * cs = *pcs;

  if (new_size > cs->len) {
    cs->len = new_size;
  }

  cchar_t * p = &cs->str[offset];
  for (int i = 0; i < rep; i++) {
    for (int li = 0; li < utf8_str_len; li++) {
      p->attr     = attr;
      p->chars[0] = code_array[li];
      p->chars[1] = '\0';
      ++p;
    }
  }
  free(code_array);

  return 0;
}


/***
Set a character in the buffer with optional repeat.
*ch* can be a utf8 string, or an integer from `utf8.codepoint`
@function set_ch
@int o offset to start of change
@param int|string ch character to insert
@int[opt=A_NORMAL] attr attributes for changed elements
@int[opt=1] rep repeat count
@usage
  size = 10
  cs = curses.chstr (size)
  cs:set_ch(1, 'A', curses.A_BOLD)
  cs:set_ch(2, '风', curses.A_NORMAL, size - 1)
*/
static int
Cset_ch(lua_State *L)
{
  chstr* cs = *checkchstr(L, 1);
  int offset = checkint(L, 2);
  luaL_argcheck(L, 0 < offset && offset <= (int)cs->len, 2, "bad index");

  int ch = checkutf8char(L, 3); // codepoint
  int set_attr = !lua_isnoneornil(L, 4);
  int attr = optint(L, 4, A_NORMAL);
  int rep = optint(L, 5, 1);
  luaL_argcheck(L, 0 < rep && rep <= (int)cs->len - offset + 1, 5, "bad rep");

  --offset;

  while (rep--) {
    if (set_attr) cs->str[offset].attr = attr;
    cs->str[offset].chars[0] = ch;
    cs->str[offset].chars[1] = '\0';

    ++offset;
  }

  return 0;
}


/***
Get information from the chstr.
@function get
@int o offset from start of *cs*, 1-based index like `string.byte()`
@treturn int character(unicode codepoint) at offset *o* in *cs*
@treturn int bitwise-OR of attributes at offset *o* in *cs*
@treturn int colorpair at offset *o* in *cs*
@usage
  cs = curses.chstr (10)
  cs:set_ch(1, 'A', curses.A_BOLD, 10)
  print (cs:get (9)) --> 65 2097152 0
*/
static int
Cget(lua_State *L)
{
  chstr* cs = *checkchstr(L, 1);
  int offset = checkint(L, 2);

  luaL_argcheck(L, offset > 0 && offset <= (int)cs->len, 2, "index range: [1 .. cs:len()]");

  cchar_t *ch = cs->str + offset - 1;

  lua_pushinteger(L, ch->chars[0]);
  lua_pushinteger(L, ch->attr & A_ATTRIBUTES);
  lua_pushinteger(L, ch->attr & A_COLOR);
  return 3;
}


/***
Retrieve chstr length.
@function len
@tparam chstr cs buffer to act on
@treturn int length of *cs*
@usage
  cs = curses.chstr ('hi,世界')
  --> 5
  print (cs:len ())
*/
static int
Clen(lua_State *L)
{
	chstr *cs = *checkchstr(L, 1);
	return pushintresult(cs->len);
}

/***
Retrieve chstr size.
@function size
@tparam chstr cs buffer to act on
@treturn int size of *cs*
@usage
  cs = curses.chstr ('hi,世界')
  print (cs:len ()) --> 9
  cs2 = cs:dup()
  print (cs2:len ()) --> 5
*/
static int
Csize(lua_State *L)
{
	chstr *cs = *checkchstr(L, 1);
	return pushintresult(cs->size);
}


/***
Duplicate chstr.
@function dup
@treturn chstr duplicate of *cs*
@usage
  dup = cs:dup ()
*/
static int
Cdup(lua_State *L)
{
  chstr *cs = *checkchstr(L, 1);
  chstr ** ncs = lua_newuserdata(L, sizeof(chstr *));

  size_t rlen = cs->len;

  *ncs = malloc(CHSTR_SIZE(rlen));

  if (!*ncs) return luaL_error(L, "[chstr:dup] malloc failed");

  memcpy(*ncs, cs, CHSTR_SIZE(rlen));
  (*ncs)->size = rlen;

  luaL_setmetatable(L, CHSTR_META);
  return 1;
}

/***
free chstr.
@function __gc
*/
static int
Cchstr_gc(lua_State *L) {
  delete_chwstr(*checkchstr(L, 1));
  return 0;
}

static int
create_chstr(lua_State *L, int narg) {
  chstr* cs;

  int tt = lua_type(L, narg);
  if (tt == LUA_TSTRING) {
    size_t len;
    const char * str = luaL_checklstring(L, narg, &len);
    int attr = optint(L, narg + 1, A_NORMAL);
    cs = chstr_new(str, len, attr);
  } else if (tt == LUA_TNUMBER) {
    int len = checkint(L, narg);
    luaL_argcheck(L, len > 0, narg, "bad len");
    cs = chstr_new_by_size(len);
  } else {
    return luaL_error(L, "bad argument");
  }

  if (!cs) return luaL_error(L, "create wstr failed!");

  *(chstr **)lua_newuserdata(L, sizeof(chstr *)) = cs;
  luaL_setmetatable(L, CHSTR_META);

  return 1;
}

/***
Initialise a new chstr.
@function __call
@string str utf8 string to create
@int[opt] attr attrbitue
@treturn chstr new chstr
@usage
  cs = curses.chstr ('example', curses.A_BOLD)
*/
static int
C__call(lua_State *L)
{
  return create_chstr(L, 2);
}


static const luaL_Reg curses_chstr_fns[] =
{
	LCURSES_FUNC( Clen		),
	LCURSES_FUNC( Csize		),
	LCURSES_FUNC( Cset_ch		),
	LCURSES_FUNC( Cset_str		),
	LCURSES_FUNC( Cget		),
	LCURSES_FUNC( Cdup		),
	{ NULL, NULL }
};


static void test() {
#if 0
#endif
}


LUALIB_API int
luaopen_curses_chstr(lua_State *L)
{
	int t, mt;

	luaL_newlib(L, curses_chstr_fns);
	t = lua_gettop(L);

	lua_createtable(L, 0, 1);		/* u = {} */
	lua_pushcfunction(L, C__call);
	lua_setfield(L, -2, "__call");		/* u.__call = C__call */
	lua_setmetatable(L, -2);		/* setmetatable (t, u) */

	luaL_newmetatable(L, CHSTR_META);
	mt = lua_gettop(L);

	lua_pushvalue(L, mt);
	lua_setfield(L, -2, "__index");		/* mt.__index = mt */

  lua_pushcfunction(L, Cchstr_gc);
	lua_setfield(L, -2, "__gc");		/* mt.__gc = Cchstr_gc*/

	lua_pushliteral(L, "CursesChstr");
	lua_setfield(L, -2, "_type");		/* mt._type = "CursesChstr" */

	/* for k,v in pairs(t) do mt[k]=v end */
	for (lua_pushnil(L); lua_next(L, t) != 0;)
		lua_setfield(L, mt, lua_tostring(L, -2));

	lua_pop(L, 1);				/* pop mt */

	/* t.version = "curses.chstr..." */
	lua_pushliteral(L, "curses.chstr for " LUA_VERSION " / " PACKAGE_STRING);
	lua_setfield(L, t, "version");

	return 1;
}

#endif /*!LCURSES_CHSTR_C*/
