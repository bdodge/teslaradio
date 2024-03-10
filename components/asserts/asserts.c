
#include "asserts.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(asserts, LOG_LEVEL_INF);

void assert_err(const char *file, const int line)
{
	LOG_ERR("%s: %d", file, line);
}

