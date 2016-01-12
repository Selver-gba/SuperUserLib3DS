#include <3ds.h>
#include <3ds/gfx.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include "font.h"
#include "libsu.h"

#define HEIGHT (240)
#define WIDTH (400)

#define FONT_HEIGHT (8)
#define FONT_WIDTH (8)

struct px {
	uint8_t b;
	uint8_t g;
	uint8_t r;
};

static int x;
static int y;

static void suPutc(int c)
{
	struct px (* frame)[HEIGHT];
	unsigned int font_x, font_y;
	const unsigned char *p;

	frame = (void *)gfxTopLeftFramebuffers[0];

	switch(c) {
		case '\n':
			x = 0;
			y -= FONT_HEIGHT;
			if (y < 0)
				y = FONT_HEIGHT;
			break;

		default:
			p = font + c * FONT_WIDTH;

			for (font_x = 0; font_x < FONT_WIDTH; font_x++) {
				for (font_y = 0; font_y < FONT_HEIGHT; font_y++) {
					if ((0x80 >> font_y) & *p) {
						frame[x][y - font_y].r = 255;
						frame[x][y - font_y].g = 255;
						frame[x][y - font_y].b = 255;
					} else {
						frame[x][y - font_y].r = 0;
						frame[x][y - font_y].g = 0;
						frame[x][y - font_y].b = 0;
					}
				}

				p++;
				x++;
			}
	}
}

void suPuts(const char *s)
{
	while (*s) {
		suPutc(*s);
		s++;
	}

	suPutc('\n');
}

int main(int argc, char **argv) {
	char s[32];

	gfxTopLeftFramebuffers[0] = linearAlloc(WIDTH * HEIGHT * sizeof(struct px));
	if(gfxTopLeftFramebuffers[0] == NULL)
		return ENOMEM;

	gfxSet3D(false);
	gfxSetScreenFormat(GFX_TOP, GSP_BGR8_OES);
	gfxConfigScreen(GFX_TOP, true);

	x = 0;
	y = HEIGHT - 1;

	sdmcInit();
	
	suPuts("Arm11 exploit demo, get access to am:u service\n");
    if(suInit() == 0)
	{
		Result res = amInit();
		sprintf(s, "am:u init : %08lX", res);
		suPuts(s);
	}
	
	suPuts("Press START to reset.");

    while(aptMainLoop()) {
        hidScanInput();
        if(hidKeysDown() & KEY_START) {
            break;
        }
    }

	suPuts("Resetting");
	aptOpenSession();
	APT_HardwareResetAsync();
	aptCloseSession();

	linearFree(gfxTopLeftFramebuffers[0]);
	sdmcExit();
	amExit();

	while (1)
		svcSleepThread(UINT64_MAX);
}
