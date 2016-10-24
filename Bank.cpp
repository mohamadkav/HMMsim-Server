/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#include "Bank.H"
#include "Memory.H"

#include <iomanip>

#include <cmath>


MemoryMapping::MemoryMapping(MappingType mappingTypeArg, unsigned numRanksArg, unsigned banksPerRankArg, unsigned rowsPerBankArg, unsigned blocksPerRowArg, unsigned blockSizeArg) {
	rankWidth = (unsigned)logb(numRanksArg);
	numRanks = 1 << rankWidth;
	bankWidth = (unsigned)logb(banksPerRankArg);
	banksPerRank = 1 << bankWidth;
	rowWidth = (unsigned)logb(rowsPerBankArg);
	rowsPerBank = 1 << rowWidth;
	columnWidth = (unsigned)logb(blocksPerRowArg);
	blocksPerRow = 1 << columnWidth;
	blockWidth = (unsigned)logb(blockSizeArg);
	blockSize = 1 << blockWidth;

	mappingType = mappingTypeArg;

	numBanks = numRanks * banksPerRank;
	totalSize = static_cast<uint64>(numBanks) * static_cast<uint64>(rowsPerBank) * static_cast<uint64>(blocksPerRow) * static_cast<uint64>(blockSize);


	//these 5 assignments depend on the address mapping used
	if (mappingType == ROW_RANK_BANK_COL){
		blockOffset = 0;
		columnOffset = blockOffset + blockWidth;
		bankOffset = columnOffset + columnWidth;
		rankOffset = bankOffset + bankWidth;
		rowOffset = rankOffset + rankWidth;
	} else if (mappingType == ROW_COL_RANK_BANK){
		blockOffset = 0;
		bankOffset = blockOffset + blockWidth;
		rankOffset = bankOffset + bankWidth;
		columnOffset = rankOffset + rankWidth;
		rowOffset = columnOffset + columnWidth;
	} else if (mappingType == RANK_BANK_ROW_COL){
		blockOffset = 0;
		columnOffset = blockOffset + blockWidth;
		rowOffset = columnOffset + columnWidth;
		bankOffset = rowOffset + rowWidth;
		rankOffset = bankOffset + bankWidth;
	} else {
		error("Invalid mapping type");
	}


	//These masks rely on the assumption that the bits to select the block, row, column, bank or rank are consecutive
	rankMask = 0;
	for (unsigned i = rankOffset; i < rankOffset+rankWidth; i++){
		rankMask |= (addrint)1U << i;
	}
	bankMask = 0;
	for (unsigned i = bankOffset; i < bankOffset+bankWidth; i++){
		bankMask |= (addrint)1U << i;
	}
	rowMask = 0;
	for (unsigned i = rowOffset; i < rowOffset+rowWidth; i++){
		rowMask |= (addrint)1U << i;
	}
	columnMask = 0;
	for (unsigned i = columnOffset; i < columnOffset+columnWidth; i++){
		columnMask |= (addrint)1U << i;
	}
	blockMask = 0;
	for (unsigned i = blockOffset; i < blockOffset+blockWidth; i++){
		blockMask |= (addrint)1U << i;
	}


//	cout << "numBanks: " << numBanks << endl;
//	cout << "totalSize: " << totalSize << endl;
//
//	cout << "rankWidth: " << rankWidth << endl;
//	cout << "rankOffset: " << rankOffset << endl;
//	cout << "rankMask: " << hex << rankMask << dec << endl;
//
//	cout << "bankWidth: " << bankWidth << endl;
//	cout << "bankOffset: " << bankOffset << endl;
//	cout << "bankMask: " << hex << bankMask << dec << endl;
//
//	cout << "rowWidth: " << rowWidth << endl;
//	cout << "rowOffset: " << rowOffset << endl;
//	cout << "rowMask: " << hex << rowMask << dec << endl;
//
//	cout << "columnWidth: " << columnWidth << endl;
//	cout << "columnOffset: " << columnOffset << endl;
//	cout << "columnMask: " << hex << columnMask << dec << endl;
//
//	cout << "blockWidth: " << blockWidth << endl;
//	cout << "blockOffset: " << blockOffset << endl;
//	cout << "blockMask: " << hex << blockMask << dec << endl;
}


