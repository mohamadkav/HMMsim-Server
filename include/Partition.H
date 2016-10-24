/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#ifndef PARTITION_H_
#define PARTITION_H_


#include "Counter.H"
#include "Error.H"
#include "Types.H"


using namespace std;

class IPartition {
public:
	virtual void calculate(uint64 cycles, vector<Counter *>& instrCounters) = 0;
	virtual unsigned getNumPolicies() = 0;
	virtual uint64 getDramPages(int pid) = 0;
	virtual double getRate(int pid) = 0;
	virtual ~IPartition() {}
};


class StaticPartition : public IPartition {
	unsigned numPolicies;
	uint64 dramPages;

	vector<uint64> dramPagesPerPid;
	vector<double> ratePerPid;

public:
	StaticPartition(unsigned numPoliciesArg, unsigned pageSizeArg, uint64 dramSizeArg, string dramFractionsArg, string rateFractionsArg);
	void calculate(uint64 cycles, vector<Counter *>& instrCounters);
	unsigned getNumPolicies() { return numPolicies; }
	uint64 getDramPages(int pid) { return dramPagesPerPid[pid]; }
	double getRate(int pid) { return ratePerPid[pid]; }
};

class OfflinePartition : public IPartition {
	unsigned numPolicies;
	uint64 dramPages;

	string prefix;
	string infix;
	string suffix;

	string periodType;

	vector<uint64> dramPagesPerPid;
	vector<double> ratePerPid;

	unsigned rateSpaceSize;

	map<unsigned, map<unsigned, CounterTraceReader*> > readers;

public:
	OfflinePartition(unsigned numPoliciesArg, unsigned pageSizeArg, uint64 dramSizeArg, const string& prefixArg, const string& infixArg, const string& suffixArg, const string& periodTypeArg);
	~OfflinePartition();
	void addCounterTrace(const string& name);

	void calculate(uint64 cycles, vector<Counter *>& instrCounters);
	unsigned getNumPolicies() { return numPolicies; }
	uint64 getDramPages(int pid) { return dramPagesPerPid[pid]; }
	double getRate(int pid) { return ratePerPid[pid]; }
};

class DynamicPartition : public IPartition {
	unsigned numPolicies;
	uint64 dramPages;

	double rateGran; //Granularity of rate allocation
	uint64 spaceGran; //Granularity of space allocation
	double constraint; //For the low-priority task, which is the second one

	vector<double> ratePerPid;
	vector<uint64> dramPagesPerPid;

	vector<double> prevRatePerPid;
	vector<uint64> prevDramPagesPerPid;

	vector<double> previousMetrics;

	const static int MAX_PROB = 10000;

	int rateProb; //Probability of changing the rate (as opposed to the space) during the next interval
	int moreRateProb; //Probability of giving more rate to the high priority application during the next interval
	int moreSpaceProb; //Probability of giving more space to the high priority application during the next interval

	//Record the decision made in the previous interval
	bool rate;
	bool moreRate;
	bool moreSpace;



public:
	DynamicPartition(unsigned numPoliciesArg, unsigned pageSizeArg, uint64 dramSizeArg, double rateGranArg, uint64 spaceGranArg, double constraintArg);
	void calculate(uint64 cycles, vector<Counter *>& instrCounters);
	unsigned getNumPolicies() { return numPolicies; }
	uint64 getDramPages(int pid) { return dramPagesPerPid[pid]; }
	double getRate(int pid) { return ratePerPid[pid]; }
};


#endif /* PARTITION_H_ */
