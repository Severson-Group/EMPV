/* EMPV for windows
Features:
- Oscilloscope-like interface
- Frequency graph
- Editor

Trigger settings: https://www.picotech.com/library/knowledge-bases/oscilloscopes/advanced-digital-triggers
*/

#include "include/ribbon.h"
#include "include/popup.h"
#include "include/win32tcp.h"
#include "include/win32Tools.h"
#include "include/fft.h"
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

typedef struct { // dropdown 
    list_t *options;
    int index;
    int window;
    int status;
    double size;
    double position[2];
    double maxXfactor;
} dropdown_t;

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
    list_t *dropdowns;
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
        int oscDataIndex;
        int oscLeftBound; // left bound (index in data list)
        int oscRightBound; // right bound (index in data list)
        double oscBottomBound; // bottom bound (y value)
        double oscTopBound; // top bound (y value)
        double oscWindowSize; // size of window (index in data list)
        double oscSamplesPerSecond; // samples per second for logged variables
        int stop; // pause and unpause
    /* frequency view */
        list_t *windowData; // segment of normal data through windowing function
        list_t *freqData; // frequency data
        int freqLeftBound;
        int freqRightBound;
        double freqZoom;
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
    return dial;
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
    return switchp;
}

void dropdownCalculateMax(dropdown_t *dropdown) {
    dropdown -> maxXfactor = 0;
    for (int i = 0; i < dropdown -> options -> length; i++) {
        double stringLength = textGLGetStringLength(dropdown -> options -> data[i].s, dropdown -> size - 1);
        if (stringLength > dropdown -> maxXfactor) {
            dropdown -> maxXfactor = stringLength;
        }
    }
}

dropdown_t *dropdownInit(list_t *options, int window, double yScale, double yOffset, double size) {
    dropdown_t *dropdown = malloc(sizeof(dropdown_t));
    dropdown -> options = options;
    dropdown -> index = 0;
    dropdown -> status = 0;
    dropdown -> window = window;
    dropdown -> position[0] = yScale;
    dropdown -> position[1] = yOffset;
    dropdown -> size = size;
    dropdownCalculateMax(dropdown);
    return dropdown;
}

