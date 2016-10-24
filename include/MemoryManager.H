/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#ifndef MEMORYMANAGER_H_
#define MEMORYMANAGER_H_

#include "Cache.H"
#include "Counter.H"
#include "CPU.H"
#include "Engine.H"
#include "Error.H"
#include "HybridMemory.H"
#include "Memory.H"
#include "Migration.H"
#include "Partition.H"
#include "Statistics.H"
#include "Types.H"


using namespace std;

#define MAX_BITSET_SIZE 64

class CPU;
class Memory;
class HybridMemory;
class IMigrationPolicy;

enum FlushPolicy {
	FLUSH_PCM_BEFORE,
	FLUSH_ONLY_AFTER,
	REMAP,
	CHANGE_TAG
};

class IMemoryManager{
public:
	/*
	 * Returns whether the CPU should stall (true: stall, false: don't stall)
	 */
	virtual bool access(int pid, addrint virtualAddr, bool read, bool instr, addrint *physicalAddr, CPU *cpu) = 0;
	virtual void finish(int coreId) = 0;
	virtual void allocate(const vector<string>& filenames) = 0;

	virtual addrint getIndex(addrint addr) const = 0;
	virtual addrint getOffset(addrint addr) const = 0;
	virtual addrint getAddress(addrint index, addrint offset) const = 0;

	virtual ~IMemoryManager() {}
};

class HybridMemoryManager : public IMemoryManager, public IMemoryCallback, public IDrainCallback, public IFlushCallback, public IRemapCallback, public ITagChangeCallback, public IInterruptHandler, public IEventHandler {
	string name;

	Engine *engine;
	uint64 debugStart;

	unsigned numCores;
	unsigned numProcesses;

	vector<CPU*> cpus;
	Cache *lastLevelCache;
	HybridMemory *memory;


	vector<IMigrationPolicy*> policies;
	IPartition *partition;

	FlushPolicy flushPolicy;
	unsigned maxFlushQueueSize;
	bool suppressFlushWritebacks;

	uint64 demoteTimeout;

	uint64 partitionPeriod;
	string periodType;

	unsigned maxMigrationTableSize;

	bool perPageStats;
	string perPageStatsFilename;

	uint64 dramSize;
	uint64 pcmSize;
	unsigned pageSize;
	uint64 numDramPages;
	uint64 numPcmPages;

	unsigned blockSize;

	unsigned indexWidth;
	unsigned offsetWidth;
	addrint indexMask;
	addrint offsetMask;

	unsigned blockOffsetWidth;

	addrint firstDramAddress;
	addrint onePastLastDramAddress;
	addrint firstPcmAddress;
	addrint onePastLastPcmAddress;

	addrint firstDramPage;
	addrint onePastLastDramPage;
	addrint firstPcmPage;
	addrint onePastLastPcmPage;

	list<addrint> dramFreePageList;
	list<addrint> pcmFreePageList;

//	struct MigrationInfo{
//		PageType dest;
//		uint64 start; //start of transfer and residence
//		uint64  end; //end of residence
//		uint64 endTransfer; //end of transfer
//		uint64 readsWhileMigrating;
//		uint64 writesWhileMigrating;
//		uint64 reads;
//		uint64 writes;
//		bitset<MAX_BITSET_SIZE> readBlocks;
//		bitset<MAX_BITSET_SIZE> writtenBlocks;
//		MigrationInfo(PageType destArg, uint64 timestamp) : dest(destArg), start(timestamp), end(0), endTransfer(0), readsWhileMigrating(0), writesWhileMigrating(0), reads(0), writes(0) {
//		}
//	};

	struct PageEntry {
		addrint page;
		PageType type;
		bool isMigrating;
		bool stallOnAccess;
		//vector<MigrationInfo> migrations;
		PageEntry(addrint pageArg, PageType typeArg, uint64 timestamp) : page(pageArg), type(typeArg), isMigrating(false), stallOnAccess(false) {
			//migrations.emplace_back(typeArg, timestamp);
		}
	};

	//typedef map<addrint, PageEntry> PageMap;
	typedef unordered_map<addrint, PageEntry> PageMap;

	PageMap *pages;

