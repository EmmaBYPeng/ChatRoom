#include "chat.h"
#include "chat_server.h"
#include <string.h>
#include <signal.h>
#include <assert.h>
#include <pthread.h>
//#include <stdlib.h>
//#include <stdio.h>

static char banner[] =
"\n\n\
/*****************************************************************/\n\
/*    CSIS0234_COMP3234 Computer and Communication Networks      */\n\
/*    Programming Assignment                                     */\n\
/*    Client/Server Application - Mutli-thread Chat Server       */\n\
/*                                                               */\n\
/*    USAGE:  ./chat_server    [port]                            */\n\
/*            Press <Ctrl + C> to terminate the server           */\n\
/*****************************************************************/\n\
\n\n";

/* 
 * Debug option 
 * In case you do not need debug information, just comment out it.
 */
#define CSIS0234_COMP3234_DEBUG

/* 
 * Use DEBUG_PRINT to print debugging info
 */
#ifdef CSIS0234_COMP3234_DEBUG
#define DEBUG_PRINT(_f, _a...) \
    do { \
        printf("[debug]<%s> " _f "\n", __func__, ## _a); \
    } while (0)
#else
#define DEBUG_PRINT(_f, _a...) do {} while (0)
#endif


void server_init(void);
void server_run(void);

void *broadcast_thread_fn(void *);
void *client_thread_fn(void *);
void shutdown_handler(int);
void enqueue(struct chat_client *newClient);


struct chat_server chatServer;

int port;
int sockfd;


/*
 * The main server process
 */
int main(int argc, char **argv)
{
    printf("%s\n", banner);
    
    if (argc > 1) {
        port = atoi(argv[1]);
    } else {
        port = DEFAULT_LISTEN_PORT;
    }

    // Register "Control + C" signal handler
    signal(SIGINT, shutdown_handler);
    signal(SIGTERM, shutdown_handler);

    // Initilize the server
    server_init();
    
    // Run the server
    server_run();

    return 0;
}

/*void printQ()
{
  struct chat_client *temp = chatServer.room.clientQ.head -> next;
  int i = 1;
  
  if(temp == NULL)
  	printf("empty queue!\n");
  	
  while(temp != NULL){
  	printf("client %d is %s\n", i, temp -> client_name);
  	temp = temp -> next;
  	i++;
  }
  	
}*/

int send_msg_to_client(int sockfd, char *msg, int command, int errortype)
{
  struct exchg_msg mbuf;
  int msg_len = 0;
  
  memset(&mbuf, 0, sizeof(struct exchg_msg));
  mbuf.instruction = htonl(command);
  
  if((command == CMD_SERVER_JOIN_OK) || (command == CMD_SERVER_CLOSE)){
  	mbuf.private_data = htonl(-1);
  }else if(command == CMD_SERVER_BROADCAST){
  	memcpy(mbuf.content, msg, strlen(msg));
  	msg_len = strlen(msg) + 1;
  	msg_len = (msg_len < CONTENT_LENGTH) ? msg_len : CONTENT_LENGTH;
  	mbuf.content[msg_len - 1] = '\0';
  	mbuf.private_data = htonl(msg_len); 
  }else if(command == CMD_SERVER_FAIL){
  	mbuf.private_data = htonl(errortype);
  }
  
  if(send(sockfd, &mbuf, sizeof(mbuf), 0) == -1){
  	printf("client socket send error!\n");
  	return -1;
  }
  
  return 0;
}

//check for duplicate name in the client queue
int nameCheck(char * content)
{
	printf("inside nameCheck: \n");
	printf("content = %s\n", content);

	if(sem_wait(&chatServer.room.clientQ.cq_lock) == -1){
		printf("semaphore wait fail!\n");
	}
	
	struct chat_client *temp = chatServer.room.clientQ.head -> next;

	if(temp == NULL)
		printf("inside nameCheck: head is null\n");
	while(temp != NULL){
		printf("temp -> name = %s\n", temp -> client_name);	
		if(strcmp(temp -> client_name, content)){
			temp = temp -> next;
		}else{
			if(sem_post(&chatServer.room.clientQ.cq_lock) == -1){
				printf("semaphore signal fail!\n");
			}
			return 0;	
		}
	}
	
	if(sem_post(&chatServer.room.clientQ.cq_lock) == -1){
		printf("semaphore signal fail!\n");
	}
	
	return 1;
}

/*
 * Initilize the chatserver
 */
void server_init(void)
{
    // TO DO:
    // Initilize all related data structures
    // 1. semaphores, mutex, pointers, etc.
    // 2. create the broadcast_thread

	//create the socket object
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
         	printf("socket create error!\n");
	 }
	
	chatServer.address.sin_family = AF_INET;
	chatServer.address.sin_port = htons(port);
	chatServer.address.sin_addr.s_addr = htonl(INADDR_ANY);
	

	//bind the server address with the socket
	if(bind(sockfd, (struct sockaddr*)&chatServer.address,                
			 sizeof(struct sockaddr)) == -1){
			printf("socket bind error!\n");
	}
	
	//initialize chat_room structure	
	chatServer.room.clientQ.count = 0;
	chatServer.room.clientQ.head = malloc(sizeof(struct chat_client));
	chatServer.room.clientQ.head -> next = NULL;
	chatServer.room.clientQ.head -> prev = NULL;
	chatServer.room.clientQ.tail = NULL;
	if(sem_init(&chatServer.room.clientQ.cq_lock, 0, 1) == -1){
		printf("error in initializing semaphore!");
	} 

	//initialize chatServer.room.chatmsgQ
	chatServer.room.chatmsgQ.head = chatServer.room.chatmsgQ.tail = 0;
	int i = 0;
	for(i = 0; i < MAX_QUEUE_MSG; i++){
		chatServer.room.chatmsgQ.slots[i] = 
			malloc(sizeof(char)*CONTENT_LENGTH);
	}
	if(sem_init(&chatServer.room.chatmsgQ.buffer_empty, 0, 0) == -1){
		printf("error in initializing semaphore!");
	}
	if(sem_init(&chatServer.room.chatmsgQ.buffer_full, 0, MAX_QUEUE_MSG) == -1){
		printf("error in initializing semaphore!");
	}
	if(sem_init(&chatServer.room.chatmsgQ.mq_lock, 0, 1) == -1){
		printf("error in initializing semaphore!");
	}

	

	//create the broadcast_thread
	if(pthread_create(&chatServer.room.broadcast_thread, 
		           NULL, broadcast_thread_fn, NULL) != 0){
		printf("can't creat broadcast thread\n"); 
	}else{
		printf(" broadcast thread create success!\n");
	}
	
} 


