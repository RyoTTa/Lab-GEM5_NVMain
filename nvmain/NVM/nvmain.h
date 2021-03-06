/*******************************************************************************
* Copyright (c) 2012-2014, The Microsystems Design Labratory (MDL)
* Department of Computer Science and Engineering, The Pennsylvania State University
* All rights reserved.
* 
* This source code is part of NVMain - A cycle accurate timing, bit accurate
* energy simulator for both volatile (e.g., DRAM) and non-volatile memory
* (e.g., PCRAM). The source code is free and you can redistribute and/or
* modify it by providing that the following conditions are met:
* 
*  1) Redistributions of source code must retain the above copyright notice,
*     this list of conditions and the following disclaimer.
* 
*  2) Redistributions in binary form must reproduce the above copyright notice,
*     this list of conditions and the following disclaimer in the documentation
*     and/or other materials provided with the distribution.
* 
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
* 
* Author list: 
*   Matt Poremba    ( Email: mrp5060 at psu dot edu 
*                     Website: http://www.cse.psu.edu/~poremba/ )
*******************************************************************************/

#ifndef __NVMAIN_H__
#define __NVMAIN_H__

#include <iostream>
#include <fstream>
#include <stdint.h>
#include "src/Params.h"
#include "src/NVMObject.h"
#include "src/Prefetcher.h"
#include "include/NVMainRequest.h"
#include "traceWriter/GenericTraceWriter.h"
#include <queue>

namespace NVM {

class Config;
class MemoryController;
class MemoryControllerManager;
class Interconnect;
class AddressTranslator;
class SimInterface;
class NVMainRequest;

class NVMain : public NVMObject
{
  public:
    NVMain( );
    ~NVMain( );

    void SetConfig( Config *conf, std::string memoryName = "defaultMemory", bool createChildren = true );

    Config *GetConfig( );

    void IssuePrefetch( NVMainRequest *request );
    bool IssueCommand( NVMainRequest *request );
    uint32_t BDI(uint8_t *cacheline, uint32_t data_size);
    uint32_t getDataSize(uint64_t data);
    bool IssueAtomic( NVMainRequest *request );
    bool IsIssuable( NVMainRequest *request, FailReason *reason );
    uint64_t GetUpdateBitNum(uint8_t *flipcacheline, uint8_t granulatiry, uint8_t size);
    uint64_t GetUpdateBitNum_Merge(uint8_t *flipcacheline, uint8_t granulatiry, uint8_t columnUpdateNum, uint8_t vector_Num, uint8_t size);

    bool RequestComplete( NVMainRequest *request );

    bool CheckPrefetch( NVMainRequest *request );

    void RegisterStats( );
    void CalculateStats( );

    void Cycle( ncycle_t steps );

    void EnqueuePendingMemoryRequests( NVMainRequest *request );

  private:
    Config *config;
    Config **channelConfig;
    MemoryController **memoryControllers;
    AddressTranslator *translator;

    ncounter_t totalReadRequests;
    ncounter_t totalWriteRequests;
    ncounter_t successfulPrefetches;
    ncounter_t unsuccessfulPrefetches;
    ncounter_t updateColumns[9];
    ncounter_t updateBit[64];
    ncounter_t compressByte[7];
    ncounter_t ReadModifiedUpdateBit;
    ncounter_t VectorUpdateBit;
    ncounter_t CompressUpdateBit;

    unsigned int numChannels;
    double syncValue;

    Prefetcher *prefetcher;
    std::list<NVMainRequest *> prefetchBuffer;
    std::queue<NVMainRequest *> pendingMemoryRequests;

    std::ofstream pretraceOutput;
    GenericTraceWriter *preTracer;

    void PrintPreTrace( NVMainRequest *request );
    void GeneratePrefetches( NVMainRequest *request, std::vector<NVMAddress>& prefetchList );

    bool do_verify = true;
};

};

#endif
