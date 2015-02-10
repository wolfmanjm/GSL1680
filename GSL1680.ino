// Original comments:
// driver for the GSL1680 touch panel
// Information gleaned from https://github.com/rastersoft/gsl1680.git and various other sources
// firmware for the specific panel was found here:- http://www.buydisplay.com/default/5-inch-tft-lcd-module-800x480-display-w-controller-i2c-serial-spi
// As was some test code.
// This is for that 800X480 display and the 480x272 from buydisplay.com
// ------
// Modified by Helge Langehaug with help from https://forum.pjrc.com/threads/26256-Has-anyone-tried-running-the-GSL16880-capacitive-touchscreen-controller-with-Teensy3

/*
Pin outs
the FPC on the touch panel is six pins, pin 1 is to the left pin 6 to the right with the display facing up

pin | function  | Arduino Mega | Arduino Uno
--------------------------------------------
1   | SCL       | SCL(21)      | A5
2   | SDA       | SDA(20)      | A4
3   | VDD (3v3) | 3v3          | 3v3
4   | Wake      | 4            | 4
5   | Int       | 2            | 2
6   | Gnd       | gnd          | gnd
*/
#include <Wire.h>
#include "Arduino.h"

#define BIGFLASH

#define GET_FAR_ADDRESS(var) \
({ \
uint_farptr_t tmp; \
\
__asm__ __volatile__( \
\
"ldi %A0, lo8(%1)" "\n\t" \
"ldi %B0, hi8(%1)" "\n\t" \
"ldi %C0, hh8(%1)" "\n\t" \
"clr %D0" "\n\t" \
: \
"=d" (tmp) \
: \
"p" (&(var)) \
); \
tmp; \
})


// TODO define for other resolution
#ifndef BIGFLASH
#include "gslfw.h" // this is compacted format made by compress_data.c
#else
#include "gslX680firmware.h"
#endif

// Pins
#define WAKE 4
#define INTRPT 2
#define LED 13

#define SCREEN_MAX_X 		800
#define SCREEN_MAX_Y 		480

#define GSLX680_I2C_ADDR 	0x40

#define GSL_DATA_REG		0x80
#define GSL_STATUS_REG		0xe0
#define GSL_PAGE_REG		0xf0

#define delayus delayMicroseconds

struct _coord { uint32_t x, y; uint8_t finger; };

struct _ts_event
{
	uint8_t  n_fingers;
	struct _coord coords[5];
};

struct _ts_event ts_event;
bool led= false;

static inline void wiresend(uint8_t x) {
#if ARDUINO >= 100
  Wire.write((uint8_t)x);
#else
  Wire.send(x);
#endif
}

static inline uint8_t wirerecv(void) {
#if ARDUINO >= 100
  return Wire.read();
#else
  return Wire.receive();
#endif
}

bool i2c_write(uint8_t reg, uint8_t *buf, int cnt)
{
	#if 0
	Serial.print("i2c write: "); Serial.println(reg, HEX);
	for(int i=0; i<cnt; i++){
	    Serial.print(buf[i], HEX); Serial.print(",");
	}
	Serial.println();
	#endif

	Wire.beginTransmission(GSLX680_I2C_ADDR);
    wiresend(reg);
    for(int i=0; i<cnt; i++){
        wiresend(buf[i]);
    }
    int r= Wire.endTransmission();
    if(r != 0){ Serial.print("i2c write error: "); Serial.print(r); Serial.print(" "); Serial.println(reg, HEX); }
    return r == 0;
}

int i2c_read(uint8_t reg, uint8_t *buf, int cnt)
{
	Wire.beginTransmission(GSLX680_I2C_ADDR);
  	wiresend(reg);
  	int r= Wire.endTransmission();
	if(r != 0){ Serial.print("i2c read error: "); Serial.print(r); Serial.print(" "); Serial.println(reg, HEX); }

  	int n= Wire.requestFrom(GSLX680_I2C_ADDR, cnt);
	if(n != cnt){ Serial.print("i2c read error: did not get expected count "); Serial.print(n); Serial.print(" - "); Serial.println(cnt); }

  	for(int i=0; i<n; i++){
  	    buf[i]= wirerecv();
  	}
  	return n;
}

void clr_reg(void)
{
	uint8_t buf[4];

	buf[0] = 0x88;
	i2c_write(0xe0, buf, 1);
	delay(20);

	buf[0] = 0x01;
	i2c_write(0x80, buf, 1);
	delay(5);

	buf[0] = 0x04;
	i2c_write(0xe4, buf, 1);
	delay(5);

	buf[0] = 0x00;
	i2c_write(0xe0, buf, 1);
	delay(20);
}

void reset_chip()
{
	uint8_t buf[4];

	buf[0] = 0x88;
    i2c_write(GSL_STATUS_REG, buf, 1);
	delay(20);

	buf[0] = 0x04;
    i2c_write(0xe4,buf, 1);
	delay(10);

	buf[0] = 0x00;
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = 0x00;
    i2c_write(0xbc,buf, 4);
	delay(10);
}

