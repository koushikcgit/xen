/******************************************************************************
 *
 * Copyright 2007-2008 Samuel Thibault <samuel.thibault@eu.citrix.com>.
 * All rights reserved.
 * Use is subject to license terms.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#undef NDEBUG
#include "xen-external/bsd-sys-queue.h"
#include <mini-os/types.h>
#include <mini-os/os.h>
#include <mini-os/mm.h>
#include <mini-os/lib.h>
#include <mini-os/gntmap.h>
#include <mini-os/events.h>
#include <mini-os/wait.h>
#include <sys/mman.h>

#include <xen/memory.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <inttypes.h>
#include <malloc.h>

#include "xc_private.h"

void minios_evtchn_close_fd(int fd);

extern struct wait_queue_head event_queue;

/* XXX Note: This is not threadsafe */
static struct evtchn_port_info* port_alloc(int fd) {
    struct evtchn_port_info *port_info;
    port_info = malloc(sizeof(struct evtchn_port_info));
    if (port_info == NULL)
        return NULL;
    port_info->pending = 0;
    port_info->port = -1;
    port_info->bound = 0;

    LIST_INSERT_HEAD(&files[fd].evtchn.ports, port_info, list);
    return port_info;
}

static void port_dealloc(struct evtchn_port_info *port_info) {
    if (port_info->bound)
        unbind_evtchn(port_info->port);
    LIST_REMOVE(port_info, list);
    free(port_info);
}

static xc_osdep_handle minios_evtchn_open(xc_evtchn *xce)
{
    int fd = alloc_fd(FTYPE_EVTCHN);
    if ( fd == -1 )
        return XC_OSDEP_OPEN_ERROR;
    LIST_INIT(&files[fd].evtchn.ports);
    printf("evtchn_open() -> %d\n", fd);
    return (xc_osdep_handle)fd;
}

static int minios_evtchn_close(xc_evtchn *xce, xc_osdep_handle h)
{
    int fd = (int)h;
    return close(fd);
}

void minios_evtchn_close_fd(int fd)
{
    struct evtchn_port_info *port_info, *tmp;
    LIST_FOREACH_SAFE(port_info, &files[fd].evtchn.ports, list, tmp)
        port_dealloc(port_info);

    files[fd].type = FTYPE_NONE;
}

static int minios_evtchn_fd(xc_evtchn *xce, xc_osdep_handle h)
{
    return (int)h;
}

static int minios_evtchn_notify(xc_evtchn *xce, xc_osdep_handle h, evtchn_port_t port)
{
    int ret;

    ret = notify_remote_via_evtchn(port);

    if (ret < 0) {
	errno = -ret;
	ret = -1;
    }
    return ret;
}

static void evtchn_handler(evtchn_port_t port, struct pt_regs *regs, void *data)
{
    int fd = (int)(intptr_t)data;
    struct evtchn_port_info *port_info;
    assert(files[fd].type == FTYPE_EVTCHN);
    mask_evtchn(port);
    LIST_FOREACH(port_info, &files[fd].evtchn.ports, list) {
        if (port_info->port == port)
            goto found;
    }
    printk("Unknown port for handle %d\n", fd);
    return;

 found:
    port_info->pending = 1;
    files[fd].read = 1;
    wake_up(&event_queue);
}

static evtchn_port_or_error_t minios_evtchn_bind_unbound_port(xc_evtchn *xce, xc_osdep_handle h, int domid)
{
    int fd = (int)h;
    struct evtchn_port_info *port_info;
    int ret;
    evtchn_port_t port;

    assert(get_current() == main_thread);
    port_info = port_alloc(fd);
    if (port_info == NULL)
	return -1;

    printf("xc_evtchn_bind_unbound_port(%d)", domid);
    ret = evtchn_alloc_unbound(domid, evtchn_handler, (void*)(intptr_t)fd, &port);
    printf(" = %d\n", ret);

    if (ret < 0) {
	port_dealloc(port_info);
	errno = -ret;
	return -1;
    }
    port_info->bound = 1;
    port_info->port = port;
    unmask_evtchn(port);
    return port;
}

