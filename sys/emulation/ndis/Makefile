# $FreeBSD: src/sys/modules/ndis/Makefile,v 1.14 2010/08/23 06:13:29 imp Exp $

KMOD=	ndis
SRCS=	subr_pe.c subr_ndis.c subr_hal.c subr_ntoskrnl.c kern_ndis.c
SRCS+=	kern_windrv.c
SRCS+=	device_if.h bus_if.h pci_if.h
SRCS+=	opt_usb.h use_usb4bsd.h

.if ${MACHINE_ARCH} == "x86_64"
SRCS+=	winx64_wrap.S
.endif

.if ${MACHINE_ARCH} == "i386"
SRCS+=	winx32_wrap.S
.endif

.if defined(WANT_USB4BSD)
SRCS+=	subr_u4bd.c
.else
SRCS+=	subr_usbd.c
.endif

.if !defined(BUILDING_WITH_KERNEL) && defined(WANT_USB4BSD)
use_usb4bsd.h:
	echo "#define NUSB4BSD 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
