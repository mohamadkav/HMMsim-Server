/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#ifndef BANK_H_
#define BANK_H_

#include "Bus.H"
#include "Engine.H"
#include "Error.H"
#include "MemoryHierarchy.H"
#include "Statistics.H"
#include "Types.H"

#include <bitset>


enum MappingType {
	ROW_RANK_BANK_COL, 	//Usually used with open page
	ROW_COL_RANK_BANK,  //Usually used with closed page
	RANK_BANK_ROW_COL	//With open page?
};

class MemoryMapping {
	unsigned numRanks;
	unsigned banksPerRank;
	unsigned rowsPerBank;
	unsigned blocksPerRow;
	unsigned blockSize;

	MappingType mappingType;

	unsigned numBanks;
	uint64 totalSize;

	unsigned rankWidth;
	unsigned bankWidth;
	unsigned rowWidth;
	unsigned columnWidth;
	unsigned blockWidth;

	unsigned rankOffset;
	unsigned bankOffset;
	unsigned rowOffset;
	unsigned columnOffset;
	unsigned blockOffset;

	addrint rankMask;
	addrint bankMask;
	addrint rowMask;
	addrint columnMask;
	addrint blockMask;

public:
	MemoryMapping(MappingType mappingTypeArg, unsigned numRanksArg, unsigned banksPerRankArg, unsigned rowsPerBankArg, unsigned blocksPerRowArg, unsigned blockSizeArg);

	unsigned getNumBanks() {return numBanks;}
	unsigned getBlocksPerRow() {return blocksPerRow;}
	uint64 getTotalSize() {return totalSize;}

	addrint getRankIndex(addrint addr) {return (addr & rankMask) >> rankOffset;}
	addrint getBankIndex(addrint addr) {return (addr & bankMask) >> bankOffset;}
	addrint getRowIndex(addrint addr) {return (addr & rowMask) >> rowOffset;}
	addrint getColumnIndex(addrint addr) {return (addr & columnMask) >> columnOffset;}
	addrint getBlockIndex(addrint addr) {return (addr & blockMask) >> blockOffset;}

	unsigned getBankId(addrint addr) {return (getRankIndex(addr) << bankWidth) | getBankIndex(addr);}

	unsigned getBlockSize() {return blockSize;}
	addrint getBlockAddress(addrint addr) {return addr & ~blockMask;}
};

//struct Request{
//	MemoryRequest *request;
//	IMemoryCallback *caller;
//	Request() : request(0), caller(0) {}
//		Request(const Request& req) : request(req.request), caller(req.caller) {}
//		Request(MemoryRequest *requestArg, IMemoryCallback *callerArg) : request(requestArg), caller(callerArg) {}
////	uint64 timestamp;
////	Request() : request(0), caller(0), timestamp(0) {}
////	Request(const Request& req) : request(req.request), caller(req.caller), timestamp(req.timestamp) {}
////	Request(MemoryRequest *requestArg, IMemoryCallback *callerArg, uint64 timestampArg) :
////		request(requestArg), caller(callerArg), timestamp(timestampArg) {
////	}
//};

enum RowBufferPolicy {
	OPEN_PAGE,
	CLOSED_PAGE
};

enum MemoryType {
	DESTRUCTIVE_READS,
	NON_DESTRUCTIVE_READS
};


class Memory;

class Bank : public IEventHandler, public IMemory, public IBusCallback {
	enum EventType {
		QUEUE,
		BANK,
		PIPELINE
	};

	string name;
	string desc;
	Engine *engine;

	uint64 debugStart;
	CounterIndex queueCounterIndex;
	CounterIndex openCounterIndex;
	CounterIndex accessCounterIndex;
	CounterIndex closeCounterIndex;
	CounterIndex busQueueCounterIndex;
	CounterIndex busCounterIndex;

	RowBufferPolicy policy;
	bool firstReadyAcrossPriorities;
	MemoryType type;
	Memory *memory;
	Bus *bus;
	MemoryMapping *mapping;

	uint64 openLatency; //time to open a row
	uint64 closeLatency; //time to close a row (when reads are destructive or row is dirty)
	uint64 accessLatency; //time to access a row once it is open

	bool longCloseLatency; //whether the close operation depends on the number of dirty columns in the row

	enum State {
		CLOSED,
		OPENING,
		OPEN_CLEAN,
		OPEN_DIRTY,
		CLOSING
	};

	State state;
	unsigned row;

	struct RequestAndTime {
		MemoryRequest *request;

		uint64 enqueueTimestamp;
		uint64 dequeueTimestamp;
		uint64 startWaitingTimestamp;
		bool waitingOnLowerPriority;
		bool waitingOnSamePriority;
		bool waitingOnHigherPriority;
		RequestAndTime () : request(0), enqueueTimestamp(0), dequeueTimestamp(0), startWaitingTimestamp(0), waitingOnLowerPriority(false), waitingOnSamePriority(false), waitingOnHigherPriority(false) {}
		RequestAndTime (MemoryRequest *requestArg, uint64 enqueueTimestampArg) : request(requestArg), enqueueTimestamp(enqueueTimestampArg), dequeueTimestamp(0), startWaitingTimestamp(enqueueTimestampArg), waitingOnLowerPriority(false), waitingOnSamePriority(false), waitingOnHigherPriority(false) {}
	};

