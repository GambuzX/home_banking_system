#include <pthread.h>
#include <stdio.h>
#include <semaphore.h>
#include "sync.h"
#include "sync_log.h"
#include "../shared/constants.h"

static sem_t full, empty;
static pthread_mutex_t accounts_db_mutex[MAX_BANK_ACCOUNTS];
static pthread_mutex_t req_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t active_office_mutex = PTHREAD_MUTEX_INITIALIZER;


int lock_log_mutex(){
	return pthread_mutex_lock(&log_mutex);
}

int unlock_log_mutex(){
	int ret;
	if((ret = pthread_mutex_unlock(&log_mutex)) != 0){
		perror("pthread_mutex_unlock:");
		return ret;
	}
	return 0;
}

int lock_active_office_mutex(){
	return pthread_mutex_lock(&active_office_mutex);
}

int unlock_active_office_mutex(){
	int ret;
	if((ret = pthread_mutex_unlock(&active_office_mutex)) != 0){
		perror("pthread_mutex_unlock:");
		return ret;
	}
	return 0;
}

int init_sync(uint32_t n_threads){
	int ret;

    if ((ret = syncLogSyncMechSem(STDOUT_FILENO, MAIN_THREAD_ID, SYNC_OP_SEM_INIT, SYNC_ROLE_PRODUCER, MAIN_THREAD_ID, 0)) < 0) 
	{
		perror("syncLogSyncMechSem error");
		return ret;
	}

    /* Initialize FULL semaphore with 0 */
    if (sem_init(&full, SHARED_SEM, 0) != 0)
		return -1;

    if ((ret = syncLogSyncMechSem(STDOUT_FILENO, MAIN_THREAD_ID, SYNC_OP_SEM_INIT, SYNC_ROLE_PRODUCER, MAIN_THREAD_ID, n_threads)) < 0) 
	{
		perror("syncLogSyncMechSem error");
		return ret;
	}

    /* Initialize EMPTY semaphore with number of threads */
    if (sem_init(&empty, SHARED_SEM, n_threads) != 0)
		return -1;

	/* Initialize account db corresponding mutexes */
	for (uint32_t i = 0; i < MAX_BANK_ACCOUNTS; i++)
        if(pthread_mutex_init(&accounts_db_mutex[i], NULL) != 0)
			return -2;
	
	return 0;
}

int del_sync(){

	/* Destroy FULL semaphore */
    if(sem_destroy(&full) != 0)
        return -1;
	
	/* Destroy EMPTY semaphore */
    if(sem_destroy(&empty) != 0)
        return -1;

    /* Destroy account db mutexes */
    for(uint32_t i = 0; i < MAX_BANK_ACCOUNTS; i++)
        if(pthread_mutex_destroy(&accounts_db_mutex[i]) != 0) 
		    return -2;

    /* Destroy request queue mutex */
	if(pthread_mutex_destroy(&req_queue_mutex) != 0)
		return -2;
	
	/* Destroy log mutex */
	if(pthread_mutex_destroy(&log_mutex) != 0)
		return -2;
	
	/* Destroy active office mutex */
	if(pthread_mutex_destroy(&active_office_mutex) != 0)
		return -2;

	return 0;
}

int get_value_sem_empty(int * result){
	int ret;
	if ((ret = sem_getvalue(&empty, result)) != 0)
	{
		perror("sem_get_value:");
		return ret;
	}
	if(*result < 0)
		*result = 0;
	return 0;
}

int wait_sem_empty(int id, pid_t sid){
	int empty_aux, ret;
	if ((ret = get_value_sem_empty(&empty_aux)) != 0) {
		perror("sem_get_value:");
		return ret;		
	}

	if ((ret = syncLogSyncMechSem(STDOUT_FILENO, id, SYNC_OP_SEM_WAIT, SYNC_ROLE_PRODUCER, sid, empty_aux)) < 0) {
		perror("syncLogSyncMechSem error");
		return ret;
	}
	
	return sem_wait(&empty);
}

