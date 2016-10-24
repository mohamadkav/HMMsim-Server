/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#ifndef MIGRATION_H_
#define MIGRATION_H_


#include "Counter.H"
#include "Engine.H"
#include "Error.H"
#include "Statistics.H"
#include "Types.H"


using namespace std;

enum AllocationPolicy {
	DRAM_FIRST,
	PCM_ONLY,
	CUSTOM
};

class IMigrationPolicy {
public:
	virtual PageType allocate(int pid, addrint addr, bool read, bool instr) = 0;
	virtual bool migrate(int pid, addrint addr) = 0;	//migrate on demand; returns whether page is a candidate
	virtual bool complete(int *pid, addrint *addr) = 0;	//complete on demand migration; determine which page, if any, should have its migration completed
	virtual bool rollback(int *pid, addrint *addr) = 0; //rollback on demand migration; determine which page, if any, should have its migration rolled back
	virtual bool demote(int *pid, addrint *addr) = 0;	//demote DRAM page to PCM; determine which page, if any, should be evicted from DRAM
	virtual void done(int pid, addrint addr) = 0;		//tells migration policy that a migration (or rollback) has finished
	virtual void monitor(const vector<CountEntry>& counts, const vector<ProgressEntry>& progress) = 0;	//update monitoring information internal to the policy
	virtual void setNumDramPages(uint64 dramPagesNew) = 0;
	virtual void setInstrCounter(Counter* counter) = 0;
	virtual ~IMigrationPolicy() {}
};

class BaseMigrationPolicy : public IMigrationPolicy {
protected:

	string name;
	Engine *engine;

	uint64 debugStart;

	Counter *instrCounter;

	uint64 dramPages;

	AllocationPolicy allocPolicy;

	unsigned numPids; 	//Number of processes served by this policy. Must be 1 or larger.
						//If equal to 1, the policy belongs to one process and 0 is used as the pid.
						//If greater than 1, the policy is shared accross processes and the pid passed as parameter in method calls is used.

	double maxFreeDram;
	uint32 completeThreshold;
	uint64 rollbackTimeout;

	int64 dramPagesLeft;
	int64 maxFreeDramPages;

	bool dramFull; //For use with DRAM_FIRST allocation policy only

	vector<ProgressEntry> progress;

public:
	BaseMigrationPolicy(
		const string& nameArg,
		Engine *engineArg,
		uint64 debugStartArg,
		uint64 dramPagesArg,
		AllocationPolicy allocPolicyArg,
		unsigned numPidsArg,
		double maxFreeDramArg,
		uint32 completeThresholdArg,
		uint64 rollbackTimeoutArg);
	PageType allocate(int pid, addrint addr, bool read, bool instr);
	bool complete(int *pid, addrint *addr);
	bool rollback(int *pid, addrint *addr);
	bool demote(int *pid, addrint *addr);
	void monitor(const vector<CountEntry>& counts, const vector<ProgressEntry>& progress);
	void setNumDramPages(uint64 dramPagesNew);
	void setInstrCounter(Counter* counter);
	virtual bool selectDemotionPage(int *pid, addrint *addr) = 0;
};

class NoMigrationPolicy : public BaseMigrationPolicy {

public:
	NoMigrationPolicy(
		const string& nameArg,
		Engine *engineArg,
		uint64 debugStartArg,
		uint64 dramPagesArg,
		AllocationPolicy allocPolicyArg,
		unsigned numPidsArg);
	bool migrate(int pid, addrint addr) {return false;}
	bool complete(int *pid, addrint *addr) {return false;}
	bool rollback(int *pid, addrint *addr) {return false;}
	void done(int pid, addrint addr) {}
	void monitor(const vector<CountEntry>& counts, const vector<ProgressEntry>& progress) {}
	bool selectDemotionPage(int *pid, addrint *addr) {return false;}
};

class MultiQueueMigrationPolicy : public BaseMigrationPolicy {
	unsigned numQueues;
	int thresholdQueue;
	uint64 lifetime;
	bool logicalTime;
	uint64 filterThreshold;
	bool secondDemotionEviction;
	bool aging;
	bool useHistory;
	bool usePendingList;
	bool enableRollback;
	bool promotionFilter;
	unsigned demotionAttempts;

