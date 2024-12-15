#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>

#define MAX_GOODS  700
#define MAX_SHOPS 3
#define LOG_FILE "logfile.log"
#define RESULT_FILE "results.txt"

// Structure for goods
typedef struct {
    char name[20];
    int price;
    float point;
    int entity;
    char last_modified[30];
    float score; // score = price * point
} Good;

// Structure for shop
typedef struct {
    char shop_name[20];
    Good goods[MAX_GOODS];
    int good_count;
    float point;
} Shop;

// Structure client
typedef struct {
    char user_name[20]; //client name
    int price_treshold;
    Good shopping_list[MAX_GOODS];
    int shopping_count;
    char shopped_shops[MAX_SHOPS][20]; // Track shops the client has shopped from
    int shop_count;
} Client;

// Mutex for logging
pthread_mutex_t log_mutex;
// Semaphore for synchronization
sem_t sem;

// Function to log thread activity
void log_activity(const char *message) {
    pthread_mutex_lock(&log_mutex);
    FILE *log_file = fopen(LOG_FILE, "a");
    if (log_file == NULL) {
        perror("Error opening log file");
        pthread_mutex_unlock(&log_mutex);
        return;
    }
    pid_t pid = getpid();
    pthread_t tid = pthread_self();
    fprintf(log_file, "PID: %d, TID: %lu - %s\n", pid, tid, message);
    fclose(log_file);
    pthread_mutex_unlock(&log_mutex);
}

// Function to read good data from file
void read_good_data(const char *filename, Good *good) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        perror("Error opening good file");
        return;
    }
    fscanf(file, "%s %d %f %d %s", good->name, &good->price, &good->point, &good->entity, good->last_modified);
    fclose(file);
    good->score = good->price * good->point; // Calculate score
}

// Thread function for processing goods
void *process_good(void *arg) {
    Good *good = (Good *)arg;
    char log_message[100];
    snprintf(log_message, sizeof(log_message), "Processed good %s with price %d", good->name, good->price);
    log_activity(log_message);
    sleep(1); // Simulate processing time
    sem_post(&sem); // Signal that processing is done
    return NULL;
}

// Function for processing a shop and writing results to file
void process_shop(const char *shop_name, int client_budget, int pipe_fd) {
    char log_message[100];
    snprintf(log_message, sizeof(log_message), "Accessed Store %s", shop_name);
    log_activity(log_message);

    DIR *d;
    struct dirent *dir;
    d = opendir(shop_name);
    if (d == NULL)
    {
        perror("Error opening shop directory");
        snprintf(log_message, sizeof(log_message), "Error opening shop directory: %s", shop_name);
        log_activity(log_message);
        return;
    }

    pthread_t threads[MAX_GOODS];
    int good_count = 0;

    // Open result file for appending
    FILE *result_file = fopen(RESULT_FILE, "a");
    if (result_file == NULL) {
        perror("Error opening result file");
        return;
    }

    fprintf(result_file, "Shop: %s\n", shop_name);

    Good best_good;
    best_good.score = 0;

    while ((dir = readdir(d)) != NULL) {
        if (dir->d_type == DT_REG) { // If it's a regular file
            Good good;
            char filepath[100];
            snprintf(filepath, sizeof(filepath), "%s/%s", shop_name, dir->d_name);
            read_good_data(filepath, &good);
            if (good.price <= client_budget) {
                pthread_create(&threads[good_count], NULL, process_good, &good);
                fprintf(result_file, "Good: %s, Price: %d, Point: %.2f, Entity: %d, Last Modified: %s, Score: %.2f\n",
                        good.name, good.price, good.point, good.entity, good.last_modified, good.score);
                sem_wait(&sem); // Wait for the thread to signal completion
                if (good.score > best_good.score) {
                    best_good = good;
                }
                good_count++;
            }
        }
    }

    for (int i = 0; i < good_count; i++) {
        pthread_join(threads[i], NULL);
    }

    // Show the best good to the client
    printf("Best Good in Shop %s: %s, Price: %d, Point: %.2f, Score: %.2f\n",
            shop_name, best_good.name, best_good.price, best_good.point, best_good.score);

    fprintf(result_file, "Best Good: %s, Price: %d, Point: %.2f, Score: %.2f\n",
            best_good.name, best_good.price, best_good.point, best_good.score);
    fprintf(result_file, "-----------------------------\n");
    fclose(result_file);
    closedir(d);

    // Notify parent process that shop processing is complete
    write(pipe_fd, "done", 4);
}

