/* EMPV for windows
Features:
- Oscilloscope-like interface
- Frequency graph
- Editor

Ethernet documentation: https://learn.microsoft.com/en-us/windows/win32/winsock/tcp-ip-raw-sockets-2
*/

#include "include/ribbon.h"
#include "include/popup.h"
#include "include/win32Tools.h"
#include "include/win32tcp.h"
#include <time.h>

#define DIAL_LINEAR     0
#define DIAL_LOG        1
#define DIAL_EXP        2

#define NUM_WINDOWS     3

#define WINDOW_OSC      1
#define WINDOW_FREQ     2
#define WINDOW_EDITOR   4

typedef struct { // dial
    char label[24];
    int window;
    int type;
    int status[2];
    double size;
    double position[2];
    double range[2];
    double *variable;
} dial_t;

typedef struct { // switch
    char label[24];
    int window;
    int status;
    double size;
    double position[2];
    int *variable;
} switch_t;

typedef struct { // general window attributes
    char title[32];
    double windowCoords[4]; // minX, minY, maxX, maxY
    double windowTop;
    double windowSide;
    double windowMinX;
    double windowMinY;
    int minimize;
    int move;
    int click;
    int resize;
    list_t *dials;
    list_t *switches;
} window_t;

typedef struct { // all the empv shared state is here
    /* general */
        list_t *data; // a list of all data collected through ethernet
        list_t *logVariables; // a list of variables logged on the AMDC
        list_t *windowRender; // which order to render windows in
        window_t windows[NUM_WINDOWS]; // window variables
        /* mouse variables */
        double mx; // mouseX
        double my; // mouseY
        double mw; // mouse wheel
        char mouseDown;  // mouse down
        double anchorX;
        double anchorY;
        double dialAnchorX;
        double dialAnchorY;
        double anchorPoints[4];
        /* color variables */
        int theme;
        int themeDark;
        double themeColors[90];
    /* oscilloscope view */
        int oscLeftBound; // left bound (index in data list)
        int oscRightBound; // right bound (index in data list)
        double oscBottomBound; // bottom bound (y value)
        double oscTopBound; // top bound (y value)
        double oscWindowSize; // size of window (index in data list)
        int stop; // pause and unpause
    /* frequency view */
        list_t *freqData;
        double topFreq; // top bound (y value)
    /* editor view */
        double editorBottomBound;
        double editorWindowSize; // size of window

} empv_t;

empv_t self; // global state

dial_t *dialInit(char *label, double *variable, int window, int type, double yScale, double yOffset, double size, double bottom, double top) {
    dial_t *dial = malloc(sizeof(dial_t));
    memcpy(dial -> label, label, strlen(label) + 1);
    dial -> status[0] = 0;
    dial -> window = window;
    dial -> type = type;
    dial -> position[0] = yScale;
    dial -> position[1] = yOffset;
    dial -> size = size;
    dial -> range[0] = bottom;
    dial -> range[1] = top;
    dial -> variable = variable;
}

switch_t *switchInit(char *label, int *variable, int window, double yScale, double yOffset, double size) {
    switch_t *switchp = malloc(sizeof(switch_t));
    memcpy(switchp -> label, label, strlen(label) + 1);
    switchp -> status = 0;
    switchp -> window = window;
    switchp -> position[0] = yScale;
    switchp -> position[1] = yOffset;
    switchp -> size = size;
    switchp -> variable = variable;
}

void init() { // initialises the empv variabes (shared state)
/* color */
    double themeCopy[42] = {
        /* light theme */
        255, 255, 255, // background color
        195, 195, 195, // window color
        255, 0, 0, // data color
        0, 0, 0, // text color
        230, 230, 230, // window background color
        0, 144, 20, // switch toggled on color
        255, 0, 0, // switch toggled off color
        /* dark theme */
        60, 60, 60, // background color
        10, 10, 10, // window color
        19, 236, 48, // data color
        200, 200, 200, // text color
        80, 80, 80, // window background color
        0, 255, 0, // switch toggled on color
        164, 28, 9 // switch toggled off color
    };
    memcpy(self.themeColors, themeCopy, sizeof(themeCopy));
    self.themeDark = sizeof(themeCopy) / sizeof(double) / 2;
    self.theme = self.themeDark;
    if (self.theme == 0) {
        ribbonLightTheme();
        popupLightTheme();
    } else {
        ribbonDarkTheme();
        popupDarkTheme();
    }
    /* data */
    self.data = list_init();
    self.logVariables = list_init();
    self.windowRender = list_init();
    list_append(self.windowRender, (unitype) WINDOW_FREQ, 'i');
    list_append(self.windowRender, (unitype) WINDOW_OSC, 'i');
    list_append(self.windowRender, (unitype) WINDOW_EDITOR, 'i');
    self.anchorX = 0;
    self.anchorY = 0;
    self.dialAnchorX = 0;
    self.dialAnchorY = 0;
    /* osc */
    self.oscLeftBound = 0;
    self.oscRightBound = 0;
    self.oscBottomBound = -100;
    self.oscTopBound = 100;
    self.oscWindowSize = 200;
    strcpy(self.windows[0].title, "Oscilloscope");
    self.windows[0].windowCoords[0] = -317;
    self.windows[0].windowCoords[1] = 25;
    self.windows[0].windowCoords[2] = -2;
    self.windows[0].windowCoords[3] = 167;
    self.windows[0].windowTop = 15;
    self.windows[0].windowSide = 50;
    self.windows[0].windowMinX = 60 + self.windows[0].windowSide;
    self.windows[0].windowMinY = 120 + self.windows[0].windowTop;
    self.windows[0].minimize = 0;
    self.windows[0].move = 0;
    self.windows[0].click = 0;
    self.windows[0].resize = 0;
    self.stop = 0;
    self.windows[0].dials = list_init();
    self.windows[0].switches = list_init();
    list_append(self.windows[0].dials, (unitype) (void *) dialInit("X Scale", &self.oscWindowSize, WINDOW_OSC, DIAL_EXP, 1, -25 - self.windows[0].windowTop, 8, 1, 1000), 'p');
    list_append(self.windows[0].dials, (unitype) (void *) dialInit("Y Scale", &self.oscTopBound, WINDOW_OSC, DIAL_EXP, 1, -65 - self.windows[0].windowTop, 8, 50, 10000), 'p');
    list_append(self.windows[0].switches, (unitype) (void *) switchInit("Pause", &self.stop, WINDOW_OSC, 1, -100 - self.windows[0].windowTop, 8), 'p');

    /* frequency */
    self.freqData = list_init();
    self.topFreq = 100;
    strcpy(self.windows[1].title, "Frequency");
    self.windows[1].windowCoords[0] = 2;
    self.windows[1].windowCoords[1] = 25;
    self.windows[1].windowCoords[2] = 317;
    self.windows[1].windowCoords[3] = 167;
    self.windows[1].windowTop = 15;
    self.windows[1].windowSide = 50;
    self.windows[1].windowMinX = 52 + self.windows[1].windowSide;
    self.windows[1].windowMinY = 120 + self.windows[1].windowTop;
    self.windows[1].minimize = 0;
    self.windows[1].move = 0;
    self.windows[1].click = 0;
    self.windows[1].resize = 0;
    self.windows[1].dials = list_init();
    self.windows[1].switches = list_init();
    list_append(self.windows[1].dials, (unitype) (void *) dialInit("Y Scale", &self.topFreq, WINDOW_FREQ, DIAL_EXP, 1, -25 - self.windows[1].windowTop, 8, 50, 10000), 'p');
    /* editor */
    strcpy(self.windows[2].title, "Editor");
    self.windows[2].windowCoords[0] = -317;
    self.windows[2].windowCoords[1] = -121;
    self.windows[2].windowCoords[2] = -2;
    self.windows[2].windowCoords[3] = 21;
    self.windows[2].windowTop = 15;
    self.windows[2].windowSide = 0;
    self.windows[2].windowMinX = 60 + self.windows[2].windowSide;
    self.windows[2].windowMinY = 120 + self.windows[2].windowTop;
    self.windows[2].minimize = 0;
    self.windows[2].move = 0;
    self.windows[2].click = 0;
    self.windows[2].resize = 0;
    self.windows[2].dials = list_init();
    self.windows[2].switches = list_init();
}

