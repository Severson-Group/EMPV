/* ribbonTest FOR WINDOWS:
test of 16:9 aspect ratio and the top ribbon (for gui interface applications)
parseRibbonOutput function added as example */

#include "include/ribbon.h"
#include "include/popup.h"
#include "include/win32Tools.h"
#include <time.h>

#define DIAL_LINEAR    0
#define DIAL_LOG       1
#define DIAL_EXP       2

typedef struct {
    char label[24];
    int type;
    int status[2];
    double size;
    double position[2];
    double range[2];
    double *variable;
} dial_t;

typedef struct {
    char label[24];
    int status;
    double size;
    double position[2];
    int *variable;
} switch_t;

typedef struct { // all empv variables (shared state) are defined here
    list_t *data; // a list of all data collected through ethernet
    list_t *logVariables; // a list of variables logged on the AMDC
    /* mouse variables */
    double mx; // mouseX
    double my; // mouseY
    double mw; // mouse wheel
    char mouseDown;  // mouse down
    /* window variables */
    int leftBound; // left bound (index in data list)
    int rightBound; // right bound (index in data list)
    double bottomBound; // bottom bound (y value)
    double topBound; // top bound (y value)
    double windowSize; // size of window (index in data list)
    double windowCoords[4]; // coordinates of window on canvas
    double windowTop; // size of window top bar
    double windowSide; // size of window side bar
    double windowMinX; // window size on canvas minimum
    double windowMinY; // window size on canvas minimum
    int move; // moving window
    double anchorX;
    double anchorY;
    double anchorPoints[4];
    int resize; // resizing window
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
} empv;

