export DEV_ROOT = $(CURDIR)
export BUILD_DIR = $(DEV_ROOT)/build

export LUA_INC_DIR=$(BUILD_DIR)/include/luajit-2.1
export LUA_LIB_DIR=$(BUILD_DIR)/lib


all:	target


target:	lua
	$(MAKE) -C sandbox

lua:
	$(MAKE) -C luajit PREFIX=$(BUILD_DIR) clean install

clean:
	$(MAKE) -C sandbox clean

.PHONY: all target clean lua


