// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2022, Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.
 */

#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/sde_io_util.h>
#include <linux/of_gpio.h>
#include "dp_lphw_hpd.h"
#include "dp_debug.h"

struct dp_lphw_hpd_private {
	struct device *dev;
	struct dp_hpd base;
	struct dp_parser *parser;
	struct dp_catalog_hpd *catalog;
	struct dss_gpio gpio_cfg;
	struct workqueue_struct *connect_wq;
	struct delayed_work work;
	struct work_struct connect;
	struct work_struct disconnect;
	struct work_struct attention;
	struct dp_hpd_cb *cb;
	int irq;
	bool hpd;
};

static void dp_lphw_hpd_attention(struct work_struct *work)
{
	struct dp_lphw_hpd_private *lphw_hpd = container_of(work,
				struct dp_lphw_hpd_private, attention);

	if (!lphw_hpd) {
		DP_ERR("invalid input\n");
		return;
	}

	lphw_hpd->base.hpd_irq = true;

	if (lphw_hpd->cb && lphw_hpd->cb->attention)
		lphw_hpd->cb->attention(lphw_hpd->dev);
}

static void dp_lphw_hpd_connect(struct work_struct *work)
{
	struct dp_lphw_hpd_private *lphw_hpd = container_of(work,
				struct dp_lphw_hpd_private, connect);

	if (!lphw_hpd) {
		DP_ERR("invalid input\n");
		return;
	}

	lphw_hpd->base.hpd_high = true;
	lphw_hpd->base.alt_mode_cfg_done = true;
	lphw_hpd->base.hpd_irq = false;

	if (lphw_hpd->cb && lphw_hpd->cb->configure)
		lphw_hpd->cb->configure(lphw_hpd->dev);
}

static void dp_lphw_hpd_disconnect(struct work_struct *work)
{
	struct dp_lphw_hpd_private *lphw_hpd = container_of(work,
				struct dp_lphw_hpd_private, disconnect);

	if (!lphw_hpd) {
		DP_ERR("invalid input\n");
		return;
	}

	lphw_hpd->base.hpd_high = false;
	lphw_hpd->base.alt_mode_cfg_done = false;
	lphw_hpd->base.hpd_irq = false;

	if (lphw_hpd->cb && lphw_hpd->cb->disconnect)
		lphw_hpd->cb->disconnect(lphw_hpd->dev);
}

static irqreturn_t dp_tlmm_isr(int unused, void *data)
{
	struct dp_lphw_hpd_private *lphw_hpd = data;
	bool hpd;

	if (!lphw_hpd)
		return IRQ_NONE;

	/*
	 * According to the DP spec, HPD high event can be confirmed only after
	 * the HPD line has een asserted continuously for more than 100ms
	 */
	usleep_range(99000, 100000);

	hpd = gpio_get_value_cansleep(lphw_hpd->gpio_cfg.gpio);

	DP_DEBUG("DP%d lphw_hpd state = %d, new hpd state = %d\n",
			lphw_hpd->parser->cell_idx, lphw_hpd->hpd, hpd);
	if (!lphw_hpd->hpd && hpd) {
		lphw_hpd->hpd = true;
		queue_work(lphw_hpd->connect_wq, &lphw_hpd->connect);
	}

	return IRQ_HANDLED;
}

static void dp_lphw_hpd_host_init(struct dp_hpd *dp_hpd,
		struct dp_catalog_hpd *catalog)
{
	struct dp_lphw_hpd_private *lphw_hpd;

	if (!dp_hpd) {
		DP_ERR("invalid input\n");
		return;
	}

	lphw_hpd = container_of(dp_hpd, struct dp_lphw_hpd_private, base);

	lphw_hpd->catalog->config_hpd(lphw_hpd->catalog, true);

	/*
	 * Changing the gpio function to dp controller for the hpd line is not
	 * stopping the tlmm interrupts generation on function 0.
	 * So, as an additional step, disable the gpio interrupt irq also
	 */
	disable_irq(lphw_hpd->irq);
}

static void dp_lphw_hpd_host_deinit(struct dp_hpd *dp_hpd,
		struct dp_catalog_hpd *catalog)
{
	struct dp_lphw_hpd_private *lphw_hpd;

	if (!dp_hpd) {
		DP_ERR("invalid input\n");
		return;
	}

	lphw_hpd = container_of(dp_hpd, struct dp_lphw_hpd_private, base);

	/* Enable the tlmm interrupt irq which was disabled in host_init */
	enable_irq(lphw_hpd->irq);

	lphw_hpd->catalog->config_hpd(lphw_hpd->catalog, false);
}

