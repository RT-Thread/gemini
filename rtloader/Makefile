COMPONENT_VBUS_HDR := $(RTT_ROOT)/components/vbus/share_hdr/
COMPONENT_VBUS_CONF_HDR := $(RTT_ROOT)/components/vbus/

ccflags-y := -I$(COMPONENT_VBUS_HDR) -I$(COMPONENT_VBUS_CONF_HDR) -I$(src) -I$(src)/vbus

obj-m += rtloader.o

VBUS_DIR := vbus
include $(src)/$(VBUS_DIR)/Makefile.in

rtloader-objs := loader.o $(VBUS_OBJS)
