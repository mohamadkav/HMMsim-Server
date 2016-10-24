/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#ifndef CACHE_HPP_
#define CACHE_HPP_

#include "Engine.H"
#include "Error.H"
#include "MemoryHierarchy.H"
#include "Statistics.H"
#include "Types.H"

#include <set>

#include <debug/map>
#include <debug/list>

#define BITSET_SIZE 64

enum CacheReplacementPolicy{
	CACHE_LRU,
	CACHE_FIFO
};

class Block {
public:
	addrint tag;
	uint64 timestamp;
	bool clean;
	bool valid;

public:
	Block();
};


class Set {
public:

	enum Result{
		INVALID,
		NO_EVICTION,
		EVICTION,
		WRITEBACK,
	};

private:
	unsigned numBlocks;
	Block *blocks;
	set<addrint> pinnedBlocks;

public:
	Set();
	~Set();
	void setNumBlocks(unsigned numBlocks);
	int access(addrint tag, uint64 timestamp, bool read);
	Result allocate(addrint tag, uint64 timestamp, bool read, CacheReplacementPolicy policy, addrint *tagEvicted, int *block);
	void pin(addrint tag);
	void unpin(addrint tag);
	Result flush(addrint tag);
	bool changeTag(addrint oldTag, addrint newTag);
	void makeDirty(addrint tag);


};


class CacheModel {
public:

	enum Result {
		HIT,
		MISS_WITHOUT_EVICTION,
		MISS_WITH_EVICTION,
		MISS_WITH_WRITEBACK,
		MISS_WITHOUT_FREE_BLOCK,
		INVALID
	};


private:
	uint64 cacheSize;
	unsigned blockSize;
	unsigned setAssoc;
	CacheReplacementPolicy policy;
	unsigned pageSize;


	uint64 numSets;
	Set *sets;

	unsigned offsetWidth;
	unsigned indexWidth;
	unsigned tagWidth;
	addrint offsetMask;
	addrint indexMask;
	addrint tagMask;

	unsigned pageOffsetWidth;
	unsigned pageIndexWidth;
	addrint pageOffsetMask;
	addrint pageIndexMask;

	addrint msbMask = 0x8000000000000000;

	uint64 timestamp;

	struct RemapTableEntry{
		addrint addr;
		unsigned count;
		RemapTableEntry(addrint addrArg, unsigned countArg) : addr(addrArg), count(countArg) {}
	};

	struct InvRemapTableEntry{
		addrint addr;
		unsigned *countPtr;
		InvRemapTableEntry(addrint addrArg, unsigned *countPtrArg) : addr(addrArg), countPtr(countPtrArg) {}
	};

	typedef map<addrint, RemapTableEntry> RemapTable;
	typedef map<addrint, InvRemapTableEntry> InvRemapTable;

	RemapTable remapTable;
	InvRemapTable invRemapTable;

	Stat<uint64> hits;
	Stat<uint64> misses_without_eviction;
	Stat<uint64> misses_with_eviction;
	Stat<uint64> misses_with_writeback;
	Stat<uint64> misses_without_free_block;

	Stat<uint64> dataLoadHits;
	Stat<uint64> dataLoadMisses;

	Stat<uint64> dataStoreHits;
	Stat<uint64> dataStoreMisses;

	Stat<uint64> instrLoadHits;
	Stat<uint64> instrLoadMisses;

	Stat<uint64> flushesWithoutEviction;
	Stat<uint64> flushesWithEviction;
	Stat<uint64> flushesWithWriteback;

	Stat<uint64> tagChangeHits;
	Stat<uint64> tagChangeMisses;

	AggregateStat<uint64> misses;
	AggregateStat<uint64> accesses;
	BinaryStat<double, divides<double>, uint64> hitRate;
	BinaryStat<double, divides<double>, uint64> missRate;

//	CalcStat<double, CacheModel> hitRate;
//	double getHitRate() {return static_cast<double>(hits)/static_cast<double>(getAccesses());}
//	CalcStat<double, CacheModel> missRate;
//	double getMissRate() {return static_cast<double>(getMisses())/static_cast<double>(getAccesses());}

public:
	CacheModel(const string& name, const string& descArg, StatContainer *statCont, uint64 cacheSize, unsigned blockSize, unsigned setAssoc, CacheReplacementPolicy policy, unsigned pageSizeArg);
	~CacheModel();
	inline uint64 getCacheSize() const {return cacheSize;}
	inline unsigned getBlockSize() const {return blockSize;}
	inline unsigned int getAssociativity() const {return setAssoc;}
	Result access(addrint addr, bool read, bool instr, addrint *evictedAddr, addrint *internalAddr);
	void pin(addrint addr);
	void unpin(addrint addr);
	Set::Result flush(addrint addr);
	bool changeTag(addrint oldAddr, addrint newAddr);
	void makeDirty(addrint addr);
	typedef list<addrint> AddrList;
	bool remap(addrint oldPage, addrint newPage, AddrList *present, AddrList *evicted);