Bank::Bank(
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
	bool longCloseLatencyArg) :
		name(nameArg),
		desc(descArg),
		engine(engineArg),
		debugStart(debugStartArg),
		queueCounterIndex(queueCounterIndexArg),
		openCounterIndex(openCounterIndexArg),
		accessCounterIndex(accessCounterIndexArg),
		closeCounterIndex(closeCounterIndexArg),
		busQueueCounterIndex(busQueueCounterIndexArg),
		busCounterIndex(busCounterIndexArg),
		policy(policyArg),
		firstReadyAcrossPriorities(false),
		type(typeArg),
		memory(memoryArg),
		bus(busArg),
		mapping(memoryArg->getMapping()),
		openLatency(openLatencyArg),
		closeLatency(closeLatencyArg),
		accessLatency(accessLatencyArg),
		longCloseLatency(longCloseLatencyArg),
		state(CLOSED),
		row(0),
		currentRequestValid(false),
		nextPipelineEvent(0),
		queueTime(statCont, nameArg + "_queue_time", "Number of cycles requests for " + descArg + " spend in the queue", 0),
		openTime(statCont, nameArg + "_open_time", "Number of cycles " + descArg + " spends opening rows for requests", 0),
		accessTime(statCont, nameArg + "_access_time", "Number of cycles " + descArg + " spends accessing rows for requests", 0),
		closeTime(statCont, nameArg + "_close_time", "Number of cycles " + descArg + " spends closing rows for requests", 0),
		numReadRequests(statCont, nameArg + "_read_requests", "Number of " + descArg + " read requests", 0),
		numWriteRequests(statCont, nameArg + "_write_requests", "Number of " + descArg + " write requests", 0),
		readQueueTime(statCont, nameArg + "_read_queue_time", "Number of cycles" + descArg + " read requests wait in the queue", 0),
		writeQueueTime(statCont, nameArg + "_write_queue_time", "Number of cycles" + descArg + " write requests wait in the queue", 0),
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
		waitHigherPriorityTime(statCont, nameArg + "_wait_higher_priority_time", "Number of cycles " + descArg + " requests wait for higher priority requests", 0){

	//debugStart = 120000000;
	//debugStart = 0;

}

bool Bank::access(MemoryRequest *request, IMemoryCallback *caller){
	uint64 timestamp = engine->getTimestamp();
	debug("(%p, %lu, %u, %s, %s, %d, %s)", request, request->addr, request->size, request->read?"read":"write", request->instr?"instr":"data", request->priority, caller->getName());

	bool found = false;
	for (Queue::iterator it = queue.begin(); it != queue.end(); it++){
		for (RequestList::iterator itReq = it->second.begin(); itReq != it->second.end(); itReq++){
			if (request->addr == itReq->request->addr){
				if (request->read && itReq->request->read){
					numRARs++;
				} else if (request->read && !itReq->request->read){
					numRAWs++;
					notify(request);
					found = true;
					break;
				} else if (!request->read && itReq->request->read){
					numWARs++;
				} else if (!request->read && !itReq->request->read){
					numWAWs++;
				} else {
					myassert(false);
				}
			}
		}
		if (found){
			break;
		}
	}
	if (!found){
		if ((state == CLOSED || state == OPEN_CLEAN || state == OPEN_DIRTY) && !currentRequestValid && queue.empty()){
			addEvent(0, BANK);

		}
		RequestAndTime reqTime(request, timestamp);
		if (currentRequestValid){
			if (currentRequest.request->priority < request->priority){
				reqTime.waitingOnHigherPriority = true;
			} else if (currentRequest.request->priority > request->priority){
				reqTime.waitingOnLowerPriority = true;
			} else {
				reqTime.waitingOnSamePriority = true;
			}
		}
		if ((state == OPEN_CLEAN || state == OPEN_DIRTY) && currentRequestValid && row == mapping->getRowIndex(request->addr) && nextPipelineEvent < timestamp){
			nextPipelineEvent = timestamp;
			addEvent(0, PIPELINE);
			debug(": \tadded PIPELINE event for %lu", nextPipelineEvent);

		}
		if (state == CLOSING && !currentRequestValid && queue.empty()){
			request->counters[closeCounterIndex] = timestamp;
		}
		queue[request->priority].emplace_back(reqTime);
		request->counters[queueCounterIndex] = timestamp;

	}
	return true;
}

