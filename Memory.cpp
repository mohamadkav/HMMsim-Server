/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#include "Memory.H"

#include <iomanip>


Staller::Staller(Engine *engineArg, uint64 penaltyArg) : engine(engineArg), penalty(penaltyArg){}

bool Staller::access(MemoryRequest *request,  IMemoryCallback *caller){
	engine->addEvent(penalty, this, reinterpret_cast<addrint>(new pair<MemoryRequest *, IMemoryCallback*>(request, caller)));
	return false;
}

void Staller::process(const Event *event){
	pair<MemoryRequest *, IMemoryCallback*> *p = reinterpret_cast<pair<MemoryRequest *, IMemoryCallback*> *>(event->getData());
	p->second->accessCompleted(p->first, this);
	delete p;
}

Memory::Memory(
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
	addrint offsetArg) :
		name(nameArg),
		desc(descArg),
		engine(engineArg),
		debugStart(debugStartArg),
		globalQueue(globalQueueArg),
		maxQueueSize(maxQueueSizeArg),
		mapping(mappingTypeArg, numRanksArg, banksPerRankArg, rowsPerBankArg, blocksPerRowArg, blockSizeArg),
		offset(offsetArg),
		stalled(false),
		stallStartTimestamp(0),
		criticalStallTime(statCont, nameArg + "_critical_stall_time", "Number of cycles " + descArg + " stalls critical requests", 0),
		readStallTime(statCont, nameArg + "_read_stall_time", "Number of cycles " + descArg + " stalls on read requests", 0),
		writeStallTime(statCont, nameArg + "_write_stall_time", "Number of cycles " + descArg + " stalls on write requests", 0),
		queueStallTime(statCont, nameArg + "_queue_stall_time", "Number of cycles " + descArg + " queue is stalled", 0),
		numReadRequests(statCont, nameArg + "_read_requests", "Number of " + descArg + " read requests", 0),
		numWriteRequests(statCont, nameArg + "_write_requests", "Number of " + descArg + " write requests", 0),
		readQueueTime(statCont, nameArg + "_read_queue_time", "Number of cycles " + descArg + " read requests wait in the queue", 0),
		writeQueueTime(statCont, nameArg + "_write_queue_time", "Number of cycles " + descArg + " write requests wait in the queue", 0),
		readTotalTime(statCont, nameArg + "_read_total_time", "Total number of cycles of " + descArg + " read requests", 0),
		writeTotalTime(statCont, nameArg + "_write_total_time", "Total number of cycles of " + descArg + " write requests", 0),
		rowBufferHits(statCont, nameArg + "_row_buffer_hits", "Number of " + descArg + " row buffer hits", 0),
		rowBufferMisses(statCont, nameArg + "_row_buffer_misses", "Number of " + descArg + " row buffer misses", 0),
		numOpens(statCont, nameArg + "_num_opens", "Number of " + descArg + " opens", 0),
		numAccesses(statCont, nameArg + "_num_accesses", "Number of " + descArg + " accesses", 0),
		numCloses(statCont, nameArg + "_num_closes", "Number of " + descArg + " closes", 0),
		numRARs(statCont, nameArg + "_num_read_after_read", "Number of " + descArg + " read after read (RAR) hazards", 0),
		numRAWs(statCont, nameArg + "_num_read_after_write", "Number of " + descArg + " read after write (RAW) hazards", 0),
		numWARs(statCont, nameArg + "_num_write_after_read", "Number of " + descArg + " write after read (WAR) hazards", 0),
		numWAWs(statCont, nameArg + "_num_write_after_write", "Number of " + descArg + " write after write (WAW) hazards", 0),
		waitLowerPriorityTime(statCont, nameArg + "_wait_lower_priority_time", "Number of cycles " + descArg + " requests wait for lower priority requests", 0),
		waitSamePriorityTime(statCont, nameArg + "_wait_same_priority_time", "Number of cycles " + descArg + " requests wait for same priority requests", 0),
		waitHigherPriorityTime(statCont, nameArg + "_wait_higher_priority_time", "Number of cycles " + descArg + " requests wait for higher priority requests", 0),
		numRequests(statCont, nameArg + "_requests", "Total number of " + descArg + " requests", &numReadRequests, &numWriteRequests),
		averageQueueStallTime(statCont, nameArg + "_avg_queue_stall_time", "Average number of cycles " + descArg + " queue is stalled", &queueStallTime, &numRequests),
		totalStallTime(statCont, nameArg + "_total_stall_time", "Total number of cycles " + descArg + " stalls on requests", &readStallTime, &writeStallTime),
		totalQueueTime(statCont, nameArg + "_total_queue_time", "Total number of cycles " + descArg + " requests wait in the queue", &readQueueTime, &writeQueueTime),
		readServiceTime(statCont, nameArg + "_read_service_time", "Number of cycles " + descArg + " spends servicing read requests", &readTotalTime, &readQueueTime),
		writeServiceTime(statCont, nameArg + "_write_service_time", "Number of cycles " + descArg + " spends servicing write requests", &writeTotalTime, &writeQueueTime),
		totalServiceTime(statCont, nameArg + "_total_service_time", "Total number of cycles " + descArg + " spends servicing requests", &readServiceTime, &writeServiceTime),
		totalTime(statCont, nameArg + "_total_time", "Total number of cycles of " + descArg + " requests", &readTotalTime, &writeTotalTime),
		averageReadQueueTime(statCont, nameArg + "_avg_read_queue_time", "Average number of cycles " + descArg + " read requests spend in the queue", &readQueueTime, &numReadRequests),
		averageWriteQueueTime(statCont, nameArg + "_avg_write_queue_time", "Average number of cycles " + descArg + " write requests spend in the queue", &writeQueueTime, &numWriteRequests),
		averageQueueTime(statCont, nameArg + "_avg_queue_time", "Average number of cycles " + descArg + " requests spend in the queue", &totalQueueTime, &numRequests),
		averageReadServiceTime(statCont, nameArg + "_avg_read_service_time", "Average number of cycles " + descArg + " spends servicing read requests", &readServiceTime, &numReadRequests),
		averageWriteServiceTime(statCont, nameArg + "_avg_write_service_time", "Average number of cycles " + descArg + " spends servicing write requests", &writeServiceTime, &numWriteRequests),
		averageServiceTime(statCont, nameArg + "_avg_service_time", "Average number of cycles " + descArg + " spends servicing requests", &totalServiceTime, &numRequests),
		averageReadTime(statCont, nameArg + "_avg_read_time", "Average number of cycles of " + descArg + " read requests", &readTotalTime, &numReadRequests),
		averageWriteTime(statCont, nameArg + "_avg_write_time", "Average number of cycles of " + descArg + " write requests", &writeTotalTime, &numWriteRequests),
		averageTime(statCont, nameArg + "_avg_time", "Average number of cycles of " + descArg + " requests", &totalTime, &numRequests),
		rowBufferAccesses(statCont, nameArg + "_row_buffer_accesses", "Number of " + descArg + " row buffer misses", &rowBufferHits, &rowBufferMisses)