int ilog2(int input) {
	return (int) lround(log2(input));
}

int pow2(int input) {
	int output = 1;
	while (input > 0) {
		output <<= 1;
		input--;
	}
	return output;
}

double angleBetween(double x1, double y1, double x2, double y2) {
    double output;
    if (y2 - y1 < 0) {
        output = 180 + atan((x2 - x1) / (y2 - y1)) * 57.2958;
    } else {
        output = atan((x2 - x1) / (y2 - y1)) * 57.2958;
    }
    if (output < 0) {
        output += 360;
    }
    return output;
}

list_t *FFT(list_t *samples) {
    /* format of samples and output is [real1, imag1, real2, imag2, real3, imag3, ...]
    Algorithm from: https://www.youtube.com/watch?v=htCj9exbGo0
    */
    int N = (samples -> length) / 2;
    if (N <= 1) {
        list_t *samplesCopy = list_init();
        list_copy(samplesCopy, samples);
        return samplesCopy;
    }
    int M = N / 2;
    list_t *Xeven = list_init();
    list_t *Xodd = list_init();
    for (int i = 0; i < M; i++) {
        list_append(Xeven, samples -> data[i * 4], 'd');
        list_append(Xeven, samples -> data[i * 4 + 1], 'd');
        list_append(Xodd, samples -> data[i * 4 + 2], 'd');
        list_append(Xodd, samples -> data[i * 4 + 3], 'd');
    }
    list_t *Feven = list_init();
    list_t *Fodd = list_init();
    Feven = FFT(Xeven);
    Fodd = FFT(Xodd);
    list_t *freqBins = list_init();
    for (int i = 0; i < N; i++) {
        list_append(freqBins, (unitype) 0, 'd');
    }
    for (int i = 0; i < N / 2; i++) {
        double subreal = 1.0 * cos(-2 * M_PI * i / N);
        double subginary = 1.0 * sin(-2 * M_PI * i / N);
        double real = subreal * Fodd -> data[i * 2].d - subginary * Fodd -> data[i * 2 + 1].d;
        double imaginary = subreal * Fodd -> data[i * 2 + 1].d + subginary * Fodd -> data[i * 2].d;
        freqBins -> data[i * 2].d = Feven -> data[i * 2].d + real;
        freqBins -> data[i * 2 + 1].d = Feven -> data[i * 2 + 1].d + imaginary;
    }
    list_free(Feven);
    list_free(Fodd);
    list_free(Xeven);
    list_free(Xodd);
    return freqBins;
}