	struct PhysicalPageEntry {
		int pid;
		addrint virtualPage;
		PhysicalPageEntry(int pidArg, addrint virtualPageArg) : pid(pidArg), virtualPage(virtualPageArg) {}
	};

	//typedef map<addrint, PhysicalPageEntry> PhysicalPageMap;
	typedef unordered_map<addrint, PhysicalPageEntry> PhysicalPageMap;

	PhysicalPageMap physicalPages;

	bool idle;
	uint64 lastStartIdleTime;

	//For managing migrations to PCM
	bool demoting;
	int currentPolicy;

	enum State {
		FLUSH_BEFORE,
		COPY,
		FLUSH_AFTER
	};

	struct MigrationEntry {
		int pid;
		addrint virtualPage;
		addrint destPhysicalPage;
		PageType dest;
		State state;
		bool rolledBack;
		bool needsCopying;
		unsigned drainRequestsLeft;
		unsigned flushRequestsLeft;
		unsigned stalledRequestsLeft;
		unsigned tagChangeRequestsLeft;
		uint64 startMigrationTime;
		uint64 startFlushTime;
		uint64 startCopyTime;
		MigrationEntry() {}
		MigrationEntry(int pidArg, addrint virtualPageArg, addrint destPhysicalPageArg, PageType destArg, State stateArg, uint64 timestamp)
		: pid(pidArg), virtualPage(virtualPageArg), destPhysicalPage(destPhysicalPageArg), dest(destArg), state(stateArg), rolledBack(false), needsCopying(false), drainRequestsLeft(0), flushRequestsLeft(0), stalledRequestsLeft(0), tagChangeRequestsLeft(0), startMigrationTime(timestamp), startFlushTime(timestamp), startCopyTime(timestamp){}
	};

	typedef unordered_map<addrint, MigrationEntry> MigrationMap;
	MigrationMap migrations;

	unsigned migrationTableSize;

	typedef list<pair<addrint, bool> > FlushQueue;

	struct StalledRequest {
		MemoryRequest * request;
		addrint page;
		StalledRequest(MemoryRequest * requestArg, addrint pageArg) : request(requestArg), page(pageArg) {}
	};

	typedef list<StalledRequest> StalledRequestQueue;
	typedef list<pair<addrint, addrint> > TagChangeQueue;


	unsigned flushQueueSize;

	FlushQueue flushQueue;
	StalledRequestQueue stalledRequests;
	TagChangeQueue tagChangeQueue;

	typedef map<addrint, list<CPU*> > StalledCpuMap;

	StalledCpuMap *stalledCpus;

	set<unsigned> coresFinished;


	uint64 lastIntervalStart;

	//Pointers to counters
	vector<Counter *> instrCounters;

	//For monitoring
	vector<CountEntry> monitors;
	vector<ProgressEntry> progress;
	vector<vector<CountEntry> > perPidMonitors;
	vector<vector<ProgressEntry>> perPidProgress;

	//Statistics
	Stat<uint64> dramFullMigrations;
	Stat<uint64> dramPartialMigrations;
	BinaryStat<uint64, plus<uint64> > dramMigrations;

	//Stat<uint64> selectPageCount;

	Stat<uint64> pcmFullMigrations;
	Stat<uint64> pcmPartialMigrations;
	BinaryStat<uint64, plus<uint64> > pcmMigrations;

	BinaryStat<uint64, plus<uint64> > allFullMigrations;
	BinaryStat<uint64, plus<uint64> > allPartialMigrations;
	BinaryStat<uint64, plus<uint64> > allMigrations;


	Stat<uint64> migrationEntriesSum;
	Stat<uint64> migrationEntriesCount;
	BinaryStat<double, divides<double>, uint64> avgMigrationEntries;

	Stat<uint64> cleanFlushedBlocks;
	Stat<uint64> dirtyFlushedBlocks;
	Stat<uint64> tagChanges;

	Stat<uint64> dramFullMigrationTime;
	Stat<uint64> dramPartialMigrationTime;
	BinaryStat<uint64, plus<uint64> > dramMigrationTime;