{


	bus = new Bus(name + "_bus", desc + " bus", engineArg, statCont, debugStartArg, busLatencyArg);

	unsigned numBanks = mapping.getNumBanks();
	for (unsigned i = 0; i < numBanks; i++) {
		stringstream ssName;
		ssName << name;
		ssName << "_bank_" << i;
		stringstream ssDesc;
		ssDesc << desc;
		ssDesc << " bank " << i;
		Bank *newBank = new Bank(ssName.str(), ssDesc.str(), engineArg, statCont, debugStartArg, queueCounterIndexArg, openCounterIndexArg, accessCounterIndexArg, closeCounterIndexArg, busQueueCounterIndexArg, busCounterIndexArg, policyArg, typeArg, this, bus, openLatencyArg,
			closeLatencyArg, accessLatencyArg, longCloseLatencyArg);
		banks.emplace_back(newBank);
		numReadRequests.addStat(newBank->getStatNumReadRequests());
		numWriteRequests.addStat(newBank->getStatNumWriteRequests());
		readQueueTime.addStat(newBank->getStatReadQueueTime());
		writeQueueTime.addStat(newBank->getStatWriteQueueTime());
		readTotalTime.addStat(newBank->getStatReadTotalTime());
		writeTotalTime.addStat(newBank->getStatWriteTotalTime());
		rowBufferHits.addStat(newBank->getStatRowBufferHits());
		rowBufferMisses.addStat(newBank->getStatRowBufferMisses());
		numOpens.addStat(newBank->getStatNumOpens());
		numAccesses.addStat(newBank->getStatNumAccesses());
		numCloses.addStat(newBank->getStatNumCloses());
		numRARs.addStat(newBank->getStatNumRARs());
		numRAWs.addStat(newBank->getStatNumRAWs());
		numWARs.addStat(newBank->getStatNumWARs());
		numWAWs.addStat(newBank->getStatNumWAWs());
		waitLowerPriorityTime.addStat(newBank->getStatWaitLowerPriorityTime());
		waitSamePriorityTime.addStat(newBank->getStatWaitSamePriorityTime());
		waitHigherPriorityTime.addStat(newBank->getStatWaitHigherPriorityTime());
	}
	if (globalQueue) {
		queueSizes = new int[1];
		queueSizes[0] = 0;
	} else {
		queueSizes = new int[numBanks];
		for (unsigned i = 0; i < numBanks; i++) {
			queueSizes[i] = 0;
		}
	}

	//debugStart = 0;
	//debugStart = 104350000;
}

