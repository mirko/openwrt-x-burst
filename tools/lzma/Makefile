# 
# Copyright (C) 2006 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#
include $(TOPDIR)/rules.mk

PKG_NAME:=lzma
PKG_VERSION:=4.65

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.bz2
PKG_SOURCE_URL:=http://downloads.openwrt.org/sources/
PKG_MD5SUM:=434e51a018b4c8ef377bf81520a53af0

HOST_BUILD_DIR:=$(BUILD_DIR_HOST)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/host-build.mk

UTIL_DIR=$(HOST_BUILD_DIR)/C/LzmaUtil
ALONE_DIR=$(HOST_BUILD_DIR)/CPP/7zip/Compress/LZMA_Alone

define Host/Compile
	$(MAKE) -C $(UTIL_DIR) -f makefile.gcc
	$(MAKE) -C $(ALONE_DIR) -f makefile.gcc
endef

define Host/Install
	$(INSTALL_DIR) $(STAGING_DIR_HOST)/lib $(STAGING_DIR_HOST)/include $(STAGING_DIR_HOST)/bin
	$(CP) $(HOST_BUILD_DIR)/C/*.h $(STAGING_DIR_HOST)/include/
	$(CP) $(UTIL_DIR)/liblzma.a $(STAGING_DIR_HOST)/lib/
endef

define Host/Clean
endef

$(eval $(call HostBuild))
