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
    bool readOk{};
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
void append(unsigned token, const Record& record) {
    std::lock_guard<std::mutex> lock(captureMutex);
    if (!token || session != token || !capturing.load() || GetTickCount64() >= deadline) return;
    if (records.size() < capacity) records.push_back(record); else ++dropped;
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
    records.clear(); dropped=0; saved=false;
    deadline=GetTickCount64()+180000; capturing.store(true);
    status="Captura iniciada por até 3 minutos. Abra A e cancele; abra A e confirme; abra B e confirme.";
}
void save() {
    std::vector<Record> snapshot;
    unsigned lost=0;
    {
        std::lock_guard<std::mutex> lock(captureMutex);
        capturing.store(false);
        if(saved) { status="Inicie uma nova captura antes de salvar."; return; }
        snapshot=records; lost=dropped;
    }
    if(directory.empty()) { status="Diretório de captura indisponível."; return; }
    try {
        std::filesystem::create_directories(directory);
        std::filesystem::path path;
        do { path=directory/("waypoint_ui_v6_"+std::to_string(++savedNumber)+".tsv"); }
        while(std::filesystem::exists(path));
        std::ofstream out(path,std::ios::trunc);
        if(!out) { status="Falha ao abrir arquivo para salvar."; return; }
        out<<"event\ttick_ms\tthread\twaypoint_id\tread_ok\tcontrol_id\tevent_subtype\tsetup_active\tevent_active\tdropped";
        for(int i=0;i<6;++i) out<<"\tgw2_rva_"<<i;
        out<<'\n';
        // Include hook health even when no candidate was hit.
        out<<"STATUS\t"<<GetTickCount64()<<"\t0\t0\t0\t0\t0\t"<<setupHooked<<'\t'<<eventHooked<<'\t'<<lost;
        for(int i=0;i<6;++i) out<<'\t';
        out<<'\n';
        for(const auto& r:snapshot) {
            out<<r.event<<'\t'<<r.tick<<'\t'<<r.thread<<'\t'<<r.waypoint<<'\t'<<r.readOk<<'\t'<<r.control<<'\t'<<r.subtype<<'\t'<<setupHooked<<'\t'<<eventHooked<<'\t'<<lost;
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
    if(!visible) return;
    ImGui::SetNextWindowSize(ImVec2(720,300),ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Waypoint Probe v0.6###MALLARD_WAYPOINT_PROBE",&visible)) {
        ImGui::TextWrapped("Observa o destino e os botões do diálogo de confirmação. Os eventos são registrados automaticamente.");
        ImGui::Text("Destino: %s | confirmação: %s",setupHooked?"ativo":"indisponível",eventHooked?"ativa":"indisponível");
        if(supported&&setupHooked&&eventHooked) {
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
        ImGui::TextWrapped("Teste: A → cancelar pelo botão; A → confirmar; B → confirmar. Não precisa marcar nem apertar atalho.");
        ImGui::Separator(); ImGui::TextWrapped("%s",status.c_str());
    }
    ImGui::End();
}
void options() { if(ImGui::Button("Abrir Waypoint Probe v0.6")) visible=true; }
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
    }
    status=setupHooked&&eventHooked?"Pronto para o teste de confirmação.":"Captura indisponível: versão/assinatura do executável ou instalação dos hooks não corresponde.";
    api->GUI_Register(RT_Render,render); api->GUI_Register(RT_OptionsRender,options);
}
void unload() {
    capturing.store(false);
    api->GUI_Deregister(render); api->GUI_Deregister(options);
    // AF_DisableHotloading: only remove while the game is shutting down.
    if(setupHooked) { auto* p=reinterpret_cast<unsigned char*>(host)+setupRva; api->MinHook_Disable(p); api->MinHook_Remove(p); }
    if(eventHooked) { auto* p=reinterpret_cast<unsigned char*>(host)+eventRva; api->MinHook_Disable(p); api->MinHook_Remove(p); }
}
}
extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    static AddonDefinition_t d{};
    d.Signature=static_cast<uint32_t>(-1847302962); d.APIVersion=NEXUS_API_VERSION;
    d.Name="Waypoint Probe"; d.Author="Mallard"; d.Description="Observa seleção e confirmação de waypoint na interface.";
    d.Version={0,6,0,0}; d.Load=load; d.Unload=unload; d.Flags=AF_DisableHotloading; d.Provider=UP_None;
    return &d;
}
