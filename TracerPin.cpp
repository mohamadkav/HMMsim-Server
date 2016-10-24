/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#include "TraceHandler.H"
#include "Types.H"

#include "pin.H"
#include "pinplay.H"

#include <sys/time.h>

#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <cassert>


/* ================================================================== */
// Types
/* ================================================================== */

VOID Fini(INT32 code, VOID *v);

/* ================================================================== */
// Global variables 
/* ================================================================== */

UINT64 icount = 0;
UINT64 icountStart;
UINT64 icountLimit;

enum Action {
	ACTION_TRACE_SINGLE,
	ACTION_TRACE_MULTI,
	ACTION_COUNTERS
};

Action action;

CompressedTraceWriter *writer;

TLS_KEY key;

struct ThreadData {
	UINT64 icount;
	UINT64 icountStart;
	UINT64 icountLimit;
	TraceWriterBase  *writer;
	THREADID parentTid;
	unordered_map<ADDRINT, UINT64> pages;
	ofstream log;
	ThreadData (THREADID parentTidArg, const string& logfile) : icount(0), icountStart(numeric_limits<UINT64>::max()), icountLimit(numeric_limits<UINT64>::max()), writer(0), parentTid(parentTidArg), log(logfile) {}
};

map<OS_THREAD_ID, THREADID> threads;

UINT64 threadsLeft; //number of threads left to collect KnobICount instructions

bool roiBeginCalled;

unordered_map<ADDRINT, UINT64> allPages;

unsigned offsetWidth;
ADDRINT getIndex(ADDRINT addr) {return addr >> offsetWidth;}
UINT64 period;
ofstream out;
unordered_set<ADDRINT> pages;


/* ===================================================================== */
// Enable PinPlay
/* ===================================================================== */
#define KNOB_LOG_NAME  "log"
#define KNOB_REPLAY_NAME "replay"
#define KNOB_FAMILY "pintool"

PINPLAY_ENGINE pinplay_engine;

KNOB<BOOL>KnobReplayer(KNOB_MODE_WRITEONCE, KNOB_FAMILY,
                       KNOB_REPLAY_NAME, "0", "Replay a pinball");
KNOB<BOOL>KnobLogger(KNOB_MODE_WRITEONCE,  KNOB_FAMILY,
                     KNOB_LOG_NAME, "0", "Create a pinball");

/* ===================================================================== */
// Command line switches
/* ===================================================================== */
KNOB<string> KnobTraceFile(KNOB_MODE_WRITEONCE, "pintool", "o", "trace", "Name of output file with the trace");

KNOB<string> KnobAction(KNOB_MODE_WRITEONCE, "pintool", "a", "trace_single", "Action to perform (trace_single|trace_multi|counters)");
//trace_*: generate trace
//counters: generate file with number of unique pages per interval

KNOB<UINT64> KnobICountStart(KNOB_MODE_WRITEONCE, "pintool", "s","0", "Instruction count start");
KNOB<UINT64> KnobICount(KNOB_MODE_WRITEONCE, "pintool", "i","0", "Instruction count");
KNOB<UINT64> KnobThreads(KNOB_MODE_WRITEONCE, "pintool", "n","1", "Number of threads");
KNOB<BOOL> KnobUseROI(KNOB_MODE_WRITEONCE, "pintool", "r","0", "Whether to use call to __parsec_roi_begin() as starting point of instruction count start");
KNOB<BOOL> KnobDryRun(KNOB_MODE_WRITEONCE, "pintool", "d","0", "Whether to do a dry run (writes traces to /dev/null");


KNOB<UINT64> KnobPeriod(KNOB_MODE_WRITEONCE, "pintool", "p","1000000", "Instruction period for counters");
KNOB<UINT64> KnobPageSize(KNOB_MODE_WRITEONCE, "pintool", "ps","4096", "Page size");



/* ===================================================================== */
// Utilities
/* ===================================================================== */

