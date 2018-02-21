/* 2PC master node */

#include <pthread.h>
#include <unistd.h>
#include "tpc.h" /* generated by rpcgen */
#include "vote.h"
#include "tpc_master.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int *results;
CLIENT **cl_arr;
char **args;

#define TEST_MASTER_DEATH 0

/* the index in the thread reaping loop at which master dies */
#define KILL_INDEX 1

typedef struct thread_arg {
    pthread_t tid;
    commit_vec_args *commit_args;
} thread_arg;

int main(int argc, char *argv[])
{

    if (argc < 2) {
        printf("Please provide IP addresses of slave nodes.\n");
        return 1;
    }

    // TODO Jahrme
    // if you decide to use message queue, try this 1-vec-at-a-time
    // message passing algorithm
    //
    // while (queue is empty)
    //  while (queue is not empty)
    //    pop message from queue
    //    2-phase commit agreement w/ slaves
    //    if all agree, make RPC calls w/ message args

    // argv[1..n] are the IP addresses of the slave nodes.
    int num_slaves = argc - 1;
    args = argv;
    pthread_t tids[num_slaves];
    results = (int *) malloc(num_slaves * sizeof(int));
    cl_arr = (CLIENT **) malloc(num_slaves * sizeof(CLIENT *));
    //tids = (pthread_t *) malloc(num_slaves * sizeof(pthread_t));

    /* Phase 1 */
    int i;
    for (i = 0; i < num_slaves; i++) {
        results[i] = 0;
        cl_arr[i] = NULL;
        pthread_create(&tids[i], NULL, get_commit_resp, (void *) i);
    }

    /* allow processes some time to vote */
    sleep(TIME_TO_VOTE);
    int successes = 0;
    // kill all threads
    for (i = 0; i < num_slaves; i++) {
        if (TEST_MASTER_DEATH && i == KILL_INDEX) exit(0);
        /* should add 1 if result was received */
        successes += results[i];
        pthread_cancel(tids[i]);
    }
    /* Phase 2 */
    if (successes == num_slaves) {
        /* broadcast COMMIT to all slaves */
        printf("Everyone agreed to commit! Woohoo!\n");
        for (i = 0; i < num_slaves; i++) {
            results[i] = 0;
            cl_arr[i] = NULL;
            pthread_t tid = (pthread_t) i;
            struct thread_arg *targ = (struct thread_arg *) malloc(sizeof(struct thread_arg));

            /* TODO: Jahrme add the IPC arguments here */
            commit_vec_args *cargs = (commit_vec_args *) malloc(sizeof(commit_vec_args));
            unsigned int vec_id = (unsigned int) i;
            unsigned long long vec = (unsigned long long) i;
            cargs->vec_id = vec_id;
            cargs->vec = vec;
            targ->tid = tid;
            targ->commit_args = cargs;

            /*
             * FIXME: code after pthread_create should go in thread,
             * but it crashes for some reason.
             */
            //pthread_create(&tid, NULL, commit_bit_vector, (void*)targ);
            CLIENT *cl = clnt_create(args[i + 1], TWO_PHASE_COMMIT_VEC, TPC_COMMIT_VEC_V1, "tcp");

            /* TODO: test point of failure in this loop */

            if (cl == NULL) {
                printf("Error: could not connect to slave %s.\n", args[i + 1]);
                continue;
            }
            int *result = commit_vec_1(cargs, cl);
            printf("Result = %d\n", *result);
            if (result == NULL) {
                printf("Commit failed.\n");
            }
            else {
                printf("Commit succeeded.\n");
            }
        }
    }
    else {
        /* TODO: following the TPC protocol,
         * multicast ABORT to all slaves
         * but it's not that necessary in this case.
         */
        printf("Failed to find all slaves\n");
        return 1;
    }
    free(results);
    free(cl_arr);
    return 0;
}

void *get_commit_resp(void *tid)
{
    int i = (intptr_t)tid;
    cl_arr[i] = clnt_create(args[i + 1], TWO_PHASE_COMMIT_VOTE, TWO_PHASE_COMMIT_VOTE_V1, "tcp");
    if (cl_arr[i] == NULL) {
        printf("Error: could not connect to slave %s.\n", args[i + 1]);
        return (void*) 1;
    }
    int *result = commit_msg_1(NULL, cl_arr[i]);
    printf("got result\n");
    if (result == NULL || *result == VOTE_ABORT) {
        printf("Couldn't commit at slave %s.\n", args[i + 1]);
    }
    else {
        results[i] = 1;
    }
    return (void*) 0;
}

// FIXME breaks when put in thread, for some reason.
void *commit_bit_vector(void *arg)
{
    struct thread_arg* a = (struct thread_arg*)arg;
    pthread_t tid = a->tid;
    int i = (int) tid;
    printf("about to commit %d: %lu\n", a->commit_args->vec_id, a->commit_args->vec);
    CLIENT* cl = clnt_create(args[i + 1], TWO_PHASE_COMMIT_VEC, TPC_COMMIT_VEC_V1, "tcp");

    if (cl == NULL) {
        printf("Error: could not connect to slave %s.\n", args[i + 1]);
        return (void*) 1;
    }
    int *result = commit_vec_1(a->commit_args, cl);
    printf("Result = %d\n", *result);
    if (result == NULL) {
        printf("Commit failed.\n");
        return (void*) 1;
    }
    printf("Commit succeeded.\n");
    return (void*) 0;
}