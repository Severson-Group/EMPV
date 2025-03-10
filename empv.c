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
#include "include/kissFFT.h"
#include <time.h>
#include <pthread.h>

#define TCP_RECEIVE_BUFFER_LENGTH 2048
#define MAX_LOGGING_SOCKETS       32

#define DIAL_LINEAR     0
#define DIAL_LOG        1
#define DIAL_EXP        2

#define NUM_WINDOWS     14

#define WINDOW_FREQ     1
#define WINDOW_EDITOR   2
#define WINDOW_OSC      4

enum trigger_type {
    TRIGGER_NONE = 0,
    TRIGGER_RISING_EDGE
};

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

typedef struct {
    int triggerType;
    int triggerIndex;
    list_t *lastTriggerIndex;
} trigger_settings_t;

typedef struct { // oscilloscope view
    trigger_settings_t trigger;
    int dataIndex; // index of data list for oscilloscope source
    int leftBound; // left bound (index in data list)
    int rightBound; // right bound (index in data list)
    double bottomBound; // bottom bound (y value)
    double topBound; // top bound (y value)
    double windowSize; // size of window (index in data list)
    double samplesPerSecond; // samples per second for logged variables
    int stop; // pause and unpause
} oscilloscope_t;

typedef struct { // all the empv shared state is here
    /* comms */
    pthread_t commsThread[MAX_LOGGING_SOCKETS];
    char commsEnabled;
    SOCKET *cmdSocket;
    int cmdSocketID;
    uint8_t tcpAsciiReceiveBuffer[TCP_RECEIVE_BUFFER_LENGTH];
    int maxSlots; // maximum logging slots on AMDC
    /* general */
        list_t *data; // a list of all data collected through ethernet
        list_t *logVariables; // a list of variable names logged on the AMDC
        list_t *logSlots; // list of slot numbers synced with logVariables (Slot 0: X)
        list_t *logSockets; // list of socket pointers
        list_t *logSocketIDs; // list of socket IDs on the AMDC
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
        oscilloscope_t osc[12]; // up to 12 oscilloscopes
    /* frequency view */
        list_t *windowData; // segment of normal data through windowing function
        list_t *freqData; // frequency data
        int freqOscIndex; // referenced oscilloscope
        int freqLeftBound;
        int freqRightBound;
        double freqZoom;
        double topFreq; // top bound (y value)
    /* editor view */
        double editorBottomBound;
        double editorWindowSize; // size of window

} empv_t;

empv_t self; // global state

/* initialise UI elements */
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

/* math functions*/
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
    int dimension = samples -> length;
    if (dimension <= 0) {
        return;
    }
    kiss_fft_cfg cfg = kiss_fft_alloc(dimension, 0, NULL, NULL);
    /* convert to complex */
    kiss_fft_cpx complexSamples[dimension];
    for (int i = 0; i < dimension; i++) {
        complexSamples[i].r = samples -> data[i].d;
        complexSamples[i].i = 0.0f;
    }
    kiss_fft(cfg, complexSamples, complexSamples);
    kiss_fft_free(cfg);
    /* parse */
    for (int i = 0; i < dimension; i++) {
        double fftSample = sqrt(complexSamples[i].r * complexSamples[i].r + complexSamples[i].i * complexSamples[i].i) / self.osc[0].windowSize; // divide by closest rounded down power of 2 instead of window size
        double fftPhase = atan(complexSamples[i].i / complexSamples[i].r);
        list_append(output, (unitype) fftSample, 'd');
    }
}

char *convertToHex(unsigned char *input, int len) {
    char *output = calloc(len * 3 + 5, 1);
    for (int i = 0; i < len; i++) {
        sprintf(output + strlen(output), "%X ", input[i]);
    }
    return output;
}

void commsCommand(char *cmd) {
    if (self.commsEnabled == 0) {
        return;
    }
    char amdc_cmd[strlen(cmd) + 4];
    for (int i = 0; i < strlen(cmd) + 4; i++) {
        amdc_cmd[i] = 0;
    }
    memcpy(amdc_cmd, cmd, strlen(cmd) + 1);
    if (amdc_cmd[strlen(amdc_cmd) - 1] != '\n' || amdc_cmd[strlen(amdc_cmd) - 2] != 'r') {
        amdc_cmd[strlen(amdc_cmd)] = '\r';
        amdc_cmd[strlen(amdc_cmd)] = '\n';
    }
    for (int i = 0; i < TCP_RECEIVE_BUFFER_LENGTH; i++) {
        self.tcpAsciiReceiveBuffer[i] = 0;
    }
    /* get log info */
    win32tcpSend(self.cmdSocket, amdc_cmd, strlen(amdc_cmd));
    win32tcpReceive2(self.cmdSocket, self.tcpAsciiReceiveBuffer, strlen(amdc_cmd) + 1);
    win32tcpReceive2(self.cmdSocket, self.tcpAsciiReceiveBuffer, TCP_RECEIVE_BUFFER_LENGTH);
    // printf("received %s\n", self.tcpAsciiReceiveBuffer);
}

