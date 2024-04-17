/*
** $Id: lbaselib.c $
** Basic library
** See Copyright Notice in lua.h
*/

#define lbaselib_c
#define LUA_LIB

#include "lprefix.h"


#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"

/*
   Implementation of "new" syntax sugar:

function new(reference)
  if type(reference) ~= "table" then
    error "bad argument #1 to 'new' (table expected, got no value)"
  end
  local metatable= {}
  local reference_metatable = reference.__metatable or {}
  local new_instance = {}

  if type(rawget(reference,"<instance metatable>")) == "table" then
    return setmetatable({},rawget(reference,"<instance metatable>"))
  end

  for k,v in pairs(reference_metatable) do
    metatable[k] = v
  end

  metatable.__index = reference

  rawgeset(reference,"<instance metatable>",metatable)

  return setmetatable(new_instance,metatable)
end

    This allows:

MyClass = {
  property = "value";
  __metatable = {
    __tostring =
      function (self)
        return "Property is: "..self.property
      end
    ;
  }
}

instance = new(MyClass)
instance.property = "another value"

print(instance) -- Property is: another value

*/

int luaB_new(lua_State *L) {
    const int reference = 1;
    const int metatable = 2;
    const int reference_metatable = 3;
    const int new_instance = 4;

    // Only pass if arg is table
    luaL_checktype(L, reference, LUA_TTABLE);

    lua_pushstring(L,"<instance metatable>");
    lua_rawget(L, reference);
    if (lua_istable(L, -1)) {
        // Field does not exist?
        lua_setmetatable(L, -2);

        return 1;
    }

    lua_pop(L, 1);  // Remove nil from stack

    // Our metatable
    lua_newtable(L);

    // Try put reference metatable field on to stack
    lua_getfield(L, reference, "__metatable");
    if (lua_isnil(L, -1)) {
        // Field does not exist?
        lua_pop(L, 1);  // Remove nil from stack
        lua_newtable(L); // Create an empty table
    }

    lua_newtable(L);

    lua_pushnil(L);  // Start iteration by pushing nil onto the stack
    while (lua_next(L, reference_metatable)) {
        // key is at index -2 and value is at index -1
        lua_pushvalue(L, -2);  // Push the key again to keep it for the next iteration
        lua_pushvalue(L, -2);  // Duplicate the value to leave a copy on the stack
        lua_settable(L, metatable);  // Set the value in the new metatable
        lua_pop(L, 1);  // Pop the value, leaving the key for the next iteration
    }

    lua_pushvalue(L, reference);
    lua_setfield(L, metatable, "__index");


    lua_pushliteral(L,"<instance metatable>");
    lua_pushvalue(L, metatable);
    lua_rawset(L, reference);

    // Set metatable for new_instance
    lua_pushvalue(L, new_instance);
    lua_pushvalue(L, metatable);
    lua_setmetatable(L, -2);

    return 1; // Return the new_instance
}

//----------------------------------------------------------------------------------------------------------------------//


/*
   Implementation of "extends" syntax sugar:

function extends(base)
  if type(base) ~= "table" then
    error "bad argument #1 to 'new' (table expected, got no value)"
  end
  return
    function (derived)
      local derived_metatable = derived.__metatable or {}
      local base_metatable = base.__metatable or {}

      for k,v in pairs(base_metatable) do
        if k ~= "__name" then
          derived_metatable[k] = v
        end
      end

      derived_metatable.__index = base

      return setmetatable(derived, derived_metatable)
    end
end

    This allows:

BaseClass = {
  property = "value";
  __metatable = {
    __tostring =
      function (self)
        return "Property is: "..self.property
      end
    ;
  }
}

DerivedClass = extends(BaseClass) {
  another_property = "another value";
  __metatable = {
    __len =
      function (self)
        return 42
      end
    ;
  }
}

instance = new(DerivedClass)
print(instance) -- Property is: value
print(#instance) -- 42
print(instance.property) -- value
print(instance.another_property) -- another value
*/


