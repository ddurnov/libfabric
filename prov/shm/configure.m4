dnl Configury specific to the libfabrics SHM provider

dnl Called to configure this provider
dnl
dnl Arguments:
dnl
dnl $1: action if configured successfully
dnl $2: action if not configured successfully
dnl
AC_DEFUN([FI_SHM_CONFIGURE],[
	# Determine if we can support the shm provider
	shm_happy=0
	AS_IF([test x"$enable_shm" != x"no"],
	      [shm_happy=1
	       AC_CHECK_HEADER([numa.h], [], [shm_happy=0])
	       AC_CHECK_LIB([numa], [numa_available], [], [shm_happy=0])])

	AS_IF([test $shm_happy -eq 1], [$1], [$2])
])
