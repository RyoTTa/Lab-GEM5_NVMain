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

#include "nvmain.h"
#include "src/Config.h"
#include "src/AddressTranslator.h"
#include "src/Interconnect.h"
#include "src/SimInterface.h"
#include "src/EventQueue.h"
#include "Interconnect/InterconnectFactory.h"
#include "MemControl/MemoryControllerFactory.h"
#include "traceWriter/TraceWriterFactory.h"
#include "Decoders/DecoderFactory.h"
#include "Utils/HookFactory.h"
#include "include/NVMainRequest.h"
#include "include/NVMHelpers.h"
#include "Prefetchers/PrefetcherFactory.h"

#include <sstream>
#include <cassert>

using namespace NVM;

NVMain::NVMain( )
{
    config = NULL;
    translator = NULL;
    memoryControllers = NULL;
    channelConfig = NULL;
    syncValue = 0.0f;
    preTracer = NULL;

    totalReadRequests = 0;
    totalWriteRequests = 0;

    prefetcher = NULL;
    successfulPrefetches = 0;
    unsuccessfulPrefetches = 0;
    for(int i= 0 ; i<9 ; i++){
        updateColumns[i] = 0;
    }
    for(int i= 0 ; i< 64; i++){
        updateBit[i] = 0;
    }
    for(int i= 0 ; i< 5; i++){
        compressByte[i] = 0;
    }
    ReadModifiedUpdateBit = 0;
    VectorUpdateBit = 0;
    CompressUpdateBit = 0;
}

NVMain::~NVMain( )
{
    if( config ) 
        delete config;
    
    if( memoryControllers )
    {
        for( unsigned int i = 0; i < numChannels; i++ )
        {
            if( memoryControllers[i] )
                delete memoryControllers[i];
        }

        delete [] memoryControllers;
    }

    if( translator )
        delete translator;

    if( channelConfig )
    {
        for( unsigned int i = 0; i < numChannels; i++ )
        {
            delete channelConfig[i];
        }

        delete [] channelConfig;
    }
}

Config *NVMain::GetConfig( )
{
    return config;
}