	Stat<uint64> pcmFullMigrationTime;
	Stat<uint64> pcmPartialMigrationTime;
	BinaryStat<uint64, plus<uint64> > pcmMigrationTime;

	BinaryStat<uint64, plus<uint64> > fullMigrationTime;
	BinaryStat<uint64, plus<uint64> > partialMigrationTime;
	BinaryStat<uint64, plus<uint64> > migrationTime;


	Stat<uint64> dramFlushBeforeTime;
	Stat<uint64> pcmFlushBeforeTime;
	BinaryStat<uint64, plus<uint64> > flushBeforeTime;

	Stat<uint64> dramFlushAfterTime;
	Stat<uint64> pcmFlushAfterTime;
	BinaryStat<uint64, plus<uint64> > flushAfterTime;

	BinaryStat<uint64, plus<uint64> > dramFlushTime;
	BinaryStat<uint64, plus<uint64> > pcmFlushTime;
	BinaryStat<uint64, plus<uint64> > flushTime;

	Stat<uint64> dramCopyTime;
	Stat<uint64> pcmCopyTime;
	BinaryStat<uint64, plus<uint64> > copyTime;

	Stat<uint64> idleTime;

	BinaryStat<double, divides<double>, uint64> avgDramMigrationTime;
	BinaryStat<double, divides<double>, uint64> avgPcmMigrationTime;
	BinaryStat<double, divides<double>, uint64> avgMigrationTime;

	BinaryStat<double, divides<double>, uint64> avgDramFlushBeforeTime;
	BinaryStat<double, divides<double>, uint64> avgPcmFlushBeforeTime;
	BinaryStat<double, divides<double>, uint64> avgFlushBeforeTime;

	BinaryStat<double, divides<double>, uint64> avgDramFlushAfterTime;
	BinaryStat<double, divides<double>, uint64> avgPcmFlushAfterTime;
	BinaryStat<double, divides<double>, uint64> avgFlushAfterTime;

	BinaryStat<double, divides<double>, uint64> avgDramFlushTime;
	BinaryStat<double, divides<double>, uint64> avgPcmFlushTime;
	BinaryStat<double, divides<double>, uint64> avgFlushTime;

	BinaryStat<double, divides<double>, uint64> avgDramCopyTime;
	BinaryStat<double, divides<double>, uint64> avgPcmCopyTime;
	BinaryStat<double, divides<double>, uint64> avgCopyTime;

	CalcStat<uint64, HybridMemoryManager> dramMemorySize;
	uint64 getDramMemorySize() {return dramSize;}

	CalcStat<uint64, HybridMemoryManager> dramMemorySizeUsed;
	uint64 getDramMemorySizeUsed() {return (numDramPages - dramFreePageList.size()) * pageSize;}

	CalcStat<uint64, HybridMemoryManager> pcmMemorySize;
	uint64 getPcmMemorySize() {return pcmSize;}

	CalcStat<uint64, HybridMemoryManager> pcmMemorySizeUsed;
	uint64 getPcmMemorySizeUsed() {return (numPcmPages - pcmFreePageList.size()) * pageSize;}

	Stat<uint64> dramMemorySizeInitial;
	Stat<uint64> pcmMemorySizeInitial;
	BinaryStat<uint64, plus<uint64> > totalMemorySizeInitial;

	ListStat<uint64> dramMemorySizeUsedPerPid;
	ListStat<uint64> pcmMemorySizeUsedPerPid;
	BinaryListStat<uint64, plus<uint64> > totalMemorySizeUsedPerPid;