void commsGetData(int logSlotIndex) {
    /*
    Per the AMDC C-code,

    Packet format: HEADER, VAR_SLOT, TS, DATA, FOOTER
    where each entry is 32 bits

    Total packet length: 5*4 = 20 bytes

    HEADER = 0x11111111
    FOOTER = 0x22222222
    */
    uint32_t header = 0x11111111;
    uint32_t footer = 0x22222222;
    int packetLength = 20;
    uint8_t tcpLoggingReceiveBuffer[TCP_RECEIVE_BUFFER_LENGTH] = {0};
    int dataIndex = self.logSlots -> data[logSlotIndex].i + 1;
    win32tcpReceive2(self.logSockets -> data[logSlotIndex].p, tcpLoggingReceiveBuffer, TCP_RECEIVE_BUFFER_LENGTH);
    int index = 0;
    while (tcpLoggingReceiveBuffer[index] == 0x11 && tcpLoggingReceiveBuffer[index + 1] == 0x11 && tcpLoggingReceiveBuffer[index + 2] == 0x11 && tcpLoggingReceiveBuffer[index + 3] == 0x11) {
        index += 4;
        uint32_t varSlot = ((uint32_t) tcpLoggingReceiveBuffer[index + 3]) << 24 | ((uint32_t) tcpLoggingReceiveBuffer[index + 2]) << 16 | ((uint32_t) tcpLoggingReceiveBuffer[index + 1]) << 8 | ((uint32_t) tcpLoggingReceiveBuffer[index + 0]);
        index += 4;
        uint32_t timestamp = ((uint32_t) tcpLoggingReceiveBuffer[index + 3]) << 24 | ((uint32_t) tcpLoggingReceiveBuffer[index + 2]) << 16 | ((uint32_t) tcpLoggingReceiveBuffer[index + 1]) << 8 | ((uint32_t) tcpLoggingReceiveBuffer[index + 0]);
        index += 4;
        uint32_t data = ((uint32_t) tcpLoggingReceiveBuffer[index + 3]) << 24 | ((uint32_t) tcpLoggingReceiveBuffer[index + 2]) << 16 | ((uint32_t) tcpLoggingReceiveBuffer[index + 1]) << 8 | ((uint32_t) tcpLoggingReceiveBuffer[index + 0]);
        index += 4;
        if (tcpLoggingReceiveBuffer[index] == 0x22 && tcpLoggingReceiveBuffer[index + 1] == 0x22 && tcpLoggingReceiveBuffer[index + 2] == 0x22 && tcpLoggingReceiveBuffer[index + 3] == 0x22) {
            // printf("packet %d:\nvar_slot: %u\ntimestamp: %u\ndata: %X\n", index - 16, varSlot, timestamp, data);
            /* add value to data */
            float dataValue = *(float *) &data;
            list_append(self.data -> data[dataIndex].r, (unitype) (double) dataValue, 'd');
            index += 4;
        } else {
            printf("bad packet at index %d\n", index);
            index += 4;
        }
    }
    // char *converted = convertToHex(tcpLoggingReceiveBuffer, TCP_RECEIVE_BUFFER_LENGTH);
    // printf("received %s\n", converted);
    // free(converted);
}

void *commsThreadFunction(void *arg) {
    int index = *(int *) arg;
    /* populate real data */
    while (1) {
        if (self.commsEnabled == 1) {
            if (self.logSlots -> data[index].i != -1) {
                commsGetData(index);
            }
        }
    }
    return NULL;
} 

void populateLoggedVariables() {
    if (self.commsEnabled == 0) {
        return;
    }
    commsCommand("log info");
    self.maxSlots = 0;
    /* parse command */
    char *testString = strtok(self.tcpAsciiReceiveBuffer, "\n");
    int stringHold = 0;
    int slotNum = 0;
    while (testString != NULL) {
        testString = strtok(NULL, "\n");
        if (testString == NULL) {
            break;
        }
        if (stringHold) {
            switch (stringHold) {
            case 5: // Name: <Name>
                for (int i = 8; i < strlen(testString); i++) {
                    if (isspace(testString[i])) {
                        testString[i] = '\0';
                        break;
                    }
                }
                list_append(self.logSlots, (unitype) slotNum, 'i');
                list_append(self.logVariables, (unitype) (testString + 8), 's');
                break;
            case 4: // Type: <type>
                break;
            case 3: // Memory address: <address>
                break;
            case 2: // Sampling interval (usec): <usec>
                break;
            case 1: // Num samples: <num>
                break;
            default:
                break;    
            }
            stringHold--;
        } else {
            int len = strlen(testString);
            char savedChar = testString[len - 3];
            testString[len - 3] = '\0';
            if (strcmp(testString, "Slot") == 0 || strcmp(testString, "Slot ") == 0) {
                testString[len - 3] = savedChar;
                sscanf(testString + 5, "%d", &slotNum);
                stringHold = 5;
            }
            if (strcmp(testString, "Max Slots") || strcmp(testString, "Max Slots:")) {
                testString[len - 3] = savedChar;
                sscanf(testString + 10, "%d", &self.maxSlots);
            }
        }
    }
    printf("Max Logging Slots: %d\n", self.maxSlots);
}

