obj-m := gpio_irq.o
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
 
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
 
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	
default:
        $(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules