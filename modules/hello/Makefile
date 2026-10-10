# Out-of-tree build against the OpenSTLinux v6.2.1 kernel build directory
# (needs .config and Module.symvers from 'make modules' there).
# usage, after sourcing the SDK environment-setup:
#   make            # default KDIR below
#   make KDIR=<kernel build dir>
obj-m := mp2_hello.o

KDIR ?= $(HOME)/st/stm32mp2/v26.06.10/developer/src/stm32mp-openstlinux-6.6-yocto-scarthgap-mpu-v26.06.10/sources/ostl-linux/linux-stm32mp-6.6.129-stm32mp-r3.1-r0/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
