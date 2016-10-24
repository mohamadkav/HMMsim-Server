/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#ifndef CPU_H_
#define CPU_H_

#include "Counter.H"
#include "Engine.H"
#include "Error.H"
#include "MemoryHierarchy.H"
#include "MemoryManager.H"
#include "TraceHandler.H"
#include "Types.H"

#include <limits>

class IMemoryManager;
class HybridMemoryManager;


class CPU : public IEventHandler, public IMemoryCallback{
protected:
	Engine *engine;

	string name;
	string desc;

	uint64 debugStart;

	unsigned coreId;
	unsigned pid;

	IMemoryManager *manager;
	IMemory *instrCache;
	IMemory *dataCache;
	TraceReaderBase *reader;

	unsigned blockSize;
	unsigned offsetWidth;
	addrint offsetMask;

	uint64 instrLimit;
	uint64 numInstr;

	uint64 startPauseTimestamp;

	TraceEntry firstEntry;
	TraceEntry secondEntry;
	bool secondEntryValid;

	//Counters
	Counter instrCounter;

	//Statistics

	Stat<uint64> instrExecuted;
	Stat<uint64> totalTime;

	Stat<uint64> instrReads;
	Stat<uint64> dataReads;
	Stat<uint64> dataWrites;

	Stat<uint64> numSleepCycles; //cycles spent sleeping due to holes in the trace
	Stat<uint64> numAccessCycles; // cycles spent waiting for data from the memory hierarchy
	Stat<uint64> numPauseCycles; //cycles spent stalling due to the memory manager

	BinaryStat<double, divides<double>, uint64> ipc;

//	CalcStat<uint64, CPU> instrExecuted;
//	uint64 getInstrExecuted() {return numInstr;}
//
//	CalcStat<uint64, CPU> totalTime;
//	uint64 getTotalTime() {return lastTimestamp;}
//
//	CalcStat<double, CPU> ipc;
//	double getIPC() {return static_cast<double>(numInstr)/static_cast<double>(lastTimestamp);}

public:
	CPU(Engine *engineArg, const string& nameArg, const string& descArg, uint64 debugStartArg, StatContainer *statCont, unsigned coreIdArg, unsigned pidArg, IMemoryManager *managerArg, IMemory *instrCacheArg, IMemory *dataCacheArg, TraceReaderBase *readerArg, unsigned blockSizeArg, uint64 instrLimitArg);
	virtual void start() = 0;
	virtual void resume() = 0;
	virtual void drain(addrint page, IDrainCallback *caller) = 0;
	virtual ~CPU() {}

	const char* getName() const {return name.c_str();}
	Counter* getInstrCounter(){return &instrCounter;}

protected:
	bool readNextEntry();
};

class OOOCPU: public CPU {

	enum EventType {
		PROCESS,
		DRAIN
	};

	unsigned robSize;
	unsigned issueWidth;

	enum State {
		FETCHING,
		DATA,
		DONE
	};

	static const addrint INVALID = numeric_limits<addrint>::max();

	struct RobEntry {
		MemoryRequest instrReqs[2];
		MemoryRequest *dataReqs[6];
		bool instrPause[2]; //if set, the corresponding address is the virtual address, not the physical one
		bool dataPause[6];  //same
		uint8 numInstr;
		uint8 numData;
		uint8 numInstrLeft;
		uint8 numDataLeft;
		State state;
	};

	RobEntry *rob;
	unsigned robHead;
	unsigned robTail;
	bool robFull;

	list<unsigned> instrPausers;
	list<unsigned> dataPausers;

	unsigned *fetchEntry; //each entry of this array points to one entry in the rob that is currently being fetched
	unsigned numFetchEntries; //number of fetched entries in the previous array that are actually valid
	unsigned nextFetchEntry;

//	struct QueueMapEntry{
//		uint16 robEntryIndex;
//		uint16 dataIndex;
//		QueueMapEntry(uint16 robEntryIndexArg, uint16 dataIndexArg) : robEntryIndex(robEntryIndexArg), dataIndex(dataIndexArg) {}
//	};
//	typedef std::tr1::unordered_map<MemoryRequest *, QueueMapEntry> QueueMap;
	typedef unordered_map<MemoryRequest *, unsigned> QueueMap;
	QueueMap lsq;


	bool nextEntryValid;
	unsigned lastEntry;
	uint64 currentTraceTimestamp;

	bool eventScheduled[2];
	bool resumed[2];
	bool instrUnstall[2];
	bool dataUnstall[2];

	vector<MemoryRequest *> instrMsg[2];
	vector<MemoryRequest *> dataMsg[2];

	list<MemoryRequest *> stalledInstrRequests; //list of instruction requests stalled waiting for the next level
	list<MemoryRequest *> stalledDataRequests; //list of instruction requests stalled waiting for the next level

	struct DrainEntry {
		IDrainCallback *callback;
		unsigned requestsLeft;
		DrainEntry(IDrainCallback *callbackArg, unsigned requestsLeftArg) : callback(callbackArg), requestsLeft(requestsLeftArg) {}
	};