static evtchn_port_or_error_t minios_evtchn_bind_interdomain(xc_evtchn *xce, xc_osdep_handle h, int domid,
    evtchn_port_t remote_port)
{
    int fd = (int)h;
    struct evtchn_port_info *port_info;
    evtchn_port_t local_port;
    int ret;

    assert(get_current() == main_thread);
    port_info = port_alloc(fd);
    if (port_info == NULL)
	return -1;

    printf("xc_evtchn_bind_interdomain(%d, %"PRId32")", domid, remote_port);
    ret = evtchn_bind_interdomain(domid, remote_port, evtchn_handler, (void*)(intptr_t)fd, &local_port);
    printf(" = %d\n", ret);

    if (ret < 0) {
	port_dealloc(port_info);
	errno = -ret;
	return -1;
    }
    port_info->bound = 1;
    port_info->port = local_port;
    unmask_evtchn(local_port);
    return local_port;
}

static int minios_evtchn_unbind(xc_evtchn *xce, xc_osdep_handle h, evtchn_port_t port)
{
    int fd = (int)h;
    struct evtchn_port_info *port_info;

    LIST_FOREACH(port_info, &files[fd].evtchn.ports, list) {
        if (port_info->port == port) {
            port_dealloc(port_info);
            return 0;
        }
    }
    printf("Warning: couldn't find port %"PRId32" for xc handle %x\n", port, fd);
    errno = EINVAL;
    return -1;
}

static evtchn_port_or_error_t minios_evtchn_bind_virq(xc_evtchn *xce, xc_osdep_handle h, unsigned int virq)
{
    int fd = (int)h;
    struct evtchn_port_info *port_info;
    evtchn_port_t port;

    assert(get_current() == main_thread);
    port_info = port_alloc(fd);
    if (port_info == NULL)
	return -1;

    printf("xc_evtchn_bind_virq(%d)", virq);
    port = bind_virq(virq, evtchn_handler, (void*)(intptr_t)fd);

    if (port < 0) {
	port_dealloc(port_info);
	errno = -port;
	return -1;
    }
    port_info->bound = 1;
    port_info->port = port;
    unmask_evtchn(port);
    return port;
}

static evtchn_port_or_error_t minios_evtchn_pending(xc_evtchn *xce, xc_osdep_handle h)
{
    int fd = (int)h;
    struct evtchn_port_info *port_info;
    unsigned long flags;
    evtchn_port_t ret = -1;

    local_irq_save(flags);
    files[fd].read = 0;

    LIST_FOREACH(port_info, &files[fd].evtchn.ports, list) {
        if (port_info->port != -1 && port_info->pending) {
            if (ret == -1) {
                ret = port_info->port;
                port_info->pending = 0;
            } else {
                files[fd].read = 1;
                break;
            }
        }
    }
    local_irq_restore(flags);
    return ret;
}

static int minios_evtchn_unmask(xc_evtchn *xce, xc_osdep_handle h, evtchn_port_t port)
{
    unmask_evtchn(port);
    return 0;
}

struct xc_osdep_ops xc_evtchn_ops = {
    .open = &minios_evtchn_open,
    .close = &minios_evtchn_close,

    .u.evtchn = {
        .fd = &minios_evtchn_fd,
        .notify = &minios_evtchn_notify,
        .bind_unbound_port = &minios_evtchn_bind_unbound_port,
        .bind_interdomain = &minios_evtchn_bind_interdomain,
        .bind_virq = &minios_evtchn_bind_virq,
        .unbind = &minios_evtchn_unbind,
        .pending = &minios_evtchn_pending,
        .unmask = &minios_evtchn_unmask,
   },
};

/* Optionally flush file to disk and discard page cache */
void discard_file_cache(xc_interface *xch, int fd, int flush)
{
    if (flush)
        fsync(fd);
}

void *xc_memalign(xc_interface *xch, size_t alignment, size_t size)
{
    return memalign(alignment, size);
}

static struct xc_osdep_ops *minios_osdep_init(xc_interface *xch, enum xc_osdep_type type)
{
    switch ( type )
    {
    case XC_OSDEP_PRIVCMD:
        return &xc_privcmd_ops;
    case XC_OSDEP_EVTCHN:
        return &xc_evtchn_ops;
    case XC_OSDEP_GNTTAB:
        return &xc_gnttab_ops;
    default:
        return NULL;
    }
}

xc_osdep_info_t xc_osdep_info = {
    .name = "Minios Native OS interface",
    .init = &minios_osdep_init,
    .fake = 0,
};

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
