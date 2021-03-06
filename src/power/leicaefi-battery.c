#include "leicaefi-power.h"

static int leicaefi_battery_get_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 union power_supply_propval *val);

static int leicaefi_battery_set_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 const union power_supply_propval *val);

static int
leicaefi_battery_property_is_writeable(struct power_supply *psy,
				       enum power_supply_property psp);

static enum power_supply_property leicaefi_power_battery_properties[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_AVG,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
};

static const struct leicaefi_battery_desc leicaefi_bat1_psy_desc = {
    .kernel_desc = {
        .name = LEICAEFI_POWER_SUPPLY_NAME_BAT1,
        .type = POWER_SUPPLY_TYPE_BATTERY,
        .properties = leicaefi_power_battery_properties,
        .num_properties = ARRAY_SIZE(leicaefi_power_battery_properties),
        .get_property = leicaefi_battery_get_property,
        .set_property = leicaefi_battery_set_property,
        .property_is_writeable = leicaefi_battery_property_is_writeable,
    },
    .validity_bit = LEICAEFI_POWERSRCBIT_BAT1VAL,
};

static int leicaefi_battery_is_present(struct leicaefi_battery *battery,
				       int *val)
{
	u16 reg_value = 0;

	int rv = leicaefi_chip_read(battery->efidev->efichip,
				    LEICAEFI_REG_PWR_SRC_STATUS, &reg_value);
	*val = (reg_value & battery->desc->validity_bit) ? 1 : 0;

	dev_dbg(&battery->supply->dev, "%s value=%d rv=%d\n", __func__, *val,
		rv);

	return rv;
}

static int leicaefi_battery_get_capacity(struct leicaefi_battery *battery,
					 int *val)
{
	u16 reg_value = 0;

	int rv = leicaefi_chip_read(battery->efidev->efichip,
				    LEICAEFI_REG_BAT_1_RSOC, &reg_value);
	*val = reg_value;

	dev_dbg(&battery->supply->dev, "%s value=%d rv=%d\n", __func__, *val,
		rv);

	return rv;
}

static int leicaefi_battery_read_msg(struct leicaefi_battery *battery, u8 cmd,
				     int *val, int default_value)
{
	u16 reg_value = 0;
	int rv = 0;
	int exists = 0;

	*val = default_value;

	// check if battery is present, if not do not send message as it may hang forever
	rv = leicaefi_battery_is_present(battery, &exists);
	if (rv != 0) {
		dev_warn(
			&battery->supply->dev,
			"%s cmd %d, failed to get battery presence, error %d\n",
			__func__, (int)cmd, rv);
		return rv;
	}
	if (!exists) {
		dev_dbg(&battery->supply->dev,
			"%s cmd=%d, battery not present, default value=%d\n",
			__func__, (int)cmd, *val);

		return 0;
	}

	rv = leicaefi_chip_gencmd(battery->efidev->efichip,
				  LEICAEFI_CMD_BATTERY1_READMSG_MASK | cmd, 0,
				  &reg_value);
	if (rv != 0) {
		dev_warn(
			&battery->supply->dev,
			"%s cmd %d, failed to execute battery command, error %d\n",
			__func__, (int)cmd, rv);
		return rv;
	}

	*val = reg_value;

	dev_dbg(&battery->supply->dev, "%s cmd=%d, success, value=%d\n",
		__func__, (int)cmd, *val);

	return 0;
}

static int leicaefi_battery_read_time_min(struct leicaefi_battery *battery,
					  u8 cmd, int *val, int default_value)
{
	int rv = leicaefi_battery_read_msg(battery, cmd, val, default_value);
	if (rv == 0) {
		*val *= 60; // min to sec
	}
	return rv;
}

static int leicaefi_battery_read_micro_unit(struct leicaefi_battery *battery,
					    u8 cmd, int *val, int default_value)
{
	int rv = leicaefi_battery_read_msg(battery, cmd, val, default_value);
	if (rv == 0) {
		*val *= 1000; // milli to micro
	}
	return rv;
}

static int
leicaefi_battery_get_time_to_empty_now(struct leicaefi_battery *battery,
				       int *val)
{
	return leicaefi_battery_read_time_min(
		battery, LEICAEFI_BAT_MSG_RUN_TIME_TO_EMPTY, val, 0);
}

