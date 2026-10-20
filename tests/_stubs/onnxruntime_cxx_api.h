#pragma once
#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <cstdint>

#define OrtArenaAllocator 0
#define OrtMemTypeDefault 0
#define ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT 1
#define ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64 7

enum OrtLoggingLevel {
    ORT_LOGGING_LEVEL_VERBOSE = 0,
    ORT_LOGGING_LEVEL_INFO = 1,
    ORT_LOGGING_LEVEL_WARNING = 2,
    ORT_LOGGING_LEVEL_ERROR = 3,
    ORT_LOGGING_LEVEL_FATAL = 4
};

enum class ExecutionMode {
    ORT_SEQUENTIAL = 0,
    ORT_PARALLEL = 1
};

enum class GraphOptimizationLevel {
    ORT_DISABLE_ALL = 0,
    ORT_ENABLE_BASIC = 1,
    ORT_ENABLE_EXTENDED = 2,
    ORT_ENABLE_ALL = 99
};

using ONNXTensorElementDataType = int;

namespace Ort {

struct Env {
    Env(OrtLoggingLevel = ORT_LOGGING_LEVEL_WARNING, const char* = "") {}
};

struct SessionOptions {
    void SetIntraOpNumThreads(unsigned int) {}
    void SetExecutionMode(ExecutionMode) {}
    void EnableCpuMemArena() {}
    void SetGraphOptimizationLevel(GraphOptimizationLevel) {}
};

struct TensorTypeAndShapeInfo {
    std::vector<int64_t> GetShape() const { return {1, 1, 512, 512}; }
    ONNXTensorElementDataType GetElementType() const { return ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT; }
    size_t GetElementCount() const { return 0; }
};

struct TypeInfo {
    TensorTypeAndShapeInfo GetTensorTypeAndShapeInfo() const { return {}; }
};

struct MemoryInfo {
    static MemoryInfo CreateCpu(int, int) { return {}; }
};

struct RunOptions {
    void* ptr = nullptr;
};

struct Value {
    template<typename T>
    static Value CreateTensor(const MemoryInfo&, T*, size_t, const int64_t*, size_t) { return {}; }

    template<typename T>
    T* GetTensorMutableData() { return nullptr; }

    template<typename T>
    const T* GetTensorData() const { return nullptr; }

    TensorTypeAndShapeInfo GetTensorTypeAndShapeInfo() const { return {}; }
};

struct AllocatorWithDefaultOptions {
    void* Get() { return nullptr; }
};

struct AllocatedStringPtr {
    std::string s;
    const char* get() const { return s.c_str(); }
};

class Session {
public:
    template<typename PathT>
    Session(Env&, const PathT*, const SessionOptions&) {}
    size_t GetInputCount() const { return 1; }
    size_t GetOutputCount() const { return 3; }
    TypeInfo GetInputTypeInfo(size_t) const { return {}; }
    AllocatedStringPtr GetInputNameAllocated(size_t, const AllocatorWithDefaultOptions&) const { return {"input"}; }
    AllocatedStringPtr GetOutputNameAllocated(size_t, const AllocatorWithDefaultOptions&) const { return {"output"}; }
    std::vector<Value> Run(const RunOptions&, const char* const*, const Value*, size_t, const char* const*, size_t) {
        return {};
    }
};

} // namespace Ort