int luaB_extends_closure(lua_State *L) {
    const int derived = 1;
    const int base = 2;
    const int derived_metatable = 3;
    const int base_metatable = 4;

    // This pushes the base from extends first parameter
    lua_pushvalue(L, lua_upvalueindex(1));

    // Check if __metatable field exists in the derived
    lua_getfield(L, derived, "__metatable");
    if (lua_isnil(L, -1)) {
        // Field does not exist?
        lua_pop(L, 1);  // Remove nil from stack
        lua_newtable(L); // Create an empty table
    }

    // Check if __metatable field exists in the derived
    lua_getfield(L, base, "__metatable");
    if (lua_isnil(L, -1)) {
        // Field does not exist?
        lua_pop(L, 1);  // Remove nil from stack
        lua_newtable(L); // Create an empty table
    }

    // Copy fields in base_metatable to derived_metatable except __name

    lua_pushnil(L);  // Push nil to start the iteration
    while (lua_next(L, base_metatable) != 0) {
        // key is at index -2 and value at index -1
        const char *key = lua_tostring(L, -2);
        if (strcmp(key, "__name") != 0) {
            // Copy the field from base_metatable to derived_metatable
            lua_pushvalue(L, -2);  // Copy key
            lua_pushvalue(L, -2);  // Copy value
            lua_settable(L, derived_metatable);  // Set key-value pair in derived_metatable
        }
        // Pop the value, leaving the key for the next iteration
        lua_pop(L, 1);
    }

    // To complete inheritance is needed to fallback fields on base
    lua_pushvalue(L, base);
    lua_setfield(L, derived_metatable, "__index");

    // Set the metatable
    lua_pushvalue(L,derived);
    lua_pushvalue(L,derived_metatable);
    lua_setmetatable(L, -2);

    return 1;
}

int luaB_extends(lua_State *L) {
    const int base = 1;                // Our first arg (local)
    // Only pass if base is a table
    luaL_checktype(L, base, LUA_TTABLE);
    // Base as local
    lua_pushvalue(L, base);

    // Push the closure that does the trick
    lua_pushcclosure(L, luaB_extends_closure, 1);
    return 1;
}

//----------------------------------------------------------------------------------------------------------------------//

int luaB_all(lua_State *L) {
    const int iterable = 1;

    // Only pass if arg is table
    luaL_checktype(L, iterable, LUA_TTABLE);

    lua_len(L, iterable); // Get the length of the table
    int n = lua_tointeger(L, -1); // Get the length
    lua_pop(L, 1); // Pop the length from the stack

    for (int i = 1; i <= n; ++i) {
        const int value = -1;
        lua_rawgeti(L, iterable, i); // Get the i-th element of the table
        if (lua_toboolean(L, value) == 0) { // Check if the element is false
            // Return false and the index
            lua_pushboolean(L, 0);
            lua_pushinteger(L, i);
            return 2;
        }
        lua_pop(L, 1); // Pop the element from the stack
    }

    lua_pushboolean(L, 1); // Push true onto the stack
    return 1; // Return true
}

//----------------------------------------------------------------------------------------------------------------------//

int luaB_any(lua_State *L) {
    const int iterable = 1;

    // Only pass if arg is table
    luaL_checktype(L, iterable, LUA_TTABLE);

    lua_len(L, iterable); // Get the length of the table
    int n = lua_tointeger(L, -1); // Get the length
    lua_pop(L, 1); // Pop the length from the stack

    for (int i = 1; i <= n; ++i) {
        const int value = -1;
        lua_rawgeti(L, iterable, i); // Get the i-th element of the table
        if (lua_toboolean(L, value) == 1) { // Check if the element is truthy
            // Return true and the index
            lua_pushboolean(L, 1);
            lua_pushinteger(L, i);
            return 2;
        }
        lua_pop(L, 1); // Pop the element from the stack
    }

    lua_pushboolean(L, 0); // Push false onto the stack
    return 1; // Return true
}

//----------------------------------------------------------------------------------------------------------------------//

