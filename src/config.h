#ifndef CONFIG_H_
#define CONFIG_H_

#include <stdint.h>
#include <stdbool.h>

struct server_config {
	// socket settings
	char* bind_address;
	int port;
	int read_timeout_seconds;

	// rf settings
	int gain_mode;
	int gain;
	int ppm;
	int bias_t;
	uint32_t buffer_size;
	// 4GHz max
	uint32_t band_sampling_rate;

	// output settings
	char *base_path;
	bool use_gzip;
};

int create_server_config(struct server_config **config, const char *path);

void destroy_server_config(struct server_config *config);

#endif /* CONFIG_H_ */
