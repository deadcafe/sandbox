#ifndef _ENGINE_H_
#define _ENGINE_H_

struct engine_config_s {
	/* something */
        const char *prog;
        const char *fname;
};

extern int start_engine(struct engine_config_s *);

#endif /* !_ENGINE_H_ */
