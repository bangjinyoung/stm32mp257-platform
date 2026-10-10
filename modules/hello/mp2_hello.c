// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal out-of-tree module for STM32MP257F-DK (M2.4).
 * Shows init/exit, a module parameter, and the load count in the kernel log.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/utsname.h>

static int greet_count = 1;
module_param(greet_count, int, 0444);
MODULE_PARM_DESC(greet_count, "Number of greeting lines printed at load (1..8)");

static int __init mp2_hello_init(void)
{
	int i;

	if (greet_count < 1 || greet_count > 8) {
		pr_err("mp2_hello: greet_count %d out of range 1..8\n", greet_count);
		return -EINVAL;
	}

	for (i = 0; i < greet_count; i++)
		pr_info("mp2_hello: hello %d/%d from %s %s\n", i + 1, greet_count,
			utsname()->machine, utsname()->release);

	return 0;
}

static void __exit mp2_hello_exit(void)
{
	pr_info("mp2_hello: bye\n");
}

module_init(mp2_hello_init);
module_exit(mp2_hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("bjy");
MODULE_DESCRIPTION("STM32MP257F-DK minimal module (init/exit, module parameter)");
