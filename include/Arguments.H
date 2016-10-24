/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

/*
 * Description: This set of classes help define command line options and arguments and provide a mechanism
 * 				to parse and set these. The arguments can be defined globally or in the main function.
 * 				There are three types of arguments. The first type are those arguments that are defined
 * 				by an option (for example -o outfile). The second type are those that don't have an option, but
 * 				are defined by their position on the command line string. The order in which the constructors of
 * 				this type of arguments are called determines the token at the command line string from which the
 * 				argument gets its value. The third type are those that make up the remaining of the command line
 * 				string that was not used to initialize type two arguments. A call to moreArgs() returns a
 * 				vector of strings containing these arguments.
 * 				Bool arguments (defined Argument<bool>) do not expect a value after their command line string. The presence
 * 				or absense of the string determines the value of the argument (true or false, respectively).
 * 	   Example:
 * 				int main(int argc, char *argv[]){
 * 					//Create object to hold arguments, including a variable number of arguments at the end of the string
 *					ArgumentContainer args("programName", true, "infiles", "input files");
 * 	 	 	 	 	//Define a required argument of type string
 *					Argument<string> outputFilePrefix(&args, "o", "outfile", "output file prefix", true, "");
 *					//Define an optional argument of type int with default value 1
 *					Argument<int> numCores(&args, "p", "num_proc", "number of processors", false, 1);
 *					//Parse arguments and, on error, print usage and return
 *					if (args.init(argc, argv)){
 *						args.usage(cerr);
 *						return -1;
 *					}
 *					//Use second argument
 *					cout << "Num Cores: " << numCores.getValue() << endl;
 *					//Use remaining arguments
 *					vector<string> rest =  args.moreArgs();
 *				}
 */

#ifndef ARGUMENTS_HPP_
#define ARGUMENTS_HPP_

#include <map>
#include <vector>
#include <iostream>
#include <sstream>
#include <string>

using namespace std;

/*
 * Base class of argument class
 */
class ArgumentBase {
protected:

	/*
	 * Name of the argument (used to print the usage)
	 */
	string _name;

	/*
	 * Description of the argument (used to print the usage)
	 */
	string _desc;

	/*
	 * Is the argument required?
	 */
	bool _req;

	/*
	 * Can the argument be supplied through the configuration file?
	 */
	bool _file;

	/*
	 * Does the argument behave like a flag?
	 */
	bool _flag;

	/*
	 * Has the argument been set?
	 */
	bool _set;


public:
	/*
	 * Constructor
	 * param name: name of the argument
	 * param desc: description of the argument
	 * param req: is this argument required?
	 */
	ArgumentBase(const string& name, const string& desc, bool req, bool file, bool flag) : _name(name), _desc(desc), _req(req), _file(file), _flag(flag),_set(false){}

	/*
	 * Parse the given string and set the value of the argument with it
	 * param val: the value to parse and set the argument with
	 */
	virtual void parseValue(const string & val) = 0;

	/*
	 * Return a string representation of the default value of the argument
	 */
	virtual string getValueAsString() const = 0;

	/*
	 * Return a string representation of the default value of the argument
	 */
	virtual string getDefaultValueAsString() const = 0;

	/*
	 * Destructor (needed because of virtual functions)
	 */
	virtual ~ArgumentBase() {};

	/*
	 * Returns the name of the argument
	 */
	string getName() {return _name;}

	/*
	 * Returns the description of the argument
	 */
	string getDesc() {return _desc;}

	/*
	 * Return whether this argument is required
	 */
	bool isReq() {return _req;}

	/*
	 * Return whether this argument can be supplied through the configuration file?
	 */
	bool isFile() {return _file;}

	/*
	 * Return whether this argument behaves like a flag?
	 */
	bool isFlag() {return _flag;}

	/*
	 * Return whether this argument has been set
	 */
	bool isSet() {return _set;}

};


template<class T> class Argument;
template<class T> class OptionalArgument;
template<class T> class PositionalArgument;

/*
 * Container of arguments. An object of this class is passed to the constructor of each argument,
 * so that it can be initalized later.
 */
class ArgumentContainer {
private:
	/*
	 * Maps option names to a pointer to an object representing an argument
	 */
	map<string, ArgumentBase*> _options;

	/*
	 * Maps a number that represents the position of an argument in the command line string
	 * to a pointer to an object representing an argument
	 */
	map<int, ArgumentBase*> _args;

	/*
	 * A vector of strings containing the remaining arguments
	 */
	vector<string> _moreArgs;

