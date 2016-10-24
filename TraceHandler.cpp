/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#include "Error.H"
#include "TraceHandler.H"

#include <iomanip>

#include <cassert>


TraceReaderBase::TraceReaderBase(){
	numInstr = 0;
	numReads = 0;
	numWrites = 0;
}

TraceReader::TraceReader(const string& filename) : TraceReaderBase(){
	trace = fopen(filename.c_str(), "r");
	if (trace == 0){
		error("Could not open file '%s'", filename.c_str());
	}
	currentEntry = BUFFER_SIZE;
	lastEntry = -1;
}

TraceReader::~TraceReader(){
	fclose(trace);
}

bool TraceReader::readEntry(TraceEntry *entry){
	if (currentEntry == BUFFER_SIZE){
		size_t r = fread(&entries, sizeof(struct TraceEntry), BUFFER_SIZE, trace);
		if (r == BUFFER_SIZE){
			currentEntry = 0;
		} else {
			if (feof(trace)){
				currentEntry = 0;
				lastEntry = r;
			}
			if (ferror(trace)){
				error("Error reading trace file");
			}
		}
	}
	if (lastEntry == currentEntry){
		return false;
	}
	*entry = entries[currentEntry];
	currentEntry++;
	if (entry->instr){
		numInstr++;
	} else {
		if (entry->read){
			numReads++;
		} else {
			numWrites++;
		}
	}
	return true;
}


CompressedTraceReader::TraceMerger::TraceMerger(const string& prefix, CompressionType compressionArg){
	compression = compressionArg;
	if (compression == GZIP){
		string filename = prefix+"-time.gz";
		if((gzTimestampFile = gzopen(filename.c_str(), "r")) == 0){
			error("Could not open file '%s'", filename.c_str());
		}
		filename = prefix+"-addr.gz";
		if((gzAddressFile = gzopen(filename.c_str(), "r")) == 0){
			error("Could not open file '%s'", filename.c_str());
		}
		filename = prefix+"-size.gz";
		if((gzSizeFile = gzopen(filename.c_str(), "r")) == 0){
			error("Could not open file '%s'", filename.c_str());
		}
	} else if (compression == BZIP2){
		string filename = prefix+"-time.bz2";
		int bzerror;
		if((timestampFile = fopen(filename.c_str(), "r")) == 0){
			error("Could not open file '%s'", filename.c_str());
		}
		if((timestampTrace = BZ2_bzReadOpen(&bzerror, timestampFile, 0, 0, 0, 0)) == 0){
			error("Could not prepare for reading compressed file '%s': %d", filename.c_str(), bzerror);
		}
		filename = prefix+"-addr.bz2";
		if((addressFile = fopen(filename.c_str(), "r")) == 0){
			error("Could not open file '%s'", filename.c_str());
		}
		if((addressTrace = BZ2_bzReadOpen(&bzerror, addressFile, 0, 0, 0, 0)) == 0){
			error("Could not prepare for reading compressed file '%s': %d", filename.c_str(), bzerror);
		}
		filename = prefix+"-size.bz2";
		if((sizeFile = fopen(filename.c_str(), "r")) == 0){
			error("Could not open file '%s'", filename.c_str());
		}
		if((sizeTrace = BZ2_bzReadOpen(&bzerror, sizeFile, 0, 0, 0, 0)) == 0){
			error("Could not prepare for reading compressed file '%s': %d", filename.c_str(), bzerror);
		}
	}
	timestampEntries = new uint64[BUFFER_SIZE];
	addressEntries = new addrint[BUFFER_SIZE];
	sizeEntries = new uint8[BUFFER_SIZE];
	currentEntry = BUFFER_SIZE;
	lastEntry = -1;
	currentTimestamp = 0;
}

CompressedTraceReader::TraceMerger::~TraceMerger(){
	if (compression == GZIP){
		gzclose(gzTimestampFile);
		gzclose(gzAddressFile);
		gzclose(gzSizeFile);
	} else if (compression == BZIP2){
		int timestampError, addressError, sizeError;
		BZ2_bzReadClose(&timestampError, timestampTrace);
		BZ2_bzReadClose(&addressError, addressTrace);
		BZ2_bzReadClose(&sizeError, sizeTrace);
		if (timestampError != BZ_OK || addressError != BZ_OK || sizeError != BZ_OK){
			error("Error closing compressed file");
		}
		fclose(timestampFile);
		fclose(addressFile);
		fclose(sizeFile);
	}
	delete [] timestampEntries;
	delete [] addressEntries;
	delete [] sizeEntries;
}

