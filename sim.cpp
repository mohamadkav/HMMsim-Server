/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#include "Arguments.H"
#include "Bank.H"
#include "Cache.H"
#include "CPU.H"
#include "Engine.H"
#include "Error.H"
#include "HybridMemory.H"
#include "Memory.H"
#include "MemoryHierarchy.H"
#include "MemoryManager.H"
#include "Migration.H"
#include "Partition.H"
#include "Statistics.H"
#include "TraceHandler.H"
#include "Types.H"

#include <cassert>

int main(int argc, char * argv[]){

//	enum A {
//		B,
//		C
//	};
//
//	cout << "sizeof(list<uint64>): " << sizeof(bitset<64>) << endl;
//	return 1;

//	ostringstream oss;
//	AllocationPolicy p;
//
//	oss << p;

	//assert(1 != 1);
	//char *
/*	ifstream f ;
	f.open("//home//coston//pinplay-drdebug-2.2-pldi2015-pin-2.14-71313-gcc.4.4.7-linux//trace-instr-addr.gz");
	int x = 123;
	f >> x;
	f >>x ;*/



	ArgumentContainer args("sim", true, true, "OTHER_TRACES", "name of remaining trace files");
	PositionalArgument<string> traceFile(&args, "FIRST_TRACE", "name of first trace", "");

	OptionalArgument<string> confPrefix(&args, "conf_prefix", "prefix of per-trace configuration file (name of trace will be appended)", "");

	OptionalArgument<string> statsFile(&args, "stats", "name of statistics file", "", false);
	OptionalArgument<string> countersPrefix(&args, "counters", "prefix of file where the counter trace is written to", "", false);

	OptionalArgument<uint64> intervalStatsPeriod(&args, "interval_stats_period", "period use by the engine to print interval statistics (0 for no interval statistics)", 0);
	OptionalArgument<string> intervalStatsFile(&args, "interval_stats_file", "name of interval statistics file (empty for no interval statistics", "");

	OptionalArgument<string> tracePrefix(&args, "trace_prefix", "prefix of trace files", "");
	OptionalArgument<string> counterTracePrefix(&args, "counter_trace_prefix", "prefix of the file where the counter trace is read from", "");
	OptionalArgument<string> counterTraceInfix(&args, "counter_trace_infix", "infix (after prefix and after conf but before name of trace) of the file where the counter trace is read from", "");

	OptionalArgument<uint64> stop(&args, "stop", "timestamp to stop execution of the simulator (0 means don't stop)", 0);

	OptionalArgument<uint64> debugStart(&args, "debug", "timestamp to start debugging output ", numeric_limits<uint64>::max());
	OptionalArgument<uint64> debugCpuStart(&args, "debug_cpu", "timestamp to start debugging output for the CPUs", numeric_limits<uint64>::max());
	OptionalArgument<uint64> debugCachesStart(&args, "debug_caches", "timestamp to start debugging output for the caches", numeric_limits<uint64>::max());
	OptionalArgument<uint64> debugHybridMemoryStart(&args, "debug_hybrid_memory", "timestamp to start debugging output for the hybrid memory", numeric_limits<uint64>::max());
	OptionalArgument<uint64> debugHybridMemoryManagerStart(&args, "debug_hybrid_memory_manager", "timestamp to start debugging output for the hybrid memory manager", numeric_limits<uint64>::max());
	OptionalArgument<uint64> debugCachesHybridStart(&args, "debug_caches_hybrid", "timestamp to start debugging output for the caches, hybrid memory and hybrid memory manager", numeric_limits<uint64>::max());

	OptionalArgument<uint64> progressPeriod(&args, "progress_period", "period use by the engine to print progress information (0 for no information)", 10000000);

	OptionalArgument<unsigned> blockSize(&args, "block_size", "block size", 64);
	OptionalArgument<unsigned> pageSize(&args, "page_size", "page size", 4096);

	OptionalArgument<uint64> instrLimit(&args, "instr_limit", "number of instructions to execute", numeric_limits<uint64>::max());
	OptionalArgument<unsigned> robSize(&args, "rob_size", "reorder buffer size", 128);
	OptionalArgument<unsigned> issueWidth(&args, "issue_width", "issue/commit width", 4);

	OptionalArgument<unsigned> instrL1CacheSize(&args, "instr_L1_size", "instruction L1 size (KB)", 64);
	OptionalArgument<unsigned> instrL1Assoc(&args, "instr_L1_assoc", "instruction L1 associativity", 4);
	OptionalArgument<uint64> instrL1Penalty(&args, "instr_L1_penalty", "instruction L1 penalty", 0);
	OptionalArgument<uint64> instrL1QueueSize(&args, "instr_L1_queue_size", "instruction L1 queue size", 8);


	OptionalArgument<unsigned> dataL1CacheSize(&args, "data_L1_size", "data L1 size (KB)", 64);
	OptionalArgument<unsigned> dataL1Assoc(&args, "data_L1_assoc", "data L1 associativity", 4);
	OptionalArgument<uint64> dataL1Penalty(&args, "data_L1_penalty", "data L1 penalty", 3);
	OptionalArgument<uint64> dataL1QueueSize(&args, "data_L1_queue_size", "data L1 queue size", 32);

	OptionalArgument<unsigned> sharedL2CacheSize(&args, "L2_size", "shared L2 size (KB)", 1024);
	OptionalArgument<unsigned> sharedL2Assoc(&args, "L2_assoc", "shared L2 associativity", 16);
	OptionalArgument<uint64> sharedL2Penalty(&args, "L2_penalty", "shared L2 penalty", 32); //8 ns @ 4GHz
	OptionalArgument<uint64> sharedL2QueueSize(&args, "L2_queue_size", "shared L2 queue size", 16);

	OptionalArgument<bool> realCacheRemap(&args, "real_cache_remap", "whether the caches use real cache remap (remap latency == penalty, flush previous levels) or not (remap latency == 0, remap previous levels)", true);

	OptionalArgument<bool> privateL2(&args, "private_L2", "whether the L2 is private", false);

	OptionalArgument<string> memoryOrganization(&args, "memory_organization", "memory organization (dram|pcm|cache|hybrid|old_hybrid)", "dram");

	OptionalArgument<unsigned> threads(&args, "threads", "number of threads (1 for single and multi-programmed workloads", 1);

	OptionalArgument<bool> useCaches(&args, "use_caches", "whether to use caches", true);

	//Arguments for Hybrid Memory Manager
	OptionalArgument<FlushPolicy> flushPolicy(&args, "flush_policy", "flush policy (flush_pcm_before|flush_only_after|remap|change_tag)", FLUSH_PCM_BEFORE);
	OptionalArgument<unsigned> flushQueueSize(&args, "flush_queue_size", "number of concurrent flushes due to migrations", 8);
	OptionalArgument<bool> supressFlushWritebacks(&args, "suppress_flush_writebacks", "whether to suppress writebacks due to L2 flushing", false);
	OptionalArgument<uint64> demoteTimout(&args, "demote_timeout", "number of clock cycles after no demotion was started to try again", 10000);
	OptionalArgument<uint64> partitionPeriod(&args, "partition_period", "size in clock cycles or number of instructions of the partition recalculation period", 1000000);
	OptionalArgument<string> periodType(&args, "period_type", "type of the partition recalculation period (cycles|instructions)", "cycles");
	OptionalArgument<unsigned> migrationTableSize(&args, "migration_table_size", "maximum size of the migration table", numeric_limits<unsigned>::max());


	//Arguments for migration policies
	OptionalArgument<double> maxFreeDram(&args, "max_free_dram", "maximum fraction of free DRAM pages (PCM migrations stop when there are more than these free pages)", 0.01);
	OptionalArgument<uint32> completeThreshold(&args, "complete_threshold", "number of blocks left to transfer that will trigger the completion of an on-demand migration", 16); //migrate 3/4 of a page
	OptionalArgument<uint64> rollbackTimeout(&args, "rollback_timeout", "number of cycles since the start of migration that triggers its rollback", 10000);

	//Arguments for Old Hybrid Memory Manager
	OptionalArgument<MigrationMechanism> migrationMechanism(&args, "migration_mechanism", "migration mechanism (pause|pin|redirect)", REDIRECT);
	OptionalArgument<MonitoringType> monitoringType(&args, "monitoring_type", "monitoring type (reads|writes|accesses)", ACCESSES);
	OptionalArgument<MonitoringLocation> monitoringLocation(&args, "monitoring_location", "monitoring location (before_caches|after_caches)", AFTER_CACHES);
//	OptionalArgument<FlushPolicy> flushPolicy(&args, "flush_policy", "flush policy (flush_pcm_before|flush_only_after|remap|change_tag)", FLUSH_PCM_BEFORE);
//	OptionalArgument<unsigned> flushQueueSize(&args, "flush_queue_size", "number of concurrent flushes due to migrations", 8);
//	OptionalArgument<bool> supressFlushWritebacks(&args, "suppress_flush_writebacks", "whether to suppress writebacks due to L2 flushing", false);
//	OptionalArgument<uint64> partitionPeriod(&args, "partition_period", "size in clock cycles of the partition recalculation period", 1000000);
//	OptionalArgument<string> periodType(&args, "period_type", "type of the partition recalculation period (cycles|instructions)", "cycles");
	OptionalArgument<double> baseMigrationRate(&args, "base_migration_rate", "migration rate used as the peak base rate", 1);
	OptionalArgument<bool> perPageStats(&args, "per_page_stats", "whether hybrid memory manager outputs per page statistics", false);
	OptionalArgument<string> perPageStatsFilename(&args, "per_page_stats_filename", "filename for per page statistics", "");
	OptionalArgument<bool> trace(&args, "trace", "whether hybrid memory manager outputs counter information", false);
	OptionalArgument<uint64> tracePeriod(&args, "trace_period", "number of instructions between consecutive trace entries", 100000);


	//Migration, allocation and partition
	OptionalArgument<string> migrationPolicy(&args, "migration_policy", "migration policy (no_migration|multi_queue|first_touch|double_clock|frequency|offline|two_lru)", "multi_queue");
	OptionalArgument<AllocationPolicy> allocationPolicy(&args, "allocation_policy", "allocation policy (dram_first|pcm_only|custom)", DRAM_FIRST);
	OptionalArgument<string> customAllocator(&args, "custom_allocator", "custom allocator (offline_frequency)", "offline_frequency");
	OptionalArgument<string> partitionPolicy(&args, "partition_policy", "partition policy (none|static|offline)", "none");

	//Arguments for the offline migration policy
	OptionalArgument<string> metricType(&args, "metric_type", "metric type (accessed|access_count|touch_count)", "access_count");
	OptionalArgument<string> accessType(&args, "access_type", "access type (reads|writes|accesses)", "accesses");
	OptionalArgument<string> weightType(&args, "weight_type", "weight type (uniform|linear|exponential)", "uniform");
	OptionalArgument<uint64> intervalCount(&args, "interval_count", "number of intervals to look into the future", 50);
	OptionalArgument<uint64> metricThreshold(&args, "metric_threshold", "minimum difference per interval between 2 pages to consider swapping them", 2);


	//Arguments for the Multi Queue algorithm
	OptionalArgument<unsigned> numQueues(&args, "num_queue", "number of queues of MQ algorithm", 15);
	OptionalArgument<unsigned> thresholdQueue(&args, "threshold_queue", "Index of the threshold queue", 5);
	OptionalArgument<uint64> lifetime(&args, "lifetime", "lifetime", 200000);
	OptionalArgument<bool> logicalTime(&args, "logical_time", "whether to use logical time (number of accesses) or real time (clock cycles) for lifetime expiration", true);
	OptionalArgument<uint64> filterThreshold(&args, "filter_threshold", "filter threshold", 0); //0 means no filter
	OptionalArgument<bool> secondDemotionEviction(&args, "second_demotion_eviction", "whether the policy evicts a page from the MQ on a second demotion without an intervening access", false);
	OptionalArgument<bool> aging(&args, "aging", "whether the policy ages access counts on demotion", false);
	OptionalArgument<bool> history(&args, "history", "whether the policy maintains access frequency for evicted pages", true);
	OptionalArgument<bool> pendingList(&args, "pending_list", "whether to use a pending list", false);
	OptionalArgument<bool> rollback(&args, "rollback", "whether to enable rollback of migrations", true);
	OptionalArgument<bool> promotionFilter(&args, "promotion_filter", "whether to filter promotions based on position in the multi queue", false);
	OptionalArgument<unsigned> demotionAttempts(&args, "demotion_attempts", "number of times the policy is consulted before it allows for a demotion", 0);


	//Arguments for static partition policy
	OptionalArgument<string> dramFractions(&args, "dram_fractions", "string representing the fraction of dram space allocated to each process", "0.0078125"); //32MB for a 4GB system
	OptionalArgument<string> rateFractions(&args, "rate_fractions", "string representing the fraction of migration rate allocated to each process", "1");


	//Arguments for dynamic partition policy
	OptionalArgument<double> rateGran(&args, "rate_granularity", "granularity of rate allocation", 0.1);
	OptionalArgument<uint64> spaceGran(&args, "space_granularity", "granularity of rate allocation", 8);
	OptionalArgument<double> IPCconstraint(&args, "ipc_constraint", "IPC constraint of the low priority application", 0.1975842);

	//Arguments for very old migration policy
	OptionalArgument<MonitoringStrategy> monitoringStrategy(&args, "monitoring_strategy", "monitoring_strategy (no_pam|pam)", NO_PAM);
	OptionalArgument<QueuePolicy> promotionPolicy(&args, "promotion_policy", "promotion policy (fifo|lru|freq)", FREQ);
	OptionalArgument<QueuePolicy> demotionPolicy(&args, "demotion_policy", "demotion policy (fifo|lru|freq)", FREQ);
	OptionalArgument<QueuePolicy> candidateListEvictionPolicy(&args, "queue_eviction_policy", "queue eviction policy (fifo|lru|freq)", FIFO);
	OptionalArgument<unsigned> candidateListSize(&args, "candidate_list_size", "candidate list size", 8388608);//8388608
	OptionalArgument<unsigned> migrationQueueSize(&args, "migration_queue_size", "migration queue size", 64);
	OptionalArgument<uint64> agingPeriod(&args, "aging_period", "aging period", 10000000);
	OptionalArgument<uint64> counterReadPeriod(&args, "counter_read_period", "counter read period", 10000);
	OptionalArgument<uint64> accessBitPeriod(&args, "access_bit_period", "access bit period", 1000);//1000
	OptionalArgument<uint64> migrationPeriod(&args, "migration_period", "migration period", 1);

	//Arguments for hybrid memory
	OptionalArgument<uint64> dramMigrationReadDelay(&args, "dram_migration_read_delay", "delay for scheduling the reading of the next block for PCM to DRAM migrations", 0);
	OptionalArgument<uint64> dramMigrationWriteDelay(&args, "dram_migration_write_delay", "delay for scheduling the writing of the next block for PCM to DRAM migrations", 0);
	OptionalArgument<uint64> pcmMigrationReadDelay(&args, "pcm_migration_read_delay", "delay for scheduling the reading of the next block for DRAM to PCM migrations", 0);
	OptionalArgument<uint64> pcmMigrationWriteDelay(&args, "pcm_migration_write_delay", "delay for scheduling the writing of the next block for DRAM to PCM migrations", 0);
	OptionalArgument<unsigned> completionThreshold(&args, "completion_threshold", "number of blocks left to migrate when the completion of the migration should be started", 0);
	OptionalArgument<bool> elideCleanDramBlocks(&args, "elide_clean_dram_blocks", "whether to elide copying of clean DRAM block for page migrations from DRAM to PCM", false);
	OptionalArgument<bool> fixedPcmMigrationCost(&args, "fixed_pcm_migration_cost", "whether the hybrid memory uses a fixed migration cost for page migrations from DRAM to PCM", false);
	OptionalArgument<uint64> pcmMigrationCost(&args, "pcm_migration_cost", "PCM migration cost", 1);

	//Arguments for Old hHybrid memory
	OptionalArgument<bool> burstMigration(&args, "burst_migration", "whether the hybrid memory issues requests for page migration in a burst", true);
	OptionalArgument<bool> fixedDramMigrationCost(&args, "fixed_dram_migration_cost", "whether the hybrid memory uses a fixed migration cost for page migrations from PCM to DRAM", false);
//	OptionalArgument<bool> fixedPcmMigrationCost(&args, "fixed_pcm_migration_cost", "whether the hybrid memory uses a fixed migration cost for page migrations from DRAM to PCM", false);
	OptionalArgument<uint64> dramMigrationCost(&args, "dram_migration_cost", "DRAM migration cost", 3);
	//OptionalArgument<uint64> pcmMigrationCost(&args, "pcm_migration_cost", "PCM migration cost", 1);

	//Arguments for DRAM cache memory
	OptionalArgument<unsigned> dramCacheblockSize(&args, "dram_cache_block_size", "dram cache block size", 4096);
	OptionalArgument<unsigned> dramCacheAssoc(&args, "dram_cache_assoc", "dram cache associativity", 32);
	OptionalArgument<uint64> dramCacheTagPenalty(&args, "dram_cache_tag_penalty", "dram cache tag penalty", 16);
	OptionalArgument<int> dramCacheQueueSize(&args, "dram_cache_queue_size", "dram cache queue size", 32);

	//DRAM parameters
	OptionalArgument<RowBufferPolicy> dramRowBufferPolicy(&args, "dram_row_buffer_policy", "DRAM row buffer policy (open_page|closed_page)", OPEN_PAGE);
	OptionalArgument<MappingType> dramMappingType(&args, "dram_mapping_type", "DRAM mapping type (row_rank_bank_col|row_col_rank_bank|rank_bank_row_col)", ROW_RANK_BANK_COL);
	OptionalArgument<bool> dramGlobalQueue(&args, "dram_global_queue", "DRAM global queue", false);
	OptionalArgument<unsigned> dramQueueSize(&args, "dram_queue_size", "DRAM queue size", 128); //8 entries per bank
	OptionalArgument<unsigned> dramRanks(&args, "dram_ranks", "number of DRAM ranks", 8); //4 DIMMs x 4 ranks per DIMM
	OptionalArgument<unsigned> dramBanksPerRank(&args, "dram_banks_per_rank", "number of DRAM banks per rank", 8);
	OptionalArgument<unsigned> dramRowsPerBank(&args, "dram_rows_per_bank", "number of DRAM rows per bank", 16*1024);
	OptionalArgument<unsigned> dramBlocksPerRow(&args, "dram_blocks_per_row", "number of DRAM blocks per row", 64); //4KB row
	OptionalArgument<uint64> dramOpenLatency(&args, "dram_open_latency", "DRAM open latency", 50); //12.5ns @4GHz
	OptionalArgument<uint64> dramCloseLatency(&args, "dram_close_latency", "DRAM close latency", 50);
	OptionalArgument<uint64> dramAccessLatency(&args, "dram_access_latency", "DRAM access_latency", 50);
	OptionalArgument<uint64> dramBusLatency(&args, "dram_bus_latency", "DRAM bus latency", 16); //4ns @4GHz; 4ns == 4 transfers @ 1000MHz (DDR-2000)
	//Total size: 8GB

	//PCM parameters
	OptionalArgument<RowBufferPolicy> pcmRowBufferPolicy(&args, "pcm_row_buffer_policy", "PCM row buffer policy (open_page|closed_page)", CLOSED_PAGE);
	OptionalArgument<MappingType> pcmMappingType(&args, "pcm_mapping_type", "PCM mapping type (row_rank_bank_col|row_col_rank_bank|rank_bank_row_col)", ROW_COL_RANK_BANK);
	OptionalArgument<bool> pcmGlobalQueue(&args,  "pcm_global_queue", "PCM global queue", false);
	OptionalArgument<unsigned> pcmQueueSize(&args, "pcm_queue_size", "PCM queue size", 8); //8 entries per bank
	OptionalArgument<unsigned> pcmRanks(&args, "pcm_ranks", "number of PCM ranks", 16); //4 DIMMs x 4 ranks per DIMM
	OptionalArgument<unsigned> pcmBanksPerRank(&args, "pcm_banks_per_rank", "number of PCM banks per rank", 8);
	OptionalArgument<unsigned> pcmRowsPerBank(&args, "pcm_rows_per_bank", "number of PCM rows per bank", 64*1024);
	OptionalArgument<unsigned> pcmBlocksPerRow(&args, "pcm_blocks_per_row", "number of PCM blocks per row", 64);
	OptionalArgument<uint64> pcmOpenLatency(&args, "pcm_open_latency", "PCM open latency", 22); //4.4 slower array reads //old: 480); //120ns
	OptionalArgument<uint64> pcmCloseLatency(&args, "pcm_close_latency", "PCM close latency", 60); //12 slower array writes //old: 5600); //350ns for 128 bit write
	OptionalArgument<uint64> pcmAccessLatency(&args, "pcm_access_latency", "PCM access_latency", 5);
	OptionalArgument<bool> pcmLongLatency(&args,  "pcm_long_latency", "whether PCM uses long latency for close operation (close latency * number of dirty columns)", true);
	OptionalArgument<uint64> pcmBusLatency(&args, "pcm_bus_latency", "PCM bus latency", 4); //10ns @4GHz; 10ns == 4 transfer @ 400MHz (DDR-800)

//	args.print(cout);
//	return -1;

	if (args.parse(argc, argv)){
		args.usage(cerr);
		return -1;
	}

	if (confPrefix.isSet()){
		args.parseFile(confPrefix.getValue() + traceFile.getValue());
	}

	if (debugCachesHybridStart.getValue() != numeric_limits<uint64>::max()){
		debugCachesStart.setValue(debugCachesHybridStart.getValue());
		debugHybridMemoryStart.setValue(debugCachesHybridStart.getValue());
		debugHybridMemoryManagerStart.setValue(debugCachesHybridStart.getValue());
	}

	if (debugStart.getValue() != numeric_limits<uint64>::max()){
		debugCpuStart.setValue(debugStart.getValue());
		debugCachesStart.setValue(debugStart.getValue());
		debugHybridMemoryStart.setValue(debugStart.getValue());
		debugHybridMemoryManagerStart.setValue(debugStart.getValue());
	}

	unsigned numCores;
	unsigned numProcesses;
	vector<string> traceNames;
	vector<string> allocationNames;
	if (threads.getValue() == 1){
		traceNames.emplace_back(traceFile.getValue());
		allocationNames.emplace_back(tracePrefix.getValue() + traceFile.getValue());
		for (vector<string>::iterator it = args.moreArgs().begin(); it != args.moreArgs().end(); ++it){
			traceNames.emplace_back(*it);
			allocationNames.emplace_back(tracePrefix.getValue() + *it);
		}
		numCores = traceNames.size();
		numProcesses = numCores;
		if (numCores < 1){
			error("There must be at least one trace file");
		}
	} else {

		numCores = threads.getValue();
		numProcesses = 1;
		for (unsigned i = 0; i < numCores; i++){
			traceNames.emplace_back(traceFile.getValue() + "-" + to_string(i));
		}
		allocationNames.emplace_back(tracePrefix.getValue() + traceFile.getValue());
		if (args.moreArgs().size() != 0){
			error("For multithreaded workloads, only one trace file can be specified");
		}
	}


	StatContainer stats;
	Engine engine(&stats, intervalStatsPeriod.getValue(), intervalStatsFile.getValue(), progressPeriod.getValue());
	Memory *dramMemory = 0;
	Memory *pcmMemory = 0;
	IMemory *memory = 0;
	CacheMemory *cacheMemory = 0;
	HybridMemory *hybridMemory = 0;
	OldHybridMemory *oldHybridMemory = 0;
	Cache *sharedL2 = 0;
	vector<IMigrationPolicy*> policies;
	vector<IOldMigrationPolicy*> oldPolicies;
	IPartition *partition;
	IMemoryManager *manager = 0;
	HybridMemoryManager *hmm = 0;
	OldHybridMemoryManager *ohmm = 0;

	if (memoryOrganization.getValue() == "dram"){
		dramMemory = new Memory("dram", "DRAM", &engine, &stats, debugStart.getValue(), DRAM_QUEUE, DRAM_OPEN, DRAM_ACCESS, DRAM_CLOSE, DRAM_BUS_QUEUE, DRAM_BUS, dramRowBufferPolicy.getValue(), DESTRUCTIVE_READS, dramMappingType.getValue(), dramGlobalQueue.getValue(), dramQueueSize.getValue(), dramRanks.getValue(), dramBanksPerRank.getValue(), dramRowsPerBank.getValue(), dramBlocksPerRow.getValue(), blockSize.getValue(), dramOpenLatency.getValue(), dramCloseLatency.getValue(), dramAccessLatency.getValue(), false, dramBusLatency.getValue(), 0);
		manager = new SimpleMemoryManager(&stats, dramMemory, numProcesses, pageSize.getValue());
		memory = dramMemory;
	} else if (memoryOrganization.getValue() == "pcm"){
		pcmMemory = new Memory("pcm", "PCM", &engine, &stats, debugStart.getValue(), PCM_QUEUE, PCM_OPEN, PCM_ACCESS, PCM_CLOSE, PCM_BUS_QUEUE, PCM_BUS, pcmRowBufferPolicy.getValue(), NON_DESTRUCTIVE_READS, pcmMappingType.getValue(), pcmGlobalQueue.getValue(), pcmQueueSize.getValue(), pcmRanks.getValue(), pcmBanksPerRank.getValue(), pcmRowsPerBank.getValue(), pcmBlocksPerRow.getValue(), blockSize.getValue(), pcmOpenLatency.getValue(), pcmCloseLatency.getValue(), pcmAccessLatency.getValue(), pcmLongLatency.getValue(), pcmBusLatency.getValue(),0);
		manager = new SimpleMemoryManager(&stats, pcmMemory, numProcesses, pageSize.getValue());
		memory = pcmMemory;
	} else if (memoryOrganization.getValue() == "cache"){
		dramMemory = new Memory("dram", "DRAM", &engine, &stats, debugStart.getValue(), DRAM_QUEUE, DRAM_OPEN, DRAM_ACCESS, DRAM_CLOSE, DRAM_BUS_QUEUE, DRAM_BUS,  dramRowBufferPolicy.getValue(), DESTRUCTIVE_READS, dramMappingType.getValue(), dramGlobalQueue.getValue(), dramQueueSize.getValue(), dramRanks.getValue(), dramBanksPerRank.getValue(), dramRowsPerBank.getValue(), dramBlocksPerRow.getValue(), blockSize.getValue(), dramOpenLatency.getValue(), dramCloseLatency.getValue(), dramAccessLatency.getValue(), false, dramBusLatency.getValue(),0);
		pcmMemory = new Memory("pcm", "PCM", &engine, &stats, debugStart.getValue(), PCM_QUEUE, PCM_OPEN, PCM_ACCESS, PCM_CLOSE, PCM_BUS_QUEUE, PCM_BUS, pcmRowBufferPolicy.getValue(), NON_DESTRUCTIVE_READS, pcmMappingType.getValue(), pcmGlobalQueue.getValue(), pcmQueueSize.getValue(), pcmRanks.getValue(), pcmBanksPerRank.getValue(), pcmRowsPerBank.getValue(), pcmBlocksPerRow.getValue(), blockSize.getValue(), pcmOpenLatency.getValue(), pcmCloseLatency.getValue(), pcmAccessLatency.getValue(), pcmLongLatency.getValue(), pcmBusLatency.getValue(),0);
		cacheMemory = new CacheMemory("cache_memory", "Cache Memory", &engine, &stats, debugStart.getValue(), dramMemory, pcmMemory, dramCacheblockSize.getValue(), dramCacheAssoc.getValue(), CACHE_LRU, pageSize.getValue(), dramCacheTagPenalty.getValue(), dramCacheQueueSize.getValue());
		manager = new SimpleMemoryManager(&stats, pcmMemory, numProcesses, pageSize.getValue());
		memory = cacheMemory;
	} else if (memoryOrganization.getValue() == "hybrid"){
		dramMemory = new Memory("dram", "DRAM", &engine, &stats, debugStart.getValue(), DRAM_QUEUE, DRAM_OPEN, DRAM_ACCESS, DRAM_CLOSE, DRAM_BUS_QUEUE, DRAM_BUS, dramRowBufferPolicy.getValue(), DESTRUCTIVE_READS, dramMappingType.getValue(), dramGlobalQueue.getValue(), dramQueueSize.getValue(), dramRanks.getValue(), dramBanksPerRank.getValue(), dramRowsPerBank.getValue(), dramBlocksPerRow.getValue(), blockSize.getValue(), dramOpenLatency.getValue(), dramCloseLatency.getValue(), dramAccessLatency.getValue(), false, dramBusLatency.getValue(),0);
		pcmMemory = new Memory("pcm", "PCM", &engine, &stats, debugStart.getValue(), PCM_QUEUE, PCM_OPEN, PCM_ACCESS, PCM_CLOSE, PCM_BUS_QUEUE, PCM_BUS, pcmRowBufferPolicy.getValue(), NON_DESTRUCTIVE_READS, pcmMappingType.getValue(), pcmGlobalQueue.getValue(), pcmQueueSize.getValue(), pcmRanks.getValue(), pcmBanksPerRank.getValue(), pcmRowsPerBank.getValue(), pcmBlocksPerRow.getValue(), blockSize.getValue(), pcmOpenLatency.getValue(), pcmCloseLatency.getValue(), pcmAccessLatency.getValue(), pcmLongLatency.getValue(), pcmBusLatency.getValue(), dramMemory->getSize());
		hybridMemory = new HybridMemory("hybrid_memory", "Hybrid Memory", &engine, &stats, debugHybridMemoryStart.getValue(), numProcesses, dramMemory, pcmMemory, blockSize.getValue(), pageSize.getValue(), dramMigrationReadDelay.getValue(), dramMigrationWriteDelay.getValue(), pcmMigrationReadDelay.getValue(), pcmMigrationWriteDelay.getValue(), completionThreshold.getValue(), elideCleanDramBlocks.getValue(), fixedPcmMigrationCost.getValue(), pcmMigrationCost.getValue());
		memory = hybridMemory;
	} else if (memoryOrganization.getValue() == "old_hybrid"){
		dramMemory = new Memory("dram", "DRAM", &engine, &stats, debugStart.getValue(), DRAM_QUEUE, DRAM_OPEN, DRAM_ACCESS, DRAM_CLOSE, DRAM_BUS_QUEUE, DRAM_BUS, dramRowBufferPolicy.getValue(), DESTRUCTIVE_READS, dramMappingType.getValue(), dramGlobalQueue.getValue(), dramQueueSize.getValue(), dramRanks.getValue(), dramBanksPerRank.getValue(), dramRowsPerBank.getValue(), dramBlocksPerRow.getValue(), blockSize.getValue(), dramOpenLatency.getValue(), dramCloseLatency.getValue(), dramAccessLatency.getValue(), false, dramBusLatency.getValue(),0);
		pcmMemory = new Memory("pcm", "PCM", &engine, &stats, debugStart.getValue(), PCM_QUEUE, PCM_OPEN, PCM_ACCESS, PCM_CLOSE, PCM_BUS_QUEUE, PCM_BUS, pcmRowBufferPolicy.getValue(), NON_DESTRUCTIVE_READS, pcmMappingType.getValue(), pcmGlobalQueue.getValue(), pcmQueueSize.getValue(), pcmRanks.getValue(), pcmBanksPerRank.getValue(), pcmRowsPerBank.getValue(), pcmBlocksPerRow.getValue(), blockSize.getValue(), pcmOpenLatency.getValue(), pcmCloseLatency.getValue(), pcmAccessLatency.getValue(), pcmLongLatency.getValue(), pcmBusLatency.getValue(), dramMemory->getSize());
		oldHybridMemory = new OldHybridMemory("hybrid_memory", "Hybrid Memory", &engine, &stats, debugHybridMemoryStart.getValue(), numProcesses, dramMemory, pcmMemory, blockSize.getValue(), pageSize.getValue(), burstMigration.getValue(), fixedDramMigrationCost.getValue(), fixedPcmMigrationCost.getValue(), dramMigrationCost.getValue(), pcmMigrationCost.getValue(), migrationMechanism.getValue() == REDIRECT);
		memory = oldHybridMemory;
	} else {
		args.usage(cerr);
		return -1;
	}

	if (useCaches.getValue()){
		sharedL2 = new Cache("L2", "Shared L2 Cache" , &engine, &stats, debugCachesStart.getValue(), L2_WAIT, L2_TAG, L2_STALL, memory, 1024*sharedL2CacheSize.getValue(), blockSize.getValue(), sharedL2Assoc.getValue(), CACHE_LRU, pageSize.getValue(), sharedL2Penalty.getValue(), sharedL2QueueSize.getValue(), realCacheRemap.getValue());
	}

	if (memoryOrganization.getValue() == "hybrid"){
		assert(useCaches.getValue());
		unsigned pidsPerPolicy = 0;
		if (partitionPolicy.getValue() == "none"){
			pidsPerPolicy = numProcesses;
			partition = new StaticPartition(1, pageSize.getValue(), dramMemory->getSize(), dramFractions.getValue(), rateFractions.getValue());
		} else if (partitionPolicy.getValue() == "static"){
			pidsPerPolicy = 1;
			partition = new StaticPartition(numProcesses, pageSize.getValue(), dramMemory->getSize(), dramFractions.getValue(), rateFractions.getValue());
		} else if (partitionPolicy.getValue() == "offline"){
			pidsPerPolicy = 1;
			OfflinePartition* offline = new OfflinePartition(numProcesses, pageSize.getValue(), dramMemory->getSize(), counterTracePrefix.getValue(), counterTraceInfix.getValue(), "_0.trace", periodType.getValue());
			string n = "mix_" + traceNames[0] + "_" + traceNames[1];
			offline->addCounterTrace(n);
			partition = offline;
		} else if (partitionPolicy.getValue() == "dynamic"){
			pidsPerPolicy = 1;
			partition = new DynamicPartition(numProcesses, pageSize.getValue(), dramMemory->getSize(), rateGran.getValue(), spaceGran.getValue(), IPCconstraint.getValue());
		} else {
			args.usage(cerr);
			return -1;
		}

		for (unsigned i = 0; i < partition->getNumPolicies(); i++){
			ostringstream ossName;
			ossName << migrationPolicy.getValue() << "_policy_" << i;
			if (migrationPolicy.getValue() == "no_migration"){
				policies.emplace_back(new NoMigrationPolicy(ossName.str(), &engine, debugStart.getValue(), partition->getDramPages(i), allocationPolicy.getValue(), pidsPerPolicy));
			} else if (migrationPolicy.getValue() == "multi_queue"){
				policies.emplace_back(new MultiQueueMigrationPolicy(ossName.str(), &engine, debugStart.getValue(), partition->getDramPages(i), allocationPolicy.getValue(), pidsPerPolicy, maxFreeDram.getValue(), completeThreshold.getValue(), rollbackTimeout.getValue(), numQueues.getValue(), thresholdQueue.getValue(), lifetime.getValue(), logicalTime.getValue(), filterThreshold.getValue(), secondDemotionEviction.getValue(), aging.getValue(), history.getValue(), pendingList.getValue(), rollback.getValue(), promotionFilter.getValue(), demotionAttempts.getValue()));
			} else  if (migrationPolicy.getValue() == "first_touch"){
//				policies.emplace_back(new FirstTouchMigrationPolicy(ossName.str(), &engine, debugStart.getValue(), partition->getDramPages(i), allocationPolicy.getValue(), allocator, pidsPerPolicy));
			} else  if (migrationPolicy.getValue() == "double_clock"){
//				policies.emplace_back(new DoubleClockMigrationPolicy(ossName.str(), &engine, debugStart.getValue(), partition->getDramPages(i), allocationPolicy.getValue(), allocator, pidsPerPolicy));
			} else if (migrationPolicy.getValue() == "frequency"){
				//monitoringStrategy.getValue(), monitorApp.getValue(), promotionPolicy.getValue(), demotionPolicy.getValue(), candidateListEvictionPolicy.getValue(), flushPolicy.getValue(), candidateListSize.getValue(), migrationQueueSize.getValue(), agingPeriod.getValue(), counterReadPeriod.getValue(), accessBitPeriod.getValue(),
			} else if (migrationPolicy.getValue() == "offline"){
//				string filename = counterTracePrefix.getValue() + traceNames[i] + ".gz";
//				policies.emplace_back(new OfflineMigrationPolicy(ossName.str(), &engine, debugStart.getValue(), partition->getDramPages(i), allocationPolicy.getValue(), allocator, pidsPerPolicy, i, filename, metricType.getValue(), accessType.getValue(), weightType.getValue(), intervalCount.getValue(), metricThreshold.getValue()));
			} else {
				args.usage(cerr);
				return -1;
			}
		}
		hmm = new HybridMemoryManager(&engine, &stats, debugHybridMemoryManagerStart.getValue(), numCores, numProcesses, sharedL2, hybridMemory, policies, partition, blockSize.getValue(), pageSize.getValue(), flushPolicy.getValue(), flushQueueSize.getValue(), supressFlushWritebacks.getValue(), demoteTimout.getValue(), partitionPeriod.getValue(), periodType.getValue(), migrationTableSize.getValue(), perPageStats.getValue(), perPageStatsFilename.getValue());
		manager = hmm;
	}

	if (memoryOrganization.getValue() == "old_hybrid"){
		assert(useCaches.getValue());
		unsigned pidsPerPolicy = 0;
		if (partitionPolicy.getValue() == "none"){
			pidsPerPolicy = numProcesses;
			partition = new StaticPartition(1, pageSize.getValue(), dramMemory->getSize(), dramFractions.getValue(), rateFractions.getValue());
		} else if (partitionPolicy.getValue() == "static"){
			pidsPerPolicy = 1;
			partition = new StaticPartition(numProcesses, pageSize.getValue(), dramMemory->getSize(), dramFractions.getValue(), rateFractions.getValue());
		} else if (partitionPolicy.getValue() == "offline"){
			pidsPerPolicy = 1;
			OfflinePartition* offline = new OfflinePartition(numProcesses, pageSize.getValue(), dramMemory->getSize(), counterTracePrefix.getValue(), counterTraceInfix.getValue(), "_0.trace", periodType.getValue());
			string n = "mix_" + traceNames[0] + "_" + traceNames[1];
			offline->addCounterTrace(n);
			partition = offline;
		} else if (partitionPolicy.getValue() == "dynamic"){
			pidsPerPolicy = 1;
			partition = new DynamicPartition(numProcesses, pageSize.getValue(), dramMemory->getSize(), rateGran.getValue(), spaceGran.getValue(), IPCconstraint.getValue());
		} else {
			args.usage(cerr);
			return -1;
		}

		for (unsigned i = 0; i < partition->getNumPolicies(); i++){
			IAllocator *allocator;
			if (allocationPolicy.getValue() == CUSTOM){
				if (customAllocator.getValue() == "offline_frequency"){
					allocator = 0; //new ...
				}
			}
			ostringstream ossName;
			ossName << migrationPolicy.getValue() << "_policy_" << i;
			if (migrationPolicy.getValue() == "no_migration"){
				oldPolicies.emplace_back(new OldNoMigrationPolicy(ossName.str(), &engine, debugStart.getValue(), partition->getDramPages(i), allocationPolicy.getValue(), allocator, pidsPerPolicy));
			} else if (migrationPolicy.getValue() == "multi_queue"){
				oldPolicies.emplace_back(new OldMultiQueueMigrationPolicy(ossName.str(), &engine, debugStart.getValue(), partition->getDramPages(i), allocationPolicy.getValue(), allocator, pidsPerPolicy, numQueues.getValue(), thresholdQueue.getValue(), lifetime.getValue(), logicalTime.getValue(), filterThreshold.getValue(), secondDemotionEviction.getValue(), aging.getValue(), history.getValue(), pendingList.getValue()));
			} else  if (migrationPolicy.getValue() == "first_touch"){
				oldPolicies.emplace_back(new OldFirstTouchMigrationPolicy(ossName.str(), &engine, debugStart.getValue(), partition->getDramPages(i), allocationPolicy.getValue(), allocator, pidsPerPolicy));
			} else  if (migrationPolicy.getValue() == "double_clock"){
				oldPolicies.emplace_back(new OldDoubleClockMigrationPolicy(ossName.str(), &engine, debugStart.getValue(), partition->getDramPages(i), allocationPolicy.getValue(), allocator, pidsPerPolicy));
			}
			else  if (migrationPolicy.getValue() == "two_lru"){
				oldPolicies.emplace_back(new OldTwoLRUMigrationPolicy(ossName.str(), &engine, debugStart.getValue(), partition->getDramPages(i), allocationPolicy.getValue(), allocator, pidsPerPolicy));
			}
			else if (migrationPolicy.getValue() == "frequency"){
				//monitoringStrategy.getValue(), monitorApp.getValue(), promotionPolicy.getValue(), demotionPolicy.getValue(), candidateListEvictionPolicy.getValue(), flushPolicy.getValue(), candidateListSize.getValue(), migrationQueueSize.getValue(), agingPeriod.getValue(), counterReadPeriod.getValue(), accessBitPeriod.getValue(),
			} else if (migrationPolicy.getValue() == "offline"){
				string filename = counterTracePrefix.getValue() + traceNames[i] + ".gz";
				oldPolicies.emplace_back(new OldOfflineMigrationPolicy(ossName.str(), &engine, debugStart.getValue(), partition->getDramPages(i), allocationPolicy.getValue(), allocator, pidsPerPolicy, i, filename, metricType.getValue(), accessType.getValue(), weightType.getValue(), intervalCount.getValue(), metricThreshold.getValue()));
			} else {
				args.usage(cerr);
				return -1;
			}
		}
		ohmm = new OldHybridMemoryManager(&engine, &stats, debugHybridMemoryManagerStart.getValue(), numCores, numProcesses, sharedL2, oldHybridMemory, oldPolicies, partition, blockSize.getValue(), pageSize.getValue(), migrationMechanism.getValue(), monitoringType.getValue(), monitoringLocation.getValue(), flushPolicy.getValue(), flushQueueSize.getValue(), supressFlushWritebacks.getValue(), partitionPeriod.getValue(), periodType.getValue(), baseMigrationRate.getValue(), perPageStats.getValue(), perPageStatsFilename.getValue(), trace.getValue(), countersPrefix.getValue(), tracePeriod.getValue());
		manager = ohmm;
	}



	//Staller staller(&engine, 100);

	map<unsigned, Cache*> instrL1s;
	map<unsigned, Cache*> dataL1s;
	map<unsigned, TraceReaderBase*> readers;
	map<unsigned, CPU*> cpus;

	for (unsigned i = 0; i < numCores; i++){
		if (useCaches.getValue()){
			ostringstream ossName, ossDesc;
			ossName << "instr_L1_" << i;
			ossDesc << "Instruction L1 Cache " << i;
			instrL1s[i] = new Cache(ossName.str(), ossDesc.str(), &engine, &stats, debugCachesStart.getValue(), L1_WAIT, L1_TAG, L1_STALL, sharedL2, 1024*instrL1CacheSize.getValue(), blockSize.getValue(), instrL1Assoc.getValue(), CACHE_LRU, pageSize.getValue(), instrL1Penalty.getValue(), instrL1QueueSize.getValue(), realCacheRemap.getValue());
			ostringstream ossName2, ossDesc2;
			ossName2 << "data_L1_" << i;
			ossDesc2 << "Data L1 Cache " << i;
			dataL1s[i] = new Cache(ossName2.str(), ossDesc2.str(), &engine, &stats, debugCachesStart.getValue(), L1_WAIT, L1_TAG, L1_STALL, sharedL2, 1024*dataL1CacheSize.getValue(), blockSize.getValue(), dataL1Assoc.getValue(), CACHE_LRU, pageSize.getValue(), dataL1Penalty.getValue(), dataL1QueueSize.getValue(), realCacheRemap.getValue());
			sharedL2->addPrevLevel(instrL1s[i]);
			sharedL2->addPrevLevel(dataL1s[i]);
		}
		readers[i] = new CompressedTraceReader(tracePrefix.getValue() + traceNames[i], GZIP);
		ostringstream ossName3, ossDesc3;
		ossName3 << "cpu_" << i;
		ossDesc3 << "CPU " << i;

		unsigned pid = i % numProcesses;

		if (useCaches.getValue()){
			cpus[i] = new OOOCPU(&engine, ossName3.str(), ossDesc3.str(), debugCpuStart.getValue(), &stats, i, pid, manager, instrL1s[i], dataL1s[i], readers[i], blockSize.getValue(), instrLimit.getValue(), robSize.getValue(), issueWidth.getValue());
		} else {
			cpus[i] = new OOOCPU(&engine, ossName3.str(), ossDesc3.str(), debugCpuStart.getValue(), &stats, i, pid, manager, memory, memory, readers[i], blockSize.getValue(), instrLimit.getValue(), robSize.getValue(), issueWidth.getValue());
		}
		cpus[i]->start();
	}

	//Add counters to hybrid memory manager
	if (hmm != 0){
		for (unsigned i = 0; i < numCores; i++){
			hmm->addCpu(cpus[i]);
		}
		for (unsigned i = 0; i < numProcesses; i++){
			hmm->addInstrCounter(cpus[i]->getInstrCounter(), i);
		}
	}

	//Add counters to old hybrid memory manager
	if (ohmm != 0){
		for (unsigned i = 0; i < numCores; i++){
			ohmm->addCpu(cpus[i]);
			ohmm->addInstrCounter(cpus[i]->getInstrCounter(), i);
			ohmm->addDramReadsCounter(oldHybridMemory->getDramReadsCounter(i), i);
			ohmm->addDramWritesCounter(oldHybridMemory->getDramWritesCounter(i), i);
			ohmm->addPcmReadsCounter(oldHybridMemory->getPcmReadsCounter(i), i);
			ohmm->addPcmWritesCounter(oldHybridMemory->getPcmWritesCounter(i), i);

			ohmm->addDramReadTimeCounter(oldHybridMemory->getDramReadTimeCounter(i), i);
			ohmm->addDramWriteTimeCounter(oldHybridMemory->getDramWriteTimeCounter(i), i);
			ohmm->addPcmReadTimeCounter(oldHybridMemory->getPcmReadTimeCounter(i), i);
			ohmm->addPcmWriteTimeCounter(oldHybridMemory->getPcmWriteTimeCounter(i), i);
		}
	}

	for(auto it = allocationNames.begin(); it != allocationNames.end(); ++it)
		cout << *it;
	manager->allocate(allocationNames);


	class Exit : public IEventHandler{
		void process(const Event * event) {
			cout << event->getTimestamp() << ": exiting due to stop event" << endl;
			exit(0);
		}
	};

	if(stop.getValue() != 0){
		engine.addEvent(stop.getValue(), new Exit(), 0);
	}

	engine.run();

	if (statsFile.getValue().empty()){
		stats.print(cout);
	} else {
		ofstream out(statsFile.getValue().c_str());
		stats.print(out);
		out.close();
	}

	delete manager;

	return 0;
}
