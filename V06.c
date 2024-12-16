#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>
#include <semaphore.h>

#define MAX_GOODS 700
#define MAX_SHOPS 3
#define MAX_NAME_LENGTH 20
#define MAX_LINE_LENGTH 256
#define LOG_FILE "logfile.log"
#define PIPE_MSG_SIZE 256

// Structure for goods
typedef struct {
    char name[20];
    int price;
    float point;
    int entity;
    char last_modified[30];
    char file_path [256];
    float score; // score = price * point
} Good;

// Structure for client
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


// Function to log thread activity with store and category details
void log_activity(const char *message, const char *username, const char *store_name, const char *category_name) {
    char log_filename[256];
    snprintf(log_filename, sizeof(log_filename), "%s.log", username);

    pthread_mutex_lock(&log_mutex);
    FILE *log_file = fopen(log_filename, "a");
    if (log_file == NULL) {
        perror("Error opening log file");
        pthread_mutex_unlock(&log_mutex);
        return;
    }
    pid_t pid = getpid();
    pthread_t tid = pthread_self();
    if (store_name && category_name) {
        fprintf(log_file, "PID: %d, TID: %lu - %s [Store: %s, Category: %s]\n", pid, tid, message, store_name, category_name);
    } else if (store_name) {
        fprintf(log_file, "PID: %d, TID: %lu - %s [Store: %s]\n", pid, tid, message, store_name);
    } else {
        fprintf(log_file, "PID: %d, TID: %lu - %s\n", pid, tid, message);
    }
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

    char line[256];

    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "Name:", 5) == 0) {
            sscanf(line, "Name: %s", good->name);
        } else if (strncmp(line, "Price:", 6) == 0) {
            sscanf(line, "Price: %d", &good->price);
        } else if (strncmp(line, "Score:", 6) == 0) {
            sscanf(line, "Score: %f", &good->point);
        } else if (strncmp(line, "Entity:", 7) == 0) {
            sscanf(line, "Entity: %d", &good->entity);
        } else if (strncmp(line, "Last Modified:", 14) == 0) {
            sscanf(line, "Last Modified: %s", good->last_modified);
        }
    }

    fclose(file);
    good->score = good->price * good->point; // Calculate score
}


typedef struct {
    Good good;
    Client *client;
    char filepath[256];
    pthread_t tid;
} GoodThreadArgs;

void *process_good(void *arg) {
    GoodThreadArgs *args = (GoodThreadArgs *)arg;
    Good *good = &args->good;
    Client *client = args->client;
    char *filepath = args->filepath;
    args->tid = pthread_self();

    char log_message[100];
    int total_price = 0;

    // Check if the amount of good required is available
    for (int i = 0; i < client->shopping_count; i++) {
        if (strcmp(client->shopping_list[i].name, good->name) == 0) {
            if (client->shopping_list[i].entity <= good->entity) {
                total_price = client->shopping_list[i].entity * good->price;
                snprintf(log_message, sizeof(log_message), "Processed good %s with total price %d for amount %d",
                         good->name, total_price, client->shopping_list[i].entity);
                log_activity(log_message, client->user_name, NULL, NULL);

                good->price = total_price;
                good->entity = client->shopping_list[i].entity; // Set the quantity the client wants to buy
                client->shopping_list[i].file_path = args->filepath;
                return (void *)filepath; // Return the filepath for later update
            }
            snprintf(log_message, sizeof(log_message), "Not enough entity for good %s. Required: %d, Available: %d",
                     good->name, client->shopping_list[i].entity, good->entity);
            log_activity(log_message, client->user_name, NULL, NULL);
        }
    }

    snprintf(log_message, sizeof(log_message), "Good %s not in client's shopping list", good->name);
    log_activity(log_message, client->user_name, NULL, NULL);
    return NULL;
}



char* get_the_name_of_the_path(const char* path) {
    char* path_copy = strdup(path);
    // if (path_copy == NULL) {
    //     perror("Failed to allocate memory");
    //     return NULL;
    // }
    char* token = strtok(path_copy, "/");
    char* last_token = token;
    while (token != NULL) {
        last_token = token;
        token = strtok(NULL, "/");
    }
    char* result = strdup(last_token);
    return result;
}


