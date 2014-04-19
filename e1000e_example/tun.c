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

int
tun_alloc(char *dev)
{
	int i;
	struct ifreq ifr;
	int fd, err;
	unsigned char mac[] = {0x10, 0x22, 0x33, 0x44, 0x55, 0x66, 0x0}; 

	if( (fd = open("/dev/net/tun", O_RDWR)) < 0 )
		return fd;

	memset(&ifr, 0, sizeof(ifr));

	/* Flags: IFF_TUN   - TUN device (no Ethernet headers) 
	*        IFF_TAP   - TAP device  
	*
	*        IFF_NO_PI - Do not provide packet information  
	*/ 
	ifr.ifr_flags = IFF_TAP; 
	if( *dev )
		strncpy(ifr.ifr_name, dev, IFNAMSIZ);

	if( (err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0 ){
		close(fd);
		printf("%s: %s (%d)\n", __func__, strerror(errno), errno);
		return err;
	}
	err = ioctl(fd, TUNSETDEBUG, 1);
	if(err < 0)
		printf("%s: %s (%d)\n", __func__, strerror(errno), errno);

	strcpy(dev, ifr.ifr_name);
	memset(&ifr, 0, sizeof(struct ifreq));
	strncpy(ifr.ifr_name, dev, IFNAMSIZ);
	err = ioctl(fd, SIOCGIFHWADDR, &ifr);
/*
	for (i=0; i<6; i++)
		ifr.ifr_hwaddr.sa_data[i] = mac[i];
	err = ioctl(fd, SIOCSIFHWADDR, &ifr);
	if(err < 0)
		printf("%s: %s (%d)\n", __func__, strerror(errno), errno);
*/
	return fd;
}

char *name;

#define BUF_SZ 4096
int main() {
	int n;
	char *buf;
	int tap_fd;
	name = malloc(IFNAMSIZ);
	name[0] = 0;

	tap_fd = tun_alloc(name);

	printf("tap %d\n", tap_fd);
	printf("%s\n", name);
	
	/* set IP address */
	int ret;
	int sockfd;
	struct ifreq ifr;

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if(sockfd < 0)
		printf("%s: %s: (%d)\n", __func__, strerror(errno), errno);
	//set_mac(tap_fd);

	memset(&ifr, 0, sizeof(struct ifreq));

	((struct sockaddr_in *)(&(ifr.ifr_addr)))->sin_addr.s_addr = inet_addr("192.168.0.2");
	ifr.ifr_addr.sa_family = AF_INET;

	memcpy(&ifr.ifr_name, name, IFNAMSIZ);

	ret = ioctl(sockfd, SIOCSIFADDR, &ifr);
	if(ret < 0)
		printf("%s: %s (%d)\n", __func__, strerror(errno), errno);
	
	memset(&ifr, 0, sizeof(struct ifreq));
	memcpy(&ifr.ifr_name, name, IFNAMSIZ);
	ret = ioctl(sockfd, SIOCGIFFLAGS, &ifr);
	if(ret < 0)
		printf("%s: %s (%d)\n", __func__, strerror(errno), errno);

	printf("flags: 0x%08x\n", ifr.ifr_flags);
	ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
	printf("flags: 0x%08x\n", ifr.ifr_flags);

	ret = ioctl(sockfd, SIOCSIFFLAGS, &ifr);
	if(ret < 0)
		printf("%s: %s (%d)\n", __func__, strerror(errno), errno);

	buf = (char*) malloc(BUF_SZ);
	while(1) {
		memset(buf, 0, BUF_SZ);
		n = read(tap_fd, buf, BUF_SZ);
		if(n< 0)
			printf("%s: %s (%d)\n", __func__, strerror(errno), errno);
		else
			printf("read %d bytes from %s\n", n, name);
	}
}
