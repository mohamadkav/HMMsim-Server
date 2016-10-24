/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#include "Arguments.H"

#include <fstream>


ArgumentContainer::ArgumentContainer(const string& progName, bool configFile, bool varArgs, const string& varArgsName, const string& varArgsDesc) : curPos(0), _progName(progName), _configFile(configFile), _varArgs(varArgs), _varArgsName(varArgsName), _varArgsDesc(varArgsDesc) {
	help = new OptionalArgument<bool>(this, "h", "show this help message and exit", false, false, true);
	if (_configFile){
		conf = new PositionalArgument<string>(this, "CONFIG_FILE", "load arguments from this configuration file", "");
	}
}

void ArgumentContainer::insertOptionalArgument(const string & name, ArgumentBase * const arg){
	if (!_options.emplace(name, arg).second){
		cerr << "Argument  '-" << name << "' already exists." << endl;
	}
}

void ArgumentContainer::insertPositionalArgument(ArgumentBase * const arg){
	_args.emplace(curPos, arg);
	curPos++;
}

bool ArgumentContainer::parseFile(const string& filename){
	ifstream ifs(filename.c_str());
	if (ifs.good()){
		while (ifs.good()){
			string line;
			getline(ifs, line);
			line.erase(0, line.find_first_not_of(" \t"));
			if (line.empty() || line[0] == '#'){
				continue;
			}
			if (line[0] == '-'){
				size_t end = line.find_first_of(" \t");
				string opt = line.substr(1, end-1);
				string value = line.substr(line.find_first_not_of(" \t", end));
				map<string, ArgumentBase*>::const_iterator it = _options.find(opt);
				if (it != _options.end()){
					if (it->second->isFile()){
						if (!it->second->isSet()){
							it->second->parseValue(value);
						}
					} else {
						cerr << "Option " << it->second->getName() << "cannot be set in configuration file" << endl;
						return true;
					}
				} else {
					cerr << "Invalid option in configuration file" << endl;
					return true;
				}

			} else {
				cerr << "Wrong format in configuration file" << endl;
				return true;
			}
		}
		ifs.close();
		return false;
	} else {
		cerr << "Could not read config file \"" << filename << "\"" << endl;
		return true;
	}
}

bool ArgumentContainer::parse(int argc, char *argv[]){
	int pos = 0;
	int i = 1;
	while (i < argc){
		string token(argv[i]);
		if (token[0] == '-'){
			token.erase(0, 1);
			map<string, ArgumentBase*>::const_iterator it = _options.find(token);
			if (it != _options.end()){
				if (it->second->isFlag()){
					it->second->parseValue("1");
				} else {
					i++;
					if(i >= argc){
						return true;
					}
					it->second->parseValue(argv[i]);
				}
			} else {
				return true;
			}
		} else {
			map<int, ArgumentBase*>::const_iterator it = _args.find(pos);
			if (it != _args.end()){
				it->second->parseValue(token);
			} else {
				if (_varArgs){
					_moreArgs.emplace_back(token);
				} else {
					return true;
				}
			}
			pos++;
		}
		i++;
	}
//	for (map<string, ArgumentBase*>::const_iterator it = _options.begin(); it != _options.end(); it++){
//		if (it->second->isReq() && !it->second->isSet()){
//			return true;
//		}
//	}
	for (map<int, ArgumentBase*>::const_iterator it = _args.begin(); it != _args.end(); it++){
		if (it->second->isReq() && !it->second->isSet()){
			return true;
		}
	}

	if (help->getValue()){
		return true;
	}

	if (_configFile){
		string filename = conf->getValue();
		if (parseFile(conf->getValue())) {
			return true;
		}
	}
	return false;
}

void ArgumentContainer::usage(ostream& out) const{
	out << "Usage: " << _progName << " [OPTIONS]";
	for (map<int, ArgumentBase*>::const_iterator it = _args.begin(); it != _args.end(); it++){
		if (it->second->isReq()) {
			out << " " << it->second->getName();
		} else {
			out << " [" << it->second->getName() << "]";
		}
	}
	if (_varArgs){
		out << " [" << _varArgsName << "...]";
	}
	out << endl << endl << "OPTIONS:" << endl;
	for (map<string, ArgumentBase*>::const_iterator it = _options.begin(); it != _options.end(); it++){
		out << "\t-" << it->first << endl;
		out << "\t\t" << it->second->getDesc();
		if (!it->second->isReq() && !it->second->isFlag()){
			out << "  (default " << it->second->getDefaultValueAsString() << ")" << endl << endl;
		} else {
			out << endl << endl;
		}
	}
	for (map<int, ArgumentBase*>::const_iterator it = _args.begin(); it != _args.end(); it++){
		out << it->second->getName() << endl;
		out << "\t" << it->second->getDesc();
		if (!it->second->isReq()){
			out << "  (default " << it->second->getDefaultValueAsString() << ")" << endl << endl;
		} else {
			out << endl << endl;
		}
	}
	if (_varArgs){
		out << _varArgsName << endl;
		out << "\t" << _varArgsDesc << endl;
	}
}

void ArgumentContainer::print(ostream& out) const{
	out << "#Optional arguments:" << endl;
	for (map<string, ArgumentBase*>::const_iterator it = _options.begin(); it != _options.end(); it++){
		out << "#" << it->second->getDesc() << endl;
		out << "-" << it->first << " " << it->second->getValueAsString() << endl << endl;
	}
	out << endl << "#Positional arguments:" << endl;
	for (map<int, ArgumentBase*>::const_iterator it = _args.begin(); it != _args.end(); it++){
		out << "#" << it->second->getDesc() << endl;
		out << it->second->getValueAsString() << endl << endl;
	}
	if (_varArgs){
		out << endl << "#Remaining arguments:" << endl;
		out << "#" << _varArgsDesc << endl;
		for (vector<string>::const_iterator it = _moreArgs.begin(); it != _moreArgs.end(); it++){
			out << *it << " ";
		}
		out << endl;
	}


}
