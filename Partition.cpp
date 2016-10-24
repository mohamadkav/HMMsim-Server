/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#include "Partition.H"

#include <cassert>
#include <cmath>

StaticPartition::StaticPartition(unsigned numPoliciesArg, unsigned pageSizeArg, uint64 dramSizeArg, string dramFractionsArg, string rateFractionsArg) : numPolicies(numPoliciesArg){
	unsigned logPageSize = (unsigned)logb(pageSizeArg);
	unsigned pageSize = 1 << logPageSize;
	dramPages = dramSizeArg / pageSize;

	size_t current;
	size_t next = -1;
	do {
		current = next + 1;
		next = dramFractionsArg.find_first_of("_", current);
		double f = atof(dramFractionsArg.substr(current, next - current).c_str());
		dramPagesPerPid.emplace_back(static_cast<uint64>(floor(f*dramPages + 0.5)));
	} while (next != string::npos);

	if (dramPagesPerPid.size() != numPolicies){
		error("dramFraction string has %u fractions but must have %lu", numPolicies, dramPagesPerPid.size());
	}

	next = -1;
	do {
		current = next + 1;
		next = rateFractionsArg.find_first_of("_", current);
		double f = atof(rateFractionsArg.substr(current, next - current).c_str());
		ratePerPid.emplace_back(f);
	} while (next != string::npos);

	if (ratePerPid.size() != numPolicies){
		error("rateFraction string has %u fractions but must have %lu", numPolicies, ratePerPid.size());
	}

}

void StaticPartition::calculate(uint64 cycles, vector<Counter *>& instrCounters) {
//	for (unsigned i = 0; i < metrics.size(); i++){
//		cout << "IPC[" << i << "]: " << metrics[i] << " ";
//	}
//	cout << endl;
}


OfflinePartition::OfflinePartition(unsigned numPoliciesArg, unsigned pageSizeArg, uint64 dramSizeArg, const string& prefixArg, const string& infixArg, const string& suffixArg, const string& periodTypeArg) : numPolicies(numPoliciesArg), prefix(prefixArg), infix(infixArg), suffix(suffixArg), periodType(periodTypeArg){
	unsigned logPageSize = (unsigned)logb(pageSizeArg);
	unsigned pageSize = 1 << logPageSize;
	dramPages = dramSizeArg / pageSize;

	rateSpaceSize = 9;

	dramPagesPerPid.emplace_back(static_cast<uint64>(floor(0.9*dramPages + 0.5)));
	dramPagesPerPid.emplace_back(static_cast<uint64>(floor(0.1*dramPages + 0.5)));

	for (unsigned i = 0; i < numPolicies; i++){
		//dramPagesPerPid.emplace_back(dramPages/numPolicies);
		ratePerPid.emplace_back(1.0/numPolicies);
	}

}

OfflinePartition::~OfflinePartition(){
//	for(unsigned i = 0; i < rateSpaceSize; i++){
//		for(unsigned j = 0; j < rateSpaceSize; j++){
//			delete readers[i][j];
//		}
//	}
}

void OfflinePartition::addCounterTrace(const string& name){
	unsigned rateSpace[] = {10, 20, 30, 40, 50, 60, 70, 80, 90};
	unsigned rateSpaceReversed[] = {90, 80, 70, 60, 50, 40, 30, 20, 10};


	for(unsigned i = 0; i < rateSpaceSize; i++){
		for(unsigned j = 0; j < rateSpaceSize; j++){
			ostringstream fileName;
			///afs/cs.pitt.edu/usr0/sab104/pcm/migration/results/04_23_13/multi_2_hybrid_ft_pause_space_90_10_rate_30_70/multi_2_401.bzip2-1_0.trace
			fileName << prefix << "_space_" << rateSpace[i] << "_" << rateSpaceReversed[i] << "_rate_" << rateSpace[j] << "_" << rateSpaceReversed[j] << infix << name << suffix;
			readers[i][j] = new CounterTraceReader(fileName.str());
		}
	}
}

