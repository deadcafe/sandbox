


DEV_ROOT ?= $(CURDIR)/..

export BUILD_DIR = $(DEV_ROOT)/build
export LUA_INC_DIR=$(BUILD_DIR)/include/luajit-2.1
export LUA_LIB_DIR=$(BUILD_DIR)/lib


CFLAGS  = -g -O0 -Werror -Wextra -Wall -Wstrict-aliasing -std=gnu99 -m64 -msse4.2 -pipe

CFLAGS += -fno-builtin

#CFLAGS += -mavx
#CFLAGS += -mavx2
#-mtune=core2 -mavx

CPPFLAGS = -c -I. -D_GNU_SOURCE -I$(LUA_INC_DIR) -I$(FRAMEWORK_DIR)
LIBS = $(LUA_LIB_DIR)/libluajit-5.1.a -ldl -lm 
LDFLAGS =

#CFLAGS += -funroll-loops -frerun-loop-opt
#CFLAGS += -fforce-addr

SRCS	=	\
	tplua.c

OBJS = ${SRCS:.c=.o}
DEPENDS = .depend

TARGET = tplua

.SUFFIXES:      .o .c
.PHONY: all clean depend

all	:	depend $(TARGET)

.c.o	:
	$(CC) $(CFLAGS) $(CPPFLAGS) $<

$(TARGET):	$(OBJS)
	$(CC) -o $@ $^ $(LIBS) $(LDFLAGS)

$(OBJS)	:	Makefile

clean:
	rm -f $(OBJS) $(TARGET) $(DEPENDS) *~

depend:	$(SRCS) Makefile
	-@ $(CC) $(CPPFLAGS) -MM -MG $(SRCS) > $(DEPENDS)

-include $(DEPENDS)