	ListStat<uint64> dramMigrationsPerPid;
	ListStat<uint64> pcmMigrationsPerPid;
	BinaryListStat<uint64, plus<uint64> > totalMigrationsPerPid;


public:
	HybridMemoryManager(
		Engine *engineArg,
		StatContainer *statCont,
		uint64 debugStartArg,
		unsigned numCoresArg,
		unsigned numProcessesArg,
		Cache *lastLevelCacheArg,
		HybridMemory *memoryArg,
		const vector<IMigrationPolicy*>& policiesArg,
		IPartition *partitionArg,
		unsigned blockSizeArg,
		unsigned pageSizeArg,
		FlushPolicy flushPolicyArg,
		unsigned maxFlushQueueSizeArg,
		bool suppressFlushWritebacksArg,
		uint64 demoteTimeout,
		uint64 partitionPeriodArg,
		const string& periodTypeArg,
		unsigned maxMigrationTableSizeArg,
		bool perPageStatsArg,
		string perPageStatsFilenameArg);
	bool access(int pid, addrint virtualAddr, bool read, bool instr, addrint *physicalAddr, CPU *cpu);
	bool migrateOnDemand(addrint physicalPage, addrint *destPhysicalPage);
	void finish(int core);
	void allocate(const vector<string>& filenames);
	void process(const Event * event);
	void accessCompleted(MemoryRequest *, IMemory *caller);
	void unstall(IMemory *caller);
	void drainCompleted(addrint page);
	void flushCompleted(addrint addr, bool dirty, IMemory *caller);
	void copyCompleted(addrint srcPhysicalPage);
	void remapCompleted(addrint pageAddr, IMemory *caller);
	void tagChangeCompleted(addrint addr);
	void processInterrupt(Counter* counter);
	~HybridMemoryManager();

	const char* getName() const {return name.c_str();}

	addrint getIndex(addrint addr) const {return addr >> offsetWidth;}
	addrint getOffset(addrint addr) const {return addr & ~indexMask;}
	addrint getAddress(addrint index, addrint offset) const {return (index << offsetWidth) | (offset & ~indexMask);}
	addrint getBlock(addrint addr) const {return (addr & ~indexMask) >> blockOffsetWidth; }
	addrint getAddressFromBlock(addrint index, addrint block) const {return (index << offsetWidth) | (block << blockOffsetWidth);}

	bool isDramAddr(addrint physicalAddr) const {return firstDramAddress <= physicalAddr && physicalAddr < onePastLastDramAddress;}
	bool isPcmAddr(addrint physicalAddr) const {return firstPcmAddress <= physicalAddr && physicalAddr < onePastLastPcmAddress;}

	bool isDramPage(addrint physicalPage) const {return firstDramPage <= physicalPage && physicalPage < onePastLastDramPage;}
	bool isPcmPage(addrint physicalPage) const {return firstPcmPage <= physicalPage && physicalPage < onePastLastPcmPage;}

	unsigned getNumCores() {return numCores;}
	int getPidOfAddress(addrint addr);

	void addCpu(CPU *cpu);
	void addInstrCounter(Counter *counter, unsigned pid);


private:
	void selectPolicyAndDemote();
	bool startDemotion(int policy);
	void updateMonitors();

	void finishFlushing(addrint srcPhysicalPage);
	void flushPage(addrint page);
	void changeTags(addrint oldPage, addrint newPage);
	void unstallCpus(int pid, addrint virtualAddr);
	bool arePagesCompatible(addrint page1, addrint page2) const;



	enum EventType {
		DEMOTE,
		COMPLETE,
		ROLLBACK,
		COPY_PAGE,
		UPDATE_PARTITION,
		UNSTALL
	};

	void addEvent(uint64 delay, EventType type){
		engine->addEvent(delay, this, static_cast<uint64>(type));
	}

};


//Old Hybrid Memory Manager

class OldHybridMemory;
class IOldMigrationPolicy;

enum MigrationMechanism {
	PAUSE,			//Pause application when page is being migrated
	PIN,			//Pin to last level cache while page is being migrated
	REDIRECT		//Have the hybrid memory controller redirect accesses based on which blocks have been transfered
};

enum MonitoringType {
	READS,
	WRITES,
	ACCESSES
};

enum MonitoringLocation {
	BEFORE_CACHES,
	AFTER_CACHES
};

class OldHybridMemoryManager : public IMemoryManager, public IMemoryCallback, public IDrainCallback, public IFlushCallback, public IPinCallback, public IRemapCallback, public ITagChangeCallback, public IInterruptHandler, public IEventHandler {
	enum EventType {
		START_MIGRATION,
		COPY_PAGE,
		UPDATE_PARTITION,
		UNSTALL
	};

	string name;

	Engine *engine;
	uint64 debugStart;

	unsigned numCores;
	unsigned numProcesses;