int luaB_same(lua_State *L) {
    const int iterable = 1;
    // Iterable must be a table
    luaL_checktype(L, iterable, LUA_TTABLE);

    // Get the length of the table
    int len = luaL_len(L, iterable);

    // Return false if table is empty
    if (len == 0) {
        lua_pushboolean(L, 0);
        return 1;
    }

    // Get the type of the first element in the table
    lua_rawgeti(L, iterable, 1);
    char *firstType = (char *)luaL_typename(L, -1);

    // If type of first element is "table", check if it has a metatable
    if (strcmp(firstType, "table") == 0) {
        const int metatable = -1;
        // Get metatable of the table
        if (lua_getmetatable(L, metatable)) { 
            firstType = "object"; // Ok, is at least an object (has a metatable)
            if (lua_istable(L, metatable)) {
                const int name = -1;
                // We are have a custom type here?
                lua_getfield(L, metatable, "__name");
                if (lua_isstring(L, name)) { 
                    firstType = (char *)lua_tostring(L, name); // Yes, lets store the custom type
                }
                lua_pop(L, 1); // Pop the "__name" field
            }
            lua_pop(L, 1); // Pop the metatable
        }
    }

    lua_pop(L, 1); // Pop the first element from the stack

    for (int i = 2; i <= len; i++) {
        const int value = -1;
        lua_rawgeti(L, iterable, i); // Get the next element

        char *valueType = (char *)luaL_typename(L, value); // Get its type

        // If type of current element is "table", check if it has a metatable
        if (strcmp(firstType, "table") == 0) {
            const int metatable = -1;
            // Get metatable of the table
            if (lua_getmetatable(L, metatable)) { 
                firstType = "object"; // Ok, is at least an object (has a metatable)
                if (lua_istable(L, metatable)) {
                    const int name = -1;
                    // We have a custom type here?
                    lua_getfield(L, metatable, "__name");
                    if (lua_isstring(L, name)) { 
                        firstType = (char *)lua_tostring(L, name); // Yes, lets store the custom type
                    }
                    lua_pop(L, 1); // Pop the "__name" field
                }
                lua_pop(L, 1); // Pop the metatable
            }
        }

        // Compare the types
        if (strcmp(valueType, firstType) != 0) {
            lua_pushboolean(L, 0); // Types are not the same, push false
            lua_replace(L, -2); // Replace the last pushed boolean with false
            return 1;
        }
        lua_pop(L, 1); // Pop the element from the stack
    }

    lua_pushboolean(L, 1);
    return 1;
}

//----------------------------------------------------------------------------------------------------------------------//

static int luaB_print (lua_State *L) {
  int n = lua_gettop(L);  /* number of arguments */
  int i;
  for (i = 1; i <= n; i++) {  /* for each argument */
    size_t l;
    const char *s = luaL_tolstring(L, i, &l);  /* convert it to string */
    if (i > 1)  /* not the first element? */
      lua_writestring("\t", 1);  /* add a tab before it */
    lua_writestring(s, l);  /* print it */
    lua_pop(L, 1);  /* pop result */
  }
  lua_writeline();
  return 0;
}


/*
** Creates a warning with all given arguments.
** Check first for errors; otherwise an error may interrupt
** the composition of a warning, leaving it unfinished.
*/
static int luaB_warn (lua_State *L) {
  int n = lua_gettop(L);  /* number of arguments */
  int i;
  luaL_checkstring(L, 1);  /* at least one argument */
  for (i = 2; i <= n; i++)
    luaL_checkstring(L, i);  /* make sure all arguments are strings */
  for (i = 1; i < n; i++)  /* compose warning */
    lua_warning(L, lua_tostring(L, i), 1);
  lua_warning(L, lua_tostring(L, n), 0);  /* close warning */
  return 0;
}


#define SPACECHARS	" \f\n\r\t\v"

static const char *b_str2int (const char *s, int base, lua_Integer *pn) {
  lua_Unsigned n = 0;
  int neg = 0;
  s += strspn(s, SPACECHARS);  /* skip initial spaces */
  if (*s == '-') { s++; neg = 1; }  /* handle sign */
  else if (*s == '+') s++;
  if (!isalnum((unsigned char)*s))  /* no digit? */
    return NULL;
  do {
    int digit = (isdigit((unsigned char)*s)) ? *s - '0'
                   : (toupper((unsigned char)*s) - 'A') + 10;
    if (digit >= base) return NULL;  /* invalid numeral */
    n = n * base + digit;
    s++;
  } while (isalnum((unsigned char)*s));
  s += strspn(s, SPACECHARS);  /* skip trailing spaces */
  *pn = (lua_Integer)((neg) ? (0u - n) : n);
  return s;
}


