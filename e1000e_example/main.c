#include <dde26.h> /* l4dde26_*() */
#include <dde26_net.h> /* l4dde26 networking */

#include <linux/netdevice.h> /* struct sk_buff */
#include <linux/pci.h> /* pci_unregister_driver() */
#include <linux/init.h>  // initcall()
#include <linux/delay.h> // msleep()
#include <linux/if_vlan.h>

#include <ddekit/semaphore.h>


volatile unsigned long long data_len = 0;

struct pernet_operations __net_initdata loopback_net_ops;

volatile int dde_test_running = 1;
ddekit_sem_t *upsem;
ddekit_sem_t *notify_sem;

#define mac_fmt       "%02X:%02X:%02X:%02X:%02X:%02X"
#define mac_str(mac)  (unsigned char)((mac)[0]), (unsigned char)((mac)[1]),(mac)[2],(mac)[3],(mac)[4],(mac)[5]

void
get_random_bytes(void *buf, int nbytes)
{
	return;
}

struct net_device* open_nw_dev(void);
struct net_device* open_nw_dev()
{
	struct net_device *dev;
	struct net_device *ret;
	struct net *net;
	int err = 0;

	read_lock(&dev_base_lock);
	for_each_net(net) {
		for_each_netdev(net, dev) {
			err = dev_open(dev);
			ret =dev;
			printk("dev: '%s'\n", dev->name);
			printk("MAC: "mac_fmt"\n", mac_str(dev->dev_addr));
		}
	}
	read_unlock(&dev_base_lock);
	return ret;
}

void close_nw_dev(void);
void close_nw_dev(void)
{
	struct net_device *dev;
	struct net *net;

	read_lock(&dev_base_lock);
	for_each_net(net) {
		for_each_netdev(net, dev) {
			int err = 0;

			err = dev_close(dev);
			printk("closed %s\n", dev->name);
		}
	}
	read_unlock(&dev_base_lock);
}

volatile int drop_cnt = 0;
int packets = 0;

inline int get_tailroom(struct sk_buff *skb)
{
	return skb_tailroom(skb);
}

inline unsigned char * get_tail_pointer(struct sk_buff *skb)
{
	return skb_tail_pointer(skb);
}

int
get_length(struct sk_buff *skb)
{
	return skb->len;
}

const void *
get_start(struct sk_buff *skb)
{
	return skb->mac_header;
}

struct sk_buff*
get_skb(struct net_device *netdev)
{
	struct sk_buff *skb;
	skb = netdev_alloc_skb(netdev, ETH_FRAME_LEN + VLAN_HLEN + ETH_FCS_LEN + NET_IP_ALIGN);
	skb_reserve(skb, NET_IP_ALIGN);
	return skb;
}

void
csum_and_transmit(struct sk_buff *skb)
{
	skb->csum = csum_partial(skb->data, skb->len, skb->csum);
	skb->csum = csum_fold(skb->csum);
	
	skb->dev->netdev_ops->ndo_start_xmit(skb, skb->dev);
}

static int net_rx_handle(struct sk_buff *skb)
{
	packets++;
	drop_cnt += deferred_handler(skb);
	return NET_RX_SUCCESS;
}

extern int write_idx, read_idx;

void
stat_func(void *arg)
{
	while(1) {
		ddekit_sem_down_timed(upsem, 10000);
		if(!packets)
			packets++;
		ddekit_printf("%llu kBit/s %llu, %d packets, %d dropped (%d %%), indexes: %d:%d\n", (data_len * 8) / 1000 / 10, data_len, packets, drop_cnt, drop_cnt*100 / packets, write_idx, read_idx);
		packets = 0;
		data_len = 0;
		drop_cnt = 0;
	}
}	

int main(int argc, char **argv)
{
	struct sk_buff *skb;
	upsem = ddekit_sem_init(0);
	notify_sem = ddekit_sem_init(0);

	atexit(close_nw_dev);

	long long unsigned last_arp_time = jiffies, last_time = jiffies;

	struct net_device *eth0;
	l4dde26_softirq_init();

	printk("Initializing skb subsystem\n");
	skb_init();

	
	eth0 = open_nw_dev();
	
	while(!netif_carrier_ok(eth0))
		sleep(1);

	if(extern_init_func(eth0)) {
		printf("TAP init failed\n");
		goto out;
	}

	ddekit_thread_create(stat_func, NULL, "stat thread", 0);
	printk("Setting rx callback @ %p\n", net_rx_handle);
	l4dde26_register_rx_callback(net_rx_handle);
	while(dde_test_running) {  
		/* just sleep on the semaphore */
		ddekit_sem_down(notify_sem);
	}

out:
	close_nw_dev();
	return 0;
}
