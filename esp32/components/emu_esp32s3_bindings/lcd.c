//Main parallel RGB LCD driver
/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include <string.h>
#include <lcdbackboard.h>
#include "io.h"

static const char *TAG = "rgblcd";

#define LCD_PIXEL_CLOCK_HZ	   (20 * 1000 * 1000)
#define PIN_NUM_HSYNC		   0
#define PIN_NUM_VSYNC		   35
#define PIN_NUM_DE			   36
#define PIN_NUM_PCLK		   37
#define PIN_NUM_DATA0		   45  // B1
#define PIN_NUM_DATA1		   48  // B2
#define PIN_NUM_DATA2		   47  // B3 - note, relocate
#define PIN_NUM_DATA3		   21  // B4
#define PIN_NUM_DATA4		   14  // B5 - note, relocate
#define PIN_NUM_DATA5		   13  // G0
#define PIN_NUM_DATA6		   12 // G1 - note, relocate
#define PIN_NUM_DATA7		   11 // G2
#define PIN_NUM_DATA8		   10 // G3
#define PIN_NUM_DATA9		   9 // G4
#define PIN_NUM_DATA10		   46 // G5
#define PIN_NUM_DATA11		   3 // R1
#define PIN_NUM_DATA12		   8 // R2
#define PIN_NUM_DATA13		   18 // R3
#define PIN_NUM_DATA14		   17 // R4
#define PIN_NUM_DATA15		   16 // R5
#define PIN_NUM_DISP_EN		   -1

#define LCD_HOST SPI2_HOST
#define PIN_NUM_MISO -1
#define PIN_NUM_MOSI 4
#define PIN_NUM_CLK	 5
#define PIN_NUM_CS	 6

// The pixel number in horizontal and vertical
#define LCD_V_RES			   640
#define LCD_H_RES			   360

//Send a command or data word to the main LCD.
static void lcd_spi(spi_device_handle_t spi, int dc, uint8_t word) {
	esp_err_t ret;
	spi_transaction_t t;
	memset(&t, 0, sizeof(t));		//Zero out the transaction
	t.length=8;
	t.addr=dc;
	t.tx_buffer=&word;				 //The data is the cmd itself
	ret=spi_device_polling_transmit(spi, &t);  //Transmit!
	assert(ret==ESP_OK);			//Should have had no issues.
}