void OfflinePartition::calculate(uint64 cycles, vector<Counter *>& instrCounters) {
	//cout << "Enter OfflinePartition::calculate" << endl;

	unsigned rateSpace[] = {10, 20, 30, 40, 50, 60, 70, 80, 90};
	unsigned rateSpaceReversed[] = {90, 80, 70, 60, 50, 40, 30, 20, 10};

	if (periodType == "cycles"){


		if (cycles == 0){
			return;
		}

		if (instrCounters[0]->getValue() == 0){
			ratePerPid.clear();
			double rate = static_cast<double>(rateSpace[0])/100.0;
			double rateReversed = static_cast<double>(rateSpaceReversed[0])/100.0;
			ratePerPid.emplace_back(rate);
			ratePerPid.emplace_back(rateReversed);
			return;
		}



		uint64 instrPeriodStart = instrCounters[0]->getTotalValue();
		uint64 instrPeriodEnd = instrPeriodStart + instrCounters[0]->getValue() - 1;

		uint64 min = numeric_limits<uint64>::max();
		//unsigned maxi = 0;
		unsigned minj = 0;

		//i: space
		//j: rate
		for(unsigned i = 0; i < rateSpaceSize; i++){
			for(unsigned j = 0; j < rateSpaceSize; j++){
				uint64 value = readers[8][j]->getValue(instrPeriodStart, instrPeriodEnd, "cycles");
				if (min < value){
					min = value;
					//maxi = i;
					minj = j;
				}
			}
		}

	//	dramPagesPerPid.clear();
	//	double space = static_cast<double>(rateSpace[mini])/100.0;
	//	double spaceReversed = static_cast<double>(rateSpaceReversed[mini])/100.0;
	//	dramPagesPerPid.emplace_back(static_cast<uint64>(floor(space*dramPages + 0.5)));
	//	dramPagesPerPid.emplace_back(static_cast<uint64>(floor(spaceReversed*dramPages + 0.5)));

	//	cout << minj << endl;

		ratePerPid.clear();
		double rate = static_cast<double>(rateSpace[minj])/100.0;
		double rateReversed = static_cast<double>(rateSpaceReversed[minj])/100.0;
		ratePerPid.emplace_back(rate);
		ratePerPid.emplace_back(rateReversed);
	} else if (periodType == "instructions"){
		uint64 instrPeriodStart = instrCounters[0]->getTotalValue();

		uint64 min = numeric_limits<uint64>::max();
		//unsigned maxi = 0;
		unsigned minj = 0;

		//i: space
		//j: rate
		for(unsigned i = 0; i < rateSpaceSize; i++){
			for(unsigned j = 0; j < rateSpaceSize; j++){
				uint64 value = readers[8][j]->getValue(instrPeriodStart, "cycles");
				if (min < value){
					min = value;
					//maxi = i;
					minj = j;
				}
			}
		}

	//	dramPagesPerPid.clear();
	//	double space = static_cast<double>(rateSpace[mini])/100.0;
	//	double spaceReversed = static_cast<double>(rateSpaceReversed[mini])/100.0;
	//	dramPagesPerPid.emplace_back(static_cast<uint64>(floor(space*dramPages + 0.5)));
	//	dramPagesPerPid.emplace_back(static_cast<uint64>(floor(spaceReversed*dramPages + 0.5)));

	//	cout << minj << endl;

		ratePerPid.clear();
		double rate = static_cast<double>(rateSpace[minj])/100.0;
		double rateReversed = static_cast<double>(rateSpaceReversed[minj])/100.0;
		ratePerPid.emplace_back(rate);
		ratePerPid.emplace_back(rateReversed);
	} else {
		assert("Invalid value of periodType");
	}

	//cout << "Leave OfflinePartition::calculate" << endl;
}






