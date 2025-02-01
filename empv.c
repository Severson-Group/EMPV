/* ribbonTest FOR WINDOWS:
test of 16:9 aspect ratio and the top ribbon (for gui interface applications)
parseRibbonOutput function added as example */

#include "include/ribbon.h"
#include "include/popup.h"
#include "include/win32Tools.h"
#include <time.h>

typedef struct { // all empv variables (shared state) are defined here
    list_t *data; // a list of all data collected through ethernet
    /* mouse variables */
    double mx;
    double my;
    double mw;
    /* window variables */
    int leftBound; // left bound (index in data list)
    int rightBound; // right bound (index in data list)
    double bottomBound; // bottom bound (y value)
    double topBound; // top bound (y value)
    int windowSize; // size of window (index in data list)
    double windowCoords[4]; // coordinates of window on canvas
    double windowTop; // size of window top bar
    double windowMinX; // window size on canvas minimum
    double windowMinY; // window size on canvas minimum
    int move; // moving window
    double anchorX;
    double anchorY;
    double anchorPoints[4];
    int resize; // resizing window
    /* color variables */
    int theme;
    double themeColors[90];
} empv;

void init(empv *selfp) { // initialises the empv variabes (shared state)
    empv self = *selfp;
    /* data */
    self.data = list_init();
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
    self.windowMinX = 50;
    self.windowMinY = 50 + self.windowTop;
    self.move = 0;
    self.anchorX = 0;
    self.anchorY = 0;
    self.resize = 0;
    /* color */
    self.theme = 0;
    double themeCopy[18] = {
        /* light theme */
        255, 255, 255, // background color
        195, 195, 195, // window color
        255, 0, 0, // data color
        /* dark theme */
        60, 60, 60, // background color
        40, 40, 40, // window color
        19, 236, 48 // data color
    };
    memcpy(self.themeColors, themeCopy, sizeof(themeCopy));
    if (self.theme == 0) {
        ribbonLightTheme();
        popupLightTheme();
    } else {
        ribbonDarkTheme();
        popupDarkTheme();
    }
    *selfp = self;
}

void renderWindow(empv *selfp) {
    empv self = *selfp;
    /* render window */
    turtlePenSize(2);
    turtlePenColor(self.themeColors[self.theme + 3], self.themeColors[self.theme + 4], self.themeColors[self.theme + 5]);
    turtleGoto(self.windowCoords[0], self.windowCoords[1]);
    turtlePenDown();
    turtleGoto(self.windowCoords[0], self.windowCoords[3]);
    turtleGoto(self.windowCoords[2], self.windowCoords[3]);
    turtleGoto(self.windowCoords[2], self.windowCoords[1]);
    turtleGoto(self.windowCoords[0], self.windowCoords[1]);
    turtleRentangle(self.windowCoords[0], self.windowCoords[3], self.windowCoords[2], self.windowCoords[3] - self.windowTop, self.themeColors[self.theme + 3], self.themeColors[self.theme + 4], self.themeColors[self.theme + 5], 0);
    turtlePenUp();

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
    *selfp = self;
}

void renderData(empv* selfp) {
    empv self = *selfp;
    self.rightBound = self.data -> length;
    if (self.rightBound > self.leftBound + self.windowSize) {
        self.leftBound = self.rightBound - self.windowSize;
    }
    turtlePenSize(2);
    turtlePenColor(self.themeColors[self.theme + 6], self.themeColors[self.theme + 7], self.themeColors[self.theme + 8]);
    double xquantum = (self.windowCoords[2] - self.windowCoords[0]) / (self.rightBound - self.leftBound - 1);
    for (int i = 0; i < self.rightBound - self.leftBound; i++) {
        turtleGoto(self.windowCoords[0] + i * xquantum, self.windowCoords[1] + ((self.data -> data[self.leftBound + i].d - self.bottomBound) / (self.topBound - self.bottomBound)) * (self.windowCoords[3] - self.windowTop - self.windowCoords[1]));
        turtlePenDown();
    }
    turtlePenUp();
    *selfp = self;
}

void parseRibbonOutput(empv* selfp) {
    empv self = *selfp;
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
                    self.theme = 9;
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
    *selfp = self;
}

int randomInt(int lowerBound, int upperBound) { // random integer between lower and upper bound (inclusive)
    return (rand() % (upperBound - lowerBound + 1) + lowerBound);
}

double randomDouble(double lowerBound, double upperBound) { // random double between lower and upper bound
    return (rand() * (upperBound - lowerBound) / RAND_MAX + lowerBound); // probably works idk
}

void utilLoop(empv *selfp) {
    turtleGetMouseCoords(); // get the mouse coordinates
    if (turtle.mouseX > 320) { // bound mouse coordinates to window coordinates
        selfp -> mx = 320;
    } else {
        if (turtle.mouseX < -320) {
            selfp -> mx = -320;
        } else {
            selfp -> mx = turtle.mouseX;
        }
    }
    if (turtle.mouseY > 180) {
        selfp -> my = 180;
    } else {
        if (turtle.mouseY < -180) {
            selfp -> my = -180;
        } else {
            selfp -> my = turtle.mouseY;
        }
    }
    selfp -> mw = turtleMouseWheel();
    if (turtleKeyPressed(GLFW_KEY_UP)) {
        selfp -> mw += 1;
    }
    if (turtleKeyPressed(GLFW_KEY_DOWN)) {
        selfp -> mw -= 1;
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
    window = glfwCreateWindow(1280, 720, "EMPV", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetWindowSizeLimits(window, 128, 72, 1280, 720);

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

    empv self;
    init(&self); // initialise empv
    turtleBgColor(self.themeColors[self.theme + 0], self.themeColors[self.theme + 1], self.themeColors[self.theme + 2]);

    while (turtle.close == 0) { // main loop
        start = clock();
        double sinValue = sin(tick / 5.0) * 90;
        list_append(self.data, (unitype) sinValue, 'd');
        utilLoop(&self);
        turtleGetMouseCoords(); // get the mouse coordinates (turtle.mouseX, turtle.mouseY)
        turtleClear();
        renderData(&self);
        renderWindow(&self);
        ribbonUpdate();
        parseRibbonOutput(&self);
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