void NVMain::SetConfig( Config *conf, std::string memoryName, bool createChildren )
{
    TranslationMethod *method;
    int channels, ranks, banks, rows, cols, subarrays;

    Params *params = new Params( );
    params->SetParams( conf );
    SetParams( params );

    StatName( memoryName );

    config = conf;
    if( config->GetSimInterface( ) != NULL )
        config->GetSimInterface( )->SetConfig( conf, createChildren );
    else
      std::cout << "Warning: Sim Interface should be allocated before configuration!" << std::endl;


    if( createChildren )
    {
        if( conf->KeyExists( "MATHeight" ) )
        {
            rows = static_cast<int>(p->MATHeight);
            subarrays = static_cast<int>( p->ROWS / p->MATHeight );
        }
        else
        {
            rows = static_cast<int>(p->ROWS);
            subarrays = 1;
        }
        cols = static_cast<int>(p->COLS);
        banks = static_cast<int>(p->BANKS);
        ranks = static_cast<int>(p->RANKS);
        channels = static_cast<int>(p->CHANNELS);

        if( config->KeyExists( "Decoder" ) )
            translator = DecoderFactory::CreateNewDecoder( config->GetString( "Decoder" ) );
        else
            translator = new AddressTranslator( );

        method = new TranslationMethod( );

        method->SetBitWidths( NVM::mlog2( rows ), 
                    NVM::mlog2( cols ), 
                    NVM::mlog2( banks ), 
                    NVM::mlog2( ranks ), 
                    NVM::mlog2( channels ), 
                    NVM::mlog2( subarrays )
                    );
        method->SetCount( rows, cols, banks, ranks, channels, subarrays );
        method->SetAddressMappingScheme( p->AddressMappingScheme );
        translator->SetConfig( config, createChildren );
        translator->SetTranslationMethod( method );
        translator->SetDefaultField( CHANNEL_FIELD );

        SetDecoder( translator );

        memoryControllers = new MemoryController* [channels];
        channelConfig = new Config* [channels];
        for( int i = 0; i < channels; i++ )
        {
            std::stringstream confString;
            std::string channelConfigFile;

            channelConfig[i] = new Config( *config );

            channelConfig[i]->SetSimInterface( config->GetSimInterface( ) );

            confString << "CONFIG_CHANNEL" << i;

            if( config->GetString( confString.str( ) ) != "" )
            {
                channelConfigFile  = config->GetString( confString.str( ) );

                if( channelConfigFile[0] != '/' )
                {
                    channelConfigFile  = NVM::GetFilePath( config->GetFileName( ) );
                    channelConfigFile += config->GetString( confString.str( ) );
                }
                
                std::cout << "Reading channel config file: " << channelConfigFile << std::endl;

                channelConfig[i]->Read( channelConfigFile );
            }

            /* Initialize memory controller */
            memoryControllers[i] = 
                MemoryControllerFactory::CreateNewController( channelConfig[i]->GetString( "MEM_CTL" ) );

            /* When selecting a MC child, use no field from the decoder (only-child). */

            confString.str( "" );
            confString << memoryName << ".channel" << i << "." 
                << channelConfig[i]->GetString( "MEM_CTL" ); 
            memoryControllers[i]->StatName( confString.str( ) );
            memoryControllers[i]->SetID( i );

            AddChild( memoryControllers[i] );
            memoryControllers[i]->SetParent( this );

            /* Set Config recursively. */
            memoryControllers[i]->SetConfig( channelConfig[i], createChildren );

            /* Register statistics. */
            memoryControllers[i]->RegisterStats( );
        }

    }

    if( p->MemoryPrefetcher != "none" )
    {
        prefetcher = PrefetcherFactory::CreateNewPrefetcher( p->MemoryPrefetcher );
        std::cout << "Made a " << p->MemoryPrefetcher << " prefetcher." << std::endl;
    }

    numChannels = static_cast<unsigned int>(p->CHANNELS);
    
    std::string pretraceFile;

    if( p->PrintPreTrace || p->EchoPreTrace )
    {
        if( config->GetString( "PreTraceFile" ) == "" )
            pretraceFile = "trace.nvt";
        else
            pretraceFile = config->GetString( "PreTraceFile" );

        if( pretraceFile[0] != '/' )
        {
            pretraceFile  = NVM::GetFilePath( config->GetFileName( ) );
            pretraceFile += config->GetString( "PreTraceFile" );
        }

        std::cout << "Using trace file " << pretraceFile << std::endl;

        if( config->GetString( "PreTraceWriter" ) == "" )
            preTracer = TraceWriterFactory::CreateNewTraceWriter( "NVMainTrace" );
        else
            preTracer = TraceWriterFactory::CreateNewTraceWriter( config->GetString( "PreTraceWriter" ) );

        if( p->PrintPreTrace )
            preTracer->SetTraceFile( pretraceFile );
        if( p->EchoPreTrace )
            preTracer->SetEcho( true );
    }

    RegisterStats( );
}

bool NVMain::IsIssuable( NVMainRequest *request, FailReason *reason )
{
    uint64_t channel, rank, bank, row, col, subarray;
    bool rv;

    assert( request != NULL );

    GetDecoder( )->Translate( request->address.GetPhysicalAddress( ), 
                           &row, &col, &rank, &bank, &channel, &subarray );

    rv = memoryControllers[channel]->IsIssuable( request, reason );

    return rv;
}

void NVMain::GeneratePrefetches( NVMainRequest *request, std::vector<NVMAddress>& prefetchList )
{
    std::vector<NVMAddress>::iterator iter;
    ncounter_t channel, rank, bank, row, col, subarray;

    for( iter = prefetchList.begin(); iter != prefetchList.end(); iter++ )
    {
        /* Make a request from the prefetch address. */
        NVMainRequest *pfRequest = new NVMainRequest( );
        *pfRequest = *request;
        pfRequest->address = (*iter);
        pfRequest->isPrefetch = true;
        pfRequest->owner = this;
        
        /* Translate the address, then copy to the address struct, and copy to request. */
        GetDecoder( )->Translate( request->address.GetPhysicalAddress( ), 
                               &row, &col, &bank, &rank, &channel, &subarray );
        request->address.SetTranslatedAddress( row, col, bank, rank, channel, subarray );
        request->bulkCmd = CMD_NOP;

        //std::cout << "Prefetching 0x" << std::hex << (*iter).GetPhysicalAddress() << " (trigger 0x"
        //          << request->address.GetPhysicalAddress( ) << std::dec << std::endl;

        /* Just type to issue; If the queue is full it simply won't be enqueued. */
        GetChild( pfRequest )->IssueCommand( pfRequest );
    }
}

