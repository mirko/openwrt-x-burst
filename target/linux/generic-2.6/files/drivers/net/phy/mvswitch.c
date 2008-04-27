/*
 * Marvell 88E6060 switch driver
 * Copyright (c) 2008 Felix Fietkau <nbd@openwrt.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/if_vlan.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include "mvswitch.h"

MODULE_DESCRIPTION("Marvell 88E6060 Switch driver");
MODULE_AUTHOR("Felix Fietkau");
MODULE_LICENSE("GPL");

struct mvswitch_priv {
	/* the driver's tx function */
	int (*hardstart)(struct sk_buff *skb, struct net_device *dev);
	struct vlan_group *grp;
	u8 vlans[16];
};

#define to_mvsw(_phy) ((struct mvswitch_priv *) (_phy)->priv)

static inline u16
r16(struct phy_device *phydev, int addr, int reg)
{
	return phydev->bus->read(phydev->bus, addr, reg);
}

static inline void
w16(struct phy_device *phydev, int addr, int reg, u16 val)
{
	phydev->bus->write(phydev->bus, addr, reg, val);
}

static int
mvswitch_mangle_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct mvswitch_priv *priv;
	struct vlan_ethhdr *eh;
	char *buf = NULL;
	u16 vid;

	priv = dev->phy_ptr;
	if (unlikely(!priv))
		goto error;

	if (unlikely(skb->len < 16))
		goto error;

	eh = (struct vlan_ethhdr *) skb->data;
	if (be16_to_cpu(eh->h_vlan_proto) != 0x8100)
		goto error;

	vid = be16_to_cpu(eh->h_vlan_TCI) & VLAN_VID_MASK;
	if (unlikely((vid > 15 || !priv->vlans[vid])))
		goto error;

	if (skb->len <= 64) {
		if (pskb_expand_head(skb, 0, 68 - skb->len, GFP_ATOMIC)) {
			if (net_ratelimit())
				printk("%s: failed to expand/update skb for the switch\n", dev->name);
			goto error;
		}

		buf = skb->data + 64;
		skb->len = 68;
	} else {
		if (skb_cloned(skb) || unlikely(skb_tailroom(skb) < 4)) {
			if (pskb_expand_head(skb, 0, 4, GFP_ATOMIC)) {
				if (net_ratelimit())
					printk("%s: failed to expand/update skb for the switch\n", dev->name);
				goto error;
			}
		}
		buf = skb_put(skb, 4);
	}

	/* move the ethernet header 4 bytes forward, overwriting the vlan tag */
	memmove(skb->data + 4, skb->data, 12);
	skb->data += 4;
	skb->len -= 4;
	skb->mac_header += 4;

	if (!buf)
		goto error;

	/* append the tag */
	*((u32 *) buf) = (
		(0x80 << 24) |
		((priv->vlans[vid] & 0x1f) << 16)
	);

	return priv->hardstart(skb, dev);

error:
	/* any errors? drop the packet! */
	dev_kfree_skb_any(skb);
	return 0;
}

static int
mvswitch_mangle_rx(struct sk_buff *skb, int napi)
{
	struct mvswitch_priv *priv;
	struct net_device *dev;
	int vlan = -1;
	unsigned char *buf;
	int i;

	dev = skb->dev;
	if (!dev)
		goto error;

	priv = dev->phy_ptr;
	if (!priv)
		goto error;

	if (!priv->grp)
		goto error;

	buf = skb->data + skb->len - 4;
	if (buf[0] != 0x80)
		goto error;

	/* look for the vlan matching the incoming port */
	for (i = 0; i < ARRAY_SIZE(priv->vlans); i++) {
		if ((1 << buf[1]) & priv->vlans[i])
			vlan = i;
	}

	if (vlan == -1)
		goto error;

	if (napi)
		return vlan_hwaccel_receive_skb(skb, priv->grp, vlan);
	else
		return vlan_hwaccel_rx(skb, priv->grp, vlan);

error:
	/* no vlan? eat the packet! */
	dev_kfree_skb_any(skb);
	return 0;
}


static int
mvswitch_netif_rx(struct sk_buff *skb)
{
	return mvswitch_mangle_rx(skb, 0);
}

static int
mvswitch_netif_receive_skb(struct sk_buff *skb)
{
	return mvswitch_mangle_rx(skb, 1);
}


static void
mvswitch_vlan_rx_register(struct net_device *dev, struct vlan_group *grp)
{
	struct mvswitch_priv *priv = dev->phy_ptr;
	priv->grp = grp;
}