void init() { // initialises the empv variabes (shared state)
/* color */
    double themeCopy[48] = {
        /* light theme */
        255, 255, 255, // background color
        195, 195, 195, // window color
        255, 0, 0, // data color
        0, 0, 0, // text color
        230, 230, 230, // window background color
        0, 144, 20, // switch toggled on color
        255, 0, 0, // switch toggled off color
        160, 160, 160, // sidebar and bottom bar color
        /* dark theme */
        60, 60, 60, // background color
        10, 10, 10, // window color
        19, 236, 48, // data color
        200, 200, 200, // text color
        80, 80, 80, // window background color
        0, 255, 0, // switch toggled on color
        164, 28, 9, // switch toggled off color
        30, 30, 30 // sidebar and bottom bar color
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
    self.logVariables = list_init();
    list_append(self.logVariables, (unitype) "Demo", 's');
    list_append(self.logVariables, (unitype) "LOG_current_a", 's');
    list_append(self.logVariables, (unitype) "LOG_current_b", 's');
    list_append(self.logVariables, (unitype) "LOG_current_c", 's');
    self.data = list_init();
    for (int i = 0; i < self.logVariables -> length; i++) {
        list_append(self.data, (unitype) list_init(), 'r');
    }
    self.windowRender = list_init();
    list_append(self.windowRender, (unitype) WINDOW_FREQ, 'i');
    list_append(self.windowRender, (unitype) WINDOW_OSC, 'i');
    list_append(self.windowRender, (unitype) WINDOW_EDITOR, 'i');
    self.anchorX = 0;
    self.anchorY = 0;
    self.dialAnchorX = 0;
    self.dialAnchorY = 0;
    /* osc */
    self.oscDataIndex = 0;
    self.oscLeftBound = 0;
    self.oscRightBound = 0;
    self.oscBottomBound = -100;
    self.oscTopBound = 100;
    self.oscWindowSize = 200;
    self.oscSamplesPerSecond = 120;
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
    self.windows[0].dropdowns = list_init();
    list_append(self.windows[0].dials, (unitype) (void *) dialInit("X Scale", &self.oscWindowSize, WINDOW_OSC, DIAL_EXP, 1, -25 - self.windows[0].windowTop, 8, 4, 1024), 'p');
    list_append(self.windows[0].dials, (unitype) (void *) dialInit("Y Scale", &self.oscTopBound, WINDOW_OSC, DIAL_EXP, 1, -65 - self.windows[0].windowTop, 8, 50, 10000), 'p');
    list_append(self.windows[0].switches, (unitype) (void *) switchInit("Pause", &self.stop, WINDOW_OSC, 1, -100 - self.windows[0].windowTop, 8), 'p');
    list_append(self.windows[0].dropdowns, (unitype) (void *) dropdownInit(self.logVariables, WINDOW_OSC, 1, -7, 8), 'p');

    /* frequency */
    self.windowData = list_init();
    self.freqData = list_init();
    self.freqLeftBound = 0;
    self.freqRightBound = 0;
    self.freqZoom = 1.0;
    self.topFreq = 5000;
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
    self.windows[1].dropdowns = list_init();
    list_append(self.windows[1].dials, (unitype) (void *) dialInit("Y Scale", &self.topFreq, WINDOW_FREQ, DIAL_EXP, 1, -25 - self.windows[1].windowTop, 8, 500, 100000), 'p');
    /* editor */
    strcpy(self.windows[2].title, "Editor");
    self.windows[2].windowCoords[0] = -317;
    self.windows[2].windowCoords[1] = -121;
    self.windows[2].windowCoords[2] = 317;
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
    self.windows[2].dropdowns = list_init();    
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

void fft_list_wrapper(list_t *samples, list_t *output) {
    /* convert to complex */
    int dimension = samples -> length;
    complex_t complexSamples[dimension];
    for (int i = 0; i < dimension; i++) {
        complexSamples[i].Re = samples -> data[i].d;
        complexSamples[i].Im = 0.0f;
    }
    complex_t scratch[dimension];
    fft(complexSamples, dimension, scratch);
    /* parse */
    for (int i = 0; i < dimension; i++) {
        double fftSample = complexSamples[i].Re;
        list_append(output, (unitype) fftSample, 'd');
    }
}

void dialTick(int window) {
    int windowID = pow2(window);
    for (int i = 0; i < self.windows[window].dials -> length; i++) {
        dial_t *dialp = (dial_t *) (self.windows[window].dials -> data[i].p);
        if ((dialp -> window & windowID) != 0) {
            textGLWriteString(dialp -> label, (self.windows[window].windowCoords[2] - self.windows[window].windowSide + self.windows[window].windowCoords[2]) / 2, self.windows[window].windowCoords[1] + (self.windows[window].windowCoords[3] - self.windows[window].windowCoords[1]) * dialp -> position[0] + dialp -> position[1] + 15, dialp -> size - 1, 50);
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
            // if (self.windowRender -> data[self.windowRender -> length - 1].i == windowID) {
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
            // }
            if (dialp -> status[0] > 0) {
                dialAngle = angleBetween(self.dialAnchorX, self.dialAnchorY, self.mx, self.my);
                if (self.my < self.dialAnchorY) {
                    dialp -> status[1] = self.mx - dialX;
                }
                if ((dialAngle < 0.000001 || dialAngle > 180) && self.my > self.dialAnchorY && dialp -> status[1] >= 0) {
                    dialAngle = 0.000001;
                }
                if ((dialAngle > 359.99999 || dialAngle < 180) && self.my > self.dialAnchorY && dialp -> status[1] < 0) {
                    dialAngle = 359.99999;
                }
                if (dialp -> type == DIAL_LOG) {
                    *(dialp -> variable) = round(dialp -> range[0] + (dialp -> range[1] - dialp -> range[0]) * (log(dialAngle) / log(360)));
                } else if (dialp -> type == DIAL_LINEAR) {
                    *(dialp -> variable) = round(dialp -> range[0] + ((dialp -> range[1] - dialp -> range[0]) * dialAngle / 360));
                } else if (dialp -> type == DIAL_EXP) {
                    *(dialp -> variable) = round(dialp -> range[0] + (dialp -> range[1] - dialp -> range[0]) * ((pow(361, dialAngle / 360) - 1) / 360));
                }
            }
            char bubble[24];
            double rounded = round(*(dialp -> variable));
            sprintf(bubble, "%.0lf", rounded);
            textGLWriteString(bubble, dialX + dialp -> size + 3, dialY, 4, 0);
        }
    }
}

void switchTick(int window) {
    int windowID = pow2(window);
    for (int i = 0; i < self.windows[window].switches -> length; i++) {
        switch_t *switchp = (switch_t *) (self.windows[window].switches -> data[i].p);
        if ((switchp -> window & windowID) != 0) {
            textGLWriteString(switchp -> label, (self.windows[window].windowCoords[2] - self.windows[window].windowSide + self.windows[window].windowCoords[2]) / 2, self.windows[window].windowCoords[1] + (self.windows[window].windowCoords[3] - self.windows[window].windowCoords[1]) * switchp -> position[0] + switchp -> position[1] + 15, switchp -> size - 1, 50);
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
            // if (self.windowRender -> data[self.windowRender -> length - 1].i == windowID) {
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
            // }
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

void dropdownTick(int window) {
    int windowID = pow2(window);
    for (int i = 0; i < self.windows[window].dropdowns -> length; i++) {
        dropdown_t *dropdown = (dropdown_t *) (self.windows[window].dropdowns -> data[i].p);
        if ((dropdown -> window & windowID) != 0) {
            double dropdownX = (self.windows[window].windowCoords[2] - self.windows[window].windowSide + self.windows[window].windowCoords[2]) / 2 + 6;
            double dropdownY = self.windows[window].windowCoords[1] + (self.windows[window].windowCoords[3] - self.windows[window].windowCoords[1]) * dropdown -> position[0] + dropdown -> position[1];
            double xfactor = textGLGetStringLength(dropdown -> options -> data[dropdown -> index].s, dropdown -> size - 1);
            double itemHeight = (dropdown -> size * 1.5);
            if (dropdown -> status == -1) {
                turtleRectangle(dropdownX - dropdown -> size - xfactor, dropdownY - dropdown -> size * 0.7, dropdownX + dropdown -> size + 10, dropdownY + dropdown -> size * 0.7, self.themeColors[self.theme + 12], self.themeColors[self.theme + 13], self.themeColors[self.theme + 14], 0);
            } else if (dropdown -> status >= 1) {
                turtleRectangle(dropdownX - dropdown -> size - dropdown -> maxXfactor, dropdownY - dropdown -> size * 0.7 - (dropdown -> options -> length - 1) * itemHeight, dropdownX + dropdown -> size + 10, dropdownY + dropdown -> size * 0.7, self.themeColors[self.theme + 12], self.themeColors[self.theme + 13], self.themeColors[self.theme + 14], 0);
            } else {
                turtleRectangle(dropdownX - dropdown -> size - xfactor, dropdownY - dropdown -> size * 0.7, dropdownX + dropdown -> size + 10, dropdownY + dropdown -> size * 0.7, self.themeColors[self.theme + 21], self.themeColors[self.theme + 22], self.themeColors[self.theme + 23], 0);
            }
            if (self.windowRender -> data[self.windowRender -> length - 1].i == windowID) {
                if (self.mx > dropdownX - dropdown -> size - xfactor && self.mx < dropdownX + dropdown -> size + 10 && self.my > dropdownY - dropdown -> size * 0.7 && self.my < dropdownY + dropdown -> size * 0.7) {
                    if (!self.mouseDown && dropdown -> status == 0) {
                        dropdown -> status = -1;
                    }
                } else {
                    if (dropdown -> status == -1) {
                        dropdown -> status = 0;
                    }
                }
                if (dropdown -> status == -1) {
                    if (self.mouseDown) {
                        dropdown -> status = 1;
                    }
                }
                if (dropdown -> status == 1) {
                    if (!self.mouseDown) {
                        dropdown -> status = 2;
                    }
                }
                if (dropdown -> status == -2) {
                    if (!self.mouseDown) {
                        dropdown -> status = 0;
                    }
                }
            }
            if (dropdown -> status == 2 || dropdown -> status == 1) {
                if (self.mx > dropdownX - dropdown -> size - dropdown -> maxXfactor && self.mx < dropdownX + dropdown -> size + 10 && self.my > dropdownY - dropdown -> size * 0.7 - (dropdown -> options -> length - 1) * itemHeight && self.my < dropdownY + dropdown -> size * 0.7) {
                    int selected = round((dropdownY - self.my) / itemHeight);
                    turtleRectangle(dropdownX - dropdown -> size - dropdown -> maxXfactor, dropdownY - dropdown -> size * 0.7 - selected * itemHeight, dropdownX + dropdown -> size + 10, dropdownY + dropdown -> size * 0.7 - selected * itemHeight, self.themeColors[self.theme + 21], self.themeColors[self.theme + 22], self.themeColors[self.theme + 23], 0);
                    if (self.mouseDown && dropdown -> status == 2) {
                        if (selected != 0) {
                            if (dropdown -> index >= selected) {
                                dropdown -> index = selected - 1;
                            } else {
                                dropdown -> index = selected;
                            }
                            self.oscDataIndex = dropdown -> index;
                            self.oscRightBound = self.data -> data[self.oscDataIndex].r -> length - 1;
                            if (self.oscRightBound < 0) {
                                self.oscRightBound = 0;
                            }
                            self.oscLeftBound = self.data -> data[self.oscDataIndex].r -> length - self.oscWindowSize - 1;
                            if (self.oscLeftBound < 0) {
                                self.oscLeftBound = 0;
                            }
                        }
                        dropdown -> status = -2;
                    }
                } else {
                    if (self.mouseDown) {
                        dropdown -> status = 0;
                    }
                }
                turtlePenColor(self.themeColors[self.theme + 9], self.themeColors[self.theme + 10], self.themeColors[self.theme + 11]);
                int renderIndex = 1;
                for (int i = 0; i < dropdown -> options -> length; i++) {
                    if (i != dropdown -> index) {
                        textGLWriteString(dropdown -> options -> data[i].s, (self.windows[window].windowCoords[2] - self.windows[window].windowSide + self.windows[window].windowCoords[2]) / 2, self.windows[window].windowCoords[1] + (self.windows[window].windowCoords[3] - self.windows[window].windowCoords[1]) * dropdown -> position[0] + dropdown -> position[1] - renderIndex * itemHeight, dropdown -> size - 1, 100);
                        renderIndex++;
                    }
                }
            }
            turtlePenColor(self.themeColors[self.theme + 9], self.themeColors[self.theme + 10], self.themeColors[self.theme + 11]);
            textGLWriteString(dropdown -> options -> data[dropdown -> index].s, (self.windows[window].windowCoords[2] - self.windows[window].windowSide + self.windows[window].windowCoords[2]) / 2, self.windows[window].windowCoords[1] + (self.windows[window].windowCoords[3] - self.windows[window].windowCoords[1]) * dropdown -> position[0] + dropdown -> position[1], dropdown -> size - 1, 100);
            if (dropdown -> status >= 1) {
                turtleTriangle(dropdownX + 11, dropdownY + 4, dropdownX + 11, dropdownY - 4, dropdownX + 5, dropdownY, self.themeColors[self.theme + 9], self.themeColors[self.theme + 10], self.themeColors[self.theme + 11], 0);
            } else {
                turtleTriangle(dropdownX + 13, dropdownY + 3, dropdownX + 5, dropdownY + 3, dropdownX + 9, dropdownY - 3, self.themeColors[self.theme + 9], self.themeColors[self.theme + 10], self.themeColors[self.theme + 11], 0);
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
        dropdownTick(window);
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
            int clickSum = 0;
            for (int i = self.windowRender -> length - 1; i >= 0; i--) {
                if (self.windowRender -> data[i].i == pow2(window)) {
                    break;
                }
                clickSum += self.windows[ilog2(self.windowRender -> data[i].i)].click;
            }
            if (self.mx > win -> windowCoords[0] && self.mx < win -> windowCoords[2] && self.my > win -> windowCoords[3] - win -> windowTop && self.my < win -> windowCoords[3] && moveSum == 0 && clickSum == 0) {
                win -> move = -1;
            } else {
                win -> move = 0;
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
        self.oscRightBound = self.data -> data[self.oscDataIndex].r -> length;
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
        turtleGoto(self.windows[0].windowCoords[2], (self.windows[0].windowCoords[1] - self.windows[0].windowTop + self.windows[0].windowCoords[3]) / 2);
        turtlePenUp();
        /* render data */
        turtlePenSize(1);
        turtlePenColor(self.themeColors[self.theme + 6], self.themeColors[self.theme + 7], self.themeColors[self.theme + 8]);
        double xquantum = (self.windows[0].windowCoords[2] - self.windows[0].windowCoords[0]) / (self.oscRightBound - self.oscLeftBound - 1);
        for (int i = 0; i < self.oscRightBound - self.oscLeftBound; i++) {
            turtleGoto(self.windows[0].windowCoords[0] + i * xquantum, self.windows[0].windowCoords[1] + ((self.data -> data[self.oscDataIndex].r -> data[self.oscLeftBound + i].d - self.oscBottomBound) / (self.oscTopBound - self.oscBottomBound)) * (self.windows[0].windowCoords[3] - self.windows[0].windowTop - self.windows[0].windowCoords[1]));
            turtlePenDown();
        }
        turtlePenUp();
        /* render mouse */
        // if (self.windowRender -> data[self.windowRender -> length - 1].i == WINDOW_OSC) {
            if (self.mx > self.windows[0].windowCoords[0] + 15 && self.my > self.windows[0].windowCoords[1] && self.mx < self.windows[0].windowCoords[2] && self.windows[0].windowCoords[3] - self.windows[0].windowTop) {
                int sample = round((self.mx - self.windows[0].windowCoords[0]) / xquantum);
                if (self.oscLeftBound + sample >= self.data -> data[self.oscDataIndex].r -> length) {
                    return;
                }
                double sampleX = self.windows[0].windowCoords[0] + sample * xquantum;
                double sampleY = self.windows[0].windowCoords[1] + ((self.data -> data[self.oscDataIndex].r -> data[self.oscLeftBound + sample].d - self.oscBottomBound) / (self.oscTopBound - self.oscBottomBound)) * (self.windows[0].windowCoords[3] - self.windows[0].windowTop - self.windows[0].windowCoords[1]);
                turtleRectangle(sampleX - 1, self.windows[0].windowCoords[3] - self.windows[0].windowTop, sampleX + 1, self.windows[0].windowCoords[1], self.themeColors[self.theme + 21], self.themeColors[self.theme + 22], self.themeColors[self.theme + 23], 100);
                turtleRectangle(self.windows[0].windowCoords[0], sampleY - 1, self.windows[0].windowCoords[2], sampleY + 1, self.themeColors[self.theme + 21], self.themeColors[self.theme + 22], self.themeColors[self.theme + 23], 100);
                turtlePenColor(215, 215, 215);
                turtlePenSize(4);
                turtleGoto(sampleX, sampleY);
                turtlePenDown();
                turtlePenUp();
                char sampleValue[24];
                /* render side box */
                sprintf(sampleValue, "%.02lf", self.data -> data[self.oscDataIndex].r -> data[self.oscLeftBound + sample].d);
                double boxLength = textGLGetStringLength(sampleValue, 8);
                double boxX = self.windows[0].windowCoords[0] + 12;
                if (sampleX - boxX < 40) {
                    boxX = self.windows[0].windowCoords[2] - self.windows[0].windowSide - boxLength - 5;
                }
                double boxY = sampleY + 10;
                turtleRectangle(boxX, boxY - 5, boxX + 4 + boxLength, boxY + 5, 215, 215, 215, 0);
                turtlePenColor(0, 0, 0);
                textGLWriteString(sampleValue, boxX + 2, boxY - 1, 8, 0);
                /* render top box */
                sprintf(sampleValue, "%d", sample);
                double boxLength2 = textGLGetStringLength(sampleValue, 8);
                double boxY2 = sampleY + 10;
                double boxX2 = sampleX - boxLength2 / 2;
                if (boxX2 - 15 < self.windows[0].windowCoords[0]) {
                    boxX2 = self.windows[0].windowCoords[0] + 15;
                }
                if (boxX2 + boxLength2 + self.windows[0].windowSide + 5 > self.windows[0].windowCoords[2]) {
                    boxX2 = self.windows[0].windowCoords[2] - boxLength2 - self.windows[0].windowSide - 5;
                }
                turtleRectangle(boxX2 - 2, self.windows[0].windowCoords[3] - self.windows[0].windowTop - 15, boxX2 + boxLength2 + 2, self.windows[0].windowCoords[3] - self.windows[0].windowTop - 5, 215, 215, 215, 0);
                turtlePenColor(0, 0, 0);
                textGLWriteString(sampleValue, boxX2, self.windows[0].windowCoords[3] - 26, 8, 0);
            }
        // }
        /* render side axis */
        turtleRectangle(self.windows[0].windowCoords[0], self.windows[0].windowCoords[1], self.windows[0].windowCoords[0] + 10, self.windows[0].windowCoords[3], self.themeColors[self.theme + 21], self.themeColors[self.theme + 22], self.themeColors[self.theme + 23], 100);
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
    int dataLength = self.oscRightBound - self.oscLeftBound;
    int threshold = (dataLength) * 0.1;
    double damping = 1.0 / threshold;
    list_clear(self.windowData);
    for (int i = 0; i < dataLength; i++) {
        double dataPoint = self.data -> data[self.oscDataIndex].r -> data[i + self.oscLeftBound].d;
        if (i < threshold) {
            dataPoint *= damping * (i + 1);
        }
        if (i >= (dataLength) - threshold) {
            dataPoint *= damping * ((dataLength) - (i - 1));
        }
        list_append(self.windowData, (unitype) dataPoint, 'd');
    }
    list_clear(self.freqData);
    fft_list_wrapper(self.windowData, self.freqData);
    double xquantum = (self.windows[1].windowCoords[2] - self.windows[1].windowCoords[0] - self.windows[1].windowSide) / ((self.windowData -> length - 2) / self.freqZoom) * 2;
    if (self.windows[1].minimize == 0) {
        /* render window background */
        turtleRectangle(self.windows[1].windowCoords[0], self.windows[1].windowCoords[1], self.windows[1].windowCoords[2], self.windows[1].windowCoords[3], self.themeColors[self.theme + 12], self.themeColors[self.theme + 13], self.themeColors[self.theme + 14], 0);
        turtlePenSize(1);
        turtlePenColor(self.themeColors[self.theme + 6], self.themeColors[self.theme + 7], self.themeColors[self.theme + 8]);
        if (self.freqData -> length % 2) {
            xquantum *= (self.freqData -> length - 2.0) / (self.freqData -> length - 1.0);
        }
        self.freqRightBound = 1 + self.freqLeftBound + (self.windows[1].windowCoords[2] - self.windows[1].windowSide - self.windows[1].windowCoords[0]) / xquantum;
        if (self.freqRightBound > self.freqData -> length / 2 + self.freqData -> length % 2) {
            self.freqRightBound = self.freqData -> length / 2 + self.freqData -> length % 2;
        }
        for (int i = self.freqLeftBound; i < self.freqRightBound; i++) { // only render the bottom half of frequency graph
            double magnitude = self.freqData -> data[i].d;
            if (magnitude < 0) {
                magnitude *= -1;
            }
            turtleGoto(self.windows[1].windowCoords[0] + (i - self.freqLeftBound) * xquantum, self.windows[1].windowCoords[1] + 9 + ((magnitude - 0) / (self.topFreq - 0)) * (self.windows[1].windowCoords[3] - self.windows[1].windowTop - self.windows[1].windowCoords[1]));
            turtlePenDown();
        }
        turtlePenUp();
        /* render mouse */
        // if (self.windowRender -> data[self.windowRender -> length - 1].i == WINDOW_FREQ) {
            if (self.mx > self.windows[1].windowCoords[0] && self.my > self.windows[1].windowCoords[1] && self.mx < self.windows[1].windowCoords[2] - self.windows[1].windowSide && self.windows[1].windowCoords[3] - self.windows[1].windowTop) {
                double sample = (self.mx - self.windows[1].windowCoords[0]) / xquantum + self.freqLeftBound;
                if (self.oscLeftBound + sample >= self.data -> data[self.oscDataIndex].r -> length) {
                    return;
                }
                int roundedSample = round(sample);
                double sampleX = self.windows[1].windowCoords[0] + (roundedSample - self.freqLeftBound) * xquantum;
                double sampleY = 9 + self.windows[1].windowCoords[1] + (fabs(self.freqData -> data[roundedSample].d) / (self.topFreq)) * (self.windows[1].windowCoords[3] - self.windows[1].windowTop - self.windows[1].windowCoords[1]);
                turtleRectangle(sampleX - 1, self.windows[1].windowCoords[3] - self.windows[1].windowTop, sampleX + 1, self.windows[1].windowCoords[1], self.themeColors[self.theme + 21], self.themeColors[self.theme + 22], self.themeColors[self.theme + 23], 100);
                turtleRectangle(self.windows[1].windowCoords[0], sampleY - 1, self.windows[1].windowCoords[2] - self.windows[1].windowSide, sampleY + 1, self.themeColors[self.theme + 21], self.themeColors[self.theme + 22], self.themeColors[self.theme + 23], 100);
                turtlePenColor(215, 215, 215);
                turtlePenSize(4);
                turtleGoto(sampleX, sampleY);
                turtlePenDown();
                turtlePenUp();
                char sampleValue[24];
                /* render side box */
                sprintf(sampleValue, "%.02lf", fabs(self.freqData -> data[roundedSample].d));
                double boxLength = textGLGetStringLength(sampleValue, 8);
                double boxX = self.windows[1].windowCoords[0] + 2;
                if (sampleX - boxX < 40) {
                    boxX = self.windows[1].windowCoords[2] - self.windows[1].windowSide - boxLength - 5;
                }
                double boxY = sampleY + 10;
                turtleRectangle(boxX, boxY - 5, boxX + 4 + boxLength, boxY + 5, 215, 215, 215, 0);
                turtlePenColor(0, 0, 0);
                textGLWriteString(sampleValue, boxX + 2, boxY - 1, 8, 0);
                /* render top box */
                sprintf(sampleValue, "%.1lfHz", sample / (dataLength / self.oscSamplesPerSecond));
                double boxLength2 = textGLGetStringLength(sampleValue, 8);
                double boxX2 = sampleX - boxLength2 / 2;
                if (boxX2 - 5 < self.windows[1].windowCoords[0]) {
                    boxX2 = self.windows[1].windowCoords[0] + 5;
                }
                if (boxX2 + boxLength2 + self.windows[1].windowSide + 5 > self.windows[1].windowCoords[2]) {
                    boxX2 = self.windows[1].windowCoords[2] - boxLength2 - self.windows[1].windowSide - 5;
                }
                turtleRectangle(boxX2 - 2, self.windows[1].windowCoords[3] - self.windows[1].windowTop - 15, boxX2 + boxLength2 + 2, self.windows[1].windowCoords[3] - self.windows[1].windowTop - 5, 215, 215, 215, 0);
                turtlePenColor(0, 0, 0);
                textGLWriteString(sampleValue, boxX2, self.windows[1].windowCoords[3] - 26, 8, 0);
                /* scrolling */
                const double scaleFactor = 1.25;
                double buckets = (self.mx - self.windows[1].windowCoords[0]) / xquantum;
                if (self.mw > 0) {
                    /* zoom in */
                    self.freqZoom *= scaleFactor;
                    if (self.freqZoom > 100.0) {
                        self.freqZoom = 100.0;
                    }
                    self.freqLeftBound += round(buckets - (buckets / scaleFactor));
                    if (self.freqLeftBound >= self.freqRightBound) {
                        self.freqLeftBound = self.freqRightBound - 1;
                    }
                } else if (self.mw < 0) {
                    /* zoom out */
                    self.freqZoom /= scaleFactor;
                    if (self.freqZoom < 1.0) {
                        self.freqZoom = 1.0;
                    }
                    self.freqLeftBound -= round(buckets - (buckets / scaleFactor));
                    if (self.freqLeftBound < 0) {
                        self.freqLeftBound = 0;
                    }
                }
            }
        // }
    }
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
        turtlePenColor(self.themeColors[self.theme + 9], self.themeColors[self.theme + 10], self.themeColors[self.theme + 11]);
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

void searchMapfile(char *filepath) {

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
    // if (turtleKeyPressed(GLFW_KEY_UP)) {
    //     self.mw += 1;
    // }
    // if (turtleKeyPressed(GLFW_KEY_DOWN)) {
    //     self.mw -= 1;
    // }
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
    if (argc > 1) {
        if (win32tcpInit("192.168.1.10", "7")) {
            printf("Failed to connect to %s, ensure AMDC is plugged in and listening on port %s\n", win32Socket.address, win32Socket.port);
        }
        SOCKET *sockets[2] = {NULL, NULL};
        sockets[0] = win32tcpCreateSocket();
        if (sockets[0] != NULL) {
            unsigned char receiveBuffer[10];
            win32tcpReceive(sockets[0], receiveBuffer, 10);
            unsigned char amdc_cmd_id[2] = {12, 34};
            win32tcpSend(sockets[0], amdc_cmd_id, 2);
            printf("Successfully created AMDC cmd socket\n");
        }
        sockets[1] = win32tcpCreateSocket();
        if (sockets[0] != NULL) {
            unsigned char receiveBuffer[10];
            win32tcpReceive(sockets[0], receiveBuffer, 10);
            unsigned char amdc_log_id[2] = {56, 78};
            win32tcpSend(sockets[0], amdc_log_id, 2);
            printf("Successfully created AMDC log socket\n");
        }
    }

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
        double sinValue3 = sin(tick * 1.1) * 12.5;
        // list_append(self.data -> data[0].r, (unitype) (sinValue1 + sinValue2 + sinValue3), 'd');
        list_append(self.data -> data[0].r, (unitype) (sinValue1), 'd');
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