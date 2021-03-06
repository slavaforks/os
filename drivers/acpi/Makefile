################################################################################
#
#   Copyright (c) 2012 Minoca Corp. All Rights Reserved
#
#   Module Name:
#
#       ACPI
#
#   Abstract:
#
#       This module implements the Advanced Configuration and Power Interface
#       (ACPI) driver, which uses platform firmware to enumerate devices,
#       manage hardware, and implement system power transitions. It is
#       supported even on systems that do not provide ACPI-specific hardware
#       (embedded controllers).
#
#   Author:
#
#       Evan Green 29-Nov-2012
#
#   Environment:
#
#       Kernel
#
################################################################################

BINARY = acpi.drv

BINARYTYPE = so

BINPLACE = bin

OBJS = acpidrv.o    \
       aml.o        \
       amlopcr.o    \
       amlopex.o    \
       amloptab.o   \
       amlos.o      \
       drvsup.o     \
       earlypci.o   \
       fixedreg.o   \
       namespce.o   \
       oprgn.o      \
       oprgnos.o    \
       proc.o       \
       resdesc.o    \

ARMV7_OBJS = armv7/procarch.o   \

ARMV6_OBJS = $(ARMV7_OBJS)

X86_OBJS = x86/procarch.o   \

EXTRA_SRC_DIRS = x86 armv7

DYNLIBS = $(BINROOT)/kernel             \

include $(SRCROOT)/os/minoca.mk

