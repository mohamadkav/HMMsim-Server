/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */


#include "MemoryManager.H"

#include <algorithm>
#include <memory>

#include <cassert>
#include <cmath>


HybridMemoryManager::HybridMemoryManager(
	Engine *engineArg,
	StatContainer *statCont,
	uint64 debugStartArg,
	unsigned numCoresArg,
	unsigned numProcessesArg,
	Cache *lastLevelCacheArg,
	HybridMemory *memoryArg,
	const vector<IMigrationPolicy*>& policiesArg,
	IPartition *partitionArg,
	unsigned blockSizeArg,
	unsigned pageSizeArg,
	FlushPolicy flushPolicyArg,
	unsigned maxFlushQueueSizeArg,
	bool suppressFlushWritebacksArg,
	uint64 demoteTimeoutArg,
	uint64 partitionPeriodArg,
	const string& periodTypeArg,
	unsigned maxMigrationTableSizeArg,
	bool perPageStatsArg,
	string perPageStatsFilenameArg
	) :
		name("HybridMemoryManager"),
		engine(engineArg),
		debugStart(debugStartArg),
		numCores(numCoresArg),
		numProcesses(numProcessesArg),
		lastLevelCache(lastLevelCacheArg),
		memory(memoryArg),
		policies(policiesArg),
		partition(partitionArg),
		flushPolicy(flushPolicyArg),
		maxFlushQueueSize(maxFlushQueueSizeArg),
		suppressFlushWritebacks(suppressFlushWritebacksArg),
		demoteTimeout(demoteTimeoutArg),
		partitionPeriod(partitionPeriodArg),
		periodType(periodTypeArg),
		maxMigrationTableSize(maxMigrationTableSizeArg),
		perPageStats(perPageStatsArg),
		perPageStatsFilename(perPageStatsFilenameArg),

		dramFullMigrations(statCont, "manager_dram_full_migrations", "Number of full DRAM migrations", 0),
		dramPartialMigrations(statCont, "manager_dram_partial_migrations", "Number of partial DRAM migrations (rolledback)", 0),
		dramMigrations(statCont, "manager_dram_migrations", "Number of DRAM migrations (full and partial)", &dramFullMigrations, &dramPartialMigrations),



		pcmFullMigrations(statCont, "manager_pcm_full_migrations", "Number of full PCM migrations", 0),
		pcmPartialMigrations(statCont, "manager_pcm_partial_migrations", "Number of partial PCM migrations (result of rollback)", 0),
		pcmMigrations(statCont, "manager_pcm_migrations", "Number of PCM migrations (full and partial)", &pcmFullMigrations, &pcmPartialMigrations),

		allFullMigrations(statCont, "manager_total_full_migrations", "Total number of full migrations", &dramFullMigrations, &pcmFullMigrations),
		allPartialMigrations(statCont, "manager_total_partial_migrations", "Total number of partial migrations (each rollbacks is counted twice)", &dramPartialMigrations, &pcmPartialMigrations),
		allMigrations(statCont, "manager_total_migrations", "Total number of migrations", &dramMigrations, &pcmMigrations),

		migrationEntriesSum(statCont, "manager_migration_entries_sum", "Sum of the number of ongoing migration each time a new migration starts", 0),
		migrationEntriesCount(statCont, "manager_migration_entries_count", "Number of migrations started", 0),
		avgMigrationEntries(statCont, "manager_avg_migration_entries", "Average number of ongoing migrations", &migrationEntriesSum, &migrationEntriesCount),

		cleanFlushedBlocks(statCont, "manager_clean_flushed_blocks", "Number of clean flushed blocks", 0),
		dirtyFlushedBlocks(statCont, "manager_dirty_flushed_blocks", "Number of dirty flushed blocks", 0),
		tagChanges(statCont, "manager_tag_changes", "Number of tag changes", 0),

		dramFullMigrationTime(statCont, "manager_dram_full_migration_time", "Number of cycles migrating pages to DRAM (full migration)", 0),
		dramPartialMigrationTime(statCont, "manager_dram_partial_migration_time", "Number of cycles migrating pages to DRAM (partial migration)", 0),
		dramMigrationTime(statCont, "manager_dram_migration_time", "Number of cycles migrating pages to DRAM (full and partial migration)", &dramFullMigrationTime, &dramPartialMigrationTime),

		pcmFullMigrationTime(statCont, "manager_pcm_full_migration_time", "Number of cycles migrating pages to PCM (full migration)", 0),
		pcmPartialMigrationTime(statCont, "manager_pcm_partial_migration_time", "Number of cycles migrating pages to PCM (partial migration)", 0),
		pcmMigrationTime(statCont, "manager_pcm_migration_time", "Number of cycles migrating pages to PCM (full and partial migration)", &pcmFullMigrationTime, &pcmPartialMigrationTime),

		fullMigrationTime(statCont, "manager_full_migration_time", "Number of cycles migrating pages (full migration)", &dramFullMigrationTime, &pcmFullMigrationTime),
		partialMigrationTime(statCont, "manager_partial_migration_time", "Number of cycles migrating pages (partial migration)", &dramPartialMigrationTime, &pcmPartialMigrationTime),
		migrationTime(statCont, "manager_total_migration_time", "Total number of cycles migrating all pages (full and partial migration)", &dramMigrationTime, &pcmMigrationTime),

		dramFlushBeforeTime(statCont, "manager_dram_flush_before_time", "Number of cycles flushing the cache before migrations to DRAM", 0),
		pcmFlushBeforeTime(statCont, "manager_pcm_flush_before_time", "Number of cycles flushing the cache before migrations to PCM", 0),
		flushBeforeTime(statCont, "manager_flush_before_time", "Number of cycles flushing the cache before migrations", &dramFlushBeforeTime, &pcmFlushBeforeTime),

		dramFlushAfterTime(statCont, "manager_dram_flush_after_time", "Number of cycles flushing the cache after migrations to DRAM", 0),
		pcmFlushAfterTime(statCont, "manager_pcm_flush_after_time", "Number of cycles flushing the cache after migrations to PCM", 0),
		flushAfterTime(statCont, "manager_flush_after_time", "Number of cycles flushing the cache after migrations", &dramFlushAfterTime, &pcmFlushAfterTime),

		dramFlushTime(statCont, "manager_dram_flush_time", "Number of cycles flushing the cache due to migrations to DRAM", &dramFlushBeforeTime, &dramFlushAfterTime),
		pcmFlushTime(statCont, "manager_pcm_flush_time", "Number of cycles flushing the cache due to migrations to PCM", &pcmFlushBeforeTime, &pcmFlushAfterTime),
		flushTime(statCont, "manager_total_flush_time", "Total number of cycles flushing the cache", &dramFlushTime, &pcmFlushTime),

		dramCopyTime(statCont, "manager_dram_copy_time", "Number of cycles copying pages during migrations to DRAM", 0),
		pcmCopyTime(statCont, "manager_pcm_copy_time", "Number of cycles copying pages during migrations to PCM", 0),
		copyTime(statCont, "manager_copy_time", "Number of cycles copying pages during migrations", &dramCopyTime, &pcmCopyTime),

		idleTime(statCont, "manager_idle_time", "Number of cycles the migration policy (demotion) is idle", 0),

		avgDramMigrationTime(statCont, "manager_avg_dram_migration_time", "Average number of cycles per migration to DRAM", &dramMigrationTime, &dramMigrations),
		avgPcmMigrationTime(statCont, "manager_avg_pcm_migration_time", "Average number of cycles per migration to PCM", &pcmMigrationTime, &pcmMigrations),
		avgMigrationTime(statCont, "manager_avg_migration_time", "Average number of cycles per migration", &migrationTime, &allMigrations),

		avgDramFlushBeforeTime(statCont, "manager_avg_dram_flush_before_time", "Average number of cycles flushing the cache before each migration to DRAM", &dramFlushBeforeTime, &dramMigrations),
		avgPcmFlushBeforeTime(statCont, "manager_avg_pcm_flush_before_time", "Average number of cycles flushing the cache before each migration to PCM", &pcmFlushBeforeTime, &pcmMigrations),
		avgFlushBeforeTime(statCont, "manager_avg_flush_before_time", "Average number of cycles flushing the cache per migration", &flushBeforeTime, &allMigrations),

		avgDramFlushAfterTime(statCont, "manager_avg_dram_flush_after_time", "Average number of cycles flushing the cache after each migration to DRAM", &dramFlushAfterTime, &dramMigrations),
		avgPcmFlushAfterTime(statCont, "manager_avg_pcm_flush_after_time", "Average number of cycles flushing the cache after each migration to PCM", &pcmFlushAfterTime, &pcmMigrations),
		avgFlushAfterTime(statCont, "manager_avg_flush_after_time", "Average number of cycles flushing the cache per migration", &flushAfterTime, &allMigrations),


		avgDramFlushTime(statCont, "manager_avg_dram_flush_time", "Average number of cycles flushing the cache per DRAM migration", &dramFlushTime, &dramMigrations),
		avgPcmFlushTime(statCont, "manager_avg_pcm_flush_time", "Average number of cycles flushing the cache per PCM migration", &pcmFlushTime, &pcmMigrations),
		avgFlushTime(statCont, "manager_avg_flush_time", "Average number of cycles flushing the cache per migration", &flushTime, &allMigrations),

		avgDramCopyTime(statCont, "manager_avg_dram_copy_time", "Average number of cycles copying pages per DRAM migration", &dramCopyTime, &dramMigrations),
		avgPcmCopyTime(statCont, "manager_avg_pcm_copy_time", "Average number of cycles copying pages per PCM migration", &pcmCopyTime, &pcmMigrations),
		avgCopyTime(statCont, "manager_avg_copy_time", "Average number of cycles copying pages per migration", &copyTime, &allMigrations),

		dramMemorySize(statCont, "manager_dram_memory_size", "Size of DRAM memory available to the memory manager", this, &HybridMemoryManager::getDramMemorySize),
		dramMemorySizeUsed(statCont, "manager_dram_memory_size_used", "Size of DRAM memory used by the memory manager", this, &HybridMemoryManager::getDramMemorySizeUsed),
		pcmMemorySize(statCont, "manager_pcm_memory_size", "Size of PCM memory available to the memory manager", this, &HybridMemoryManager::getPcmMemorySize),
		pcmMemorySizeUsed(statCont, "manager_pcm_memory_size_used", "Size of PCM memory used by the memory manager", this, &HybridMemoryManager::getPcmMemorySizeUsed),

		dramMemorySizeInitial(statCont, "manager_dram_memory_size_initial", "Size of DRAM memory at start of simulation", 0),
		pcmMemorySizeInitial(statCont, "manager_pcm_memory_size_initial", "Size of PCM memory at start of simulation", 0),
		totalMemorySizeInitial(statCont, "manager_total_memory_size_initial", "Total size of DRAM and PCM memory at start of simulation", &dramMemorySizeInitial, &pcmMemorySizeInitial),

		dramMemorySizeUsedPerPid(statCont, numProcesses, "manager_dram_memory_size_per_pid", "Size of DRAM memory used by process", true),
		pcmMemorySizeUsedPerPid(statCont, numProcesses, "manager_pcm_memory_size_per_pid", "Size of PCM memory used by process", true),
		totalMemorySizeUsedPerPid(statCont, "manager_total_memory_size_per_pid", "Size of total memory used by process", &dramMemorySizeUsedPerPid, &pcmMemorySizeUsedPerPid),

		dramMigrationsPerPid(statCont, numProcesses, "manager_dram_migrations_per_pid", "Number of DRAM migrations by process"),
		pcmMigrationsPerPid(statCont, numProcesses, "manager_pcm_migrations_per_pid", "Number of PCM migrations by process"),
		totalMigrationsPerPid(statCont, "manager_total_migrations_per_pid", "Number of total migrations by process", &dramMigrationsPerPid, &pcmMigrationsPerPid)
{
	memory->setManager(this);

	unsigned logBlockSize = static_cast<unsigned>(logb(blockSizeArg));
	blockSize = 1 << logBlockSize;

	unsigned logPageSize = (unsigned)logb(pageSizeArg);
	pageSize = 1 << logPageSize;
	numDramPages = memory->getDramSize() / pageSize;
	dramSize = numDramPages * pageSize;
	numPcmPages = memory->getPcmSize() / pageSize;
	pcmSize = numPcmPages * pageSize;

	offsetWidth = logPageSize;
	indexWidth = sizeof(addrint)*8 - offsetWidth;

	offsetMask = 0;
	for (unsigned i = 0; i < offsetWidth; i++){
		offsetMask |= (addrint)1U << i;
	}
	indexMask = 0;
	for (unsigned i = offsetWidth; i < indexWidth+offsetWidth; i++){
		indexMask |= (addrint)1U << i;
	}

	blockOffsetWidth = (uint64)logb(blockSize);

	firstDramAddress = 0;
	onePastLastDramAddress = dramSize;
	firstPcmAddress = dramSize;
	onePastLastPcmAddress = dramSize + pcmSize;

	firstDramPage = getIndex(firstDramAddress);
	onePastLastDramPage = getIndex(onePastLastDramAddress);
	firstPcmPage = getIndex(firstPcmAddress);
	onePastLastPcmPage = getIndex(onePastLastPcmAddress);

	for(addrint page = firstDramPage; page < onePastLastDramPage; page++){
		dramFreePageList.emplace_back(page);
	}

	for(addrint page = firstPcmPage; page < onePastLastPcmPage; page++){
		pcmFreePageList.emplace_back(page);
	}

	pages = new PageMap[numProcesses];

	if (partition->getNumPolicies() == 1){
		policies.resize(numProcesses, policies[0]);
	} else if (partition->getNumPolicies() == numProcesses) {

	} else {
		error("Not yet implemented");
	}

	for (unsigned i = 0; i < partition->getNumPolicies(); i++){
		policies[i]->setNumDramPages(partition->getDramPages(i));
	}

	idle = true;
	lastStartIdleTime = 0;

	demoting = false;
	currentPolicy = 0;

	migrationTableSize = 0;

	flushQueueSize = 0;

	stalledCpus = new StalledCpuMap[numProcesses];

	lastIntervalStart = 0;

	perPidMonitors.resize(partition->getNumPolicies());
	perPidProgress.resize(partition->getNumPolicies());

	addEvent(0, DEMOTE);
	addEvent(0, COMPLETE);
	addEvent(0, ROLLBACK);
	if (periodType == "cycles"){
		addEvent(0, UPDATE_PARTITION);
	}

}

