#pragma once

#ifdef __cplusplus
#  ifndef EXTERN_C_BEGIN
#    define EXTERN_C_BEGIN extern "C" {
#  endif
#  ifndef EXTERN_C_END
#    define EXTERN_C_END   }
#  endif
#  ifndef EXTERN_C
#    define EXTERN_C extern "C"
#  endif
#  ifndef __BEGIN_DECLS
#    define __BEGIN_DECLS extern "C" {
#  endif
#  ifndef __END_DECLS
#    define __END_DECLS }
#  endif
#else
#  ifndef EXTERN_C_BEGIN
#    define EXTERN_C_BEGIN
#  endif
#  ifndef EXTERN_C_END
#    define EXTERN_C_END
#  endif
#  ifndef EXTERN_C
#    define EXTERN_C
#  endif
#  ifndef __BEGIN_DECLS
#    define __BEGIN_DECLS
#  endif
#  ifndef __END_DECLS
#    define __END_DECLS
#  endif
#endif

#ifndef __cplusplus
#  ifdef __GNUC_STDC_INLINE__
#    define L4_INLINE static inline
#  else
#    define L4_INLINE extern inline
#  endif
#else
#define L4_INLINE inline
#endif

#define L4_stringify_helper(x) #x                       ///< stringify helper. \hideinitializer
#define L4_stringify(x)        L4_stringify_helper(x)   ///< stringify. \hideinitializer


/**
 * \brief Handcoded version of __attribute__((constructor(xx))).
 * \param func function declaration (prototype)
 * \param prio the prio must be 65535 - \a gcc_prio
 */
#ifdef __ARM_EABI__
#  define L4_DECLARE_CONSTRUCTOR(func, prio) \
    static void (* func ## _ctor__)(void) __attribute__((used,section(".init_array." L4_stringify(prio)))) = &func;
#else
#  define L4_DECLARE_CONSTRUCTOR(func, prio) \
    static void (* func ## _ctor__)(void) __attribute__((used,section(".ctors." L4_stringify(prio)))) = &func;
#endif