void dialTick(int window) {
    int windowID = pow2(window);
    for (int i = 0; i < self.windows[window].dials -> length; i++) {
        dial_t *dialp = (dial_t *) (self.windows[window].dials -> data[i].p);
        if ((dialp -> window & windowID) != 0) {
            textGLWriteString(dialp -> label, (self.windows[window].windowCoords[2] - self.windows[window].windowSide + self.windows[window].windowCoords[2]) / 2, self.windows[window].windowCoords[1] + (self.windows[window].windowCoords[3] - self.windows[window].windowCoords[1]) * dialp -> position[0] + dialp -> position[1] + 15, 7, 50);
            turtlePenSize(dialp -> size * 2);
            double dialX = (self.windows[window].windowCoords[2] - self.windows[window].windowSide + self.windows[window].windowCoords[2]) / 2;
            double dialY = self.windows[window].windowCoords[1] + (self.windows[window].windowCoords[3] - self.windows[window].windowCoords[1]) * dialp -> position[0] + dialp -> position[1];
            turtleGoto(dialX, dialY);
            turtlePenDown();
            turtlePenUp();
            turtlePenSize(dialp -> size * 2 * 0.8);
            turtlePenColor(self.themeColors[self.theme + 3], self.themeColors[self.theme + 4], self.themeColors[self.theme + 5]);
            turtlePenDown();
            turtlePenUp();
            turtlePenColor(self.themeColors[self.theme + 9], self.themeColors[self.theme + 10], self.themeColors[self.theme + 11]);
            turtlePenSize(1);
            turtlePenDown();
            double dialAngle;
            if (dialp -> type == DIAL_LOG) {
                dialAngle = pow(360, (*(dialp -> variable) - dialp -> range[0]) / (dialp -> range[1] - dialp -> range[0]));
            } else if (dialp -> type == DIAL_LINEAR) {
                dialAngle = (*(dialp -> variable) - dialp -> range[0]) / (dialp -> range[1] - dialp -> range[0]) * 360;
            } else if (dialp -> type == DIAL_EXP) {
                dialAngle = 360 * (log(((*(dialp -> variable) - dialp -> range[0]) / (dialp -> range[1] - dialp -> range[0])) * 360 + 1) / log(361));
            }
            turtleGoto(dialX + sin(dialAngle / 57.2958) * dialp -> size, dialY + cos(dialAngle / 57.2958) * dialp -> size);
            turtlePenUp();
            if (self.mouseDown) {
                if (dialp -> status[0] < 0) {
                    self.dialAnchorX = dialX;
                    self.dialAnchorY = dialY;
                    dialp -> status[0] *= -1;
                    dialp -> status[1] = self.mx - dialX;
                }
            } else {
                if (self.mx > dialX - dialp -> size && self.mx < dialX + dialp -> size && self.my > dialY - dialp -> size && self.my < dialY + dialp -> size) {
                    dialp -> status[0] = -1;
                } else {
                    dialp -> status[0] = 0;
                }
            }
            if (dialp -> status[0] > 0) {
                dialAngle = angleBetween(self.dialAnchorX, self.dialAnchorY, self.mx, self.my);
                if (self.my < self.dialAnchorY) {
                    dialp -> status[1] = self.mx - dialX;
                }
                if ((dialAngle < 0.0001 || dialAngle > 180) && self.my > self.dialAnchorY && dialp -> status[1] >= 0) {
                    dialAngle = 0.0001;
                }
                if ((dialAngle > 359.999 || dialAngle < 180) && self.my > self.dialAnchorY && dialp -> status[1] < 0) {
                    dialAngle = 359.999;
                }
                if (dialp -> type == DIAL_LOG) {
                    *(dialp -> variable) =dialp -> range[0] + (dialp -> range[1] - dialp -> range[0]) * (log(dialAngle) / log(360));
                } else if (dialp -> type == DIAL_LINEAR) {
                    *(dialp -> variable) = dialp -> range[0] + ((dialp -> range[1] - dialp -> range[0]) * dialAngle / 360);
                } else if (dialp -> type == DIAL_EXP) {
                    *(dialp -> variable) = dialp -> range[0] + (dialp -> range[1] - dialp -> range[0]) * ((pow(361, dialAngle / 360) - 1) / 360);
                }
            }
            char bubble[24];
            sprintf(bubble, "%.0lf", *(dialp -> variable));
            textGLWriteString(bubble, dialX + dialp -> size + 3, dialY, 4, 0);
        }
    }
}

void switchTick(int window) {
    int windowID = pow2(window);
    for (int i = 0; i < self.windows[window].switches -> length; i++) {
        switch_t *switchp = (switch_t *) (self.windows[window].switches -> data[i].p);
        if ((switchp -> window & windowID) != 0) {
            textGLWriteString(switchp -> label, (self.windows[window].windowCoords[2] - self.windows[window].windowSide + self.windows[window].windowCoords[2]) / 2, self.windows[window].windowCoords[1] + (self.windows[window].windowCoords[3] - self.windows[window].windowCoords[1]) * switchp -> position[0] + switchp -> position[1] + 15, 7, 50);
            turtlePenColor(self.themeColors[self.theme + 12], self.themeColors[self.theme + 13], self.themeColors[self.theme + 14]);
            turtlePenSize(switchp -> size * 1.2);
            double switchX = (self.windows[window].windowCoords[2] - self.windows[window].windowSide + self.windows[window].windowCoords[2]) / 2;
            double switchY = self.windows[window].windowCoords[1] + (self.windows[window].windowCoords[3] - self.windows[window].windowCoords[1]) * switchp -> position[0] + switchp -> position[1];
            turtleGoto(switchX - switchp -> size * 0.8, switchY);
            turtlePenDown();
            turtleGoto(switchX + switchp -> size * 0.8, switchY);
            turtlePenUp();
            turtlePenSize(switchp -> size);
            turtlePenColor(self.themeColors[self.theme + 9], self.themeColors[self.theme + 10], self.themeColors[self.theme + 11]);
            if (*(switchp -> variable)) {
                // turtlePenColor(self.themeColors[self.theme + 15], self.themeColors[self.theme + 16], self.themeColors[self.theme + 17]);
                turtleGoto(switchX + switchp -> size * 0.8, switchY);
            } else {
                // turtlePenColor(self.themeColors[self.theme + 18], self.themeColors[self.theme + 19], self.themeColors[self.theme + 20]);
                turtleGoto(switchX - switchp -> size * 0.8, switchY);
            }
            turtlePenDown();
            turtlePenUp();
            if (self.mouseDown) {
                if (switchp -> status < 0) {
                    switchp -> status *= -1;
                }
            } else {
                if (self.mx > switchX - switchp -> size && self.mx < switchX + switchp -> size && self.my > switchY - switchp -> size && self.my < switchY + switchp -> size) {
                    switchp -> status = -1;
                } else {
                    switchp -> status = 0;
                }
            }
            if (switchp -> status > 0) {
                if (*(switchp -> variable)) {
                    *(switchp -> variable) = 0;
                } else {
                    *(switchp -> variable) = 1;
                }
                switchp -> status = 0;
            }
        }
    }
}

