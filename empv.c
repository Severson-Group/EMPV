/* EMPV for windows
Features:
- Oscilloscope-like interface
- Frequency graph
- Orbital plot
*/

#include "include/ribbon.h"
#include "include/win32tcp.h"
#include "include/win32Tools.h"
#include "include/kissFFT.h"
#include <time.h>
#include <direct.h>
#include <pthread.h>

// #define DEBUGGING_FLAG // enable logging debugging (terminal)

#define TCP_RECEIVE_BUFFER_LENGTH        2048
#define MAX_SIMULTANEOUS_LOGGING_SOCKETS 4   // see https://docs.amdc.dev/getting-started/user-guide/logging/streaming.html#performance

#define DIAL_LINEAR       0
#define DIAL_LOG          1
#define DIAL_EXP          2

#define NUM_WINDOWS       10
#define WINDOW_INFO       1
#define WINDOW_FREQ       2
#define WINDOW_EDITOR     4
#define WINDOW_ORBIT      8
#define WINDOW_OSC        32

#define TRIGGER_TIMEOUT   150
#define PHASE_THRESHOLD   0.5
#define ORBIT_DIST_THRESH 2500

#define NUMBER_OF_OSC     4
#define NUMBER_OF_ORBIT   2

void delay_ms(int delay) {
    clock_t start;
    clock_t end;
    start = clock();
    end = clock();
    while ((double) (end - start) / CLOCKS_PER_SEC < delay / 1000) {
        end = clock();
    }
}

enum trigger_type {
    TRIGGER_NONE = 0,
    TRIGGER_RISING_EDGE,
    TRIGGER_FALLING_EDGE
};

typedef struct { // dial
    char label[24];
    int window;
    int type;
    int status[2];
    double size;
    double position[2]; // xOffset, yOffset
    double range[2];
    double renderNumberFactor; // divide rendered variable by this amount
    double defaultValue;
    double *variable;
} dial_t;

typedef struct { // switch
    char label[24];
    int window;
    int status;
    double size;
    double position[2]; // xOffset, yOffset
    int *variable;
} switch_t;

typedef struct {
    char inUse;
    int selectIndex;
    double color[3];
} dropdown_metadata_t;

typedef struct { // dropdown 
    list_t *options;
    char label[24];
    int index;
    int window;
    int status;
    double size;
    double position[2]; // xOffset, yOffset
    double maxXfactor;
    int *variable;
    dropdown_metadata_t metadata;
} dropdown_t;

#define BUTTON_SHAPE_RECTANGLE         0
#define BUTTON_SHAPE_ROUNDED_RECTANGLE 1
#define BUTTON_SHAPE_CIRCLE            2

typedef struct { // button
    char label[24];
    int window;
    int status;
    int shape;
    double size;
    double position[2]; // xOffset, yOffset
    int *variable;
} button_t;

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
    int close;
    list_t *dials;
    list_t *switches;
    list_t *dropdowns;
    list_t *buttons;
    int dropdownLogicIndex;
} window_t;

typedef struct {
    double threshold;
    int type;
    int index;
    int timeout;
    list_t *lastIndex;
} trigger_settings_t;

typedef struct { // oscilloscope view
    trigger_settings_t trigger;
    int dataIndex[4]; // index of data list for oscilloscope source (up to four channels)
    int oldSelectedChannel; // keep track of selected channel last tick
    int selectedChannel; // selected channel (1-4) of oscilloscope
    int leftBound[4]; // left bound (index in data list) - local per channel
    int rightBound[4]; // right bound (index in data list) - local per channel
    double bottomBound[4]; // bottom bound (y value) - local per channel
    double topBound[4]; // top bound (y value) - local per channel
    double dummyTopBound; // dummy top bound for manipulation via dial
    double dummyOffset; // dummy offset for manipulation via dial
    double windowSizeMicroseconds; // size of window (in microseconds) - global per oscilloscope
    int windowSizeSamples[4]; // size of window (in samples) - local per channel
    int stop; // pause and unpause - global per oscilloscope
    int above; // whether the current data point is above or below the trigger point
} oscilloscope_t;

typedef struct { // orbit view
    int stop;
    double scale[2];
    double offset[2];
    double samples;
    int stopIndex[2]; // index of most recent orbit sample
    int dataIndex[2]; // index of data list for orbit source (X, Y)
    int plotIndex[2]; // index inside data list for orbit plot (X, Y)
} orbit_t;

typedef struct {
    char name[128]; // name of variable
    int slot; // Slot # within AMDC (e.g. Slot 0: LOG_amdc_channel_1), -1 when not in use
    SOCKET *socketPtr; // pointer to SOCKET used to stream data for this variable, NULL when not in use
    int socketID; // ID of socket on AMDC (AMDC gives us this when the socket is created), -1 when not in use
    pthread_t thread; // data logging thread for this variable, -1 when not in use
} logVariable_t;

typedef struct { // all the empv shared state is here
    /* comms */
    int tcpInit;
    int threadCloseSignal;
    char commsEnabled;
    SOCKET *cmdSocket;
    int cmdSocketID;
    uint8_t tcpAsciiReceiveBuffer[TCP_RECEIVE_BUFFER_LENGTH];
    int maxSlots; // maximum logging slots on AMDC
    /* general */
        list_t *data; // a list of lists of all data collected through ethernet (first element is samples/s)
        list_t *logVariables; // a list of variables logged on the AMDC (logVariable_t)
        list_t *usedVariableIndices;
        list_t *oldUsedVariableIndices;
        list_t *windowRender; // which order to render windows in
        window_t windows[NUM_WINDOWS]; // window variables
        /* mouse variables */
        double mx; // mouseX
        double my; // mouseY
        double mw; // mouse wheel
        char mouseDown; // mouse down
        char rightMouseDown; // right mouse button
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
        list_t *oscTitles; // list of oscilloscope titles (for frequency dropdown)
        oscilloscope_t osc[NUMBER_OF_OSC]; // up to four oscilloscopes
        int newOsc;
    /* frequency view */
        list_t *windowData; // segment of normal data through windowing function
        list_t *freqData; // frequency data
        list_t *phaseData; // phase data
        int freqOscIndex; // referenced oscilloscope
        int freqOscChannel; // referenced channel
        int freqLeftBound;
        int freqRightBound;
        double freqZoom;
        double topFreq; // top bound (y value)
    /* orbit view */
        orbit_t orbit[NUMBER_OF_ORBIT]; // up to two orbit plots
        int newOrbit;
    /* editor view */
        double editorBottomBound;
        double editorWindowSize; // size of window
    /* info view */
        int infoRefresh;
        double infoAnimation;

} empv_t;

/* global state */
empv_t self;

/* initialise UI elements */
dial_t *dialInit(char *label, double *variable, int window, int type, double xOffset, double yOffset, double size, double bottom, double top, double renderNumberFactor) {
    dial_t *dial = malloc(sizeof(dial_t));
    if (label == NULL) {
        memcpy(dial -> label, "", strlen("") + 1);
    } else {
        memcpy(dial -> label, label, strlen(label) + 1);
    }
    dial -> status[0] = 0;
    dial -> window = window;
    dial -> type = type;
    dial -> position[0] = xOffset;
    dial -> position[1] = yOffset;
    dial -> size = size;
    dial -> range[0] = bottom;
    dial -> range[1] = top;
    dial -> variable = variable;
    dial -> renderNumberFactor = renderNumberFactor;
    dial -> defaultValue = *variable;
    return dial;
}

switch_t *switchInit(char *label, int *variable, int window, double xOffset, double yOffset, double size) {
    switch_t *switchp = malloc(sizeof(switch_t));
    if (label == NULL) {
        memcpy(switchp -> label, "", strlen("") + 1);
    } else {
        memcpy(switchp -> label, label, strlen(label) + 1);
    }
    switchp -> status = 0;
    switchp -> window = window;
    switchp -> position[0] = xOffset;
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

dropdown_t *dropdownInit(char *label, list_t *options, int *variable, int window, double xOffset, double yOffset, double size, dropdown_metadata_t metadata) {
    dropdown_t *dropdown = malloc(sizeof(dropdown_t));
    if (label == NULL) {
        memcpy(dropdown -> label, "", strlen("") + 1);
    } else {
        memcpy(dropdown -> label, label, strlen(label) + 1);
    }
    dropdown -> options = options;
    dropdown -> index = *variable;
    dropdown -> status = 0;
    dropdown -> window = window;
    dropdown -> position[0] = xOffset;
    dropdown -> position[1] = yOffset;
    dropdown -> size = size;
    dropdown -> variable = variable;
    dropdown -> metadata = metadata;
    dropdownCalculateMax(dropdown);
    return dropdown;
}

button_t *buttonInit(char *label, int *variable, int window, double xOffset, double yOffset, double size, int shape) {
    button_t *button = malloc(sizeof(button_t));
    if (label == NULL) {
        memcpy(button -> label, "", strlen("") + 1);
    } else {
        memcpy(button -> label, label, strlen(label) + 1);
    }
    button -> status = 0;
    button -> window = window;
    button -> shape = shape;
    button -> position[0] = xOffset;
    button -> position[1] = yOffset;
    button -> size = size;
    button -> variable = variable;
    return button;
}

logVariable_t *variableInit(char *name, int slot, SOCKET *socketPtr, int socketID, pthread_t thread) {
    logVariable_t *variable = malloc(sizeof(logVariable_t));
    if (name == NULL) {
        memcpy(variable -> name, "", strlen("") + 1);
    } else {
        memcpy(variable -> name, name, strlen(name) + 1);
    }
    variable -> slot = slot;
    variable -> socketPtr = socketPtr;
    variable -> socketID = socketID;
    variable -> thread = thread;
    return variable;
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

void fft_list_wrapper(list_t *samples, list_t *frequencyOutput, list_t *phaseOutput) {
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
        double fftSample = sqrt(complexSamples[i].r * complexSamples[i].r + complexSamples[i].i * complexSamples[i].i) / self.osc[self.freqOscIndex].windowSizeSamples[self.freqOscChannel]; // divide by closest rounded down power of 2 instead of window size
        list_append(frequencyOutput, (unitype) fftSample, 'd');
        double fftPhase = 0.0;
        /* https://www.gaussianwaves.com/2015/11/interpreting-fft-results-obtaining-magnitude-and-phase-information/ */
        if (fftSample > PHASE_THRESHOLD) {
            fftPhase = atan2(complexSamples[i].i, complexSamples[i].r);
        }
        list_append(phaseOutput, (unitype) fftPhase, 'd');
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
    int dataIndex = ((logVariable_t *) self.logVariables -> data[logSlotIndex].p) -> slot + 1;
    win32tcpReceive2(((logVariable_t *) self.logVariables -> data[logSlotIndex].p) -> socketPtr, tcpLoggingReceiveBuffer, TCP_RECEIVE_BUFFER_LENGTH);
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
            /* add value to data */
            float dataValue = *(float *) &data;
            list_append(self.data -> data[dataIndex].r, (unitype) (double) dataValue, 'd');
            index += 4;
        } else {
            printf("bad packet at index %d\n", index);
            index += 4;
        }
    }
}

void *commsThreadFunction(void *arg) {
    int index = *(int *) arg;
    int *socketID = &(((logVariable_t *) self.logVariables -> data[index].p) -> socketID);
    /* populate real data */
    while (1) {
        if (self.commsEnabled == 1) {
            if (*socketID != -1) {
                commsGetData(index);
            }
        }
        if (self.threadCloseSignal) {
            return NULL;
        }
    }
    return NULL;
}

void *specialInitThread(void *arg) {
    self.tcpInit = 0;
    if (win32tcpInit("192.168.1.10", "7")) {
        self.tcpInit = -1;
        pthread_exit(NULL);
    }
    self.tcpInit = 1;
    pthread_exit(NULL);
}

void gatherUsedSockets() {
    /* gather all used sockets */
    list_clear(self.usedVariableIndices);
    for (int i = 0; i < NUMBER_OF_ORBIT; i++) {
        for (int j = 0; j < 2; j++) {
            int usedChannel = self.orbit[i].dataIndex[j];
            if (list_count(self.usedVariableIndices, (unitype) usedChannel, 'i') == 0) {
                list_append(self.usedVariableIndices, (unitype) usedChannel, 'i');
            }
        }
    }
    for (int i = 0; i < NUMBER_OF_OSC; i++) {
        for (int j = 0; j < 4; j++) {
            int usedChannel = self.osc[i].dataIndex[j];
            if (list_count(self.usedVariableIndices, (unitype) usedChannel, 'i') == 0) {
                list_append(self.usedVariableIndices, (unitype) usedChannel, 'i');
            }
        }
    }
    int indexOfZero = list_find(self.usedVariableIndices, (unitype) 0, 'i');
    while (indexOfZero != -1) {
        list_delete(self.usedVariableIndices, indexOfZero);
        indexOfZero = list_find(self.usedVariableIndices, (unitype) 0, 'i');
    }
    if (self.usedVariableIndices -> length > MAX_SIMULTANEOUS_LOGGING_SOCKETS) {
        printf("warning - only %d logging sockets allowed concurrently. You are requesting %d simultaneous sources\n", MAX_SIMULTANEOUS_LOGGING_SOCKETS, self.usedVariableIndices -> length);
        while (self.usedVariableIndices -> length > MAX_SIMULTANEOUS_LOGGING_SOCKETS) {
            list_pop(self.usedVariableIndices);
        }
    }
}