void Bank::changeState(){
	uint64 timestamp = engine->getTimestamp();
	debug(": state: %d", state);
	if (state == CLOSED){
		selectNextRequest();
		state = OPENING;
		row = mapping->getRowIndex(currentRequest.request->addr);
		addEvent(openLatency, BANK);
		openTime += openLatency;
		numOpens++;
		currentRequest.request->counters[openCounterIndex] = timestamp;
	} else if (state == OPENING){
		if (currentRequest.request->read){
			state = OPEN_CLEAN;
			uint64 actualBusDelay = bus->schedule(accessLatency, this);
			nextPipelineEvent = timestamp + actualBusDelay - accessLatency + bus->getLatency();
			addEvent(actualBusDelay - accessLatency + bus->getLatency(), PIPELINE);
			debug(": \tadded PIPELINE event for %lu", nextPipelineEvent);
			currentRequest.request->counters[accessCounterIndex] = accessLatency;
			currentRequest.request->counters[busQueueCounterIndex] = actualBusDelay - accessLatency;
			currentRequest.request->counters[busCounterIndex] = bus->getLatency();
		} else {
			state = OPEN_DIRTY;
			dirtyColumns.set(mapping->getColumnIndex(currentRequest.request->addr));
			uint64 actualBusDelay = bus->schedule(0, this);
			currentRequest.request->counters[accessCounterIndex] = accessLatency;
			currentRequest.request->counters[busQueueCounterIndex] = actualBusDelay;
			currentRequest.request->counters[busCounterIndex] = bus->getLatency();
		}
		numAccesses++;
		currentRequest.request->counters[openCounterIndex] = timestamp - currentRequest.request->counters[openCounterIndex];
	} else if (state == OPEN_CLEAN){
		RequestAndTime oldRequest(currentRequest);
		bool oldRequestValid = currentRequestValid;
		currentRequestValid = false;
		if (pipelineRequests.empty()){
			selectNextRequest();
			if (currentRequestValid){
				if (row == mapping->getRowIndex(currentRequest.request->addr)){
					if (currentRequest.request->read){
						uint64 actualBusDelay = bus->schedule(accessLatency, this);
						nextPipelineEvent = timestamp + actualBusDelay - accessLatency + bus->getLatency();
						addEvent(actualBusDelay - accessLatency + bus->getLatency(), PIPELINE);
						debug(": \tadded PIPELINE event for %lu", nextPipelineEvent);
						currentRequest.request->counters[accessCounterIndex] = accessLatency;
						currentRequest.request->counters[busQueueCounterIndex] = actualBusDelay - accessLatency;
						currentRequest.request->counters[busCounterIndex] = bus->getLatency();
					} else {
						state = OPEN_DIRTY;
						dirtyColumns.set(mapping->getColumnIndex(currentRequest.request->addr));
						uint64 actualBusDelay = bus->schedule(0, this);
						currentRequest.request->counters[accessCounterIndex] = accessLatency;
						currentRequest.request->counters[busQueueCounterIndex] = actualBusDelay;
						currentRequest.request->counters[busCounterIndex] = bus->getLatency();
					}
					numAccesses++;
				} else {
					if (type == DESTRUCTIVE_READS) {
						state = CLOSING;
						addEvent(closeLatency, BANK);
						closeTime += closeLatency;
						numCloses++;
						currentRequest.request->counters[closeCounterIndex] = timestamp;
					} else if (type == NON_DESTRUCTIVE_READS) {
						state = OPENING;
						row = mapping->getRowIndex(currentRequest.request->addr);
						addEvent(openLatency, BANK);
						openTime += openLatency;
						numOpens++;
						currentRequest.request->counters[openCounterIndex] = timestamp;
					} else {
						error("Invalid memory type;")
					}
				}
			} else {
				if (policy == OPEN_PAGE){

				} else if (policy == CLOSED_PAGE){
					if (type == DESTRUCTIVE_READS) {
						state = CLOSING;
						addEvent(closeLatency, BANK);
						numCloses++;
					} else if (type == NON_DESTRUCTIVE_READS) {
						//state = CLOSED; keep buffer open if it's clean
					} else {
						error("Invalid memory type;")
					}
				} else {
					error("Invalid row buffer policy");
				}
			}
		} else {
			currentRequestValid = true;
			currentRequest = pipelineRequests.front();
			pipelineRequests.pop_front();
		}
		if (oldRequestValid){
			if (oldRequest.request->read) {
				numReadRequests++;
				readQueueTime += (oldRequest.dequeueTimestamp - oldRequest.enqueueTimestamp);
				readTotalTime += (timestamp - oldRequest.enqueueTimestamp);
			} else {
				numWriteRequests++;
				writeQueueTime += (oldRequest.dequeueTimestamp - oldRequest.enqueueTimestamp);
				writeTotalTime += (timestamp - oldRequest.enqueueTimestamp);
			}
			queueTime += (oldRequest.dequeueTimestamp - oldRequest.enqueueTimestamp);
			memory->accessCompleted(oldRequest.request, this);
		}
	} else if (state == OPEN_DIRTY){
		RequestAndTime oldRequest(currentRequest);
		bool oldRequestValid = currentRequestValid;
		currentRequestValid = false;
		if (pipelineRequests.empty()){
			selectNextRequest();
			if (currentRequestValid){
				if (row == mapping->getRowIndex(currentRequest.request->addr)){
					if (currentRequest.request->read){
						uint64 actualBusDelay = bus->schedule(accessLatency, this);
						nextPipelineEvent = timestamp + actualBusDelay - accessLatency + bus->getLatency();
						addEvent(actualBusDelay - accessLatency + bus->getLatency(), PIPELINE);
						debug(": \tadded PIPELINE event for %lu", nextPipelineEvent);
						currentRequest.request->counters[accessCounterIndex] = accessLatency;
						currentRequest.request->counters[busQueueCounterIndex] = actualBusDelay - accessLatency;
						currentRequest.request->counters[busCounterIndex] = bus->getLatency();
					} else {
						dirtyColumns.set(mapping->getColumnIndex(currentRequest.request->addr));
						uint64 actualBusDelay = bus->schedule(0, this);
						currentRequest.request->counters[accessCounterIndex] = accessLatency;
						currentRequest.request->counters[busQueueCounterIndex] = actualBusDelay;
						currentRequest.request->counters[busCounterIndex] = bus->getLatency();
					}
					numAccesses++;
				} else {
					state = CLOSING;
					uint64 latency;
					if (longCloseLatency){
						latency = closeLatency * dirtyColumns.count();
					} else {
						latency = closeLatency;
					}
					debug(": close latency: %lu", latency);
					dirtyColumns.reset();
					addEvent(latency, BANK);
					closeTime += latency;
					numCloses++;
					currentRequest.request->counters[closeCounterIndex] = timestamp;
				}
			} else {
				if (policy == OPEN_PAGE){

				} else if (policy == CLOSED_PAGE){
					state = CLOSING;
					uint64 latency;
					if (longCloseLatency){
						latency = closeLatency * dirtyColumns.count();
					} else {
						latency = closeLatency;
					}
					dirtyColumns.reset();
					debug(": close latency: %lu", latency);
					addEvent(latency, BANK);
					numCloses++;
				} else {
					error("Invalid row buffer policy");
				}
			}
		} else {
			currentRequestValid = true;
			currentRequest = pipelineRequests.front();
			pipelineRequests.pop_front();
		}
		if (oldRequestValid){
			if (oldRequest.request->read) {
				numReadRequests++;
				readQueueTime += (oldRequest.dequeueTimestamp - oldRequest.enqueueTimestamp);
				readTotalTime += (timestamp - oldRequest.enqueueTimestamp);
			} else {
				numWriteRequests++;
				writeQueueTime += (oldRequest.dequeueTimestamp - oldRequest.enqueueTimestamp);
				writeTotalTime += (timestamp - oldRequest.enqueueTimestamp);
			}
			queueTime += (oldRequest.dequeueTimestamp - oldRequest.enqueueTimestamp);
			memory->accessCompleted(oldRequest.request, this);
		}
	} else if (state == CLOSING){
		if (currentRequestValid){
			state = OPENING;
			row = mapping->getRowIndex(currentRequest.request->addr);
			addEvent(openLatency, BANK);
			openTime += openLatency;
			numOpens++;
			currentRequest.request->counters[closeCounterIndex] = timestamp - currentRequest.request->counters[closeCounterIndex];
			currentRequest.request->counters[openCounterIndex] = timestamp;
		} else {
			selectNextRequest();
			if (currentRequestValid){
				state = OPENING;
				row = mapping->getRowIndex(currentRequest.request->addr);
				addEvent(openLatency, BANK);
				openTime += openLatency;
				numOpens++;
				currentRequest.request->counters[closeCounterIndex] = currentRequest.request->counters[queueCounterIndex];
				currentRequest.request->counters[queueCounterIndex] = 0;
				currentRequest.request->counters[openCounterIndex] = timestamp;
			} else {
				state = CLOSED;
			}
		}
	} else {
		error("Wrong bank state");
	}
}