INT32 Usage()
{
    cerr << "This tool creates a memory trace " << endl << endl;

    cerr << KNOB_BASE::StringKnobSummary() << endl;

    return -1;
}



/* ===================================================================== */
// Analysis routines
/* ===================================================================== */

VOID ProcessMemAccess(ADDRINT addr, UINT32 size, BOOL read, BOOL instr, THREADID tid) {
	assert(size <= numeric_limits<UINT8>::max());

	if (action == ACTION_TRACE_SINGLE || action == ACTION_COUNTERS) {
		if (instr){
			icount++;
			if (icountLimit != 0 && icount >= icountLimit){
				Fini(0, 0);
				exit(0);
			}
		}
	}


	if (action == ACTION_TRACE_SINGLE){
		if (icount < icountStart){
			ADDRINT index1 = getIndex(addr);
			ADDRINT index2 = getIndex(addr + size - 1);
			if (pages.emplace(index1).second){
				out << index1 << endl;
			}
			if (index1 != index2){
				if (pages.emplace(index2).second){
					out << index2 << endl;
				}
			}
		} else {
			TraceEntry entry;
			entry.timestamp = icount;
			entry.address = addr;
			entry.size = (UINT8)size;
			entry.read = read;
			entry.instr = instr;
			writer->writeEntry(&entry);
		}
	} else if (action == ACTION_TRACE_MULTI){
		ThreadData *tdata = static_cast<ThreadData*>(PIN_GetThreadData(key, tid));
		if (instr){
			if (tdata->icount == 0){
				if (tdata->parentTid != INVALID_THREADID){
					if (PIN_StopApplicationThreads(tid)){
						ThreadData *parent_tdata = static_cast<ThreadData*>(PIN_GetThreadData(key, tdata->parentTid));
						tdata->icount = parent_tdata->icount;
						tdata->icountStart = max(parent_tdata->icount, parent_tdata->icountStart);
						if (KnobICount.Value() != 0){
							tdata->icountLimit = tdata->icountStart + KnobICount.Value();
						}
						tdata->pages = parent_tdata->pages;
						struct timeval t;
						gettimeofday(&t,NULL);
						PIN_ResumeApplicationThreads(tid);
						tdata->log << "Thread " << tid << ": copied icount (" << tdata->icount <<  ") from parent: " << tdata->parentTid << " at time: " << t.tv_sec << "." << t.tv_usec << endl;
					} else {
						cerr << "Thread " << tid << ": could not stop application threads when starting" << endl;
						exit(1);
					}
				}
			}
		}
		if (tdata->icount < tdata->icountStart){
			ADDRINT index1 = getIndex(addr);
			ADDRINT index2 = getIndex(addr + size - 1);
			tdata->pages.emplace(index1, tdata->icount);
			if (index1 != index2){
				tdata->pages.emplace(index2, tdata->icount);
			}
		} else {
			if (tdata->icount < tdata->icountLimit) {
				if (tdata->writer == 0){
					if (KnobDryRun.Value()){
						tdata->writer = new TraceWriter("/dev/null");
					} else {
						tdata->writer = new CompressedTraceWriter(KnobTraceFile.Value() + "-" + to_string(tid), GZIP);

					}
					struct timeval t;
					gettimeofday(&t,NULL);
					tdata->log << "Thread " << tid << ": started collection with " << tdata->icount << " at time: " << t.tv_sec << "." << t.tv_usec << endl;
				}
				if (!KnobDryRun.Value()){
					TraceEntry entry;
					entry.timestamp = tdata->icount;
					entry.address = addr;
					entry.size = (UINT8)size;
					entry.read = read;
					entry.instr = instr;
					tdata->writer->writeEntry(&entry);
				}
			}
		}
		if (instr){
			if (tdata->icount >= tdata->icountLimit){
				if (tdata->writer != 0){
					if (PIN_StopApplicationThreads(tid)){
						delete tdata->writer;
						tdata->writer = 0;
						struct timeval t;
						gettimeofday(&t,NULL);
						tdata->log << "Thread " << tid << ": finished collection with icount: " << tdata->icount << " at time: " << t.tv_sec << "." << t.tv_usec << endl;
						threadsLeft--;
						if (threadsLeft == 0){
							tdata->log << "Thread " << tid << ": finished execution with icount: " << tdata->icount << " at time: " << t.tv_sec << "." << t.tv_usec << endl;
							for (auto it = tdata->pages.begin(); it != tdata->pages.end(); ++it){
						    	auto ait = allPages.emplace(it->first, it->second);
						    	if (!ait.second){
						    		ait.first->second = min(ait.first->second, it->second);
						    	}
						    }
							tdata->log.close();
							for (UINT32 i = 0; i < PIN_GetStoppedThreadCount(); i++){
								THREADID otid = PIN_GetStoppedThreadId(i);
								ThreadData *td = static_cast<ThreadData*>(PIN_GetThreadData(key, otid));
								if (td != 0) {
									if(td->writer != 0){
										delete td->writer;
										td->writer = 0;
										td->log << "Thread " << otid << ": finished collection with icount: " << td->icount << " at time: " << t.tv_sec << "." << t.tv_usec << endl;
									}
									td->log << "Thread " << otid << ": finished execution with icount: " << td->icount << " at time: " << t.tv_sec << "." << t.tv_usec << endl;
									for (auto it = td->pages.begin(); it != td->pages.end(); ++it){
								    	auto ait = allPages.emplace(it->first, it->second);
								    	if (!ait.second){
								    		ait.first->second = min(ait.first->second, it->second);
								    	}
								    }
									td->log.close();
								}
							}
							Fini(0, 0);
							exit(0);
						}
						PIN_ResumeApplicationThreads(tid);
					} else {
						cerr << "Thread " << tid << ": could not stop application threads when finishing" << endl;
					}
				}
			}
			tdata->icount++;
		}
	} else if (action == ACTION_COUNTERS){
		if (instr && icount % period == 0){
			out << pages.size() << endl;
			out << icount << " ";
			pages.clear();
		}
		ADDRINT index1 = getIndex(addr);
		ADDRINT index2 = getIndex(addr + size - 1);
		pages.emplace(index1);
		if (index1 != index2){
			pages.emplace(index2);
		}

	}

}



