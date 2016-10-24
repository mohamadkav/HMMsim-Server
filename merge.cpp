/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

/*
 * This program decompreses and merges a trace from multiple files into one file.
 */

#include "Arguments.H"
#include "TraceHandler.H"


int main(int argc, char * argv[]){

	ArgumentContainer args("merge", false);
	PositionalArgument<string> tracePrefix(&args, "trace_prefix", "trace prefix", "");
	PositionalArgument<string> outputFile(&args, "output_file", "output file", "");
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

	CompressedTraceReader reader(tracePrefix.getValue(), comp);
	TraceWriter writer(outputFile.getValue());

	TraceEntry entry;
	while(reader.readEntry(&entry)){
		writer.writeEntry(&entry);
	}

	return 0;

}
