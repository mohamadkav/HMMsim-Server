/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#ifndef HYBRIDMEMORY_H_
#define HYBRIDMEMORY_H_

#include "Counter.H"
#include "Engine.H"
#include "Error.H"
#include "Memory.H"
#include "MemoryHierarchy.H"
#include "MemoryManager.H"
#include "Statistics.H"
#include "Types.H"



class Memory;
class HybridMemoryManager;

class HybridMemory : public IEventHandler, public IMemory, public IMemoryCallback {
	string name;
	string desc;
	Engine *engine;

	uint64 debugStart;

	unsigned numProcesses;
	Memory *dram;
	Memory *pcm;

	HybridMemoryManager *manager;

	unsigned blockSize;
	unsigned pageSize;
	unsigned blocksPerPage;

	uint64 dramMigrationReadDelay;
	uint64 dramMigrationWriteDelay;
	uint64 pcmMigrationReadDelay;
	uint64 pcmMigrationWriteDelay;

	unsigned completionThreshold;

	bool elideCleanDramBlocks;

	bool fixedPcmMigrationCost;
	uint64 pcmMigrationCost;

	addrint pcmOffset;

	enum BlockState {
		NOT_READ, 		//Read has not been sent to src memory
		READING, 		//Read has been sent to src memory
		BUFFERED,		//Read has returned from src memory but write has not been sent to dest memory
		WRITTEN 		//Write to dest memory has been sent
	};

	struct Caller {
		MemoryRequest *request;
		IMemoryCallback *callback;
		Caller(MemoryRequest *requestArg, IMemoryCallback *callbackArg) : request(requestArg), callback(callbackArg) {}
	};

	struct BlockEntry {
		BlockState state;
		bool dirty;
		MemoryRequest *request;
		list<Caller> callers;
		uint64 startTime;
		BlockEntry() : state(NOT_READ), dirty(false), request(0), startTime(0) {}
	};

	typedef vector<BlockEntry> BlockList;

	struct MigrationEntry {
		addrint destPage;
		Memory *src;
		Memory *dest;
		uint64 readDelay;
		uint64 writeDelay;
		BlockList blocks;
		uint32 blocksLeftToRead;
		uint32 blockLeftToCompleteRead;
		uint32 blocksLeftToWrite;
		int nextReadBlock;
		int nextWriteBlock; //next block to write to memory, -1 if none
		bool rolledBack;
		uint64 lastWrite; //time the last write was sent to memory
		uint64 startPageCopyTime;
		MigrationEntry(addrint destPageArg, Memory *srcArg, Memory *destArg, uint64 readDelayArg, uint64 writeDelayArg, uint32 blocksLeftArg, uint64 startPageCopyTimeArg) :
			destPage(destPageArg), src(srcArg), dest(destArg), readDelay(readDelayArg), writeDelay(writeDelayArg), blocksLeftToRead(blocksLeftArg), blockLeftToCompleteRead(blocksLeftArg), blocksLeftToWrite(blocksLeftArg), nextReadBlock(0), nextWriteBlock(-1), rolledBack(false), lastWrite(0), startPageCopyTime(startPageCopyTimeArg) {}
	};

	typedef unordered_map<addrint, MigrationEntry> MigrationTable;

	MigrationTable migrations;

	typedef unordered_map<addrint, addrint> RolledBackTable;

	RolledBackTable rolledBackMigrations;

	struct CallbackEntry{
		IMemoryCallback* callback;
		addrint callbackAddr;
		bool partOfMigration;
		addrint page; //valid only if part of migration
		uint64 startTime;
		CallbackEntry(IMemoryCallback* callbackArg, addrint callbackAddrArg, bool partOfMigrationArg, addrint pageArg, uint64 startTimeArg) : callback(callbackArg), callbackAddr(callbackAddrArg), partOfMigration(partOfMigrationArg), page(pageArg), startTime(startTimeArg) {}
	};

	unordered_map<MemoryRequest *, CallbackEntry> callbacks;

	list<addrint> stalledOnRead;		//list of pages being copied that stalled while reading blocks from source
	list<addrint> stalledOnWrite;		//list of pages being copied that stalled while writing blocks to destination

	set<IMemoryCallback *> dramStalledCallers;
	set<IMemoryCallback *> pcmStalledCallers;

	list<Caller> notifications;

	//for keeping track of dirty block in DRAM
	typedef unordered_map<addrint, vector<bool >> DirtyMap;
	DirtyMap dirties;

	//Monitoring
	typedef unordered_map<addrint, CountEntry> MonitorMap;
	MonitorMap monitors;

