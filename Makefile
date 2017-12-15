export LC_ALL=C
export DEV_ROOT=$(CURDIR)
export BUILD_DIR=build

export RTE_SDK=$(DEV_ROOT)/dpdk
export RTE_TARGET=../$(BUILD_DIR)
export RTE_OUTPUT=$(DEV_ROOT)/$(BUILD_DIR)

export LUA_DIR=$(DEV_ROOT)/luajit
export LUA_INC_DIR=$(BUILD_DIR)/include/luajit-2.1
export LUA_LIB_DIR=$(BUILD_DIR)/lib

#export IPSEC_MB_DIR=$(DEV_ROOT)/intel-ipsec-mb
export FRAMEWORK_DIR=$(DEV_ROOT)/framework
export COMPAT_DIR=$(DEV_ROOT)/compat

#export PCAP_DIR=$(DEV_ROOT)/libpcap
#export CPPFLAGS += -I$(PCAP_DIR)
#export LDFLAGS += -L$(PCAP_DIR)

DEV_JX ?= -j3


.PHONY:	all config clean tags
all:	dpdk framework app

clean:	clean-app clean-framework

claen-all:	clean clean-dpdk

config:
	cd $(RTE_SDK); patch -p0 -s -t < $(DEV_ROOT)/patches/deadcafe_dpdk.patch
	$(MAKE) -C $(RTE_SDK) config T=deadcafe

htags:
	htags --suggest2

#app
.PHONY:	app clean-app

app:	framework
	$(MAKE) -C app

clean-app:
	$(MAKE) -C app clean

#sandbox
.PHONY:	sandbox clean-sandbox

sandbox:	framework lua
	$(MAKE) -C sandbox

clean-sandbox:
	$(MAKE) -C sandbox clean

#framework
.PHONY:	framework clean-framework

framework:
	$(MAKE) $(DEV_JX) -C framework all

clean-framework:
	$(MAKE) $(DEV_JX) -C framework clean

#DPDK
.PHONY:	clean-dpdk dpdk

dpdk:
	$(MAKE) $(DEV_JX) -C $(RTE_SDK) all

clean-dpdk:	
	$(MAKE) $(DEV_JX) -C $(RTE_SDK) clean

#PCAP
.PHONY:	clean-pcap pcap
pcap:
	$(MAKE) $(DEV_JX) -C $(PCAP_DIR) all

clean-pcap:
	$(MAKE) $(DEV_JX) -C $(PCAP_DIR) clean

#IPSEC
.PHONY:	clean-ipsec_mb ipsec_mb
ipsec_mb:
	$(MAKE) $(DEV_JX) -C $(IPSEC_MB_DIR)

clean-ipsec_mb:
	$(MAKE) $(DEV_JX) -C $(IPSEC_MB_DIR) clean

#lua
.PHONY:	lua clean-lua
lua:
	$(MAKE) -C $(LUA_DIR) PREFIX=$(DEV_ROOT)/$(BUILD_DIR) install

clean-lua:
	$(MAKE) -C $(LUA_DIR) clean

#bond
.PHONY:	bond clean-bond

bond:
	$(MAKE) $(DEV_JX) -C bond all

clean-bond:
	$(MAKE) $(DEV_JX) -C bond clean

#DPDK examples
.PHONY:	examples clean-examples
examples:
	$(MAKE) $(DEV_JX) -C dpdk/examples

clean-examples:
	$(MAKE) $(DEV_JX) -C dpdk/examples clean