	addrint getBlockAddress(addrint addr) const {return addr & ~offsetMask;}
	addrint getBlockOffset(addrint addr) const {return addr & offsetMask;}

	bool isSameSet(addrint addr1, addrint addr2) const {return getIndex(addr1) == getIndex(addr2);}

	uint64 getInstrLoadAccesses() const {return instrLoadHits + instrLoadMisses;}
	uint64 getMisses() const {return misses_without_eviction + misses_with_eviction + misses_with_writeback + misses_without_free_block;}
	uint64 getMissesWithWriteBack() const {return misses_with_writeback;}
	uint64 getDataLoadHits() const {return dataLoadHits;}
	uint64 getDataStoreHits() const {return dataStoreHits;}
	uint64 getAccesses() const {return hits+getMisses();}

private:
	addrint getIndex(addrint addr) const {return (addr & indexMask) >> offsetWidth;}
	addrint getTag(addrint addr) const {return (addr & tagMask) >> (indexWidth + offsetWidth);}


	addrint getActualAddress(addrint addr) const;
	addrint getInvActualAddress(addrint addr) const;
public:

	addrint getPageIndex(addrint addr) const {return addr >> pageOffsetWidth;}
	addrint getPageOffset(addrint addr) const {return addr & ~pageIndexMask;}
	addrint getPageAddress(addrint index, addrint offset) const {return (index << pageOffsetWidth) | (offset & ~pageIndexMask);}

};






class Cache : public IEventHandler, public IMemory, public IMemoryCallback, public IFlushCallback, public IRemapCallback {

	struct Caller {
		bool read;
		MemoryRequest *request;
		IMemoryCallback *callback;
		Caller(bool readArg, MemoryRequest *requestArg, IMemoryCallback *callbackArg) : read(readArg), request(requestArg), callback(callbackArg) {}
	};

	typedef list<Caller> CallerList;

	struct Pinner {
		addrint addr;
		IPinCallback *callback;
		Pinner(addrint addrArg, IPinCallback *callbackArg) : addr(addrArg), callback(callbackArg) {}
	};

	typedef list<Pinner> PinnerList;

	typedef list<addrint> RepeatFlushList;

	struct Request{
		MemoryRequest *request;
		CacheModel::Result result;
		addrint evictedAddr;
		uint64 numCallers;
		CallerList callers;
		PinnerList pinners;
		uint64 timestamp;
		bool waitingForTag;
		bool waitingForRead;
		bool waitingForFlush;
		bool repeatFlush;
		Request(MemoryRequest *requestArg) : request(requestArg), result(CacheModel::INVALID), evictedAddr(0), numCallers(0), callers(), pinners(), timestamp(0), waitingForTag(true), waitingForRead(false), waitingForFlush(false), repeatFlush(false) {}
	};

	typedef map<addrint, Request> RequestMap;
//	typedef unordered_map<addrint, uint64> RequestMap;

	enum AccessType {
		ACCESS,
		FLUSH,
		REMAP,
		TAG_CHANGE,
		UNSTALL,
		ACCESS_TYPE_SIZE
	};

	typedef pair<addrint, AccessType> AddrEventType;

	typedef vector<Cache*> CacheList;

	string name;
	string desc;
	Engine *engine;
	uint64 debugStart;
	CounterIndex waitCounterIndex;
	CounterIndex tagCounterIndex;
	CounterIndex stallCounterIndex;


	IMemory *nextLevel;
	CacheList prevLevels;
	CacheModel cacheModel;
	uint64 penalty;
	uint64 maxQueueSize;
	bool realRemap;

	RequestMap requests;

	struct FlushRequest{
		Set::Result result;
		IFlushCallback *caller;
		bool guarantee;
		bool repeat;
		bool dirty;
		bool done;
		unsigned stalledRequestsLeft;
		FlushRequest(bool guaranteeArg) : guarantee(guaranteeArg), repeat(false), dirty(false), done(false), stalledRequestsLeft(0) {}
	};

	struct RemapRequest{
		bool result;
		addrint newPage;
		CacheModel::AddrList present;
		CacheModel::AddrList evicted;
		unsigned notificationsLeft;
		IRemapCallback *caller;
	};

	struct TagChangeRequest{
		bool result;
		addrint newAddr;
		ITagChangeCallback *caller;
	};

	struct OutgoingFlushRequestListEntry {
		AccessType type;
		addrint origAddr;
		OutgoingFlushRequestListEntry(AccessType typeArg, addrint origAddrArg) : type(typeArg), origAddr(origAddrArg) {}
	};

