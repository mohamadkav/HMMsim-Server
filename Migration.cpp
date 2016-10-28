/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#include "Migration.H"
#include "MemoryManager.H"

#include <zlib.h>

#include <cmath>

BaseMigrationPolicy::BaseMigrationPolicy(
        const string& nameArg,
        Engine *engineArg,
        uint64 debugStartArg,
        uint64 dramPagesArg,
        AllocationPolicy allocPolicyArg,
        unsigned numPidsArg,
        double maxFreeDramArg,
        uint32 completeThresholdArg,
        uint64 rollbackTimeoutArg) :
name(nameArg),
engine(engineArg),
debugStart(debugStartArg),
dramPages(dramPagesArg),
allocPolicy(allocPolicyArg),
numPids(numPidsArg),
maxFreeDram(maxFreeDramArg),
completeThreshold(completeThresholdArg),
rollbackTimeout(rollbackTimeoutArg) {

    myassert(numPids > 0);
    dramPagesLeft = dramPages;
    maxFreeDramPages = dramPages * maxFreeDram;

    dramFull = false;
}

PageType BaseMigrationPolicy::allocate(int pid, addrint addr, bool read, bool instr) {
    PageType ret;
    if (allocPolicy == DRAM_FIRST) {
        if (dramFull) {
            ret = PCM;
        } else {
            ret = DRAM;
        }
    } else if (allocPolicy == PCM_ONLY) {
        ret = PCM;
    } else {
        myassert(false);
    }

    if (ret == DRAM) {
        myassert(dramPagesLeft > 0);
        dramPagesLeft--;
        if (dramPagesLeft == 0) {
            dramFull = true;
        }

    }

    return ret;
}

bool BaseMigrationPolicy::complete(int *pid, addrint *addr) {
    auto minPit = progress.end();
    uint32 minBlocks = completeThreshold;
    auto pit = progress.begin();
    for (; pit != progress.end(); ++pit) {
        if (pit->blocksLeft < minBlocks) {
            minBlocks = pit->blocksLeft;
            minPit = pit;
        }
    }
    if (pit != progress.end()) {
        *pid = pit->pid;
        *addr = pit->page;
        return true;
    } else {
        return false;
    }
}

bool BaseMigrationPolicy::rollback(int *pid, addrint *addr) {
    auto maxPit = progress.end();
    uint64 maxTime = rollbackTimeout;
    auto pit = progress.begin();
    for (; pit != progress.end(); ++pit) {
        uint64 time = engine->getTimestamp() - pit->startTime;
        if (time > maxTime) {
            maxTime = time;
            maxPit = pit;
        }
    }
    if (pit != progress.end()) {
        *pid = pit->pid;
        *addr = pit->page;


        return true;
    } else {
        return false;
    }
}

bool BaseMigrationPolicy::demote(int *pid, addrint *addr) {
    if (allocPolicy == DRAM_FIRST) {
        if (dramFull) {
            return selectDemotionPage(pid, addr);
        } else {
            return false;
        }
    } else if (allocPolicy == PCM_ONLY) {
        return selectDemotionPage(pid, addr);
    } else {
        myassert(false);
        return false;
    }
}

void BaseMigrationPolicy::monitor(const vector<CountEntry>& counts, const vector<ProgressEntry>& progressArg) {
    progress = progressArg;
}

void BaseMigrationPolicy::setInstrCounter(Counter* counter) {
    instrCounter = counter;
}

void BaseMigrationPolicy::setNumDramPages(uint64 dramPagesNew) {
    if (dramPagesNew > dramPages) {
        dramPagesLeft += (dramPagesNew - dramPages);
    } else if (dramPagesNew < dramPages) {
        myassert(dramPagesNew >= 0);
        dramPagesLeft -= (dramPages - dramPagesNew);
        if (!dramFull) {
            if (dramPagesLeft <= 0) {
                dramFull = true;
            }
        }
    } else {
        return;
    }
    dramPages = dramPagesNew;
    maxFreeDramPages = dramPages * maxFreeDram;
}

NoMigrationPolicy::NoMigrationPolicy(
        const string& nameArg,
        Engine *engineArg,
        uint64 debugArg,
        uint64 dramPagesArg,
        AllocationPolicy allocPolicyArg,
        unsigned numPidsArg) : BaseMigrationPolicy(nameArg, engineArg, debugArg, dramPagesArg, allocPolicyArg, numPidsArg, 0.0, 0, 0) {
}

//Multi Queue

MultiQueueMigrationPolicy::MultiQueueMigrationPolicy(
        const string& nameArg,
        Engine *engineArg,
        uint64 debugStartArg,
        uint64 dramPagesArg,
        AllocationPolicy allocPolicyArg,
        unsigned numPidsArg,
        double maxFreeDramArg,
        uint32 completeThresholdArg,
        uint64 rollbackTimeoutArg,
        unsigned numQueuesArg,
        unsigned thresholdQueueArg,
        uint64 lifetimeArg,
        bool logicalTimeArg,
        uint64 filterThresholdArg,
        bool secondDemotionEvictionArg,
        bool agingArg,
        bool useHistoryArg,
        bool usePendingListArg,
        bool enableRollbackArg,
        bool promotionFilterArg,
        unsigned demotionAttemptsArg) :
BaseMigrationPolicy(nameArg, engineArg, debugStartArg, dramPagesArg, allocPolicyArg, numPidsArg, maxFreeDramArg, completeThresholdArg, rollbackTimeoutArg),
numQueues(numQueuesArg),
thresholdQueue(thresholdQueueArg),
lifetime(lifetimeArg),
logicalTime(logicalTimeArg),
filterThreshold(filterThresholdArg),
secondDemotionEviction(secondDemotionEvictionArg),
aging(agingArg),
useHistory(useHistoryArg),
usePendingList(usePendingListArg),
enableRollback(enableRollbackArg),
promotionFilter(promotionFilterArg),
demotionAttempts(demotionAttemptsArg) {

    queues[DRAM].resize(numQueues);
    queues[PCM].resize(numQueues);
    thresholds.resize(numQueues);

    for (unsigned i = 0; i < numQueues - 1; i++) {
        thresholds[i] = static_cast<uint64> (pow(2.0, (int) i + 1));
    }
    thresholds[numQueues - 1] = numeric_limits<uint64>::max();

    pages = new PageMap[numPids];

    currentTime = 0;

    tries = demotionAttempts;

    myassert(thresholdQueue > 0);
}

PageType MultiQueueMigrationPolicy::allocate(int pid, addrint addr, bool read, bool instr) {
    int index = numPids == 1 ? 0 : pid;
    PageType ret = BaseMigrationPolicy::allocate(pid, addr, read, instr);
    if (ret == DRAM) {
        uint64 exp = (logicalTime ? currentTime : engine->getTimestamp()) + lifetime;
        //uint64 count = thresholds[thresholdQueue-1];
        //auto ait = queues[DRAM][thresholdQueue].emplace(queues[DRAM][thresholdQueue].end(), AccessEntry(pid, addr, exp, count, false, false));
        uint64 count = 0;
        auto ait = queues[DRAM][0].emplace(queues[DRAM][0].end(), AccessEntry(pid, addr, exp, count, false, false));
        bool ins = pages[index].emplace(addr, PageEntry(ret, 0, ait)).second;
        myassert(ins);
    } else if (ret == PCM) {
        auto ait = history.emplace(history.end(), AccessEntry(pid, addr, 0, 0, false, false));
        bool ins = pages[index].emplace(addr, PageEntry(ret, -2, ait)).second;
        myassert(ins);
    } else {
        myassert(false);
    }
    return ret;
}

