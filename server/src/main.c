#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h> // Cung cap API lap trinh Socket Linux
#include <netinet/in.h> // Cung cap dinh nghia IPPROTO_TCP
#include <unistd.h> // Cung cap ham close()

#include "protocol.h"

int main(void) {
    printf("Server Starting...\n");

    /*
        AF_INET: IPv4
        SOCK_STEAM: socket dang luong byte co ket noi
        SOCK_DGRAM: socket dang datagram   khong ket noi mac dinh
        IPPROTO_TCP: TCP
        IPPROTO_UDP: UDP
        server_fd: file descriptor dai dien cho socket
    */
    int server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd == -1) {
        perror("Khong the tao socket");
        return EXIT_FAILURE;
    }

    /*
        sockaddr_in: luu ipv4 va socket port
        sin_family: ho dia chi cua socket
        sin_port: so port cua server
        htons: giao thuc mang luu thu tu byte chuan
            may tinh co the se luu thu tu byte khac
            can htons de luu chinh xac so port
        sin_addr.s_addr: luu ipv4 cua server
        INADDR_ANY: server lang nghe tren tat ca dia chi
            ipv4 cuc bo
        htonl: chuyen doi 32-bit xang thu tu byte mang
    */
    struct sockaddr_in server_addr =
    {
        /* data */
        0
    };
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    /*
        bind: gan socket voi IP va cong SERVER_PORT

    */
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Khong the bind socket");
        close(server_fd);
        return EXIT_FAILURE;
    }

    close(server_fd);

    return 0;
}