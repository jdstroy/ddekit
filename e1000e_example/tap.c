#include <stdio.h>
#include <string.h>
#include <linux/if_tun.h>
#include <fcntl.h>
#include <net/if.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <linux/sockios.h>

#include "tap.h"

void
tap_set_debug(int fd, int level)
{
	int ret;
	
	ret = ioctl(fd, TUNSETDEBUG, level);
	if(ret < 0)
		printf("%s: %s (%d)\n", __func__, strerror(errno), errno);
}

void
tap_set_mac(struct tap_struct *tap, unsigned char *mac)
{
	int ret, i;
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(struct ifreq));
	strncpy(ifr.ifr_name, tap->name, IFNAMSIZ);

	ret = ioctl(tap->fd, SIOCGIFHWADDR, &ifr);
	if(ret < 0)
		goto err;

	for (i=0; i<6; i++)
		ifr.ifr_hwaddr.sa_data[i] = mac[i];
	ret = ioctl(tap->fd, SIOCSIFHWADDR, &ifr);
	if(ret < 0)
		goto err;

	return;
err:
	printf("%s: %s (%d)\n", __func__, strerror(errno), errno);

}

int
set_ip_and_ifup(struct tap_struct *tap)
{
	int ret;
	int sockfd;
	struct ifreq ifr;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0) {
		printf("%s(%d): %s: (%d)\n", __func__, __LINE__, strerror(errno), errno);
		return sockfd;
	}

	memset(&ifr, 0, sizeof(struct ifreq));

	((struct sockaddr_in *)(&(ifr.ifr_addr)))->sin_addr.s_addr = inet_addr("192.168.0.2");
	ifr.ifr_addr.sa_family = AF_INET;

	memcpy(&ifr.ifr_name, tap->name, IFNAMSIZ);

	ret = ioctl(sockfd, SIOCSIFADDR, &ifr);
	if(ret < 0) {
		printf("%s(%d): %s: (%d)\n", __func__, __LINE__, strerror(errno), errno);
		return ret;
	}
	
	memset(&ifr, 0, sizeof(struct ifreq));
	memcpy(&ifr.ifr_name, tap->name, IFNAMSIZ);
	
	ret = ioctl(sockfd, SIOCGIFFLAGS, &ifr);
	if(ret < 0) {
		printf("%s(%d): %s: (%d)\n", __func__, __LINE__, strerror(errno), errno);
		return ret;
	}


	ifr.ifr_flags |= IFF_UP | IFF_RUNNING;

	ret = ioctl(sockfd, SIOCSIFFLAGS, &ifr);
	if(ret < 0)
		printf("%s(%d): %s: (%d)\n", __func__, __LINE__, strerror(errno), errno);

	return ret;
}

int
tun_alloc(char *dev_name)
{
	struct ifreq ifr;
	int fd, err;

	if( (fd = open("/dev/net/tun", O_RDWR)) < 0 )
		return fd;

	memset(&ifr, 0, sizeof(ifr));

	/* Flags: IFF_TUN   - TUN device (no Ethernet headers) 
	*        IFF_TAP   - TAP device  
	*
	*        IFF_NO_PI - Do not provide packet information  
	*/ 
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI; 
	if( *dev_name )
		strncpy(ifr.ifr_name, dev_name, IFNAMSIZ);

	if( (err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0 ){
		close(fd);
		printf("%s: %s (%d)\n", __func__, strerror(errno), errno);
		return err;
	}

	strcpy(dev_name, ifr.ifr_name);
	return fd;
}

struct tap_struct *
tap_init() {
	//unsigned char mac[] = {0x00, 0xde, 0xad, 0xba, 0xbe, 0x00};
	unsigned char mac[] = {0x00, 0x1c, 0xc0, 0xfa, 0x0e, 0x3b};
	struct tap_struct *tap = NULL;

	tap = malloc(sizeof(*tap));
	if(!tap)
		goto out;

	tap->name = (char*)malloc(IFNAMSIZ);
	if(!tap->name)
		goto err;

	tap->name[0] = 0;

	tap->fd = tun_alloc(tap->name);

	//tap_set_mac(tap, mac);

	if(set_ip_and_ifup(tap))
		goto err_ip;

	return tap;

err_ip:
	close(tap->fd);
	free(tap->name);
err:
	free(tap);
	tap = NULL;
out:
	printf("%s: %s (%d)\n", __func__, strerror(errno), errno);
	return tap;
}
