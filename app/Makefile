##############################################################################
ifeq ($(origin DEV_ROOT),undefined)
##############################################################################
all:
	$(MAKE) -C .. app

clean:
	$(MAKE) -C .. clean-app

##############################################################################
else
##############################################################################

include $(RTE_SDK)/mk/rte.vars.mk

APP = deadcafe

CPPFLAGS += -I./ -I$(COMPAT_DIR) -I$(FRAMEWORK_DIR) -I$(PCAP_DIR) 
CFLAGS += -std=gnu11 -Werror

_LDLIBS-y += -ldc
_LDLIBS-y += -L$(PCAP_DIR) -lpcap

SRCS-y := 		\
	termination.c	\
	jbuff.c		\
	master.c	\
	worker.c	\
	tester.c	\
	secondary.c	\
	main.c

include $(RTE_SDK)/mk/rte.extapp.mk

##############################################################################
endif
##############################################################################
