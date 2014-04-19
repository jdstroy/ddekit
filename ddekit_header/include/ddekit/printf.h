/*
 * This file is part of DDEKit.
 *
 * (c) 2006-2010 Bjoern Doebel <doebel@os.inf.tu-dresden.de>
 *               Christian Helmuth <ch12@os.inf.tu-dresden.de>
 *               Thomas Friebel <tf13@os.inf.tu-dresden.de>
 *     economic rights: Technische Universitaet Dresden (Germany)
 *
 * This file is part of TUD:OS and distributed under the terms of the
 * GNU General Public License 2.
 * Please see the COPYING-GPL-2 file for details.
 */

#pragma once

#include <ddekit/compiler.h>
#include <stdarg.h>

EXTERN_C_BEGIN

/** Print message.
 * \ingroup DDEKit_util
 */
void ddekit_print(const char *);

/** Print message with format.
 * \ingroup DDEKit_util
 */
void ddekit_printf(const char *fmt, ...);

/** Print message with format list.
 * \ingroup DDEKit_util
 */
void ddekit_vprintf(const char *fmt, va_list va);


EXTERN_C_END

/** Log function and message.
 * \ingroup DDEKit_util
 */
#ifdef __cplusplus
#  define ddekit_log(doit, ...)                         \
	do {                                               \
		if (doit) {                                \
			ddekit_printf("%s(): ", __func__); \
			ddekit_printf(__VA_ARGS__);                \
			ddekit_printf("\n");               \
		}                                          \
	} while(0);
#else
#  define ddekit_log(doit, ...)                            \
	do {                                               \
		if (doit) {                                \
			ddekit_printf("%s(): ", __func__); \
			ddekit_printf(__VA_ARGS__);        \
			ddekit_printf("\n");               \
		}                                          \
	} while(0);
#endif

#ifdef __cplusplus
#define ddekit_info(format, ...)   ddekit_printf(format , ##__VA_ARGS__)
#define ddekit_notify(format, ...) ddekit_printf(format , ##__VA_ARGS__)
#define ddekit_urgent(format, ...) ddekit_printf(format , ##__VA_ARGS__)
#define ddekit_error(format, ...)  ddekit_printf(format , ##__VA_ARGS__)
#define ddekit_fatal(format, ...)  ddekit_printf(format , ##__VA_ARGS__)
#else
#define ddekit_info(format, ...)   ddekit_printf(format , ##__VA_ARGS__)
#define ddekit_notify(format, ...) ddekit_printf(format , ##__VA_ARGS__)
#define ddekit_urgent(format, ...) ddekit_printf(format , ##__VA_ARGS__)
#define ddekit_error(format, ...)  ddekit_printf(format , ##__VA_ARGS__)
#define ddekit_fatal(format, ...)  ddekit_printf(format , ##__VA_ARGS__)
#endif

