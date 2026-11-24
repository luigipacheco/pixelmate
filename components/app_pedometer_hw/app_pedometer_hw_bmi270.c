// SPDX-License-Identifier: Apache-2.0
/* BMI270 step counter on a board whose adapter does not own the IMU (the
 * M5Stack StopWatch): this driver sets the sensor up itself on the board's
 * internal I2C bus, found by port number (PIXELMATE_I2C_PORT from the board's
 * pixelmate_setting(I2C_PORT ...)). Accelerometer and gyroscope settings match
 * the ESP-Mosaico board adapter's, so the TOOLS apps read the same scales. */
#include <stdbool.h>
#include <string.h>
#include "app_pedometer_hw.h"
#include "bmi270.h"
#include "driver/i2c_master.h"
#include "esp_rom_sys.h"

#ifndef PIXELMATE_I2C_PORT
#define PIXELMATE_I2C_PORT 0
#endif
#define IMU_TIMEOUT_MS 100
#define IMU_TRANSFER_BYTES 32U

static struct bmi2_dev s_sensor;
static i2c_master_dev_handle_t s_device;
static bool s_started;              /* Owned by the game loop task. */

static BMI2_INTF_RETURN_TYPE imu_read(uint8_t reg, uint8_t *data, uint32_t count, void *ctx)
{
    (void)ctx;
    return i2c_master_transmit_receive(s_device, &reg, 1, data, count, IMU_TIMEOUT_MS) == ESP_OK
           ? BMI2_INTF_RET_SUCCESS : -1;
}

static BMI2_INTF_RETURN_TYPE imu_write(uint8_t reg, const uint8_t *data, uint32_t count, void *ctx)
{
    (void)ctx;
    if (count > IMU_TRANSFER_BYTES) {
        return -1;
    }
    uint8_t bytes[IMU_TRANSFER_BYTES + 1] = {reg};
    memcpy(bytes + 1, data, count);
    return i2c_master_transmit(s_device, bytes, count + 1, IMU_TIMEOUT_MS) == ESP_OK ? BMI2_INTF_RET_SUCCESS : -1;
}

static void imu_delay(uint32_t us, void *ctx)
{
    (void)ctx;
    esp_rom_delay_us(us);
}

esp_err_t app_pedometer_hw_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    if (!s_device) {
        i2c_master_bus_handle_t bus = NULL;
        if (i2c_master_get_bus_handle(PIXELMATE_I2C_PORT, &bus) != ESP_OK) {
            return ESP_ERR_INVALID_STATE;   /* The board has not opened its bus yet. */
        }
        i2c_device_config_t device = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x68, .scl_speed_hz = 400000,
        };
        if (i2c_master_probe(bus, 0x68, IMU_TIMEOUT_MS) != ESP_OK) {
            device.device_address = 0x69;   /* The other strap address. */
        }
        if (i2c_master_bus_add_device(bus, &device, &s_device) != ESP_OK) {
            return ESP_FAIL;
        }
    }
    s_sensor = (struct bmi2_dev) {
        .intf = BMI2_I2C_INTF, .intf_ptr = NULL,
        .read = imu_read, .write = imu_write, .delay_us = imu_delay,
        .read_write_len = IMU_TRANSFER_BYTES,
    };
    if (bmi270_init(&s_sensor) != BMI2_OK) {   /* Uploads the feature config (~8 KB). */
        return ESP_FAIL;
    }
    struct bmi2_sens_config sensors[2] = {{.type = BMI2_ACCEL}, {.type = BMI2_GYRO}};
    if (bmi2_get_sensor_config(sensors, 2, &s_sensor) != BMI2_OK) {
        return ESP_FAIL;
    }
    sensors[0].cfg.acc.odr = BMI2_ACC_ODR_100HZ;
    sensors[0].cfg.acc.range = BMI2_ACC_RANGE_4G;
    sensors[0].cfg.acc.bwp = BMI2_ACC_NORMAL_AVG4;
    sensors[0].cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;
    sensors[1].cfg.gyr.odr = BMI2_GYR_ODR_100HZ;
    sensors[1].cfg.gyr.range = BMI2_GYR_RANGE_2000;
    sensors[1].cfg.gyr.bwp = BMI2_GYR_NORMAL_MODE;
    sensors[1].cfg.gyr.noise_perf = BMI2_POWER_OPT_MODE;
    sensors[1].cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;
    const uint8_t enabled[] = {BMI2_ACCEL, BMI2_GYRO};
    if (bmi2_set_sensor_config(sensors, 2, &s_sensor) != BMI2_OK ||
            bmi2_sensor_enable(enabled, 2, &s_sensor) != BMI2_OK) {
        return ESP_FAIL;
    }
    const uint8_t features[] = {BMI2_STEP_COUNTER};
    if (bmi270_sensor_enable(features, 1, &s_sensor) != BMI2_OK) {
        return ESP_FAIL;
    }
    s_started = true;
    return ESP_OK;
}

esp_err_t app_pedometer_hw_read(uint32_t *steps)
{
    if (!steps) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    struct bmi2_feat_sensor_data data = {.type = BMI2_STEP_COUNTER};
    if (bmi270_get_feature_data(&data, 1, &s_sensor) != BMI2_OK) {
        return ESP_FAIL;
    }
    *steps = data.sens_data.step_counter_output;
    return ESP_OK;
}

esp_err_t app_pedometer_hw_motion(float accel_g[3], float gyro_dps[3])
{
    if (!accel_g || !gyro_dps) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    struct bmi2_sens_data data = {0};
    if (bmi2_get_sensor_data(&data, &s_sensor) != BMI2_OK) {
        return ESP_FAIL;
    }
    const float acc = 4.0f / 32768.0f, gyr = 2000.0f / 32768.0f;   /* The ranges set in start. */
    accel_g[0] = data.acc.x * acc;
    accel_g[1] = data.acc.y * acc;
    accel_g[2] = data.acc.z * acc;
    gyro_dps[0] = data.gyr.x * gyr;
    gyro_dps[1] = data.gyr.y * gyr;
    gyro_dps[2] = data.gyr.z * gyr;
    return ESP_OK;
}

esp_err_t app_pedometer_hw_low_power(void)
{
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t gyro[] = {BMI2_GYRO};
    if (bmi270_sensor_disable(gyro, 1, &s_sensor) != BMI2_OK) {
        return ESP_FAIL;
    }
    struct bmi2_sens_config accel = {.type = BMI2_ACCEL};
    if (bmi2_get_sensor_config(&accel, 1, &s_sensor) != BMI2_OK) {
        return ESP_FAIL;
    }
    accel.cfg.acc.odr = BMI2_ACC_ODR_50HZ;
    accel.cfg.acc.bwp = BMI2_ACC_OSR2_AVG2;
    accel.cfg.acc.filter_perf = BMI2_POWER_OPT_MODE;
    if (bmi2_set_sensor_config(&accel, 1, &s_sensor) != BMI2_OK) {
        return ESP_FAIL;
    }
    return bmi2_set_adv_power_save(BMI2_ENABLE, &s_sensor) == BMI2_OK ? ESP_OK : ESP_FAIL;
}