bool CompressedTraceReader::TraceMerger::readEntry(uint64 *timestamp, addrint *addr, uint8 *size){
	static long testFileRead;
	if (currentEntry == BUFFER_SIZE){
		if (compression == GZIP){
			testFileRead++;
			//cout<<"RD: "<<testFileRead<<endl;
			int timestampRead = gzread(gzTimestampFile, timestampEntries, BUFFER_SIZE*sizeof(uint64))/sizeof(uint64);
			int addressRead = gzread(gzAddressFile, addressEntries, BUFFER_SIZE*sizeof(addrint))/sizeof(addrint);
			int sizeRead = gzread(gzSizeFile, sizeEntries, BUFFER_SIZE*sizeof(uint8))/sizeof(uint8);

			if (timestampRead != addressRead || timestampRead != sizeRead){
				error("Bytes read from all three files are not the same (timestamp: %d, address: %d and size: %d)", timestampRead, addressRead, sizeRead);
			}

			if (timestampRead == -1){
				error("Error reading trace file");
			} else if (timestampRead == BUFFER_SIZE){
				currentEntry = 0;
			} else {
				currentEntry = 0;
				lastEntry = timestampRead;
			}

		} else if (compression == BZIP2){
			int timestampError, addressError, sizeError;
			int timestampRead = BZ2_bzRead(&timestampError, timestampTrace, timestampEntries, BUFFER_SIZE*sizeof(uint64))/sizeof(uint64);
			int addressRead = BZ2_bzRead(&addressError, addressTrace, addressEntries, BUFFER_SIZE*sizeof(addrint))/sizeof(addrint);
			int sizeRead = BZ2_bzRead(&sizeError, sizeTrace, sizeEntries, BUFFER_SIZE*sizeof(uint8))/sizeof(uint8);

			if (timestampError != addressError || timestampError != sizeError){
				error("Errors from all three files are not the same (timestamp: %d, address: %d and size: %d)", timestampError, addressError, sizeError);
			}

			if (timestampRead != addressRead || timestampRead != sizeRead){
				error("Bytes read from all three files are not the same (timestamp: %d, address: %d and size: %d)", timestampRead, addressRead, sizeRead);
			}

			if (timestampError == BZ_OK){
				assert(timestampRead == BUFFER_SIZE);
				currentEntry = 0;
			} else if (timestampError == BZ_STREAM_END){
				currentEntry = 0;
				lastEntry = timestampRead;
			} else {
				error("Error reading trace file");
			}
		}
	}
	if (lastEntry == currentEntry){
		return false;
	}
	*timestamp = timestampEntries[currentEntry];
	*addr = addressEntries[currentEntry];
	*size = sizeEntries[currentEntry];
	currentEntry++;
	return true;
}


CompressedTraceReader::CompressedTraceReader(const string& prefix, CompressionType compressionArg) : TraceReaderBase(), instrMerger(prefix+"-instr", compressionArg), readMerger(prefix+"-read", compressionArg), writeMerger(prefix+"-write", compressionArg){
	instrValid = instrMerger.readEntry(&instrTimestamp, &instrAddress, &instrSize);
	readValid = readMerger.readEntry(&readTimestamp, &readAddress, &readSize);
	writeValid = writeMerger.readEntry(&writeTimestamp, &writeAddress, &writeSize);
}

