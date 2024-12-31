#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <arpa/inet.h> 
#define PORT 8080
#define THREAD_POOL_SIZE 4
#define QUEUE_SIZE 100

pthread_mutex_t queue_mutex;
pthread_cond_t queue_cond;
int request_queue[QUEUE_SIZE];  // Queue for  holding client sockets (tas scheduling mechanism is FIFO) 
int queue_front = 0;
int queue_rear = 0;
int COMMAND_LINE_THREAD_POOL_SIZE;
volatile sig_atomic_t server_running = 1; 
int server_fd;

void handle_signal(int sig) {
    printf("\nShutting down server...\n");
    server_running = 0;
    shutdown(server_fd, SHUT_RDWR);
    pthread_cond_broadcast(&queue_cond);
}
// Worker thread function to handle client requests
void *worker_thread(void *arg) {
    while (server_running) {
        pthread_mutex_lock(&queue_mutex);

        while (queue_front == queue_rear && server_running) {
            pthread_cond_wait(&queue_cond, &queue_mutex);
        }
         if (!server_running) {
        pthread_mutex_unlock(&queue_mutex);
        break;  // Exit if the server is shutting down
    }

        int client_socket = request_queue[queue_front];
        queue_front = (queue_front + 1) % QUEUE_SIZE;

        pthread_mutex_unlock(&queue_mutex);
        pthread_t thread_id = pthread_self();
        printf("[Thread %lu] Handling client_socket: %d\n", (unsigned long)thread_id, client_socket);
        char buffer[1024] = {0};
        ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer));
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            printf("[Thread %lu] Received request: %s\n", (unsigned long)thread_id, buffer);
        }
        const char *response = "HTTP/1.1 200 OK\nContent-Type: text/plain\n\nHello from the Web Server!";
        send(client_socket, response, strlen(response), 0);

        close(client_socket);
    }
    return NULL;
}

// Function to accept incoming client connections
void *accept_connections(void *arg) {
    struct sockaddr_in address;
    socklen_t addr_len = sizeof(address);

    while (server_running) {
        int client_socket = accept(server_fd, (struct sockaddr *)&address, &addr_len);
        if (client_socket < 0) {
	    if (!server_running) {
                break;  // Exit if the server is shutting down
            }
            perror("Accept failed");
            continue;
        }
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(address.sin_addr), client_ip, INET_ADDRSTRLEN);
        int client_port = ntohs(address.sin_port);

        printf("[Accept Thread] Connection from %s:%d (FD: %d)\n",
               client_ip, client_port, client_socket);
        // Adding client socket to the request queue
        pthread_mutex_lock(&queue_mutex);
        request_queue[queue_rear] = client_socket;
        queue_rear = (queue_rear + 1) % QUEUE_SIZE;
        pthread_cond_signal(&queue_cond);
        pthread_mutex_unlock(&queue_mutex);
    }
    return NULL;
}

int main(int argc, char *argv[]) {

 signal(SIGINT, handle_signal);


if (argc > 1) {
        COMMAND_LINE_THREAD_POOL_SIZE = atoi(argv[1]);
        if (COMMAND_LINE_THREAD_POOL_SIZE <= 0) {
            fprintf(stderr, "Invalid thread pool size. Using default (%d).\n", COMMAND_LINE_THREAD_POOL_SIZE);
            COMMAND_LINE_THREAD_POOL_SIZE = THREAD_POOL_SIZE;
        }
    } else {
        COMMAND_LINE_THREAD_POOL_SIZE = THREAD_POOL_SIZE;
    }
 printf("Starting server with %d worker threads...\n", COMMAND_LINE_THREAD_POOL_SIZE);
    // Creating  server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Socket creation failed");
        return -1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(server_fd);
        return -1;
    }

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        return -1;
    }

    // Listening for incoming client connections
    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        close(server_fd);
        return -1;
    }

    
    //pthread_t threads[THREAD_POOL_SIZE];
    pthread_mutex_init(&queue_mutex, NULL);
    pthread_cond_init(&queue_cond, NULL);

    // Thread pool creation
    pthread_t *threads = malloc(sizeof(pthread_t) * 
    COMMAND_LINE_THREAD_POOL_SIZE);
    for (int i = 0; i < COMMAND_LINE_THREAD_POOL_SIZE; ++i) {
        pthread_create(&threads[i], NULL, worker_thread, NULL);
    }

    // One thread for  accepting incoming connections
    pthread_t accept_thread;
    pthread_create(&accept_thread, NULL, accept_connections, &server_fd);

    for (int i = 0; i <COMMAND_LINE_THREAD_POOL_SIZE; ++i) {
        pthread_join(threads[i], NULL);
    }

    pthread_join(accept_thread, NULL);
    free(threads);
    pthread_mutex_destroy(&queue_mutex);
    pthread_cond_destroy(&queue_cond);
    close(server_fd);
    return 0;
}
