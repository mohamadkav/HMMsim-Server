/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#include "Arguments.H"
#include "Counter.H"


int main(int argc, char * argv[]){

	ArgumentContainer args("parse", false);
	PositionalArgument<string> filePrefix(&args, "file_prefix", "file prefix", "");
	PositionalArgument<string> fileSuffix(&args, "file_suffix", "file suffix", "");

	if (args.parse(argc, argv)){
		args.usage(cerr);
		return -1;
	}

	unsigned rateSpace[] = {10, 20, 30, 40, 50, 60, 70, 80, 90};
	unsigned rateSpaceReversed[] = {90, 80, 70, 60, 50, 40, 30, 20, 10};
	unsigned rateSpaceSize = 9;

	CounterTraceReader baseline(filePrefix.getValue() + fileSuffix.getValue());
	map<unsigned, map<unsigned, CounterTraceReader*> > readers;

	for(unsigned i = 0; i < rateSpaceSize; i++){
		for(unsigned j = 0; j < rateSpaceSize; j++){
			ostringstream fileName;
			//multi_2_hybrid_ft_pause_space_10_90_rate_10_90
			fileName << filePrefix.getValue() << "_space_" << rateSpace[i] << "_" << rateSpaceReversed[i] << "_rate_" << rateSpace[j] << "_" << rateSpaceReversed[j] << fileSuffix.getValue();
			readers[i][j] = new CounterTraceReader(fileName.str());
		}
	}



	vector<uint64> keyList;
	baseline.getKeyList(&keyList);

	uint64 sum = 0;
	uint64 sumBaseline = 0;
	map<unsigned, map<unsigned, uint64> >sums;
	for(unsigned k = 0; k < keyList.size(); k++){
		uint64 min = numeric_limits<uint64>::max();
		unsigned minRate = numeric_limits<unsigned>::max(), minSpace = numeric_limits<unsigned>::max();
		for(unsigned i = 0; i < rateSpaceSize; i++){
			for(unsigned j = 0; j < rateSpaceSize; j++){
				uint64 v = readers[i][j]->getValue(keyList[k], "cycles");
				if (v < min){
					min = v;
					minSpace = i;
					minRate = j;
				}
				sums[i][j] += readers[i][j]->getValue(keyList[k], "cycles");
			}
		}
		cout << "rate: " << rateSpace[minRate] << " space: " << rateSpace[minSpace] << endl;
		sum += min;
		sumBaseline += baseline.getValue(keyList[k], "cycles");
	}

	uint64 min = numeric_limits<uint64>::max();
	for(unsigned i = 0; i < rateSpaceSize; i++){
		for(unsigned j = 0; j < rateSpaceSize; j++){
			uint64 v = sums[i][j];
			if (v < min){
				min = v;
			}
		}
	}

	cout << sumBaseline << "\t" << min << "\t" << sum << endl;

	return 0;
}