Memory::~Memory(){
	for (unsigned i = 0; i < mapping.getNumBanks(); i++) {
		delete banks[i];
	}
	delete bus;
	delete [] queueSizes;
}

bool Memory::access(MemoryRequest *request, IMemoryCallback *caller){
	uint64 timestamp = engine->getTimestamp();
	debug("(%p, %lu, %u, %s, %s, %d, %s)", request, request->addr, request->size, request->read?"read":"write", request->instr?"instr":"data", request->priority, caller->getName());
	if (mapping.getBlockSize() != request->size){
		warn("Size of access (%u) is different from block size (%u)", request->size, mapping.getBlockSize());
		myassert(false);
	}

	if (request->addr < offset || request->addr >= getSize() + offset){
		cout << request->addr << endl;
		warn("Memory access is out of range");
		myassert(false);
	}

	if (stalled){
		stalledCallers.insert(caller);
		return false;
	}

	request->addr -= offset;

	unsigned bankIndex = mapping.getBankId(request->addr);
	unsigned queueIndex;
	if (globalQueue){
		queueIndex = 0;
	} else {
		queueIndex = bankIndex;
	}

	requests.emplace(request, caller);

	banks[bankIndex]->access(request, this);

	myassert(queueSizes[queueIndex] < maxQueueSize);
	queueSizes[queueIndex]++;
	if (queueSizes[queueIndex] == maxQueueSize) {
		stalled = true;
		stallStartTimestamp = timestamp;
	}

	return true;
}

void Memory::accessCompleted(MemoryRequest *request, IMemory *caller) {
	uint64 timestamp = engine->getTimestamp();
	debug("(%p, %s)", request, caller->getName());

	unsigned bankIndex = mapping.getBankId(request->addr);
	unsigned queueIndex;
	if (globalQueue){
		queueIndex = 0;
	} else {
		queueIndex = bankIndex;
	}

	RequestMap::iterator it = requests.find(request);
	myassert(it != requests.end());
	IMemoryCallback * callback = it->second;
	requests.erase(it);
	if (request->read){
		request->addr += offset;
		callback->accessCompleted(request, this);
	} else {
		delete request;
	}

	if (queueSizes[queueIndex] == maxQueueSize){
		myassert(stalled);
		stalled = false;
		for (set<IMemoryCallback *>::iterator it = stalledCallers.begin(); it != stalledCallers.end(); ++it){
			(*it)->unstall(this);
		}
		stalledCallers.clear();
		queueStallTime += (timestamp - stallStartTimestamp);
	}

	myassert(queueSizes[queueIndex] > 0);
	queueSizes[queueIndex]--;
}


