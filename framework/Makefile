##############################################################################
ifeq ($(origin DEV_ROOT),undefined)
##############################################################################
all:
	$(MAKE) -C .. framework

clean:
	$(MAKE) -C .. clean-framework

##############################################################################
else
##############################################################################

include $(RTE_SDK)/mk/rte.vars.mk

LIB = libdc.a

CPPFLAGS += -I./ -I$(COMPAT_DIR)
CFLAGS += -std=gnu11 -Werror -Wunused -Wextra

SRCS-y := 		\
	dc_conf.c	\
	dc_timer.c	\
	dc_thread.c	\
	dc_mbuf.c	\
	dc_port.c	\
	dc_addon.c	\
	dc_fw_log.c

include $(RTE_SDK)/mk/rte.extlib.mk

##############################################################################
endif
##############################################################################
