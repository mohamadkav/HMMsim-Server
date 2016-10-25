/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#include "Engine.H"

#include <cassert>

Engine::Engine(StatContainer *statsArg, uint64 statsPeriodArg, const string& statsFilename, uint64 progressPeriodArg) :
		stats(statsArg),
		statsPeriod(statsPeriodArg),
		progressPeriod(progressPeriodArg),
		currentInterval(0),
		statsNextEvent(statsPeriodArg),
		progressNextEvent(progressPeriodArg),
		done(false),
		timestamp(0),
		lastTimestamp(0),
		numEvents(0),
		lastNumEvents(0),
		finalTimestamp(stats, "final_timestamp", "Final timestamp", this, &Engine::getFinalTimestamp),
		totalEvents(stats, "total_events", "Total number of events", 0),
		executionTime(stats, "execution_time", "Execution time in seconds", 0),
		eventRate(stats, "event_rate", "Event rate in events per second", &totalEvents, &executionTime)

{

	if (statsFilename == ""){
		statsPeriod = statsNextEvent = 0;
	}

	if (statsNextEvent == 0){
		if (progressNextEvent == 0){

		} else {
			addEvent(progressNextEvent - timestamp, this);
		}
	} else {
		statsOut.open(statsFilename.c_str());
		if (!statsOut.is_open()){
			error("Could not open statistics file '%s'", statsFilename.c_str());
		}
		statsOut.setf(std::ios::fixed);
		statsOut.precision(2);
		if (progressNextEvent == 0){
			addEvent(statsNextEvent -timestamp, this);
		} else {
			if (statsNextEvent < progressNextEvent){
				addEvent(statsNextEvent -timestamp, this);
			} else {
				addEvent(progressNextEvent - timestamp, this);
			}
		}
	}

	gettimeofday(&start, NULL);
	last = start;
}

void Engine::run(){
	if (statsNextEvent != 0){
		stats->printNames(statsOut);
		statsOut << endl;
	}
	bool empty = currentEventsEmpty();
	while (!done && !(empty && events.empty()) ){
		if (currentEvents[timestamp % currentSize].empty()){
			bool found = false;
			for (unsigned i = 1; i < currentSize; i++){
				deque<Event>::iterator it = currentEvents[(timestamp + i) % currentSize].begin();
				while (!events.empty() && events.top().getTimestamp() == timestamp + i){
					it = currentEvents[(timestamp + i) % currentSize].emplace(it, events.top());
					++it;
					events.pop();
				}
				if (!currentEvents[(timestamp + i) % currentSize].empty()){
					timestamp = timestamp + i;
					found = true;
					break;
				}
			}
			if (!found){
				Event firstEvent = events.top();
				timestamp = firstEvent.getTimestamp();
				currentEvents[timestamp % currentSize].emplace_back(firstEvent);
				events.pop();
				while (!events.empty() && events.top().getTimestamp() == timestamp){
					currentEvents[timestamp % currentSize].emplace_back(events.top());
					events.pop();
				}
			}

		}
		Event event = currentEvents[timestamp % currentSize].front();
		currentEvents[timestamp % currentSize].pop_front();
		assert(timestamp == event.getTimestamp());
	//	cout<<timestamp<<endl;

 		numEvents++;
		event.execute();

		empty = currentEventsEmpty();

//		if (DEBUG){
//			if (timestamp >= 190000000){
//				done = true;
//			}
//		}
	}
	updateStats();
	if (statsNextEvent != 0){
		statsOut.close();
	}
}

void Engine::quit(){
	done = true;
}

void Engine::addEvent(uint64 delay, IEventHandler *handler, uint64 data){
//	if (timestamp+delay == 6338739) {
//		cout << "Hello from add" << endl;
//	}
//	delays[delay]++;
	if (delay < currentSize){
		currentEvents[(timestamp + delay) % currentSize].emplace_back(timestamp+delay, handler, data);
	} else {
		events.emplace(timestamp+delay, handler, data);
	}
}

bool Engine::currentEventsEmpty(){
	for (unsigned i = 0; i < currentSize; i++){
		if (!currentEvents[(timestamp + i) % currentSize].empty()){
			return false;
		}
	}
	return true;
}

void Engine::process(const Event * event){
	if (timestamp == statsNextEvent){
		updateStats();
		statsNextEvent += statsPeriod;
		stats->printInterval(statsOut);
		statsOut << endl;
		currentInterval++;
		stats->startInterval();
	}
	if (timestamp == progressNextEvent){
		progressNextEvent += progressPeriod;
		uint64 eventsInPeriod = numEvents - lastNumEvents;
		struct timeval current;
		gettimeofday(&current, NULL);
		long seconds  = current.tv_sec  - last.tv_sec;
		long useconds = current.tv_usec - last.tv_usec;
		long mtime = seconds * 1000000 + useconds;
		double eventsPerSecond = 1000000 * (static_cast<double>(eventsInPeriod) / static_cast<double>(mtime));
		cout << "Between timestamps " << lastTimestamp << " and " << timestamp << " executed " << eventsInPeriod << " events (" << eventsPerSecond << " events per second)" << endl;
		lastTimestamp = timestamp;
		lastNumEvents = numEvents;
		last = current;
	}
	if (!currentEventsEmpty() || !events.empty()){
		if (statsNextEvent == 0){
			if (progressNextEvent != 0){
				addEvent(progressNextEvent - timestamp, this);
			}
		} else {
			if (progressNextEvent == 0){
				addEvent(statsNextEvent -timestamp, this);
			} else {
				if (statsNextEvent < progressNextEvent){
					addEvent(statsNextEvent -timestamp, this);
				} else {
					addEvent(progressNextEvent - timestamp, this);
				}
			}
		}
	}
}

void Engine::updateStats(){
	gettimeofday(&end, NULL);
	totalEvents = numEvents;
	long seconds  = end.tv_sec  - start.tv_sec;
	long useconds = end.tv_usec - start.tv_usec;
	executionTime = static_cast<double>(seconds * 1000000 + useconds)/1000000;

}