	vector<CPU*> cpus;
	Cache *lastLevelCache;
	OldHybridMemory *memory;

	vector<IOldMigrationPolicy*> policies;
	IPartition *partition;

	MigrationMechanism mechanism;
	MonitoringType monitoringType;
	MonitoringLocation monitoringLocation;
	FlushPolicy flushPolicy;
	unsigned flushQueueSize;
	bool suppressFlushWritebacks;
	uint64 partitionPeriod;
	string periodType;
	double baseMigrationRate;
	bool perPageStats;
	string perPageStatsFilename;
	bool trace;
	uint64 tracePeriod;

	uint64 dramSize;
	uint64 pcmSize;
	unsigned pageSize;
	uint64 numDramPages;
	uint64 numPcmPages;

	unsigned blockSize;

	unsigned indexWidth;
	unsigned offsetWidth;
	addrint indexMask;
	addrint offsetMask;

	unsigned blockOffsetWidth;

	addrint firstDramAddress;
	addrint onePastLastDramAddress;
	addrint firstPcmAddress;
	addrint onePastLastPcmAddress;

	addrint firstDramPage;
	addrint onePastLastDramPage;
	addrint firstPcmPage;
	addrint onePastLastPcmPage;

	list<addrint> dramFreePageList;
	list<addrint> pcmFreePageList;

	struct MigrationInfo{
		PageType dest;
		uint64 start; //start of transfer and residence
		uint64  end; //end of residence
		uint64 endTransfer; //end of transfer
		uint64 readsWhileMigrating;
		uint64 writesWhileMigrating;
		uint64 reads;
		uint64 writes;
		bitset<MAX_BITSET_SIZE> readBlocks;
		bitset<MAX_BITSET_SIZE> writtenBlocks;
		MigrationInfo(PageType destArg, uint64 timestamp) : dest(destArg), start(timestamp), end(0), endTransfer(0), readsWhileMigrating(0), writesWhileMigrating(0), reads(0), writes(0) {
		}
	};

	struct PageEntry {
		addrint page;
		PageType type;
		bool isMigrating;
		bool stallOnWrite;
		bool stallOnAccess;
		//per page statistics
		vector<MigrationInfo> migrations;
		PageEntry(addrint pageArg, PageType typeArg, uint64 timestamp) : page(pageArg), type(typeArg), isMigrating(false), stallOnWrite(false), stallOnAccess(false) {
			migrations.emplace_back(typeArg, timestamp);
		}
	};

	//typedef map<addrint, PageEntry> PageMap;
	typedef unordered_map<addrint, PageEntry> PageMap;

	PageMap *pages;

	struct PhysicalPageEntry {
		int pid;
		addrint virtualPage;
		PhysicalPageEntry(int pidArg, addrint virtualPageArg) : pid(pidArg), virtualPage(virtualPageArg) {}
	};

	//typedef map<addrint, PhysicalPageEntry> PhysicalPageMap;
	typedef unordered_map<addrint, PhysicalPageEntry> PhysicalPageMap;

	PhysicalPageMap physicalPages;

	uint64 lastStartMigrationTime;
	uint64 lastStartWaitingTime;
	uint64 lastStartFlushTime;
	uint64 lastStartCopyTime;

	bool idle;
	uint64 lastStartIdleTime;

	enum State {
		NOT_MIGRATING,
		FLUSH_BEFORE,
		COPY,
		FLUSH_AFTER,
		WAITING
	};

	State state;
	uint64 wakeupTime;

	unsigned numPolicies;
	map<int, unsigned> pidToPolicy;

	int currentPolicy;

	vector<bool> active; //if true, that process is actively migrating pages and accruing tokens
	vector<int64> tokens;

	struct MigrationEntry {
		int pid;
		addrint virtualPage;
		addrint srcPhysicalPage;
		addrint destPhysicalPage;
		PageType dest;
		MigrationEntry() {}
		MigrationEntry(int pidArg, addrint virtualPageArg, addrint srcPhysicalPageArg, addrint destPysicalPageArg, PageType destArg)
		: pid(pidArg), virtualPage(virtualPageArg), srcPhysicalPage(srcPhysicalPageArg), destPhysicalPage(destPysicalPageArg), dest(destArg){}
	};