void renderWindow(int window) {
    window_t *win = &self.windows[window];
    if (win -> minimize == 0) {
        /* render window */
        turtlePenSize(2);
        turtlePenColor(self.themeColors[self.theme + 3], self.themeColors[self.theme + 4], self.themeColors[self.theme + 5]);
        turtleGoto(win -> windowCoords[0], win -> windowCoords[1]);
        turtlePenDown();
        turtleGoto(win -> windowCoords[0], win -> windowCoords[3]);
        turtleGoto(win -> windowCoords[2], win -> windowCoords[3]);
        turtleGoto(win -> windowCoords[2], win -> windowCoords[1]);
        turtleGoto(win -> windowCoords[0], win -> windowCoords[1]);
        turtlePenUp();
        turtleRectangle(win -> windowCoords[0], win -> windowCoords[3], win -> windowCoords[2], win -> windowCoords[3] - win -> windowTop, self.themeColors[self.theme + 3], self.themeColors[self.theme + 4], self.themeColors[self.theme + 5], 0);
        turtleRectangle(win -> windowCoords[2] - win -> windowSide, win -> windowCoords[1], win -> windowCoords[2], win -> windowCoords[3], self.themeColors[self.theme + 3], self.themeColors[self.theme + 4], self.themeColors[self.theme + 5], 40);
        turtlePenColor(self.themeColors[self.theme + 9], self.themeColors[self.theme + 10], self.themeColors[self.theme + 11]);
        /* write title */
        textGLWriteString(win -> title, (win -> windowCoords[0] + win -> windowCoords[2] - win -> windowSide) / 2, win -> windowCoords[3] - win -> windowTop * 0.45, win -> windowTop * 0.5, 50);
        /* draw sidebar UI elements */
        dialTick(window);
        switchTick(window);
    }
    /* window move and resize logic */
    /* move */
    if (self.mouseDown) {
        if (win -> move < 0) {
            self.anchorX = self.mx;
            self.anchorY = self.my;
            memcpy(self.anchorPoints, win -> windowCoords, sizeof(double) * 4);
            win -> move *= -1;
        }
        if (win -> click > 0 && win -> click < 100) {
            if (win -> click == 1) {
                list_remove(self.windowRender, (unitype) pow2(window), 'i');
                list_append(self.windowRender, (unitype) pow2(window), 'i');
            } else if (win -> click == 2) {
                if (win -> minimize == 1) {
                    list_remove(self.windowRender, (unitype) pow2(window), 'i');
                    list_append(self.windowRender, (unitype) pow2(window), 'i');
                    win -> minimize = 0;
                } else {
                    if (self.windowRender -> data[self.windowRender -> length - 1].i == pow2(window)) {
                        win -> minimize = 1;
                    } else {
                        list_remove(self.windowRender, (unitype) pow2(window), 'i');
                        list_append(self.windowRender, (unitype) pow2(window), 'i');
                    }
                }
            }
            win -> click = 100;
        }
    } else {
        if (win -> minimize == 0) {
            int moveSum = 0;
            for (int i = self.windowRender -> length - 1; i >= 0; i--) {
                if (self.windowRender -> data[i].i == pow2(window)) {
                    break;
                }
                moveSum += self.windows[ilog2(self.windowRender -> data[i].i)].move;
            }
            if (self.mx > win -> windowCoords[0] && self.mx < win -> windowCoords[2] && self.my > win -> windowCoords[3] - win -> windowTop && self.my < win -> windowCoords[3] && moveSum == 0) {
                win -> move = -1;
            } else {
                win -> move = 0;
            }
            int clickSum = 0;
            for (int i = self.windowRender -> length - 1; i >= 0; i--) {
                if (self.windowRender -> data[i].i == pow2(window)) {
                    break;
                }
                clickSum += self.windows[ilog2(self.windowRender -> data[i].i)].click;
            }
            if (self.mx > win -> windowCoords[0] && self.mx < win -> windowCoords[2] && self.my > win -> windowCoords[1] && self.my < win -> windowCoords[3] && clickSum == 0) {
                win -> click = 1;
            } else {
                win -> click = 0;
            }
        } else {
            win -> click = 0;
        }
    }
    if (win -> minimize == 0) {
        if (win -> move > 0) {
            win -> click = 1;
            win -> windowCoords[0] = self.anchorPoints[0] + self.mx - self.anchorX;
            win -> windowCoords[1] = self.anchorPoints[1] + self.my - self.anchorY;
            win -> windowCoords[2] = self.anchorPoints[2] + self.mx - self.anchorX;
            win -> windowCoords[3] = self.anchorPoints[3] + self.my - self.anchorY;
        }
        /* resize */
        double epsilon = 3;
        if (self.mouseDown) {
            if (win -> resize < 0) {
                win -> click = 1;
                win -> resize *= -1;
                for (int i = 0; i < NUM_WINDOWS; i++) { // don't move and resize
                    self.windows[i].move = 0;
                }
                switch (win -> resize) {
                    case 1:
                        win32SetCursor(CURSOR_DIAGONALRIGHT);
                    break;
                    case 2:
                        win32SetCursor(CURSOR_UPDOWN);
                    break;
                    case 3:
                        win32SetCursor(CURSOR_DIAGONALLEFT);
                    break;
                    case 4:
                        win32SetCursor(CURSOR_SIDESIDE);
                    break;
                    case 5:
                        win32SetCursor(CURSOR_DIAGONALRIGHT);
                    break;
                    case 6:
                        win32SetCursor(CURSOR_UPDOWN);
                    break;
                    case 7:
                        win32SetCursor(CURSOR_DIAGONALLEFT);
                    break;
                    case 8:
                        win32SetCursor(CURSOR_SIDESIDE);
                    break;
                    default:
                    break;
                }
            }
        } else {
            int resizeSum = 0;
            for (int i = self.windowRender -> length - 1; i >= 0; i--) {
                if (self.windowRender -> data[i].i == pow2(window)) {
                    break;
                }
                resizeSum += self.windows[ilog2(self.windowRender -> data[i].i)].resize;
                resizeSum += self.windows[ilog2(self.windowRender -> data[i].i)].click;
            }
            if (resizeSum == 0) {
                if (self.mx > win -> windowCoords[2] - epsilon && self.mx < win -> windowCoords[2] + epsilon && self.my > win -> windowCoords[1] - epsilon && self.my < win -> windowCoords[1] + epsilon) {
                    win32SetCursor(CURSOR_DIAGONALLEFT);
                    win -> resize = -3;
                } else if (self.mx > win -> windowCoords[0] - epsilon && self.mx < win -> windowCoords[0] + epsilon && self.my > win -> windowCoords[3] - epsilon && self.my < win -> windowCoords[3] + epsilon) {
                    win32SetCursor(CURSOR_DIAGONALLEFT);
                    win -> resize = -7;
                } else if (self.mx > win -> windowCoords[0] - epsilon && self.mx < win -> windowCoords[0] + epsilon && self.my > win -> windowCoords[1] - epsilon && self.my < win -> windowCoords[1] + epsilon) {
                    win32SetCursor(CURSOR_DIAGONALRIGHT);
                    win -> resize = -1;
                } else if (self.mx > win -> windowCoords[2] - epsilon && self.mx < win -> windowCoords[2] + epsilon && self.my > win -> windowCoords[3] - epsilon && self.my < win -> windowCoords[3] + epsilon) {
                    win32SetCursor(CURSOR_DIAGONALRIGHT);
                    win -> resize = -5;
                } else if (self.mx > win -> windowCoords[0] && self.mx < win -> windowCoords[2] && self.my > win -> windowCoords[1] - epsilon && self.my < win -> windowCoords[1] + epsilon) {
                    win32SetCursor(CURSOR_UPDOWN);
                    win -> resize = -2;
                } else if (self.mx > win -> windowCoords[2] - epsilon && self.mx < win -> windowCoords[2] + epsilon && self.my > win -> windowCoords[1] && self.my < win -> windowCoords[3]) {
                    win32SetCursor(CURSOR_SIDESIDE);
                    win -> resize = -4;
                } else if (self.mx > win -> windowCoords[0] && self.mx < win -> windowCoords[2] && self.my > win -> windowCoords[3] - epsilon && self.my < win -> windowCoords[3] + epsilon) {
                    win32SetCursor(CURSOR_UPDOWN);
                    win -> resize = -6;
                } else if (self.mx > win -> windowCoords[0] - epsilon && self.mx < win -> windowCoords[0] + epsilon && self.my > win -> windowCoords[1] && self.my < win -> windowCoords[3]) {
                    win32SetCursor(CURSOR_SIDESIDE);
                    win -> resize = -8;
                } else {
                    win -> resize = 0;
                }
            } else {
                win -> resize = 0;
            }
        }
        if (win -> resize > 0) {
            switch (win -> resize) {
                case 1:
                win -> windowCoords[0] = self.mx;
                win -> windowCoords[1] = self.my;
                if (win -> windowCoords[0] > win -> windowCoords[2] - win -> windowMinX) {
                    win -> windowCoords[0] = win -> windowCoords[2] - win -> windowMinX;
                }
                if (win -> windowCoords[1] > win -> windowCoords[3] - win -> windowMinY) {
                    win -> windowCoords[1] = win -> windowCoords[3] - win -> windowMinY;
                }
                break;
                case 2:
                win -> windowCoords[1] = self.my;
                if (win -> windowCoords[1] > win -> windowCoords[3] - win -> windowMinY) {
                    win -> windowCoords[1] = win -> windowCoords[3] - win -> windowMinY;
                }
                break;
                case 3:
                win -> windowCoords[2] = self.mx;
                win -> windowCoords[1] = self.my;
                if (win -> windowCoords[2] < win -> windowCoords[0] + win -> windowMinX) {
                    win -> windowCoords[2] = win -> windowCoords[0] + win -> windowMinX;
                }
                if (win -> windowCoords[1] > win -> windowCoords[3] - win -> windowMinY) {
                    win -> windowCoords[1] = win -> windowCoords[3] - win -> windowMinY;
                }
                break;
                case 4:
                win -> windowCoords[2] = self.mx;
                if (win -> windowCoords[2] < win -> windowCoords[0] + win -> windowMinX) {
                    win -> windowCoords[2] = win -> windowCoords[0] + win -> windowMinX;
                }
                break;
                case 5:
                win -> windowCoords[2] = self.mx;
                win -> windowCoords[3] = self.my;
                if (win -> windowCoords[2] < win -> windowCoords[0] + win -> windowMinX) {
                    win -> windowCoords[2] = win -> windowCoords[0] + win -> windowMinX;
                }
                if (win -> windowCoords[3] < win -> windowCoords[1] + win -> windowMinY) {
                    win -> windowCoords[3] = win -> windowCoords[1] + win -> windowMinY;
                }
                break;
                case 6:
                win -> windowCoords[3] = self.my;
                if (win -> windowCoords[3] < win -> windowCoords[1] + win -> windowMinY) {
                    win -> windowCoords[3] = win -> windowCoords[1] + win -> windowMinY;
                }
                break;
                case 7:
                win -> windowCoords[0] = self.mx;
                win -> windowCoords[3] = self.my;
                if (win -> windowCoords[0] > win -> windowCoords[2] - win -> windowMinX) {
                    win -> windowCoords[0] = win -> windowCoords[2] - win -> windowMinX;
                }
                if (win -> windowCoords[3] < win -> windowCoords[1] + win -> windowMinY) {
                    win -> windowCoords[3] = win -> windowCoords[1] + win -> windowMinY;
                }
                break;
                case 8:
                win -> windowCoords[0] = self.mx;
                if (win -> windowCoords[0] > win -> windowCoords[2] - win -> windowMinX) {
                    win -> windowCoords[0] = win -> windowCoords[2] - win -> windowMinX;
                }
                break;
                default:
                break;
            }
        }
    }
}

