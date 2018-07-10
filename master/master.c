#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <time.h>

#include <sys/ipc.h>
#include <sys/types.h>

#include "master.h"
#include "master_rq.h"
#include "tpc_master.h"
#include "../bitmap-vector/read_vec.h"
#include "../consistent-hash/ring/src/tree_map.h"
#include "../util/ds_util.h"
#include "../experiments/fault_tolerance.h"

/* variables for use in all master functions */
//slave_ll *slavelist;
u_int slave_id_counter = 0;
partition_t partition;
query_plan_t query_plan;
u_int num_slaves;

/* Ring CH variables */
rbt *chash_table;

/* static partition variables */
int *partition_scale_1, *partition_scale_2; /* partitions and backups */
u_int num_keys; /* e.g., value of largest known key, plus 1 */
int separation;

/**
 * Master Process
 *
 * Coordinates/delegates tasks for the system.
 */
int main(int argc, char *argv[])
{
    int i, j;
    partition = RING_CH;
    query_plan = STARFISH;

    int c;
    num_slaves = fill_slave_arr(SLAVELIST_PATH, &slave_addresses);
    if (num_slaves == -1) {
        puts("Master: could not register slaves, exiting...");
        return 1;
    }
    if (num_slaves == 1) replication_factor = 1;

    if (partition == RING_CH) {
        chash_table = new_rbt();
    }

    /* index in slave list will be the machine ID (0 is master) */
    slave *s;
    for (i = 0; i < num_slaves; i++) {
        s = new_slave(slave_addresses[i]);
        if (setup_slave(s) && partition == RING_CH) { /* connected to slave? */
            insert_slave(chash_table, s);
        }
        else {
            printf("Failed to setup slave %s\n", slave_addresses[i]);
        }
    }

    /* Message queue setup */
    int msq_id = msgget(MSQ_KEY, MSQ_PERMISSIONS | IPC_CREAT), rc, qnum = 0;
    int slave_death_inst = FT_KILL_Q;
    ///bool killed = false;
    struct msgbuf *request;
    struct msqid_ds buf;
    u_int64_t pre_kill_times[FT_PREKILL_Q], post_kill_times[FT_POSTKILL_Q];
    u_int64_t pre_kill_tot = 0, post_kill_tot = 0;

    /* fault tolerance experiment output file */
    FILE *ft_exp_out;
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    char *timestamp = asctime(timeinfo) + 11; /* skip month and day */
    timestamp[strlen(timestamp) - 6] = '\0'; /* trim year */
    char outfile_nmbuf[36];
    snprintf(outfile_nmbuf, 36, "%s%s.csv", FT_OUT_PREFIX, timestamp);
    ft_exp_out = fopen(outfile_nmbuf, "w");
    char *first_ln = "qnum,time,sum\n";
    fwrite(first_ln, sizeof(char), strlen(first_ln), ft_exp_out);
    u_int64_t sum_dt = 0;
    int slave_death_index = 0;

    while (qnum < FT_NUM_QUERIES) {
        msgctl(msq_id, IPC_STAT, &buf);

        /* For experiment: if a certain query 0 by the modulus is reached,
         * kill a slave */

        if (qnum > 0 && qnum % FT_KILL_MODULUS == 0 && num_slaves > 2) {
            switch (FT_EXP_TYPE) {
                case ORDERED:
                    kill_slave(slaves_to_die[slave_death_index++]);
                    break;
                case RANDOM_SLAVE:
                    kill_random_slave(num_slaves);
                    break;
                default:
                    break;
            }

        }

        heartbeat(); // TODO: this can be called elsewhere
        if (buf.msg_qnum > 0) {

            request = (struct msgbuf *) malloc(sizeof(msgbuf));
            /* Grab from queue. */
            rc = msgrcv(msq_id, request, sizeof(msgbuf), 0, 0);

            /* Error Checking */
            if (rc < 0) {
                perror( strerror(errno) );
                printf("msgrcv failed, rc = %d\n", rc);
                continue;
            }

            if (request->mtype == mtype_put) {
                if (M_DEBUG)
                    printf("Putting vector %d\n", request->vector.vec_id);
                slave **commit_slaves =
                    get_machines_for_vector(request->vector.vec_id, true);
                int commit_res = commit_vector(request->vector.vec_id,
                    request->vector.vec, commit_slaves, replication_factor);
                if (commit_res)
                    heartbeat();
                free(commit_slaves);
            }
            else if (request->mtype == mtype_range_query) {
                range_query_contents contents = request->range_query;
                struct timespec start, end;
                clock_gettime(CLOCK_REALTIME, &start);
                switch (query_plan) { // TODO: fill in cases
                    case STARFISH: {
                        while (starfish(contents))
                            heartbeat();
                        break;
                    }
                    case UNISTAR: {

                    }
                    case MULTISTAR: {

                    }
                    case ITER_PRIM: {

                    }
                    default: {

                    }
                }
                clock_gettime(CLOCK_REALTIME, &end);
                u_int64_t dt = (end.tv_sec - start.tv_sec) * 1000000
                    + (end.tv_nsec - start.tv_nsec) / 1000;
                char res_buf[128];
                sum_dt += dt;
                snprintf(res_buf, 128, "%d,%lu,%lu\n", qnum, dt, sum_dt);
                fwrite(res_buf, sizeof(char), strlen(res_buf), ft_exp_out);
                /*
                if (killed) {
                    post_kill_tot += dt;
                    post_kill_times[qnum % FT_PREKILL_Q] = dt;
                }
                else {
                    pre_kill_tot += dt;
                    pre_kill_times[qnum] = dt;
                }
                */
                if (M_DEBUG)
                    printf("%ld: Range query %d took %lu mus\n",
                        end.tv_sec, qnum, dt);
                qnum++;
            }
            else if (request->mtype == mtype_point_query) {
                /* TODO: Call Jahrme function here */
            }
            free(request);
        }
    }
    fclose(ft_exp_out);
    /*
    double prekill_avg = ((double) pre_kill_tot) / FT_PREKILL_Q;
    double prekill_stdev = stdev(pre_kill_times, prekill_avg, FT_PREKILL_Q);
    printf("Avg time pre kill: %fms, stdev = %fms\n", prekill_avg,
            prekill_stdev);
    printf("Recovery time = %lums\n", reac_time);
    double postkill_avg = ((double) post_kill_tot) / FT_POSTKILL_Q;
    double postkill_stdev = stdev(post_kill_times, postkill_avg, FT_POSTKILL_Q);
    printf("Avg time postkill: %fms, stdev = %fms\n",
        postkill_avg, postkill_stdev);
    */
    /* deallocation */
    // while (slavelist != NULL) {
    //     free(slavelist->slave_node->address);
    //     free(slavelist->slave_node);
    //     slave_ll *temp = slavelist->next;
    //     free(slavelist);
    //     slavelist = temp;
    // }
    // if (dead_slave != NULL) {
    //     free(dead_slave->address);
    //     free(dead_slave);
    // }
}

