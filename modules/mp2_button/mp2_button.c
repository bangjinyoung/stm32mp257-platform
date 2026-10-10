// SPDX-License-Identifier: GPL-2.0
/*
 * USER2 button (B4, PC11) platform driver for STM32MP257F-DK.
 *
 * Bound by the DT node compatible "bjy,mp2-button" with a button-gpios
 * property. Counts rising edges in a threaded IRQ, drops edges closer
 * than debounce_ms, and exposes the count in sysfs as press_count.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/ktime.h>

static unsigned int debounce_ms = 20;
module_param(debounce_ms, uint, 0644);
MODULE_PARM_DESC(debounce_ms, "Ignore edges closer than this (ms)");

struct mp2_button {
	struct device *dev;
	struct gpio_desc *gpio;
	unsigned int press_count;
	unsigned int bounce_count;
	ktime_t last;
};

static irqreturn_t mp2_button_irq(int irq, void *data)
{
	struct mp2_button *btn = data;
	ktime_t now = ktime_get();

	if (ktime_ms_delta(now, btn->last) < debounce_ms) {
		btn->bounce_count++;
		dev_info(btn->dev, "ignored (bounce %u)\n", btn->bounce_count);
		return IRQ_HANDLED;
	}
	btn->last = now;
	btn->press_count++;
	dev_info(btn->dev, "press %u\n", btn->press_count);
	return IRQ_HANDLED;
}

static ssize_t press_count_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mp2_button *btn = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", btn->press_count);
}
static DEVICE_ATTR_RO(press_count);

static struct attribute *mp2_button_attrs[] = {
	&dev_attr_press_count.attr,
	NULL,
};
ATTRIBUTE_GROUPS(mp2_button);

static int mp2_button_probe(struct platform_device *pdev)
{
	struct mp2_button *btn;
	int irq, ret;

	btn = devm_kzalloc(&pdev->dev, sizeof(*btn), GFP_KERNEL);
	if (!btn)
		return -ENOMEM;
	btn->dev = &pdev->dev;
	platform_set_drvdata(pdev, btn);

	btn->gpio = devm_gpiod_get(&pdev->dev, "button", GPIOD_IN);
	if (IS_ERR(btn->gpio))
		return dev_err_probe(&pdev->dev, PTR_ERR(btn->gpio), "get button gpio\n");

	irq = gpiod_to_irq(btn->gpio);
	if (irq < 0)
		return dev_err_probe(&pdev->dev, irq, "gpio to irq\n");

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL, mp2_button_irq,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					"mp2-button", btn);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "request irq\n");

	dev_info(&pdev->dev, "irq %d ready\n", irq);

	return 0;
}

static int mp2_button_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "pdevice remove\n");
	return 0;
}

static const struct of_device_id mp2_button_of_match[] = {
	{.compatible = "bjy,mp2-button"},
	{ }
};
MODULE_DEVICE_TABLE(of, mp2_button_of_match);

static struct platform_driver mp2_button_driver = {
	.probe = mp2_button_probe,
	.remove = mp2_button_remove,
	.driver = {
		.name = "mp2-button",
		.of_match_table = mp2_button_of_match,
		.dev_groups = mp2_button_groups,
	},
};
module_platform_driver(mp2_button_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("bjy");
MODULE_DESCRIPTION("STM32MP257F-DK USER2 button platform driver");
