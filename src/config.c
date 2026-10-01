/* SPDX-License-Identifier: MIT */
#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kv3p.h"
#include "log.h"

void config_defaults(struct config *c)
{
	c->chroma = 1;
	c->poll_seconds = 0.5;
	c->settle_seconds = 2.0;
	c->min_send_interval = 5.0;
	c->keepalive_seconds = 0.0;
}

static char *trim(char *s)
{
	char *end;

	while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
		s++;
	end = s + strlen(s);
	while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
		end--;
	*end = '\0';
	return s;
}

static int parse_bool(const char *v, int *out)
{
	if (!strcasecmp(v, "yes") || !strcasecmp(v, "true") || !strcasecmp(v, "on") || !strcmp(v, "1")) {
		*out = 1;
		return 0;
	}
	if (!strcasecmp(v, "no") || !strcasecmp(v, "false") || !strcasecmp(v, "off") || !strcmp(v, "0")) {
		*out = 0;
		return 0;
	}
	return -1;
}

static int parse_seconds(const char *v, double lo, double hi, double *out)
{
	char *end;
	double d;

	errno = 0;
	d = strtod(v, &end);
	if (errno || end == v || *trim(end) != '\0' || d < lo || d > hi)
		return -1;
	*out = d;
	return 0;
}

int config_load(struct config *c, const char *path)
{
	char line[512];
	FILE *f = fopen(path, "re");
	int lineno = 0;

	if (!f)
		return errno == ENOENT ? KV3P_MISSING : KV3P_ERR;

	while (fgets(line, sizeof(line), f)) {
		char *key, *value, *eq, *hash;
		int bad = 0;

		lineno++;
		hash = strpbrk(line, "#;");
		if (hash)
			*hash = '\0';
		eq = strchr(line, '=');
		if (!eq) {
			if (*trim(line))
				log_warn("%s:%d: ignoring line without '='", path, lineno);
			continue;
		}
		*eq = '\0';
		key = trim(line);
		value = trim(eq + 1);
		if (!*key)
			continue;

		if (!strcmp(key, "chroma"))
			bad = parse_bool(value, &c->chroma);
		else if (!strcmp(key, "poll_seconds"))
			bad = parse_seconds(value, 0.05, 60.0, &c->poll_seconds);
		else if (!strcmp(key, "settle_seconds"))
			bad = parse_seconds(value, 0.0, 60.0, &c->settle_seconds);
		else if (!strcmp(key, "min_send_interval"))
			bad = parse_seconds(value, 0.0, 3600.0, &c->min_send_interval);
		else if (!strcmp(key, "keepalive_seconds"))
			bad = parse_seconds(value, 0.0, 86400.0, &c->keepalive_seconds);
		else
			log_warn("%s:%d: unknown setting '%s'", path, lineno, key);

		if (bad)
			log_warn("%s:%d: '%s' is not a valid value for %s; keeping the default",
				 path, lineno, value, key);
	}

	fclose(f);
	return KV3P_OK;
}