/* ===================================================================== */
// Instrumentation callbacks
/* ===================================================================== */

VOID Instruction(INS ins, VOID *v) {
	USIZE instrSize = INS_Size(ins);
	INS_InsertCall(
		ins, IPOINT_BEFORE, (AFUNPTR)ProcessMemAccess,
		IARG_INST_PTR,
		IARG_UINT32, instrSize,
		IARG_BOOL, true,
		IARG_BOOL, true,
		IARG_THREAD_ID,
		IARG_END);

	if (INS_IsMemoryRead(ins)){
		INS_InsertPredicatedCall(
			ins, IPOINT_BEFORE, (AFUNPTR)ProcessMemAccess,
			IARG_MEMORYREAD_EA,
			IARG_MEMORYREAD_SIZE,
			IARG_BOOL, true,
			IARG_BOOL, false,
			IARG_THREAD_ID,
			IARG_END);
		if (INS_HasMemoryRead2(ins)){
			INS_InsertPredicatedCall(
				ins, IPOINT_BEFORE, (AFUNPTR)ProcessMemAccess,
				IARG_MEMORYREAD2_EA,
				IARG_MEMORYREAD_SIZE,
				IARG_BOOL, true,
				IARG_BOOL, false,
				IARG_THREAD_ID,
				IARG_END);
			}
	}

	if (INS_IsMemoryWrite(ins)){
		INS_InsertPredicatedCall(
			ins, IPOINT_BEFORE, (AFUNPTR)ProcessMemAccess,
			IARG_MEMORYWRITE_EA,
			IARG_MEMORYWRITE_SIZE,
			IARG_BOOL, false,
			IARG_BOOL, false,
			IARG_THREAD_ID,
			IARG_END);

	}
}

