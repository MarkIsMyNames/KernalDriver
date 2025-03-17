obj-m += simple_module.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	sudo cp simple_module.ko /lib/modules/6.13.6-200.fc41.x86_64/kernel/drivers/usb/misc/
	sudo depmod

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	sudo rmmod simple_module