bool MultiQueueMigrationPolicy::migrate(int pid, addrint addr) {
    static long selectPageCount2;
    selectPageCount2++;
    if (selectPageCount2 % 10000 == 0)
        cout << "new multi queue_selectpge" << selectPageCount2 << endl;
    if (dramPagesLeft > 0) {
        int index = numPids == 1 ? 0 : pid;
        auto it = pages[index].find(addr);
        myassert(it != pages[index].end());
        myassert(it->second.type == PCM);
        myassert(it->second.accessIt->pid == pid);
        myassert(it->second.accessIt->addr == addr);
        myassert(!it->second.accessIt->migrating);

        if (!promotionFilter || (promotionFilter && it->second.queue >= thresholdQueue)) {
            AccessEntry entry(*it->second.accessIt);
            if (it->second.queue == -2) {
                history.erase(it->second.accessIt);
            } else if (it->second.queue == -1) {
                victims.erase(it->second.accessIt);
            } else {
                queues[PCM][it->second.queue].erase(it->second.accessIt);
            }
            it->second.type = DRAM;
            entry.migrating = true;
            it->second.queue = 0;
            for (unsigned i = 0; i < numQueues - 1; i++) {
                if (entry.count < thresholds[i]) {
                    it->second.queue = i;
                    break;
                }
            }
            it->second.accessIt = queues[DRAM][it->second.queue].emplace(queues[DRAM][it->second.queue].end(), entry);
            dramPagesLeft--;
            return true;
        } else {
            return false;
        }
    } else {
        return false;
    }
}

//bool MultiQueueMigrationPolicy::complete(int *pid, addrint *addr){
//
//}
//
//bool MultiQueueMigrationPolicy::rollback(int *pid, addrint *addr){
//
//}

void MultiQueueMigrationPolicy::done(int pid, addrint addr) {
    int index = numPids == 1 ? 0 : pid;
    auto it = pages[index].find(addr);
    myassert(it != pages[index].end());
    myassert(it->second.accessIt->migrating);
    it->second.accessIt->migrating = false;
}

void MultiQueueMigrationPolicy::monitor(const vector<CountEntry>& counts, const vector<ProgressEntry>& progress) {
    for (auto cit = counts.begin(); cit != counts.end(); ++cit) {
        int index = numPids == 1 ? 0 : cit->pid;
        uint64 timestamp = engine->getTimestamp();
        PageMap::iterator it = pages[index].find(cit->page);
        myassert(it != pages[index].end());
        uint64 count = cit->reads;

        currentTime++;
        uint64 exp = (logicalTime ? currentTime : timestamp) + lifetime;
        if (it->second.queue == -2) {
            //bring back from history list
            uint64 oldCount = it->second.accessIt->count + count;
            bool oldMigrating = it->second.accessIt->migrating;
            if (useHistory) {
                if (aging) {
                    uint64 timeSinceExpiration = timestamp - it->second.accessIt->expirationTime;
                    uint64 periodsSinceExpiration = timeSinceExpiration / lifetime;
                    if (periodsSinceExpiration >= 64) {
                        periodsSinceExpiration = 63;
                    }
                    oldCount /= static_cast<uint64> (pow(2.0l, static_cast<int> (periodsSinceExpiration)));
                }
            } else {
                oldCount = count;
            }
            history.erase(it->second.accessIt);
            it->second.queue = 0;
            for (unsigned i = 0; i < numQueues - 1; i++) {
                if (oldCount < thresholds[i]) {
                    it->second.queue = i;
                    break;
                }
            }
            it->second.accessIt = queues[it->second.type][it->second.queue].emplace(queues[it->second.type][it->second.queue].end(), AccessEntry(cit->pid, cit->page, exp, oldCount, false, oldMigrating));

        } else if (it->second.queue == -1) {
            //bring back from victim list
            uint64 oldCount = it->second.accessIt->count + count;
            bool oldMigrating = it->second.accessIt->migrating;
            if (aging) {
                uint64 timeSinceExpiration = timestamp - it->second.accessIt->expirationTime;
                uint64 periodsSinceExpiration = timeSinceExpiration / lifetime;
                if (periodsSinceExpiration >= 64) {
                    periodsSinceExpiration = 63;
                }
                oldCount /= static_cast<uint64> (pow(2.0l, static_cast<int> (periodsSinceExpiration)));
            }
            victims.erase(it->second.accessIt);
            it->second.queue = 0;
            for (unsigned i = 0; i < numQueues - 1; i++) {
                if (oldCount < thresholds[i]) {
                    it->second.queue = i;
                    break;
                }
            }
            it->second.accessIt = queues[it->second.type][it->second.queue].emplace(queues[it->second.type][it->second.queue].end(), AccessEntry(cit->pid, cit->page, exp, oldCount, false, oldMigrating));
        } else if (it->second.queue >= 0) {
            it->second.accessIt->count += count;
            uint64 oldCount = it->second.accessIt->count;
            bool oldMigrating = it->second.accessIt->migrating;
            queues[it->second.type][it->second.queue].erase(it->second.accessIt);
            it->second.queue = 0;
            for (unsigned i = 0; i < numQueues - 1; i++) {
                if (oldCount < thresholds[i]) {
                    it->second.queue = i;
                    if (usePendingList && it->second.queue >= thresholdQueue) {
                        pending.emplace_back(make_pair(index, it->first));
                    }
                    break;
                }
            }
            it->second.accessIt = queues[it->second.type][it->second.queue].emplace(queues[it->second.type][it->second.queue].end(), AccessEntry(cit->pid, cit->page, exp, oldCount, false, oldMigrating));
        } else {
            myassert(false);
        }

        for (unsigned i = 0; i < 2; i++) {
            for (vector<AccessQueue>::iterator qit = queues[i].begin(); qit != queues[i].end(); ++qit) {
                if (!qit->empty()) {
                    if ((logicalTime && currentTime > qit->front().expirationTime) || (!logicalTime && timestamp > qit->front().expirationTime)) {
                        int frontPid = qit->front().pid;
                        PageMap::iterator pageIt = pages[frontPid].find(qit->front().addr);
                        myassert(pageIt != pages[frontPid].end());
                        uint64 oldCount = pageIt->second.accessIt->count;
                        bool oldMigrating = pageIt->second.accessIt->migrating;
                        uint64 exp = (logicalTime ? currentTime : timestamp) + lifetime;
                        if (aging) {
                            oldCount /= 2;
                        }
                        if (pageIt->second.queue == 0) {
                            queues[i][pageIt->second.queue].erase(pageIt->second.accessIt);
                            if (pageIt->second.type == DRAM) {
                                pageIt->second.accessIt = victims.emplace(victims.end(), AccessEntry(frontPid, pageIt->first, exp, oldCount, false, oldMigrating));
                                pageIt->second.queue = -1;
                            } else {
                                pageIt->second.accessIt = history.emplace(history.end(), AccessEntry(frontPid, pageIt->first, exp, oldCount, false, oldMigrating));
                                pageIt->second.queue = -2;
                            }
                        } else if (pageIt->second.queue > 0) {
                            if (secondDemotionEviction && pageIt->second.accessIt->demoted) {
                                queues[i][pageIt->second.queue].erase(pageIt->second.accessIt);
                                if (pageIt->second.type == DRAM) {
                                    pageIt->second.accessIt = victims.emplace(victims.end(), AccessEntry(frontPid, pageIt->first, exp, oldCount, false, oldMigrating));
                                    pageIt->second.queue = -1;
                                } else {
                                    pageIt->second.accessIt = history.emplace(history.end(), AccessEntry(frontPid, pageIt->first, exp, oldCount, false, oldMigrating));
                                    pageIt->second.queue = -2;
                                }
                            } else {
                                queues[i][pageIt->second.queue].erase(pageIt->second.accessIt);
                                pageIt->second.queue--;
                                pageIt->second.accessIt = queues[i][pageIt->second.queue].emplace(queues[i][pageIt->second.queue].end(), AccessEntry(frontPid, pageIt->first, exp, oldCount, true, oldMigrating));
                            }
                        } else {
                            myassert(false);
                        }
                    }
                }
            }
        }
    }
    BaseMigrationPolicy::monitor(counts, progress);
}