//Init sequence for the main LCD. Obtained from the guy I bought it from.
//There's deeper magic in there, I have no idea what half of the writes
//do, but it works.
static void lcd_init_panel(spi_device_handle_t spi) {
	lcd_spi(spi, 0, 0x1); //sw reset
	vTaskDelay(pdMS_TO_TICKS(500));
	lcd_spi(spi, 0, 0x11); //sleep out
	vTaskDelay(pdMS_TO_TICKS(120));

	lcd_spi(spi, 0, 0xFF); //command 2bank sel
	lcd_spi(spi, 1, 0x77);
	lcd_spi(spi, 1, 0x01);
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x13); //bank 2 ena, select bk3

	lcd_spi(spi, 0, 0xE8);//unknown
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x0C);
	vTaskDelay(pdMS_TO_TICKS(10));

	lcd_spi(spi, 0, 0xE8);//unknown
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x00);

	lcd_spi(spi, 0, 0xFF); //command 2 bank sel
	lcd_spi(spi, 1, 0x77);
	lcd_spi(spi, 1, 0x01);
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x00); //select bk0, dis bk2


	lcd_spi(spi, 0, 0xFF); //command 2 bk sel
	lcd_spi(spi, 1, 0x77);
	lcd_spi(spi, 1, 0x01);
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x10); //enable bk2, sel bk0

	lcd_spi(spi, 0, 0xC0); //Line setting
	lcd_spi(spi, 1, 0x4F); //640 lines
	lcd_spi(spi, 1, 0x00);

	lcd_spi(spi, 0, 0xC1); //porch ctl
	lcd_spi(spi, 1, 0x07); //vert front porch
	lcd_spi(spi, 1, 0x02); //vert back porch

	lcd_spi(spi, 0, 0xC2); //Inversion selection & frame rate ctl
	lcd_spi(spi, 1, 0x31); //2dot inversion
	lcd_spi(spi, 1, 0x05); //min 592 pix/line

	lcd_spi(spi, 0, 0xC3);	   //RGB control
	lcd_spi(spi, 1, 0x80);	   //7=de/~hv, 3=vert pol, 2=hor pol, 1=dotclk pol, 0=enable pol
	lcd_spi(spi, 1, 2);		  //vert back porch
	lcd_spi(spi, 1, 2);		  //hor back porch

	lcd_spi(spi, 0, 0xC6);	 //undocumented?
	lcd_spi(spi, 1, 0x01);

	lcd_spi(spi, 0, 0xCC);	//undocumented?
	lcd_spi(spi, 1, 0x18); 

	lcd_spi(spi, 0, 0xCD); //Color control
	lcd_spi(spi, 1, 0x08); //pixels from db[17..0]

	lcd_spi(spi, 0, 0xB0); //Positive voltage gamma ctl
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x0A);
	lcd_spi(spi, 1, 0x11);
	lcd_spi(spi, 1, 0x0C);
	lcd_spi(spi, 1, 0x10);
	lcd_spi(spi, 1, 0x05);
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x08);
	lcd_spi(spi, 1, 0x08);
	lcd_spi(spi, 1, 0x1F);
	lcd_spi(spi, 1, 0x07);
	lcd_spi(spi, 1, 0x13);
	lcd_spi(spi, 1, 0x10);
	lcd_spi(spi, 1, 0xA9);
	lcd_spi(spi, 1, 0x30);
	lcd_spi(spi, 1, 0x18);

	lcd_spi(spi, 0, 0xB1); //Negative voltage gamma
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x0B);
	lcd_spi(spi, 1, 0x11);
	lcd_spi(spi, 1, 0x0D);
	lcd_spi(spi, 1, 0x0F);
	lcd_spi(spi, 1, 0x05);
	lcd_spi(spi, 1, 0x02);
	lcd_spi(spi, 1, 0x07);
	lcd_spi(spi, 1, 0x06);
	lcd_spi(spi, 1, 0x20);
	lcd_spi(spi, 1, 0x05);
	lcd_spi(spi, 1, 0x15);
	lcd_spi(spi, 1, 0x13);
	lcd_spi(spi, 1, 0xA9);
	lcd_spi(spi, 1, 0x30);
	lcd_spi(spi, 1, 0x18);

	lcd_spi(spi, 0, 0xFF); //Command2 bk sel
	lcd_spi(spi, 1, 0x77);
	lcd_spi(spi, 1, 0x01);
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x11); //Enable command2, sel bk1

	lcd_spi(spi, 0, 0xB0);	//Vop amplitude setting
	lcd_spi(spi, 1, 0x53);

	lcd_spi(spi, 0, 0xB1); //Vcom amplitude setting
	lcd_spi(spi, 1, 0x60);

	lcd_spi(spi, 0, 0xB2);	//VGH voltage setting
	lcd_spi(spi, 1, 0x87);