VOID afterRoiBegin(THREADID tid) {
	cerr << "Inside afterRoiBegin: " << tid << endl;
	ThreadData *tdata = static_cast<ThreadData*>(PIN_GetThreadData(key, tid));
	tdata->icountStart = tdata->icount + KnobICountStart.Value();
	if (KnobICount.Value() != 0){
		tdata->icountLimit = tdata->icountStart + KnobICount.Value();
	}
	if (PIN_StopApplicationThreads(tid)){
		assert(!roiBeginCalled);
		roiBeginCalled = true;
		for (UINT32 i = 0; i < PIN_GetStoppedThreadCount(); i++){
			ThreadData *td = static_cast<ThreadData*>(PIN_GetThreadData(key, PIN_GetStoppedThreadId(i)));
			if (td != 0){
				td->icountStart = td->icount + KnobICountStart.Value();
				if (KnobICount.Value() != 0){
					td->icountLimit = td->icountStart + KnobICount.Value();
				}
			}
		}

		PIN_ResumeApplicationThreads(tid);
	} else {
		cerr << "Count not stop threads from afterRoiBegin()" << endl;
		exit(1);
	}

	struct timeval t;
	gettimeofday(&t,NULL);
	tdata->log << "Thread " << tid << ": entered ROI with icount: " << tdata->icount << " at time: " << t.tv_sec << "." << t.tv_usec << endl;
}

VOID beforeRoiEnd(THREADID tid) {
	cerr << "Inside beforeRoiEnd: " << tid << endl;
	ThreadData *tdata = static_cast<ThreadData*>(PIN_GetThreadData(key, tid));
	struct timeval t;
	gettimeofday(&t,NULL);
	tdata->log << "Thread " << tid << ": left ROI with icount: " << tdata->icount << " at time: " << t.tv_sec << "." << t.tv_usec << endl;
}

VOID ImageLoad(IMG img, VOID *) {
    RTN rtn = RTN_FindByName(img, "__parsec_roi_begin");
    if ( RTN_Valid( rtn )) {
        RTN_Open(rtn);
        RTN_InsertCall(rtn, IPOINT_AFTER, AFUNPTR(afterRoiBegin),
                       IARG_THREAD_ID, IARG_END);
        RTN_Close(rtn);
    }
    rtn = RTN_FindByName(img, "__parsec_roi_end");
    if ( RTN_Valid( rtn )) {
        RTN_Open(rtn);
        RTN_InsertCall(rtn, IPOINT_BEFORE, AFUNPTR(beforeRoiEnd),
                       IARG_THREAD_ID, IARG_END);
        RTN_Close(rtn);
    }
}


VOID Fini(INT32 code, VOID *v) {
    if (action == ACTION_TRACE_SINGLE){
    	out.close();
    	delete writer;
    } else if (action == ACTION_TRACE_MULTI){
    	multimap<UINT64, ADDRINT> reversed;
    	for (auto ait = allPages.begin(); ait != allPages.end(); ++ait){
    		reversed.emplace(ait->second, ait->first);
    	}
    	for (auto it = reversed.begin(); it != reversed.end(); ++it){
    		out << it->second << endl;
    	}
    	out.close();
    } else if (action == ACTION_COUNTERS){
    	out << pages.size() << endl;
    	out.close();
    }
}

VOID ThreadStart(THREADID tid, CONTEXT *ctxt, INT32 flags, VOID *v) {
	string logfile = KnobTraceFile.Value() + "_" + to_string(tid) + ".log";
	THREADID parent_tid = INVALID_THREADID;
	auto it = threads.find(PIN_GetParentTid());
	if (it != threads.end()){
		parent_tid = it->second;
	}
	ThreadData *tdata = new ThreadData(parent_tid, logfile);
	PIN_SetThreadData(key, tdata, tid);

	if (!KnobUseROI.Value() && parent_tid == INVALID_THREADID){
		tdata->icountStart = tdata->icount + KnobICountStart.Value();
		if (KnobICount.Value() != 0){
			tdata->icountLimit = tdata->icountStart + KnobICount.Value();
		}
	}

	bool ins = threads.emplace(PIN_GetTid(), tid).second;
	assert(ins);

	struct timeval t;
	gettimeofday(&t,NULL);
	tdata->log << "Thread " << tid << ": started execution " << t.tv_sec << "." << t.tv_usec << endl;
	tdata->log << "Thread " << tid << ": OS Thread: " << PIN_GetTid() << ", Parent thread: " << PIN_GetParentTid() << endl;
}

