/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#include "HybridMemory.H"

#include <iomanip>
#include <iostream>
#include <cmath>

HybridMemory::HybridMemory(
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
	uint64 pcmMigrationCostArg) :
		name(nameArg),
		desc(descArg),
		engine(engineArg),
		debugStart(debugStartArg),
		numProcesses(numProcessesArg),
		dram(dramArg),
		pcm(pcmArg),
		manager(0),
		blockSize(1 << static_cast<unsigned>(logb(blockSizeArg))),
		pageSize(1 << static_cast<unsigned>(logb(pageSizeArg))),
		blocksPerPage(pageSize/blockSize),
		dramMigrationReadDelay(dramMigrationReadDelayArg),
		dramMigrationWriteDelay(dramMigrationWriteDelayArg),
		pcmMigrationReadDelay(pcmMigrationReadDelayArg),
		pcmMigrationWriteDelay(pcmMigrationWriteDelayArg),
		completionThreshold(completionThresholdArg),
		elideCleanDramBlocks(elideCleanDramBlocksArg),
		fixedPcmMigrationCost(fixedPcmMigrationCostArg),
		pcmMigrationCost(pcmMigrationCostArg),
		pcmOffset(dramArg->getSize()),

		dramReads(statCont, nameArg + "_dram_reads", "Number of DRAM reads seen by the " + descArg, 0),
		dramWrites(statCont, nameArg + "_dram_writes", "Number of DRAM writes seen by the " + descArg, 0),
		dramAccesses(statCont, nameArg + "_dram_accesses", "Number of DRAM accesses seen by the " + descArg, &dramReads, &dramWrites),

		pcmReads(statCont, nameArg + "_pcm_reads", "Number of PCM reads seen by the " + descArg, 0),
		pcmWrites(statCont, nameArg + "_pcm_writes", "Number of PCM writes seen by the " + descArg, 0),
		pcmAccesses(statCont, nameArg + "_pcm_accesses", "Number of PCM accesses seen by the " + descArg, &pcmReads, &pcmWrites),

		totalReads(statCont, nameArg + "_total_reads", "Number of total reads seen by the " + descArg, &dramReads, &pcmReads),
		totalWrites(statCont, nameArg + "_total_writes", "Number of total writes seen by the " + descArg, &dramWrites, &pcmWrites),
		totalAccesses(statCont, nameArg + "_total_accesses", "Number of total accesses seen by the " + descArg, &dramAccesses, &pcmAccesses),

		dramReadFraction(statCont, nameArg + "_fraction_dram_reads", "Fraction of DRAM reads seen by the " + descArg, &dramReads, &totalReads),
		pcmReadFraction(statCont, nameArg + "_fraction_pcm_reads", "Fraction of PCM reads seen by the " + descArg, &pcmReads, &totalReads),

		dramWriteFraction(statCont, nameArg + "_fraction_dram_writes", "Fraction of DRAM writes seen by the " + descArg, &dramWrites, &totalWrites),
		pcmWriteFraction(statCont, nameArg + "_fraction_pcm_writes", "Fraction of PCM writes seen by the " + descArg, &pcmWrites, &totalWrites),

		dramAccessFraction(statCont, nameArg + "_fraction_dram_accesses", "Fraction of DRAM accesses seen by the " + descArg, &dramAccesses, &totalAccesses),
		pcmAccessFraction(statCont, nameArg + "_fraction_pcm_accesses", "Fraction of PCM accesses seen by the " + descArg, &pcmAccesses, &totalAccesses),

		readsFromDram(statCont, nameArg + "_reads_from_dram", "Number of reads to the " + descArg + " served by DRAM", 0),
		readsFromPcm(statCont, nameArg + "_reads_from_pcm", "Number of reads to the " + descArg + " served by PCM", 0),
		readsFromBuffer(statCont, nameArg + "_reads_from_buffer", "Number of reads to the " + descArg + " served by the buffer", 0),

		writesToDram(statCont, nameArg + "_writes_to_dram", "Number of writes to the " + descArg + " served by DRAM", 0),
		writesToPcm(statCont, nameArg + "_writes_to_pcm", "Number of writes to the " + descArg + " served by PCM", 0),
		writesToBuffer(statCont, nameArg + "_writes_to_buffer", "Number of writes to the " + descArg + " served by the buffer", 0),

		dramReadTime(statCont, nameArg + "_dram_read_time", "Number of cycles servicing DRAM reads as seen by the " + descArg, 0),
		dramWriteTime(statCont, nameArg + "_dram_write_time", "Number of cycles servicing DRAM writes as seen by the " + descArg, 0),
		dramAccessTime(statCont, nameArg + "_dram_access_time", "Number of cycles servicing DRAM accesses as seen by the " + descArg, &dramReadTime, &dramWriteTime),

		pcmReadTime(statCont, nameArg + "_pcm_read_time", "Number of cycles servicing PCM reads as seen by the " + descArg, 0),
		pcmWriteTime(statCont, nameArg + "_pcm_write_time", "Number of cycles servicing PCM writes as seen by the " + descArg, 0),
		pcmAccessTime(statCont, nameArg + "_pcm_access_time", "Number of cycles servicing PCM accesses as seen by the " + descArg, &pcmReadTime, &pcmWriteTime),

		totalAccessTime(statCont, nameArg + "_total_access_time", "Number of cycles servicing all accesses as seen by the " + descArg, &dramAccessTime, &pcmAccessTime),

		avgDramReadTime(statCont, nameArg + "_avg_dram_read_time", "Average number of cycles servicing DRAM reads as seen by the " + descArg, &dramReadTime, &dramReads),
		avgDramWriteTime(statCont, nameArg + "_avg_dram_write_time", "Average number of cycles servicing DRAM writes as seen by the " + descArg, &dramWriteTime, &dramWrites),
		avgDramAccessTime(statCont, nameArg + "_avg_dram_access_time", "Average number of cycles servicing DRAM accesses as seen by the " + descArg, &dramAccessTime, &dramAccesses),

		avgPcmReadTime(statCont, nameArg + "_avg_pcm_read_time", "Average number of cycles servicing PCM reads as seen by the " + descArg, &pcmReadTime, &pcmReads),
		avgPcmWriteTime(statCont, nameArg + "_avg_pcm_write_time", "Average number of cycles servicing PCM writes as seen by the " + descArg, &pcmWriteTime, &pcmWrites),
		avgPcmAccessTime(statCont, nameArg + "_avg_pcm_access_time", "Average number of cycles servicing PCM accesses as seen by the " + descArg, &pcmAccessTime, &pcmAccesses),

		avgAccessTime(statCont, nameArg + "_avg_access_time", "Average number of cycles servicing all accesses as seen by the " + descArg, &totalAccessTime, &totalAccesses),

		dramCopyReads(statCont, nameArg + "_dram_copy_reads", "Number of DRAM reads due to page copies by the " + descArg, 0),
		dramCopyWrites(statCont, nameArg + "_dram_copy_writes", "Number of DRAM writes due to page copies by the " + descArg, 0),
		dramCopyAccesses(statCont, nameArg + "_dram_copy_accesses", "Number of DRAM accesses due to page copies by the " + descArg, &dramCopyReads, &dramCopyWrites),

		pcmCopyReads(statCont, nameArg + "_pcm_copy_reads", "Number of PCM reads due to page copies by the " + descArg, 0),
		pcmCopyWrites(statCont, nameArg + "_pcm_copy_writes", "Number of PCM writes due to page copies by the " + descArg, 0),
		pcmCopyAccesses(statCont, nameArg + "_pcm_copy_accesses", "Number of PCM accesses due to page copies by the " + descArg, &pcmCopyReads, &pcmCopyWrites),

		totalCopyAccesses(statCont, nameArg + "_total_copy_accesses", "Number of total accesses due to page copies by the " + descArg, &dramCopyAccesses, &pcmCopyAccesses),

		dramCopyReadTime(statCont, nameArg + "_dram_copy_read_time", "Number of cycles servicing DRAM reads due to page copies by the " + descArg, 0),
		dramCopyWriteTime(statCont, nameArg + "_dram_copy_write_time", "Number of cycles servicing DRAM writes due to page copies by the " + descArg, 0),
		dramCopyAccessTime(statCont, nameArg + "_dram_copy_access_time", "Number of cycles servicing DRAM accesses due to page copies by the " + descArg, &dramCopyReadTime, &dramCopyWriteTime),

		pcmCopyReadTime(statCont, nameArg + "_pcm_copy_read_time", "Number of cycles servicing PCM reads due to page copies by the " + descArg, 0),
		pcmCopyWriteTime(statCont, nameArg + "_pcm_copy_write_time", "Number of cycles servicing PCM writes due to page copies by the " + descArg, 0),
		pcmCopyAccessTime(statCont, nameArg + "_pcm_copy_access_time", "Number of cycles servicing PCM accesses due to page copies by the " + descArg, &pcmCopyReadTime, &pcmCopyWriteTime),

		totalCopyAccessTime(statCont, nameArg + "_total_copy_access_time", "Number of cycles servicing all accesses due to page copies by the " + descArg, &dramCopyAccessTime, &pcmCopyAccessTime),

		avgCopyDramReadTime(statCont, nameArg + "_avg_dram_copy_read_time", "Average number of cycles servicing DRAM reads due to page copies by the " + descArg, &dramCopyReadTime, &dramCopyReads),
		avgCopyDramWriteTime(statCont, nameArg + "_avg_dram_copy_write_time", "Average number of cycles servicing DRAM writes due to page copies by the " + descArg, &dramCopyWriteTime, &dramCopyWrites),
		avgCopyDramAccessTime(statCont, nameArg + "_avg_dram_copy_access_time", "Average number of cycles servicing DRAM accesses due to page copies by the " + descArg, &dramCopyAccessTime, &dramCopyAccesses),

		avgCopyPcmReadTime(statCont, nameArg + "_avg_pcm_copy_read_time", "Average number of cycles servicing PCM reads due to page copies by the " + descArg, &pcmCopyReadTime, &pcmCopyReads),
		avgCopyPcmWriteTime(statCont, nameArg + "_avg_pcm_copy_write_time", "Average number of cycles servicing PCM writes due to page copies by the " + descArg, &pcmCopyWriteTime, &pcmCopyWrites),
		avgCopyPcmAccessTime(statCont, nameArg + "_avg_pcm_copy_access_time", "Average number of cycles servicing PCM accesses due to page copies by the " + descArg, &pcmCopyAccessTime, &pcmCopyAccesses),

		avgCopyAccessTime(statCont, nameArg + "_avg_access_copy_time", "Average number of cycles servicing all accesses due to page copies by the " + descArg, &totalCopyAccessTime, &totalCopyAccesses),


		dramPageCopies(statCont, nameArg + "_dram_page_copies", "Number of DRAM pages copied by " + descArg, 0),
		pcmPageCopies(statCont, nameArg + "_pcm_page_copies", "Number of PCM pages copied by " + descArg, 0),

		dramPageCopyTime(statCont, nameArg + "_dram_page_copy_time", "Number of cycles copying DRAM pages by " + descArg, 0),
		pcmPageCopyTime(statCont, nameArg + "_pcm_page_copy_time", "Number of cycles copying PCM pages by " + descArg, 0),

		dramReadsPerPid(statCont, numProcesses, nameArg + "_dram_reads_per_pid", "Number of DRAM reads seen by the " + descArg + " from process"),
		dramWritesPerPid(statCont, numProcesses, nameArg + "_dram_writes_per_pid", "Number of DRAM writes seen by the " + descArg + " from process"),
		dramAccessesPerPid(statCont, nameArg + "_dram_accesses_per_pid", "Number of DRAM accesses seen by the " + descArg + " from process", &dramReadsPerPid, &dramWritesPerPid),

		pcmReadsPerPid(statCont, numProcesses, nameArg + "_pcm_reads_per_pid", "Number of PCM reads seen by the " + descArg + " from process"),
		pcmWritesPerPid(statCont, numProcesses, nameArg + "_pcm_writes_per_pid", "Number of PCM writes seen by the " + descArg + " from process"),
		pcmAccessesPerPid(statCont, nameArg + "_pcm_accesses_per_pid", "Number of PCM accesses seen by the " + descArg + " from process", &pcmReadsPerPid, &pcmWritesPerPid),

		totalReadsPerPid(statCont, nameArg + "_total_reads_per_pid", "Number of total reads seen by the " + descArg + " from process", &dramReadsPerPid, &pcmReadsPerPid),
		totalWritesPerPid(statCont, nameArg + "_total_writes_per_pid", "Number of total writes seen by the " + descArg + " from process", &dramWritesPerPid, &pcmWritesPerPid),
		totalAccessesPerPid(statCont, nameArg + "_total_accesses_per_pid", "Number of total accesses seen by the " + descArg + " from process", &dramAccessesPerPid, &pcmAccessesPerPid),


		dramReadFractionPerPid(statCont, nameArg + "_fraction_dram_reads_per_pid", "Fraction of DRAM reads seen by the " + descArg + " from process", &dramReadsPerPid, &totalReadsPerPid),
		pcmReadFractionPerPid(statCont, nameArg + "_fraction_pcm_reads_per_pid", "Fraction of PCM reads seen by the " + descArg + " from process", &pcmReadsPerPid, &totalReadsPerPid),

		dramWriteFractionPerPid(statCont, nameArg + "_fraction_dram_writes_per_pid", "Fraction of DRAM writes seen by the " + descArg + " from process", &dramWritesPerPid, &totalWritesPerPid),
		pcmWriteFractionPerPid(statCont, nameArg + "_fraction_pcm_writes_per_pid", "Fraction of PCM writes seen by the " + descArg + " from process", &pcmWritesPerPid, &totalWritesPerPid),

		dramAccessFractionPerPid(statCont, nameArg + "_fraction_dram_accesses_per_pid", "Fraction of DRAM accesses seen by the " + descArg + " from process", &dramAccessesPerPid, &totalAccessesPerPid),
		pcmAccessFractionPerPid(statCont, nameArg + "_fraction_pcm_accesses_per_pid", "Fraction of PCM accesses seen by the " + descArg + " from process", &pcmAccessesPerPid, &totalAccessesPerPid),


		dramReadTimePerPid(statCont, numProcesses, nameArg + "_dram_read_time_per_pid", "Number of cycles servicing DRAM reads as seen by the " + descArg + " from process"),
		dramWriteTimePerPid(statCont, numProcesses, nameArg + "_dram_write_time_per_pid", "Number of cycles servicing DRAM writes as seen by the " + descArg + " from process"),
		dramAccessTimePerPid(statCont, nameArg + "_dram_access_time_per_pid", "Number of cycles servicing DRAM accesses as seen by the " + descArg + " from process", &dramReadTimePerPid, &dramWriteTimePerPid),

		pcmReadTimePerPid(statCont, numProcesses, nameArg + "_pcm_read_time_per_pid", "Number of cycles servicing DRAM reads as seen by the " + descArg + " from process"),
		pcmWriteTimePerPid(statCont, numProcesses, nameArg + "_pcm_write_time_per_pid", "Number of cycles servicing DRAM writes as seen by the " + descArg + " from process"),
		pcmAccessTimePerPid(statCont, nameArg + "_pcm_access_time_per_pid", "Number of cycles servicing DRAM accesses as seen by the " + descArg + " from process", &pcmReadTimePerPid, &pcmWriteTimePerPid),

		totalAccessTimePerPid(statCont, nameArg + "_total_access_time_per_pid", "Number of cycles servicing all accesses as seen by the " + descArg + " from process", &dramAccessTimePerPid, &pcmAccessTimePerPid),


		avgDramReadTimePerPid(statCont, nameArg + "_avg_dram_read_time_per_pid", "Average number of cycles servicing DRAM reads as seen by the " + descArg + " from process", &dramReadTimePerPid, &dramReadsPerPid),
		avgDramWriteTimePerPid(statCont, nameArg + "_avg_dram_write_time_per_pid", "Average number of cycles servicing DRAM writes as seen by the " + descArg + " from process", &dramWriteTimePerPid, &dramWritesPerPid),
		avgDramAccessTimePerPid(statCont, nameArg + "_avg_dram_access_time_per_pid", "Average number of cycles servicing DRAM accesses as seen by the " + descArg + " from process", &dramAccessTimePerPid, &dramAccessesPerPid),

		avgPcmReadTimePerPid(statCont, nameArg + "_avg_pcm_read_time_per_pid", "Average number of cycles servicing PCM reads as seen by the " + descArg + " from process", &pcmReadTimePerPid, &pcmReadsPerPid),
		avgPcmWriteTimePerPid(statCont, nameArg + "_avg_pcm_write_time_per_pid", "Average number of cycles servicing PCM writes as seen by the " + descArg + " from process", &pcmWriteTimePerPid, &pcmWritesPerPid),
		avgPcmAccessTimePerPid(statCont, nameArg + "_avg_pcm_access_time_per_pid", "Average number of cycles servicing PCM accesses as seen by the " + descArg + " from process", &pcmAccessTimePerPid, &pcmAccessesPerPid),

		avgAccessTimePerPid(statCont, nameArg + "_avg_access_time_per_pid", "Average number of cycles servicing all accesses as seen by the " + descArg + " from process", &totalAccessTimePerPid, &totalAccessesPerPid)
{

}