void renderOscData() {
    self.oscBottomBound = self.oscTopBound * -1;
    /* render data */
    if (!self.stop) { 
        self.oscRightBound = self.data -> length;
    }
    /* TODO - check this logic to ensure correctness */
    if (self.oscRightBound - self.oscLeftBound < self.oscWindowSize) {
        self.oscLeftBound = self.oscRightBound - self.oscWindowSize;
        if (self.oscLeftBound < 0) {
            self.oscLeftBound = 0;
        }
    }
    if (self.oscRightBound > self.oscLeftBound + self.oscWindowSize) {
        self.oscLeftBound = self.oscRightBound - self.oscWindowSize;
    }
    if (self.windows[0].minimize == 0) {
        /* render window background */
        turtleRectangle(self.windows[0].windowCoords[0], self.windows[0].windowCoords[1], self.windows[0].windowCoords[2], self.windows[0].windowCoords[3], self.themeColors[self.theme + 12], self.themeColors[self.theme + 13], self.themeColors[self.theme + 14], 0);
        turtlePenSize(1);
        turtlePenColor(0, 0, 0);
        turtleGoto(self.windows[0].windowCoords[0], (self.windows[0].windowCoords[1] - self.windows[0].windowTop + self.windows[0].windowCoords[3]) / 2);
        turtlePenDown();
        turtleGoto(self.windows[0].windowCoords[2], (self.windows[0].windowCoords[1] - self.windows[0].windowTop + self.windows[0].windowCoords[3]) / 2
    );
        turtlePenUp();
        /* render data */
        turtlePenSize(1);
        turtlePenColor(self.themeColors[self.theme + 6], self.themeColors[self.theme + 7], self.themeColors[self.theme + 8]);
        double xquantum = (self.windows[0].windowCoords[2] - self.windows[0].windowCoords[0]) / (self.oscRightBound - self.oscLeftBound - 1);
        for (int i = 0; i < self.oscRightBound - self.oscLeftBound; i++) {
            turtleGoto(self.windows[0].windowCoords[0] + i * xquantum, self.windows[0].windowCoords[1] + ((self.data -> data[self.oscLeftBound + i].d - self.oscBottomBound) / (self.oscTopBound - self.oscBottomBound)) * (self.windows[0].windowCoords[3] - self.windows[0].windowTop - self.windows[0].windowCoords[1]));
            turtlePenDown();
        }
        turtlePenUp();
        /* render mouse */
        if (self.windowRender -> data[self.windowRender -> length - 1].i == WINDOW_OSC) {
            if (self.mx > self.windows[0].windowCoords[0] + 15 && self.my > self.windows[0].windowCoords[1] && self.mx < self.windows[0].windowCoords[2] && self.windows[0].windowCoords[3] - self.windows[0].windowTop) {
                int sample = round((self.mx - self.windows[0].windowCoords[0]) / xquantum);
                double sampleX = self.windows[0].windowCoords[0] + sample * xquantum;
                double sampleY = self.windows[0].windowCoords[1] + ((self.data -> data[self.oscLeftBound + sample].d - self.oscBottomBound) / (self.oscTopBound - self.oscBottomBound)) * (self.windows[0].windowCoords[3] - self.windows[0].windowTop - self.windows[0].windowCoords[1]);
                turtlePenColor(215, 215, 215);
                turtlePenSize(4);
                turtleGoto(sampleX, sampleY);
                turtlePenDown();
                turtlePenUp();
                char sampleValue[24];
                sprintf(sampleValue, "%.02lf", self.data -> data[self.oscLeftBound + sample].d);
                double boxLength = textGLGetStringLength(sampleValue, 8);
                double boxX = sampleX - boxLength / 2;
                if (boxX - 15 < self.windows[0].windowCoords[0]) {
                    boxX = self.windows[0].windowCoords[0] + 15;
                }
                if (boxX + boxLength + self.windows[0].windowSide + 5 > self.windows[0].windowCoords[2]) {
                    boxX = self.windows[0].windowCoords[2] - boxLength - self.windows[0].windowSide - 5;
                }
                turtleRectangle(boxX - 3, self.windows[0].windowCoords[3] - self.windows[0].windowTop - 15, boxX + boxLength + 3, self.windows[0].windowCoords[3] - self.windows[0].windowTop - 5, 215, 215, 215, 0);
                turtlePenColor(0, 0, 0);
                textGLWriteString(sampleValue, boxX, self.windows[0].windowCoords[3] - 26, 8, 0);
            }
        }
        /* render side axis */
        turtleRectangle(self.windows[0].windowCoords[0], self.windows[0].windowCoords[1], self.windows[0].windowCoords[0] + 10, self.windows[0].windowCoords[3], 30, 30, 30, 100);
        turtlePenColor(0, 0, 0);
        turtlePenSize(1);
        double ycenter = (self.windows[0].windowCoords[1] + self.windows[0].windowCoords[3] - self.windows[0].windowTop) / 2;
        turtleGoto(self.windows[0].windowCoords[0], ycenter);
        turtlePenDown();
        turtleGoto(self.windows[0].windowCoords[0] + 5, ycenter);
        turtlePenUp();
        int tickMarks = round(self.oscTopBound / 4) * 4;
        double culling = self.oscTopBound;
        while (culling > 60) {
            culling /= 4;
            tickMarks /= 4;
        }
        tickMarks = ceil(tickMarks / 4) * 4;
        double yquantum = (self.windows[0].windowCoords[3] - self.windows[0].windowTop - self.windows[0].windowCoords[1]) / tickMarks;
        for (int i = 1; i < tickMarks; i++) {
            double ypos = self.windows[0].windowCoords[1] + i * yquantum;
            turtleGoto(self.windows[0].windowCoords[0], ypos);
            turtlePenDown();
            int tickLength = 2;
            if (i % (tickMarks / 4) == 0) {
                tickLength = 4;
            }
            turtleGoto(self.windows[0].windowCoords[0] + tickLength, ypos);
            turtlePenUp();
        }
        if (self.windowRender -> data[self.windowRender -> length - 1].i == WINDOW_OSC) {
            int mouseSample = round((self.my - self.windows[0].windowCoords[1]) / yquantum);
            if (mouseSample > 0 && mouseSample < tickMarks) {
                double ypos = self.windows[0].windowCoords[1] + mouseSample * yquantum;
                int tickLength = 2;
                if (mouseSample % (tickMarks / 4) == 0) {
                    tickLength = 4;
                }
                if (self.mx > self.windows[0].windowCoords[0] && self.mx < self.windows[0].windowCoords[0] + 15) {
                    turtleTriangle(self.windows[0].windowCoords[0] + tickLength + 2, ypos, self.windows[0].windowCoords[0] + tickLength + 10, ypos + 6, self.windows[0].windowCoords[0] + tickLength + 10, ypos - 6, 215, 215, 215, 0);
                    char tickValue[24];
                    sprintf(tickValue, "%d", (int) (self.oscTopBound / (tickMarks / 2) * mouseSample - self.oscTopBound));
                    turtlePenColor(215, 215, 215);
                    textGLWriteString(tickValue, self.windows[0].windowCoords[0] + tickLength + 13, ypos, 8, 0);
                }
            }
        }
    }
}