	MigrationEntry currentMigration;

	unsigned drainRequestsLeft;

	map<addrint, bool> flushQueue;
	list<pair<addrint, addrint> > tagChangeQueue;

	multiset<addrint> writebacks;
	list<MemoryRequest *> stalledRequests;

	typedef map<addrint, list<CPU*> > StalledCpuMap;

	StalledCpuMap *stalledCpus;

	set<unsigned> coresFinished;

	//For determining IPC in each interval
	//vector<uint64> instrCount;
	uint64 lastIntervalStart;

	//Counters
	vector<CycleCounter> cycleCounters;
	vector<Counter> dramMigrationsCounters;
	vector<Counter> pcmMigrationsCounters;

	vector<Counter> dramMigrationTimeCounters;
	vector<Counter> pcmMigrationTimeCounters;

	//Pointers to counters
	vector<Counter *> instrCounters;
	vector<Counter *> dramReadsCounters;
	vector<Counter *> dramWritesCounters;
	vector<Counter *> pcmReadsCounters;
	vector<Counter *> pcmWritesCounters;

	vector<Counter *> dramReadTimeCounters;
	vector<Counter *> dramWriteTimeCounters;
	vector<Counter *> pcmReadTimeCounters;
	vector<Counter *> pcmWriteTimeCounters;

	//Tracers
	vector<ofstream *> traceFiles;

	//Statistics
	Stat<uint64> dramMigrations;
	Stat<uint64> pcmMigrations;
	BinaryStat<uint64, plus<uint64> > migrations;

	Stat<uint64> cleanFlushedBlocks;
	Stat<uint64> dirtyFlushedBlocks;
	Stat<uint64> tagChanges;

	Stat<uint64> dramMigrationTime;
	Stat<uint64> pcmMigrationTime;
	BinaryStat<uint64, plus<uint64> > migrationTime;


	Stat<uint64> dramFlushBeforeTime;
	Stat<uint64> pcmFlushBeforeTime;
	BinaryStat<uint64, plus<uint64> > flushBeforeTime;

	Stat<uint64> dramFlushAfterTime;
	Stat<uint64> pcmFlushAfterTime;
	BinaryStat<uint64, plus<uint64> > flushAfterTime;

	BinaryStat<uint64, plus<uint64> > dramFlushTime;
	BinaryStat<uint64, plus<uint64> > pcmFlushTime;
	BinaryStat<uint64, plus<uint64> > flushTime;

	Stat<uint64> dramCopyTime;
	Stat<uint64> pcmCopyTime;
	BinaryStat<uint64, plus<uint64> > copyTime;

	Stat<uint64> idleTime;

	BinaryStat<double, divides<double>, uint64> avgDramMigrationTime;
	BinaryStat<double, divides<double>, uint64> avgPcmMigrationTime;
	BinaryStat<double, divides<double>, uint64> avgMigrationTime;

	BinaryStat<double, divides<double>, uint64> avgDramFlushBeforeTime;
	BinaryStat<double, divides<double>, uint64> avgPcmFlushBeforeTime;
	BinaryStat<double, divides<double>, uint64> avgFlushBeforeTime;

	BinaryStat<double, divides<double>, uint64> avgDramFlushAfterTime;
	BinaryStat<double, divides<double>, uint64> avgPcmFlushAfterTime;
	BinaryStat<double, divides<double>, uint64> avgFlushAfterTime;

	BinaryStat<double, divides<double>, uint64> avgDramFlushTime;
	BinaryStat<double, divides<double>, uint64> avgPcmFlushTime;
	BinaryStat<double, divides<double>, uint64> avgFlushTime;

	BinaryStat<double, divides<double>, uint64> avgDramCopyTime;
	BinaryStat<double, divides<double>, uint64> avgPcmCopyTime;
	BinaryStat<double, divides<double>, uint64> avgCopyTime;


	//Statistics
	CalcStat<uint64, OldHybridMemoryManager> dramMemorySize;
	uint64 getDramMemorySize() {return dramSize;}

	CalcStat<uint64, OldHybridMemoryManager> dramMemorySizeUsed;
	uint64 getDramMemorySizeUsed() {return (numDramPages - dramFreePageList.size()) * pageSize;}

