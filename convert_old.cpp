/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

/*
 * This program converts and compresses a trace read from a file (or pipe) into multiple files.
 */

#include "Arguments.H"
#include "TraceHandler.H"
#include "Error.H"


int main(int argc, char * argv[]){

	ArgumentContainer args("split", false);
	PositionalArgument<string> inputFile(&args, "input_file", "input file", "");
	PositionalArgument<string> outputPrefix(&args, "output_prefix", "output prefix", "");
	OptionalArgument<string> compression(&args, "c", "compression algorithm (gzip|bzip2)", "gzip");

	if (args.parse(argc, argv)){
		args.usage(cerr);
		return -1;
	}

	CompressionType comp;
	if (compression.getValue() == "gzip"){
		comp = GZIP;
	} else if (compression.getValue() == "bzip2"){
		comp = BZIP2;
	} else {
		args.usage(cerr);
		return -1;
	}

	gzFile reader;
	if((reader = gzopen(inputFile.getValue().c_str(), "r")) == 0){
		error("Could not open file '%s'", inputFile.getValue().c_str());
	}

	char buffer[1024];


	//skip first line
//	char * line = gzgets(reader, buffer, 1024);
	char * line;

	CompressedTraceWriter writer(outputPrefix.getValue(), comp);

	TraceEntry entry;
	bool done = false;

	while(!done){
		line = gzgets(reader, buffer, 1024);
		if (line == 0){
			done = true;
		} else {
			string lineStr(line);
			istringstream iss(lineStr);
			//uint64 vAddr;
			char rw;
			char dec;
			//iss >> hex >> vAddr >> entry.address >> dec >> rw >> entry.timestamp;
			iss >> entry.timestamp >> entry.address >> entry.size >> rw >> dec;
			//entry.size = 4;
			if (rw == 'R'){
				entry.read = true;
			} else {
				entry.read = false;
			}
			if(dec == 'D')
			{
				entry.instr = false;
			}else{
				entry.instr = true;
			}
			
				
			entry.size -= 48;
			cout << entry.timestamp << " " <<  entry.address << " " << entry.size << " " << entry.read << " " << entry.instr << endl;
			writer.writeEntry(&entry);

			//string lineStr(line);
			//vector<string> tokens;
			//size_t current;
			//size_t next = -1;
			//do {
		//		current = next + 1;
		//		next = lineStr.find_first_of("\t", current);
		//		tokens.emplace_back(lineStr.substr(current, next - current));
		//	} while (next != string::npos);
		//	entry.timestamp = atol(tokens[3].c_str());
		//	entry.address = tokens[1];
		//	entry.size = 4;
		//	if (tokens[2] == "R"){
		//		entry.read = true;
		//	} else {
		//		entry.read = false;
		//	}
		//	entry.instr = false;
		}
	}

	gzclose(reader);

	return 0;

}


//int main(int argc, char * argv[]){
//
//	ArgumentContainer args("split");
//	Argument<string> traceFilename(&args, "trace_file_name", "trace file name", false, "trace");
//
//	if (args.init(argc, argv)){
//		args.usage(cerr);
//		return -1;
//	}
//
//	TraceReader reader(traceFilename.getValue());
//
//	BZFILE *instrTimestamp;
//	BZFILE *instrAddress;
//	BZFILE *instrSize;
//	BZFILE *readTimestamp;
//	BZFILE *readAddress;
//	BZFILE *readSize;
//	BZFILE *writeTimestamp;
//	BZFILE *writeAddress;
//	BZFILE *writeSize;
//
//	string filename = traceFilename.getValue()+"-instr-time.bz2";
//	if((instrTimestamp = BZ2_bzopen(filename.c_str(), "w")) == 0){
//		error("Could not open file '%s'", filename.c_str());
//	}
//	filename = traceFilename.getValue()+"-instr-addr.bz2";
//	if((instrAddress = BZ2_bzopen(filename.c_str(), "w")) == 0){
//		error("Could not open file '%s'", filename.c_str());
//	}
//	filename = traceFilename.getValue()+"-instr-size.bz2";
//	if((instrSize = BZ2_bzopen(filename.c_str(), "w")) == 0){
//		error("Could not open file '%s'", filename.c_str());
//	}
//	filename = traceFilename.getValue()+"-read-time.bz2";
//	if((readTimestamp = BZ2_bzopen(filename.c_str(), "w")) == 0){
//		error("Could not open file '%s'", filename.c_str());
//	}
//	filename = traceFilename.getValue()+"-read-addr.bz2";
//	if((readAddress = BZ2_bzopen(filename.c_str(), "w")) == 0){
//		error("Could not open file '%s'", filename.c_str());
//	}
//	filename = traceFilename.getValue()+"-read-size.bz2";
//	if((readSize = BZ2_bzopen(filename.c_str(), "w")) == 0){
//		error("Could not open file '%s'", filename.c_str());
//	}
//	filename = traceFilename.getValue()+"-write-time.bz2";
//	if((writeTimestamp = BZ2_bzopen(filename.c_str(), "w")) == 0){
//		error("Could not open file '%s'", filename.c_str());
//	}
//	filename = traceFilename.getValue()+"-write-addr.bz2";
//	if((writeAddress = BZ2_bzopen(filename.c_str(), "w")) == 0){
//		error("Could not open file '%s'", filename.c_str());
//	}
//	filename = traceFilename.getValue()+"-write-size.bz2";
//	if((writeSize = BZ2_bzopen(filename.c_str(), "w")) == 0){
//		error("Could not open file '%s'", filename.c_str());
//	}
//
//	UINT64 lastITimestamp = 0, lastRTimestamp = 0, lastWTimestamp = 0;
//
//	UINT64 timestamp;
//	ADDRINT addr;
//	UINT8 size;
//	BOOL read;
//	BOOL instr;
//	while(reader.readEntry(&timestamp, &addr, &size, &read, &instr)){
//		if (instr){
//			UINT64 ts = timestamp - lastITimestamp;
//			lastITimestamp = timestamp;
//			BZ2_bzwrite(instrTimestamp, &ts, sizeof(UINT64));
//			BZ2_bzwrite(instrAddress, &addr, sizeof(ADDRINT));
//			BZ2_bzwrite(instrSize, &size, sizeof(UINT8));
//		} else {
//			if (read){
//				UINT64 ts = timestamp - lastRTimestamp;
//				lastRTimestamp = timestamp;
//				BZ2_bzwrite(readTimestamp, &ts, sizeof(UINT64));
//				BZ2_bzwrite(readAddress, &addr, sizeof(ADDRINT));
//				BZ2_bzwrite(readSize, &size, sizeof(UINT8));
//			} else {
//				UINT64 ts = timestamp - lastWTimestamp;
//				lastWTimestamp = timestamp;
//				BZ2_bzwrite(writeTimestamp, &ts , sizeof(UINT64));
//				BZ2_bzwrite(writeAddress, &addr, sizeof(ADDRINT));
//				BZ2_bzwrite(writeSize, &size, sizeof(UINT8));
//			}
//		}
//	}
//
//	BZ2_bzclose(instrTimestamp);
//	BZ2_bzclose(instrAddress);
//	BZ2_bzclose(instrSize);
//	BZ2_bzclose(readTimestamp);
//	BZ2_bzclose(readAddress);
//	BZ2_bzclose(readSize);
//	BZ2_bzclose(writeTimestamp);
//	BZ2_bzclose(writeAddress);
//	BZ2_bzclose(writeSize);
//
//	return 0;
//
//}