void renderFreqData() {
    (self.windows[1].windowCoords[0], self.windows[1].windowCoords[1], self.windows[1].windowCoords[2], self.windows[1].windowCoords[3], self.themeColors[self.theme + 12], self.themeColors[self.theme + 13], self.themeColors[self.theme + 14], 0);
    /* linear windowing function over 10% of the sample */
    int threshold = (self.oscRightBound - self.oscLeftBound) * 0.1;
    double damping = 1.0 / threshold;
    list_clear(self.freqData);
    for (int i = 0; i < self.oscRightBound - self.oscLeftBound; i++) {
        double dataPoint = self.data -> data[i + self.oscLeftBound].d;
        if (i < threshold) {
            dataPoint *= damping * (i + 1);
        }
        if (i >= (self.oscRightBound - self.oscLeftBound) - threshold) {
            dataPoint *= damping * ((self.oscRightBound - self.oscLeftBound) - (i - 1));
        }
        list_append(self.freqData, (unitype) dataPoint, 'd');
    }
    // list_print(self.freqData);
    // list_t *tempData = FFT(self.freqData);
    // list_print(tempData);
    if (self.windows[1].minimize == 0) {
        /* render window background */
        turtleRectangle(self.windows[1].windowCoords[0], self.windows[1].windowCoords[1], self.windows[1].windowCoords[2], self.windows[1].windowCoords[3], self.themeColors[self.theme + 12], self.themeColors[self.theme + 13], self.themeColors[self.theme + 14], 0);
        turtlePenSize(1);
        turtlePenColor(self.themeColors[self.theme + 6], self.themeColors[self.theme + 7], self.themeColors[self.theme + 8]);
        double xquantum = (self.windows[1].windowCoords[2] - self.windows[1].windowCoords[0]) / (self.freqData -> length - 1);
        for (int i = 0; i < self.freqData -> length; i++) {
            double magnitude = self.freqData -> data[i].d;
            if (magnitude < 0) {
                magnitude = 0;
            }
            turtleGoto(self.windows[1].windowCoords[0] + i * xquantum, self.windows[1].windowCoords[1] + ((magnitude - 0) / (self.topFreq - 0)) * (self.windows[1].windowCoords[3] - self.windows[1].windowTop - self.windows[1].windowCoords[1]));
            turtlePenDown();
        }
        turtlePenUp();
    }
    // list_free(tempData);
}