bool CompressedTraceReader::readEntry(TraceEntry *entry){
	//static int i;
	//i++;
//	cout<<i<<"   "<<entry->address<<"Time: "<<entry->timestamp<<endl;
	int type; //0:instr, 1: read, 2: write, 3: invalid
	if (instrValid){
		if (readValid){
			if (writeValid){
				if (instrTimestamp <= readTimestamp){
					if (instrTimestamp <= writeTimestamp){
						type = 0;
					} else {
						type = 2;
					}
				} else {
					if (readTimestamp <= writeTimestamp){
						type = 1;
					} else {
						type = 2;
					}
				}
			} else {
				if (instrTimestamp <= readTimestamp){
					type = 0;
				} else {
					type = 1;
				}
			}
		} else {
			if (writeValid){
				if (instrTimestamp <= writeTimestamp){
					type = 0;
				} else {
					type = 2;
				}
			} else {
				type = 0;
			}
		}
	} else {
		if (readValid){
			if (writeValid){
				if (readTimestamp <= writeTimestamp){
					type = 1;
				} else {
					type = 2;
				}
			} else {
				type = 1;
			}
		} else {
			if (writeValid){
				type = 2;
			} else {
				type = 3;
			}
		}
	}

	uint64 ts;
	if (type == 0){
		entry->timestamp = instrTimestamp;
		entry->address = instrAddress;
		entry->size = instrSize;
		entry->read = true;
		entry->instr = true;
		instrValid = instrMerger.readEntry(&ts, &instrAddress, &instrSize);
		instrTimestamp += ts;
		numInstr++;
		return true;
	} else if (type == 1){
		entry->timestamp = readTimestamp;
		entry->address = readAddress;
		entry->size = readSize;
		entry->read = true;
		entry->instr = false;
		readValid = readMerger.readEntry(&ts, &readAddress, &readSize);
		readTimestamp += ts;
		numReads++;
		return true;
	} else if (type == 2){
		entry->timestamp = writeTimestamp;
		entry->address = writeAddress;
		entry->size = writeSize;
		entry->read = false;
		entry->instr = false;
		writeValid = writeMerger.readEntry(&ts, &writeAddress, &writeSize);
		writeTimestamp += ts;
		numWrites++;
		return true;
	} else if (type == 3){
		return false;
	} else {
		error("Error merging entries");
		return false;
	}
}


TraceWriter::TraceWriter(const string& filename){
	if((trace = fopen(filename.c_str(), "w")) == 0){
		error("Could not open file '%s'", filename.c_str());
	}

	currentEntry = 0;
}

TraceWriter::~TraceWriter(){
	fwrite(&entries, sizeof(struct TraceEntry), currentEntry, trace);
	fclose(trace);
}

void TraceWriter::writeEntry(TraceEntry *entry){
	entries[currentEntry] = *entry;
//	entries[currentEntry].timestamp = timestamp;
//	entries[currentEntry].address = addr;
//	entries[currentEntry].size = size;
//	entries[currentEntry].read = read;
//	entries[currentEntry].instr = instr;
	currentEntry++;
	if (currentEntry >= BUFFER_SIZE){
		fwrite(&entries, sizeof(struct TraceEntry), BUFFER_SIZE, trace);
		currentEntry = 0;
	}
}


CompressedTraceWriter::TraceSplitter::TraceSplitter(const string& prefix, CompressionType compressionArg){
	compression = compressionArg;
	if (compression == GZIP){
		string filename = prefix+"-time.gz";
		if((gzTimestampFile = gzopen(filename.c_str(), "w1")) == 0){
			error("Could not open file '%s'", filename.c_str());
		}
		filename = prefix+"-addr.gz";
		if((gzAddressFile = gzopen(filename.c_str(), "w1")) == 0){
			error("Could not open file '%s'", filename.c_str());
		}
		filename = prefix+"-size.gz";
		if((gzSizeFile = gzopen(filename.c_str(), "w1")) == 0){
			error("Could not open file '%s'", filename.c_str());
		}
	} else if (compression == BZIP2){
		string filename = prefix+"-time.bz2";
		int bzerror;
		if((timestampFile = fopen(filename.c_str(), "w")) == 0){
			error("Could not open file '%s'", filename.c_str());
		}
		if((timestampTrace = BZ2_bzWriteOpen(&bzerror, timestampFile, 9, 0, 0)) == 0){
			error("Could not prepare for reading compressed file '%s': %d", filename.c_str(), bzerror);
		}
		filename = prefix+"-addr.bz2";
		if((addressFile = fopen(filename.c_str(), "w")) == 0){
			error("Could not open file '%s'", filename.c_str());
		}
		if((addressTrace = BZ2_bzWriteOpen(&bzerror, addressFile, 9, 0, 0)) == 0){
			error("Could not prepare for reading compressed file '%s': %d", filename.c_str(), bzerror);
		}
		filename = prefix+"-size.bz2";
		if((sizeFile = fopen(filename.c_str(), "w")) == 0){
			error("Could not open file '%s'", filename.c_str());
		}
		if((sizeTrace = BZ2_bzWriteOpen(&bzerror, sizeFile, 9, 0, 0)) == 0){
			error("Could not prepare for reading compressed file '%s': %d", filename.c_str(), bzerror);
		}
	} else {
		error("Invalid compression type");
	}
	timestampEntries = new uint64[BUFFER_SIZE];
	addressEntries = new addrint[BUFFER_SIZE];
	sizeEntries = new uint8[BUFFER_SIZE];
	currentEntry = 0;
	lastTimestamp = 0;
}

