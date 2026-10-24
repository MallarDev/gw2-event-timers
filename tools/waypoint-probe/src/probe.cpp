#include <winsock2.h>
#include <windows.h>
#include "Nexus.h"
#include "imgui.h"
#include "MinHook.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
constexpr DWORD expectedTimestamp = 0x6ad7acf4;
constexpr DWORD expectedImageSize = 0x02c49000;
constexpr size_t capacity = 16384;
constexpr int framesPerCall = 12;
constexpr const char* windowName = "Waypoint Probe###MALLARD_WAYPOINT_PROBE";
constexpr const char* markerBind = "MALLARD_WAYPOINT_PROBE_MARK";
constexpr size_t preSendRva = 0x00fe9e60;
constexpr unsigned char preSendBytes[] = {0x48, 0x8b, 0x49, 0x30, 0x45, 0x8b, 0xc8,
                                          0x4c, 0x8b, 0xc2, 0x41, 0x8b, 0xd1};
using SendFn = int (WSAAPI*)(SOCKET, const char*, int, int);
using SendToFn = int (WSAAPI*)(SOCKET, const char*, int, int, const sockaddr*, int);
using PreSendFn = uintptr_t (*)(void*, const void*, unsigned);

struct Record {
    std::atomic<bool> ready{false};
    ULONGLONG tick{};
    DWORD thread{};
    unsigned char phase{};
    char kind{};
    int length{};
    std::array<uint64_t, framesPerCall> rva{};
};
std::array<Record, capacity> records{};
std::atomic<size_t> nextRecord{0};
std::atomic<unsigned> inFlight{0};
std::atomic<unsigned> activePhase{0};
std::atomic<ULONGLONG> deadline{0};
std::atomic<unsigned> dropped{0};
std::atomic<unsigned> markers{0};
AddonAPI_t* api = nullptr;
HMODULE host = nullptr;
size_t hostSize = 0;
void** sendSlot = nullptr;
void** sendToSlot = nullptr;
SendFn originalSend = nullptr;
SendToFn originalSendTo = nullptr;
PreSendFn originalPreSend = nullptr;
void* preSendAddress = nullptr;
bool minHookInitialized = false;
bool preSendHooked = false;
bool visible = true;
bool supported = false;
bool hooked = false;
bool baselineSaved = false;
bool travelSaved = false;
std::filesystem::path directory;
std::string status;

bool supportedExecutable() {
    host = GetModuleHandleW(nullptr);
    if (!host) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(host);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(reinterpret_cast<const char*>(host) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return false;
    hostSize = nt->OptionalHeader.SizeOfImage;
    return nt->FileHeader.TimeDateStamp == expectedTimestamp && hostSize == expectedImageSize;
}

// Called once on the Nexus load thread. Only the main executable's imported functions are changed.
void** findImport(const char* function, WORD ordinal) {
    const auto* base = reinterpret_cast<const unsigned char*>(host);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress || dir.VirtualAddress >= hostSize || dir.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR) ||
        static_cast<size_t>(dir.VirtualAddress) + dir.Size > hostSize) return nullptr;
    const auto* imports = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
    const size_t count = dir.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);
    for (size_t i = 0; i < count && imports[i].Name; ++i) {
        const auto& imp = imports[i];
        if (imp.Name >= hostSize || !imp.OriginalFirstThunk || !imp.FirstThunk ||
            imp.OriginalFirstThunk >= hostSize || imp.FirstThunk >= hostSize) continue;
        if (_stricmp(reinterpret_cast<const char*>(base + imp.Name), "WS2_32.dll") != 0) continue;
        const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + imp.OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(const_cast<unsigned char*>(base) + imp.FirstThunk);
        const size_t n = (hostSize - imp.OriginalFirstThunk) / sizeof(IMAGE_THUNK_DATA64);
        const size_t m = (hostSize - imp.FirstThunk) / sizeof(IMAGE_THUNK_DATA64);
        for (size_t j = 0; j < n && j < m && names[j].u1.AddressOfData; ++j) {
            const auto value = names[j].u1.Ordinal;
            const bool ordinalMatch = IMAGE_SNAP_BY_ORDINAL64(value) && IMAGE_ORDINAL64(value) == ordinal;
            bool nameMatch = false;
            if (!IMAGE_SNAP_BY_ORDINAL64(value) && value < hostSize && value + sizeof(IMAGE_IMPORT_BY_NAME) < hostSize) {
                const auto* imported = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + value);
                nameMatch = std::strcmp(imported->Name, function) == 0;
            }
            if (ordinalMatch || nameMatch) return reinterpret_cast<void**>(&slots[j].u1.Function);
        }
    }
    return nullptr;
}

