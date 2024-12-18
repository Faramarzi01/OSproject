//-------------------------------------------- example for shared memory ----------------------------------
pthread_mutex_t shared_mutex; // Mutex for shared data access
Good[] sharedGoods;             // define it global
int finishGoods = 0;          // global int for counting threads that finish their process

//------------client thread---------------------
// how to wait for goods information and processing order
while (finishGoods != client->shopping_count);
pthread_mutex_lock(&shared_mutex);
for (int i = 0; i < client->shopping_count; i++)
{
    // write code for show goods in sharedGoods
}
pthread_mutex_unlock(&shared_mutex);


//-------------goods threads-----------------------


while (&shared_mutex);
pthread_mutex_lock(&shared_mutex);
// write code for processing each good from files or...
sharedGoods[finishGoods] = my_good;
finishGoods++;
pthread_mutex_unlock(&shared_mutex);


//-------------------------------------------- prototypes of functions ----------------------------------
// add this lines between structures and function implementions
void log_activity(const char *message);
void read_good_data(const char *filename, Good *good);
void *process_good(void *arg);
void process_category(const char *category_path, Client *client, int pipe_fd);
void process_shop(const char *shop_name, Client *client, int pipe_fd);
int has_shopped_before(Client *client, const char *shop_name);
void *calculate_total_price(void *arg);
void *update_goods_amount(Good *good, int entity);
void get_client_data(Client *client);