VOID ThreadFini(THREADID tid, const CONTEXT *ctxt, INT32 code, VOID *v) {
	ThreadData *tdata = static_cast<ThreadData*>(PIN_GetThreadData(key, tid));
	struct timeval t;
	gettimeofday(&t,NULL);
	if (tdata->writer != 0){
		delete tdata->writer;
		tdata->log << "Thread " << tid << ": finished collection with icount: " << tdata->icount << " at time: "<< t.tv_sec << "." << t.tv_usec << endl;
	}
	tdata->log << "Thread " << tid << ": finished execution with icount: " << tdata->icount << " at time: "<< t.tv_sec << "." << t.tv_usec << endl;
	for (auto it = tdata->pages.begin(); it != tdata->pages.end(); ++it){
    	auto ait = allPages.emplace(it->first, it->second);
    	if (!ait.second){
    		ait.first->second = min(ait.first->second, it->second);
    	}
    }
	delete tdata;
    PIN_SetThreadData(key, 0, tid);
}

/*!
 * The main procedure of the tool.
 * This function is called when the application image is loaded but not yet started.
 * @param[in]   argc            total number of elements in the argv array
 * @param[in]   argv            array of command line arguments, 
 *                              including pin -t <toolname> -- ...
 */
int main(int argc, char *argv[])
{
	// Initialize PIN library. Print help message if -h(elp) is specified
    // in the command line or the command line is invalid
    if( PIN_Init(argc,argv) )
    {
        return Usage();
    }

    if (KnobAction.Value() == "trace_single"){
    	action = ACTION_TRACE_SINGLE;
    } else if (KnobAction.Value() == "trace_multi"){
        action = ACTION_TRACE_MULTI;
    } else if (KnobAction.Value() == "counters"){
    	action = ACTION_COUNTERS;
    } else {
    	return Usage();
    }

    INS_AddInstrumentFunction(Instruction, 0);
    //TRACE_AddInstrumentFunction(InstrumentTrace, 0);

    PIN_AddFiniFunction(Fini, 0);

    pinplay_engine.Activate(argc, argv, KnobLogger, KnobReplayer);


    icountStart = KnobICountStart.Value();
    icountLimit = icountStart + KnobICount.Value();

	offsetWidth = (UINT64)logb(KnobPageSize.Value());
	out.open(KnobTraceFile.Value(), ofstream::out);

    if (action == ACTION_TRACE_SINGLE){
    	writer = new CompressedTraceWriter(KnobTraceFile.Value(), GZIP);
    } else if (action == ACTION_TRACE_MULTI){
    	PIN_InitSymbols();
    	threadsLeft = KnobThreads.Value();
    	roiBeginCalled = false;
    	key = PIN_CreateThreadDataKey(0);
    	PIN_AddThreadStartFunction(ThreadStart, 0);
    	PIN_AddThreadFiniFunction(ThreadFini, 0);
    	if (KnobUseROI.Value()){
    		IMG_AddInstrumentFunction(ImageLoad, 0);
    	}
    } else if (action == ACTION_COUNTERS){
    	period = KnobPeriod.Value();
    	out << icount << " ";
    }

    cerr << KnobUseROI.Value() << endl;
    cerr <<  "===============================================" << endl;
    cerr <<  "This application is instrumented by TracerPin" << endl;
    cerr << "See file " << KnobTraceFile.Value() << " for the trace" << endl;
    cerr <<  "===============================================" << endl;
    cerr << "sizeof(TraceEntry): " << sizeof(TraceEntry) << endl;
    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */

/*
 * Old stuff
 */

//VOID Instruction(INS ins, VOID *v) {
//    UINT32 memOperands = INS_MemoryOperandCount(ins);
//
//    // Iterate over each memory operand of the instruction.
//    for (UINT32 memOp = 0; memOp < memOperands; memOp++) {
//        if (INS_MemoryOperandIsRead(ins, memOp)) {
//            INS_InsertPredicatedCall(
//                ins, IPOINT_BEFORE, (AFUNPTR)ProcessMemAccess,
//                IARG_MEMORYOP_EA, memOp,
//                IARG_MEMORYREAD_SIZE,
//                IARG_BOOL, true,
//                IARG_BOOL, false,
//                IARG_END);
//        }
//        // Note that in some architectures a single memory operand can be
//        // both read and written (for instance incl (%eax) on IA-32)
//        // In that case we instrument it once for read and once for write.
//        if (INS_MemoryOperandIsWritten(ins, memOp)) {
//            INS_InsertPredicatedCall(
//                ins, IPOINT_BEFORE, (AFUNPTR)ProcessMemAccess,
//                IARG_MEMORYOP_EA, memOp,
//                IARG_MEMORYWRITE_SIZE,
//                IARG_BOOL, false,
//                IARG_BOOL, false,
//                IARG_END);
//        }
//    }
//}

//VOID ProcessBbl(UINT32 numIns, void *a, void *is, THREADID tid){
//	ADDRINT *addresses = static_cast<ADDRINT* >(a);
//	UINT32 *sizes = static_cast<UINT32* >(is);
//	for (UINT32 i = 0; i < numIns; i++){
//		ProcessMemAccess(addresses[i], sizes[i], true, true, tid);
//	}
//}

//VOID InstrumentBbl(BBL bbl){
//	UINT32 numIns = BBL_NumIns(bbl);
//	UINT32 *instrSizes = new UINT32[numIns];
//	ADDRINT *addresses = new ADDRINT[numIns];
//	BBL_InsertCall(bbl, IPOINT_BEFORE, (AFUNPTR)ProcessBbl, IARG_UINT32, numIns, IARG_PTR, addresses, IARG_PTR, instrSizes, IARG_THREAD_ID, IARG_END);
//	USIZE i = 0;
//	for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)){
//		instrSizes[i] = INS_Size(ins);
//		addresses[i] = INS_Address(ins);
//		i++;
//
//		UINT32 memOperands = INS_MemoryOperandCount(ins);
//
//		// Iterate over each memory operand of the instruction.
//		for (UINT32 memOp = 0; memOp < memOperands; memOp++)
//		{
//			if (INS_MemoryOperandIsRead(ins, memOp))
//			{
//				INS_InsertPredicatedCall(
//					ins, IPOINT_BEFORE, (AFUNPTR)ProcessMemAccess,
//					IARG_MEMORYOP_EA, memOp,
//					IARG_MEMORYREAD_SIZE,
//					IARG_BOOL, true,
//					IARG_BOOL, false,
//					IARG_END);
//			}
//			// Note that in some architectures a single memory operand can be
//			// both read and written (for instance incl (%eax) on IA-32)
//			// In that case we instrument it once for read and once for write.
//			if (INS_MemoryOperandIsWritten(ins, memOp))
//			{
//				INS_InsertPredicatedCall(
//					ins, IPOINT_BEFORE, (AFUNPTR)ProcessMemAccess,
//					IARG_MEMORYOP_EA, memOp,
//					IARG_MEMORYWRITE_SIZE,
//					IARG_BOOL, false,
//					IARG_BOOL, false,
//					IARG_END);
//			}
//		}
//	}
//}
//
//void InstrumentTrace(TRACE trace, void *)
//{
//    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
//    {
//        InstrumentBbl(bbl);
//    }
//}