CacheMemory::CacheMemory(
		const string& nameArg,
		const string& descArg,
		Engine *engineArg,
		StatContainer *statCont,
		uint64 debugStartArg,
		Memory *dramArg,
		Memory *pcmArg,
		unsigned int blockSizeArg,
		unsigned int setAssocArg,
		CacheReplacementPolicy policyArg,
		unsigned pageSizeArg,
		uint64 penaltyArg,
		int maxQueueSizeArg) :
			name(nameArg),
			desc(descArg),
			engine(engineArg),
			debugStart(debugStartArg),
			dram(dramArg),
			pcm(pcmArg),
			cacheModel(nameArg, descArg, statCont, dram->getSize(), blockSizeArg, setAssocArg, policyArg, pageSizeArg),
			penalty(penaltyArg),
			maxQueueSize(maxQueueSizeArg),

			criticalTagAccessTime(statCont, nameArg + "_critical_tag_access_time", "Number of cycles " + descArg + " spends accessing the tag array for critical requests", 0),
			criticalStallTime(statCont, nameArg + "_critical_stall_time", "Number of cycles " + descArg + " stalls on critical requests", 0),
			criticalWaitTime(statCont, nameArg + "_critical_wait_time", "Number of cycles " + descArg + " waits on critical requests", 0),
			numWaitsOnData(statCont, nameArg + "_waits_on_data", "Number of " + descArg + " waits on data", 0),
			numWaitsOnWriteback(statCont, nameArg + "_waits_on_writeback", "Number of " + descArg + " waits on writeback", 0),

			readStallTime(statCont, nameArg + "_read_stall_time", "Number of cycles " + descArg + " stalls on reads", 0),
			writeStallTime(statCont, nameArg + "_write_stall_time", "Number of cycles " + descArg + " stalls on writes", 0),

			readQueueTime(statCont, nameArg + "_read_queue_time", "Number of cycles " + descArg + " read requests wait in the queue", 0),
			writeQueueTime(statCont, nameArg + "_write_queue_time","Number of cycles " + descArg + " write requests wait in the queue", 0),

			readTagAccessTime(statCont, nameArg + "_read_tag_access_time", "Number of cycles " + descArg + " spends accessing the tag array for read requests", 0),
			writeTagAccessTime(statCont, nameArg + "_write_tag_access_time", "Number of cycles " + descArg + " spends accessing the tag array for write requests", 0),

			dramCriticalReadAccessTime(statCont, nameArg + "_dram_critical_read_access_time", "Number of cycles of " + descArg + "DRAM read requests that are critical", 0),
			pcmCriticalReadAccessTime(statCont, nameArg + "_pcm_critical_read_access_time", "Number of cycles of " + descArg + "PCM read requests that are critical", 0),

			readRequestTime(statCont, nameArg + "_read_request_time", "Number of cycles of " + descArg + " read requests", 0),

			dramReadAccessTime(statCont, nameArg + "_dram_read_access_time", "Number of cycles of " + descArg + "DRAM read requests", 0),
			pcmReadAccessTime(statCont, nameArg + "_pcm_read_access_time", "Number of cycles of " + descArg + "PCM read requests", 0),

			queueStallTime(statCont, nameArg + "_queue_stall_time", "Number of cycles " + descArg + " queue is stalled", 0)

{
	if (dram->getBlockSize() != pcm->getBlockSize()) {
		error("DRAM and PCM blocks sizes are different");
	}
	smallBlockSize = dram->getBlockSize();
	numBlocks = cacheModel.getBlockSize() / smallBlockSize;
	queueSize = 0;

	eventTypeMask = 63;
	myassert(eventTypeMask < cacheModel.getBlockSize());
	myassert(EVENT_TYPE_SIZE - 1 <= eventTypeMask);

}