static int luaB_tonumber (lua_State *L) {
  if (lua_isnoneornil(L, 2)) {  /* standard conversion? */
    if (lua_type(L, 1) == LUA_TNUMBER) {  /* already a number? */
      lua_settop(L, 1);  /* yes; return it */
      return 1;
    }
    else {
      size_t l;
      const char *s = lua_tolstring(L, 1, &l);
      if (s != NULL && lua_stringtonumber(L, s) == l + 1)
        return 1;  /* successful conversion to number */
      /* else not a number */
      luaL_checkany(L, 1);  /* (but there must be some parameter) */
    }
  }
  else {
    size_t l;
    const char *s;
    lua_Integer n = 0;  /* to avoid warnings */
    lua_Integer base = luaL_checkinteger(L, 2);
    luaL_checktype(L, 1, LUA_TSTRING);  /* no numbers as strings */
    s = lua_tolstring(L, 1, &l);
    luaL_argcheck(L, 2 <= base && base <= 36, 2, "base out of range");
    if (b_str2int(s, (int)base, &n) == s + l) {
      lua_pushinteger(L, n);
      return 1;
    }  /* else not a number */
  }  /* else not a number */
  luaL_pushfail(L);  /* not a number */
  return 1;
}


static int luaB_error (lua_State *L) {
  int level = (int)luaL_optinteger(L, 2, 1);
  lua_settop(L, 1);
  if (lua_type(L, 1) == LUA_TSTRING && level > 0) {
    luaL_where(L, level);   /* add extra information */
    lua_pushvalue(L, 1);
    lua_concat(L, 2);
  }
  return lua_error(L);
}


static int luaB_getmetatable (lua_State *L) {
  luaL_checkany(L, 1);
  if (!lua_getmetatable(L, 1)) {
    lua_pushnil(L);
    return 1;  /* no metatable */
  }
  luaL_getmetafield(L, 1, "__metatable");
  return 1;  /* returns either __metatable field (if present) or metatable */
}


static int luaB_setmetatable (lua_State *L) {
  int t = lua_type(L, 2);
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_argexpected(L, t == LUA_TNIL || t == LUA_TTABLE, 2, "nil or table");
  if (l_unlikely(luaL_getmetafield(L, 1, "__metatable") != LUA_TNIL))
    return luaL_error(L, "cannot change a protected metatable");
  lua_settop(L, 2);
  lua_setmetatable(L, 1);
  return 1;
}


static int luaB_rawequal (lua_State *L) {
  luaL_checkany(L, 1);
  luaL_checkany(L, 2);
  lua_pushboolean(L, lua_rawequal(L, 1, 2));
  return 1;
}


static int luaB_rawlen (lua_State *L) {
  int t = lua_type(L, 1);
  luaL_argexpected(L, t == LUA_TTABLE || t == LUA_TSTRING, 1,
                      "table or string");
  lua_pushinteger(L, lua_rawlen(L, 1));
  return 1;
}


static int luaB_rawget (lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_checkany(L, 2);
  lua_settop(L, 2);
  lua_rawget(L, 1);
  return 1;
}

static int luaB_rawset (lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_checkany(L, 2);
  luaL_checkany(L, 3);
  lua_settop(L, 3);
  lua_rawset(L, 1);
  return 1;
}


static int pushmode (lua_State *L, int oldmode) {
  if (oldmode == -1)
    luaL_pushfail(L);  /* invalid call to 'lua_gc' */
  else
    lua_pushstring(L, (oldmode == LUA_GCINC) ? "incremental"
                                             : "generational");
  return 1;
}


/*
** check whether call to 'lua_gc' was valid (not inside a finalizer)
*/
#define checkvalres(res) { if (res == -1) break; }

