#include "../src/probe.cpp"
#include <iostream>
#include <algorithm>
#include <iterator>

namespace {
unsigned setupCalls=0, eventCalls=0;
void* expectedDialog=nullptr;
const void* expectedArgument=nullptr;
bool forwarded=true;
void fakeSetup(void* dialog,const void* argument) {
    ++setupCalls; forwarded &= dialog==expectedDialog && argument==expectedArgument;
}
void fakeEvent(void* dialog,const void* argument) {
    ++eventCalls; forwarded &= dialog==expectedDialog && argument==expectedArgument;
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
    save(); CHECK(saved && std::filesystem::exists(directory/"waypoint_ui_v6_1.tsv"));
    std::ifstream in(directory/"waypoint_ui_v6_1.tsv");
    std::string row; size_t rows=0;
    while(std::getline(in,row)) { CHECK(std::count(row.begin(),row.end(),'\t')==15); ++rows; }
    CHECK(rows==capacity+2); in.close();
    start(); save(); CHECK(std::filesystem::exists(directory/"waypoint_ui_v6_2.tsv"));
    std::filesystem::remove_all(directory);
    std::cout<<"Forwarding, bounded reads, capture reset, capacity and TSV verified\n";
}