//	  lcd_spi(spi, 0, 0xB3); //TESTCMD
//	  lcd_spi(spi, 1, 0x80);

	lcd_spi(spi, 0, 0xB5); //VGL voltage setting
	lcd_spi(spi, 1, 0x49);

	lcd_spi(spi, 0, 0xB7); //Power control 1
	lcd_spi(spi, 1, 0x85);

	lcd_spi(spi, 0, 0xB8); //Power control 2
	lcd_spi(spi, 1, 0x21);

	lcd_spi(spi, 0, 0xC1); //Source pre-drive
	lcd_spi(spi, 1, 0x78);

	lcd_spi(spi, 0, 0xC2); //Source EQ2
	lcd_spi(spi, 1, 0x78);

	vTaskDelay(pdMS_TO_TICKS(100));

	lcd_spi(spi, 0, 0xE0); //Undocumented
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x02);

	lcd_spi(spi, 0, 0xE1); //Undocumented
	lcd_spi(spi, 1, 0x03);
	lcd_spi(spi, 1, 0xA0);
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x02);
	lcd_spi(spi, 1, 0xA0);
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x33);
	lcd_spi(spi, 1, 0x33);

	lcd_spi(spi, 0, 0xE2); //Undocumented
	lcd_spi(spi, 1, 0x22);
	lcd_spi(spi, 1, 0x22);
	lcd_spi(spi, 1, 0x33);
	lcd_spi(spi, 1, 0x33);
	lcd_spi(spi, 1, 0x88);
	lcd_spi(spi, 1, 0xA0);
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x87);
	lcd_spi(spi, 1, 0xA0);
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x00);

	lcd_spi(spi, 0, 0xE3); //Undocumented
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x22);
	lcd_spi(spi, 1, 0x22);

	lcd_spi(spi, 0, 0xE4); //Undocumented
	lcd_spi(spi, 1, 0x44);
	lcd_spi(spi, 1, 0x44);

	lcd_spi(spi, 0, 0xE5); //Undocumented
	lcd_spi(spi, 1, 0x04);
	lcd_spi(spi, 1, 0x84);
	lcd_spi(spi, 1, 0xA0);
	lcd_spi(spi, 1, 0xA0);
	lcd_spi(spi, 1, 0x06);
	lcd_spi(spi, 1, 0x86);
	lcd_spi(spi, 1, 0xA0);
	lcd_spi(spi, 1, 0xA0);
	lcd_spi(spi, 1, 0x08);
	lcd_spi(spi, 1, 0x88);
	lcd_spi(spi, 1, 0xA0);
	lcd_spi(spi, 1, 0xA0);
	lcd_spi(spi, 1, 0x0A);
	lcd_spi(spi, 1, 0x8A);
	lcd_spi(spi, 1, 0xA0);
	lcd_spi(spi, 1, 0xA0);

	lcd_spi(spi, 0, 0xE6); //Undocumented
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x22);
	lcd_spi(spi, 1, 0x22);

	lcd_spi(spi, 0, 0xE7); //Undocumented
	lcd_spi(spi, 1, 0x44);
	lcd_spi(spi, 1, 0x44);

	lcd_spi(spi, 0, 0xE8); //Undocumented
	lcd_spi(spi, 1, 0x03);
	lcd_spi(spi, 1, 0x83);
	lcd_spi(spi, 1, 0xA0);
	lcd_spi(spi, 1, 0xA0);
	lcd_spi(spi, 1, 0x05);
	lcd_spi(spi, 1, 0x85);
	lcd_spi(spi, 1, 0xA0);
	lcd_spi(spi, 1, 0xA0);
	lcd_spi(spi, 1, 0x07);
	lcd_spi(spi, 1, 0x87);
	lcd_spi(spi, 1, 0xA0);
	lcd_spi(spi, 1, 0xA0);
	lcd_spi(spi, 1, 0x09);
	lcd_spi(spi, 1, 0x89);
	lcd_spi(spi, 1, 0xA0);
	lcd_spi(spi, 1, 0xA0);

	lcd_spi(spi, 0, 0xEB); //Undocumented
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x01);
	lcd_spi(spi, 1, 0xE4);
	lcd_spi(spi, 1, 0xE4);
	lcd_spi(spi, 1, 0x88);
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x40);

	lcd_spi(spi, 0, 0xEC); //Undocumented
	lcd_spi(spi, 1, 0x3C);
	lcd_spi(spi, 1, 0x01);

	lcd_spi(spi, 0, 0xED); //Undocumented
	lcd_spi(spi, 1, 0xAB);
	lcd_spi(spi, 1, 0x89);
	lcd_spi(spi, 1, 0x76);
	lcd_spi(spi, 1, 0x54);
	lcd_spi(spi, 1, 0x02);
	lcd_spi(spi, 1, 0xFF);
	lcd_spi(spi, 1, 0xFF);
	lcd_spi(spi, 1, 0xFF);
	lcd_spi(spi, 1, 0xFF);
	lcd_spi(spi, 1, 0xFF);
	lcd_spi(spi, 1, 0xFF);
	lcd_spi(spi, 1, 0x20);
	lcd_spi(spi, 1, 0x45);
	lcd_spi(spi, 1, 0x67);
	lcd_spi(spi, 1, 0x98);
	lcd_spi(spi, 1, 0xBA);

	lcd_spi(spi, 0, 0xEF); //Undocumented
	lcd_spi(spi, 1, 0x08);
	lcd_spi(spi, 1, 0x08);
	lcd_spi(spi, 1, 0x08);
	lcd_spi(spi, 1, 0x08);
	lcd_spi(spi, 1, 0x3F);
	lcd_spi(spi, 1, 0x1F);

	lcd_spi(spi, 0, 0xFF);//Command 2 bank sel
	lcd_spi(spi, 1, 0x77);
	lcd_spi(spi, 1, 0x01);
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x00);
	lcd_spi(spi, 1, 0x00);//Bk 0, disable command2

	lcd_spi(spi, 0, 0x3A);
	lcd_spi(spi, 1, 0x66); //16bpp

	lcd_spi(spi, 0,0x11);  //slpout
	lcd_spi(spi, 0, 0x29); //dispon
	vTaskDelay(pdMS_TO_TICKS(20));
}