bool HybridMemoryManager::access(int pid, addrint virtualAddr, bool read, bool instr, addrint *physicalAddr, CPU *cpu){
	uint64 timestamp = engine->getTimestamp();
	//debug("(%d, %lu, %s, %s)", pid, virtualAddr, read ? "read" : "write", instr ? "instr" : "data");
	addrint virtualPage = getIndex(virtualAddr);
	PageMap::iterator it = pages[pid].find(virtualPage);
	if (it == pages[pid].end()){
		PageType type = policies[pid]->allocate(pid, virtualPage, read, instr);
		addrint freePage;
		if (type == DRAM){
			myassert(!dramFreePageList.empty());
			freePage = dramFreePageList.front();
			dramFreePageList.pop_front();
			dramMemorySizeUsedPerPid[pid] += pageSize;
		} else if (type == PCM){
			if (pcmFreePageList.empty()){
				error("PCM free page list is empty");
			}
			freePage = pcmFreePageList.front();
			pcmFreePageList.pop_front();
			pcmMemorySizeUsedPerPid[pid] += pageSize;
		} else {
			myassert(false);
		}
		it = pages[pid].emplace(virtualPage, PageEntry(freePage, type, timestamp)).first;
		bool ins = physicalPages.emplace(it->second.page, PhysicalPageEntry(pid, virtualPage)).second;
		myassert(ins);
	}
	myassert((isDramPage(it->second.page) && it->second.type == DRAM) || (isPcmPage(it->second.page) && it->second.type == PCM));

	if(it->second.stallOnAccess){
		stalledCpus[pid][virtualPage].emplace_back(cpu);
		debug(": stalled on access to %lu", virtualPage);
		return true;
	} else {
		*physicalAddr = getAddress(it->second.page, getOffset(virtualAddr));
		return false;
	}
}

bool HybridMemoryManager::migrateOnDemand(addrint physicalPage, addrint *destPhysicalPage){
	uint64 timestamp = engine->getTimestamp();
	debug("(%lu)", physicalPage);
//	cout << dramFreePageList.size();
	if (dramFreePageList.empty()){
		return false;
	}

	auto pit = physicalPages.find(physicalPage);
	myassert(pit != physicalPages.end());
	auto it = pages[pit->second.pid].find(pit->second.virtualPage);
	myassert(it != pages[pit->second.pid].end());
	myassert(isPcmPage(it->second.page));
	myassert(it->second.type == PCM);

	if (it->second.isMigrating){
		//happens when migration has finished copying blocks but flushing is not done
		return false;
	}


	if(migrationTableSize < maxMigrationTableSize && policies[pit->second.pid]->migrate(pit->second.pid, pit->second.virtualPage)){
		it->second.isMigrating = true;

		*destPhysicalPage = dramFreePageList.front();
		dramFreePageList.pop_front();

		bool ins = migrations.emplace(it->second.page, MigrationEntry(pit->second.pid, pit->second.virtualPage, *destPhysicalPage, DRAM, COPY, timestamp)).second;
		myassert(ins);

		migrationTableSize++;

		debug(": pid: %d, virtualPage: %lu, srcPhysPage: %lu, destPhysPage: %lu, dest: DRAM, state: COPY", pit->second.pid, pit->second.virtualPage, it->second.page, *destPhysicalPage);

		migrationEntriesSum += migrations.size();
		migrationEntriesCount++;
		dramMigrationsPerPid[pit->second.pid]++;
		dramMemorySizeUsedPerPid[pit->second.pid] += pageSize;

		return true;
	} else {
		return false;
	}
}

void HybridMemoryManager::finish(int core){
	coresFinished.insert(core);
//	if (perPageStats){
//		ofstream out(perPageStatsFilename.c_str());
//		out << "#pid virtualPageAddress numMigrations" << endl;
//		out << "#dest(0:DRAM;1:PCM) start end endTransfer readsWhileMigrating writesWhileMigrating reads writes readBlocks writtenBlocks accessedBlocks" << endl;
//		for (unsigned i = 0; i < numProcesses; i++){
//			for (auto it = pages[i].begin(); it != pages[i].end(); ++it){
//				myassert(it->second.migrations.back().end == 0);
//				it->second.migrations.back().end = engine->getTimestamp();
//				out << i << " " << it->first << " " << it->second.migrations.size() << endl;
//				for (auto it2 = it->second.migrations.begin(); it2 != it->second.migrations.end(); ++it2){
//					out << it2->dest << " ";
//					out << it2->start << " ";
//					out << it2->end << " ";
//					out << it2->endTransfer << " ";
//					out << it2->readsWhileMigrating << " ";
//					out << it2->writesWhileMigrating << " ";
//					out << it2->reads << " ";
//					out << it2->writes << " ";
//					out << it2->readBlocks.count() << " ";
//					out << it2->writtenBlocks.count() << " ";
//					out << (it2->readBlocks | it2->writtenBlocks).count() << endl;
//				}
//			}
//		}
//	}
}

void HybridMemoryManager::allocate(const vector<string>& filenames){
	uint64 dramPages = 0;
	for (unsigned i = 0; i < partition->getNumPolicies(); i++){
		dramPages += partition->getDramPages(i);
	}
	uint64 dramPagesPerProcess = dramPages/filenames.size();
	vector<unique_ptr<ifstream> >ifs;
	for (unsigned pid = 0; pid < filenames.size(); pid++){
		ifs.emplace_back(new ifstream(filenames[pid]));
		addrint virtualPage;
		uint64 count = 0;
		while (count < dramPagesPerProcess && *ifs[pid] >> virtualPage){
			PageType type = policies[pid]->allocate(pid, virtualPage, false, false);
			myassert(type == DRAM);
			myassert(!dramFreePageList.empty());
			addrint freePage = dramFreePageList.front();
			dramFreePageList.pop_front();
			dramMemorySizeInitial += pageSize;
			dramMemorySizeUsedPerPid[pid] += pageSize;
			PageMap::iterator it = pages[pid].emplace(virtualPage, PageEntry(freePage, type, engine->getTimestamp())).first;
			bool ins = physicalPages.emplace(it->second.page, PhysicalPageEntry(pid, virtualPage)).second;
			myassert(ins);
			count++;
		}
	}
	for (unsigned pid = 0; pid < filenames.size(); pid++){
		addrint virtualPage;
		while (*ifs[pid] >> virtualPage){
			PageType type = policies[pid]->allocate(pid, virtualPage, false, false);
			addrint freePage;
			if (type == DRAM){
				myassert(!dramFreePageList.empty());
				freePage = dramFreePageList.front();
				dramFreePageList.pop_front();
				dramMemorySizeInitial += pageSize;
				dramMemorySizeUsedPerPid[pid] += pageSize;
			} else if (type == PCM){
				if (pcmFreePageList.empty()){
					error("PCM free page list is empty");
				}
				freePage = pcmFreePageList.front();
				pcmFreePageList.pop_front();
				pcmMemorySizeInitial += pageSize;
				pcmMemorySizeUsedPerPid[pid] += pageSize;
			} else {
				myassert(false);
			}
			PageMap::iterator it = pages[pid].emplace(virtualPage, PageEntry(freePage, type, engine->getTimestamp())).first;
			bool ins = physicalPages.emplace(it->second.page, PhysicalPageEntry(pid, virtualPage)).second;
			myassert(ins);
		}
	}

//	ifstream ifs(filename.c_str());
//	addrint virtualPage;
//	while (ifs >> virtualPage){
//		PageType type = policies[pid]->allocate(pid, virtualPage, false, false);
//		addrint freePage;
//		if (type == DRAM){
//			myassert(!dramFreePageList.empty());
//			freePage = dramFreePageList.front();
//			dramFreePageList.pop_front();
//			dramMemorySizeInitial += pageSize;
//			dramMemorySizeUsedPerPid[pid] += pageSize;
//		} else if (type == PCM){
//			if (pcmFreePageList.empty()){
//				error("PCM free page list is empty");
//			}
//			freePage = pcmFreePageList.front();
//			pcmFreePageList.pop_front();
//			pcmMemorySizeInitial += pageSize;
//			pcmMemorySizeUsedPerPid[pid] += pageSize;
//		} else {
//			myassert(false);
//		}
//		PageMap::iterator it = pages[pid].emplace(virtualPage, PageEntry(freePage, type, engine->getTimestamp())).first;
//		bool ins = physicalPages.emplace(it->second.page, PhysicalPageEntry(pid, virtualPage)).second;
//		myassert(ins);
//	}
}

void HybridMemoryManager::process(const Event * event){
	uint64 timestamp = engine->getTimestamp();
	EventType type = static_cast<EventType>(event->getData());
	debug("(%d)", type);
	if (type == DEMOTE){
		updateMonitors();
		selectPolicyAndDemote();
	} else if (type == COMPLETE){

	} else if (type == ROLLBACK){

	} else if (type == COPY_PAGE){
		auto mig = migrations.begin();
		while(mig != migrations.end() && !mig->second.needsCopying){
			++mig;
		}
		myassert(mig != migrations.end());
		mig->second.needsCopying = false;
		memory->copyPage(mig->first, mig->second.destPhysicalPage);
		mig->second.startCopyTime = timestamp;
	} else if (type == UPDATE_PARTITION){
		if (coresFinished.size() != numCores){
			uint64 timeElapsed = timestamp - lastIntervalStart;
			lastIntervalStart = timestamp;
			partition->calculate(timeElapsed, instrCounters);
			for (unsigned i = 0; i < numProcesses; i++){
				instrCounters[i]->reset();
			}
			for (unsigned i = 0; i < partition->getNumPolicies(); i++){
				policies[i]->setNumDramPages(partition->getDramPages(i));
			}
			addEvent(partitionPeriod, UPDATE_PARTITION);
		}
	} else if (type == UNSTALL) {
		auto it = stalledRequests.begin();
		while(it != stalledRequests.end()){
			if (memory->access(it->request, this)){
				addrint page = it->page;
				it = stalledRequests.erase(it);
				auto mig = migrations.find(page);
				myassert(mig != migrations.end());
				mig->second.stalledRequestsLeft--;
				if (mig->second.flushRequestsLeft == 0 && mig->second.stalledRequestsLeft == 0){
					finishFlushing(page);
				}
			} else {
				break;
			}
		}
	} else {
		myassert(false);
	}
}

void HybridMemoryManager::selectPolicyAndDemote(){
	assert(!demoting);
	int previousPolicy = currentPolicy;
	do {
		currentPolicy = (currentPolicy + 1) % partition->getNumPolicies();
		demoting = startDemotion(currentPolicy);
	} while(!demoting && previousPolicy != currentPolicy);
	if (!demoting && coresFinished.size() != numCores) {
		addEvent(demoteTimeout, DEMOTE);
	}
}

bool HybridMemoryManager::startDemotion(int policy){
	uint64 timestamp = engine->getTimestamp();
	debug("(%d)", policy);
	int pid;
	addrint virtualPage;
	if (migrationTableSize < maxMigrationTableSize && policies[policy]->demote(&pid, &virtualPage)){
		PageMap::iterator it = pages[pid].find(virtualPage);
		myassert(it != pages[pid].end());
		if (it->second.isMigrating){
			myassert(isPcmPage(it->second.page));
			myassert(it->second.type == PCM);
			auto mit = migrations.find(it->second.page);
			myassert(mit != migrations.end());
			mit->second.rolledBack = true;
			if (mit->second.state == COPY) {
				memory->rollback(mit->first);
			}

			debug(": rollback: pid: %d, virtualPage: %lu, srcPhysPage: %lu, destPhysPage: %lu, dest: PCM, state: %d", pid, virtualPage, it->second.page, mit->second.destPhysicalPage, mit->second.state);

			dramPartialMigrations++;
			uint64 migrationTime = timestamp - mit->second.startMigrationTime;
			dramPartialMigrationTime += migrationTime;
			mit->second.startMigrationTime = timestamp;
		} else {
			myassert(isDramPage(it->second.page));
			myassert(it->second.type == DRAM);
			it->second.isMigrating = true;
			if (pcmFreePageList.empty()){
				error("PCM free page list is empty");
			}
			addrint destPhysPage = pcmFreePageList.front();
			pcmFreePageList.pop_front();

			State state;
			if (flushPolicy == FLUSH_PCM_BEFORE){
				state = FLUSH_BEFORE;
				it->second.stallOnAccess = true;
			} else if (flushPolicy == FLUSH_ONLY_AFTER){
				state = COPY;
				memory->copyPage(it->second.page, destPhysPage);
			} else if (flushPolicy == REMAP){
				state = COPY;
				memory->copyPage(it->second.page, destPhysPage);
			} else if (flushPolicy == CHANGE_TAG){
				state = COPY;
				memory->copyPage(it->second.page, destPhysPage);
			} else {
				myassert(false);
			}

			bool ins = migrations.emplace(it->second.page, MigrationEntry(pid, virtualPage, destPhysPage, PCM, state, timestamp)).second;
			myassert(ins);

			migrationTableSize++;

			if(state == FLUSH_BEFORE){
				flushPage(it->second.page);
			}

			debug(": demotion: pid: %d, virtualPage: %lu, srcPhysPage: %lu, destPhysPage: %lu, dest: PCM, state: %d", pid, virtualPage, it->second.page, destPhysPage, state);

			migrationEntriesSum += migrations.size();
			migrationEntriesCount++;
			pcmMigrationsPerPid[pid]++;
			pcmMemorySizeUsedPerPid[pid] += pageSize;

			//for per page statistics
	//		myassert(it->second.migrations.back().end == 0);
	//		it->second.migrations.back().end = timestamp;
	//		it->second.migrations.emplace_back(PCM, timestamp);
		}
		if(idle){
			idleTime += timestamp - lastStartIdleTime;
			idle = false;
		}
		return true;
	} else {
		if (!idle){
			lastStartIdleTime = timestamp;
			idle = true;
		}
		return false;
	}
}

