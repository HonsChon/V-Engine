#include "DX12RHIShader.h"

#include <stdexcept>
#include <d3dcompiler.h>
#include <windows.h>
#include <vector>

namespace {

std::wstring toWide(const std::string& s) {
    if (s.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], size);
    return out;
}

std::wstring exeDirectory() {
    wchar_t buf[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0) {
        return {};
    }
    std::wstring path(buf, len);
    const size_t slash = path.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? L"." : path.substr(0, slash);
}

std::wstring joinPath(const std::wstring& dir, const std::wstring& rel) {
    if (dir.empty()) {
        return rel;
    }
    return dir + L"\\" + rel;
}

// Engine passes hard-code the Vulkan blob path ("shaders/X.spv"); on DX12 the
// matching DXIL blob is produced by the CMake "CompileDX12EngineShaders" target
// at "shaders_dx12/X.dxil". Translate when the as-given path fails to load.
std::string dxilFromSpvPath(const std::string& spvPath) {
    const std::string kShaderPrefix = "shaders/";
    if (spvPath.rfind(kShaderPrefix, 0) != 0 || spvPath.size() <= kShaderPrefix.size() + 4) {
        return {};
    }
    std::string rel = spvPath.substr(kShaderPrefix.size());
    if (rel.compare(rel.size() - 4, 4, ".spv") != 0) {
        return {};
    }
    rel = rel.substr(0, rel.size() - 4);
    return "shaders_dx12/" + rel + ".dxil";
}

} // namespace

DX12RHIShader::DX12RHIShader(DX12RHIDevice* device, RHIShaderStage stage,
                             const std::string& filePath,
                             const std::string& entryPoint)
    : device_(device), stage_(stage), entryPoint_(entryPoint)
{
    // Prefer the path as given (matches the process CWD), then fall back to the
    // executable's directory so the demo works regardless of the shell's CWD.
    // Engine passes written against the Vulkan blob layout carry ".spv" paths
    // that DO exist in bin/shaders next to the exe - those bytes are SPIR-V and
    // must never reach a DXIL consumer. When the path looks like a .spv blob,
    // the translated "shaders_dx12/X.dxil" candidate is tried FIRST.
    std::vector<std::string> pathCandidates;
    const std::string translated = dxilFromSpvPath(filePath);
    if (!translated.empty()) {
        pathCandidates.push_back(translated);
    }
    pathCandidates.push_back(filePath);

    for (const std::string& candidatePath : pathCandidates) {
        const std::wstring wideRel = toWide(candidatePath);
        std::vector<std::wstring> candidates;

        // Raw path only when absolute; otherwise try CWD first then exe dir.
        if (candidatePath.size() > 2 && candidatePath[1] == ':') {
            candidates.push_back(wideRel);
        } else {
            candidates.push_back(wideRel);                       // CWD (e.g. bin/ when launched from bin)
            candidates.push_back(joinPath(exeDirectory(), wideRel));
        }

        for (const auto& candidate : candidates) {
            if (SUCCEEDED(D3DReadFileToBlob(candidate.c_str(), &shaderBlob_))) {
                return;
            }
        }
    }
    throw std::runtime_error("[DX12RHIShader] Failed to load shader blob: " + filePath);
}

D3D12_SHADER_BYTECODE DX12RHIShader::getBytecode() const {
    D3D12_SHADER_BYTECODE bytecode = {};
    bytecode.pShaderBytecode = shaderBlob_->GetBufferPointer();
    bytecode.BytecodeLength = shaderBlob_->GetBufferSize();
    return bytecode;
}
