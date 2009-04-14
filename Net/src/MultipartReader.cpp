//
// MultipartReader.cpp
//
// $Id: //poco/Main/Net/src/MultipartReader.cpp#13 $
//
// Library: Net
// Package: Messages
// Module:  MultipartReader
//
// Copyright (c) 2005-2006, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// Permission is hereby granted, free of charge, to any person or organization
// obtaining a copy of the software and accompanying documentation covered by
// this license (the "Software") to use, reproduce, display, distribute,
// execute, and transmit the Software, and to prepare derivative works of the
// Software, and to permit third-parties to whom the Software is furnished to
// do so, all subject to the following:
// 
// The copyright notices in the Software and this entire statement, including
// the above license grant, this restriction and the following disclaimer,
// must be included in all copies of the Software, in whole or in part, and
// all derivative works of the Software, unless such copies or derivative
// works are solely in the form of machine-executable object code generated by
// a source language processor.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//


#include "Poco/Net/MultipartReader.h"
#include "Poco/Net/MessageHeader.h"
#include "Poco/Net/NetException.h"
#include <cctype>


using Poco::BufferedStreamBuf;


namespace Poco {
namespace Net {


//
// MultipartStreamBuf
//


MultipartStreamBuf::MultipartStreamBuf(std::istream& istr, const std::string& boundary):
	BufferedStreamBuf(STREAM_BUFFER_SIZE, std::ios::in),
	_istr(istr),
	_boundary(boundary),
	_lastPart(false)
{
	poco_assert (!boundary.empty() && boundary.length() < STREAM_BUFFER_SIZE - 6);
}


MultipartStreamBuf::~MultipartStreamBuf()
{
}


int MultipartStreamBuf::readFromDevice(char* buffer, std::streamsize length)
{
	poco_assert_dbg (length >= _boundary.length() + 6);

	static const int eof = std::char_traits<char>::eof();
	int n  = 0;
	int ch = _istr.get();
	if (ch == eof) return -1;
	*buffer++ = (char) ch; ++n;
	if (ch == '\n' || (ch == '\r' && _istr.peek() == '\n'))
	{
		if (ch == '\r')
		{
			ch = _istr.get(); // '\n'
			*buffer++ = (char) ch; ++n;
		}
		ch = _istr.peek();
		if (ch == '\r' || ch == '\n') return n;
		*buffer++ = (char) _istr.get(); ++n;
		if (ch == '-' && _istr.peek() == '-')
		{
			ch = _istr.get(); // '-'
			*buffer++ = (char) ch; ++n;
			std::string::const_iterator it  = _boundary.begin();
			std::string::const_iterator end = _boundary.end();
			ch = _istr.get();
			*buffer++ = (char) ch; ++n;
			while (it != end && ch == *it)
			{
				++it;
				ch = _istr.get();
				*buffer++ = (char) ch; ++n;
			}
			if (it == end)
			{
				if (ch == '\n' || (ch == '\r' && _istr.peek() == '\n'))
				{
					if (ch == '\r')
					{
						ch = _istr.get(); // '\n'
					}
					return 0;					
				}
				else if (ch == '-' && _istr.peek() == '-')
				{
					ch = _istr.get(); // '-'
					_lastPart = true;
					return 0;
				}
			}
		}
	}
	ch = _istr.peek();
	while (ch != eof && ch != '\r' && ch != '\n' && n < length)
	{
		*buffer++ = (char) _istr.get(); ++n;
		ch = _istr.peek();
	}
	return n;
}


bool MultipartStreamBuf::lastPart() const
{
	return _lastPart;
}


//
// MultipartIOS
//


MultipartIOS::MultipartIOS(std::istream& istr, const std::string& boundary):
	_buf(istr, boundary)
{
	poco_ios_init(&_buf);
}


MultipartIOS::~MultipartIOS()
{
	_buf.sync();
}


MultipartStreamBuf* MultipartIOS::rdbuf()
{
	return &_buf;
}


bool MultipartIOS::lastPart() const
{
	return _buf.lastPart();
}


//
// MultipartInputStream
//


MultipartInputStream::MultipartInputStream(std::istream& istr, const std::string& boundary):
	MultipartIOS(istr, boundary),
	std::istream(&_buf)
{
}


MultipartInputStream::~MultipartInputStream()
{
}


//
// MultipartReader
//


MultipartReader::MultipartReader(std::istream& istr):
	_istr(istr),
	_pMPI(0)
{
}


MultipartReader::MultipartReader(std::istream& istr, const std::string& boundary):
	_istr(istr),
	_boundary(boundary),
	_pMPI(0)
{
}


MultipartReader::~MultipartReader()
{
	delete _pMPI;
}


void MultipartReader::nextPart(MessageHeader& messageHeader)
{
	if (!_pMPI)
	{
		if (_boundary.empty())
			guessBoundary();
		else
			findFirstBoundary();
	}
	else if (_pMPI->lastPart())
	{
		throw MultipartException("No more parts available");
	}
	parseHeader(messageHeader);
	delete _pMPI;
	_pMPI = new MultipartInputStream(_istr, _boundary);
}


bool MultipartReader::hasNextPart()
{
	return (!_pMPI || !_pMPI->lastPart()) && _istr.good();
}

	
std::istream& MultipartReader::stream() const
{
	poco_check_ptr (_pMPI);
	
	return *_pMPI;
}


const std::string& MultipartReader::boundary() const
{
	return _boundary;
}


void MultipartReader::findFirstBoundary()
{
	std::string expect("--");
	expect.append(_boundary);
	std::string line;
	line.reserve(expect.length());
	bool ok = true;
	do
	{
		ok = readLine(line, expect.length());
	}
	while (ok && line != expect);

	if (!ok) throw MultipartException("No boundary line found");
}


void MultipartReader::guessBoundary()
{
	static const int eof = std::char_traits<char>::eof();
	int ch = _istr.get();
	while (std::isspace(ch))
		ch = _istr.get();
	if (ch == '-' && _istr.peek() == '-')
	{
		_istr.get();
		ch = _istr.peek();
		while (ch != eof && ch != '\r' && ch != '\n')
		{
			_boundary += (char) _istr.get();
			ch = _istr.peek();
		}
		if (ch == '\r' || ch == '\n')
			ch = _istr.get();
		if (_istr.peek() == '\n')
			_istr.get();
	}
	else throw MultipartException("No boundary line found");
}


void MultipartReader::parseHeader(MessageHeader& messageHeader)
{
	messageHeader.clear();
	messageHeader.read(_istr);
	int ch = _istr.get();
	if (ch == '\r' && _istr.peek() == '\n') ch = _istr.get();
}


bool MultipartReader::readLine(std::string& line, std::string::size_type n)
{
	static const int eof = std::char_traits<char>::eof();

	line.clear();
	int ch = _istr.peek();
	while (ch != eof && ch != '\r' && ch != '\n')
	{
		ch = (char) _istr.get();
		if (line.length() < n) line += ch;
		ch = _istr.peek();
	}
	if (ch != eof) _istr.get();
	if (ch == '\r' && _istr.peek() == '\n') _istr.get();
	return ch != eof;
}


} } // namespace Poco::Net
