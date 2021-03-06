// From https://github.com/MaJerle/stm32f429/blob/master/00-STM32F429_LIBRARIES/tm_stm32f4_ssd1306.c

/**
 * |----------------------------------------------------------------------
 * | Copyright (C) Tilen Majerle, 2015
 * |
 * | This program is free software: you can redistribute it and/or modify
 * | it under the terms of the GNU General Public License as published by
 * | the Free Software Foundation, either version 3 of the License, or
 * | any later version.
 * |
 * | This program is distributed in the hope that it will be useful,
 * | but WITHOUT ANY WARRANTY; without even the implied warranty of
 * | MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * | GNU General Public License for more details.
 * |
 * | You should have received a copy of the GNU General Public License
 * | along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * |----------------------------------------------------------------------
 */

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "tm_stm32f4_fonts.h"
#include "tm_stm32f4_ssd1306.h"
#include "stm32f1xx_hal_i2c.h"

#include "main.h"

#define MODE_I2C (0)
#define MODE_SPI (1)

// Default to I2C, switch to SPI if things don't work.
static uint8_t mode = MODE_I2C;

// Changes to use HAL
#define TM_DELAY_Init() do {} while(0)
#define TM_I2C_Init(a,b,c) do {} while(0)
#define Delayms(x) (vTaskDelay(x/portTICK_PERIOD_MS))
#if !defined(OLED_INTERNAL_DCDC) && !defined(OLED_EXTERNAL_DCDC)
#error Must set display to use either the internal or external DCDC in main.h
#endif
#if defined(OLED_SSD1306)
#define OLED_LOWER_START_COL (0x00)
#define OLED_UPPER_START_COL (0x10)
const uint8_t DISP_INIT[] = {
	0xAE, //display off
	0xD5, //--set display clock divide ratio/oscillator frequency
	0x80, //--set divide ratio
	0xA8, //--set multiplex ratio(1 to 64)
	0x3F, //
	0xD3, //-set display offset
	0x00, //-not offset
	0x40, //--set start line address
	0x8D, //--set DC-DC enable
#ifdef OLED_INTERNAL_DCDC
	0x14, //
#elif defined(OLED_EXTERNAL_DCDC)
	0x04, //
#endif
	0xA1, //--set segment re-map 0 to 127
	0xC8, //Set COM Output Scan Direction
	0xDA, //--set com pins hardware configuration
	0x12,
	0x81, //--set contrast control register
	0xCF,
	0xD9, //--set pre-charge period
#ifdef OLED_INTERNAL_DCDC
	0xF1, //
#elif defined(OLED_EXTERNAL_DCDC)
	0x22, //
#endif
	0xDB, //--set vcomh
	0x40, //0x20,0.77xVcc
	0xA4, //0xa4,Output follows RAM content;0xa5,Output ignores RAM content
	0xA6, //--set normal display
	0x20, //Set Memory Addressing Mode
	0x10, //00,Horizontal Addressing Mode;01,Vertical Addressing Mode;10,Page Addressing Mode (RESET);11,Invalid
	0xB0, //Set Page Start Address for Page Addressing Mode,0-7
	OLED_LOWER_START_COL, //---set low column address
	OLED_UPPER_START_COL //---set high column address
};
#elif defined(OLED_SH1106)
// Initialization commands from https://www.displaymodule.com/products/dm-oled13-625
#define OLED_LOWER_START_COL (0x02)
#define OLED_UPPER_START_COL (0x10)

const uint8_t DISP_INIT[] = {

		0xD5, //--set display clock divide ratio/oscillator frequency
		0x80, // (+15%) (divide by 1)
		0xA8, //--set multiplex ratio(1 to 64)
		0x3F, // (64)
		0xD3, //--set display offset
		0x00, //--no offset
		0x40, //--set start line address
		0xAD, //--Set charge pump
		0x8b, //
		0xA1, //--set segment re-map 0 to 127
		0xC8, //Set COM Output Scan Direction
		0xDA, //--set com pins hardware configuration
		0x12,
		0x81, //--set contrast control register
		0x60,
		0xD9, //--set pre-charge period
		0x22, //
		0xDB, //--set vcomh
		0x40, //0x20,0.77xVcc
		0x32, //-- 8.0 V
		0xA6, //--set normal display (not inverted)

		// And extra commands:
		OLED_LOWER_START_COL, //---set low column address
		OLED_UPPER_START_COL, //---set high column address

		0xB0 //Set Page Start Address for Page Addressing Mode,0-7

};

