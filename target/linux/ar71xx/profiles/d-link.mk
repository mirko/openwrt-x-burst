#
# Copyright (C) 2009 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

define Profile/DIR615C1
	NAME:=D-Link DIR-615 rev. C1
	PACKAGES:=kmod-ath9k hostapd-mini
endef

define Profile/DIR615C1/Description
	Package set optimized for the D-Link DIR-615 rev. C1.
endef

$(eval $(call Profile,DIR615C1))