void populateUsedSockets() {
    gatherUsedSockets();
    list_t *toAdd = list_init();
    list_t *toRemove = list_init();
    for (int i = 0; i < self.usedVariableIndices -> length; i++) {
        if (list_remove(self.oldUsedVariableIndices, self.usedVariableIndices -> data[i], 'i') == -1) {
            list_append(toAdd, self.usedVariableIndices -> data[i], 'i');
        }
    }
    for (int i = 0; i < self.oldUsedVariableIndices -> length; i++) {
        if (list_count(self.usedVariableIndices, self.oldUsedVariableIndices -> data[i], 'i') == 0) {
            list_append(toRemove, self.oldUsedVariableIndices -> data[i], 'i');
        }
    }
    #ifdef DEBUGGING_FLAG
    printf("toAdd: ");
    list_print(toAdd);
    printf("toRemove: ");
    list_print(toRemove);
    printf("usedVariablesIndices: ");
    list_print(self.usedVariableIndices);
    #endif

    if (self.commsEnabled == 1) {
        /* open a new logging socket for each used logged variable */
        for (int i = 1; i < self.logVariables -> length; i++) {
            if (list_count(toAdd, (unitype) i, 'i') > 0) {
                SOCKET *sptr = win32tcpCreateSocket();
                if (sptr != NULL) {
                    unsigned char receiveBuffer[10] = {0};
                    win32tcpReceive(sptr, receiveBuffer, 1);
                    unsigned char amdc_log_id[2] = {56, 78};
                    win32tcpSend(sptr, amdc_log_id, 2);
                    printf("Successfully opened AMDC log socket with id %d\n", *receiveBuffer);
                    int sID = *receiveBuffer;
                    ((logVariable_t *) self.logVariables -> data[i].p) -> socketPtr = sptr;
                    ((logVariable_t *) self.logVariables -> data[i].p) -> socketID = sID;
                }
            }
            if (list_count(toRemove, (unitype) i, 'i') > 0) {
                int savedSocketID = ((logVariable_t *) self.logVariables -> data[i].p) -> socketID;
                ((logVariable_t *) self.logVariables -> data[i].p) -> socketID = -1;
                delay_ms(10); // allow thread function to exit
                closesocket(*(((logVariable_t *) self.logVariables -> data[i].p) -> socketPtr));
                ((logVariable_t *) self.logVariables -> data[i].p) -> socketPtr = NULL;
                printf("Successfully closed AMDC log socket with id %d\n", savedSocketID);
            }    
        }
        int threadArg[self.logVariables -> length];
        for (int i = 1; i < self.logVariables -> length; i++) {
            if (list_count(toAdd, (unitype) i, 'i') > 0) {
                threadArg[i] = i;
                pthread_create(&((logVariable_t *) self.logVariables -> data[i].p) -> thread, NULL, commsThreadFunction, &threadArg[i]);
            }
        }
        /* clear all streams - FIXME */
        // for (int i = 0; i < self.maxSlots; i++) {
        //     char command[128];
        //     sprintf(command, "log stream stop %d %d", i, ((logVariable_t *) self.logVariables -> data[i + 1].p) -> socketID);
        //     printf("%s\n", command);
        //     commsCommand(command);
        // }
        /* start stream for logged variables */
        for (int i = 1; i < self.logVariables -> length; i++) {
            if (list_count(toAdd, (unitype) i, 'i') > 0) {
                logVariable_t *variable = self.logVariables -> data[i].p;
                if (variable -> slot != -1) {
                    char command[128];
                    sprintf(command, "log stream start %d %d", variable -> slot, variable -> socketID);
                    commsCommand(command);
                }
            }
        }
    }
    list_free(toAdd);
    list_free(toRemove);
    list_copy(self.usedVariableIndices, self.oldUsedVariableIndices);
}