/*
 * Run the chat server 
 */
void server_run(void)
{
    while (1) {
        // TO DO:
        // Listen for new connections
        // 1. if it is a CMD_CLIENT_JOIN, try to create a new client_thread
        //  1.1) check whether the room is full or not
        //  1.2) check whether the username has been used or not
        // 2. otherwise, return ERR_UNKNOWN_CMD

	struct sockaddr_in newAdd;
	struct exchg_msg newClientMsg;
	socklen_t sin_size;
	int recvCmd;
	int new_fd;

	printf("starting listen\n");
	if(listen(sockfd, BACKLOG) == -1){
		printf("socket listen error!\n");
	}

	printf("starting accept\n");
	
	sin_size = sizeof(struct sockaddr_in);
	if((new_fd = 
            accept(sockfd, (struct sockaddr *)&newAdd, &sin_size))
	    == -1){
		printf("socket accecpt error!\n");
	}
	printf("after accept\n");
	
	if(recv(new_fd, &newClientMsg, sizeof(newClientMsg), 0) == -1){
		printf("socket receive error!\n");
	}
	recvCmd = ntohl(newClientMsg.instruction);
	printf("receive success--server_run!\n");

	if(recvCmd == CMD_CLIENT_JOIN){
		//mutex on count
		if(sem_wait(&chatServer.room.clientQ.cq_lock) == -1){
		printf("semaphore wait fail!\n");
		}
		
		if(chatServer.room.clientQ.count < MAX_ROOM_CLIENT){
		
			if(sem_post(&chatServer.room.clientQ.cq_lock) == -1){
				printf("semaphore signal fail!\n");
			}
		//check if the room is full
			//printQ();
			if(nameCheck(newClientMsg.content)){
				
				// create a new client, enqueue
				struct chat_client * joinClient = 
					malloc(sizeof(struct chat_client));
				joinClient -> address = newAdd;
				joinClient -> socketfd = new_fd;
				strcpy(joinClient -> client_name, 
				       newClientMsg.content);
				enqueue(joinClient);

				//create a client thread
				if(pthread_create(&joinClient -> client_thread, 
		  		   		  NULL, client_thread_fn, 
		  		   	          (void *)(joinClient)) != 0){
					printf("can't creat client thread\n"); 
				}else{
					printf("client thread create success!\n");
				}
				
			}else{
				if(send_msg_to_client(new_fd, NULL, 
				   CMD_SERVER_FAIL, ERR_JOIN_DUP_NAME) != 0){
  					printf("socket send error!\n");
				}
			}		
		}else{
			if(sem_post(&chatServer.room.clientQ.cq_lock) == -1){
				printf("semaphore signal fail!\n");
			}
			
			if(send_msg_to_client(new_fd, NULL, CMD_SERVER_FAIL, 
				ERR_JOIN_ROOM_FULL) != 0){
  				printf("socket send error!\n");
			}
		}

	}else{
		if(send_msg_to_client(new_fd, NULL, CMD_SERVER_FAIL, 
			ERR_UNKNOWN_CMD) != 0){
  			printf("socket send error!\n");
		}		
	}

	//printQ();

	
  }	
	
}


