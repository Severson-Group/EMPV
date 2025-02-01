/* customisable top bar */

#ifndef RIBBONSET
#define RIBBONSET 1 // include guard
#include "textGL.h"

typedef struct {
    unsigned char marginSize;
    signed char mainselect[4]; // 0 - select, 1 - mouseHover, 2 - selected, 3 - premove close dropdown
    signed char subselect[4]; // 0 - select, 1 - mouseHover, 2 - selected, 3 - free
    signed char output[3]; // 0 - toggle, 1 - mainselect, 2 - subselect
    signed char mouseDown; // keeps track of previous frame mouse information
    int bounds[4]; // list of coordinate bounds (minX, minY, maxX, maxY)
    double ribbonSize;
    double colors[12]; // (0, 1, 2) - ribbon colour, (3, 4, 5) - ribbon highlight & dropdown colour, (6, 7, 8) - dropdown highlight colour, (9, 10, 11) - text colour
    list_t *options;
    list_t* lengths;
} ribbon;

ribbon ribbonRender;

int ribbonInit(GLFWwindow* window, const char *filename) { // read from config file
    ribbonRender.marginSize = 10; // number of pixels between different items in the ribbon (not affected by ribbonSize)
    ribbonRender.mainselect[0] = -1;
    ribbonRender.mainselect[1] = -1;
    ribbonRender.mainselect[2] = -1;
    ribbonRender.subselect[0] = -1;
    ribbonRender.subselect[1] = -1;
    ribbonRender.subselect[2] = -1;
    ribbonRender.output[0] = 0;
    ribbonRender.output[1] = -1;
    ribbonRender.output[2] = -1;

    ribbonRender.mouseDown = 0;

    ribbonRender.bounds[0] = turtle.bounds[0];
    ribbonRender.bounds[1] = turtle.bounds[1];
    ribbonRender.bounds[2] = turtle.bounds[2];
    ribbonRender.bounds[3] = turtle.bounds[3];
    /* default colours */
    ribbonRender.colors[0] = 200.0;
    ribbonRender.colors[1] = 200.0;
    ribbonRender.colors[2] = 200.0;
    ribbonRender.colors[3] = 140.0;
    ribbonRender.colors[4] = 140.0;
    ribbonRender.colors[5] = 140.0;
    ribbonRender.colors[6] = 100.0;
    ribbonRender.colors[7] = 100.0;
    ribbonRender.colors[8] = 100.0;
    ribbonRender.colors[9] = 0.0;
    ribbonRender.colors[10] = 0.0;
    ribbonRender.colors[11] = 0.0;

    ribbonRender.ribbonSize = 1; // 1 is default, below 1 is smaller, above 1 is larger (scales as a multiplier, 0.1 is 100x smaller than 10)
    ribbonRender.options = list_init();
    ribbonRender.lengths = list_init();

    FILE* configFile = fopen(filename, "r"); // load from config file
    if (configFile == NULL) {
        printf("Error: file %s not found\n", filename);
        return -1;
    }
    list_t* sublist = list_init();
    int checksum = 0;
    char throw[256]; // maximum size of any option or sub-option (characters)
    int j = 0;
    list_clear(sublist);
    while (checksum != EOF) {
        checksum = fscanf(configFile, "%[^,\n]%*c,", throw);
        char whitespace = 0;
        if (throw[0] == ' ') {
            whitespace += 1;
        } else {
            if (j != 0) {
                list_t* appendList = list_init();
                list_copy(sublist, appendList);
                list_clear(sublist);
                list_append(ribbonRender.options, (unitype) appendList, 'r');
            }
        }
        list_append(sublist, (unitype) (throw + whitespace), 's');
        j++;
    }
    list_pop(sublist);
    list_t* appendList = list_init();
    list_copy(sublist, appendList);
    list_clear(sublist);
    list_append(ribbonRender.options, (unitype) appendList, 'r');
    list_free(sublist);
    fclose(configFile);

    for (int i = 0; i < ribbonRender.options -> length; i++) {
        list_append(ribbonRender.lengths, (unitype) textGLGetStringLength(ribbonRender.options -> data[i].r -> data[0].s, 7 * ribbonRender.ribbonSize), 'd');
        double max = 0;
        for (int j = 1; j < ribbonRender.options -> data[i].r -> length; j++) {
            double cur = textGLGetStringLength(ribbonRender.options -> data[i].r -> data[j].s, 7 * ribbonRender.ribbonSize);
            if (cur > max) {
                max = cur;
            }
        }
        list_append(ribbonRender.lengths, (unitype) max, 'd');
    }
    return 0;
}