	struct AccessEntry {
		int pid;
		addrint addr;
		uint64 expirationTime;
		uint64 count;
		bool demoted;
		bool migrating;
		AccessEntry(int pidArg, addrint addrArg, uint64 expirationTimeArg, uint64 countArg, bool demotedArg, bool migratingArg) : pid(pidArg), addr(addrArg), expirationTime(expirationTimeArg), count(countArg), demoted(demotedArg), migrating(migratingArg) {}
	};

	typedef list<AccessEntry> AccessQueue;

	struct PageEntry {
		PageType type;
		int queue;  //-1 means the victim list
					//-2 means this history list
		AccessQueue::iterator accessIt;
		PageEntry(PageType typeArg, int queueArg, const AccessQueue::iterator& accessItArg) : type(typeArg), queue(queueArg), accessIt(accessItArg) {}
	};

	//typedef map<addrint, PageEntry> PageMap;
	typedef unordered_map<addrint, PageEntry> PageMap;


	//2 queues: one for DRAM and one for PCM (index is type)
	vector<AccessQueue> queues[2];
	AccessQueue victims;
	AccessQueue history;
	vector<uint64> thresholds;

	PageMap *pages;

	typedef std::pair<int, addrint> PidAddrPair;

	list<PidAddrPair> pending;

	uint64 currentTime;

	unsigned tries;

public:
	MultiQueueMigrationPolicy(
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
		unsigned demotionAttemptsArg);
	PageType allocate(int pid, addrint addr, bool read, bool instr);
	bool migrate(int pid, addrint addr);
//	bool complete(int *pid, addrint *addr);
//	bool rollback(int *pid, addrint *addr);
	void done(int pid, addrint addr);
	void monitor(const vector<CountEntry>& counts, const vector<ProgressEntry>& progress);
	bool selectDemotionPage(int *pid, addrint *addr);
};


//Old Migration Policies:

class IAllocator {
public:
	virtual PageType hint(int pid, addrint addr, bool read, bool instr) = 0;
	virtual ~IAllocator() {}
};

class IOldMigrationPolicy {
public:
	virtual void setInstrCounter(Counter* counter) = 0;
	virtual void monitor(int pid, addrint addr) = 0;
	virtual PageType allocate(int pid, addrint addr, bool read, bool instr) = 0;
	virtual bool migrate(int *pid, addrint *addr) = 0;
	virtual void changeNumDramPages(uint64 dramPagesNew) = 0;
	virtual ~IOldMigrationPolicy() {}
};

class OldBaseMigrationPolicy : public IOldMigrationPolicy {
protected:

	string name;
	Engine *engine;

	uint64 debugStart;

	Counter *instrCounter;

	uint64 dramPages;

	AllocationPolicy allocPolicy;
	IAllocator *allocator;
	//Stat<uint64> selectPageCount;
	unsigned numPids; 	//Number of processes served by this policy. Must be 1 or larger.
						//If equal to 1, the policy belongs to one process and 0 is used as the pid.
						//If greater than 1, the policy is shared accross processes and the pid passed as parameter in method calls is used.

	int64 dramPagesLeft;

	bool dramFull; //For use with DRAM_FIRST allocation policy only



public:
	OldBaseMigrationPolicy(
		const string& nameArg,
		Engine *engineArg,
		uint64 debugStartArg,
		uint64 dramPagesArg,
		AllocationPolicy allocPolicyArg,
		IAllocator *allocatorArg,
		unsigned numPidsArg);
	void setInstrCounter(Counter* counter);
	PageType allocate(int pid, addrint addr, bool read, bool instr);
	bool migrate(int *pid, addrint *addr);
	void changeNumDramPages(uint64 dramPagesNew);
	virtual bool selectPage(int *pid, addrint *addr) = 0;
};

class OldNoMigrationPolicy : public OldBaseMigrationPolicy {

public:
	OldNoMigrationPolicy(
		const string& nameArg,
		Engine *engineArg,
		uint64 debugStartArg,
		uint64 dramPagesArg,
		AllocationPolicy allocPolicyArg,
		IAllocator *allocatorArg,
		unsigned numPidsArg);
	void monitor(int pid, addrint addr) {}
	bool selectPage(int *pid, addrint *addr) {return false;}
};

class OldOfflineMigrationPolicy : public OldBaseMigrationPolicy {

	int thisPid;

	enum MetricType {
		ACCESSED,
		ACCESS_COUNT,
		TOUCH_COUNT
	};