//add new client into the client queue
void enqueue(struct chat_client *newClient)
{
	  if(sem_wait(&chatServer.room.clientQ.cq_lock) == -1){
		printf("semaphore wait fail!\n");
	  }
	 
	  //if join success, put the new client into the client queue
	  printf("enqueue: count in clientQ is %d\n", 
	  	  chatServer.room.clientQ.count);

	  if(chatServer.room.clientQ.count == 0){
		chatServer.room.clientQ.head -> next = newClient;
		newClient -> prev = chatServer.room.clientQ.head;
		newClient -> next = NULL;	
	  }else{
		newClient -> next = chatServer.room.clientQ.head -> next;
		newClient -> prev = chatServer.room.clientQ.head;
		chatServer.room.clientQ.head -> next -> prev = newClient;
		chatServer.room.clientQ.head -> next = newClient;
	  }

	  chatServer.room.clientQ.count++;  
	  printf("enqueue: count in clientQ is %d\n", 
	  	  chatServer.room.clientQ.count);
		
	  if(sem_post(&chatServer.room.clientQ.cq_lock) == -1){
		printf("semaphore signal fail!\n");
	  }


}

//remove client from the client queue
void dequeue(struct chat_client * client)
{
	if(sem_wait(&chatServer.room.clientQ.cq_lock) == -1){
		printf("semaphore wait fail!\n");
	}
	
	printf("inside dequeue!\n");
	printf("count in clientQ is %d\n", chatServer.room.clientQ.count);
	if(chatServer.room.clientQ.count == 1){
		chatServer.room.clientQ.head -> next = NULL;
		chatServer.room.clientQ.count--;
	}else{
		printf("here1!\n");
		struct chat_client * temp = chatServer.room.clientQ.head -> next;
		while(temp -> socketfd != client -> socketfd &&
		      temp != NULL){
			temp = temp -> next;
		}
		printf("here2!\n");
		if(temp != NULL){
			if(temp -> next != NULL){
				temp -> prev -> next = temp -> next;
				temp -> next -> prev = temp -> prev;
			}else{
				temp -> prev -> next = NULL;
			}
			chatServer.room.clientQ.count--;
		}
		printf("here3!\n");
	}
	
	printf("count in clientQ is %d\n", chatServer.room.clientQ.count);
	if(sem_post(&chatServer.room.clientQ.cq_lock) == -1){
		printf("semaphore signal fail!\n");
	}
}