void NVMain::IssuePrefetch( NVMainRequest *request )
{
    /* 
     *  Generate prefetches here. It makes the most sense to prefetch in this class
     *  since we may prefetch across channels. However, this may be applicable to any
     *  class in the hierarchy as long as we filter out the prefetch addresses that
     *  do not belong to a child of the current module.
     */
    /* TODO: We are assuming this is the master root! */
    std::vector<NVMAddress> prefetchList;
    if( prefetcher && request->type == READ && request->isPrefetch == false 
        && prefetcher->DoPrefetch(request, prefetchList) )
    {
        GeneratePrefetches( request, prefetchList );
    }
}

bool NVMain::CheckPrefetch( NVMainRequest *request )
{
    bool rv = false;
    NVMainRequest *pfRequest = NULL;
    std::list<NVMainRequest *>::const_iterator iter;
    std::vector<NVMAddress> prefetchList;

    for( iter = prefetchBuffer.begin(); iter!= prefetchBuffer.end(); iter++ )
    {
        if( (*iter)->address.GetPhysicalAddress() == request->address.GetPhysicalAddress() )
        {
            if( prefetcher->NotifyAccess(request, prefetchList) )
            {
                GeneratePrefetches( request, prefetchList );
            }

            successfulPrefetches++;
            rv = true;
            pfRequest = (*iter);
            delete pfRequest;
            break;
        }
    }

    if( pfRequest != NULL )
    {
        //std::cout << "Prefetched 0x" << std::hex << request->address.GetPhysicalAddress( )
        //          << std::dec << " (list size " << prefetchBuffer.size() << " -> ";
        prefetchBuffer.remove( pfRequest );
        //std::cout << prefetchBuffer.size() << ")" << std::endl;
    }

    return rv;
}

void NVMain::PrintPreTrace( NVMainRequest *request )
{
    /*
     *  Here we can generate a data trace to use with trace-based testing later.
     */
    if( p->PrintPreTrace || p->EchoPreTrace )
    {
        TraceLine tl;

        tl.SetLine( request->address,
                    request->type,
                    GetEventQueue( )->GetCurrentCycle( ),
                    request->data,
                    request->oldData,
                    request->threadId 
                  );

        preTracer->SetNextAccess( &tl );
    }
}