void renderEditorData() {
    if (self.windows[2].minimize == 0) {
        /* render window background */
        turtleRectangle(self.windows[2].windowCoords[0], self.windows[2].windowCoords[1], self.windows[2].windowCoords[2], self.windows[2].windowCoords[3], self.themeColors[self.theme + 12], self.themeColors[self.theme + 13], self.themeColors[self.theme + 14], 0);
    }
}

void renderOrder() {
    for (int i = 0; i < self.windowRender -> length; i++) {
        switch (self.windowRender -> data[i].i) {
        case WINDOW_OSC:
            renderOscData();
            break;
        case WINDOW_FREQ:
            renderFreqData();
            break;
        case WINDOW_EDITOR:
            renderEditorData();
            break;
        default:
            break;    
        }
        renderWindow(ilog2(self.windowRender -> data[i].i));
    }
    /* render bottom bar */
    turtleRectangle(-320, -180, 320, -170, self.themeColors[self.theme + 3], self.themeColors[self.theme + 4], self.themeColors[self.theme + 5], 50);
    for (int i = 0; i < self.windowRender -> length; i++) {
        /* write title */
        int minX = -320 + (1) + 50 * i;
        int minY = -179;
        int maxX = -320 + (49) + 50 * i;
        int maxY = -171;
        if (!self.mouseDown && self.mx >= minX && self.mx <= maxX && self.my >= minY && self.my <= maxY) {
            turtleRectangle(minX, minY, maxX, maxY, self.themeColors[self.theme + 12], self.themeColors[self.theme + 13], self.themeColors[self.theme + 14], 0);
            self.windows[i].click = 2;
        } else {
            turtleRectangle(minX, minY, maxX, maxY, self.themeColors[self.theme + 3], self.themeColors[self.theme + 4], self.themeColors[self.theme + 5], 0);
        }
        textGLWriteString(self.windows[i].title, -320 + (50 / 2) + 50 * i, -175, 5, 50);
    }
}

