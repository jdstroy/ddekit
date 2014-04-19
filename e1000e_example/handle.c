#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#include <errno.h>
#include <net/if.h>
#include <stdio.h>
#include <semaphore.h>
#include<time.h>

#define NET_IP_ALIGN 2

#include <ddekit/semaphore.h>
#include <ddekit/printf.h>

#include "tap.h"

struct sk_buff;
struct net_device;

struct tap_struct tap_rx;

#define SIZE 16
struct sk_buff* buffer[SIZE];
volatile int write_idx = 0, read_idx = 0;
sem_t idx_sem;

static void
print_tcp_flags(uint8_t flags)
{
	ddekit_printf("%c", (flags & (1 << 5)) ? 'U' : '.');
	ddekit_printf("%c", (flags & (1 << 4)) ? 'A' : '.');
	ddekit_printf("%c", (flags & (1 << 3)) ? 'P' : '.');
	ddekit_printf("%c", (flags & (1 << 2)) ? 'R' : '.');
	ddekit_printf("%c", (flags & (1 << 1)) ? 'S' : '.');
	ddekit_printf("%c", (flags & (1 << 0)) ? 'F' : '.');
}

#define ip_fmt       "%d.%d.%d.%d"
#define ip_str(ip)  (unsigned char)((ip)[0]), (unsigned char)((ip)[1]),(unsigned char)(ip)[2],(unsigned char)(ip)[3]

#define port_str(port) (unsigned short)(port[0] << 8 | port[1])
struct sk_buff * get_skb(struct net_device *);
int get_len(void);
/* defined in linux/skbuff.h, but we can't get it because of header foo */
unsigned char * skb_put(struct sk_buff*, int);
inline unsigned char * get_tail_pointer(struct sk_buff *);
inline int get_tailroom(struct sk_buff *);

void
transmit(void *arg)
{
	int i;
	int ret;
	unsigned char *skb_data;
	unsigned char *data;
	
	struct sk_buff *skb = NULL;
	struct tap_struct *tap;

	tap = (struct tap_struct*)arg;

	while(1) {
		if(!skb)
			skb = get_skb(tap->dev);
		
		data = get_tail_pointer(skb);
		ret = read(tap->fd, data, get_tailroom(skb));

		if(ret <= 0)
			continue;
		data = skb_put(skb, ret);

		csum_and_transmit(skb);
		skb = NULL;

	}
}

const void * get_start(struct sk_buff*);

struct tap_struct * tap_init(void);
/* we can't dereference struct net_device * here, so pass in the flags as extra parameter */

int
extern_init_func(struct net_device *eth0, int flags)
{
	char name[16];
	struct tap_struct *tap;

	tap = tap_init();
	if(!tap)
		return -1;
	tap->dev = eth0;
	memcpy(&tap_rx, tap, sizeof(*tap));
	dev_set_promiscuity(eth0, 1);

	sem_init(&idx_sem, 0, 1);

	snprintf(name, 16, "rx_thread_%s", tap->name);
	ddekit_thread_create(transmit, tap, name, 0);

	return 0;
}

extern volatile unsigned long long data_len;
int
deferred_handler(struct sk_buff *skb)
{
	int ret;
	int len;

	len = get_length(skb) + 14;
	data_len +=len;
	ret = write(tap_rx.fd, get_start(skb), len);

	if(ret < 0)
		printf("%s: %s (%d)\n", __func__, strerror(errno), errno);

	kfree_skb(skb);

}