	//Statistics
	Stat<uint64> dramReads;
	Stat<uint64> dramWrites;
	BinaryStat<uint64, plus<uint64> > dramAccesses;

	Stat<uint64> pcmReads;
	Stat<uint64> pcmWrites;
	BinaryStat<uint64, plus<uint64> > pcmAccesses;

	BinaryStat<uint64, plus<uint64> > totalReads;
	BinaryStat<uint64, plus<uint64> > totalWrites;
	BinaryStat<uint64, plus<uint64> > totalAccesses;


	BinaryStat<double, divides<double>, uint64> dramReadFraction;
	BinaryStat<double, divides<double>, uint64> pcmReadFraction;

	BinaryStat<double, divides<double>, uint64> dramWriteFraction;
	BinaryStat<double, divides<double>, uint64> pcmWriteFraction;

	BinaryStat<double, divides<double>, uint64> dramAccessFraction;
	BinaryStat<double, divides<double>, uint64> pcmAccessFraction;


	Stat<uint64> readsFromDram;
	Stat<uint64> readsFromPcm;
	Stat<uint64> readsFromBuffer;

	Stat<uint64> writesToDram;
	Stat<uint64> writesToPcm;
	Stat<uint64> writesToBuffer;


	Stat<uint64> dramReadTime;
	Stat<uint64> dramWriteTime;
	BinaryStat<uint64, plus<uint64> > dramAccessTime;

	Stat<uint64> pcmReadTime;
	Stat<uint64> pcmWriteTime;
	BinaryStat<uint64, plus<uint64> > pcmAccessTime;

	BinaryStat<uint64, plus<uint64> > totalAccessTime;

	BinaryStat<double, divides<double>, uint64> avgDramReadTime;
	BinaryStat<double, divides<double>, uint64> avgDramWriteTime;
	BinaryStat<double, divides<double>, uint64> avgDramAccessTime;

	BinaryStat<double, divides<double>, uint64> avgPcmReadTime;
	BinaryStat<double, divides<double>, uint64> avgPcmWriteTime;
	BinaryStat<double, divides<double>, uint64> avgPcmAccessTime;

	BinaryStat<double, divides<double>, uint64> avgAccessTime;

	Stat<uint64> dramCopyReads;
	Stat<uint64> dramCopyWrites;
	BinaryStat<uint64, plus<uint64> > dramCopyAccesses;

	Stat<uint64> pcmCopyReads;
	Stat<uint64> pcmCopyWrites;
	BinaryStat<uint64, plus<uint64> > pcmCopyAccesses;

	BinaryStat<uint64, plus<uint64> > totalCopyAccesses;

	Stat<uint64> dramCopyReadTime;
	Stat<uint64> dramCopyWriteTime;
	BinaryStat<uint64, plus<uint64> > dramCopyAccessTime;

	Stat<uint64> pcmCopyReadTime;
	Stat<uint64> pcmCopyWriteTime;
	BinaryStat<uint64, plus<uint64> > pcmCopyAccessTime;

	BinaryStat<uint64, plus<uint64> > totalCopyAccessTime;

	BinaryStat<double, divides<double>, uint64> avgCopyDramReadTime;
	BinaryStat<double, divides<double>, uint64> avgCopyDramWriteTime;
	BinaryStat<double, divides<double>, uint64> avgCopyDramAccessTime;

	BinaryStat<double, divides<double>, uint64> avgCopyPcmReadTime;
	BinaryStat<double, divides<double>, uint64> avgCopyPcmWriteTime;
	BinaryStat<double, divides<double>, uint64> avgCopyPcmAccessTime;

	BinaryStat<double, divides<double>, uint64> avgCopyAccessTime;


	Stat<uint64> dramPageCopies; //This means migrate TO dram
	Stat<uint64> pcmPageCopies;  //This means migrate TO pcm

	Stat<uint64> dramPageCopyTime;
	Stat<uint64> pcmPageCopyTime;


	ListStat<uint64> dramReadsPerPid;
	ListStat<uint64> dramWritesPerPid;
	BinaryListStat<uint64, plus<uint64> > dramAccessesPerPid;

	ListStat<uint64> pcmReadsPerPid;
	ListStat<uint64> pcmWritesPerPid;
	BinaryListStat<uint64, plus<uint64> > pcmAccessesPerPid;

	BinaryListStat<uint64, plus<uint64> > totalReadsPerPid;
	BinaryListStat<uint64, plus<uint64> > totalWritesPerPid;
	BinaryListStat<uint64, plus<uint64> > totalAccessesPerPid;

	BinaryListStat<double, divides<double>, uint64> dramReadFractionPerPid;
	BinaryListStat<double, divides<double>, uint64> pcmReadFractionPerPid;