bool HybridMemory::access(MemoryRequest *request, IMemoryCallback *caller){
	uint64 timestamp = engine->getTimestamp();
	debug("(%p, %lu, %u, %s, %s, %d, %s)", request, request->addr, request->size, request->read?"read":"write", request->instr?"instr":"data", request->priority, caller->getName());

	addrint page = manager->getIndex(request->addr);
	addrint block = manager->getBlock(request->addr);

	addrint callbackAddr = request->addr; //addr might get overwritten

	PageType type;
	if (request->addr < pcmOffset){
		type = DRAM;
	} else {
		type = PCM;
	}
	bool read = request->read;
	int pid = manager->getPidOfAddress(request->addr);

	auto mit = migrations.find(page);
	if (mit != migrations.end()){
		debug(": state == %d", mit->second.blocks[block].state);
		addrint destPage;
		if (mit->second.rolledBack){
			destPage = mit->first;
		} else {
			destPage = mit->second.destPage;
		}
		if (mit->second.blocks[block].state == NOT_READ){
			myassert(mit->second.blocks[block].request == 0);
			if(request->read){
				if(accessNextLevel(request, caller, callbackAddr, true, page)){
					mit->second.blocks[block].state = READING;
					mit->second.blocks[block].request = request;
					mit->second.blocksLeftToRead--;
					if (mit->second.blocksLeftToRead == completionThreshold && mit->second.blocksLeftToRead > 0){
						auto bit = mit->second.blocks.begin();
						while (bit != mit->second.blocks.end() && bit->state != NOT_READ){
							++bit;
							mit->second.nextReadBlock++;
						}
						myassert(mit->second.nextReadBlock != static_cast<int>(block));
						addEvent(0, READ, mit->first);
					}
					if (type == DRAM){
						readsFromDram++;
					} else {
						readsFromPcm++;
					}
				} else {
					return false;
				}
			} else {
				request->addr = manager->getAddressFromBlock(destPage, block);
				mit->second.blocks[block].state = BUFFERED;
				mit->second.blocks[block].dirty = true;
				mit->second.blocks[block].request = request;
				mit->second.blocksLeftToRead--;
				if (mit->second.blocksLeftToRead == completionThreshold && mit->second.blocksLeftToRead > 0){
					auto bit = mit->second.blocks.begin();
					while (bit != mit->second.blocks.end() && bit->state != NOT_READ){
						++bit;
						mit->second.nextReadBlock++;
					}
					myassert(mit->second.nextReadBlock != static_cast<int>(block));
					addEvent(0, READ, mit->first);
				}
				if (mit->second.nextWriteBlock == -1){
					mit->second.nextWriteBlock = block;
					debug(": adding event 1: blocksLeftToWrite: %u", mit->second.blocksLeftToWrite);
					if (mit->second.lastWrite + mit->second.writeDelay < timestamp){
						addEvent(0, WRITE, mit->first);
					} else {
						addEvent(mit->second.lastWrite + mit->second.writeDelay - timestamp, WRITE, mit->first);
					}
				}
				writesToBuffer++;
			}
		} else if (mit->second.blocks[block].state == READING){
			if(request->read){
				mit->second.blocks[block].callers.emplace_back(request, caller);
				if (type == DRAM){
					readsFromDram++;
				} else {
					readsFromPcm++;
				}
			} else {
				mit->second.blocks[block].state = BUFFERED;
				mit->second.blocks[block].dirty = true;
				myassert(mit->second.blocks[block].request != 0);
				mit->second.blocks[block].request = request;
				mit->second.blocks[block].request->addr = manager->getAddressFromBlock(destPage, block);
				if (mit->second.nextWriteBlock == -1){
					mit->second.nextWriteBlock = block;
					debug(": adding event 2: blocksLeftToWrite: %u", mit->second.blocksLeftToWrite);
					if (mit->second.lastWrite + mit->second.writeDelay < timestamp){
						addEvent(0, WRITE, mit->first);
					} else {
						addEvent(mit->second.lastWrite + mit->second.writeDelay - timestamp, WRITE, mit->first);
					}
				}
				writesToBuffer++;
			}
		} else if (mit->second.blocks[block].state == BUFFERED){
			if(request->read){
				if (notifications.size() == 0){
					addEvent(0, NOTIFY);
				}
				notifications.emplace_back(request, caller);
				readsFromBuffer++;
			} else {
				mit->second.blocks[block].dirty = true;
				delete request;
				writesToBuffer++;
			}
		} else if (mit->second.blocks[block].state == WRITTEN){
			request->addr = manager->getAddressFromBlock(destPage, block);
			if(accessNextLevel(request, caller, callbackAddr, false, 0)){
				if (read){
					if (request->addr < pcmOffset){
						readsFromDram++;
					} else {
						readsFromPcm++;
					}
				} else {
					mit->second.blocks[block].dirty = true;
					if (request->addr < pcmOffset){
						writesToDram++;
					} else {
						writesToPcm++;
					}
				}
			} else {
				return false;
			}
		} else {
			myassert(false);
		}
	} else {
		addrint pcmPageOffset = manager->getIndex(pcmOffset);
		addrint destPage;
		if(page >= pcmPageOffset && caller != manager && manager->migrateOnDemand(page, &destPage)){
			myassert(destPage < pcmPageOffset);
			auto p = migrations.emplace(page, MigrationEntry(destPage, pcm, dram, dramMigrationReadDelay, dramMigrationWriteDelay, blocksPerPage, timestamp));
			myassert(p.second);
			//cout << "on demand: " << page << ", " << destPage << endl;
			debug(": %s(%lu) to %s(%lu)", pcm->getName(), manager->getAddressFromBlock(page, 0), dram->getName(), manager->getAddressFromBlock(destPage, 0));
			pcmPageCopies++;
			p.first->second.blocks.resize(blocksPerPage);

			if (request->read){
				if(accessNextLevel(request, caller, callbackAddr, true, page)){
					p.first->second.blocks[block].state = READING;
					p.first->second.blocks[block].request = request;
					p.first->second.blocksLeftToRead--;
					readsFromDram++;
				} else {
					return false;
				}
			} else {
				request->addr = manager->getAddressFromBlock(p.first->second.destPage, block);
				p.first->second.blocks[block].state = BUFFERED;
				p.first->second.blocks[block].dirty = true;
				p.first->second.blocks[block].request = request;
				p.first->second.blocksLeftToRead--;
				p.first->second.nextWriteBlock = block;
				debug(": adding event: blocksLeftToWrite: %u", p.first->second.blocksLeftToWrite);
				addEvent(0, WRITE, p.first->first);
				writesToBuffer++;
			}
			if (p.first->second.blocksLeftToRead == completionThreshold){
				auto bit = p.first->second.blocks.begin();
				while (bit != p.first->second.blocks.end() && bit->state != NOT_READ){
					++bit;
					p.first->second.nextReadBlock++;
				}
				myassert(p.first->second.nextReadBlock != static_cast<int>(block));
				addEvent(0, READ, p.first->first);
			}
		} else {
			if(accessNextLevel(request, caller, callbackAddr, false, 0)){
				if (read){
					if (type == DRAM){
						readsFromDram++;
					} else {
						readsFromPcm++;
					}
				} else {
					if (type == DRAM){
						writesToDram++;
					} else {
						writesToPcm++;
					}
				}
			} else {
				return false;
			}
		}
	}
	if (caller != manager){
		//ignore accesses that come from hybrid memory manager (these are due to flushes, which are not monitored)
		auto monit = monitors.find(page);
		if (monit == monitors.end()){
			monit = monitors.emplace(page, CountEntry(page)).first;
			monit->second.readBlocks.resize(blocksPerPage);
			monit->second.writtenBlocks.resize(blocksPerPage);
		}
		if (read){
			monit->second.reads++;
			monit->second.readBlocks[block]++;
		} else {
			monit->second.writes++;
			monit->second.writtenBlocks[block]++;
		}
	}
	if(type == DRAM){
		if (read){
			dramReads++;
			if (pid >= 0){
				dramReadsPerPid[pid]++;
			}
		} else {
			dramWrites++;
			if (pid >= 0){
				dramWritesPerPid[pid]++;
			}
		}
	} else {
		if (read){
			pcmReads++;
			if (pid >= 0){
				pcmReadsPerPid[pid]++;
			}
		} else {
			pcmWrites++;
			if (pid >= 0){
				pcmWritesPerPid[pid]++;
			}
		}
	}
	return true;
}

