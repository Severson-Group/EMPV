/* textGL uses openGL to render text on the screen */

#ifndef TEXTGLSET
#define TEXTGLSET 1 // include guard
#include "turtle.h"

typedef struct { // textGL variables
    int bezierPrez; // precision for bezier curves
    int charCount; // number of supported characters
    unsigned int *supportedCharReference; // array containing links from (int) unicode values of characters to an index from 0 to (charCount - 1)
    int *fontPointer; // array containing links from char indices (0 to (charCount - 1)) to their corresponding data position in fontData
    int *fontData; // array containing packaged instructions on how to draw each character in the character set
} textGL;

textGL textGLRender;

int textGLInit(GLFWwindow* window, const char *filename) { // initialise values, must supply a font file (tgl)
    turtlePenColor(0, 0, 0);
    textGLRender.bezierPrez = 10;

    /* load file */
    FILE *tgl = fopen(filename, "r");
    if (tgl == NULL) {
        printf("Error: could not open file %s\n", filename);
        return -1;
    }

    list_t *fontDataInit = list_init(); // these start as lists and become int arrays
    list_t *fontPointerInit = list_init();
    list_t *supportedCharReferenceInit = list_init();
    unsigned char parsedInt[12]; // max possible length of an int as a string is 11
    unsigned int fontPar;
    int oldptr;
    int strptr;

    char line[2048]; // maximum line length
    line[2047] = 0; // canary value
    textGLRender.charCount = 0;
    while (fgets(line, 2048, tgl) != NULL) { // fgets to prevent overflows
        if (line[2047] != 0) {
            printf("Error: line %d in file %s exceeded appropriate length\n", 0, filename);
            return -1;
        }

        oldptr = 0;
        strptr = 0;
        int loops = 0;
        int firstIndex = 0;
        
        while (line[strptr] != '\n' && line[strptr] != '\0') {
            while (line[strptr] != ',' && line[strptr] != '\n' && line[strptr] != '\0') {
                parsedInt[strptr - oldptr] = line[strptr];
                strptr += 1;
            }
            parsedInt[strptr - oldptr] = '\0';
            if (oldptr == 0) {
                fontPar = 0; // parse as unicode char (basically just take the multibyte utf-8 encoding of the character and literal cast it to an unsigned int (maximum of 4 bytes per character in utf-8 (?)))
                if (strlen((char *) parsedInt) > 4) {
                    printf("Error: character at line %d too long for unsigned int\n", supportedCharReferenceInit -> length + 1);
                }
                for (int i = strlen((char *) parsedInt); i > 0; i--) {
                    int abri = (strlen((char *) parsedInt) - i);
                    fontPar += (unsigned int) parsedInt[abri] << ((i - 1) * 8);
                }
                if (fontPar == 0) { // exception for the comma character
                    fontPar = 44;
                }
                list_append(supportedCharReferenceInit, (unitype) (int) fontPar, 'i'); // adds as an int but will typecast back to unsigned later, this might not work correctly but it also doesn't really matter
                list_append(fontPointerInit, (unitype) (int) (fontDataInit -> length), 'i');
                firstIndex = fontDataInit -> length;
                list_append(fontDataInit, (unitype) 1, 'i');
            } else {
                sscanf((char *) parsedInt, "%u", &fontPar); // parse as integer
                if (strcmp((char *) parsedInt, "b") == 0) {
                    list_append(fontDataInit, (unitype) 140894115, 'i'); // all b's get converted to the integer 140894115 (chosen semi-randomly)
                } else {
                    list_append(fontDataInit, (unitype) (int) (fontPar), 'i'); // fontPar will double count when it encounters a b (idk why) so if there's a b we ignore the second fontPar (which is a duplicate of the previous one)
                }
                loops += 1;
            }
            if (line[strptr] != '\n' && line[strptr] != '\0')
                strptr += 2;
            oldptr = strptr;
        }
        fontDataInit -> data[firstIndex] = (unitype) loops;
        firstIndex += 1; // using firstIndex as iteration variable
        int len1 = fontDataInit -> data[firstIndex].i;
        int maximums[4] = {-2147483648, -2147483648, 2147483647, 2147483647}; // for describing bounding box of a character
        
        /* good programmng alert*/
        #define CHECKS_EMB(ind) \
            if (fontDataInit -> data[ind].i > maximums[0]) {\
                maximums[0] = fontDataInit -> data[ind].i;\
            }\
            if (fontDataInit -> data[ind].i < maximums[3]) {\
                maximums[3] = fontDataInit -> data[ind].i;\
            }\
            if (fontDataInit -> data[ind + 1].i > maximums[1]) {\
                maximums[1] = fontDataInit -> data[ind + 1].i;\
            }\
            if (fontDataInit -> data[ind + 1].i < maximums[3]) {\
                maximums[3] = fontDataInit -> data[ind + 1].i;\
            }
        for (int i = 0; i < len1; i++) {
            firstIndex += 1;
            int len2 = fontDataInit -> data[firstIndex].i;
            for (int j = 0; j < len2; j++) {
                firstIndex += 1;
                if (fontDataInit -> data[firstIndex].i == 140894115) {
                    firstIndex += 1;
                    fontDataInit -> data[firstIndex] = (unitype) (fontDataInit -> data[firstIndex].i + 160);
                    fontDataInit -> data[firstIndex + 1] = (unitype) (fontDataInit -> data[firstIndex + 1].i + 100);
                    CHECKS_EMB(firstIndex);
                    firstIndex += 2;
                    fontDataInit -> data[firstIndex] = (unitype) (fontDataInit -> data[firstIndex].i + 160);
                    fontDataInit -> data[firstIndex + 1] = (unitype) (fontDataInit -> data[firstIndex + 1].i + 100);
                    CHECKS_EMB(firstIndex);
                    firstIndex += 1;
                    if (fontDataInit -> data[firstIndex + 1].i != 140894115) {
                        firstIndex += 1;
                        fontDataInit -> data[firstIndex] = (unitype) (fontDataInit -> data[firstIndex].i + 160);
                        fontDataInit -> data[firstIndex + 1] = (unitype) (fontDataInit -> data[firstIndex + 1].i + 100);
                        CHECKS_EMB(firstIndex);
                        firstIndex += 1;
                    }
                } else {
                    fontDataInit -> data[firstIndex] = (unitype) (fontDataInit -> data[firstIndex].i + 160);
                    fontDataInit -> data[firstIndex + 1] = (unitype) (fontDataInit -> data[firstIndex + 1].i + 100);
                    CHECKS_EMB(firstIndex);
                    firstIndex += 1;
                }
            }
        }
        if (maximums[0] < 0) {
            list_append(fontDataInit, (unitype) 90, 'i');
        } else {
            list_append(fontDataInit, (unitype) maximums[0], 'i');
        }
        if (maximums[3] > 0) {
            list_append(fontDataInit, (unitype) 0, 'i');
        } else {
            list_append(fontDataInit, (unitype) maximums[3], 'i');
        }
        if (maximums[1] < 0) {
            if (textGLRender.charCount == 0)
                list_append(fontDataInit, (unitype) 0, 'i');
            else
                list_append(fontDataInit, (unitype) 120, 'i');
        } else {
            list_append(fontDataInit, (unitype) maximums[1], 'i');
        }
        if (maximums[2] > 0) {
            list_append(fontDataInit, (unitype) 0, 'i');
        } else {
            list_append(fontDataInit, (unitype) maximums[2], 'i');
        }
        textGLRender.charCount += 1;
    }
    list_append(fontPointerInit, (unitype) (int) (fontDataInit -> length), 'i'); // last pointer
    textGLRender.fontData = malloc(sizeof(int) * fontDataInit -> length); // convert lists to arrays (could be optimised cuz we already have the -> data arrays but who really cares this runs once)
    for (int i = 0; i < fontDataInit -> length; i++) {
        textGLRender.fontData[i] = fontDataInit -> data[i].i;
    }
    textGLRender.fontPointer = malloc(sizeof(int) * fontPointerInit -> length);
    for (int i = 0; i < fontPointerInit -> length; i++) {
        textGLRender.fontPointer[i] = fontPointerInit -> data[i].i;
    }
    textGLRender.supportedCharReference = malloc(sizeof(int) * supportedCharReferenceInit -> length);
    for (int i = 0; i < supportedCharReferenceInit -> length; i++) {
        textGLRender.supportedCharReference[i] = supportedCharReferenceInit -> data[i].i;
    }

    printf("%d characters loaded from %s\n", textGLRender.charCount, filename);

    list_free(fontDataInit);
    list_free(fontPointerInit);
    list_free(supportedCharReferenceInit);
    return 0;
}

