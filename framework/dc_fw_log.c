
#include <stdio.h>

#include <rte_log.h>

#include "dc_fw_log.h"

int DC_FW_LOGTYPE = -1;
const char const *DC_FW_LOG_NAME = "DCF";

RTE_INIT(framework_log_reg);
static void
framework_log_reg(void)
{
        if (DC_FW_LOGTYPE < 0) {
                int logtype = rte_log_register(DC_FW_LOG_NAME);

                if (logtype < 0) {
                        RTE_LOG(ERR, EAL, "failed register log\n");
                } else {
                        DC_FW_LOGTYPE = logtype;
                        rte_log_set_level(logtype, RTE_LOG_ERR);
                }
        }
}

void
dc_framework_log_init(void)
{
        rte_openlog_stream(stderr);
        rte_log_set_global_level(RTE_LOG_DEBUG);
        dc_framework_log_set_level(RTE_LOG_DEBUG);
}

