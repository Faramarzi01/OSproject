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

#define MAX_GOODS 700
#define MAX_SHOPS 3
#define LOG_FILE "logfile.log"
#define RESULT_FILE "results.txt"
#define PIPE_MSG_SIZE 256
#define MAX_NAME_LENGTH 20
#define MAX_LINE_LENGTH 256

// ---------------------------------- global variables and structures ---------------------------
// Structure for goods
typedef struct {
    char name[20];
    int price;
    float point;
    int entity;
    char last_modified[30];
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

// a strucure for saving the good and client data:
typedef struct {
    Good *good;
    Client *client;
} GoodClientArgs;


// Structure for shop
typedef struct {
    char shop_name[20];
    Good goods[MAX_GOODS];
    int good_count;
    float point;
} Shop;

// Mutex for logging
pthread_mutex_t log_mutex;
// Semaphore for synchronization
sem_t sem;

// Thread structure for processing goods
typedef struct {
    Good good;
    Client *client;
    char filepath[256];
} GoodThreadArgs;

// ----------------------------------------- functions ---------------------------------------
// Function to log thread activity
void log_activity(const char *message) {
    pthread_mutex_lock(&log_mutex);

    FILE *log_file = fopen(LOG_FILE, "a");
    if (!log_file) {
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
    if (!file) {
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

// Function to check and make a shopping and save it
void *process_good(void *arg) {
    GoodThreadArgs *args = (GoodThreadArgs *)arg;
    Good *good = &args->good;
    Client *client = args->client;
    char *filepath = args->filepath;

    char log_message[100];
    int total_price = 0;

    // Check if the amount of good required is available
    for (int i = 0; i < client->shopping_count; i++) {
        if (strcmp(client->shopping_list[i].name, good->name) == 0) {
            if (client->shopping_list[i].entity <= good->entity) {
                total_price = client->shopping_list[i].entity * good->price;
                snprintf(log_message, sizeof(log_message), "Processed good %s with total price %d for amount %d",
                         good->name, total_price, client->shopping_list[i].entity);
                log_activity(log_message);

                // Show the result to the client
                printf("Good: %s, Price: %d, Entity: %d, Total Price: %d\n", good->name, good->price, good->entity, total_price);

                // Ask the client to rate the good (mmkf)------------ begin
                // float new_point;
                // printf("Please rate this good (0 to 5): ");
                // scanf("%f", &new_point);
                // good->point = (new_point + good->point) / 2.0;
                // good->score = good->price * good->point;
                // -------------------------------------------------- end


                // update the amount of good in shop (mmkf) ---------- begin
                update_goods_amount(good, client->shopping_list[i].entity);
                // --------------------------------------------------- end

                // Update the good's file with the new rating
                FILE *file = fopen(filepath, "r+");
                if (!file) {
                    perror("Error opening good file for updating");
                    return NULL;
                }

                // Read the file into memory, update the score, and write it back
                char file_content[1024] = {0};
                size_t read_size = fread(file_content, 1, sizeof(file_content) - 1, file);
                rewind(file);
                fprintf(file, "Name: %s\nPrice: %d\nScore: %.2f\nEntity: %d\nLast Modified: %s\n",
                        good->name, good->price, good->point, good->entity, good->last_modified);
                ftruncate(fileno(file), strlen(file_content));
                fclose(file);


                sem_post(&sem); // Signal that processing is done
                return NULL;
            } else {
                snprintf(log_message, sizeof(log_message), "Not enough entity for good %s. Required: %d, Available: %d",
                         good->name, client->shopping_list[i].entity, good->entity);
                log_activity(log_message);
            }
        }
    }

    snprintf(log_message, sizeof(log_message), "Good %s not in client's shopping list", good->name);
    log_activity(log_message);
    sem_post(&sem); // Signal that processing is done
    return NULL;
}

// Function for processing a category and writing results to file
void process_category(const char *category_path, Client *client, int pipe_fd) {
    DIR *d;
    struct dirent *dir;
    d = opendir(category_path);
    if (!d) {
        perror("Error opening category directory");
        char log_message[100];
        snprintf(log_message, sizeof(log_message), "Error opening category directory: %s", category_path);
        log_activity(log_message);
        return;
    }

    pthread_t threads[MAX_GOODS];
    GoodThreadArgs args[MAX_GOODS];
    int good_count = 0;

    Good best_good;
    best_good.score = 0;

    while (dir = readdir(d)) {
        if (dir->d_type == DT_REG && strstr(dir->d_name, ".txt")) { // If it's a regular file and a .txt file
            char filepath[256];
            snprintf(filepath, sizeof(filepath), "%s/%s", category_path, dir->d_name);

            read_good_data(filepath, &args[good_count].good);
            args[good_count].client = client;
            strcpy(args[good_count].filepath, filepath);

            pthread_create(&threads[good_count], NULL, process_good, &args[good_count]);

            // Ask the client to rate the good (mmkf) ----------- begin
            float new_point;
            printf("Please rate this good (0 to 5): ");
            scanf("%f", &new_point);
            good->point = (new_point + good->point) / 2.0;
            good->score = good->price * good->point;
            // -------------------------------------------------- end

            sem_wait(&sem); // Wait for the thread to signal completion

            if (args[good_count].good.score > best_good.score) {
                best_good = args[good_count].good;
            }
            good_count++;
        }
    }

    for (int i = 0; i < good_count; i++) {
        pthread_join(threads[i], NULL);
    }

    if (good_count > 0) {
        // Log best good
        char log_message[256];
        snprintf(log_message, sizeof(log_message), "Best Good in Category %s: %s, Price: %d, Point: %.2f, Score: %.2f",
                 category_path, best_good.name, best_good.price, best_good.point, best_good.score);
        log_activity(log_message);

        // Show the best good to the client
        printf("Best Good in Category %s: %s, Price: %d, Point: %.2f, Score: %.2f\n",
               category_path, best_good.name, best_good.price, best_good.point, best_good.score);

        // Send message to parent process
        char pipe_msg[PIPE_MSG_SIZE];
        snprintf(pipe_msg, sizeof(pipe_msg), "Best Good in %s: %s, Price: %d, Point: %.2f, Score: %.2f",
                 category_path, best_good.name, best_good.price, best_good.point, best_good.score);
        write(pipe_fd, pipe_msg, strlen(pipe_msg) + 1);
    }

    closedir(d);
    // Notify parent process that category processing is complete
    write(pipe_fd, "done", 4);
}

// Function for processing a shop and writing results to file
void process_shop(const char *shop_name, Client *client, int pipe_fd) {
    char log_message[100];
    snprintf(log_message, sizeof(log_message), "Accessed Store %s", shop_name);
    log_activity(log_message);

    DIR *d;
    struct dirent *dir;
    d = opendir(shop_name);
    if (!d) {
        perror("Error opening shop directory");
        snprintf(log_message, sizeof(log_message), "Error opening shop directory: %s", shop_name);
        log_activity(log_message);
        return;
    }

    while (dir = readdir(d)) {
        if (dir->d_type == DT_DIR && strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0) {
            // If it's a directory and not "." or ".."
            char category_path[256];
            snprintf(category_path, sizeof(category_path), "%s/%s", shop_name, dir->d_name);
            pid_t category_pid = fork();
            if (category_pid == 0) {
                // Child process for category
                process_category(category_path, client, pipe_fd);
                exit(0);
            }
        }
    }

    // Wait for all category processes to finish
    while (wait(NULL) > 0);

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

// Thread function to update the amount of goods (mmkf)
void *update_goods_amount(Good *good, int entity) {
    // Placeholder for updating the amount of goods in the shop
    GoodThreadArgs *args = (GoodThreadArgs *)arg;
    char *filepath = args->filepath;
    good->entity -= entity;
    log_activity("Goods amount updated.");
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
        if (!strcmp(line, "end")) {
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
        if (!client_pid) {
            // Child process for client
            close(main_pipe[0]); // Close reading end in child

            // Get shop directories
            char *shops[] = {"/home/faramarzi/OSdataset/Store1", "/home/faramarzi/OSdataset/Store2", "/home/faramarzi/OSdataset/Store2"};
            int shop_count = sizeof(shops) / sizeof(shops[0]);

            // Create pipes for communication with shop processes
            int pipes[MAX_SHOPS][2];
            for (int j = 0; j < shop_count; j++) {
                if (pipe(pipes[j]) == -1) {
                    perror("pipe");
                    exit(1);
                }
            }

            for (int j = 0; j < shop_count; j++) {
                pid_t shop_pid = fork();
                if (!shop_pid) {
                    // Child process for shop
                    close(pipes[j][0]); // Close reading end in child
                    process_shop(shops[j], &clients[i], pipes[j][1]);
                    close(pipes[j][1]); // Close writing end in child
                    exit(0);
                }
            }

            // Close writing ends in parent
            for (int j = 0; j < shop_count; j++) {
                close(pipes[j][1]);
            }

            // Wait for all shop processes to finish and read from pipes
            char buffer[PIPE_MSG_SIZE];
            while (read(main_pipe[0], buffer, PIPE_MSG_SIZE) > 0) {
                printf("Message from shop: %s\n", buffer);
            }

            close(main_pipe[0]);
            exit(0);
        }
    }

    // Wait for all client processes to finish
    while (wait(NULL) > 0);

    // Destroy mutex and semaphore
    pthread_mutex_destroy(&log_mutex);
    sem_destroy(&sem);

    printf("All processing completed. Check %s for logs and %s for results.\n", LOG_FILE, RESULT_FILE);
    return 0;
}