void process_category(const char *category_path, Client *client, Good *found_goods, char filepaths[MAX_GOODS][256], int *good_count, pthread_t tids[MAX_GOODS]) {
    DIR *d;
    struct dirent *dir;
    d = opendir(category_path);
    if (d == NULL) {
        perror("Error opening category directory");
        char log_message[100];
        snprintf(log_message, sizeof(log_message), "Error opening category directory: %s", category_path);
        log_activity(log_message, client->user_name, NULL, category_path);
        return;
    }

    pthread_t threads[MAX_GOODS];
    GoodThreadArgs args[MAX_GOODS];
    int local_good_count = 0;

    while ((dir = readdir(d)) != NULL) {
        if (dir->d_type == DT_REG && strstr(dir->d_name, ".txt")) { // If it's a regular file and a .txt file
            char filepath[256];
            snprintf(filepath, sizeof(filepath), "%s/%s", category_path, dir->d_name);

            read_good_data(filepath, &args[local_good_count].good);
            args[local_good_count].client = client;
            strcpy(args[local_good_count].filepath, filepath);

            pthread_create(&threads[local_good_count], NULL, process_good, &args[local_good_count]);
            log_activity("Created thread for processing good", client->user_name, NULL, category_path);

            void *result;
            pthread_join(threads[local_good_count], &result);
            if (result != NULL) {
                strcpy(filepaths[*good_count], (char *)result);
                tids[*good_count] = args[local_good_count].tid;
                found_goods[*good_count] = args[local_good_count].good;
                (*good_count)++;
                local_good_count++;
            }
        }
    }

    closedir(d);
}


