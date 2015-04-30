obj-m += xusb.o
obj-m += xbox360.o
obj-m += xbox360wr.o

ccflags-y   += -DDEBUG

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

install:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules_install

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