/* render functions */

void renderBezier(double x1, double y1, double x2, double y2, double x3, double y3, int prez) { // renders a quadratic bezier curve on the screen
    turtleGoto(x1, y1);
    turtlePenDown();
    double iter1 = 1;
    double iter2 = 0;
    for (int i = 0; i < prez; i++) {
        iter1 -= (double) 1 / prez;
        iter2 += (double) 1 / prez;
        double t1 = iter1 * iter1;
        double t2 = iter2 * iter2;
        double t3 = 2 * iter1 * iter2;
        turtleGoto(t1 * x1 + t3 * x2 + t2 * x3, t1 * y1 + t3 * y2 + t2 * y3);
    }
    turtleGoto(x3, y3);
}

void renderChar(int index, double x, double y, double size) { // renders a single character
    index += 1;
    int len1 = textGLRender.fontData[index];
    for (int i = 0; i < len1; i++) {
        index += 1;
        if (turtle.pen == 1)
            turtlePenUp();
        int len2 = textGLRender.fontData[index];
        for (int j = 0; j < len2; j++) {
            index += 1;
            if (textGLRender.fontData[index] == 140894115) { // 140894115 is the b value (reserved)
                index += 4;
                if (textGLRender.fontData[index + 1] != 140894115) {
                    renderBezier(x + textGLRender.fontData[index - 3] * size, y + textGLRender.fontData[index - 2] * size, x + textGLRender.fontData[index - 1] * size, y + textGLRender.fontData[index] * size, x + textGLRender.fontData[index + 1] * size, y + textGLRender.fontData[index + 2] * size, textGLRender.bezierPrez);
                    index += 2;
                } else {
                    renderBezier(x + textGLRender.fontData[index - 3] * size, y + textGLRender.fontData[index - 2] * size, x + textGLRender.fontData[index - 1] * size, y + textGLRender.fontData[index] * size, x + textGLRender.fontData[index + 2] * size, y + textGLRender.fontData[index + 3] * size, textGLRender.bezierPrez);
                }
            } else {
                index += 1;
                turtleGoto(x + textGLRender.fontData[index - 1] * size, y + textGLRender.fontData[index] * size);
            }
            turtlePenDown();
        }
    }
    turtlePenUp();
    // no variables in textGLRender are changed
}
// gets the length of a string in pixels on the screen
double textGLGetLength(const unsigned int *text, int textLength, double size) {
    size /= 175;
    double xTrack = 0;
    for (int i = 0; i < textLength; i++) {
        int currentDataAddress = 0;
        for (int j = 0; j < textGLRender.charCount; j++) { // change to hashmap later
            if (textGLRender.supportedCharReference[j] == text[i]) {
                currentDataAddress = j;
                break;
            }
        }
        xTrack += (textGLRender.fontData[textGLRender.fontPointer[currentDataAddress + 1] - 4] + 40) * size;
    }
    xTrack -= 40 * size;
    return xTrack;
}
// gets the length of a string in pixels on the screen
double textGLGetStringLength(const char *str, double size) {
    int len = strlen(str);
    unsigned int converted[len];
    for (int i = 0; i < len; i++) {
        converted[i] = (unsigned int) str[i];
    }
    return textGLGetLength(converted, len, size);
}