bool CacheMemory::access(MemoryRequest *request, IMemoryCallback *caller){

	uint64 timestamp = engine->getTimestamp();
	debug("(%p, %lu, %u, %s, %s, %d, %s)", request, request->addr, request->size, request->read?"read":"write", request->instr?"instr":"data", request->priority, caller->getName());
	addrint smallBlockAddr = dram->getBlockAddress(request->addr);
	addrint blockAddr = cacheModel.getBlockAddress(request->addr);
	addrint lastByteAddr = cacheModel.getBlockAddress(request->addr + request->size - 1);
	myassert(request->size == dram->getBlockSize());
	myassert(cacheModel.getBlockAddress(smallBlockAddr) == blockAddr);
	myassert(blockAddr == lastByteAddr);

	if (queueSize == maxQueueSize){
		stalledCallers.insert(caller);
		debug(": queue full (queueSize == %u)", queueSize);
		return false;
	}

	myassert(queueSize < maxQueueSize);
	queueSize++;


	bool found = false;
	for(InternalRequestMap::iterator it = internalRequests.begin(); it != internalRequests.end(); it++){
		if (it->second.result == CacheModel::MISS_WITH_WRITEBACK && blockAddr == it->second.evictedAddr){
			WaitQueueMap::iterator itWait = waitQueue.find(it->first);
			if (itWait == waitQueue.end()){
				itWait = waitQueue.emplace(it->first, RequestQueue()).first;
			}
			itWait->second.emplace(request);
			found = true;
			numWaitsOnWriteback++;
			debug("wait on writeback");
		}
	}

	if (!found){
		InternalRequestMap::iterator it = internalRequests.find(blockAddr);
		if (it == internalRequests.end()){
			it = internalRequests.emplace(blockAddr, InternalRequest(request, caller, START, timestamp)).first;
			it->second.result = cacheModel.access(blockAddr, request->read, request->instr, &it->second.evictedAddr, &it->second.internalAddr);
			it->second.smallBlockOffset = cacheModel.getBlockOffset(smallBlockAddr);
			myassert(smallBlockAddr == (blockAddr | it->second.smallBlockOffset));
			addEvent(penalty, blockAddr, TAG_ARRAY);
		} else {
			WaitQueueMap::iterator itWait = waitQueue.find(it->first);
			if (itWait == waitQueue.end()){
				itWait = waitQueue.emplace(it->first, RequestQueue()).first;
			}
			itWait->second.emplace(request);
			numWaitsOnData++;
			debug("wait on data");
		}
	}
	return true;
}