bool NVMain::IssueCommand( NVMainRequest *request )
{
    ncounter_t channel, rank, bank, row, col, subarray;
    bool mc_rv;

    if( !config )
    {
        std::cout << "NVMain: Received request before configuration!\n";
        return false;
    }

    /* Translate the address, then copy to the address struct, and copy to request. */
    GetDecoder( )->Translate( request->address.GetPhysicalAddress( ), 
                           &row, &col, &bank, &rank, &channel, &subarray );
    request->address.SetTranslatedAddress( row, col, bank, rank, channel, subarray );
    request->bulkCmd = CMD_NOP;

    /* Check for any successful prefetches. */
    if( CheckPrefetch( request ) )
    {
        GetEventQueue()->InsertEvent( EventResponse, this, request, 
                                      GetEventQueue()->GetCurrentCycle() + 1 );

        return true;
    }

    assert( GetChild( request )->GetTrampoline( ) == memoryControllers[channel] );
    mc_rv = GetChild( request )->IssueCommand( request );
    if( mc_rv == true )
    {
        IssuePrefetch( request );

        if( request->type == READ ) 
        {
            totalReadRequests++;
        }
        else
        {
            totalWriteRequests++;
            
            //Adding Part Start
            
            uint8_t *bitCountFlipData = new uint8_t[request->data.GetSize()];
            uint8_t *NewData = new uint8_t[request->data.GetSize()];

            int columnIndex = 0;
            int update_check = 0;
            int columnUpdateNum = 0;
            int temp_list[8] = {7,6,5,4,3,2,1,0};

            int columUpdateVector[8] = {0,};
            int columnVectorNum = 0;
            int bitUpdateVector[16] = {0,};
            int bitVectorNum_4=0;
            int bitVectorNum_8=0;

            uint64_t tempForReadModifiedUpdateBit = 0;
            uint64_t tempForVectorUpdateBit = 0;
            //uint64_t tempForCompressUpdateBit = 0;

            for( uint64_t bitCountByte = 0; bitCountByte < request->data.GetSize();){ //8개 Columns(64 Byte)
                update_check = 0;
                for( uint64_t ByteIndex = 0; ByteIndex<8; ByteIndex++){ //1개 Column (8 Byte)
                    bitCountFlipData[bitCountByte] = request->data.GetByte( bitCountByte ) ^ request->oldData.GetByte( bitCountByte );
                    NewData[bitCountByte] = request->data.GetByte( bitCountByte );
                    if(bitCountFlipData[bitCountByte] >= 1){
                        update_check++;
                        for(int8_t BitIndex = 7; BitIndex >= 0; BitIndex--){ //1개 Column 내부 1Byte
                            int8_t mask=0;
                            mask = 1 << temp_list[BitIndex];
                            if(bitCountFlipData[bitCountByte] & mask ? 1 : 0) {
                                updateBit[ByteIndex*8 + temp_list[BitIndex]]++;
                                bitUpdateVector[(ByteIndex*8 + temp_list[BitIndex])/4] = 1;
                                tempForReadModifiedUpdateBit++;
                            }
                        }
                    }
                    bitCountByte++;
                }
                if (update_check >= 1){
                    columnUpdateNum++;
                    columUpdateVector[(bitCountByte-1)/8] = 1;
                }
                columnIndex++;
            }
            ++updateColumns[columnUpdateNum];
            

            for( int8_t i = 0; i < 8; i++){
                if (columUpdateVector[i] == 1)
                    columnVectorNum++;
            }
            for( int8_t i = 0; i < 14; i++){
                if (bitUpdateVector[i] == 1)
                    bitVectorNum_4++;
            }
            if (bitUpdateVector[14] == 1 || bitUpdateVector[15] == 1)
                bitVectorNum_8++;
            tempForVectorUpdateBit = columnVectorNum * (4 * bitVectorNum_4);
            tempForVectorUpdateBit += columnVectorNum * (8 * bitVectorNum_8);
            
            ReadModifiedUpdateBit += tempForReadModifiedUpdateBit;
            VectorUpdateBit += tempForVectorUpdateBit;
            //Adding Part End

            uint32_t CompressDataSize = BDI(NewData, request->data.GetSize());

            if (CompressDataSize <=32){
                compressByte[0]++;
                if (columnVectorNum == 0){

                }else if (columnVectorNum <= 3){
                    CompressUpdateBit += tempForReadModifiedUpdateBit;
                }else if(columnVectorNum <= 5){
                    CompressUpdateBit += GetUpdateBitNum(bitCountFlipData, 2, request->data.GetSize());
                }else if(columnVectorNum == 6){
                    CompressUpdateBit += GetUpdateBitNum(bitCountFlipData, 4, request->data.GetSize());
                }else if(columnVectorNum == 7){
                    CompressUpdateBit += GetUpdateBitNum(bitCountFlipData, 8, request->data.GetSize());
                }else if(columnVectorNum == 8){
                    CompressUpdateBit += tempForVectorUpdateBit;
                }
            }else if(CompressDataSize <= 40){
                compressByte[1]++;
                if (columnVectorNum == 0){

                }else if (columnVectorNum <= 2){
                    CompressUpdateBit += tempForReadModifiedUpdateBit;
                }else if(columnVectorNum <= 5){
                    CompressUpdateBit += GetUpdateBitNum(bitCountFlipData, 2, request->data.GetSize());
                }else if(columnVectorNum == 6){
                    CompressUpdateBit += GetUpdateBitNum(bitCountFlipData, 4, request->data.GetSize());
                }else if(columnVectorNum == 7){
                    CompressUpdateBit += GetUpdateBitNum(bitCountFlipData, 8, request->data.GetSize());
                }else if(columnVectorNum == 8){
                    CompressUpdateBit += tempForVectorUpdateBit;
                }

            }else if(CompressDataSize <= 48){
                compressByte[2]++;
                if (columnVectorNum == 0){

                }else if(columnVectorNum <= 1){
                    CompressUpdateBit += tempForReadModifiedUpdateBit;
                }else if(columnVectorNum <= 3){
                    CompressUpdateBit += GetUpdateBitNum(bitCountFlipData, 2, request->data.GetSize());
                }else if(columnVectorNum <= 6){
                    CompressUpdateBit += GetUpdateBitNum(bitCountFlipData, 4, request->data.GetSize());
                }else if(columnVectorNum == 7){
                    CompressUpdateBit += GetUpdateBitNum(bitCountFlipData, 8, request->data.GetSize());
                }else if(columnVectorNum == 8){
                    CompressUpdateBit += tempForVectorUpdateBit;
                }
            }else if(CompressDataSize <= 56){
                compressByte[3]++;
                if (columnVectorNum == 0){

                }else if(columnVectorNum <= 1){
                    CompressUpdateBit += GetUpdateBitNum(bitCountFlipData, 2, request->data.GetSize());
                }else if(columnVectorNum <= 3){
                    CompressUpdateBit += GetUpdateBitNum(bitCountFlipData, 4, request->data.GetSize());
                }else if(columnVectorNum <= 5){
                    CompressUpdateBit += GetUpdateBitNum(bitCountFlipData, 8, request->data.GetSize());
                }else if(columnVectorNum <= 7){
                    CompressUpdateBit += GetUpdateBitNum(bitCountFlipData, 16, request->data.GetSize());
                }else if(columnVectorNum == 8){
                    CompressUpdateBit += tempForVectorUpdateBit;
                }
            }else{
                compressByte[4]++;
                CompressUpdateBit += 64*8;
            }


        }

        PrintPreTrace( request );
    }

    return mc_rv;
}