void populateLoggedVariables() {
    self.threadCloseSignal = 1; // destroy all existing threads
    delay_ms(100);
    list_clear(self.data);
    list_append(self.data, (unitype) list_init(), 'r'); // unused list
    list_append(self.data -> data[0].r, (unitype) 120.0, 'd'); // dummy 120 samples/s

    list_clear(self.logVariables);
    logVariable_t *dummyVariable = variableInit("Unused", -1, NULL, -1, -1);
    list_append(self.logVariables, (unitype) (void *) dummyVariable, 'p');
    if (self.commsEnabled == 0) {
        /* make demo slots */
        logVariable_t *demoVariable1 = variableInit("Demo1", -1, NULL, -1, -1);
        list_append(self.logVariables, (unitype) (void *) demoVariable1, 'p');
        list_append(self.data, (unitype) list_init(), 'r');
        list_append(self.data -> data[self.data -> length - 1].r, (unitype) 120.0, 'd'); // set samples/s

        logVariable_t *demoVariable2 = variableInit("Demo2", -1, NULL, -1, -1);
        list_append(self.logVariables, (unitype) (void *) demoVariable2, 'p');
        list_append(self.data, (unitype) list_init(), 'r');
        list_append(self.data -> data[self.data -> length - 1].r, (unitype) 240.0, 'd'); // set samples/s

        logVariable_t *demoVariable3 = variableInit("Demo3", -1, NULL, -1, -1);
        list_append(self.logVariables, (unitype) (void *) demoVariable3, 'p');
        list_append(self.data, (unitype) list_init(), 'r');
        list_append(self.data -> data[self.data -> length - 1].r, (unitype) 120.0, 'd'); // set samples/s

        logVariable_t *demoVariable4 = variableInit("Demo4", -1, NULL, -1, -1);
        list_append(self.logVariables, (unitype) (void *) demoVariable4, 'p');
        list_append(self.data, (unitype) list_init(), 'r');
        list_append(self.data -> data[self.data -> length - 1].r, (unitype) 120.0, 'd'); // set samples/s
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
                logVariable_t *newVariable = variableInit(testString + 8, slotNum, NULL, -1, -1);
                list_append(self.logVariables, (unitype) (void *) newVariable, 'p');
                list_append(self.data, (unitype) list_init(), 'r');
                #ifdef DEBUGGING_FLAG
                printf("identified logging variable: %s\n", testString + 8);
                #endif
                // list_append(self.data -> data[self.data -> length - 1].r, (unitype) 120.0, 'd'); // set samples/s
                break;
            case 4: // Type: <type>
                break;
            case 3: // Memory address: <address>
                break;
            case 2: // Sampling interval (usec): <usec>
                double samplingInterval = 0.0; // in microseconds
                sscanf(testString + 28, "%lf", &samplingInterval);
                list_append(self.data -> data[self.data -> length - 1].r, (unitype) (1 / (samplingInterval / 1000000)), 'd'); // set samples/s
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
    #ifdef DEBUGGING_FLAG
    printf("Max Logging Slots: %d\n", self.maxSlots);
    #endif
    self.threadCloseSignal = 0; // enable threads
    /* populate sockets */
    populateUsedSockets();
}

void createNewOsc() {
    if (self.newOsc > NUMBER_OF_OSC - 1) {
        return;
    }
    self.osc[self.newOsc].trigger.threshold = 0.0;
    self.osc[self.newOsc].trigger.type = TRIGGER_NONE;
    self.osc[self.newOsc].trigger.index = 0;
    self.osc[self.newOsc].trigger.lastIndex = list_init();
    if (self.logVariables -> length > 1) {
        self.osc[self.newOsc].dataIndex[0] = 1; // Demo 1
    } else {
        self.osc[self.newOsc].dataIndex[0] = 0; // unused
    }
    self.osc[self.newOsc].dataIndex[1] = 0; // unused
    self.osc[self.newOsc].dataIndex[2] = 0; // unused
    self.osc[self.newOsc].dataIndex[3] = 0; // unused
    self.osc[self.newOsc].selectedChannel = 0;
    self.osc[self.newOsc].oldSelectedChannel = 0;
    for (int i = 0; i < 4; i++) {
        self.osc[self.newOsc].leftBound[i] = 1;
        self.osc[self.newOsc].rightBound[i] = 1;
        self.osc[self.newOsc].bottomBound[i] = -100;
        self.osc[self.newOsc].topBound[i] = 100;
    }
    self.osc[self.newOsc].dummyTopBound = 100;
    self.osc[self.newOsc].dummyOffset = 0;
    self.osc[self.newOsc].windowSizeMicroseconds = 1000000;
    self.osc[self.newOsc].stop = 0;
    self.osc[self.newOsc].above = 0;
    int oscIndex = ilog2(WINDOW_OSC) + self.newOsc;
    sprintf(self.windows[oscIndex].title, "Oscilloscope %d", self.newOsc + 1);
    list_append(self.oscTitles, (unitype) self.windows[oscIndex].title, 's');
    self.windows[oscIndex].windowCoords[0] = -317;
    self.windows[oscIndex].windowCoords[1] = 0;
    self.windows[oscIndex].windowCoords[2] = 37;
    self.windows[oscIndex].windowCoords[3] = 167;
    self.windows[oscIndex].windowTop = 15;
    self.windows[oscIndex].windowSide = 100;
    self.windows[oscIndex].windowMinX = 100 + self.windows[oscIndex].windowSide;
    self.windows[oscIndex].windowMinY = 150 + self.windows[oscIndex].windowTop;
    self.windows[oscIndex].minimize = 0;
    self.windows[oscIndex].move = 0;
    self.windows[oscIndex].click = 0;
    self.windows[oscIndex].resize = 0;
    self.windows[oscIndex].dials = list_init();
    self.windows[oscIndex].switches = list_init();
    self.windows[oscIndex].dropdowns = list_init();
    self.windows[oscIndex].buttons = list_init();
    list_append(self.windows[oscIndex].dials, (unitype) (void *) dialInit("Win (ms)", &self.osc[self.newOsc].windowSizeMicroseconds, WINDOW_OSC * pow2(self.newOsc), DIAL_EXP, -25, -25 - self.windows[oscIndex].windowTop, 8, 1000, 10000000, 1000), 'p');
    list_append(self.windows[oscIndex].dials, (unitype) (void *) dialInit("Scale", &self.osc[self.newOsc].dummyTopBound, WINDOW_OSC * pow2(self.newOsc), DIAL_EXP, -25, -60 - self.windows[oscIndex].windowTop, 8, 1, 10000, 1), 'p');
    list_append(self.windows[oscIndex].dials, (unitype) (void *) dialInit("Offset", &self.osc[self.newOsc].dummyOffset, WINDOW_OSC * pow2(self.newOsc), DIAL_LINEAR, -25, -95 - self.windows[oscIndex].windowTop, 8, -1000, 1000, 1), 'p');
    list_append(self.windows[oscIndex].switches, (unitype) (void *) switchInit("Pause", &self.osc[self.newOsc].stop, WINDOW_OSC * pow2(self.newOsc), -25, -135 - self.windows[oscIndex].windowTop, 8), 'p');
    list_append(self.windows[oscIndex].dials, (unitype) (void *) dialInit("Threshold", &self.osc[self.newOsc].trigger.threshold, WINDOW_OSC * pow2(self.newOsc), DIAL_LINEAR, -75, -135 - self.windows[oscIndex].windowTop, 8, -100, 100, 1), 'p');
    list_t *triggerOptions = list_init();
    list_append(triggerOptions, (unitype) "None", 's');
    list_append(triggerOptions, (unitype) "Rising", 's');
    list_append(triggerOptions, (unitype) "Falling", 's');
    dropdown_metadata_t metadata;
    metadata.inUse = 0;
    list_append(self.windows[oscIndex].dropdowns, (unitype) (void *) dropdownInit("Trigger", triggerOptions, &self.osc[self.newOsc].trigger.type, WINDOW_OSC * pow2(self.newOsc), -70, -100 - self.windows[oscIndex].windowTop, 8, metadata), 'p');
    metadata.inUse = 1;
    metadata.selectIndex = 3;
    metadata.color[0] = self.themeColors[self.theme + 33];
    metadata.color[1] = self.themeColors[self.theme + 34];
    metadata.color[2] = self.themeColors[self.theme + 35];
    /* putting self.logVariables as the options list actually works since the pointer can be interpreted as a string (since the name field is at the start of the logVariable_t struct) */
    list_append(self.windows[oscIndex].dropdowns, (unitype) (void *) dropdownInit(NULL, self.logVariables, &self.osc[self.newOsc].dataIndex[3], WINDOW_OSC * pow2(self.newOsc), -70, -70 - self.windows[oscIndex].windowTop, 8, metadata), 'p');
    metadata.selectIndex = 2;
    metadata.color[0] = self.themeColors[self.theme + 30];
    metadata.color[1] = self.themeColors[self.theme + 31];
    metadata.color[2] = self.themeColors[self.theme + 32];
    list_append(self.windows[oscIndex].dropdowns, (unitype) (void *) dropdownInit(NULL, self.logVariables, &self.osc[self.newOsc].dataIndex[2], WINDOW_OSC * pow2(self.newOsc), -70, -50 - self.windows[oscIndex].windowTop, 8, metadata), 'p');
    metadata.selectIndex = 1;
    metadata.color[0] = self.themeColors[self.theme + 27];
    metadata.color[1] = self.themeColors[self.theme + 28];
    metadata.color[2] = self.themeColors[self.theme + 29];
    list_append(self.windows[oscIndex].dropdowns, (unitype) (void *) dropdownInit(NULL, self.logVariables, &self.osc[self.newOsc].dataIndex[1], WINDOW_OSC * pow2(self.newOsc), -70, -30 - self.windows[oscIndex].windowTop, 8, metadata), 'p');
    metadata.selectIndex = 0;
    metadata.color[0] = self.themeColors[self.theme + 24];
    metadata.color[1] = self.themeColors[self.theme + 25];
    metadata.color[2] = self.themeColors[self.theme + 26];
    list_append(self.windows[oscIndex].dropdowns, (unitype) (void *) dropdownInit(NULL, self.logVariables, &self.osc[self.newOsc].dataIndex[0], WINDOW_OSC * pow2(self.newOsc), -70, -10 - self.windows[oscIndex].windowTop, 8, metadata), 'p');
    self.windows[oscIndex].dropdownLogicIndex = -1;
    list_append(self.windowRender, (unitype) (WINDOW_OSC * pow2(self.newOsc)), 'i');
    self.newOsc++;
    populateUsedSockets();
}

void createNewOrbit() {
    if (self.newOrbit > NUMBER_OF_ORBIT - 1) {
        return;
    }
    self.orbit[self.newOrbit].stop = 0;
    self.orbit[self.newOrbit].scale[0] = 70;
    self.orbit[self.newOrbit].scale[1] = 70;
    self.orbit[self.newOrbit].offset[0] = 0;
    self.orbit[self.newOrbit].offset[1] = 0;
    self.orbit[self.newOrbit].stopIndex[0] = 1;
    self.orbit[self.newOrbit].stopIndex[1] = 1;
    self.orbit[self.newOrbit].samples = 20;
    int orbitIndex = ilog2(WINDOW_ORBIT) + self.newOrbit;
    sprintf(self.windows[orbitIndex].title, "Orbit %d", self.newOrbit + 1);
    self.windows[orbitIndex].windowCoords[0] = -317;
    self.windows[orbitIndex].windowCoords[1] = -161;
    self.windows[orbitIndex].windowCoords[2] = -55;
    self.windows[orbitIndex].windowCoords[3] = -3;
    self.windows[orbitIndex].windowTop = 15;
    self.windows[orbitIndex].windowSide = 50;
    self.windows[orbitIndex].windowMinX = 100 + self.windows[orbitIndex].windowSide;
    self.windows[orbitIndex].windowMinY = 120 + self.windows[orbitIndex].windowTop;
    self.windows[orbitIndex].minimize = 0;
    self.windows[orbitIndex].move = 0;
    self.windows[orbitIndex].click = 0;
    self.windows[orbitIndex].resize = 0;
    self.windows[orbitIndex].dials = list_init();
    self.windows[orbitIndex].switches = list_init();
    self.windows[orbitIndex].dropdowns = list_init();
    self.windows[orbitIndex].buttons = list_init();
    dropdown_metadata_t metadata;
    metadata.inUse = 0;
    list_append(self.windows[orbitIndex].dropdowns, (unitype) (void *) dropdownInit("Y source", self.logVariables, &self.orbit[self.newOrbit].dataIndex[1], WINDOW_ORBIT * pow2(self.newOrbit), -90, -60 - self.windows[orbitIndex].windowTop, 8, metadata), 'p');
    list_append(self.windows[orbitIndex].dropdowns, (unitype) (void *) dropdownInit("X source", self.logVariables, &self.orbit[self.newOrbit].dataIndex[0], WINDOW_ORBIT * pow2(self.newOrbit), -90, -25 - self.windows[orbitIndex].windowTop, 8, metadata), 'p');
    list_append(self.windows[orbitIndex].dials, (unitype) (void *) dialInit("Scale", &self.orbit[self.newOrbit].scale[0], WINDOW_ORBIT * pow2(self.newOrbit), DIAL_EXP, -55, -25 - self.windows[orbitIndex].windowTop, 8, 1, 500, 1), 'p');
    list_append(self.windows[orbitIndex].dials, (unitype) (void *) dialInit("Scale", &self.orbit[self.newOrbit].scale[1], WINDOW_ORBIT * pow2(self.newOrbit), DIAL_EXP, -55, -60 - self.windows[orbitIndex].windowTop, 8, 1, 500, 1), 'p');
    list_append(self.windows[orbitIndex].dials, (unitype) (void *) dialInit("Offset", &self.orbit[self.newOrbit].offset[0], WINDOW_ORBIT * pow2(self.newOrbit), DIAL_LINEAR, -20, -25 - self.windows[orbitIndex].windowTop, 8, 500, -500, 1), 'p');
    list_append(self.windows[orbitIndex].dials, (unitype) (void *) dialInit("Offset", &self.orbit[self.newOrbit].offset[1], WINDOW_ORBIT * pow2(self.newOrbit), DIAL_LINEAR, -20, -60 - self.windows[orbitIndex].windowTop, 8, -500, 500, 1), 'p');
    list_append(self.windows[orbitIndex].dials, (unitype) (void *) dialInit("Samples", &self.orbit[self.newOrbit].samples, WINDOW_ORBIT * pow2(self.newOrbit), DIAL_EXP, -90, -95 - self.windows[orbitIndex].windowTop, 8, 1, 500, 1), 'p');
    list_append(self.windows[orbitIndex].switches, (unitype) (void *) switchInit("Pause", &self.orbit[self.newOrbit].stop, WINDOW_ORBIT * pow2(self.newOrbit), -20, -95 - self.windows[orbitIndex].windowTop, 8), 'p');
    list_append(self.windowRender, (unitype) (WINDOW_ORBIT * pow2(self.newOrbit)), 'i');
    self.newOrbit++;
}

/* initialises the empv variabes (shared state) */
void init() {
/* comms */
    self.threadCloseSignal = 0;
    self.maxSlots = 0;
    for (int i = 0; i < TCP_RECEIVE_BUFFER_LENGTH; i++) {
        self.tcpAsciiReceiveBuffer[i] = 0;
    }
/* color */
    double themeCopy[90] = {
        /* light theme */
        255, 255, 255, // background color
        195, 195, 195, // window color
        255, 0, 0, // data color (default)
        0, 0, 0, // text color
        230, 230, 230, // window background color
        0, 144, 20, // switch toggled on color
        255, 0, 0, // switch toggled off color
        160, 160, 160, // sidebar and bottom bar color
        255, 0, 0, // data color (channel 1)
        255, 0, 0, // data color (channel 2)
        255, 0, 0, // data color (channel 3)
        255, 0, 0, // data color (channel 4)
        255, 0, 0, // phase data color
        186, 41, 26, // [X] color
        230, 41, 41, // [X] color (hover)
        /* dark theme */
        60, 60, 60, // background color (0)
        10, 10, 10, // window color (3)
        19, 236, 48, // data color (default) (6)
        200, 200, 200, // text color (9)
        80, 80, 80, // window background color (12)
        0, 255, 0, // switch toggled on color (15)
        164, 28, 9, // switch toggled off color (18)
        30, 30, 30, // sidebar and bottom bar color (21)
        19, 236, 48, // data color (channel 1) (24)
        0, 221, 255, // data color (channel 2) (25)
        200, 200, 200, // data color (channel 3) (26)
        232, 15, 136, // data color (channel 4) (27)
        255, 0, 0, // phase data color
        186, 41, 26, // [X] color
        230, 41, 41, // [X] color (hover)
    };
    memcpy(self.themeColors, themeCopy, sizeof(themeCopy));
    self.themeDark = sizeof(themeCopy) / sizeof(double) / 2;
    self.theme = self.themeDark;
    if (self.theme == 0) {
        ribbonLightTheme();
    } else {
        ribbonDarkTheme();
    }
    /* data */
    self.logVariables = list_init();
    self.usedVariableIndices = list_init();
    self.oldUsedVariableIndices = list_init();
    logVariable_t *dummyVariable = variableInit("Unused", -1, NULL, -1, -1);
    list_append(self.logVariables, (unitype) (void *) dummyVariable, 'p');
    self.data = list_init();
    populateLoggedVariables(); // gather logged variables
    self.windowRender = list_init();
    list_append(self.windowRender, (unitype) WINDOW_FREQ, 'i');
    list_append(self.windowRender, (unitype) WINDOW_EDITOR, 'i'); // unfinished feature
    list_append(self.windowRender, (unitype) WINDOW_INFO, 'i');
    self.anchorX = 0;
    self.anchorY = 0;
    self.dialAnchorX = 0;
    self.dialAnchorY = 0;
    /* osc */
    self.newOsc = 0;
    self.oscTitles = list_init();
    createNewOsc();
    /* frequency */
    self.windowData = list_init();
    self.freqData = list_init();
    self.phaseData = list_init();
    self.freqOscIndex = 0;
    self.freqOscChannel = 0;
    self.freqLeftBound = 0;
    self.freqRightBound = 0;
    self.freqZoom = 1.0;
    self.topFreq = 20;
    int freqIndex = ilog2(WINDOW_FREQ);
    strcpy(self.windows[freqIndex].title, "Frequency");
    self.windows[freqIndex].windowCoords[0] = 40;
    self.windows[freqIndex].windowCoords[1] = 0;
    self.windows[freqIndex].windowCoords[2] = 317;
    self.windows[freqIndex].windowCoords[3] = 167;
    self.windows[freqIndex].windowTop = 15;
    self.windows[freqIndex].windowSide = 50;
    self.windows[freqIndex].windowMinX = 100 + self.windows[freqIndex].windowSide;
    self.windows[freqIndex].windowMinY = 120 + self.windows[freqIndex].windowTop;
    self.windows[freqIndex].minimize = 0;
    self.windows[freqIndex].move = 0;
    self.windows[freqIndex].click = 0;
    self.windows[freqIndex].resize = 0;
    self.windows[freqIndex].dials = list_init();
    self.windows[freqIndex].switches = list_init();
    self.windows[freqIndex].dropdowns = list_init();
    self.windows[freqIndex].buttons = list_init();
    list_append(self.windows[freqIndex].dials, (unitype) (void *) dialInit("Y Scale", &self.topFreq, WINDOW_FREQ, DIAL_EXP, -25, -25 - self.windows[freqIndex].windowTop, 8, 1, 500, 1), 'p');
    list_t *freqChannels = list_init();
    list_append(freqChannels, (unitype) "Channel 1", 's');
    list_append(freqChannels, (unitype) "Channel 2", 's');
    list_append(freqChannels, (unitype) "Channel 3", 's');
    list_append(freqChannels, (unitype) "Channel 4", 's');
    dropdown_metadata_t metadata;
    metadata.inUse = 0;
    list_append(self.windows[freqIndex].dropdowns, (unitype) (void *) dropdownInit(NULL, freqChannels, &self.freqOscChannel, pow2(freqIndex), -20, -65 - self.windows[freqIndex].windowTop, 8, metadata), 'p');
    list_append(self.windows[freqIndex].dropdowns, (unitype) (void *) dropdownInit(NULL, self.oscTitles, &self.freqOscIndex, pow2(freqIndex), -20, -45 - self.windows[freqIndex].windowTop, 8, metadata), 'p');
    self.windows[freqIndex].dropdownLogicIndex = -1;
    /* orbit */
    self.newOrbit = 0;
    createNewOrbit();
    /* editor */
    int editorIndex = ilog2(WINDOW_EDITOR);
    strcpy(self.windows[editorIndex].title, "Editor");
    self.windows[editorIndex].windowCoords[0] = 175;
    self.windows[editorIndex].windowCoords[1] = -161;
    self.windows[editorIndex].windowCoords[2] = 317;
    self.windows[editorIndex].windowCoords[3] = 167;
    self.windows[editorIndex].windowTop = 15;
    self.windows[editorIndex].windowSide = 0;
    self.windows[editorIndex].windowMinX = 100 + self.windows[editorIndex].windowSide;
    self.windows[editorIndex].windowMinY = 120 + self.windows[editorIndex].windowTop;
    self.windows[editorIndex].minimize = 0;
    self.windows[editorIndex].move = 0;
    self.windows[editorIndex].click = 0;
    self.windows[editorIndex].resize = 0;
    self.windows[editorIndex].dials = list_init();
    self.windows[editorIndex].switches = list_init();
    self.windows[editorIndex].dropdowns = list_init();
    self.windows[editorIndex].buttons = list_init();
    /* info */
    self.infoRefresh = 0;
    self.infoAnimation = 0;
    int infoIndex = ilog2(WINDOW_INFO);
    strcpy(self.windows[infoIndex].title, "Info");
    self.windows[infoIndex].windowCoords[0] = -52;
    self.windows[infoIndex].windowCoords[1] = -161;
    self.windows[infoIndex].windowCoords[2] = 317;
    self.windows[infoIndex].windowCoords[3] = -3;
    self.windows[infoIndex].windowTop = 15;
    self.windows[infoIndex].windowSide = 0;
    self.windows[infoIndex].windowMinX = 100 + self.windows[infoIndex].windowSide;
    self.windows[infoIndex].windowMinY = 120 + self.windows[infoIndex].windowTop;
    self.windows[infoIndex].minimize = 0;
    self.windows[infoIndex].move = 0;
    self.windows[infoIndex].click = 0;
    self.windows[infoIndex].resize = 0;
    self.windows[infoIndex].dials = list_init();
    self.windows[infoIndex].switches = list_init();
    self.windows[infoIndex].dropdowns = list_init();
    self.windows[infoIndex].buttons = list_init();
    list_append(self.windows[infoIndex].buttons, (unitype) (void *) buttonInit("Refresh", &self.infoRefresh, WINDOW_INFO, -22, -24, 8, BUTTON_SHAPE_RECTANGLE), 'p');
}

/* UI elements */
void dialTick(int window) {
    int windowID = pow2(window);
    for (int i = 0; i < self.windows[window].dials -> length; i++) {
        dial_t *dialp = (dial_t *) (self.windows[window].dials -> data[i].p);
        if ((dialp -> window & windowID) != 0) {
            textGLWriteUnicode(dialp -> label, self.windows[window].windowCoords[2] + dialp -> position[0], self.windows[window].windowCoords[1] + (self.windows[window].windowCoords[3] - self.windows[window].windowCoords[1]) + dialp -> position[1] + 15, dialp -> size - 1, 50);
            turtlePenSize(dialp -> size * 2);
            double dialX = self.windows[window].windowCoords[2] + dialp -> position[0];
            double dialY = self.windows[window].windowCoords[1] + (self.windows[window].windowCoords[3] - self.windows[window].windowCoords[1]) + dialp -> position[1];
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
            if (self.rightMouseDown) {
                if (dialp -> status[0] < 0) {
                    *(dialp -> variable) = dialp -> defaultValue;
                }
            }
            if (dialp -> status[0] > 0) {
                dialAngle = angleBetween(self.dialAnchorX, self.dialAnchorY, self.mx, self.my);
                if (self.my < self.dialAnchorY) {
                    dialp -> status[1] = self.mx - dialX;
                }
                if ((dialAngle < 0.000000001 || dialAngle > 180) && self.my > self.dialAnchorY && dialp -> status[1] >= 0) {
                    dialAngle = 0.000000001;
                }
                if ((dialAngle > 359.99999999 || dialAngle < 180) && self.my > self.dialAnchorY && dialp -> status[1] < 0) {
                    dialAngle = 359.99999999;
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
            double rounded = round(*(dialp -> variable) / dialp -> renderNumberFactor);
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
            double switchX = self.windows[window].windowCoords[2] + switchp -> position[0];
            double switchY = self.windows[window].windowCoords[1] + (self.windows[window].windowCoords[3] - self.windows[window].windowCoords[1]) + switchp -> position[1];
            textGLWriteUnicode(switchp -> label, switchX, switchY + 15, switchp -> size - 1, 50);
            turtlePenColor(self.themeColors[self.theme + 12], self.themeColors[self.theme + 13], self.themeColors[self.theme + 14]);
            turtlePenSize(switchp -> size * 1.2);
            turtleGoto(switchX - switchp -> size * 0.8, switchY);
            turtlePenDown();
            turtleGoto(switchX + switchp -> size * 0.8, switchY);
            turtlePenUp();
            turtlePenSize(switchp -> size);
            turtlePenColor(self.themeColors[self.theme + 9], self.themeColors[self.theme + 10], self.themeColors[self.theme + 11]);
            if (*(switchp -> variable)) {
                turtleGoto(switchX + switchp -> size * 0.8, switchY);
            } else {
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

void dropdownTick(int window) {
    if (self.windows[window].dropdowns -> length > 0) {
        self.windows[window].windowSide = 0;
    }
    int windowID = pow2(window);
    int logicIndex = -1;
    for (int i = 0; i < self.windows[window].dropdowns -> length; i++) {
        dropdown_t *dropdown = (dropdown_t *) (self.windows[window].dropdowns -> data[i].p);
        if ((dropdown -> window & windowID) != 0) {
            /* render dropdown default position */
            double dropdownX = self.windows[window].windowCoords[2] + dropdown -> position[0];
            double dropdownY = self.windows[window].windowCoords[1] + (self.windows[window].windowCoords[3] - self.windows[window].windowCoords[1]) + dropdown -> position[1];
            if (strlen(dropdown -> label) > 0) {
                textGLWriteUnicode(dropdown -> label, dropdownX - 5, dropdownY + 15, dropdown -> size - 1, 50);
            }
            double xfactor = textGLGetUnicodeLength(dropdown -> options -> data[dropdown -> index].s, dropdown -> size - 1);
            if (self.windows[window].windowSide < (xfactor - dropdown -> position[0] + 10)) {
                self.windows[window].windowSide = xfactor - dropdown -> position[0] + 10;
            }
            double itemHeight = (dropdown -> size * 1.5);
            /* special: oscillator select channel */
            if (dropdown -> metadata.inUse) {
                int oscIndex = window - ilog2(WINDOW_OSC);
                if (self.osc[oscIndex].selectedChannel == dropdown -> metadata.selectIndex) {
                    turtleRectangle(dropdownX - dropdown -> size - xfactor - 1, dropdownY - dropdown -> size * 0.7 - 1, dropdownX + dropdown -> size + 10 + 1, dropdownY + dropdown -> size * 0.7 + 1, dropdown -> metadata.color[0], dropdown -> metadata.color[1], dropdown -> metadata.color[2], 0);
                }
            }
            logicIndex = self.windows[window].dropdownLogicIndex;
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
                        logicIndex = -1;
                    }
                } else {
                    if (dropdown -> status == -1) {
                        dropdown -> status = 0;
                        logicIndex = -1;
                    }
                }
                if (dropdown -> status == -1) {
                    if (i > logicIndex && self.mouseDown) {
                        dropdown -> status = 1;
                        /* special: set selectedChannel if this is an oscilloscope */
                        if (dropdown -> metadata.inUse) {
                            int oscIndex = window - ilog2(WINDOW_OSC);
                            self.osc[oscIndex].selectedChannel = dropdown -> metadata.selectIndex;
                        }
                    }
                }
                if (dropdown -> status == 1) {
                    if (!self.mouseDown) {
                        dropdown -> status = 2;
                        logicIndex = -1;
                    }
                }
                if (dropdown -> status == -2) {
                    if (!self.mouseDown) {
                        dropdown -> status = 0;
                        logicIndex = -1;
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
                            *dropdown -> variable = dropdown -> index;
                        }
                        dropdown -> status = -2;
                        /* special: set usedVariableIndices if this is an oscilloscope or orbit plot */
                        if (dropdown -> metadata.inUse || dropdown -> window >= WINDOW_ORBIT) {
                            populateUsedSockets();
                        }
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
                        textGLWriteUnicode(dropdown -> options -> data[i].s, dropdownX, self.windows[window].windowCoords[1] + (self.windows[window].windowCoords[3] - self.windows[window].windowCoords[1]) + dropdown -> position[1] - renderIndex * itemHeight, dropdown -> size - 1, 100);
                        renderIndex++;
                    }
                }
            }
            if (dropdown -> status >= 1) {
                logicIndex = i;
            }
            self.windows[window].dropdownLogicIndex = logicIndex;
            if (dropdown -> metadata.inUse) {
                turtlePenColor(dropdown -> metadata.color[0], dropdown -> metadata.color[1], dropdown -> metadata.color[2]);
            } else {
                turtlePenColor(self.themeColors[self.theme + 9], self.themeColors[self.theme + 10], self.themeColors[self.theme + 11]);
            }
            textGLWriteUnicode(dropdown -> options -> data[dropdown -> index].s, dropdownX, self.windows[window].windowCoords[1] + (self.windows[window].windowCoords[3] - self.windows[window].windowCoords[1]) + dropdown -> position[1], dropdown -> size - 1, 100);
            if (dropdown -> status >= 1) {
                turtleTriangle(dropdownX + 11, dropdownY + 4, dropdownX + 11, dropdownY - 4, dropdownX + 5, dropdownY, self.themeColors[self.theme + 9], self.themeColors[self.theme + 10], self.themeColors[self.theme + 11], 0);
            } else {
                turtleTriangle(dropdownX + 13, dropdownY + 3, dropdownX + 5, dropdownY + 3, dropdownX + 9, dropdownY - 3, self.themeColors[self.theme + 9], self.themeColors[self.theme + 10], self.themeColors[self.theme + 11], 0);
            }
        }
    }
}

void buttonTick(int window) {
    int windowID = pow2(window);
    for (int i = 0; i < self.windows[window].buttons -> length; i++) {
        button_t *button = (button_t *) (self.windows[window].buttons -> data[i].p);
        if ((button -> window & windowID) != 0) {
            double buttonX = self.windows[window].windowCoords[2] + button -> position[0];
            double buttonY = self.windows[window].windowCoords[1] + (self.windows[window].windowCoords[3] - self.windows[window].windowCoords[1]) + button -> position[1];
            double buttonWidth = textGLGetUnicodeLength(button -> label, button -> size);
            double buttonHeight = 14;
            if (button -> status == 0) {
                turtleRectangle(buttonX - buttonWidth / 2, buttonY - buttonHeight / 2, buttonX + buttonWidth / 2, buttonY + buttonHeight / 2, self.themeColors[self.theme + 0], self.themeColors[self.theme + 1], self.themeColors[self.theme + 2], 0);
            } else {
                turtleRectangle(buttonX - buttonWidth / 2, buttonY - buttonHeight / 2, buttonX + buttonWidth / 2, buttonY + buttonHeight / 2, self.themeColors[self.theme + 3], self.themeColors[self.theme + 4], self.themeColors[self.theme + 5], 0);
            }
            turtlePenColor(self.themeColors[self.theme + 9], self.themeColors[self.theme + 10], self.themeColors[self.theme + 11]);
            textGLWriteUnicode(button -> label, buttonX, buttonY, button -> size - 1, 50);
            if (self.mouseDown) {
                if (button -> status < 0) {
                    button -> status *= -1;
                }
            } else {
                if (self.mx > buttonX - buttonWidth / 2 && self.mx < buttonX + buttonWidth / 2 && self.my > buttonY - buttonHeight / 2 && self.my < buttonY + buttonHeight / 2) {
                    button -> status = -1;
                } else {
                    button -> status = 0;
                }
            }
            *(button -> variable) = 0;
            if (button -> status > 0) {
                *(button -> variable) = 1;
                button -> status = 0;
            }
        }
    }
}

void renderWindow(int window, char top) {
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
        textGLWriteUnicode(win -> title, (win -> windowCoords[0] + win -> windowCoords[2] - win -> windowSide) / 2, win -> windowCoords[3] - win -> windowTop * 0.45, win -> windowTop * 0.5, 50);
        /* draw [X] */
        char hovering = top && self.mx >= win -> windowCoords[2] - 10 && self.mx <= win -> windowCoords[2] - 2 && self.my >= win -> windowCoords[3] - 10 && self.my <= win -> windowCoords[3] - 2;
        if (self.mouseDown) {
            if (win -> close == 1) {
                win -> close = 2;
            }
        } else {
            if (win -> close == 2 && hovering) {
                win -> minimize = 1;
                list_remove(self.windowRender, (unitype) pow2(window), 'i');
                list_insert(self.windowRender, 0, (unitype) pow2(window), 'i');
                win -> close = 0;
                win -> resize = 0;
                win -> move = 0;
            } else {
                if (hovering) {
                    win -> close = 1;
                } else {
                    win -> close = 0;
                }
            }
        }
        if (win -> close == 2) {
            win -> resize = 0;
            win -> move = 0;
        }
        if (win -> close >= 1) {
            turtleRectangle(win -> windowCoords[2] - 10, win -> windowCoords[3] - 10, win -> windowCoords[2] - 2, win -> windowCoords[3] - 2, self.themeColors[self.theme + 42], self.themeColors[self.theme + 43], self.themeColors[self.theme + 44], 0);
        } else {
            turtleRectangle(win -> windowCoords[2] - 10, win -> windowCoords[3] - 10, win -> windowCoords[2] - 2, win -> windowCoords[3] - 2, self.themeColors[self.theme + 39], self.themeColors[self.theme + 40], self.themeColors[self.theme + 41], 0);
        }
        turtlePenColor(self.themeColors[self.theme + 9], self.themeColors[self.theme + 10], self.themeColors[self.theme + 11]);
        turtlePenSize(1);
        turtleGoto(win -> windowCoords[2] - 8, win -> windowCoords[3] - 8);
        turtlePenDown();
        turtleGoto(win -> windowCoords[2] - 4, win -> windowCoords[3] - 4);
        turtlePenUp();
        turtleGoto(win -> windowCoords[2] - 8, win -> windowCoords[3] - 4);
        turtlePenDown();
        turtleGoto(win -> windowCoords[2] - 4, win -> windowCoords[3] - 8);
        turtlePenUp();
        /* draw sidebar UI elements */
        dialTick(window);
        switchTick(window);
        dropdownTick(window);
        buttonTick(window);
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
                        list_remove(self.windowRender, (unitype) pow2(window), 'i');
                        list_insert(self.windowRender, 0, (unitype) pow2(window), 'i');
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

void setBoundsNoTrigger(int oscIndex, int stopped) {
    if (!stopped) {
        for (int i = 0; i < 4; i++) {
            self.osc[oscIndex].rightBound[i] = self.data -> data[self.osc[oscIndex].dataIndex[i]].r -> length;
        }
    }
    for (int i = 0; i < 4; i++) {
        if (self.osc[oscIndex].rightBound[i] - self.osc[oscIndex].leftBound[i] < self.osc[oscIndex].windowSizeSamples[i]) {
            self.osc[oscIndex].leftBound[i] = self.osc[oscIndex].rightBound[i] - self.osc[oscIndex].windowSizeSamples[i];
            if (self.osc[oscIndex].leftBound[i] < 0) {
                self.osc[oscIndex].leftBound[i] = 1;
            }
        }
        if (self.osc[oscIndex].rightBound[i] > self.osc[oscIndex].leftBound[i] + self.osc[oscIndex].windowSizeSamples[i]) {
            self.osc[oscIndex].leftBound[i] = self.osc[oscIndex].rightBound[i] - self.osc[oscIndex].windowSizeSamples[i];
        }
    }
}

void renderOscData(int oscIndex) {
    int windowIndex = ilog2(WINDOW_OSC) + oscIndex;
    for (int i = 0; i < 4; i++) {
        self.osc[oscIndex].windowSizeSamples[i] = round((self.osc[oscIndex].windowSizeMicroseconds / 1000000) * self.data -> data[self.osc[oscIndex].dataIndex[i]].r -> data[0].d);
    }
    if (self.osc[oscIndex].oldSelectedChannel != self.osc[oscIndex].selectedChannel) {
        self.osc[oscIndex].dummyOffset = (self.osc[oscIndex].topBound[self.osc[oscIndex].selectedChannel] + self.osc[oscIndex].bottomBound[self.osc[oscIndex].selectedChannel]) / -2;
        if (fabs(self.osc[oscIndex].dummyOffset) < 0.01) {
            self.osc[oscIndex].dummyOffset = 0;
        }
        self.osc[oscIndex].dummyTopBound = self.osc[oscIndex].topBound[self.osc[oscIndex].selectedChannel] + self.osc[oscIndex].dummyOffset;
        self.osc[oscIndex].oldSelectedChannel = self.osc[oscIndex].selectedChannel;
    }
    self.osc[oscIndex].topBound[self.osc[oscIndex].selectedChannel] = self.osc[oscIndex].dummyTopBound - self.osc[oscIndex].dummyOffset;
    self.osc[oscIndex].bottomBound[self.osc[oscIndex].selectedChannel] = self.osc[oscIndex].dummyTopBound * -1 - self.osc[oscIndex].dummyOffset;
    /* set left and right bounds */
    if (!self.osc[oscIndex].stop) {
        if (self.osc[oscIndex].trigger.type == TRIGGER_NONE) {
            list_clear(self.osc[oscIndex].trigger.lastIndex);
            setBoundsNoTrigger(oscIndex, 0);
        } else {
            /* calculate difference in time from trigger point*/
            double timeDifference = (self.data -> data[self.osc[oscIndex].dataIndex[self.osc[oscIndex].selectedChannel]].r -> length - self.osc[oscIndex].trigger.index) / self.data -> data[self.osc[oscIndex].dataIndex[self.osc[oscIndex].selectedChannel]].r -> data[0].d;
            for (int i = 0; i < 4; i++) {
                self.osc[oscIndex].rightBound[i] = self.data -> data[self.osc[oscIndex].dataIndex[i]].r -> length - timeDifference * self.data -> data[self.osc[oscIndex].dataIndex[i]].r -> data[0].d;
                if (self.osc[oscIndex].rightBound[i] > self.data -> data[self.osc[oscIndex].dataIndex[i]].r -> length) {
                    self.osc[oscIndex].rightBound[i] = self.data -> data[self.osc[oscIndex].dataIndex[i]].r -> length;
                }
                self.osc[oscIndex].leftBound[i] = self.osc[oscIndex].rightBound[i] - self.osc[oscIndex].windowSizeSamples[i];
                if (self.osc[oscIndex].leftBound[i] < 0) {
                    self.osc[oscIndex].leftBound[i] = 1;
                }
            }

            /* identify triggerIndex (trigger index is right side of window) */
            int dataLength = self.data -> data[self.osc[oscIndex].dataIndex[self.osc[oscIndex].selectedChannel]].r -> length;
            if (self.osc[oscIndex].trigger.lastIndex -> length > 0 && self.osc[oscIndex].trigger.lastIndex -> data[0].i < dataLength) {
                self.osc[oscIndex].trigger.index = self.osc[oscIndex].trigger.lastIndex -> data[0].i;
                self.osc[oscIndex].trigger.timeout = 0;
                list_delete(self.osc[oscIndex].trigger.lastIndex, 0);
            }
            self.osc[oscIndex].trigger.timeout++;
            if (self.osc[oscIndex].trigger.timeout > TRIGGER_TIMEOUT) {
                self.osc[oscIndex].trigger.index = 0;
            }
            if (self.osc[oscIndex].trigger.index == 0) {
                setBoundsNoTrigger(oscIndex, 0);
            }
            int oldAbove = self.osc[oscIndex].above;
            if (self.data -> data[self.osc[oscIndex].dataIndex[self.osc[oscIndex].selectedChannel]].r -> data[dataLength - 1].d >= self.osc[oscIndex].trigger.threshold) {
                self.osc[oscIndex].above = 1;
            } else {
                self.osc[oscIndex].above = 0;
            }
            if (self.osc[oscIndex].trigger.type == TRIGGER_RISING_EDGE) {
                if (oldAbove == 0 && self.osc[oscIndex].above == 1) {
                    list_append(self.osc[oscIndex].trigger.lastIndex, (unitype) (dataLength - 2), 'i');
                }
            }
            if (self.osc[oscIndex].trigger.type == TRIGGER_FALLING_EDGE) {
                if (oldAbove == 1 && self.osc[oscIndex].above == 0) {
                    list_append(self.osc[oscIndex].trigger.lastIndex, (unitype) (dataLength - 2), 'i');
                }
            }
        }
    } else {
        setBoundsNoTrigger(oscIndex, 1);
    }
    if (self.windows[windowIndex].minimize == 0) {
        /* render window background */
        turtleRectangle(self.windows[windowIndex].windowCoords[0], self.windows[windowIndex].windowCoords[1], self.windows[windowIndex].windowCoords[2], self.windows[windowIndex].windowCoords[3], self.themeColors[self.theme + 12], self.themeColors[self.theme + 13], self.themeColors[self.theme + 14], 0);
        turtlePenSize(1);
        double dashedY = self.windows[windowIndex].windowCoords[1] + (self.osc[oscIndex].trigger.threshold - self.osc[oscIndex].bottomBound[self.osc[oscIndex].selectedChannel]) / (self.osc[oscIndex].topBound[self.osc[oscIndex].selectedChannel] - self.osc[oscIndex].bottomBound[self.osc[oscIndex].selectedChannel]) * (self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - self.windows[windowIndex].windowCoords[1]);
        if (dashedY < self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop && dashedY > self.windows[windowIndex].windowCoords[1]) {
            turtlePenShape("none");
            turtlePenColorAlpha(0, 0, 0, 200);
            double dashedX = self.windows[windowIndex].windowCoords[0] + (self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowCoords[0]) / 40;
            for (int i = 0; i < 20; i++) {
                turtleGoto(dashedX, dashedY);
                turtlePenDown();
                dashedX += (self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowCoords[0]) / 40;
                turtleGoto(dashedX, dashedY);
                turtlePenUp();
                dashedX += (self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowCoords[0]) / 40;
            }
        }
        turtlePenColor(0, 0, 0);
        turtlePenShape("circle");
        turtleGoto(self.windows[windowIndex].windowCoords[0], (self.windows[windowIndex].windowCoords[1] - self.windows[windowIndex].windowTop + self.windows[windowIndex].windowCoords[3]) / 2);
        turtlePenDown();
        turtleGoto(self.windows[windowIndex].windowCoords[2], (self.windows[windowIndex].windowCoords[1] - self.windows[windowIndex].windowTop + self.windows[windowIndex].windowCoords[3]) / 2);
        turtlePenUp();
        /* render data */
        turtlePenSize(1);
        double xquantum[4];
        for (int j = 0; j < 4; j++) {
            xquantum[j] = (self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowCoords[0]) / (self.osc[oscIndex].rightBound[j] - self.osc[oscIndex].leftBound[j] - 1);
            if (self.osc[oscIndex].dataIndex[j] <= 0) {
                continue;
            }
            turtlePenColor(self.themeColors[self.theme + 24 + j * 3], self.themeColors[self.theme + 25 + j * 3], self.themeColors[self.theme + 26 + j * 3]);
            for (int i = 0; i < self.osc[oscIndex].rightBound[j] - self.osc[oscIndex].leftBound[j]; i++) {
                turtleGoto(self.windows[windowIndex].windowCoords[0] + i * xquantum[j], self.windows[windowIndex].windowCoords[1] + ((self.data -> data[self.osc[oscIndex].dataIndex[j]].r -> data[self.osc[oscIndex].leftBound[j] + i].d - self.osc[oscIndex].bottomBound[j]) / (self.osc[oscIndex].topBound[j] - self.osc[oscIndex].bottomBound[j])) * (self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - self.windows[windowIndex].windowCoords[1]));
                turtlePenDown();
            }
            turtlePenUp();
        }
        /* render mouse */
        if (self.mx > self.windows[windowIndex].windowCoords[0] + 15 && self.my > self.windows[windowIndex].windowCoords[1] && self.mx < self.windows[windowIndex].windowCoords[2] && self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop) { // unintentional forgot "self.my <" but i prefer it this way
            int sample = round((self.mx - self.windows[windowIndex].windowCoords[0]) / xquantum[self.osc[oscIndex].selectedChannel]);
            if (self.osc[oscIndex].leftBound[self.osc[oscIndex].selectedChannel] + sample >= self.data -> data[self.osc[oscIndex].dataIndex[self.osc[oscIndex].selectedChannel]].r -> length) {
                goto OSC_SIDE_AXIS; // skip this section
            }
            double sampleX = self.windows[windowIndex].windowCoords[0] + sample * xquantum[self.osc[oscIndex].selectedChannel];
            double sampleY = self.windows[windowIndex].windowCoords[1] + ((self.data -> data[self.osc[oscIndex].dataIndex[self.osc[oscIndex].selectedChannel]].r -> data[self.osc[oscIndex].leftBound[self.osc[oscIndex].selectedChannel] + sample].d - self.osc[oscIndex].bottomBound[self.osc[oscIndex].selectedChannel]) / (self.osc[oscIndex].topBound[self.osc[oscIndex].selectedChannel] - self.osc[oscIndex].bottomBound[self.osc[oscIndex].selectedChannel])) * (self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - self.windows[windowIndex].windowCoords[1]);
            turtleRectangle(sampleX - 1, self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop, sampleX + 1, self.windows[windowIndex].windowCoords[1], self.themeColors[self.theme + 21], self.themeColors[self.theme + 22], self.themeColors[self.theme + 23], 100);
            turtleRectangle(self.windows[windowIndex].windowCoords[0], sampleY - 1, self.windows[windowIndex].windowCoords[2], sampleY + 1, self.themeColors[self.theme + 21], self.themeColors[self.theme + 22], self.themeColors[self.theme + 23], 100);
            turtlePenColor(215, 215, 215);
            turtlePenSize(4);
            turtleGoto(sampleX, sampleY);
            turtlePenDown();
            turtlePenUp();
            char sampleValue[24];
            /* render side box */
            sprintf(sampleValue, "%.02lf", self.data -> data[self.osc[oscIndex].dataIndex[self.osc[oscIndex].selectedChannel]].r -> data[self.osc[oscIndex].leftBound[self.osc[oscIndex].selectedChannel] + sample].d);
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
            turtleRectangle(boxX2 - 2, self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - 16, boxX2 + boxLength2 + 2, self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - 5, 215, 215, 215, 0);
            turtlePenColor(0, 0, 0);
            textGLWriteString(sampleValue, boxX2, self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - 11, 8, 0);
        }
        /* render side axis */
        OSC_SIDE_AXIS:
        turtleRectangle(self.windows[windowIndex].windowCoords[0], self.windows[windowIndex].windowCoords[1], self.windows[windowIndex].windowCoords[0] + 10, self.windows[windowIndex].windowCoords[3], self.themeColors[self.theme + 21], self.themeColors[self.theme + 22], self.themeColors[self.theme + 23], 100);
        turtlePenColor(0, 0, 0);
        turtlePenSize(1);
        double ycenter = (self.windows[windowIndex].windowCoords[1] + self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop) / 2;
        turtleGoto(self.windows[windowIndex].windowCoords[0], ycenter);
        turtlePenDown();
        turtleGoto(self.windows[windowIndex].windowCoords[0] + 5, ycenter);
        turtlePenUp();
        int tickMarks = round((self.osc[oscIndex].topBound[self.osc[oscIndex].selectedChannel] - self.osc[oscIndex].bottomBound[self.osc[oscIndex].selectedChannel]) / 8) * 4;
        double culling = (self.osc[oscIndex].topBound[self.osc[oscIndex].selectedChannel] - self.osc[oscIndex].bottomBound[self.osc[oscIndex].selectedChannel]) / 2;
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
                sprintf(tickValue, "%d", (int) ((self.osc[oscIndex].topBound[self.osc[oscIndex].selectedChannel] - self.osc[oscIndex].bottomBound[self.osc[oscIndex].selectedChannel]) / tickMarks * mouseSample - self.osc[oscIndex].topBound[self.osc[oscIndex].selectedChannel] - self.osc[oscIndex].dummyOffset * 2));
                turtlePenColor(215, 215, 215);
                textGLWriteString(tickValue, self.windows[windowIndex].windowCoords[0] + tickLength + 13, ypos, 8, 0);
            }
        }
    }
}

void renderFreqData() {
    int windowIndex = ilog2(WINDOW_FREQ);
    int sideAxisWidth = 10;
    int bottomAxisHeight = 10;
    /* linear windowing function over 10% of the sample */
    int dataLength = self.osc[self.freqOscIndex].rightBound[self.freqOscChannel] - self.osc[self.freqOscIndex].leftBound[self.freqOscChannel];
    int threshold = (dataLength) * 0.1;
    double damping = 1.0 / threshold;
    list_clear(self.windowData);
    if (self.data -> data[self.osc[self.freqOscIndex].dataIndex[self.freqOscChannel]].r -> length < self.osc[self.freqOscIndex].rightBound[self.freqOscChannel]) {
        turtleRectangle(self.windows[windowIndex].windowCoords[0], self.windows[windowIndex].windowCoords[1], self.windows[windowIndex].windowCoords[2], self.windows[windowIndex].windowCoords[3], self.themeColors[self.theme + 12], self.themeColors[self.theme + 13], self.themeColors[self.theme + 14], 0);
        return;
    }
    for (int i = 0; i < dataLength; i++) {
        double dataPoint = self.data -> data[self.osc[self.freqOscIndex].dataIndex[self.freqOscChannel]].r -> data[i + self.osc[self.freqOscIndex].leftBound[self.freqOscChannel]].d;
        if (i < threshold) {
            dataPoint *= damping * (i + 1);
        }
        if (i >= (dataLength) - threshold) {
            dataPoint *= damping * ((dataLength) - (i - 1));
        }
        list_append(self.windowData, (unitype) dataPoint, 'd');
    }
    list_clear(self.freqData);
    list_clear(self.phaseData);
    fft_list_wrapper(self.windowData, self.freqData, self.phaseData);
    double xquantum = (self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowCoords[0] - self.windows[windowIndex].windowSide - sideAxisWidth) / ((self.windowData -> length - 2) / self.freqZoom) * 2;
    if (self.windows[windowIndex].minimize == 0) {
        /* render window background */
        turtleRectangle(self.windows[windowIndex].windowCoords[0], self.windows[windowIndex].windowCoords[1], self.windows[windowIndex].windowCoords[2], self.windows[windowIndex].windowCoords[3], self.themeColors[self.theme + 12], self.themeColors[self.theme + 13], self.themeColors[self.theme + 14], 0);
        turtlePenSize(1);
        /* render frequency data */
        turtlePenColor(self.themeColors[self.theme + 24 + self.freqOscChannel * 3], self.themeColors[self.theme + 25 + self.freqOscChannel * 3], self.themeColors[self.theme + 26 + self.freqOscChannel * 3]);
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
            turtleGoto(sideAxisWidth + self.windows[windowIndex].windowCoords[0] + (i - self.freqLeftBound) * xquantum, self.windows[windowIndex].windowCoords[1] + bottomAxisHeight + ((magnitude - 0) / (self.topFreq - 0)) * (self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - self.windows[windowIndex].windowCoords[1]));
            turtlePenDown();
        }
        turtlePenUp();
        /* render phase data */
        // turtlePenColor(self.themeColors[self.theme + 36], self.themeColors[self.theme + 37], self.themeColors[self.theme + 38]);
        // if (self.phaseData -> length % 2) {
        //     xquantum *= (self.phaseData -> length - 2.0) / (self.phaseData -> length - 1.0);
        // }
        // self.freqRightBound = 1 + self.freqLeftBound + (self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowSide - self.windows[windowIndex].windowCoords[0]) / xquantum;
        // if (self.freqRightBound > self.phaseData -> length / 2 + self.phaseData -> length % 2) {
        //     self.freqRightBound = self.phaseData -> length / 2 + self.phaseData -> length % 2;
        // }
        // for (int i = self.freqLeftBound; i < self.freqRightBound; i++) {
        //     double magnitude = self.phaseData -> data[i].d;
        //     turtleGoto(self.windows[windowIndex].windowCoords[0] + (i - self.freqLeftBound) * xquantum, (self.windows[windowIndex].windowCoords[1] + self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop) / 2 + ((magnitude - 0) / (self.topFreq - 0)) * (self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - self.windows[windowIndex].windowCoords[1]));
        //     turtlePenDown();
        // }
        // turtlePenUp();

        /* render mouse */
        if (self.mx > self.windows[windowIndex].windowCoords[0] + sideAxisWidth && self.my > self.windows[windowIndex].windowCoords[1] + bottomAxisHeight && self.mx < self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowSide && self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop) {
            double sample = (self.mx - self.windows[windowIndex].windowCoords[0] - sideAxisWidth) / xquantum + self.freqLeftBound;
            if (self.osc[self.freqOscIndex].leftBound[self.freqOscChannel] + sample >= self.data -> data[self.osc[self.freqOscIndex].dataIndex[self.freqOscChannel]].r -> length) {
                goto FREQ_SIDE_AXIS;
            }
            int roundedSample = round(sample);
            double sampleX = sideAxisWidth + self.windows[windowIndex].windowCoords[0] + (roundedSample - self.freqLeftBound) * xquantum;
            double sampleY = bottomAxisHeight + self.windows[windowIndex].windowCoords[1] + (fabs(self.freqData -> data[roundedSample].d) / (self.topFreq)) * (self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - self.windows[windowIndex].windowCoords[1]);
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
            double samplesPerSecond = self.data -> data[self.osc[self.freqOscIndex].dataIndex[self.freqOscChannel]].r -> data[0].d;
            sprintf(sampleValue, "%.1lfHz", sample / (dataLength / samplesPerSecond));
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
            double buckets = (self.mx - self.windows[windowIndex].windowCoords[0] - sideAxisWidth) / xquantum;
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
        FREQ_SIDE_AXIS:
        /* render side axis */
        turtleRectangle(self.windows[windowIndex].windowCoords[0], self.windows[windowIndex].windowCoords[1], self.windows[windowIndex].windowCoords[0] + 10, self.windows[windowIndex].windowCoords[3], self.themeColors[self.theme + 21], self.themeColors[self.theme + 22], self.themeColors[self.theme + 23], 100);
        turtlePenColor(0, 0, 0);
        turtlePenSize(1);
        double ycenter = (self.windows[windowIndex].windowCoords[1] + self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop) / 2;
        turtleGoto(self.windows[windowIndex].windowCoords[0], ycenter);
        turtlePenDown();
        turtleGoto(self.windows[windowIndex].windowCoords[0] + 5, ycenter);
        turtlePenUp();
        int tickMarks = round(self.topFreq / 4) * 4;
        double culling = self.topFreq;
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
                sprintf(tickValue, "%d", (int) (self.topFreq / tickMarks * mouseSample - bottomAxisHeight / ((self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - self.windows[windowIndex].windowCoords[1]) / self.topFreq)));
                turtlePenColor(215, 215, 215);
                textGLWriteString(tickValue, self.windows[windowIndex].windowCoords[0] + tickLength + 13, ypos, 8, 0);
            }
        }
        /* render bottom axis */
        turtleRectangle(self.windows[windowIndex].windowCoords[0] + 10, self.windows[windowIndex].windowCoords[1], self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowSide, self.windows[windowIndex].windowCoords[1] + 10, self.themeColors[self.theme + 21], self.themeColors[self.theme + 22], self.themeColors[self.theme + 23], 100);
        turtlePenColor(0, 0, 0);
        turtlePenSize(1);
        double xcenter = (self.windows[windowIndex].windowCoords[0] + self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowSide + sideAxisWidth) / 2;
        turtleGoto(xcenter, self.windows[windowIndex].windowCoords[1]);
        turtlePenDown();
        turtleGoto(xcenter, self.windows[windowIndex].windowCoords[1] + 5);
        turtlePenUp();
        tickMarks = round((self.freqRightBound - self.freqLeftBound) / 4) * 4;
        culling = self.freqRightBound - self.freqLeftBound;
        while (culling > 60) {
            culling /= 4;
            tickMarks /= 4;
        }
        tickMarks = ceil(tickMarks / 4) * 4;
        double xquantum = (self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowSide - self.windows[windowIndex].windowCoords[0] - sideAxisWidth) / tickMarks;
        for (int i = 1; i < tickMarks; i++) {
            double xpos = sideAxisWidth + self.windows[windowIndex].windowCoords[0] + i * xquantum;
            turtleGoto(xpos, self.windows[windowIndex].windowCoords[1]);
            turtlePenDown();
            int tickLength = 2;
            if (i % (tickMarks / 4) == 0) {
                tickLength = 4;
            }
            turtleGoto(xpos, self.windows[windowIndex].windowCoords[1] + tickLength);
            turtlePenUp();
        }
        mouseSample = round((self.mx - sideAxisWidth - self.windows[windowIndex].windowCoords[0]) / xquantum);
        if (mouseSample > 0 && mouseSample < tickMarks) {
            double xpos = self.windows[windowIndex].windowCoords[0] + sideAxisWidth + mouseSample * xquantum;
            int tickLength = 2;
            if (mouseSample % (tickMarks / 4) == 0) {
                tickLength = 4;
            }
            if (self.my > self.windows[windowIndex].windowCoords[1] && self.my < self.windows[windowIndex].windowCoords[1] + 15) {
                turtleTriangle(xpos, self.windows[windowIndex].windowCoords[1] + tickLength + 2, xpos + 6, self.windows[windowIndex].windowCoords[1] + tickLength + 10, xpos - 6, self.windows[windowIndex].windowCoords[1] + tickLength + 10, 215, 215, 215, 0);
                char tickValue[24];
                sprintf(tickValue, "%dHz", (int) ((self.freqRightBound - self.freqLeftBound) / tickMarks * mouseSample));
                turtlePenColor(215, 215, 215);
                textGLWriteString(tickValue, xpos, self.windows[windowIndex].windowCoords[1] + tickLength + 17, 8, 50);
            }
        }
    }
}

