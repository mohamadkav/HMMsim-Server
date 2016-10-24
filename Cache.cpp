/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#include "Cache.H"

#include <iomanip>

#include <cassert>
#include <cmath>


using namespace std;


Block::Block() : valid(false){}

Set::Set() : numBlocks(0), blocks(0){}

Set::~Set(){
	delete [] blocks;
}

void Set::setNumBlocks(unsigned int numBlocks){
	if (numBlocks <= 0){
		error("Number of blocks in a set (%d) must be greater than 0", numBlocks);
	}
	if (Set::numBlocks != numBlocks){
		if (blocks != 0){
			delete [] blocks;
		}
		Set::numBlocks = numBlocks;
		blocks = new Block[numBlocks];
	} else {
		for (unsigned int i = 0; i < numBlocks; i++){
			blocks[i].valid = false;
		}
	}
}

int Set::access(addrint tag, uint64 timestamp, bool read){
	unsigned int i;
	for (i = 0; i < numBlocks; i++){
		if (blocks[i].valid && blocks[i].tag == tag){
			blocks[i].timestamp = timestamp;
			if (blocks[i].clean && read){
				blocks[i].clean = true;
			} else {
				blocks[i].clean = false;
			}
			return i;
		}
	}
	return -1;
}

Set::Result Set::allocate(addrint tag, uint64 timestamp, bool read, CacheReplacementPolicy policy, addrint *tagEvicted, int *block){
	unsigned int i;
	int victim;
	Result ret;
	uint64 min;
	victim = -1;
	for (i = 0; i < numBlocks; i++){
		if (!blocks[i].valid){
			victim = i;
			break;
		}
	}
	if (victim == -1){
		if (policy == CACHE_LRU){
			min = numeric_limits<uint64>::max();
			for (i = 0; i < numBlocks; i++){
				if (blocks[i].timestamp < min && pinnedBlocks.count(blocks[i].tag) == 0){
					victim = i;
					min = blocks[i].timestamp;
				}
			}
		} else {
			error("Unsupported cache policy: %d", policy);
		}
	}
	if (victim == -1){
		ret = INVALID;
	} else {
		if (blocks[victim].valid){
			*tagEvicted = blocks[victim].tag;
			if (blocks[victim].clean){
				ret = EVICTION;
			} else {
				ret = WRITEBACK;
			}
		} else {
			ret = NO_EVICTION;
		}
		blocks[victim].tag = tag;
		blocks[victim].timestamp = timestamp;
		blocks[victim].valid = true;
		blocks[victim].clean = read;
	}
	*block = victim;
	return ret;
}

void Set::pin(addrint tag){
	if (!pinnedBlocks.insert(tag).second){
		warn("Block was previously pinned");
	}
}

void Set::unpin(addrint tag){
	if (pinnedBlocks.erase(tag) != 1){
		warn("Block was not pinned");
	}
}

Set::Result Set::flush(addrint tag){
//	for (unsigned int i = 0; i < numBlocks; i++){
//		if (blocks[i].valid && blocks[i].tag == tag){
//			int pos = 0; //number of valid blocks that have a timestamp greater than the flushed block (that is, blocks that are fresher than this one)
//			for (unsigned int j = 0; j < numBlocks; j++){
//				if (i != j && blocks[j].valid && blocks[i].timestamp < blocks[j].timestamp){
//					pos++;
//				}
//			}
//			if (numBlocks == 16){
//				cout << pos << endl;
//			}
//			blocks[i].valid = false;
//			if (blocks[i].clean){
//				return EVICTION;
//			} else {
//				return WRITEBACK;
//			}
//		}
//	}
//	return NO_EVICTION;
	for (unsigned int i = 0; i < numBlocks; i++){
		if (blocks[i].valid && blocks[i].tag == tag){
			blocks[i].valid = false;
			if (blocks[i].clean){
				return EVICTION;
			} else {
				return WRITEBACK;
			}
		}
	}
	return NO_EVICTION;
}

bool Set::changeTag(addrint oldTag, addrint newTag){
	for (unsigned int i = 0; i < numBlocks; i++){
		if (blocks[i].valid && blocks[i].tag == oldTag){
			blocks[i].tag = newTag;
			return true;
		}
	}
	return false;
}

void Set::makeDirty(addrint tag){
	for (unsigned int i = 0; i < numBlocks; i++){
		if (blocks[i].valid && blocks[i].tag == tag){
			blocks[i].clean = false;
			return;
		}
	}
	warn("Trying to make dirty a block that was not present");
}



