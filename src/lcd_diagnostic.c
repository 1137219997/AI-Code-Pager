#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

#define LCD_WIDTH  320U
#define LCD_HEIGHT 170U

#define ST7789_SLPOUT  0x11
#define ST7789_INVON   0x21
#define ST7789_DISPON  0x29
#define ST7789_CASET   0x2A
#define ST7789_RASET   0x2B
#define ST7789_RAMWR   0x2C
#define ST7789_MADCTL  0x36
#define ST7789_COLMOD  0x3A

static const struct device *const spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi1));
static const struct gpio_dt_spec lcd_dc =
	GPIO_DT_SPEC_GET(DT_PATH(mipi_dbi), dc_gpios);
static const struct gpio_dt_spec lcd_reset =
	GPIO_DT_SPEC_GET(DT_PATH(mipi_dbi), reset_gpios);
static const struct gpio_dt_spec lcd_cs =
	GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(spi1), cs_gpios, 0);

/*
 * The vendor STM32 hardware-SPI example uses CPOL=1/CPHA=1. Start slowly so
 * jumper wires and breadboards are not being tested at their signal limit.
 */
static const struct spi_config spi_cfg = {
	.frequency = 1000000U,
	.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB |
		     SPI_MODE_CPOL | SPI_MODE_CPHA,
	.slave = 0,
};

static int spi_send(const uint8_t *data, size_t length)
{
	struct spi_buf buffer = {
		.buf = (void *)data,
		.len = length,
	};
	const struct spi_buf_set buffers = {
		.buffers = &buffer,
		.count = 1,
	};

	return spi_write(spi_dev, &spi_cfg, &buffers);
}

static int lcd_command(uint8_t command, const uint8_t *data, size_t length)
{
	int err;

	/* gpio_pin_set_dt() takes logical values; CS is active-low. */
	(void)gpio_pin_set_dt(&lcd_cs, 1);
	(void)gpio_pin_set_dt(&lcd_dc, 0);
	err = spi_send(&command, sizeof(command));

	if ((err == 0) && (length != 0U)) {
		(void)gpio_pin_set_dt(&lcd_dc, 1);
		err = spi_send(data, length);
	}

	(void)gpio_pin_set_dt(&lcd_cs, 0);
	return err;
}

static int lcd_initialize(void)
{
	static const uint8_t madctl[] = { 0x70 };
	static const uint8_t colmod[] = { 0x05 };
	static const uint8_t porch[] = { 0x0C, 0x0C, 0x00, 0x33, 0x33 };
	static const uint8_t gctrl[] = { 0x35 };
	static const uint8_t vcom[] = { 0x1A };
	static const uint8_t lcm[] = { 0x2C };
	static const uint8_t vdv_vrh_en[] = { 0x01 };
	static const uint8_t vrhs[] = { 0x0B };
	static const uint8_t vdvs[] = { 0x20 };
	static const uint8_t frctrl2[] = { 0x0F };
	static const uint8_t pwctrl1[] = { 0xA4, 0xA1 };
	static const uint8_t pvgam[] = {
		0x00, 0x03, 0x07, 0x08, 0x07, 0x15, 0x2A,
		0x44, 0x42, 0x0A, 0x17, 0x18, 0x25, 0x27,
	};
	static const uint8_t nvgam[] = {
		0x00, 0x03, 0x08, 0x07, 0x07, 0x23, 0x2A,
		0x43, 0x42, 0x09, 0x18, 0x17, 0x25, 0x27,
	};
	int err;

	/* Match the module example: reset low 30 ms, then settle for 120 ms. */
	(void)gpio_pin_set_dt(&lcd_reset, 1);
	k_msleep(30);
	(void)gpio_pin_set_dt(&lcd_reset, 0);
	k_msleep(120);

	err = lcd_command(ST7789_SLPOUT, NULL, 0);
	if (err != 0) {
		return err;
	}
	k_msleep(120);

#define SEND(command_, data_)                                                    \
	do {                                                                       \
		err = lcd_command((command_), (data_), sizeof(data_));               \
		if (err != 0) {                                                      \
			return err;                                                    \
		}                                                                      \
	} while (false)

	SEND(ST7789_MADCTL, madctl);
	SEND(ST7789_COLMOD, colmod);
	SEND(0xB2, porch);
	SEND(0xB7, gctrl);
	SEND(0xBB, vcom);
	SEND(0xC0, lcm);
	SEND(0xC2, vdv_vrh_en);
	SEND(0xC3, vrhs);
	SEND(0xC4, vdvs);
	SEND(0xC6, frctrl2);
	SEND(0xD0, pwctrl1);

	err = lcd_command(ST7789_INVON, NULL, 0);
	if (err != 0) {
		return err;
	}

	SEND(0xE0, pvgam);
	SEND(0xE1, nvgam);

#undef SEND

	err = lcd_command(ST7789_DISPON, NULL, 0);
	k_msleep(100);
	return err;
}

static int lcd_fill(uint16_t rgb565)
{
	static const uint8_t columns[] = { 0x00, 0x00, 0x01, 0x3F };
	static const uint8_t rows[] = { 0x00, 0x23, 0x00, 0xCC };
	uint8_t pixels[128];
	uint32_t remaining = LCD_WIDTH * LCD_HEIGHT;
	int err;

	err = lcd_command(ST7789_CASET, columns, sizeof(columns));
	if (err != 0) {
		return err;
	}
	err = lcd_command(ST7789_RASET, rows, sizeof(rows));
	if (err != 0) {
		return err;
	}

	for (size_t index = 0; index < sizeof(pixels); index += 2U) {
		pixels[index] = (uint8_t)(rgb565 >> 8);
		pixels[index + 1U] = (uint8_t)rgb565;
	}

	(void)gpio_pin_set_dt(&lcd_cs, 1);
	(void)gpio_pin_set_dt(&lcd_dc, 0);
	{
		const uint8_t command = ST7789_RAMWR;

		err = spi_send(&command, sizeof(command));
	}
	(void)gpio_pin_set_dt(&lcd_dc, 1);

	while ((err == 0) && (remaining != 0U)) {
		const uint32_t batch = MIN(remaining, (uint32_t)(sizeof(pixels) / 2U));

		err = spi_send(pixels, batch * 2U);
		remaining -= batch;
	}

	(void)gpio_pin_set_dt(&lcd_cs, 0);
	return err;
}

int main(void)
{
	static const uint16_t colors[] = {
		0xF800, /* red */
		0x07E0, /* green */
		0x001F, /* blue */
		0xFFFF, /* white */
	};
	size_t color_index = 0;

	if (!device_is_ready(spi_dev) || !gpio_is_ready_dt(&lcd_dc) ||
	    !gpio_is_ready_dt(&lcd_reset) || !gpio_is_ready_dt(&lcd_cs)) {
		return -ENODEV;
	}

	if ((gpio_pin_configure_dt(&lcd_dc, GPIO_OUTPUT_INACTIVE) != 0) ||
	    (gpio_pin_configure_dt(&lcd_reset, GPIO_OUTPUT_INACTIVE) != 0) ||
	    (gpio_pin_configure_dt(&lcd_cs, GPIO_OUTPUT_INACTIVE) != 0)) {
		return -EIO;
	}

	/* P0.13's GPIO hog has enabled board-edge VCC by this point. */
	k_msleep(150);

	for (;;) {
		if (lcd_initialize() != 0) {
			k_msleep(1000);
			continue;
		}

		while (lcd_fill(colors[color_index]) == 0) {
			color_index = (color_index + 1U) % ARRAY_SIZE(colors);
			k_msleep(1500);
		}
	}

	return 0;
}