void renderEditorData() {
    int windowIndex = ilog2(WINDOW_EDITOR);
    if (self.windows[windowIndex].minimize == 0) {
        /* render window background */
        turtleRectangle(self.windows[windowIndex].windowCoords[0], self.windows[windowIndex].windowCoords[1], self.windows[windowIndex].windowCoords[2], self.windows[windowIndex].windowCoords[3], self.themeColors[self.theme + 12], self.themeColors[self.theme + 13], self.themeColors[self.theme + 14], 0);
    }
}

void renderOrbitData(int orbitIndex) {
    /*
    TODO
    time sync
    */
    int windowIndex = ilog2(WINDOW_ORBIT) + orbitIndex;
    if (self.windows[windowIndex].minimize == 0) {
        /* render window background */
        turtleRectangle(self.windows[windowIndex].windowCoords[0], self.windows[windowIndex].windowCoords[1], self.windows[windowIndex].windowCoords[2], self.windows[windowIndex].windowCoords[3], self.themeColors[self.theme + 12], self.themeColors[self.theme + 13], self.themeColors[self.theme + 14], 0);
        /* render data */
        turtlePenSize(1);
        turtlePenColor(self.themeColors[self.theme + 6], self.themeColors[self.theme + 7], self.themeColors[self.theme + 8]);
        if (!self.orbit[orbitIndex].stop) {
            self.orbit[orbitIndex].stopIndex[0] = self.data -> data[self.orbit[orbitIndex].dataIndex[0]].r -> length;
            self.orbit[orbitIndex].stopIndex[1] = self.data -> data[self.orbit[orbitIndex].dataIndex[1]].r -> length;
        }
        double orbitX = (self.windows[windowIndex].windowCoords[0] + self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowSide) / 2 + self.orbit[orbitIndex].offset[0] / self.orbit[orbitIndex].scale[0] * (self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowSide - self.windows[windowIndex].windowCoords[0]);
        double orbitY = (self.windows[windowIndex].windowCoords[1] + self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop) / 2 + self.orbit[orbitIndex].offset[1] / self.orbit[orbitIndex].scale[1] * (self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - self.windows[windowIndex].windowCoords[1]);
        for (int i = 1; i < self.orbit[orbitIndex].samples; i++) {
            if (self.orbit[orbitIndex].stopIndex[0] > i) {
                orbitX = (self.windows[windowIndex].windowCoords[0] + self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowSide) / 2 + ((self.data -> data[self.orbit[orbitIndex].dataIndex[0]].r -> data[self.orbit[orbitIndex].stopIndex[0] - i - 1].d + self.orbit[orbitIndex].offset[0]) / self.orbit[orbitIndex].scale[0]) * (self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowSide - self.windows[windowIndex].windowCoords[0]);
            }
            if (self.orbit[orbitIndex].stopIndex[1] > i) {
                orbitY = (self.windows[windowIndex].windowCoords[1] + self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop) / 2 + ((self.data -> data[self.orbit[orbitIndex].dataIndex[1]].r -> data[self.orbit[orbitIndex].stopIndex[1] - i - 1].d + self.orbit[orbitIndex].offset[1]) / self.orbit[orbitIndex].scale[1]) * (self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - self.windows[windowIndex].windowCoords[1]);
            }
            turtleGoto(orbitX, orbitY);
            turtlePenDown();
        }
        turtlePenUp();
        /* render mouse */
        if (self.mx > self.windows[windowIndex].windowCoords[0] + 15 && self.my > self.windows[windowIndex].windowCoords[1] + 15 && self.mx < self.windows[windowIndex].windowCoords[2] && self.my < self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop) {
            /* find closest point on orbit plot */
            int closestIndex = -1;
            double distClosest = 10000.0;
            for (int i = 0; i < self.orbit[orbitIndex].samples; i++) {
                if (self.orbit[orbitIndex].stopIndex[0] >= i && self.orbit[orbitIndex].stopIndex[1] >= i) {
                    double xDist = (self.windows[windowIndex].windowCoords[0] + self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowSide) / 2 + (self.data -> data[self.orbit[orbitIndex].dataIndex[0]].r -> data[self.orbit[orbitIndex].stopIndex[0] - i - 1].d + self.orbit[orbitIndex].offset[0]) / self.orbit[orbitIndex].scale[0] * (self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowSide - self.windows[windowIndex].windowCoords[0]) - self.mx;
                    double yDist = (self.windows[windowIndex].windowCoords[1] + self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop) / 2 + (self.data -> data[self.orbit[orbitIndex].dataIndex[1]].r -> data[self.orbit[orbitIndex].stopIndex[1] - i - 1].d + self.orbit[orbitIndex].offset[1]) / self.orbit[orbitIndex].scale[1] * (self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - self.windows[windowIndex].windowCoords[1]) - self.my;
                    double distSquared = xDist * xDist + yDist * yDist;
                    if (distSquared < distClosest) {
                        distClosest = distSquared;
                        closestIndex = i;
                    }
                } else {
                    break;
                }
            }
            if (closestIndex != -1 && distClosest < ORBIT_DIST_THRESH) {
                double orbitX = (self.windows[windowIndex].windowCoords[0] + self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowSide) / 2;
                double orbitY = (self.windows[windowIndex].windowCoords[1] + self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop) / 2;
                if (self.orbit[orbitIndex].stopIndex[0] >= closestIndex) {
                    orbitX = (self.windows[windowIndex].windowCoords[0] + self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowSide) / 2 + (self.data -> data[self.orbit[orbitIndex].dataIndex[0]].r -> data[self.orbit[orbitIndex].stopIndex[0] - closestIndex - 1].d + self.orbit[orbitIndex].offset[0]) / self.orbit[orbitIndex].scale[0] * (self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowSide - self.windows[windowIndex].windowCoords[0]);
                }
                if (self.orbit[orbitIndex].stopIndex[1] >= closestIndex) {
                    orbitY = (self.windows[windowIndex].windowCoords[1] + self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop) / 2 + (self.data -> data[self.orbit[orbitIndex].dataIndex[1]].r -> data[self.orbit[orbitIndex].stopIndex[1] - closestIndex - 1].d + self.orbit[orbitIndex].offset[1]) / self.orbit[orbitIndex].scale[1] * (self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - self.windows[windowIndex].windowCoords[1]);
                }
                turtleRectangle(orbitX - 1, self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop, orbitX + 1, self.windows[windowIndex].windowCoords[1], self.themeColors[self.theme + 21], self.themeColors[self.theme + 22], self.themeColors[self.theme + 23], 100);
                turtleRectangle(self.windows[windowIndex].windowCoords[0], orbitY - 1, self.windows[windowIndex].windowCoords[2], orbitY + 1, self.themeColors[self.theme + 21], self.themeColors[self.theme + 22], self.themeColors[self.theme + 23], 100);
                turtleGoto(orbitX, orbitY);
                turtlePenColor(215, 215, 215);
                turtlePenSize(4);
                turtlePenDown();
                turtlePenUp();
                char sampleValue[24];
                /* render side box */
                sprintf(sampleValue, "%.02lf", self.data -> data[self.orbit[orbitIndex].dataIndex[1]].r -> data[self.orbit[orbitIndex].stopIndex[1] - closestIndex - 1].d);
                double boxLength = textGLGetStringLength(sampleValue, 8);
                double boxX = self.windows[windowIndex].windowCoords[0] + 12;
                if (orbitX - boxX < 40) {
                    boxX = self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowSide - boxLength - 5;
                }
                double boxY = orbitY + 10;
                turtleRectangle(boxX, boxY - 5, boxX + 4 + boxLength, boxY + 5, 215, 215, 215, 0);
                turtlePenColor(0, 0, 0);
                textGLWriteString(sampleValue, boxX + 2, boxY - 1, 8, 0);
                /* render top box */
                sprintf(sampleValue, "%.02lf", self.data -> data[self.orbit[orbitIndex].dataIndex[0]].r -> data[self.orbit[orbitIndex].stopIndex[0] - closestIndex - 1].d);
                double boxLength2 = textGLGetStringLength(sampleValue, 8);
                double boxY2 = orbitY + 10;
                double boxX2 = orbitX - boxLength2 / 2;
                if (boxX2 - 15 < self.windows[windowIndex].windowCoords[0]) {
                    boxX2 = self.windows[windowIndex].windowCoords[0] + 15;
                }
                if (boxX2 + boxLength2 + self.windows[windowIndex].windowSide + 5 > self.windows[windowIndex].windowCoords[2]) {
                    boxX2 = self.windows[windowIndex].windowCoords[2] - boxLength2 - self.windows[windowIndex].windowSide - 5;
                }
                turtleRectangle(boxX2 - 2, self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - 16, boxX2 + boxLength2 + 2, self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - 5, 215, 215, 215, 0);
                turtlePenColor(0, 0, 0);
                textGLWriteString(sampleValue, boxX2, self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - 11, 8, 0);
            }
        }
        /* render side axis */
        turtleRectangle(self.windows[windowIndex].windowCoords[0], self.windows[windowIndex].windowCoords[1], self.windows[windowIndex].windowCoords[0] + 10, self.windows[windowIndex].windowCoords[3], self.themeColors[self.theme + 21], self.themeColors[self.theme + 22], self.themeColors[self.theme + 23], 100);
        turtlePenColor(0, 0, 0);
        turtlePenSize(1);
        double ycenter = (self.windows[windowIndex].windowCoords[1] + self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop) / 2;
        turtleGoto(self.windows[windowIndex].windowCoords[0], ycenter);
        turtlePenDown();
        turtleGoto(self.windows[windowIndex].windowCoords[0] + 5, ycenter);
        turtlePenUp();
        int tickMarks = round(self.orbit[orbitIndex].scale[1] / 4) * 4;
        double culling = self.orbit[orbitIndex].scale[1];
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
                sprintf(tickValue, "%d", (int) ((self.orbit[orbitIndex].scale[1] / tickMarks * mouseSample - self.orbit[orbitIndex].scale[1] / 2) - self.orbit[orbitIndex].offset[1]));
                turtlePenColor(215, 215, 215);
                textGLWriteString(tickValue, self.windows[windowIndex].windowCoords[0] + tickLength + 13, ypos, 8, 0);
            }
        }
        /* render bottom axis */
        turtleRectangle(self.windows[windowIndex].windowCoords[0] + 10, self.windows[windowIndex].windowCoords[1], self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowSide, self.windows[windowIndex].windowCoords[1] + 10, self.themeColors[self.theme + 21], self.themeColors[self.theme + 22], self.themeColors[self.theme + 23], 100);
        turtlePenColor(0, 0, 0);
        turtlePenSize(1);
        double xcenter = (self.windows[windowIndex].windowCoords[0] + self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowSide) / 2;
        turtleGoto(xcenter, self.windows[windowIndex].windowCoords[1]);
        turtlePenDown();
        turtleGoto(xcenter, self.windows[windowIndex].windowCoords[1] + 5);
        turtlePenUp();
        tickMarks = round(self.orbit[orbitIndex].scale[0] / 4) * 4;
        culling = self.orbit[orbitIndex].scale[0];
        while (culling > 60) {
            culling /= 4;
            tickMarks /= 4;
        }
        tickMarks = ceil(tickMarks / 4) * 4;
        double xquantum = (self.windows[windowIndex].windowCoords[2] - self.windows[windowIndex].windowSide - self.windows[windowIndex].windowCoords[0]) / tickMarks;
        for (int i = 1; i < tickMarks; i++) {
            double xpos = self.windows[windowIndex].windowCoords[0] + i * xquantum;
            turtleGoto(xpos, self.windows[windowIndex].windowCoords[1]);
            turtlePenDown();
            int tickLength = 2;
            if (i % (tickMarks / 4) == 0) {
                tickLength = 4;
            }
            turtleGoto(xpos, self.windows[windowIndex].windowCoords[1] + tickLength);
            turtlePenUp();
        }
        mouseSample = round((self.mx - self.windows[windowIndex].windowCoords[0]) / xquantum);
        if (mouseSample > 0 && mouseSample < tickMarks) {
            double xpos = self.windows[windowIndex].windowCoords[0] + mouseSample * xquantum;
            int tickLength = 2;
            if (mouseSample % (tickMarks / 4) == 0) {
                tickLength = 4;
            }
            if (self.my > self.windows[windowIndex].windowCoords[1] && self.my < self.windows[windowIndex].windowCoords[1] + 15) {
                turtleTriangle(xpos, self.windows[windowIndex].windowCoords[1] + tickLength + 2, xpos + 6, self.windows[windowIndex].windowCoords[1] + tickLength + 10, xpos - 6, self.windows[windowIndex].windowCoords[1] + tickLength + 10, 215, 215, 215, 0);
                char tickValue[24];
                sprintf(tickValue, "%d", (int) ((self.orbit[orbitIndex].scale[0] / tickMarks * mouseSample - self.orbit[orbitIndex].scale[0] / 2) - self.orbit[orbitIndex].offset[0]));
                turtlePenColor(215, 215, 215);
                textGLWriteString(tickValue, xpos, self.windows[windowIndex].windowCoords[1] + tickLength + 17, 8, 50);
            }
        }
    }
}