#endif

SemaphoreHandle_t I2CSemaphore = NULL;
__IO UBaseType_t c = 0;
// SPI stuff
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi) {
	HAL_GPIO_WritePin(SSD_1306_SPI_NSS_Port,SSD_1306_SPI_NSS_Pin,GPIO_PIN_SET);
	static BaseType_t xHigherPriorityTaskWoken;
	xSemaphoreGiveFromISR(I2CSemaphore, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
}
static void WriteSPIByte(uint8_t x) {
	HAL_GPIO_WritePin(SSD_1306_SPI_NSS_Port,SSD_1306_SPI_NSS_Pin,GPIO_PIN_RESET);
	HAL_SPI_Transmit_IT(SSD1306_SPI,&x,1);
	xSemaphoreTake(I2CSemaphore, portMAX_DELAY);
}

/* Write command */
void  SSD1306_WRITECOMMAND(uint8_t command) {
	switch(mode) {
	case MODE_I2C:
		HAL_I2C_Mem_Write(SSD1306_I2C,SSD1306_I2C_ADDR,
				0x00, I2C_MEMADD_SIZE_8BIT,
				&command,1,
				100);
		break;
	case MODE_SPI:
		HAL_GPIO_WritePin(SSD1306_DC_Port,SSD1306_DC_Pin,GPIO_PIN_RESET);
		WriteSPIByte(command);
		break;
	}
	Delayms(5);
}

void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *hi2c) {

	static BaseType_t xHigherPriorityTaskWoken;

	xSemaphoreGiveFromISR(I2CSemaphore, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR( xHigherPriorityTaskWoken );

}
void TM_I2C_WriteMulti(I2C_HandleTypeDef *hi2c, uint16_t DevAddress,
		uint8_t reg, uint8_t *buf, uint16_t count) {
	HAL_StatusTypeDef status = HAL_I2C_Mem_Write_DMA(hi2c, DevAddress,
			reg, I2C_MEMADD_SIZE_8BIT,
			buf,count);
	if(status != HAL_OK)
		while(1) ;
	xSemaphoreTake(I2CSemaphore, portMAX_DELAY);
}
void SSD1306_WRITEDATA(uint8_t *buf, uint16_t count) {
	switch(mode) {
	case MODE_I2C:
		TM_I2C_WriteMulti(SSD1306_I2C, SSD1306_I2C_ADDR, 0x40, buf,count);
		break;
	case MODE_SPI:
		HAL_GPIO_WritePin(SSD1306_DC_Port,SSD1306_DC_Pin,GPIO_PIN_SET);
		for(int i=0; i<count; i++) {
			WriteSPIByte(buf[i]);
		}
		break;
	}

}
uint8_t TM_I2C_IsDeviceConnected(I2C_HandleTypeDef *hi2c, uint8_t addr) {
	HAL_StatusTypeDef ready = HAL_I2C_IsDeviceReady(hi2c, addr, 3, 500);
	return ready == HAL_OK;
}

/* Write data */
//#define SSD1306_WRITEDATA(data)            TM_I2C_Write(SSD1306_I2C, SSD1306_I2C_ADDR, 0x40, (data))
/* Absolute value */
#define ABS(x)   ((x) > 0 ? (x) : -(x))

/* SSD1306 data buffer */
static uint8_t SSD1306_Buffer[SSD1306_WIDTH * SSD1306_HEIGHT / 8];

/* Private SSD1306 structure */
typedef struct {
	uint16_t CurrentX;
	uint16_t CurrentY;
	uint8_t Inverted;
	uint8_t Initialized;
} SSD1306_t;

/* Private variable */
static SSD1306_t SSD1306;
uint8_t TM_SSD1306_Init(void) {
	/* Init delay */
	TM_DELAY_Init();
	if(I2CSemaphore == NULL)
		I2CSemaphore = xSemaphoreCreateBinary();
	configASSERT( I2CSemaphore );
	c = uxSemaphoreGetCount(I2CSemaphore);
	/* Init I2C */
	TM_I2C_Init(SSD1306_I2C, SSD1306_I2C_PINSPACK, 400000);

	/* Check if LCD connected to I2C */
	if (!TM_I2C_IsDeviceConnected(SSD1306_I2C, SSD1306_I2C_ADDR)) {
		/* Default to SPI */
		mode = MODE_SPI;
		/* And reset the display.... */
		HAL_GPIO_WritePin(SSD1306_DC_Port,SSD1306_DC_Pin,GPIO_PIN_RESET);
		Delayms(100);
		HAL_GPIO_WritePin(SPI1_nRST_GPIO_Port,SPI1_nRST_Pin,GPIO_PIN_SET);
	}

	/* A little delay */
	Delayms(100);

	/* Init LCD */
	for(int i=0; i<sizeof(DISP_INIT); i++) {
		SSD1306_WRITECOMMAND(DISP_INIT[i]);
	}

	/* Clear screen */
	//TM_SSD1306_Fill(SSD1306_COLOR_BLACK);

	/* Update screen */
	TM_SSD1306_UpdateScreen();

	SSD1306_WRITECOMMAND(0xAF); //--turn on SSD1306 panel

	/* Set default values */
	SSD1306.CurrentX = 0;
	SSD1306.CurrentY = 0;

	/* Initialized OK */
	SSD1306.Initialized = 1;

	/* Return OK */
	return 1;
}

void TM_SSD1306_UpdateScreen(void) {
	uint8_t m;

	for (m = 0; m < 8; m++) {
		SSD1306_WRITECOMMAND(0xB0 + m); // Page m
		SSD1306_WRITECOMMAND(OLED_LOWER_START_COL);     // lower start col 0x00
		SSD1306_WRITECOMMAND(OLED_UPPER_START_COL);     // upper start col 0x00

		/* Write multi data */
		SSD1306_WRITEDATA(&SSD1306_Buffer[SSD1306_WIDTH * m], SSD1306_WIDTH);
	}
}

void TM_SSD1306_ToggleInvert(void) {
	uint16_t i;

	/* Toggle invert */
	SSD1306.Inverted = !SSD1306.Inverted;

	/* Do memory toggle */
	for (i = 0; i < sizeof(SSD1306_Buffer); i++) {
		SSD1306_Buffer[i] = ~SSD1306_Buffer[i];
	}
}

void TM_SSD1306_Fill(SSD1306_COLOR_t color) {
	/* Set memory */
	memset(SSD1306_Buffer, (color == SSD1306_COLOR_BLACK) ? 0x00 : 0xFF, sizeof(SSD1306_Buffer));
}

void TM_SSD1306_DrawPixel(uint16_t x, uint16_t y, SSD1306_COLOR_t color) {
	if (
		x >= SSD1306_WIDTH ||
		y >= SSD1306_HEIGHT
	) {
		/* Error */
		return;
	}

	/* Check if pixels are inverted */
	if (SSD1306.Inverted) {
		color = (SSD1306_COLOR_t)!color;
	}

	/* Set color */
	if (color == SSD1306_COLOR_WHITE) {
		SSD1306_Buffer[x + (y / 8) * SSD1306_WIDTH] |= 1 << (y % 8);
	} else {
		SSD1306_Buffer[x + (y / 8) * SSD1306_WIDTH] &= ~(1 << (y % 8));
	}
}

void TM_SSD1306_GotoXY(uint16_t x, uint16_t y) {
	/* Set write pointers */
	SSD1306.CurrentX = x;
	SSD1306.CurrentY = y;
}

char TM_SSD1306_Putc(const char ch, TM_FontDef_t* Font, SSD1306_COLOR_t color) {
	uint32_t i, b, j;

	/* Check available space in LCD */
	if (
		SSD1306_WIDTH <= (SSD1306.CurrentX + Font->FontWidth) ||
		SSD1306_HEIGHT <= (SSD1306.CurrentY + Font->FontHeight)
	) {
		/* Error */
		return 0;
	}

	/* Go through font */
	for (i = 0; i < Font->FontHeight; i++) {
		b = Font->data[(ch - 32) * Font->FontHeight + i];
		for (j = 0; j < Font->FontWidth; j++) {
			if ((b << j) & 0x8000) {
				TM_SSD1306_DrawPixel(SSD1306.CurrentX + j, (SSD1306.CurrentY + i), (SSD1306_COLOR_t) color);
			} else {
				TM_SSD1306_DrawPixel(SSD1306.CurrentX + j, (SSD1306.CurrentY + i), (SSD1306_COLOR_t)!color);
			}
		}
	}

	/* Increase pointer */
	SSD1306.CurrentX += Font->FontWidth;

	/* Return character written */
	return ch;
}

char TM_SSD1306_Puts(const char* str, TM_FontDef_t* Font, SSD1306_COLOR_t color) {
	/* Write characters */
	while (*str) {
		/* Write character by character */
		if (TM_SSD1306_Putc(*str, Font, color) != *str) {
			/* Return error */
			return *str;
		}

		/* Increase string pointer */
		str++;
	}

	/* Everything OK, zero should be returned */
	return *str;
}


void TM_SSD1306_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, SSD1306_COLOR_t c) {
	int16_t dx, dy, sx, sy, err, e2, i, tmp;

	/* Check for overflow */
	if (x0 >= SSD1306_WIDTH) {
		x0 = SSD1306_WIDTH - 1;
	}
	if (x1 >= SSD1306_WIDTH) {
		x1 = SSD1306_WIDTH - 1;
	}
	if (y0 >= SSD1306_HEIGHT) {
		y0 = SSD1306_HEIGHT - 1;
	}
	if (y1 >= SSD1306_HEIGHT) {
		y1 = SSD1306_HEIGHT - 1;
	}

	dx = (x0 < x1) ? (x1 - x0) : (x0 - x1);
	dy = (y0 < y1) ? (y1 - y0) : (y0 - y1);
	sx = (x0 < x1) ? 1 : -1;
	sy = (y0 < y1) ? 1 : -1;
	err = ((dx > dy) ? dx : -dy) / 2;

	if (dx == 0) {
		if (y1 < y0) {
			tmp = y1;
			y1 = y0;
			y0 = tmp;
		}

		if (x1 < x0) {
			tmp = x1;
			x1 = x0;
			x0 = tmp;
		}

		/* Vertical line */
		for (i = y0; i <= y1; i++) {
			TM_SSD1306_DrawPixel(x0, i, c);
		}

		/* Return from function */
		return;
	}

	if (dy == 0) {
		if (y1 < y0) {
			tmp = y1;
			y1 = y0;
			y0 = tmp;
		}

		if (x1 < x0) {
			tmp = x1;
			x1 = x0;
			x0 = tmp;
		}

		/* Horizontal line */
		for (i = x0; i <= x1; i++) {
			TM_SSD1306_DrawPixel(i, y0, c);
		}

		/* Return from function */
		return;
	}

	while (1) {
		TM_SSD1306_DrawPixel(x0, y0, c);
		if (x0 == x1 && y0 == y1) {
			break;
		}
		e2 = err;
		if (e2 > -dx) {
			err -= dy;
			x0 += sx;
		}
		if (e2 < dy) {
			err += dx;
			y0 += sy;
		}
	}
}