void HybridMemoryManager::updateMonitors(){
	monitors.clear();
	progress.clear();
	memory->readCountsAndProgress(&monitors,&progress);

	if (partition->getNumPolicies() == 1){
		for (auto mit = monitors.begin(); mit != monitors.end(); ++mit){
			auto it = physicalPages.find(mit->page);
			if (it != physicalPages.end()){
				mit->pid = it->second.pid;
				mit->page = it->second.virtualPage;
			} else {
				warn("%lu: Why is this page (%lu) not in the physical map?", engine->getTimestamp(), mit->page);
				for (unsigned i = 0; i < pageSize/blockSize; i++){
					if (mit->readBlocks[i] != 0){
						cout << "read: " << getAddressFromBlock(mit->page, i) << endl;
					}
					if (mit->writtenBlocks[i] != 0){
						cout << "written: " << getAddressFromBlock(mit->page, i) << endl;
					}
				}
				myassert(false);
			}
		}
		for (auto pit = progress.begin();pit != progress.end(); ++pit){
			auto it = physicalPages.find(pit->page);
			if (it != physicalPages.end()){
				pit->pid = it->second.pid;
				pit->page = it->second.virtualPage;
			} else {
				warn("%lu: Why is this page (%lu) not in the physical map?", engine->getTimestamp(), pit->page);
				myassert(false);
			}
		}
		policies[0]->monitor(monitors, progress);
	} else {
		for (unsigned i = 0; i < partition->getNumPolicies(); i++){
			perPidMonitors[i].clear();
			perPidProgress[i].clear();
		}

		for(auto mit = monitors.begin(); mit != monitors.end(); ++mit){
			auto it = physicalPages.find(mit->page);
			if (it != physicalPages.end()){
				mit->pid = it->second.pid;
				mit->page = it->second.virtualPage;
				perPidMonitors[mit->pid].emplace_back(*mit);
			} else {
				warn("Why is this page (%lu) not in the physical map?", mit->page);
				myassert(false);
			}
		}

		for(auto pit = progress.begin(); pit != progress.end(); ++pit){
			auto it = physicalPages.find(pit->page);
			if (it != physicalPages.end()){
				pit->pid = it->second.pid;
				pit->page = it->second.virtualPage;
				perPidProgress[pit->pid].emplace_back(*pit);
			} else {
				warn("Why is this page (%lu) not in the physical map?", pit->page);
				myassert(false);
			}
		}
		for (unsigned i = 0; i < partition->getNumPolicies(); i++){
			policies[i]->monitor(perPidMonitors[i], perPidProgress[i]);
		}
	}



}

void HybridMemoryManager::accessCompleted(MemoryRequest *, IMemory *caller) {
	//all accesses are flush writebacks, which don't cause a call to accessCompleted
	myassert(false);
}

void HybridMemoryManager::unstall(IMemory *caller){
	addEvent(0, UNSTALL);
}

void HybridMemoryManager::drainCompleted(addrint page){
	auto mig = migrations.find(page);
	myassert(mig != migrations.end());
	mig->second.drainRequestsLeft--;
	if (mig->second.drainRequestsLeft == 0){
		flushPage(mig->first);
	}
}

void HybridMemoryManager::flushCompleted(addrint addr, bool dirty, IMemory *caller){
	uint64 timestamp = engine->getTimestamp();
	debug("(%lu, %s, %s)", addr, dirty?"dirty":"clean", caller->getName());
	addrint pageAddr = getIndex(addr);
	auto mig = migrations.find(pageAddr);
	myassert(mig != migrations.end());
	myassert(mig->second.state == FLUSH_BEFORE || mig->second.state == FLUSH_AFTER);
	auto fit = flushQueue.begin();
	while (fit != flushQueue.end() && fit->first != addr){
		++fit;
	}
	myassert(fit != flushQueue.end());
	myassert(fit->second);
	flushQueue.erase(fit);
	flushQueueSize--;
	if (dirty){
		if (!suppressFlushWritebacks){
			addrint offset = getOffset(addr);
			addrint writebackAddr = 0;
			if (mig->second.state == FLUSH_BEFORE){
				writebackAddr = getAddress(mig->first, offset);
			} else if (mig->second.state == FLUSH_AFTER){
				writebackAddr = getAddress(mig->second.destPhysicalPage, offset);
			} else {
				error("Wrong state: should be in FLUSH_BEFORE or FLUSH_AFTER when flushing completes");
			}

			MemoryRequest *req = new MemoryRequest(writebackAddr, blockSize, false, false, HIGH);
			if (!stalledRequests.empty() || !memory->access(req, this)){
				mig->second.stalledRequestsLeft++;
				stalledRequests.emplace_back(req, pageAddr);
			}
		}
		dirtyFlushedBlocks++;
	} else {
		cleanFlushedBlocks++;
	}
	mig->second.flushRequestsLeft--;
	if (mig->second.flushRequestsLeft == 0 && mig->second.stalledRequestsLeft == 0){
		finishFlushing(mig->first);
	}

	auto it = flushQueue.begin();
	while (it != flushQueue.end() && flushQueueSize < maxFlushQueueSize){
		if (!it->second){
			lastLevelCache->flush(it->first, blockSize, true, this);
			it->second = true;
			flushQueueSize++;
		}
		++it;
	}
}

void HybridMemoryManager::remapCompleted(addrint pageAddr, IMemory *caller){
	uint64 timestamp = engine->getTimestamp();
	debug("(%lu, %s)", pageAddr, caller->getName());
	auto mig = migrations.find(pageAddr);
	myassert(mig != migrations.end() &&  mig->second.state == FLUSH_AFTER);
	finishFlushing(pageAddr);
}

void HybridMemoryManager::tagChangeCompleted(addrint addr){
	addrint pageAddr = getIndex(addr);
	auto mig = migrations.find(pageAddr);
	myassert(mig != migrations.end() &&  mig->second.state == FLUSH_AFTER);
	myassert(addr == tagChangeQueue.front().first);
	tagChangeQueue.pop_front();
	mig->second.tagChangeRequestsLeft--;
	tagChanges++;
	if (mig->second.tagChangeRequestsLeft == 0){
		finishFlushing(mig->first);
	}
	if (!tagChangeQueue.empty()){
		lastLevelCache->changeTag(tagChangeQueue.front().first, tagChangeQueue.front().second, blockSize, this);
	}
}

void HybridMemoryManager::finishFlushing(addrint srcPhysicalPage){
	uint64 timestamp = engine->getTimestamp();
	debug("(%lu)", srcPhysicalPage);
	auto mig = migrations.find(srcPhysicalPage);
	myassert(mig != migrations.end() && (mig->second.state == FLUSH_BEFORE || mig->second.state == FLUSH_AFTER));
	PageMap::iterator it = pages[mig->second.pid].find(mig->second.virtualPage);
	myassert(it != pages[mig->second.pid].end());
	myassert((isDramPage(it->second.page) && it->second.type == DRAM) || (isPcmPage(it->second.page) && it->second.type == PCM));
	if (mig->second.rolledBack){
		myassert(mig->second.dest == DRAM);
		myassert(mig->second.state == FLUSH_AFTER);
		//this shouldn't happen because on demand migration does not set state to FLUSH_BEFORE
		it->second.page = mig->second.destPhysicalPage;
		it->second.type = mig->second.dest;
		myassert(it->second.type == DRAM);
		addrint destPhysPage = mig->first;
		int pid = mig->second.pid;
		addrint virtualPage = mig->second.virtualPage;

		it->second.stallOnAccess = false;

		//update per page statistics
		//it->second.migrations.back().endTransfer = timestamp;

		PhysicalPageMap::iterator ppit = physicalPages.find(mig->first);
		myassert(ppit != physicalPages.end());
		physicalPages.erase(ppit);
		bool ins = physicalPages.emplace(mig->second.destPhysicalPage, PhysicalPageEntry(mig->second.pid, mig->second.virtualPage)).second;
		myassert(ins);

		unstallCpus(mig->second.pid, mig->second.virtualPage);

		memory->finishMigration(mig->first);

		debug(": finished promotion (rolled back): pid: %d, virtualPage: %lu", mig->second.pid, mig->second.virtualPage);

		dramFullMigrations++;

		uint64 migrationTime = timestamp - mig->second.startMigrationTime;
		uint64 flushTime = timestamp - mig->second.startFlushTime;

		dramFullMigrationTime += migrationTime;
		dramFlushAfterTime += flushTime;

		migrations.erase(mig);

		memory->copyPage(it->second.page, destPhysPage);

		bool ins2 = migrations.emplace(it->second.page, MigrationEntry(pid, virtualPage, destPhysPage, PCM, COPY, timestamp)).second;
		myassert(ins2);

		debug(": starting rollback: pid: %d, virtualPage: %lu, srcPhysPage: %lu, destPhysPage: %lu, dest: PCM", pid, virtualPage, it->second.page, destPhysPage);

		migrationEntriesSum += migrations.size();
		migrationEntriesCount++;
		pcmMigrationsPerPid[pid]++;
		pcmMemorySizeUsedPerPid[pid] += pageSize;

		//for per page statistics
		//myassert(it->second.migrations.back().end == 0);
		//it->second.migrations.back().end = timestamp;
		//it->second.migrations.emplace_back(PCM, timestamp);
	} else {
		if (mig->second.state == FLUSH_BEFORE){
			mig->second.state = COPY;
			it->second.stallOnAccess = false;

			mig->second.needsCopying = true;
			addEvent(0, COPY_PAGE);
			unstallCpus(mig->second.pid, mig->second.virtualPage);

			if(mig->second.dest == DRAM){
				dramFlushBeforeTime += (timestamp - mig->second.startFlushTime);
			} else if (mig->second.dest == PCM){
				pcmFlushBeforeTime += (timestamp - mig->second.startFlushTime);
			} else {
				myassert(false);
			}
		} else if (mig->second.state == FLUSH_AFTER){
			if (mig->second.dest == PCM){
				demoting = false;
				addEvent(1, DEMOTE);
			}

			it->second.page = mig->second.destPhysicalPage;
			it->second.type = mig->second.dest;
			if (it->second.type == DRAM) {
				pcmFreePageList.emplace_back(mig->first);
				pcmMemorySizeUsedPerPid[mig->second.pid] -= pageSize;
			} else if (it->second.type == PCM){
				dramFreePageList.emplace_back(mig->first);
				dramMemorySizeUsedPerPid[mig->second.pid] -= pageSize;
			} else {
				myassert(false);
			}
			it->second.stallOnAccess = false;
			it->second.isMigrating = false;

			//update per page statistics
			//it->second.migrations.back().endTransfer = timestamp;

			PhysicalPageMap::iterator ppit = physicalPages.find(mig->first);
			myassert(ppit != physicalPages.end());
			physicalPages.erase(ppit);
			bool ins = physicalPages.emplace(mig->second.destPhysicalPage, PhysicalPageEntry(mig->second.pid, mig->second.virtualPage)).second;
			myassert(ins);

			unstallCpus(mig->second.pid, mig->second.virtualPage);

			policies[mig->second.pid]->done(mig->second.pid, mig->second.virtualPage);

			memory->finishMigration(mig->first);

			if (mig->second.dest == DRAM){
				debug(": finished promotion: pid: %d, virtualPage: %lu", mig->second.pid, mig->second.virtualPage);
			} else {
				debug(": finished demotion: pid: %d, virtualPage: %lu", mig->second.pid, mig->second.virtualPage);
			}

			uint64 migrationTime = timestamp - mig->second.startMigrationTime;
			uint64 flushTime = timestamp - mig->second.startFlushTime;

			if(mig->second.dest == DRAM){
				dramFullMigrations++;
				dramFullMigrationTime += migrationTime;
				dramFlushAfterTime += flushTime;
			} else if (mig->second.dest == PCM){
				pcmFullMigrations++;
				pcmFullMigrationTime += migrationTime;
				pcmFlushAfterTime += flushTime;
			} else {
				error("Invalid page type");
			}

			migrations.erase(mig);
			migrationTableSize--;

		} else {
			myassert(false);
		}
	}
}

