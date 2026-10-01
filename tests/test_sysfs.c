/* SPDX-License-Identifier: MIT */
/* The sysfs layer against a fake /sys tree: no hardware, no root. */
#include <assert.h>
#include <errno.h>
#include <ftw.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "kv3p.h"
#include "log.h"
#include "sysfs.h"

static char root[] = "/tmp/kv3p-sysfs-XXXXXX";

static void mkdirs(const char *fmt, ...)
{
	char path[512];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(path, sizeof(path), fmt, ap);
	va_end(ap);

	for (char *p = path + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (mkdir(path, 0755) < 0 && errno != EEXIST)
			assert(0 && "mkdir");
		*p = '/';
	}
	if (mkdir(path, 0755) < 0 && errno != EEXIST)
		assert(0 && "mkdir");
}

static void put(const char *text, const char *fmt, ...)
{
	char path[512];
	va_list ap;
	FILE *f;

	va_start(ap, fmt);
	vsnprintf(path, sizeof(path), fmt, ap);
	va_end(ap);

	f = fopen(path, "we");
	assert(f);
	fputs(text, f);
	fclose(f);
}

static void slurp(char *out, size_t n, const char *fmt, ...)
{
	char path[512];
	va_list ap;
	FILE *f;
	size_t got;

	va_start(ap, fmt);
	vsnprintf(path, sizeof(path), fmt, ap);
	va_end(ap);

	f = fopen(path, "re");
	assert(f);
	got = fread(out, 1, n - 1, f);
	out[got] = '\0';
	fclose(f);
}

static void build_tree(void)
{
	/* The dongle, with razerkraken_cdc holding its comm interface. */
	mkdirs("%s/sys/bus/usb/devices/1-3", root);
	put("1532\n", "%s/sys/bus/usb/devices/1-3/idVendor", root);
	put("052c\n", "%s/sys/bus/usb/devices/1-3/idProduct", root);
	put("1\n", "%s/sys/bus/usb/devices/1-3/busnum", root);
	put("18\n", "%s/sys/bus/usb/devices/1-3/devnum", root);

	mkdirs("%s/sys/bus/usb/devices/1-3:1.4", root);
	mkdirs("%s/sys/bus/usb/devices/1-3:1.5", root);
	mkdirs("%s/sys/bus/usb/drivers/razerkraken_cdc", root);
	mkdirs("%s/sys/bus/usb/drivers/cdc_acm", root);

	/* Another Razer device that must be ignored: a Basilisk V3 Pro. */
	mkdirs("%s/sys/bus/usb/devices/1-2", root);
	put("1532\n", "%s/sys/bus/usb/devices/1-2/idVendor", root);
	put("00ab\n", "%s/sys/bus/usb/devices/1-2/idProduct", root);
	put("1\n", "%s/sys/bus/usb/devices/1-2/busnum", root);
	put("2\n", "%s/sys/bus/usb/devices/1-2/devnum", root);

	/* The patched OpenRazer driver's HID device and its attributes. */
	mkdirs("%s/sys/bus/hid/drivers/razerkraken/0003:1532:052C.001D", root);
	put("1\n", "%s/sys/bus/hid/drivers/razerkraken/0003:1532:052C.001D/charge_status", root);

	put("", "%s/sys/bus/usb/drivers/razerkraken_cdc/bind", root);
	put("", "%s/sys/bus/usb/drivers/razerkraken_cdc/unbind", root);
	put("", "%s/sys/bus/usb/drivers_probe", root);
}

static void bind_driver_symlink(const char *driver)
{
	char link[512], target[512];

	snprintf(link, sizeof(link), "%s/sys/bus/usb/devices/1-3:1.4/driver", root);
	snprintf(target, sizeof(target), "../../../../bus/usb/drivers/%s", driver);
	unlink(link);
	assert(symlink(target, link) == 0);
}

static void test_find_dongle(void)
{
	struct dongle d;

	assert(sysfs_find_dongle(&d) == KV3P_OK);
	assert(strcmp(d.sysname, "1-3") == 0);
	assert(strcmp(d.ifid, "1-3:1.4") == 0);
	assert(strcmp(d.node, "/dev/bus/usb/001/018") == 0);
	assert(d.busnum == 1 && d.devnum == 18);
}