void ribbonLightTheme() { // light theme preset (default)
    ribbonRender.colors[0] = 200.0;
    ribbonRender.colors[1] = 200.0;
    ribbonRender.colors[2] = 200.0;
    ribbonRender.colors[3] = 140.0;
    ribbonRender.colors[4] = 140.0;
    ribbonRender.colors[5] = 140.0;
    ribbonRender.colors[6] = 100.0;
    ribbonRender.colors[7] = 100.0;
    ribbonRender.colors[8] = 100.0;
    ribbonRender.colors[9] = 0.0;
    ribbonRender.colors[10] = 0.0;
    ribbonRender.colors[11] = 0.0;
}

void ribbonDarkTheme() { // dark theme preset
    ribbonRender.colors[0] = 70.0; // top bar color
    ribbonRender.colors[1] = 70.0;
    ribbonRender.colors[2] = 70.0;
    ribbonRender.colors[3] = 80.0; // dropdown color
    ribbonRender.colors[4] = 80.0;
    ribbonRender.colors[5] = 80.0;
    ribbonRender.colors[6] = 70.0; // select color
    ribbonRender.colors[7] = 70.0;
    ribbonRender.colors[8] = 70.0;
    ribbonRender.colors[9] = 160.0; // text color
    ribbonRender.colors[10] = 160.0;
    ribbonRender.colors[11] = 160.0;
}