uint64_t NVMain::GetUpdateBitNum(uint8_t *flipcacheline, uint8_t granulatiry, uint8_t size){


    int columnIndex = 0;
    int update_check = 0;
    int columnUpdateNum = 0;
    int temp_list[8] = {7,6,5,4,3,2,1,0};

    int columUpdateVector[8] = {0,};
    int columnVectorNum = 0;
    uint8_t *bitUpdateVector = new uint8_t[64/granulatiry];
    int bitVectorNum=0;

    uint64_t tempForUpdateBit = 0;

    for( uint64_t bitCountByte = 0; bitCountByte < size;){ //8개 Columns(64 Byte)
        update_check = 0;
        for( uint64_t ByteIndex = 0; ByteIndex<8; ByteIndex++){ //1개 Column (8 Byte)
            if(flipcacheline[bitCountByte] >= 1){
                update_check++;
                for(int8_t BitIndex = 7; BitIndex >= 0; BitIndex--){ //1개 Column 내부 1Byte
                    int8_t mask=0;
                    mask = 1 << temp_list[BitIndex];
                    if(flipcacheline[bitCountByte] & mask ? 1 : 0) {
                        updateBit[ByteIndex*8 + temp_list[BitIndex]]++;
                        bitUpdateVector[(ByteIndex*8 + temp_list[BitIndex])/(64/granulatiry)] = 1;
                    }
                }
            }
            bitCountByte++;
        }
        if (update_check >= 1){
            columnUpdateNum++;
            columUpdateVector[(bitCountByte-1)/8] = 1;
        }
        columnIndex++;
    }
    ++updateColumns[columnUpdateNum];
    

    for( int8_t i = 0; i < 8; i++){
        if (columUpdateVector[i] == 1)
            columnVectorNum++;
    }
    for( int8_t i = 0; i < 64/granulatiry; i++){
        if (bitUpdateVector[i] == 1)
            bitVectorNum++;
    }
    tempForUpdateBit = columnVectorNum * (granulatiry * bitVectorNum);

    return tempForUpdateBit;
}
uint32_t NVMain::BDI(uint8_t *cacheline, uint32_t data_size)
{


    int min_compressed_size = data_size;  //byte
    std::vector<uint64_t> data_vec;

    std::vector<uint32_t> base_size;
    std::map<uint32_t, uint32_t> min_delta_size_map;
    std::map<uint32_t, uint64_t> min_base_map;
    std::map<uint32_t, uint32_t> min_compressed_size_map;
    base_size.push_back(2);
    base_size.push_back(4);
    base_size.push_back(8);

    std::vector<uint64_t> delta_base_vector;
    std::vector<uint64_t> delta_immd_vector;
    std::vector<uint32_t> delta_map;

    
    uint64_t min_base = 0;
    int min_k = 0;
    uint64_t min_delta_size_base=0;
    uint64_t min_delta_size_immd=0;
    uint64_t min_num_delta_immd=0;
    uint64_t min_num_delta_base=0;
    std::vector<uint64_t> min_delta_base_vector;
    std::vector<uint64_t> min_delta_immd_vector;
    std::vector<uint32_t> min_delta_map;
    
    //calculate the delta
    int compressed_size = 0;

    for (auto &k: base_size) {
        uint8_t *ptr;
        data_vec.clear();
        for (uint32_t i = 0; i < data_size / k; i++) {
            ptr = (cacheline + i * k);
            if (k == 2)
                data_vec.push_back((uint64_t) (*(uint16_t *) ptr));
            else if (k == 4)
                data_vec.push_back((uint64_t) (*(uint32_t *) ptr));
            else if (k == 8)
                data_vec.push_back((uint64_t) (*(uint64_t *) ptr));
        }

        int64_t delta_base = 0;
        uint64_t delta_immd = 0;
        uint64_t immd = 0;

    


        for (auto &base:data_vec) {
            delta_immd_vector.clear();
            delta_base_vector.clear();
            delta_map.clear();

            uint64_t delta_size_immd = 0;
            uint64_t delta_size_base = 0;

            //calculate delta for a base
            for (auto &data:data_vec) {

                delta_base = data - base;
                delta_immd = data - immd;

                bool select_immd=false;

                //decide the base
                if(delta_base<0) //delta_immd is always positive because we assume that the immd is zero
                    select_immd=true;
                else if(delta_base>=0)
                {
                    if (delta_base >= delta_immd)
                        select_immd=true;
                    else
                        select_immd=false;
                }
                else
                    break;
                
               // std::cout<<std::hex<<"delta_base:"<<delta_base<<" ";
               // std::cout<<std::hex<<"delta_immd:"<<delta_immd<<" ";
               // std::cout<<std::dec<<"select_immd:"<<select_immd<<std::endl;

                //calulate delta
                if (select_immd) {
                    uint64_t size = getDataSize(delta_immd);
                    if (size > 8)
                        break;

                    if (delta_size_immd < size)
                        delta_size_immd = size;

                    delta_immd_vector.push_back(delta_immd);
                    delta_map.push_back(0);

                } else {
                    uint64_t size = getDataSize(delta_base);
                    if (size > 8)
                        break;

                    if (delta_size_base < size)
                    {
                        delta_size_base = size;
                    }

                    delta_base_vector.push_back(delta_base);
                    delta_map.push_back(1);
                }
            }

            //calculate compressed size
            compressed_size =                                              // compressed size (byte)=
                    1                                                      //base_size
                    +1                                                     //delta_size (base (=4bit) and immd (=4bit) = 8 bits = 1byte)
                    +k                                                      //base
                    + ceil(((double)data_size/(double)k)/(double)8)                                      //delta map (base or immd?)
                    + delta_base_vector.size() * delta_size_base       //delta for base
                    + delta_immd_vector.size() * delta_size_immd ;     //delta for immd base
            
            //std::cerr<<"comressed_size"<<compressed_size<<std::endl;
            //get min compressed size
            if (compressed_size < min_compressed_size) {
                min_compressed_size = compressed_size;
                
                if (do_verify) {
                    min_k = k;
                    min_base = base;
                    min_delta_size_base= delta_size_base;
                    min_delta_size_immd= delta_size_immd;
                    min_num_delta_base = delta_base_vector.size();
                    min_num_delta_immd = delta_immd_vector.size();
                    min_delta_base_vector=delta_base_vector;
                    min_delta_immd_vector=delta_immd_vector;
                    min_delta_map=delta_map;
                }

            }

            //std::cerr<<"done"<<std::endl;
        }
    }



    //validate compression algorithm
    if (do_verify && min_compressed_size<data_size) {

        data_vec.clear();
        for (uint32_t i = 0; i < data_size / min_k; i++) {
            uint8_t *ptr = (cacheline + i * min_k);
            if (min_k == 2)
                data_vec.push_back((uint64_t) (*(uint16_t *) ptr));
            else if (min_k == 4)
                data_vec.push_back((uint64_t) (*(uint32_t *) ptr));
            else if (min_k == 8)
                data_vec.push_back((uint64_t) (*(uint64_t *) ptr));
        }
        ////////// Compression ////////////////
        //insert base size
        std::deque<uint8_t> compressed_data;
        compressed_data.push_back(min_k);

        //insert delta size
        uint8_t delta_size= ((uint8_t)min_delta_size_base)<<4;
        delta_size+= ((uint8_t)min_delta_size_immd);
        compressed_data.push_back(delta_size);

        //std::cout<<"delta_size_base:"<<min_delta_size_base<<" "
        //    <<"delta_size_immd:"<<min_delta_size_immd<<std::endl;
     
        //insert base
        for(int i=0;i<min_k;i++)
            compressed_data.push_back((uint8_t)(min_base>>8*i));
        //insert delta map
        uint8_t tmp_map=0;
        //std::cout<<"delta map"<<std::endl;
        for(uint i=0;i<min_delta_map.size();i++)
        {
            if(min_delta_map[i]==1)
            {
                tmp_map|=0x80;
            }

            if((i+1)%8==0)
            {
                compressed_data.push_back((uint8_t)tmp_map);
                tmp_map=0;
            }
            else
                tmp_map=tmp_map>>1;
        }

        //insert delta
        for(uint i=0; i<min_num_delta_base;i++)
        {
            for(uint j=0;j<min_delta_size_base;j++)
            {
                compressed_data.push_back((uint8_t)(min_delta_base_vector[i]>>8*j));
            }
        }


        for(uint i=0; i<min_num_delta_immd;i++)
        { 
            //printf("delta_immd: %llx\n",min_delta_immd_vector[i]);
            for(uint j=0;j<min_delta_size_immd;j++)
            {
                compressed_data.push_back((uint8_t)(min_delta_immd_vector[i]>>8*j));
            }
        }
        
        if(compressed_data.size()!=(uint)min_compressed_size)
        {
            printf("commpression error!! compressed_data.size():%d min_compressed_size:%d\n",(int)compressed_data.size(),min_compressed_size);
            exit(1);     
        }

        ////////// Decompression ////////////////
        //read base size
        uint8_t base_size =compressed_data.front();
        compressed_data.pop_front();
       
        //read delta size
        uint8_t decomp_num_delta = data_size/base_size;
        uint8_t decomp_size_delta = compressed_data.front();
        compressed_data.pop_front();


        uint8_t decomp_size_delta_immd = decomp_size_delta & 0xf;
        uint8_t decomp_size_delta_base = (decomp_size_delta>>4) & 0xf;
       
        //delta_map
        uint64_t decomp_base=0;
        for(int i=0;i<base_size;i++)
        {
            uint64_t tmp=compressed_data.front();
            compressed_data.pop_front();
            decomp_base+=tmp<<8*i;
        }

        std::vector<uint8_t>decomp_delta_map;
        for(int i=0;i<decomp_num_delta;)
        {
            uint8_t tmp=compressed_data.front();
            compressed_data.pop_front();

            for(int j=0;j<8;j++)
            {
                decomp_delta_map.push_back(((uint8_t)(tmp>>1*j)&0x1));
                i++;
            }
        }

        int decomp_num_delta_base=0;
        int decomp_num_delta_immd=0;

        for(uint i=0;i<decomp_delta_map.size();i++)
        {
            if(decomp_delta_map[i]==0)
                decomp_num_delta_immd++;
            else
                decomp_num_delta_base++;
        }


        std::vector<uint64_t>decomp_delta_base_vector;
        std::vector<uint64_t>decomp_delta_immd_vector;
        
        for(int i=0;i<decomp_num_delta_base;i++)
        {
            int64_t tmp=0;

            for(int j=0;j<decomp_size_delta_base;j++)
            {
                tmp+=((int64_t)compressed_data.front()<<8*j);
                compressed_data.pop_front();
            }
            decomp_delta_base_vector.push_back(tmp);
        }

        for(int i=0;i<decomp_num_delta_immd;i++)
        {
            int64_t tmp=0;

            for(int j=0;j<decomp_size_delta_immd;j++)
            {
                tmp+=((int64_t)compressed_data.front()<<8*j);

                if(!compressed_data.empty())
                    compressed_data.pop_front();
            }
            decomp_delta_immd_vector.push_back(tmp);
        }


        int base_idx = 0;
        int immd_idx = 0;
        int data_idx = 0;
        int64_t delta = 0;
        int64_t decomp_delta =0;

         for (auto &flag:decomp_delta_map) {
            uint64_t decompressed_data=0;
            if (flag == 0) {
                decomp_delta = decomp_delta_immd_vector[immd_idx++];
                decompressed_data = decomp_delta;
                        

                if(getDataSize(delta)>decomp_size_delta_immd)
                {
                    printf("decompression error, getDataSize(delta)>delta_size_immd, getDataSize(data):%d delta_size_immd:%d\n",getDataSize(delta),decomp_size_delta_immd);
                    exit(1);
                }

            } else {
                decomp_delta = decomp_delta_base_vector[base_idx++];
                if(decomp_delta<0)
                {
                    printf("delta should not be negative value\n");
                    exit(1);
                }

                decompressed_data = decomp_base + decomp_delta;

                if(getDataSize(delta)>decomp_size_delta_base)
                {
                    printf("decompression error, getDataSize(delta)>delta_size_base, getDataSize(data):%d delta_size_base:%d\n",getDataSize(delta),decomp_size_delta_base);
                    exit(1);
                }
            }
                    
        
            if (decompressed_data != data_vec[data_idx]) {
                printf("decompression error, data_idx:%d flag:%d base:%lx delta:%ld decompressed_data:%lx, data:%lx\n",data_idx,flag,decomp_base,decomp_delta,decompressed_data,data_vec[data_idx]);
                exit(1);
            }
            data_idx++;
            }

        }


    #ifdef COMP_DEBUG
        printf("base size: %u base: %lx delta_size_base:%d num_delta_base:%u delta_size_immd:%d num_delta_immd:%u compressed_size:%d\n",min_k, min_base,min_delta_size_base,min_num_delta_base,min_delta_size_immd,min_num_delta_immd,min_compressed_size);
    #endif
    return min_compressed_size;  //byte

}

