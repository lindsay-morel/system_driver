# MemryX MX3 PCIe kernel module Makefile (Yocto-friendly)

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

obj-m := memx_cascade_plus_pcie.o

memx_cascade_plus_pcie-objs := \
	memx_feature.o \
	memx_xflow.o \
	memx_msix_irq.o \
	memx_cascade_pciemain.o \
	memx_fw_cmd.o \
	memx_fw_init.o \
	memx_pcie_dev_list_ctrl.o \
	memx_fs_proc.o \
	memx_fs_sys.o \
	memx_fs.o \
	memx_fw_log.o \
	memx_fs_hwmon.o \
	memx_dma.o

all: modules

modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(CURDIR) modules

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(CURDIR) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(CURDIR) clean