bool HybridMemory::accessNextLevel(MemoryRequest *request, IMemoryCallback *caller, addrint callbackAddr, bool partOfMigration, addrint srcPage){
	uint64 timestamp = engine->getTimestamp();
	if (request->addr < pcmOffset){
		if (dramStalledCallers.empty() && dram->access(request, this)){
			if (!request->read){
				addrint page = manager->getIndex(request->addr);
				addrint block = manager->getBlock(request->addr);
				auto dit = dirties.find(page);
				if (dit != dirties.end()){
					dit->second[block] = true;
				}
			}
		} else {
			debug(": stalled due to dram");
			dramStalledCallers.insert(caller);
			request->addr = callbackAddr;
			return false;
		}
	} else {
		if (!pcmStalledCallers.empty() || !pcm->access(request, this)){
			debug(": stalled due to pcm");
			pcmStalledCallers.insert(caller);
			request->addr = callbackAddr;
			return false;
		}
	}
	if (request->read){
		bool ins = callbacks.emplace(request, CallbackEntry(caller, callbackAddr, partOfMigration, srcPage, engine->getTimestamp())).second;
		myassert(ins);
	}
	return true;
}

void HybridMemory::accessCompleted(MemoryRequest *request, IMemory *caller){
	uint64 timestamp = engine->getTimestamp();

	debug("(%p, %lu, %u, %s, %s)", request, request->addr, request->size, request->read ? "read" : "write", caller->getName());
	bool partOfMigration = true;
	addrint block = manager->getBlock(request->addr);
	addrint page = manager->getIndex(request->addr);
	bool calledBack = false;
	auto it = callbacks.find(request);
	if (it != callbacks.end()){
		int pid = manager->getPidOfAddress(request->addr);
		uint64 accessTime = timestamp - it->second.startTime;
		if (caller == dram){
			if (request->read){
				dramReadTime += accessTime;
				if (pid >= 0){
					dramReadTimePerPid[pid] += accessTime;
				}
			} else {
				dramWriteTime += accessTime;
				if (pid >= 0){
					dramWriteTimePerPid[pid] += accessTime;
				}
			}
		} else if (caller == pcm){
			if (request->read){
				pcmReadTime += accessTime;
				if (pid >= 0){
					pcmReadTimePerPid[pid] += accessTime;
				}
			} else {
				pcmWriteTime += accessTime;
				if (pid >= 0){
					pcmWriteTimePerPid[pid] += accessTime;
				}
			}
		} else {
			myassert(false);
		}
		IMemoryCallback *callback = it->second.callback;
		request->addr = it->second.callbackAddr;
		partOfMigration = it->second.partOfMigration;
		if (partOfMigration){
			page = it->second.page;
		}
		callbacks.erase(it);
		callback->accessCompleted(request, this);
		calledBack = true;
	}
	if (partOfMigration){
		addrint migPage = page;
		auto rit = rolledBackMigrations.find(page);
		if (rit != rolledBackMigrations.end()){
			debug(": page present in rolledBackMigrations");
			migPage = rit->second;
		}
		auto mit = migrations.find(migPage);
		myassert(mit != migrations.end());
		if (mit->second.blocks[block].state == NOT_READ){
			myassert(false);
		} else if(mit->second.blocks[block].state == READING){
			myassert(caller == mit->second.src);
			mit->second.blocks[block].state = BUFFERED;
			if (calledBack){
				mit->second.blocks[block].request = 0;
			}
			if (mit->second.nextWriteBlock == -1){
				mit->second.nextWriteBlock = block;
				debug(": adding event: blocksLeftToWrite: %u", mit->second.blocksLeftToWrite);
				if (mit->second.lastWrite + mit->second.writeDelay < timestamp){
					addEvent(0, WRITE, mit->first);
				} else {
					addEvent(mit->second.lastWrite + mit->second.writeDelay - timestamp, WRITE, mit->first);
				}
			}
			for (auto cit = mit->second.blocks[block].callers.begin(); cit != mit->second.blocks[block].callers.end(); ++cit){
				cit->callback->accessCompleted(cit->request, this);
			}
			mit->second.blocks[block].callers.clear();
			if (mit->second.src == dram){
				dramCopyReads++;
				dramCopyReadTime += (timestamp - mit->second.startPageCopyTime);
			} else if (mit->second.src == pcm){
				pcmCopyReads++;
				pcmCopyReadTime += (timestamp - mit->second.startPageCopyTime);
			} else {
				myassert(false);
			}
		} else if(mit->second.blocks[block].state == BUFFERED){
			//block became buffered because of a write to it since the read request was sent, ignore it
			myassert(mit->second.blocks[block].request != request); //check request is different from
			//if request was not returned up the hierarchy, delete it
			if (!calledBack){
				delete request;
			}
		} else if(mit->second.blocks[block].state == WRITTEN){
			//block became written because of a write to it since the read request was sent, ignore it
			myassert(mit->second.blocks[block].request != request); //check request is different from
			//if migration was rolled back and there were requests waiting for a read, send them up the hierarchy now
			if (mit->second.rolledBack){
				for (auto cit = mit->second.blocks[block].callers.begin(); cit != mit->second.blocks[block].callers.end(); ++cit){
					cit->callback->accessCompleted(cit->request, this);
				}
				mit->second.blocks[block].callers.clear();
			}
			//if request was not returned up the hierarchy, delete it
			if (!calledBack){
				delete request;
			}
		} else {
			myassert(false);
		}
		mit->second.blockLeftToCompleteRead--;
		if (mit->second.blockLeftToCompleteRead == 0 && mit->second.blocksLeftToWrite == 0){
			debug(": finish copy, src: %s, dest: %s", mit->second.src->getName(), mit->second.dest->getName());

			if (mit->second.dest == dram){
				dramPageCopyTime += (timestamp - mit->second.startPageCopyTime);
			} else if ( mit->second.dest == pcm){
				pcmPageCopyTime += (timestamp - mit->second.startPageCopyTime);
			} else {
				myassert(false);
			}

			manager->copyCompleted(mit->first);
		}
	}
}

