# configure.ac - autoconf script for the AArch64 bootwrapper
#
# Copyright (c) 2014 ARM Limited. All rights reserved.
#
# Use of this source code is governed by a BSD-style license that can
# be found in the LICENSE.txt file.

AC_INIT([boot-wrapper], [v0.2])

AM_INIT_AUTOMAKE([foreign])

# Allow a user to pass --with-bw-arch={aarch64-a,aarch32-a,aarch64-r}
AC_ARG_WITH([bw-arch],
	AS_HELP_STRING([--with-bw-arch], [specify boot-wrapper architecture: aarch64-a (default), aarch32-a or aarch64-r]),
	[case "${withval}" in
		no|yes|aarch64-a) USE_ARCH=aarch64-a ;;
		aarch32-a) USE_ARCH=aarch32-a ;;
		aarch64-r) USE_ARCH=aarch64-r ;;
		*) AC_MSG_ERROR([Bad value "${withval}" for --with-bw-arch. Use "aarch64-a", "aarch32-a" or "aarch64-r"]) ;;
	esac])
AM_CONDITIONAL([BOOTWRAPPER_64R], [test "x$USE_ARCH" = "xaarch64-r"])

AS_IF([test "x$USE_ARCH" = "xaarch32-a"],
	[BOOTWRAPPER_ES=32], [BOOTWRAPPER_ES=64]
)
AM_CONDITIONAL([BOOTWRAPPER_32], [test "x$BOOTWRAPPER_ES" = "x32"])

AC_ARG_ENABLE([aarch32-kernel],
	AC_HELP_STRING([--enable-aarch32-kernel],
		       [start kernel in AArch32 instead of AArch64]),
	[KERNEL_ES=32 USE_PSCI=yes AC_SUBST([KERNEL_32], [1]) AC_DEFINE([KERNEL_32])],
	[KERNEL_ES=64])
AM_CONDITIONAL([KERNEL_32], [test "x$KERNEL_ES" = x32])

AS_IF([test "x$BOOTWRAPPER_ES" = x32 -a "x$KERNEL_ES" != x32],
	[AC_MSG_ERROR([a 32-bit boot-wrapper cannot launch a 64-bit kernel])]
)

# Allow a user to pass --with-kernel-dir
AC_ARG_WITH([kernel-dir],
	AS_HELP_STRING([--with-kernel-dir], [specify the root Linux kernel build directory]),
	AC_SUBST([KERN_DIR], [$withval]),
	AC_MSG_NOTICE([Kernel dir not specified])
)

# Allow a user to pass a specific kernel image file
AC_ARG_WITH([kernel-image],
	AS_HELP_STRING([--with-kernel-image], [specify kernel image]),
	AC_SUBST([KERN_IMAGE], [$withval]),
	AS_IF([test "x$KERN_DIR" != "x"],
		AS_IF([test "x$KERNEL_ES" = x32],
			[KERN_IMAGE=$KERN_DIR/arch/arm/boot/zImage],
			[KERN_IMAGE=$KERN_DIR/arch/arm64/boot/Image]
		),
		AC_MSG_ERROR([No kernel image specified. Use --with-kernel-image or --with-kernel-dir])
	)
)

# Allow the user to override the default DTB
AC_ARG_WITH([dtb],
	AS_HELP_STRING([--with-dtb], [Specify a particular DTB to use]),
	[KERN_DTB="$withval"],
	AS_IF([test "x$KERN_DIR" != "x"],
		[KERN_DTB=$KERN_DIR/arch/arm64/boot/dts/arm/fvp-base-revc.dtb],
		AC_MSG_ERROR([No DTB specified. Use --with-dtb or --with-kernel-dir])
	)
)

AC_ARG_WITH([xen],
	AS_HELP_STRING([--with-xen], [Compile for Xen, and specify a particular Xen to use]),
	X_IMAGE=$withval)

AS_IF([test "x$X_IMAGE" == "x"], [],
      [AS_IF([test ! -f "$X_IMAGE"],
	     [AC_MSG_ERROR([Could not find Xen hypervisor binary: $X_IMAGE])]
      )]
)
AC_SUBST([XEN_IMAGE], [$X_IMAGE])
AM_CONDITIONAL([XEN], [test "x$X_IMAGE" != "x"])

AC_MSG_CHECKING([whether DTB file exists])
if ! test -f $KERN_DTB; then
	AC_MSG_RESULT([no])
	AC_MSG_ERROR([Could not find DTB file: $KERN_DTB])
else
	AC_MSG_RESULT([yes])
fi

AC_MSG_CHECKING([whether kernel image exists])
if ! test -f $KERN_IMAGE; then
	AC_MSG_RESULT([no])
	AC_MSG_ERROR([Could not find kernel image: $KERN_IMAGE])