void renderInfoData() {
    int windowIndex = ilog2(WINDOW_INFO);
    if (self.windows[windowIndex].minimize == 0) {
        /* render window background */
        turtleRectangle(self.windows[windowIndex].windowCoords[0], self.windows[windowIndex].windowCoords[1], self.windows[windowIndex].windowCoords[2], self.windows[windowIndex].windowCoords[3], self.themeColors[self.theme + 12], self.themeColors[self.theme + 13], self.themeColors[self.theme + 14], 0);
        /* refresh button */
        if (self.infoRefresh) {
            if (self.commsEnabled == 1) {
                /* close all existing logging sockets */
                for (int i = 1; i < self.logVariables -> length; i++) {
                    logVariable_t *variable = self.logVariables -> data[i].p;
                    if (variable -> socketPtr != NULL) {
                        closesocket(*(variable -> socketPtr));
                    }
                }
                /* IMPORTANT - must close and reopen command socket (otherwise log info command is out of date) */
                closesocket(*self.cmdSocket);
                self.cmdSocket = win32tcpCreateSocket();
                unsigned char receiveBuffer[10] = {0};
                win32tcpReceive(self.cmdSocket, receiveBuffer, 1);
                unsigned char amdc_cmd_id[2] = {12, 34};
                win32tcpSend(self.cmdSocket, amdc_cmd_id, 2);
                printf("Successfully opened AMDC cmd socket with id %d\n", *receiveBuffer);
                self.cmdSocketID = *receiveBuffer;
            }
            list_clear(self.oldUsedVariableIndices);
            populateLoggedVariables();
            /* update broken dropdowns */
            for (int i = 0; i < self.windowRender -> length; i++) {
                if (self.windowRender -> data[i].i >= WINDOW_OSC) {
                    int windowIndex = ilog2(self.windowRender -> data[i].i);
                    for (int j = 0; j < self.windows[windowIndex].dropdowns -> length; j++) {
                        dropdownCalculateMax((dropdown_t *) self.windows[windowIndex].dropdowns -> data[j].p);
                    }
                }
                if (self.windowRender -> data[i].i == WINDOW_ORBIT) {
                    int windowIndex = ilog2(self.windowRender -> data[i].i);
                    for (int j = 0; j < self.windows[windowIndex].dropdowns -> length; j++) {
                        dropdownCalculateMax((dropdown_t *) self.windows[windowIndex].dropdowns -> data[j].p);
                    }
                }
            }
        }
        /* render data */
        double nameColumnWidth = textGLGetStringLength("Name", 8);
        turtlePenColor(self.themeColors[self.theme + 9], self.themeColors[self.theme + 10], self.themeColors[self.theme + 11]);
        for (int i = 1; i < self.logVariables -> length; i++) {
            if (textGLGetStringLength(self.logVariables -> data[i].s, 6) > nameColumnWidth) {
                nameColumnWidth = textGLGetStringLength(self.logVariables -> data[i].s, 6);
            }
        }
        turtleRectangle(self.windows[windowIndex].windowCoords[0], self.windows[windowIndex].windowCoords[1], self.windows[windowIndex].windowCoords[0] + nameColumnWidth + 20, self.windows[windowIndex].windowCoords[3], self.themeColors[self.theme + 0], self.themeColors[self.theme + 1], self.themeColors[self.theme + 2], 0);
        textGLWriteString("Name", self.windows[windowIndex].windowCoords[0] + 10 + nameColumnWidth / 2, self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - 10, 8, 50);
        for (int i = 1; i < self.logVariables -> length; i++) {
            textGLWriteString(self.logVariables -> data[i].s, self.windows[windowIndex].windowCoords[0] + 10 + nameColumnWidth / 2, self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - 25 - (i - 1) * 10, 6, 50);
        }
        double samplesColumnWidth = textGLGetStringLength("Samples/s", 8);
        turtleRectangle(self.windows[windowIndex].windowCoords[0] + nameColumnWidth + 20, self.windows[windowIndex].windowCoords[1], self.windows[windowIndex].windowCoords[0] + nameColumnWidth + 35 + samplesColumnWidth + 5, self.windows[windowIndex].windowCoords[3], self.themeColors[self.theme + 0] - 8, self.themeColors[self.theme + 1] - 8, self.themeColors[self.theme + 2] - 8, 0);
        textGLWriteString("Samples/s", self.windows[windowIndex].windowCoords[0] + nameColumnWidth + 30 + samplesColumnWidth / 2, self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - 10, 8, 50);
        for (int i = 1; i < self.logVariables -> length; i++) {
            int samplesPerSecond = self.data -> data[i].r -> data[0].d;
            char sampleString[24];
            sprintf(sampleString, "%d", samplesPerSecond);
            textGLWriteString(sampleString, self.windows[windowIndex].windowCoords[0] + nameColumnWidth + 30 + samplesColumnWidth / 2, self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - 25 - (i - 1) * 10, 6, 50);
        }
        double totalColumnWidth = textGLGetStringLength("Total Samples", 8);
        turtleRectangle(self.windows[windowIndex].windowCoords[0] + nameColumnWidth + 40 + samplesColumnWidth, self.windows[windowIndex].windowCoords[1], self.windows[windowIndex].windowCoords[0] + nameColumnWidth + 60 + samplesColumnWidth + totalColumnWidth, self.windows[windowIndex].windowCoords[3], self.themeColors[self.theme + 0] - 16, self.themeColors[self.theme + 1] - 16, self.themeColors[self.theme + 2] - 16, 0);
        textGLWriteString("Total Samples", self.windows[windowIndex].windowCoords[0] + nameColumnWidth + 50 + samplesColumnWidth + totalColumnWidth / 2, self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - 10, 8, 50);
        for (int i = 1; i < self.logVariables -> length; i++) {
            int totalSamples = self.data -> data[i].r -> length - 1;
            char sampleString[24];
            sprintf(sampleString, "%d", totalSamples);
            textGLWriteString(sampleString, self.windows[windowIndex].windowCoords[0] + nameColumnWidth + 50 + samplesColumnWidth + totalColumnWidth / 2, self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - 25 - (i - 1) * 10, 6, 50);
        }
        double valuesColumnWidth = textGLGetStringLength("Value", 8);
        turtleRectangle(self.windows[windowIndex].windowCoords[0] + nameColumnWidth + 60 + samplesColumnWidth + totalColumnWidth, self.windows[windowIndex].windowCoords[1], self.windows[windowIndex].windowCoords[0] + nameColumnWidth + 80 + samplesColumnWidth + totalColumnWidth + valuesColumnWidth, self.windows[windowIndex].windowCoords[3], self.themeColors[self.theme + 0] - 32, self.themeColors[self.theme + 1] - 32, self.themeColors[self.theme + 2] - 32, 0);
        textGLWriteString("Value", self.windows[windowIndex].windowCoords[0] + nameColumnWidth + 70 + samplesColumnWidth + totalColumnWidth + valuesColumnWidth / 2, self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - 10, 8, 50);
        for (int i = 1; i < self.logVariables -> length; i++) {
            double value = self.data -> data[i].r -> data[self.data -> data[i].r -> length - 1].d;
            char sampleString[24];
            sprintf(sampleString, "%0.2lf", value);
            textGLWriteString(sampleString, self.windows[windowIndex].windowCoords[0] + nameColumnWidth + 70 + samplesColumnWidth + totalColumnWidth + valuesColumnWidth / 2, self.windows[windowIndex].windowCoords[3] - self.windows[windowIndex].windowTop - 25 - (i - 1) * 10, 6, 50);
        }
        self.windows[windowIndex].windowMinX = nameColumnWidth + samplesColumnWidth + totalColumnWidth + 103;
    }
}

