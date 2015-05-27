#ifndef __TEST_UTIL_H__
#define __TEST_UTIL_H__


void parse_config_data(const char *config_source,
                              const char *config_data,
                              int config_len,
                              libxl_domain_config *d_config);

int freemem(uint32_t domid, libxl_domain_build_info *b_info);
#endif
