#ifndef SLAVELIST
#define SLAVELIST

#define DB_CAPSTONE_1_ADDR "10.250.94.63"
#define DB_CAPSTONE_2_ADDR "10.250.94.56"
#define DB_CAPSTONE_3_ADDR "10.250.94.72"
#define HOME_ADDR "127.0.1.1"

//#define LOCALTEST

#ifdef LOCALTEST
    #define NUM_SLAVES 1

    static char SLAVE_ADDR[NUM_SLAVES][32] = {
        HOME_ADDR
    };
#else
    //#define NUM_SLAVES 3
    #define NUM_SLAVES 1

    static char SLAVE_ADDR[NUM_SLAVES][32] = {
        DB_CAPSTONE_1_ADDR,
        // DB_CAPSTONE_2_ADDR,
        // DB_CAPSTONE_3_ADDR
    };
#endif

#endif