CacheModel::CacheModel(const string& nameArg, const string& descArg, StatContainer *statCont, uint64 cacheSizeArg, unsigned blockSizeArg, unsigned setAssocArg, CacheReplacementPolicy policyArg, unsigned pageSizeArg) :
	hits (statCont, nameArg + "_all_hits", "Number of " + descArg + " hits", 0),
	misses_without_eviction (statCont, nameArg + "_misses_without_eviction", "Number of " + descArg + " misses without eviction", 0),
	misses_with_eviction (statCont, nameArg + "_misses_with_eviction", "Number of " + descArg + " misses with eviction", 0),
	misses_with_writeback (statCont, nameArg + "_misses_with_writeback", "Number of " + descArg + " misses with writeback", 0),
	misses_without_free_block (statCont, nameArg + "_misses_without_free_block", "Number of " + descArg + " misses without free block", 0),

	dataLoadHits (statCont, nameArg + "_data_load_hits", "Number of " + descArg + " data load hits", 0),
	dataLoadMisses (statCont, nameArg + "_data_load_misses", "Number of " + descArg + " data load misses", 0),

	dataStoreHits (statCont, nameArg + "_data_store_hits", "Number of " + descArg + " data store hits", 0),
	dataStoreMisses (statCont, nameArg + "_data_store_misses", "Number of " + descArg + " data store misses", 0),

	instrLoadHits (statCont, nameArg + "_instr_load_hits", "Number of " + descArg + " instruction load hits", 0),
	instrLoadMisses (statCont, nameArg + "_instr_load_misses", "Number of " + descArg + " instruction load misses", 0),

	flushesWithoutEviction (statCont, nameArg + "_flushes_without_eviction", "Number of " + descArg + " flushes without eviction", 0),
	flushesWithEviction (statCont, nameArg + "_flushes_with_eviction", "Number of " + descArg + " flushes with eviction", 0),
	flushesWithWriteback (statCont, nameArg + "_flushes_with_writeback", "Number of " + descArg + " flushes with writeback", 0),

	tagChangeHits (statCont, nameArg + "_tag_change_hits", "Number of " + descArg + " tag change hits", 0),
	tagChangeMisses (statCont, nameArg + "_tag_change_misses", "Number of " + descArg + " tag change misses", 0),

	misses(statCont, nameArg + "_all_misses", "Number of " + descArg + " misses", 0, &misses_without_eviction, &misses_with_eviction, &misses_with_writeback, &misses_without_free_block),
	accesses(statCont, nameArg + "_accesses", "Number of " + descArg + " accesses", 0, &hits, &misses),
	hitRate(statCont, nameArg + "_hit_rate", descArg + " hit rate", &hits, &accesses),
	missRate(statCont, nameArg + "_miss_rate", descArg + " miss rate", &misses, &accesses)
{
	uint64 i, numBlocks;
	cacheSize = cacheSizeArg;
	if (blockSizeArg < sizeof(addrint)){
		error("The block size (%d bytes) cannot be smaller than the word size (%zd bytes)", blockSizeArg, sizeof(addrint));
	}
	unsigned logBlockSize = (unsigned)logb(blockSizeArg);
	blockSize = 1 << logBlockSize;
	setAssoc = setAssocArg;
	policy = policyArg;
	numBlocks = cacheSize / blockSize;
	numSets = numBlocks / setAssoc;
	if (numSets == 0){
		error("Number of blocks (cache size divided by block size; %zu/%u = %zu) must be greater or equal to associativity (%u)", cacheSize, blockSize, numBlocks, setAssoc);
	}
	offsetWidth = (int)logb(blockSize);
	indexWidth = (int)logb(numSets);
	tagWidth = sizeof(addrint)*8 - offsetWidth - indexWidth;
	sets = new Set[numSets];
	for (i = 0; i < numSets; i++){
		sets[i].setNumBlocks(setAssoc);
	}
	offsetMask = 0;
	for (i = 0; i < offsetWidth; i++){
		offsetMask |= (addrint)1U << i;
	}
	indexMask = 0;
	for (i = offsetWidth; i < indexWidth+offsetWidth; i++){
		indexMask |= (addrint)1U << i;
	}
	tagMask = 0;
	for (i = indexWidth+offsetWidth; i < tagWidth+indexWidth+offsetWidth; i++){
		tagMask |= (addrint)1U << i;
	}


	unsigned logPageSize = (unsigned)logb(pageSizeArg);
	pageSize = 1 << logPageSize;

	pageOffsetWidth = logPageSize;
	pageIndexWidth = sizeof(addrint)*8 - pageOffsetWidth;

	pageOffsetMask = 0;
	for (i = 0; i < pageOffsetWidth; i++){
		pageOffsetMask |= (addrint)1U << i;
	}
	pageIndexMask = 0;
	for (i = pageOffsetWidth; i < pageIndexWidth+pageOffsetWidth; i++){
		pageIndexMask |= (addrint)1U << i;
	}

	timestamp = 0;
}

CacheModel::~CacheModel(){
	delete [] sets;
}

CacheModel::Result CacheModel::access(addrint addr, bool read, bool instr, addrint *evictedAddr, addrint *internalAddr){
	//debug2("CacheModel.access(%lu)", addr);
	assert((addr & msbMask) == 0);
	addrint actualAddr;
	auto it = remapTable.find(getPageIndex(addr));
	if (it == remapTable.end()){
		actualAddr = addr;
	} else {
		addrint newAddr = getPageAddress(it->second.addr, getPageOffset(addr));
		assert((newAddr & msbMask) != 0);
		actualAddr = newAddr;
	}

	addrint index = getIndex(actualAddr);
	addrint tag = getTag(actualAddr);
	Result ret;
	timestamp++;
	int block = sets[index].access(tag, timestamp, read);
	if (block == -1){
		if (read){
			if (instr){
				instrLoadMisses++;
			} else {
				dataLoadMisses++;
			}
		} else {
			dataStoreMisses++;
		}
		addrint tagEvicted;
		Set::Result res = sets[index].allocate(tag, timestamp, read, policy, &tagEvicted, &block);
		if (it != remapTable.end()){
			it->second.count++;
		}
		if (res == Set::NO_EVICTION){
			misses_without_eviction++;
			ret = MISS_WITHOUT_EVICTION;
		} else if (res == Set::EVICTION || res == Set::WRITEBACK){
			addrint actualEvictedAddr = (tagEvicted << (indexWidth + offsetWidth)) | ( actualAddr & ~tagMask & ~offsetMask);
			addrint actualEvictedPage = getPageIndex(actualEvictedAddr);
			auto itInv = invRemapTable.find(actualEvictedPage);
			if (itInv == invRemapTable.end()){
				//printf("itInv == invRemapTable.end()");
				*evictedAddr = actualEvictedAddr;
			} else {
//				printf("itInv != invRemapTable.end()\n");
//				printf("tagEvicted: %lu\n", tagEvicted);
//				printf("actualEvictedAddr: %lu\n", actualEvictedAddr);
//				printf("actualEvictedPage: %lu\n", actualEvictedPage);
				assert((actualEvictedAddr & msbMask) != 0);
				*evictedAddr = getPageAddress(itInv->second.addr, getPageOffset(actualEvictedAddr));
				(*itInv->second.countPtr)--;
//				printf("*itInv->second.countPtr: %u\n", *itInv->second.countPtr);
				if (*itInv->second.countPtr == 0){
					invRemapTable.erase(itInv);
					remapTable.erase(itInv->second.addr);
				}
//				printf("evictedAddr: %lu\n", *evictedAddr);
			}
			assert((*evictedAddr & msbMask) == 0);
			if (res == Set::EVICTION){
				misses_with_eviction++;
				ret = MISS_WITH_EVICTION;
			} else {
				misses_with_writeback++;
				ret = MISS_WITH_WRITEBACK;
			}
		} else if (res == Set::INVALID){
			misses_without_free_block++;
			ret = MISS_WITHOUT_FREE_BLOCK;
		} else {
			assert(false);
		}
	} else {
		hits++;
		if (read){
			if (instr){
				instrLoadHits++;
			} else {
				dataLoadHits++;
			}
		} else {
			dataStoreHits++;
		}
		ret = HIT;
	}
	if (internalAddr != 0){
		*internalAddr = (block << (indexWidth + offsetWidth)) | (index << offsetWidth);
	}
	return ret;
}

void CacheModel::pin(addrint addr){
	addrint actualAddr = getActualAddress(addr);
	addrint index = getIndex(actualAddr);
	addrint tag = getTag(actualAddr);
	sets[index].pin(tag);
}

void CacheModel::unpin(addrint addr){
	addrint actualAddr = getActualAddress(addr);
	addrint index = getIndex(actualAddr);
	addrint tag = getTag(actualAddr);
	sets[index].unpin(tag);
}