void HybridMemoryManager::copyCompleted(addrint srcPhysicalPage){
	auto mig = migrations.find(srcPhysicalPage);
	myassert(mig != migrations.end());
	myassert(mig->second.state == COPY);
	uint64 timestamp = engine->getTimestamp();
	//debug("(): virtualPage: %lu: ", mig->second.virtualPage);
	PageMap::iterator it = pages[mig->second.pid].find(mig->second.virtualPage);
	myassert(it != pages[mig->second.pid].end());
	myassert((isDramPage(it->second.page) && it->second.type == DRAM) || (isPcmPage(it->second.page) && it->second.type == PCM));

	if (mig->second.rolledBack){
		myassert(mig->second.dest == DRAM);
		myassert(it->second.type == PCM);
		dramFreePageList.emplace_back(mig->second.destPhysicalPage);
		dramMemorySizeUsedPerPid[mig->second.pid] -= pageSize;
		it->second.isMigrating = false;
		myassert(!it->second.stallOnAccess);

		policies[mig->second.pid]->done(mig->second.pid, mig->second.virtualPage);

		memory->finishMigration(mig->first);

		debug(": finished rollback: pid: %d, virtualPage: %lu", mig->second.pid, mig->second.virtualPage);

		pcmPartialMigrations++;
		uint64 migrationTime = timestamp - mig->second.startMigrationTime;
		pcmPartialMigrationTime += migrationTime;

		demoting = false;
		addEvent(1, DEMOTE);
		migrations.erase(mig);
		migrationTableSize--;
	} else {
		mig->second.state = FLUSH_AFTER;
		it->second.stallOnAccess = true;
		if (flushPolicy == FLUSH_PCM_BEFORE || flushPolicy == FLUSH_ONLY_AFTER){
			for (auto cit = cpus.begin(); cit != cpus.end(); ++cit){
				mig->second.drainRequestsLeft++;
				(*cit)->drain(mig->first, this);
			}
		} else if (flushPolicy == REMAP){
			lastLevelCache->remap(mig->first, mig->second.destPhysicalPage, this);
		} else if (flushPolicy == CHANGE_TAG){
			changeTags(mig->first, mig->second.destPhysicalPage);
		} else {
			myassert(false);
		}

		mig->second.startFlushTime = timestamp;

		uint64 cpTime = timestamp - mig->second.startCopyTime;
		if(mig->second.dest == DRAM){
			dramCopyTime += cpTime;
		} else if (mig->second.dest == PCM){
			pcmCopyTime += cpTime;
		} else {
			myassert(false);
		}
	}

}

void HybridMemoryManager::flushPage(addrint page){
	auto mig = migrations.find(page);
	myassert(mig != migrations.end());
	myassert(mig->second.state == FLUSH_BEFORE || mig->second.state == FLUSH_AFTER);
	for (addrint offset = 0; offset < pageSize; offset += blockSize){
		addrint addr = getAddress(page,offset);
		flushQueue.emplace_back(addr, false);
		mig->second.flushRequestsLeft++;
	}
	auto it = flushQueue.begin();
	while (it != flushQueue.end() && flushQueueSize < maxFlushQueueSize){
		if (!it->second){
			lastLevelCache->flush(it->first, blockSize, true, this);
			it->second = true;
			flushQueueSize++;
		}
		++it;
	}
	mig->second.startFlushTime = engine->getTimestamp();
}

void HybridMemoryManager::changeTags(addrint oldPage, addrint newPage){
	myassert(false);
	//TODO: change tag change logic so that tag changes happen concurrently
	//TODO: fix tag change logic so that finishFlushing is called for the correct page
	for (addrint offset = 0; offset < pageSize; offset += blockSize){
		tagChangeQueue.emplace_back(make_pair(getAddress(oldPage,offset), getAddress(newPage,offset)));
	}
	lastLevelCache->changeTag(tagChangeQueue.front().first, tagChangeQueue.front().second, blockSize, this);
}

void HybridMemoryManager::unstallCpus(int pid, addrint virtualPage){
	//uint64 timestamp = engine->getTimestamp();
	//debug("(%d, %lu)", pid, virtualPage);
	StalledCpuMap::iterator it = stalledCpus[pid].find(virtualPage);
	if (it != stalledCpus[pid].end()){
		for (list<CPU*>::iterator itList = it->second.begin(); itList != it->second.end(); itList++){
			(*itList)->resume();
		}
		stalledCpus[pid].erase(it);
	}

}

bool HybridMemoryManager::arePagesCompatible(addrint page1, addrint page2) const {
	if (flushPolicy == FLUSH_PCM_BEFORE || flushPolicy == FLUSH_ONLY_AFTER || REMAP){
		return true;
	} else if (flushPolicy == CHANGE_TAG){
		return lastLevelCache->isSameSet(getAddress(page1, 0), getAddress(page2, 0));
	} else {
		myassert(false);
		return false;
	}
}

void HybridMemoryManager::processInterrupt(Counter* counter){
	myassert(periodType == "instructions");
	myassert(instrCounters[0] == counter);
	if (coresFinished.size() != numCores){
		uint64 timestamp = engine->getTimestamp();
		uint64 timeElapsed = timestamp - lastIntervalStart;
		lastIntervalStart = timestamp;
		partition->calculate(timeElapsed, instrCounters);
		for (unsigned i = 0; i < numCores; i++){
			instrCounters[i]->reset();
		}
		for (unsigned i = 0; i < partition->getNumPolicies(); i++){
			policies[i]->setNumDramPages(partition->getDramPages(i));
		}
	}
}

int HybridMemoryManager::getPidOfAddress(addrint addr){
	PhysicalPageMap::iterator it = physicalPages.find(getIndex(addr));
	if (it == physicalPages.end()){
		return -1;
	} else {
		return it->second.pid;
	}
}

void HybridMemoryManager::addCpu(CPU *cpu){
	cpus.emplace_back(cpu);
}

void HybridMemoryManager::addInstrCounter(Counter* counter, unsigned pid){
	myassert(instrCounters.size() == pid);
	instrCounters.emplace_back(counter);
	policies[pid]->setInstrCounter(counter);
	if (periodType == "instructions"){
		if (pid == 0){
			counter->setInterrupt(partitionPeriod, this);
		}
	}
}

HybridMemoryManager::~HybridMemoryManager(){
	delete [] pages;
	delete [] stalledCpus;
}



//Old Hybrid Memory Manager:

OldHybridMemoryManager::OldHybridMemoryManager(
	Engine *engineArg,
	StatContainer *statCont,
	uint64 debugStartArg,
	unsigned numCoresArg,
	unsigned numProcessesArg,
	Cache *lastLevelCacheArg,
	OldHybridMemory *memoryArg,
	const vector<IOldMigrationPolicy*>& policiesArg,
	IPartition *partitionArg,
	unsigned blockSizeArg,
	unsigned pageSizeArg,
	MigrationMechanism mechanismArg,
	MonitoringType monitoringTypeArg,
	MonitoringLocation monitoringLocationArg,
	FlushPolicy flushPolicyArg,
	unsigned flushQueueSizeArg,
	bool suppressFlushWritebacksArg,
	uint64 partitionPeriodArg,
	const string& periodTypeArg,
	double baseMigrationRateArg,
	bool perPageStatsArg,
	string perPageStatsFilenameArg,
	bool traceArg,
	string tracePrefixArg,
	uint64 tracePeriodArg) :
		name("HybridMemoryManager"),
		engine(engineArg),
		debugStart(debugStartArg),
		numCores(numCoresArg),
		numProcesses(numProcessesArg),
		lastLevelCache(lastLevelCacheArg),
		memory(memoryArg),
		policies(policiesArg),
		partition(partitionArg),
		mechanism(mechanismArg),
		monitoringType(monitoringTypeArg),
		monitoringLocation(monitoringLocationArg),
		flushPolicy(flushPolicyArg),
		flushQueueSize(flushQueueSizeArg),
		suppressFlushWritebacks(suppressFlushWritebacksArg),
		partitionPeriod(partitionPeriodArg),
		periodType(periodTypeArg),
		baseMigrationRate(baseMigrationRateArg),
		perPageStats(perPageStatsArg),
		perPageStatsFilename(perPageStatsFilenameArg),
		trace(traceArg),
		tracePeriod(tracePeriodArg),

		dramMigrations(statCont, "manager_dram_migrations", "Number of DRAM migrations", 0),
		pcmMigrations(statCont, "manager_pcm_migrations", "Number of PCM migrations", 0),
		migrations(statCont, "manager_total_migrations", "Total number of migrations", &dramMigrations, &pcmMigrations),

		cleanFlushedBlocks(statCont, "manager_clean_flushed_blocks", "Number of clean flushed blocks", 0),
		dirtyFlushedBlocks(statCont, "manager_dirty_flushed_blocks", "Number of dirty flushed blocks", 0),
		tagChanges(statCont, "manager_tag_changes", "Number of tag changes", 0),

		dramMigrationTime(statCont, "manager_dram_migration_time", "Number of cycles migrating pages to DRAM", 0),
		pcmMigrationTime(statCont, "manager_pcm_migration_time", "Number of cycles migrating pages to PCM", 0),
		migrationTime(statCont, "manager_total_migration_time", "Total number of cycles migrating all pages", &dramMigrationTime, &pcmMigrationTime),

		dramFlushBeforeTime(statCont, "manager_dram_flush_before_time", "Number of cycles flushing the cache before migrations to DRAM", 0),
		pcmFlushBeforeTime(statCont, "manager_pcm_flush_before_time", "Number of cycles flushing the cache before migrations to PCM", 0),
		flushBeforeTime(statCont, "manager_flush_before_time", "Number of cycles flushing the cache before migrations", &dramFlushBeforeTime, &pcmFlushBeforeTime),

		dramFlushAfterTime(statCont, "manager_dram_flush_after_time", "Number of cycles flushing the cache after migrations to DRAM", 0),
		pcmFlushAfterTime(statCont, "manager_pcm_flush_after_time", "Number of cycles flushing the cache after migrations to PCM", 0),
		flushAfterTime(statCont, "manager_flush_after_time", "Number of cycles flushing the cache after migrations", &dramFlushAfterTime, &pcmFlushAfterTime),

		dramFlushTime(statCont, "manager_dram_flush_time", "Number of cycles flushing the cache due to migrations to DRAM", &dramFlushBeforeTime, &dramFlushAfterTime),
		pcmFlushTime(statCont, "manager_pcm_flush_time", "Number of cycles flushing the cache due to migrations to PCM", &pcmFlushBeforeTime, &pcmFlushAfterTime),
		flushTime(statCont, "manager_total_flush_time", "Total number of cycles flushing the cache", &dramFlushTime, &pcmFlushTime),

		dramCopyTime(statCont, "manager_dram_copy_time", "Number of cycles copying pages during migrations to DRAM", 0),
		pcmCopyTime(statCont, "manager_pcm_copy_time", "Number of cycles copying pages during migrations to PCM", 0),
		copyTime(statCont, "manager_copy_time", "Number of cycles copying pages during migrations", &dramCopyTime, &pcmCopyTime),

		idleTime(statCont, "manager_idle_time", "Number of cycles the migration policy is idle", 0),

		avgDramMigrationTime(statCont, "manager_avg_dram_migration_time", "Average number of cycles per migration to DRAM", &dramMigrationTime, &dramMigrations),
		avgPcmMigrationTime(statCont, "manager_avg_pcm_migration_time", "Average number of cycles per migration to PCM", &pcmMigrationTime, &pcmMigrations),
		avgMigrationTime(statCont, "manager_avg_migration_time", "Average number of cycles per migration", &migrationTime, &migrations),

		avgDramFlushBeforeTime(statCont, "manager_avg_dram_flush_before_time", "Average number of cycles flushing the cache before each migration to DRAM", &dramFlushBeforeTime, &dramMigrations),
		avgPcmFlushBeforeTime(statCont, "manager_avg_pcm_flush_before_time", "Average number of cycles flushing the cache before each migration to PCM", &pcmFlushBeforeTime, &pcmMigrations),
		avgFlushBeforeTime(statCont, "manager_avg_flush_before_time", "Average number of cycles flushing the cache per migration", &flushBeforeTime, &migrations),

		avgDramFlushAfterTime(statCont, "manager_avg_dram_flush_after_time", "Average number of cycles flushing the cache after each migration to DRAM", &dramFlushAfterTime, &dramMigrations),
		avgPcmFlushAfterTime(statCont, "manager_avg_pcm_flush_after_time", "Average number of cycles flushing the cache after each migration to PCM", &pcmFlushAfterTime, &pcmMigrations),
		avgFlushAfterTime(statCont, "manager_avg_flush_after_time", "Average number of cycles flushing the cache per migration", &flushAfterTime, &migrations),


		avgDramFlushTime(statCont, "manager_avg_dram_flush_time", "Average number of cycles flushing the cache per DRAM migration", &dramFlushTime, &dramMigrations),
		avgPcmFlushTime(statCont, "manager_avg_pcm_flush_time", "Average number of cycles flushing the cache per PCM migration", &pcmFlushTime, &pcmMigrations),
		avgFlushTime(statCont, "manager_avg_flush_time", "Average number of cycles flushing the cache per migration", &flushTime, &migrations),

		avgDramCopyTime(statCont, "manager_avg_dram_copy_time", "Average number of cycles copying pages per DRAM migration", &dramCopyTime, &dramMigrations),
		avgPcmCopyTime(statCont, "manager_avg_pcm_copy_time", "Average number of cycles copying pages per PCM migration", &pcmCopyTime, &pcmMigrations),
		avgCopyTime(statCont, "manager_avg_copy_time", "Average number of cycles copying pages per migration", &copyTime, &migrations),

		dramMemorySize(statCont, "manager_dram_memory_size", "Size of DRAM memory available to the memory manager", this, &OldHybridMemoryManager::getDramMemorySize),
		dramMemorySizeUsed(statCont, "manager_dram_memory_size_used", "Size of DRAM memory used by the memory manager", this, &OldHybridMemoryManager::getDramMemorySizeUsed),
		pcmMemorySize(statCont, "manager_pcm_memory_size", "Size of PCM memory available to the memory manager", this, &OldHybridMemoryManager::getPcmMemorySize),
		pcmMemorySizeUsed(statCont, "manager_pcm_memory_size_used", "Size of PCM memory used by the memory manager", this, &OldHybridMemoryManager::getPcmMemorySizeUsed),

		dramMemorySizeUsedPerPid(statCont, numProcesses, "dram_memory_size_per_pid", "Size of DRAM memory used by process", true),
		pcmMemorySizeUsedPerPid(statCont, numProcesses, "pcm_memory_size_per_pid", "Size of PCM memory used by process", true),
		totalMemorySizeUsedPerPid(statCont, "total_memory_size_per_pid", "Size of total memory used by process", &dramMemorySizeUsedPerPid, &pcmMemorySizeUsedPerPid),

		dramMigrationsPerPid(statCont, numProcesses, "manager_dram_migrations_per_pid", "Number of DRAM migrations by process"),
		pcmMigrationsPerPid(statCont, numProcesses, "manager_pcm_migrations_per_pid", "Number of PCM migrations by process"),
		totalMigrationsPerPid(statCont, "manager_total_migrations_per_pid", "Number of total migrations by process", &dramMigrationsPerPid, &pcmMigrationsPerPid)

