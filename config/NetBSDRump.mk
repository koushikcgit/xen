include $(XEN_ROOT)/config/StdGNU.mk

DLOPEN_LIBS =
PTHREAD_LIBS =

XEN_LOCK_DIR = /var/lib

WGET = ftp

nosharedlibs=y
libextension=.a
XENSTORE_STATIC_CLIENTS=y

XENSTORE_XENSTORED=n