double stdev(u_int64_t *items, double avg, int N) {
    double sum = 0.0;
    int i;
    for (i = 0; i < N; i++)
        sum += pow(items[i] - avg, 2.0);
    return sqrt(sum / (N - 1)); /* Bessel's correction */
}

int starfish(range_query_contents contents)
{
    int i;
    int num_ints_needed = 0;
    for (i = 0; i < contents.num_ranges; i++) {
        u_int *range = contents.ranges[i];
        // each range needs this much data:
        // number of vectors (inside parens), a machine/vector ID
        // for each one, preceded by the number of vectors to query
        int row_len = (range[1] - range[0] + 1) * 2 + 1;
        num_ints_needed += row_len;
    }

    /* this array will eventually include data for the coordinator
       slave's RPC  as described in the distributed system wiki. */

    /* NB: freed in master_rq */
    u_int *range_array = (u_int *) malloc(sizeof(u_int) * num_ints_needed);
    int array_index = 0;
    bool flip = true;
    for (i = 0; i < contents.num_ranges; i++) {
        u_int *range = contents.ranges[i];
        vec_id_t j;
        // FIXME: clarify this code by naming range1, range0
        // start of range is number of vectors
        u_int range_len = range[1] - range[0] + 1;
        range_array[array_index++] = range_len;
        u_int **machine_vec_ptrs = (u_int **) malloc(sizeof(int *) * range_len);
        for (j = range[0]; j <= range[1]; j++) {
            slave **tuple = get_machines_for_vector(j, false);
            if (flip) {
                slave *temp = tuple[0];
                tuple[0] = tuple[1];
                tuple[1] = temp;
            }
            int mvp_addr = j - range[0];
            machine_vec_ptrs[mvp_addr] = (u_int *) malloc(sizeof(u_int) * 2);
            machine_vec_ptrs[mvp_addr][0] = tuple[0]->id;
            machine_vec_ptrs[mvp_addr][1] = j;
            free(tuple);
        }

        qsort(machine_vec_ptrs, range_len,
            sizeof(u_int) * 2, compare_machine_vec_tuple);
        flip = !flip;

        /* save machine/vec IDs into the array */
        int tuple_index;
        for (j = range[0]; j <= range[1]; j++) {
            tuple_index = j - range[0];
            range_array[array_index++] = machine_vec_ptrs[tuple_index][0];
            range_array[array_index++] = machine_vec_ptrs[tuple_index][1];
        }

        for (j = range[0]; j <= range[1]; j++) {
            free(machine_vec_ptrs[j - range[0]]);
        }
        free(machine_vec_ptrs);
    }
    return init_range_query(range_array, contents.num_ranges,
        contents.ops, array_index);
}

/**
 * Send out a heartbeat to every slave. If you don't get a response, perform
 * a reallocation of the vectors it held to machines that don't already
 * have them.
 *
 */