void Bank::process(const Event *event) {
	uint64 timestamp = engine->getTimestamp();
	EventType eventType = static_cast<EventType>(event->getData());
	debug("(%d): state: %d", eventType, state);
	if (eventType == BANK){
		if (state == CLOSED || state == CLOSING){
			changeState();
		} else if (state == OPENING) {
			myassert(currentRequestValid);
			changeState();
		} else if (state == OPEN_CLEAN || state == OPEN_DIRTY){
			if (currentRequestValid){
				if(!currentRequest.request->read){
					changeState();
				} else {
					myassert(false);
				}
			} else {
				changeState();
			}
		} else {
			myassert(false);
		}

	} else if (eventType == QUEUE){
		myassert(!notifications.empty());
		for (list<MemoryRequest *>::iterator it = notifications.begin(); it != notifications.end(); it++){
			memory->accessCompleted(*it, this);
		}
		notifications.clear();
	} else if (eventType == PIPELINE){
		if ((state == OPEN_CLEAN || state == OPEN_DIRTY) && currentRequestValid && nextPipelineEvent == timestamp){
			bool found = false;
			bool defer = false;
			RequestAndTime prevRequest;
			if (pipelineRequests.empty()){
				prevRequest = currentRequest;
			} else {
				prevRequest = pipelineRequests.back();
			}
			Queue::iterator itQueue = queue.lower_bound(0);
			if (itQueue != queue.end()){
				for (list<RequestAndTime>::iterator it = itQueue->second.begin(); it != itQueue->second.end(); it++){
					if (mapping->getRowIndex(it->request->addr) == row){
						if (prevRequest.request->read == it->request->read){
							//either read-read or write-write
							pipelineRequests.emplace_back(*it);
							itQueue->second.erase(it);
							if (itQueue->second.empty()){
								queue.erase(itQueue);
							}
							found = true;
							rowBufferHits++;
							break;
						} else {
							if (it->request->read){
								//write-read
								//try again later
								//defer = true;
							} else {
								//read-write
							}
						}
					}
				}
				if (!found){
					if (firstReadyAcrossPriorities){
						for (Queue::iterator itQueueAll = queue.begin(); itQueueAll != queue.end(); itQueueAll++){
							if (itQueueAll != itQueue){
								for (list<RequestAndTime>::iterator it = itQueueAll->second.begin(); it != itQueueAll->second.end(); it++){
									if (mapping->getRowIndex(it->request->addr) == row){
										if (prevRequest.request->read == it->request->read){
											pipelineRequests.emplace_back(*it);
											itQueueAll->second.erase(it);
											if (itQueueAll->second.empty()){
												queue.erase(itQueueAll);
											}
											found = true;
											rowBufferHits++;
											break;
										} else {
											if (it->request->read){
												//write-read
												//defer = true;
											} else {
												//read-write
											}
										}
									}
								}
							}
						}
					}
				}
				if (found){
					myassert(row ==  mapping->getRowIndex(pipelineRequests.back().request->addr));
					if (pipelineRequests.back().request->read){
							uint64 actualBusDelay = bus->schedule(accessLatency, this);
							nextPipelineEvent = timestamp + actualBusDelay - accessLatency + bus->getLatency();
							addEvent(actualBusDelay - accessLatency + bus->getLatency(), PIPELINE);
							debug("\tadded PIPELINE event for %lu", nextPipelineEvent);
							pipelineRequests.back().request->counters[accessCounterIndex] = accessLatency;
							pipelineRequests.back().request->counters[busQueueCounterIndex] = actualBusDelay - accessLatency;
							pipelineRequests.back().request->counters[busCounterIndex] = bus->getLatency();
					} else {
						state = OPEN_DIRTY;
						dirtyColumns.set(mapping->getColumnIndex(pipelineRequests.back().request->addr));
						uint64 actualBusDelay = bus->schedule(0, this);
						pipelineRequests.back().request->counters[accessCounterIndex] = accessLatency;
						pipelineRequests.back().request->counters[busQueueCounterIndex] = actualBusDelay;
						pipelineRequests.back().request->counters[busCounterIndex] = bus->getLatency();
					}
					numAccesses++;
					debug("\tcurrent request addr: %lu", pipelineRequests.back().request->addr);
					pipelineRequests.back().request->counters[queueCounterIndex] = timestamp - pipelineRequests.back().request->counters[queueCounterIndex];
				} else {
					if (defer){
						nextPipelineEvent = timestamp + bus->getLatency();
						addEvent(bus->getLatency(), PIPELINE);
					}
				}
			}
		}
	} else {
		myassert(false);
	}
}