static int luaB_collectgarbage (lua_State *L) {
  static const char *const opts[] = {"stop", "restart", "collect",
    "count", "step", "setpause", "setstepmul",
    "isrunning", "generational", "incremental", NULL};
  static const int optsnum[] = {LUA_GCSTOP, LUA_GCRESTART, LUA_GCCOLLECT,
    LUA_GCCOUNT, LUA_GCSTEP, LUA_GCSETPAUSE, LUA_GCSETSTEPMUL,
    LUA_GCISRUNNING, LUA_GCGEN, LUA_GCINC};
  int o = optsnum[luaL_checkoption(L, 1, "collect", opts)];
  switch (o) {
    case LUA_GCCOUNT: {
      int k = lua_gc(L, o);
      int b = lua_gc(L, LUA_GCCOUNTB);
      checkvalres(k);
      lua_pushnumber(L, (lua_Number)k + ((lua_Number)b/1024));
      return 1;
    }
    case LUA_GCSTEP: {
      int step = (int)luaL_optinteger(L, 2, 0);
      int res = lua_gc(L, o, step);
      checkvalres(res);
      lua_pushboolean(L, res);
      return 1;
    }
    case LUA_GCSETPAUSE:
    case LUA_GCSETSTEPMUL: {
      int p = (int)luaL_optinteger(L, 2, 0);
      int previous = lua_gc(L, o, p);
      checkvalres(previous);
      lua_pushinteger(L, previous);
      return 1;
    }
    case LUA_GCISRUNNING: {
      int res = lua_gc(L, o);
      checkvalres(res);
      lua_pushboolean(L, res);
      return 1;
    }
    case LUA_GCGEN: {
      int minormul = (int)luaL_optinteger(L, 2, 0);
      int majormul = (int)luaL_optinteger(L, 3, 0);
      return pushmode(L, lua_gc(L, o, minormul, majormul));
    }
    case LUA_GCINC: {
      int pause = (int)luaL_optinteger(L, 2, 0);
      int stepmul = (int)luaL_optinteger(L, 3, 0);
      int stepsize = (int)luaL_optinteger(L, 4, 0);
      return pushmode(L, lua_gc(L, o, pause, stepmul, stepsize));
    }
    default: {
      int res = lua_gc(L, o);
      checkvalres(res);
      lua_pushinteger(L, res);
      return 1;
    }
  }
  luaL_pushfail(L);  /* invalid call (inside a finalizer) */
  return 1;
}


static int luaB_type (lua_State *L) {
  int t = lua_type(L, 1);
  luaL_argcheck(L, t != LUA_TNONE, 1, "value expected");
  const int any = 1;

  if (lua_istable(L,1)) {
    if (lua_getmetatable(L,any)) {
      const int meta = 2;
      const int __name = 3;

      lua_pushstring(L,"__name");
      lua_rawget(L,meta);

      if (lua_type(L, 3) == LUA_TSTRING) {
        return 1;
      }

      lua_pushstring(L, "object");
      return 1;
    }
    lua_pushstring(L, "table");
    return 1;
  }

  lua_pushstring(L, lua_typename(L, t));
  return 1;
}


static int luaB_next (lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_settop(L, 2);  /* create a 2nd argument if there isn't one */
  if (lua_next(L, 1))
    return 2;
  else {
    lua_pushnil(L);
    return 1;
  }
}


static int pairscont (lua_State *L, int status, lua_KContext k) {
  (void)L; (void)status; (void)k;  /* unused */
  return 3;
}

static int luaB_pairs (lua_State *L) {
  luaL_checkany(L, 1);
  if (luaL_getmetafield(L, 1, "__pairs") == LUA_TNIL) {  /* no metamethod? */
    lua_pushcfunction(L, luaB_next);  /* will return generator, */
    lua_pushvalue(L, 1);  /* state, */
    lua_pushnil(L);  /* and initial value */
  }
  else {
    lua_pushvalue(L, 1);  /* argument 'self' to metamethod */
    lua_callk(L, 1, 3, 0, pairscont);  /* get 3 values from metamethod */
  }
  return 3;
}


/*
** Traversal function for 'ipairs'
*/
static int ipairsaux (lua_State *L) {
  lua_Integer i = luaL_checkinteger(L, 2);
  i = luaL_intop(+, i, 1);
  lua_pushinteger(L, i);
  return (lua_geti(L, 1, i) == LUA_TNIL) ? 1 : 2;
}


/*
** 'ipairs' function. Returns 'ipairsaux', given "table", 0.
** (The given "table" may not be a table.)
*/
static int luaB_ipairs (lua_State *L) {
  luaL_checkany(L, 1);
  lua_pushcfunction(L, ipairsaux);  /* iteration function */
  lua_pushvalue(L, 1);  /* state */
  lua_pushinteger(L, 0);  /* initial value */
  return 3;
}


