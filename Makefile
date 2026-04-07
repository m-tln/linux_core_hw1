obj-m += telegram_mod.o

all: module daemon

module:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

daemon:
	gcc -Wall -o telegram_daemon telegram_daemon.c

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f telegram_daemon

install: all
	sudo insmod telegram_mod.ko
	sudo ./telegram_daemon &

remove:
	sudo pkill telegram_daemon || true
	sudo rmmod telegram_mod