{


	memory->setManager(this);

	unsigned logBlockSize = static_cast<unsigned>(logb(blockSizeArg));
	blockSize = 1 << logBlockSize;

	unsigned logPageSize = (unsigned)logb(pageSizeArg);
	pageSize = 1 << logPageSize;
	numDramPages = memory->getDramSize() / pageSize;
	dramSize = numDramPages * pageSize;
	numPcmPages = memory->getPcmSize() / pageSize;
	pcmSize = numPcmPages * pageSize;

	offsetWidth = logPageSize;
	indexWidth = sizeof(addrint)*8 - offsetWidth;

	offsetMask = 0;
	for (unsigned i = 0; i < offsetWidth; i++){
		offsetMask |= (addrint)1U << i;
	}
	indexMask = 0;
	for (unsigned i = offsetWidth; i < indexWidth+offsetWidth; i++){
		indexMask |= (addrint)1U << i;
	}

	blockOffsetWidth = (uint64)logb(blockSize);

	firstDramAddress = 0;
	onePastLastDramAddress = dramSize;
	firstPcmAddress = dramSize;
	onePastLastPcmAddress = dramSize + pcmSize;
	//TODO: fix addrint size problem

	firstDramPage = getIndex(firstDramAddress);
	onePastLastDramPage = getIndex(onePastLastDramAddress);
	firstPcmPage = getIndex(firstPcmAddress);
	onePastLastPcmPage = getIndex(onePastLastPcmAddress);

	for(addrint page = firstDramPage; page < onePastLastDramPage; page++){
		dramFreePageList.emplace_back(page);
	}

	for(addrint page = firstPcmPage; page < onePastLastPcmPage; page++){
		pcmFreePageList.emplace_back(page);
	}

	pages = new PageMap[numProcesses];

	idle = false;

	state = NOT_MIGRATING;

	numPolicies = policies.size();
	if (numPolicies == 1){
		for (unsigned i = 0; i < numProcesses; i++){
			pidToPolicy[i] = 0;
		}
	} else  if (numPolicies == numProcesses) {
		for (unsigned i = 0; i < numProcesses; i++){
			pidToPolicy[i] = i;
		}
	} else {
		error("Not yet implemented");
	}

	currentPolicy = 0;

	tokens.resize(numPolicies, 0);
	active.resize(numPolicies, false);

	stalledCpus = new StalledCpuMap[numProcesses];

	lastIntervalStart = 0;

	cycleCounters.resize(numProcesses, CycleCounter(engineArg));

	dramMigrationsCounters.resize(numProcesses);
	pcmMigrationsCounters.resize(numProcesses);

	dramMigrationTimeCounters.resize(numProcesses);
	pcmMigrationTimeCounters.resize(numProcesses);

	if (perPageStats){
		myassert(numProcesses == 1);
	}

	if (trace){
		myassert(numCores == numProcesses);
		for (unsigned i = 0; i < numProcesses; i++){
			ostringstream fileName;
			fileName << tracePrefixArg << "_" << i << ".trace";
			traceFiles.emplace_back(new ofstream(fileName.str().c_str()));
		}
	} else {
		if (periodType == "cycles"){
			addEvent(0, UPDATE_PARTITION);
		}
	}

}

/*
 * Returns whether the CPU should stall
 */
bool OldHybridMemoryManager::access(int pid, addrint virtualAddr, bool read, bool instr, addrint *physicalAddr, CPU *cpu){

	uint64 timestamp = engine->getTimestamp();
	//debug("(%d, %lu, %s, %s)", pid, virtualAddr, read ? "read" : "write", instr ? "instr" : "data");
	addrint virtualPage = getIndex(virtualAddr);
	//cout << pages[pid].size() <<endl;
	PageMap::iterator it = pages[pid].find(virtualPage);
	if (it == pages[pid].end()){
		PageType type = policies[pidToPolicy[pid]]->allocate(pid, virtualPage, read, instr);
		addrint freePage;
		if (type == DRAM){
			myassert(!dramFreePageList.empty());
			freePage = dramFreePageList.front();
			dramFreePageList.pop_front();
			dramMemorySizeUsedPerPid[pid] += pageSize;
		} else if (type == PCM){
			if (pcmFreePageList.empty()){
				error("PCM free page list is empty");
			}
			freePage = pcmFreePageList.front();
			pcmFreePageList.pop_front();
			pcmMemorySizeUsedPerPid[pid] += pageSize;
		} else {
			myassert(false);
		}
		it = pages[pid].emplace(virtualPage, PageEntry(freePage, type, timestamp)).first;
		bool ins = physicalPages.emplace(it->second.page, PhysicalPageEntry(pid, virtualPage)).second;
		myassert(ins);
	}
	myassert((isDramPage(it->second.page) && it->second.type == DRAM) || (isPcmPage(it->second.page) && it->second.type == PCM));

	if (monitoringLocation == BEFORE_CACHES){
		if (monitoringType == READS){
			if (read){
				policies[pidToPolicy[pid]]->monitor(pid, virtualPage);
			}
		} else if (monitoringType == WRITES){
			if (!read){
				policies[pidToPolicy[pid]]->monitor(pid, virtualPage);
			}
		} else if (monitoringType == ACCESSES){
			policies[pidToPolicy[pid]]->monitor(pid, virtualPage);
		} else {
			myassert(false);
		}
	}

	if (state == NOT_MIGRATING || state == WAITING){
		selectPolicyAndMigrate();
	}
/*	if(state == COPY){
		cout<<"COPY\n";
	}*/

	if((it->second.stallOnWrite && !read) || it->second.stallOnAccess){
		stalledCpus[pid][virtualPage].emplace_back(cpu);
		debug(": stalled on access to %lu", virtualPage);
		return true;
	} else {
		*physicalAddr = getAddress(it->second.page, getOffset(virtualAddr));
/*		if(physicalAddr==0)
			cout<<"IT IS ZERO!"<<endl;*/
		return false;
	}

}

void OldHybridMemoryManager::finish(int core){
	coresFinished.insert(core);

	if (perPageStats){
		ofstream out(perPageStatsFilename.c_str());
		out << "#pid virtualPageAddress numMigrations" << endl;
		out << "#dest(0:DRAM;1:PCM) start end endTransfer readsWhileMigrating writesWhileMigrating reads writes readBlocks writtenBlocks accessedBlocks" << endl;

		for (auto it = pages[0].begin(); it != pages[0].end(); ++it){
			myassert(it->second.migrations.back().end == 0);
			it->second.migrations.back().end = engine->getTimestamp();
			out << "0 " << it->first << " " << it->second.migrations.size() << endl;
			for (auto it2 = it->second.migrations.begin(); it2 != it->second.migrations.end(); ++it2){
				out << it2->dest << " ";
				out << it2->start << " ";
				out << it2->end << " ";
				out << it2->endTransfer << " ";
				out << it2->readsWhileMigrating << " ";
				out << it2->writesWhileMigrating << " ";
				out << it2->reads << " ";
				out << it2->writes << " ";
				out << it2->readBlocks.count() << " ";
				out << it2->writtenBlocks.count() << " ";
				out << (it2->readBlocks | it2->writtenBlocks).count() << endl;
			}
		}

	}


	if (trace){
		*traceFiles[core] << "instructions " << instrCounters[core]->getTotalValue() << ", ";
		*traceFiles[core] << "cycles " << cycleCounters[core].getValue() << ", ";
		*traceFiles[core] << "dram_reads " << dramReadsCounters[core]->getValue() << ", ";
		*traceFiles[core] << "dram_writes " << dramWritesCounters[core]->getValue() << ", ";
		*traceFiles[core] << "pcm_reads " << pcmReadsCounters[core]->getValue() << ", ";
		*traceFiles[core] << "pcm_writes " << pcmWritesCounters[core]->getValue() << ", ";
		*traceFiles[core] << "dram_read_time " << dramReadTimeCounters[core]->getValue() << ", ";
		*traceFiles[core] << "dram_write_time " << dramWriteTimeCounters[core]->getValue() << ", ";
		*traceFiles[core] << "pcm_read_time " << pcmReadTimeCounters[core]->getValue() << ", ";
		*traceFiles[core] << "pcm_write_time " << pcmWriteTimeCounters[core]->getValue() << ", ";
		*traceFiles[core] << "dram_migrations " << dramMigrationsCounters[core].getValue() << ", ";
		*traceFiles[core] << "pcm_migrations " << pcmMigrationsCounters[core].getValue() << ", ";
		*traceFiles[core] << "dram_migration_time " << dramMigrationTimeCounters[core].getValue() << ", ";
		*traceFiles[core] << "pcm_migration_time " << pcmMigrationTimeCounters[core].getValue() << endl;

		instrCounters[core]->reset();
		cycleCounters[core].reset();
		dramReadsCounters[core]->reset();
		dramWritesCounters[core]->reset();
		pcmReadsCounters[core]->reset();
		pcmWritesCounters[core]->reset();
		dramReadTimeCounters[core]->reset();
		dramWriteTimeCounters[core]->reset();
		pcmReadTimeCounters[core]->reset();
		pcmWriteTimeCounters[core]->reset();
		dramMigrationsCounters[core].reset();
		pcmMigrationsCounters[core].reset();
		dramMigrationTimeCounters[core].reset();
		pcmMigrationTimeCounters[core].reset();
	} else{
		if (periodType == "instructions"){
			if (coresFinished.size() != numCores){
				uint64 timestamp = engine->getTimestamp();
				uint64 timeElapsed = timestamp - lastIntervalStart;
				lastIntervalStart = timestamp;
				partition->calculate(timeElapsed, instrCounters);
				for (unsigned i = 0; i < numCores; i++){
					instrCounters[i]->reset();
				}
				for (unsigned i = 0; i < numPolicies; i++){
					policies[i]->changeNumDramPages(partition->getDramPages(i));
				}
			}
		}
	}
}

