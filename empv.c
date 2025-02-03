/* EMPV for windows
Features:
- Oscilloscope-like interface
- Frequency graph
*/

#include "include/ribbon.h"
#include "include/popup.h"
#include "include/win32Tools.h"
#include <time.h>

#define DIAL_LINEAR    0
#define DIAL_LOG       1
#define DIAL_EXP       2

typedef struct { // dial
    char label[24];
    int type;
    int status[2];
    double size;
    double position[2];
    double range[2];
    double *variable;
} dial_t;

typedef struct { // switch
    char label[24];
    int status;
    double size;
    double position[2];
    int *variable;
} switch_t;

typedef struct { // all the empv shared state is here
    /* general */
        list_t *data; // a list of all data collected through ethernet
        list_t *logVariables; // a list of variables logged on the AMDC
        /* mouse variables */
        double mx; // mouseX
        double my; // mouseY
        double mw; // mouse wheel
        char mouseDown;  // mouse down
        double anchorX;
        double anchorY;
        double anchorPoints[4];
    /* oscilloscope view */
        /* window variables */
        int leftBound; // left bound (index in data list)
        int rightBound; // right bound (index in data list)
        double bottomBound; // bottom bound (y value)
        double topBound; // top bound (y value)
        double windowSize; // size of window (index in data list)
        double oscWindowCoords[4]; // coordinates of window on canvas
        double oscWindowTop; // size of window top bar
        double oscWindowSide; // size of window side bar
        double oscWindowMinX; // window size on canvas minimum
        double oscWindowMinY; // window size on canvas minimum
        int oscMove; // moving window
        int oscResize; // resizing window
        int stop;
        /* ui variables */
        dial_t dials[2];
        double dialAnchorX;
        double dialAnchorY;
        switch_t switches[1];
        /* color variables */
        int theme;
        int themeDark;
        double themeColors[90];
    /* frequency view */
        list_t *freqData;
        double freqWindowCoords[4]; // coordinates of window on canvas
        double freqWindowTop; // size of window top bar
        double freqWindowSide; // size of window side bar
        double freqWindowMinX; // window size on canvas minimum
        double freqWindowMinY; // window size on canvas minimum
        int freqMove; // moving window
        int freqResize; // resizing window
} empv_t;

empv_t self; // global state

void dialInit(dial_t *dial, char *label, double *variable, int type, double yScale, double yOffset, double size, double bottom, double top) {
    memcpy(dial -> label, label, strlen(label) + 1);
    dial -> status[0] = 0;
    dial -> type = type;
    dial -> position[0] = yScale;
    dial -> position[1] = yOffset;
    dial -> size = size;
    dial -> range[0] = bottom;
    dial -> range[1] = top;
    dial -> variable = variable;
}

void switchInit(switch_t *switchp, char *label, int *variable, double yScale, double yOffset, double size) {
    memcpy(switchp -> label, label, strlen(label) + 1);
    switchp -> status = 0;
    switchp -> position[0] = yScale;
    switchp -> position[1] = yOffset;
    switchp -> size = size;
    switchp -> variable = variable;
}

