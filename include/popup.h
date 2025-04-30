/* A generic popup box rendered with openGL */

#ifndef POPUPSET
#define POPUPSET 1 // include guard
#include "textGL.h"

typedef struct { // popup variables
    char *message; // message displayed on the popup
    double minX; // left edge of box
    double minY; // bottom of box
    double maxX; // right edge of box
    double maxY; // top of box
    list_t *options; // list of popup box options
    /*
    style:
    0 - default
    1 to 50 - rounded rectangle (not implemented)
    51 - triangle (wacky)
    52 - gaussian integral
    */
    signed char style;
    signed char output[2]; // [toggle, select]
    signed char mouseDown;
    double colors[12]; // (0, 1, 2) - popup colour, (3, 4, 5) - boxes colour, (6, 7, 8) - boxes highlight colour, (9, 10, 11) - text colour
} popup_t;

popup_t popup;

int popupInit(const char *filename, double minX, double minY, double maxX, double maxY) {
    popup.minX = minX;
    popup.minY = minY;
    popup.maxX = maxX;
    popup.maxY = maxY;
    popup.output[0] = 0;
    popup.output[1] = -1;
    popup.mouseDown = 0;
    /* default colours */
    popup.colors[0] = 200.0;
    popup.colors[1] = 200.0;
    popup.colors[2] = 200.0;
    popup.colors[3] = 140.0;
    popup.colors[4] = 140.0;
    popup.colors[5] = 140.0;
    popup.colors[6] = 100.0;
    popup.colors[7] = 100.0;
    popup.colors[8] = 100.0;
    popup.colors[9] = 0.0;
    popup.colors[10] = 0.0;
    popup.colors[11] = 0.0;
    popup.style = 0;
    // read information from config file
    FILE *configFile = fopen(filename, "r");
    if (configFile == NULL) {
        printf("Error: file %s not found\n", filename);
        return -1;
    }
    char throw[256] = {1, 0}; // maximum size of message or option
    char *checksum = fgets(throw, 256, configFile); // read message
    throw[strlen(throw) - 1] = '\0'; // cull newline
    popup.message = strdup(throw);
    popup.options = list_init();
    while (checksum != NULL) {
        checksum = fgets(throw, 256, configFile);
        if (checksum != NULL) {
            if (throw[strlen(throw) - 1] == '\n') {
                throw[strlen(throw) - 1] = '\0'; // cull newline
            }
            list_append(popup.options, (unitype) strdup(throw), 's');
        }
    }
    return 0;
}

void popupLightTheme() { // light theme preset (default)
    popup.colors[0] = 200.0;
    popup.colors[1] = 200.0;
    popup.colors[2] = 200.0;
    popup.colors[3] = 140.0;
    popup.colors[4] = 140.0;
    popup.colors[5] = 140.0;
    popup.colors[6] = 100.0;
    popup.colors[7] = 100.0;
    popup.colors[8] = 100.0;
    popup.colors[9] = 0.0;
    popup.colors[10] = 0.0;
    popup.colors[11] = 0.0;
}

void popupDarkTheme() { // dark theme preset
    popup.colors[0] = 10.0; // popup colour
    popup.colors[1] = 10.0;
    popup.colors[2] = 10.0;
    popup.colors[3] = 40.0; // boxes colour
    popup.colors[4] = 40.0;
    popup.colors[5] = 40.0;
    popup.colors[6] = 60.0; // highlight color
    popup.colors[7] = 60.0;
    popup.colors[8] = 60.0;
    popup.colors[9] = 160.0; // text color
    popup.colors[10] = 160.0;
    popup.colors[11] = 160.0;
}

void popupUpdate() {
    turtleQuad(popup.minX, popup.minY, popup.minX, popup.maxY, 
    popup.maxX, popup.maxY, popup.maxX, popup.minY, popup.colors[0], popup.colors[1], popup.colors[2], 0);
    double textSize = 5;
    double textX = popup.minX + (popup.maxX - popup.minX) / 2;
    double textY = popup.maxY - textSize * 2;
    turtlePenColor(popup.colors[9], popup.colors[10], popup.colors[11]);
    textGLWriteString(popup.message, textX, textY, textSize, 50);
    textY -= textSize * 4;
    double fullLength = 0;
    for (int i = 0; i < popup.options -> length; i++) {
        fullLength += textGLGetStringLength(popup.options -> data[i].s, textSize);
    }
    // we have the length of the strings, now we pad with n + 1 padding regions
    double padThai = (popup.maxX - popup.minX - fullLength) / (popup.options -> length + 1);
    textX = popup.minX + padThai;
    char flagged = 0;
    if (!turtleMouseDown() && popup.mouseDown == 1) {
        flagged = 1; // flagged for mouse misbehaviour
    }
    for (int i = 0; i < popup.options -> length; i++) {
        double strLen = textGLGetStringLength(popup.options -> data[i].s, textSize);
        if (turtle.mouseX > textX - textSize && turtle.mouseX < textX + strLen + textSize &&
        turtle.mouseY > textY - textSize && turtle.mouseY < textY + textSize) {
            turtleQuad(textX - textSize, textY - textSize, textX + textSize + strLen, textY - textSize, 
            textX + textSize + strLen, textY + textSize, textX - textSize, textY + textSize, popup.colors[6], popup.colors[7], popup.colors[8], 0);
            if (turtleMouseDown()) {
                if (popup.mouseDown == 0) {
                    popup.mouseDown = 1;
                    if (popup.output[0] == 0) {
                        popup.output[1] = i;
                    }
                }
            } else {
                if (popup.mouseDown == 1) {
                    popup.mouseDown = 0;
                    if (popup.output[1] == i) {
                        popup.output[0] = 1;
                    }
                }
            }
        } else {
            turtleQuad(textX - textSize, textY - textSize, textX + textSize + strLen, textY - textSize, 
            textX + textSize + strLen, textY + textSize, textX - textSize, textY + textSize, popup.colors[3], popup.colors[4], popup.colors[5], 0);
        }
        textGLWriteString(popup.options -> data[i].s, textX, textY, textSize, 0);
        textX += strLen + padThai;
    }
    if (!turtleMouseDown() && popup.mouseDown == 1 && flagged == 1) {
        popup.mouseDown = 0;
        popup.output[0] = 0;
        popup.output[1] = -1;
    }
}

void popupFree() {
    free(popup.message);
}
#endif