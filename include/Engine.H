/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#ifndef ENGINE_H_
#define ENGINE_H_

#include "Statistics.H"
#include "Types.H"

#include <sys/time.h>

#include <queue>
#include <map>
#include <unordered_map>
#include <fstream>

using namespace std;

class Event;

class IEventHandler{
public:
	virtual void process(const Event * event) = 0;
	virtual ~IEventHandler() {}
};


class Event{
private:
	uint64 timestamp;
	IEventHandler *handler;
	uintptr_t data;

public:
	Event(uint64 timestampArg, IEventHandler *handlerArg, uint64 dataArg) : timestamp(timestampArg), handler(handlerArg), data(dataArg) {}
	~Event(){}
	bool operator>(const Event& rhs) const {return this->timestamp > rhs.timestamp;}
	void execute() const {handler->process(this);}
	uint64 getTimestamp() const {return timestamp;}
	uint64 getData() const {return data;}
};


class Engine : public IEventHandler{
private:
	priority_queue<Event, vector<Event>, greater<Event> > events;
	static const uint64 currentSize = 4;
	deque<Event> currentEvents[currentSize];

	StatContainer *stats;
	uint64 statsPeriod;
	uint64 progressPeriod;

	uint64 currentInterval;

	uint64 statsNextEvent;
	uint64 progressNextEvent;

	ofstream statsOut;

	bool done;
	uint64 timestamp;
	uint64 lastTimestamp;
	uint64 numEvents;
	uint64 lastNumEvents;
	struct timeval start;
	struct timeval end;
	struct timeval last;

	//map<uint64, uint64> delays;


	//Statistics
	CalcStat<uint64, Engine> finalTimestamp;
	uint64 getFinalTimestamp() {return timestamp;}

	Stat<uint64> totalEvents;
	Stat<double> executionTime;
	BinaryStat<double, divides<double>, uint64, double> eventRate;


public:
	Engine(StatContainer *statsArg, uint64 statsPeriodArg, const string& statsFilename, uint64 progressPeriodArg);
	void run();
	void quit();
	void addEvent(uint64 delay, IEventHandler *handler, addrint addr = 0);

	uint64 getTimestamp() const {return timestamp;}

	bool currentEventsEmpty();

	void process(const Event * event);

private:
	void updateStats();

};

//template <typename DataType> class EventMap {
//	Engine *engine;
//	IEventHandler *handler;
//	//typedef std::tr1::unordered_map<uint64, list<DataType> > DataMap;
//	typedef multimap<uint64, DataType> DataMap;
////	typedef map<uint64, list<DataType> > DataMap;
//	DataMap data;
//public:
//	EventMap(Engine *engineArg, IEventHandler *handlerArg) : engine(engineArg), handler(handlerArg) {}
//
//	void addEvent(uint64 delay, const DataType& dataArg){
//		engine->addEvent(delay, handler);
//		data.emplace(delay + engine->getTimestamp(), dataArg);
//		//data[delay + engine->getTimestamp()].emplace_back(dataArg);
//	}
//
//	DataType getEventData(){
//		typename DataMap::iterator it = data.find(engine->getTimestamp());
//		DataType ret = it->second;
//		data.erase(it);
//		return ret;
////		DataType ret = it->second.front();
////		it->second.pop_front();
////		if (it->second.empty()){
////			data.erase(it);
////		}
////		return ret;
//	}
//
//	uint64 getTimestamp() const {return engine->getTimestamp();}
//};

#endif /* ENGINE_H_ */