void init() { // initialises the empv variabes (shared state)
    /* data */
    self.data = list_init();
    self.logVariables = list_init();
    /* window */
    self.leftBound = 0;
    self.rightBound = 0;
    self.bottomBound = -100;
    self.topBound = 100;
    self.windowSize = 200;
    self.oscWindowCoords[0] = -317;
    self.oscWindowCoords[1] = 25;
    self.oscWindowCoords[2] = 317;
    self.oscWindowCoords[3] = 167;
    self.oscWindowTop = 15;
    self.oscWindowSide = 50;
    self.oscWindowMinX = 60 + self.oscWindowSide;
    self.oscWindowMinY = 120 + self.oscWindowTop;
    self.oscMove = 0;
    self.anchorX = 0;
    self.anchorY = 0;
    self.oscResize = 0;
    self.stop = 0;
    /* ui */
    dialInit(&self.dials[0], "X Scale", &self.windowSize, DIAL_EXP, 1, -25 - self.oscWindowTop, 8, 1, 1000);
    dialInit(&self.dials[1], "Y Scale", &self.topBound, DIAL_EXP, 1, -65 - self.oscWindowTop, 8, 50, 10000);
    self.dialAnchorX = 0;
    self.dialAnchorY = 0;
    switchInit(&self.switches[0], "Pause", &self.stop, 1, -100 - self.oscWindowTop, 8);
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

    /* frequency */
    self.freqData = list_init();
    self.freqWindowCoords[0] = -317;
    self.freqWindowCoords[1] = -120;
    self.freqWindowCoords[2] = 317;
    self.freqWindowCoords[3] = 22;
    self.freqWindowTop = 15;
    self.freqWindowSide = 100;
    self.freqWindowMinX = 52 + self.freqWindowSide;
    self.freqWindowMinY = 120 + self.freqWindowTop;
    self.freqMove = 0;
    self.freqResize = 0;
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

void dialTick() {
    for (int i = 0; i < sizeof(self.dials) / sizeof(dial_t); i++) {
        textGLWriteString(self.dials[i].label, (self.oscWindowCoords[2] - self.oscWindowSide + self.oscWindowCoords[2]) / 2, self.oscWindowCoords[1] + (self.oscWindowCoords[3] - self.oscWindowCoords[1]) * self.dials[i].position[0] + self.dials[i].position[1] + 15, 7, 50);
        turtlePenSize(self.dials[i].size * 2);
        double dialX = (self.oscWindowCoords[2] - self.oscWindowSide + self.oscWindowCoords[2]) / 2;
        double dialY = self.oscWindowCoords[1] + (self.oscWindowCoords[3] - self.oscWindowCoords[1]) * self.dials[i].position[0] + self.dials[i].position[1];
        turtleGoto(dialX, dialY);
        turtlePenDown();
        turtlePenUp();
        turtlePenSize(self.dials[i].size * 2 * 0.8);
        turtlePenColor(self.themeColors[self.theme + 3], self.themeColors[self.theme + 4], self.themeColors[self.theme + 5]);
        turtlePenDown();
        turtlePenUp();
        turtlePenColor(self.themeColors[self.theme + 9], self.themeColors[self.theme + 10], self.themeColors[self.theme + 11]);
        turtlePenSize(1);
        turtlePenDown();
        double dialAngle;
        if (self.dials[i].type == DIAL_LOG) {
            dialAngle = pow(360, (*(self.dials[i].variable) - self.dials[i].range[0]) / (self.dials[i].range[1] - self.dials[i].range[0]));
        } else if (self.dials[i].type == DIAL_LINEAR) {
            dialAngle = (*(self.dials[i].variable) - self.dials[i].range[0]) / (self.dials[i].range[1] - self.dials[i].range[0]) * 360;
        } else if (self.dials[i].type == DIAL_EXP) {
            dialAngle = 360 * (log(((*(self.dials[i].variable) - self.dials[i].range[0]) / (self.dials[i].range[1] - self.dials[i].range[0])) * 360 + 1) / log(361));
        }
        turtleGoto(dialX + sin(dialAngle / 57.2958) * self.dials[i].size, dialY + cos(dialAngle / 57.2958) * self.dials[i].size);
        turtlePenUp();
        if (self.mouseDown) {
            if (self.dials[i].status[0] < 0) {
                self.dialAnchorX = dialX;
                self.dialAnchorY = dialY;
                self.dials[i].status[0] *= -1;
                self.dials[i].status[1] = self.mx - dialX;
            }
        } else {
            if (self.mx > dialX - self.dials[i].size && self.mx < dialX + self.dials[i].size && self.my > dialY - self.dials[i].size && self.my < dialY + self.dials[i].size) {
                self.dials[i].status[0] = -1;
            } else {
                self.dials[i].status[0] = 0;
            }
        }
        if (self.dials[i].status[0] > 0) {
            dialAngle = angleBetween(self.dialAnchorX, self.dialAnchorY, self.mx, self.my);
            if (self.my < self.dialAnchorY) {
                self.dials[i].status[1] = self.mx - dialX;
            }
            if ((dialAngle < 0.0001 || dialAngle > 180) && self.my > self.dialAnchorY && self.dials[i].status[1] >= 0) {
                dialAngle = 0.0001;
            }
            if ((dialAngle > 359.999 || dialAngle < 180) && self.my > self.dialAnchorY && self.dials[i].status[1] < 0) {
                dialAngle = 359.999;
            }
            if (self.dials[i].type == DIAL_LOG) {
                *(self.dials[i].variable) = self.dials[i].range[0] + (self.dials[i].range[1] - self.dials[i].range[0]) * (log(dialAngle) / log(360));
            } else if (self.dials[i].type == DIAL_LINEAR) {
                *(self.dials[i].variable) = self.dials[i].range[0] + ((self.dials[i].range[1] - self.dials[i].range[0]) * dialAngle / 360);
            } else if (self.dials[i].type == DIAL_EXP) {
                *(self.dials[i].variable) = self.dials[i].range[0] + (self.dials[i].range[1] - self.dials[i].range[0]) * ((pow(361, dialAngle / 360) - 1) / 360);
            }
        }
        char bubble[24];
        sprintf(bubble, "%.0lf", *(self.dials[i].variable));
        textGLWriteString(bubble, dialX + self.dials[i].size + 3, dialY, 4, 0);
    }
}

void switchTick() {
    for (int i = 0; i < sizeof(self.switches) / sizeof(switch_t); i++) {
        textGLWriteString(self.switches[i].label, (self.oscWindowCoords[2] - self.oscWindowSide + self.oscWindowCoords[2]) / 2, self.oscWindowCoords[1] + (self.oscWindowCoords[3] - self.oscWindowCoords[1]) * self.switches[i].position[0] + self.switches[i].position[1] + 15, 7, 50);
        turtlePenColor(self.themeColors[self.theme + 12], self.themeColors[self.theme + 13], self.themeColors[self.theme + 14]);
        turtlePenSize(self.switches[i].size * 1.2);
        double switchX = (self.oscWindowCoords[2] - self.oscWindowSide + self.oscWindowCoords[2]) / 2;
        double switchY = self.oscWindowCoords[1] + (self.oscWindowCoords[3] - self.oscWindowCoords[1]) * self.switches[i].position[0] + self.switches[i].position[1];
        turtleGoto(switchX - self.switches[i].size * 0.8, switchY);
        turtlePenDown();
        turtleGoto(switchX + self.switches[i].size * 0.8, switchY);
        turtlePenUp();
        turtlePenSize(self.switches[i].size);
        turtlePenColor(self.themeColors[self.theme + 9], self.themeColors[self.theme + 10], self.themeColors[self.theme + 11]);
        if (*(self.switches[i].variable)) {
            // turtlePenColor(self.themeColors[self.theme + 15], self.themeColors[self.theme + 16], self.themeColors[self.theme + 17]);
            turtleGoto(switchX + self.switches[i].size * 0.8, switchY);
        } else {
            // turtlePenColor(self.themeColors[self.theme + 18], self.themeColors[self.theme + 19], self.themeColors[self.theme + 20]);
            turtleGoto(switchX - self.switches[i].size * 0.8, switchY);
        }
        turtlePenDown();
        turtlePenUp();
        if (self.mouseDown) {
            if (self.switches[i].status < 0) {
                self.switches[i].status *= -1;
            }
        } else {
            if (self.mx > switchX - self.dials[i].size && self.mx < switchX + self.dials[i].size && self.my > switchY - self.dials[i].size && self.my < switchY + self.dials[i].size) {
                self.switches[i].status = -1;
            } else {
                self.switches[i].status = 0;
            }
        }
        if (self.switches[i].status > 0) {
            if (*(self.switches[i].variable)) {
                *(self.switches[i].variable) = 0;
            } else {
                *(self.switches[i].variable) = 1;
            }
            self.switches[i].status = 0;
        }
    }
}

void renderOscWindow() {
    /* render window */
    turtlePenSize(2);
    turtlePenColor(self.themeColors[self.theme + 3], self.themeColors[self.theme + 4], self.themeColors[self.theme + 5]);
    turtleGoto(self.oscWindowCoords[0], self.oscWindowCoords[1]);
    turtlePenDown();
    turtleGoto(self.oscWindowCoords[0], self.oscWindowCoords[3]);
    turtleGoto(self.oscWindowCoords[2], self.oscWindowCoords[3]);
    turtleGoto(self.oscWindowCoords[2], self.oscWindowCoords[1]);
    turtleGoto(self.oscWindowCoords[0], self.oscWindowCoords[1]);
    turtlePenUp();
    turtleRentangle(self.oscWindowCoords[0], self.oscWindowCoords[3], self.oscWindowCoords[2], self.oscWindowCoords[3] - self.oscWindowTop, self.themeColors[self.theme + 3], self.themeColors[self.theme + 4], self.themeColors[self.theme + 5], 0);
    turtleRentangle(self.oscWindowCoords[2] - self.oscWindowSide, self.oscWindowCoords[1], self.oscWindowCoords[2], self.oscWindowCoords[3], self.themeColors[self.theme + 3], self.themeColors[self.theme + 4], self.themeColors[self.theme + 5], 40);
    turtlePenColor(self.themeColors[self.theme + 9], self.themeColors[self.theme + 10], self.themeColors[self.theme + 11]);
    /* write title */
    textGLWriteString("Oscilloscope", (self.oscWindowCoords[0] + self.oscWindowCoords[2] - self.oscWindowSide) / 2, self.oscWindowCoords[3] - self.oscWindowTop * 0.45, self.oscWindowTop * 0.5, 50);
    /* draw sidebar UI elements */
    dialTick();
    switchTick();
    /* window move and resize logic */
    /* move */
    if (self.mouseDown) {
        if (self.oscMove < 0) {
            self.anchorX = self.mx;
            self.anchorY = self.my;
            memcpy(self.anchorPoints, self.oscWindowCoords, sizeof(double) * 4);
            self.oscMove *= -1;
        }
    } else {
        if (self.mx > self.oscWindowCoords[0] && self.mx < self.oscWindowCoords[2] && self.my > self.oscWindowCoords[3] - self.oscWindowTop && self.my < self.oscWindowCoords[3]) {
            self.oscMove = -1;
        } else {
            self.oscMove = 0;
        }
    }
    if (self.oscMove > 0) {
        self.oscWindowCoords[0] = self.anchorPoints[0] + self.mx - self.anchorX;
        self.oscWindowCoords[1] = self.anchorPoints[1] + self.my - self.anchorY;
        self.oscWindowCoords[2] = self.anchorPoints[2] + self.mx - self.anchorX;
        self.oscWindowCoords[3] = self.anchorPoints[3] + self.my - self.anchorY;
    }
    /* resize */
    double epsilon = 3;
    if (self.mouseDown) {
        if (self.oscResize < 0) {
            self.oscResize *= -1;
            self.oscMove = 0; // don't move and resize
            switch (self.oscResize) {
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
        if (self.mx > self.oscWindowCoords[2] - epsilon && self.mx < self.oscWindowCoords[2] + epsilon && self.my > self.oscWindowCoords[1] - epsilon && self.my < self.oscWindowCoords[1] + epsilon) {
            win32SetCursor(CURSOR_DIAGONALLEFT);
            self.oscResize = -3;
        } else if (self.mx > self.oscWindowCoords[0] - epsilon && self.mx < self.oscWindowCoords[0] + epsilon && self.my > self.oscWindowCoords[3] - epsilon && self.my < self.oscWindowCoords[3] + epsilon) {
            win32SetCursor(CURSOR_DIAGONALLEFT);
            self.oscResize = -7;
        } else if (self.mx > self.oscWindowCoords[0] - epsilon && self.mx < self.oscWindowCoords[0] + epsilon && self.my > self.oscWindowCoords[1] - epsilon && self.my < self.oscWindowCoords[1] + epsilon) {
            win32SetCursor(CURSOR_DIAGONALRIGHT);
            self.oscResize = -1;
        } else if (self.mx > self.oscWindowCoords[2] - epsilon && self.mx < self.oscWindowCoords[2] + epsilon && self.my > self.oscWindowCoords[3] - epsilon && self.my < self.oscWindowCoords[3] + epsilon) {
            win32SetCursor(CURSOR_DIAGONALRIGHT);
            self.oscResize = -5;
        } else if (self.mx > self.oscWindowCoords[0] && self.mx < self.oscWindowCoords[2] && self.my > self.oscWindowCoords[1] - epsilon && self.my < self.oscWindowCoords[1] + epsilon) {
            win32SetCursor(CURSOR_UPDOWN);
            self.oscResize = -2;
        } else if (self.mx > self.oscWindowCoords[2] - epsilon && self.mx < self.oscWindowCoords[2] + epsilon && self.my > self.oscWindowCoords[1] && self.my < self.oscWindowCoords[3]) {
            win32SetCursor(CURSOR_SIDESIDE);
            self.oscResize = -4;
        } else if (self.mx > self.oscWindowCoords[0] && self.mx < self.oscWindowCoords[2] && self.my > self.oscWindowCoords[3] - epsilon && self.my < self.oscWindowCoords[3] + epsilon) {
            win32SetCursor(CURSOR_UPDOWN);
            self.oscResize = -6;
        } else if (self.mx > self.oscWindowCoords[0] - epsilon && self.mx < self.oscWindowCoords[0] + epsilon && self.my > self.oscWindowCoords[1] && self.my < self.oscWindowCoords[3]) {
            win32SetCursor(CURSOR_SIDESIDE);
            self.oscResize = -8;
        } else {
            self.oscResize = 0;
        }
    }
    if (self.oscResize > 0) {
        switch (self.oscResize) {
            case 1:
            self.oscWindowCoords[0] = self.mx;
            self.oscWindowCoords[1] = self.my;
            if (self.oscWindowCoords[0] > self.oscWindowCoords[2] - self.oscWindowMinX) {
                self.oscWindowCoords[0] = self.oscWindowCoords[2] - self.oscWindowMinX;
            }
            if (self.oscWindowCoords[1] > self.oscWindowCoords[3] - self.oscWindowMinY) {
                self.oscWindowCoords[1] = self.oscWindowCoords[3] - self.oscWindowMinY;
            }
            break;
            case 2:
            self.oscWindowCoords[1] = self.my;
            if (self.oscWindowCoords[1] > self.oscWindowCoords[3] - self.oscWindowMinY) {
                self.oscWindowCoords[1] = self.oscWindowCoords[3] - self.oscWindowMinY;
            }
            break;
            case 3:
            self.oscWindowCoords[2] = self.mx;
            self.oscWindowCoords[1] = self.my;
            if (self.oscWindowCoords[2] < self.oscWindowCoords[0] + self.oscWindowMinX) {
                self.oscWindowCoords[2] = self.oscWindowCoords[0] + self.oscWindowMinX;
            }
            if (self.oscWindowCoords[1] > self.oscWindowCoords[3] - self.oscWindowMinY) {
                self.oscWindowCoords[1] = self.oscWindowCoords[3] - self.oscWindowMinY;
            }
            break;
            case 4:
            self.oscWindowCoords[2] = self.mx;
            if (self.oscWindowCoords[2] < self.oscWindowCoords[0] + self.oscWindowMinX) {
                self.oscWindowCoords[2] = self.oscWindowCoords[0] + self.oscWindowMinX;
            }
            break;
            case 5:
            self.oscWindowCoords[2] = self.mx;
            self.oscWindowCoords[3] = self.my;
            if (self.oscWindowCoords[2] < self.oscWindowCoords[0] + self.oscWindowMinX) {
                self.oscWindowCoords[2] = self.oscWindowCoords[0] + self.oscWindowMinX;
            }
            if (self.oscWindowCoords[3] < self.oscWindowCoords[1] + self.oscWindowMinY) {
                self.oscWindowCoords[3] = self.oscWindowCoords[1] + self.oscWindowMinY;
            }
            break;
            case 6:
            self.oscWindowCoords[3] = self.my;
            if (self.oscWindowCoords[3] < self.oscWindowCoords[1] + self.oscWindowMinY) {
                self.oscWindowCoords[3] = self.oscWindowCoords[1] + self.oscWindowMinY;
            }
            break;
            case 7:
            self.oscWindowCoords[0] = self.mx;
            self.oscWindowCoords[3] = self.my;
            if (self.oscWindowCoords[0] > self.oscWindowCoords[2] - self.oscWindowMinX) {
                self.oscWindowCoords[0] = self.oscWindowCoords[2] - self.oscWindowMinX;
            }
            if (self.oscWindowCoords[3] < self.oscWindowCoords[1] + self.oscWindowMinY) {
                self.oscWindowCoords[3] = self.oscWindowCoords[1] + self.oscWindowMinY;
            }
            break;
            case 8:
            self.oscWindowCoords[0] = self.mx;
            if (self.oscWindowCoords[0] > self.oscWindowCoords[2] - self.oscWindowMinX) {
                self.oscWindowCoords[0] = self.oscWindowCoords[2] - self.oscWindowMinX;
            }
            break;
            default:
            break;
        }
    }
}

void renderFreqWindow() {
    /* render window */
    turtlePenSize(2);
    turtlePenColor(self.themeColors[self.theme + 3], self.themeColors[self.theme + 4], self.themeColors[self.theme + 5]);
    turtleGoto(self.freqWindowCoords[0], self.freqWindowCoords[1]);
    turtlePenDown();
    turtleGoto(self.freqWindowCoords[0], self.freqWindowCoords[3]);
    turtleGoto(self.freqWindowCoords[2], self.freqWindowCoords[3]);
    turtleGoto(self.freqWindowCoords[2], self.freqWindowCoords[1]);
    turtleGoto(self.freqWindowCoords[0], self.freqWindowCoords[1]);
    turtlePenUp();
    turtleRentangle(self.freqWindowCoords[0], self.freqWindowCoords[3], self.freqWindowCoords[2], self.freqWindowCoords[3] - self.freqWindowTop, self.themeColors[self.theme + 3], self.themeColors[self.theme + 4], self.themeColors[self.theme + 5], 0);
    turtleRentangle(self.freqWindowCoords[2] - self.freqWindowSide, self.freqWindowCoords[1], self.freqWindowCoords[2], self.freqWindowCoords[3], self.themeColors[self.theme + 3], self.themeColors[self.theme + 4], self.themeColors[self.theme + 5], 40);
    turtlePenColor(self.themeColors[self.theme + 9], self.themeColors[self.theme + 10], self.themeColors[self.theme + 11]);
    /* write title */
    textGLWriteString("Frequency", (self.freqWindowCoords[0] + self.freqWindowCoords[2] - self.freqWindowSide) / 2, self.freqWindowCoords[3] - self.freqWindowTop * 0.45, self.freqWindowTop * 0.5, 50);
    /* draw sidebar UI elements */
    dialTick();
    switchTick();
    /* window move and resize logic */
    /* move */
    if (self.mouseDown) {
        if (self.freqMove < 0) {
            self.anchorX = self.mx;
            self.anchorY = self.my;
            memcpy(self.anchorPoints, self.freqWindowCoords, sizeof(double) * 4);
            self.freqMove *= -1;
        }
    } else {
        if (self.mx > self.freqWindowCoords[0] && self.mx < self.freqWindowCoords[2] && self.my > self.freqWindowCoords[3] - self.freqWindowTop && self.my < self.freqWindowCoords[3]) {
            self.freqMove = -1;
        } else {
            self.freqMove = 0;
        }
    }
    if (self.freqMove > 0) {
        self.freqWindowCoords[0] = self.anchorPoints[0] + self.mx - self.anchorX;
        self.freqWindowCoords[1] = self.anchorPoints[1] + self.my - self.anchorY;
        self.freqWindowCoords[2] = self.anchorPoints[2] + self.mx - self.anchorX;
        self.freqWindowCoords[3] = self.anchorPoints[3] + self.my - self.anchorY;
    }
    /* resize */
    double epsilon = 3;
    if (self.mouseDown) {
        if (self.freqResize < 0) {
            self.freqResize *= -1;
            self.freqMove = 0; // don't move and resize
            switch (self.freqResize) {
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
        if (self.mx > self.freqWindowCoords[2] - epsilon && self.mx < self.freqWindowCoords[2] + epsilon && self.my > self.freqWindowCoords[1] - epsilon && self.my < self.freqWindowCoords[1] + epsilon) {
            win32SetCursor(CURSOR_DIAGONALLEFT);
            self.freqResize = -3;
        } else if (self.mx > self.freqWindowCoords[0] - epsilon && self.mx < self.freqWindowCoords[0] + epsilon && self.my > self.freqWindowCoords[3] - epsilon && self.my < self.freqWindowCoords[3] + epsilon) {
            win32SetCursor(CURSOR_DIAGONALLEFT);
            self.freqResize = -7;
        } else if (self.mx > self.freqWindowCoords[0] - epsilon && self.mx < self.freqWindowCoords[0] + epsilon && self.my > self.freqWindowCoords[1] - epsilon && self.my < self.freqWindowCoords[1] + epsilon) {
            win32SetCursor(CURSOR_DIAGONALRIGHT);
            self.freqResize = -1;
        } else if (self.mx > self.freqWindowCoords[2] - epsilon && self.mx < self.freqWindowCoords[2] + epsilon && self.my > self.freqWindowCoords[3] - epsilon && self.my < self.freqWindowCoords[3] + epsilon) {
            win32SetCursor(CURSOR_DIAGONALRIGHT);
            self.freqResize = -5;
        } else if (self.mx > self.freqWindowCoords[0] && self.mx < self.freqWindowCoords[2] && self.my > self.freqWindowCoords[1] - epsilon && self.my < self.freqWindowCoords[1] + epsilon) {
            win32SetCursor(CURSOR_UPDOWN);
            self.freqResize = -2;
        } else if (self.mx > self.freqWindowCoords[2] - epsilon && self.mx < self.freqWindowCoords[2] + epsilon && self.my > self.freqWindowCoords[1] && self.my < self.freqWindowCoords[3]) {
            win32SetCursor(CURSOR_SIDESIDE);
            self.freqResize = -4;
        } else if (self.mx > self.freqWindowCoords[0] && self.mx < self.freqWindowCoords[2] && self.my > self.freqWindowCoords[3] - epsilon && self.my < self.freqWindowCoords[3] + epsilon) {
            win32SetCursor(CURSOR_UPDOWN);
            self.freqResize = -6;
        } else if (self.mx > self.freqWindowCoords[0] - epsilon && self.mx < self.freqWindowCoords[0] + epsilon && self.my > self.freqWindowCoords[1] && self.my < self.freqWindowCoords[3]) {
            win32SetCursor(CURSOR_SIDESIDE);
            self.freqResize = -8;
        } else {
            self.freqResize = 0;
        }
    }
    if (self.freqResize > 0) {
        switch (self.freqResize) {
            case 1:
            self.freqWindowCoords[0] = self.mx;
            self.freqWindowCoords[1] = self.my;
            if (self.freqWindowCoords[0] > self.freqWindowCoords[2] - self.freqWindowMinX) {
                self.freqWindowCoords[0] = self.freqWindowCoords[2] - self.freqWindowMinX;
            }
            if (self.freqWindowCoords[1] > self.freqWindowCoords[3] - self.freqWindowMinY) {
                self.freqWindowCoords[1] = self.freqWindowCoords[3] - self.freqWindowMinY;
            }
            break;
            case 2:
            self.freqWindowCoords[1] = self.my;
            if (self.freqWindowCoords[1] > self.freqWindowCoords[3] - self.freqWindowMinY) {
                self.freqWindowCoords[1] = self.freqWindowCoords[3] - self.freqWindowMinY;
            }
            break;
            case 3:
            self.freqWindowCoords[2] = self.mx;
            self.freqWindowCoords[1] = self.my;
            if (self.freqWindowCoords[2] < self.freqWindowCoords[0] + self.freqWindowMinX) {
                self.freqWindowCoords[2] = self.freqWindowCoords[0] + self.freqWindowMinX;
            }
            if (self.freqWindowCoords[1] > self.freqWindowCoords[3] - self.freqWindowMinY) {
                self.freqWindowCoords[1] = self.freqWindowCoords[3] - self.freqWindowMinY;
            }
            break;
            case 4:
            self.freqWindowCoords[2] = self.mx;
            if (self.freqWindowCoords[2] < self.freqWindowCoords[0] + self.freqWindowMinX) {
                self.freqWindowCoords[2] = self.freqWindowCoords[0] + self.freqWindowMinX;
            }
            break;
            case 5:
            self.freqWindowCoords[2] = self.mx;
            self.freqWindowCoords[3] = self.my;
            if (self.freqWindowCoords[2] < self.freqWindowCoords[0] + self.freqWindowMinX) {
                self.freqWindowCoords[2] = self.freqWindowCoords[0] + self.freqWindowMinX;
            }
            if (self.freqWindowCoords[3] < self.freqWindowCoords[1] + self.freqWindowMinY) {
                self.freqWindowCoords[3] = self.freqWindowCoords[1] + self.freqWindowMinY;
            }
            break;
            case 6:
            self.freqWindowCoords[3] = self.my;
            if (self.freqWindowCoords[3] < self.freqWindowCoords[1] + self.freqWindowMinY) {
                self.freqWindowCoords[3] = self.freqWindowCoords[1] + self.freqWindowMinY;
            }
            break;
            case 7:
            self.freqWindowCoords[0] = self.mx;
            self.freqWindowCoords[3] = self.my;
            if (self.freqWindowCoords[0] > self.freqWindowCoords[2] - self.freqWindowMinX) {
                self.freqWindowCoords[0] = self.freqWindowCoords[2] - self.freqWindowMinX;
            }
            if (self.freqWindowCoords[3] < self.freqWindowCoords[1] + self.freqWindowMinY) {
                self.freqWindowCoords[3] = self.freqWindowCoords[1] + self.freqWindowMinY;
            }
            break;
            case 8:
            self.freqWindowCoords[0] = self.mx;
            if (self.freqWindowCoords[0] > self.freqWindowCoords[2] - self.freqWindowMinX) {
                self.freqWindowCoords[0] = self.freqWindowCoords[2] - self.freqWindowMinX;
            }
            break;
            default:
            break;
        }
    }
}

void renderWindow() {
    renderFreqWindow();
    renderOscWindow();
}

void renderOscData() {
    self.bottomBound = self.topBound * -1;
    /* render window background */
    turtleRentangle(self.oscWindowCoords[0], self.oscWindowCoords[1], self.oscWindowCoords[2], self.oscWindowCoords[3], self.themeColors[self.theme + 12], self.themeColors[self.theme + 13], self.themeColors[self.theme + 14], 0);
    /* render data */
    if (!self.stop) { 
        self.rightBound = self.data -> length;
    }
    /* TODO - check this logic to ensure correctness */
    if (self.rightBound - self.leftBound < self.windowSize) {
        self.leftBound = self.rightBound - self.windowSize;
        if (self.leftBound < 0) {
            self.leftBound = 0;
        }
    }
    if (self.rightBound > self.leftBound + self.windowSize) {
        self.leftBound = self.rightBound - self.windowSize;
    }
    turtlePenSize(1);
    turtlePenColor(self.themeColors[self.theme + 6], self.themeColors[self.theme + 7], self.themeColors[self.theme + 8]);
    double xquantum = (self.oscWindowCoords[2] - self.oscWindowCoords[0]) / (self.rightBound - self.leftBound - 1);
    for (int i = 0; i < self.rightBound - self.leftBound; i++) {
        turtleGoto(self.oscWindowCoords[0] + i * xquantum, self.oscWindowCoords[1] + ((self.data -> data[self.leftBound + i].d - self.bottomBound) / (self.topBound - self.bottomBound)) * (self.oscWindowCoords[3] - self.oscWindowTop - self.oscWindowCoords[1]));
        turtlePenDown();
    }
    turtlePenUp();
}

void renderFreqData() {
    self.bottomBound = self.topBound * -1;
    /* render window background */
    turtleRentangle(self.freqWindowCoords[0], self.freqWindowCoords[1], self.freqWindowCoords[2], self.freqWindowCoords[3], self.themeColors[self.theme + 12], self.themeColors[self.theme + 13], self.themeColors[self.theme + 14], 0);
    /* linear windowing function over 10% of the sample */
    int threshold = (self.rightBound - self.leftBound) * 0.1;
    double damping = 1.0 / threshold;
    list_clear(self.freqData);
    for (int i = 0; i < self.rightBound - self.leftBound; i++) {
        double dataPoint = self.data -> data[i + self.leftBound].d;
        if (i < threshold) {
            dataPoint *= damping * (i + 1);
        }
        if (i >= self.rightBound - threshold) {
            dataPoint *= damping * (self.rightBound - (i - 1));
        }
        list_append(self.freqData, (unitype) (i / 60.0), 'd');
        list_append(self.freqData, (unitype) dataPoint, 'd');
    }
    // list_print(self.freqData);
    // list_t *tempData = FFT(self.freqData);
    // list_print(tempData);
    // turtlePenSize(1);
    // turtlePenColor(self.themeColors[self.theme + 6], self.themeColors[self.theme + 7], self.themeColors[self.theme + 8]);
    // double xquantum = (self.freqWindowCoords[2] - self.freqWindowCoords[0]) / ((tempData -> length) / 2 - 1);
    // for (int i = 0; i < (tempData -> length) / 2; i++) {
    //     double magnitude = tempData -> data[i * 2].d * tempData -> data[i * 2].d + tempData -> data[i * 2 + 1].d * tempData -> data[i * 2 + 1].d;
    //     turtleGoto(self.freqWindowCoords[0] + i * xquantum, self.freqWindowCoords[1] + ((magnitude - self.bottomBound) / (self.topBound - self.bottomBound)) * (self.freqWindowCoords[3] - self.freqWindowTop - self.freqWindowCoords[1]));
    //     turtlePenDown();
    // }
    // turtlePenUp();
    // list_free(tempData);
}

void renderData() {
    renderFreqData();
    renderOscData();
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
    window = glfwCreateWindow(3456, 1944, "EMPV", NULL, NULL);
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
    /* initialiseTwin32tools */
    win32ToolsInit();
    win32FileDialogAddExtension("txt"); // add txt to extension restrictions
    win32FileDialogAddExtension("csv"); // add csv to extension restrictions

    int tps = 60; // ticks per second (locked to fps in this case)
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
        renderData();
        renderWindow();
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