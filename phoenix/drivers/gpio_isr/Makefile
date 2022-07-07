obj-m += gpio_isr.o

all: modules

modules:
	$(MAKE) -C ../../../linux/ M=$(shell pwd) ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE) INSTALL_MOD_PATH=output/ modules

install:
	cp -a ./gpio_isr.ko ../../bin/

uninstall:
	rm -fr ../../bin/gpio_isr.ko

clean:
	rm -fr Module.symvers ; rm -fr Module.markers ; rm -fr modules.order
	rm -fr *.mod.c *.mod *.o .*.cmd *.ko *~
	rm -fr .tmp_versions