	BinaryListStat<double, divides<double>, uint64> dramWriteFractionPerPid;
	BinaryListStat<double, divides<double>, uint64> pcmWriteFractionPerPid;

	BinaryListStat<double, divides<double>, uint64> dramAccessFractionPerPid;
	BinaryListStat<double, divides<double>, uint64> pcmAccessFractionPerPid;

	ListStat<uint64> dramReadTimePerPid;
	ListStat<uint64> dramWriteTimePerPid;
	BinaryListStat<uint64, plus<uint64> > dramAccessTimePerPid;

	ListStat<uint64> pcmReadTimePerPid;
	ListStat<uint64> pcmWriteTimePerPid;
	BinaryListStat<uint64, plus<uint64> > pcmAccessTimePerPid;

	BinaryListStat<uint64, plus<uint64> > totalAccessTimePerPid;

	BinaryListStat<double, divides<double>, uint64> avgDramReadTimePerPid;
	BinaryListStat<double, divides<double>, uint64> avgDramWriteTimePerPid;
	BinaryListStat<double, divides<double>, uint64> avgDramAccessTimePerPid;
	BinaryListStat<double, divides<double>, uint64> avgPcmReadTimePerPid;
	BinaryListStat<double, divides<double>, uint64> avgPcmWriteTimePerPid;
	BinaryListStat<double, divides<double>, uint64> avgPcmAccessTimePerPid;
	BinaryListStat<double, divides<double>, uint64> avgAccessTimePerPid;

public:
	HybridMemory(
		const string& nameArg,
		const string& descArg,
		Engine *engineArg,
		StatContainer *statCont,
		uint64 debugStartArg,
		unsigned numProcessesArg,
		Memory *dramArg,
		Memory *pcmArg,
		unsigned blockSizeArg,
		unsigned pageSizeArg,
		uint64 dramMigrationReadDelayArg,
		uint64 dramMigrationWriteDelayArg,
		uint64 pcmMigrationReadDelayArg,
		uint64 pcmMigrationWriteDelayArg,
		unsigned completionThresholdArg,
		bool elideCleanDramBlocksArg,
		bool fixedPcmMigrationCostArg,
		uint64 pcmMigrationCostArg);

	bool access(MemoryRequest *request, IMemoryCallback *caller);
	void accessCompleted(MemoryRequest *request, IMemory *caller);
	void copyPage(addrint srcPage, addrint destPage);
	void finishMigration(addrint srcPage);
	void complete(addrint srcPage);
	void rollback(addrint srcPage);
	void process(const Event *event);
	void unstall(IMemory *caller);

	void readCountsAndProgress(vector<CountEntry> *monitor, vector<ProgressEntry> *progress);

	void setManager(HybridMemoryManager *managerArg);
	uint64 getDramSize();
	uint64 getPcmSize();

	const char* getName() const {return name.c_str();}

private:

	enum EventType {
		COPY,
		READ,
		WRITE,
		NOTIFY
	};

	struct EventData {
		EventType type;
		addrint page;
		EventData(EventType typeArg, addrint pageArg) : type(typeArg), page(pageArg) {}
	};

	void addEvent(uint64 delay, EventType type, addrint page = 0);

	bool accessNextLevel(MemoryRequest *request, IMemoryCallback *caller, addrint callbackAddr, bool partOfMigration, addrint page);

};


class OldHybridMemoryManager;

class OldHybridMemory : public IEventHandler, public IMemory, public IMemoryCallback {

	enum EventType {
		COPY,
		UNSTALL_DRAM,
		UNSTALL_PCM
	};

	string name;
	string desc;
	Engine *engine;

	uint64 debugStart;

	unsigned numProcesses;
	Memory *dram;
	Memory *pcm;

	OldHybridMemoryManager *manager;

	unsigned blockSize;
	unsigned pageSize;

	bool burstMigration;
	bool fixedDramMigrationCost;
	bool fixedPcmMigrationCost;
	uint64 dramMigrationCost;
	uint64 pcmMigrationCost;

	bool redirect;

	addrint pcmOffset;

	bool copying;
	addrint srcPage;
	addrint destPage;
	Memory *src;
	Memory *dest;

	enum BlockState {
		WAITING, 		//Read has not been sent to src memory
		READING, 		//Read has been sent to src memory
	};

	struct BlockEntry {
		BlockState state;
		bool ignoreRead; //true if the read should be ignored when it comes back
		MemoryRequest *request;
		uint64 startTime;
		BlockEntry() : state(WAITING), ignoreRead(false), request(0), startTime(0) {}
	};

