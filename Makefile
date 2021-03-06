BIN := bin
RPC_BIN := rpc/bin

CFLAGS := -Wall
CIGNORE := -Wno-incompatible-pointer-types -Wno-unused-variable
CC := gcc $(CFLAGS) $(CIGNORE)

all: clean .master .slave .dbms

clean:
	@echo "Cleaning..."
	@if test -d $(BIN); then rm -rf $(BIN)/*; else mkdir $(BIN); fi

$(BIN)/tree_map.o:
	@echo "Compiling tree map"
	@$(CC) -c -o $(BIN)/tree_map.o \
		consistent-hash/ring/src/tree_map.c

.rpc:
	@echo "Compiling RPC modules"
	@cd rpc && make

.master: .slave .rpc .bitmap-vector $(BIN)/tree_map.o .ds-util
	@echo "Compiling Master"
	@$(CC) -c -o $(BIN)/master_rq.o \
		master/master_rq.c
	@$(CC) -c -o $(BIN)/master_tpc.o \
		master/master_tpc_vector.c
	@echo "Compiling master main"
	@cp query-planner/iter-mst/mst_planner.py $(BIN) # TODO Jahrme copy remaining query planners
	@$(CC) -o $(BIN)/master \
		$(RPC_BIN)/slave_clnt.o \
		$(RPC_BIN)/slave_xdr.o \
		$(BIN)/tree_map.o \
		$(BIN)/master_rq.o \
		$(BIN)/master_tpc.o \
		$(BIN)/WAHQuery.o \
		$(BIN)/SegUtil.o \
		$(BIN)/read_vec.o \
		$(BIN)/slavelist.o \
		master/master.c \
		-lssl -lpthread -lcrypto -lm #-lpython2.7 # TODO: make `MASTER_FLAGS` target

.master_cent: .bitmap-vector
	@echo "Compiling Centralized Master"
	@$(CC) -c -o $(BIN)/master_rq_cent.o \
		master/master_rq_cent.c
	@$(CC) -o $(BIN)/master_cent \
		$(BIN)/master_rq_cent.o \
		$(BIN)/WAHQuery.o \
	$(BIN)/SegUtil.o \
		$(BIN)/read_vec.o \
	master/master_cent.c -lpthread

master_cent: .master_cent
	@cd bin && ./master_cent

.engine:
	@echo "Compiling Bitmap Engine Query function"
	@$(CC) -c -o $(BIN)/WAHQuery.o \
		../bitmap-engine/BitmapEngine/src/wah/WAHQuery.c
	@$(CC) -c -o $(BIN)/SegUtil.o \
		../bitmap-engine/BitmapEngine/src/seg-util/SegUtil.c

.slave: .rpc .engine .bitmap-vector .ds-util
	@echo "Compiling Slave"
	@$(CC) -o $(BIN)/slave \
		$(RPC_BIN)/slave_clnt.o \
		$(RPC_BIN)/slave_svc.o \
		$(RPC_BIN)/slave_xdr.o \
		$(BIN)/WAHQuery.o \
		$(BIN)/SegUtil.o \
		$(BIN)/read_vec.o \
		$(BIN)/slavelist.o \
		slave/slave.c \
		-lm

.dbms: .bitmap-vector
	@echo "Compiling DBMS"
	@$(CC) -o $(BIN)/dbms \
		$(BIN)/read_vec.o \
		dbms/dbms.c

.bitmap-vector:
	@echo "Compiling bitmap vector utilities"
	@$(CC) -c -o $(BIN)/read_vec.o \
		bitmap-vector/read_vec.c

.ds-util:
	@echo "Compiling distributed system utilities"
	@$(CC) -c -o $(BIN)/slavelist.o \
		util/slavelist.c

.start_dbms:
	@$(bin)/dbms

spawn_slave: all
	@echo "Starting slave"
	@cd $(BIN) && ./slave

basic_test: all
	@cd $(BIN) && ./dbms 0

# Voting data test
vd_test: all
	@echo "Creating test data..."
	@cd tst_data/rep-votes && python3 convert_voting_data.py
	@cd $(BIN) && ./dbms 1

# TPC Benchmarking Data Test
tpc_test: all
	@cd tst_data/tpc/qs && python3 qshuffle.py query_lt128.dat
	@cd $(BIN) && ./dbms 3
	@rm tst_data/tpc/qs/query_lt128.shuffled.dat
	@echo "tpc-c test complete"