bool replaceImport(void** slot, void* replacement, void** prior) {
    DWORD protection = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection)) return false;
    // Publish the original target before another thread can enter the hook.
    *prior = *slot;
    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(slot), replacement);
    DWORD unused = 0;
    VirtualProtect(slot, sizeof(void*), protection, &unused);
    return true;
}

void recordCall(char kind, int length) {
    const unsigned phase = activePhase.load(std::memory_order_acquire);
    if (!phase) return;
    inFlight.fetch_add(1, std::memory_order_acq_rel);
    if (activePhase.load(std::memory_order_acquire) == phase && GetTickCount64() < deadline.load(std::memory_order_acquire)) {
        const size_t index = nextRecord.fetch_add(1, std::memory_order_relaxed);
        if (index < capacity) {
            Record& r = records[index];
            r.tick = GetTickCount64();
            r.thread = GetCurrentThreadId();
            r.phase = static_cast<unsigned char>(phase);
            r.kind = kind;
            r.length = length;
            if (kind != 'M') {
                void* stack[framesPerCall]{};
                const USHORT count = CaptureStackBackTrace(2, framesPerCall, stack, nullptr);
                const uintptr_t base = reinterpret_cast<uintptr_t>(host);
                for (USHORT i = 0; i < count; ++i) {
                    const uintptr_t p = reinterpret_cast<uintptr_t>(stack[i]);
                    r.rva[i] = p >= base && p - base < hostSize ? static_cast<uint64_t>(p - base) : 0;
                }
            }
            r.ready.store(true, std::memory_order_release);
            if (kind == 'M') markers.fetch_add(1, std::memory_order_relaxed);
        } else dropped.fetch_add(1, std::memory_order_relaxed);
    }
    inFlight.fetch_sub(1, std::memory_order_release);
}

int WSAAPI tracedSend(SOCKET socket, const char* data, int length, int flags) {
    recordCall('S', length);
    return originalSend(socket, data, length, flags);
}
int WSAAPI tracedSendTo(SOCKET socket, const char* data, int length, int flags, const sockaddr* address, int addressLength) {
    recordCall('T', length);
    return originalSendTo(socket, data, length, flags, address, addressLength);
}

uintptr_t tracedPreSend(void* object, const void* bytes, unsigned length) {
    recordCall('P', static_cast<int>(length));
    return originalPreSend(object, bytes, length);
}

void markConfirmation() {
    if (activePhase.load(std::memory_order_acquire) != 2) {
        status = "Inicie a captura de viagem antes de marcar a confirmação.";
        return;
    }
    recordCall('M', 0);
}

void onMarkerBind(const char*, bool release) {
    if (!release && activePhase.load(std::memory_order_acquire) == 2) recordCall('M', 0);
}

void installPreSendHook() {
    if (preSendRva + sizeof(preSendBytes) > hostSize) return;
    preSendAddress = reinterpret_cast<unsigned char*>(host) + preSendRva;
    if (std::memcmp(preSendAddress, preSendBytes, sizeof(preSendBytes)) != 0) return;
    if (MH_Initialize() != MH_OK) return;
    minHookInitialized = true;
    if (MH_CreateHook(preSendAddress, reinterpret_cast<void*>(&tracedPreSend),
                      reinterpret_cast<void**>(&originalPreSend)) != MH_OK) return;
    if (MH_EnableHook(preSendAddress) == MH_OK) preSendHooked = true;
    else { MH_RemoveHook(preSendAddress); originalPreSend = nullptr; }
}

