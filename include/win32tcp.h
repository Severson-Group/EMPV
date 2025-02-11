/*
https://gist.github.com/mmozeiko/c0dfcc8fec527a90a02145d2cc0bfb6d
https://learn.microsoft.com/en-us/windows/win32/winsock/complete-server-code
*/

#ifndef WIN32TCP
#define WIN32TCP 1 // include guard

#include <windows.h>
#include <shobjidl.h>
#include <string.h>

typedef struct {
    SOCKET socket;
    struct sockaddr address;
} win32SocketObject;

win32SocketObject win32Socket;

void win32tcpInit(char *address) {
    win32Socket.socket = socket(AF_INET, SOCK_STREAM, 0);
    char modifiable[strlen(address) + 1];
    strcpy(modifiable, address);
    win32Socket.address.sa_family = AF_INET; // IPv4
    char *check = strtok(modifiable, ".");
    int segments = 0;
    unsigned char ipAddress[4] = {0};
    while (check != NULL) {
        if (segments > 3) {
            printf("Could not initialise win32tcp - invalid ip address\n");
            return;
        }
        int segmentValue = atoi(check);
        if (segmentValue > 255 || segmentValue < 0) {
            printf("Could not initialise win32tcp - invalid ip address\n");
            return;
        }
        ipAddress[segments] = segmentValue;
        check = strtok(NULL, ".");
        segments++;
    }
    if (segments != 4) {
        printf("Could not initialise win32tcp - invalid ip address\n");
        return;
    }
    printf("Connecting to %d.%d.%d.%d\n", ipAddress[0], ipAddress[1], ipAddress[2], ipAddress[3]);
    memcpy(win32Socket.address.sa_data, ipAddress, 4);
    int err = bind(win32Socket.socket, &win32Socket.address, sizeof(SOCKADDR_IN));
    err = listen(win32Socket.socket, 0);
}

void win32tcpUpdate() {
    SOCKET newSocket = accept(win32Socket.socket, NULL, NULL);
    char c;
    // read(newSocket, &c, 1);
}

void win32tcpSend(char *data, int length) {
    
}

#endif