static int load_aux (lua_State *L, int status, int envidx) {
  if (l_likely(status == LUA_OK)) {
    if (envidx != 0) {  /* 'env' parameter? */
      lua_pushvalue(L, envidx);  /* environment for loaded function */
      if (!lua_setupvalue(L, -2, 1))  /* set it as 1st upvalue */
        lua_pop(L, 1);  /* remove 'env' if not used by previous call */
    }
    return 1;
  }
  else {  /* error (message is on top of the stack) */
    luaL_pushfail(L);
    lua_insert(L, -2);  /* put before error message */
    return 2;  /* return fail plus error message */
  }
}


static int luaB_loadfile (lua_State *L) {
  const char *fname = luaL_optstring(L, 1, NULL);
  const char *mode = luaL_optstring(L, 2, NULL);
  int env = (!lua_isnone(L, 3) ? 3 : 0);  /* 'env' index or 0 if no 'env' */
  int status = luaL_loadfilex(L, fname, mode);
  return load_aux(L, status, env);
}


/*
** {======================================================
** Generic Read function
** =======================================================
*/


/*
** reserved slot, above all arguments, to hold a copy of the returned
** string to avoid it being collected while parsed. 'load' has four
** optional arguments (chunk, source name, mode, and environment).
*/
#define RESERVEDSLOT	5


/*
** Reader for generic 'load' function: 'lua_load' uses the
** stack for internal stuff, so the reader cannot change the
** stack top. Instead, it keeps its resulting string in a
** reserved slot inside the stack.
*/
static const char *generic_reader (lua_State *L, void *ud, size_t *size) {
  (void)(ud);  /* not used */
  luaL_checkstack(L, 2, "too many nested functions");
  lua_pushvalue(L, 1);  /* get function */
  lua_call(L, 0, 1);  /* call it */
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);  /* pop result */
    *size = 0;
    return NULL;
  }
  else if (l_unlikely(!lua_isstring(L, -1)))
    luaL_error(L, "reader function must return a string");
  lua_replace(L, RESERVEDSLOT);  /* save string in reserved slot */
  return lua_tolstring(L, RESERVEDSLOT, size);
}


static int luaB_load (lua_State *L) {
  int status;
  size_t l;
  const char *s = lua_tolstring(L, 1, &l);
  const char *mode = luaL_optstring(L, 3, "bt");
  int env = (!lua_isnone(L, 4) ? 4 : 0);  /* 'env' index or 0 if no 'env' */
  if (s != NULL) {  /* loading a string? */
    const char *chunkname = luaL_optstring(L, 2, s);
    status = luaL_loadbufferx(L, s, l, chunkname, mode);
  }
  else {  /* loading from a reader function */
    const char *chunkname = luaL_optstring(L, 2, "=(load)");
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_settop(L, RESERVEDSLOT);  /* create reserved slot */
    status = lua_load(L, generic_reader, NULL, chunkname, mode);
  }
  return load_aux(L, status, env);
}

/* }====================================================== */


static int dofilecont (lua_State *L, int d1, lua_KContext d2) {
  (void)d1;  (void)d2;  /* only to match 'lua_Kfunction' prototype */
  return lua_gettop(L) - 1;
}


static int luaB_dofile (lua_State *L) {
  const char *fname = luaL_optstring(L, 1, NULL);
  lua_settop(L, 1);
  if (l_unlikely(luaL_loadfile(L, fname) != LUA_OK))
    return lua_error(L);
  lua_callk(L, 0, LUA_MULTRET, 0, dofilecont);
  return dofilecont(L, 0, 0);
}


static int luaB_assert (lua_State *L) {
  if (l_likely(lua_toboolean(L, 1)))  /* condition is true? */
    return lua_gettop(L);  /* return all arguments */
  else {  /* error */
    luaL_checkany(L, 1);  /* there must be a condition */
    lua_remove(L, 1);  /* remove it */
    lua_pushliteral(L, "assertion failed!");  /* default message */
    lua_settop(L, 1);  /* leave only message (default if no other one) */
    return luaB_error(L);  /* call 'error' */
  }
}


