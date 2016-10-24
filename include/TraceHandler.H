/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#ifndef TRACEHANDLER_H_
#define TRACEHANDLER_H_

#include "Types.H"

#include <bzlib.h>
#include <zlib.h>

#include <iostream>

using namespace std;


struct TraceEntry{
	uint64 timestamp;
	addrint address;
	uint8 size;
	bool read;
	bool instr;
	uint8 padding[5];
};

class TraceReaderBase {
public:
	TraceReaderBase();
	virtual bool readEntry(TraceEntry *entry) = 0;
	virtual ~TraceReaderBase() {}

	//Statistics
	uint64 numInstr;
	uint64 numReads;
	uint64 numWrites;
};

class TraceReader : public TraceReaderBase{
private:
	const static size_t BUFFER_SIZE = 32768;
	FILE *trace;
	TraceEntry entries[BUFFER_SIZE];
	size_t currentEntry;
	size_t lastEntry;

public:
	TraceReader(const string& filename);
	virtual ~TraceReader();
	bool readEntry(TraceEntry *entry);
};

enum CompressionType {
	GZIP,
	BZIP2
};

class CompressedTraceReader : public TraceReaderBase{
private:
	class TraceMerger {
	private:
		const static int BUFFER_SIZE = 1;
		CompressionType compression;
		gzFile gzTimestampFile;
		gzFile gzAddressFile;
		gzFile gzSizeFile;
		FILE *timestampFile;
		FILE *addressFile;
		FILE *sizeFile;
		BZFILE *timestampTrace;
		BZFILE *addressTrace;
		BZFILE *sizeTrace;
		uint64 *timestampEntries;
		addrint *addressEntries;
		uint8 *sizeEntries;
		int currentEntry;
		int lastEntry;
		uint64 currentTimestamp;
	public:
		TraceMerger(const string& prefix, CompressionType compressionArg);
		~TraceMerger();
		bool readEntry(uint64 *timestamp, addrint *addr, uint8 *size);
	};

private:
	TraceMerger instrMerger;
	TraceMerger readMerger;
	TraceMerger writeMerger;

	uint64 instrTimestamp;
	uint64 readTimestamp;
	uint64 writeTimestamp;
	addrint instrAddress;
	addrint readAddress;
	addrint writeAddress;
	uint8 instrSize;
	uint8 readSize;
	uint8 writeSize;

	bool instrValid;
	bool readValid;
	bool writeValid;

public:
	CompressedTraceReader(const string& prefix, CompressionType compressionArg);
	bool readEntry(TraceEntry *entry);
};

class TraceWriterBase {
public:
	virtual void writeEntry(TraceEntry *entry) = 0;
	virtual ~TraceWriterBase() {}
};

class TraceWriter : public TraceWriterBase {
private:
	const static size_t BUFFER_SIZE = 32768;
	FILE *trace;
	struct TraceEntry entries[BUFFER_SIZE];
	size_t currentEntry;
public:
	TraceWriter(const string& filename);
	~TraceWriter();
	void writeEntry(TraceEntry *entry);
};


class CompressedTraceWriter : public TraceWriterBase {
private:
	class TraceSplitter {
	private:
		const static int BUFFER_SIZE = 1;//4194304;
		CompressionType compression;
		gzFile gzTimestampFile;
		gzFile gzAddressFile;
		gzFile gzSizeFile;
		FILE *timestampFile;
		FILE *addressFile;
		FILE *sizeFile;
		BZFILE *timestampTrace;
		BZFILE *addressTrace;
		BZFILE *sizeTrace;
		uint64 *timestampEntries;
		addrint *addressEntries;
		uint8 *sizeEntries;
		int currentEntry;
		uint64 lastTimestamp;
	public:
		TraceSplitter(const string& prefix, CompressionType compressionArg);
		~TraceSplitter();
		void writeEntry(uint64 timestamp, addrint addr, uint8 size);
	};

private:
	TraceSplitter instrSplitter;
	TraceSplitter readSplitter;
	TraceSplitter writeSplitter;

public:
	CompressedTraceWriter(const string& prefix, CompressionType compressionArg);
	void writeEntry(TraceEntry *entry);
};

#endif /* TRACEHANDLER_H_ */
