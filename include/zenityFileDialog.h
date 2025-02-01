/* This is the zenity version of win32FileDialog.h, it's for linux */

/* 
a note on zenity:
FILE* filenameStream = popen("zenity --file-selection", "r"); (popen runs the first argument in the shell as a bash script)
this function returns a pointer to a stream of data that contains only the filepath
popen with (zenity --file-selection) will not return a FILE* to the location of the file. You cannot read the file from this FILE*, you must call fopen on the filename

additionally, filters can be added with
FILE* filenameStream = popen("zenity --file-selection --file-filter='Name | *.ext *.ext2 *.ext3'", "r");
This is similar to COMDLG_FILTERSPEC struct's pszName and pszSpec, so you can add more filter "profiles" by using multiple --file-filter tags in the command
*/

#ifndef ZENITYFILE
#define ZENITYFILE 1 // include guard

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char executableFilepath[4097]; // filepath of executable
    char selectedFilename[4097]; // output filename - maximum filepath is 4096 characters (?)
    char openOrSave; // 0 - open, 1 - save
    int numExtensions; // number of extensions
    char **extensions; // array of allowed extensions (7 characters long max (cuz *.json ))
} zenityFileDialogObject;

zenityFileDialogObject zenityFileDialog;

void zenityFileDialogInit(char argv0[]) {
    /* get executable filepath */
    FILE *exStringFile = popen("pwd", "r");
    fscanf(exStringFile, "%s", zenityFileDialog.executableFilepath);
    strcat(zenityFileDialog.executableFilepath, "/");
    strcat(zenityFileDialog.executableFilepath, argv0);
    
    int index = strlen(zenityFileDialog.executableFilepath) - 1;
    while (index > -1 && zenityFileDialog.executableFilepath[index] != '/') {
        index--;
    }
    zenityFileDialog.executableFilepath[index + 1] = '\0';

    /* initialise file dialog */
    strcpy(zenityFileDialog.selectedFilename, "null");
    zenityFileDialog.openOrSave = 0; // open by default
    zenityFileDialog.numExtensions = 0; // 0 means all extensions
    zenityFileDialog.extensions = malloc(1 * sizeof(char *)); // malloc list
}

void zenityFileDialogAddExtension(char *extension) {
    if (strlen(extension) <= 4) {
        zenityFileDialog.numExtensions += 1;
        zenityFileDialog.extensions = realloc(zenityFileDialog.extensions, zenityFileDialog.numExtensions * 8);
        zenityFileDialog.extensions[zenityFileDialog.numExtensions - 1] = strdup(extension);
    } else {
        printf("extension name: %s too long\n", extension);
    }
}

int zenityFileDialogPrompt(char openOrSave, char *prename) { // 0 - open, 1 - save, prename refers to autofill filename ("null" or empty string for no autofill)
    char fullCommand[23 + 13 + 256 + 15 + 34 + 7 * zenityFileDialog.numExtensions + 14 + 1]; // 23 for zenity --file-selection, 13 for --filename=', 256 for prename, 15 for --title='Open', 34 for --file-filter='Specified Types | , 7 for each extension, 14 for title, 1 for \0
    strcpy(fullCommand, "zenity --file-selection");
    /* configure autofill filename */
    if (openOrSave == 1 && strcmp(prename, "null") != 0) {
        strcat(fullCommand, " --filename='");
        strcat(fullCommand, prename);
        strcat(fullCommand, "'");
    }

    /* configure title */
    char title[16] = " --title='Open'";
    if (openOrSave == 1) {
        strcpy(title, " --title='Save'");
    }
    strcat(fullCommand, title);

    /* configure extensions */
    if (zenityFileDialog.numExtensions > 0) {
        char buildFilter[7 * zenityFileDialog.numExtensions + 1]; // last space is replaced with ' and followed by \0
        int j = 0;
        for (int i = 0; i < zenityFileDialog.numExtensions; i++) {
            buildFilter[j] = '*';
            buildFilter[j + 1] = '.';
            j += 2;
            for (int k = 0; k < strlen(zenityFileDialog.extensions[i]) && k < 8; k++) {
                buildFilter[j] = zenityFileDialog.extensions[i][k];
                j += 1;
            }
            if (i != zenityFileDialog.numExtensions - 1) { // dont add space if it's the last element
                buildFilter[j] = ' ';
                j += 1;
            }
        }
        buildFilter[j] = '\'';
        buildFilter[j + 1] = '\0';
        char filterName[35] = " --file-filter='Specified Types | ";
        strcat(fullCommand, filterName);
        strcat(fullCommand, buildFilter); // really glad that C is such a good language for string manipulation /s
    }

    /* execute */
    // printf("%s\n", fullCommand);
    FILE* filenameStream = popen(fullCommand, "r");
    if (fgets(zenityFileDialog.selectedFilename, 4097, filenameStream) == NULL) { // adds a \n before \0 (?)
        // printf("Error: fgets\n");
        strcpy(zenityFileDialog.selectedFilename, "null");
        return -1;
    }
    for (int i = 0; i < 4096; i++) {
        if (zenityFileDialog.selectedFilename[i] == '\n') {
            zenityFileDialog.selectedFilename[i] = '\0'; // replace all newlines with null characters
        }
    }
    // printf("Success, filename: %s\n", zenityFileDialog.filename);
    pclose(filenameStream);
    return 0;
}
#endif