double textGLGetUnicodeLength(const char *str, int textLength, double size) { // gets the length of a u-string in pixels on the screen
    int len = strlen((char *) str);
    unsigned int converted[len]; // max number of characters in a utf-8 string of length len
    int byteLength;
    int i = 0;
    int next = 0;
    while (i < len) {
        byteLength = 0;
        for (int j = 0; j < 8; j++) {
            unsigned char mask = 128 >> j;
            if (str[i] & mask) {
                byteLength += 1;
            } else {
                j = 8; // end loop
            }
        }
        if (byteLength == 0) { // case: ASCII character
            converted[next] = (unsigned int) str[i];
            byteLength = 1;
        } else { // case: multi-byte character
            unsigned int convert = 0;
            for (int k = 0; k < byteLength; k++) {
                convert = convert << 8;
                convert += (unsigned int) str[i + k];
            }
            converted[next] = convert;
        }
        i += byteLength;
        next += 1;
    }
    return textGLGetLength(converted, len, size);
}

void textGLWrite(const unsigned int *text, int textLength, double x, double y, double size, double align) { // writes to the screen
    char saveShape = turtle.penshape;
    double saveSize = turtle.pensize;
    textGLRender.bezierPrez = (int) ceil(sqrt(size * 1)); // change the 1 for higher or lower bezier precision
    double xTrack = x;
    size /= 175;
    y -= size * 70;
    turtlePenSize(20 * size);
    // turtlePenShape("connected"); // fast
    // turtlePenShape("circle"); // pretty
    turtlePenShape("text"); // dedicated setting that blends circle and connected
    list_t* xvals = list_init();
    list_t* dataIndStored = list_init();
    for (int i = 0; i < textLength; i++) {
        int currentDataAddress = 0;
        for (int j = 0; j < textGLRender.charCount; j++) { // change to hashmap later
            if (textGLRender.supportedCharReference[j] == text[i]) {
                currentDataAddress = j;
                break;
            }
        }
        list_append(xvals, (unitype) xTrack, 'd');
        list_append(dataIndStored, (unitype) currentDataAddress, 'i');
        xTrack += (textGLRender.fontData[textGLRender.fontPointer[currentDataAddress + 1] - 4] + 40) * size;
    }
    xTrack -= 40 * size;
    for (int i = 0; i < textLength; i++) {
        renderChar((double) textGLRender.fontPointer[dataIndStored -> data[i].i], xvals -> data[i].d - ((xTrack - x) * (align / 100)), y, size);
    }
    list_free(dataIndStored);
    list_free(xvals);
    turtle.penshape = saveShape; // restore the shape and size before the write
    turtle.pensize = saveSize;
    // no variables in textGLRender are changed
}

