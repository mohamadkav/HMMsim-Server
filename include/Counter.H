/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#ifndef COUNTER_H_
#define COUNTER_H_

#include "Engine.H"
#include "Types.H"


class Counter;

class IInterruptHandler {
public:
	virtual void processInterrupt(Counter* counter) = 0;
	virtual ~IInterruptHandler() {}
};


class Counter {
	uint64 value;
	uint64 totalValue;
	IInterruptHandler *handler;
	uint64 interruptValue;

public:
	Counter();
	void setInterrupt(uint64 interruptValueArg, IInterruptHandler* handlerArg);
	void add(uint64 amount);
	void reset();
	uint64 getValue(){return value;}
	uint64 getTotalValue(){return totalValue + value;}
	void operator++() {add(1);}
	void operator++(int) {add(1);}
	void operator+=(uint64 rhs) {add(rhs);}
};



class CycleCounter {
	Engine* engine;
	uint64 lastCycleCount;
public:
	CycleCounter(Engine *engineArg);
	CycleCounter(const CycleCounter& counter);
	void reset();
	uint64 getValue();

};


class CounterTraceReader{
	map<uint64, map<string, uint64> > mapa;
	ifstream file;
public:
	CounterTraceReader(string fileName);
	uint64 getValue(uint64 instr, string key);
	uint64 getValue(uint64 instrStart, uint64 instrEnd, string key);
	void getKeyList(vector<uint64>* list);

	void print(ostream& os);
};


#endif /* COUNTER_H_ */
