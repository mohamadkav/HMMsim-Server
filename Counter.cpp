/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#include "Counter.H"
#include "Error.H"

#include <iterator>

Counter::Counter() : value(0), totalValue(0), handler(0), interruptValue(0) {

}



void Counter::add(uint64 amount){
	value += amount;
	if (handler != 0 && value >= interruptValue){
		handler->processInterrupt(this);
	}
}

void Counter::reset(){
	totalValue += value;
	value = 0;
}

void Counter::setInterrupt(uint64 interruptValueArg, IInterruptHandler* handlerArg){
	handler = handlerArg;
	interruptValue = interruptValueArg;
}


CycleCounter::CycleCounter(Engine *engineArg) : engine(engineArg), lastCycleCount(0){

}

CycleCounter::CycleCounter(const CycleCounter& counter) : engine(counter.engine), lastCycleCount(counter.lastCycleCount){

}

void CycleCounter::reset(){
	lastCycleCount = engine->getTimestamp();
}

uint64 CycleCounter::getValue(){
	return engine->getTimestamp() - lastCycleCount;
}

CounterTraceReader::CounterTraceReader(string fileName) {
	ifstream file(fileName.c_str());
	if(file.fail()){
		error("Could not open counter trace file %s", fileName.c_str());
	}
	string line;
	while(getline(file, line)){
		istringstream iss(line);
		string line2;
		map<uint64, map<string, uint64> >::iterator it;
		while(getline(iss, line2, ',')) {
			istringstream iss2(line2);
			string key;
			uint64 value;
			iss2 >> key >> value;
			if (key == "instructions"){
				it = mapa.emplace(value, map<string, uint64>()).first;
			} else {
				it->second.emplace(key, value);
			}
		}
	}
}

uint64 CounterTraceReader::getValue(uint64 instr, string key){
	map<uint64, map<string, uint64> >::iterator it = mapa.find(instr);
	if (it == mapa.end()){
		return 0;
	} else {
		map<string, uint64>::iterator it2 = it->second.find(key);
		if (it2 == it->second.end()){
			return 0;
		} else {
			return it2->second;
		}
	}
}

uint64 CounterTraceReader::getValue(uint64 instrStart, uint64 instrEnd, string key){
	if (instrEnd < instrStart){
		return 0;
	}
	uint64 value = 0;
	map<uint64, map<string, uint64> >::iterator itStart = mapa.lower_bound(instrStart);
	map<uint64, map<string, uint64> >::iterator itEnd = mapa.lower_bound(instrEnd);
	while(itStart != itEnd){
		map<string, uint64>::iterator it2 = itStart->second.find(key);
		if (it2 != itStart->second.end()){
			value += it2->second;
		}
		++itStart;
	}
	return value;
}


void CounterTraceReader::print(ostream& os){
	string orderStr("cycles dram_reads dram_writes pcm_reads pcm_writes dram_read_time dram_write_time pcm_read_time pcm_write_time dram_migrations pcm_migrations dram_migration_time pcm_migration_time");
	stringstream strstr(orderStr);
	istream_iterator<string> itStr(strstr);
	istream_iterator<string> end;
	vector<string> order(itStr, end);

	for(map<uint64, map<string, uint64> >::iterator it = mapa.begin(); it != mapa.end(); ++it){
		os << "instructions " << it->first << ", ";
		for (unsigned i = 0; i < order.size(); i++){
			map<string, uint64>::iterator it2 = it->second.find(order[i]);
			if (it2 != it->second.end()){
				os << it2->first << " " << it2->second;
				if (i == order.size() - 1){
					os << endl;
				} else {
					os << ", ";
				}
			}
		}
	}
}

void CounterTraceReader::getKeyList(vector<uint64>* list){
	list->clear();
	for(map<uint64, map<string, uint64> >::iterator it = mapa.begin(); it != mapa.end(); ++it){
		list->emplace_back(it->first);
	}
}
