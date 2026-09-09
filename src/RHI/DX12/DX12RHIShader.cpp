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

} // namespace

DX12RHIShader::DX12RHIShader(DX12RHIDevice* device, RHIShaderStage stage,
                             const std::string& filePath,
                             const std::string& entryPoint)
    : device_(device), stage_(stage), entryPoint_(entryPoint)
{
    // Prefer the path as given (matches the process CWD), then fall back to the
    // executable's directory so the demo works regardless of the shell's CWD.
    const std::wstring wideRel = toWide(filePath);
    std::vector<std::wstring> candidates;

    // Raw path only when absolute; otherwise try CWD first then exe dir.
    if (filePath.size() > 2 && filePath[1] == ':') {
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
    throw std::runtime_error("[DX12RHIShader] Failed to load shader blob: " + filePath);
}

D3D12_SHADER_BYTECODE DX12RHIShader::getBytecode() const {
    D3D12_SHADER_BYTECODE bytecode = {};
    bytecode.pShaderBytecode = shaderBlob_->GetBufferPointer();
    bytecode.BytecodeLength = shaderBlob_->GetBufferSize();
    return bytecode;
}