void CacheMemory::accessCompleted(MemoryRequest *request, IMemory *caller){
	uint64 timestamp = engine->getTimestamp();
	debug("(%p, %s)", request, caller->getName());
	myassert(request->read);

	InternalRequestMap::iterator it;
	if (caller == dram){
		OutgoingRequestMap::iterator outIt = dramRequests.find(request->addr);
		myassert(outIt != dramRequests.end());
		it = internalRequests.find(outIt->second.internalRequestAddr);
		myassert(it != internalRequests.end());
		bool done = false;
		it->second.dramReadNotificationsLeft--;
		if (it->second.dramReadNotificationsLeft == 0){
			done = true;
		}
		dramReadAccessTime += (engine->getTimestamp() - outIt->second.timestamp);
		dramRequests.erase(outIt);

		if (request->addr == (it->second.internalAddr | it->second.smallBlockOffset)){
			if (it->second.result == CacheModel::HIT){
				it->second.dataReadyTimestamp = engine->getTimestamp();
				request->addr = it->second.originalAddr;
				it->second.caller->accessCompleted(it->second.request, this);
			}
		}

		if (done){
			if (it->second.result == CacheModel::HIT){
				myassert(request->addr == (it->second.internalAddr | it->second.smallBlockOffset));
				it->second.state = DONE;
			} else if (it->second.result == CacheModel::MISS_WITHOUT_EVICTION || it->second.result == CacheModel::MISS_WITH_EVICTION){
				it->second.state = DONE;
			} else if (it->second.result == CacheModel::MISS_WITH_WRITEBACK){
				//DRAM
				if (it->second.state == START){
					it->second.state = READ_DRAM;
					accessPcmBlock(it->second.evictedAddr, 0, it->first, 0, false, false, false);
				} else if (it->second.state == READ_PCM){
					it->second.state = DONE;
					accessPcmBlock(it->second.evictedAddr, 0, it->first, 0, false, false, false);
					accessDramBlock(it->second.internalAddr, 0, it->first, false, false, true);
				} else {
					myassert(false);
				}
			} else if (it->second.result == CacheModel::MISS_WITHOUT_FREE_BLOCK){
				error("CacheModel::access() returned MISS_WITHOUT_FREE_BLOCK");
			} else {
				myassert(false);
			}
		}
	} else if (caller == pcm){
		OutgoingRequestMap::iterator outIt = pcmRequests.find(request->addr);
		myassert(outIt != pcmRequests.end());
		it = internalRequests.find(outIt->second.internalRequestAddr);
		myassert(it != internalRequests.end());
		bool done = false;
		it->second.pcmReadNotificationsLeft--;
		if (it->second.pcmReadNotificationsLeft == 0){
			done = true;
		}
		pcmReadAccessTime += (engine->getTimestamp() - outIt->second.timestamp);

		pcmRequests.erase(outIt);

		if (request->addr == (it->first | it->second.smallBlockOffset)){
			if (it->second.result == CacheModel::MISS_WITHOUT_EVICTION || it->second.result == CacheModel::MISS_WITH_EVICTION || it->second.result == CacheModel::MISS_WITH_WRITEBACK){
				it->second.dataReadyTimestamp = engine->getTimestamp();
				it->second.caller->accessCompleted(it->second.request, this);
			}
		}

		if (done){
			if (it->second.result == CacheModel::HIT){
				myassert(false);
			} else if (it->second.result == CacheModel::MISS_WITHOUT_EVICTION || it->second.result == CacheModel::MISS_WITH_EVICTION){
				it->second.state = DONE;
				accessDramBlock(it->second.internalAddr, 0, it->first, false, false, false);
			} else if (it->second.result == CacheModel::MISS_WITH_WRITEBACK){
				//PCM
				if (it->second.state == START){
					it->second.state = READ_PCM;
				} else if (it->second.state == READ_DRAM){
					it->second.state = DONE;
					accessDramBlock(it->second.internalAddr, 0, it->first, false, false, false);
				} else {
					myassert(false);
				}
			} else if (it->second.result == CacheModel::MISS_WITHOUT_FREE_BLOCK){
				error("CacheModel::access() returned MISS_WITHOUT_FREE_BLOCK");
			} else {
				myassert(false);
			}
		}
	} else {
		myassert(false);
	}

	if (it->second.state == DONE){
		//Calculate statistics for request
		if (it->second.request->read){
			readQueueTime += it->second.dequeueTimestamp - it->second.arrivalTimestamp;
			readTagAccessTime += it->second.tagAccessedTimestamp - it->second.dequeueTimestamp;
			if (it->second.result == CacheModel::HIT){
				dramCriticalReadAccessTime += it->second.dataReadyTimestamp - it->second.tagAccessedTimestamp;
			} else {
				pcmCriticalReadAccessTime += it->second.dataReadyTimestamp - it->second.tagAccessedTimestamp;
			}
			readRequestTime += engine->getTimestamp() - it->second.arrivalTimestamp;
		} else {
			writeQueueTime += it->second.dequeueTimestamp - it->second.arrivalTimestamp;
			writeTagAccessTime += it->second.tagAccessedTimestamp - it->second.dequeueTimestamp;
		}
		addrint smallBlockAddr = dram->getBlockAddress(request->addr);
		WaitQueueMap::iterator itWait = waitQueue.find(it->first);
		if (itWait != waitQueue.end()){
			//MemoryRequest *req = itWait->second.front();
			itWait->second.pop();
			if (itWait->second.empty()){
				waitQueue.erase(itWait);
			}
			it->second.result = CacheModel::HIT;
			it->second.smallBlockOffset = cacheModel.getBlockOffset(smallBlockAddr);
			//TODO: fix where to get arrivalTimestamp from
			it->second.arrivalTimestamp = engine->getTimestamp();
			it->second.dequeueTimestamp = engine->getTimestamp();
			it->second.tagAccessedTimestamp = engine->getTimestamp();
			addEvent(0, it->first, TAG_ARRAY);

		} else {
			if (!it->second.request->read){
				delete it->second.request;
			}
			internalRequests.erase(it);
			if (queueSize == maxQueueSize){
				for (set<IMemoryCallback *>::iterator imcit = stalledCallers.begin(); imcit != stalledCallers.end(); ++imcit){
					(*imcit)->unstall(this);
				}
				stalledCallers.clear();
			}
			queueSize--;
			myassert(queueSize >= 0);
		}
	}
}



