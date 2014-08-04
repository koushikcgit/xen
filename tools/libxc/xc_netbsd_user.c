/******************************************************************************
 *
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * xc_gnttab functions:
 * Copyright (c) 2007-2008, D G Murray <Derek.Murray@cl.cam.ac.uk>
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

#include "xc_private.h"

#include <xen/sys/evtchn.h>
#include <unistd.h>
#include <fcntl.h>
#include <malloc.h>
#include <sys/mman.h>

#define EVTCHN_DEV_NAME  "/dev/xenevt"

static xc_osdep_handle netbsd_evtchn_open(xc_evtchn *xce)
{
    int fd = open(EVTCHN_DEV_NAME, O_NONBLOCK|O_RDWR);
    if ( fd == -1 )
        return XC_OSDEP_OPEN_ERROR;

    return (xc_osdep_handle)fd;
}

static int netbsd_evtchn_close(xc_evtchn *xce, xc_osdep_handle h)
{
    int fd = (int)h;
    return close(fd);
}

static int netbsd_evtchn_fd(xc_evtchn *xce, xc_osdep_handle h)
{
    return (int)h;
}

static int netbsd_evtchn_notify(xc_evtchn *xce, xc_osdep_handle h, evtchn_port_t port)
{
    int fd = (int)h;
    struct ioctl_evtchn_notify notify;

    notify.port = port;

    return ioctl(fd, IOCTL_EVTCHN_NOTIFY, &notify);
}

static evtchn_port_or_error_t
netbsd_evtchn_bind_unbound_port(xc_evtchn * xce, xc_osdep_handle h, int domid)
{
    int fd = (int)h;
    struct ioctl_evtchn_bind_unbound_port bind;
    int ret;

    bind.remote_domain = domid;

    ret = ioctl(fd, IOCTL_EVTCHN_BIND_UNBOUND_PORT, &bind);
    if (ret == 0)
	return bind.port;
    else
	return -1;
}

static evtchn_port_or_error_t
netbsd_evtchn_bind_interdomain(xc_evtchn *xce, xc_osdep_handle h, int domid,
                               evtchn_port_t remote_port)
{
    int fd = (int)h;
    struct ioctl_evtchn_bind_interdomain bind;
    int ret;

    bind.remote_domain = domid;
    bind.remote_port = remote_port;

    ret = ioctl(fd, IOCTL_EVTCHN_BIND_INTERDOMAIN, &bind);
    if (ret == 0)
	return bind.port;
    else
	return -1;
}

static int netbsd_evtchn_unbind(xc_evtchn *xce, xc_osdep_handle h, evtchn_port_t port)
{
    int fd = (int)h;
    struct ioctl_evtchn_unbind unbind;

    unbind.port = port;

    return ioctl(fd, IOCTL_EVTCHN_UNBIND, &unbind);
}

static evtchn_port_or_error_t
netbsd_evtchn_bind_virq(xc_evtchn *xce, xc_osdep_handle h, unsigned int virq)
{
    int fd = (int)h;
    struct ioctl_evtchn_bind_virq bind;
    int err;

    bind.virq = virq;

    err = ioctl(fd, IOCTL_EVTCHN_BIND_VIRQ, &bind);
    if (err)
	return -1;
    else
	return bind.port;
}

static evtchn_port_or_error_t
netbsd_evtchn_pending(xc_evtchn *xce, xc_osdep_handle h)
{
    int fd = (int)h;
    evtchn_port_t port;

    if ( read_exact(fd, (char *)&port, sizeof(port)) == -1 )
        return -1;

    return port;
}

static int netbsd_evtchn_unmask(xc_evtchn *xce, xc_osdep_handle h, evtchn_port_t port)
{
    int fd = (int)h;
    return write_exact(fd, (char *)&port, sizeof(port));
}

struct xc_osdep_ops xc_evtchn_ops = {
    .open = &netbsd_evtchn_open,
    .close = &netbsd_evtchn_close,

    .u.evtchn = {
         .fd = &netbsd_evtchn_fd,
         .notify = &netbsd_evtchn_notify,
         .bind_unbound_port = &netbsd_evtchn_bind_unbound_port,
         .bind_interdomain = &netbsd_evtchn_bind_interdomain,
         .bind_virq = &netbsd_evtchn_bind_virq,
         .unbind = &netbsd_evtchn_unbind,
         .pending = &netbsd_evtchn_pending,
         .unmask = &netbsd_evtchn_unmask,
    },
};

/* Optionally flush file to disk and discard page cache */
void discard_file_cache(xc_interface *xch, int fd, int flush) 
{
    off_t cur = 0;
    int saved_errno = errno;

    if ( flush && (fsync(fd) < 0) )
    {
        /*PERROR("Failed to flush file: %s", strerror(errno));*/
        goto out;
    }

    /*
     * Calculate last page boundry of amount written so far
     * unless we are flushing in which case entire cache
     * is discarded.
     */
    if ( !flush )
    {
        if ( ( cur = lseek(fd, 0, SEEK_CUR)) == (off_t)-1 )
            cur = 0;
        cur &= ~(PAGE_SIZE - 1);
    }

    /* Discard from the buffer cache. */
    if ( posix_fadvise(fd, 0, cur, POSIX_FADV_DONTNEED) < 0 )
    {
        /*PERROR("Failed to discard cache: %s", strerror(errno));*/
        goto out;
    }

 out:
    errno = saved_errno;
}

void *xc_memalign(xc_interface *xch, size_t alignment, size_t size)
{
    return valloc(size);
}