static void test_interface_driver(void)
{
	char drv[DRIVER_LEN];

	bind_driver_symlink("razerkraken_cdc");
	assert(sysfs_interface_driver("1-3", KV3P_IF_COMM, drv, sizeof(drv)) == KV3P_OK);
	assert(strcmp(drv, "razerkraken_cdc") == 0);

	bind_driver_symlink("cdc_acm");
	assert(sysfs_interface_driver("1-3", KV3P_IF_COMM, drv, sizeof(drv)) == KV3P_OK);
	assert(strcmp(drv, "cdc_acm") == 0);

	/* An interface with no driver, which is what a borrow leaves behind. */
	assert(sysfs_interface_driver("1-3", KV3P_IF_DATA, drv, sizeof(drv)) == KV3P_MISSING);
	bind_driver_symlink("razerkraken_cdc");
}

static void test_bind_unbind_writes(void)
{
	char buf[64];

	assert(sysfs_driver_unbind("razerkraken_cdc", "1-3:1.4") == KV3P_OK);
	slurp(buf, sizeof(buf), "%s/sys/bus/usb/drivers/razerkraken_cdc/unbind", root);
	assert(strcmp(buf, "1-3:1.4") == 0);

	assert(sysfs_driver_bind("razerkraken_cdc", "1-3:1.4") == KV3P_OK);
	slurp(buf, sizeof(buf), "%s/sys/bus/usb/drivers/razerkraken_cdc/bind", root);
	assert(strcmp(buf, "1-3:1.4") == 0);

	assert(sysfs_drivers_probe("1-3:1.4") == KV3P_OK);
	slurp(buf, sizeof(buf), "%s/sys/bus/usb/drivers_probe", root);
	assert(strcmp(buf, "1-3:1.4") == 0);

	/* A driver that isn't there must report failure rather than pretending it worked. */
	assert(sysfs_driver_bind("no_such_driver", "1-3:1.4") != KV3P_OK);
}

static void test_charge_status(void)
{
	char hid[HIDNAME_LEN];

	assert(kraken_hid_device(hid, sizeof(hid)) == KV3P_OK);
	assert(strcmp(hid, "0003:1532:052C.001D") == 0);

	assert(kraken_charge_status(hid) == 1);
	put("0\n", "%s/sys/bus/hid/drivers/razerkraken/0003:1532:052C.001D/charge_status", root);
	assert(kraken_charge_status(hid) == 0);
	put("1\n", "%s/sys/bus/hid/drivers/razerkraken/0003:1532:052C.001D/charge_status", root);

	/* -1 means "the driver doesn't know": unknown, not "not charging". */
	put("-1\n", "%s/sys/bus/hid/drivers/razerkraken/0003:1532:052C.001D/charge_status", root);
	assert(kraken_charge_status(hid) < 0);

	assert(kraken_charge_status("0003:1532:052C.nope") == KV3P_MISSING);
}

static void test_nothing_plugged_in(void)
{
	char empty[] = "/tmp/kv3p-empty-XXXXXX";
	struct dongle d;
	char hid[HIDNAME_LEN];

	assert(mkdtemp(empty));
	sysfs_set_root(empty);
	assert(sysfs_find_dongle(&d) == KV3P_MISSING);
	assert(kraken_hid_device(hid, sizeof(hid)) == KV3P_MISSING);
	rmdir(empty);
	sysfs_set_root(root);
}

static int unlink_cb(const char *path, const struct stat *st, int type, struct FTW *ftw)
{
	(void)st;
	(void)type;
	(void)ftw;
	return remove(path);
}

int main(void)
{
	int ret;

	assert(mkdtemp(root));
	build_tree();
	sysfs_set_root(root);
	log_level = LOG_L_ERR;

	test_find_dongle();
	test_interface_driver();
	test_bind_unbind_writes();
	test_charge_status();
	test_nothing_plugged_in();

	ret = nftw(root, unlink_cb, 16, FTW_DEPTH | FTW_PHYS);
	assert(ret == 0);
	printf("sysfs: ok\n");
	return 0;
}