/* initialise global state */
void init() { // initialises the empv variabes (shared state)
/* comms */
    self.maxSlots = 0;
    for (int i = 0; i < TCP_RECEIVE_BUFFER_LENGTH; i++) {
        self.tcpAsciiReceiveBuffer[i] = 0;
    }
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
    self.logSlots = list_init();
    self.logSockets = list_init();
    self.logSocketIDs = list_init();
    list_append(self.logVariables, (unitype) "Demo", 's');
    list_append(self.logSlots, (unitype) -1, 'i');
    list_append(self.logSockets, (unitype) NULL, 'p');
    list_append(self.logSocketIDs, (unitype) -1, 'i');
    populateLoggedVariables(); // gather logged variables
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
    self.osc[0].trigger.triggerType = TRIGGER_NONE;
    self.osc[0].trigger.triggerIndex = 0;
    self.osc[0].trigger.lastTriggerIndex = list_init();
    self.osc[0].dataIndex = 0;
    self.osc[0].leftBound = 0;
    self.osc[0].rightBound = 0;
    self.osc[0].bottomBound = -100;
    self.osc[0].topBound = 100;
    self.osc[0].windowSize = 200;
    self.osc[0].samplesPerSecond = 120;
    self.osc[0].stop = 0;
    int oscIndex = ilog2(WINDOW_OSC);
    strcpy(self.windows[oscIndex].title, "Oscilloscope");
    self.windows[oscIndex].windowCoords[0] = -317;
    self.windows[oscIndex].windowCoords[1] = 0;
    self.windows[oscIndex].windowCoords[2] = -2;
    self.windows[oscIndex].windowCoords[3] = 167;
    self.windows[oscIndex].windowTop = 15;
    self.windows[oscIndex].windowSide = 50;
    self.windows[oscIndex].windowMinX = 60 + self.windows[oscIndex].windowSide;
    self.windows[oscIndex].windowMinY = 150 + self.windows[oscIndex].windowTop;
    self.windows[oscIndex].minimize = 0;
    self.windows[oscIndex].move = 0;
    self.windows[oscIndex].click = 0;
    self.windows[oscIndex].resize = 0;
    self.windows[oscIndex].dials = list_init();
    self.windows[oscIndex].switches = list_init();
    self.windows[oscIndex].dropdowns = list_init();
    list_append(self.windows[oscIndex].dials, (unitype) (void *) dialInit("X Scale", &self.osc[0].windowSize, WINDOW_OSC, DIAL_EXP, 1, -25 - self.windows[oscIndex].windowTop, 8, 4, 1024), 'p');
    list_append(self.windows[oscIndex].dials, (unitype) (void *) dialInit("Y Scale", &self.osc[0].topBound, WINDOW_OSC, DIAL_EXP, 1, -65 - self.windows[oscIndex].windowTop, 8, 1, 10000), 'p');
    list_append(self.windows[oscIndex].switches, (unitype) (void *) switchInit("Pause", &self.osc[0].stop, WINDOW_OSC, 1, -100 - self.windows[oscIndex].windowTop, 8), 'p');
    list_append(self.windows[oscIndex].switches, (unitype) (void *) switchInit("Trigger", &self.osc[0].trigger.triggerType, WINDOW_OSC, 1, -130 - self.windows[oscIndex].windowTop, 8), 'p');
    list_append(self.windows[oscIndex].dropdowns, (unitype) (void *) dropdownInit(self.logVariables, WINDOW_OSC, 1, -7, 8), 'p');

    /* frequency */
    self.windowData = list_init();
    self.freqData = list_init();
    self.freqOscIndex = 0;
    self.freqLeftBound = 0;
    self.freqRightBound = 0;
    self.freqZoom = 1.0;
    self.topFreq = 20;
    int freqIndex = ilog2(WINDOW_FREQ);
    strcpy(self.windows[freqIndex].title, "Frequency");
    self.windows[freqIndex].windowCoords[0] = 2;
    self.windows[freqIndex].windowCoords[1] = 0;
    self.windows[freqIndex].windowCoords[2] = 317;
    self.windows[freqIndex].windowCoords[3] = 167;
    self.windows[freqIndex].windowTop = 15;
    self.windows[freqIndex].windowSide = 50;
    self.windows[freqIndex].windowMinX = 52 + self.windows[freqIndex].windowSide;
    self.windows[freqIndex].windowMinY = 120 + self.windows[freqIndex].windowTop;
    self.windows[freqIndex].minimize = 0;
    self.windows[freqIndex].move = 0;
    self.windows[freqIndex].click = 0;
    self.windows[freqIndex].resize = 0;
    self.windows[freqIndex].dials = list_init();
    self.windows[freqIndex].switches = list_init();
    self.windows[freqIndex].dropdowns = list_init();
    list_append(self.windows[freqIndex].dials, (unitype) (void *) dialInit("Y Scale", &self.topFreq, WINDOW_FREQ, DIAL_EXP, 1, -25 - self.windows[freqIndex].windowTop, 8, 1, 500), 'p');
    /* editor */
    int editorIndex = ilog2(WINDOW_EDITOR);
    strcpy(self.windows[editorIndex].title, "Editor");
    self.windows[editorIndex].windowCoords[0] = -317;
    self.windows[editorIndex].windowCoords[1] = -161;
    self.windows[editorIndex].windowCoords[2] = 317;
    self.windows[editorIndex].windowCoords[3] = -5;
    self.windows[editorIndex].windowTop = 15;
    self.windows[editorIndex].windowSide = 0;
    self.windows[editorIndex].windowMinX = 60 + self.windows[editorIndex].windowSide;
    self.windows[editorIndex].windowMinY = 120 + self.windows[editorIndex].windowTop;
    self.windows[editorIndex].minimize = 0;
    self.windows[editorIndex].move = 0;
    self.windows[editorIndex].click = 0;
    self.windows[editorIndex].resize = 0;
    self.windows[editorIndex].dials = list_init();
    self.windows[editorIndex].switches = list_init();   
    self.windows[editorIndex].dropdowns = list_init();    
}

