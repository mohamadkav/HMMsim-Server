/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#include "Bus.H"


Bus::Bus(const string& nameArg,
		const string& descArg,
		Engine *engineArg,
		StatContainer *statCont,
		uint64 debugStartArg,
		uint64 latencyArg) :
		name(nameArg),
				desc(descArg),
				engine(engineArg),
				debugStart(debugStartArg),

				latency(latencyArg) {
	//debugStart = 121000000;
	//debugStart = 0;

}

uint64 Bus::schedule(uint64 delay, IBusCallback *caller){
	uint64 timestamp = engine->getTimestamp();
	debug("(%lu, %s)", delay, caller->getName());
	uint64 start = delay + timestamp;
	Queue::iterator it = queue.lower_bound(start-latency);
	uint64 actualDelay;
	if (it == queue.end()){
		queue.emplace(start, caller);
		actualDelay = start - timestamp;
	} else {
		Queue::iterator itNext = it;
		++itNext;
		while (itNext != queue.end() && (start < it->first + latency || itNext->first < it->first + 2*latency)){
			++it;
			++itNext;
		}
		bool ins = queue.emplace(it->first + latency, caller).second;
		myassert(ins);
		actualDelay = it->first + latency - timestamp;
	}
	debug(": \tscheduled bus at : %lu (callback at %lu)", actualDelay+timestamp, actualDelay+latency+timestamp);
	engine->addEvent(actualDelay+latency, this);
	return actualDelay;
}


void Bus::process(const Event *event){
	uint64 timestamp = engine->getTimestamp();
	debug("()");
	Queue::iterator it = queue.begin();
	myassert(it != queue.end());
	myassert(it->first == timestamp - latency);
	IBusCallback *caller = it->second;
	queue.erase(it);
	caller->transferCompleted();
}