void HybridMemory::copyPage(addrint srcPage, addrint destPage){
	uint64 timestamp = engine->getTimestamp();
	debug("(%lu, %lu)", srcPage, destPage);
	//cout << "copyPage: " << srcPage << ", " << destPage << endl;
	addrint pcmPageOffset = manager->getIndex(pcmOffset);
	if (srcPage < pcmPageOffset){
		if (destPage < pcmPageOffset){
			error("Source and destination pages are both in DRAM")
		} else {
			auto p = migrations.emplace(srcPage, MigrationEntry(destPage, dram, pcm, pcmMigrationReadDelay, pcmMigrationWriteDelay, blocksPerPage, timestamp));
			myassert(p.second);
			debug(": %s(%lu) to %s(%lu)", dram->getName(), manager->getAddressFromBlock(srcPage, 0), pcm->getName(), manager->getAddressFromBlock(destPage, 0));

			if (fixedPcmMigrationCost){
				addEvent(pcmMigrationCost, COPY, srcPage);
			} else {
				p.first->second.blocks.resize(blocksPerPage);
				auto dit = dirties.find(srcPage);
				if (dit != dirties.end()){
					if(elideCleanDramBlocks){
						int firstBlock = -1;
						for (unsigned i = 0; i < blocksPerPage; i++){
							if (dit->second[i]){
								p.first->second.blocks[i].state = WRITTEN;
								p.first->second.blocksLeftToWrite--;
							} else {
								if (firstBlock < 0){
									firstBlock = i;
								}
							}
						}
						p.first->second.nextReadBlock = firstBlock;
					}
					dirties.erase(dit);
				}
				addEvent(0, READ, srcPage);
			}

			pcmPageCopies++;
		}
	} else {
		if (destPage < pcmPageOffset){
			error("Destination is in DRAM");
		} else {
			error("Source and destination pages are both in PCM")
		}
	}
}

void HybridMemory::finishMigration(addrint page){
	//debug("(%lu)", page);
	//cout << "finishMigration: " << page << endl;
	auto mit = migrations.find(page);
	myassert(mit != migrations.end());
	if (mit->second.rolledBack){
		auto rit = rolledBackMigrations.find(mit->second.destPage);
		myassert(rit != rolledBackMigrations.end());
		rolledBackMigrations.erase(rit);
		for (auto cit = callbacks.begin(); cit != callbacks.end(); ++cit){
			if (cit->second.partOfMigration && cit->second.page == page){
				cit->second.partOfMigration = false;
			}
		}
	} else {
		if (mit->second.dest == dram){
			auto dit = dirties.emplace(mit->second.destPage, vector<bool>(blocksPerPage));
			myassert(dit.second);
			for (unsigned i = 0; i < blocksPerPage; i++){
				dit.first->second[i] = mit->second.blocks[i].dirty;
			}
		}
		auto monit = monitors.find(page);
		if (monit != monitors.end()){
			monit->second.page = mit->second.destPage;
			bool ins = monitors.emplace(mit->second.destPage, monit->second).second;
			myassert(ins);
			monitors.erase(monit);
		}
	}
	migrations.erase(mit);
}

void HybridMemory::complete(addrint srcPage){

}

void HybridMemory::rollback(addrint srcPage){
	uint64 timestamp = engine->getTimestamp();
	debug("(%lu)", srcPage);
	auto mit = migrations.find(srcPage);
	myassert(mit != migrations.end());
	myassert(mit->second.dest == dram);

	mit->second.src = dram;
	mit->second.dest = pcm;
	mit->second.blocksLeftToRead = 0;
	mit->second.blockLeftToCompleteRead = 0;
	mit->second.blocksLeftToWrite = 0;
	mit->second.rolledBack = true;
	for(auto bit = mit->second.blocks.begin(); bit != mit->second.blocks.end(); ++bit){
		if (bit->state == NOT_READ){
			bit->state = WRITTEN;
		} else if (bit->state == READING){
			bit->state = WRITTEN;
			bit->request = 0;
			//must ignore read when it comes back (don't send to DRAM)
		} else if (bit->state == BUFFERED){
			if (bit->dirty){
				bit->state = BUFFERED;
				bit->request = 0;
				mit->second.blocksLeftToWrite++;
				//must schedule write to src (PCM)
			} else {
				bit->state = WRITTEN;
			}
		} else if (bit->state == WRITTEN){
			if (bit->dirty){
				bit->state = NOT_READ;
				bit->request = 0;
				mit->second.blocksLeftToRead++;
				mit->second.blockLeftToCompleteRead++;
				mit->second.blocksLeftToWrite++;
				//must schedule read to dest (DRAM) and write to src (PCM)
			} else {
				bit->state = WRITTEN;
			}
		} else {
			myassert(false);
		}
	}
	debug(": blocksLeftToRead: %u, blocksLeftToWrite: %u", mit->second.blocksLeftToRead, mit->second.blocksLeftToWrite);
	if (mit->second.blocksLeftToRead == 0 && mit->second.blocksLeftToWrite == 0){
		addEvent(1, COPY, mit->first);
	} else {
		if (mit->second.blocksLeftToRead > 0){
			int block = 0;
			auto bit = mit->second.blocks.begin();
			mit->second.nextReadBlock = 0;
			while (bit != mit->second.blocks.end() && bit->state != NOT_READ){
				++bit;
				block++;
			}
			myassert(bit != mit->second.blocks.end());
			mit->second.nextReadBlock = block;
			addEvent(0, READ, mit->first);
		}
		if (mit->second.blocksLeftToWrite > 0){
			int block = 0;
			//find first block in BUFFERED state
			auto it = mit->second.blocks.begin();
			while (it != mit->second.blocks.end() && it->state != BUFFERED){
				++it;
				block++;
			}
			if (it != mit->second.blocks.end()){
				if (mit->second.nextWriteBlock == -1){
					debug(": adding event: blocksLeftToWrite: %u", mit->second.blocksLeftToWrite);
					addEvent(0, WRITE, mit->first);
				}
				mit->second.nextWriteBlock = block;
			} else {
				mit->second.nextWriteBlock = -1;
			}
		}
	}
	bool ins = rolledBackMigrations.emplace(mit->second.destPage, mit->first).second;
	myassert(ins);
}