void CacheMemory::process(const Event *event){
	uint64 timestamp = engine->getTimestamp();
	addrint data = event->getData();
	EventType eventType = static_cast<EventType>(data & eventTypeMask);
	if (eventType == TAG_ARRAY){
		addrint blockAddr = data & ~eventTypeMask;
		InternalRequestMap::iterator it = internalRequests.find(blockAddr);
		myassert(it != internalRequests.end());
		if (it->second.result == CacheModel::HIT){
			it->second.originalAddr = it->second.request->addr;
			it->second.request->addr = it->second.internalAddr | it->second.smallBlockOffset;

			accessDram(it->second.request, blockAddr, false);

			if (it->second.request->read){
				it->second.dramReadNotificationsLeft = 1;
			}
		} else if (it->second.result == CacheModel::MISS_WITHOUT_EVICTION || it->second.result == CacheModel::MISS_WITH_EVICTION){
			it->second.pcmReadNotificationsLeft = accessPcmBlock(blockAddr, it->second.smallBlockOffset, blockAddr, it->second.request, true, it->second.request->instr, false);
		} else if (it->second.result == CacheModel::MISS_WITH_WRITEBACK){
			it->second.pcmReadNotificationsLeft = accessPcmBlock(blockAddr, it->second.smallBlockOffset, blockAddr, it->second.request, true, it->second.request->instr, false);
			it->second.dramReadNotificationsLeft = accessDramBlock(it->second.internalAddr, it->second.smallBlockOffset, blockAddr, true, false, false);
		} else if (it->second.result == CacheModel::MISS_WITHOUT_FREE_BLOCK){
			error("CacheModel::access() returned MISS_WITHOUT_FREE_BLOCK");
		} else {
			myassert(false);
		}
		it->second.tagAccessedTimestamp = engine->getTimestamp();
	} else if (eventType == ACCESS){
		myassert(!delayedRequests.empty());
		for (DelayedRequestList::iterator it = delayedRequests.begin(); it != delayedRequests.end(); it++){
			if (it->memory == dram){
				if(!stalledDramRequests.empty() || !dram->access(it->request, this)){
					stalledDramRequests.emplace_back(it->request);
					it->request->counters[DRAM_CACHE_STALL] = timestamp;
				}
			} else if (it->memory == pcm){
				if(!stalledPcmRequests.empty() || !pcm->access(it->request, this)){
					stalledPcmRequests.emplace_back(it->request);
					it->request->counters[DRAM_CACHE_STALL] = timestamp;
				}
			}
			it->memory->access(it->request, this);
		}
		delayedRequests.clear();
	} else if (eventType == UNSTALL_DRAM){
		myassert(!stalledDramRequests.empty());
		StalledRequestList::iterator it = stalledDramRequests.begin();
		while(it != stalledDramRequests.end()){
			uint64 origStallTimestamp = (*it)->counters[DRAM_CACHE_STALL];
			(*it)->counters[DRAM_CACHE_STALL] = timestamp - (*it)->counters[DRAM_CACHE_STALL];
			if (dram->access(*it, this)){
				it = stalledDramRequests.erase(it);
			} else {
				(*it)->counters[DRAM_CACHE_STALL] = origStallTimestamp;
				break;
			}
		}
	} else if (eventType == UNSTALL_PCM){
		myassert(!stalledPcmRequests.empty());
		StalledRequestList::iterator it = stalledPcmRequests.begin();
		while(it != stalledPcmRequests.end()){
			uint64 origStallTimestamp = (*it)->counters[DRAM_CACHE_STALL];
			(*it)->counters[DRAM_CACHE_STALL] = timestamp - (*it)->counters[DRAM_CACHE_STALL];
			if (pcm->access(*it, this)){
				it = stalledPcmRequests.erase(it);
			} else {
				(*it)->counters[DRAM_CACHE_STALL] = origStallTimestamp;
				break;
			}
		}
	} else {
		myassert(false);
	}
}