empv self; // global state

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
    self.windowCoords[0] = -240;
    self.windowCoords[1] = -160;
    self.windowCoords[2] = 240;
    self.windowCoords[3] = 160;
    self.windowTop = 15;
    self.windowSide = 50;
    self.windowMinX = 50 + self.windowSide;
    self.windowMinY = 120 + self.windowTop;
    self.move = 0;
    self.anchorX = 0;
    self.anchorY = 0;
    self.resize = 0;
    self.stop = 0;
    /* ui */
    dialInit(&self.dials[0], "X Scale", &self.windowSize, DIAL_EXP, 1, -25 - self.windowTop, 8, 1, 1000);
    dialInit(&self.dials[1], "Y Scale", &self.topBound, DIAL_EXP, 1, -65 - self.windowTop, 8, 50, 10000);
    self.dialAnchorX = 0;
    self.dialAnchorY = 0;
    switchInit(&self.switches[0], "Stop", &self.stop, 1, -100 - self.windowTop, 8);
    /* color */
    double themeCopy[42] = {
        /* light theme */
        255, 255, 255, // background color
        195, 195, 195, // window color
        255, 0, 0, // data color
        0, 0, 0, // text color
        230, 230, 230, // window background color
        0, 255, 0, // switch toggled on color
        255, 0, 0, // switch toggled off color
        /* dark theme */
        60, 60, 60, // background color
        10, 10, 10, // window color
        19, 236, 48, // data color
        200, 200, 200, // text color
        80, 80, 80, // window background color
        0, 255, 0, // switch toggled on color
        255, 0, 0 // switch toggled off color
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

void dialTick() {
    for (int i = 0; i < sizeof(self.dials) / sizeof(dial_t); i++) {
        textGLWriteString(self.dials[i].label, (self.windowCoords[2] - self.windowSide + self.windowCoords[2]) / 2, self.windowCoords[1] + (self.windowCoords[3] - self.windowCoords[1]) * self.dials[i].position[0] + self.dials[i].position[1] + 15, 7, 50);
        turtlePenSize(self.dials[i].size * 2);
        double dialX = (self.windowCoords[2] - self.windowSide + self.windowCoords[2]) / 2;
        double dialY = self.windowCoords[1] + (self.windowCoords[3] - self.windowCoords[1]) * self.dials[i].position[0] + self.dials[i].position[1];
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
        textGLWriteString(self.switches[i].label, (self.windowCoords[2] - self.windowSide + self.windowCoords[2]) / 2, self.windowCoords[1] + (self.windowCoords[3] - self.windowCoords[1]) * self.switches[i].position[0] + self.switches[i].position[1] + 15, 7, 50);
        turtlePenColor(self.themeColors[self.theme + 12], self.themeColors[self.theme + 13], self.themeColors[self.theme + 14]);
        turtlePenSize(self.switches[i].size * 1.2);
        double switchX = (self.windowCoords[2] - self.windowSide + self.windowCoords[2]) / 2;
        double switchY = self.windowCoords[1] + (self.windowCoords[3] - self.windowCoords[1]) * self.switches[i].position[0] + self.switches[i].position[1];
        turtleGoto(switchX - self.switches[i].size * 0.8, switchY);
        turtlePenDown();
        turtleGoto(switchX + self.switches[i].size * 0.8, switchY);
        turtlePenUp();
        turtlePenColor(self.themeColors[self.theme + 9], self.themeColors[self.theme + 10], self.themeColors[self.theme + 11]);
        turtlePenSize(self.switches[i].size);
        if (*(self.switches[i].variable)) {
            turtleGoto(switchX + self.switches[i].size * 0.8, switchY);
        } else {
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

void renderWindow() {
    /* render window */
    turtlePenSize(2);
    turtlePenColor(self.themeColors[self.theme + 3], self.themeColors[self.theme + 4], self.themeColors[self.theme + 5]);
    turtleGoto(self.windowCoords[0], self.windowCoords[1]);
    turtlePenDown();
    turtleGoto(self.windowCoords[0], self.windowCoords[3]);
    turtleGoto(self.windowCoords[2], self.windowCoords[3]);
    turtleGoto(self.windowCoords[2], self.windowCoords[1]);
    turtleGoto(self.windowCoords[0], self.windowCoords[1]);
    turtlePenUp();
    turtleRentangle(self.windowCoords[0], self.windowCoords[3], self.windowCoords[2], self.windowCoords[3] - self.windowTop, self.themeColors[self.theme + 3], self.themeColors[self.theme + 4], self.themeColors[self.theme + 5], 0);
    turtleRentangle(self.windowCoords[2] - self.windowSide, self.windowCoords[1], self.windowCoords[2], self.windowCoords[3], self.themeColors[self.theme + 3], self.themeColors[self.theme + 4], self.themeColors[self.theme + 5], 40);
    turtlePenColor(self.themeColors[self.theme + 9], self.themeColors[self.theme + 10], self.themeColors[self.theme + 11]);
    /* write title */
    textGLWriteString("Display", (self.windowCoords[0] + self.windowCoords[2] - self.windowSide) / 2, self.windowCoords[3] - self.windowTop * 0.45, self.windowTop * 0.5, 50);
    /* draw sidebar UI elements */
    dialTick();
    switchTick();
    /* window move and resize logic */
    /* move */
    if (self.mouseDown) {
        if (self.move < 0) {
            self.anchorX = self.mx;
            self.anchorY = self.my;
            memcpy(self.anchorPoints, self.windowCoords, sizeof(double) * 4);
            self.move *= -1;
        }
    } else {
        if (self.mx > self.windowCoords[0] && self.mx < self.windowCoords[2] && self.my > self.windowCoords[3] - self.windowTop && self.my < self.windowCoords[3]) {
            self.move = -1;
        } else {
            self.move = 0;
        }
    }
    if (self.move > 0) {
        self.windowCoords[0] = self.anchorPoints[0] + self.mx - self.anchorX;
        self.windowCoords[1] = self.anchorPoints[1] + self.my - self.anchorY;
        self.windowCoords[2] = self.anchorPoints[2] + self.mx - self.anchorX;
        self.windowCoords[3] = self.anchorPoints[3] + self.my - self.anchorY;
    }
    /* resize */
    double epsilon = 3;
    if (self.mouseDown) {
        if (self.resize < 0) {
            self.resize *= -1;
            self.move = 0; // don't move and resize
            switch (self.resize) {
                case 1:
                    win32SetCursor(CURSOR_DIAGONALRIGHT);
                break;
                case 2:
                    win32SetCursor(CURSOR_DIAGONALLEFT);
                break;
                case 3:
                    win32SetCursor(CURSOR_DIAGONALRIGHT);
                break;
                case 4:
                    win32SetCursor(CURSOR_DIAGONALLEFT);
                break;
                default:
                break;
            }
        }
    } else {
        if (self.mx > self.windowCoords[2] - epsilon && self.mx < self.windowCoords[2] + epsilon && self.my > self.windowCoords[1] - epsilon && self.my < self.windowCoords[1] + epsilon) {
            win32SetCursor(CURSOR_DIAGONALLEFT);
            self.resize = -2;
        } else if (self.mx > self.windowCoords[0] - epsilon && self.mx < self.windowCoords[0] + epsilon && self.my > self.windowCoords[3] - epsilon && self.my < self.windowCoords[3] + epsilon) {
            win32SetCursor(CURSOR_DIAGONALLEFT);
            self.resize = -4;
        } else if (self.mx > self.windowCoords[0] - epsilon && self.mx < self.windowCoords[0] + epsilon && self.my > self.windowCoords[1] - epsilon && self.my < self.windowCoords[1] + epsilon) {
            win32SetCursor(CURSOR_DIAGONALRIGHT);
            self.resize = -1;
        } else if (self.mx > self.windowCoords[2] - epsilon && self.mx < self.windowCoords[2] + epsilon && self.my > self.windowCoords[3] - epsilon && self.my < self.windowCoords[3] + epsilon) {
            win32SetCursor(CURSOR_DIAGONALRIGHT);
            self.resize = -3;
        } else {
            self.resize = 0;
        }
    }
    if (self.resize > 0) {
        switch (self.resize) {
            case 1:
            self.windowCoords[0] = self.mx;
            self.windowCoords[1] = self.my;
            if (self.windowCoords[0] > self.windowCoords[2] - self.windowMinX) {
                self.windowCoords[0] = self.windowCoords[2] - self.windowMinX;
            }
            if (self.windowCoords[1] > self.windowCoords[3] - self.windowMinY) {
                self.windowCoords[1] = self.windowCoords[3] - self.windowMinY;
            }
            break;
            case 2:
            self.windowCoords[2] = self.mx;
            self.windowCoords[1] = self.my;
            if (self.windowCoords[2] < self.windowCoords[0] + self.windowMinX) {
                self.windowCoords[2] = self.windowCoords[0] + self.windowMinX;
            }
            if (self.windowCoords[1] > self.windowCoords[3] - self.windowMinY) {
                self.windowCoords[1] = self.windowCoords[3] - self.windowMinY;
            }
            break;
            case 3:
            self.windowCoords[2] = self.mx;
            self.windowCoords[3] = self.my;
            if (self.windowCoords[2] < self.windowCoords[0] + self.windowMinX) {
                self.windowCoords[2] = self.windowCoords[0] + self.windowMinX;
            }
            if (self.windowCoords[3] < self.windowCoords[1] + self.windowMinY) {
                self.windowCoords[3] = self.windowCoords[1] + self.windowMinY;
            }
            break;
            case 4:
            self.windowCoords[0] = self.mx;
            self.windowCoords[3] = self.my;
            if (self.windowCoords[0] > self.windowCoords[2] - self.windowMinX) {
                self.windowCoords[0] = self.windowCoords[2] - self.windowMinX;
            }
            if (self.windowCoords[3] < self.windowCoords[1] + self.windowMinY) {
                self.windowCoords[3] = self.windowCoords[1] + self.windowMinY;
            }
            break;
            default:
            break;
        }
    }
}

void renderData() {
    self.bottomBound = self.topBound * -1;
    /* render window background */
    turtleRentangle(self.windowCoords[0], self.windowCoords[1], self.windowCoords[2], self.windowCoords[3], self.themeColors[self.theme + 12], self.themeColors[self.theme + 13], self.themeColors[self.theme + 14], 0);
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
    double xquantum = (self.windowCoords[2] - self.windowCoords[0]) / (self.rightBound - self.leftBound - 1);
    for (int i = 0; i < self.rightBound - self.leftBound; i++) {
        turtleGoto(self.windowCoords[0] + i * xquantum, self.windowCoords[1] + ((self.data -> data[self.leftBound + i].d - self.bottomBound) / (self.topBound - self.bottomBound)) * (self.windowCoords[3] - self.windowTop - self.windowCoords[1]));
        turtlePenDown();
    }
    turtlePenUp();
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
    window = glfwCreateWindow(1728, 972, "EMPV", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetWindowSizeLimits(window, 128, 72, 1728, 972);

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
        double sinValue = sin(tick / 5.0) * 50;
        list_append(self.data, (unitype) sinValue, 'd');
        utilLoop();
        turtleGetMouseCoords(); // get the mouse coordinates (turtle.mouseX, turtle.mouseY)
        turtleClear();
        renderData();
        renderWindow();
        ribbonUpdate();
        parseRibbonOutput();
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