	RequestAndTime currentRequest;
	bool currentRequestValid;

	typedef list<RequestAndTime> RequestList;

	uint64 nextPipelineEvent;
	RequestList pipelineRequests;

	typedef map<uint8, RequestList> Queue;
	Queue queue;

	bitset<64> dirtyColumns;

	list<MemoryRequest *> notifications;

	//Statistics
	Stat<uint64> queueTime; //Number of cycles critical requests for this bank spend in the queue
	Stat<uint64> openTime; //Number of cycles this bank spends opening rows for critical requests
	Stat<uint64> accessTime; //Number of cycles this bank spends accesing rows for critical requests
	Stat<uint64> closeTime; //Number of cycles this bank spends closing rows for critical requests

	Stat<uint64> numReadRequests;
	Stat<uint64> numWriteRequests;
	Stat<uint64> readQueueTime;
	Stat<uint64> writeQueueTime;
	Stat<uint64> readTotalTime;
	Stat<uint64> writeTotalTime;

	Stat<uint64> rowBufferHits;
	Stat<uint64> rowBufferMisses;

	Stat<uint64> numOpens;
	Stat<uint64> numAccesses;
	Stat<uint64> numCloses;

	Stat<uint64> numRARs;
	Stat<uint64> numRAWs;
	Stat<uint64> numWARs;
	Stat<uint64> numWAWs;

	Stat<uint64> waitLowerPriorityTime;
	Stat<uint64> waitSamePriorityTime;
	Stat<uint64> waitHigherPriorityTime;


public:
	Bank(
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
		Memory *memoryArg,
		Bus *busArg,
		uint64 openLatencyArg,
		uint64 closeLatencyArg,
		uint64 accessLatencyArg,
		bool longCloseLatencyArg);
	~Bank() {}
	void process(const Event *event);
	bool access(MemoryRequest *request, IMemoryCallback *caller);
	void transferCompleted();

	Stat<uint64>* getStatNumReadRequests() {return &numReadRequests;}
	Stat<uint64>* getStatNumWriteRequests() {return &numWriteRequests;}
	Stat<uint64>* getStatReadQueueTime() {return &readQueueTime;}
	Stat<uint64>* getStatWriteQueueTime() {return &writeQueueTime;}
	Stat<uint64>* getStatReadTotalTime() {return &readTotalTime;}
	Stat<uint64>* getStatWriteTotalTime() {return &writeTotalTime;}

	Stat<uint64>* getStatRowBufferHits() {return &rowBufferHits;}
	Stat<uint64>* getStatRowBufferMisses() {return &rowBufferMisses;}

	Stat<uint64>* getStatNumOpens() {return &numOpens;}
	Stat<uint64>* getStatNumAccesses() {return &numAccesses;}
	Stat<uint64>* getStatNumCloses() {return &numCloses;}

	Stat<uint64>* getStatNumRARs() {return &numRARs;}
	Stat<uint64>* getStatNumRAWs() {return &numRAWs;}
	Stat<uint64>* getStatNumWARs() {return &numWARs;}
	Stat<uint64>* getStatNumWAWs() {return &numWAWs;}

	Stat<uint64>* getStatWaitLowerPriorityTime() {return &waitLowerPriorityTime;}
	Stat<uint64>* getStatWaitSamePriorityTime() {return &waitSamePriorityTime;}
	Stat<uint64>* getStatWaitHigherPriorityTime() {return &waitHigherPriorityTime;}

	uint64 getReadNumRequests() {return numReadRequests;}
	uint64 getWriteNumRequests() {return numWriteRequests;}
	uint64 getReadQueueTime() {return readQueueTime;}
	uint64 getWriteQueueTime() {return writeQueueTime;}
	uint64 getReadTotalTime() {return readTotalTime;}
	uint64 getWriteTotalTime() {return writeTotalTime;}
	uint64 getRowBufferHits() {return rowBufferHits;}
	uint64 getRowBufferMisses() {return rowBufferMisses;}
	uint64 getNumOpens() { return numOpens;}
	uint64 getNumAccesses() { return numAccesses;}
	uint64 getNumCloses() { return numCloses;}
	uint64 getNumRARs() {return numRARs;}
	uint64 getNumRAWs() {return numRAWs;}
	uint64 getNumWARs() {return numWARs;}
	uint64 getNumWAWs() {return numWAWs;}
	uint64 getWaitSamePriorityTime() {return waitSamePriorityTime;}
	uint64 getWaitHigherPriorityTime() {return waitHigherPriorityTime;}
	uint64 getWaitLowerPriorityTime() {return waitLowerPriorityTime;}

	const char* getName() const {return name.c_str();}

private:
	void changeState();
	void selectNextRequest();
	void notify(MemoryRequest * request);
	void addEvent(uint64 delay, EventType type){
		engine->addEvent(delay, this, static_cast<uint64>(type));
	}
};

istream& operator>>(istream& lhs, RowBufferPolicy& rhs);
ostream& operator<<(ostream& lhs, RowBufferPolicy rhs);

istream& operator>>(istream& lhs, MappingType& rhs);
ostream& operator<<(ostream& lhs, MappingType rhs);

#endif /* BANK_H_ */