//Current frame buffer pointer, palette and pitch, for use in actual displaying
static uint8_t *cur_fb;
static uint16_t cur_pal[256];
static int cur_pitch;

//Fill a bounce buffer with 16-bit data based on the VGA image and palette.
//note: assumes buffers that start at a line and are an integer number
//of lines
static bool IRAM_ATTR fill_fb(esp_lcd_panel_handle_t panel, void *bounce_buf, int pos_px, int len_bytes, void *arg) {
	if (!cur_fb) return false;
	int len_px=len_bytes/2;
	int ypos=pos_px/360;
	uint16_t *out=(uint16_t*)bounce_buf;
	uint8_t *p=&cur_fb[cur_pitch*ypos];
	int margin=360-320;
	for (int y=0; y<len_px/360; y++) {
		//black margin on the right side
		for (int i=0; i<margin/2; i++) *out++=0;
		if (ypos+y<32 || ypos+y>607) {
			//Don't draw DMD; we already show that on the backboard display.
			//Also don't draw lower bit of vram; there's nothing there.
			for (int x=0; x<320; x++) *out++=0;
		} else {
			//fill with pixel data
			for (int x=0; x<320; x++) {
				*out++=cur_pal[*p++];
			}
		}
		p+=(cur_pitch-320);
		//black margin on the left side
		for (int i=margin/2; i<margin; i++) *out++=0;
	}
	return false;
}

void lcd_show(uint8_t *buf, uint32_t *pal, int w, int h, int show_dmd) {
	if (show_dmd) lcdbb_handle_dmd(buf, pal);
	cur_fb=buf;
	cur_pitch=w;
	//Convert from 32-bit R8G8B8 palette to 16-bit R5G6B5 palette for display
	for (int i=0; i<256; i++) {
		int r=(pal[i]>>16)&0xff;
		int g=(pal[i]>>8)&0xff;
		int b=(pal[i]>>0)&0xff;
		cur_pal[i]=((r>>3)<<11)|((g>>2)<<5)|(b>>3);
	}
}

static int frame_ctr;

int lcd_get_frame() {
	return frame_ctr;
}

static bool frame_done(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *ev, void * arg) {
	frame_ctr++;
	return true;
}