bool MultiQueueMigrationPolicy::selectDemotionPage(int *pid, addrint *addr) {
    if (dramPagesLeft > maxFreeDramPages) {
        return false;
    }

    if (tries == demotionAttempts) {
        tries = 0;
    } else {
        tries++;
        return false;
    }

    auto vit = victims.begin();
    if (!enableRollback) {
        while (vit != victims.end() && vit->migrating) {
            ++vit;
        }
    }
    if (vit != victims.end()) {
        AccessEntry entry = *vit;
        PageMap::iterator pageIt = pages[entry.pid].find(entry.addr);
        myassert(pageIt != pages[entry.pid].end());
        myassert(pageIt->second.type == DRAM);
        myassert(pageIt->second.queue == -1);
        uint64 oldCount = pageIt->second.accessIt->count;
        uint64 exp = pageIt->second.accessIt->expirationTime;
        victims.erase(vit);
        *pid = entry.pid;
        *addr = entry.addr;
        pageIt->second.type = PCM;
        pageIt->second.accessIt = history.emplace(history.end(), AccessEntry(entry.pid, entry.addr, pageIt->second.type, exp, oldCount, true));
        pageIt->second.queue = -2;
        dramPagesLeft++;
        return true;
    } else {
        for (int i = 0; i < thresholdQueue; i++) {
            if (!queues[DRAM][i].empty()) {
                auto qit = queues[DRAM][i].begin();
                if (!enableRollback) {
                    while (qit != queues[DRAM][i].end() && qit->migrating) {
                        ++qit;
                    }
                }
                if (qit != queues[DRAM][i].end()) {
                    AccessEntry entry(*qit);
                    queues[DRAM][i].erase(qit);
                    *pid = entry.pid;
                    *addr = entry.addr;
                    PageMap::iterator pageIt = pages[*pid].find(*addr);
                    pageIt->second.type = PCM;
                    entry.migrating = true;
                    pageIt->second.accessIt = queues[PCM][pageIt->second.queue].emplace(queues[PCM][pageIt->second.queue].end(), entry);
                    dramPagesLeft++;
                    return true;
                }
            }
        }
        return false;
    }
}




//Old Migration Policies:

OldBaseMigrationPolicy::OldBaseMigrationPolicy(
        const string& nameArg,
        Engine *engineArg,
        uint64 debugStartArg,
        //StatContainer *statCont,
        //selectPageCount(statCont, "select_page_count", "Number of selectPAge count", 0),
        uint64 dramPagesArg,
        AllocationPolicy allocPolicyArg,
        IAllocator *allocatorArg,
        unsigned numPidsArg) :
name(nameArg),
engine(engineArg),
debugStart(debugStartArg),
dramPages(dramPagesArg),
allocPolicy(allocPolicyArg),
allocator(allocatorArg),
numPids(numPidsArg) {

    myassert(numPids > 0);
    dramPagesLeft = dramPages;

    dramFull = false;
}

void OldBaseMigrationPolicy::setInstrCounter(Counter* counter) {
    instrCounter = counter;
}

PageType OldBaseMigrationPolicy::allocate(int pid, addrint addr, bool read, bool instr) {
    /*	static int j=0;
            if(j%10 == 0)
                    cout<<"allocate in migration: "<<j<<endl;
            j++;*/
    PageType ret;
    if (allocPolicy == DRAM_FIRST) {
        if (dramFull) {
            ret = PCM;
        } else {
            ret = DRAM;
        }
    } else if (allocPolicy == PCM_ONLY) {
        ret = PCM;
    } else if (allocPolicy == CUSTOM) {
        PageType hint = allocator->hint(pid, addr, read, instr);
        if (hint == DRAM && dramPagesLeft > 0) {
            ret = DRAM;
        } else {
            ret = PCM;
        }
    } else {
        myassert(false);
    }

    if (ret == DRAM) {
        myassert(dramPagesLeft > 0);
        dramPagesLeft--;
        if (dramPagesLeft == 0) {
            dramFull = true;
        }

    }

    return ret;
}

bool OldBaseMigrationPolicy::migrate(int *pid, addrint *addr) {
    /*	static int migrationsCalls = 0;
    if(migrationsCalls == 0)
            cout << "migrate calls: ";
    migrationsCalls++;
    if(migrationsCalls % 1000 == 0)
            cout << migrationsCalls << "\n";
    else
            cout << migrationsCalls << "  ";*/
    static bool isDramEmpty = true;
    if (allocPolicy == DRAM_FIRST) {
        if (dramFull) {
            if (isDramEmpty) {
                //	cout<<"\n\n\n\n\n\n\nDRAMSTATECHANGED TO FULL\n";
                isDramEmpty = false;
            }
            return selectPage(pid, addr);
        } else {
            if (!isDramEmpty) {
                //	cout<<"\n\n\n\n\n\n\nDRAMSTATECHANGED TO EMPTY\n";
                isDramEmpty = true;
            }
            return false;
        }
    } else if (allocPolicy == PCM_ONLY || allocPolicy == CUSTOM) {
        return selectPage(pid, addr);
    } else {
        myassert(false);
        return false;
    }
}

void OldBaseMigrationPolicy::changeNumDramPages(uint64 dramPagesNew) {
    if (dramPagesNew > dramPages) {
        dramPagesLeft += (dramPagesNew - dramPages);

    } else if (dramPagesNew < dramPages) {
        myassert(dramPagesNew >= 0);
        dramPagesLeft -= (dramPages - dramPagesNew);
        dramPages = dramPagesNew;
        if (!dramFull) {
            if (dramPagesLeft <= 0) {
                dramFull = true;
            }
        }
    } else {

    }
}

OldNoMigrationPolicy::OldNoMigrationPolicy(
        const string& nameArg,
        Engine *engineArg,
        uint64 debugArg,
        uint64 dramPagesArg,
        AllocationPolicy allocPolicyArg,
        IAllocator *allocatorArg,
        unsigned numPidsArg) : OldBaseMigrationPolicy(nameArg, engineArg, debugArg, dramPagesArg, allocPolicyArg, allocatorArg, numPidsArg) {
}

OldOfflineMigrationPolicy::OldOfflineMigrationPolicy(
        const string& nameArg,
        Engine *engineArg,
        uint64 debugArg,
        uint64 dramPagesArg,
        AllocationPolicy allocPolicyArg,
        IAllocator *allocatorArg,
        unsigned numPidsArg,
        int thisPidArg,
        const string& filenameArg,
        const string& metricTypeArg,
        const string& accessTypeArg,
        const string& weightTypeArg,
        uint64 intervalCountArg,
        uint64 metricThresholdArg) :
