#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/msg.h>
#include <sys/ipc.h>

#define ADDRESS "127.0.0.1"
#define PORT 8888
#define BUFFER_SIZE 1024

// Define message queue key
#define MSG_QUEUE_KEY 12345

// Define message structure
struct message {
	long mtype;
	char mtext[BUFFER_SIZE];
};

int main(int argc, char const *argv[]) {
	int sock = 0, valread = 0;
	struct sockaddr_in serv_addr;
	char buffer[BUFFER_SIZE] = {0};
	char input[BUFFER_SIZE];

	// Create socket
	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    	perror("[ERROR] CAN'T CREATE SOCKET");
    	return -1;
	}

	// Prepare server address
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(PORT);

	// Convert IPv4 and IPv6 addresses from text to binary form
	if (inet_pton(AF_INET, ADDRESS, &serv_addr.sin_addr) == -1) {
    	perror("[ERROR] INVALID ADDRESS/ADDRESS NOT SUPPORTED");
    	return -1;
	}

	// Connect to the server
	if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1) {
    	perror("[ERROR] CAN'T CONNECT TO THE HOST");
    	return -1;
	}
