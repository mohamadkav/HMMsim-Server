/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#include "Arguments.H"
#include "Cache.H"
#include "Error.H"
#include "TraceHandler.H"
#include "Statistics.H"

#include <bitset>

#include <cmath>
#include <cassert>


#define MAX_BITSET_SIZE 256

struct Address {
	unsigned numBlocks;
	unsigned pageIndexWidth;
	unsigned blockIndexWidth;
	unsigned offsetWidth;
	addrint pageIndexMask;
	addrint blockIndexMask;
	addrint offsetMask;

	Address(unsigned pageSize, unsigned blockSize){
		numBlocks = pageSize/blockSize;
		offsetWidth = (uint64)logb(blockSize);
		blockIndexWidth = (uint64)logb(pageSize/blockSize);
		pageIndexWidth = sizeof(addrint)*8 - offsetWidth - blockIndexWidth;

		offsetMask = 0;
		for (unsigned i = 0; i < offsetWidth; i++){
			offsetMask |= (addrint)1U << i;
		}
		blockIndexMask = 0;
		for (unsigned i = offsetWidth; i < blockIndexWidth+offsetWidth; i++){
			blockIndexMask |= (addrint)1U << i;
		}
		pageIndexMask = 0;
		for (unsigned i = blockIndexWidth+offsetWidth; i < pageIndexWidth+blockIndexWidth+offsetWidth; i++){
			pageIndexMask |= (addrint)1U << i;
		}


//		cout << offsetWidth << endl;
//		cout << blockIndexWidth << endl;
//		cout << pageIndexWidth << endl;
//		cout << hex;
//		cout << offsetMask << endl;
//		cout << blockIndexMask << endl;
//		cout << pageIndexMask << endl;
//		cout << dec;
	}

	//page index | block index | offset
	addrint getPageIndex(addrint addr) const {return addr >> (blockIndexWidth+offsetWidth);}
	addrint getSecondPageIndex(addrint addr, uint8 size) const {return (addr + size - 1) >> (blockIndexWidth+offsetWidth);}
	addrint getBlockIndex(addrint addr) const {return (addr & ~pageIndexMask) >> offsetWidth;}
	addrint getSecondBlockIndex(addrint addr, uint8 size) const {return ((addr + size - 1) & ~pageIndexMask) >> offsetWidth;}
	addrint getAddr(addrint addr) const {return addr & ~offsetMask;}
	addrint getSecondAddr(addrint addr, uint8 size) const {return (addr + size - 1) & ~offsetMask;}
	addrint getOffset(addrint addr) const {return addr & offsetMask;}





};

struct PageCounter {
	uint64 reads;
	uint64 writes;
	bitset<MAX_BITSET_SIZE> readBlocks;
	bitset<MAX_BITSET_SIZE> writtenBlocks;
};

struct PageInfo {
	uint32 reads;
	uint32 writes;
	uint8 readBlocks;
	uint8 writtenBlocks;
	uint8 accessedBlocks;
};