void stop() { activePhase.store(0, std::memory_order_release); }

void save(unsigned phase) {
    if (activePhase.load(std::memory_order_acquire) != 0) { status = "Pare a captura antes de salvar."; return; }
    if (inFlight.load(std::memory_order_acquire) != 0) { status = "Aguardando chamadas pendentes; clique em salvar novamente."; return; }
    if (directory.empty()) { status = "Nexus não forneceu um diretório de captura."; return; }
    try {
        std::filesystem::create_directories(directory);
        const auto path = directory / (phase == 1 ? "baseline_v2.tsv" : "waypoint_v2.tsv");
        std::ofstream out(path, std::ios::trunc);
        if (!out) { status = "Não foi possível abrir o arquivo de captura."; return; }
        out << "phase\ttick_ms\tthread\tapi\tbytes";
        for (int i = 0; i < framesPerCall; ++i) out << "\tgw2_rva_" << i;
        out << '\n';
        size_t count = 0;
        const size_t end = (std::min)(nextRecord.load(std::memory_order_acquire), capacity);
        for (size_t i = 0; i < end; ++i) {
            const Record& r = records[i];
            if (!r.ready.load(std::memory_order_acquire) || r.phase != phase) continue;
            out << static_cast<int>(r.phase) << '\t' << r.tick << '\t' << r.thread << '\t' << r.kind << '\t' << r.length;
            for (const auto rva : r.rva) {
                char hex[24]{};
                if (rva) std::snprintf(hex, sizeof(hex), "%llX", static_cast<unsigned long long>(rva));
                out << '\t' << hex;
            }
            out << '\n';
            ++count;
        }
        out.close();
        if (!out) { status = "Falha ao terminar a gravação do arquivo."; return; }
        if (phase == 1) baselineSaved = true; else travelSaved = true;
        status = "Salvos " + std::to_string(count) + " registros em " + path.u8string();
    } catch (...) { status = "Falha ao salvar a captura no diretório do addon."; }
}

void start(unsigned phase, ULONGLONG seconds) {
    if (activePhase.load(std::memory_order_acquire)) { status = "Pare a captura atual primeiro."; return; }
    if (inFlight.load(std::memory_order_acquire)) { status = "Aguarde as chamadas pendentes."; return; }
    if (phase == 1 && baselineSaved) { status = "A captura de repouso já foi salva nesta sessão."; return; }
    if (phase == 2 && travelSaved) { status = "A captura da viagem já foi salva nesta sessão."; return; }
    deadline.store(GetTickCount64() + seconds * 1000, std::memory_order_release);
    activePhase.store(phase, std::memory_order_release);
    status = phase == 1 ? "Registrando repouso por 10 segundos." : "Registrando viagem por 30 segundos. Viaje manualmente agora.";
}

void render() {
    if (!visible) return;
    ImGui::SetNextWindowSize(ImVec2(680, 290), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(windowName, &visible)) {
        ImGui::TextWrapped("Diagnóstico de send/sendto e preparação de envio. Não registra conteúdo dos pacotes.");
        if (!supported) ImGui::TextWrapped("Executável diferente do GW2 fornecido: captura desativada.");
        else if (!hooked) ImGui::TextWrapped("Não foi possível instalar a captura nos imports de WS2_32.");
        else {
            const unsigned phase = activePhase.load(std::memory_order_acquire);
            if (phase && GetTickCount64() >= deadline.load(std::memory_order_acquire)) stop();
            if (ImGui::Button("Capturar repouso (10 s)")) start(1, 10);
            ImGui::SameLine();
            if (ImGui::Button("Capturar viagem (30 s)")) start(2, 30);
            ImGui::SameLine();
            if (ImGui::Button("Parar")) stop();
            if (ImGui::Button("Salvar repouso")) save(1);
            ImGui::SameLine();
            if (ImGui::Button("Salvar viagem")) save(2);
            if (ImGui::Button("Marcar confirmação (Ctrl+Alt+M)")) markConfirmation();
            ImGui::Text("Estado: %s | chamadas: %zu / %zu | descartadas: %u",
                activePhase.load() == 1 ? "repouso" : activePhase.load() == 2 ? "viagem" : "parado",
                (std::min)(nextRecord.load(), capacity), capacity, dropped.load());
            ImGui::Text("Marcas: %u | preparação de envio: %s", markers.load(),
                preSendHooked ? "ativa" : "indisponível (send ainda ativo)");
            ImGui::TextWrapped("Cada fase pode ser salva uma vez por sessão; reinicie o jogo para repetir.");
        }
        ImGui::Separator();
        ImGui::TextWrapped("%s", status.c_str());
    }
    ImGui::End();
}