void textGLWriteString(const char *str, double x, double y, double size, double align) { // wrapper function for writing strings easier
    int len = strlen(str);
    unsigned int converted[len];
    for (int i = 0; i < len; i++) {
        converted[i] = (unsigned int) str[i];
    }
    textGLWrite(converted, len, x, y, size, align);
}

void textGLWriteUnicode(const unsigned char *str, double x, double y, double size, double align) { // wrapper function for unicode strings (UTF-8, u8"Hello World")
    int len = strlen((char *) str);
    unsigned int converted[len]; // max number of characters in a utf-8 string of length len
    int byteLength;
    int i = 0;
    int next = 0;
    while (i < len) {
        byteLength = 0;
        for (int j = 0; j < 8; j++) {
            unsigned char mask = 128 >> j;
            if (str[i] & mask) {
                byteLength += 1;
            } else {
                j = 8; // end loop
            }
        }
        if (byteLength == 0) { // case: ASCII character
            converted[next] = (unsigned int) str[i];
            byteLength = 1;
        } else { // case: multi-byte character
            unsigned int convert = 0;
            for (int k = 0; k < byteLength; k++) {
                convert = convert << 8;
                convert += (unsigned int) str[i + k];
            }
            converted[next] = convert;
        }
        i += byteLength;
        next += 1;
    }
    textGLWrite(converted, next, x, y, size, align);
}
#endif