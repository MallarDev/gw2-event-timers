#include <windows.h>
#include "Nexus.h"
#include "imgui.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace {
constexpr DWORD expectedTimestamp = 0x6ad7acf4;
constexpr DWORD expectedImageSize = 0x02c49000;
constexpr size_t capacity = 1024;
constexpr size_t setupRva = 0x004f0160;
constexpr size_t eventRva = 0x004efe30;
constexpr unsigned char setupBytes[] = {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x55};
constexpr unsigned char eventBytes[] = {0x40,0x53,0x48,0x83,0xec,0x40,0x48,0x8b,0x05,0x83,0x3d,0x0a,0x02,0x48,0x33,0xc4};
constexpr size_t simpleRva = 0x01263c70;
constexpr size_t routedRva = 0x01263cc0;
constexpr size_t connectionSlotRva = 0x0266c1d0;
constexpr unsigned char simpleBytes[] = {0x40,0x53,0x48,0x83,0xec,0x20,0x8b,0xd9,0x85,0xc9,0x75,0x19,0x41,0xb8,0x12,0x39};
constexpr unsigned char routedBytes[] = {0x48,0x89,0x5c,0x24,0x18,0x57,0x48,0x83,0xec,0x40,0x48,0x8b,0x05,0xef,0xfe,0x32};
using SimpleFn = void (*)(uint32_t);
using RoutedFn = void (*)(uint32_t, const void*);
SimpleFn originalSimple = nullptr;
RoutedFn originalRouted = nullptr;
bool simpleHooked = false, routedHooked = false;
thread_local uint32_t scopedWaypoint = 0;
struct Verification { DWORD thread{}; const void* connection{}; };
std::array<Verification,2> verified{};
constexpr std::array<uint32_t,2> testWaypoints{1072,1038};
std::atomic<ULONGLONG> nextDirectAt{0};
std::atomic<HWND> gameWindow{nullptr};
std::atomic<DWORD> windowThread{0};
UINT dispatchMessage=0;
bool windowCallbackRegistered=false;
struct PendingTravel {
    uint32_t waypoint{};
    unsigned capture{};
    uintptr_t ticket{};
    HWND window{};
    DWORD thread{};
    ULONGLONG expiresAt{};
};
PendingTravel pendingTravel{};
std::atomic<bool> pendingActive{false};
uintptr_t nextTicket=0;
DWORD renderThread = 0;
using SetupFn = void (*)(void*, const void*);
using EventFn = void (*)(void*, const void*);
SetupFn originalSetup = nullptr;
EventFn originalEvent = nullptr;
AddonAPI_t* api = nullptr;
HMODULE host = nullptr;
size_t hostSize = 0;
bool setupHooked = false, eventHooked = false, supported = false, visible = true;
std::atomic<bool> capturing{false};
std::mutex captureMutex;
unsigned session = 0, dropped = 0, savedNumber = 0;
bool saved = true;
ULONGLONG deadline = 0;
std::filesystem::path directory;
std::string status;
struct Record {
    ULONGLONG tick{};
    DWORD thread{};
    const char* event{};
    uint32_t waypoint{}, control{}, subtype{};
    bool readOk{}, correlated{}, connected{};
    uint32_t confirmation{};
    std::array<uint64_t, 6> stack{};
};
std::vector<Record> records;