void lcd_init(void) {
	io_lcd_reset(1);
	vTaskDelay(pdMS_TO_TICKS(10));
	io_lcd_reset(0);
	vTaskDelay(pdMS_TO_TICKS(10));
	io_lcd_reset(1);
	vTaskDelay(pdMS_TO_TICKS(10));

	ESP_LOGI(TAG, "Install RGB panel driver");
	esp_lcd_panel_handle_t panel_handle = NULL;
	esp_lcd_rgb_panel_config_t panel_config = {
		.data_width = 16, // RGB565 in parallel mode, thus 16bit in width
		.psram_trans_align = 64,
		.clk_src = LCD_CLK_SRC_PLL160M,
		.disp_gpio_num = PIN_NUM_DISP_EN,
		.pclk_gpio_num = PIN_NUM_PCLK,
		.vsync_gpio_num = PIN_NUM_VSYNC,
		.hsync_gpio_num = PIN_NUM_HSYNC,
		.de_gpio_num = PIN_NUM_DE,
		.data_gpio_nums = {
			PIN_NUM_DATA0,
			PIN_NUM_DATA1,
			PIN_NUM_DATA2,
			PIN_NUM_DATA3,
			PIN_NUM_DATA4,
			PIN_NUM_DATA5,
			PIN_NUM_DATA6,
			PIN_NUM_DATA7,
			PIN_NUM_DATA8,
			PIN_NUM_DATA9,
			PIN_NUM_DATA10,
			PIN_NUM_DATA11,
			PIN_NUM_DATA12,
			PIN_NUM_DATA13,
			PIN_NUM_DATA14,
			PIN_NUM_DATA15,
		},
		.timings = {
			.pclk_hz = LCD_PIXEL_CLOCK_HZ,
			.h_res = LCD_H_RES,
			.v_res = LCD_V_RES,
			// The following parameters should refer to LCD spec
			.hsync_back_porch = 58,
			.hsync_front_porch = 4,
			.hsync_pulse_width = 2,
			.vsync_back_porch = 1,
			.vsync_front_porch = 139, //picked to make it exactly 60Hz
			.vsync_pulse_width = 6,
		},
		.flags.fb_in_psram = 1, // allocate frame buffer in PSRAM
		.flags.no_fb = true,
		.bounce_buffer_size_px = 360*32 //note: chosen to be an integer, even amount of lines
	};
	
	ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));

	ESP_LOGI(TAG, "Register event callbacks");
	esp_lcd_rgb_panel_event_callbacks_t cbs = {
		.on_bounce_empty = fill_fb,
		.on_vsync = frame_done
	};
	ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &cbs, NULL));

	ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
	ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

	esp_err_t ret;
	spi_device_handle_t spi;
	spi_bus_config_t buscfg={
		.miso_io_num=PIN_NUM_MISO,
		.mosi_io_num=PIN_NUM_MOSI,
		.sclk_io_num=PIN_NUM_CLK,
		.quadwp_io_num=-1,
		.quadhd_io_num=-1,
		.max_transfer_sz=64000 //todo: tweak to whatever the backbox lcd really requires
	};
	spi_device_interface_config_t devcfg={
		.clock_speed_hz=10*1000,
		.mode=0,
		.address_bits=1, // d/c bit
		.spics_io_num=PIN_NUM_CS,
		.queue_size=1,
		.flags=SPI_DEVICE_3WIRE,
	};
	//Initialize the SPI bus
	ret=spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);
	ESP_ERROR_CHECK(ret);
	//Attach the LCD to the SPI bus
	ret=spi_bus_add_device(LCD_HOST, &devcfg, &spi);
	ESP_ERROR_CHECK(ret);
	spi_device_handle_t bbspi=lcdbb_add_to_bus(LCD_HOST);
	//Initialize the LCD
	lcd_init_panel(spi);
	ESP_LOGI(TAG, "Main LCD init done.");
	//Initialize the backboard LCD (which is attached to the same SPI bus)
	lcdbb_init(bbspi);
	ESP_LOGI(TAG, "Backboard LCD init done.");
}