	typedef map<addrint, BlockEntry> BlockMap;
	BlockMap blocks;
	//if the block is not in the map, it means it has already been written

	struct CallbackEntry{
		IMemoryCallback* callback;
		uint64 startTime;
		CallbackEntry(IMemoryCallback* callbackArg, uint64 startTimeArg) : callback(callbackArg), startTime(startTimeArg) {}
	};
	multimap<MemoryRequest *, CallbackEntry> callbacks;

	set<IMemoryCallback *> dramStalledCallers;
	set<IMemoryCallback *> pcmStalledCallers;

	list<MemoryRequest *> dramStalledRequests;
	list<MemoryRequest *> pcmStalledRequests;

	list<MemoryRequest *>* srcStalledRequests;
	list<MemoryRequest *>* destStalledRequests;

	//Counters
	vector<Counter> dramReadsCounters;
	vector<Counter> dramWritesCounters;
	vector<Counter> pcmReadsCounters;
	vector<Counter> pcmWritesCounters;

	vector<Counter> dramReadTimeCounters;
	vector<Counter> dramWriteTimeCounters;
	vector<Counter> pcmReadTimeCounters;
	vector<Counter> pcmWriteTimeCounters;


	//Statistics
	Stat<uint64> dramReads;
	Stat<uint64> dramWrites;
	BinaryStat<uint64, plus<uint64> > dramAccesses;

	Stat<uint64> pcmReads;
	Stat<uint64> pcmWrites;
	BinaryStat<uint64, plus<uint64> > pcmAccesses;

	BinaryStat<uint64, plus<uint64> > totalReads;
	BinaryStat<uint64, plus<uint64> > totalWrites;
	BinaryStat<uint64, plus<uint64> > totalAccesses;

	BinaryStat<double, divides<double>, uint64> dramReadFraction;
	BinaryStat<double, divides<double>, uint64> pcmReadFraction;

	BinaryStat<double, divides<double>, uint64> dramWriteFraction;
	BinaryStat<double, divides<double>, uint64> pcmWriteFraction;

	BinaryStat<double, divides<double>, uint64> dramAccessFraction;
	BinaryStat<double, divides<double>, uint64> pcmAccessFraction;


	Stat<uint64> dramReadTime;
	Stat<uint64> dramWriteTime;
	BinaryStat<uint64, plus<uint64> > dramAccessTime;

	Stat<uint64> pcmReadTime;
	Stat<uint64> pcmWriteTime;
	BinaryStat<uint64, plus<uint64> > pcmAccessTime;

	BinaryStat<uint64, plus<uint64> > totalAccessTime;

	BinaryStat<double, divides<double>, uint64> avgDramReadTime;
	BinaryStat<double, divides<double>, uint64> avgDramWriteTime;
	BinaryStat<double, divides<double>, uint64> avgDramAccessTime;

	BinaryStat<double, divides<double>, uint64> avgPcmReadTime;
	BinaryStat<double, divides<double>, uint64> avgPcmWriteTime;
	BinaryStat<double, divides<double>, uint64> avgPcmAccessTime;

	BinaryStat<double, divides<double>, uint64> avgAccessTime;

	Stat<uint64> dramCopyReads;
	Stat<uint64> dramCopyWrites;
	BinaryStat<uint64, plus<uint64> > dramCopyAccesses;

	Stat<uint64> pcmCopyReads;
	Stat<uint64> pcmCopyWrites;
	BinaryStat<uint64, plus<uint64> > pcmCopyAccesses;

	BinaryStat<uint64, plus<uint64> > totalCopyAccesses;

	Stat<uint64> dramCopyReadTime;
	Stat<uint64> dramCopyWriteTime;
	BinaryStat<uint64, plus<uint64> > dramCopyAccessTime;

	Stat<uint64> pcmCopyReadTime;
	Stat<uint64> pcmCopyWriteTime;
	BinaryStat<uint64, plus<uint64> > pcmCopyAccessTime;

	BinaryStat<uint64, plus<uint64> > totalCopyAccessTime;

	BinaryStat<double, divides<double>, uint64> avgCopyDramReadTime;
	BinaryStat<double, divides<double>, uint64> avgCopyDramWriteTime;
	BinaryStat<double, divides<double>, uint64> avgCopyDramAccessTime;

	BinaryStat<double, divides<double>, uint64> avgCopyPcmReadTime;
	BinaryStat<double, divides<double>, uint64> avgCopyPcmWriteTime;
	BinaryStat<double, divides<double>, uint64> avgCopyPcmAccessTime;

	BinaryStat<double, divides<double>, uint64> avgCopyAccessTime;


