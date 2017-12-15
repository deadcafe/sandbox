

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "luajit.h"
#include "lualib.h"
#include "lauxlib.h"


int
eng_reset(lua_State *L)
{
        lua_pushinteger(L, 0);
        return 1;
}

static int
eng_start(lua_State *L)
{
        lua_pushinteger(L, 1);
        return 1;

}

static int
eng_stop(lua_State *L)
{
        lua_pushinteger(L, 2);
        return 1;

}

static int
eng_poll(lua_State *L)
{
        lua_pushinteger(L, 3);
        return 1;
}

const luaL_Reg EngineTable[] = {
        { "eng_reset", eng_reset },
        { "eng_start", eng_start },
        { "eng_stop",  eng_stop },
        { "eng_poll",  eng_poll },
        { NULL, NULL }
};

static int
lua_test(lua_State *L,
         const char *file)
{
        int ret = -1;

        if (L) {
                luaL_openlibs(L);
                lua_register(L, "eng_reset", eng_reset);
                lua_register(L, "eng_start", eng_start);
                lua_register(L, "eng_stop", eng_stop);
                lua_register(L, "eng_poll", eng_poll);


                if (luaL_loadfile(L, file) || lua_pcall(L, 0, 0, 0)) {
                        fprintf(stderr, "Error: %s\n", lua_tostring(L, -1));
                } else {
                        ret = 0;
                }

                lua_close(L);
        }
        return ret;
}

/*******************************************************************
 *
 *******************************************************************/

static void
usage(const char *prog)
{
        fprintf(stderr, "usage: %s -f LUA_FILE\n", prog);
}

int
main(int ac,
     char **av)
{
        int opt;
        const char *prog = av[0];
        const char *lua_file = NULL;

        if ((prog = strrchr(av[0], '/')) == NULL)
                prog = av[0];
        else
                prog++;

        while ((opt = getopt(ac, av, "hf:")) != -1) {

                switch (opt) {
                case 'f':
                        lua_file = optarg;
                        break;

                case 'h':
                default:
                        usage(prog);
                        exit(0);
                }
        }

        if (!lua_file) {
                usage(prog);
                exit(0);
        }

        if (lua_test(luaL_newstate(), lua_file))
                fprintf(stderr, "failed\n");

        return 0;
}