void TM_SSD1306_DrawRectangle(uint16_t x, uint16_t y, uint16_t w, uint16_t h, SSD1306_COLOR_t c) {
	/* Check input parameters */
	if (
		x >= SSD1306_WIDTH ||
		y >= SSD1306_HEIGHT
	) {
		/* Return error */
		return;
	}

	/* Check width and height */
	if ((x + w) >= SSD1306_WIDTH) {
		w = SSD1306_WIDTH - x;
	}
	if ((y + h) >= SSD1306_HEIGHT) {
		h = SSD1306_HEIGHT - y;
	}

	/* Draw 4 lines */
	TM_SSD1306_DrawLine(x, y, x + w, y, c);         /* Top line */
	TM_SSD1306_DrawLine(x, y + h, x + w, y + h, c); /* Bottom line */
	TM_SSD1306_DrawLine(x, y, x, y + h, c);         /* Left line */
	TM_SSD1306_DrawLine(x + w, y, x + w, y + h, c); /* Right line */
}

void TM_SSD1306_DrawFilledRectangle(uint16_t x, uint16_t y, uint16_t w, uint16_t h, SSD1306_COLOR_t c) {
	uint8_t i;

	/* Check input parameters */
	if (
		x >= SSD1306_WIDTH ||
		y >= SSD1306_HEIGHT
	) {
		/* Return error */
		return;
	}

	/* Check width and height */
	if ((x + w) >= SSD1306_WIDTH) {
		w = SSD1306_WIDTH - x;
	}
	if ((y + h) >= SSD1306_HEIGHT) {
		h = SSD1306_HEIGHT - y;
	}

	/* Draw lines */
	for (i = 0; i <= h; i++) {
		/* Draw lines */
		TM_SSD1306_DrawLine(x, y + i, x + w, y + i, c);
	}
}

