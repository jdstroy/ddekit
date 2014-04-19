/*
 * This file is part of DDE/Linux2.6.
 *
 * (c) 2006-2010 Bjoern Doebel <doebel@os.inf.tu-dresden.de>
 *               Christian Helmuth <ch12@os.inf.tu-dresden.de>
 *     economic rights: Technische Universitaet Dresden (Germany)
 *
 * This file is part of TUD:OS and distributed under the terms of the
 * GNU General Public License 2.
 * Please see the COPYING-GPL-2 file for details.
 */

#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/slab.h>



#if 1
#define DEBUGP printk
#else
#define DEBUGP(fmt, a...)
#endif

static inline char dash2underscore(char c)
{
	if (c == '-')
		return '_';
	return c;
}

static inline int parameq(const char *input, const char *paramname)
{
	unsigned int i;
	for (i = 0; dash2underscore(input[i]) == paramname[i]; i++)
		if (input[i] == '\0')
			return 1;
	return 0;
}

static int parse_one(char *param,
		     char *val,
		     struct kernel_param *params, 
		     unsigned num_params,
		     int (*handle_unknown)(char *param, char *val))
{
	unsigned int i;

	/* Find parameter */
	for (i = 0; i < num_params; i++) {
		printk("comparing %s and %s\n", param, params[i].name);
		if (parameq(param, params[i].name)) {
			DEBUGP("They are equal!  Calling %p (%s)\n",
				params[i].set, val);
			return params[i].set(val, &params[i]);
		}
	}

	if (handle_unknown) {
		DEBUGP("Unknown argument: calling %p\n", handle_unknown);
		return handle_unknown(param, val);
	}

	DEBUGP("Unknown argument `%s'\n", param);
	return -ENOENT;
}

/* You can use " around spaces, but can't escape ". */
/* Hyphens and underscores equivalent in parameter names. */
static char *next_arg(char *args, char **param, char **val)
{
        unsigned int i, equals = 0;
        int in_quote = 0, quoted = 0;
        char *next;

        if (*args == '"') {
                args++;
                in_quote = 1;
                quoted = 1;
        }

        for (i = 0; args[i]; i++) {
                if (args[i] == ' ' && !in_quote)
                        break;
                if (equals == 0) {
                        if (args[i] == '=')
                                equals = i;
                }
                if (args[i] == '"')
                        in_quote = !in_quote;
        }

        *param = args;
        if (!equals)
                *val = NULL;
        else {
                args[equals] = '\0';
                *val = args + equals + 1;

                /* Don't include quotes in value. */
                if (**val == '"') {
                        (*val)++;
                        if (args[i-1] == '"')
                                args[i-1] = '\0';
                }
                if (quoted && args[i-1] == '"')
                        args[i-1] = '\0';
        }

        if (args[i]) {
                args[i] = '\0';
                next = args + i + 1;
        } else
                next = args + i;

        /* Chew up trailing spaces. */
        while (*next == ' ')
                next++;
        return next;
}

/* Args looks like "foo=bar,bar2 baz=fuz wiz". */
int parse_args(const char *name,
               char *args,
               struct kernel_param *params,
               unsigned num,
               int (*unknown)(char *param, char *val))
{
	char *param, *val;

	DEBUGP("Parsing ARGS: %s\n", args);

	/* Chew leading spaces */
	while (*args == ' ')
		args++;

	while (*args) {
		int ret;
		int irq_was_disabled;

		args = next_arg(args, &param, &val);
		irq_was_disabled = irqs_disabled();
		ret = parse_one(param, val, params, num, unknown);
		if (irq_was_disabled && !irqs_disabled()) {
			printk(KERN_WARNING "parse_args(): option '%s' enabled "
				"irq's!\n", param);
		}
		switch (ret) {
		case -ENOENT:
			printk(KERN_ERR "%s: Unknown parameter `%s'\n",
				name, param);
			return ret;
		case -ENOSPC:
			printk(KERN_ERR
				"%s: `%s' too large for parameter `%s'\n",
				name, val ?: "", param);
			return ret;
		case 0:
			break;
		default:
			printk(KERN_ERR
				"%s: `%s' invalid for parameter `%s'\n",
				name, val ?: "", param);
			return ret;
		}
	}

	/* All parsed OK. */
	return 0;
}