void HybridMemory::process(const Event *event){
	uint64 timestamp = engine->getTimestamp();
	EventData *data = reinterpret_cast<EventData *>(event->getData());
	debug("(): type: %d, page %lu", data->type, data->page);
	if (data->type == COPY){
		auto mit = migrations.find(data->page);
		myassert(mit != migrations.end());
		pcmPageCopyTime += (timestamp - mit->second.startPageCopyTime);
		manager->copyCompleted(mit->first);
	} else if (data->type == READ){
		auto mit = migrations.find(data->page);
		myassert(mit != migrations.end());
		if (mit->second.blocksLeftToRead > 0){
			auto it = mit->second.blocks.begin();
			it += mit->second.nextReadBlock;
			myassert(it != mit->second.blocks.end());
			if (it->state != NOT_READ){
				//if block is not in NOT_READ state anymore, find the next one
				while (it != mit->second.blocks.end() && it->state != NOT_READ){
					++it;
					mit->second.nextReadBlock++;
				}
			}
			if (it != mit->second.blocks.end()){
				bool created = false;
				if (it->request == 0){
					created = true;
					addrint srcPage;
					if (mit->second.rolledBack){
						srcPage = mit->second.destPage;
					} else {
						srcPage = mit->first;
					}
					it->request = new MemoryRequest(manager->getAddressFromBlock(srcPage, mit->second.nextReadBlock), blockSize, true, false, LOW);
					debug(": new memoryRequest: %p", it->request);
				}
				debug(": blocksLeftToWrite: %u", mit->second.blocksLeftToWrite);
				debug(": %s.access(%p, %lu, %u, %s, %s, %d)", mit->second.src->getName(), it->request, it->request->addr, it->request->size, it->request->read?"read":"write", it->request->instr?"instr":"data", it->request->priority);
				if (stalledOnRead.empty() && mit->second.src->access(it->request, this)){
					it->state = READING;
					it->startTime = timestamp;
					mit->second.blocksLeftToRead--;
					auto bit = mit->second.blocks.begin();
					int block = 0;
					while (bit != mit->second.blocks.end() && bit->state != NOT_READ){
						++bit;
						block++;
					}
					if (bit != mit->second.blocks.end()){
						mit->second.nextReadBlock = block;
						addEvent(mit->second.readDelay, READ, data->page);
					}
				} else{
					if (created){
						delete(it->request);
						it->request = 0;
					}
					stalledOnRead.emplace_back(mit->first);
				}
			}
		}
	} else if (data->type == WRITE){
		auto mit = migrations.find(data->page);
		myassert(mit != migrations.end());
		if (mit->second.blocksLeftToWrite > 0){
			myassert(mit->second.nextWriteBlock != -1);
			auto it = mit->second.blocks.begin();
			it += mit->second.nextWriteBlock;
			debug(": nextWriteBlock: %d", mit->second.nextWriteBlock);
			debug(": blocksLeftToWrite: %u", mit->second.blocksLeftToWrite);
			if (it->state != BUFFERED){
				int blo = 0;
				for (auto sit = mit->second.blocks.begin(); sit != mit->second.blocks.end(); ++sit){
					debug(": block: %d, state: %d", blo, sit->state);
					blo++;
				}
			}
			myassert(it->state == BUFFERED);
			addrint destPage;
			if (mit->second.rolledBack){
				destPage = mit->first;
			} else {
				destPage = mit->second.destPage;
			}
			if(it->request == 0){
				it->request = new MemoryRequest(manager->getAddressFromBlock(destPage, mit->second.nextWriteBlock), blockSize, false, false, LOW);
			} else {
				it->request->addr = manager->getAddressFromBlock(destPage, mit->second.nextWriteBlock);
				it->request->read = false;
			}
			debug(": %s.access(%p, %lu, %u, %s, %s, %d", mit->second.dest->getName(), it->request, it->request->addr, it->request->size, it->request->read?"read":"write", it->request->instr?"instr":"data", it->request->priority);
			if (stalledOnWrite.empty() && mit->second.dest->access(it->request, this)){
				debug(": not stalled");
				if (mit->second.dest == dram){
					dramCopyWrites++;
				} else if (mit->second.dest == pcm){
					pcmCopyWrites++;
				} else {
					myassert(false);
				}
				it->state = WRITTEN;
				it->startTime = timestamp;
				mit->second.lastWrite = timestamp;

				mit->second.blocksLeftToWrite--;
				debug(": blocksLeftToWrite: %u", mit->second.blocksLeftToWrite);
				if (mit->second.blocksLeftToWrite > 0){
					int block = 0;
					//find first block in BUFFERED state
					auto it = mit->second.blocks.begin();
					while (it != mit->second.blocks.end() && it->state != BUFFERED){
						++it;
						block++;
					}
					debug(": adding event: blocksLeftToWrite: %u", mit->second.blocksLeftToWrite);
					if (it != mit->second.blocks.end()){
						mit->second.nextWriteBlock = block;
						addEvent(mit->second.writeDelay, WRITE, data->page);
					} else {
						mit->second.nextWriteBlock = -1;
					}
				}
				if (mit->second.blocksLeftToWrite == 0 && mit->second.blockLeftToCompleteRead == 0){
					debug(": finish copy, src: %s, dest: %s", mit->second.src->getName(), mit->second.dest->getName());

					if (mit->second.dest == dram){
						dramPageCopyTime += (timestamp - mit->second.startPageCopyTime);
					} else if ( mit->second.dest == pcm){
						pcmPageCopyTime += (timestamp - mit->second.startPageCopyTime);
					} else {
						myassert(false);
					}

					manager->copyCompleted(mit->first);
				}
			} else {
				debug(": stalled");
				stalledOnWrite.emplace_back(mit->first);
			}
		}

	} else if (data->type == NOTIFY){
		myassert(!notifications.empty());
		for (auto it = notifications.begin(); it != notifications.end(); it++){
			it->callback->accessCompleted(it->request, this);
		}
		notifications.clear();
	} else {
		myassert(false);
	}
	delete data;
}

void HybridMemory::unstall(IMemory *caller){
	uint64 timestamp = engine->getTimestamp();
	debug("(%s)", caller->getName());
	if (caller == dram){
		if (!dramStalledCallers.empty()){
			for (auto it = dramStalledCallers.begin(); it != dramStalledCallers.end(); ++it){
				(*it)->unstall(this);
			}
			dramStalledCallers.clear();
		}
	} else if (caller == pcm){
		if (!pcmStalledCallers.empty()){
			for (auto it = pcmStalledCallers.begin(); it != pcmStalledCallers.end(); ++it){
				(*it)->unstall(this);
			}
			pcmStalledCallers.clear();
		}
	} else {
		myassert(false);
	}

	for (auto it = stalledOnRead.begin(); it != stalledOnRead.end(); ++it){
		auto mit = migrations.find(*it);
		myassert(mit != migrations.end());
		if (mit->second.src == caller){
			//delay 2 cycles so that regular request have higher priority while unstalling
			addEvent(2, READ, *it);
		}
	}
	stalledOnRead.clear();
	for (auto it = stalledOnWrite.begin(); it != stalledOnWrite.end(); ++it){
		auto mit = migrations.find(*it);
		myassert(mit != migrations.end());
		if (mit->second.dest == caller){
			//delay 2 cycles so that regular request have higher priority while unstalling
			addEvent(2, WRITE, *it);
		}
	}
	stalledOnWrite.clear();
}

void HybridMemory::readCountsAndProgress(vector<CountEntry> *monitor, vector<ProgressEntry> *progress){
	for (auto monit = monitors.begin(); monit != monitors.end(); ++monit){
		monitor->emplace_back(monit->second);
	}
	monitors.clear();
	for (auto it = migrations.begin(); it != migrations.end(); ++it){
		progress->emplace_back(it->first, it->second.blocksLeftToWrite, it->second.startPageCopyTime);
	}
}

void HybridMemory::setManager(HybridMemoryManager *managerArg) {
	manager = managerArg;
}

uint64 HybridMemory::getDramSize() {
	return dram->getSize();
}

uint64 HybridMemory::getPcmSize() {
	return pcm->getSize();
}

void HybridMemory::addEvent(uint64 delay, EventType type, addrint page){
	EventData *data = new EventData(type, page);
	engine->addEvent(delay, this, reinterpret_cast<uintptr_t>(data));
}



//Old Hybrid Memory:

OldHybridMemory::OldHybridMemory(
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
	bool redirectArg) :
		name(nameArg),
		desc(descArg),
		engine(engineArg),
		debugStart(debugStartArg),
		numProcesses(numProcessesArg),
		dram(dramArg),
		pcm(pcmArg),
		manager(0),
		blockSize(1 << static_cast<unsigned>(logb(blockSizeArg))),
		pageSize(1 << static_cast<unsigned>(logb(pageSizeArg))),
		burstMigration(burstMigrationArg),
		fixedDramMigrationCost(fixedDramMigrationCostArg),
		fixedPcmMigrationCost(fixedPcmMigrationCostArg),
		dramMigrationCost(dramMigrationCostArg),
		pcmMigrationCost(pcmMigrationCostArg),
		redirect(redirectArg),
		pcmOffset(dramArg->getSize()),

		copying(false),

		dramReads(statCont, nameArg + "_dram_reads", "Number of DRAM reads seen by the " + descArg, 0),
		dramWrites(statCont, nameArg + "_dram_writes", "Number of DRAM writes seen by the " + descArg, 0),
		dramAccesses(statCont, nameArg + "_dram_accesses", "Number of DRAM accesses seen by the " + descArg, &dramReads, &dramWrites),

		pcmReads(statCont, nameArg + "_pcm_reads", "Number of PCM reads seen by the " + descArg, 0),
		pcmWrites(statCont, nameArg + "_pcm_writes", "Number of PCM writes seen by the " + descArg, 0),
		pcmAccesses(statCont, nameArg + "_pcm_accesses", "Number of PCM accesses seen by the " + descArg, &pcmReads, &pcmWrites),

		totalReads(statCont, nameArg + "_total_reads", "Number of total reads seen by the " + descArg, &dramReads, &pcmReads),
		totalWrites(statCont, nameArg + "_total_writes", "Number of total writes seen by the " + descArg, &dramWrites, &pcmWrites),
		totalAccesses(statCont, nameArg + "_total_accesses", "Number of total accesses seen by the " + descArg, &dramAccesses, &pcmAccesses),

		dramReadFraction(statCont, nameArg + "_fraction_dram_reads", "Fraction of DRAM reads seen by the " + descArg, &dramReads, &totalReads),
		pcmReadFraction(statCont, nameArg + "_fraction_pcm_reads", "Fraction of PCM reads seen by the " + descArg, &pcmReads, &totalReads),

		dramWriteFraction(statCont, nameArg + "_fraction_dram_writes", "Fraction of DRAM writes seen by the " + descArg, &dramWrites, &totalWrites),
		pcmWriteFraction(statCont, nameArg + "_fraction_pcm_writes", "Fraction of PCM writes seen by the " + descArg, &pcmWrites, &totalWrites),

		dramAccessFraction(statCont, nameArg + "_fraction_dram_accesses", "Fraction of DRAM accesses seen by the " + descArg, &dramAccesses, &totalAccesses),
		pcmAccessFraction(statCont, nameArg + "_fraction_pcm_accesses", "Fraction of PCM accesses seen by the " + descArg, &pcmAccesses, &totalAccesses),

		dramReadTime(statCont, nameArg + "_dram_read_time", "Number of cycles servicing DRAM reads as seen by the " + descArg, 0),
		dramWriteTime(statCont, nameArg + "_dram_write_time", "Number of cycles servicing DRAM writes as seen by the " + descArg, 0),
		dramAccessTime(statCont, nameArg + "_dram_access_time", "Number of cycles servicing DRAM accesses as seen by the " + descArg, &dramReadTime, &dramWriteTime),

		pcmReadTime(statCont, nameArg + "_pcm_read_time", "Number of cycles servicing PCM reads as seen by the " + descArg, 0),
		pcmWriteTime(statCont, nameArg + "_pcm_write_time", "Number of cycles servicing PCM writes as seen by the " + descArg, 0),
		pcmAccessTime(statCont, nameArg + "_pcm_access_time", "Number of cycles servicing PCM accesses as seen by the " + descArg, &pcmReadTime, &pcmWriteTime),

		totalAccessTime(statCont, nameArg + "_total_access_time", "Number of cycles servicing all accesses as seen by the " + descArg, &dramAccessTime, &pcmAccessTime),

		avgDramReadTime(statCont, nameArg + "_avg_dram_read_time", "Average number of cycles servicing DRAM reads as seen by the " + descArg, &dramReadTime, &dramReads),
		avgDramWriteTime(statCont, nameArg + "_avg_dram_write_time", "Average number of cycles servicing DRAM writes as seen by the " + descArg, &dramWriteTime, &dramWrites),
		avgDramAccessTime(statCont, nameArg + "_avg_dram_access_time", "Average number of cycles servicing DRAM accesses as seen by the " + descArg, &dramAccessTime, &dramAccesses),

		avgPcmReadTime(statCont, nameArg + "_avg_pcm_read_time", "Average number of cycles servicing PCM reads as seen by the " + descArg, &pcmReadTime, &pcmReads),
		avgPcmWriteTime(statCont, nameArg + "_avg_pcm_write_time", "Average number of cycles servicing PCM writes as seen by the " + descArg, &pcmWriteTime, &pcmWrites),
		avgPcmAccessTime(statCont, nameArg + "_avg_pcm_access_time", "Average number of cycles servicing PCM accesses as seen by the " + descArg, &pcmAccessTime, &pcmAccesses),

		avgAccessTime(statCont, nameArg + "_avg_access_time", "Average number of cycles servicing all accesses as seen by the " + descArg, &totalAccessTime, &totalAccesses),

		dramCopyReads(statCont, nameArg + "_dram_copy_reads", "Number of DRAM reads due to page copies by the " + descArg, 0),
		dramCopyWrites(statCont, nameArg + "_dram_copy_writes", "Number of DRAM writes due to page copies by the " + descArg, 0),
		dramCopyAccesses(statCont, nameArg + "_dram_copy_accesses", "Number of DRAM accesses due to page copies by the " + descArg, &dramCopyReads, &dramCopyWrites),

		pcmCopyReads(statCont, nameArg + "_pcm_copy_reads", "Number of PCM reads due to page copies by the " + descArg, 0),
		pcmCopyWrites(statCont, nameArg + "_pcm_copy_writes", "Number of PCM writes due to page copies by the " + descArg, 0),
		pcmCopyAccesses(statCont, nameArg + "_pcm_copy_accesses", "Number of PCM accesses due to page copies by the " + descArg, &pcmCopyReads, &pcmCopyWrites),

		totalCopyAccesses(statCont, nameArg + "_total_copy_accesses", "Number of total accesses due to page copies by the " + descArg, &dramCopyAccesses, &pcmCopyAccesses),

		dramCopyReadTime(statCont, nameArg + "_dram_copy_read_time", "Number of cycles servicing DRAM reads due to page copies by the " + descArg, 0),
		dramCopyWriteTime(statCont, nameArg + "_dram_copy_write_time", "Number of cycles servicing DRAM writes due to page copies by the " + descArg, 0),
		dramCopyAccessTime(statCont, nameArg + "_dram_copy_access_time", "Number of cycles servicing DRAM accesses due to page copies by the " + descArg, &dramCopyReadTime, &dramCopyWriteTime),

		pcmCopyReadTime(statCont, nameArg + "_pcm_copy_read_time", "Number of cycles servicing PCM reads due to page copies by the " + descArg, 0),
		pcmCopyWriteTime(statCont, nameArg + "_pcm_copy_write_time", "Number of cycles servicing PCM writes due to page copies by the " + descArg, 0),
		pcmCopyAccessTime(statCont, nameArg + "_pcm_copy_access_time", "Number of cycles servicing PCM accesses due to page copies by the " + descArg, &pcmCopyReadTime, &pcmCopyWriteTime),

		totalCopyAccessTime(statCont, nameArg + "_total_copy_access_time", "Number of cycles servicing all accesses due to page copies by the " + descArg, &dramCopyAccessTime, &pcmCopyAccessTime),

		avgCopyDramReadTime(statCont, nameArg + "_avg_dram_copy_read_time", "Average number of cycles servicing DRAM reads due to page copies by the " + descArg, &dramCopyReadTime, &dramCopyReads),
		avgCopyDramWriteTime(statCont, nameArg + "_avg_dram_copy_write_time", "Average number of cycles servicing DRAM writes due to page copies by the " + descArg, &dramCopyWriteTime, &dramCopyWrites),
		avgCopyDramAccessTime(statCont, nameArg + "_avg_dram_copy_access_time", "Average number of cycles servicing DRAM accesses due to page copies by the " + descArg, &dramCopyAccessTime, &dramCopyAccesses),

		avgCopyPcmReadTime(statCont, nameArg + "_avg_pcm_copy_read_time", "Average number of cycles servicing PCM reads due to page copies by the " + descArg, &pcmCopyReadTime, &pcmCopyReads),
		avgCopyPcmWriteTime(statCont, nameArg + "_avg_pcm_copy_write_time", "Average number of cycles servicing PCM writes due to page copies by the " + descArg, &pcmCopyWriteTime, &pcmCopyWrites),
		avgCopyPcmAccessTime(statCont, nameArg + "_avg_pcm_copy_access_time", "Average number of cycles servicing PCM accesses due to page copies by the " + descArg, &pcmCopyAccessTime, &pcmCopyAccesses),

		avgCopyAccessTime(statCont, nameArg + "_avg_access_copy_time", "Average number of cycles servicing all accesses due to page copies by the " + descArg, &totalCopyAccessTime, &totalCopyAccesses),


		dramPageCopies(statCont, nameArg + "_dram_page_copies", "Number of DRAM pages copied by " + descArg, 0),
		pcmPageCopies(statCont, nameArg + "_pcm_page_copies", "Number of PCM pages copied by " + descArg, 0),

		dramPageCopyTime(statCont, nameArg + "_dram_page_copy_time", "Number of cycles copying DRAM pages by " + descArg, 0),
		pcmPageCopyTime(statCont, nameArg + "_pcm_page_copy_time", "Number of cycles copying PCM pages by " + descArg, 0),

		dramReadsPerPid(statCont, numProcesses, nameArg + "_dram_reads_per_pid", "Number of DRAM reads seen by the " + descArg + " from process"),
		dramWritesPerPid(statCont, numProcesses, nameArg + "_dram_writes_per_pid", "Number of DRAM writes seen by the " + descArg + " from process"),
		dramAccessesPerPid(statCont, nameArg + "_dram_accesses_per_pid", "Number of DRAM accesses seen by the " + descArg + " from process", &dramReadsPerPid, &dramWritesPerPid),

		pcmReadsPerPid(statCont, numProcesses, nameArg + "_pcm_reads_per_pid", "Number of PCM reads seen by the " + descArg + " from process"),
		pcmWritesPerPid(statCont, numProcesses, nameArg + "_pcm_writes_per_pid", "Number of PCM writes seen by the " + descArg + " from process"),
		pcmAccessesPerPid(statCont, nameArg + "_pcm_accesses_per_pid", "Number of PCM accesses seen by the " + descArg + " from process", &pcmReadsPerPid, &pcmWritesPerPid),

		totalReadsPerPid(statCont, nameArg + "_total_reads_per_pid", "Number of total reads seen by the " + descArg + " from process", &dramReadsPerPid, &pcmReadsPerPid),
		totalWritesPerPid(statCont, nameArg + "_total_writes_per_pid", "Number of total writes seen by the " + descArg + " from process", &dramWritesPerPid, &pcmWritesPerPid),
		totalAccessesPerPid(statCont, nameArg + "_total_accesses_per_pid", "Number of total accesses seen by the " + descArg + " from process", &dramAccessesPerPid, &pcmAccessesPerPid),


		dramReadFractionPerPid(statCont, nameArg + "_fraction_dram_reads_per_pid", "Fraction of DRAM reads seen by the " + descArg + " from process", &dramReadsPerPid, &totalReadsPerPid),
		pcmReadFractionPerPid(statCont, nameArg + "_fraction_pcm_reads_per_pid", "Fraction of PCM reads seen by the " + descArg + " from process", &pcmReadsPerPid, &totalReadsPerPid),

		dramWriteFractionPerPid(statCont, nameArg + "_fraction_dram_writes_per_pid", "Fraction of DRAM writes seen by the " + descArg + " from process", &dramWritesPerPid, &totalWritesPerPid),
		pcmWriteFractionPerPid(statCont, nameArg + "_fraction_pcm_writes_per_pid", "Fraction of PCM writes seen by the " + descArg + " from process", &pcmWritesPerPid, &totalWritesPerPid),

		dramAccessFractionPerPid(statCont, nameArg + "_fraction_dram_accesses_per_pid", "Fraction of DRAM accesses seen by the " + descArg + " from process", &dramAccessesPerPid, &totalAccessesPerPid),
		pcmAccessFractionPerPid(statCont, nameArg + "_fraction_pcm_accesses_per_pid", "Fraction of PCM accesses seen by the " + descArg + " from process", &pcmAccessesPerPid, &totalAccessesPerPid),


		dramReadTimePerPid(statCont, numProcesses, nameArg + "_dram_read_time_per_pid", "Number of cycles servicing DRAM reads as seen by the " + descArg + " from process"),
		dramWriteTimePerPid(statCont, numProcesses, nameArg + "_dram_write_time_per_pid", "Number of cycles servicing DRAM writes as seen by the " + descArg + " from process"),
		dramAccessTimePerPid(statCont, nameArg + "_dram_access_time_per_pid", "Number of cycles servicing DRAM accesses as seen by the " + descArg + " from process", &dramReadTimePerPid, &dramWriteTimePerPid),

		pcmReadTimePerPid(statCont, numProcesses, nameArg + "_pcm_read_time_per_pid", "Number of cycles servicing DRAM reads as seen by the " + descArg + " from process"),
		pcmWriteTimePerPid(statCont, numProcesses, nameArg + "_pcm_write_time_per_pid", "Number of cycles servicing DRAM writes as seen by the " + descArg + " from process"),
		pcmAccessTimePerPid(statCont, nameArg + "_pcm_access_time_per_pid", "Number of cycles servicing DRAM accesses as seen by the " + descArg + " from process", &pcmReadTimePerPid, &pcmWriteTimePerPid),

		totalAccessTimePerPid(statCont, nameArg + "_total_access_time_per_pid", "Number of cycles servicing all accesses as seen by the " + descArg + " from process", &dramAccessTimePerPid, &pcmAccessTimePerPid),


		avgDramReadTimePerPid(statCont, nameArg + "_avg_dram_read_time_per_pid", "Average number of cycles servicing DRAM reads as seen by the " + descArg + " from process", &dramReadTimePerPid, &dramReadsPerPid),
		avgDramWriteTimePerPid(statCont, nameArg + "_avg_dram_write_time_per_pid", "Average number of cycles servicing DRAM writes as seen by the " + descArg + " from process", &dramWriteTimePerPid, &dramWritesPerPid),
		avgDramAccessTimePerPid(statCont, nameArg + "_avg_dram_access_time_per_pid", "Average number of cycles servicing DRAM accesses as seen by the " + descArg + " from process", &dramAccessTimePerPid, &dramAccessesPerPid),

		avgPcmReadTimePerPid(statCont, nameArg + "_avg_pcm_read_time_per_pid", "Average number of cycles servicing PCM reads as seen by the " + descArg + " from process", &pcmReadTimePerPid, &pcmReadsPerPid),
		avgPcmWriteTimePerPid(statCont, nameArg + "_avg_pcm_write_time_per_pid", "Average number of cycles servicing PCM writes as seen by the " + descArg + " from process", &pcmWriteTimePerPid, &pcmWritesPerPid),
		avgPcmAccessTimePerPid(statCont, nameArg + "_avg_pcm_access_time_per_pid", "Average number of cycles servicing PCM accesses as seen by the " + descArg + " from process", &pcmAccessTimePerPid, &pcmAccessesPerPid),

		avgAccessTimePerPid(statCont, nameArg + "_avg_access_time_per_pid", "Average number of cycles servicing all accesses as seen by the " + descArg + " from process", &totalAccessTimePerPid, &totalAccessesPerPid)


		{

	//debug = 104350000;
	//debug = 0;
}