static int
mvswitch_config_init(struct phy_device *pdev)
{
	struct mvswitch_priv *priv = to_mvsw(pdev);
	struct net_device *dev = pdev->attached_dev;
	u8 vlmap = 0;
	int i;

	if (!dev)
		return -EINVAL;

	printk("%s: Marvell 88E6060 PHY driver attached.\n", dev->name);
	pdev->supported = ADVERTISED_100baseT_Full;
	pdev->advertising = ADVERTISED_100baseT_Full;
	dev->phy_ptr = priv;

	/* initialize default vlans */
	for (i = 0; i < MV_PORTS; i++)
		priv->vlans[(i == MV_WANPORT ? 1 : 0)] |= (1 << i);

	/* before entering reset, disable all ports */
	for (i = 0; i < MV_PORTS; i++)
		w16(pdev, MV_PORTREG(CONTROL, i), 0x00);

	msleep(2); /* wait for the status change to settle in */

	/* put the device in reset and set ATU flags */
	w16(pdev, MV_SWITCHREG(ATU_CTRL),
		MV_ATUCTL_RESET |
		MV_ATUCTL_ATU_1K |
		MV_ATUCTL_AGETIME(4080) /* maximum */
	);

	i = 100; /* timeout */
	do {
		if (!(r16(pdev, MV_SWITCHREG(ATU_CTRL)) & MV_ATUCTL_RESET))
			break;
		msleep(1);
	} while (--i > 0);

	if (!i) {
		printk("%s: Timeout waiting for the switch to reset.\n", dev->name);
		return -ETIMEDOUT;
	}

	/* initialize the cpu port */
	w16(pdev, MV_PORTREG(CONTROL, MV_CPUPORT),
		MV_PORTCTRL_ENABLED |
		MV_PORTCTRL_RXTR |
		MV_PORTCTRL_TXTR
	);
	/* wait for the phy change to settle in */
	msleep(2);
	for (i = 0; i < MV_PORTS; i++) {
		u8 pvid = 0;
		int j;

		vlmap = 0;

		/* look for the matching vlan */
		for (j = 0; j < ARRAY_SIZE(priv->vlans); j++) {
			if (priv->vlans[j] & (1 << i)) {
				vlmap = priv->vlans[j];
				pvid = j;
			}
		}
		/* leave port unconfigured if it's not part of a vlan */
		if (!vlmap)
			break;

		/* add the cpu port to the allowed destinations list */
		vlmap |= (1 << MV_CPUPORT);

		/* take port out of its own vlan destination map */
		vlmap &= ~(1 << i);

		/* apply vlan settings */
		w16(pdev, MV_PORTREG(VLANMAP, i),
			MV_PORTVLAN_PORTS(vlmap) |
			MV_PORTVLAN_ID(pvid)
		);

		/* re-enable port */
		w16(pdev, MV_PORTREG(CONTROL, i), MV_PORTCTRL_ENABLED);
	}

	/* build the target list for the cpu port */
	for (i = 0; i < MV_PORTS; i++)
		vlmap |= (1 << i);

	w16(pdev, MV_PORTREG(VLANMAP, MV_CPUPORT),
		MV_PORTVLAN_PORTS(vlmap)
	);

	/* set the port association vector */
	for (i = 0; i <= MV_PORTS; i++) {
		w16(pdev, MV_PORTREG(ASSOC, i),
			MV_PORTASSOC_PORTS(1 << i)
		);
	}

	/* hook into the tx function */
	priv->hardstart = dev->hard_start_xmit;
	pdev->netif_receive_skb = mvswitch_netif_receive_skb;
	pdev->netif_rx = mvswitch_netif_rx;
	dev->hard_start_xmit = mvswitch_mangle_tx;
	dev->vlan_rx_register = mvswitch_vlan_rx_register;
	dev->features |= NETIF_F_HW_VLAN_RX;

	return 0;
}

static int
mvswitch_read_status(struct phy_device *phydev)
{
	phydev->speed = SPEED_100;
	phydev->duplex = DUPLEX_FULL;
	phydev->state = PHY_UP;
	return 0;
}

static int
mvswitch_config_aneg(struct phy_device *phydev)
{
	return 0;
}

static void
mvswitch_remove(struct phy_device *pdev)
{
	struct mvswitch_priv *priv = to_mvsw(pdev);
	struct net_device *dev = pdev->attached_dev;

	/* restore old xmit handler */
	if (priv->hardstart && dev)
		dev->hard_start_xmit = priv->hardstart;
	dev->vlan_rx_register = NULL;
	dev->vlan_rx_kill_vid = NULL;
	dev->phy_ptr = NULL;
	dev->features &= ~NETIF_F_HW_VLAN_RX;
	kfree(priv);
}

static bool
mvswitch_detect(struct mii_bus *bus, int addr)
{
	u16 reg;
	int i;

	/* we attach to phy id 31 to make sure that the late probe works */
	if (addr != 31)
		return false;

	/* look for the switch on the bus */
	reg = bus->read(bus, MV_PORTREG(IDENT, 0)) & MV_IDENT_MASK;
	if (reg != MV_IDENT_VALUE)
		return false;

	/* 
	 * Now that we've established that the switch actually exists, let's 
	 * get rid of the competition :)
	 */
	for (i = 0; i < 31; i++) {
		if (!bus->phy_map[i])
			continue;

		device_unregister(&bus->phy_map[i]->dev);
		kfree(bus->phy_map[i]);
		bus->phy_map[i] = NULL;
	}

	return true;
}

static int
mvswitch_probe(struct phy_device *pdev)
{
	struct mvswitch_priv *priv;

	priv = kzalloc(sizeof(struct mvswitch_priv), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;

	pdev->priv = priv;

	return 0;
}


static struct phy_driver mvswitch_driver = {
	.name		= "Marvell 88E6060",
	.features	= PHY_BASIC_FEATURES,
	.detect		= &mvswitch_detect,
	.probe		= &mvswitch_probe,
	.remove		= &mvswitch_remove,
	.config_init	= &mvswitch_config_init,
	.config_aneg	= &mvswitch_config_aneg,
	.read_status	= &mvswitch_read_status,
	.driver		= { .owner = THIS_MODULE,},
};

static int __init
mvswitch_init(void)
{
	return phy_driver_register(&mvswitch_driver);
}

static void __exit
mvswitch_exit(void)
{
	phy_driver_unregister(&mvswitch_driver);
}

module_init(mvswitch_init);
module_exit(mvswitch_exit);