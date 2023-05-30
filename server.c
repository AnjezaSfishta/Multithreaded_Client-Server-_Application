#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define ADDRESS "127.0.0.1"
#define PORT 8888
#define BUFFER_SIZE 1024
#define RESPONSE_SIZE 2048
#define MAX_MESSAGE_SIZE 1024

// Structure to hold client information
struct ClientInfo {
	int socket;
	int messageQueueId;
	pthread_t threadId;
};

// Shared memory and semaphore variables
int shmId;
char* sharedMemory;
int semId;

// Message structure for communication between server and clients
struct Message {
	long messageType;
	char messageData[MAX_MESSAGE_SIZE];
};

// Global variables
struct ClientInfo clients[FD_SETSIZE];
int clientCount = 0;
pthread_mutex_t clientsMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t sharedMemoryMutex = PTHREAD_MUTEX_INITIALIZER;

// Semaphore operations
void P();
void V();

// Function declarations
void* clientThreadHandler(void* arg);
void processClientMessage(int clientSocket, const char* message);
void initSharedMemory();
void destroySharedMemory();
void initSemaphore();
void destroySemaphore();

int main(int argc, char* argv[]) {
	int masterSocket, newSocket;
	struct sockaddr_in serverAddress, clientAddress;
	socklen_t clientAddressLength = sizeof(clientAddress);

	// Initialize shared memory and semaphore
	initSharedMemory();
	initSemaphore();

	// Create message queue
	int messageQueueId;
	key_t key = ftok(".", 'M');
	messageQueueId = msgget(key, IPC_CREAT | 0666);

	// Create master socket
	if ((masterSocket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    	perror("[ERROR] CAN'T CREATE SOCKET");
    	destroySharedMemory();
    	destroySemaphore();
    	return -1;
	}

	// Prepare server address
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_addr.s_addr = inet_addr(ADDRESS);
	serverAddress.sin_port = htons(PORT);

	// Bind the master socket to server address and port
	if (bind(masterSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == -1) {
    	perror("[ERROR][BIND]");
    	destroySharedMemory();
    	destroySemaphore();
    	return -1;
	}

	// Listen for incoming connections
	if (listen(masterSocket, SOMAXCONN) == -1) {
    	perror("[ERROR][LISTEN]");
    	destroySharedMemory();
    	destroySemaphore();
    	return -1;
	}

	printf("[INFO] WAITING FOR INCOMING CONNECTIONS\n");

	while (1) {
    	// Accept a new connection
    	if ((newSocket = accept(masterSocket, (struct sockaddr*)&clientAddress, &clientAddressLength)) == -1) {
        	perror("[WARNING] CAN'T ACCEPT NEW CONNECTION");
        	continue;
    	}

    	// Create a new client thread to handle the connection
    	pthread_t threadId;
    	struct ClientInfo* newClient = malloc(sizeof(struct ClientInfo));
    	newClient->socket = newSocket;
    	newClient->threadId = threadId;

    	// Create a new message queue for the client
    	key_t clientKey = ftok(".", 'C');
    	int clientQueueId = msgget(clientKey, IPC_CREAT | 0666);
    	if (clientQueueId == -1) {
        	perror("[WARNING] FAILED TO CREATE MESSAGE QUEUE");
        	free(newClient);
        	close(newSocket);
        	continue;
    	}
    	newClient->messageQueueId = clientQueueId;

    	if (pthread_create(&threadId, NULL, clientThreadHandler, (void*)newClient) != 0) {
        	perror("[WARNING] CAN'T CREATE NEW THREAD");
        	free(newClient);
        	close(newSocket);
        	continue;
    	}

    	// Add the new client to the clients array
    	pthread_mutex_lock(&clientsMutex);
    	clients[clientCount++] = *newClient;
    	pthread_mutex_unlock(&clientsMutex);
	}

	// Close the master socket
	close(masterSocket);

	// Cleanup shared memory and semaphore
	destroySharedMemory();
	destroySemaphore();

	return 0;
}
void* clientThreadHandler(void* arg) {
	struct ClientInfo* clientInfo = (struct ClientInfo*)arg;
	int clientSocket = clientInfo->socket;
	int clientQueueId = clientInfo->messageQueueId;

	char buffer[BUFFER_SIZE] = {0};
	ssize_t bytesRead;

	struct sockaddr_in clientAddress;
	socklen_t clientAddressLength = sizeof(clientAddress);
	getpeername(clientSocket, (struct sockaddr*)&clientAddress, &clientAddressLength);
	char clientIP[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(clientAddress.sin_addr), clientIP, INET_ADDRSTRLEN);
	int clientPort = ntohs(clientAddress.sin_port);

	printf("[INFO] NEW CONNECTION ACCEPTED FROM %s:%d\n", clientIP, clientPort);

	while (1) {
    	// Receive message from client
    	bytesRead = recv(clientSocket, buffer, BUFFER_SIZE, 0);
    	if (bytesRead <= 0) {
        	break; // Client disconnected
    	}

    	// Send the message to the client's message queue
    	struct Message message;
    	message.messageType = clientQueueId;
    	strncpy(message.messageData, buffer, MAX_MESSAGE_SIZE);
    	if (msgsnd(clientQueueId, &message, sizeof(struct Message) - sizeof(long), 0) == -1) {
        	perror("[WARNING] FAILED TO SEND MESSAGE");
        	break;
    	}

    	// Wait for the response from the client's message queue
    	struct Message response;
    	if (msgrcv(clientQueueId, &response, sizeof(struct Message) - sizeof(long), 0, 0) == -1) {
        	perror("[WARNING] FAILED TO RECEIVE MESSAGE");
        	break;
    	}

    	// Process the received message
    	processClientMessage(clientSocket, response.messageData);

    	// Clear the buffer
    	memset(buffer, 0, BUFFER_SIZE);
	}

	// Remove the client from the clients array
	pthread_mutex_lock(&clientsMutex);
	for (int i = 0; i < clientCount; i++) {
    	if (clients[i].socket == clientSocket) {
        	for (int j = i; j < clientCount - 1; j++) {
            	clients[j] = clients[j + 1];
        	}
        	clientCount--;
        	break;
    	}
	}
	pthread_mutex_unlock(&clientsMutex);

	close(clientSocket);

	printf("[INFO] CONNECTION CLOSED: %s:%d\n", clientIP, clientPort);
	printf("[INFO] THREAD TERMINATED\n");

	free(clientInfo);

	pthread_exit(NULL);
}