Set::Result CacheModel::flush(addrint addr){

	addrint index = getIndex(addr);
	addrint tag = getTag(addr);
	Set::Result res = sets[index].flush(tag);
	if (res == Set::NO_EVICTION){
		flushesWithoutEviction++;
	} else if (res == Set::EVICTION){
		flushesWithEviction++;
	} else if (res == Set::WRITEBACK){
		flushesWithWriteback++;
	} else {
		assert(false);
	}
	return res;
}

bool CacheModel::changeTag(addrint oldAddr, addrint newAddr){
	//debug2("CacheModel.changeTag(%lu, %lu)", oldAddr, newAddr);
	addrint oldIndex = getIndex(oldAddr);
	addrint newIndex = getIndex(newAddr);
	if (oldIndex != newIndex) {
		error("Cache blocks are not compatible");
	}
	addrint oldTag = getTag(oldAddr);
	addrint newTag = getTag(newAddr);
	bool res = sets[oldIndex].changeTag(oldTag, newTag);
	if (res){
		//debug2("changed tag: index: %lu, oldTag: %lu, newTag: %lu", oldIndex, oldTag, newTag);
		tagChangeHits++;
	} else {
		tagChangeMisses++;
	}
	return res;
}

void CacheModel::makeDirty(addrint addr){
	addrint actualAddr = getActualAddress(addr);
	addrint index = getIndex(actualAddr);
	addrint tag = getTag(actualAddr);
	sets[index].makeDirty(tag);
}

bool CacheModel::remap(addrint oldPage, addrint newPage, AddrList *present, AddrList *evicted){
	present->clear();
	auto it = remapTable.find(oldPage);
	if (it == remapTable.end()){
		addrint oldIndex = getIndex(getPageAddress(oldPage, 0));
		addrint newIndex = getIndex(getPageAddress(newPage, 0));
		if (oldIndex == newIndex){
			for (addrint offset = 0; offset < pageSize; offset += blockSize){
				addrint oldAddr = getPageAddress(oldPage, offset);
				if (changeTag(oldAddr, getPageAddress(newPage, offset))){
					present->push_back(oldAddr);
				}
			}
		} else {
			assert(getPageIndex(getPageAddress(oldPage, 0) | msbMask) == (oldPage | getPageIndex(msbMask)));
			addrint oldPageAndBit = oldPage | getPageIndex(msbMask);
			//printf("oldPageAndBit: %lu\n", oldPageAndBit);
			for (addrint offset = 0; offset < pageSize; offset += blockSize){
				addrint oldAddr = getPageAddress(oldPage, offset);
				if (changeTag(oldAddr, getPageAddress(oldPageAndBit, offset))){
					present->push_back(oldAddr);
				}
			}
			unsigned count = present->size();
			//printf("count: %u\n", count);
			if (count > 0){
				it = remapTable.emplace(newPage, RemapTableEntry(oldPageAndBit, count)).first;
				invRemapTable.emplace(oldPageAndBit, InvRemapTableEntry(newPage, &it->second.count));
			}
		}
	} else {
		auto itInv = invRemapTable.find(it->second.addr);
		assert(itInv != invRemapTable.end());
		addrint prevIndex = getIndex(getPageAddress(it->second.addr, 0));
		addrint newIndex = getIndex(getPageAddress(newPage, 0));
		if (prevIndex == newIndex){
			for (addrint offset = 0; offset < pageSize; offset += blockSize){
				addrint oldAddr = getPageAddress(it->second.addr, offset);
				if (changeTag(oldAddr, getPageAddress(newPage, offset))){
					present->push_back(oldAddr);
				}
			}
			unsigned count = present->size();
			assert(count == it->second.count);
			assert(count != 0);
			remapTable.erase(it);
			invRemapTable.erase(itInv);
		} else {
			assert((it->second.addr & getPageIndex(msbMask)) != 0);
			for (addrint offset = 0; offset < pageSize; offset += blockSize){
				addrint oldAddr = getPageAddress(it->second.addr, offset);
				if (changeTag(oldAddr, oldAddr)){
					present->push_back(oldAddr);
				}
			}
			unsigned count = present->size();
			assert(count == it->second.count);
			assert(count != 0);
			remapTable.erase(it);
			it = remapTable.emplace(newPage, RemapTableEntry(itInv->first, count)).first;
			itInv->second.addr = newPage;
			itInv->second.countPtr = &it->second.count;
		}
	}
	return false;
}

addrint CacheModel::getActualAddress(addrint addr) const {
	assert((addr & msbMask) == 0);
	auto it = remapTable.find(getPageIndex(addr));
	if (it == remapTable.end()){
		return addr;
	} else {
		addrint newAddr = getPageAddress(it->second.addr, getPageOffset(addr));
		assert((newAddr & msbMask) != 0);
		return newAddr;
	}
}


Cache::Cache(
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
	bool realRemapArg) :
		name(nameArg),
		desc(descArg),
		engine(engineArg),
		debugStart(debugStartArg),
		waitCounterIndex(waitCounterIndexArg),
		tagCounterIndex(tagCounterIndexArg),
		stallCounterIndex(stallCounterIndexArg),
		nextLevel(nextLevelArg),
		cacheModel(nameArg, descArg, statCont, cacheSizeArg, blockSizeArg, setAssocArg, policyArg, pageSizeArg),
		penalty(penaltyArg),
		maxQueueSize(maxQueueSizeArg),
		realRemap(realRemapArg),
		queueSize(0),
		nextStalledCaller(0),
		readAccessTime(statCont, nameArg + "_read_access_time", "Number of cycles of " + descArg + " read requests", 0),
		missesFromFlush(statCont, nameArg + "_misses_from_flush", "Number of " + descArg + " misses from flush", 0),
		writebacksFromFlush(statCont, nameArg + "_writebacks_from_flush", "Number of " + descArg + " writebacks from flush", 0) {

	accessTypeMask = 63;
	myassert(accessTypeMask < cacheModel.getBlockSize());
	myassert(ACCESS_TYPE_SIZE - 1 <= accessTypeMask);
}