OldHybridMemory::~OldHybridMemory() {}

void OldHybridMemory::process(const Event *event){
	uint64 timestamp = engine->getTimestamp();
	EventType type = static_cast<EventType>(event->getData());
	debug("(): type: %d", type);
	if (type == COPY){

		if (dest == dram && fixedDramMigrationCost){
			manager->copyCompleted();
			dramPageCopyTime += (timestamp - startDramPageCopyTime);
			copying = false;
		} else if (dest == pcm && fixedPcmMigrationCost){
			manager->copyCompleted();
			pcmPageCopyTime += (timestamp - startPcmPageCopyTime);
			copying = false;
		} else {
			for (BlockMap::iterator it = blocks.begin(); it != blocks.end(); it++){
				if (it->second.state == WAITING){
					it->second.state = READING;
					it->second.request = new MemoryRequest(manager->getAddress(srcPage, it->first), blockSize, true, false, LOW);
					it->second.startTime = timestamp;
					if (!srcStalledRequests->empty() || !src->access(it->second.request, this)){
						srcStalledRequests->emplace_back(it->second.request);
					}
					if (!burstMigration){
						return;
					}
				}
			}
		}

//		if (fixedMigrationCost){
//			manager->copyCompleted();
//			if (dest == dram){
//				dramPageCopyTime += (timestamp - startDramPageCopyTime);
//			} else if (dest == pcm){
//				pcmPageCopyTime += (timestamp - startPcmPageCopyTime);
//			} else {
//				myassert(false);
//			}
//			copying = false;
//		} else {
//			for (BlockMap::iterator it = blocks.begin(); it != blocks.end(); it++){
//				if (it->second.state == WAITING){
//					it->second.state = READING;
//					it->second.request = new MemoryRequest(manager->getAddress(srcPage, it->first), blockSize, true, false, LOW);
//					it->second.startTime = timestamp;
//					if (!srcStalledRequests->empty() || !src->access(it->second.request, this)){
//						srcStalledRequests->emplace_back(it->second.request);
//					}
//					if (!burstMigration){
//						return;
//					}
//				}
//			}
//		}
	} else if (type == UNSTALL_DRAM){
		list<MemoryRequest *>::iterator it = dramStalledRequests.begin();
		while(it != dramStalledRequests.end()){
			if (dram->access(*it, this)){
				it = dramStalledRequests.erase(it);
			} else {
				break;
			}
		}
	} else if (type == UNSTALL_PCM){
		list<MemoryRequest *>::iterator it = pcmStalledRequests.begin();
		while(it != pcmStalledRequests.end()){
			if (pcm->access(*it, this)){
				it = pcmStalledRequests.erase(it);
			} else {
				break;
			}
		}
	} else {
		myassert(false);
	}
}