	MetricType metricType;

	enum AccessType {
		READS,
		WRITES,
		ACCESSES
	};

	AccessType accessType;

	enum WeightType {
		UNIFORM,
		LINEAR,
		EXPONENTIAL
	};

	WeightType weightType;

	uint64 intervalCount;

	vector<uint64> weights;

	uint64 metricThreshold; //minumum difference between 2 pages to consider swapping them

//	struct PageInfo {
//		uint32 reads;
//		uint32 writes;
//		uint8 readBlocks;
//		uint8 writtenBlocks;
//		uint8 accessedBlocks;
//	};


	uint64 period;

	uint64 previousInterval;

	struct Entry {
		uint32 interval;
		uint32 count;
		Entry(uint32 intervalArg, uint32 countArg) : interval(intervalArg), count(countArg) {}
	};

	typedef vector<Entry> Counters;

	struct PageEntry {
		PageType type;
		uint64 cur;
		Counters counters;
		PageEntry(PageType typeArg) : type(typeArg), cur(0), counters() {}
	};

	typedef map<addrint, PageEntry> PageMap;

	PageMap pages;

	typedef multimap<uint64, addrint, less<uint64> > DramMetricMap;
	typedef multimap<uint64, addrint, greater<uint64> > PcmMetricMap;

	DramMetricMap dramMetricMap;
	PcmMetricMap pcmMetricMap;

public:
	OldOfflineMigrationPolicy(
		const string& nameArg,
		Engine *engineArg,
		uint64 debugStartArg,
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
		uint64 metricThresholdArg);

	void monitor(int pid, addrint addr);
	PageType allocate(int pid, addrint addr, bool read, bool instr);
	bool selectPage(int *pid, addrint *addr);
};


class OldMultiQueueMigrationPolicy : public OldBaseMigrationPolicy, public IEventHandler {
protected:
	unsigned numQueues;
	int thresholdQueue;
	uint64 lifetime;
	bool logicalTime;
	uint64 filterThreshold;
	bool secondDemotionEviction;
	bool aging;
	bool useHistory;
	bool usePendingList;

	struct AccessEntry {
		int pid;
		addrint addr;
		uint64 expirationTime;
		uint64 count;
		bool demoted;
		AccessEntry(int pidArg, addrint addrArg, uint64 expirationTimeArg, uint64 countArg, bool demotedArg) : pid(pidArg), addr(addrArg), expirationTime(expirationTimeArg), count(countArg), demoted(demotedArg) {}
	};

	typedef list<AccessEntry> AccessQueue;

	struct PageEntry {
		PageType type;
		int queue;  //-1 means the victim list
					//-2 means this history list
		AccessQueue::iterator accessIt;
		bool firstMonitor; //whether this page has been monitored for the first time
		uint64 lastMonitor;
		PageEntry(PageType typeArg, int queueArg, const AccessQueue::iterator& accessItArg) : type(typeArg), queue(queueArg), accessIt(accessItArg), firstMonitor(false), lastMonitor(0) {}
	};

	//typedef map<addrint, PageEntry> PageMap;
	typedef unordered_map<addrint, PageEntry> PageMap;


	vector<AccessQueue> queues[2];
	AccessQueue victims;
	AccessQueue history;
	vector<uint64> thresholds;

	PageMap *pages;

	typedef std::pair<int, addrint> PidAddrPair;

	list<PidAddrPair> pending;

	bool dramFull; //For use with DRAM_FIRST allocation policy only

	uint64 currentTime;


public:
	OldMultiQueueMigrationPolicy(
		const string& nameArg,
		Engine *engineArg,
		uint64 debugStartArg,
		uint64 dramPagesArg,
		AllocationPolicy allocPolicyArg,
		IAllocator *allocatorArg,
		unsigned numPids,
		unsigned numQueuesArg,
		unsigned thresholdQueueArg,
		uint64 lifetimeArg,
		bool logicalTimeArg,
		uint64 filterThresholdArg,
		bool secondDemotionEvictionArg,
		bool agingArg,
		bool useHistoryArg,
		bool usePendingListArg);
	void monitor(int pid, addrint addr);
	PageType allocate(int pid, addrint addr, bool read, bool instr);
	bool selectPage(int *pid, addrint *addr);

	void process(const Event * event);

private:
	void printQueue(ostream& os);
};