bool Cache::access(MemoryRequest *request, IMemoryCallback *caller){
	uint64 timestamp = engine->getTimestamp();
	debug("(%p, %lu, %u, %s, %s, %d, %s)", request, request->addr, request->size, request->read?"read":"write", request->instr?"instr":"data", request->priority, caller->getName());

	if (DEBUG){
		for (auto it = requests.begin(); it != requests.end(); ++it){
			debug(": timestamp: %lu,  it->first: %lu, request: %p, request->addr: %lu, waitingForFlush: %s", it->second.timestamp, it->first, it->second.request, it->second.request->addr, it->second.waitingForFlush?"true":"false");
		}
	}

	if (queueSize == maxQueueSize){
//		auto sit = stalledCallers.begin();
//		while (sit != stalledCallers.end() && *sit != caller){
//			++sit;
//		}
//		if (sit == stalledCallers.end()){
//			stalledCallers.push_back(caller);
//		}
		stalledCallers.insert(caller);
		debug(": queue full (queueSize == %lu)", queueSize);
		return false;
	}

	myassert(queueSize < maxQueueSize);
	queueSize++;

	addrint blockAddr = cacheModel.getBlockAddress(request->addr);
	addrint lastByteAddr = cacheModel.getBlockAddress(request->addr + request->size - 1);

	if (blockAddr != lastByteAddr){
//		return true;
		error("Unaligned cache access");
	}
	if (request->size != cacheModel.getBlockSize()){
		error("Invalid request size (%u; cache model block size: %u", request->size, cacheModel.getBlockSize());
	}

	auto fit = flushRequests.find(request->addr);
	if (fit != flushRequests.end()){
		fit->second.repeat = true;

//		if (fit->second.result == Set::NO_EVICTION){
//			//this should no happen because caches are inclusive (no writeback from L1 of data that is not in L2) and CPU should not be accessing a cache block when it is being flushed
//			myassert(false);
//		} else if (fit->second.result == Set::EVICTION){
//			//access should only come from writeback from L1 (CPU should not be accessing a cache block when it is being flushed
//			//cache block becomes dirty
//			myassert(!request->read);
//			fit->second.result = Set::WRITEBACK;
//			delete request;
//			return true;
//		} else if (fit->second.result == Set::WRITEBACK){
//			//access should only come from writeback from L1 (CPU should not be accessing a cache block when it is being flushed
//			myassert(!request->read);
//			delete request;
//			return true;
//		} else {
//			myassert(false);
//		}
	}

	RequestMap::iterator it = requests.find(blockAddr);
	auto res = requests.emplace(blockAddr, Request(request));
	if (res.second){
		res.first->second.result = cacheModel.access(blockAddr, request->read, request->instr, &res.first->second.evictedAddr, 0);
		addEvent(penalty, blockAddr, ACCESS);
		if (!request->read){
			res.first->second.request->counters[TOTAL] = timestamp - res.first->second.request->counters[TOTAL];
//			if(!res.first->second.request->checkCounters()){
//				cout << timestamp << ": " << res.first->second.request << ", " << res.first->second.request->addr << endl;
//				res.first->second.request->printCounters();
//			}
			//assert(res.first->second.->request->checkCounters());
			res.first->second.request->resetCounters();
			res.first->second.request->counters[TOTAL] = timestamp;
		}
		request->counters[tagCounterIndex] = timestamp;
		debug(": %s, evictedAddr: %lu", res.first->second.result == CacheModel::HIT ? "hit" : (res.first->second.result == CacheModel::MISS_WITHOUT_EVICTION ? "miss without eviction" : (res.first->second.result == CacheModel::MISS_WITH_EVICTION ? "miss with eviction" : (res.first->second.result == CacheModel::MISS_WITH_WRITEBACK ? "miss with writeback" : ("")))), res.first->second.evictedAddr);
	} else {
		if (res.first->second.callers.empty()){
			myassert(!res.first->second.waitingForRead);
			res.first->second.result = CacheModel::HIT;
			addEvent(0, blockAddr, ACCESS);
			debug(": ongoing access after next level access");
			debug(": waitingForFlush: %s", res.first->second.waitingForFlush?"true":"false");
		} else {
			debug(": ongoing access before next level access");
			debug(": numCallers: %lu", res.first->second.numCallers);
			if (DEBUG){
				for (auto i = res.first->second.callers.begin(); i != res.first->second.callers.end(); ++i){
					debug(": caller: %p", i->request);
				}
			}
		}
		request->counters[waitCounterIndex] = timestamp;
	}
	res.first->second.numCallers++;
	debug(": emplaced request: %p", request);
	res.first->second.callers.emplace_back(request->read, request, caller);
	return true;
}

void Cache::accessCompleted(MemoryRequest *request, IMemory *caller){
	uint64 timestamp = engine->getTimestamp();
	debug("(%p, %lu, %s)", request, request->addr, caller->getName());
	myassert(caller == nextLevel);

	addrint blockAddr = request->addr;

	RequestMap::iterator it = requests.find(request->addr);
	myassert(it != requests.end());

	readAccessTime += (timestamp - it->second.timestamp);

	it->second.waitingForRead = false;
	for (CallerList::iterator callerIt = it->second.callers.begin(); callerIt != it->second.callers.end(); callerIt++){
		if (callerIt->request != it->second.request){
			uint64 waitTime = timestamp - callerIt->request->counters[waitCounterIndex];
			callerIt->request->counters[waitCounterIndex] = waitTime;
//			for (int i = COUNTER_INDEX_SIZE - 1; i >= tagCounterIndex; i--){
//				if (waitTime >= it->second.request->counters[i]){
//					callerIt->request->counters[i] += it->second.request->counters[i];
//					waitTime -= it->second.request->counters[i];
//				} else {
//					callerIt->request->counters[i] += waitTime;
//					break;
//				}
//
//			}
		}
	}
	for (CallerList::iterator callerIt = it->second.callers.begin(); callerIt != it->second.callers.end(); callerIt++){
		if (callerIt->read){
			callerIt->callback->accessCompleted(callerIt->request, this);
		} else {
			callerIt->request->counters[TOTAL] = timestamp - callerIt->request->counters[TOTAL];
//			if(!callerIt->request->checkCounters()){
//				cout << timestamp << ": " << callerIt->request << ", " << callerIt->request->addr << endl;
//				callerIt->request->printCounters();
//			}
//			myassert(callerIt->request->checkCounters());
			delete callerIt->request;
		}
	}
	it->second.callers.clear();

	if (!it->second.waitingForFlush){
		if (it->second.repeatFlush){
			FlushRequestMap::iterator fit = flushRequests.find(blockAddr);
			myassert(fit != flushRequests.end());
			myassert(fit->second.repeat);
			fit->second.result = cacheModel.flush(blockAddr);
			fit->second.repeat = false;
			addEvent(penalty, blockAddr, FLUSH);
		}
		if (queueSize == maxQueueSize){
			if (nextStalledCaller >= stalledCallers.size()){
				nextStalledCaller = 0;
			}
			auto sit = stalledCallers.begin();
			advance(sit, nextStalledCaller);
			auto last = sit;
			while (sit != stalledCallers.end()){
				(*sit)->unstall(this);
				++sit;
			}
			sit = stalledCallers.begin();
			while(sit != last){
				(*sit)->unstall(this);
				++sit;
			}
			stalledCallers.clear();
			nextStalledCaller++;
//			while (!stalledCallers.empty()){
//				stalledCallers.front()->unstall(this);
//				stalledCallers.pop_front();
//			}
		}
		myassert(queueSize >= it->second.numCallers);
		queueSize -= it->second.numCallers;
		requests.erase(it);
	}
}