else
	AC_MSG_RESULT([yes])
fi

AC_SUBST([KERNEL_IMAGE], [$KERN_IMAGE])
AC_SUBST([KERNEL_DTB], [$KERN_DTB])
AS_IF([test "x$X_IMAGE" != "x"],
      [AC_SUBST([IMAGE], ["xen-system.axf"])],
      [AC_SUBST([IMAGE], ["linux-system.axf"])]
)

# Allow a user to pass --enable-psci
AC_ARG_ENABLE([psci],
	AS_HELP_STRING([--disable-psci], [disable the psci boot method]),
	[USE_PSCI=$enableval], [USE_PSCI="yes"])
AM_CONDITIONAL([PSCI], [test "x$USE_PSCI" = "xyes"])
AS_IF([test "x$USE_PSCI" = "xyes"], [], [USE_PSCI=no])
AS_IF([test "x$USE_ARCH" = "xaarch64-r"],
	AC_SUBST([PSCI_METHOD], [hvc]), AC_SUBST([PSCI_METHOD], [smc])
)

AS_IF([test "x$USE_PSCI" != "xyes" -a "x$KERNEL_ES" = "x32"],
	[AC_MSG_ERROR([With an AArch32 kernel, boot method must be PSCI.])]
)

AS_IF([test "x$USE_PSCI" = "xyes" -a "x$USE_ARCH" = "xaarch64-r" -a "x$X_IMAGE" != "x"],
	[AC_MSG_ERROR([With an AArch64-R Xen, boot method must be spin-table.])]
)

# Allow a user to pass --with-initrd
AC_ARG_WITH([initrd],
	AS_HELP_STRING([--with-initrd], [embed an initrd in the kernel image]),
	USE_INITRD=$withval)
AC_SUBST([FILESYSTEM], [$USE_INITRD])
AM_CONDITIONAL([INITRD], [test "x$USE_INITRD" != "x"])

AS_IF([test "x$X_IMAGE" = "x"],[C_CONSOLE="ttyAMA0"],[C_CONSOLE="hvc0"])
C_CMDLINE="console=$C_CONSOLE earlycon=pl011,0x1c090000"
AC_ARG_WITH([cmdline],
	AS_HELP_STRING([--with-cmdline], [set a command line for the kernel]),
	[C_CMDLINE=$withval])
AC_SUBST([CMDLINE], [$C_CMDLINE])

X_CMDLINE="console=dtuart dtuart=serial0 no-bootscrub"
AC_ARG_WITH([xen-cmdline],
	AS_HELP_STRING([--with-xen-cmdline], [set Xen command line]),
	[X_CMDLINE=$withval])
AC_SUBST([XEN_CMDLINE], [$X_CMDLINE])

# Allow a user to pass --enable-gicv3
AC_ARG_ENABLE([gicv3],
	AS_HELP_STRING([--enable-gicv3], [enable GICv3 instead of GICv2]),
	[USE_GICV3=$enableval])
AM_CONDITIONAL([GICV3], [test "x$USE_GICV3" = "xyes"])
AS_IF([test "x$USE_GICV3" = "xyes"], [], [USE_GICV3=no])

# Ensure that we have all the needed programs
AC_PROG_CC
AC_PROG_CPP
AM_PROG_AS
AC_PROG_SED
AC_PROG_LN_S
AC_PATH_PROG([DTC], dtc, error, [$PATH$PATH_SEPARATOR$KERN_DIR/scripts/dtc])
if test "x$DTC" = "xerror"; then
	AC_MSG_ERROR([cannot find the device tree compiler (dtc). Use --with-kernel-dir or put dtc on the PATH])
fi
AC_CHECK_TOOL(LD, ld)

AC_CONFIG_FILES([Makefile])

AC_OUTPUT

# Print the final config to the user.
echo ""
echo "  Boot wrapper configuration"
echo "  =========================="
echo ""
echo "  Linux kernel build dir:            ${KERN_DIR:-NONE}"
echo "  Linux kernel image:                ${KERN_IMAGE}"
echo "  Device tree blob:                  ${KERN_DTB}"
echo "  Device tree compiler:              ${DTC}"
echo "  Linux kernel command line:         ${CMDLINE}"
echo "  Embedded initrd:                   ${FILESYSTEM:-NONE}"
echo "  Use PSCI?                          ${USE_PSCI}"
echo "  Use GICv3?                         ${USE_GICV3}"
echo "  Boot-wrapper execution state:      AArch${BOOTWRAPPER_ES}"
echo "  Kernel execution state:            AArch${KERNEL_ES}"
echo "  Xen image                          ${XEN_IMAGE:-NONE}"
if test "x${XEN_IMAGE}" != "x"; then
echo "  Xen command line:                  ${XEN_CMDLINE}"
fi
echo ""
