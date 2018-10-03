#define PROGMEM
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "gslX680firmware.h"

#define GSL_PAGE_REG		0xf0

uint8_t packed[30000];
int cnt= 0;
int n= -1;
int offset= 0;

int sl= 0;
void i2c_write(uint8_t reg, uint8_t *buf, int cnt)
{
	if(cnt % 4 != 0) printf("Error cnt needs to be mod 4\n");
	//printf("reg: %d, cnt: %d, data: %02X\n", reg, cnt, buf[0]);
	//return;

	int n= cnt/4;
	int i;
	for(i=0;i<n;i++) {
		uint8_t r= GSLX680_FW[sl].offset;
		if(r != reg) {
			printf("%d: Error expected reg %d, got %d\n", sl, r, reg);
		}
		uint32_t l= GSLX680_FW[sl].val;

		uint8_t Wrbuf[4];
		Wrbuf[0] = (char)(GSLX680_FW[sl].val & 0x000000ff);
		Wrbuf[1] = (char)((GSLX680_FW[sl].val & 0x0000ff00) >> 8);
		Wrbuf[2] = (char)((GSLX680_FW[sl].val & 0x00ff0000) >> 16);
		Wrbuf[3] = (char)((GSLX680_FW[sl].val & 0xff000000) >> 24);

		if(buf[(i*4)+0] != Wrbuf[0] || buf[(i*4)+1] != Wrbuf[1] || buf[(i*4)+2] != Wrbuf[2] || buf[(i*4)+3] != Wrbuf[3]) {
			printf("%d: Error expected val [%02X%02X%02X%02X], got [%02X%02X%02X%02X]\n", sl,
				Wrbuf[0], Wrbuf[1], Wrbuf[2], Wrbuf[3],
				buf[(i*4)], buf[(i*4)+1], buf[(i*4)+2], buf[(i*4)+3]);
		}
		reg+=4;
		sl++;
	}
}

// the data is in blocks of 128 bytes, each one preceded by the page number
// we first send the page number then we send the data in blocks of 32 until the entire page is sent
void load_fw(void)
{
	uint8_t buf[32];
	size_t source_len = cnt; // sizeof(gslx680_fw);
	int blockstart= 1;
	int reg= 0;
	int off= 0;
	size_t source_line;
	for (source_line=0; source_line < source_len; source_line++) {
		if(off == 32){
			i2c_write(reg, buf, 32);
			reg += 32;
			off= 0;
			if(reg >= 128) blockstart= 1;
		}

		if(blockstart) {
			blockstart= 0;
			buf[0] = packed[source_line];
			buf[1] = 0;
			buf[2] = 0;
			buf[3] = 0;
			i2c_write(GSL_PAGE_REG, buf, 4);
			reg= 0;

		}else{
			buf[off++] = packed[source_line];
		}
	}
	if(off == 32){ // last block
		i2c_write(reg, buf, 32);
	}
}

int main(int argc, char const *argv[])
{
	size_t source_len = sizeof(GSLX680_FW)/sizeof(struct fw_data);
	printf("size= %d bytes\n", sizeof(GSLX680_FW));

	size_t source_line;
	for (source_line=0; source_line < source_len; source_line++) {
		if(cnt >= sizeof(packed)) {
			printf("Size too big %d, needs to be less than %d\n", cnt, sizeof(packed));
			exit(1);
		}
		if(GSLX680_FW[source_line].offset == 0xF0) {
			if(source_line != 0 && offset != 128) printf("Bad block size: %d\n", offset);
			n= 1;
			packed[cnt++]= GSLX680_FW[source_line].val;
			offset= 0;
			continue;
		}

		if(GSLX680_FW[source_line].offset != offset) printf("Bad offset: %d - %d\n", GSLX680_FW[source_line].offset, offset);
		memcpy(&packed[cnt], &GSLX680_FW[source_line].val, 4); // NOTE this is forlittle endian hosts, bigendian will get them in the wrong order
		cnt += 4;
		n+=4;
		offset+=4;
	}

	printf("packed size= %d\n", cnt);

	FILE *fp= fopen("gslfw.h", "w");
	fprintf(fp, "#include <avr/pgmspace.h>\n\n");
	fprintf(fp, "const prog_uchar gslx680_fw[] PROGMEM = {\n");
	int i;
	n= 0;
	int x= 0;
	for (i = 0; i < cnt; ++i) {
		fprintf(fp, "0x%02X", packed[i]);
		if(i < cnt-1) fprintf(fp, ",");
		if(n == 0) fprintf(fp, "\n"); // block
		else if(++x % 16 == 0) {
			fprintf(fp, "\n");
		}
		if(x == 128) {
			x= 0;
			n= 0;
		}else n++;
	}
	fprintf(fp, "\n};\n");
	fclose(fp);

	// test it works
	load_fw();

	return 0;
}
