/* 
a note on COM objects:
COM objects are C++ classes/structs
This means that it has methods
This is simulated in C via lpVtbl (vtable) which is an array of function pointers
Use the lpVtbl member of a struct to call methods via STRUCTNAME -> lpVtbl -> METHODNAME(args)
https://www.codeproject.com/Articles/13601/COM-in-plain-C

Under the hood, the pointer to the struct is also a pointer to an array of function pointers (lbVtbl)
The struct is therefore the size of the number of elements of the lbVtbl array * 8 plus the data in the struct which succeeds it

Whenever we call one of these methods, we have to pass in the object (which implicitly happens in OOP languages)
Actually, we pass in a pointer to the object, obviously i'm quite familiar with this

One more nuance is that whenever we pass a COM object in a function as an argument, it must always be &object
This is because in order to call methods we use object -> lpVtbl -> method, I mean we could use object.lpVtbl -> method but it's easier and allocates less stack memory to just use pointers

So whenever you take C++ COM object sample code, just follow this process:
change all the methods to -> lpVtbl -> methods
Add &obj as the first argument of every method
Change obj to &obj for all objects passed as arguments to functions or methods

That's it! (probably)

IFileDialog: https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ifiledialog
Clipboard: https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard
*/

#ifndef WIN32FILE
#define WIN32FILE 1 // include guard

#include <windows.h>
#include <shobjidl.h>

typedef struct {
    char executableFilepath[MAX_PATH + 1]; // filepath of executable
    char selectedFilename[MAX_PATH + 1]; // output filename - maximum filepath is 260 characters (?)
    char openOrSave; // 0 - open, 1 - save
    int numExtensions; // number of extensions
    char **extensions; // array of allowed extensions (7 characters long max (cuz *.json;))
} win32FileDialogObject; // almost as bad of naming as the windows API

typedef struct {
    char *text; // clipboard text data (heap allocated)
} win32ClipboardObject;

win32FileDialogObject win32FileDialog;
win32ClipboardObject win32Clipboard;

int win32ToolsInit() {
    /* get executable filepath */
    GetModuleFileNameA(NULL, win32FileDialog.executableFilepath, MAX_PATH);
    if (GetLastError() != ERROR_SUCCESS) {
        strcpy(win32FileDialog.executableFilepath, "null");
        printf("error: could not retrieve executable filepath\n");
    }
    int index = strlen(win32FileDialog.executableFilepath) - 1;
    while (index > -1 && win32FileDialog.executableFilepath[index] != '\\' && win32FileDialog.executableFilepath[index] != '/') {
        index--;
    }
    win32FileDialog.executableFilepath[index + 1] = '\0';
    /* initialise file dialog */
    strcpy(win32FileDialog.selectedFilename, "null");
    win32FileDialog.openOrSave = 0; // open by default
    win32FileDialog.numExtensions = 0; // 0 means all extensions
    win32FileDialog.extensions = malloc(1 * sizeof(char *)); // malloc list

    /* initialise clipboard */
    if (!OpenClipboard(NULL)) { // initialises win32Clipboard.text as clipboard text data
        printf("error: could not open clipboard (windows)\n");
        return -1;
    }
    HANDLE clipboardHandle = GetClipboardData(CF_TEXT);
    LPTSTR wstrData;
    if (clipboardHandle != NULL) {
        wstrData = GlobalLock(clipboardHandle);
        if (wstrData != NULL) {
            unsigned int i = 0;
            unsigned int dynMem = 8; // start with 7 characters
            win32Clipboard.text = malloc(dynMem);
            while (wstrData[i] != '\0' && i < 4294967295) {
                win32Clipboard.text[i] = wstrData[i]; // convert from WCHAR to char
                i++;
                if (i >= dynMem) { // if i is eight we need to realloc to at least 9
                    dynMem *= 2;
                    win32Clipboard.text = realloc(win32Clipboard.text, dynMem);
                }
            }
            win32Clipboard.text[i] = '\0';
            GlobalUnlock(clipboardHandle);
        } else {
            printf("error: could not lock clipboard\n");
            CloseClipboard();
            return -1;
        }
    } else {
        printf("error: could not read from clipboard\n");
        CloseClipboard();
        return -1;
    }
    CloseClipboard();
    return 0;
}