DynamicPartition::DynamicPartition(unsigned numPoliciesArg, unsigned pageSizeArg, uint64 dramSizeArg, double rateGranArg, uint64 spaceGranArg, double constraintArg) : numPolicies(numPoliciesArg), rateGran(rateGranArg), spaceGran(spaceGranArg), constraint(constraintArg) {
	unsigned logPageSize = (unsigned)logb(pageSizeArg);
	unsigned pageSize = 1 << logPageSize;
	dramPages = dramSizeArg / pageSize;

	if (numPolicies != 2){
		error("Dynamic partitioning not implemented for more than 2 policies");
	}

	uint64 initialDramPagesPerPolicy = dramPages/numPolicies;
	double initialRatePerPolicy = 1.0/numPolicies;

	for (unsigned i = 0; i < numPolicies; i++){
		dramPagesPerPid.emplace_back(initialDramPagesPerPolicy);
		ratePerPid.emplace_back(initialRatePerPolicy);
	}

	rateProb = MAX_PROB/2;
	moreRateProb = MAX_PROB/2;
	moreSpaceProb = MAX_PROB/2;

}

void DynamicPartition::calculate(uint64 cycles, vector<Counter *>& instrCounters) {
//	for (unsigned i = 0; i < metrics.size(); i++){
//		cout << "IPC[" << i << "]: " << metrics[i] << " ";
//	}

//	This code below is not really working
//	bool backoff = false;
//	bool search = false;
//
//	if (previousMetrics.size() != 0){
//
//		double metric, prevMetric;
//		if (metrics[1] < constraint){
//			cout << " constraint: bad  ";
//			metric = metrics[1];
//			prevMetric = previousMetrics[1];
//			//moreRateProb = MAX_PROB/2;
//		} else {
//			cout << " constraint: good ";
//			metric = metrics[0];
//			prevMetric = previousMetrics[0];
//		}
//
//
//		if (metric - prevMetric > 0.0){
//			search = true;
//		} else {
//			//search = true;
//			backoff = true;
//		}
//
//	} else {
//		search = true;
//	}
//
//	if (backoff){
//		cout << "backoff: yes ";
//		dramPagesPerPid = prevDramPagesPerPid;
//		ratePerPid = prevRatePerPid;
//
//		if (rate){
//			if (moreRate){
//				moreRateProb -= 1000;
//				if (moreRateProb < 1000){
//					moreRateProb = 1000;
//				}
//			} else {
//				moreRateProb += 1000;
//				if(moreRateProb > 9000){
//					moreRateProb = 9000;
//				}
//			}
//		} else {
//			if (moreSpace){
//				moreSpaceProb -= 1000;
//				if (moreSpaceProb < 1000){
//					moreSpaceProb = 1000;
//				}
//			} else {
//				moreSpaceProb += 1000;
//				if(moreSpaceProb > 9000){
//					moreSpaceProb = 9000;
//				}
//			}
//		}
//
//		previousMetrics.resize(0);
//
//	} else {
//		cout << "backoff: no ";
//	}
//
//	cout << "moreRateProb: " << static_cast<double>(moreRateProb)/MAX_PROB << " ";
//
//	if (search){
//		prevDramPagesPerPid = dramPagesPerPid;
//		prevRatePerPid = ratePerPid;
//
//		rate = true; //For now, always use rate
//
//
//		int random = rand() % MAX_PROB;
//		if (random < moreRateProb){
//			cout << " more ";
//			moreRate = true;
//			ratePerPid[0] += rateGran;
//			ratePerPid[1] -= rateGran;
//		} else {
//			cout << " less ";
//			moreRate = false;
//			ratePerPid[0] -= rateGran;
//			ratePerPid[1] += rateGran;
//		}
//		if (ratePerPid[0] > 1.0 - rateGran || ratePerPid[1] < 0.0 + rateGran){
//			ratePerPid[0] = 1.0 - rateGran;
//			ratePerPid[1] = 0.0 + rateGran;
//		} else if (ratePerPid[1] > 1.0 - rateGran || ratePerPid[0] < 0.0 + rateGran){
//			ratePerPid[1] = 1.0 - rateGran;
//			ratePerPid[0] = 0.0 + rateGran;
//		}
//
//
//
//		previousMetrics = metrics;
//
//	}
//
//	for (unsigned i = 0; i < metrics.size(); i++){
//		cout << "ratePerPid[" << i << "]: " << ratePerPid[i] << " ";
//	}
//
//	cout << endl;
}



