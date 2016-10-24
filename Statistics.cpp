/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#include "Statistics.H"
#include "Error.H"

#include <cassert>

void StatContainer::insert(StatBase *stat){
	//cout << "insert: " << stat->getName() << endl;
	if (stat->getName().find_first_of(" \t\n\r") != string::npos){
		error("Statistic %s contains a whitespace in its name", stat->getName().c_str());
	}
	for (list<StatBase*>::iterator it = stats.begin(); it != stats.end(); ++it){
		if ((*it)->getName() == stat->getName()){
			error("Statistic %s has already been defined", stat->getName().c_str());
		}
	}
	stats.emplace_back(stat);
}

/*
 * Inserts stat after parent and returns an iterator to the newly inserted element
 */
StatListIter StatContainer::insertAfter(StatBase *stat, StatListIter parent){
	//cout << "insertAfter: " << stat->getName() << ", " << (*parent)->getName() << endl;
	if (stat->getName().find_first_of(" \t\n\r") != string::npos){
		error("Statistic %s contains a whitespace in its name", stat->getName().c_str());
	}
	for (list<StatBase*>::iterator it = stats.begin(); it != stats.end(); ++it){
		//cout << "insertAfter: " << (*it)->getName() << endl;
		if ((*it)->getName() == stat->getName()){
			error("Statistic %s has already been defined", stat->getName().c_str());
		}
	}

	assert(parent != stats.end());

	++parent;
	return stats.insert(parent, stat);
}

StatListIter StatContainer::erase(StatListIter iter){
	return stats.erase(iter);
}

void StatContainer::reset(){
	for (list<StatBase*>::iterator it = stats.begin(); it != stats.end(); ++it){
		(*it)->reset();
	}
}

void StatContainer::startInterval(){
	for (list<StatBase*>::iterator it = stats.begin(); it != stats.end(); ++it){
		(*it)->startInterval();
	}
}

void StatContainer::genListStats(){
	for (list<StatBase*>::iterator it = stats.begin(); it != stats.end(); ++it){
		it = (*it)->generate(it);
	}
}

void StatContainer::print(ostream& os) {
	for (list<StatBase*>::const_iterator it = stats.begin(); it != stats.end(); ++it){
		os << "#" << (*it)->getDesc() << endl;
		os << (*it)->getName() << " " << (*it)->getValueAsString() << endl << endl;
	}
}

void StatContainer::printNames(ostream& os) {
	for (list<StatBase*>::const_iterator it = stats.begin(); it != stats.end(); ++it){
		os << (*it)->getName() << '\t' << flush;
	}
}

void StatContainer::printInterval(ostream& os) {
	for (list<StatBase*>::const_iterator it = stats.begin(); it != stats.end(); ++it){
		//os << (*it)->getName() << " " << (*it)->getIntervalValueAsString() << endl;
		(*it)->printIntervalValue(os);
		os << '\t' << flush;
	}
}