uint32_t NVMain::getDataSize(uint64_t data)
{   
    int size=0; //bit

    if(data==0)
        return 0;

    //uint64_t data=llabs(data_);

    for(uint64_t i=0; i<=64; i+=8)
    {
        uint64_t mask=~(0xffffffffffffffff<<i);
        
        if(i==64)
            mask=0xffffffffffffffff;
        
        if(data==(data & mask))
        {
            size = i;
            break;
        }
    }
//    cout<<data<<" "<<size<<" "<<ceil((double)size/(double)8)<<endl;
    return ceil((double)size/(double)8);
}






bool NVMain::IssueAtomic( NVMainRequest *request )
{
    ncounter_t channel, rank, bank, row, col, subarray;
    bool mc_rv;

    if( !config )
    {
        std::cout << "NVMain: Received request before configuration!\n";
        return false;
    }

    /* Translate the address, then copy to the address struct, and copy to request. */
    GetDecoder( )->Translate( request->address.GetPhysicalAddress( ), 
                           &row, &col, &bank, &rank, &channel, &subarray );
    request->address.SetTranslatedAddress( row, col, bank, rank, channel, subarray );
    request->bulkCmd = CMD_NOP;

    /* Check for any successful prefetches. */
    if( CheckPrefetch( request ) )
    {
        return true;
    }

    mc_rv = memoryControllers[channel]->IssueAtomic( request );
    if( mc_rv == true )
    {
        IssuePrefetch( request );

        if( request->type == READ ) 
        {
            totalReadRequests++;
        }
        else
        {
            totalWriteRequests++;
        }

        PrintPreTrace( request );
    }
    
    return mc_rv;
}