void TM_SSD1306_DrawTriangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, SSD1306_COLOR_t color) {
	/* Draw lines */
	TM_SSD1306_DrawLine(x1, y1, x2, y2, color);
	TM_SSD1306_DrawLine(x2, y2, x3, y3, color);
	TM_SSD1306_DrawLine(x3, y3, x1, y1, color);
}


void TM_SSD1306_DrawFilledTriangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, SSD1306_COLOR_t color) {
	int16_t deltax = 0, deltay = 0, x = 0, y = 0, xinc1 = 0, xinc2 = 0,
	yinc1 = 0, yinc2 = 0, den = 0, num = 0, numadd = 0, numpixels = 0,
	curpixel = 0;

	deltax = ABS(x2 - x1);
	deltay = ABS(y2 - y1);
	x = x1;
	y = y1;

	if (x2 >= x1) {
		xinc1 = 1;
		xinc2 = 1;
	} else {
		xinc1 = -1;
		xinc2 = -1;
	}

	if (y2 >= y1) {
		yinc1 = 1;
		yinc2 = 1;
	} else {
		yinc1 = -1;
		yinc2 = -1;
	}

	if (deltax >= deltay){
		xinc1 = 0;
		yinc2 = 0;
		den = deltax;
		num = deltax / 2;
		numadd = deltay;
		numpixels = deltax;
	} else {
		xinc2 = 0;
		yinc1 = 0;
		den = deltay;
		num = deltay / 2;
		numadd = deltax;
		numpixels = deltay;
	}

	for (curpixel = 0; curpixel <= numpixels; curpixel++) {
		TM_SSD1306_DrawLine(x, y, x3, y3, color);

		num += numadd;
		if (num >= den) {
			num -= den;
			x += xinc1;
			y += yinc1;
		}
		x += xinc2;
		y += yinc2;
	}
}

