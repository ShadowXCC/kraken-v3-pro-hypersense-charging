/* SPDX-License-Identifier: MIT */
/*
 * Everything this service learns about the device, it learns from sysfs:
 *   - which dongle is present, and its bus/dev numbers (the usbfs node path);
 *   - which driver owns the CDC control interface, and the bind/unbind files to move it;
 *   - the patched OpenRazer driver's charge_status, which is a pure memory read inside the
 *     driver (unlike charge_level, it never triggers USB traffic).
 *
 * Every path goes through sysfs_root(), so the tests can run the whole lot against a fake tree.
 */

#ifndef KV3P_SYSFS_H
#define KV3P_SYSFS_H

#include <stddef.h>

#define SYSNAME_LEN 32  /* "1-3" */
#define IFID_LEN 48     /* "1-3:1.4" */
#define DRIVER_LEN 64   /* "razerkraken_cdc" */
#define HIDNAME_LEN 64  /* "0003:1532:052C.001D" */
#define NODE_LEN 64     /* "/dev/bus/usb/001/018" */

/* Tests only: prefix every sysfs path with `root`. "" (the default) means the real /sys. */
void sysfs_set_root(const char *root);

struct dongle {
	char sysname[SYSNAME_LEN];
	char ifid[IFID_LEN]; /* the comm interface, e.g. "1-3:1.4" */
	char node[NODE_LEN];
	int busnum;
	int devnum;
};

/* KV3P_OK, or KV3P_MISSING when no 1532:052c is plugged in. */
int sysfs_find_dongle(struct dongle *d);

/* The driver bound to interface `ifno`. KV3P_OK with `drv` set, or KV3P_MISSING when unbound. */
int sysfs_interface_driver(const char *sysname, int ifno, char *drv, size_t n);

/* Write `ifid` to /sys/bus/usb/drivers/<driver>/{bind,unbind}. */
int sysfs_driver_bind(const char *driver, const char *ifid);
int sysfs_driver_unbind(const char *driver, const char *ifid);
/* Ask the bus to re-probe an unbound interface (the fallback if a bind write fails). */
int sysfs_drivers_probe(const char *ifid);

/* The patched OpenRazer razerkraken HID device, e.g. "0003:1532:052C.001D". */
int kraken_hid_device(char *name, size_t n);
/* 1 charging, 0 not; KV3P_MISSING when the attribute isn't there, KV3P_ERR when it can't be read. */
int kraken_charge_status(const char *hidname);

#endif /* KV3P_SYSFS_H */