void Cache::unstall(IMemory *caller){
	uint64 timestamp = engine->getTimestamp();
	debug("(%s)", caller->getName());
	addEvent(1, 0, UNSTALL);
}

void Cache::process(const Event *event){
	uint64 timestamp = engine->getTimestamp();
	addrint data = event->getData();
	AccessType type = static_cast<AccessType>(data & accessTypeMask);
	addrint blockAddr = data & ~accessTypeMask;
	debug("(): %lu, %d", blockAddr, type);
	if (type == ACCESS){
		RequestMap::iterator it = requests.find(blockAddr);
		RequestMap::iterator it2;
//		for(it2 = requests.begin(); it2 != requests.end(); it2++)
//				cout<<it2->first << " : "<< it2->second.request->addr<<endl;
//		cout << "****";
//			if(it2->second.request->addr == 0)
		myassert(it != requests.end());
		it->second.request->counters[tagCounterIndex] = timestamp - it->second.request->counters[tagCounterIndex];
		it->second.waitingForTag = false;
		if (it->second.result == CacheModel::HIT){

		} else if (it->second.result == CacheModel::MISS_WITHOUT_EVICTION){
			if (outgoingFlushRequests.count(blockAddr) > 0){
				missesFromFlush++;
			} else {
				it->second.timestamp = timestamp;
				it->second.waitingForRead = true;
				it->second.request->read = true;
				if (!stalledRequests.empty() || !nextLevel->access(it->second.request, this)){
					debug(": next level is stalled: %p, %lu", it->second.request, it->second.request->addr);
					stalledRequests.emplace_back(it->second.request);
					it->second.request->counters[stallCounterIndex] = timestamp;
				}
			}
		} else if (it->second.result == CacheModel::MISS_WITH_EVICTION){
			if (outgoingFlushRequests.count(blockAddr) > 0){
				missesFromFlush++;
			} else {
				it->second.timestamp = timestamp;
				it->second.waitingForRead = true;
				it->second.request->read = true;
				if (!stalledRequests.empty() || !nextLevel->access(it->second.request, this)){
					debug(": next level is stalled: %p, %lu", it->second.request, it->second.request->addr);
					stalledRequests.emplace_back(it->second.request);
					it->second.request->counters[stallCounterIndex] = timestamp;
				}
			}
			if (prevLevels.size() > 0){
				it->second.waitingForFlush = true;
				auto p = outgoingFlushRequests.emplace(it->second.evictedAddr, OutgoingFlushRequest(ACCESS, blockAddr, prevLevels.size(), false, false));
				if(p.second){
					for (CacheList::iterator itCache = prevLevels.begin(); itCache != prevLevels.end(); itCache++){
						(*itCache)->flush(it->second.evictedAddr, cacheModel.getBlockSize(), false, this);
					}
				} else {
					p.first->second.requests.emplace_back(ACCESS, blockAddr);
				}
			}
		} else if (it->second.result == CacheModel::MISS_WITH_WRITEBACK){
			if (outgoingFlushRequests.count(blockAddr) > 0){
				missesFromFlush++;
			} else {
				it->second.timestamp = timestamp;
				it->second.waitingForRead = true;
				it->second.request->read = true;
				if (!stalledRequests.empty() || !nextLevel->access(it->second.request, this)){
					debug(": next level is stalled: %p, %lu", it->second.request, it->second.request->addr);
					stalledRequests.emplace_back(it->second.request);
					it->second.request->counters[stallCounterIndex] = timestamp;
				}
			}
			if (prevLevels.size() > 0){
				it->second.waitingForFlush = true;
				auto p = outgoingFlushRequests.emplace(it->second.evictedAddr, OutgoingFlushRequest(ACCESS, blockAddr, prevLevels.size(), true, true));
				if(p.second){
					for (CacheList::iterator itCache = prevLevels.begin(); itCache != prevLevels.end(); itCache++){
						(*itCache)->flush(it->second.evictedAddr, cacheModel.getBlockSize(), false, this);
					}
				} else {
					p.first->second.requests.emplace_back(ACCESS, blockAddr);
				}
			} else {
				MemoryRequest *wbRequest = new MemoryRequest(it->second.evictedAddr, cacheModel.getBlockSize(), false, false, HIGH);
				wbRequest->counters[TOTAL] = timestamp;
				if (!stalledRequests.empty() || !nextLevel->access(wbRequest, this)){
					debug(": next level is stalled: %p, %lu", wbRequest, wbRequest->addr);
					auto sit = stalledRequests.emplace(stalledRequests.end(), wbRequest);
					auto fit = flushRequests.find(wbRequest->addr);
					if (fit != flushRequests.end()){
						sit->flushing = true;
						fit->second.stalledRequestsLeft++;
					}
					wbRequest->counters[stallCounterIndex] = timestamp;
				}
			}
		} else if (it->second.result == CacheModel::MISS_WITHOUT_FREE_BLOCK){
			error("CacheModel::access() returned MISS_WITHOUT_FREE_BLOCK");
		} else {
			myassert(false);
		}

		if (!it->second.waitingForRead){
			for (CallerList::iterator callerIt = it->second.callers.begin(); callerIt != it->second.callers.end(); callerIt++){
				debug(": callerIt->request: %p", callerIt->request);
				if (callerIt->request != it->second.request){
//					uint64 waitTime = timestamp - callerIt->request->counters[waitCounterIndex];
//					callerIt->request->counters[waitCounterIndex] = waitTime;
//					for (int i = COUNTER_INDEX_SIZE - 1; i >= tagCounterIndex; i--){
//						if (waitTime >= it->second.request->counters[i]){
//							callerIt->request->counters[i] += it->second.request->counters[i];
//							waitTime -= it->second.request->counters[i];
//						} else {
//							callerIt->request->counters[i] += waitTime;
//							break;
//						}
//
//					}
				}
			}
			for (CallerList::iterator callerIt = it->second.callers.begin(); callerIt != it->second.callers.end(); callerIt++){
				if (callerIt->read){
					callerIt->callback->accessCompleted(callerIt->request, this);
				} else {
					callerIt->request->counters[TOTAL] = timestamp - callerIt->request->counters[TOTAL];
//					if(!callerIt->request->checkCounters()){
//						cout << timestamp << ": " << callerIt->request << ", " << callerIt->request->addr << endl;
//						callerIt->request->printCounters();
//					}
//					assert(callerIt->request->checkCounters());
					delete callerIt->request;
				}
			}
			it->second.callers.clear();
		}
		if (!it->second.waitingForFlush && !it->second.pinners.empty()){
			for (PinnerList::iterator pinnerIt = it->second.pinners.begin(); pinnerIt != it->second.pinners.end(); pinnerIt++){
				pinnerIt->callback->pinCompleted(pinnerIt->addr, this);
			}
			it->second.pinners.clear();
		}
		if (!it->second.waitingForRead && !it->second.waitingForFlush){
			if (it->second.repeatFlush){
				FlushRequestMap::iterator fit = flushRequests.find(blockAddr);
				myassert(fit != flushRequests.end());
				myassert(fit->second.repeat);
				fit->second.result = cacheModel.flush(blockAddr);
				fit->second.repeat = false;
				addEvent(penalty, blockAddr, FLUSH);
			}
			if (queueSize == maxQueueSize){
				if (nextStalledCaller >= stalledCallers.size()){
					nextStalledCaller = 0;
				}
				auto sit = stalledCallers.begin();
				advance(sit, nextStalledCaller);
				auto last = sit;
				while (sit != stalledCallers.end()){
					(*sit)->unstall(this);
					++sit;
				}
				sit = stalledCallers.begin();
				while(sit != last){
					(*sit)->unstall(this);
					++sit;
				}
				stalledCallers.clear();
				nextStalledCaller++;
			}
			myassert(queueSize >= it->second.numCallers);
			queueSize -= it->second.numCallers;
			requests.erase(it);
		}
	} else if (type == FLUSH){
//		if (DEBUG){
//			if (timestamp >= debugStart) {
//				for (FlushRequestMap::iterator it = flushRequests.begin(); it != flushRequests.end(); it++){
//					debug("it->first: %lu", it->first);
//				}
//			}
//		}
		FlushRequestMap::iterator it = flushRequests.find(blockAddr);
		myassert(it != flushRequests.end());
		if (it->second.result == Set::NO_EVICTION){
			if (it->second.guarantee){
				if (prevLevels.size() > 0){
					auto p = outgoingFlushRequests.emplace(blockAddr, OutgoingFlushRequest(FLUSH, 0, prevLevels.size(), false, it->second.guarantee));
					if (p.second){
						for (CacheList::iterator itCache = prevLevels.begin(); itCache != prevLevels.end(); itCache++){
							(*itCache)->flush(blockAddr, cacheModel.getBlockSize(), it->second.guarantee, this);
						}
					} else {
						p.first->second.requests.emplace_back(FLUSH, 0);
						if (!p.first->second.guarantee){
							p.first->second.guarantee = true;
							p.first->second.notificationsLeft = prevLevels.size();
							for (CacheList::iterator itCache = prevLevels.begin(); itCache != prevLevels.end(); itCache++){
								(*itCache)->flush(blockAddr, cacheModel.getBlockSize(), true, this);
							}
						}
					}
				} else {
					if (it->second.repeat){
						auto rit = requests.find(blockAddr);
						if (rit == requests.end()){
							it->second.result = cacheModel.flush(blockAddr);
							it->second.repeat = false;
							addEvent(penalty, blockAddr, FLUSH);
						} else {
							rit->second.repeatFlush = true;
						}
					} else {
						it->second.done = true;
						if (it->second.stalledRequestsLeft == 0){
							IFlushCallback *caller = it->second.caller;
							bool dirty = it->second.dirty;
							flushRequests.erase(it);
							caller->flushCompleted(blockAddr, dirty, this);
						}
					}
				}
			} else {
				it->second.done = true;
				if (it->second.stalledRequestsLeft == 0){
					IFlushCallback *caller = it->second.caller;
					bool dirty = it->second.dirty;
					flushRequests.erase(it);
					caller->flushCompleted(blockAddr, dirty, this);
				}
			}
		} else if (it->second.result == Set::EVICTION || it->second.result == Set::WRITEBACK){
			if(it->second.result == Set::WRITEBACK){
				it->second.dirty = true;
			}
			if (prevLevels.size() > 0){
				auto p = outgoingFlushRequests.emplace(blockAddr, OutgoingFlushRequest(FLUSH, 0, prevLevels.size(), it->second.dirty, it->second.guarantee));
				if (p.second){
					for (CacheList::iterator itCache = prevLevels.begin(); itCache != prevLevels.end(); itCache++){
						(*itCache)->flush(blockAddr, cacheModel.getBlockSize(), it->second.guarantee, this);
					}
				} else {
					p.first->second.requests.emplace_back(FLUSH, 0);
					if (!p.first->second.guarantee && it->second.guarantee){
						p.first->second.guarantee = true;
						p.first->second.notificationsLeft = prevLevels.size();
						for (CacheList::iterator itCache = prevLevels.begin(); itCache != prevLevels.end(); itCache++){
							(*itCache)->flush(blockAddr, cacheModel.getBlockSize(), true, this);
						}
					}
				}
			} else {
				if (it->second.guarantee && it->second.repeat){
					auto rit = requests.find(blockAddr);
					if (rit == requests.end()){
						it->second.result = cacheModel.flush(blockAddr);
						it->second.repeat = false;
						addEvent(penalty, blockAddr, FLUSH);
					} else {
						rit->second.repeatFlush = true;
					}
				} else {
					it->second.done = true;
					if (it->second.stalledRequestsLeft == 0){
						IFlushCallback *caller = it->second.caller;
						bool dirty = it->second.dirty;
						flushRequests.erase(it);
						caller->flushCompleted(blockAddr, dirty, this);
					}
				}
			}
		} else {
			error("Invalid flush result");
		}
	} else if (type == REMAP){
		addrint oldPage = cacheModel.getPageIndex(blockAddr);
		RemapRequestMap::iterator it = remapRequests.find(oldPage);
		myassert(it != remapRequests.end());
		if (it->second.result){
			myassert(false);
			//TODO: implement eviction from remap table
		}
		if (realRemap){
			if (prevLevels.size() > 0 && it->second.present.size() > 0){
				it->second.notificationsLeft = it->second.present.size();
				for (auto itList = it->second.present.begin(); itList != it->second.present.end(); ++itList){
					auto p = outgoingFlushRequests.emplace(*itList, OutgoingFlushRequest(REMAP, oldPage, prevLevels.size(), false, true));
					if (p.second){
						for (CacheList::iterator itCache = prevLevels.begin(); itCache != prevLevels.end(); itCache++){
							(*itCache)->flush(*itList, cacheModel.getBlockSize(), true, this);
						}
					} else {
						p.first->second.requests.emplace_back(REMAP, oldPage);
						if (!p.first->second.guarantee){
							p.first->second.guarantee = true;
							p.first->second.notificationsLeft = prevLevels.size();
							for (CacheList::iterator itCache = prevLevels.begin(); itCache != prevLevels.end(); itCache++){
								(*itCache)->flush(*itList, cacheModel.getBlockSize(), true, this);
							}
						}
					}
				}
			} else {
				IRemapCallback *caller = it->second.caller;
				remapRequests.erase(it);
				caller->remapCompleted(oldPage, this);
			}
		} else {
			if (prevLevels.size() > 0 && it->second.present.size() > 0){
				it->second.notificationsLeft = prevLevels.size();
				for (CacheList::iterator itCache = prevLevels.begin(); itCache != prevLevels.end(); itCache++){
					(*itCache)->remap(oldPage, it->second.newPage, this);
				}
			} else {
				IRemapCallback *caller = it->second.caller;
				remapRequests.erase(it);
				caller->remapCompleted(oldPage, this);
			}
		}
	} else if (type == TAG_CHANGE){
		TagChangeRequestMap::iterator it = tagChangeRequests.find(blockAddr);
		myassert(it != tagChangeRequests.end());
		if (it->second.result){
			if (prevLevels.size() > 0){
				auto p = outgoingFlushRequests.emplace(blockAddr, OutgoingFlushRequest(TAG_CHANGE, 0, prevLevels.size(), false, true));
				if (p.second){
					for (CacheList::iterator itCache = prevLevels.begin(); itCache != prevLevels.end(); itCache++){
						(*itCache)->flush(blockAddr, cacheModel.getBlockSize(), true, this);
					}
				} else {
					p.first->second.requests.emplace_back(TAG_CHANGE, 0);
					if (!p.first->second.guarantee){
						p.first->second.guarantee = true;
						p.first->second.notificationsLeft = prevLevels.size();
						for (CacheList::iterator itCache = prevLevels.begin(); itCache != prevLevels.end(); itCache++){
							(*itCache)->flush(blockAddr, cacheModel.getBlockSize(), true, this);
						}
					}
				}
			} else {
				ITagChangeCallback *caller = it->second.caller;
				tagChangeRequests.erase(it);
				caller->tagChangeCompleted(blockAddr);
			}
		} else {
			ITagChangeCallback *caller = it->second.caller;
			tagChangeRequests.erase(it);
			caller->tagChangeCompleted(blockAddr);
		}
	} else if(type == UNSTALL){
		debug(": unstalling cache");
		auto it = stalledRequests.begin();
		myassert(it != stalledRequests.end());
		uint64 origStallTimestamp = it->request->counters[stallCounterIndex];
		it->request->counters[stallCounterIndex] = timestamp - it->request->counters[stallCounterIndex];
		addrint reqAddr = it->request->addr;
		if (nextLevel->access(it->request, this)){
			if (it->flushing){
				auto fit = flushRequests.find(reqAddr);
				myassert(fit != flushRequests.end());
				fit->second.stalledRequestsLeft--;
				if (fit->second.stalledRequestsLeft == 0 && fit->second.done){
					IFlushCallback *caller = fit->second.caller;
					bool dirty = fit->second.dirty;
					flushRequests.erase(fit);
					caller->flushCompleted(reqAddr, dirty, this);
				}
			}
			for (PinnerList::iterator pinnerIt = it->pinners.begin(); pinnerIt != it->pinners.end(); ++pinnerIt){
				pinnerIt->callback->pinCompleted(pinnerIt->addr, this);
			}
			it = stalledRequests.erase(it);
			if (it != stalledRequests.end()){
				addEvent(0, 0, UNSTALL);
			}
		} else {
			it->request->counters[stallCounterIndex] = origStallTimestamp;
		}
	} else {
		myassert(false);
	}
}