void renderOrder() {
    for (int i = 0; i < self.windowRender -> length; i++) {
        if (self.windowRender -> data[i].i == WINDOW_EDITOR) {
            /* SKIP unfinished EDITOR window */
            continue;
        }
        if (self.windowRender -> data[i].i >= WINDOW_OSC) {
            renderOscData(ilog2(self.windowRender -> data[i].i) - ilog2(WINDOW_OSC));
        } else if (self.windowRender -> data[i].i >= WINDOW_ORBIT) {
            renderOrbitData(ilog2(self.windowRender -> data[i].i) - ilog2(WINDOW_ORBIT));
        } else if (self.windowRender -> data[i].i == WINDOW_FREQ) {
            renderFreqData();
        } else if (self.windowRender -> data[i].i == WINDOW_EDITOR) {
            renderEditorData();
        } else if (self.windowRender -> data[i].i == WINDOW_INFO) {
            renderInfoData();
        }
        renderWindow(ilog2(self.windowRender -> data[i].i), i == self.windowRender -> length - 1);
    }
    /* render bottom bar */
    int subtract = 0;
    turtleRectangle(-320, -180, 320, -170, self.themeColors[self.theme + 3], self.themeColors[self.theme + 4], self.themeColors[self.theme + 5], 50);
    for (int i = 0; i < NUM_WINDOWS; i++) {
        if (strlen(self.windows[i].title) == 0 || strcmp(self.windows[i].title, "Editor") == 0) {
            /* SKIP unfinished EDITOR window */
            subtract += 1;
            continue;
        }
        int minX = -320 + (1) + 50 * (i - subtract);
        int minY = -179;
        int maxX = -320 + (49) + 50 * (i - subtract);
        int maxY = -171;
        if (!self.mouseDown && self.mx >= minX && self.mx <= maxX && self.my >= minY && self.my <= maxY) {
            turtleRectangle(minX, minY, maxX, maxY, self.themeColors[self.theme + 12], self.themeColors[self.theme + 13], self.themeColors[self.theme + 14], 0);
            self.windows[i].click = 2;
        } else {
            turtleRectangle(minX, minY, maxX, maxY, self.themeColors[self.theme + 3], self.themeColors[self.theme + 4], self.themeColors[self.theme + 5], 0);
        }
        /* write title */
        turtlePenColor(self.themeColors[self.theme + 9], self.themeColors[self.theme + 10], self.themeColors[self.theme + 11]);
        textGLWriteUnicode(self.windows[i].title, -320 + (50 / 2) + 50 * (i - subtract), -175, 5, 50);
    }
}

