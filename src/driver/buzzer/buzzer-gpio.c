/*
 * driver/buzzer/buzzer-gpio.c
 *
 * Copyright(c) 2007-2017 Jianjun Jiang <8192542@qq.com>
 * Official site: http://xboot.org
 * Mobile phone: +86-18665388956
 * QQ: 8192542
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <xboot.h>
#include <gpio/gpio.h>
#include <buzzer/buzzer.h>

struct beep_param_t {
	int frequency;
	int millisecond;
};

struct buzzer_gpio_pdata_t {
	struct timer_t timer;
	struct queue_t * queue;
	int gpio;
	int gpiocfg;
	int active_low;
	int frequency;
};

static void buzzer_gpio_set_frequency(struct buzzer_gpio_pdata_t * pdat, int frequency)
{
	if(frequency > 0)
		gpio_direction_output(pdat->gpio, pdat->active_low ? 0 : 1);
	else
		gpio_direction_output(pdat->gpio, pdat->active_low ? 1 : 0);
}

static void iter_queue_node(struct queue_node_t * node)
{
	if(node && node->data)
		free(node->data);
}

static void buzzer_gpio_set(struct buzzer_t * buzzer, int frequency)
{
	struct buzzer_gpio_pdata_t * pdat = (struct buzzer_gpio_pdata_t *)buzzer->priv;

	if(pdat->frequency != frequency)
	{
		buzzer_gpio_set_frequency(pdat, frequency);
		pdat->frequency = frequency;
	}
}

static int buzzer_gpio_get(struct buzzer_t * buzzer)
{
	struct buzzer_gpio_pdata_t * pdat = (struct buzzer_gpio_pdata_t *)buzzer->priv;
	return pdat->frequency;
}

static void buzzer_gpio_beep(struct buzzer_t * buzzer, int frequency, int millisecond)
{
	struct buzzer_gpio_pdata_t * pdat = (struct buzzer_gpio_pdata_t *)buzzer->priv;
	struct beep_param_t * param;

	if((frequency == 0) && (millisecond == 0))
	{
		timer_cancel(&pdat->timer);
		queue_clear(pdat->queue, iter_queue_node);
		buzzer_gpio_set(buzzer, 0);
		return;
	}

	param = malloc(sizeof(struct beep_param_t));
	if(!param)
		return;
	param->frequency = frequency;
	param->millisecond = millisecond;

	queue_push(pdat->queue, param);
	if(queue_avail(pdat->queue) == 1)
		timer_start_now(&pdat->timer, ms_to_ktime(1));
}

static int buzzer_gpio_timer_function(struct timer_t * timer, void * data)
{
	struct buzzer_t * buzzer = (struct buzzer_t *)(data);
	struct buzzer_gpio_pdata_t * pdat = (struct buzzer_gpio_pdata_t *)buzzer->priv;
	struct beep_param_t * param = queue_pop(pdat->queue);

	if(!param)
	{
		buzzer_gpio_set(buzzer, 0);
		return 0;
	}
	buzzer_gpio_set(buzzer, param->frequency);
	timer_forward_now(&pdat->timer, ms_to_ktime(param->millisecond));
	free(param);
	return 1;
}

static struct device_t * buzzer_gpio_probe(struct driver_t * drv, struct dtnode_t * n)
{
	struct buzzer_gpio_pdata_t * pdat;
	struct buzzer_t * buzzer;
	struct device_t * dev;

	if(!gpio_is_valid(dt_read_int(n, "gpio", -1)))
		return NULL;

	pdat = malloc(sizeof(struct buzzer_gpio_pdata_t));
	if(!pdat)
		return NULL;

	buzzer = malloc(sizeof(struct buzzer_t));
	if(!buzzer)
	{
		free(pdat);
		return NULL;
	}

	timer_init(&pdat->timer, buzzer_gpio_timer_function, buzzer);
	pdat->queue = queue_alloc();
	pdat->gpio = dt_read_int(n, "gpio", -1);
	pdat->gpiocfg = dt_read_int(n, "gpio-config", -1);
	pdat->active_low = dt_read_bool(n, "active-low", 0);
	pdat->frequency = 0;

	buzzer->name = alloc_device_name(dt_read_name(n), dt_read_id(n));
	buzzer->set = buzzer_gpio_set,
	buzzer->get = buzzer_gpio_get,
	buzzer->beep = buzzer_gpio_beep,
	buzzer->priv = pdat;

	if(pdat->gpiocfg >= 0)
		gpio_set_cfg(pdat->gpio, pdat->gpiocfg);
	gpio_set_pull(pdat->gpio, pdat->active_low ? GPIO_PULL_UP :GPIO_PULL_DOWN);
	buzzer_gpio_set_frequency(pdat, pdat->frequency);

	if(!register_buzzer(&dev, buzzer))
	{
		buzzer_gpio_set_frequency(pdat, 0);
		timer_cancel(&pdat->timer);
		queue_free(pdat->queue, iter_queue_node);

		free_device_name(buzzer->name);
		free(buzzer->priv);
		free(buzzer);
		return NULL;
	}
	dev->driver = drv;

	return dev;
}

static void buzzer_gpio_remove(struct device_t * dev)
{
	struct buzzer_t * buzzer = (struct buzzer_t *)dev->priv;
	struct buzzer_gpio_pdata_t * pdat = (struct buzzer_gpio_pdata_t *)buzzer->priv;

	if(buzzer && unregister_buzzer(buzzer))
	{
		buzzer_gpio_set_frequency(pdat, 0);
		timer_cancel(&pdat->timer);
		queue_free(pdat->queue, iter_queue_node);

		free_device_name(buzzer->name);
		free(buzzer->priv);
		free(buzzer);
	}
}

static void buzzer_gpio_suspend(struct device_t * dev)
{
	struct buzzer_t * buzzer = (struct buzzer_t *)dev->priv;
	struct buzzer_gpio_pdata_t * pdat = (struct buzzer_gpio_pdata_t *)buzzer->priv;

	buzzer_gpio_set_frequency(pdat, 0);
}

static void buzzer_gpio_resume(struct device_t * dev)
{
	struct buzzer_t * buzzer = (struct buzzer_t *)dev->priv;
	struct buzzer_gpio_pdata_t * pdat = (struct buzzer_gpio_pdata_t *)buzzer->priv;

	buzzer_gpio_set_frequency(pdat, pdat->frequency);
}

static struct driver_t buzzer_gpio = {
	.name		= "buzzer-gpio",
	.probe		= buzzer_gpio_probe,
	.remove		= buzzer_gpio_remove,
	.suspend	= buzzer_gpio_suspend,
	.resume		= buzzer_gpio_resume,
};

static __init void buzzer_gpio_driver_init(void)
{
	register_driver(&buzzer_gpio);
}

static __exit void buzzer_gpio_driver_exit(void)
{
	unregister_driver(&buzzer_gpio);
}

driver_initcall(buzzer_gpio_driver_init);
driver_exitcall(buzzer_gpio_driver_exit);