void TM_SSD1306_DrawCircle(int16_t x0, int16_t y0, int16_t r, SSD1306_COLOR_t c) {
	int16_t f = 1 - r;
	int16_t ddF_x = 1;
	int16_t ddF_y = -2 * r;
	int16_t x = 0;
	int16_t y = r;

    TM_SSD1306_DrawPixel(x0, y0 + r, c);
    TM_SSD1306_DrawPixel(x0, y0 - r, c);
    TM_SSD1306_DrawPixel(x0 + r, y0, c);
    TM_SSD1306_DrawPixel(x0 - r, y0, c);

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        TM_SSD1306_DrawPixel(x0 + x, y0 + y, c);
        TM_SSD1306_DrawPixel(x0 - x, y0 + y, c);
        TM_SSD1306_DrawPixel(x0 + x, y0 - y, c);
        TM_SSD1306_DrawPixel(x0 - x, y0 - y, c);

        TM_SSD1306_DrawPixel(x0 + y, y0 + x, c);
        TM_SSD1306_DrawPixel(x0 - y, y0 + x, c);
        TM_SSD1306_DrawPixel(x0 + y, y0 - x, c);
        TM_SSD1306_DrawPixel(x0 - y, y0 - x, c);
    }
}

void TM_SSD1306_DrawFilledCircle(int16_t x0, int16_t y0, int16_t r, SSD1306_COLOR_t c) {
	int16_t f = 1 - r;
	int16_t ddF_x = 1;
	int16_t ddF_y = -2 * r;
	int16_t x = 0;
	int16_t y = r;

    TM_SSD1306_DrawPixel(x0, y0 + r, c);
    TM_SSD1306_DrawPixel(x0, y0 - r, c);
    TM_SSD1306_DrawPixel(x0 + r, y0, c);
    TM_SSD1306_DrawPixel(x0 - r, y0, c);
    TM_SSD1306_DrawLine(x0 - r, y0, x0 + r, y0, c);

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        TM_SSD1306_DrawLine(x0 - x, y0 + y, x0 + x, y0 + y, c);
        TM_SSD1306_DrawLine(x0 + x, y0 - y, x0 - x, y0 - y, c);

        TM_SSD1306_DrawLine(x0 + y, y0 + x, x0 - y, y0 + x, c);
        TM_SSD1306_DrawLine(x0 + y, y0 - x, x0 - y, y0 - x, c);
    }
}

void SSD1306_ON(void) {
	SSD1306_WRITECOMMAND(0x8D);
	SSD1306_WRITECOMMAND(0x14);
	SSD1306_WRITECOMMAND(0xAF);
}
void SSD1306_OFF(void) {
	SSD1306_WRITECOMMAND(0x8D);
	SSD1306_WRITECOMMAND(0x10);
	SSD1306_WRITECOMMAND(0xAE);
}
