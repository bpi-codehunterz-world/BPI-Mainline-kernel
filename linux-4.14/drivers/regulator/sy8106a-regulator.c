/*
 * sy8106a-regulator.c - Regulator device driver for SY8106A
 *
 * Copyright (C) 2016 Ondřej Jirman <megous@megous.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

#define SY8106A_REG_VOUT1_SEL		0x01
#define SY8106A_REG_VOUT_COM		0x02
#define SY8106A_REG_VOUT1_SEL_MASK	0x7f
#define SY8106A_DISABLE_REG		BIT(0)
#define SY8106A_GO_BIT			BIT(7)

struct sy8106a {
	struct regulator_dev *rdev;
	struct regmap *regmap;
};

static const struct regmap_config sy8106a_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int sy8106a_set_voltage_sel(struct regulator_dev *rdev, unsigned int sel)
{
	/* We use our set_voltage_sel in order to avoid unnecessary I2C
	 * chatter, because the regulator_get_voltage_sel_regmap using
	 * apply_bit would perform 4 unnecessary transfers instead of one,
	 * increasing the chance of error.
	 */
	return regmap_write(rdev->regmap, rdev->desc->vsel_reg,
			    sel | SY8106A_GO_BIT);
}

static const struct regulator_ops sy8106a_ops = {
	.set_voltage_sel = sy8106a_set_voltage_sel,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.list_voltage = regulator_list_voltage_linear,
	/* Enabling/disabling the regulator is not yet implemented */
};

/* Default limits measured in millivolts and milliamps */
#define SY8106A_MIN_MV		680
#define SY8106A_MAX_MV		1950
#define SY8106A_STEP_MV		10

static const struct regulator_desc sy8106a_reg = {
	.name = "SY8106A",
	.id = 0,
	.ops = &sy8106a_ops,
	.type = REGULATOR_VOLTAGE,
	.n_voltages = ((SY8106A_MAX_MV - SY8106A_MIN_MV) / SY8106A_STEP_MV) + 1,
	.min_uV = (SY8106A_MIN_MV * 1000),
	.uV_step = (SY8106A_STEP_MV * 1000),
	.vsel_reg = SY8106A_REG_VOUT1_SEL,
	.vsel_mask = SY8106A_REG_VOUT1_SEL_MASK,
	/*
	 * This ramp_delay is a conservative default value which works on
	 * H3/H5 boards VDD-CPUX situations.
	 */
	.ramp_delay = 200,
	.owner = THIS_MODULE,
};

/*
 * I2C driver interface functions
 */
static int sy8106a_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct sy8106a *chip;
	struct device *dev = &i2c->dev;
	struct regulator_dev *rdev = NULL;
	struct regulator_config config = { };
	unsigned int selector;
	int error;

	chip = devm_kzalloc(&i2c->dev, sizeof(struct sy8106a), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->regmap = devm_regmap_init_i2c(i2c, &sy8106a_regmap_config);
	if (IS_ERR(chip->regmap)) {
		error = PTR_ERR(chip->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			error);
		return error;
	}

	config.dev = &i2c->dev;
	config.regmap = chip->regmap;
	config.driver_data = chip;

	config.of_node = dev->of_node;
	config.init_data = of_get_regulator_init_data(dev, dev->of_node,
						      &sy8106a_reg);

	if (!config.init_data)
		return -ENOMEM;

	/* Probe regulator */
	error = regmap_read(chip->regmap, SY8106A_REG_VOUT1_SEL, &selector);
	if (error) {
		dev_err(&i2c->dev, "Failed to read voltage at probe time: %d\n", error);
		return error;
	}

	rdev = devm_regulator_register(&i2c->dev, &sy8106a_reg, &config);
	if (IS_ERR(rdev)) {
		error = PTR_ERR(rdev);
		dev_err(&i2c->dev, "Failed to register SY8106A regulator: %d\n", error);
		return error;
	}

	chip->rdev = rdev;

	i2c_set_clientdata(i2c, chip);

	return 0;
}

static const struct of_device_id sy8106a_i2c_of_match[] = {
	{ .compatible = "silergy,sy8106a" },
	{ },
};
MODULE_DEVICE_TABLE(of, sy8106a_i2c_of_match);

static const struct i2c_device_id sy8106a_i2c_id[] = {
	{ "sy8106a", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, sy8106a_i2c_id);

static struct i2c_driver sy8106a_regulator_driver = {
	.driver = {
		.name = "sy8106a",
		.of_match_table	= of_match_ptr(sy8106a_i2c_of_match),
	},
	.probe = sy8106a_i2c_probe,
	.id_table = sy8106a_i2c_id,
};

module_i2c_driver(sy8106a_regulator_driver);

MODULE_AUTHOR("Ondřej Jirman <megous@megous.com>");
MODULE_DESCRIPTION("Regulator device driver for Silergy SY8106A");
MODULE_LICENSE("GPL v2");
