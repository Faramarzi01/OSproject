#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_GOODS 10
#define MAX_SHOPS 5
#define LOG_FILE "log.txt"
#define SHOP_FILE "shops.txt"
#define RESULT_FILE "results.txt"

// Structure to represent a good
typedef struct {
    char name[20];
    int price;
} Good;

// Structure to represent a shop
typedef struct {
    char shop_name[20];
    Good goods[MAX_GOODS];
    int good_count;
} Shop;

// Structure to represent a client
typedef struct {
    char client_name[20];
    int budget;
    Good shopping_list[MAX_GOODS];
    int shopping_count;
} Client;

// Mutex for logging
pthread_mutex_t log_mutex;

// Function to log thread activity
void log_activity(const char *message) {
    pthread_mutex_lock(&log_mutex);
    FILE *log_file = fopen(LOG_FILE, "a");
    if (log_file == NULL) {
        perror("Error opening log file");
        pthread_mutex_unlock(&log_mutex);
        return;
    }
    fprintf(log_file, "%s\n", message);
    fclose(log_file);
    pthread_mutex_unlock(&log_mutex);
}

// Thread function for processing goods
void *process_good(void *arg) {
    Good *good = (Good *)arg;
    char log_message[100];
    snprintf(log_message, sizeof(log_message), "Thread with TID %lu processed good %s with price %d",
             pthread_self(), good->name, good->price);
    log_activity(log_message);
    sleep(1); // Simulate processing time
    return NULL;
}

// Function for processing a shop and writing results to file
void process_shop(Shop shop, int client_budget) {
    pthread_t threads[MAX_GOODS];

    // Open result file for appending
    FILE *result_file = fopen(RESULT_FILE, "a");
    if (result_file == NULL) {
        perror("Error opening result file");
        return;
    }

    fprintf(result_file, "Shop: %s\n", shop.shop_name);

    for (int i = 0; i < shop.good_count; i++) {
        if (shop.goods[i].price <= client_budget) {
            pthread_create(&threads[i], NULL, process_good, &shop.goods[i]);
            fprintf(result_file, "Good: %s, Price: %d\n", shop.goods[i].name, shop.goods[i].price);
        }
    }

    for (int i = 0; i < shop.good_count; i++) {
        pthread_join(threads[i], NULL);
    }

    fprintf(result_file, "-----------------------------\n");
    fclose(result_file);
}

// Function to load shops from a file
int load_shops(Shop shops[], const char *filename) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        perror("Error opening shop file");
        return 0;
    }

    int shop_count = 0;
    while (fscanf(file, "%s", shops[shop_count].shop_name) != EOF) {
        int good_count = 0;
        while (fscanf(file, "%s %d", shops[shop_count].goods[good_count].name, &shops[shop_count].goods[good_count].price) == 2) {
            good_count++;
        }
        shops[shop_count].good_count = good_count;
        shop_count++;
    }

    fclose(file);
    return shop_count;
}

// Main function
int main() {
    // Initialize mutex
    pthread_mutex_init(&log_mutex, NULL);

    // Load shops from file
    Shop shops[MAX_SHOPS];
    int shop_count = load_shops(shops, SHOP_FILE);
    if (shop_count == 0) {
        fprintf(stderr, "No shops loaded. Exiting.\n");
        return 1;
    }

    // Example client data
    Client client = {"John", 30, { {"Apple", 10}, {"Banana", 5}, {"Milk", 12} }, 3};

    // Create a process for the client
    pid_t client_pid = fork();
    if (client_pid == 0) {
        // Child process for client
        for (int i = 0; i < shop_count; i++) {
            pid_t shop_pid = fork();
            if (shop_pid == 0) {
                // Child process for shop
                process_shop(shops[i], client.budget);
                exit(0);
            }
        }
        // Wait for all shop processes to finish
        while (wait(NULL) > 0);
        exit(0);
    }

    // Wait for the client process to finish
    while (wait(NULL) > 0);

    // Destroy mutex
    pthread_mutex_destroy(&log_mutex);

    printf("All processing completed. Check %s for logs and %s for results.\n", LOG_FILE, RESULT_FILE);
    return 0;
}