static void dp_lphw_hpd_isr(struct dp_hpd *dp_hpd)
{
	struct dp_lphw_hpd_private *lphw_hpd;
	u32 isr = 0, status;
	int rc = 0;

	if (!dp_hpd) {
		DP_ERR("invalid input\n");
		return;
	}

	lphw_hpd = container_of(dp_hpd, struct dp_lphw_hpd_private, base);

	isr = lphw_hpd->catalog->get_interrupt(lphw_hpd->catalog);
	status = (isr >> 29) & 0x7;

	/* Check for uncommon cases */
	switch (status) {
	case DP_HPD_STATUS_DISCONNECTED:
		if (!(isr & DP_HPD_UNPLUG_INT_STATUS))
			DP_INFO("DP%d disconnect but no interrupt, hpd isr state: 0x%x\n",
					lphw_hpd->parser->cell_idx, isr);
		if (isr & (DP_HPD_PLUG_INT_STATUS | DP_HPD_REPLUG_INT_STATUS))
			DP_INFO("DP%d missed connect interrupt, hpd isr state: 0x%x\n",
					lphw_hpd->parser->cell_idx, isr);
		if (isr & DP_IRQ_HPD_INT_STATUS)
			DP_INFO("DP%d missed hpd_irq interrupt, hpd isr state: 0x%x\n",
					lphw_hpd->parser->cell_idx, isr);
		break;
	case DP_HPD_STATUS_CONNECT_PENDING:
		DP_INFO("DP%d connect pending, hpd isr state: 0x%x\n",
				lphw_hpd->parser->cell_idx, isr);
		break;
	case DP_HPD_STATUS_CONNECTED:
		if (!(isr & (DP_HPD_PLUG_INT_STATUS | DP_HPD_REPLUG_INT_STATUS)))
			DP_INFO("DP%d connect but no interrupt, hpd isr state: 0x%x\n",
					lphw_hpd->parser->cell_idx, isr);
		if (isr & DP_HPD_UNPLUG_INT_STATUS)
			DP_INFO("DP%d missed disconnect interrupt, hpd isr state: 0x%x\n",
					lphw_hpd->parser->cell_idx, isr);
		if (isr & DP_IRQ_HPD_INT_STATUS)
			DP_INFO("DP%d missed hpd_irq interrupt, hpd isr state: 0x%x\n",
					lphw_hpd->parser->cell_idx, isr);
		break;
	case DP_HPD_STATUS_HPD_IO_GLITCH_COUNT:
		DP_INFO("DP%d hpd io glich counting, hpd isr state: 0x%x\n",
				lphw_hpd->parser->cell_idx, isr);
		break;
	case DP_HPD_STATUS_IRQ_HPD_PULSE_COUNT:
		DP_INFO("DP%d hpd irq counting, hpd isr state: 0x%x\n",
				lphw_hpd->parser->cell_idx, isr);
		break;
	case DP_HPD_STATUS_HPD_REPLUG_COUNT:
		DP_INFO("DP%d hpd replug counting, hpd isr state: 0x%x\n",
				lphw_hpd->parser->cell_idx, isr);
		break;
	default:
		break;
	}

	/* Process based on most updated HPD status, instead of interrupt */
	if (status == DP_HPD_STATUS_DISCONNECTED) { /* disconnect status */

		DP_DEBUG("DP%d disconnect interrupt, hpd isr state: 0x%x\n",
				lphw_hpd->parser->cell_idx, isr);

		if (lphw_hpd->base.hpd_high) {
			lphw_hpd->hpd = false;
			lphw_hpd->base.hpd_high = false;
			lphw_hpd->base.alt_mode_cfg_done = false;
			lphw_hpd->base.hpd_irq = false;

			rc = queue_work(lphw_hpd->connect_wq,
					&lphw_hpd->disconnect);
			if (!rc)
				DP_DEBUG("DP%d disconnect not queued\n",
						lphw_hpd->parser->cell_idx);
		} else {
			DP_INFO("DP%d already disconnected\n", lphw_hpd->parser->cell_idx);
		}

	} else if ((status == DP_HPD_STATUS_CONNECTED) &&
			!(isr & DP_IRQ_HPD_INT_STATUS)) { /* connected status */

		DP_DEBUG("DP%d connect interrupt, hpd isr state: 0x%x\n",
				lphw_hpd->parser->cell_idx, isr);

		if (!lphw_hpd->hpd) {
			lphw_hpd->hpd = true;
			rc = queue_work(lphw_hpd->connect_wq,
					&lphw_hpd->connect);
			if (!rc)
				DP_DEBUG("DP%d connect not queued\n",
						lphw_hpd->parser->cell_idx);
		} else {
			DP_INFO("DP%d already connected\n", lphw_hpd->parser->cell_idx);
		}

	} else if ((status == DP_HPD_STATUS_CONNECTED) &&
			(isr & DP_IRQ_HPD_INT_STATUS)) { /* attention interrupt */

		DP_DEBUG("DP%d hpd_irq interrupt, hpd isr state: 0x%x\n",
				lphw_hpd->parser->cell_idx, isr);

		rc = queue_work(lphw_hpd->connect_wq, &lphw_hpd->attention);
		if (!rc)
			DP_DEBUG("DP%d attention not queued\n", lphw_hpd->parser->cell_idx);

	} else { /* intermediate status */

		DP_INFO("DP%d ignored, hpd isr state: 0x%x\n",
				lphw_hpd->parser->cell_idx, isr);

	}
}