void OldHybridMemoryManager::allocate(const vector<string>& filenames){
	static long allocateCall = 0;
	if(allocateCall %100 == 0)
		cout<<endl<<"allocate call in memory manager: " << allocateCall << endl;
	allocateCall++;
	uint64 dramPages = 0;
	for (unsigned i = 0; i < partition->getNumPolicies(); i++){
		dramPages += partition->getDramPages(i);
	}
	uint64 dramPagesPerProcess = dramPages/filenames.size();
	vector<unique_ptr<ifstream> >ifs;
	for (unsigned pid = 0; pid < filenames.size(); pid++){
		ifs.emplace_back(new ifstream(filenames[pid]));
		addrint virtualPage;
		uint64 count = 0;

		cout<<filenames[pid];
	//	cout<<*ifs[pid]>>virtualPage;
		bool a = (bool)(*ifs[pid] >> virtualPage);
		cout <<a;
		while (count < dramPagesPerProcess &&
				*ifs[pid] >> virtualPage){
			*ifs[pid] >> virtualPage;
			PageType type = policies[pidToPolicy[pid]]->allocate(pid, virtualPage, false, false);
			myassert(type == DRAM);
			myassert(!dramFreePageList.empty());
			addrint freePage = dramFreePageList.front();
			dramFreePageList.pop_front();
			dramMemorySizeUsedPerPid[pid] += pageSize;
			PageMap::iterator it = pages[pid].emplace(virtualPage, PageEntry(freePage, type, engine->getTimestamp())).first;
			bool ins = physicalPages.emplace(it->second.page, PhysicalPageEntry(pid, virtualPage)).second;
			myassert(ins);
			count++;
		}
	}
	for (unsigned pid = 0; pid < filenames.size(); pid++){
		addrint virtualPage;
		while (*ifs[pid] >> virtualPage){
			PageType type = policies[pidToPolicy[pid]]->allocate(pid, virtualPage, false, false);
			addrint freePage;
			if (type == DRAM){
				myassert(!dramFreePageList.empty());
				freePage = dramFreePageList.front();
				dramFreePageList.pop_front();
				dramMemorySizeUsedPerPid[pid] += pageSize;
			} else if (type == PCM){
				if (pcmFreePageList.empty()){
					error("PCM free page list is empty");
				}
				freePage = pcmFreePageList.front();
				pcmFreePageList.pop_front();
				pcmMemorySizeUsedPerPid[pid] += pageSize;
			} else {
				myassert(false);
			}
			PageMap::iterator it = pages[pid].emplace(virtualPage, PageEntry(freePage, type, engine->getTimestamp())).first;
			bool ins = physicalPages.emplace(it->second.page, PhysicalPageEntry(pid, virtualPage)).second;
			myassert(ins);
		}
	}
//	ifstream ifs(filename.c_str());
//	addrint virtualPage;
//	while (ifs >> virtualPage){
//		PageType type = policies[pidToPolicy[pid]]->allocate(pid, virtualPage, false, false);
//		addrint freePage;
//		if (type == DRAM){
//			myassert(!dramFreePageList.empty());
//			freePage = dramFreePageList.front();
//			dramFreePageList.pop_front();
//			dramMemorySizeUsedPerPid[pid] += pageSize;
//		} else if (type == PCM){
//			if (pcmFreePageList.empty()){
//				error("PCM free page list is empty");
//			}
//			freePage = pcmFreePageList.front();
//			pcmFreePageList.pop_front();
//			pcmMemorySizeUsedPerPid[pid] += pageSize;
//		} else {
//			myassert(false);
//		}
//		PageMap::iterator it = pages[pid].emplace(virtualPage, PageEntry(freePage, type, engine->getTimestamp())).first;
//		bool ins = physicalPages.emplace(it->second.page, PhysicalPageEntry(pid, virtualPage)).second;
//		myassert(ins);
//	}
}

void OldHybridMemoryManager::monitorPhysicalAccess(addrint addr, bool read, bool instr){
	addrint page = getIndex(addr);
	auto it = physicalPages.find(page);
	if (it != physicalPages.end()){
		//per page statistics
		auto it2 = pages[it->second.pid].find(it->second.virtualPage);
		MigrationInfo &last = it2->second.migrations.back();
		if (read){
			if(it2->second.isMigrating){
				last.readsWhileMigrating++;
			}
			last.reads++;
			last.readBlocks.set(getBlock(addr));
		} else {
			if(it2->second.isMigrating){
				last.writesWhileMigrating++;
			}
			last.writes++;
			last.writtenBlocks.set(getBlock(addr));
		}
	} else {
		cout << "Couldn't find page in physical page map" << endl;
		myassert(false);
	}

	//monitoring
	if (monitoringLocation == AFTER_CACHES){
		if ((state == FLUSH_BEFORE || state == COPY || state == FLUSH_AFTER) && page == currentMigration.destPhysicalPage){
			static int q=0;
			cout<<"FLUSH__MEMANAGER "<<q;
			q++;
			//This access is due to a flush redirect, so we don't monitor
			return;
		}
		if (it != physicalPages.end()){
			if (monitoringType == READS){
				if (read){
					policies[pidToPolicy[it->second.pid]]->monitor(it->second.pid, it->second.virtualPage);
				}
			} else if (monitoringType == WRITES){
				if (!read){
					policies[pidToPolicy[it->second.pid]]->monitor(it->second.pid, it->second.virtualPage);
				}
			} else if (monitoringType == ACCESSES){
				policies[pidToPolicy[it->second.pid]]->monitor(it->second.pid, it->second.virtualPage);
			}
		}
	}
}

bool OldHybridMemoryManager::startMigration(int pid){
	myassert(state == NOT_MIGRATING || state == WAITING);
	myassert(flushQueue.empty());
	uint64 timestamp = engine->getTimestamp();
	//debug("(%d)", pid);
	if (policies[pid]->migrate(&currentMigration.pid, &currentMigration.virtualPage)){
		//myassert(pid == currentMigration.pid);
		PageMap::iterator it = pages[currentMigration.pid].find(currentMigration.virtualPage);
		it->second.isMigrating = true;
		myassert(it != pages[currentMigration.pid].end());
		myassert((isDramPage(it->second.page) && it->second.type == DRAM) || (isPcmPage(it->second.page) && it->second.type == PCM));
		currentMigration.srcPhysicalPage = it->second.page;
		if (it->second.type == DRAM) {
			if (pcmFreePageList.empty()){
				error("PCM free page list is empty");
			}
			currentMigration.destPhysicalPage = pcmFreePageList.front();
			pcmFreePageList.pop_front();
			currentMigration.dest = PCM;
			pcmMigrations++;
			pcmMigrationsPerPid[pid]++;
			pcmMigrationsCounters[pid]++;
			pcmMemorySizeUsedPerPid[currentMigration.pid] += pageSize;
		} else if (it->second.type == PCM) {
			myassert(!dramFreePageList.empty());
			currentMigration.destPhysicalPage = dramFreePageList.front();
			dramFreePageList.pop_front();
			currentMigration.dest = DRAM;
			dramMigrations++;
			dramMigrationsPerPid[pid]++;
			dramMigrationsCounters[pid]++;
			dramMemorySizeUsedPerPid[currentMigration.pid] += pageSize;
		} else {
			myassert(false);
		}

		debug(": started migration: pid: %d, virtualPage: %lu, srcPhysPage: %lu, destPhysPage: %lu, dest: %s", currentMigration.pid, currentMigration.virtualPage, currentMigration.srcPhysicalPage, currentMigration.destPhysicalPage, currentMigration.dest == DRAM?"DRAM":"PCM");

		if (mechanism == PAUSE){
			state = FLUSH_BEFORE;
			it->second.stallOnAccess = true;
			flushPage(currentMigration.srcPhysicalPage);
		} else if (mechanism == PIN){
			if (flushPolicy == FLUSH_PCM_BEFORE){
				if (currentMigration.dest == PCM){
					state = FLUSH_BEFORE;
					it->second.stallOnAccess = true;
					pinPage(currentMigration.srcPhysicalPage);
					//flushPage(currentMigration.srcPhysicalPage);
					drainRequestsLeft = 0;
					for (auto cit = cpus.begin(); cit != cpus.end(); ++cit){
						drainRequestsLeft++;
						(*cit)->drain(currentMigration.srcPhysicalPage, this);
					}
				} else {
					pinPage(currentMigration.srcPhysicalPage);
					if (writebacks.empty()){
						state = COPY;
						memory->copyPage(currentMigration.srcPhysicalPage, currentMigration.destPhysicalPage);
						lastStartCopyTime = timestamp;
					} else {
						state = FLUSH_BEFORE;
						it->second.stallOnAccess = true;
					}
				}
			} else if (flushPolicy == FLUSH_ONLY_AFTER){
				pinPage(currentMigration.srcPhysicalPage);
				if (writebacks.empty()){
					state = COPY;
					memory->copyPage(currentMigration.srcPhysicalPage, currentMigration.destPhysicalPage);
					lastStartCopyTime = timestamp;
				} else {
					state = FLUSH_BEFORE;
					it->second.stallOnAccess = true;
				}
			} else if (flushPolicy == REMAP){
				pinPage(currentMigration.srcPhysicalPage);
				if (writebacks.empty()){
					state = COPY;
					memory->copyPage(currentMigration.srcPhysicalPage, currentMigration.destPhysicalPage);
					lastStartCopyTime = timestamp;
				} else {
					state = FLUSH_BEFORE;
					it->second.stallOnAccess = true;
				}
			} else if (flushPolicy == CHANGE_TAG){
				pinPage(currentMigration.srcPhysicalPage);
				if (writebacks.empty()){
					state = COPY;
					memory->copyPage(currentMigration.srcPhysicalPage, currentMigration.destPhysicalPage);
					lastStartCopyTime = timestamp;
				} else {
					state = FLUSH_BEFORE;
					it->second.stallOnAccess = true;
				}
			} else {
				myassert(false);
			}
		} else if (mechanism == REDIRECT){
			if (flushPolicy == FLUSH_PCM_BEFORE){
				if (currentMigration.dest == PCM){
					state = FLUSH_BEFORE;
					it->second.stallOnAccess = true;
					flushPage(currentMigration.srcPhysicalPage);
				} else {
					state = COPY;
					memory->copyPage(currentMigration.srcPhysicalPage, currentMigration.destPhysicalPage);
					lastStartCopyTime = timestamp;
				}
			} else if (flushPolicy == FLUSH_ONLY_AFTER){
				state = COPY;
				memory->copyPage(currentMigration.srcPhysicalPage, currentMigration.destPhysicalPage);
				lastStartCopyTime = timestamp;
			} else if (flushPolicy == REMAP){
				state = COPY;
				memory->copyPage(currentMigration.srcPhysicalPage, currentMigration.destPhysicalPage);
				lastStartCopyTime = timestamp;
			} else if (flushPolicy == CHANGE_TAG){
				state = COPY;
				memory->copyPage(currentMigration.srcPhysicalPage, currentMigration.destPhysicalPage);
				lastStartCopyTime = timestamp;
			} else {
				myassert(false);
			}
		} else {
			myassert(false);
		}

		//for per page statistics
		myassert(it->second.migrations.back().end == 0);
		it->second.migrations.back().end = timestamp;
		it->second.migrations.emplace_back(currentMigration.dest, timestamp);

		lastStartMigrationTime = timestamp;
		if(idle){
			idleTime += timestamp - lastStartIdleTime;
			idle = false;
		}
		return true;
	} else {
		if (!idle){
			lastStartIdleTime = timestamp;
			idle = true;
		}
		return false;
	}
}

void OldHybridMemoryManager::selectPolicyAndMigrate(){
	uint64 timestamp = engine->getTimestamp();
	//debug("(): state: %d", state);
	if (state == NOT_MIGRATING){
		int previousPolicy = currentPolicy;
		bool found = false;
		do {
			currentPolicy = (currentPolicy + 1) % numPolicies;
			if (active[currentPolicy]){
				if (tokens[currentPolicy] >= 0){
					if (startMigration(currentPolicy)){
						found = true;
					} else {
						tokens[currentPolicy] = 0;
						active[currentPolicy] = false;
					}
				}
			} else {
				if (startMigration(currentPolicy)){
					found = true;
					active[currentPolicy] = true;
				}
			}

		} while(!found && previousPolicy != currentPolicy);

		if (!found){
			//find pid with minimum remaining time among active pids
			uint64 minValue = numeric_limits<uint64>::max();
			for (unsigned i = 0; i < numPolicies; i++){
				if (active[i]){
					myassert(tokens[i] < 0);
					uint64 value = static_cast<uint64>(floor(-tokens[i]/partition->getRate(i)*baseMigrationRate+0.5));
					if (value < minValue){
						minValue = value;
					}
				}
			}
			if (minValue != numeric_limits<uint64>::max()){
				state = WAITING;
				wakeupTime = minValue + timestamp;
				addEvent(minValue, START_MIGRATION);
				lastStartWaitingTime = timestamp;
			}
		}
	} else if (state == WAITING){
		if (timestamp == wakeupTime){
			state = NOT_MIGRATING;
			distributeTokens(timestamp - lastStartWaitingTime);
			addEvent(0, START_MIGRATION);
		} else {
			int previousPolicy = currentPolicy;
			bool found = false;
			do {
				currentPolicy = (currentPolicy + 1) % numPolicies;
				if (!active[currentPolicy]){
					if (startMigration(currentPolicy)){
						found = true;
					}
				}
			} while(!found && previousPolicy != currentPolicy);
			if (found){

			}
		}
	} else {
		//another event already scheduled a migration, so don't do anything
	}
}

void OldHybridMemoryManager::process(const Event * event){
	uint64 timestamp = engine->getTimestamp();
	EventType type = static_cast<EventType>(event->getData());
	if (type == START_MIGRATION){
		selectPolicyAndMigrate();
	} else if (type == COPY_PAGE){
		memory->copyPage(currentMigration.srcPhysicalPage, currentMigration.destPhysicalPage);
		lastStartCopyTime = timestamp;
	} else if (type == UPDATE_PARTITION){
		if (coresFinished.size() != numCores){
			uint64 timeElapsed = timestamp - lastIntervalStart;
			lastIntervalStart = timestamp;
			partition->calculate(timeElapsed, instrCounters);
			for (unsigned i = 0; i < numCores; i++){
				instrCounters[i]->reset();
			}
			for (unsigned i = 0; i < numPolicies; i++){
				policies[i]->changeNumDramPages(partition->getDramPages(i));
			}
			addEvent(partitionPeriod, UPDATE_PARTITION);
		}
	} else if (type == UNSTALL) {
		list<MemoryRequest *>::iterator it = stalledRequests.begin();
		while(it != stalledRequests.end()){
			if (memory->access(*it, this)){
				it = stalledRequests.erase(it);
			} else {
				break;
			}
		}
		if (flushQueue.empty() && writebacks.empty() && stalledRequests.empty()){
			finishFlushing();
		}
	} else {
		myassert(false);
	}

}

