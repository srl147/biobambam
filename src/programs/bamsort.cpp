/**
    bambam
    Copyright (C) 2009-2013 German Tischler
    Copyright (C) 2011-2013 Genome Research Limited

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**/
#include "config.h"

#include <iostream>
#include <queue>
#include <libmaus/util/ArgInfo.hpp>
#include <libmaus/bambam/BamAlignment.hpp>
#include <libmaus/bambam/BamDecoder.hpp>
#include <libmaus/util/PutObject.hpp>
#include <libmaus/util/GetObject.hpp>
#include <libmaus/aio/CheckedOutputStream.hpp>
#include <libmaus/util/TempFileRemovalContainer.hpp>
#include <libmaus/lz/SnappyCompress.hpp>
#include <libmaus/bambam/BamWriter.hpp>
#include <libmaus/bambam/ProgramHeaderLineSet.hpp>
#include <libmaus/bambam/BamEntryContainer.hpp>
#include <libmaus/bambam/BamAlignmentPosComparator.hpp>
#include <libmaus/bambam/BamAlignmentNameComparator.hpp>
#include <biobambam/Licensing.hpp>

static int getDefaultLevel() { return Z_DEFAULT_COMPRESSION; }
static int getDefaultVerbose() { return 1; }
static std::string getDefaultSortOrder() { return "coordinate"; }
static uint64_t getDefaultBlockSize() { return 1024; }

int bamsort(::libmaus::util::ArgInfo const & arginfo)
{
	::libmaus::util::TempFileRemovalContainer::setup();

	if ( isatty(STDIN_FILENO) )
	{
		::libmaus::exception::LibMausException se;
		se.getStream() << "Refusing to read binary data from terminal, please redirect standard input to pipe or file." << std::endl;
		se.finish();
		throw se;
	}

	if ( isatty(STDOUT_FILENO) )
	{
		::libmaus::exception::LibMausException se;
		se.getStream() << "Refusing write binary data to terminal, please redirect standard output to pipe or file." << std::endl;
		se.finish();
		throw se;
	}

	int const level = arginfo.getValue<int>("level",getDefaultLevel());
	int const verbose = arginfo.getValue<int>("verbose",getDefaultVerbose());
	
	switch ( level )
	{
		case Z_NO_COMPRESSION:
		case Z_BEST_SPEED:
		case Z_BEST_COMPRESSION:
		case Z_DEFAULT_COMPRESSION:
			break;
		default:
		{
			::libmaus::exception::LibMausException se;
			se.getStream()
				<< "Unknown compression level, please use"
				<< " level=" << Z_DEFAULT_COMPRESSION << " (default) or"
				<< " level=" << Z_BEST_SPEED << " (fast) or"
				<< " level=" << Z_BEST_COMPRESSION << " (best) or"
				<< " level=" << Z_NO_COMPRESSION << " (no compression)" << std::endl;
			se.finish();
			throw se;
		}
			break;
	}

	// prefix for tmp files
	std::string const tmpfilenamebase = arginfo.getValue<std::string>("tmpfile",arginfo.getDefaultTmpFileName());
	std::string const tmpfilenameout = tmpfilenamebase + "_bamsort";
	::libmaus::util::TempFileRemovalContainer::addTempFile(tmpfilenameout);
	uint64_t blockmem = arginfo.getValue<uint64_t>("blockmb",getDefaultBlockSize())*1024*1024;
	std::string const sortorder = arginfo.getValue<std::string>("SO","coordinate");

	::libmaus::bambam::BamDecoder dec(std::cin,false);
	::libmaus::bambam::BamHeader const & header = dec.bamheader;

	std::string const headertext(header.text);

	// add PG line to header
	std::string const upheadtext = ::libmaus::bambam::ProgramHeaderLineSet::addProgramLine(
		headertext,
		"bamsort", // ID
		"bamsort", // PN
		arginfo.commandline, // CL
		::libmaus::bambam::ProgramHeaderLineSet(headertext).getLastIdInChain(), // PP
		std::string(PACKAGE_VERSION) // VN			
	);
	// construct new header
	::libmaus::bambam::BamHeader uphead(upheadtext);
	
	if ( sortorder != "queryname" )
	{
		uphead.changeSortOrder("coordinate");
		::libmaus::bambam::BamEntryContainer< ::libmaus::bambam::BamAlignmentPosComparator > BEC(blockmem,tmpfilenameout);

		if ( verbose )
			std::cerr << "[V] Reading alignments from source." << std::endl;
		uint64_t incnt = 0;
		
		while ( dec.readAlignment() )
		{
			BEC.putAlignment(dec.alignment);
			incnt++;
			if ( verbose && (incnt % (1024*1024) == 0) )
				std::cerr << "[V] " << incnt/(1024*1024) << "M" << std::endl;
		}

		if ( verbose )
			std::cerr << "[V] read " << incnt << " alignments" << std::endl;

		BEC.createOutput(std::cout, uphead, level, verbose);
	}
	else
	{
		uphead.changeSortOrder("queryname");
		::libmaus::bambam::BamEntryContainer< ::libmaus::bambam::BamAlignmentNameComparator > BEC(blockmem,tmpfilenameout);
		
		if ( verbose )
			std::cerr << "[V] Reading alignments from source." << std::endl;
		uint64_t incnt = 0;
		
		while ( dec.readAlignment() )
		{
			BEC.putAlignment(dec.alignment);
			incnt++;
			if ( verbose && (incnt % (1024*1024) == 0) )
				std::cerr << "[V] " << incnt/(1024*1024) << "M" << std::endl;
		}
		
		if ( verbose )
			std::cerr << "[V] read " << incnt << " alignments" << std::endl;

		BEC.createOutput(std::cout, uphead, level, verbose);
	}

	return EXIT_SUCCESS;
}