int post_sem_empty(int id, pid_t sid){
	int empty_aux, ret;

	if((ret = sem_post(&empty)) != 0){
		perror("sem_post:");
		return ret;
	}

	if ((ret = get_value_sem_empty(&empty_aux)) != 0)
	{
		perror("sem_get_value:");
		return ret;
	}

	if ((ret = syncLogSyncMechSem(STDOUT_FILENO, id, SYNC_OP_SEM_POST, SYNC_ROLE_CONSUMER, sid, empty_aux)) < 0) {
		perror("syncLogSyncMechSem error");
		return ret;
	}

	return 0;
}

int get_value_sem_full(int * result){
	int ret;
	if ((ret = sem_getvalue(&full, result)) != 0)
	{
		perror("sem_get_value:");
		return ret;
	}
	if(*result < 0)
		*result = 0;
	return 0;
}

int wait_sem_full(int id, pid_t sid){
	int full_aux, ret;
	if ((ret = get_value_sem_full(&full_aux)) != 0)
	{
		perror("sem_get_value:");
		return ret;
	}

	if ((ret = syncLogSyncMechSem(STDOUT_FILENO, id, SYNC_OP_SEM_WAIT, SYNC_ROLE_CONSUMER, sid, full_aux)) < 0) {
		perror("syncLogSyncMechSem error");
		return ret;
	}

	return sem_wait(&full);
}

int post_sem_full(int id, pid_t sid){
	int full_aux, ret;

	if ((ret = sem_post(&full)) != 0){
		perror("sem_post:");
		return ret;
	}
	
	if ((ret = get_value_sem_full(&full_aux)) != 0){
		perror("sem_get_value:");
		return ret;
	}

	if ((ret = syncLogSyncMechSem(STDOUT_FILENO, id, SYNC_OP_SEM_POST, SYNC_ROLE_PRODUCER, sid, full_aux)) < 0) {
		perror("syncLogSyncMechSem error");
		return ret;
	}

	return 0;
}

int lock_queue_mutex(int balcony_id, sync_role_t role, pid_t sid){
	
	int ret;
	if ((ret = syncLogSyncMech(STDOUT_FILENO, balcony_id, SYNC_OP_MUTEX_LOCK, role, sid)) < 0) {
		perror("syncLogSyncMechSem error");
		return ret;
	}
	
	return pthread_mutex_lock(&req_queue_mutex);
}

int unlock_queue_mutex(int balcony_id, sync_role_t role, pid_t sid){
	int ret;

	if((ret = pthread_mutex_unlock(&req_queue_mutex)) != 0){
		perror("pthread_mutex_unlock:");
		return ret;
	}

    if((ret = syncLogSyncMech(STDOUT_FILENO, balcony_id, SYNC_OP_MUTEX_UNLOCK, role, sid)) < 0) {
		perror("syncLogSyncMechSem error");
		return ret;
	}

	return 0;
}

int lock_accounts_db_mutex(int id, sync_role_t role, pid_t sid){
	int ret;

	if ((ret = syncLogSyncMech(STDOUT_FILENO, id, SYNC_OP_MUTEX_LOCK, role, sid)) < 0) {
		perror("syncLogSyncMech:");
		return ret;
	}

    return pthread_mutex_lock(&accounts_db_mutex[sid]);
}

int unlock_accounts_db_mutex(int id, sync_role_t role, pid_t sid) {
	int ret;

	if((ret = pthread_mutex_unlock(&accounts_db_mutex[sid])) != 0){
		perror("pthread_mutex_unlock:");
		return ret;
	}

	if((ret = syncLogSyncMech(STDOUT_FILENO, id, SYNC_OP_MUTEX_UNLOCK, role, sid)) < 0){
		perror("syncLogSyncMech:");
		return ret;
	}

	return 0;
}