	/*
	 * The position to be assigned to the next argument (type two) that is registered with this container
	 */
	int curPos;

	/*
	 * The name of the program
	 */
	string _progName;

	/*
	 * Can arguments be read from a config file?
	 */
	bool _configFile;

	/*
	 * Does this object contain a variables number of arguments?
	 */
	bool _varArgs;

	/*
	 * The name of the variable arguments (for printing usage)
	 */
	string _varArgsName;

	/*
	 * The description of the variable arguments (for printing usage)
	 */
	string _varArgsDesc;

	Argument<bool> *help;
	PositionalArgument<string> *conf;


public:

	/*
	 * Constructor
	 * param progName: name of the program
	 * param configFile: can arguments be read from a config file?
	 * param varArgs: does this object contain a variables number of arguments?
	 * param varArgsName: name of the variable arguments
	 * param varArgsDesc: description of the variable arguments
	 */
	ArgumentContainer(const string& progName, bool configFile = false, bool varArgs = false, const string& varArgsName = "", const string& varArgsDesc = "");

	/*
	 * Parse the given file and set the value of the arguments
	 * Return true if the file is invalid
	 */
	bool parseFile(const string& filename);

	/*
	 * Parse the command line string and initialize all registered arguments
	 * Returns true if the command line or the configuration file is invalid or if help was called
	 */
	bool parse(int argc, char *argv[]);

	/*
	 * Register an argument (type one) with this container
	 * param name: the option or switch in the command line associated with this argument
	 * param arg: the argument to register
	 */
	void insertOptionalArgument(const string & name, ArgumentBase * const arg);

	/*
	 * Register an argument (type two) with this container
	 * param arg: the argument to register
	 */
	void insertPositionalArgument(ArgumentBase * const arg);

	/*
	 * Return a reference to a vector containing all remaining arguments
	 */
	inline vector<string>& moreArgs(){return _moreArgs;}

	/*
	 * Print usage
	 */
	void usage(ostream& out) const;

	/*
	 * Print value of all arguments
	 */
	void print(ostream& out) const;

};

/*
 * Template class to hold arguments of different types. To parse a value from the command line
 * and print its default value, the type of the argument must implement the << and >> operators.
 */
template<class T> class Argument : public ArgumentBase {
private:
	/*
	 * Current value of the argument
	 */
	T _value;

	/*
	 * Default value of the argument
	 */
	T _defaultValue;

protected:

	Argument(const string& name, const string& desc, const T& defaultValue, bool req, bool file, bool flag) : ArgumentBase(name, desc, req, file, flag), _value(defaultValue), _defaultValue(defaultValue) {}

public:
	/*
	 * Returns the current value of this argument
	 */
	T getValue() const {return _value;}

	T getDefaultValue() const {return _defaultValue;}

	void setValue(T value) {_value = value;}

	string getValueAsString() const {
		ostringstream oss;
		oss << _value;
		return oss.str();
	}

	string getDefaultValueAsString() const {
		ostringstream oss;
		oss << _defaultValue;
		return oss.str();
	}

	/*
	 * Parse the string and set the value of the argument
	 * param val: the string to be parsed
	 */
	void parseValue(const string & val){
		istringstream iss(val);
		iss >> _value;
		_set = true;
	}


};

template<class T> class OptionalArgument : public  Argument<T> {
public:
	/*
	 * Constructor of optional arguments
	 * param cont: the container with which to register this argument
	 * param name: the name of this argument
	 * param desc: the description of this argument
	 * param defaultValue: the default value of this argument
	 * param file whether this argument can be read from the config file
	 */
	OptionalArgument(ArgumentContainer *cont, const string& name, const string& desc, const T& defaultValue, bool file = true, bool flag = false) : Argument<T>(name, desc, defaultValue, false, file, flag) {
		cont->insertOptionalArgument(name, this);
	}
};

template<class T> class PositionalArgument : public Argument<T> {
public:
	/*
	 * Constructor of type two arguments
	 * param cont: the container with which to register this argument
	 * param name: the name of this argument
	 * param desc: the description of this argument
	 * param defaultValue: the default value of this argument
	 * param req: whether this is a required argument
	 */
	PositionalArgument(ArgumentContainer *cont, const string& name, const string& desc, const T& defaultValue, bool req = true) : Argument<T>(name, desc, defaultValue, req, false, false) {
		cont->insertPositionalArgument(this);
	}
};

#endif /* ARGUMENTS_HPP_ */
