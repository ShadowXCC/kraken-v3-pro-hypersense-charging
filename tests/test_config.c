/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "kv3p.h"
#include "log.h"

static char path[] = "/tmp/kv3p-conf-XXXXXX";

static void write_conf(const char *text)
{
	FILE *f = fopen(path, "we");

	assert(f);
	fputs(text, f);
	fclose(f);
}

static void test_defaults(void)
{
	struct config c;

	config_defaults(&c);
	assert(c.chroma == 1);
	assert(c.poll_seconds == 0.5);
	assert(c.settle_seconds == 2.0);
	assert(c.min_send_interval == 5.0);
	assert(c.keepalive_seconds == 0.0); /* the keepalive is opt-in */
}

static void test_full_file(void)
{
	struct config c;

	config_defaults(&c);
	write_conf("# a comment\n"
		   "chroma = no\n"
		   "poll_seconds=1.5\n"
		   "   settle_seconds   =   3   ; trailing comment\n"
		   "min_send_interval = 10\n"
		   "keepalive_seconds = 120\n"
		   "\n");
	assert(config_load(&c, path) == KV3P_OK);
	assert(c.chroma == 0);
	assert(c.poll_seconds == 1.5);
	assert(c.settle_seconds == 3.0);
	assert(c.min_send_interval == 10.0);
	assert(c.keepalive_seconds == 120.0);
}

static void test_bools(void)
{
	struct config c;
	static const char *const yes[] = { "yes", "true", "on", "1", "YES", "True" };
	static const char *const no[] = { "no", "false", "off", "0", "NO", "Off" };
	char buf[64];

	for (size_t i = 0; i < sizeof(yes) / sizeof(*yes); i++) {
		config_defaults(&c);
		c.chroma = 0;
		snprintf(buf, sizeof(buf), "chroma = %s\n", yes[i]);
		write_conf(buf);
		assert(config_load(&c, path) == KV3P_OK);
		assert(c.chroma == 1);
	}
	for (size_t i = 0; i < sizeof(no) / sizeof(*no); i++) {
		config_defaults(&c);
		snprintf(buf, sizeof(buf), "chroma = %s\n", no[i]);
		write_conf(buf);
		assert(config_load(&c, path) == KV3P_OK);
		assert(c.chroma == 0);
	}
}

/* A broken value must never silently become something dangerous: the default stands. */
static void test_bad_values_keep_defaults(void)
{
	struct config c;

	config_defaults(&c);
	write_conf("chroma = maybe\n"
		   "poll_seconds = -3\n"
		   "settle_seconds = fast\n"
		   "min_send_interval = 99999\n"
		   "keepalive_seconds = 4nonsense\n"
		   "what_is_this = 7\n"
		   "a line with no equals sign\n");
	assert(config_load(&c, path) == KV3P_OK);
	assert(c.chroma == 1);
	assert(c.poll_seconds == 0.5);
	assert(c.settle_seconds == 2.0);
	assert(c.min_send_interval == 5.0);
	assert(c.keepalive_seconds == 0.0);
}

static void test_missing_file(void)
{
	struct config c;

	config_defaults(&c);
	assert(config_load(&c, "/tmp/kv3p-does-not-exist-9d3f") == KV3P_MISSING);
	assert(c.poll_seconds == 0.5);
}

int main(void)
{
	int fd = mkstemp(path);

	assert(fd >= 0);
	close(fd);
	log_level = LOG_L_ERR; /* the bad-value test warns on purpose */

	test_defaults();
	test_full_file();
	test_bools();
	test_bad_values_keep_defaults();
	test_missing_file();

	unlink(path);
	printf("config: ok\n");
	return 0;
}