void Bank::transferCompleted(){
	uint64 timestamp = engine->getTimestamp();
	debug("()");
	myassert(currentRequestValid);
	if (currentRequest.request->read){
		changeState();
	} else {
		addEvent(accessLatency, BANK);
		accessTime += accessLatency;
		nextPipelineEvent = timestamp;
		addEvent(0, PIPELINE);
		debug("\tadded PIPELINE event for %lu", nextPipelineEvent);
	}
}

/*
 * Assumes current request is not valid
 */
void Bank::selectNextRequest(){
	uint64 timestamp = engine->getTimestamp();
	debug("()");
	myassert (!currentRequestValid);
	for (Queue::iterator itQueue = queue.begin(); itQueue != queue.end(); itQueue++){
		for (list<RequestAndTime>::iterator it = itQueue->second.begin(); it != itQueue->second.end(); it++){
			if (it->waitingOnLowerPriority){
				waitLowerPriorityTime += timestamp - it->startWaitingTimestamp;
			}
			if (it->waitingOnSamePriority){
				waitSamePriorityTime += timestamp - it->startWaitingTimestamp;
			}
			if (it->waitingOnHigherPriority){
				waitHigherPriorityTime += timestamp - it->startWaitingTimestamp;
			}
			it->waitingOnSamePriority = it->waitingOnHigherPriority = it->waitingOnLowerPriority = false;
		}
	}

	if(!queue.empty()) {
		Queue::iterator itQueue = queue.lower_bound(0);
		myassert(itQueue != queue.end());
		if (state == CLOSED || state == CLOSING){
			currentRequest = itQueue->second.front();
			itQueue->second.pop_front();
			rowBufferMisses++;
		} else if (state == OPENING){
			error("Bank should not be opening when selecting new request");
		} else if (state == OPEN_CLEAN || state == OPEN_DIRTY){
			bool found = false;
			for (list<RequestAndTime>::iterator it = itQueue->second.begin(); it != itQueue->second.end(); it++){
				if (mapping->getRowIndex(it->request->addr) == row){
					currentRequest = *it;
					itQueue->second.erase(it);
					found = true;
					rowBufferHits++;
					break;
				}
			}
			if (!found){
				if (firstReadyAcrossPriorities){
					for (Queue::iterator itQueueAll = queue.begin(); itQueueAll != queue.end(); itQueueAll++){
						if (itQueueAll != itQueue){
							for (list<RequestAndTime>::iterator it = itQueueAll->second.begin(); it != itQueueAll->second.end(); it++){
								if (mapping->getRowIndex(it->request->addr) == row){
									currentRequest = *it;
									itQueueAll->second.erase(it);
									if (itQueueAll->second.empty()){
										queue.erase(itQueueAll);
									}
									found = true;
									rowBufferHits++;
									break;
								}
							}
						}
					}
				}
				if (!found){
					currentRequest = itQueue->second.front();
					itQueue->second.pop_front();
					rowBufferMisses++;
				}
			}
			currentRequest.request->counters[accessCounterIndex] = timestamp;
		} else {
			error("Invalid bank state");
		}
		if (itQueue->second.empty()){
			queue.erase(itQueue);
		}

		for (Queue::iterator itQueueAll = queue.begin(); itQueueAll != queue.end(); itQueueAll++){
			for (list<RequestAndTime>::iterator it = itQueueAll->second.begin(); it != itQueueAll->second.end(); it++){
				if (currentRequest.request->priority < it->request->priority){
					it->waitingOnHigherPriority = true;
				} else if (currentRequest.request->priority > it->request->priority){
					it->waitingOnLowerPriority = true;
				} else {
					it->waitingOnSamePriority = true;
				}
				it->startWaitingTimestamp = timestamp;
			}
		}

		currentRequest.dequeueTimestamp = timestamp;
		currentRequest.request->counters[queueCounterIndex] = timestamp - currentRequest.request->counters[queueCounterIndex];
		currentRequestValid = true;
	}


	if (currentRequestValid){
		debug("\tcurrent request addr: %lu", currentRequest.request->addr);
	} else {
		debug(": \tcurrent request invalid");
	}
}

