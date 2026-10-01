/* SPDX-License-Identifier: MIT */
#include "sysfs.h"

#include <dirent.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kv3p.h"
#include "log.h"

#define USB_DEVICES "/sys/bus/usb/devices"
#define USB_DRIVERS "/sys/bus/usb/drivers"
#define HID_RAZERKRAKEN "/sys/bus/hid/drivers/razerkraken"
#define HID_PREFIX "0003:1532:052C."

static char root_prefix[PATH_MAX];

void sysfs_set_root(const char *root)
{
	snprintf(root_prefix, sizeof(root_prefix), "%s", root ? root : "");
}

static void mkpath(char *out, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

static void mkpath(char *out, size_t n, const char *fmt, ...)
{
	va_list ap;
	int w;

	w = snprintf(out, n, "%s", root_prefix);
	if (w < 0 || (size_t)w >= n) {
		out[0] = '\0';
		return;
	}
	va_start(ap, fmt);
	vsnprintf(out + w, n - (size_t)w, fmt, ap);
	va_end(ap);
}

static int read_text(const char *path, char *out, size_t n)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t got;

	if (fd < 0)
		return errno == ENOENT ? KV3P_MISSING : KV3P_ERR;
	got = read(fd, out, n - 1);
	close(fd);
	if (got < 0)
		return KV3P_ERR;
	out[got] = '\0';
	while (got > 0 && (out[got - 1] == '\n' || out[got - 1] == ' '))
		out[--got] = '\0';
	return KV3P_OK;
}

static int read_int(const char *path, int base, int *out)
{
	char buf[64];
	char *end;
	long v;
	int ret = read_text(path, buf, sizeof(buf));

	if (ret != KV3P_OK)
		return ret;
	errno = 0;
	v = strtol(buf, &end, base);
	if (errno || end == buf || v < INT_MIN || v > INT_MAX)
		return KV3P_ERR;
	*out = (int)v;
	return KV3P_OK;
}

static int write_text(const char *path, const char *text)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC);
	ssize_t wrote;

	if (fd < 0) {
		log_debug("open %s: %s", path, strerror(errno));
		return errno == ENOENT ? KV3P_MISSING : KV3P_ERR;
	}
	wrote = write(fd, text, strlen(text));
	if (wrote < 0)
		log_debug("write %s to %s: %s", text, path, strerror(errno));
	close(fd);
	return wrote == (ssize_t)strlen(text) ? KV3P_OK : KV3P_ERR;
}

int sysfs_find_dongle(struct dongle *d)
{
	char path[PATH_MAX];
	struct dirent *ent;
	DIR *dir;
	int found = KV3P_MISSING;

	mkpath(path, sizeof(path), USB_DEVICES);
	dir = opendir(path);
	if (!dir)
		return KV3P_MISSING;

	while ((ent = readdir(dir))) {
		int vid = 0, pid = 0, busnum = 0, devnum = 0;
		char name[SYSNAME_LEN];
		size_t namelen = strlen(ent->d_name);

		if (ent->d_name[0] == '.' || strchr(ent->d_name, ':'))
			continue; /* interfaces, not devices */
		/* A USB sysname is "1-3" or "1-6.4.2"; anything that would not fit is not one. */
		if (namelen >= sizeof(name) - 8)
			continue;
		memcpy(name, ent->d_name, namelen + 1);

		mkpath(path, sizeof(path), USB_DEVICES "/%s/idVendor", name);
		if (read_int(path, 16, &vid) != KV3P_OK || vid != KV3P_VID)
			continue;
		mkpath(path, sizeof(path), USB_DEVICES "/%s/idProduct", name);
		if (read_int(path, 16, &pid) != KV3P_OK || pid != KV3P_PID)
			continue;
		mkpath(path, sizeof(path), USB_DEVICES "/%s/busnum", name);
		if (read_int(path, 10, &busnum) != KV3P_OK)
			continue;
		mkpath(path, sizeof(path), USB_DEVICES "/%s/devnum", name);
		if (read_int(path, 10, &devnum) != KV3P_OK)
			continue;

		snprintf(d->sysname, sizeof(d->sysname), "%s", name);
		snprintf(d->ifid, sizeof(d->ifid), "%s:1.%d", name, KV3P_IF_COMM);
		snprintf(d->node, sizeof(d->node), "/dev/bus/usb/%03d/%03d", busnum, devnum);
		d->busnum = busnum;
		d->devnum = devnum;
		found = KV3P_OK;
		break;
	}

	closedir(dir);
	return found;
}

int sysfs_interface_driver(const char *sysname, int ifno, char *drv, size_t n)
{
	char path[PATH_MAX];
	char target[PATH_MAX];
	ssize_t len;
	const char *base;

	mkpath(path, sizeof(path), USB_DEVICES "/%s:1.%d/driver", sysname, ifno);
	len = readlink(path, target, sizeof(target) - 1);
	if (len < 0)
		return errno == ENOENT ? KV3P_MISSING : KV3P_ERR;
	target[len] = '\0';
	base = strrchr(target, '/');
	snprintf(drv, n, "%s", base ? base + 1 : target);
	return KV3P_OK;
}

static int driver_action(const char *driver, const char *action, const char *ifid)
{
	char path[PATH_MAX];

	mkpath(path, sizeof(path), USB_DRIVERS "/%s/%s", driver, action);
	return write_text(path, ifid);
}

int sysfs_driver_bind(const char *driver, const char *ifid)
{
	return driver_action(driver, "bind", ifid);
}

int sysfs_driver_unbind(const char *driver, const char *ifid)
{
	return driver_action(driver, "unbind", ifid);
}

int sysfs_drivers_probe(const char *ifid)
{
	char path[PATH_MAX];

	mkpath(path, sizeof(path), "/sys/bus/usb/drivers_probe");
	return write_text(path, ifid);
}

int kraken_hid_device(char *name, size_t n)
{
	char path[PATH_MAX];
	struct dirent *ent;
	DIR *dir;
	int found = KV3P_MISSING;

	mkpath(path, sizeof(path), HID_RAZERKRAKEN);
	dir = opendir(path);
	if (!dir)
		return KV3P_MISSING;

	while ((ent = readdir(dir))) {
		if (strncasecmp(ent->d_name, HID_PREFIX, strlen(HID_PREFIX)) != 0)
			continue;
		snprintf(name, n, "%s", ent->d_name);
		found = KV3P_OK;
		break;
	}

	closedir(dir);
	return found;
}

int kraken_charge_status(const char *hidname)
{
	char path[PATH_MAX];
	int value = 0;
	int ret;

	mkpath(path, sizeof(path), HID_RAZERKRAKEN "/%s/charge_status", hidname);
	ret = read_int(path, 10, &value);
	if (ret != KV3P_OK)
		return ret;
	if (value < 0) /* the driver uses -1 for "not known yet" on some attributes */
		return KV3P_ERR;
	return value ? 1 : 0;
}