#ifndef BIGFLASH
// the data is in blocks of 128 bytes, each one preceded by the page number
// we first send the page number then we send the data in blocks of 32 until the entire page is sent
// NOTE that the firmware data is stored in flash as it is huge! around 28kBytes
void load_fw(void)
{
	uint8_t buf[32];
	size_t source_len = sizeof(gslx680_fw);
	Serial.print("Firmware length: "); Serial.println(source_len);
	int blockstart= 1;
	int reg= 0;
	int off= 0;
	size_t source_line;
	for (source_line=0; source_line < source_len; source_line++) {
		if(off == 32){
			i2c_write(reg, buf, 32); // write accumulated block
			reg += 32;
			off= 0;
			if(reg >= 128) blockstart= 1;
		}

		if(blockstart) {
			blockstart= 0;
			buf[0] = pgm_read_byte_near(gslx680_fw + source_line); // gslx680_fw[source_line];
			buf[1] = 0;
			buf[2] = 0;
			buf[3] = 0;
			i2c_write(GSL_PAGE_REG, buf, 4);
			reg= 0;

		}else{
			buf[off++] = pgm_read_byte_near(gslx680_fw + source_line); // gslx680_fw[source_line];
		}
	}
	if(off == 32){ // write last accumulated block
		i2c_write(reg, buf, 32);
	}
}

#else
void load_fw(void)
{    
    uint8_t addr;
    uint8_t Wrbuf[4];
    uint16_t source_line = 0;
    uint16_t source_len = sizeof(GSLX680_FW) / sizeof(struct fw_data);
    Serial.print("Firmware size: "); Serial.println(sizeof(GSLX680_FW));
    Serial.print("Line numbers : "); Serial.println(source_len);

    for (source_line = 0; source_line < source_len; source_line++) {
      
       addr = pgm_read_byte_far(GET_FAR_ADDRESS(GSLX680_FW[0].offset)+source_line*5);
       
       Wrbuf[0] = (char) (pgm_read_dword_far(GET_FAR_ADDRESS(GSLX680_FW[0].val)+source_line*5) & 0x000000ff);

       Wrbuf[1] = (char) ((pgm_read_dword_far(GET_FAR_ADDRESS(GSLX680_FW[0].val)+source_line*5) & 0x0000ff00) >> 8);
       Wrbuf[2] = (char) ((pgm_read_dword_far(GET_FAR_ADDRESS(GSLX680_FW[0].val)+source_line*5) & 0x00ff0000) >> 16);
       Wrbuf[3] = (char) ((pgm_read_dword_far(GET_FAR_ADDRESS(GSLX680_FW[0].val)+source_line*5) & 0xff000000) >> 24);

       i2c_write(addr, Wrbuf, 4);
    }
}

#endif

void startup_chip(void)
{
	uint8_t buf[4];

	buf[0] = 0x00;
    i2c_write(0xe0, buf, 1);
}

void init_chip()
{
#if 1
	Serial.println("Toggle Wake");
	digitalWrite(WAKE, HIGH);
	delay(50);
	digitalWrite(WAKE, LOW);
	delay(50);
	digitalWrite(WAKE, HIGH);
	delay(10);

	// CTP startup sequence
	Serial.println("clr reg");
	clr_reg();
	delay(50);

	Serial.println("reset_chip");
	reset_chip();
	delay(10);

	Serial.println("load_fw");
	load_fw();
	delay(50);

        startup_chip();
	Serial.println("reset_chip2");
	reset_chip();
	Serial.println("startup_chip");
	startup_chip();
	Serial.println("init done");

#else
	// rastersoft int sequence
	reset_chip();
	load_fw();
	startup_chip();
	reset_chip();

	digitalWrite(WAKE, LOW);
	delay(50);
	digitalWrite(WAKE, HIGH);
	delay(30);
	digitalWrite(WAKE, LOW);
	delay(5);
	digitalWrite(WAKE, HIGH);
	delay(20);

	reset_chip();
	startup_chip();
#endif
}

int read_data(void)
{

	Serial.println("reading data...");
	uint8_t touch_data[24] = {0};
	int n= i2c_read(GSL_DATA_REG, touch_data, 24);
	Serial.print("read: "); Serial.println(n);

	ts_event.n_fingers= touch_data[0];
	for(int i=0; i<ts_event.n_fingers; i++){
		ts_event.coords[i].x = ( (((uint32_t)touch_data[(i*4)+5])<<8) | (uint32_t)touch_data[(i*4)+4] ) & 0x00000FFF; // 12 bits of X coord
		ts_event.coords[i].y = ( (((uint32_t)touch_data[(i*4)+7])<<8) | (uint32_t)touch_data[(i*4)+6] ) & 0x00000FFF;
		ts_event.coords[i].finger = (uint32_t)touch_data[(i*4)+7] >> 4; // finger that did the touch
	}

	return ts_event.n_fingers;
}

void setup() {
	Serial.begin(9600);
	Serial.println("Starting");
	pinMode(LED, OUTPUT);
	pinMode(WAKE, OUTPUT);
	digitalWrite(WAKE, LOW);
	pinMode(INTRPT, INPUT_PULLUP);
	delay(10);
	Wire.begin();
	init_chip();

#if 0
	uint8_t buf[4];
	int n= i2c_read(0xB0, buf, 4);
	Serial.print(buf[0], HEX); Serial.print(","); Serial.print(buf[1], HEX); Serial.print(","); Serial.print(buf[2], HEX); Serial.print(","); Serial.println(buf[3], HEX);
#endif
}

void loop() {
	if(digitalRead(INTRPT) == HIGH) {
		//digitalWrite(LED, led);
		//led= !led;
		int n= read_data();
		for(int i=0; i<n; i++){
			Serial.print(ts_event.coords[i].finger); Serial.print(" "), Serial.print(ts_event.coords[i].x); Serial.print(" "), Serial.print(ts_event.coords[i].y);
		 	Serial.println("");
		}
		Serial.println("---");
	}

}

