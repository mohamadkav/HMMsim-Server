/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#include "Arguments.H"
#include "TraceHandler.H"


int main(int argc, char * argv[]){

	ArgumentContainer args("texter", false);
	PositionalArgument<string> inputFile(&args, "input_file", "input file", "");

	if (args.parse(argc, argv)){
		args.usage(cerr);
		return -1;
	}


	//TraceReader reader(inputFile.getValue());
	CompressedTraceReader reader(inputFile.getValue(), GZIP);

	TraceEntry entry;
	while(reader.readEntry(&entry)){
		cout << entry.timestamp << "\t" << entry.address <<  "\t" << (uint32)entry.size << "\t" << (entry.read ? "R" : "W") << "\t" << (entry.instr ? "I" : "D") << endl;
	}

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