	typedef map<addrint, DrainEntry> DrainMap;
	DrainMap drainRequests;

	//Statistics
	Stat<uint64> instrTotalTime;
	Stat<uint64> instrL1WaitTime;
	Stat<uint64> instrL2WaitTime;
	Stat<uint64> instrCpuPauseTime;
	Stat<uint64> instrCpuStallTime;
	Stat<uint64> instrL1TagTime;
	Stat<uint64> instrL1StallTime;
	Stat<uint64> instrL2TagTime;
	Stat<uint64> instrL2StallTime;
	Stat<uint64> instrDramQueueTime;
	Stat<uint64> instrDramCloseTime;
	Stat<uint64> instrDramOpenTime;
	Stat<uint64> instrDramAccessTime;
	Stat<uint64> instrDramBusQueueTime;
	Stat<uint64> instrDramBusTime;
	Stat<uint64> instrPcmQueueTime;
	Stat<uint64> instrPcmCloseTime;
	Stat<uint64> instrPcmOpenTime;
	Stat<uint64> instrPcmAccessTime;
	Stat<uint64> instrPcmBusQueueTime;
	Stat<uint64> instrPcmBusTime;

	Stat<uint64> instrL1Count;
	Stat<uint64> instrL2Count;
	Stat<uint64> instrDramCloseCount;
	Stat<uint64> instrDramOpenCount;
	Stat<uint64> instrDramAccessCount;
	Stat<uint64> instrPcmCloseCount;
	Stat<uint64> instrPcmOpenCount;
	Stat<uint64> instrPcmAccessCount;

	Stat<uint64> dataTotalTime;
	Stat<uint64> dataL1WaitTime;
	Stat<uint64> dataL2WaitTime;
	Stat<uint64> dataCpuPauseTime;
	Stat<uint64> dataCpuStallTime;
	Stat<uint64> dataL1TagTime;
	Stat<uint64> dataL1StallTime;
	Stat<uint64> dataL2TagTime;
	Stat<uint64> dataL2StallTime;
	Stat<uint64> dataDramQueueTime;
	Stat<uint64> dataDramCloseTime;
	Stat<uint64> dataDramOpenTime;
	Stat<uint64> dataDramAccessTime;
	Stat<uint64> dataDramBusQueueTime;
	Stat<uint64> dataDramBusTime;
	Stat<uint64> dataPcmQueueTime;
	Stat<uint64> dataPcmCloseTime;
	Stat<uint64> dataPcmOpenTime;
	Stat<uint64> dataPcmAccessTime;
	Stat<uint64> dataPcmBusQueueTime;
	Stat<uint64> dataPcmBusTime;

	Stat<uint64> dataL1Count;
	Stat<uint64> dataL2Count;
	Stat<uint64> dataDramCloseCount;
	Stat<uint64> dataDramOpenCount;
	Stat<uint64> dataDramAccessCount;
	Stat<uint64> dataPcmCloseCount;
	Stat<uint64> dataPcmOpenCount;
	Stat<uint64> dataPcmAccessCount;



public:
	OOOCPU(Engine *engineArg,
			const string& nameArg,
			const string& descArg,
			uint64 debugStartArg,
			StatContainer *statCont,
			unsigned coreIdArg,
			unsigned pidArg,
			IMemoryManager *managerArg,
			IMemory *instrCacheArg,
			IMemory *dataCacheArg,
			TraceReaderBase *readerArg,
			unsigned blockSizeArg,
			uint64 instrLimitArg,
			unsigned robSizeArg,
			unsigned frontendLengthArg);
	void start();
	void process(const Event * event);
	void accessCompleted(MemoryRequest *request, IMemory *caller);
	void unstall(IMemory *caller);
	void resume();
	void drain(addrint page, IDrainCallback *caller);


private:
	void countInstr(MemoryRequest *request);
	void countData(MemoryRequest *request);
	void resumePrivate();
	void unstallInstr();
	void unstallData();
	void processInstr(uint64 index);
	void processData(uint64 index);
	void fetch();
	void commit();
	void scheduleEvent();

	void addEvent(uint64 delay, EventType type);


};

class IOCPU : public CPU {

	uint64 timestamp; // not the engine timestamp

	//addrint physicalAddr;
	MemoryRequest request;

	uint64 startAccessTime;

	bool waiting;

	//Statistics
	Stat<uint64> exec;

public:
	IOCPU(Engine *engineArg, const string& nameArg, const string& descArg, uint64 debugStartArg, StatContainer *statCont, unsigned coreIdArg, unsigned pidArg, IMemoryManager *managerArg, IMemory *instrCacheArg, IMemory *dataCacheArg, TraceReaderBase *readerArg, unsigned blockSizeArg, uint64 instrLimitArg);
	void start();
	void process(const Event * event);
	void accessCompleted(addrint addr, IMemory *caller);
	void resume();

private:
	void scheduleNextAccess();
};




#endif /* CPU_H_ */