OldBaseMigrationPolicy(nameArg, engineArg, debugArg, dramPagesArg, allocPolicyArg, allocatorArg, numPidsArg),
thisPid(thisPidArg),
intervalCount(intervalCountArg),
metricThreshold(metricThresholdArg),
previousInterval(0) {

    //	FILE *trace = fopen(filenameArg.c_str(), "r");
    //	if (trace == 0){
    //		error("Could not open file %s", filenameArg.c_str());
    //	}

    if (metricTypeArg == "accessed") {
        metricType = ACCESSED;
    } else if (metricTypeArg == "access_count") {
        metricType = ACCESS_COUNT;
    } else if (metricTypeArg == "touch_count") {
        metricType = TOUCH_COUNT;
    } else {
        error("Invalid metric type: %s", metricTypeArg.c_str());
    }

    if (accessTypeArg == "reads") {
        accessType = READS;
    } else if (accessTypeArg == "writes") {
        accessType = WRITES;
    } else if (accessTypeArg == "accesses") {
        accessType = ACCESSES;
    } else {
        error("Invalid access type: %s", accessTypeArg.c_str());
    }

    if (weightTypeArg == "uniform") {
        for (uint64 i = 0; i < intervalCount; i++) {
            weights.emplace_back(1);
        }
    } else if (weightTypeArg == "linear") {
        for (uint64 i = 0; i < intervalCount; i++) {
            weights.emplace_back(intervalCount - i);
        }
    } else if (weightTypeArg == "exponential") {
        for (uint64 i = 0; i < intervalCount; i++) {
            weights.emplace_back(static_cast<uint64> (pow(2.0, static_cast<double> (intervalCount - i - 1))));
        }
    } else {
        error("Invalid weight type: %s", weightTypeArg.c_str());
    }

    gzFile trace = gzopen(filenameArg.c_str(), "r");
    if (trace == 0) {
        error("Could not open file %s", filenameArg.c_str());
    }

    period = 0;

    if (numPids != 1) {
        error("Sharing offline policies is not yet implemented");
    }

    bool done = false;
    while (!done) {
        uint64 icount;
        uint32 size;
        //size_t read = fread(&icount, sizeof(uint64), 1, trace);
        int read = gzread(trace, &icount, sizeof (uint64));
        if (read > 0) {
            if (period == 0) {
                period = icount;
            }
            //fread(&size, sizeof(uint32), 1, trace);
            gzread(trace, &size, sizeof (uint32));
            for (uint32 i = 0; i < size; i++) {
                addrint page;
                uint32 reads;
                uint32 writes;
                uint8 readBlocks;
                uint8 writtenBlocks;
                uint8 accessedBlocks;
                //				fread(&reads, sizeof(uint32), 1, trace);
                //				fread(&writes, sizeof(uint32), 1, trace);
                //				fread(&readBlocks, sizeof(uint8), 1, trace);
                //				fread(&writtenBlocks, sizeof(uint8), 1, trace);
                //				fread(&accessedBlocks, sizeof(uint8), 1, trace);
                //fread(&page, sizeof(addrint), 1, trace);
                gzread(trace, &page, sizeof (addrint));
                gzread(trace, &reads, sizeof (uint32));
                gzread(trace, &writes, sizeof (uint32));
                gzread(trace, &readBlocks, sizeof (uint8));
                gzread(trace, &writtenBlocks, sizeof (uint8));
                gzread(trace, &accessedBlocks, sizeof (uint8));

                uint64 readCount = 0, writeCount = 0, accessCount = 0;
                if (metricType == ACCESSED) {
                    readCount = reads == 0 ? 0 : 1;
                    writeCount = writes == 0 ? 0 : 1;
                    accessCount = readCount || writeCount;
                } else if (metricType == ACCESS_COUNT) {
                    readCount = reads;
                    writeCount = writes;
                    accessCount = readCount + writeCount;
                } else if (metricType == TOUCH_COUNT) {
                    readCount = readBlocks;
                    writeCount = writtenBlocks;
                    accessCount = accessedBlocks;
                } else {
                    myassert(false);
                }

                uint64 count = 0;
                if (accessType == READS) {
                    count = readCount;
                } else if (accessType == WRITES) {
                    count = writeCount;
                } else if (accessType == ACCESSES) {
                    count = accessCount;
                } else {
                    myassert(false);
                }

                PageMap::iterator pit = pages.emplace(page, PageEntry(INVALID)).first;
                //pit->second.counters.emplace_back(Entry(icount/period, count));
                pit->second.counters.emplace_back(icount / period, count);

            }
        } else {
            //if (feof(trace)){
            if (gzeof(trace)) {
                done = true;
            } else {
                error("Error reading file %s", filenameArg.c_str());
            }
        }
    }

    //fclose(trace);
    gzclose(trace);

    //	for (PageMap::iterator it = pages.begin(); it != pages.end(); ++it){
    //		cout << it->first << endl;
    //	}

    //debug = 0;

}

void OldOfflineMigrationPolicy::monitor(int pid, addrint addr) {

}

PageType OldOfflineMigrationPolicy::allocate(int pid, addrint addr, bool read, bool instr) {
    myassert(pid == thisPid);
    PageType ret = OldBaseMigrationPolicy::allocate(pid, addr, read, instr);
    PageMap::iterator pit = pages.find(addr);
    if (pit == pages.end()) {
        pit = pages.emplace(addr, PageEntry(INVALID)).first;
    }
    myassert(pit != pages.end());
    myassert(pit->second.type == INVALID);
    pit->second.cur = 0;
    pit->second.type = ret;

    while (pit->second.cur < pit->second.counters.size() && pit->second.counters[pit->second.cur].interval < previousInterval) {
        ++pit->second.cur;
    }
    uint64 sum = 0;
    uint64 i = pit->second.cur;
    uint64 lastInterval = previousInterval + intervalCount;
    while (i < pit->second.counters.size() && pit->second.counters[i].interval < lastInterval) {
        uint64 windex = pit->second.counters[i].interval - previousInterval;
        sum += pit->second.counters[i].count * weights.at(windex);
        ++i;
    }

    if (pit->second.type == DRAM) {
        dramMetricMap.emplace(sum, pit->first);
    } else if (pit->second.type == PCM) {
        pcmMetricMap.emplace(sum, pit->first);
    } else {
        myassert(false);
    }

    return ret;
}

bool OldOfflineMigrationPolicy::selectPage(int *pid, addrint *addr) {
    uint64 timestamp = engine->getTimestamp();
    uint64 curInstr = instrCounter->getTotalValue();
    uint64 currentInterval = curInstr / period + 1;

    //cout << curInstr << " " << currentInterval << endl;

    if (previousInterval != currentInterval) {
        //		struct timeval start;
        //		gettimeofday(&start, NULL);

        previousInterval = currentInterval;

        dramMetricMap.clear();
        pcmMetricMap.clear();

        uint64 lastInterval = currentInterval + intervalCount;

        //cout << "currentInterval: " << currentInterval << " lastInterval: " << lastInterval << endl;
        //if ()

        for (PageMap::iterator it = pages.begin(); it != pages.end(); ++it) {
            if (it->second.type != INVALID) {
                while (it->second.cur < it->second.counters.size() && it->second.counters[it->second.cur].interval < currentInterval) {
                    ++it->second.cur;
                }
                uint64 sum = 0;
                uint64 i = it->second.cur;
                while (i < it->second.counters.size() && it->second.counters[i].interval < lastInterval) {
                    uint64 windex = it->second.counters[i].interval - currentInterval;
                    sum += it->second.counters[i].count * weights.at(windex);
                    ++i;
                }

                if (it->second.type == DRAM) {
                    dramMetricMap.emplace(sum, it->first);
                } else if (it->second.type == PCM) {
                    pcmMetricMap.emplace(sum, it->first);
                } else {
                    myassert(false);
                }
            }
        }

        //		struct timeval end;
        //		gettimeofday(&end, NULL);
        //		long seconds  = end.tv_sec  - start.tv_sec;
        //		long useconds = end.tv_usec - start.tv_usec;
        //		long mtime = seconds * 1000000 + useconds;
        //		cout << mtime << endl;

    }

    //Find page to migrate

    PcmMetricMap::iterator addrOfMaxPcmIt = pcmMetricMap.begin();
    if (addrOfMaxPcmIt == pcmMetricMap.end() || addrOfMaxPcmIt->first == 0) {
        //warn("There are no PCM pages ranked. pcmMetricMap.size(): %lu", pcmMetricMap.size());
        //		if (engine->getTimestamp() >= debug){
        //			cout << engine->getTimestamp() << ": instruction: " << instrCounter->getTotalValue() << "No PCM pages ranked"<< endl;
        //		}
        return false;
    }
    PageMap::iterator maxPcmIt = pages.find(addrOfMaxPcmIt->second);
    myassert(maxPcmIt != pages.end());

    if (dramPagesLeft <= 0) {
        //migrate to PCM
        DramMetricMap::iterator addrOfMinDramIt = dramMetricMap.begin();
        if (addrOfMinDramIt == dramMetricMap.end()) {
            //warn("There are no DRAM pages ranked");
            debug(": instruction: %lu. No DRAM pages ranked", instrCounter->getTotalValue());
            return false;
        }
        PageMap::iterator minDramIt = pages.find(addrOfMinDramIt->second);
        myassert(minDramIt != pages.end());

        if (addrOfMaxPcmIt->first > addrOfMinDramIt->first * metricThreshold) {
            debug(": instruction: %lu; DRAM(%lu, %lu) to PCM(%lu, %lu)", instrCounter->getTotalValue(), addrOfMinDramIt->first, addrOfMinDramIt->second, addrOfMaxPcmIt->first, addrOfMaxPcmIt->second);
            pcmMetricMap.emplace(addrOfMinDramIt->first, addrOfMinDramIt->second);
            dramMetricMap.erase(addrOfMinDramIt);
            myassert(minDramIt->second.type == DRAM);
            *pid = thisPid;
            *addr = minDramIt->first;
            minDramIt->second.type = PCM;
            dramPagesLeft++;
            return true;
        } else {
            debug(": instruction: %lu. Below threshold", instrCounter->getTotalValue());
            return false;
        }
    } else {
        //migrate to DRAM
        debug(": instruction: %lu; PCM(%lu, %lu) to DRAM", instrCounter->getTotalValue(), addrOfMaxPcmIt->first, addrOfMaxPcmIt->second);
        dramMetricMap.emplace(addrOfMaxPcmIt->first, addrOfMaxPcmIt->second);
        pcmMetricMap.erase(addrOfMaxPcmIt);
        myassert(maxPcmIt->second.type == PCM);
        *pid = thisPid;
        *addr = maxPcmIt->first;
        maxPcmIt->second.type = DRAM;
        dramPagesLeft--;
        return true;
    }

}