template<class T> bool readField(const void* object, size_t offset, T& out) {
    if (!object || reinterpret_cast<uintptr_t>(object) > UINTPTR_MAX-offset) return false;
    T value{};
    SIZE_T copied = 0;
    const bool ok = ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(object)+offset),
        &value, sizeof(T), &copied) && copied == sizeof(T);
    out = ok ? value : T{};
    return ok;
}
Record makeRecord(const char* event) {
    Record r{}; r.event=event; r.tick=GetTickCount64(); r.thread=GetCurrentThreadId();
    void* frames[6]{};
    const USHORT count = CaptureStackBackTrace(2, 6, frames, nullptr);
    const uintptr_t base = reinterpret_cast<uintptr_t>(host);
    for (USHORT i=0; i<count; ++i) {
        const uintptr_t address = reinterpret_cast<uintptr_t>(frames[i]);
        if (address >= base && address-base < hostSize) r.stack[i] = address-base;
    }
    return r;
}
unsigned captureSession() {
    std::lock_guard<std::mutex> lock(captureMutex);
    if (!capturing.load() || GetTickCount64() >= deadline) { capturing.store(false); return 0; }
    return session;
}
bool append(unsigned token, const Record& record) {
    std::lock_guard<std::mutex> lock(captureMutex);
    if (!token || session != token || !capturing.load() || GetTickCount64() >= deadline) return false;
    if (records.size() >= capacity) { ++dropped; return false; }
    records.push_back(record); return true;
}
bool connectionState(const void*& connection) {
    connection=nullptr;
    if(!host || connectionSlotRva+sizeof(void*)>hostSize ||
       !readField(host,connectionSlotRva,connection) || !connection) return false;
    uint32_t state=0; unsigned char flags=0;
    return readField(connection,0x108,state) && readField(connection,0,flags) && state==3 && (flags&4)!=0;
}
void observeRequest(const char* kind,uint32_t id,bool simple) {
    const unsigned token=capturing.load()?captureSession():0;
    if(!token) return;
    Record r=makeRecord(kind); r.waypoint=id; r.readOk=true;
    r.confirmation=scopedWaypoint; r.correlated=scopedWaypoint!=0 && scopedWaypoint==id;
    const void* connection=nullptr; r.connected=connectionState(connection);
    if(append(token,r) && simple && r.correlated && r.connected) {
        std::lock_guard<std::mutex> lock(captureMutex);
        if(session==token && capturing.load())
            for(size_t i=0;i<testWaypoints.size();++i)
                if(id==testWaypoints[i]) verified[i]={r.thread,connection};
    }
}
void tracedSimple(uint32_t id) {
    observeRequest("REQUEST_SIMPLE",id,true);
    originalSimple(id);
}
void tracedRouted(uint32_t id,const void* route) {
    // Forward the route unchanged. Its contents are never copied or logged.
    observeRequest("REQUEST_ROUTED",id,false);
    originalRouted(id,route);
}
bool canExecute(uint32_t id,DWORD executionThread) {
    if(!supported || !setupHooked || !eventHooked || !simpleHooked || !routedHooked || !originalSimple ||
       !capturing.load() || !captureSession() || GetTickCount64()<nextDirectAt) return false;
    const void* connection=nullptr;
    if(!connectionState(connection)) return false;
    std::lock_guard<std::mutex> lock(captureMutex);
    for(size_t i=0;i<testWaypoints.size();++i)
        if(id==testWaypoints[i] && verified[i].thread==executionThread &&
           verified[i].connection==connection) return true;
    return false;
}
bool canDirect(uint32_t id) { return canExecute(id,GetCurrentThreadId()); }
void directTravel(uint32_t id,unsigned expectedCapture=0) {
    if(!canDirect(id)) {
        Record r=makeRecord("DIRECT_REJECT"); r.waypoint=id;
        append(captureSession(),r); return;
    }
    const unsigned token=expectedCapture?expectedCapture:captureSession();
    if(!token) return;
    {
        std::lock_guard<std::mutex> lock(captureMutex);
        if(session!=token || !capturing.load() || GetTickCount64()>=deadline || GetTickCount64()<nextDirectAt) return;
        // Reserve execution in this capture. A concurrent reset can cancel queued
        // work, but cannot turn an old ticket into an invocation in a new capture.
        nextDirectAt=GetTickCount64()+5000;
    }
    Record r=makeRecord("DIRECT_ENTER"); r.waypoint=id; r.readOk=true; r.connected=true;
    append(token,r);
    // Only this user button invokes the verified native sender. Do not retain buffers or receiver objects.
    originalSimple(id);
    r.event="DIRECT_RETURN"; r.tick=GetTickCount64(); append(token,r);

}
bool canQueue(uint32_t id) {
    const HWND window=gameWindow.load();
    const DWORD thread=windowThread.load();
    if(!windowCallbackRegistered || !dispatchMessage || !window || !thread || pendingActive.load()) return false;
    DWORD process=0;
    if(GetWindowThreadProcessId(window,&process)!=thread || process!=GetCurrentProcessId()) return false;
    return canExecute(id,thread);
}
void queueTravel(uint32_t id) {
    if(!canQueue(id)) { status="Fila indisponível: destino, janela ou conexão ainda não validados."; return; }
    const unsigned token=captureSession();
    if(!token) return;
    PendingTravel request{};
    {
        std::lock_guard<std::mutex> lock(captureMutex);
        if(pendingActive.load() || session!=token || !capturing.load()) return;
        ++nextTicket; if(!nextTicket) ++nextTicket;
        request={id,token,nextTicket,gameWindow.load(),windowThread.load(),GetTickCount64()+5000};
        pendingTravel=request; pendingActive.store(true);
    }
    Record r=makeRecord("QUEUED"); r.waypoint=id; r.readOk=true;
    append(token,r);
    if(!PostMessageW(request.window,dispatchMessage,static_cast<WPARAM>(request.ticket),static_cast<LPARAM>(token))) {
        const DWORD error=GetLastError();
        {
            std::lock_guard<std::mutex> lock(captureMutex);
            if(pendingActive.load() && pendingTravel.ticket==request.ticket) pendingActive.store(false);
        }
        r.event="QUEUE_FAILED"; r.control=error; append(token,r);
        status="Não foi possível postar o pedido na janela do jogo.";
    } else status="Pedido enfileirado para a thread do jogo. Aguarde loading e chegada antes de outro teste.";
}
void expirePending() {
    PendingTravel expired{};
    {
        std::lock_guard<std::mutex> lock(captureMutex);
        if(pendingActive.load() && GetTickCount64()>=pendingTravel.expiresAt) {
            expired=pendingTravel; pendingActive.store(false);
        }
    }
    if(expired.ticket) {
        Record r=makeRecord("QUEUE_TIMEOUT"); r.waypoint=expired.waypoint;
        append(expired.capture,r);
    }
}
UINT onWindowMessage(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    const DWORD current=GetCurrentThreadId();
    DWORD process=0;
    const DWORD owner=GetWindowThreadProcessId(window,&process);
    if(owner==current && process==GetCurrentProcessId()) {
        if(message==WM_NCDESTROY) {
            if(gameWindow.load()==window) { gameWindow.store(nullptr); windowThread.store(0); }
        } else { gameWindow.store(window); windowThread.store(current); }
    }
    if(!dispatchMessage || message!=dispatchMessage) return 1;
    PendingTravel request{};
    {
        std::lock_guard<std::mutex> lock(captureMutex);
        if(!pendingActive.load() || pendingTravel.ticket!=static_cast<uintptr_t>(wParam) ||
           pendingTravel.capture!=static_cast<unsigned>(lParam) || pendingTravel.window!=window) return 0;
        request=pendingTravel; pendingActive.store(false);
    }
    const unsigned token=captureSession();
    Record r=makeRecord("DISPATCH"); r.waypoint=request.waypoint;
    r.readOk=owner==current && process==GetCurrentProcessId() && current==request.thread;
    append(request.capture,r);
    if(token!=request.capture || !r.readOk || GetTickCount64()>=request.expiresAt) {
        r.event="DISPATCH_REJECT"; append(request.capture,r); return 0;
    }
    // Revalidate connection, destination, capture and thread immediately before
    // invoking the original sender, on the window thread rather than the renderer.
    directTravel(request.waypoint,request.capture);
    return 0; // consume only this addon's registered message
}
void tracedSetup(void* dialog, const void* poi) {
    if (capturing.load()) {
        const unsigned token = captureSession();
        Record r = makeRecord("DESTINATION");
        r.readOk = readField(poi, 0x28, r.waypoint);
        append(token, r);
    }
    originalSetup(dialog, poi);
}
void tracedEvent(void* dialog, const void* event) {
    const unsigned token = capturing.load() ? captureSession() : 0;
    Record r{};
    if (token) {
        uint32_t control = 0, subtype = 0;
        const bool eventOk = readField(event, 4, control) && readField(event, 8, subtype);
        // Only the two button branches used by this specific dialog, plus failed reads.
        if (!eventOk || (subtype == 3 && (control == 0 || control == 1))) {
            r = makeRecord(!eventOk ? "READ_FAILED" : control == 1 ? "ACCEPT_ENTER" : "CANCEL");
            r.control=control; r.subtype=subtype;
            const void* poi = nullptr;
            r.readOk = eventOk && readField(dialog, 0x68, poi) && readField(poi, 0x28, r.waypoint);
            append(token, r);
        }
    }
    // Preserve the original call exactly once, including unrecognized events.
    struct ConfirmationScope {
        uint32_t previous;
        explicit ConfirmationScope(uint32_t id):previous(scopedWaypoint) { scopedWaypoint=id; }
        ~ConfirmationScope() { scopedWaypoint=previous; }
    } scope(token && r.readOk && r.event && std::strcmp(r.event,"ACCEPT_ENTER")==0 ? r.waypoint : 0);
    originalEvent(dialog, event);
    if (token && r.event && std::strcmp(r.event,"ACCEPT_ENTER")==0) {
        // The original may destroy the dialog: never read it again after forwarding.
        r.event="ACCEPT_RETURN"; r.tick=GetTickCount64();
        append(token, r);
    }
}
bool supportedExecutable() {
    host=GetModuleHandleW(nullptr);
    if (!host) return false;
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(host);
    if (dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<=0 || dos->e_lfanew>4096) return false;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(reinterpret_cast<const char*>(host)+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC) return false;
    hostSize=nt->OptionalHeader.SizeOfImage;
    return nt->FileHeader.TimeDateStamp==expectedTimestamp && hostSize==expectedImageSize;
}
bool install(size_t rva, const unsigned char* signature, size_t size, void* replacement, void** original) {
    if(rva+size>hostSize) return false;
    auto* address=reinterpret_cast<unsigned char*>(host)+rva;
    if(std::memcmp(address,signature,size)!=0) return false;
    if(api->MinHook_Create(address,replacement,original)!=MH_OK) return false;
    if(api->MinHook_Enable(address)==MH_OK) return true;
    api->MinHook_Remove(address); *original=nullptr; return false;
}
void start() {
    std::lock_guard<std::mutex> lock(captureMutex);
    capturing.store(false); ++session; if(!session) ++session;
    records.clear(); dropped=0; saved=false; verified={}; nextDirectAt=0; pendingActive.store(false);
    deadline=GetTickCount64()+300000; capturing.store(true);
    status="Captura iniciada por até 5 minutos. Viaje manualmente para A e B. Depois confira os botões de teste direto.";
}
void save() {
    std::vector<Record> snapshot;
    unsigned lost=0;
    {
        std::lock_guard<std::mutex> lock(captureMutex);
        capturing.store(false); pendingActive.store(false);
        if(saved) { status="Inicie uma nova captura antes de salvar."; return; }
        snapshot=records; lost=dropped;
    }
    if(directory.empty()) { status="Diretório de captura indisponível."; return; }
    try {
        std::filesystem::create_directories(directory);
        std::filesystem::path path;
        do { path=directory/("waypoint_request_v8_"+std::to_string(++savedNumber)+".tsv"); }
        while(std::filesystem::exists(path));
        std::ofstream out(path,std::ios::trunc);
        if(!out) { status="Falha ao abrir arquivo para salvar."; return; }
        out<<"event\ttick_ms\tthread\twaypoint_id\tread_ok\tcontrol_id\tevent_subtype\tsetup_active\tevent_active\tdropped\tconfirmation_id\tcorrelated\tconnection_active\trender_thread\tsimple_active\trouted_active\twindow_thread\twindow_callback_active";
        for(int i=0;i<6;++i) out<<"\tgw2_rva_"<<i;
        out<<'\n';
        // Include hook health even when no candidate was hit.
        out<<"STATUS\t"<<GetTickCount64()<<"\t0\t0\t0\t0\t0\t"<<setupHooked<<'\t'<<eventHooked<<'\t'<<lost<<"\t0\t0\t0\t"<<renderThread<<'\t'<<simpleHooked<<'\t'<<routedHooked<<'\t'<<windowThread.load()<<'\t'<<windowCallbackRegistered;
        for(int i=0;i<6;++i) out<<'\t';
        out<<'\n';
        for(const auto& r:snapshot) {
            out<<r.event<<'\t'<<r.tick<<'\t'<<r.thread<<'\t'<<r.waypoint<<'\t'<<r.readOk<<'\t'<<r.control<<'\t'<<r.subtype<<'\t'<<setupHooked<<'\t'<<eventHooked<<'\t'<<lost<<'\t'<<r.confirmation<<'\t'<<r.correlated<<'\t'<<r.connected<<'\t'<<renderThread<<'\t'<<simpleHooked<<'\t'<<routedHooked<<'\t'<<windowThread.load()<<'\t'<<windowCallbackRegistered;
            for(const auto address:r.stack) { char hex[24]{}; if(address) std::snprintf(hex,sizeof(hex),"0x%llX",static_cast<unsigned long long>(address)); out<<'\t'<<hex; }
            out<<'\n';
        }
        out.close();
        if(!out) { status="Falha ao terminar a gravação."; return; }
        saved=true;
        status="Salvos "+std::to_string(snapshot.size())+" eventos em "+path.u8string();
    } catch(...) { status="Falha ao salvar a captura."; }
}
void render() {
    renderThread=GetCurrentThreadId();
    expirePending();
    if(!visible) return;
    ImGui::SetNextWindowSize(ImVec2(800,440),ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Waypoint Probe v0.8###MALLARD_WAYPOINT_PROBE",&visible)) {
        ImGui::TextWrapped("Observa a solicitação nativa de viagem e permite um teste direto após validar a chamada manual.");
        ImGui::Text("Destino: %s | confirmação: %s",setupHooked?"ativo":"indisponível",eventHooked?"ativa":"indisponível");
        if(supported&&setupHooked&&eventHooked&&simpleHooked&&routedHooked) {
            if(ImGui::Button("Iniciar / recomeçar (descarta captura atual)")) start();
            ImGui::SameLine(); if(ImGui::Button("Parar e salvar")) save();
        }
        {
            std::lock_guard<std::mutex> lock(captureMutex);
            if(capturing.load()&&GetTickCount64()>=deadline) capturing.store(false);
            ImGui::Text("Estado: %s | eventos: %zu | descartados: %u",capturing.load()?"capturando":"parado",records.size(),dropped);
            if(!records.empty()) {
                const auto& r=records.back();
                ImGui::Text("Último: %s | destino: %u | leitura: %s",r.event,r.waypoint,r.readOk?"OK":"falhou");
            }
        }
        ImGui::Text("Solicitação simples: %s | com rota: %s | thread do painel: %u",simpleHooked?"ativa":"indisponível",routedHooked?"ativa":"indisponível",renderThread);
        ImGui::Text("Thread da janela: %u | callback: %s | pedido: %s",windowThread.load(),windowCallbackRegistered?"ativo":"indisponível",pendingActive.load()?"na fila":"nenhum");
        for(size_t i=0;i<testWaypoints.size();++i) {
            const uint32_t id=testWaypoints[i];
            const bool ready=canQueue(id);
            DWORD manualThread=0;
            { std::lock_guard<std::mutex> lock(captureMutex); manualThread=verified[i].thread; }
            // Nexus's pinned ImGui predates BeginDisabled. Draw muted buttons and
            // ignore activation unless the independent native-call gate passes.
            if(!ready) ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*0.4f);
            const bool clicked=ImGui::Button(i==0?"Viajar direto para A (teste)":"Viajar direto para B (teste)");
            if(!ready) ImGui::PopStyleVar();
            if(clicked && ready) queueTravel(id);
            ImGui::SameLine(); ImGui::Text("Thread manual validada: %u",manualThread);
        }
        ImGui::TextWrapped("Primeiro viaje manualmente para A e B. Se os botões liberarem, teste A e depois B, aguardando cada chegada. Se ficarem desativados, salve o registro.");
        ImGui::Separator(); ImGui::TextWrapped("%s",status.c_str());
    }
    ImGui::End();
}
void options() { if(ImGui::Button("Abrir Waypoint Probe v0.8")) visible=true; }
void load(AddonAPI_t* nexus) {
    api=nexus;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(api->ImguiContext));
    ImGui::SetAllocatorFunctions(reinterpret_cast<void*(*)(size_t,void*)>(api->ImguiMalloc),reinterpret_cast<void(*)(void*,void*)>(api->ImguiFree));
    records.reserve(capacity);
    try { const char* path=api->Paths_GetAddonDirectory("WaypointProbe"); if(path) directory=std::filesystem::u8path(path); } catch(...) {}
    supported=supportedExecutable();
    if(supported&&api->MinHook_Create&&api->MinHook_Enable&&api->MinHook_Disable&&api->MinHook_Remove) {
        setupHooked=install(setupRva,setupBytes,sizeof(setupBytes),reinterpret_cast<void*>(&tracedSetup),reinterpret_cast<void**>(&originalSetup));
        eventHooked=install(eventRva,eventBytes,sizeof(eventBytes),reinterpret_cast<void*>(&tracedEvent),reinterpret_cast<void**>(&originalEvent));
        simpleHooked=install(simpleRva,simpleBytes,sizeof(simpleBytes),reinterpret_cast<void*>(&tracedSimple),reinterpret_cast<void**>(&originalSimple));
        routedHooked=install(routedRva,routedBytes,sizeof(routedBytes),reinterpret_cast<void*>(&tracedRouted),reinterpret_cast<void**>(&originalRouted));
    }
    status=setupHooked&&eventHooked&&simpleHooked&&routedHooked?"Pronto para o teste de confirmação.":"Captura indisponível: versão/assinatura do executável ou instalação dos hooks não corresponde.";
    dispatchMessage=RegisterWindowMessageW(L"Mallard.WaypointProbe.Dispatch.v8");
    if(dispatchMessage && api->WndProc_Register && api->WndProc_Deregister) {
        api->WndProc_Register(onWindowMessage); windowCallbackRegistered=true;
    }
    api->GUI_Register(RT_Render,render); api->GUI_Register(RT_OptionsRender,options);
}
void unload() {
    capturing.store(false);
    { std::lock_guard<std::mutex> lock(captureMutex); pendingActive.store(false); }
    if(windowCallbackRegistered) api->WndProc_Deregister(onWindowMessage);
    api->GUI_Deregister(render); api->GUI_Deregister(options);
    // AF_DisableHotloading: only remove while the game is shutting down.
    if(setupHooked) { auto* p=reinterpret_cast<unsigned char*>(host)+setupRva; api->MinHook_Disable(p); api->MinHook_Remove(p); }
    if(simpleHooked) { auto* p=reinterpret_cast<unsigned char*>(host)+simpleRva; api->MinHook_Disable(p); api->MinHook_Remove(p); }
    if(routedHooked) { auto* p=reinterpret_cast<unsigned char*>(host)+routedRva; api->MinHook_Disable(p); api->MinHook_Remove(p); }
    if(eventHooked) { auto* p=reinterpret_cast<unsigned char*>(host)+eventRva; api->MinHook_Disable(p); api->MinHook_Remove(p); }
}
}
extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    static AddonDefinition_t d{};
    d.Signature=static_cast<uint32_t>(-1847302962); d.APIVersion=NEXUS_API_VERSION;
    d.Name="Waypoint Probe"; d.Author="Mallard"; d.Description="Observa seleção e confirmação de waypoint na interface.";
    d.Version={0,8,0,0}; d.Load=load; d.Unload=unload; d.Flags=AF_DisableHotloading; d.Provider=UP_None;
    return &d;
}