int heartbeat()
{
    switch (partition) {
        case RING_CH: {
            slave **slavelist = ring_flattened_slavelist(chash_table);
            int i;
            for (i = 0; i < num_slaves; i++) {
                if (!is_alive(slavelist[i]->address)) {
                    if (--num_slaves > 1) {
                        struct timespec start, end;
                        clock_gettime(CLOCK_REALTIME, &start);
                        reallocate(slavelist[i]);
                        clock_gettime(CLOCK_REALTIME, &end);
                        u_int64_t reac_time;
                        reac_time = (end.tv_sec - start.tv_sec) * 1000000
                            + (end.tv_nsec - start.tv_nsec) / 1000;
                        printf("Recovery time: %lu mus\n", reac_time);
                    }
                    delete_entry(chash_table, slavelist[i]->id);
                    if (M_DEBUG) puts("deleted from slave tree");
                    return heartbeat(); /* refresh slave list */
                }
            }
            break;
        }
        default: {
            break;
        }
    }

    return 0;
}

/**
 * Reallocate vectors such that each is replicated at least r times,
 * after dead_slave is unreachable
 */
void reallocate(slave *dead_slave)
{
    switch (partition) {
        case RING_CH: {
            slave *pred = ring_get_pred_slave(chash_table, dead_slave->id);
            slave *succ = ring_get_succ_slave(chash_table, dead_slave->id);
            slave *sucsuc = ring_get_succ_slave(chash_table, succ->id);

            if (M_DEBUG) puts("pred -> succ");
            slave_vector *vec;
            /* transfer predecessor's nodes to successor */
            if (pred != succ) {
                vec = pred->primary_vector_head;
                for (; vec != NULL; vec = vec->next) {
                    send_vector(pred, vec->id, succ);
                }
            }

            if (M_DEBUG) puts("suc -> sucsuc");
            /* transfer successor's nodes to its successor */
            if (succ != sucsuc) {
                vec = dead_slave->primary_vector_head;
                for (; vec != NULL; vec = vec->next) {
                    send_vector(succ, vec->id, sucsuc);
                }
            }

            /* join dead node's linked list with the successor's */
            succ->primary_vector_tail->next = dead_slave->primary_vector_head;
            succ->primary_vector_tail = dead_slave->primary_vector_tail;

            printf("Reallocated after %s died\n",
                slave_addresses[dead_slave->id]);
            break;
        }

        case JUMP_CH: {
            break;
        }

        case STATIC_PARTITION: {
            break;
        }
    }
}

/**
 * Returns replication_factor (currently, hard at 2)-tuple of vectors such
 * that t = (m1, m2) and m1 != m2 if there at least 2 slaves available.
 * If this is a *new* vector, updating should be true, false otherwise.
 */
slave **get_machines_for_vector(vec_id_t vec_id, bool updating)
{
    switch (partition) {
        case RING_CH: {
            slave **tr = ring_get_machines_for_vector(chash_table, vec_id,
                replication_factor);
            if (M_DEBUG) {
                printf("v_4 for: %u,%u\n", tr[0]->id, tr[1]->id);
            }
            if (updating) {
                /* update slave's primary vector list */
                // slave_ll *head = slavelist;
                // while (head->slave_node->id != tr[0]) head = head->next;
                slave *slv = tr[0];//head->slave_node;
                /* insert it at the head and tail */
                slave_vector *vec = (slave_vector *)
                    malloc(sizeof(slave_vector));
                if (slv->primary_vector_head == NULL) {
                    slv->primary_vector_head = vec;
                    slv->primary_vector_head->id = vec_id;
                    slv->primary_vector_head->next = NULL;
                    slv->primary_vector_tail = slv->primary_vector_head;
                }
                else { /* insert it at the tail */
                    slv->primary_vector_tail->next = vec;
                    slv->primary_vector_tail = vec;
                    vec->id = vec_id;
                    vec->next = NULL;
                }
            }
            return tr;
        }

        case JUMP_CH: {
            // TODO Jahrme
            return NULL;
        }

        case STATIC_PARTITION: {
            u_int *machines = (u_int *)
                malloc(sizeof(u_int) * replication_factor);
            int index = vec_id / separation;
            assert(index >= 0 && index < num_slaves);
            machines[0] = partition_scale_1[index];
            machines[1] = partition_scale_2[index];
            // return machines;
            return NULL;
        }

        default: {
            return NULL;
        }
    }
}

/**
 * Comparator for machine-vector tuples to sort in ascending order of machine ID.
 * Source: https://en.wikipedia.org/wiki/Qsort
 */
int compare_machine_vec_tuple(const void *p, const void *q) {
    u_int x = **((const u_int **)p);
    u_int y = **((const u_int **)q);

    if (x < y)
        return -1;
    else if (x > y)
        return 1;

    return 0;
}

int get_new_slave_id(void)
{
    return slave_id_counter++;
}

slave *new_slave(char *address)
{
    slave *s = (slave *) malloc(sizeof(slave));
    s->id = get_new_slave_id();
    s->address = malloc(strlen(address) + 1);
    strcpy(s->address, address);
    s->is_alive = true;
    s->primary_vector_head = NULL;
    s->primary_vector_tail = NULL;
    return s;
}

void sigint_handler(int sig)
{
    // TODO free allocated memory (RBT, ...)

    // TODO signal death to slave nodes
}