void Cache::flush(addrint blockAddr, uint8 size, bool guarantee, IFlushCallback *caller){
	uint64 timestamp = engine->getTimestamp();
	debug("(%lu, %u, %s, %s)", blockAddr, size, guarantee?"true":"false", caller->getName());
	myassert(size == cacheModel.getBlockSize());
	myassert(blockAddr == cacheModel.getBlockAddress(blockAddr));
	auto res = flushRequests.emplace(blockAddr, FlushRequest(guarantee));
	if (res.second){
		res.first->second.result = cacheModel.flush(blockAddr);
		debug(": %s", res.first->second.result == Set::EVICTION ? "eviction" : (res.first->second.result == Set::NO_EVICTION ? "no eviction" : (res.first->second.result == Set::WRITEBACK ? "writeback" :  (""))));
		res.first->second.caller = caller;
		addEvent(penalty, blockAddr, FLUSH);
	} else {
		if (guarantee){
			res.first->second.guarantee = true;
		}
	}
	for (auto sit = stalledRequests.begin(); sit != stalledRequests.end(); ++sit){
		if (sit->request->addr == blockAddr){
			sit->flushing = true;
			res.first->second.stalledRequestsLeft++;
		}
	}
	debug(": stalledRequestsLeft: %u", res.first->second.stalledRequestsLeft);
}