static int luaB_select (lua_State *L) {
  int n = lua_gettop(L);
  if (lua_type(L, 1) == LUA_TSTRING && *lua_tostring(L, 1) == '#') {
    lua_pushinteger(L, n-1);
    return 1;
  }
  else {
    lua_Integer i = luaL_checkinteger(L, 1);
    if (i < 0) i = n + i;
    else if (i > n) i = n;
    luaL_argcheck(L, 1 <= i, 1, "index out of range");
    return n - (int)i;
  }
}


/*
** Continuation function for 'pcall' and 'xpcall'. Both functions
** already pushed a 'true' before doing the call, so in case of success
** 'finishpcall' only has to return everything in the stack minus
** 'extra' values (where 'extra' is exactly the number of items to be
** ignored).
*/
static int finishpcall (lua_State *L, int status, lua_KContext extra) {
  if (l_unlikely(status != LUA_OK && status != LUA_YIELD)) {  /* error? */
    lua_pushboolean(L, 0);  /* first result (false) */
    lua_pushvalue(L, -2);  /* error message */
    return 2;  /* return false, msg */
  }
  else
    return lua_gettop(L) - (int)extra;  /* return all results */
}


static int luaB_pcall (lua_State *L) {
  int status;
  luaL_checkany(L, 1);
  lua_pushboolean(L, 1);  /* first result if no errors */
  lua_insert(L, 1);  /* put it in place */
  status = lua_pcallk(L, lua_gettop(L) - 2, LUA_MULTRET, 0, 0, finishpcall);
  return finishpcall(L, status, 0);
}


/*
** Do a protected call with error handling. After 'lua_rotate', the
** stack will have <f, err, true, f, [args...]>; so, the function passes
** 2 to 'finishpcall' to skip the 2 first values when returning results.
*/
static int luaB_xpcall (lua_State *L) {
  int status;
  int n = lua_gettop(L);
  luaL_checktype(L, 2, LUA_TFUNCTION);  /* check error function */
  lua_pushboolean(L, 1);  /* first result */
  lua_pushvalue(L, 1);  /* function */
  lua_rotate(L, 3, 2);  /* move them below function's arguments */
  status = lua_pcallk(L, n - 2, LUA_MULTRET, 2, 2, finishpcall);
  return finishpcall(L, status, 2);
}


static int luaB_tostring (lua_State *L) {
  luaL_checkany(L, 1);
  luaL_tolstring(L, 1, NULL);
  return 1;
}


static const luaL_Reg base_funcs[] = {
  {"assert", luaB_assert},
  {"collectgarbage", luaB_collectgarbage},
  {"dofile", luaB_dofile},
  {"error", luaB_error},
  {"getmetatable", luaB_getmetatable},
  {"ipairs", luaB_ipairs},
  {"loadfile", luaB_loadfile},
  {"load", luaB_load},
  {"next", luaB_next},
  {"pairs", luaB_pairs},
  {"pcall", luaB_pcall},
  {"print", luaB_print},
  {"warn", luaB_warn},
  {"rawequal", luaB_rawequal},
  {"rawlen", luaB_rawlen},
  {"rawget", luaB_rawget},
  {"rawset", luaB_rawset},
  {"select", luaB_select},
  {"setmetatable", luaB_setmetatable},
  {"tonumber", luaB_tonumber},
  {"tostring", luaB_tostring},
  {"type", luaB_type},
  {"xpcall", luaB_xpcall},
  /* Iasy */
  {"new", luaB_new},
  {"extends", luaB_extends},
  {"all",luaB_all},
  {"any",luaB_any},
  {"same",luaB_same},
  /* placeholders */
  {LUA_GNAME, NULL},
  {"_VERSION", NULL},
  {NULL, NULL}
};


LUAMOD_API int luaopen_base (lua_State *L) {
  /* open lib into global table */
  lua_pushglobaltable(L);
  luaL_setfuncs(L, base_funcs, 0);
  /* set global _G */
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, LUA_GNAME);
  /* set global _VERSION */
  lua_pushliteral(L, LUA_VERSION);
  lua_setfield(L, -2, "_VERSION");
  return 1;
}