//add message into the message queue
void addMsg(char * clientMsg)
{

	if(sem_wait(&chatServer.room.chatmsgQ.buffer_full) == -1){
			printf("msgQ full semaphore wait fail!\n");
	} 
	
	if(sem_wait(&chatServer.room.chatmsgQ.mq_lock) == -1){
			printf("msgQ access semaphore wait fail!\n");
	}
	printf("inside addMsg!\n");
	
	int msgTail = chatServer.room.chatmsgQ.tail;
	strcpy(chatServer.room.chatmsgQ.slots[msgTail], clientMsg);
	printf("msgTail = %s\n", chatServer.room.chatmsgQ.slots[msgTail]);
	
	chatServer.room.chatmsgQ.tail = (chatServer.room.chatmsgQ.tail + 1) %
					MAX_QUEUE_MSG;
	
	if(sem_post(&chatServer.room.chatmsgQ.mq_lock) == -1){
		printf("semaphore signal fail!\n");
	}
	
	if(sem_post(&chatServer.room.chatmsgQ.buffer_empty) == -1){
		printf("semaphore signal fail!\n");
	}
}

void *client_thread_fn(void *arg)
{
    // TO DO:
    // Put one message into the bounded buffer "$client_name$ just joins, welcome!
   
   	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    	
	struct chat_client *client = (struct chat_client *)arg;
	
	char message1[CONTENT_LENGTH]; 
	strcpy(message1, client -> client_name);
	strcat(message1, " just joins, welcome!");
	printf("message1 = %s\n", message1);
	
	addMsg(message1);
	
	if(send_msg_to_client(client -> socketfd, NULL, CMD_SERVER_JOIN_OK, 0) == -1)
	{
		printf("socket send error!\n");
	}
	
	while (1) { 
        // TO DO
        // Wait for incomming messages from this client
        // 1. if it is CMD_CLIENT_SEND, put the message to the bounded buffer
        // 2. if it is CMD_CLIENT_DEPART: 
        //  2.1) send a message "$client_name$ leaves, goodbye!" to all other clients
        //  2.2) free/destroy the resources allocated to this client
        //  2.3) terminate this thread	  
        
	printf("enter client while loop\n");
	printf("newClient name = %s\n", client -> client_name);
	
	//receive message from client
	struct exchg_msg newClientMsg;
	int recvCmd;
   		
	if(recv(client -> socketfd, &newClientMsg, 
	        sizeof(newClientMsg), 0) == -1){
		printf("client thread receive error!\n");
	}else{
		recvCmd = ntohl(newClientMsg.instruction);
		printf("recvCmd = %d\n", recvCmd);
	}
	
	//printf("recvCmd = %d\n", recvCmd);
	if(recvCmd == CMD_CLIENT_SEND){
		char message2[CONTENT_LENGTH];
		strcpy(message2, client -> client_name);
		strcat(message2, ": ");
		strcat(message2, newClientMsg.content);	
		printf("send msg = %s\n", message2);
		addMsg(message2);
			    
	}else if(recvCmd == CMD_CLIENT_DEPART){
		
		printf("client depart!\n");
		//printQ();
		char* c = " just leaves the chat room, goodbye!";
		char message2[CONTENT_LENGTH];
		strcpy(message2, client -> client_name);
		strcat(message2, c);		
		printf("depart msg = %s\n", message2);
		addMsg(message2);
		
		//remove the client from the client queue
		//printQ();
		dequeue(client);
		//printQ();
		
		pthread_detach(pthread_self());
		pthread_exit(0);

	}else{
		printf("unknown command -- client thread\n");
	}
	
	}

}


