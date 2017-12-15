#ifndef _DC_FW_LOG_H_
#define _DC_FW_LOG_H_

#include <stdint.h>

#include <rte_log.h>

extern int DC_FW_LOGTYPE;
extern const char const *DC_FW_LOG_NAME;
extern void dc_framework_log_init(void);

#define dc_framework_log_set_level(_lv)	rte_log_set_level(DC_FW_LOGTYPE, (_lv))

#ifdef DC_LOG_ENABLE_FUNC
# define DC_FW_LOG(_lv,_fmt, args...)                                   \
        rte_log(RTE_LOG_ ## _lv,                                        \
                DC_FW_LOGTYPE,                                          \
                "%s: %s(%d) " _fmt "\n",                                \
                DC_FW_LOG_NAME, __func__, __LINE__, ##args)
#else
# define DC_FW_LOG(_lv,_fmt, args...)                                   \
        rte_log(RTE_LOG_ ## _lv,                                        \
                DC_FW_LOGTYPE,                                          \
                "%s: " _fmt "\n",                                       \
                DC_FW_LOG_NAME, ##args)
#endif

#define DC_FW_DEBUG(_fmt, args...)	DC_FW_LOG(DEBUG, _fmt, ## args)
#define DC_FW_INFO(_fmt, args...)	DC_FW_LOG(INFO, _fmt, ## args)
#define DC_FW_NOTICE(_fmt, args...)	DC_FW_LOG(NOTICE, _fmt, ## args)
#define DC_FW_WARN(_fmt, args...)	DC_FW_LOG(WARNING, _fmt, ## args)
#define DC_FW_ERR(_fmt, args...)	DC_FW_LOG(ERR, _fmt, ## args)

#endif	/* !_DC_FW_LOG_H_ */
