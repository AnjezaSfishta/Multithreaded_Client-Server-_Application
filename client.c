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

	// Create or get the message queue
	int msgqid;
	if ((msgqid = msgget(MSG_QUEUE_KEY, IPC_CREAT | 0666)) == -1) {
    	perror("[ERROR] CAN'T CREATE/ACCESS MESSAGE QUEUE");
    	return -1;
	}

	struct message msg;

	while (1) {
    	printf("[TCP]> ");
    	fgets(input, BUFFER_SIZE, stdin);

    	// Remove trailing newline character
    	input[strcspn(input, "\n")] = '\0';

    	if (strlen(input) == 0) {
        	strcpy(input, "default message");
    	} else if (strcmp(input, "exit") == 0) {
        	printf("Client connection terminated\n");
       	 
        	// Send disconnect request to the message queue
        	struct message disconnect_msg;
        	disconnect_msg.mtype = 2; // Set message type for disconnect request
        	strcpy(disconnect_msg.mtext, "disconnect"); // Set disconnect request message
        	if (msgsnd(msgqid, &disconnect_msg, sizeof(disconnect_msg.mtext), 0) == -1) {
            	perror("[ERROR] CAN'T SEND DISCONNECT REQUEST TO THE QUEUE");
            	return -1;
        	}
       	 
        	break;
    	}

    	// Send the request to the server
    	send(sock, input, strlen(input), 0);
    	//printf("[SEND] %s\n", input);

    	// Send message to the message queue
    	msg.mtype = 1; // Set message type
    	strcpy(msg.mtext, input); // Copy input to message text
    	if (msgsnd(msgqid, &msg, sizeof(msg.mtext), 0) == -1) {
        	perror("[ERROR] CAN'T SEND MESSAGE TO THE QUEUE");
        	return -1;
    	}

    	// Receive the response from the server
    	valread = recv(sock, buffer, BUFFER_SIZE, 0);
    	printf("[RECV] %s\n", buffer);

    	// Clear the buffer
    	memset(buffer, 0, BUFFER_SIZE);
	}

	close(sock);

	return 0;
}