OldMultiQueueMigrationPolicy::OldMultiQueueMigrationPolicy(
        const string& nameArg,
        Engine *engineArg,
        uint64 debugArg,
        uint64 dramPagesArg,
        AllocationPolicy allocPolicyArg,
        IAllocator *allocatorArg,
        unsigned numPidsArg,
        unsigned numQueuesArg,
        unsigned thresholdQueueArg,
        uint64 lifetimeArg,
        bool logicalTimeArg,
        uint64 filterThresholdArg,
        bool secondDemotionEvictionArg,
        bool agingArg,
        bool useHistoryArg,
        bool usePendingListArg) :
OldBaseMigrationPolicy(nameArg, engineArg, debugArg, dramPagesArg, allocPolicyArg, allocatorArg, numPidsArg),
numQueues(numQueuesArg),
thresholdQueue(thresholdQueueArg),
lifetime(lifetimeArg),
logicalTime(logicalTimeArg),
filterThreshold(filterThresholdArg),
secondDemotionEviction(secondDemotionEvictionArg),
aging(agingArg),
useHistory(useHistoryArg),
usePendingList(usePendingListArg) {

    queues[DRAM].resize(numQueues);
    queues[PCM].resize(numQueues);
    thresholds.resize(numQueues);

    for (unsigned i = 0; i < numQueues - 1; i++) {
        thresholds[i] = static_cast<uint64> (pow(2.0, (int) i + 1));
    }
    thresholds[numQueues - 1] = numeric_limits<uint64>::max();

    pages = new PageMap[numPids];

    currentTime = 0;
}

void OldMultiQueueMigrationPolicy::monitor(int pid, addrint addr) {
    cout << "Monitor ADDR: " << addr << endl;
    /*	static int i=0;
            if(i %1000 == 0)
                    cout<<"monitor in migration: "<< i<<endl;
            i++;*/
    int index = numPids == 1 ? 0 : pid;
    uint64 timestamp = engine->getTimestamp();
    PageMap::iterator it = pages[index].find(addr);
    myassert(it != pages[index].end());
    bool mon;
    if (it->second.firstMonitor) {
        if (timestamp - it->second.lastMonitor >= filterThreshold) {
            mon = true;
        } else {
            mon = false;
        }
    } else {
        mon = true;
        it->second.firstMonitor = true;
    }

    if (mon) {
        currentTime++;
        it->second.lastMonitor = timestamp;
        uint64 exp = (logicalTime ? currentTime : timestamp) + lifetime;
        if (it->second.queue == -2) {
            //bring back from history list
            uint64 oldCount = it->second.accessIt->count;
            if (useHistory) {
                if (aging) {
                    uint64 timeSinceExpiration = timestamp - it->second.accessIt->expirationTime;
                    uint64 periodsSinceExpiration = timeSinceExpiration / lifetime;
                    if (periodsSinceExpiration >= 64) {
                        periodsSinceExpiration = 63;
                    }
                    oldCount /= static_cast<uint64> (pow(2.0l, static_cast<int> (periodsSinceExpiration)));
                }
            } else {
                oldCount = 0;
            }
            history.erase(it->second.accessIt);
            it->second.queue = 0;
            for (unsigned i = 0; i < numQueues - 1; i++) {
                if (oldCount < thresholds[i]) {
                    it->second.queue = i;
                    break;
                }
            }
            it->second.accessIt = queues[it->second.type][it->second.queue].emplace(queues[it->second.type][it->second.queue].end(), AccessEntry(pid, addr, exp, oldCount, false));

        } else if (it->second.queue == -1) {
            //bring back from victim list
            uint64 oldCount = it->second.accessIt->count;
            if (aging) {
                uint64 timeSinceExpiration = timestamp - it->second.accessIt->expirationTime;
                uint64 periodsSinceExpiration = timeSinceExpiration / lifetime;
                if (periodsSinceExpiration >= 64) {
                    periodsSinceExpiration = 63;
                }
                oldCount /= static_cast<uint64> (pow(2.0l, static_cast<int> (periodsSinceExpiration)));
            }
            victims.erase(it->second.accessIt);
            it->second.queue = 0;
            for (unsigned i = 0; i < numQueues - 1; i++) {
                if (oldCount < thresholds[i]) {
                    it->second.queue = i;
                    break;
                }
            }
            it->second.accessIt = queues[it->second.type][it->second.queue].emplace(queues[it->second.type][it->second.queue].end(), AccessEntry(pid, addr, exp, oldCount, false));
        } else if (it->second.queue >= 0) {
            it->second.accessIt->count++;
            uint64 oldCount = it->second.accessIt->count;
            queues[it->second.type][it->second.queue].erase(it->second.accessIt);
            if (oldCount >= thresholds[it->second.queue]) {
                it->second.queue++;
                if (usePendingList && it->second.queue == thresholdQueue) {
                    pending.emplace_back(make_pair(index, it->first));
                }
            }
            it->second.accessIt = queues[it->second.type][it->second.queue].emplace(queues[it->second.type][it->second.queue].end(), AccessEntry(pid, addr, exp, oldCount, false));
        } else {
            myassert(false);
        }

        for (unsigned i = 0; i < 2; i++) {
            for (vector<AccessQueue>::iterator qit = queues[i].begin(); qit != queues[i].end(); ++qit) {
                if (!qit->empty()) {
                    if ((logicalTime && currentTime > qit->front().expirationTime) || (!logicalTime && timestamp > qit->front().expirationTime)) {
                        int frontPid = qit->front().pid;
                        PageMap::iterator pageIt = pages[frontPid].find(qit->front().addr);
                        myassert(pageIt != pages[frontPid].end());
                        uint64 oldCount = pageIt->second.accessIt->count;
                        uint64 exp = (logicalTime ? currentTime : timestamp) + lifetime;
                        if (aging) {
                            oldCount /= 2;
                        }
                        if (pageIt->second.queue == 0) {
                            queues[i][pageIt->second.queue].erase(pageIt->second.accessIt);
                            if (pageIt->second.type == DRAM) {
                                pageIt->second.accessIt = victims.emplace(victims.end(), AccessEntry(frontPid, pageIt->first, pageIt->second.type, exp, oldCount));
                                pageIt->second.queue = -1;
                            } else {
                                pageIt->second.accessIt = history.emplace(history.end(), AccessEntry(frontPid, pageIt->first, pageIt->second.type, exp, oldCount));
                                pageIt->second.queue = -2;
                            }
                        } else if (pageIt->second.queue > 0) {
                            if (secondDemotionEviction && pageIt->second.accessIt->demoted) {
                                queues[i][pageIt->second.queue].erase(pageIt->second.accessIt);
                                if (pageIt->second.type == DRAM) {
                                    pageIt->second.accessIt = victims.emplace(victims.end(), AccessEntry(frontPid, pageIt->first, pageIt->second.type, exp, oldCount));
                                    pageIt->second.queue = -1;
                                } else {
                                    pageIt->second.accessIt = history.emplace(history.end(), AccessEntry(frontPid, pageIt->first, pageIt->second.type, exp, oldCount));
                                    pageIt->second.queue = -2;
                                }
                            } else {
                                queues[i][pageIt->second.queue].erase(pageIt->second.accessIt);
                                pageIt->second.queue--;
                                pageIt->second.accessIt = queues[i][pageIt->second.queue].emplace(queues[i][pageIt->second.queue].end(), AccessEntry(frontPid, pageIt->first, exp, oldCount, true));
                            }
                        } else {
                            myassert(false);
                        }
                    }
                }
            }
        }
        //printQueue(cout);
    }
}