void Cache::flushCompleted(addrint blockAddr, bool dirty, IMemory *caller){
	uint64 timestamp = engine->getTimestamp();
	debug("(%lu, %s, %s)", blockAddr, dirty?"dirty":"clean", caller->getName());
	OutgoingFlushRequestMap::iterator outIt = outgoingFlushRequests.find(blockAddr);
	myassert (outIt != outgoingFlushRequests.end());
	outIt->second.notificationsLeft--;
	if (dirty){
		if (!outIt->second.dirty){
			outIt->second.dirty = true;
			writebacksFromFlush++;
		}
	}
	if (outIt->second.notificationsLeft == 0){
		for (auto lit = outIt->second.requests.begin(); lit != outIt->second.requests.end(); ++lit){
			debug(": %d", lit->type);
			if (lit->type == ACCESS){
				RequestMap::iterator it = requests.find(lit->origAddr);
				myassert(it != requests.end());
				it->second.waitingForFlush = false;
				if (outIt->second.dirty){
					MemoryRequest *wbRequest = new MemoryRequest(it->second.evictedAddr, cacheModel.getBlockSize(), false, false, HIGH);
					wbRequest->counters[TOTAL] = timestamp;
					if (!stalledRequests.empty() || !nextLevel->access(wbRequest, this)){
						stalledRequests.emplace_back(wbRequest, it->second.pinners);
						it->second.pinners.clear();
						wbRequest->counters[stallCounterIndex] = timestamp;
					} else {
						for (PinnerList::iterator pinnerIt = it->second.pinners.begin(); pinnerIt != it->second.pinners.end(); pinnerIt++){
							pinnerIt->callback->pinCompleted(pinnerIt->addr, this);
						}
					}
				} else {
					for (PinnerList::iterator pinnerIt = it->second.pinners.begin(); pinnerIt != it->second.pinners.end(); pinnerIt++){
						pinnerIt->callback->pinCompleted(pinnerIt->addr, this);
					}
				}
				it->second.pinners.clear();
				if (!it->second.waitingForRead){
					if (it->second.repeatFlush){
						FlushRequestMap::iterator fit = flushRequests.find(lit->origAddr);
						myassert(fit != flushRequests.end());
						myassert(fit->second.repeat);
						fit->second.result = cacheModel.flush(lit->origAddr);
						fit->second.repeat = false;
						addEvent(penalty, lit->origAddr, FLUSH);
					}
					if (queueSize == maxQueueSize){
						if (nextStalledCaller >= stalledCallers.size()){
							nextStalledCaller = 0;
						}
						auto sit = stalledCallers.begin();
						advance(sit, nextStalledCaller);
						auto last = sit;
						while (sit != stalledCallers.end()){
							(*sit)->unstall(this);
							++sit;
						}
						sit = stalledCallers.begin();
						while(sit != last){
							(*sit)->unstall(this);
							++sit;
						}
						stalledCallers.clear();
						nextStalledCaller++;
					}
					myassert(queueSize >= it->second.numCallers);
					queueSize -= it->second.numCallers;
					requests.erase(it);
				}
			} else if (lit->type == FLUSH){
				FlushRequestMap::iterator it = flushRequests.find(blockAddr);
				myassert(it != flushRequests.end());
				if(outIt->second.dirty){
					it->second.dirty = true;
				}
				debug(": guarantee: %s, repeat: %s", it->second.guarantee?"true":"false", it->second.repeat?"true":"false");
				if (it->second.guarantee && it->second.repeat){
					auto rit = requests.find(blockAddr);
					if (rit == requests.end()){
						it->second.result = cacheModel.flush(blockAddr);
						it->second.repeat = false;
						addEvent(penalty, blockAddr, FLUSH);
					} else {
						rit->second.repeatFlush = true;
					}
				} else {
					it->second.done = true;
					debug(": stalledRequestsLeft: %u", it->second.stalledRequestsLeft);
					if (it->second.stalledRequestsLeft == 0){
						IFlushCallback *caller = it->second.caller;
						bool d = it->second.dirty;
						flushRequests.erase(it);
						caller->flushCompleted(blockAddr, d, this);
					}
				}
			} else if (lit->type == REMAP){
				addrint oldPage = lit->origAddr;
				RemapRequestMap::iterator it = remapRequests.find(oldPage);
				myassert(it != remapRequests.end());
				if (outIt->second.dirty){
					addrint newAddr = cacheModel.getPageAddress(it->second.newPage, cacheModel.getPageOffset(outIt->first));
					debug("make dirty: %lu", newAddr);
					cacheModel.makeDirty(newAddr);
				}
				it->second.notificationsLeft--;
				if (it->second.notificationsLeft == 0){
					IRemapCallback *caller = it->second.caller;
					remapRequests.erase(it);
					caller->remapCompleted(oldPage, this);
				}
			} else if (lit->type == TAG_CHANGE){
				TagChangeRequestMap::iterator it = tagChangeRequests.find(blockAddr);
				myassert(it != tagChangeRequests.end());
				if (outIt->second.dirty){
					cacheModel.makeDirty(it->second.newAddr);
				}
				ITagChangeCallback *caller = it->second.caller;
				tagChangeRequests.erase(it);
				caller->tagChangeCompleted(blockAddr);
			} else {
				myassert(false);
			}
		}
		outgoingFlushRequests.erase(outIt);
	}


}