int main(int argc, char * argv[]){

	ArgumentContainer args("analyze", false);
	PositionalArgument<string> inputFile(&args, "input_file", "input file", "");
	OptionalArgument<string> statsFile(&args, "stats", "name of statistics file", "", false);
	OptionalArgument<string> traceFile(&args, "trace_file", "name of output trace file", "", false);

	OptionalArgument<string> type(&args, "type", "type of analysis (trace|trace_before_cache|blocks|page|cache)", "trace");

	OptionalArgument<unsigned> cacheSizeArg(&args, "cache_size", "Cache sizes in kilobytes", 2048);
	OptionalArgument<unsigned> assocArg(&args, "cache_assoc", "Cache associativity", 16);
	OptionalArgument<unsigned> pageSize(&args, "page_size", "Page size", 4096);
	OptionalArgument<unsigned> blockSize(&args, "block_size", "Block size", 64);

	OptionalArgument<unsigned> cacheSizeStartArg(&args, "cache_size_start", "Start of cache sizes in kilobytes", 64);
	OptionalArgument<unsigned> cacheSizeEndArg(&args, "cache_size_end", "End of cache sizes in kilobytes", 524288);
	OptionalArgument<unsigned> blockSizeStartArg(&args, "block_size_start", "Start of block sizes", 64);
	OptionalArgument<unsigned> blockSizeEndArg(&args, "block_size_end", "End of block sizes", 64);


	OptionalArgument<uint64> period(&args, "period", "number of instructions between trace entries", 100000);



	if (args.parse(argc, argv)){
		args.usage(cerr);
		return -1;
	}

	if (type.getValue() == "trace"){
		StatContainer stats;
		CacheModel cache("Cache", "Cache", &stats, cacheSizeArg.getValue(), blockSize.getValue(), assocArg.getValue(), CACHE_LRU, pageSize.getValue());

		CompressedTraceReader reader(inputFile.getValue(), GZIP);
		Address address(pageSize.getValue(), blockSize.getValue());
		assert(address.numBlocks <= MAX_BITSET_SIZE);

//		FILE *trace = fopen(traceFile.getValue().c_str(), "w");
//		if (trace == 0){
//			error("Could not open file %s", traceFile.getValue().c_str());
//		}

		gzFile trace = gzopen(traceFile.getValue().c_str(), "w");
		if (trace == 0){
			error("Could not open file %s", traceFile.getValue().c_str());
		}

		set<addrint> unique;

		map<addrint, PageCounter> pages;
		uint64 icount = 0;

		TraceEntry entry;
		while(reader.readEntry(&entry)){
			if (entry.instr){
				icount++;
				if (icount % period.getValue() == 0){
					//fwrite(&icount, sizeof(uint64), 1, trace);
					gzwrite(trace, &icount, sizeof(uint64));
					uint32 size = pages.size();
					//fwrite(&size, sizeof(uint32), 1, trace);
					gzwrite(trace, &size, sizeof(uint32));
					//cout << icount << "\t" << pages.size() << "\t";
					for (map<addrint, PageCounter>::iterator it = pages.begin(); it != pages.end(); ++it){
						addrint page = it->first;
						uint32 reads = it->second.reads;
						uint32 writes = it->second.writes;
						uint8 readBlocks = it->second.readBlocks.count();
						uint8 writtenBlocks = it->second.writtenBlocks.count();
						uint8 accessedBlocks = (it->second.readBlocks | it->second.writtenBlocks).count();
//						fwrite(&page, sizeof(addrint), 1, trace);
//						fwrite(&reads, sizeof(uint32), 1, trace);
//						fwrite(&writes, sizeof(uint32), 1, trace);
//						fwrite(&readBlocks, sizeof(uint8), 1, trace);
//						fwrite(&writtenBlocks, sizeof(uint8), 1, trace);
//						fwrite(&accessedBlocks, sizeof(uint8), 1, trace);
						gzwrite(trace, &page, sizeof(addrint));
						gzwrite(trace, &reads, sizeof(uint32));
						gzwrite(trace, &writes, sizeof(uint32));
						gzwrite(trace, &readBlocks, sizeof(uint8));
						gzwrite(trace, &writtenBlocks, sizeof(uint8));
						gzwrite(trace, &accessedBlocks, sizeof(uint8));
						//cout << it->first << "\t" << it->second.reads << "\t" << it->second.writes << "\t" << it->second.readBlocks.count() << "\t" << it->second.writtenBlocks.count() << "\t" << (it->second.readBlocks | it->second.writtenBlocks).count() << "\t";
					}
					//cout << endl;
					pages.clear();
				}
			}


			addrint firstAddr = address.getAddr(entry.address);
			addrint secondAddr = address.getSecondAddr(entry.address, entry.size);
			addrint firstPage = address.getPageIndex(entry.address);
			addrint secondPage = address.getSecondPageIndex(entry.address, entry.size);
			addrint firstBlock = address.getBlockIndex(entry.address);
			addrint secondBlock = address.getSecondBlockIndex(entry.address, entry.size);

			unique.insert(firstPage);
			if (firstPage != secondPage){
				unique.insert(secondPage);
			}

			addrint evictedAddr;
			CacheModel::Result res = cache.access(firstAddr, entry.read, entry.instr, &evictedAddr, 0);
			if (res == CacheModel::HIT){

			} else if (res == CacheModel::MISS_WITHOUT_EVICTION || res == CacheModel::MISS_WITH_EVICTION){
				PageCounter &pc = pages[firstPage];
				pc.reads++;
				pc.readBlocks.set(firstBlock);
			} else  if(res == CacheModel::MISS_WITH_WRITEBACK){
				PageCounter &pc = pages[firstPage];
				pc.reads++;
				pc.readBlocks.set(firstBlock);

				addrint evictedPage = address.getPageIndex(evictedAddr);
				addrint evictedBlock = address.getBlockIndex(evictedAddr);
				PageCounter &pc2 = pages[evictedPage];
				pc2.writes++;
				pc2.writtenBlocks.set(evictedBlock);
			} else {
				assert(false);
			}

			if (firstAddr != secondAddr){
				res = cache.access(secondAddr, entry.read, entry.instr, &evictedAddr, 0);
				if (res == CacheModel::HIT){

				} else if (res == CacheModel::MISS_WITHOUT_EVICTION || res == CacheModel::MISS_WITH_EVICTION){
					PageCounter &pc = pages[secondPage];
					pc.reads++;
					pc.readBlocks.set(secondBlock);
				} else  if(res == CacheModel::MISS_WITH_WRITEBACK){
					PageCounter &pc = pages[secondPage];
					pc.reads++;
					pc.readBlocks.set(secondBlock);

					addrint evictedPage = address.getPageIndex(evictedAddr);
					addrint evictedBlock = address.getBlockIndex(evictedAddr);
					PageCounter &pc2 = pages[evictedPage];
					pc2.writes++;
					pc2.writtenBlocks.set(evictedBlock);
				} else {
					assert(false);
				}
			}

		}

		//fclose(trace);
		gzclose(trace);

		cout << unique.size() << endl;


	} else if (type.getValue() == "trace_before_cache"){
		CompressedTraceReader reader(inputFile.getValue(), GZIP);
		Address address(pageSize.getValue(), blockSize.getValue());
		assert(address.numBlocks <= MAX_BITSET_SIZE);

//		FILE *trace = fopen(traceFile.getValue().c_str(), "w");
//		if (trace == 0){
//			error("Could not open file %s", traceFile.getValue().c_str());
//		}

		gzFile trace = gzopen(traceFile.getValue().c_str(), "w");
		if (trace == 0){
			error("Could not open file %s", traceFile.getValue().c_str());
		}

		map<addrint, PageCounter> pages;
		uint64 icount = 0;

		TraceEntry entry;
		while(reader.readEntry(&entry)){
			if (entry.instr){
				icount++;
				if (icount % period.getValue() == 0){
					//fwrite(&icount, sizeof(uint64), 1, trace);
					gzwrite(trace, &icount, sizeof(uint64));
					uint32 size = pages.size();
					//fwrite(&size, sizeof(uint32), 1, trace);
					gzwrite(trace, &size, sizeof(uint32));
					cout << icount << "\t" << pages.size() << "\t";
					for (map<addrint, PageCounter>::iterator it = pages.begin(); it != pages.end(); ++it){
						addrint page = it->first;
						uint32 reads = it->second.reads;
						uint32 writes = it->second.writes;
						uint8 readBlocks = it->second.readBlocks.count();
						uint8 writtenBlocks = it->second.writtenBlocks.count();
						uint8 accessedBlocks = (it->second.readBlocks | it->second.writtenBlocks).count();
//						fwrite(&page, sizeof(addrint), 1, trace);
//						fwrite(&reads, sizeof(uint32), 1, trace);
//						fwrite(&writes, sizeof(uint32), 1, trace);
//						fwrite(&readBlocks, sizeof(uint8), 1, trace);
//						fwrite(&writtenBlocks, sizeof(uint8), 1, trace);
//						fwrite(&accessedBlocks, sizeof(uint8), 1, trace);
						gzwrite(trace, &page, sizeof(addrint));
						gzwrite(trace, &reads, sizeof(uint32));
						gzwrite(trace, &writes, sizeof(uint32));
						gzwrite(trace, &readBlocks, sizeof(uint8));
						gzwrite(trace, &writtenBlocks, sizeof(uint8));
						gzwrite(trace, &accessedBlocks, sizeof(uint8));
						cout << it->first << "\t" << it->second.reads << "\t" << it->second.writes << "\t" << it->second.readBlocks.count() << "\t" << it->second.writtenBlocks.count() << "\t" << (it->second.readBlocks | it->second.writtenBlocks).count() << "\t";
					}
					cout << endl;
					pages.clear();
				}
			}

			addrint firstPage = address.getPageIndex(entry.address);
			addrint secondPage = address.getSecondPageIndex(entry.address, entry.size);
			addrint firstBlock = address.getBlockIndex(entry.address);
			addrint secondBlock = address.getSecondBlockIndex(entry.address, entry.size);


			if (firstPage == secondPage){
				PageCounter &pc = pages[firstPage];
				if (entry.read){
					pc.reads++;
					pc.readBlocks.set(firstBlock);
					pc.readBlocks.set(secondBlock);
				} else {
					pc.writes++;
					pc.writtenBlocks.set(firstBlock);
					pc.writtenBlocks.set(secondBlock);
				}
			} else if (firstPage == secondPage - 1){
				if (firstBlock == secondBlock){
					error("Access covers two pages but only one block");
				}
				PageCounter &pc1 = pages[firstPage];
				if (entry.read){
					pc1.reads++;
					pc1.readBlocks.set(firstBlock);
				} else {
					pc1.writes++;
					pc1.writtenBlocks.set(firstBlock);
				}
				PageCounter &pc2 = pages[secondPage];
				if (entry.read){
					pc2.reads++;
					pc2.readBlocks.set(secondBlock);
				} else {
					pc2.writes++;
					pc2.writtenBlocks.set(secondBlock);
				}
			} else{
				error("Access covers more than two pages");
			}

		}

		//fclose(trace);
		gzclose(trace);

	} else if (type.getValue() == "blocks"){
		CompressedTraceReader reader(inputFile.getValue(), GZIP);
		Address address(pageSize.getValue(), blockSize.getValue());
		assert(address.numBlocks <= MAX_BITSET_SIZE);
		map<addrint, bitset<MAX_BITSET_SIZE> >pages;

		TraceEntry entry;
		while(reader.readEntry(&entry)){
			//cout << address.getBlockIndex(entry.address) << endl;
			pages[address.getPageIndex(entry.address)][address.getBlockIndex(entry.address)] = true;

		}

		map<unsigned, unsigned> hist;
		for (map<addrint, bitset<MAX_BITSET_SIZE> >::iterator it = pages.begin(); it != pages.end(); ++it){
			hist[it->second.count()]++;
		}

		for (map<unsigned, unsigned>::iterator it = hist.begin(); it != hist.end(); ++it){

		}

		if (statsFile.getValue().empty()){
			for (unsigned i = 1; i <= address.numBlocks; i++){
				cout << "#Number of pages with " << i << " blocks" << endl;
				cout << "pages_with_blocks_" << i << " " << hist[i] << endl << endl;
			}
		} else {
			ofstream out(statsFile.getValue().c_str());
			for (unsigned i = 1; i <= address.numBlocks; i++){
				out << "#Number of pages with " << i << " blocks" << endl;
				out << "pages_with_blocks_" << i << " " << hist[i] << endl << endl;
			}
			out.close();
		}
	} else if (type.getValue() == "page"){
		CompressedTraceReader reader(inputFile.getValue(), GZIP);
		Address address(pageSize.getValue(), blockSize.getValue());
		map<addrint, tuple<uint64, uint64, uint64> >pages;

		TraceEntry entry;
		while(reader.readEntry(&entry)){
			//cout << address.getBlockIndex(entry.address) << endl;
			if (entry.instr){
				get<0>(pages[address.getPageIndex(entry.address)])++;
			} else {
				if (entry.read){
					get<1>(pages[address.getPageIndex(entry.address)])++;
				} else {
					get<2>(pages[address.getPageIndex(entry.address)])++;
				}
			}

		}


		if (statsFile.getValue().empty()){
			cout << "#page\tinstr\tdataReads\tdataWrites" << endl;
			for (map<addrint, tuple<uint64, uint64, uint64> >::iterator it = pages.begin(); it != pages.end(); ++it){
				cout << it->first << "\t" << get<0>(it->second) << "\t" << get<1>(it->second) << "\t "<< get<2>(it->second) << endl;
			}
		} else {
			ofstream out(statsFile.getValue().c_str());
			out << "#page\tinstr\tdataReads\tdataWrites" << endl;
			for (map<addrint, tuple<uint64, uint64, uint64> >::iterator it = pages.begin(); it != pages.end(); ++it){
				out << it->first << "\t" << get<0>(it->second) << "\t" << get<1>(it->second) << "\t "<< get<2>(it->second) << endl;
			}
			out.close();
		}


	} else if (type.getValue() == "cache"){

		CompressedTraceReader reader(inputFile.getValue(), GZIP);
		StatContainer stats;
		map<uint64, map<uint64, CacheModel*> > caches;
		map<uint64, unsigned> offsetMask;
		set<uint64> times;

		uint64 cacheSizeStart = cacheSizeStartArg.getValue()*1024;
		uint64 cacheSizeEnd = cacheSizeEndArg.getValue()*1024;
		uint64 blockSizeStart = blockSizeStartArg.getValue();
		uint64 blockSizeEnd = blockSizeEndArg.getValue();
		unsigned assoc = assocArg.getValue();

		for (uint64 s = cacheSizeStart; s <= cacheSizeEnd; s *= 2){
			for (uint64 b = blockSizeStart; b <= blockSizeEnd; b *= 2){
				ostringstream ossName, ossDesc;
				if (s < 1024*1024){
					ossName << "cache_size_" << (s/1024) << "K_block_size_" << b;
					ossDesc << "Cache size: " << (s/1024) << "K Block size: " << b;
				} else {
					ossName << "cache_size_" << (s/1024/1024) << "M_block_size_" << b;
					ossDesc << "Cache size: " << (s/1024/1024) << "M Block size: " << b;
				}
				caches[s][b] = new CacheModel(ossName.str(), ossDesc.str(), &stats, s, b, assoc, CACHE_LRU, pageSize.getValue());
			}
		}

		for (uint64 b = blockSizeStart; b <= blockSizeEnd; b *= 2){
			unsigned logBlockSize = (unsigned) logb(b);
			unsigned blockSize = 1 << logBlockSize;
			unsigned offsetWidth = (int) logb(blockSize);
			offsetMask[b] = 0;
			for (unsigned i = 0; i < offsetWidth; i++) {
				offsetMask[b] |= (addrint) 1U << i;
			}
		}

		TraceEntry entry;
		while(reader.readEntry(&entry)){
			times.insert(entry.timestamp);
			uint64 evictedAddr, internalAddr;
			for (uint64 s = cacheSizeStart; s <= cacheSizeEnd; s *= 2){
				for (uint64 b = blockSizeStart; b <= blockSizeEnd; b *= 2){
					addrint firstByteBlockAddress = entry.address & ~offsetMask[b];
					addrint lastByteBlockAddress = (entry.address + entry.size - 1) & ~offsetMask[b];
					if (firstByteBlockAddress == lastByteBlockAddress){
						caches[s][b]->access(firstByteBlockAddress, entry.read, entry.instr, &evictedAddr, &internalAddr);
					} else if (firstByteBlockAddress + b == lastByteBlockAddress){
						caches[s][b]->access(firstByteBlockAddress, entry.read, entry.instr, &evictedAddr, &internalAddr);
						caches[s][b]->access(lastByteBlockAddress, entry.read, entry.instr, &evictedAddr, &internalAddr);
					} else {
						error("Access covers more than one cache block");
					}


				}
			}
		}

		if (statsFile.getValue().empty()){
			stats.print(cout);
			cout << "#Number of distinct timestamps" << endl;
			cout << "distinct_timestamps" << times.size() << endl;
		} else {
			ofstream out(statsFile.getValue().c_str());
			stats.print(out);
			out << "#Number of distinct timestamps" << endl;
			out << "distinct_timestamps " << times.size() << endl;
			out.close();
		}

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
