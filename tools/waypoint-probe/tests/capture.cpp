#include "../src/probe.cpp"
#include <iostream>
#include <algorithm>
#include <iterator>

namespace {
unsigned setupCalls=0, eventCalls=0;
void* expectedDialog=nullptr;
const void* expectedArgument=nullptr;
bool forwarded=true, simulateRequest=false;
unsigned simpleCalls=0, routedCalls=0;
uint32_t lastSimple=0, lastRouted=0;
const void* lastRoute=nullptr;
void fakeSimple(uint32_t id) { ++simpleCalls; lastSimple=id; }
void fakeRouted(uint32_t id,const void* route) { ++routedCalls; lastRouted=id; lastRoute=route; }
void fakeSetup(void* dialog,const void* argument) {
    ++setupCalls; forwarded &= dialog==expectedDialog && argument==expectedArgument;
}
void fakeEvent(void* dialog,const void* argument) {
    ++eventCalls; forwarded &= dialog==expectedDialog && argument==expectedArgument;
    if(simulateRequest) tracedSimple(1072);
}
}
#define CHECK(x) do { if(!(x)) { std::cerr<<"Failed line "<<__LINE__<<": " #x "\n"; return 1; } } while(false)
int main() {
    records.reserve(capacity); originalSetup=fakeSetup; originalEvent=fakeEvent;
    std::array<unsigned char,0xd0> dialog{};
    std::array<unsigned char,0x30> poi{};
    std::array<uint32_t,3> event{0,1,3};
    const void* poiPointer=poi.data();
    uint32_t id=1072;
    std::memcpy(poi.data()+0x28,&id,sizeof(id));
    std::memcpy(dialog.data()+0x68,&poiPointer,sizeof(poiPointer));
    expectedDialog=dialog.data(); expectedArgument=poi.data();
    start(); tracedSetup(expectedDialog,expectedArgument);
    CHECK(setupCalls==1 && forwarded && records.size()==1);
    CHECK(records[0].waypoint==1072 && records[0].readOk);
    expectedArgument=event.data(); tracedEvent(expectedDialog,expectedArgument);
    CHECK(eventCalls==1 && forwarded && records.size()==3);
    CHECK(std::strcmp(records[1].event,"ACCEPT_ENTER")==0 && std::strcmp(records[2].event,"ACCEPT_RETURN")==0);
    CHECK(records[1].waypoint==1072 && records[2].waypoint==1072);
    event[1]=0; tracedEvent(expectedDialog,expectedArgument);
    CHECK(eventCalls==2 && records.size()==4 && std::strcmp(records.back().event,"CANCEL")==0);
    event[1]=6; tracedEvent(expectedDialog,expectedArgument);
    CHECK(eventCalls==3 && records.size()==4);
    event[1]=1; event[2]=2; tracedEvent(expectedDialog,expectedArgument);
    CHECK(eventCalls==4 && records.size()==4);
    // Bad diagnostic pointers must still forward to the original exactly once.
    expectedDialog=reinterpret_cast<void*>(1); expectedArgument=reinterpret_cast<void*>(1);
    tracedEvent(expectedDialog,expectedArgument);
    CHECK(eventCalls==5 && forwarded && !records.back().readOk);
    const auto token=session; const auto old=records[0];
    start(); append(token,old); CHECK(records.empty());
    deadline=0; tracedEvent(expectedDialog,expectedArgument);
    CHECK(eventCalls==6 && records.empty() && !capturing.load());
    start(); for(size_t i=0;i<capacity+3;++i) append(session,old);
    CHECK(records.size()==capacity && dropped==3);
    directory=std::filesystem::temp_directory_path()/("waypoint_probe_test_"+std::to_string(GetCurrentProcessId()));
    save(); CHECK(saved && std::filesystem::exists(directory/"waypoint_request_v7_1.tsv"));
    std::ifstream in(directory/"waypoint_request_v7_1.tsv");
    std::string row; size_t rows=0;
    while(std::getline(in,row)) { CHECK(std::count(row.begin(),row.end(),'\t')==21); ++rows; }
    CHECK(rows==capacity+2); in.close();
    start(); save(); CHECK(std::filesystem::exists(directory/"waypoint_request_v7_2.tsv"));
    std::filesystem::remove_all(directory);
    // Simulate an active connection and verify that direct calls require a matching
    // native sender invocation nested inside the real confirmation wrapper.
    std::vector<unsigned char> image(connectionSlotRva+sizeof(void*));
    std::array<unsigned char,0x110> connection{};
    const void* connectionPointer=connection.data();
    uint32_t activeState=3; connection[0]=4;
    std::memcpy(connection.data()+0x108,&activeState,sizeof(activeState));
    std::memcpy(image.data()+connectionSlotRva,&connectionPointer,sizeof(connectionPointer));
    host=reinterpret_cast<HMODULE>(image.data()); hostSize=image.size();
    supported=setupHooked=eventHooked=simpleHooked=routedHooked=true;
    originalSimple=fakeSimple; originalRouted=fakeRouted;
    start(); tracedSimple(1072);
    CHECK(simpleCalls==1 && lastSimple==1072 && !canDirect(1072));
    scopedWaypoint=1038; tracedSimple(1072); scopedWaypoint=0;
    CHECK(simpleCalls==2 && !canDirect(1072));
    scopedWaypoint=1072; tracedRouted(1072,reinterpret_cast<void*>(1)); scopedWaypoint=0;
    CHECK(routedCalls==1 && lastRouted==1072 && lastRoute==reinterpret_cast<void*>(1) && !canDirect(1072));
    start(); simulateRequest=true; expectedDialog=dialog.data(); expectedArgument=event.data();
    event[1]=1; event[2]=3;
    tracedEvent(expectedDialog,expectedArgument); simulateRequest=false;
    CHECK(scopedWaypoint==0 && canDirect(1072) && !canDirect(1038));
    CHECK(records.size()==3 && records[1].correlated && records[1].confirmation==1072);
    CHECK(std::strcmp(records[1].event,"REQUEST_SIMPLE")==0);
    auto manual=verified[0]; verified[0].thread=GetCurrentThreadId()+1;
    CHECK(!canDirect(1072)); verified[0]=manual;
    connection[0]=0; CHECK(!canDirect(1072)); connection[0]=4;
    verified[0].connection=reinterpret_cast<void*>(1); CHECK(!canDirect(1072)); verified[0]=manual;
    const unsigned before=simpleCalls;
    directTravel(1072); CHECK(simpleCalls==before+1 && lastSimple==1072);
    CHECK(std::strcmp(records.back().event,"DIRECT_RETURN")==0);
    directTravel(1072); CHECK(simpleCalls==before+1); // debounce
    nextDirectAt=0; directTravel(999); CHECK(simpleCalls==before+1); // fixed allowlist
    start(); CHECK(!canDirect(1072)); // reset invalidates previous manual verification
    deadline=0; CHECK(!canDirect(1072));
    std::cout<<"Forwarding, capture, correlation, direct-call gates and TSV verified\n";
}
