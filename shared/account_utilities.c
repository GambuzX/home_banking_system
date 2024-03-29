#include "account_utilities.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "constants.h"
#include "utilities.h"
#include "crypto.h"

int valid_account_id(int account_id){
	return account_id >= 0 && account_id < MAX_BANK_ACCOUNTS;
}

int valid_user_account_id(int account_id) {
	return valid_account_id(account_id) && account_id != 0;
}

int valid_pwd(char * pwd) {
    return strlen(pwd) >= MIN_PASSWORD_LEN && strlen(pwd) <= MAX_PASSWORD_LEN;
}

int authenticate(const char * pwd, bank_account_t * bank_account){
	if (bank_account->account_id == EMPTY_ACCOUNT_ID) return RC_ID_NOT_FOUND;
	char hash[HASH_LEN + 1];
	gen_hash(pwd, bank_account->salt, hash);
	int comp = strcmp(hash, bank_account->hash);
	return comp;
}


int create_account(const char * pwd, uint32_t account_id, uint32_t balance, bank_account_t * accounts_database[]){
	if(gen_salt(accounts_database[account_id]->salt, SALT_LEN + 1, SALT_LEN) != 0)
		return RC_OTHER;
    if(gen_hash(pwd, accounts_database[account_id]->salt, accounts_database[account_id]->hash) != 0)
		return RC_OTHER;
	accounts_database[account_id]->account_id = account_id;
	accounts_database[account_id]->balance = balance;
	return RC_OK;
}