void ribbonUpdate() {
    char shapeSave = turtle.penshape;
    double sizeSave = turtle.pensize;
    turtlePenSize(20);
    turtlePenShape("square");
    turtleGetMouseCoords(); // get the mouse coordinates (turtle.mouseX, turtle.mouseY)
    turtleQuad(ribbonRender.bounds[0], ribbonRender.bounds[3] - 10, ribbonRender.bounds[2], ribbonRender.bounds[3] - 10, ribbonRender.bounds[2], ribbonRender.bounds[3], ribbonRender.bounds[0], ribbonRender.bounds[3], ribbonRender.colors[0], ribbonRender.colors[1], ribbonRender.colors[2], 50.0); // render ribbon
    turtlePenColor(ribbonRender.colors[9], ribbonRender.colors[10], ribbonRender.colors[11]); // text colour
    double cutoff = ribbonRender.bounds[0] + ribbonRender.marginSize;
    ribbonRender.mainselect[0] = -1;
    ribbonRender.subselect[0] = -1;
    for (int i = 0; i < ribbonRender.options -> length; i++) {
        double prevCutoff = cutoff;
        if (i == ribbonRender.mainselect[2]) {
            double xLeft = prevCutoff - ribbonRender.marginSize / 2.0;
            double xRight = prevCutoff + ribbonRender.lengths -> data[i * 2 + 1].d + ribbonRender.marginSize / 2.0;
            double yDown = ribbonRender.bounds[3] - 10 - 15 * (ribbonRender.options -> data[i].r -> length - 1) - ribbonRender.marginSize / 2.0;
            turtleQuad(xLeft, ribbonRender.bounds[3] - 10, xRight, ribbonRender.bounds[3] - 10, xRight, yDown, xLeft, yDown, ribbonRender.colors[3], ribbonRender.colors[4], ribbonRender.colors[5], 0.0); // ribbon highlight
            for (int j = 1; j < ribbonRender.options -> data[i].r -> length; j++) {
                if (turtle.mouseY > ribbonRender.bounds[3] - 10 - 15 * j - ribbonRender.marginSize / 4.0 && turtle.mouseY < ribbonRender.bounds[3] - 10 && turtle.mouseX > xLeft && turtle.mouseX < xRight && ribbonRender.subselect[0] == -1) {
                    turtleQuad(xLeft, ribbonRender.bounds[3] - 10 - 15 * (j - 1) - ribbonRender.marginSize / 4.0, xRight, ribbonRender.bounds[3] - 10 - 15 * (j - 1) - ribbonRender.marginSize / 4.0, xRight, ribbonRender.bounds[3] - 10 - 15 * j - ribbonRender.marginSize / 3.0, xLeft, ribbonRender.bounds[3] - 10 - 15 * j - ribbonRender.marginSize / 3.0, ribbonRender.colors[6], ribbonRender.colors[7], ribbonRender.colors[8], 0.0); // dropdown highlight
                    ribbonRender.subselect[0] = j;
                }
                textGLWriteString(ribbonRender.options -> data[i].r -> data[j].s, prevCutoff, 174.5 - j * 15, 7 * ribbonRender.ribbonSize, 0);
            }
        }
        cutoff += ribbonRender.lengths -> data[i * 2].d + ribbonRender.marginSize;
        if (turtle.mouseY > ribbonRender.bounds[3] - 10 && turtle.mouseY < ribbonRender.bounds[3] && turtle.mouseX > ribbonRender.bounds[0] + ribbonRender.marginSize / 2.0 && turtle.mouseX < cutoff - ribbonRender.marginSize / 2.0 && ribbonRender.mainselect[0] == -1) { // -217, -195, -164
            turtleQuad(prevCutoff - ribbonRender.marginSize / 2.0, 179, cutoff - ribbonRender.marginSize / 2.0, 179, cutoff - ribbonRender.marginSize / 2.0, 171, prevCutoff - ribbonRender.marginSize / 2.0, 171, ribbonRender.colors[3], ribbonRender.colors[4], ribbonRender.colors[5], 0.0); // render dropdown
            ribbonRender.mainselect[0] = i;
        }
        textGLWriteString(ribbonRender.options -> data[i].r -> data[0].s, prevCutoff, 174.5, 7 * ribbonRender.ribbonSize, 0);
    }
    if (turtleMouseDown()) { // this is hideous
        if (ribbonRender.mouseDown == 0) {
            ribbonRender.mouseDown = 1;
            if (ribbonRender.subselect[0] == ribbonRender.subselect[1] && ribbonRender.subselect[0] != -1) {
                ribbonRender.subselect[2] = ribbonRender.subselect[0];
                ribbonRender.output[0] = 1;
                ribbonRender.output[1] = ribbonRender.mainselect[2];
                ribbonRender.output[2] = ribbonRender.subselect[2];
            }
            if (ribbonRender.mainselect[0] == ribbonRender.mainselect[1]) {
                if (ribbonRender.mainselect[0] == ribbonRender.mainselect[2]) {
                    ribbonRender.mainselect[3] = -1;
                } else {
                    ribbonRender.mainselect[2] = ribbonRender.mainselect[0];
                }
            }
        }
    } else {
        if (ribbonRender.mouseDown == 1) {
            if (ribbonRender.subselect[0] != -1) {
                ribbonRender.subselect[2] = ribbonRender.subselect[0];
                ribbonRender.output[0] = 1;
                ribbonRender.output[1] = ribbonRender.mainselect[2];
                ribbonRender.output[2] = ribbonRender.subselect[2];
                ribbonRender.mainselect[2] = -1;
                ribbonRender.subselect[2] = -1;
            }
        }
        if (ribbonRender.mainselect[3] == -1 && ribbonRender.mainselect[0] == ribbonRender.mainselect[2]) {
            ribbonRender.mainselect[2] = -1;
        }
        ribbonRender.mainselect[3] = 0;
        ribbonRender.mouseDown = 0;
        ribbonRender.mainselect[1] = ribbonRender.mainselect[0];
        ribbonRender.subselect[1] = ribbonRender.subselect[0];
    }
    turtle.penshape = shapeSave;
    turtle.pensize = sizeSave;
}
#endif