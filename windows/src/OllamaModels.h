//
//  OllamaModels.h — Ollama 已装模型列表、绑定模型预加载
//  （对应 macOS 的 OllamaModelResolver + ModelsSettingsView 数据源）。
//  A missing model is only ever reported (ProfileStatus), never replaced by another one.
//
//  线程约定：Settings 只在主线程读写。异步接口在调用线程拍好
//  已提交的 host/port 快照带进工作线程，结果 PostMessage 回主线程处理。
//
#pragma once
#include <windows.h>
#include "ProfileStatus.h"
#include <cstdint>
#include <string>
#include <vector>

namespace ollamamodels {

/// GET {host}:{port}/api/tags，返回已安装模型名（阻塞；host/port 由调用方快照传入）。
/// 失败返回空并填 error。
std::vector<std::wstring> ListInstalled(const std::string& host, int port, std::wstring* error);

/// One finished model discovery, posted back to the requesting window.
struct Discovery {
    profilestatus::Ticket ticket;  // as passed to FetchInstalledAsync; check it before use
    bool ok = false;
    std::vector<std::wstring> models;
    std::wstring error;
};

/// 异步拉取模型列表：主线程调用（内部拍已提交 Settings 的快照），完成后向 notify 投递
/// message（lParam = new Discovery*，接收方释放）。
void FetchInstalledAsync(HWND notify, UINT message, profilestatus::Ticket ticket);

/// Main thread: warms the Ollama models committed profiles actually bind, each once, for 10
/// minutes. With a known `installed` list, models that are not installed are skipped. A later
/// call supersedes a preload still in progress.
void PreloadAsync(const std::vector<std::wstring>* installed = nullptr);

/// Main thread: reacts to a discovered model list. It never changes or saves settings (a
/// missing model stays visible as missing); it only preloads bound, installed models.
void OnDiscovered(const std::vector<std::wstring>& installed);

/// Largest installed model that should fit comfortably in the supplied physical memory.
/// Returns the first model only when no tag exposes a parameter count such as `:7b`.
std::wstring RecommendedForMemory(const std::vector<std::wstring>& installed,
                                  uint64_t physicalMemoryBytes,
                                  double* maximumParameterBillions = nullptr);

} // namespace ollamamodels