// Function to check if the client has shopped from the shop before
int has_shopped_before(Client *client, const char *shop_name) {
    for (int i = 0; i < client->shop_count; i++) {
        if (strcmp(client->shopped_shops[i], shop_name) == 0) {
            return 1; // Client has shopped from this shop before
        }
    }
    return 0;
}

// Thread function to calculate total price with discount if applicable
void *calculate_total_price(void *arg) {
    Client *client = (Client *)arg;
    int total_price = 0;
    for (int i = 0; i < client->shopping_count; i++) {
        total_price += client->shopping_list[i].price;
    }
    // Check if discount applies
    if (has_shopped_before(client, client->shopping_list[0].name)) {
        total_price *= 0.9; // Apply 10% discount
    }
    char log_message[100];
    snprintf(log_message, sizeof(log_message), "Total price for client %s is %d", client->user_name, total_price);
    log_activity(log_message);
    return NULL;
}

// Thread function to update the amount of goods
void *update_goods_amount(void *arg) {
    // Placeholder for updating the amount of goods in the shop
    log_activity("Goods amount updated.");
    return NULL;
}

// Main function
int main() {
    // Initialize mutex and semaphore
    pthread_mutex_init(&log_mutex, NULL);
    sem_init(&sem, 0, 0);

    // Example client data
    Client client = {"John", 650, { {"lamp", 100}, {"computer", 450}, {"shower", 100} }, 3};
    client.shop_count = 0;

    // Create a process for the client
    pid_t client_pid = fork();
    if (client_pid == 0) {
        // Child process for client

        // Get shop directories
        char *shops[] = {"Store1", "Store2", "Store3"};
        int shop_count = sizeof(shops) / sizeof(shops[0]);

        // Create pipes for communication with shop processes
        int pipes[MAX_SHOPS][2];
        for (int i = 0; i < shop_count; i++) {
            if (pipe(pipes[i]) == -1) {
                perror("pipe");
                exit(1);
            }
        }

        for (int i = 0; i < shop_count; i++) {
            pid_t shop_pid = fork();
            if (shop_pid == 0) {
                // Child process for shop
                close(pipes[i][0]); // Close reading end in child
                process_shop(shops[i], client.price_treshold, pipes[i][1]);
                close(pipes[i][1]); // Close writing end in child
                exit(0);
            }
        }

        // Close writing ends in parent
        for (int i = 0; i < shop_count; i++) {
            close(pipes[i][1]);
        }

        // Wait for all shop processes to finish and read from pipes
        char buffer[4];
        for (int i = 0; i < shop_count; i++) {
            read(pipes[i][0], buffer, 4);
            close(pipes[i][0]);

            // Add shop to client's shopped shops
            strncpy(client.shopped_shops[client.shop_count++], shops[i], sizeof(client.shopped_shops[0]) - 1);
        }

        // Create threads for client processing
        pthread_t price_thread, update_thread;
        pthread_create(&price_thread, NULL, calculate_total_price, &client);
        pthread_create(&update_thread, NULL, update_goods_amount, NULL);

        // Wait for threads to finish
        pthread_join(price_thread, NULL);
        pthread_join(update_thread, NULL);

        // Wait for all shop processes to finish
        while (wait(NULL) > 0);
        exit(0);
    }
    // Wait for the client process to finish
    while (wait(NULL) > 0);

    // Destroy mutex and semaphore
    pthread_mutex_destroy(&log_mutex);
    sem_destroy(&sem);

    printf("All processing completed. Check %s for logs and %s for results.\n", LOG_FILE, RESULT_FILE);
    return 0;
}
