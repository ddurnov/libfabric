dnl Configury specific to the libfabric SHM provider

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

	# Save CPPFLAGS and LDFLAGS because if SHM is found, we need
	# to do additional tests which require resetting them (and
	# then we want to set them back to their original values when
	# done).
	shm_CPPFLAGS_SAVE=$CPPFLAGS
	shm_LDFLAGS_SAVE=$LDFLAGS

	AS_IF([test x"$enable_shm" != x"no"],
	      [FI_CHECK_PACKAGE([shm],
				[numa.h],
				[numa],
				[numa_available],
				[],
				[$shm_PREFIX],
				[$shm_LIBDIR],
				[shm_happy=1],
				[shm_happy=0])
	       AS_IF([test $shm_happy -eq 1],
		     [LDFLAGS="$LDFLAGS $shm_LDFLAGS"
		      CPPFLAGS="$CPPFLAGS $shm_CPPFLAGS"])
	      ])

	AS_IF([test $shm_happy -eq 1], [$1], [$2])

	# Reset CPPFLAGS and LDFLAGS back to their original values and
	# tidy up
	CPPFLAGS=$shm_CPPFLAGS_SAVE
	unset shm_CPPFLAGS_SAVE
	LDFLAGS=$shm_LDFLAGS_SAVE
	unset shm_LDFLAGS_SAVE
])