/* UI elements */
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
                            self.osc[0].dataIndex = dropdown -> index;
                            self.osc[0].rightBound = self.data -> data[self.osc[0].dataIndex].r -> length - 1;
                            if (self.osc[0].rightBound < 0) {
                                self.osc[0].rightBound = 0;
                            }
                            self.osc[0].leftBound = self.data -> data[self.osc[0].dataIndex].r -> length - self.osc[0].windowSize - 1;
                            if (self.osc[0].leftBound < 0) {
                                self.osc[0].leftBound = 0;
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

void setBoundsNoTrigger(int oscIndex) {
    self.osc[oscIndex].rightBound = self.data -> data[self.osc[oscIndex].dataIndex].r -> length;
    if (self.osc[oscIndex].rightBound - self.osc[oscIndex].leftBound < self.osc[oscIndex].windowSize) {
        self.osc[oscIndex].leftBound = self.osc[oscIndex].rightBound - self.osc[oscIndex].windowSize;
        if (self.osc[oscIndex].leftBound < 0) {
            self.osc[oscIndex].leftBound = 0;
        }
    }
    if (self.osc[oscIndex].rightBound > self.osc[oscIndex].leftBound + self.osc[oscIndex].windowSize) {
        self.osc[oscIndex].leftBound = self.osc[oscIndex].rightBound - self.osc[oscIndex].windowSize;
    }
}

void renderOscData(int oscIndex) {
    int windowIndex = ilog2(WINDOW_OSC);
    self.osc[oscIndex].bottomBound = self.osc[oscIndex].topBound * -1;
    /* set left and right bounds */
    if (!self.osc[oscIndex].stop) { 
        if (self.osc[oscIndex].trigger.triggerType == TRIGGER_NONE) {
            list_clear(self.osc[oscIndex].trigger.lastTriggerIndex);
            setBoundsNoTrigger(oscIndex);
        } else {
            self.osc[oscIndex].rightBound = self.osc[oscIndex].trigger.triggerIndex + self.osc[oscIndex].windowSize / 2;
            if (self.osc[oscIndex].rightBound > self.data -> data[self.osc[oscIndex].dataIndex].r -> length) {
                self.osc[oscIndex].rightBound = self.data -> data[self.osc[oscIndex].dataIndex].r -> length;
            }
            self.osc[oscIndex].leftBound = self.osc[oscIndex].trigger.triggerIndex - self.osc[oscIndex].windowSize / 2;
            if (self.osc[oscIndex].leftBound < 0) {
                self.osc[oscIndex].leftBound = 0;
            }

            /* identify triggerIndex */
            int dataLength = self.data -> data[self.osc[oscIndex].dataIndex].r -> length;
            if (self.osc[oscIndex].trigger.lastTriggerIndex -> length > 0 && self.osc[oscIndex].trigger.lastTriggerIndex -> data[0].i + self.osc[oscIndex].windowSize / 2 <= dataLength) {
                /* trigger takes some time to kick in */
                self.osc[oscIndex].trigger.triggerIndex = self.osc[oscIndex].trigger.lastTriggerIndex -> data[0].i;
                list_delete(self.osc[oscIndex].trigger.lastTriggerIndex, 0);
            }
            if (self.osc[oscIndex].trigger.triggerIndex == 0) {
                setBoundsNoTrigger(oscIndex);
            }
            if (self.osc[oscIndex].trigger.triggerType == TRIGGER_RISING_EDGE) {
                if (self.data -> data[self.osc[oscIndex].dataIndex].r -> data[dataLength - 2].d < 0 && self.data -> data[self.osc[oscIndex].dataIndex].r -> data[dataLength - 1].d >= 0 && dataLength > self.osc[oscIndex].windowSize / 2) {
                    list_append(self.osc[oscIndex].trigger.lastTriggerIndex, (unitype) (dataLength - 2), 'i');
                }
            }
            // printf("triggerIndex %d\n", self.osc[oscIndex].trigger.triggerIndex);
            // list_print(self.osc[oscIndex].trigger.lastTriggerIndex);
        }
    }
    if (self.windows[windowIndex].minimize == 0) {
        /* render window background */
        turtleRectangle(self.windows[windowIndex].windowCoords[0], self.windows[windowIndex].windowCoords[1], self.windows[windowIndex].windowCoords[2], self.windows[windowIndex].windowCoords[3], self.themeColors[self.theme + 12], self.themeColors[self.theme + 13], self.themeColors[self.theme + 14], 0);
        turtlePenSize(1);
        turtlePenColor(0, 0, 0);
        turtleGoto(self.windows[windowIndex].windowCoords[0], (self.windows[windowIndex].windowCoords[1] - self.windows[windowIndex].windowTop + self.windows[windowIndex].windowCoords[3]) / 2);
        turtlePenDown();
        turtleGoto(self.windows[windowIndex].windowCoords[2], (self.windows[windowIndex].windowCoords[1] - self.windows[windowIndex].windowTop + self.windows[windowIndex].windowCoords[3]) / 2);
        turtlePenUp();
        /* render data */
        turtlePenSize(1);
        turtlePenColor(self.themeColors[self.theme + 6], self.themeColors[self.theme + 7], self.themeColors[self.theme + 8]);
        double xquantum = (self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowCoords[0]) / (self.osc[oscIndex].rightBound - self.osc[oscIndex].leftBound - 1);
        for (int i = 0; i < self.osc[oscIndex].rightBound - self.osc[oscIndex].leftBound; i++) {
            turtleGoto(self.windows[windowIndex].windowCoords[0] + i * xquantum, self.windows[windowIndex].windowCoords[1] + ((self.data -> data[self.osc[oscIndex].dataIndex].r -> data[self.osc[oscIndex].leftBound + i].d - self.osc[oscIndex].bottomBound) / (self.osc[oscIndex].topBound - self.osc[oscIndex].bottomBound)) * (self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - self.windows[windowIndex].windowCoords[1]));
            turtlePenDown();
        }
        turtlePenUp();
        /* render mouse */
        // if (self.windowRender -> data[self.windowRender -> length - 1].i == WINDOW_OSC) {
            if (self.mx > self.windows[windowIndex].windowCoords[0] + 15 && self.my > self.windows[windowIndex].windowCoords[1] && self.mx < self.windows[windowIndex].windowCoords[2] && self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop) {
                int sample = round((self.mx - self.windows[windowIndex].windowCoords[0]) / xquantum);
                if (self.osc[oscIndex].leftBound + sample >= self.data -> data[self.osc[oscIndex].dataIndex].r -> length) {
                    return;
                }
                double sampleX = self.windows[windowIndex].windowCoords[0] + sample * xquantum;
                double sampleY = self.windows[windowIndex].windowCoords[1] + ((self.data -> data[self.osc[oscIndex].dataIndex].r -> data[self.osc[oscIndex].leftBound + sample].d - self.osc[oscIndex].bottomBound) / (self.osc[oscIndex].topBound - self.osc[oscIndex].bottomBound)) * (self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - self.windows[windowIndex].windowCoords[1]);
                turtleRectangle(sampleX - 1, self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop, sampleX + 1, self.windows[windowIndex].windowCoords[1], self.themeColors[self.theme + 21], self.themeColors[self.theme + 22], self.themeColors[self.theme + 23], 100);
                turtleRectangle(self.windows[windowIndex].windowCoords[0], sampleY - 1, self.windows[windowIndex].windowCoords[2], sampleY + 1, self.themeColors[self.theme + 21], self.themeColors[self.theme + 22], self.themeColors[self.theme + 23], 100);
                turtlePenColor(215, 215, 215);
                turtlePenSize(4);
                turtleGoto(sampleX, sampleY);
                turtlePenDown();
                turtlePenUp();
                char sampleValue[24];
                /* render side box */
                sprintf(sampleValue, "%.02lf", self.data -> data[self.osc[oscIndex].dataIndex].r -> data[self.osc[oscIndex].leftBound + sample].d);
                double boxLength = textGLGetStringLength(sampleValue, 8);
                double boxX = self.windows[windowIndex].windowCoords[0] + 12;
                if (sampleX - boxX < 40) {
                    boxX = self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowSide - boxLength - 5;
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
                if (boxX2 - 15 < self.windows[windowIndex].windowCoords[0]) {
                    boxX2 = self.windows[windowIndex].windowCoords[0] + 15;
                }
                if (boxX2 + boxLength2 + self.windows[windowIndex].windowSide + 5 > self.windows[windowIndex].windowCoords[2]) {
                    boxX2 = self.windows[windowIndex].windowCoords[2] - boxLength2 - self.windows[windowIndex].windowSide - 5;
                }
                turtleRectangle(boxX2 - 2, self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - 15, boxX2 + boxLength2 + 2, self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - 5, 215, 215, 215, 0);
                turtlePenColor(0, 0, 0);
                textGLWriteString(sampleValue, boxX2, self.windows[windowIndex].windowCoords[3] - 26, 8, 0);
            }
        // }
        /* render side axis */
        turtleRectangle(self.windows[windowIndex].windowCoords[0], self.windows[windowIndex].windowCoords[1], self.windows[windowIndex].windowCoords[0] + 10, self.windows[windowIndex].windowCoords[3], self.themeColors[self.theme + 21], self.themeColors[self.theme + 22], self.themeColors[self.theme + 23], 100);
        turtlePenColor(0, 0, 0);
        turtlePenSize(1);
        double ycenter = (self.windows[windowIndex].windowCoords[1] + self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop) / 2;
        turtleGoto(self.windows[windowIndex].windowCoords[0], ycenter);
        turtlePenDown();
        turtleGoto(self.windows[windowIndex].windowCoords[0] + 5, ycenter);
        turtlePenUp();
        int tickMarks = round(self.osc[oscIndex].topBound / 4) * 4;
        double culling = self.osc[oscIndex].topBound;
        while (culling > 60) {
            culling /= 4;
            tickMarks /= 4;
        }
        tickMarks = ceil(tickMarks / 4) * 4;
        double yquantum = (self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - self.windows[windowIndex].windowCoords[1]) / tickMarks;
        for (int i = 1; i < tickMarks; i++) {
            double ypos = self.windows[windowIndex].windowCoords[1] + i * yquantum;
            turtleGoto(self.windows[windowIndex].windowCoords[0], ypos);
            turtlePenDown();
            int tickLength = 2;
            if (i % (tickMarks / 4) == 0) {
                tickLength = 4;
            }
            turtleGoto(self.windows[windowIndex].windowCoords[0] + tickLength, ypos);
            turtlePenUp();
        }
        if (self.windowRender -> data[self.windowRender -> length - 1].i == WINDOW_OSC) {
            int mouseSample = round((self.my - self.windows[windowIndex].windowCoords[1]) / yquantum);
            if (mouseSample > 0 && mouseSample < tickMarks) {
                double ypos = self.windows[windowIndex].windowCoords[1] + mouseSample * yquantum;
                int tickLength = 2;
                if (mouseSample % (tickMarks / 4) == 0) {
                    tickLength = 4;
                }
                if (self.mx > self.windows[windowIndex].windowCoords[0] && self.mx < self.windows[windowIndex].windowCoords[0] + 15) {
                    turtleTriangle(self.windows[windowIndex].windowCoords[0] + tickLength + 2, ypos, self.windows[windowIndex].windowCoords[0] + tickLength + 10, ypos + 6, self.windows[windowIndex].windowCoords[0] + tickLength + 10, ypos - 6, 215, 215, 215, 0);
                    char tickValue[24];
                    sprintf(tickValue, "%d", (int) (self.osc[oscIndex].topBound / (tickMarks / 2) * mouseSample - self.osc[oscIndex].topBound));
                    turtlePenColor(215, 215, 215);
                    textGLWriteString(tickValue, self.windows[windowIndex].windowCoords[0] + tickLength + 13, ypos, 8, 0);
                }
            }
        }
    }
}

void renderFreqData() {
    int windowIndex = ilog2(WINDOW_FREQ);
    /* linear windowing function over 10% of the sample */
    int dataLength = self.osc[self.freqOscIndex].rightBound - self.osc[self.freqOscIndex].leftBound;
    int threshold = (dataLength) * 0.1;
    double damping = 1.0 / threshold;
    list_clear(self.windowData);
    for (int i = 0; i < dataLength; i++) {
        double dataPoint = self.data -> data[self.osc[self.freqOscIndex].dataIndex].r -> data[i + self.osc[self.freqOscIndex].leftBound].d;
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
    double xquantum = (self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowCoords[0] - self.windows[windowIndex].windowSide) / ((self.windowData -> length - 2) / self.freqZoom) * 2;
    if (self.windows[windowIndex].minimize == 0) {
        /* render window background */
        turtleRectangle(self.windows[windowIndex].windowCoords[0], self.windows[windowIndex].windowCoords[1], self.windows[windowIndex].windowCoords[2], self.windows[windowIndex].windowCoords[3], self.themeColors[self.theme + 12], self.themeColors[self.theme + 13], self.themeColors[self.theme + 14], 0);
        turtlePenSize(1);
        turtlePenColor(self.themeColors[self.theme + 6], self.themeColors[self.theme + 7], self.themeColors[self.theme + 8]);
        if (self.freqData -> length % 2) {
            xquantum *= (self.freqData -> length - 2.0) / (self.freqData -> length - 1.0);
        }
        self.freqRightBound = 1 + self.freqLeftBound + (self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowSide - self.windows[windowIndex].windowCoords[0]) / xquantum;
        if (self.freqRightBound > self.freqData -> length / 2 + self.freqData -> length % 2) {
            self.freqRightBound = self.freqData -> length / 2 + self.freqData -> length % 2;
        }
        for (int i = self.freqLeftBound; i < self.freqRightBound; i++) { // only render the bottom half of frequency graph
            double magnitude = self.freqData -> data[i].d;
            if (magnitude < 0) {
                magnitude *= -1;
            }
            turtleGoto(self.windows[windowIndex].windowCoords[0] + (i - self.freqLeftBound) * xquantum, self.windows[windowIndex].windowCoords[1] + 9 + ((magnitude - 0) / (self.topFreq - 0)) * (self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - self.windows[windowIndex].windowCoords[1]));
            turtlePenDown();
        }
        turtlePenUp();
        /* render mouse */
        // if (self.windowRender -> data[self.windowRender -> length - 1].i == WINDOW_FREQ) {
            if (self.mx > self.windows[windowIndex].windowCoords[0] && self.my > self.windows[windowIndex].windowCoords[1] && self.mx < self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowSide && self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop) {
                double sample = (self.mx - self.windows[windowIndex].windowCoords[0]) / xquantum + self.freqLeftBound;
                if (self.osc[self.freqOscIndex].leftBound + sample >= self.data -> data[self.osc[self.freqOscIndex].dataIndex].r -> length) {
                    return;
                }
                int roundedSample = round(sample);
                double sampleX = self.windows[windowIndex].windowCoords[0] + (roundedSample - self.freqLeftBound) * xquantum;
                double sampleY = 9 + self.windows[windowIndex].windowCoords[1] + (fabs(self.freqData -> data[roundedSample].d) / (self.topFreq)) * (self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - self.windows[windowIndex].windowCoords[1]);
                turtleRectangle(sampleX - 1, self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop, sampleX + 1, self.windows[windowIndex].windowCoords[1], self.themeColors[self.theme + 21], self.themeColors[self.theme + 22], self.themeColors[self.theme + 23], 100);
                turtleRectangle(self.windows[windowIndex].windowCoords[0], sampleY - 1, self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowSide, sampleY + 1, self.themeColors[self.theme + 21], self.themeColors[self.theme + 22], self.themeColors[self.theme + 23], 100);
                turtlePenColor(215, 215, 215);
                turtlePenSize(4);
                turtleGoto(sampleX, sampleY);
                turtlePenDown();
                turtlePenUp();
                char sampleValue[24];
                /* render side box */
                sprintf(sampleValue, "%.02lf", fabs(self.freqData -> data[roundedSample].d));
                double boxLength = textGLGetStringLength(sampleValue, 8);
                double boxX = self.windows[windowIndex].windowCoords[0] + 2;
                if (sampleX - boxX < 40) {
                    boxX = self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowSide - boxLength - 5;
                }
                double boxY = sampleY + 10;
                turtleRectangle(boxX, boxY - 5, boxX + 4 + boxLength, boxY + 5, 215, 215, 215, 0);
                turtlePenColor(0, 0, 0);
                textGLWriteString(sampleValue, boxX + 2, boxY - 1, 8, 0);
                /* render top box */
                sprintf(sampleValue, "%.1lfHz", sample / (dataLength / self.osc[self.freqOscIndex].samplesPerSecond));
                double boxLength2 = textGLGetStringLength(sampleValue, 8);
                double boxX2 = sampleX - boxLength2 / 2;
                if (boxX2 - 5 < self.windows[windowIndex].windowCoords[0]) {
                    boxX2 = self.windows[windowIndex].windowCoords[0] + 5;
                }
                if (boxX2 + boxLength2 + self.windows[windowIndex].windowSide + 5 > self.windows[windowIndex].windowCoords[2]) {
                    boxX2 = self.windows[windowIndex].windowCoords[2] - boxLength2 - self.windows[windowIndex].windowSide - 5;
                }
                turtleRectangle(boxX2 - 2, self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - 15, boxX2 + boxLength2 + 2, self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - 5, 215, 215, 215, 0);
                turtlePenColor(0, 0, 0);
                textGLWriteString(sampleValue, boxX2, self.windows[windowIndex].windowCoords[3] - 26, 8, 0);
                /* scrolling */
                const double scaleFactor = 1.25;
                double buckets = (self.mx - self.windows[windowIndex].windowCoords[0]) / xquantum;
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
    int windowIndex = ilog2(WINDOW_EDITOR);
    if (self.windows[windowIndex].minimize == 0) {
        /* render window background */
        turtleRectangle(self.windows[windowIndex].windowCoords[0], self.windows[windowIndex].windowCoords[1], self.windows[windowIndex].windowCoords[2], self.windows[windowIndex].windowCoords[3], self.themeColors[self.theme + 12], self.themeColors[self.theme + 13], self.themeColors[self.theme + 14], 0);
    }
}

void renderOrder() {
    for (int i = 0; i < self.windowRender -> length; i++) {
        switch (self.windowRender -> data[i].i) {
        case WINDOW_OSC:
            renderOscData(0);
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
        /* write title */
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
    glfwSetWindowSizeLimits(window, 128, 72, windowHeight * 16 / 9, windowHeight);
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
        self.cmdSocket = win32tcpCreateSocket();
        if (self.cmdSocket != NULL) {
            unsigned char receiveBuffer[10] = {0};
            win32tcpReceive(self.cmdSocket, receiveBuffer, 1);
            unsigned char amdc_cmd_id[2] = {12, 34};
            win32tcpSend(self.cmdSocket, amdc_cmd_id, 2);
            printf("Successfully created AMDC cmd socket with id %d\n", *receiveBuffer);
            self.cmdSocketID = *receiveBuffer;
        }
        self.commsEnabled = 1;
    }

    int tps = 120; // ticks per second (locked to fps in this case)
    uint64_t tick = 0;

    clock_t start;
    clock_t end;

    init(); // initialise empv
    turtleBgColor(self.themeColors[self.theme + 0], self.themeColors[self.theme + 1], self.themeColors[self.theme + 2]);
    /* populate sockets */
    int populatedSlots = self.logVariables -> length - 1; // subtract demo data
    if (self.commsEnabled == 1) {
        for (int i = 0; i < populatedSlots; i++) {
            SOCKET *sptr = win32tcpCreateSocket();
            if (sptr != NULL) {
                unsigned char receiveBuffer[10] = {0};
                win32tcpReceive(sptr, receiveBuffer, 1);
                unsigned char amdc_log_id[2] = {56, 78};
                win32tcpSend(sptr, amdc_log_id, 2);
                printf("Successfully created AMDC log socket with id %d\n", *receiveBuffer);
                int sID = *receiveBuffer;
                list_append(self.logSockets, (unitype) (void *) sptr, 'p');
                list_append(self.logSocketIDs, (unitype) sID, 'i');
            }
        }
        int threadArg[populatedSlots];
        for (int i = 0; i < populatedSlots; i++) {
            threadArg[i] = i + 1;
            pthread_create(&self.commsThread[i], NULL, commsThreadFunction, &threadArg[i]);
        }
        /* clear all streams */
        for (int i = 0; i < self.maxSlots; i++) {
            char command[128];
            sprintf(command, "log stream stop %d %d", i, self.logSocketIDs -> data[i].i);
            commsCommand(command);
        }
        /* start stream for logged variables */
        for (int i = 0; i < populatedSlots; i++) {
            if (self.logSlots -> data[i + 1].i != -1) {
                char command[128];
                sprintf(command, "log stream start %d %d", self.logSlots -> data[i + 1].i, self.logSocketIDs -> data[i + 1].i);
                commsCommand(command);
            }
        }
    }

    while (turtle.close == 0) { // main loop
        start = clock();
        /* populate demo data */
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