bool NVMain::RequestComplete( NVMainRequest *request )
{
    bool rv = false;

    if( request->owner == this )
    {
        if( request->isPrefetch )
        {
            //std::cout << "Placing 0x" << std::hex << request->address.GetPhysicalAddress( )
            //          << std::dec << " into prefetch buffer (cur size: " << prefetchBuffer.size( )
            //          << ")." << std::endl;

            /* Place in prefetch buffer. */
            if( prefetchBuffer.size() >= p->PrefetchBufferSize )
            {
                unsuccessfulPrefetches++;
                //std::cout << "Prefetch buffer is full. Removing oldest prefetch: 0x" << std::hex
                //          << prefetchBuffer.front()->address.GetPhysicalAddress() << std::dec
                //          << std::endl;

                delete prefetchBuffer.front();
                prefetchBuffer.pop_front();
            }

            prefetchBuffer.push_back( request );
            rv = true;
        }
        else
        {
            delete request;
            rv = true;
        }
    }
    else
    {
        rv = GetParent( )->RequestComplete( request );
    }

    /* This is used in the main memory system when a DRAMCache is present.
     * DRAMCache misses need to issue main memory requests than might not
     * be issuable at that time. Try to issue these here. */
    if( !pendingMemoryRequests.empty() ){
       NVMainRequest *staleMemReq = pendingMemoryRequests.front();
        if( IsIssuable(staleMemReq, NULL) ) {
            IssueCommand( staleMemReq );
            pendingMemoryRequests.pop();
        }
    }

    return rv;
}

void NVMain::Cycle( ncycle_t /*steps*/ )
{
}

void NVMain::RegisterStats( )
{
    AddStat(totalReadRequests);
    AddStat(totalWriteRequests);
    AddStat(successfulPrefetches);
    AddStat(unsuccessfulPrefetches);
    for(int i= 0 ; i<9 ; i++){
        AddNameStat(updateColumns[i], "updateColumns", std::to_string(i));
    }
    for(int i= 0 ; i< 64; i++){
        AddNameStat(updateBit[i], "updateBit", std::to_string(i));
    }
    AddStat(ReadModifiedUpdateBit);
    AddStat(VectorUpdateBit)
    AddStat(CompressUpdateBit)
    for(int i= 0 ; i< 5; i++){
        AddNameStat(compressByte[i], "compressByteSize", std::to_string(i+1));
    }
}

void NVMain::CalculateStats( )
{
    for( unsigned int i = 0; i < numChannels; i++ )
        memoryControllers[i]->CalculateStats( );
}

void NVMain::EnqueuePendingMemoryRequests( NVMainRequest *req )
{
    pendingMemoryRequests.push(req);
}