int main(int argc, char * argv[])
{
	try
	{
		::libmaus::util::ArgInfo const arginfo(argc,argv);
		
		for ( uint64_t i = 0; i < arginfo.restargs.size(); ++i )
			if ( 
				arginfo.restargs[i] == "-v"
				||
				arginfo.restargs[i] == "--version"
			)
			{
				std::cerr << ::biobambam::Licensing::license();
				return EXIT_SUCCESS;
			}
			else if ( 
				arginfo.restargs[i] == "-h"
				||
				arginfo.restargs[i] == "--help"
			)
			{
				std::cerr << ::biobambam::Licensing::license();
				std::cerr << std::endl;
				std::cerr << "Key=Value pairs:" << std::endl;
				std::cerr << std::endl;
				
				std::vector< std::pair<std::string,std::string> > V;
			
				V.push_back ( std::pair<std::string,std::string> ( "level=<["+::biobambam::Licensing::formatNumber(getDefaultLevel())+"]>", "compression settings for output bam file (0=uncompressed,1=fast,9=best,-1=zlib default)" ) );
				V.push_back ( std::pair<std::string,std::string> ( "SO=<["+getDefaultSortOrder()+"]>", "sorting order (coordinate or queryname)" ) );
				V.push_back ( std::pair<std::string,std::string> ( "verbose=<["+::biobambam::Licensing::formatNumber(getDefaultVerbose())+"]>", "print progress report" ) );
				V.push_back ( std::pair<std::string,std::string> ( "blockmb=<["+::biobambam::Licensing::formatNumber(getDefaultBlockSize())+"]>", "size of internal memory buffer used for sorting in MiB" ) );
				V.push_back ( std::pair<std::string,std::string> ( "tmpfile=<filename>", "prefix for temporary files, default: create files in current directory" ) );

				::biobambam::Licensing::printMap(std::cerr,V);

				std::cerr << std::endl;
				return EXIT_SUCCESS;
			}
			
		return bamsort(arginfo);
	}
	catch(std::exception const & ex)
	{
		std::cerr << ex.what() << std::endl;
		return EXIT_FAILURE;
	}
}