bool OldHybridMemory::access(MemoryRequest *request, IMemoryCallback *caller){
	uint64 timestamp = engine->getTimestamp();
	debug("(%p, %lu, %u, %s, %s, %d, %s)", request, request->addr, request->size, request->read?"read":"write", request->instr?"instr":"data", request->priority, caller->getName());
/*	static int u=0;
	if(u%100==0)
		cout<<"ACCCCCCCSES"<<u<<endl;
	u++;*/
	if (caller != manager){
		manager->monitorPhysicalAccess(request->addr, request->read, request->instr);
	}
	addrint callbackAddr = request->addr; //addr might get overwritten
	BlockMap::iterator it = blocks.end();
	if (copying && !request->read){
		addrint page = manager->getIndex(request->addr);
		if (page == srcPage){
			if (redirect){
				addrint offset = manager->getOffset(request->addr);
				request->addr = manager->getAddress(destPage, offset); //redirect write to dest
				it = blocks.find(offset);
			} else {
				warn("Writing to page under migration (src)");
				myassert(false);
			}
		}
		if (page == destPage){
			error("Writing to page under migration (dest)");
		}
	}

	int pid = manager->getPidOfAddress(request->addr);
	if (request->addr < pcmOffset){
		if (dramStalledCallers.empty() && dram->access(request, this)){
			if (request->read){
				dramReads++;
				if (pid >= 0){
					dramReadsPerPid[pid]++;
					dramReadsCounters[pid]++;
				}
			} else {
				dramWrites++;
				if (pid >= 0){
					dramWritesPerPid[pid]++;
					dramWritesCounters[pid]++;
				}
			}
		} else {
			dramStalledCallers.insert(caller);
			request->addr = callbackAddr;
			return false;
		}
	} else {
		if (pcmStalledCallers.empty() && pcm->access(request, this)){
			if (request->read){
				pcmReads++;
				if (pid >= 0){
					pcmReadsPerPid[pid]++;
					pcmReadsCounters[pid]++;
				}
			} else {
				pcmWrites++;
				if (pid >= 0){
					pcmWritesPerPid[pid]++;
					pcmWritesCounters[pid]++;
				}
			}
		} else {
			pcmStalledCallers.insert(caller);
			request->addr = callbackAddr;
			return false;
		}
	}

	if (it != blocks.end()){
		if (it->second.state == WAITING){
			//Read request not yet sent, so cancel migration read for this block
			blocks.erase(it);
		} else if (it->second.state == READING){
			//Read request sent, but not received , so ignore migration read for this block when it comes back
			it->second.ignoreRead = true;
		} else {
			myassert(false);
		}
	} else {
		//Write request completed, so don't do anything (latest write will overwrite migration write)
	}

	if (request->read){
		callbacks.emplace(request, CallbackEntry(caller, timestamp));
	}
	return true;
}

void OldHybridMemory::accessCompleted(MemoryRequest *request, IMemory *caller){
	uint64 timestamp = engine->getTimestamp();
	debug("(%p, %s, %s)", request, request->read ? "read" : "write", caller->getName());
	multimap<MemoryRequest *, CallbackEntry>::iterator it = callbacks.find(request);
	if (it != callbacks.end()){

		int pid = manager->getPidOfAddress(request->addr);
		uint64 accessTime = timestamp - it->second.startTime;
		if (caller == dram){
			if (request->read){
				dramReadTime += accessTime;
				if (pid >= 0){
					dramReadTimePerPid[pid] += accessTime;
					dramReadTimeCounters[pid] += accessTime;
				}
			} else {
				dramWriteTime += accessTime;
				if (pid >= 0){
					dramWriteTimePerPid[pid] += accessTime;
					dramWriteTimeCounters[pid] += accessTime;
				}
			}
		} else if (caller == pcm){
			if (request->read){
				pcmReadTime += accessTime;
				if (pid >= 0){
					pcmReadTimePerPid[pid] += accessTime;
					pcmReadTimeCounters[pid] += accessTime;
				}
			} else {
				pcmWriteTime += accessTime;
				if (pid >= 0){
					pcmWriteTimePerPid[pid] += accessTime;
					pcmWriteTimeCounters[pid] += accessTime;
				}
			}
		} else {
			myassert(false);
		}
		IMemoryCallback *callback = it->second.callback;
		callbacks.erase(it);
		callback->accessCompleted(request, this);
	} else {
		addrint index = manager->getIndex(request->addr);
		addrint offset = manager->getOffset(request->addr);

		BlockMap::iterator it = blocks.find(offset);
		if (it == blocks.end()){
			error("Could not find block");
		}

		if (it->second.state == READING){
			//Finished reading
			myassert(caller == src);
			myassert(index == srcPage);

			if (src == dram){
				dramCopyReads++;
				dramCopyReadTime += (timestamp - it->second.startTime);
			} else if (src == pcm){
				pcmCopyReads++;
				pcmCopyReadTime += (timestamp - it->second.startTime);
			} else {
				myassert(false);
			}

			if (!it->second.ignoreRead){
				it->second.startTime = timestamp;
				it->second.request->addr = manager->getAddress(destPage, offset);
				it->second.request->read = false;
				if (!destStalledRequests->empty() || !dest->access(it->second.request, this)){
					destStalledRequests->emplace_back(it->second.request);
				}
				if (dest == dram){
					dramCopyWrites++;
				} else if (dest == pcm){
					pcmCopyWrites++;
				} else {
					myassert(false);
				}
			}
			blocks.erase(it);

			if (blocks.empty()){
					debug(": finish copy, src: %s, dest: %s", src->getName(), dest->getName());
					manager->copyCompleted();
					if (dest == dram){
						dramPageCopyTime += (timestamp - startDramPageCopyTime);
					} else if (dest == pcm){
						pcmPageCopyTime += (timestamp - startPcmPageCopyTime);
					} else {
						myassert(false);
					}
					copying = false;
			} else {
				if (!burstMigration){
					addEvent(0, COPY);
				}
			}
		} else {
			myassert(false);
		}
	}
}

void OldHybridMemory::unstall(IMemory *caller){
	bool unstallRequests = false;
	if (caller == dram){
		if (dramStalledCallers.empty()){
			unstallRequests = true;
		} else {
			for (set<IMemoryCallback*>::iterator it = dramStalledCallers.begin(); it != dramStalledCallers.end(); ++it){
				(*it)->unstall(this);
			}
			dramStalledCallers.clear();
		}
	} else if (caller == pcm){
		if (pcmStalledCallers.empty()){
			unstallRequests = true;
		} else {
			for (set<IMemoryCallback*>::iterator it = pcmStalledCallers.begin(); it != pcmStalledCallers.end(); ++it){
				(*it)->unstall(this);
			}
			pcmStalledCallers.clear();
		}

	} else {
		myassert(false);
	}

	if (unstallRequests){
		if (caller == dram){
			addEvent(0, UNSTALL_DRAM);
		} else if (caller == pcm){
			addEvent(0, UNSTALL_PCM);
		} else {
			myassert(false);
		}
	}
}

void OldHybridMemory::copyPage(addrint srcPageArg, addrint destPageArg){
	uint64 timestamp = engine->getTimestamp();
	debug("(%lu, %lu)", srcPageArg, destPageArg);
	srcPage = srcPageArg;
	destPage = destPageArg;
	if (copying){
		error("Another page is already under migration");
	}

	copying = true;
	addrint pcmPageOffset = manager->getIndex(pcmOffset);
	if (srcPage < pcmPageOffset){
		if (destPage < pcmPageOffset){
			error("Source and destination pages are both in DRAM")
		} else {
			src = dram;
			dest = pcm;
			srcStalledRequests = &dramStalledRequests;
			destStalledRequests = &pcmStalledRequests;
			pcmPageCopies++;
			startPcmPageCopyTime = timestamp;
			//cout << "Copying page to PCM" << endl;
		}
	} else {
		if (destPage < pcmPageOffset){
			src = pcm;
			dest = dram;
			srcStalledRequests = &pcmStalledRequests;
			destStalledRequests = &dramStalledRequests;
			dramPageCopies++;
			startDramPageCopyTime = timestamp;
			//cout << "Copying page to DRAM" << endl;
		} else {
			error("Source and destination pages are both in PCM")
		}
	}

	debug(": %s(%lu) to %s(%lu)", src->getName(), manager->getAddress(srcPage, 0), dest->getName(), manager->getAddress(destPage, 0));

	if (src == dram && fixedPcmMigrationCost){
		addEvent(pcmMigrationCost, COPY);
	} else if (src == pcm && fixedDramMigrationCost){
		addEvent(dramMigrationCost, COPY);
	} else {
		for (addrint offset = 0; offset < pageSize; offset += blockSize){
			blocks.emplace(offset, BlockEntry());
		}
		addEvent(0, COPY);
	}
}

void OldHybridMemory::setManager(OldHybridMemoryManager *managerArg) {
	manager = managerArg;
	unsigned cores = manager->getNumCores();

	dramReadsCounters.resize(cores);
	dramWritesCounters.resize(cores);
	pcmReadsCounters.resize(cores);
	pcmWritesCounters.resize(cores);

	dramReadTimeCounters.resize(cores);
	dramWriteTimeCounters.resize(cores);
	pcmReadTimeCounters.resize(cores);
	pcmWriteTimeCounters.resize(cores);

}

uint64 OldHybridMemory::getDramSize() {
	return dram->getSize();
}

uint64 OldHybridMemory::getPcmSize() {
	return pcm->getSize();
}
