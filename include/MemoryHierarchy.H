/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#ifndef MEMORYHIERARCHY_H_
#define MEMORYHIERARCHY_H_

#include "Types.H"

#include <cstring>

class IMemory;

enum Priority {
	HIGH = 0,
	LOW = 1
};

enum CounterIndex {
	TOTAL,
	L1_WAIT,		//access waits at L1 for another request to come with the data
	L2_WAIT,		//access waits at L2 for another request to come with the data
	CPU_PAUSE,		//access pauses at CPU because of address translation
	CPU_STALL,		//access stalls at CPU because IL1 queue is full
	L1_TAG,			//L1 tag access time
	L1_STALL,		//access stalls at L1 because L2 queue is full
	L2_TAG,			//L2 tag access time
	L2_STALL,		//access stalls at L2 because memory queue is full
	DRAM_CACHE_STALL,	//access stalls at DRAM cache because memory queue is full
	DRAM_QUEUE,		//DRAM queue time
	DRAM_CLOSE,		//DRAM row close time
	DRAM_OPEN,		//DRAM row buffer open time
	DRAM_ACCESS,	//DRAM row access time
	DRAM_BUS_QUEUE,	//DRAM bus queue time
	DRAM_BUS,		//DRAM bus transfer time
	PCM_QUEUE,		//PCM queue time
	PCM_CLOSE,		//PCM row close time
	PCM_OPEN,		//PCM row buffer open time
	PCM_ACCESS,		//PCM row access time
	PCM_BUS_QUEUE,	//PCM bus queue time
	PCM_BUS,		//PCM bus transfer time


	COUNTER_INDEX_SIZE
};

struct MemoryRequest {
	addrint addr;
	uint8 size;
	bool read;
	bool instr;
	Priority priority;
	uint64 counters[COUNTER_INDEX_SIZE];
	MemoryRequest() {}
	MemoryRequest(addrint addrArg, uint8 sizeArg, bool readArg, bool instrArg, Priority priorityArg) : addr(addrArg), size(sizeArg), read(readArg), instr(instrArg), priority(priorityArg), counters() {
	//	if(addrArg==0)
//			cout<<"F T S"<<endl<<addrArg;
	}
	void resetCounters() {
		memset(counters, 0, sizeof(counters));
	}
	bool checkCounters() {
		uint64 sum = 0;
		for (int i = L1_WAIT; i < COUNTER_INDEX_SIZE; i++){
			sum += counters[i];
		}
		return counters[TOTAL] == sum;
	}
	void printCounters() {
		for (int i = TOTAL; i < COUNTER_INDEX_SIZE; i++){
			cout << i << ": " << counters[i] << endl;
		}
	}
};

class IMemoryCallback {
public:
	virtual void accessCompleted(MemoryRequest *request, IMemory *caller) = 0;
	virtual void unstall(IMemory *caller) = 0;
	virtual const char* getName() const = 0;
	virtual ~IMemoryCallback() {}
};

class IMemory {
public:
	/*
	 * Returns whether the access was succesfully processed or queued.
	 * If return value is false, the caller's unstall method will be called when this object is ready to receive requests again.
	 */
	virtual bool access(MemoryRequest *request, IMemoryCallback *caller) = 0;
	virtual const char* getName() const = 0;
	virtual ~IMemory() {}
};


class IDrainCallback {
public:
	virtual void drainCompleted(addrint page) = 0;
	virtual const char* getName() const = 0;
	virtual ~IDrainCallback() {}
};

class IFlushCallback {
public:
	virtual void flushCompleted(addrint addr, bool dirty, IMemory *caller) = 0;
	virtual const char* getName() const = 0;
	virtual ~IFlushCallback() {}
};

class IRemapCallback {
public:
	virtual void remapCompleted(addrint pageAddr, IMemory *caller) = 0;
	virtual const char* getName() const = 0;
	virtual ~IRemapCallback() {}
};

class IPinCallback {
public:
	virtual void pinCompleted(addrint, IMemory * caller) = 0;
	virtual const char* getName() const = 0;
	virtual ~IPinCallback() {}
};

class ITagChangeCallback {
public:
	virtual void tagChangeCompleted(addrint addr) = 0;
	virtual const char* getName() const = 0;
	virtual ~ITagChangeCallback() {}
};

class IBusCallback {
public:
	virtual void transferCompleted() = 0;
	virtual const char* getName() const = 0;
	virtual ~IBusCallback() {}
};

#endif /* MEMORYHIERARCHY_H_ */