/* Lazy bastard, eh? */
#define STANDARD_PARAM_DEF(name, type, format, tmptype, strtolfn)      	\
	int param_set_##name(const char *val, struct kernel_param *kp)	\
	{								\
		tmptype l;                                              \
		int ret;                                                \
		if (!val) return -EINVAL;                               \
		ret = strtolfn(val, 0, &l);                             \
		if (ret == -EINVAL || ((type)l != l))                   \
			return -EINVAL;                                 \
		*((type *)kp->arg) = l;                                 \
		return 0;                                               \
	}								\
	int param_get_##name(char *buffer, struct kernel_param *kp)	\
	{                                                               \
		return sprintf(buffer, format, *((type *)kp->arg));     \
	}

STANDARD_PARAM_DEF(byte, unsigned char, "%c", unsigned long, strict_strtoul);
STANDARD_PARAM_DEF(short, short, "%hi", long, strict_strtol);
STANDARD_PARAM_DEF(ushort, unsigned short, "%hu", unsigned long, strict_strtoul);
STANDARD_PARAM_DEF(int, int, "%i", long, strict_strtol);
STANDARD_PARAM_DEF(uint, unsigned int, "%u", unsigned long, strict_strtoul);
STANDARD_PARAM_DEF(long, long, "%li", long, strict_strtol);
STANDARD_PARAM_DEF(ulong, unsigned long, "%lu", unsigned long, strict_strtoul);

/* We break the rule and mangle the string. */
static int param_array(const char *name,
		       const char *val,
		       unsigned int min, unsigned int max,
		       void *elem, int elemsize,
		       int (*set)(const char *, struct kernel_param *kp),
		       unsigned int *num)
{
	int ret;
	struct kernel_param kp;
	char save;

	/* Get the name right for errors. */
	kp.name = name;
	kp.arg = elem;

	/* No equals sign? */
	if (!val) {
		printk(KERN_ERR "%s: expects arguments\n", name);
		return -EINVAL;
	}

	*num = 0;
	/* We expect a comma-separated list of values. */
	do {
		int len;

		if (*num == max) {
			printk(KERN_ERR "%s: can only take %i arguments\n",
			name, max);
			return -EINVAL;
		}
		len = strcspn(val, ",");

		/* nul-terminate and parse */
		save = val[len];
		((char *)val)[len] = '\0';
		ret = set(val, &kp);

		if (ret != 0)
			return ret;
		kp.arg += elemsize;
		val += len+1;
		(*num)++;
	} while (save == ',');

	if (*num < min) {
		printk(KERN_ERR "%s: needs at least %i arguments\n",
		name, min);
		return -EINVAL;
	}
	return 0;
}


int param_array_set(const char *val, struct kernel_param *kp)
{
	const struct kparam_array *arr = kp->arr;
	unsigned int temp_num;

	param_array(kp->name, val, 1, arr->max, arr->elem,
		arr->elemsize, arr->set, arr->num ?: &temp_num);
	printk("arr->num = %d, temp_num = %d\n", *arr->num, temp_num);
	return 0;
}

int param_array_get(char *buffer, struct kernel_param *kp)
{
	int i, off, ret;
	const struct kparam_array *arr = kp->arr;
	struct kernel_param p;

	p = *kp;
	for (i = off = 0; i < (arr->num ? *arr->num : arr->max); i++) {
		if (i)
			buffer[off++] = ',';
		p.arg = arr->elem + arr->elemsize * i;
		ret = arr->get(buffer + off, &p);
		if (ret < 0)
			return ret;
		off += ret;
	}
	buffer[off] = '\0';
	return off;
}

extern struct kernel_param __start___param[], __stop___param[];

EXPORT_SYMBOL(param_set_uint);
EXPORT_SYMBOL(param_get_uint);
EXPORT_SYMBOL(param_array_get);
EXPORT_SYMBOL(param_array_set);

int printk_ratelimit(void)
{
	return 0;
}

void
parse_cmdline_parameters(int argc, char **argv, char **envp)
{
	int num;
	int i;

	num = __stop___param - __start___param;

	printk("%d cmdline args, %d params\n", argc, num);

	for(i = 0; i < argc; i++) {
		printk("parsing parameter (%d of %d): %s\n", i, argc, argv[i]);
		parse_args("dde_linux26", argv[i], __start___param, num, NULL);
	}
}
__attribute__((section(".preinit_array"))) typeof(parse_cmdline_parameters) *__preinit = parse_cmdline_parameters;