void saveOsc(char *filename) {
    char header[1024] = "Time (ms), ";
    FILE *fp = fopen(filename, "w");
    /* assess oscilloscope */
    int oscIndex = 0;
    int windowIndex = 0;
    for (int i = 0; i < self.windowRender -> length; i++) {
        if (self.windowRender -> data[i].i >= WINDOW_OSC) {
            oscIndex = ilog2(self.windowRender -> data[i].i) - ilog2(WINDOW_OSC);
            windowIndex = ilog2(WINDOW_OSC) + oscIndex;
        }
    }
    /* assess number of channels used and datarate */
    list_t *channels = list_init();
    double xquantum[4] = {-1, -1, -1, -1};
    double globalQuantum = 10000000.0;
    int iterations = 0;
    for (int i = 0; i < 4; i++) {
        int dataIndex = self.osc[oscIndex].dataIndex[i];
        if (dataIndex > 0) {
            xquantum[i] = (self.osc[oscIndex].windowSizeMicroseconds / 1000) / (self.osc[oscIndex].rightBound[i] - self.osc[oscIndex].leftBound[i]);
            if (xquantum[i] < globalQuantum) {
                globalQuantum = xquantum[i];
                iterations = self.osc[oscIndex].rightBound[i] - self.osc[oscIndex].leftBound[i];
            }
            sprintf(header, "%s%s, ", header, self.logVariables -> data[dataIndex].s);
            list_append(channels, (unitype) dataIndex, 'i');
            list_append(channels, (unitype) self.osc[oscIndex].leftBound[i], 'i');
            list_append(channels, (unitype) xquantum[i], 'd');
        }
    }
    header[strlen(header) - 2] = '\0';
    fprintf(fp, "%s\n", header);
    /* linear interpolate */
    double timestep = 0.0;
    for (int j = 0; j < iterations; j++) {
        char line[1024];
        sprintf(line, "%lf, ", timestep);
        for (int i = 0; i < channels -> length / 3; i++) {
            double preciseIndex = channels -> data[i * 3 + 1].i + timestep / channels -> data[i * 3 + 2].d;
            double valueLower = self.data -> data[channels -> data[i * 3 + 0].i].r -> data[(int) preciseIndex].d;
            double valueUpper = self.data -> data[channels -> data[i * 3 + 0].i].r -> data[(int) preciseIndex + 1].d;
            double value = valueLower + (valueUpper - valueLower) * (preciseIndex - (int) preciseIndex);
            sprintf(line, "%s%lf, ", line, value);
        }
        line[strlen(line) - 2] = '\0';
        fprintf(fp, "%s\n", line);
        timestep += globalQuantum;
    }
    fclose(fp);
    list_free(channels);
}

