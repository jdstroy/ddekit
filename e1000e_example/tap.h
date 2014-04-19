#pragma once

struct tap_struct {
	int fd;
	struct net_device *dev;
	char *name;
};