void Bank::notify(MemoryRequest * request) {
	myassert(request->read);
	if (notifications.size() == 0){
		addEvent(0, QUEUE);
	}
	notifications.emplace_back(request);
}


istream& operator>>(istream& lhs, RowBufferPolicy& rhs){
	string s;
	lhs >> s;
	if (s == "open_page"){
		rhs = OPEN_PAGE;
	} else if (s == "closed_page"){
		rhs = CLOSED_PAGE;
	} else {
		error("Invalid row buffer policy: %s", s.c_str());
	}
	return lhs;
}

ostream& operator<<(ostream& lhs, RowBufferPolicy rhs){
	if(rhs == OPEN_PAGE){
		lhs << "open_page";
	} else if(rhs == CLOSED_PAGE){
		lhs << "closed_page";
	} else {
		error("Invalid row buffer policy");
	}
	return lhs;
}

istream& operator>>(istream& lhs, MappingType& rhs){
	string s;
	lhs >> s;
	if (s == "row_rank_bank_col"){
		rhs = ROW_RANK_BANK_COL;
	} else if (s == "row_col_rank_bank"){
		rhs = ROW_COL_RANK_BANK;
	} else if (s == "rank_bank_row_col"){
		rhs = RANK_BANK_ROW_COL;
	} else {
		error("Invalid mapping type: %s", s.c_str());
	}
	return lhs;
}

ostream& operator<<(ostream& lhs, MappingType rhs){
	if(rhs == ROW_RANK_BANK_COL){
		lhs << "row_rank_bank_col";
	} else if(rhs == ROW_COL_RANK_BANK){
		lhs << "row_col_rank_bank";
	} else if(rhs == RANK_BANK_ROW_COL){
		lhs << "rank_bank_row_col";
	} else {
		error("Invalid mapping type");
	}
	return lhs;
}