static int
leicaefi_battery_get_time_to_empty_avg(struct leicaefi_battery *battery,
				       int *val)
{
	return leicaefi_battery_read_time_min(
		battery, LEICAEFI_BAT_MSG_AVERAGE_TIME_TO_EMPTY, val, 0);
}

static int
leicaefi_battery_get_time_to_full_avg(struct leicaefi_battery *battery,
				      int *val)
{
	return leicaefi_battery_read_time_min(
		battery, LEICAEFI_BAT_MSG_AVERAGE_TIME_TO_FULL, val, 0);
}

static int leicaefi_battery_get_current_now(struct leicaefi_battery *battery,
					    int *val)
{
	return leicaefi_battery_read_micro_unit(
		battery, LEICAEFI_BAT_MSG_CURRENT, val, 0);
}

static int leicaefi_battery_get_current_avg(struct leicaefi_battery *battery,
					    int *val)
{
	return leicaefi_battery_read_micro_unit(
		battery, LEICAEFI_BAT_MSG_AVERAGE_CURRENT, val, 0);
}

static int leicaefi_battery_get_voltage_now(struct leicaefi_battery *battery,
					    int *val)
{
	return leicaefi_battery_read_micro_unit(
		battery, LEICAEFI_BAT_MSG_VOLTAGE, val, 0);
}

static int leicaefi_battery_get_temp(struct leicaefi_battery *battery, int *val)
{
	int rv = leicaefi_battery_read_msg(
		battery, LEICAEFI_BAT_MSG_TEMPERATURE, val, 0);
	if (rv == 0) {
		*val -= 2732; // 0.1K to 0.1C (273.15 changed to tenths)
	}
	return rv;
}

static int leicaefi_battery_get_cycle_count(struct leicaefi_battery *battery,
					    int *val)
{
	return leicaefi_battery_read_msg(battery, LEICAEFI_BAT_MSG_CYCLE_COUNT,
					 val, 0);
}

static int leicaefi_battery_get_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 union power_supply_propval *val)
{
	struct leicaefi_battery *battery = power_supply_get_drvdata(psy);

	dev_dbg(&psy->dev, "%s property=%d\n", __func__, psp);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		return leicaefi_battery_is_present(battery, &val->intval);
	case POWER_SUPPLY_PROP_CAPACITY:
		return leicaefi_battery_get_capacity(battery, &val->intval);
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW:
		return leicaefi_battery_get_time_to_empty_now(battery,
							      &val->intval);
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
		return leicaefi_battery_get_time_to_empty_avg(battery,
							      &val->intval);
	case POWER_SUPPLY_PROP_TIME_TO_FULL_AVG:
		return leicaefi_battery_get_time_to_full_avg(battery,
							     &val->intval);
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		return leicaefi_battery_get_current_now(battery, &val->intval);
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		return leicaefi_battery_get_current_avg(battery, &val->intval);
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		return leicaefi_battery_get_voltage_now(battery, &val->intval);
	case POWER_SUPPLY_PROP_TEMP:
		return leicaefi_battery_get_temp(battery, &val->intval);
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		return leicaefi_battery_get_cycle_count(battery, &val->intval);
	default:
		break;
	}

	return -EINVAL;
}

static int leicaefi_battery_set_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 const union power_supply_propval *val)
{
	struct leicaefi_battery *battery = power_supply_get_drvdata(psy);

	dev_dbg(&psy->dev, "%s property=%d\n", __func__, psp);

	return -EINVAL;
}

static int
leicaefi_battery_property_is_writeable(struct power_supply *psy,
				       enum power_supply_property psp)
{
	return 0;
}

static int leicaefi_battery_register(struct leicaefi_power_device *efidev,
				     struct leicaefi_battery *battery,
				     const struct leicaefi_battery_desc *desc)
{
	struct power_supply_config config;

	memset(&config, 0, sizeof(config));

	config.drv_data = battery;

	battery->efidev = efidev;
	battery->desc = desc;

	battery->supply = devm_power_supply_register(
		&efidev->pdev->dev, &battery->desc->kernel_desc, &config);
	if (IS_ERR(battery->supply)) {
		dev_err(&efidev->pdev->dev,
			"Failed to register power supply %s\n",
			battery->desc->kernel_desc.name);
		return PTR_ERR(battery->supply);
	}
	return 0;
}

int leicaefi_power_init_bat1(struct leicaefi_power_device *efidev)
{
	return leicaefi_battery_register(efidev, &efidev->bat1_psy,
					 &leicaefi_bat1_psy_desc);
}