void CacheMemory::unstall(IMemory *caller){
	uint64 timestamp = engine->getTimestamp();
	debug("(%s)", caller->getName());
	if (caller == dram){
		addEvent(0, 0, UNSTALL_DRAM);
	} else if (caller == pcm){
		addEvent(0, 0, UNSTALL_PCM);
	}
}

void CacheMemory::accessDram(MemoryRequest *request, addrint internalRequestAddr, bool delay){
	uint64 timestamp = engine->getTimestamp();
	bool ins = dramRequests.emplace(request->addr, OutgoingRequest(internalRequestAddr, timestamp)).second;
	myassert(ins);
	if(delay){
		if (delayedRequests.empty()){
			addEvent(0, 0, ACCESS);
		}
		delayedRequests.emplace_back(DelayedRequest(dram, request));
	} else {
		if(!stalledDramRequests.empty() || !dram->access(request, this)){
			stalledDramRequests.emplace_back(request);
			request->counters[DRAM_CACHE_STALL] = timestamp;
		}
	}
}

void CacheMemory::accessPcm(MemoryRequest *request, addrint internalRequestAddr, bool delay){
	uint64 timestamp = engine->getTimestamp();
	bool ins = pcmRequests.emplace(request->addr, OutgoingRequest(internalRequestAddr, engine->getTimestamp())).second;
	myassert(ins);
	if(delay){
		if (delayedRequests.empty()){
			addEvent(0, 0, ACCESS);
		}
		delayedRequests.emplace_back(DelayedRequest(pcm, request));
	} else {
		if(!stalledPcmRequests.empty() || !pcm->access(request, this)){
			stalledPcmRequests.emplace_back(request);
			request->counters[DRAM_CACHE_STALL] = timestamp;
		}
	}
}

unsigned CacheMemory::accessDramBlock(addrint addr, addrint startOffset, addrint internalRequestAddr, bool read, bool instr, bool delay){
	addrint block = startOffset;
	unsigned i = 0;
	while (i < numBlocks){
		MemoryRequest *req = new MemoryRequest(addr | block, smallBlockSize, read, instr, HIGH);
		accessDram(req, internalRequestAddr, delay);
		i++;
		block = (block + dram->getBlockSize()) % cacheModel.getBlockSize();
	}
	return i;
}

unsigned CacheMemory::accessPcmBlock(addrint addr, addrint startOffset, addrint internalRequestAddr, MemoryRequest *originalRequest, bool read, bool instr, bool delay){
	//cout << "read: " << read << " instr: " << instr << " record: " << record << " delay: " << delay << endl;
	addrint block = startOffset;
	unsigned i = 0;
	while (i < numBlocks){
		MemoryRequest *req;
		if (originalRequest != 0 && (addr|block) == originalRequest->addr) {
			myassert(read);
			myassert(originalRequest->read);
			req = originalRequest;
		} else {
			req = new MemoryRequest(addr | block, smallBlockSize, read, instr, HIGH);
		}
		accessPcm(req, internalRequestAddr, delay);
		i++;
		block = (block + pcm->getBlockSize()) % cacheModel.getBlockSize();
	}
	return i;
}
