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
// Function declaration
int isClientAuthorized(int clientSocket, const char* message);

void processClientMessage(int clientSocket, const char* message) {
	// Perform access control checks to validate the client's access rights
	// Implement your access control logic here
	// Example: Check if the client is authorized to access the message

	if (!isClientAuthorized(clientSocket, message)) {
    	// If the client is not authorized, send an error message
    	const char* errorMsg = "Access denied. You are not authorized to access this message.";
    	send(clientSocket, errorMsg, strlen(errorMsg), 0);
    	printf("[INFO] Access denied for client: %d\n", clientSocket);
    	return;
	}

	// If the client has access, process the message and generate a response
	char response[RESPONSE_SIZE];
	snprintf(response, RESPONSE_SIZE, "Hello from server! You sent: %s", message);

	// Send the response to the client
	send(clientSocket, response, strlen(response), 0);
	printf("[INFO] Message processed: %s\n", message);
}

int isClientAuthorized(int clientSocket, const char* message) {
	struct sockaddr_in clientAddress;
	socklen_t addressLength = sizeof(clientAddress);
	getpeername(clientSocket, (struct sockaddr*)&clientAddress, &addressLength);

	// Get the client's IP address
	char clientIP[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(clientAddress.sin_addr), clientIP, INET_ADDRSTRLEN);

	// Implement your access control logic here
	// Example: Allow access only to clients with a specific IP address
	if (strcmp(clientIP, "127.0.0.1") == 0) {
    	return 1; // Client is authorized
	}

	return 0; // Client is not authorized
}
void initSharedMemory() {
	key_t key = ftok(".", 'S');
	shmId = shmget(key, BUFFER_SIZE, IPC_CREAT | 0666);
	if (shmId == -1) {
    	perror("[ERROR] FAILED TO CREATE SHARED MEMORY");
    	exit(1);
	}

	sharedMemory = (char*)shmat(shmId, NULL, 0);
	if (sharedMemory == (char*)-1) {
    	perror("[ERROR] FAILED TO ATTACH SHARED MEMORY");
    	exit(1);
	}

	memset(sharedMemory, 0, BUFFER_SIZE);
}

void destroySharedMemory() {
	shmdt(sharedMemory);
	shmctl(shmId, IPC_RMID, NULL);
}

void initSemaphore() {
	key_t key = ftok(".", 'E');
	semId = semget(key, 1, IPC_CREAT | 0666);
	if (semId == -1) {
    	perror("[ERROR] FAILED TO CREATE SEMAPHORE");
    	exit(1);
	}

	// Set the initial value of the semaphore to 1
	if (semctl(semId, 0, SETVAL, 1) == -1) {
    	perror("[ERROR] FAILED TO SET SEMAPHORE VALUE");
    	exit(1);
	}
}

void destroySemaphore() {
	semctl(semId, 0, IPC_RMID);
}

void P() {
	struct sembuf operation;
	operation.sem_num = 0;
	operation.sem_op = -1;
	operation.sem_flg = SEM_UNDO;
	if (semop(semId, &operation, 1) == -1) {
    	perror("[ERROR] SEMAPHORE P OPERATION FAILED");
    	exit(1);
	}
}

void V() {
	struct sembuf operation;
	operation.sem_num = 0;
	operation.sem_op = 1;
	operation.sem_flg = SEM_UNDO;
	if (semop(semId, &operation, 1) == -1) {
    	perror("[ERROR] SEMAPHORE V OPERATION FAILED");
    	exit(1);
	}
}