void process_shop(const char *shop_name, Client *client, Good *found_goods, char filepaths[MAX_GOODS][256], int *good_count, pthread_t tids[MAX_GOODS]) {
    char log_message[100];
    snprintf(log_message, sizeof(log_message), "Accessed Store %s", shop_name);
    log_activity(log_message, client->user_name, shop_name, NULL);

    DIR *d;
    struct dirent *dir;
    d = opendir(shop_name);
    if (d == NULL) {
        perror("Error opening shop directory");
        snprintf(log_message, sizeof(log_message), "Error opening shop directory: %s", shop_name);
        log_activity(log_message, client->user_name, shop_name, NULL);
        return;
    }

    while ((dir = readdir(d)) != NULL) {
        if (dir->d_type == DT_DIR && strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0) {
            // If it's a directory and not "." or ".."
            char category_path[256];
            snprintf(category_path, sizeof(category_path), "%s/%s", shop_name, dir->d_name);
            pid_t category_pid = fork();
            if (category_pid == 0) {
                // Child process for category
                process_category(category_path, client, found_goods, filepaths, good_count, tids);
                exit(0);
            } else if (category_pid > 0) {
                snprintf(log_message, sizeof(log_message), "Process %d created for category %s", category_pid, category_path);
                log_activity(log_message, client->user_name, shop_name, category_path);
            } else {
                perror("fork");
                exit(1);
            }
        }
    }

    // Wait for all category processes to finish
    while (wait(NULL) > 0);

    closedir(d);
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
// void *calculate_total_price(void *arg) {
//     Client *client = (Client *)arg;
//     int total_price = 0;
//     for (int i = 0; i < client->shopping_count; i++) {
//         total_price += client->shopping_list[i].price;
//     }
//     // Check if discount applies
//     if (has_shopped_before(client, client->shopping_list[0].name)) {
//         total_price *= 0.9; // Apply 10% discount
//     }
//     char log_message[100];
//     snprintf(log_message, sizeof(log_message), "Total price for client %s is %d", client->user_name, total_price);
//     log_activity(log_message,client->user_name);
//     return NULL;
// }

// Thread function to update the amount of goods
void *update_goods_amount(void *arg) {
    // Placeholder for updating the amount of goods in the shop
    // log_activity("Goods amount updated.", );
    return NULL;
}

// Function to get client data from terminal input
void get_client_data(Client *client) {
    char line[MAX_LINE_LENGTH];
    client->shopping_count = 0;
    client->price_treshold = -1; // No price threshold by default

    printf("Enter user name: ");
    fgets(client->user_name, sizeof(client->user_name), stdin);
    client->user_name[strcspn(client->user_name, "\n")] = 0; // Remove newline character

    printf("Enter shopping list (format: item amount) and type 'end' to finish:\n");
    while (1) {
        printf("Item %d: ", client->shopping_count + 1);
        fgets(line, sizeof(line), stdin);
        line[strcspn(line, "\n")] = 0; // Remove newline character
        if (strcmp(line, "end") == 0) {
            break;
        }
        sscanf(line, "%s %d", client->shopping_list[client->shopping_count].name, &client->shopping_list[client->shopping_count].entity);
        client->shopping_list[client->shopping_count].price = 0; // Default price (will be set later)
        client->shopping_list[client->shopping_count].point = 0; // Default point
        client->shopping_count++;
    }

    printf("Enter price threshold (or press Enter to skip): ");
    fgets(line, sizeof(line), stdin);
    line[strcspn(line, "\n")] = 0; // Remove newline character
    if (strlen(line) > 0) {
        client->price_treshold = atoi(line);
    }
}


void *orders_thread(void *arg) {
    Client *client = (Client *)arg;
    char log_message[256];
    Good best_list[MAX_GOODS];
    float highest_score = 0.0;
    int shop_count = 3; // Assume there are 3 shops

    for (int i = 0; i < shop_count; i++) {
        // Simulate receiving order lists from each store
        Good order_list[MAX_GOODS];
        int order_count = 0;

        // Populate order_list from store i (simulation)
        // In real scenario, we would receive this data from the stores
        // Here we are just filling it with some sample data for demonstration
        snprintf(order_list[order_count].name, sizeof(order_list[order_count].name), "Item%d", i);
        order_list[order_count].price = (i + 1) * 10;
        order_list[order_count].point = 4.5;
        order_list[order_count].entity = 5;
        order_list[order_count].score = order_list[order_count].price * order_list[order_count].point;
        order_count++;

        // Calculate the score of the current order list
        float total_score = 0.0;
        for (int j = 0; j < order_count; j++) {
            total_score += order_list[j].price * order_list[j].point;
        }

        // Compare with the highest score found so far
        if (total_score > highest_score) {
            highest_score = total_score;
            memcpy(best_list, order_list, sizeof(order_list));
        }
    }

    // Log the best order list
    snprintf(log_message, sizeof(log_message), "Best order list found with highest score: %.2f", highest_score);
    log_activity(log_message, client->user_name, NULL, NULL);

    // Store the best list in the client structure for further processing
    memcpy(client->shopping_list, best_list, sizeof(best_list));
    client->shopping_count = MAX_GOODS; // Set the correct count based on actual data

    pthread_exit(NULL);
}


void *final_thread(void *arg) {
    Client *client = (Client *)arg;
    char log_message[256];
    float total_price = 0.0;

    // Calculate total price of the best order list
    for (int i = 0; i < client->shopping_count; i++) {
        total_price += client->shopping_list[i].price;
    }

    // Check if the total price exceeds the client's price threshold
    if (total_price > client->price_treshold) {
        snprintf(log_message, sizeof(log_message), "Total price %.2f exceeds client's price threshold of %d. No purchase made.", total_price, client->price_treshold);
        log_activity(log_message, client->user_name, NULL, NULL);
    } else {
        snprintf(log_message, sizeof(log_message), "Total price %.2f is within client's price threshold of %d. Proceeding with purchase.", total_price, client->price_treshold);
        log_activity(log_message, client->user_name, NULL, NULL);

        // Simulate updating the entity of each good after purchase
        for (int i = 0; i < client->shopping_count; i++) {
            client->shopping_list[i].entity -= 1; // Reduce the entity by 1 (for demonstration purposes)
        }
    }

    pthread_exit(NULL);
}


void *update_thread(void *arg) {
    Client *client = (Client *)arg;
    char log_message[256];

    // Get the point for each good and update the goods file
    for (int i = 0; i < client->shopping_count; i++) {
        float new_point;
        printf("Please rate the good %s (0 to 5): ", client->shopping_list[i].name);
        scanf("%f", &new_point);

        client->shopping_list[i].point = new_point;
        client->shopping_list[i].score = client->shopping_list[i].price * client->shopping_list[i].point;

        // Update the good's file
        char *filepath = client->shopping_list[i].file_path; // Assume we store the file path in the client structure
        FILE *file = fopen(filepath, "r+");
        if (file == NULL) {
            perror("Error opening good file for updating");
            exit(1);
        }

        // Read the file into memory, update the score, and write it back
        char file_content[1024] = {0};
        size_t read_size = fread(file_content, 1, sizeof(file_content) - 1, file);
        rewind(file);
        fprintf(file, "Name: %s\nPrice: %d\nScore: %.2f\nEntity: %d\nLast Modified: %s\n",
                client->shopping_list[i].name, client->shopping_list[i].price, client->shopping_list[i].point, client->shopping_list[i].entity, client->shopping_list[i].last_modified);
        ftruncate(fileno(file), strlen(file_content));
        fclose(file);

        // Log the update
        snprintf(log_message, sizeof(log_message), "Updated file for good %s with new point %.2f", client->shopping_list[i].name, new_point);
        log_activity(log_message, client->user_name, NULL, NULL);
    }

    pthread_exit(NULL);
}


int main() {
    // Initialize mutex and semaphore
    pthread_mutex_init(&log_mutex, NULL);
    sem_init(&sem, 0, 0);

    int client_count;
    printf("Enter number of clients: ");
    scanf("%d", &client_count);
    getchar(); // Consume newline character after integer input

    Client clients[client_count];

    // Get data for each client
    for (int i = 0; i < client_count; i++) {
        printf("\nClient %d:\n", i + 1);
        get_client_data(&clients[i]);
    }

    // Create a process for each client
    for (int i = 0; i < client_count; i++) {
        int main_pipe[2];
        if (pipe(main_pipe) == -1) {
            perror("pipe");
            exit(1);
        }

        pid_t client_pid = fork();
        if (client_pid == 0) {
            // Child process for client
            close(main_pipe[0]); // Close reading end in child

            // Create threads for orders, final, and update
            pthread_t orders_tid, final_tid, update_tid;
            pthread_create(&orders_tid, NULL, orders_thread, &clients[i]);
            pthread_create(&final_tid, NULL, final_thread, &clients[i]);
            pthread_create(&update_tid, NULL, update_thread, &clients[i]);

            // Get shop directories
            char *shops[] = {"Store1", "Store2", "Store3"};
            int shop_count = sizeof(shops) / sizeof(shops[0]);

            Good found_goods[MAX_GOODS];
            char filepaths[MAX_GOODS][256];
            pthread_t tids[MAX_GOODS];
            int good_count = 0;

            // Create a process for each store
            for (int j = 0; j < shop_count; j++) {
                pid_t shop_pid = fork();
                if (shop_pid == 0) {
                    // Child process for store
                    process_shop(shops[j], &clients[i], found_goods, filepaths, &good_count, tids);
                    exit(0);
                } else if (shop_pid > 0) {
                    // Parent process logs the creation of the shop process
                    char log_message[256];
                    snprintf(log_message, sizeof(log_message), "Client %s created process for %s with PID: %d", clients[i].user_name, shops[j], shop_pid);
                    log_activity(log_message, clients[i].user_name, shops[j], NULL);
                } else {
                    perror("fork");
                    exit(1);
                }
            }

            // Wait for all shop processes to finish
            while (wait(NULL) > 0);

            // Join threads
            pthread_join(orders_tid, NULL);
            pthread_join(final_tid, NULL);
            pthread_join(update_tid, NULL);

            close(main_pipe[0]);
            exit(0);
        } else {
            // Parent process logs the creation of the client process
            char log_message[256];
            snprintf(log_message, sizeof(log_message), "Client %s created process with PID: %d", clients[i].user_name, client_pid);
            log_activity(log_message, clients[i].user_name, NULL, NULL);
        }
    }

    // Wait for all client processes to finish
    while (wait(NULL) > 0);

    // Destroy mutex and semaphore
    pthread_mutex_destroy(&log_mutex);
    sem_destroy(&sem);

    printf("All processing completed. Check the individual log files for details and results.\n");
    return 0;
}

