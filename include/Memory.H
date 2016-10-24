/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#ifndef MEMORY_H_
#define MEMORY_H_


#include "Bank.H"
#include "Engine.H"
#include "Error.H"
#include "MemoryHierarchy.H"
#include "MemoryManager.H"
#include "Statistics.H"
#include "Types.H"


class Staller : public IEventHandler, public IMemory {
	Engine *engine;
	uint64 penalty;


public:
	Staller(Engine *engineArg, uint64 penaltyArg);
	bool access(MemoryRequest *request, IMemoryCallback *caller);
	void process(const Event *event);
};


class Memory : public IMemory, public IMemoryCallback {
	string name;
	string desc;
	Engine *engine;

	uint64 debugStart;

	bool globalQueue;
	int maxQueueSize; // for per bank queues, the size of each queue
	MemoryMapping mapping;
	addrint offset;
	Bus* bus;
	vector<Bank*> banks;

	int *queueSizes;

	typedef multimap<MemoryRequest *, IMemoryCallback *> RequestMap;
	RequestMap requests;

	bool stalled;
	set<IMemoryCallback*> stalledCallers;
	uint64 stallStartTimestamp;

	//Statistics
	Stat<uint64> criticalStallTime;

	Stat<uint64> readStallTime;
	Stat<uint64> writeStallTime;

	Stat<uint64> queueStallTime;

	//Aggregate bank statistics
	AggregateStat<uint64> numReadRequests;
	AggregateStat<uint64> numWriteRequests;
	AggregateStat<uint64> readQueueTime;
	AggregateStat<uint64> writeQueueTime;
	AggregateStat<uint64> readTotalTime;
	AggregateStat<uint64> writeTotalTime;

	AggregateStat<uint64> rowBufferHits;
	AggregateStat<uint64> rowBufferMisses;

	AggregateStat<uint64> numOpens;
	AggregateStat<uint64> numAccesses;
	AggregateStat<uint64> numCloses;

	AggregateStat<uint64> numRARs;
	AggregateStat<uint64> numRAWs;
	AggregateStat<uint64> numWARs;
	AggregateStat<uint64> numWAWs;

	AggregateStat<uint64> waitLowerPriorityTime;
	AggregateStat<uint64> waitSamePriorityTime;
	AggregateStat<uint64> waitHigherPriorityTime;

	//Derived statistics
	BinaryStat<uint64, plus<uint64> > numRequests;
	BinaryStat<double, divides<double>, uint64> averageQueueStallTime;
	BinaryStat<uint64, plus<uint64> > totalStallTime;
	BinaryStat<uint64, plus<uint64> > totalQueueTime;
	BinaryStat<uint64, minus<uint64> > readServiceTime;
	BinaryStat<uint64, minus<uint64> > writeServiceTime;
	BinaryStat<uint64, plus<uint64> > totalServiceTime;
	BinaryStat<uint64, plus<uint64> > totalTime;
	BinaryStat<double, divides<double>, uint64> averageReadQueueTime;
	BinaryStat<double, divides<double>, uint64> averageWriteQueueTime;
	BinaryStat<double, divides<double>, uint64> averageQueueTime;
	BinaryStat<double, divides<double>, uint64> averageReadServiceTime;
	BinaryStat<double, divides<double>, uint64> averageWriteServiceTime;
	BinaryStat<double, divides<double>, uint64> averageServiceTime;
	BinaryStat<double, divides<double>, uint64> averageReadTime;
	BinaryStat<double, divides<double>, uint64> averageWriteTime;
	BinaryStat<double, divides<double>, uint64> averageTime;
	BinaryStat<uint64, plus<uint64> > rowBufferAccesses;


public:
	Memory(
		const string& nameArg,
		const string& descArg,
		Engine *engineArg,
		StatContainer *statCont,
		uint64 debugStartArg,
		CounterIndex queueCounterIndexArg,
		CounterIndex openCounterIndexArg,
		CounterIndex accessCounterIndexArg,
		CounterIndex closeCounterIndexArg,
		CounterIndex busQueueCounterIndexArg,
		CounterIndex busCounterIndexArg,
		RowBufferPolicy policyArg,
		MemoryType typeArg,
		MappingType mappingTypeArg,
		bool globalQueueArg,
		unsigned maxQueueSizeArg,
		unsigned numRanksArg,
		unsigned banksPerRankArg,
		unsigned rowsPerBankArg,
		unsigned blocksPerRowArg,
		unsigned blockSizeArg,
		uint64 openLatencyArg,
		uint64 closeLatencyArg,
		uint64 accessLatencyArg,
		bool longCloseLatencyArg,
		uint64 busLatencyArg,
		addrint offsetArg);
	virtual ~Memory();

	void process(const Event *event);
	bool access(MemoryRequest *request, IMemoryCallback *caller);
	void accessCompleted(MemoryRequest *request, IMemory *caller);
	void unstall(IMemory *caller) {}
	MemoryMapping* getMapping() {return &mapping;}

	uint64 getSize() {return mapping.getTotalSize();}
	uint64 getBlockSize() {return mapping.getBlockSize();}
	addrint getBlockAddress(addrint addr) {return mapping.getBlockAddress(addr);}

	const char* getName() const {return name.c_str();}

};


class CacheMemory : public IEventHandler, public IMemory, public IMemoryCallback {

	enum State {
		START,
		READ_DRAM,
		READ_PCM,
		DONE
	};