PageType OldMultiQueueMigrationPolicy::allocate(int pid, addrint addr, bool read, bool instr) {
    //	int index = numPids == 1 ? 0 : pid;
    //	PageType ret = OldBaseMigrationPolicy::allocate(pid, addr, read, instr);
    //	bool ins = pages[index].emplace(addr, PageEntry(ret, history.emplace(history.end(), AccessEntry(pid, addr, ret, 0, 0)))).second;
    //	myassert(ins);
    //	return ret;
    static long selectPageCount3;
    selectPageCount3++;
    if (selectPageCount3 % 10 == 0)
        cout << "old_mutiqueue_allocate: " << selectPageCount3 << endl;

    int index = numPids == 1 ? 0 : pid;
    PageType ret = OldBaseMigrationPolicy::allocate(pid, addr, read, instr);
    if (ret == DRAM) {
        uint64 exp = (logicalTime ? currentTime : engine->getTimestamp()) + lifetime;
        uint64 count = thresholds[thresholdQueue - 1];
        auto ait = queues[DRAM][thresholdQueue].emplace(queues[DRAM][thresholdQueue].end(), AccessEntry(pid, addr, exp, count, false));
        bool ins = pages[index].emplace(addr, PageEntry(ret, thresholdQueue, ait)).second;
        myassert(ins);
    } else if (ret == PCM) {
        auto ait = history.emplace(history.end(), AccessEntry(pid, addr, 0, 0, false));
        bool ins = pages[index].emplace(addr, PageEntry(ret, -2, ait)).second;
        myassert(ins);
    } else {
        myassert(false);
    }
    return ret;
}

bool OldMultiQueueMigrationPolicy::selectPage(int *pid, addrint *addr) {
    static long selectPageCount1;
    selectPageCount1++;
    if (selectPageCount1 % 1000 == 0)
        cout << "mutiqueue_selectpge" << selectPageCount1 << endl;
    if (dramPagesLeft <= 0) {
        //migrate to PCM
        if (victims.empty()) {
            for (int i = 0; i < thresholdQueue; i++) {
                //cout << "queue[DRAM][" << i << "]: " << queues[DRAM][i].size() << endl;
                if (!queues[DRAM][i].empty()) {
                    AccessEntry entry(queues[DRAM][i].front());
                    queues[DRAM][i].pop_front();
                    *pid = entry.pid;
                    *addr = entry.addr;
                    PageMap::iterator pageIt = pages[*pid].find(*addr);
                    pageIt->second.type = PCM;
                    pageIt->second.accessIt = queues[PCM][pageIt->second.queue].emplace(queues[PCM][pageIt->second.queue].end(), entry);

                    dramPagesLeft++;
                    return true;
                }
            }
            return false;
        } else {
            AccessEntry entry = victims.front();
            PageMap::iterator pageIt = pages[entry.pid].find(entry.addr);
            myassert(pageIt != pages[entry.pid].end());
            myassert(pageIt->second.type == DRAM);
            myassert(pageIt->second.queue == -1);
            uint64 oldCount = pageIt->second.accessIt->count;
            uint64 exp = pageIt->second.accessIt->expirationTime;
            victims.pop_front();
            *pid = entry.pid;
            *addr = entry.addr;
            pageIt->second.type = PCM;
            pageIt->second.accessIt = history.emplace(history.end(), AccessEntry(entry.pid, entry.addr, pageIt->second.type, exp, oldCount));
            pageIt->second.queue = -2;
            dramPagesLeft++;
            //printQueue(cout);
            return true;
        }
    } else {
        //printQueue(cout);
        if (usePendingList) {
            list<PidAddrPair>::iterator listIt = pending.begin();
            while (listIt != pending.end()) {
                PageMap::iterator it = pages[listIt->first].find(listIt->second);
                myassert(it != pages[listIt->first].end());
                if (it->second.type == DRAM || (it->second.type == PCM && it->second.queue < thresholdQueue)) {
                    listIt = pending.erase(listIt);
                } else {
                    ++listIt;
                }
            }

            if (pending.empty()) {
                return false;
            } else {
                *pid = pending.front().first;
                *addr = pending.front().second;
                pending.pop_front();
                PageMap::iterator maxIt = pages[*pid].find(*addr);
                maxIt->second.type = DRAM;
                AccessQueue::iterator oldAccessIt = maxIt->second.accessIt;
                maxIt->second.accessIt = queues[DRAM][maxIt->second.queue].emplace(queues[DRAM][maxIt->second.queue].end(), AccessEntry(*pid, *addr, maxIt->second.accessIt->expirationTime, maxIt->second.accessIt->count, maxIt->second.accessIt->demoted));
                queues[PCM][maxIt->second.queue].erase(oldAccessIt);
                dramPagesLeft--;
                //printQueue(cout);
                return true;
            }
        } else {
            for (unsigned i = 0; i < numQueues - thresholdQueue; i++) {
                unsigned q = numQueues - 1 - i;
                //cout << "queue[PCM][" << q << "]: " << queues[PCM][q].size() << endl;
                if (!queues[PCM][q].empty()) {
                    AccessEntry entry(queues[PCM][q].front());
                    queues[PCM][q].pop_front();
                    *pid = entry.pid;
                    *addr = entry.addr;
                    PageMap::iterator pageIt = pages[entry.pid].find(entry.addr);
                    pageIt->second.type = DRAM;
                    pageIt->second.accessIt = queues[DRAM][pageIt->second.queue].emplace(queues[DRAM][pageIt->second.queue].end(), entry);

                    dramPagesLeft--;
                    return true;
                }
            }
            return false;
        }

    }

}

void OldMultiQueueMigrationPolicy::process(const Event * event) {

}

void OldMultiQueueMigrationPolicy::printQueue(ostream& os) {
    //	os << "v: ";
    //	for (AccessQueue::iterator it = victims.begin(); it != victims.end(); ++it){
    //		os << it->count << " ";
    //	}
    //	os << endl;
    os << "v size: " << victims.size() << endl;

    for (unsigned i = 0; i < numQueues; i++) {
        for (unsigned j = 0; j < 2; j++) {
            if (j == DRAM) {
                os << i << "DRAM: ";
            } else if (j == PCM) {
                os << i << "PCM: ";
            }
            for (AccessQueue::iterator it = queues[j][i].begin(); it != queues[j][i].end(); ++it) {
                os << it->count << " ";
            }
            os << endl;
        }
    }
}