//First touch uses clock as the demotion policy
class OldFirstTouchMigrationPolicy : public OldBaseMigrationPolicy {
private:
	struct AccessEntry {
		int pid;
		addrint addr;
		bool ref;
		AccessEntry(int pidArg, addrint addrArg, bool refArg = false) : pid(pidArg), addr(addrArg), ref(refArg) {}
	};

	typedef list<AccessEntry> AccessQueue;

	AccessQueue queue;
	AccessQueue::iterator currentIt;

	struct PageEntry {
		PageType type;
		AccessQueue::iterator accessIt;
		PageEntry(PageType typeArg, AccessQueue::iterator accessItArg) : type(typeArg), accessIt(accessItArg) {}
	};

	typedef map<addrint, PageEntry> PageMap;

	PageMap *pages;

	bool lastPcmAccessValid;
	int lastPcmAccessPid;
	addrint lastPcmAccessAddr;

public:
	OldFirstTouchMigrationPolicy(
			const string& nameArg,
			Engine *engineArg,
			uint64 debugStartArg,
			uint64 dramPagesArg,
			AllocationPolicy allocPolicyArg,
			IAllocator *allocatorArg,
			unsigned numPidsArg);
	PageType allocate(int pid, addrint addr, bool read, bool instr);
	void monitor(int pid, addrint addr);
	bool selectPage(int *pid, addrint *addr);
};


class OldDoubleClockMigrationPolicy : public OldBaseMigrationPolicy {
private:
	struct AccessEntry {
		int pid;
		addrint addr;
		bool ref;
		AccessEntry(int pidArg, addrint addrArg, bool refArg = false) : pid(pidArg), addr(addrArg), ref(refArg) {}
	};

	typedef list<AccessEntry> AccessQueue;

	AccessQueue dramQueue;
	AccessQueue::iterator currentDramIt;

	AccessQueue pcmActiveQueue;
	AccessQueue pcmInactiveQueue;

	enum ListType {
		DRAM_LIST,
		PCM_ACTIVE_LIST,
		PCM_INACTIVE_LIST
	};

	struct PageEntry {
		ListType type;
		AccessQueue::iterator accessIt;
		PageEntry(ListType typeArg, AccessQueue::iterator accessItArg) : type(typeArg), accessIt(accessItArg) {}
	};

	typedef map<addrint, PageEntry> PageMap;

	PageMap *pages;

public:
	OldDoubleClockMigrationPolicy(
			const string& nameArg,
			Engine *engineArg,
			uint64 debugStartArg,
			uint64 dramPagesArg,
			AllocationPolicy allocPolicyArg,
			IAllocator *allocatorArg,
			unsigned numPidsArg);
	PageType allocate(int pid, addrint addr, bool read, bool instr);
	void monitor(int pid, addrint addr);
	bool selectPage(int *pid, addrint *addr);
};

class OldTwoLRUMigrationPolicy : public OldBaseMigrationPolicy{

private:
	struct AccessEntry {
		int pid;
		addrint addr;
		int hitCount;
		AccessEntry(int pidArg, addrint addrArg, int hitCountArg = 0) : pid(pidArg), addr(addrArg),hitCount(hitCountArg) {}
	};

	typedef list<AccessEntry> AccessQueue;

	AccessQueue dramQueue;
	AccessQueue::iterator currentDramIt;

	AccessQueue pcmQueue;

	enum ListType {
		DRAM_LIST,
		PCM_LIST,
	};

	struct PageEntry {
		ListType type;
		AccessQueue::iterator accessIt;
		PageEntry(ListType typeArg, AccessQueue::iterator accessItArg) : type(typeArg), accessIt(accessItArg) {}
	};

	typedef map<addrint, PageEntry> PageMap;

	PageMap *pages;

public:
	OldTwoLRUMigrationPolicy(
			const string& nameArg,
			Engine *engineArg,
			uint64 debugStartArg,
			uint64 dramPagesArg,
			AllocationPolicy allocPolicyArg,
			IAllocator *allocatorArg,
			unsigned numPidsArg);
	PageType allocate(int pid, addrint addr, bool read, bool instr);
	void monitor(int pid, addrint addr);
	bool selectPage(int *pid, addrint *addr);

};


istream& operator>>(istream& lhs, AllocationPolicy& rhs);

ostream& operator<<(ostream& lhs, AllocationPolicy rhs);

#endif /* MIGRATION_H_ */