void parseRibbonOutput() {
    if (ribbonRender.output[0] == 1) {
        ribbonRender.output[0] = 0; // untoggle
        if (ribbonRender.output[1] == 0) { // file
            if (ribbonRender.output[2] == 1) { // new
                printf("New file created\n");
                strcpy(win32FileDialog.selectedFilename, "null");
            }
            if (ribbonRender.output[2] == 2) { // save
                if (strcmp(win32FileDialog.selectedFilename, "null") == 0) {
                    if (win32FileDialogPrompt(1, "") != -1) {
                        printf("Saved to: %s\n", win32FileDialog.selectedFilename);
                    }
                } else {
                    printf("Saved to: %s\n", win32FileDialog.selectedFilename);
                }
            }
            if (ribbonRender.output[2] == 3) { // save as
                if (win32FileDialogPrompt(1, "") != -1) {
                    printf("Saved to: %s\n", win32FileDialog.selectedFilename);
                }
            }
            if (ribbonRender.output[2] == 4) { // load
                if (win32FileDialogPrompt(0, "") != -1) {
                    printf("Loaded data from: %s\n", win32FileDialog.selectedFilename);
                }
            }
        }
        if (ribbonRender.output[1] == 1) { // edit
            if (ribbonRender.output[2] == 1) { // undo
                printf("Undo\n");
            }
            if (ribbonRender.output[2] == 2) { // redo
                printf("Redo\n");
            }
            if (ribbonRender.output[2] == 3) { // cut
                win32ClipboardSetText("test123");
                printf("Cut \"test123\" to clipboard!\n");
            }
            if (ribbonRender.output[2] == 4) { // copy
                win32ClipboardSetText("test345");
                printf("Copied \"test345\" to clipboard!\n");
            }
            if (ribbonRender.output[2] == 5) { // paste
                win32ClipboardGetText();
                printf("Pasted \"%s\" from clipboard!\n", win32Clipboard.text);
            }
        }
        if (ribbonRender.output[1] == 2) { // view
            if (ribbonRender.output[2] == 1) { // change theme
                if (self.theme == 0) {
                    self.theme = self.themeDark;
                } else {
                    self.theme = 0;
                }
                if (self.theme == 0) {
                    ribbonLightTheme();
                    popupLightTheme();
                } else {
                    ribbonDarkTheme();
                    popupDarkTheme();
                }
                turtleBgColor(self.themeColors[self.theme + 0], self.themeColors[self.theme + 1], self.themeColors[self.theme + 2]);
            } 
            if (ribbonRender.output[2] == 2) { // GLFW
                printf("GLFW settings\n");
            } 
        }
    }
}

int randomInt(int lowerBound, int upperBound) { // random integer between lower and upper bound (inclusive)
    return (rand() % (upperBound - lowerBound + 1) + lowerBound);
}

double randomDouble(double lowerBound, double upperBound) { // random double between lower and upper bound
    return (rand() * (upperBound - lowerBound) / RAND_MAX + lowerBound); // probably works idk
}

void utilLoop() {
    turtleGetMouseCoords(); // get the mouse coordinates
    self.mouseDown = turtleMouseDown();
    if (turtle.mouseX > 320) { // bound mouse coordinates to window coordinates
        self.mx = 320;
    } else {
        if (turtle.mouseX < -320) {
            self.mx = -320;
        } else {
            self.mx = turtle.mouseX;
        }
    }
    if (turtle.mouseY > 180) {
        self.my = 180;
    } else {
        if (turtle.mouseY < -180) {
            self.my = -180;
        } else {
            self.my = turtle.mouseY;
        }
    }
    self.mw = turtleMouseWheel();
    if (turtleKeyPressed(GLFW_KEY_UP)) {
        self.mw += 1;
    }
    if (turtleKeyPressed(GLFW_KEY_DOWN)) {
        self.mw -= 1;
    }
    turtleClear();
}

int main(int argc, char *argv[]) {
    GLFWwindow* window;
    /* Initialize glfw */
    if (!glfwInit()) {
        return -1;
    }
    glfwWindowHint(GLFW_SAMPLES, 4); // MSAA (Anti-Aliasing) with 4 samples (must be done before window is created (?))

    /* Create a windowed mode window and its OpenGL context */
    const GLFWvidmode *monitorSize = glfwGetVideoMode(glfwGetPrimaryMonitor());
    int windowHeight = monitorSize -> height * 0.85;
    window = glfwCreateWindow(windowHeight * 16 / 9, windowHeight, "EMPV", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetWindowSizeLimits(window, 128, 72, 3456, 1944);
    int width, height, oldWidth, oldHeight;
    glfwGetWindowSize(window, &oldWidth, &oldHeight);

    /* initialize turtle */
    turtleInit(window, -320, -180, 320, 180);
    /* initialise textGL */
    textGLInit(window, "include/fontBez.tgl");
    /* initialise ribbon */
    ribbonInit(window, "include/ribbonConfig.txt");
    ribbonDarkTheme(); // dark theme preset
    /* initialise win32tools */
    win32ToolsInit();
    win32FileDialogAddExtension("txt"); // add txt to extension restrictions
    win32FileDialogAddExtension("csv"); // add csv to extension restrictions
    /* initialise win32tcp */
    win32tcpInit("192.168.1.10");

    int tps = 120; // ticks per second (locked to fps in this case)
    uint64_t tick = 0;

    clock_t start;
    clock_t end;

    init(); // initialise empv
    turtleBgColor(self.themeColors[self.theme + 0], self.themeColors[self.theme + 1], self.themeColors[self.theme + 2]);

    while (turtle.close == 0) { // main loop
        start = clock();
        double sinValue1 = sin(tick / 5.0) * 25;
        double sinValue2 = sin(tick / 3.37) * 25;
        list_append(self.data, (unitype) (sinValue1 + sinValue2), 'd');
        // list_append(self.data, (unitype) (sinValue2), 'd');
        utilLoop();
        turtleGetMouseCoords(); // get the mouse coordinates (turtle.mouseX, turtle.mouseY)
        turtleClear();
        renderOrder();
        ribbonUpdate();
        parseRibbonOutput();
        glfwGetWindowSize(window, &width, &height);
        if (width != oldWidth || height != oldHeight) {
            printf("window size change\n");
            oldWidth = width;
            oldHeight = height;
            turtleSetWorldCoordinates(-320, -180, 320, 180); // doesn't work correctly
        }
        turtleUpdate(); // update the screen
        win32tcpUpdate();
        end = clock();
        while ((double) (end - start) / CLOCKS_PER_SEC < (1.0 / tps)) {
            end = clock();
        }
        tick++;
    }
    turtleFree();
    glfwTerminate();
    return 0;
}