static int dp_lphw_hpd_simulate_connect(struct dp_hpd *dp_hpd, bool hpd)
{
	struct dp_lphw_hpd_private *lphw_hpd;

	if (!dp_hpd) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	lphw_hpd = container_of(dp_hpd, struct dp_lphw_hpd_private, base);

	lphw_hpd->base.hpd_high = hpd;
	lphw_hpd->base.alt_mode_cfg_done = hpd;
	lphw_hpd->base.hpd_irq = false;

	if (!lphw_hpd->cb || !lphw_hpd->cb->configure ||
			!lphw_hpd->cb->disconnect) {
		DP_ERR("invalid callback\n");
		return -EINVAL;
	}

	if (hpd)
		lphw_hpd->cb->configure(lphw_hpd->dev);
	else
		lphw_hpd->cb->disconnect(lphw_hpd->dev);

	return 0;
}

static int dp_lphw_hpd_simulate_attention(struct dp_hpd *dp_hpd, int vdo)
{
	struct dp_lphw_hpd_private *lphw_hpd;

	if (!dp_hpd) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	lphw_hpd = container_of(dp_hpd, struct dp_lphw_hpd_private, base);

	lphw_hpd->base.hpd_irq = true;

	if (lphw_hpd->cb && lphw_hpd->cb->attention)
		lphw_hpd->cb->attention(lphw_hpd->dev);

	return 0;
}

int dp_lphw_hpd_register(struct dp_hpd *dp_hpd)
{
	struct dp_lphw_hpd_private *lphw_hpd;
	int rc = 0;

	if (!dp_hpd)
		return -EINVAL;

	lphw_hpd = container_of(dp_hpd, struct dp_lphw_hpd_private, base);

	lphw_hpd->hpd = gpio_get_value_cansleep(lphw_hpd->gpio_cfg.gpio);

	rc = devm_request_threaded_irq(lphw_hpd->dev, lphw_hpd->irq, NULL,
		dp_tlmm_isr,
		IRQF_TRIGGER_RISING | IRQF_ONESHOT,
		"dp-gpio-intp", lphw_hpd);
	if (rc) {
		DP_ERR("DP%d Failed to request INTP threaded IRQ: %d\n",
				lphw_hpd->parser->cell_idx, rc);
		return rc;
	}
	enable_irq_wake(lphw_hpd->irq);

	if (lphw_hpd->hpd)
		queue_work(lphw_hpd->connect_wq, &lphw_hpd->connect);

	return rc;
}

static void dp_lphw_hpd_deinit(struct dp_lphw_hpd_private *lphw_hpd)
{
	struct dp_parser *parser = lphw_hpd->parser;
	int i = 0;

	for (i = 0; i < parser->mp[DP_PHY_PM].num_vreg; i++) {

		if (!strcmp(parser->mp[DP_PHY_PM].vreg_config[i].vreg_name,
					"hpd-pwr")) {
			/* disable the hpd-pwr voltage regulator */
			if (msm_dss_enable_vreg(
				&parser->mp[DP_PHY_PM].vreg_config[i], 1,
				false))
				DP_ERR("DP%d hpd-pwr vreg not disabled\n",
						lphw_hpd->parser->cell_idx);

			break;
		}
	}
}

static void dp_lphw_hpd_init(struct dp_lphw_hpd_private *lphw_hpd)
{
	struct dp_pinctrl pinctrl = {0};
	struct dp_parser *parser = lphw_hpd->parser;
	int i = 0, rc = 0;

	for (i = 0; i < parser->mp[DP_PHY_PM].num_vreg; i++) {

		if (!strcmp(parser->mp[DP_PHY_PM].vreg_config[i].vreg_name,
					"hpd-pwr")) {
			/* enable the hpd-pwr voltage regulator */
			if (msm_dss_enable_vreg(
				&parser->mp[DP_PHY_PM].vreg_config[i], 1,
				true))
				DP_ERR("DP%d hpd-pwr vreg not enabled\n",
						lphw_hpd->parser->cell_idx);

			break;
		}
	}

	pinctrl.pin = devm_pinctrl_get(lphw_hpd->dev);

	if (!IS_ERR_OR_NULL(pinctrl.pin)) {
		pinctrl.state_hpd_active = pinctrl_lookup_state(pinctrl.pin,
						"mdss_dp_hpd_active");

		if (!IS_ERR_OR_NULL(pinctrl.state_hpd_active)) {
			rc = pinctrl_select_state(pinctrl.pin,
					pinctrl.state_hpd_active);
			if (rc)
				DP_ERR("DP%d failed to set hpd_active state\n",
						lphw_hpd->parser->cell_idx);
		}
		pinctrl.state_hpd_tlmm = pinctrl.state_hpd_ctrl = NULL;
	}
}