void Cache::remap(addrint oldPage, addrint newPage, IRemapCallback *caller){
	uint64 timestamp = engine->getTimestamp();
	debug("(%lu, %lu, %s)", oldPage, newPage, caller->getName());
	auto p = remapRequests.emplace(oldPage, RemapRequest());
	assert(p.second);
	RemapRequestMap::iterator it = p.first;
	it->second.result = cacheModel.remap(oldPage, newPage, &it->second.present, &it->second.evicted);
	it->second.newPage = newPage;
	it->second.caller = caller;
	uint64 latency = realRemap ? penalty : 0;
	addEvent(latency, cacheModel.getPageAddress(oldPage, 0), REMAP);
}

void Cache::remapCompleted(addrint page, IMemory *caller){
	RemapRequestMap::iterator it = remapRequests.find(page);
	myassert(it != remapRequests.end());
	it->second.notificationsLeft--;
	if (it->second.notificationsLeft == 0){
		IRemapCallback *caller = it->second.caller;
		remapRequests.erase(it);
		caller->remapCompleted(page, this);
	}
}

void Cache::changeTag(addrint oldAddr, addrint newAddr, uint8 size, ITagChangeCallback *caller){
	myassert(size == cacheModel.getBlockSize());
	myassert(oldAddr == cacheModel.getBlockAddress(oldAddr));
	myassert(newAddr == cacheModel.getBlockAddress(newAddr));
	TagChangeRequestMap::iterator it = tagChangeRequests.emplace(oldAddr, TagChangeRequest()).first;
	it->second.result = cacheModel.changeTag(oldAddr, newAddr);
	it->second.newAddr = newAddr;
	it->second.caller = caller;
	addEvent(penalty, oldAddr, TAG_CHANGE);
}

/*
 * Returns the number of blocks in the page that are being evicted or written back when the pin request arrives.
 */
unsigned Cache::pin(addrint addr, IPinCallback *caller){
	uint64 timestamp = engine->getTimestamp();
	myassert(addr == cacheModel.getBlockAddress(addr));
	unsigned count = 0;
	for (RequestMap::iterator it = requests.begin(); it != requests.end(); it++){
		if (it->second.result != CacheModel::HIT && it->second.evictedAddr == addr && (it->second.waitingForTag || it->second.waitingForFlush)){
			it->second.pinners.emplace_back(Pinner(addr, caller));
			count++;
		}
	}
	cacheModel.pin(addr);
	debug(": %lu, %s", addr, caller->getName());
	return count;
}