int win32ClipboardGetText() { // gets the text from win32Clipboard
    free(win32Clipboard.text);
    if (!OpenClipboard(NULL)) { // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-openclipboard
        printf("error: could not open clipboard\n");
        return -1;
    }
    HANDLE clipboardHandle = GetClipboardData(CF_TEXT); // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclipboarddata
    LPTSTR wstrData; // WCHAR string
    if (clipboardHandle != NULL) {
        wstrData = GlobalLock(clipboardHandle); // https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-globallock
        if (wstrData != NULL) {
            unsigned int i = 0;
            unsigned int dynMem = 8; // start with 7 characters
            win32Clipboard.text = malloc(dynMem);
            while (wstrData[i] != '\0' && i < 4294967295) {
                win32Clipboard.text[i] = wstrData[i]; // convert from WCHAR to char
                i++;
                if (i >= dynMem) { // if i is eight we need to realloc to at least 9
                    dynMem *= 2;
                    win32Clipboard.text = realloc(win32Clipboard.text, dynMem);
                }
            }
            win32Clipboard.text[i] = '\0';
            GlobalUnlock(clipboardHandle);
        } else {
            printf("error: could not lock clipboard\n");
            CloseClipboard();
            return -1;
        }
    } else {
        printf("error: could not read from clipboard\n");
        CloseClipboard();
        return -1;
    }
    CloseClipboard();
    return 0;
}

int win32ClipboardSetText(const char *input) { // takes null terminated strings
    if (!OpenClipboard(NULL)) { // technically (according to windows documentation) I should get the HWND (window handle) for the GLFW window, but that requires using the glfw3native.h header which would require lots of rewrites and endanger cross-platform compatibility
        printf("error: could not open clipboard\n");
        return -1;
    }
    unsigned int dynMem = strlen(input) + 1; // +1 for the null character
    /* GlobalAlloc is like malloc but windows */
    /* "Handles" are like pointers to data but not directly, you have to "GlobalLock" to actually access the data */
    /* GlobalAlloc allows you to alloc memory at the place that the Handle points to */
    HANDLE clipboardBufferHandle = GlobalAlloc(GMEM_MOVEABLE, dynMem); // https://learn.microsoft.com/en-us/windows/win32/sysinfo/handles-and-objects
    LPTSTR clipboardBufferObject = GlobalLock(clipboardBufferHandle); // WCHAR string
    for (int i = 0; i < dynMem; i++) {
        clipboardBufferObject[i] = input[i]; // convert from char to WCHAR
    }
    GlobalUnlock(clipboardBufferObject);
    /* Empty clipboard: Empties the clipboard and frees handles to data in the clipboard. The function then assigns ownership of the clipboard to the window that currently has the clipboard open. 
    This is a problem because our openClipboard window handle (HWND) is NULL, so the ownership doesn't get transferred, but it still works on my machine */
    if (!EmptyClipboard()) { // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-emptyclipboard
        printf("error: could not empty clipboard\n");
        CloseClipboard();
        return -1;
    }
    if (SetClipboardData(CF_TEXT, clipboardBufferHandle) == NULL) { // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setclipboarddata
        printf("error: could not set cliboard data\n");
        CloseClipboard();
        return -1;
    }
    CloseClipboard();
    return 0;
}

void win32FileDialogAddExtension(char *extension) {
    if (strlen(extension) <= 4) {
        win32FileDialog.numExtensions += 1;
        win32FileDialog.extensions = realloc(win32FileDialog.extensions, win32FileDialog.numExtensions * sizeof(char *));
        win32FileDialog.extensions[win32FileDialog.numExtensions - 1] = strdup(extension);
    } else {
        printf("extension name: %s too long\n", extension);
    }
}