static int dp_lphw_hpd_create_workqueue(struct dp_lphw_hpd_private *lphw_hpd)
{
	lphw_hpd->connect_wq = create_singlethread_workqueue("dp_lphw_work");
	if (IS_ERR_OR_NULL(lphw_hpd->connect_wq)) {
		DP_ERR("DP%d Error creating connect_wq\n", lphw_hpd->parser->cell_idx);
		return -EPERM;
	}

	INIT_WORK(&lphw_hpd->connect, dp_lphw_hpd_connect);
	INIT_WORK(&lphw_hpd->disconnect, dp_lphw_hpd_disconnect);
	INIT_WORK(&lphw_hpd->attention, dp_lphw_hpd_attention);

	return 0;
}

struct dp_hpd *dp_lphw_hpd_get(struct device *dev, struct dp_parser *parser,
	struct dp_catalog_hpd *catalog, struct dp_hpd_cb *cb)
{
	int rc = 0;
	const char *hpd_gpio_name = "qcom,dp-hpd-gpio";
	struct dp_lphw_hpd_private *lphw_hpd = NULL;
	unsigned int gpio;

	if (!dev || !parser || !cb) {
		DP_ERR("invalid device\n");
		rc = -EINVAL;
		goto error;
	}

	gpio = of_get_named_gpio(dev->of_node, hpd_gpio_name, 0);
	if (!gpio_is_valid(gpio)) {
		DP_DEBUG("%s gpio not specified\n", hpd_gpio_name);
		rc = -EINVAL;
		goto error;
	}

	lphw_hpd = devm_kzalloc(dev, sizeof(*lphw_hpd), GFP_KERNEL);
	if (!lphw_hpd) {
		rc = -ENOMEM;
		goto error;
	}

	lphw_hpd->gpio_cfg.gpio = gpio;
	strlcpy(lphw_hpd->gpio_cfg.gpio_name, hpd_gpio_name,
		sizeof(lphw_hpd->gpio_cfg.gpio_name));
	lphw_hpd->gpio_cfg.value = 0;

	rc = gpio_request(lphw_hpd->gpio_cfg.gpio,
		lphw_hpd->gpio_cfg.gpio_name);
	if (rc) {
		DP_ERR("%s: failed to request gpio\n", hpd_gpio_name);
		goto gpio_error;
	}
	gpio_direction_input(lphw_hpd->gpio_cfg.gpio);

	lphw_hpd->dev = dev;
	lphw_hpd->cb = cb;
	lphw_hpd->irq = gpio_to_irq(lphw_hpd->gpio_cfg.gpio);

	rc = dp_lphw_hpd_create_workqueue(lphw_hpd);
	if (rc) {
		DP_ERR("DP%d Failed to create a dp_hpd workqueue\n", parser->cell_idx);
		goto gpio_error;
	}

	lphw_hpd->parser = parser;
	lphw_hpd->catalog = catalog;
	lphw_hpd->base.isr = dp_lphw_hpd_isr;
	lphw_hpd->base.host_init = dp_lphw_hpd_host_init;
	lphw_hpd->base.host_deinit = dp_lphw_hpd_host_deinit;
	lphw_hpd->base.simulate_connect = dp_lphw_hpd_simulate_connect;
	lphw_hpd->base.simulate_attention = dp_lphw_hpd_simulate_attention;
	lphw_hpd->base.register_hpd = dp_lphw_hpd_register;

	dp_lphw_hpd_init(lphw_hpd);

	return &lphw_hpd->base;

gpio_error:
	devm_kfree(dev, lphw_hpd);
error:
	return ERR_PTR(rc);
}

void dp_lphw_hpd_put(struct dp_hpd *dp_hpd)
{
	struct dp_lphw_hpd_private *lphw_hpd;

	if (!dp_hpd)
		return;

	lphw_hpd = container_of(dp_hpd, struct dp_lphw_hpd_private, base);

	dp_lphw_hpd_deinit(lphw_hpd);
	gpio_free(lphw_hpd->gpio_cfg.gpio);
	devm_kfree(lphw_hpd->dev, lphw_hpd);
}