void OldHybridMemoryManager::distributeTokens(uint64 numTokens){
	for (unsigned i = 0; i < numPolicies; i++){
		if (active[i]){
			tokens[i] += static_cast<uint64>(floor(numTokens*partition->getRate(i)*baseMigrationRate + 0.5));
		}
	}
}

void OldHybridMemoryManager::pinCompleted(addrint addr, IMemory *caller){
	uint64 timestamp = engine->getTimestamp();
	debug("(%lu, %s)", addr, caller->getName());
	myassert(caller == memory || caller == lastLevelCache);
	myassert(state == FLUSH_BEFORE || state == FLUSH_AFTER);
	multiset<addrint>::iterator it = writebacks.find(addr);
	myassert(it != writebacks.end());
	writebacks.erase(it);
	if (flushQueue.empty() && writebacks.empty() && stalledRequests.empty()){
		finishFlushing();
	}
}

void OldHybridMemoryManager::unstall(IMemory *caller){
	addEvent(0, UNSTALL);
}

void OldHybridMemoryManager::drainCompleted(addrint page){
	myassert(currentMigration.srcPhysicalPage == page);
	drainRequestsLeft--;
	if (drainRequestsLeft == 0){
		flushPage(currentMigration.srcPhysicalPage);
	}
}

void OldHybridMemoryManager::flushCompleted(addrint addr, bool dirty, IMemory *caller){
	uint64 timestamp = engine->getTimestamp();
	debug("(%lu, %s, %s)", addr, dirty?"dirty":"clean", caller->getName());
	myassert(state == FLUSH_BEFORE || state == FLUSH_AFTER);
	myassert(flushQueue.count(addr) != 0);
	myassert(flushQueue.find(addr)->second);
	myassert(getIndex(addr) == currentMigration.srcPhysicalPage);
	flushQueue.erase(addr);
	debug(" flushQueue.size(): %lu", flushQueue.size());
	if (DEBUG){
		if (flushQueue.size() == 1){
			debug("flushQueue.begin: %lu", flushQueue.begin()->first);
		}
	}
	if (dirty){
		if (!suppressFlushWritebacks){
			addrint offset = getOffset(addr);
			addrint writebackAddr = 0;
			if (state == FLUSH_BEFORE){
				writebackAddr = getAddress(currentMigration.srcPhysicalPage, offset);
			} else if (state == FLUSH_AFTER){
				writebackAddr = getAddress(currentMigration.destPhysicalPage, offset);
			} else {
				error("Wrong state: should be in FLUSH_BEFORE or FLUSH_AFTER when flushing completes");
			}

			MemoryRequest *req = new MemoryRequest(writebackAddr, blockSize, false, false, HIGH);
			if (!stalledRequests.empty() || !memory->access(req, this)){
				stalledRequests.emplace_back(req);
			}
		}
		dirtyFlushedBlocks++;
	} else {
		cleanFlushedBlocks++;
	}
	if (flushQueue.empty()){
		debug(": writebacks.empty(): %d; stalledRequests.empty(): %d", writebacks.empty(), stalledRequests.empty());
		if (writebacks.empty() && stalledRequests.empty()){
			finishFlushing();
		}
	} else {
		map<addrint, bool>::iterator it = flushQueue.begin();
		while (it != flushQueue.end()){
			if (!it->second){
				lastLevelCache->flush(it->first, blockSize, true, this);
				it->second = true;
				it = flushQueue.end();
			} else {
				++it;
			}
		}

	}
}

void OldHybridMemoryManager::finishFlushing(){
	uint64 timestamp = engine->getTimestamp();
	debug("()");
	PageMap::iterator it = pages[currentMigration.pid].find(currentMigration.virtualPage);
	myassert(it != pages[currentMigration.pid].end());
	myassert((isDramPage(it->second.page) && it->second.type == DRAM) || (isPcmPage(it->second.page) && it->second.type == PCM));
	if (state == FLUSH_BEFORE){
		state = COPY;
		it->second.stallOnAccess = false;
		if (mechanism == PAUSE){
			it->second.stallOnWrite = true;
		}
		addEvent(0, COPY_PAGE);
		unstallCpus(currentMigration.pid, currentMigration.virtualPage);

		if(currentMigration.dest == DRAM){
			dramFlushBeforeTime += (timestamp - lastStartFlushTime);
		} else if (currentMigration.dest == PCM){
			pcmFlushBeforeTime += (timestamp - lastStartFlushTime);
		} else {
			myassert(false);
		}
	} else if (state == FLUSH_AFTER){
		myassert(mechanism == PIN || mechanism == REDIRECT);
		state = NOT_MIGRATING;
		if (mechanism == PIN){
			unpinPage(currentMigration.srcPhysicalPage);
		}
		it->second.page = currentMigration.destPhysicalPage;
		it->second.type = currentMigration.dest;
		if (it->second.type == DRAM) {
			pcmFreePageList.emplace_back(currentMigration.srcPhysicalPage);
			pcmMemorySizeUsedPerPid[currentMigration.pid] -= pageSize;
		} else if (it->second.type == PCM){
			dramFreePageList.emplace_back(currentMigration.srcPhysicalPage);
			dramMemorySizeUsedPerPid[currentMigration.pid] -= pageSize;
		} else {
			myassert(false);
		}
		it->second.stallOnAccess = false;
		it->second.stallOnWrite = false;
		it->second.isMigrating = false;

		//update per page statistics
		it->second.migrations.back().endTransfer = timestamp;

		PhysicalPageMap::iterator ppit = physicalPages.find(currentMigration.srcPhysicalPage);
		myassert(ppit != physicalPages.end());
		physicalPages.erase(ppit);
		bool ins = physicalPages.emplace(currentMigration.destPhysicalPage, PhysicalPageEntry(currentMigration.pid, currentMigration.virtualPage)).second;
		myassert(ins);

		addEvent(0, START_MIGRATION);
		unstallCpus(currentMigration.pid, currentMigration.virtualPage);

		uint64 migrationTime = timestamp - lastStartMigrationTime;
		uint64 flushTime = timestamp - lastStartFlushTime;
		tokens[currentPolicy] -= migrationTime;
		distributeTokens(migrationTime);

		int pid = currentMigration.pid;
		if(currentMigration.dest == DRAM){
			dramMigrationTime += migrationTime;
			dramMigrationTimeCounters[pid] += migrationTime;
			dramFlushAfterTime += flushTime;
		} else if (currentMigration.dest == PCM){
			pcmMigrationTime += migrationTime;
			pcmMigrationTimeCounters[pid] += migrationTime;
			pcmFlushAfterTime += flushTime;
		} else {
			error("Invalid page type");
		}
	} else {
		error("Wrong state: should be in FLUSH_BEFORE or FLUSH_AFTER when flushing completes");
	}
}



void OldHybridMemoryManager::copyCompleted(){
		myassert(state == COPY);
	uint64 timestamp = engine->getTimestamp();
	debug("(): sourcePage: %lu, destPage: %lu, virtualPage: %lu: ", currentMigration.srcPhysicalPage, currentMigration.destPhysicalPage, currentMigration.virtualPage);
	PageMap::iterator it = pages[currentMigration.pid].find(currentMigration.virtualPage);
	myassert(it != pages[currentMigration.pid].end());
	myassert((isDramPage(it->second.page) && it->second.type == DRAM) || (isPcmPage(it->second.page) && it->second.type == PCM));
	if (mechanism == PAUSE){
		state = NOT_MIGRATING;
		it->second.page = currentMigration.destPhysicalPage;
		it->second.type = currentMigration.dest;
		if (it->second.type == DRAM) {
			pcmFreePageList.emplace_back(currentMigration.srcPhysicalPage);
			pcmMemorySizeUsedPerPid[currentMigration.pid] -= pageSize;
		} else if (it->second.type == PCM){
			dramFreePageList.emplace_back(currentMigration.srcPhysicalPage);
			dramMemorySizeUsedPerPid[currentMigration.pid] -= pageSize;
		} else {
			myassert(false);
		}
		it->second.stallOnAccess = false;
		it->second.stallOnWrite = false;
		it->second.isMigrating = false;

		//update per page statistics
		it->second.migrations.back().endTransfer = timestamp;

		PhysicalPageMap::iterator ppit = physicalPages.find(currentMigration.srcPhysicalPage);
		myassert(ppit != physicalPages.end());
		physicalPages.erase(ppit);
		bool ins = physicalPages.emplace(currentMigration.destPhysicalPage, PhysicalPageEntry(currentMigration.pid, currentMigration.virtualPage)).second;
		myassert(ins);

		addEvent(0, START_MIGRATION);
		unstallCpus(currentMigration.pid, currentMigration.virtualPage);

		uint64 migrationTime = timestamp - lastStartMigrationTime;
		uint64 copyTime = timestamp - lastStartCopyTime;
		tokens[currentPolicy] -= migrationTime;
		distributeTokens(migrationTime);

		int pid = currentMigration.pid;
		if(currentMigration.dest == DRAM){
			dramMigrationTime += migrationTime;
			dramMigrationTimeCounters[pid] += migrationTime;
			dramCopyTime += copyTime;
		} else if (currentMigration.dest == PCM){
			pcmMigrationTime += migrationTime;
			pcmMigrationTimeCounters[pid] += migrationTime;
			pcmCopyTime += copyTime;
		} else {
			myassert(false);
		}
	} else if (mechanism == PIN || mechanism == REDIRECT) {
		state = FLUSH_AFTER;
		it->second.stallOnAccess = true;
		if (flushPolicy == FLUSH_PCM_BEFORE || flushPolicy == FLUSH_ONLY_AFTER){
			//flushPage(currentMigration.srcPhysicalPage);
			drainRequestsLeft = 0;
			for (auto cit = cpus.begin(); cit != cpus.end(); ++cit){
				drainRequestsLeft++;
				(*cit)->drain(currentMigration.srcPhysicalPage, this);
			}
		} else if (flushPolicy == REMAP){
			lastLevelCache->remap(currentMigration.srcPhysicalPage, currentMigration.destPhysicalPage, this);
			lastStartFlushTime = timestamp;
		} else if (flushPolicy == CHANGE_TAG){
			changeTags(currentMigration.srcPhysicalPage, currentMigration.destPhysicalPage);
		} else {
			myassert(false);
		}

		uint64 cpTime = engine->getTimestamp() - lastStartCopyTime;
/*		cout<<"CPTIME: "<<cpTime<<"\n";
		cout<< "DEST: "<<currentMigration.dest<<"\n";*/
		if(currentMigration.dest == DRAM){
			dramCopyTime += cpTime;
	//		cout<<"Dram copy time: "<<dramCopyTime<<"  "<<DRAM<<endl;
		} else if (currentMigration.dest == PCM){
			pcmCopyTime += cpTime;
	//		cout<<"PCM copy time: "<<pcmCopyTime<<"  "<<PCM<<endl;
		} else {
			myassert(false);
		}
	}
}

void OldHybridMemoryManager::remapCompleted(addrint pageAddr, IMemory *caller){
	myassert(state == FLUSH_AFTER);
	finishFlushing();
}

void OldHybridMemoryManager::tagChangeCompleted(addrint addr){
	myassert(state == FLUSH_AFTER);
	myassert(addr == tagChangeQueue.front().first);
	myassert(getIndex(addr) == currentMigration.srcPhysicalPage);
	tagChangeQueue.pop_front();
	tagChanges++;
	if (tagChangeQueue.empty()){
		finishFlushing();
	} else {
		lastLevelCache->changeTag(tagChangeQueue.front().first, tagChangeQueue.front().second, blockSize, this);
	}
}

void OldHybridMemoryManager::pinPage(addrint page){
	for (addrint offset = 0; offset < pageSize; offset += blockSize){
		unsigned count = lastLevelCache->pin(getAddress(page,offset), this);
		while (count > 0){
			writebacks.insert(getAddress(page,offset));
			count--;
		}
	}
}

void OldHybridMemoryManager::unpinPage(addrint page){
	for (addrint offset = 0; offset < pageSize; offset += blockSize){
		lastLevelCache->unpin(getAddress(page,offset));
	}
}

void OldHybridMemoryManager::flushPage(addrint page){
	for (addrint offset = 0; offset < pageSize; offset += blockSize){
		flushQueue.emplace(getAddress(page,offset), false);
	}
	map<addrint, bool>::iterator it = flushQueue.begin();
	for (unsigned i = 0; i < flushQueueSize; i++){
		lastLevelCache->flush(it->first, blockSize, true, this);
		it->second = true;
		++it;
	}

	lastStartFlushTime = engine->getTimestamp();
}

void OldHybridMemoryManager::changeTags(addrint oldPage, addrint newPage){
	myassert(false);
	//TODO: change tag change logic so that tag changes happen concurrently
	for (addrint offset = 0; offset < pageSize; offset += blockSize){
		tagChangeQueue.emplace_back(make_pair(getAddress(oldPage,offset), getAddress(newPage,offset)));
	}
	lastLevelCache->changeTag(tagChangeQueue.front().first, tagChangeQueue.front().second, blockSize, this);
}

void OldHybridMemoryManager::unstallCpus(int pid, addrint virtualPage){
	uint64 timestamp = engine->getTimestamp();
	debug("(%d, %lu)", pid, virtualPage);
	StalledCpuMap::iterator it = stalledCpus[pid].find(virtualPage);
	if (it != stalledCpus[pid].end()){
		for (list<CPU*>::iterator itList = it->second.begin(); itList != it->second.end(); itList++){
			(*itList)->resume();
		}
		stalledCpus[pid].erase(it);
	}

}

