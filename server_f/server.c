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
#include "../shared/utilities.h"
#include "../shared/constants.h"
#include "../shared/types.h"
#include "../shared/crypto.h"
#include "../shared/queue.h"
#include "../shared/sync.h"
#include "../shared/account_utilities.h"
#include "../shared/com_protocol.h"
#include "../shared/sope.h"
#include "server_parse.h"
#include "requests.h"


bank_account_t *accounts_database[MAX_BANK_ACCOUNTS];
int n_threads, balcony_open = 1;
queue_t *request_queue;

int dummy_connection;

void *balconies(void *arg)
{
    int id_thread = *(int *)arg;
    int pid_thread = pthread_self();
    int ret, req_ret;
    tlv_request_t *first_request;

    logBankOfficeOpen(STDOUT_FILENO, id_thread, pid_thread);

    /*
    * Passos:
    * 	1) Receber pedido
    *   2) Validar pedido
    * 	3) Se válido executar pedido
    * 	4) Responder ao cliente    
    */
    while (1)
    {
		
        /* Wait full */
		if((ret = wait_sem_full(id_thread)) != 0){
			fprintf(stderr, "wait_sem_full: error %d\n", ret);
            exit(RC_OTHER);
		}

        /* Server shutdown */
		if(!balcony_open && is_queue_empty(request_queue))
			break;

        /* Wait mutex */
		// TODO: ver este hardcoded 0 que estava
		if((ret = lock_queue_mutex(id_thread, SYNC_ROLE_CONSUMER, 0)) != 0){
			fprintf(stderr, "lock_queue_mutex: error %d\n", ret);
            exit(RC_OTHER);
		}

        /* Take item */
        first_request = (tlv_request_t *)queue_front(request_queue);
        if (first_request == NULL)
        {
            fprintf(stderr, "queue_front: error getting first queue element\n");
            exit(RC_OTHER);
        }

        if (queue_pop(request_queue) != 0)
        {
            fprintf(stderr, "queue_pop: error removing first queue element\n");
        }

        /* Signal mutex */
		if((ret = unlock_queue_mutex(id_thread, SYNC_ROLE_CONSUMER, first_request->value.header.pid)) != 0){
			fprintf(stderr, "unlock_queue_mutex: error %d\n", ret);
            exit(RC_OTHER);
		}

        /* Signal empty */
		if((ret = post_sem_empty(id_thread, first_request->value.header.pid)) != 0){
			fprintf(stderr, "post_sem_empty: error %d\n", ret);
            exit(RC_OTHER);
		}

        if (logRequest(STDOUT_FILENO, id_thread, first_request) < 0)
        {
            fprintf(stderr, "logRequest: error writing request to stdout\n");
        }

		uint32_t balance;
        int ret;
        if ((req_ret = is_valid_request(first_request, accounts_database)) == 0)
        { 
            switch (first_request->type)
            {
            case OP_CREATE_ACCOUNT:
                if (create_request(&first_request->value, accounts_database, id_thread) != 0)
                    fprintf(stderr, "create_request: failed to create account.\n");
                break;

            case OP_TRANSFER:
                if(transfer_request(&first_request->value, accounts_database, &balance, id_thread) != 0)
                    fprintf(stderr, "transfer_request: failed to perform transfer request.\n");
                break;

            case OP_BALANCE:
                if(balance_request(&first_request->value, accounts_database, &balance, id_thread) != 0)
                    fprintf(stderr, "balance_request: failed to perform balance request.\n");
                break;

            case OP_SHUTDOWN:
                if ((ret = shutdown_request(first_request, &balcony_open, id_thread, dummy_connection)) != RC_OK)
                    exit(ret);
                break;
                
			default:
				break;
            }
        }

        char secure_fifo_name[strlen(USER_FIFO_PATH_PREFIX) + WIDTH_PID + 1];
        init_secure_fifo_name(secure_fifo_name, first_request->value.header.pid);

        int user_fifo;
        if ((user_fifo = open(secure_fifo_name, O_WRONLY)) == -1)
        {
            perror("secure_fifo_name");
        }

        tlv_reply_t request_reply;		
        init_reply(&request_reply, first_request, req_ret, n_threads, balance);
		
        if (logReply(STDOUT_FILENO, id_thread, &request_reply) < 0)
        {
            fprintf(stderr, "logRequest: error writing reply to stdout\n");
        }

        /* Send reply to user */
        if (write_reply(user_fifo, &request_reply) != 0)
        {
            fprintf(stderr, "write_reply: error writing reply to server\n");
        }

        /* Close user specific fifo */
        if (close(user_fifo) != 0)
        {
            perror("error closing down server fifo");
        }

        free(first_request);
    }

    logBankOfficeClose(STDOUT_FILENO, id_thread, pid_thread);
	return NULL;
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <n_threads> <admin_pwd>\n", argv[0]);
        exit(RC_OTHER);
    }

    /* Generate new random seed */
    srand(time(NULL));

    n_threads = min(atoi(argv[1]), MAX_BANK_OFFICES);
    char *pwd = argv[2];
    int ret;

    if(!valid_args(n_threads, pwd)) {
        fprintf(stderr, "Invalid arguments given\n");
        exit(RC_OTHER);
    }

    /* Open log file and point STDOUT to it */
    int logfile;
    if ((logfile = open(SERVER_LOGFILE, O_WRONLY | O_CREAT | O_APPEND, 0644)) == -1) {
        perror("Open server log file");
        exit(RC_OTHER);
    }
    dup2(logfile, STDOUT_FILENO);

    /* Initialize requests queue */
    if ((request_queue = init_queue()) == NULL)
    {
        perror("init_queue: error initialing the queue");
        exit(RC_OTHER);
    }

	if((ret = init_sync(n_threads)) != 0){
		fprintf(stderr, "init_sync: error: %d\n", ret);
        exit(RC_OTHER);
	}
    
    /* Create server fifo */
    if (mkfifo(SERVER_FIFO_PATH, 0660) < 0)
    {
        if (errno != EEXIST)
        {
            fprintf(stderr, "Could not create fifo %s\n", SERVER_FIFO_PATH);
            exit(RC_OTHER);
        }
    }

    /* Create balconies */
    int id[n_threads];
    pthread_t thread_tid[n_threads];
    for (int i = 0; i < n_threads; i++)
    {
        id[i] = i + 1;
        if (pthread_create(&thread_tid[i], NULL, balconies, &id[i]) != 0)
        {
            perror("pthread_create: error creating thread ");
            exit(RC_OTHER);
        }
    }

    /* Allocate accounts_database */
    for (uint32_t i = 0; i < MAX_BANK_ACCOUNTS; i++)
    {
        accounts_database[i] = (bank_account_t *)malloc(sizeof(bank_account_t));
        accounts_database[i]->account_id = EMPTY_ACCOUNT_ID;
    }

    /* Create admin account */
    if ((ret = create_account(pwd, ADMIN_ACCOUNT_ID, 0, accounts_database)) != 0)
	{
        fprintf(stderr, "create_account: failed to create ADMIN_ACCOUNT. Error: %d\n", ret);
        exit(RC_OTHER);
    }

    /* Open server fifo in read mode */
    int secure_svr;
    if ((secure_svr = open(SERVER_FIFO_PATH, O_RDONLY)) == -1)
    {
        perror("open");
        exit(RC_SRV_DOWN);
    }

    /* Open server fifo in write mode */
    if ((dummy_connection = open(SERVER_FIFO_PATH, O_WRONLY)) == -1) {
        perror("dummy connection");
        exit(RC_SRV_DOWN);
    }

    while (balcony_open)
    {
        /* Produce item */
        tlv_request_t * request = (tlv_request_t*)malloc(sizeof(tlv_request_t));;
        int ret_val;
        if ((ret_val=read_request(secure_svr, request)) < 0)
        {
		    free(request);
            fprintf(stderr, "read_request: error reading the request\n");
            break;
        }

		if(!balcony_open)
			break;

        if (logRequest(STDOUT_FILENO, MAIN_THREAD_ID, request) < 0)
        {
            fprintf(stderr, "logRequest: error writing request to stdout\n");
            exit(RC_OTHER);
        }

        /* Wait empty */
		if((ret = wait_sem_empty(request->value.header.pid)) != 0){
			fprintf(stderr, "wait_sem_empty: error %d\n", ret);
            exit(RC_OTHER);
		}

        /* Wait mutex */
		if((ret = lock_queue_mutex(MAIN_THREAD_ID, SYNC_ROLE_ACCOUNT, request->value.header.pid)) != 0){
			fprintf(stderr, "lock_queue_mutex: error %d\n", ret);
            exit(RC_OTHER);
		}
       
        /* Append item */
        if (queue_push(request_queue, request) != 0)
        {
            fprintf(stderr, "queue_push: error pushing request to queue\n");
            exit(RC_OTHER);
        }

        /* Signal mutex */
		if((ret = unlock_queue_mutex(MAIN_THREAD_ID, SYNC_ROLE_ACCOUNT, request->value.header.pid)) != 0){
			fprintf(stderr, "lock_queue_mutex: error %d\n", ret);
            exit(RC_OTHER);
		}
        
        /* Signal full */
		if((ret = post_sem_full(request->value.header.pid)) != 0){
			fprintf(stderr, "lock_queue_mutex: error %d\n", ret);
            exit(RC_OTHER);
		}
    }

    /* Wait for threads to finish */
	for(int i = 0; i < n_threads; i++){
		post_sem_full(MAIN_THREAD_ID);
	}

    for(int i = 0; i < n_threads; i++){
        if(pthread_join(thread_tid[i], NULL) != 0)
			fprintf(stderr, "Error joininig thread %d\n", i);
	}

    /* Delete requests queue */
    if (del_queue(request_queue) != 0)
    {
        fprintf(stderr, "del_queue: error deleting queue\n");
        exit(RC_OTHER);
    }

    /* Delete sync mechanisms */
	if(del_sync() != 0){
		fprintf(stderr, "del_sync: error %d\n", ret);
        exit(RC_OTHER);
	}

    /* Free accounts database allocated memory */
    for (uint32_t i = 0; i < MAX_BANK_ACCOUNTS; i++)
        free(accounts_database[i]);

    /* Close server fifo descriptor */
    if (close(secure_svr) != 0)
    {
        perror("close: error closing down server fifo");
        exit(RC_OTHER);
    }

    /* Unlink server fifo */
    if (unlink(SERVER_FIFO_PATH) != 0)
    {
        perror("unlink: error unlinking user fifo");
        exit(RC_OTHER);
    }

    return RC_OK;
}