OldFirstTouchMigrationPolicy::OldFirstTouchMigrationPolicy(
        const string& nameArg,
        Engine *engineArg,
        uint64 debugArg,
        uint64 dramPagesArg,
        AllocationPolicy allocPolicyArg,
        IAllocator *allocatorArg,
        unsigned numPidsArg) : OldBaseMigrationPolicy(nameArg, engineArg, debugArg, dramPagesArg, allocPolicyArg, allocatorArg, numPidsArg), lastPcmAccessValid(false) {

    currentIt = queue.begin();
    pages = new PageMap[numPids];
}

PageType OldFirstTouchMigrationPolicy::allocate(int pid, addrint addr, bool read, bool instr) {
    int index = numPids == 1 ? 0 : pid;
    PageType ret = OldBaseMigrationPolicy::allocate(pid, addr, read, instr);
    AccessQueue::iterator accessIt = queue.end();
    if (ret == DRAM) {
        accessIt = queue.emplace(currentIt, AccessEntry(pid, addr));
        currentIt = accessIt;
        ++currentIt;
        if (currentIt == queue.end()) {
            currentIt = queue.begin();
        }
    }
    bool ins = pages[index].emplace(addr, PageEntry(ret, accessIt)).second;
    myassert(ins);
    return ret;
}

void OldFirstTouchMigrationPolicy::monitor(int pid, addrint addr) {
    int index = numPids == 1 ? 0 : pid;
    PageMap::iterator it = pages[index].find(addr);
    myassert(it != pages[index].end());
    it->second.accessIt->ref = true;
    if (it->second.type == PCM) {
        lastPcmAccessValid = true;
        lastPcmAccessPid = pid;
        lastPcmAccessAddr = addr;
    }
}

bool OldFirstTouchMigrationPolicy::selectPage(int *pid, addrint *addr) {
    if (dramPagesLeft <= 0) {
        //migrate to PCM
        while (currentIt->ref) {
            currentIt->ref = false;
            ++currentIt;
            if (currentIt == queue.end()) {
                currentIt = queue.begin();
            }

        }
        *pid = currentIt->pid;
        *addr = currentIt->addr;
        PageMap::iterator it = pages[currentIt->pid].find(currentIt->addr);
        myassert(it != pages[currentIt->pid].end());
        myassert(it->second.accessIt == currentIt);
        it->second.type = PCM;
        it->second.accessIt = queue.end();
        currentIt = queue.erase(currentIt);
        if (currentIt == queue.end()) {
            currentIt = queue.begin();
        }
        lastPcmAccessValid = false;
        dramPagesLeft++;
        return true;
    } else {
        //migrate to DRAM
        if (lastPcmAccessValid) {
            *pid = lastPcmAccessPid;
            *addr = lastPcmAccessAddr;
            PageMap::iterator it = pages[lastPcmAccessPid].find(lastPcmAccessAddr);
            myassert(it != pages[lastPcmAccessPid].end());
            it->second.type = DRAM;
            it->second.accessIt = queue.emplace(currentIt, AccessEntry(lastPcmAccessPid, lastPcmAccessAddr));

            lastPcmAccessValid = false;
            dramPagesLeft--;
            return true;
        } else {
            return false;
        }
    }
}

OldDoubleClockMigrationPolicy::OldDoubleClockMigrationPolicy(
        const string& nameArg,
        Engine *engineArg,
        uint64 debugArg,
        uint64 dramPagesArg,
        AllocationPolicy allocPolicyArg,
        IAllocator *allocatorArg,
        unsigned numPidsArg) : OldBaseMigrationPolicy(nameArg, engineArg, debugArg, dramPagesArg, allocPolicyArg, allocatorArg, numPidsArg) {

    currentDramIt = dramQueue.begin();
    pages = new PageMap[numPids];
}

PageType OldDoubleClockMigrationPolicy::allocate(int pid, addrint addr, bool read, bool instr) {
    int index = numPids == 1 ? 0 : pid;
    PageType ret = OldBaseMigrationPolicy::allocate(pid, addr, read, instr);
    AccessQueue::iterator accessIt;
    ListType list;
    if (ret == DRAM) {
        accessIt = dramQueue.emplace(currentDramIt, AccessEntry(pid, addr));
        currentDramIt = accessIt;
        ++currentDramIt;
        if (currentDramIt == dramQueue.end()) {
            currentDramIt = dramQueue.begin();
        }
        list = DRAM_LIST;
    } else {
        accessIt = pcmInactiveQueue.emplace(pcmInactiveQueue.end(), AccessEntry(pid, addr));
        list = PCM_INACTIVE_LIST;
    }
    bool ins = pages[index].emplace(addr, PageEntry(list, accessIt)).second;
    myassert(ins);
    return ret;
}

void OldDoubleClockMigrationPolicy::monitor(int pid, addrint addr) {
    cout << "DOUBLECLOCK MONITOR ADDR: " << addr << endl;
    /*	static int i=0;
                    if(i %100 == 0)
                            cout<<"monitor in migration: "<< i<<endl;
                    i++;*/
    int index = numPids == 1 ? 0 : pid;
    PageMap::iterator it = pages[index].find(addr);
    myassert(it != pages[index].end());
    if (it->second.type == DRAM_LIST) {
        it->second.accessIt->ref = true;
    } else if (it->second.type == PCM_ACTIVE_LIST) {
        it->second.accessIt->ref = true;
    } else if (it->second.type == PCM_INACTIVE_LIST) {
        pcmInactiveQueue.erase(it->second.accessIt);
        it->second.type = PCM_ACTIVE_LIST;
        it->second.accessIt = pcmActiveQueue.emplace(pcmActiveQueue.end(), AccessEntry(pid, addr, true));
    } else {
        myassert(false);
    }
}

bool OldDoubleClockMigrationPolicy::selectPage(int *pid, addrint *addr) {
    cout << "Doubleclock SELECTPAGE ADDR: " << *addr << endl;
    /*	static long selectPageCount;
            selectPageCount++;
            if(selectPageCount%10==0)
                    cout << "DOubleclock_selectpge"<<selectPageCount<<endl;*/
    if (dramPagesLeft <= 0) {
        //migrate to PCM (demotion)
        while (currentDramIt->ref) {
            currentDramIt->ref = false;
            ++currentDramIt;
            if (currentDramIt == dramQueue.end()) {
                currentDramIt = dramQueue.begin();
            }

        }
        *pid = currentDramIt->pid;
        *addr = currentDramIt->addr;
        PageMap::iterator it = pages[currentDramIt->pid].find(currentDramIt->addr);
        myassert(it != pages[currentDramIt->pid].end());
        myassert(it->second.accessIt == currentDramIt);
        it->second.type = PCM_INACTIVE_LIST;
        it->second.accessIt = pcmInactiveQueue.emplace(pcmInactiveQueue.end(), AccessEntry(it->first, false));
        currentDramIt = dramQueue.erase(currentDramIt);
        if (currentDramIt == dramQueue.end()) {
            currentDramIt = dramQueue.begin();
        }

        dramPagesLeft++;
        return true;
    } else {
        //migrate to DRAM (promotion)

        bool found = false;
        AccessQueue::iterator pcmIt = pcmActiveQueue.begin();
        while (pcmIt != pcmActiveQueue.end()) {
            if (pcmIt->ref) {
                if (!found) {
                    found = true;
                    *pid = pcmIt->pid;
                    *addr = pcmIt->addr;
                    pcmIt = pcmActiveQueue.erase(pcmIt);
                } else {
                    pcmIt->ref = false;
                    pcmIt++;
                }
            } else {
                PageMap::iterator it = pages[pcmIt->pid].find(pcmIt->addr);
                myassert(it != pages[pcmIt->pid].end());
                it->second.type = PCM_INACTIVE_LIST;
                it->second.accessIt = pcmInactiveQueue.emplace(pcmInactiveQueue.end(), AccessEntry(pcmIt->pid, pcmIt->addr));
                pcmIt = pcmActiveQueue.erase(pcmIt);
            }
        }

        if (found) {
            PageMap::iterator it = pages[*pid].find(*addr);
            myassert(it != pages[*pid].end());
            it->second.type = DRAM_LIST;
            it->second.accessIt = dramQueue.emplace(currentDramIt, AccessEntry(*pid, *addr));
            dramPagesLeft--;
            return true;
        } else {
            return false;
        }
    }
}

