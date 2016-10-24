
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
	//char * line = gzgets(reader, buffer, 1024);
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
//			entry.size = 4;
			if (rw == 'R'){
				entry.read = true;
			} else {
				entry.read = false;
			}
			if(dec == 'D')
				entry.instr = false;
			else
				entry.instr = true;

			entry.size -= 48;
			//cout << entry.timestamp << " " <<  entry.address << "" << entry.size << " " << entry.read << " " << entry.instr << endl;
			writer.writeEntry(&entry);

//			string lineStr(line);
//			vector<string> tokens;
//			size_t current;
//			size_t next = -1;
//			do {
//				current = next + 1;
//				next = lineStr.find_first_of("\t", current);
//				tokens.emplace_back(lineStr.substr(current, next - current));
//			} while (next != string::npos);
//			entry.timestamp = atol(tokens[3].c_str());
//			entry.address = tokens[1];
//			entry.size = 4;
//			if (tokens[2] == "R"){
//				entry.read = true;
//			} else {
//				entry.read = false;
//			}
//			entry.instr = false;
		}
	}

	gzclose(reader);

	return 0;

}
