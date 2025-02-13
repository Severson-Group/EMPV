/*
https://gist.github.com/mmozeiko/c0dfcc8fec527a90a02145d2cc0bfb6d
https://learn.microsoft.com/en-us/windows/win32/winsock/complete-server-code
*/

#ifndef WIN32TCP
#define WIN32TCP 1 // include guard

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shobjidl.h>
#include <string.h>

typedef struct {
    SOCKET connectSocket;
    struct sockaddr address;
} win32SocketObject;

win32SocketObject win32Socket;

void win32tcpInit(char *address, char *port) {
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
    /* Initialize Winsock */
    WSADATA wsaData;
    int iResult;
    iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed with error: %d\n", iResult);
        return;
    }
    
    /* hints */
    struct addrinfo hints;
    struct addrinfo *result;
    struct addrinfo *ptr;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    /* Resolve the server address and port */
    iResult = getaddrinfo(address, port, &hints, &result);
    if (iResult != 0) {
        printf("getaddrinfo failed with error: %d\n", iResult);
        WSACleanup();
        return;
    }

    /* Attempt to connect to an address until one succeeds */
    for (ptr = result; ptr != NULL ;ptr=ptr->ai_next) {

        /* Create a SOCKET for connecting to server */
        win32Socket.connectSocket = socket(ptr->ai_family, ptr->ai_socktype, 
            ptr->ai_protocol);
        if (win32Socket.connectSocket == INVALID_SOCKET) {
            printf("socket failed with error: %ld\n", WSAGetLastError());
            WSACleanup();
            return;
        }

        /* Connect to server */
        iResult = connect(win32Socket.connectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
        if (iResult == SOCKET_ERROR) {
            closesocket(win32Socket.connectSocket);
            win32Socket.connectSocket = INVALID_SOCKET;
            continue;
        }
        break;
    }

    /* Send an initial buffer */
    char sendbuf[2] = {12, 34};
    iResult = send(win32Socket.connectSocket, sendbuf, (int) strlen(sendbuf), 0);
    if (iResult == SOCKET_ERROR) {
        printf("send failed with error: %d\n", WSAGetLastError());
        closesocket(win32Socket.connectSocket);
        WSACleanup();
        return;
    }

    printf("Bytes Sent: %ld\n", iResult);

    /* shutdown the connection since no more data will be sent */
    iResult = shutdown(win32Socket.connectSocket, SD_SEND);
    if (iResult == SOCKET_ERROR) {
        printf("shutdown failed with error: %d\n", WSAGetLastError());
        closesocket(win32Socket.connectSocket);
        WSACleanup();
        return;
    }

    /* Receive until the peer closes the connection */
    int recvbuflen = 512;
    char recvbuf[recvbuflen];
    do {

        iResult = recv(win32Socket.connectSocket, recvbuf, recvbuflen, 0);
        if ( iResult > 0 )
            printf("Bytes received: %d\n", iResult);
        else if ( iResult == 0 )
            printf("Connection closed\n");
        else
            printf("recv failed with error: %d\n", WSAGetLastError());

    } while (iResult > 0);

    /* cleanup */
    closesocket(win32Socket.connectSocket);
    WSACleanup();

    return;
}

void win32tcpUpdate() {

}

void win32tcpSend(char *data, int length) {
    
}

#endif