	CalcStat<uint64, OldHybridMemoryManager> pcmMemorySize;
	uint64 getPcmMemorySize() {return pcmSize;}

	CalcStat<uint64, OldHybridMemoryManager> pcmMemorySizeUsed;
	uint64 getPcmMemorySizeUsed() {return (numPcmPages - pcmFreePageList.size()) * pageSize;}


	ListStat<uint64> dramMemorySizeUsedPerPid;
	ListStat<uint64> pcmMemorySizeUsedPerPid;
	BinaryListStat<uint64, plus<uint64> > totalMemorySizeUsedPerPid;

	ListStat<uint64> dramMigrationsPerPid;
	ListStat<uint64> pcmMigrationsPerPid;
	BinaryListStat<uint64, plus<uint64> > totalMigrationsPerPid;


public:
	OldHybridMemoryManager(
		Engine *engineArg,
		StatContainer *statCont,
		uint64 debugStartArg,
		unsigned numCoresArg,
		unsigned numProcessesArg,
		Cache *lastLevelCacheArg,
		OldHybridMemory *memoryArg,
		const vector<IOldMigrationPolicy*>& policiesArg,
		IPartition *partitionArg,
		unsigned blockSizeArg,
		unsigned pageSizeArg,
		MigrationMechanism mechanismArg,
		MonitoringType monitoringTypeArg,
		MonitoringLocation monitoringLocationArg,
		FlushPolicy flushPolicyArg,
		unsigned flushQueueSizeArg,
		bool suppressFlushWritebacksArg,
		uint64 partitionPeriodArg,
		const string& periodTypeArg,
		double baseMigrationRateArg,
		bool perPageStatsArg,
		string perPageStatsFilenameArg,
		bool traceArg,
		string tracePrefixArg,
		uint64 tracePeriodArg);
	bool access(int pid, addrint virtualAddr, bool read, bool instr, addrint *physicalAddr, CPU *cpu);
	void finish(int core);
	void allocate(const vector<string>& filenames);
	void monitorPhysicalAccess(addrint addr, bool read, bool instr);
	void process(const Event * event);
	void pinCompleted(addrint addr, IMemory *caller);
	void accessCompleted(MemoryRequest *, IMemory *caller) {myassert(false);}
	void unstall(IMemory *caller);
	void drainCompleted(addrint page);
	void flushCompleted(addrint addr, bool dirty, IMemory *caller);
	void copyCompleted();
	void remapCompleted(addrint pageAddr, IMemory *caller);
	void tagChangeCompleted(addrint addr);
	void processInterrupt(Counter* counter);
	~OldHybridMemoryManager();

	const char* getName() const {return name.c_str();}

	addrint getIndex(addrint addr) const {return addr >> offsetWidth;}
	addrint getOffset(addrint addr) const {return addr & ~indexMask;}
	addrint getAddress(addrint index, addrint offset) const {return (index << offsetWidth) | (offset & ~indexMask);}
	addrint getBlock(addrint addr) const {return (addr & ~indexMask) >> blockOffsetWidth; }
	addrint getAddressFromBlock(addrint index, addrint block) const {return (index << offsetWidth) | (block << blockOffsetWidth);}


	bool isDramAddr(addrint physicalAddr) const {return firstDramAddress <= physicalAddr && physicalAddr < onePastLastDramAddress;}
	bool isPcmAddr(addrint physicalAddr) const {return firstPcmAddress <= physicalAddr && physicalAddr < onePastLastPcmAddress;}

	bool isDramPage(addrint physicalPage) const {return firstDramPage <= physicalPage && physicalPage < onePastLastDramPage;}
	bool isPcmPage(addrint physicalPage) const {return firstPcmPage <= physicalPage && physicalPage < onePastLastPcmPage;}

	unsigned getNumCores() {return numCores;}
	int getPidOfAddress(addrint addr);

