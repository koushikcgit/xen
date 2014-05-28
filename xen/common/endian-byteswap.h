/*
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
#ifndef __ENDIAN_BYTESWAP_H__
#define __ENDIAN_BYTESWAP_H__

#ifdef __XEN__

#include <endian.h>

#include <asm/byteorder.h>
#define bswap_16(x) swab16(x)
#define bswap_32(x) swab32(x)
#define bswap_64(x) swab64(x)

#else /* !__XEN__ */

#ifdef __sun__
#include <sys/byteorder.h>
#define bswap_16(x) BSWAP_16(x)
#define bswap_32(x) BSWAP_32(x)
#define bswap_64(x) BSWAP_64(x)
#elif defined(__NetBSD__)
#include <sys/endian.h>
#include <sys/bswap.h>
#define bswap_16(x) bswap16(x)
#define bswap_32(x) bswap32(x)
#define bswap_64(x) bswap64(x)
#elif defined(__OpenBSD__)
#include <machine/endian.h>
#define bswap_16(x) swap16(x)
#define bswap_32(x) swap32(x)
#define bswap_64(x) swap64(x)
#elif defined(__linux__) || defined(__Linux__) || defined(__MINIOS__)
#include <endian.h>
#include <byteswap.h>
#else
#error Unsupported OS
#endif

#endif

#endif /* __ENDIAN_BYTESWAP_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
