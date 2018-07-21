//=====================================================================
// @file 	Memory.h
//
// @brief 	Implements a simple template class loigc combinational memory module.
//
//=====================================================================
//  Original Authors:
//    Alonso Loaiza
//=====================================================================

#ifndef TLM_CUBE_MEMORY_H_
#define TLM_CUBE_MEMORY_H_

#include <systemc.h>
#include <algorithm>

template<unsigned int MemSize>
class Memory : public sc_module
{
public:
	sc_in<bool> WriteFlag;
	sc_in<int> Address;
	sc_in<int> DataIn;
	sc_out<int> DataOut;
	sc_out<bool> ErrorFlag;

	void ReadOrWrite()
	{
		if(Address.read() < MemSize)
		{
			//Reset flags
			ErrorFlag.write(false);
			if(WriteFlag.read())
			{
				MemoryData[Address.read()] = DataIn.read();
			}
			else
			{
				DataOut.write(MemoryData[Address.read()]);
			}
		}
		else
		{
			ErrorFlag.write(true);
		}
	}

	SC_CTOR(Memory)
	{
		// Initialize the memory
		std::fill(MemoryData, MemoryData + MemSize, 0);

		SC_METHOD( ReadOrWrite );
			sensitive << WriteFlag;
			sensitive << Address;
			sensitive << DataIn;
	}

private:
	int MemoryData[MemSize];
};

#endif