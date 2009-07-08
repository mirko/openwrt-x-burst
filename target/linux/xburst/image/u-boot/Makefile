#
# Copyright (C) 2009 Qi hardware inc.,
#
# This is free software, licensed under the GNU General Public License v3.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=u-boot
PKG_VERSION:=2009.06
PKG_RELEASE:=1

PKG_BUILD_DIR:=$(KDIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.bz2
PKG_SOURCE_URL:=ftp://ftp.denx.de/pub/u-boot
PKG_MD5SUM:=
PKG_CAT:=bzcat

include $(INCLUDE_DIR)/package.mk

UBOOT_CONFIG=$(strip $(subst ",, $(CONFIG_XBURST_UBOOT_TARGET)))

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR) $(UBOOT_CONFIG)_config
	$(MAKE) -C $(PKG_BUILD_DIR) CROSS_COMPILE=$(TARGET_CROSS)
endef

define Build/InstallDev
	mkdir -p $(BIN_DIR)
	dd if=$(PKG_BUILD_DIR)/u-boot-nand.bin of=$(BIN_DIR)/u-boot-$(UBOOT_CONFIG).bin bs=64k conv=sync
endef

$(eval $(call Build/DefaultTargets))
