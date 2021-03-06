#
#           DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
#                   Version 2, December 2004
#
# Copyright (C) 2020 SBKarr <sbkarr@stappler.org>
#
# Everyone is permitted to copy and distribute verbatim or modified
# copies of this license document, and changing it is allowed as long
# as the name is changed.
#
#           DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
#  TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION
#
# 0. You just DO WHAT THE FUCK YOU WANT TO.

STAPPLER_ROOT ?= ../stappler
CONFIG_INCLUDE ?= config

CONF ?= local
CONF_DIR:= $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

LOCAL_OUTDIR := lib
LOCAL_LIBRARY := Trubach

LOCAL_TOOLKIT := serenity
LOCAL_INSTALL_DIR := $(LOCAL_OUTDIR)

LOCAL_ROOT = .

LOCAL_SRCS_DIRS :=  src users
LOCAL_SRCS_OBJS :=

LOCAL_INCLUDES_DIRS := src users
LOCAL_INCLUDES_OBJS := $(CONFIG_INCLUDE)

LOCAL_CFLAGS = 

LOCAL_FORCE_INSTALL := 1
LOCAL_OPTIMIZATION := -g -O2

$(LOCAL_OUTDIR)/httpd.conf: Makefile
	@mkdir -p $(LOCAL_OUTDIR)
	@mkdir -p $(CONF_DIR)logs
	@echo '# Autogenerated by makefile\n' > $@
	@echo 'ServerRoot "$(CONF_DIR)"' >> $@
	@echo 'LoadModule serenity_module $(abspath $(SERENITY_OUTPUT))' >> $@
	@echo 'ErrorLog "$(CONF_DIR)logs/error_log"' >> $@
	@echo 'CustomLog "$(CONF_DIR)logs/access_log" common' >> $@
	@echo 'SerenitySourceRoot "$(CONF_DIR)lib"' >> $@
	@echo 'Include $(CONF_DIR)conf/httpd-default.conf' >> $@
	@echo 'Include $(CONF_DIR)conf/serenity-$(CONF).conf' >> $@
	@echo 'Include $(CONF_DIR)conf/sites-$(CONF)/*.conf' >> $@

include $(STAPPLER_ROOT)/make/local.mk

all: $(LOCAL_OUTDIR)/httpd.conf