CompressedTraceWriter::TraceSplitter::~TraceSplitter(){
	if (compression == GZIP){
		if (currentEntry != 0){
			int timestampWritten = gzwrite(gzTimestampFile, timestampEntries, currentEntry*sizeof(uint64));
			int addressWritten = gzwrite(gzAddressFile, addressEntries, currentEntry*sizeof(addrint));
			int sizeWritten = gzwrite(gzSizeFile, sizeEntries, currentEntry*sizeof(uint8));
			if (timestampWritten == 0 || addressWritten == 0 || sizeWritten == 0){
				error("Error writing to compressed file");
			}
		}
		gzclose(gzTimestampFile);
		gzclose(gzAddressFile);
		gzclose(gzSizeFile);
	} else if (compression == BZIP2){
		int timestampError, addressError, sizeError;
		if (currentEntry != 0){
			BZ2_bzWrite(&timestampError, timestampTrace, timestampEntries, currentEntry*sizeof(uint64));
			BZ2_bzWrite(&addressError, addressTrace, addressEntries, currentEntry*sizeof(addrint));
			BZ2_bzWrite(&sizeError, sizeTrace, sizeEntries, currentEntry*sizeof(uint8));
			if (timestampError != BZ_OK || addressError != BZ_OK || sizeError != BZ_OK){
				error("Error writing to compressed file");
			}
		}
		BZ2_bzWriteClose(&timestampError, timestampTrace, 0, 0, 0);
		BZ2_bzWriteClose(&addressError, addressTrace, 0, 0, 0);
		BZ2_bzWriteClose(&sizeError, sizeTrace, 0, 0, 0);
		if (timestampError != BZ_OK || addressError != BZ_OK || sizeError != BZ_OK){
			error("Error closing compressed file");
		}
		fclose(timestampFile);
		fclose(addressFile);
		fclose(sizeFile);
	}
	delete [] timestampEntries;
	delete [] addressEntries;
	delete [] sizeEntries;
}

void CompressedTraceWriter::TraceSplitter::writeEntry(uint64 timestamp, addrint addr, uint8 size){
	uint64 ts = timestamp - lastTimestamp;
	lastTimestamp = timestamp;
	timestampEntries[currentEntry] = ts;
	addressEntries[currentEntry] = addr;
	sizeEntries[currentEntry] = size;
	currentEntry++;
	if (currentEntry == BUFFER_SIZE){
		if (compression == GZIP){
			int timestampWritten = gzwrite(gzTimestampFile, timestampEntries, BUFFER_SIZE*sizeof(uint64));
			int addressWritten = gzwrite(gzAddressFile, addressEntries, BUFFER_SIZE*sizeof(addrint));
			int sizeWritten = gzwrite(gzSizeFile, sizeEntries, BUFFER_SIZE*sizeof(uint8));
			if (timestampWritten == 0 || addressWritten == 0 || sizeWritten == 0){
				error("Error writing to compressed file");
			}
		} else if (compression == BZIP2){
			int timestampError, addressError, sizeError;
			BZ2_bzWrite(&timestampError, timestampTrace, timestampEntries, BUFFER_SIZE*sizeof(uint64));
			BZ2_bzWrite(&addressError, addressTrace, addressEntries, BUFFER_SIZE*sizeof(addrint));
			BZ2_bzWrite(&sizeError, sizeTrace, sizeEntries, BUFFER_SIZE*sizeof(uint8));
			if (timestampError != BZ_OK || addressError != BZ_OK || sizeError != BZ_OK){
				error("Error writing to compressed file");
			}
		}
		currentEntry = 0;
	}
}


CompressedTraceWriter::CompressedTraceWriter(const string& prefix, CompressionType compressionArg) : instrSplitter(prefix+"-instr", compressionArg), readSplitter(prefix+"-read", compressionArg), writeSplitter(prefix+"-write", compressionArg) {

}

void CompressedTraceWriter::writeEntry(TraceEntry *entry){
	if (entry->instr){
		instrSplitter.writeEntry(entry->timestamp, entry->address, entry->size);
	} else {
		if (entry->read){
			readSplitter.writeEntry(entry->timestamp, entry->address, entry->size);
		} else {
			writeSplitter.writeEntry(entry->timestamp, entry->address, entry->size);
		}
	}
}