	Stat<uint64> dramPageCopies; //This means migrate TO dram
	Stat<uint64> pcmPageCopies;  //This means migrate TO pcm

	Stat<uint64> dramPageCopyTime;
	Stat<uint64> pcmPageCopyTime;


	uint64 startDramPageCopyTime;
	uint64 startPcmPageCopyTime;

	ListStat<uint64> dramReadsPerPid;
	ListStat<uint64> dramWritesPerPid;
	BinaryListStat<uint64, plus<uint64> > dramAccessesPerPid;

	ListStat<uint64> pcmReadsPerPid;
	ListStat<uint64> pcmWritesPerPid;
	BinaryListStat<uint64, plus<uint64> > pcmAccessesPerPid;

	BinaryListStat<uint64, plus<uint64> > totalReadsPerPid;
	BinaryListStat<uint64, plus<uint64> > totalWritesPerPid;
	BinaryListStat<uint64, plus<uint64> > totalAccessesPerPid;

	BinaryListStat<double, divides<double>, uint64> dramReadFractionPerPid;
	BinaryListStat<double, divides<double>, uint64> pcmReadFractionPerPid;

	BinaryListStat<double, divides<double>, uint64> dramWriteFractionPerPid;
	BinaryListStat<double, divides<double>, uint64> pcmWriteFractionPerPid;

	BinaryListStat<double, divides<double>, uint64> dramAccessFractionPerPid;
	BinaryListStat<double, divides<double>, uint64> pcmAccessFractionPerPid;

	ListStat<uint64> dramReadTimePerPid;
	ListStat<uint64> dramWriteTimePerPid;
	BinaryListStat<uint64, plus<uint64> > dramAccessTimePerPid;

	ListStat<uint64> pcmReadTimePerPid;
	ListStat<uint64> pcmWriteTimePerPid;
	BinaryListStat<uint64, plus<uint64> > pcmAccessTimePerPid;

	BinaryListStat<uint64, plus<uint64> > totalAccessTimePerPid;

	BinaryListStat<double, divides<double>, uint64> avgDramReadTimePerPid;
	BinaryListStat<double, divides<double>, uint64> avgDramWriteTimePerPid;
	BinaryListStat<double, divides<double>, uint64> avgDramAccessTimePerPid;
	BinaryListStat<double, divides<double>, uint64> avgPcmReadTimePerPid;
	BinaryListStat<double, divides<double>, uint64> avgPcmWriteTimePerPid;
	BinaryListStat<double, divides<double>, uint64> avgPcmAccessTimePerPid;
	BinaryListStat<double, divides<double>, uint64> avgAccessTimePerPid;


public:
	OldHybridMemory(
			const string& nameArg,
			const string& descArg,
			Engine *engineArg,
			StatContainer *statCont,
			uint64 debugStartArg,
			unsigned numProcessesArg,
			Memory *dramArg,
			Memory *pcmArg,
			unsigned blockSizeArg,
			unsigned pageSizeArg,
			bool burstMigrationArg,
			bool fixedDramMigrationCostArg,
			bool fixedPcmMigrationCostArg,
			uint64 dramMigrationCostArg,
			uint64 pcmMigrationCostArg,
			bool redirectArg);
	virtual ~OldHybridMemory();

	void process(const Event *event);
	bool access(MemoryRequest *request, IMemoryCallback *caller);
	void accessCompleted(MemoryRequest *request, IMemory *caller);
	void unstall(IMemory *caller);
	void copyPage(addrint srcPage, addrint destPage);

	void setManager(OldHybridMemoryManager *managerArg);
	uint64 getDramSize();
	uint64 getPcmSize();

	const char* getName() const {return name.c_str();}

	Counter* getDramReadsCounter(int pid){return &dramReadsCounters[pid];}
	Counter* getDramWritesCounter(int pid){return &dramWritesCounters[pid];}
	Counter* getPcmReadsCounter(int pid){return &pcmReadsCounters[pid];}
	Counter* getPcmWritesCounter(int pid){return &pcmWritesCounters[pid];}

	Counter* getDramReadTimeCounter(int pid){return &dramReadTimeCounters[pid];}
	Counter* getDramWriteTimeCounter(int pid){return &dramWriteTimeCounters[pid];}
	Counter* getPcmReadTimeCounter(int pid){return &pcmReadTimeCounters[pid];}
	Counter* getPcmWriteTimeCounter(int pid){return &pcmWriteTimeCounters[pid];}

private:
	void addEvent(uint64 delay, EventType type){
		engine->addEvent(delay, this, static_cast<uint64>(type));
	}
};



#endif /* HYBRIDMEMORY_H_ */