void options() {
    if (ImGui::Button("Abrir Waypoint Probe")) visible = true;
    ImGui::TextWrapped("Marque a confirmação com Ctrl+Alt+M imediatamente antes de clicar em Viajar. As capturas ficam no diretório WaypointProbe dos addons do Nexus.");
}

void load(AddonAPI_t* nexus) {
    api = nexus;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(api->ImguiContext));
    ImGui::SetAllocatorFunctions(reinterpret_cast<void*(*)(size_t, void*)>(api->ImguiMalloc),
                                 reinterpret_cast<void(*)(void*, void*)>(api->ImguiFree));
    try { directory = std::filesystem::u8path(api->Paths_GetAddonDirectory("WaypointProbe")); }
    catch (...) { directory.clear(); }
    supported = supportedExecutable();
    if (directory.empty()) status = "Nexus não forneceu um diretório de captura.";
    if (supported) {
        installPreSendHook();
        sendSlot = findImport("send", 19);
        sendToSlot = findImport("sendto", 20);
        if (sendSlot && replaceImport(sendSlot, reinterpret_cast<void*>(&tracedSend), reinterpret_cast<void**>(&originalSend))) {
            hooked = true;
            if (sendToSlot && !replaceImport(sendToSlot, reinterpret_cast<void*>(&tracedSendTo), reinterpret_cast<void**>(&originalSendTo)))
                status = "Apenas send foi instrumentado; sendto indisponível.";
        }
        if (!hooked) status = "Import send não encontrado ou não modificável.";
    } else status = "Assinatura do executável não corresponde ao arquivo analisado.";
    api->InputBinds_RegisterWithString(markerBind, onMarkerBind, "CTRL+ALT+M");
    api->GUI_Register(RT_Render, render);
    api->GUI_Register(RT_OptionsRender, options);
}

void unload() {
    stop();
    if (api) {
        api->GUI_Deregister(render); api->GUI_Deregister(options);
        api->InputBinds_Deregister(markerBind);
    }
    // Hotloading is disabled; this callback is reached as the game is shutting down.
    void* ignored = nullptr;
    if (sendSlot && originalSend) replaceImport(sendSlot, reinterpret_cast<void*>(originalSend), &ignored);
    if (sendToSlot && originalSendTo) replaceImport(sendToSlot, reinterpret_cast<void*>(originalSendTo), &ignored);
    if (preSendHooked) MH_DisableHook(preSendAddress);
    if (minHookInitialized) MH_Uninitialize();
}
}

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    static AddonDefinition_t definition{};
    definition.Signature = static_cast<uint32_t>(-1847302962);
    definition.APIVersion = NEXUS_API_VERSION;
    definition.Name = "Waypoint Probe";
    definition.Author = "Mallard";
    definition.Description = "Captura diagnóstica de chamadas de rede durante viagem manual.";
    definition.Version = {0, 2, 0, 0};
    definition.Load = load; definition.Unload = unload;
    definition.Flags = AF_DisableHotloading;
    definition.Provider = UP_None;
    return &definition;
}
