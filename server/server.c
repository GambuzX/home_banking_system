#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <semaphore.h>
#include "../shared/vetor.h"
#include "../shared/utilities.h"
#include "../shared/constants.h"
#include "../shared/types.h"
#include "../shared/crypto.h"
#include "../shared/queue.h"

#define SHARED 0

sem_t full, empty;
pthread_mutex_t mutex_accounts_database = PTHREAD_MUTEX_INITIALIZER;
vetor *accounts_database;
queue *resquest_queue;

void *balconies(void *arg)
{
  // pseudo codigo

  /*Process Consumer;
    ...
    Repeat
    ...
    Wait(Full);
    Wait(Mutex);
    Item=Take();
    Signal(Mutex);
    Signal(Empty);
    Consume(Item);
    ...
    Until ...;*/
}

int main(int argc, char *argv[])
{

    if (argc != 3)
    {
        printf("Usage: %s <n_threads> <admin_pwd>\n", argv[0]);
        exit(RC_OTHER);
    }

    int n_threads_console = atoi(argv[1]);

    if (n_threads_console <= 0)
    {
        printf("Error: The numbers of threads cannot be negative or zero\n");
        exit(RC_OTHER);
    }



    accounts_database = vetor_novo();
    resquest_queue = init_queue();
    int n_threads;
    n_threads = min(atoi(argv[1]), MAX_BANK_OFFICES);

    //   INITIALIZING SEMAPHORES , PARA DEPOIS  QUANDO ESTIVEREMOS A TRATAR DA SINCRONIZAÇAO

    /* if (sem_init(&full, SHARED, 0) != 0)
    {
        perror("sem_init: erro initializing semaphore");
        exit(RC_OTHER);
    }
    
    if (sem_init(&empty, SHARED, n_threads_console) != 0)
    {
        perror("sem_init: erro initializing semaphore empty");
        exit(RC_OTHER);
    }*/

    int id;
    pthread_t thread_id[MAX_BANK_OFFICES];

    for (int i = 0; i < n_threads; i++)
    {
        if (pthread_create(&thread_id[i], NULL, balconies, &id) != 0)
        {
            perror("pthread_create: error creating thread ");
            exit(RC_OTHER);
        }
    }

    srand(time(NULL));

    int secure_svr;


    if (mkfifo(SERVER_FIFO_PATH, 0660) < 0)
    {
        if (errno != EEXIST)
        {
            printf("Could not create fifo %s\n", SERVER_FIFO_PATH);
            exit(RC_OTHER);
        }
    }

    // CREATE METHOD TO ADD ACCOUNTS - store in a list???
    bank_account_t admin_account = {ADMIN_ACCOUNT_ID, "hash", "salt", 0};
    gen_salt(admin_account.salt, SALT_LEN + 1, SALT_LEN);
    gen_hash(argv[2], admin_account.salt, admin_account.hash);

    vetor_insere(accounts_database, &admin_account, -1);

    if ((secure_svr = open(SERVER_FIFO_PATH, O_RDWR)) == -1)
    {
        perror("open");
        exit(RC_SRV_DOWN);
    }

    tlv_request_t request;

    while (1)
    {
        if (read(secure_svr, &request, sizeof(tlv_request_t)) != 0)
        {
            printf("header :   pid : %d , account_id %d , password %s, delay %d\n", request.value.header.pid, request.value.header.account_id, request.value.header.password, request.value.header.op_delay_ms);
            printf("transfer:  accoutn_id %d, ammount %d\n", request.value.transfer.account_id, request.value.transfer.amount);
            printf("create:  accoutn_id %d, balance %d, password %s\n", request.value.create.account_id, request.value.create.balance, request.value.create.password);
        }

        // inserir na queue
        // pseudo codigo

        /*   Process Producer;
        ...
        Repeat
        ...
        Produce(Item);
        Wait(Empty);
        Wait(Mutex);
        Append(Item);
        Signal(Mutex);
        Signal(Full);
        ...
        Until ...;*/
    }

    if (fchmod(secure_svr, 0444) != 0)
    {
        perror("fchmod: error altering server fifo permissions");
        exit(RC_OTHER);
    }

    // Free allocated memory

    if (empty_queue(resquest_queue) != 0)
    {
        perror("empt_queue: error emptying queue");
        exit(RC_OTHER);
    }

    free(resquest_queue);

    if (vetor_free(accounts_database) != 0)
    {
        perror("vetor_free: error emptying vector");
        exit(RC_OTHER);
    }

    free(accounts_database);

    if (close(secure_svr) != 0)
    {
        perror("close: error closing down server fifo");
        exit(RC_OTHER);
    }
    if (unlink(SERVER_FIFO_PATH) != 0)
    {
        perror("unlink: error unlinking user fifo");
        exit(RC_OTHER);
    }

    return RC_OK;
}