	struct InternalRequest {
		MemoryRequest *request;
		IMemoryCallback *caller;
		State state;
		CacheModel::Result result;
		addrint evictedAddr;
		addrint internalAddr;
		addrint smallBlockOffset;
		addrint originalAddr;
		int dramReadNotificationsLeft;
		int pcmReadNotificationsLeft;
		//Statistics
		uint64 arrivalTimestamp;
		uint64 dequeueTimestamp;
		uint64 tagAccessedTimestamp;
		uint64 dataReadyTimestamp;
		InternalRequest(MemoryRequest *reqArg, IMemoryCallback *callerArg, State stateArg, uint64 timestampArg) :
				request(reqArg),
				caller(callerArg),
				state(stateArg),
				arrivalTimestamp(timestampArg),
				dequeueTimestamp(timestampArg) {
		}
	};

	typedef map<addrint, InternalRequest> InternalRequestMap;

	struct OutgoingRequest {
		addrint internalRequestAddr;
		uint64 timestamp;
		OutgoingRequest(addrint internalRequestAddrArg, uint64 timestampArg) : internalRequestAddr(internalRequestAddrArg), timestamp(timestampArg) {}
	};

	typedef map<addrint, OutgoingRequest> OutgoingRequestMap;

	enum EventType {
		TAG_ARRAY,
		ACCESS,
		UNSTALL_DRAM,
		UNSTALL_PCM,
		EVENT_TYPE_SIZE
	};

	string name;
	string desc;
	Engine *engine;

	uint64 debugStart;
	Memory *dram;
	Memory *pcm;
	CacheModel cacheModel;
	uint64 penalty;
	int maxQueueSize;

	uint64 smallBlockSize;
	unsigned numBlocks;


	InternalRequestMap internalRequests;
	OutgoingRequestMap dramRequests;
	OutgoingRequestMap pcmRequests;



	struct DelayedRequest {
		Memory *memory;
		MemoryRequest *request;
		DelayedRequest(Memory *memoryArg, MemoryRequest *reqArg) :
				memory(memoryArg), request(reqArg){
		}
	};

	typedef list<DelayedRequest> DelayedRequestList;
	DelayedRequestList delayedRequests;

	typedef list<MemoryRequest *> RequestList;
	typedef queue<MemoryRequest *, RequestList> RequestQueue;
	typedef map<addrint, RequestQueue> WaitQueueMap;

	WaitQueueMap waitQueue;

	int queueSize;
	set<IMemoryCallback *> stalledCallers;

	typedef list<MemoryRequest *> StalledRequestList;

	StalledRequestList stalledDramRequests; //list of dram requests stalled waiting for the next level
	StalledRequestList stalledPcmRequests; //list of pcm requests stalled waiting for the next level

//	RequestQueue stallQueue;
//	uint64 stallStartTimestamp;

	addrint eventTypeMask;

	//Statistics
	Stat<uint64> criticalTagAccessTime;
	Stat<uint64> criticalStallTime;
	Stat<uint64> criticalWaitTime;

	Stat<uint64> numWaitsOnData;
	Stat<uint64> numWaitsOnWriteback;

	//Total time spent stalling
	Stat<uint64> readStallTime;
	Stat<uint64> writeStallTime;

	//Total time spent by requests in the queue (due to a previous request to the same block or to an ongoing writeback to the requested block)
	Stat<uint64> readQueueTime;
	Stat<uint64> writeQueueTime;

	//Total time spent accessing the tag array
	Stat<uint64> readTagAccessTime;
	Stat<uint64> writeTagAccessTime;

	//Total time spent fetching the critical block from PCM/DRAM
	Stat<uint64> dramCriticalReadAccessTime;
	Stat<uint64> pcmCriticalReadAccessTime;

	//Total time spent by an internal request since its arrival until all its accesses are finished (including data movements to and from DRAM/PCM)
	Stat<uint64> readRequestTime;

	//Total time spent in all DRAM and PCM accesses as seen by the cache memory
	Stat<uint64> dramReadAccessTime;
	Stat<uint64> pcmReadAccessTime;

	Stat<uint64> queueStallTime;


public:
	CacheMemory(
		const string& nameArg,
		const string& descArg,
		Engine *engineArg,
		StatContainer *statCont,
		uint64 debugStartArg,
		Memory *dramArg,
		Memory *pcmArg,
		unsigned blockSizeArg,
		unsigned setAssocArg,
		CacheReplacementPolicy policyArg,
		unsigned pageSizeArg,
		uint64 penaltyArg,
		int maxQueueSizeArg);
	virtual ~CacheMemory() {}
	bool access(MemoryRequest *request, IMemoryCallback *caller);
	void accessCompleted(MemoryRequest *request, IMemory *caller);
	void unstall(IMemory *caller);
	void process(const Event *event);

	const char* getName() const {return name.c_str();}

private:
	void accessDram(MemoryRequest *request, addrint internalRequestAddr, bool delay);
	void accessPcm(MemoryRequest *request, addrint internalRequestAddr, bool delay);
	unsigned accessDramBlock(addrint addr, addrint startOffset, addrint internalRequestAddr, bool read, bool instr, bool delay);
	unsigned accessPcmBlock(addrint addr, addrint startOffset, addrint internalRequestAddr, MemoryRequest *originalRequest, bool read, bool instr, bool delay);
	void addEvent(uint64 delay, addrint addr, EventType type) {
		engine->addEvent(delay, this, addr | type);
	}
};




#endif /* MEMORY_H_ */