	void addCpu(CPU *cpu);
	void addInstrCounter(Counter *counter, unsigned pid);
	void addDramReadsCounter(Counter *counter, unsigned pid);
	void addDramWritesCounter(Counter *counter, unsigned pid);
	void addPcmReadsCounter(Counter *counter, unsigned pid);
	void addPcmWritesCounter(Counter *counter, unsigned pid);
	void addDramReadTimeCounter(Counter *counter, unsigned pid);
	void addDramWriteTimeCounter(Counter *counter, unsigned pid);
	void addPcmReadTimeCounter(Counter *counter, unsigned pid);
	void addPcmWriteTimeCounter(Counter *counter, unsigned pid);


private:
	void selectPolicyAndMigrate();
	bool startMigration(int pid);
	void distributeTokens(uint64 numTokens);
	void finishFlushing();
	void pinPage(addrint page);
	void unpinPage(addrint page);
	void flushPage(addrint page);
	void changeTags(addrint oldPage, addrint newPage);
	void unstallCpus(int pid, addrint virtualAddr);
	bool arePagesCompatible(addrint page1, addrint page2) const;

	void addEvent(uint64 delay, EventType type){
		engine->addEvent(delay, this, static_cast<uint64>(type));
	}
};


class SimpleMemoryManager : public IMemoryManager {
	//typedef pair<int, addrint> PidAddrPair;
	//typedef map<PidAddrPair, addrint> PageMap;
	//typedef std::tr1::unordered_map<PidAddrPair, addrint> PageMap;

	//typedef map<addrint, addrint> PageMap;
	typedef unordered_map<addrint, addrint> PageMap;


	string name;

	Memory *memory;

	unsigned numProcesses;

	unsigned logPageSize;
	unsigned pageSize;
	uint64 numPages;
	uint64 size;

	unsigned indexWidth;
	unsigned offsetWidth;
	addrint indexMask;
	addrint offsetMask;

	addrint firstAddress;
	addrint onePastLastAddress;
	addrint firstPage;
	addrint onePastLastPage;

	list<addrint> freePageList;

	PageMap *pages;


public:
	SimpleMemoryManager(StatContainer *cont, Memory *memoryArg, unsigned numProcessesArg, unsigned pageSizeArg);
	~SimpleMemoryManager();
	bool access(int pid, addrint virtualAddr, bool read, bool instr, addrint *physicalAddr, CPU *cpu);
	void finish(int coreId);
	void allocate(const vector<string>& filenames);

	addrint getIndex(addrint addr) const {return addr >> offsetWidth;}
	addrint getOffset(addrint addr) const {return addr & ~indexMask;}
	addrint getAddress(addrint index, addrint offset) const {return (index << offsetWidth) | (offset & ~indexMask);}

	//Statistics
	CalcStat<uint64, SimpleMemoryManager> memorySize;
	uint64 getMemorySize() {return size;}

	CalcStat<uint64, SimpleMemoryManager> memorySizeUsed;
	uint64 getMemorySizeUsed() {return (numPages - freePageList.size()) * pageSize;}

};

//For the old hybrid memory manager:
enum MonitoringStrategy {
	NO_PAM, //Don't use the candidate list, but start migrating the page with the first request after the previous migration finished
	PAM		//Use the candidate lis
};

enum QueuePolicy {
	FIFO,
	LRU,
	FREQ	//The entry with the highest number of accesses (or writes) has higher priority
};


istream& operator>>(istream& lhs, MigrationMechanism& rhs);
ostream& operator<<(ostream& lhs, MigrationMechanism rhs);

istream& operator>>(istream& lhs, MonitoringType& rhs);
ostream& operator<<(ostream& lhs, MonitoringType rhs);

istream& operator>>(istream& lhs, MonitoringLocation& rhs);
ostream& operator<<(ostream& lhs, MonitoringLocation rhs);

istream& operator>>(istream& lhs, FlushPolicy& rhs);
ostream& operator<<(ostream& lhs, FlushPolicy rhs);

istream& operator>>(istream& lhs, MonitoringStrategy& rhs);
ostream& operator<<(ostream& lhs, MonitoringStrategy rhs);

istream& operator>>(istream& lhs, QueuePolicy& rhs);
ostream& operator<<(ostream& lhs, QueuePolicy rhs);

#undef MAX_BITSET_SIZE

#endif /* MEMORYMANAGER_H_ */