bool OldHybridMemoryManager::arePagesCompatible(addrint page1, addrint page2) const {
	if (flushPolicy == FLUSH_PCM_BEFORE || flushPolicy == FLUSH_ONLY_AFTER || REMAP){
		return true;
	} else if (flushPolicy == CHANGE_TAG){
		return lastLevelCache->isSameSet(getAddress(page1, 0), getAddress(page2, 0));
	} else {
		myassert(false);
		return false;
	}
}

int OldHybridMemoryManager::getPidOfAddress(addrint addr){
	PhysicalPageMap::iterator it = physicalPages.find(getIndex(addr));
	if (it == physicalPages.end()){
		return -1;
	} else {
		return it->second.pid;
	}
}

void OldHybridMemoryManager::processInterrupt(Counter* counter){
	unsigned core = 0;
	while (core < numCores){
		if (instrCounters[core] == counter){
			break;
		}
		core++;
	}

	if(trace){
		*traceFiles[core] << "instructions " << instrCounters[core]->getTotalValue() << ", ";
		*traceFiles[core] << "cycles " << cycleCounters[core].getValue() << ", ";
		*traceFiles[core] << "dram_reads " << dramReadsCounters[core]->getValue() << ", ";
		*traceFiles[core] << "dram_writes " << dramWritesCounters[core]->getValue() << ", ";
		*traceFiles[core] << "pcm_reads " << pcmReadsCounters[core]->getValue() << ", ";
		*traceFiles[core] << "pcm_writes " << pcmWritesCounters[core]->getValue() << ", ";
		*traceFiles[core] << "dram_read_time " << dramReadTimeCounters[core]->getValue() << ", ";
		*traceFiles[core] << "dram_write_time " << dramWriteTimeCounters[core]->getValue() << ", ";
		*traceFiles[core] << "pcm_read_time " << pcmReadTimeCounters[core]->getValue() << ", ";
		*traceFiles[core] << "pcm_write_time " << pcmWriteTimeCounters[core]->getValue() << ", ";
		*traceFiles[core] << "dram_migrations " << dramMigrationsCounters[core].getValue() << ", ";
		*traceFiles[core] << "pcm_migrations " << pcmMigrationsCounters[core].getValue() << ", ";
		*traceFiles[core] << "dram_migration_time " << dramMigrationTimeCounters[core].getValue() << ", ";
		*traceFiles[core] << "pcm_migration_time " << pcmMigrationTimeCounters[core].getValue() << endl;


		instrCounters[core]->reset();
		cycleCounters[core].reset();
		dramReadsCounters[core]->reset();
		dramWritesCounters[core]->reset();
		pcmReadsCounters[core]->reset();
		pcmWritesCounters[core]->reset();
		dramReadTimeCounters[core]->reset();
		dramWriteTimeCounters[core]->reset();
		pcmReadTimeCounters[core]->reset();
		pcmWriteTimeCounters[core]->reset();
		dramMigrationsCounters[core].reset();
		pcmMigrationsCounters[core].reset();
		dramMigrationTimeCounters[core].reset();
		pcmMigrationTimeCounters[core].reset();
	} else {
		if (periodType == "instructions"){
			myassert(core == 0);
			if (coresFinished.size() != numCores){
				uint64 timestamp = engine->getTimestamp();
				uint64 timeElapsed = timestamp - lastIntervalStart;
				lastIntervalStart = timestamp;
				partition->calculate(timeElapsed, instrCounters);
				for (unsigned i = 0; i < numCores; i++){
					instrCounters[i]->reset();
				}
				for (unsigned i = 0; i < numPolicies; i++){
					policies[i]->changeNumDramPages(partition->getDramPages(i));
				}
			}
		}
	}
}

OldHybridMemoryManager::~OldHybridMemoryManager(){
	delete [] pages;
	delete [] stalledCpus;
	if (trace){
		for (unsigned i = 0; i < numProcesses; i++){
			delete traceFiles[i];
		}
	}
}

void OldHybridMemoryManager::addCpu(CPU *cpu){
	cpus.emplace_back(cpu);
}

void OldHybridMemoryManager::addInstrCounter(Counter* counter, unsigned pid){
	myassert(instrCounters.size() == pid);
	instrCounters.emplace_back(counter);
	policies[pidToPolicy[pid]]->setInstrCounter(counter);
	if (trace){
		counter->setInterrupt(tracePeriod, this);
	} else {
		if (periodType == "instructions"){
			if (pid == 0){
				counter->setInterrupt(partitionPeriod, this);
			}
		}
	}
}

void OldHybridMemoryManager::addDramReadsCounter(Counter *counter, unsigned pid){
	myassert(dramReadsCounters.size() == pid);
	dramReadsCounters.emplace_back(counter);
}

void OldHybridMemoryManager::addDramWritesCounter(Counter *counter, unsigned pid){
	myassert(dramWritesCounters.size() == pid);
	dramWritesCounters.emplace_back(counter);
}

void OldHybridMemoryManager::addPcmReadsCounter(Counter *counter, unsigned pid){
	myassert(pcmReadsCounters.size() == pid);
	pcmReadsCounters.emplace_back(counter);
}

void OldHybridMemoryManager::addPcmWritesCounter(Counter *counter, unsigned pid){
	myassert(pcmWritesCounters.size() == pid);
	pcmWritesCounters.emplace_back(counter);
}

void OldHybridMemoryManager::addDramReadTimeCounter(Counter *counter, unsigned pid){
	myassert(dramReadTimeCounters.size() == pid);
	dramReadTimeCounters.emplace_back(counter);
}

void OldHybridMemoryManager::addDramWriteTimeCounter(Counter *counter, unsigned pid){
	myassert(dramWriteTimeCounters.size() == pid);
	dramWriteTimeCounters.emplace_back(counter);
}

void OldHybridMemoryManager::addPcmReadTimeCounter(Counter *counter, unsigned pid){
	myassert(pcmReadTimeCounters.size() == pid);
	pcmReadTimeCounters.emplace_back(counter);
}

void OldHybridMemoryManager::addPcmWriteTimeCounter(Counter *counter, unsigned pid){
	myassert(pcmWriteTimeCounters.size() == pid);
	pcmWriteTimeCounters.emplace_back(counter);
}


SimpleMemoryManager::SimpleMemoryManager(StatContainer *cont, Memory *memoryArg, unsigned numProcessesArg, unsigned pageSizeArg) :
	memorySize(cont, "memory_size", "Size of memory available to the memory manager", this, &SimpleMemoryManager::getMemorySize),
	memorySizeUsed(cont, "memory_size_used", "Size of memory used by the memory manager", this, &SimpleMemoryManager::getMemorySizeUsed)
{
	name = "MemoryManager";

	memory = memoryArg;

	numProcesses = numProcessesArg;
	pages = new PageMap[numProcesses];

	logPageSize = (unsigned)logb(pageSizeArg);
	pageSize = 1 << logPageSize;
	numPages = memory->getSize() / pageSize;
	size = numPages * pageSize;

	offsetWidth = logPageSize;
	indexWidth = sizeof(addrint)*8 - offsetWidth;

	offsetMask = 0;
	for (unsigned i = 0; i < offsetWidth; i++){
		offsetMask |= (addrint)1U << i;
	}
	indexMask = 0;
	for (unsigned i = offsetWidth; i < indexWidth+offsetWidth; i++){
		indexMask |= (addrint)1U << i;
	}

	firstAddress = 0;
	onePastLastAddress = size;

	firstPage = getIndex(firstAddress);
	onePastLastPage = getIndex(onePastLastAddress);

	for(addrint page = firstPage; page < onePastLastPage; page++){
		freePageList.emplace_back(page);
	}

}

SimpleMemoryManager::~SimpleMemoryManager(){
	delete [] pages;
}

bool SimpleMemoryManager::access(int pid, addrint virtualAddr, bool read, bool instr, addrint *physicalAddr, CPU *cpu){
	//PidAddrPair pidPlusVirtualPage = make_pair(pid, getIndex(virtualAddr));
	addrint virtualPage = getIndex(virtualAddr);

	PageMap::iterator it = pages[pid].find(virtualPage);
	if (it == pages[pid].end()){
		if (freePageList.empty()){
			error("SimpleMemoryManager::access(): there are no free physical pages");
		}
		addrint freePage = freePageList.front();
		freePageList.pop_front();
		it = pages[pid].emplace(virtualPage, freePage).first;
	}
	addrint physicalPage = it->second;
	addrint offset = getOffset(virtualAddr);
	*physicalAddr = getAddress(physicalPage, offset);
	return false;
}

void SimpleMemoryManager::finish(int coreId){

}

void SimpleMemoryManager::allocate(const vector<string>& filenames){
	for (unsigned pid = 0; pid < filenames.size(); pid++){
		ifstream ifs(filenames[pid]);
		addrint virtualPage;
		while (ifs >> virtualPage){
			if (freePageList.empty()){
				error("SimpleMemoryManager::allocate(): there are no free physical pages");
			}
			addrint freePage = freePageList.front();
			freePageList.pop_front();
			bool ins = pages[pid].emplace(virtualPage, freePage).second;
			assert(ins);
		}
	}
}


istream& operator>>(istream& lhs, MigrationMechanism& rhs){
	string s;
	lhs >> s;
	if (s == "pause"){
		rhs = PAUSE;
	} else if (s == "pin"){
		rhs = PIN;
	} else if (s == "redirect"){
		rhs = REDIRECT;
	} else {
		error("Invalid migration mechanism: %s", s.c_str());
	}
	return lhs;
}

ostream& operator<<(ostream& lhs, MigrationMechanism rhs){
	if (rhs == PAUSE){
		lhs << "pause";
	} else if(rhs == PIN){
		lhs << "pin";
	} else if(rhs == REDIRECT){
		lhs << "redirect";
	} else {
		error("Invalid migration mechanism");
	}
	return lhs;
}

istream& operator>>(istream& lhs, MonitoringType& rhs){
	string s;
	lhs >> s;
	if (s == "reads"){
		rhs = READS;
	} else if (s == "writes"){
		rhs = WRITES;
	} else if (s == "accesses"){
			rhs = ACCESSES;
	} else {
		error("Invalid migration mechanism: %s", s.c_str());
	}
	return lhs;
}

ostream& operator<<(ostream& lhs, MonitoringType rhs){
	if (rhs == READS){
		lhs << "reads";
	} else if(rhs == WRITES){
		lhs << "writes";
	} else if(rhs == ACCESSES){
		lhs << "accesses";
	} else {
		error("Invalid migration mechanism");
	}
	return lhs;
}

istream& operator>>(istream& lhs, MonitoringLocation& rhs){
	string s;
	lhs >> s;
	if (s == "before_caches"){
		rhs = BEFORE_CACHES;
	} else if (s == "after_caches"){
		rhs = AFTER_CACHES;
	} else {
		error("Invalid migration mechanism: %s", s.c_str());
	}
	return lhs;
}

ostream& operator<<(ostream& lhs, MonitoringLocation rhs){
	if (rhs == BEFORE_CACHES){
		lhs << "before_caches";
	} else if(rhs == AFTER_CACHES){
		lhs << "after_caches";
	} else {
		error("Invalid migration mechanism");
	}
	return lhs;
}

istream& operator>>(istream& lhs, FlushPolicy& rhs){
	string s;
	lhs >> s;
	if (s == "flush_pcm_before"){
		rhs = FLUSH_PCM_BEFORE;
	} else if (s == "flush_only_after"){
		rhs = FLUSH_ONLY_AFTER;
	} else if (s == "remap"){
		rhs = REMAP;
	} else if (s == "change_tag"){
		rhs = CHANGE_TAG;
	} else {
		error("Invalid flush policy: %s", s.c_str());
	}

	return lhs;
}

ostream& operator<<(ostream& lhs, FlushPolicy rhs){
	if(rhs == FLUSH_PCM_BEFORE){
		lhs << "flush_pcm_before";
	} else if(rhs == FLUSH_ONLY_AFTER){
		lhs << "flush_only_after";
	} else if(rhs == REMAP){
		lhs << "remap";
	} else if(rhs == CHANGE_TAG){
		lhs << "change_tag";
	} else {
		error("Invalid flush policy");
	}
	return lhs;
}

istream& operator>>(istream& lhs, MonitoringStrategy& rhs){
	string s;
	lhs >> s;
	if (s == "no_pam"){
		rhs = NO_PAM;
	} else if (s == "pam"){
		rhs = PAM;
	} else {
		error("Invalid monitoring strategy: %s", s.c_str());
	}
	return lhs;
}

ostream& operator<<(ostream& lhs, MonitoringStrategy rhs){
	if(rhs == NO_PAM){
		lhs << "no_pam";
	} else if(rhs == PAM){
		lhs << "pam";
	} else {
		error("Invalid monitoring strategy");
	}
	return lhs;
}

istream& operator>>(istream& lhs, QueuePolicy& rhs){
	string s;
	lhs >> s;
	if (s == "fifo"){
		rhs = FIFO;
	} else if (s == "lru"){
		rhs = LRU;
	} else if (s == "freq"){
		rhs = FREQ;
	} else {
		error("Invalid queue policy: %s", s.c_str());
	}

	return lhs;
}

ostream& operator<<(ostream& lhs, QueuePolicy rhs){
	if(rhs == FIFO){
		lhs << "fifo";
	} else if(rhs == LRU){
		lhs << "lru";
	} else if(rhs == FREQ){
		lhs << "freq";
	} else {
		error("Invalid queue policy");
	}
	return lhs;
}