OldTwoLRUMigrationPolicy::OldTwoLRUMigrationPolicy(
        const string& nameArg,
        Engine *engineArg,
        uint64 debugArg,
        uint64 dramPagesArg,
        AllocationPolicy allocPolicyArg,
        IAllocator *allocatorArg,
        unsigned numPidsArg) : OldBaseMigrationPolicy(nameArg, engineArg, debugArg, dramPagesArg, allocPolicyArg, allocatorArg, numPidsArg) {


    pages = new PageMap[numPids];
}

PageType OldTwoLRUMigrationPolicy::allocate(int pid, addrint addr, bool read, bool instr) {
    //cout<<addr<<" Allocare"<<endl;
    int index = numPids == 1 ? 0 : pid;
    PageType ret = OldBaseMigrationPolicy::allocate(pid, addr, read, instr);
    AccessQueue::iterator accessIt;
    ListType list;
    if (ret == DRAM) {
        accessIt = dramQueue.emplace(dramQueue.begin(), AccessEntry(pid, addr, 0));
        list = DRAM_LIST;
    } else {
        accessIt = pcmQueue.emplace(pcmQueue.begin(), AccessEntry(pid, addr, 0));
        list = PCM_LIST;
    }
    if (addr == 34225147537)
        cout << "insert" << endl;
    bool ins = pages[index].emplace(addr, PageEntry(list, accessIt)).second;
    myassert(ins);

    return ret;
}

void OldTwoLRUMigrationPolicy::monitor(int pid, addrint addr) {
    cout<<"two lru MONITOR ADDR: "<<addr<<endl;
    int index = numPids == 1 ? 0 : pid;
    if (addr == 34128307266)
        cout << "addr?!" << endl;
    PageMap::iterator it = pages[index].find(addr);
    //	cout<<addr<<"MONITOR CALL IN TWOLRU"<<endl;
    myassert(it != pages[index].end());
    if (it->second.type == DRAM_LIST) {
        //	cout<<addr<<" WAS THE MONITOR ADDR IN DRAM "<<endl;
        it->second.accessIt->hitCount++;
        AccessEntry temp = *it->second.accessIt;
        dramQueue.erase(it->second.accessIt);
        it->second.accessIt = dramQueue.emplace(dramQueue.begin(), temp);
        cout << "dummy" <<endl;
        //currentDramIt=dramQueue.begin();
    } else if (it->second.type == PCM_LIST) {
        it->second.accessIt->hitCount++;
        AccessEntry temp = *it->second.accessIt;
        pcmQueue.erase(it->second.accessIt);
        it->second.accessIt = pcmQueue.emplace(pcmQueue.begin(), temp);
    } else {
        myassert(false);
    }
}

bool OldTwoLRUMigrationPolicy::selectPage(int *pid, addrint *addr) {

    //	if(*addr == 34225228180)
    //		cout<<"TWOLRU SELECTPAGE ADDR: "<<*addr<<endl;
    //	AccessQueue::iterator dramIt = dramQueue.begin();
    //cout << "2" <<flush<<endl;
    //	while (dramIt != dramQueue.end()){
    //				cout<<dramIt->hitCount<<endl;
    //
    //					dramIt++;
    //			}
    if (dramPagesLeft <= 0) {
        AccessQueue::iterator dramIt = dramQueue.end();
        //cout << "3" <<flush<<endl;
        //	while (dramIt != dramQueue.end()){
        //		cout<<"DRAM HIT COUNT: "<<dramIt->hitCount<<endl;
        //			if(dramIt->hitCount<100)

        //			{
        if (dramQueue.size() < 1) {
            cout << "WHAT?!" << endl;
            myassert(false);
        }

        dramIt--;


        //				break;
        //			}
        //			else
        //				dramIt++;
        //}
        //migrate to PCM (demotion)
        //	cout << "4" <<flush<<endl;
        *pid = dramIt->pid;
        *addr = dramIt->addr;
        PageMap::iterator it = pages[dramIt->pid].find(dramIt->addr);
        //cout << "5" <<flush<<endl;
        myassert(it != pages[dramIt->pid].end());
        myassert(it->second.accessIt == dramIt);
        it->second.type = PCM_LIST;
        it->second.accessIt = pcmQueue.emplace(pcmQueue.begin(), AccessEntry(0, it->first, 0));
        //cout << "6" <<flush<<endl;
        dramQueue.erase(dramIt);
        //cout << "7" <<flush<<endl;
        //if (currentDramIt == dramQueue.end()){
        //	cout<<"HOW CAN I EXACTLY REACH HERE?! BUG POSSIBILITY!!"<<endl;
        //	currentDramIt = dramQueue.begin();
        //}
        //cout << "8" <<flush<<endl;
        dramPagesLeft++;
        //		cout<<*addr<<" selpage"<<endl;
        return true;
    } else {
        //migrate to DRAM (promotion)

        bool found = false;
        AccessQueue::iterator pcmIt = pcmQueue.begin();
        //		while (pcmIt != pcmQueue.end()){

        if (pcmIt->hitCount > 100) {
            found = true;
            *pid = pcmIt->pid;
            *addr = pcmIt->addr;
            //cout << "9" <<flush<<endl;
            pcmIt = pcmQueue.erase(pcmIt);

        }
        //	else
        //	pcmIt++;
        //		}
        /*		if (pcmIt->hitCount>1){
                                if (!found){
                                        found = true;
                                        pcmIt=pcmQueue.erase(pcmIt);
                                }
                        }*/

        if (found) {
            //cout<<"ADDress: "<<*addr<<dramPagesLeft<<endl;
            //cout<<"before find: "<< *addr<<endl;
            PageMap::iterator it = pages[*pid].find(*addr);
            //cout<<"after find: "<< *pid<<endl;
            myassert(it != pages[*pid].end());
            //cout<<"after ass: "<< *pid<<endl;
            it->second.type = DRAM_LIST;
            //	currentDramIt=dramQueue.end();
            it->second.accessIt = dramQueue.emplace(dramQueue.end(), AccessEntry(*pid, *addr, 0));
            dramPagesLeft--;

            //	cout<<*addr<<" selpage"<<endl;
            return true;
        } else {
            //	cout<<*addr<<" selpage2"<<endl;
            return false;
        }
    }
}

istream& operator>>(istream& lhs, AllocationPolicy& rhs) {
    string s;
    lhs >> s;
    if (s == "dram_first") {
        rhs = DRAM_FIRST;
    } else if (s == "pcm_only") {
        rhs = PCM_ONLY;
    } else {
        error("Invalid monitoring strategy: %s", s.c_str());
    }
    return lhs;
}

ostream& operator<<(ostream& lhs, AllocationPolicy rhs) {
    if (rhs == DRAM_FIRST) {
        lhs << "dram_first";
    } else if (rhs == PCM_ONLY) {
        lhs << "pcm_only";
    } else {
        error("Invalid monitoring strategy");
    }
    return lhs;
}
