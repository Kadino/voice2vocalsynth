#include "Voice2VocalSynth/PhonemeOnnxRunner.h"

#include <string>
#include <utility>

#ifdef VOICE2VOCALSYNTH_WITH_ONNX
#include <onnxruntime_cxx_api.h>
#endif

namespace Voice2VocalSynth
{
namespace
{

[[nodiscard]] std::int64_t flatElementCount(const std::vector<std::int64_t>& shape)
{
    if (shape.empty()) {
        return 0;
    }
    std::int64_t product = 1;
    for (const auto dimension : shape) {
        if (dimension <= 0) {
            return -1;
        }
        product *= dimension;
    }
    return product;
}

} // namespace

struct PhonemeOnnxRunner::Impl
{
#ifdef VOICE2VOCALSYNTH_WITH_ONNX
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "Voice2VocalSynth"};
    Ort::SessionOptions session_options {};
    std::unique_ptr<Ort::Session> session {};
    std::vector<std::string> input_names_owned {};
    std::vector<std::string> output_names_owned {};
    std::vector<const char*> input_name_ptrs {};
    std::vector<const char*> output_name_ptrs {};
    std::vector<std::int64_t> input_shape {};
    std::int64_t input_elements = 0;
#endif
};

PhonemeOnnxRunner::PhonemeOnnxRunner() = default;

PhonemeOnnxRunner::~PhonemeOnnxRunner() = default;

PhonemeOnnxRunner::PhonemeOnnxRunner(PhonemeOnnxRunner&&) noexcept = default;

PhonemeOnnxRunner& PhonemeOnnxRunner::operator=(PhonemeOnnxRunner&&) noexcept = default;

bool PhonemeOnnxRunner::loaded() const noexcept
{
#ifdef VOICE2VOCALSYNTH_WITH_ONNX
    return static_cast<bool>(impl_) && static_cast<bool>(impl_->session);
#else
    return false;
#endif
}

bool PhonemeOnnxRunner::load(const std::filesystem::path& model_path, std::string& error)
{
#ifdef VOICE2VOCALSYNTH_WITH_ONNX
    error.clear();
    impl_ = std::make_unique<Impl>();

    try {
        impl_->session_options.SetIntraOpNumThreads(1);
        impl_->session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

        const auto path_string = model_path.string();
        impl_->session = std::make_unique<Ort::Session>(impl_->env, path_string.c_str(), impl_->session_options);
    } catch (const Ort::Exception& exception) {
        error = exception.what();
        impl_.reset();
        return false;
    } catch (const std::exception& exception) {
        error = exception.what();
        impl_.reset();
        return false;
    }

    try {
        Ort::AllocatorWithDefaultOptions allocator {};
        const std::size_t input_count = impl_->session->GetInputCount();
        const std::size_t output_count = impl_->session->GetOutputCount();
        if (input_count < 1 || output_count < 1) {
            error = "Model must declare at least one input and one output";
            impl_.reset();
            return false;
        }

        impl_->input_names_owned.reserve(input_count);
        impl_->input_name_ptrs.reserve(input_count);
        for (std::size_t i = 0; i < input_count; ++i) {
            auto name = impl_->session->GetInputNameAllocated(i, allocator);
            impl_->input_names_owned.emplace_back(name.get());
            impl_->input_name_ptrs.push_back(impl_->input_names_owned.back().c_str());
        }

        impl_->output_names_owned.reserve(output_count);
        impl_->output_name_ptrs.reserve(output_count);
        for (std::size_t i = 0; i < output_count; ++i) {
            auto name = impl_->session->GetOutputNameAllocated(i, allocator);
            impl_->output_names_owned.emplace_back(name.get());
            impl_->output_name_ptrs.push_back(impl_->output_names_owned.back().c_str());
        }

        const Ort::TypeInfo input_type = impl_->session->GetInputTypeInfo(0);
        const auto tensor_info = input_type.GetTensorTypeAndShapeInfo();
        impl_->input_shape = tensor_info.GetShape();
        impl_->input_elements = flatElementCount(impl_->input_shape);
        if (impl_->input_elements <= 0) {
            error = "First model input must use a fully static positive shape (dynamic axes not supported yet)";
            impl_.reset();
            return false;
        }
    } catch (const Ort::Exception& exception) {
        error = exception.what();
        impl_.reset();
        return false;
    }

    return true;
#else
    (void)model_path;
    error = "ONNX Runtime support was not enabled in this build (configure with VOICE2VOCALSYNTH_WITH_ONNX=ON and a valid ONNXRUNTIME_ROOT).";
    return false;
#endif
}

PhonemeOnnxRunner::RunResult PhonemeOnnxRunner::run(const std::vector<float>& input)
{
#ifdef VOICE2VOCALSYNTH_WITH_ONNX
    RunResult result;
    if (!loaded()) {
        result.error = "load() must succeed before run()";
        return result;
    }

    if (static_cast<std::int64_t>(input.size()) != impl_->input_elements) {
        result.error = "Input element count does not match model (expected " + std::to_string(impl_->input_elements) +
                       ", got " + std::to_string(input.size()) + ")";
        return result;
    }

    try {
        Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory,
            const_cast<float*>(input.data()),
            input.size(),
            impl_->input_shape.data(),
            impl_->input_shape.size());

        auto outputs = impl_->session->Run(Ort::RunOptions{nullptr},
                                             impl_->input_name_ptrs.data(),
                                             &input_tensor,
                                             1,
                                             impl_->output_name_ptrs.data(),
                                             impl_->output_name_ptrs.size());

        if (outputs.empty()) {
            result.error = "Session::Run returned no outputs";
            return result;
        }

        const float* output_data = outputs.front().GetTensorData<float>();
        const Ort::TensorTypeAndShapeInfo out_info = outputs.front().GetTensorTypeAndShapeInfo();
        result.output_shape = out_info.GetShape();
        const std::int64_t out_elements = flatElementCount(result.output_shape);
        if (out_elements <= 0) {
            result.error = "Unsupported dynamic or empty output shape";
            return result;
        }

        result.output.assign(output_data, output_data + out_elements);
        result.ok = true;
        return result;
    } catch (const Ort::Exception& exception) {
        RunResult result;
        result.error = exception.what();
        return result;
    } catch (const std::exception& exception) {
        RunResult result;
        result.error = exception.what();
        return result;
    }
#else
    (void)input;
    RunResult result;
    result.error = "ONNX Runtime support was not enabled in this build";
    return result;
#endif
}

} // namespace Voice2VocalSynth