void parseRibbonOutput() {
    if (ribbonRender.output[0] == 1) {
        ribbonRender.output[0] = 0; // untoggle
        if (ribbonRender.output[1] == 0) { // file
            if (ribbonRender.output[2] == 1) { // new oscilloscope
                createNewOsc();
            }
            if (ribbonRender.output[2] == 2) { // new orbit
                createNewOrbit();
            }
            if (ribbonRender.output[2] == 3) { // save/save as
                if (win32FileDialogPrompt(1, "") != -1) {
                    saveOsc(win32FileDialog.selectedFilename);
                    printf("Saved to: %s\n", win32FileDialog.selectedFilename);
                }
            }
            if (ribbonRender.output[2] == 4) { // open
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
                } else {
                    ribbonDarkTheme();
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
    self.rightMouseDown = turtleMouseRight();
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
    if (turtleKeyPressed(GLFW_KEY_C)) {
        for (int i = 0; i < self.windowRender -> length; i++) {
            int windowIndex = ilog2(self.windowRender -> data[i].i);
            window_t printdow = self.windows[windowIndex];
            printf("%lf %lf %lf %lf\n", printdow.windowCoords[0], printdow.windowCoords[1], printdow.windowCoords[2], printdow.windowCoords[3]);
        }
        printf("\n\n\n");
    }
    turtleClear();
}

int main(int argc, char *argv[]) {
    /* hide console */
    #ifndef DEBUGGING_FLAG
    FreeConsole();
    #endif
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
    glfwSetWindowSizeLimits(window, windowHeight * 16 / 9, windowHeight, windowHeight * 16 / 9, windowHeight);
    int width, height, oldWidth, oldHeight;
    glfwGetWindowSize(window, &oldWidth, &oldHeight);

    /* initialize turtle */
    turtleInit(window, -320, -180, 320, 180);
    _mkdir("include");
    /* initialise textGL */
    textGLInit(window, "include/fontBez.tgl");
    /* initialise ribbon */
    ribbonInit(window, "include/ribbonConfig.txt");
    ribbonDarkTheme(); // dark theme preset
    /* initialise win32tools */
    win32ToolsInit();
    win32FileDialogAddExtension("csv"); // add csv to extension restrictions

    int tps = 120; // ticks per second (locked to fps in this case)
    uint64_t tick = 0;

    clock_t start;
    clock_t end;

    turtleBgColor(30, 30, 30);

    /* initialise win32tcp */
    if (argc == 1) {
        pthread_t initThread;
        pthread_create(&initThread, NULL, specialInitThread, NULL);
        turtlePenColor(200, 200, 200);
        while (self.tcpInit == 0 && turtle.close == 0) {
            start = clock();
            turtleClear();
            if (tick / 30 % 4 == 0) {
                textGLWriteString("Connecting to AMDC", 0, 0, 40, 50);
            } else if (tick / 30 % 4 == 1) {
                textGLWriteString("Connecting to AMDC.", 0, 0, 40, 50);
            } else if (tick / 30 % 4 == 2) {
                textGLWriteString("Connecting to AMDC..", 0, 0, 40, 50);
            } else if (tick / 30 % 4 == 3) {
                textGLWriteString("Connecting to AMDC...", 0, 0, 40, 50);
            }
            turtleUpdate(); // update the screen
            end = clock();
            while ((double) (end - start) / CLOCKS_PER_SEC < (1.0 / tps)) {
                end = clock();
            }
            tick++;
        }
        if (turtle.close == 1) {
            return -1;
        }
        if (self.tcpInit == -1) {
            char errorMessage[128];
            sprintf(errorMessage, "Failed to connect to %s, ensure AMDC is plugged in and listening on port %s\n", win32Socket.address, win32Socket.port);
            #ifdef DEBUGGING_FLAG
            printf(errorMessage);
            #endif
            while (turtle.close == 0) {
                start = clock();
                turtleClear();
                textGLWriteString(errorMessage, 0, 0, 11, 50);
                textGLWriteString("restart EMPV to try again\n", 0, -20, 11, 50);
                turtleUpdate(); // update the screen
                end = clock();
                while ((double) (end - start) / CLOCKS_PER_SEC < (1.0 / tps)) {
                    end = clock();
                }
                tick++;
            }
            return -1;
        }
        self.cmdSocket = win32tcpCreateSocket();
        if (self.cmdSocket != NULL) {
            unsigned char receiveBuffer[10] = {0};
            win32tcpReceive(self.cmdSocket, receiveBuffer, 1);
            unsigned char amdc_cmd_id[2] = {12, 34};
            win32tcpSend(self.cmdSocket, amdc_cmd_id, 2);
            printf("Successfully opened AMDC cmd socket with id %d\n", *receiveBuffer);
            self.cmdSocketID = *receiveBuffer;
        }
        self.commsEnabled = 1;
    }

    init(); // initialise empv

    while (turtle.close == 0) { // main loop
        start = clock();
        if (self.commsEnabled == 0) {
            /* populate demo data */
            double sinValue1 = sin(tick / 5.0) * 25;
            double sinValue2 = sin(tick / 3.37) * 25;
            double sinValue3 = sin(tick * 1.1) * 12.5;
            list_append(self.data -> data[1].r, (unitype) (sinValue1), 'd');
            list_append(self.data -> data[2].r, (unitype) (sin(tick / 5.0 + M_PI / 3 * 2) * 25), 'd');
            list_append(self.data -> data[2].r, (unitype) (sin((tick + 0.5) / 5.0 + M_PI / 3 * 2) * 25), 'd');
            list_append(self.data -> data[3].r, (unitype) (sin(tick / 5.0 + M_PI / 3 * 4) * 25), 'd');
            list_append(self.data -> data[4].r, (unitype) (sin(tick / 5.0 + M_PI / 2) * 25), 'd');
        }
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