	struct OutgoingFlushRequest {
		list<OutgoingFlushRequestListEntry> requests;
		unsigned notificationsLeft;
		bool dirty;
		bool guarantee;
		OutgoingFlushRequest(AccessType typeArg, addrint origAddrArg, unsigned notificationsLeftArg, bool guaranteeArg, bool dirtyArg) : notificationsLeft(notificationsLeftArg), dirty(dirtyArg), guarantee(guaranteeArg) {
			requests.emplace_back(typeArg, origAddrArg);
		}
	};

//	struct OutgoingRemapRequest {
//		unsigned notificationsLeft;
//
//	};


	typedef map<addrint, FlushRequest> FlushRequestMap;
	typedef map<addrint, RemapRequest> RemapRequestMap;
	typedef map<addrint, TagChangeRequest> TagChangeRequestMap;
	typedef map<addrint, OutgoingFlushRequest> OutgoingFlushRequestMap;
	typedef map<addrint, unsigned> OutgoingRemapRequestMap;

//	typedef unordered_map<addrint, FlushRequest> FlushRequestMap;
//	typedef unordered_map<addrint, RemapRequest> RemapRequestMap;
//	typedef unordered_map<addrint, TagChangeRequest> TagChangeRequestMap;
//	typedef unordered_map<addrint, OutgoingFlushRequest> OutgoingFlushRequestMap;
//	typedef unordered_map<addrint, unsigned> OutgoingRemapRequestMap;

	FlushRequestMap flushRequests;
	RemapRequestMap remapRequests;
	TagChangeRequestMap tagChangeRequests;
	OutgoingFlushRequestMap outgoingFlushRequests;
	OutgoingFlushRequestMap outgoingRemapRequests;

	uint64 queueSize;
	set<IMemoryCallback *> stalledCallers;
	unsigned nextStalledCaller;

	struct StalledRequest{
		MemoryRequest *request;
		bool flushing;
		PinnerList pinners;
		StalledRequest(MemoryRequest *requestArg) : request(requestArg), flushing(false), pinners() {}
		StalledRequest(MemoryRequest *requestArg, const PinnerList& pinnersArg) : request(requestArg), flushing(false), pinners(pinnersArg) {}
	};

	typedef deque<StalledRequest> StalledRequestList;

	StalledRequestList stalledRequests; //list of requests stalled waiting for the next level

	addrint accessTypeMask;

	//Statistics
	Stat<uint64> readAccessTime;

	Stat<uint64> missesFromFlush;		// number of times a miss was handled from buffers waiting for flushes to previous level caches
	Stat<uint64> writebacksFromFlush; // number of times an eviction became a writeback after flushing the previous level caches


public:
	Cache(
		const string& nameArg,
		const string& descArg,
		Engine *engineArg,
		StatContainer *statCont,
		uint64 debugStartArg,
		CounterIndex waitCounterIndexArg,
		CounterIndex tagCounterIndexArg,
		CounterIndex stallCounterIndexArg,
		IMemory *nextLevelArg,
		uint64 cacheSizeArg,
		unsigned blockSizeArg,
		unsigned setAssocArg,
		CacheReplacementPolicy policyArg,
		unsigned pageSizeArg,
		uint64 penaltyArg,
		uint64 maxQueueSizeArg,
		bool realRemapArg);
	~Cache() {}
	bool access(MemoryRequest *request, IMemoryCallback *caller);
	void accessCompleted(MemoryRequest *request, IMemory *caller);
	void unstall(IMemory *caller);
	void process(const Event *event);
	void flush(addrint addr, uint8 size, bool guarantee, IFlushCallback *caller);
	void flushCompleted(addrint addr, bool dirty, IMemory *caller);
	void remap(addrint oldPage, addrint newPage, IRemapCallback *caller);
	void remapCompleted(addrint page, IMemory *caller);
	void changeTag(addrint oldAddr, addrint newAddr, uint8 size, ITagChangeCallback *caller);

	unsigned pin(addrint addr, IPinCallback *caller);

	void unpin(addrint addr){cacheModel.unpin(addr);}
	void addPrevLevel(Cache *cache){prevLevels.emplace_back(cache);}
	bool isSameSet(addrint addr1, addrint addr2) {return cacheModel.isSameSet(addr1, addr2);}

	const char* getName() const {return name.c_str();}

private:
	void addEvent(uint64 delay, addrint addr, AccessType type) {
		myassert(0 <= type && type < ACCESS_TYPE_SIZE);
		myassert((addr & accessTypeMask) == 0);
		engine->addEvent(delay, this, addr | type);
	}
};


#endif /* CACHE_HPP_ */

