//GFX driver for ESP32
/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
 */

#include <stdio.h>
#include <esp_log.h>
#include <gfx.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "cpu_addr_space.h"

#define BTN_LEFT 39
#define BTN_START 32
#define BTN_PLUNGER 33
#define BTN_RIGHT 27

void gfx_init() {
	lcd_init();
	
	gpio_config_t bconfig={
		.pin_bit_mask=(1ULL << BTN_LEFT)|(1ULL << BTN_START)|(1ULL << BTN_PLUNGER)|(1ULL << BTN_RIGHT),
		.mode=GPIO_MODE_DEF_INPUT,
		.pull_up_en=GPIO_PULLUP_ENABLE,
	};
	gpio_config(&bconfig);
}

#define BM_LEFT 1
#define BM_RIGHT 2
#define BM_START 4
#define BM_PLUNGER 8
static int get_btn_bitmap() {
	int v=0;
	if (!gpio_get_level(BTN_LEFT)) v|=BM_LEFT;
	if (!gpio_get_level(BTN_RIGHT)) v|=BM_RIGHT;
	if (!gpio_get_level(BTN_START)) v|=BM_START;
	if (!gpio_get_level(BTN_PLUNGER)) v|=BM_PLUNGER;
	return v;
}

int old_btns;

int gfx_get_key() {
	const int keys[]={INPUT_LFLIP,INPUT_RFLIP,INPUT_F1,INPUT_SPRING};
	int r=0;
	int new_btns=get_btn_bitmap();
	for (int i=0; i<4; i++) {
		if ((new_btns^old_btns)&(1<<i)) {
			r=keys[i];
			if (old_btns&(1<<i)) r|=INPUT_RELEASE;
		}
	}
	old_btns=new_btns;
	return r;
}



uint64_t last_frame;

uint64_t fps_ts;
int fps_frames;
int skipped;

#define FRAME_TIME (1000000/60)

void gfx_show(uint8_t *buf, uint32_t *pal, int h, int w, int scroll) {
	uint64_t ts=esp_timer_get_time();
	if (ts-fps_ts > (1000000*5)) {
		printf("%d fps\n", fps_frames/5);
		printf("Free internal: %d\n", heap_caps_get_free_size(MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL));
		cpu_addr_dump_hitctr();
		fps_frames=0;
		fps_ts=ts;
	}
	fps_frames++;

	if (ts-last_frame < FRAME_TIME || skipped==1) {
		lcd_show(buf, pal, h, w, scroll-32);
		skipped=0;
	} else {
		skipped++;
	}
	last_frame=ts;
}

int gfx_get_plunger() {
	return 0; //no analog plunger supported
}

int gfx_frame_done() {
	return (esp_timer_get_time()-last_frame)>(1000000/60);
}

void gfx_wait_frame_done() {
	int delay_us=(1000000/60)-(esp_timer_get_time()-last_frame);
	if (delay_us<=0) return;
	vTaskDelay(pdMS_TO_TICKS(delay_us/1000));
}

void gfx_enable_dmd(int enable) {
}

void backboard_show(int image) {
	//stub
	printf("backboard_show: %d\n", image);
}
