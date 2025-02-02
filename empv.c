/* ribbonTest FOR WINDOWS:
test of 16:9 aspect ratio and the top ribbon (for gui interface applications)
parseRibbonOutput function added as example */

#include "include/ribbon.h"
#include "include/popup.h"
#include "include/win32Tools.h"
#include <time.h>

#define THEME_DARK     15

#define DIAL_LINEAR    0
#define DIAL_LOG       1
#define DIAL_EXP       2

typedef struct { // all empv variables (shared state) are defined here
    list_t *data; // a list of all data collected through ethernet
    list_t *logVariables; // a list of variables logged on the AMDC
    /* mouse variables */
    double mx;
    double my;
    double mw;
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
    /* dial variables */
    double dialRange[6];
    double dialSize[2];
    double *dialVariable[2];
    int dialStatus[4];
    double dialPosition[4];
    double dialAnchorX;
    double dialAnchorY;
    /* color variables */
    int theme;
    double themeColors[90];
} empv;

empv self; // global state

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
    self.windowMinY = 80 + self.windowTop;
    self.move = 0;
    self.anchorX = 0;
    self.anchorY = 0;
    self.resize = 0;
    /* dial */
    self.dialRange[0] = DIAL_EXP;
    self.dialRange[1] = 1;
    self.dialRange[2] = 1000;
    self.dialRange[3] = DIAL_EXP;
    self.dialRange[4] = 50;
    self.dialRange[5] = 10000;
    self.dialSize[0] = 8;
    self.dialSize[1] = 8;
    self.dialStatus[0] = 0;
    self.dialStatus[2] = 0;
    self.dialVariable[0] = &self.windowSize;
    self.dialVariable[1] = &self.topBound;
    self.dialPosition[0] = 0.95;
    self.dialPosition[1] = -15 - self.windowTop;
    self.dialPosition[2] = 0.95;
    self.dialPosition[3] = -55 - self.windowTop;
    self.dialAnchorX = 0;
    self.dialAnchorY = 0;
    /* color */
    self.theme = THEME_DARK;
    double themeCopy[30] = {
        /* light theme */
        255, 255, 255, // background color
        195, 195, 195, // window color
        255, 0, 0, // data color
        0, 0, 0, // text color
        230, 230, 230, // window background color
        /* dark theme */
        60, 60, 60, // background color
        10, 10, 10, // window color
        19, 236, 48, // data color
        200, 200, 200, // text color
        80, 80, 80, // window background color
    };
    memcpy(self.themeColors, themeCopy, sizeof(themeCopy));
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
    turtleRentangle(self.windowCoords[2] - self.windowSide, self.windowCoords[1], self.windowCoords[2], self.windowCoords[3], self.themeColors[self.theme + 3], self.themeColors[self.theme + 4], self.themeColors[self.theme + 5], 0);
    turtlePenColor(self.themeColors[self.theme + 9], self.themeColors[self.theme + 10], self.themeColors[self.theme + 11]);
    /* write title */
    textGLWriteString("Display", (self.windowCoords[0] + self.windowCoords[2] - self.windowSide) / 2, self.windowCoords[3] - self.windowTop * 0.45, self.windowTop * 0.5, 50);
    /* draw sidebar */
    textGLWriteString("X Scale", (self.windowCoords[2] - self.windowSide + self.windowCoords[2]) / 2, self.windowCoords[1] + (self.windowCoords[3] - self.windowCoords[1]) * 0.95 - self.windowTop, 7, 50);
    textGLWriteString("Y Scale", (self.windowCoords[2] - self.windowSide + self.windowCoords[2]) / 2, self.windowCoords[1] + (self.windowCoords[3] - self.windowCoords[1]) * 0.95 - 40 - self.windowTop, 7, 50);
    /* draw dials */
    for (int i = 0; i < sizeof(self.dialVariable) / sizeof(double *); i++) {
        turtlePenSize(self.dialSize[1] * 2);
        double dialX = (self.windowCoords[2] - self.windowSide + self.windowCoords[2]) / 2;
        double dialY = self.windowCoords[1] + (self.windowCoords[3] - self.windowCoords[1]) * self.dialPosition[i * 2] + self.dialPosition[i * 2 + 1];
        turtleGoto(dialX, dialY);
        turtlePenDown();
        turtlePenUp();
        turtlePenSize(self.dialSize[i] * 2 * 0.8);
        turtlePenColor(self.themeColors[self.theme + 3], self.themeColors[self.theme + 4], self.themeColors[self.theme + 5]);
        turtlePenDown();
        turtlePenUp();
        turtlePenColor(self.themeColors[self.theme + 9], self.themeColors[self.theme + 10], self.themeColors[self.theme + 11]);
        turtlePenSize(1);
        turtlePenDown();
        double dialAngle;
        if (self.dialRange[i * 3] == DIAL_LOG) {
            dialAngle = pow(360, (*(self.dialVariable[i]) - self.dialRange[i * 3 + 1]) / (self.dialRange[i * 3 + 2] - self.dialRange[i * 3 + 1]));
        } else if (self.dialRange[i * 3] == DIAL_LINEAR) {
            dialAngle = (*(self.dialVariable[i]) - self.dialRange[i * 3 + 1]) / (self.dialRange[i * 3 + 2] - self.dialRange[i * 3 + 1]) * 360;
        } else if (self.dialRange[i * 3] == DIAL_EXP) {
            dialAngle = 360 * (log(((*(self.dialVariable[i]) - self.dialRange[i * 3 + 1]) / (self.dialRange[i * 3 + 2] - self.dialRange[i * 3 + 1])) * 360 + 1) / log(361));
        }
        turtleGoto(dialX + sin(dialAngle / 57.2958) * self.dialSize[i], dialY + cos(dialAngle / 57.2958) * self.dialSize[i]);
        turtlePenUp();
        if (turtleMouseDown()) {
            if (self.dialStatus[i * 2] < 0) {
                self.dialAnchorX = dialX;
                self.dialAnchorY = dialY;
                self.dialStatus[i * 2] *= -1;
                self.dialStatus[i * 2 + 1] = self.mx - dialX;
            }
        } else {
            if (self.mx > dialX - self.dialSize[i] && self.mx < dialX + self.dialSize[i] && self.my > dialY - self.dialSize[i] && self.my < dialY + self.dialSize[i]) {
                self.dialStatus[i * 2] = -1;
            } else {
                self.dialStatus[i * 2] = 0;
            }
        }
        if (self.dialStatus[i * 2] > 0) {
            dialAngle = angleBetween(self.dialAnchorX, self.dialAnchorY, self.mx, self.my);
            if (self.my < self.dialAnchorY) {
                self.dialStatus[i * 2 + 1] = self.mx - dialX;
            }
            if ((dialAngle < 0.001 || dialAngle > 180) && self.my > self.dialAnchorY && self.dialStatus[i * 2 + 1] >= 0) {
                dialAngle = 0.001;
            }
            if ((dialAngle > 359.99 || dialAngle < 180) && self.my > self.dialAnchorY && self.dialStatus[i * 2 + 1] < 0) {
                dialAngle = 359.99;
            }
            if (self.dialRange[i * 3] == DIAL_LOG) {
                *(self.dialVariable[i]) = self.dialRange[i * 3 + 1] + (self.dialRange[i * 3 + 2] - self.dialRange[i * 3 + 1]) * (log(dialAngle) / log(360));
            } else if (self.dialRange[i * 3] == DIAL_LINEAR) {
                *(self.dialVariable[i]) = self.dialRange[i * 3 + 1] + ((self.dialRange[i * 3 + 2] - self.dialRange[i * 3 + 1]) * dialAngle / 360);
            } else if (self.dialRange[i * 3] == DIAL_EXP) {
                *(self.dialVariable[i]) = self.dialRange[i * 3 + 1] + (self.dialRange[i * 3 + 2] - self.dialRange[i * 3 + 1]) * ((pow(361, dialAngle / 360) - 1) / 360);
            }
        }
        char bubble[24];
        sprintf(bubble, "%.0lf", *(self.dialVariable[i]));
        textGLWriteString(bubble, dialX + self.dialSize[i] + 3, dialY, 4, 0);
    }

    /* window move and resize logic */
    /* move */
    if (turtleMouseDown()) {
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
    if (turtleMouseDown()) {
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
    self.rightBound = self.data -> length;
    if (self.rightBound > self.leftBound + self.windowSize) {
        self.leftBound = self.rightBound - self.windowSize;
    }
    turtlePenSize(1);
    turtlePenColor(self.themeColors[self.theme + 6], self.themeColors[self.theme + 7], self.themeColors[self.theme + 8]);
    double xquantum = (self.windowCoords[2] - self.windowSide - self.windowCoords[0]) / (self.rightBound - self.leftBound - 1);
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
                    self.theme = THEME_DARK;
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