int win32FileDialogPrompt(char openOrSave, char *filename) { // 0 - open, 1 - save, filename refers to autofill filename ("null" or empty string for no autofill)
    win32FileDialog.openOrSave = openOrSave;
    HRESULT hr = CoInitializeEx(NULL, 0); // https://learn.microsoft.com/en-us/windows/win32/api/objbase/ne-objbase-coinit
    if (SUCCEEDED(hr)) {
        IFileDialog *fileDialog;
        IShellItem *psiResult;
        PWSTR pszFilePath = NULL;
        hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_ALL, &IID_IFileOpenDialog, (void**) &fileDialog);
        if (SUCCEEDED(hr)) {
            fileDialog -> lpVtbl -> SetOptions(fileDialog, 0); // https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/ne-shobjidl_core-_fileopendialogoptions from my tests these don't seem to do anything

            /* configure autofill filename */
            if (openOrSave == 1 && strcmp(filename, "null") != 0) {
                int i = 0;
                unsigned short prename[MAX_PATH + 1];
                while (filename[i] != '\0' && i < MAX_PATH + 1) {
                    prename[i] = filename[i]; // convert from char to WCHAR
                    i++;
                }
                prename[i] = '\0';
                fileDialog -> lpVtbl -> SetFileName(fileDialog, prename);
            }

            /* load file restrictions
            Info: each COMDLG creates one more entry to the dropdown to the right of the text box in the file dialog window
            You can only see files that are specified in the types on the current COMDLG_FILTERSPEC selected in the dropdown
            Thats why I shove all the types into one COMDLG_FILTERSPEC, because I want the user to be able to see all compatible files at once
             */
            if (win32FileDialog.numExtensions > 0) {
                COMDLG_FILTERSPEC *fileExtensions = malloc(sizeof(COMDLG_FILTERSPEC)); // just one filter
                WCHAR *buildFilter = malloc(10 * win32FileDialog.numExtensions * sizeof(WCHAR));
                int j = 0;
                for (int i = 0; i < win32FileDialog.numExtensions; i++) {
                    buildFilter[j] = (unsigned short) '*';
                    buildFilter[j + 1] = (unsigned short) '.';
                    j += 2;
                    for (int k = 0; k < strlen(win32FileDialog.extensions[i]) && k < 8; k++) {
                        buildFilter[j] = win32FileDialog.extensions[i][k];
                        j += 1;
                    }
                    buildFilter[j] = (unsigned short) ';';
                    j += 1;
                }
                buildFilter[j] = (unsigned short) '\0';
                (*fileExtensions).pszName = L"Specified Types";
                (*fileExtensions).pszSpec = buildFilter;
                fileDialog -> lpVtbl -> SetFileTypes(fileDialog, 1, fileExtensions);
                free(buildFilter);
                free(fileExtensions);
            }

            /* configure title and button text */
            if (openOrSave == 0) { // open
                fileDialog -> lpVtbl -> SetOkButtonLabel(fileDialog, L"Open");
                fileDialog -> lpVtbl -> SetTitle(fileDialog, L"Open");
            } else { // save
                fileDialog -> lpVtbl -> SetOkButtonLabel(fileDialog, L"Save");
                fileDialog -> lpVtbl -> SetTitle(fileDialog, L"Save");
            }

            /* execute */
            fileDialog -> lpVtbl -> Show(fileDialog, NULL); // opens window
            hr = fileDialog -> lpVtbl -> GetResult(fileDialog, &psiResult); // succeeds if a file is selected
            if (SUCCEEDED(hr)){
                hr = psiResult -> lpVtbl -> GetDisplayName(psiResult, SIGDN_FILESYSPATH, &pszFilePath); // extracts path name
                if (SUCCEEDED(hr)) {
                    int i = 0;
                    while (pszFilePath[i] != '\0' && i < MAX_PATH + 1) {
                        win32FileDialog.selectedFilename[i] = pszFilePath[i]; // convert from WCHAR to char
                        i++;
                    }
                    win32FileDialog.selectedFilename[i] = '\0';
                    CoTaskMemFree(pszFilePath);
                    return 0;
                }
                psiResult -> lpVtbl -> Release(psiResult);
            }
            fileDialog -> lpVtbl -> Release(fileDialog);
        } else {
            printf("ERROR - HRESULT: %lx\n", hr);
        }
        CoUninitialize();
    }
    return -1;
}
#endif