void *broadcast_thread_fn(void *arg)
{
    
    while (1) {
        // TO DO:
        // Broadcast the messages in the bounded buffer to all clients, one by one
	printf("enter broadcast!\n");
	
	if(sem_wait(&chatServer.room.chatmsgQ.buffer_empty) == -1){
			printf("msgQ full semaphore wait fail!\n");
	}
	if(sem_wait(&chatServer.room.chatmsgQ.mq_lock) == -1){
			printf("msgQ access semaphore wait fail!\n");
	}
	
    	
    	int msgHead = chatServer.room.chatmsgQ.head;
	char* content = chatServer.room.chatmsgQ.slots[msgHead];
	printf("msgHead = %s\n", content);
	
	//send message to all clients, access client queue
	if(sem_wait(&chatServer.room.clientQ.cq_lock) == -1){
		printf("semaphore wait fail!\n");
	}
	
	struct chat_client *temp = chatServer.room.clientQ.head -> next;
	printf("inside broadcast\n");
	//printQ();
	while(temp != NULL){
		if(send_msg_to_client(temp -> socketfd, content,
			              CMD_SERVER_BROADCAST, 0) != 0){
			printf("broadcast send error!\n");
		}
		temp = temp -> next;
	}
	
	
	if(sem_post(&chatServer.room.clientQ.cq_lock) == -1){
		printf("semaphore signal fail!\n");
	}
	
	chatServer.room.chatmsgQ.head = (chatServer.room.chatmsgQ.head + 1) %
					 MAX_QUEUE_MSG;
					 
	//signal clientmsgQ semaphore
	if(sem_post(&chatServer.room.chatmsgQ.mq_lock) == -1){
		printf("semaphore signal fail!\n");
	}
	if(sem_post(&chatServer.room.chatmsgQ.buffer_full) == -1){
		printf("semaphore signal fail!\n");
	}
	
	
    }
}





/*
 * Signal handler (when "Ctrl + C" is pressed)
 */
void shutdown_handler(int signum)
{
    // TO DO:
    // Implement server shutdown here
    // 1. send CMD_SERVER_CLOSE message to all clients
    // 2. terminates all threads: broadcast_thread, client_thread(s)
    // 3. free/destroy all dynamically allocated resources: 
    //    memory, mutex, semaphore, whatever.
	
   	//cancel broadcast thread
   	pthread_cancel(chatServer.room.broadcast_thread);
   	pthread_join(chatServer.room.broadcast_thread, NULL);
   	
   	//send message to all clients
   	if(sem_wait(&chatServer.room.clientQ.cq_lock) == -1){
		printf("semaphore wait fail!\n");
	}
	
	struct chat_client *temp = chatServer.room.clientQ.head -> next;
	while(temp != NULL){
		if(send_msg_to_client(temp -> socketfd, NULL, 
				      CMD_SERVER_CLOSE, 0) != 0){
  			printf("shut down send error!\n");
		}
		//cancel client thread
		temp = temp -> next;
	}
	
	temp = chatServer.room.clientQ.head -> next;
	while(temp != NULL){
		pthread_cancel(temp -> client_thread);
		pthread_join(temp -> client_thread, NULL);
		temp = temp -> next;
	}
	
    	if(sem_post(&chatServer.room.clientQ.cq_lock) == -1){
		printf("semaphore signal fail!\n");
	}
	
	//destroy semaphores
	if(sem_destroy(&chatServer.room.clientQ.cq_lock) == -1){
		printf("semaphore detroy fail!\n");
	}
	
	if(sem_destroy(&chatServer.room.chatmsgQ.mq_lock) == -1){
		printf("semaphore detroy fail!\n");
	}
	
	if(sem_destroy(&chatServer.room.chatmsgQ.buffer_full) == -1){
		printf("semaphore detroy fail!\n");
	}
	
	if(sem_destroy(&chatServer.room.chatmsgQ.buffer_empty) == -1){
		printf("semaphore detroy fail!\n");
	}
	
	//free message queue
	printf("free msg queue!\n");
	int i = 0;
	for(i = 0; i < MAX_QUEUE_MSG; i++){
		free(chatServer.room.chatmsgQ.slots[i]);
	}
	
	//free client queue
	printf("free client queue!\n");
	struct chat_client *current = chatServer.room.clientQ.head;
	struct chat_client *prev = NULL;
	while(current != NULL){
		prev = current;
		current = current -> next;
		free(prev);
	}
	
    exit(0);
}
