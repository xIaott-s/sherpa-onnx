// sherpa-onnx/csrc/offline-whisper-model.cc
//
// Copyright (c)  2022-2023  Xiaomi Corporation

#include "sherpa-onnx/csrc/offline-whisper-model.h"

#include <algorithm>
#include <string>
#include <tuple>
#include <utility>

#include "sherpa-onnx/csrc/macros.h"
#include "sherpa-onnx/csrc/onnx-utils.h"
#include "sherpa-onnx/csrc/session.h"
#include "sherpa-onnx/csrc/text-utils.h"

namespace sherpa_onnx {

class OfflineWhisperModel::Impl {
 public:
  explicit Impl(const OfflineModelConfig &config)
      : config_(config),
        env_(ORT_LOGGING_LEVEL_ERROR),
        sess_opts_(GetSessionOptions(config)),
        allocator_{} {
    {
      auto buf = ReadFile(config.whisper.encoder);
      InitEncoder(buf.data(), buf.size());
    }

    {
      auto buf = ReadFile(config.whisper.decoder);
      InitDecoder(buf.data(), buf.size());
    }
  }

  std::pair<Ort::Value, Ort::Value> ForwardEncoder(Ort::Value features) {
    auto encoder_out = encoder_sess_->Run(
        {}, encoder_input_names_ptr_.data(), &features, 1,
        encoder_output_names_ptr_.data(), encoder_output_names_ptr_.size());

    return {std::move(encoder_out[0]), std::move(encoder_out[1])};
  }

  std::tuple<Ort::Value, Ort::Value, Ort::Value, Ort::Value, Ort::Value,
             Ort::Value>
  ForwardDecoder(Ort::Value tokens, Ort::Value n_layer_self_k_cache,
                 Ort::Value n_layer_self_v_cache, Ort::Value n_layer_cross_k,
                 Ort::Value n_layer_cross_v, Ort::Value offset) {
    std::array<Ort::Value, 6> decoder_input = {std::move(tokens),
                                               std::move(n_layer_self_k_cache),
                                               std::move(n_layer_self_v_cache),
                                               std::move(n_layer_cross_k),
                                               std::move(n_layer_cross_v),
                                               std::move(offset)};

    auto decoder_out = decoder_sess_->Run(
        {}, decoder_input_names_ptr_.data(), decoder_input.data(),
        decoder_input.size(), decoder_output_names_ptr_.data(),
        decoder_output_names_ptr_.size());

    return {std::move(decoder_out[0]),   std::move(decoder_out[1]),
            std::move(decoder_out[2]),   std::move(decoder_input[3]),
            std::move(decoder_input[4]), std::move(decoder_input[5])};
  }

  std::pair<Ort::Value, Ort::Value> GetInitialSelfKVCache() {
    std::array<int64_t, 4> shape{n_text_layer_, 1, n_text_ctx_, n_text_state_};

    Ort::Value n_layer_self_k_cache = Ort::Value::CreateTensor<float>(
        Allocator(), shape.data(), shape.size());

    Ort::Value n_layer_self_v_cache = Ort::Value::CreateTensor<float>(
        Allocator(), shape.data(), shape.size());

    auto n = shape[0] * shape[1] * shape[2] * shape[3];

    float *p_k = n_layer_self_k_cache.GetTensorMutableData<float>();
    float *p_v = n_layer_self_v_cache.GetTensorMutableData<float>();

    memset(p_k, 0, sizeof(float) * n);
    memset(p_v, 0, sizeof(float) * n);

    return {std::move(n_layer_self_k_cache), std::move(n_layer_self_v_cache)};
  }

  OrtAllocator *Allocator() const { return allocator_; }

  const std::vector<int64_t> &GetInitialTokens() const { return sot_sequence_; }

  int32_t EOT() const { return eot_; }

  int32_t TextCtx() const { return n_text_ctx_; }

 private:
  void InitEncoder(void *model_data, size_t model_data_length) {
    encoder_sess_ = std::make_unique<Ort::Session>(
        env_, model_data, model_data_length, sess_opts_);

    GetInputNames(encoder_sess_.get(), &encoder_input_names_,
                  &encoder_input_names_ptr_);

    GetOutputNames(encoder_sess_.get(), &encoder_output_names_,
                   &encoder_output_names_ptr_);

    // get meta data
    Ort::ModelMetadata meta_data = encoder_sess_->GetModelMetadata();
    if (config_.debug) {
      std::ostringstream os;
      os << "---encoder---\n";
      PrintModelMetadata(os, meta_data);
      SHERPA_ONNX_LOGE("%s\n", os.str().c_str());
    }

    Ort::AllocatorWithDefaultOptions allocator;  // used in the macro below
    SHERPA_ONNX_READ_META_DATA(n_text_layer_, "n_text_layer");
    SHERPA_ONNX_READ_META_DATA(n_text_ctx_, "n_text_ctx");
    SHERPA_ONNX_READ_META_DATA(n_text_state_, "n_text_state");
    SHERPA_ONNX_READ_META_DATA(sot_, "sot");
    SHERPA_ONNX_READ_META_DATA(eot_, "eot");
    SHERPA_ONNX_READ_META_DATA(blank_, "blank_id");
    SHERPA_ONNX_READ_META_DATA(translate_, "translate");
    SHERPA_ONNX_READ_META_DATA(no_timestamps_, "no_timestamps");
    SHERPA_ONNX_READ_META_DATA(no_speech_, "no_speech");
    SHERPA_ONNX_READ_META_DATA_VEC(sot_sequence_, "sot_sequence");
  }

  void InitDecoder(void *model_data, size_t model_data_length) {
    decoder_sess_ = std::make_unique<Ort::Session>(
        env_, model_data, model_data_length, sess_opts_);

    GetInputNames(decoder_sess_.get(), &decoder_input_names_,
                  &decoder_input_names_ptr_);

    GetOutputNames(decoder_sess_.get(), &decoder_output_names_,
                   &decoder_output_names_ptr_);
  }

 private:
  OfflineModelConfig config_;
  Ort::Env env_;
  Ort::SessionOptions sess_opts_;
  Ort::AllocatorWithDefaultOptions allocator_;

  std::unique_ptr<Ort::Session> encoder_sess_;
  std::unique_ptr<Ort::Session> decoder_sess_;

  std::vector<std::string> encoder_input_names_;
  std::vector<const char *> encoder_input_names_ptr_;

  std::vector<std::string> encoder_output_names_;
  std::vector<const char *> encoder_output_names_ptr_;

  std::vector<std::string> decoder_input_names_;
  std::vector<const char *> decoder_input_names_ptr_;

  std::vector<std::string> decoder_output_names_;
  std::vector<const char *> decoder_output_names_ptr_;

  // model meta data
  int32_t n_text_layer_;
  int32_t n_text_ctx_;
  int32_t n_text_state_;
  int32_t sot_;
  int32_t eot_;
  int32_t blank_;
  int32_t translate_;
  int32_t no_timestamps_;
  int32_t no_speech_;
  std::vector<int64_t> sot_sequence_;
};

OfflineWhisperModel::OfflineWhisperModel(const OfflineModelConfig &config)
    : impl_(std::make_unique<Impl>(config)) {}

OfflineWhisperModel::~OfflineWhisperModel() = default;

std::pair<Ort::Value, Ort::Value> OfflineWhisperModel::ForwardEncoder(
    Ort::Value features) {
  return impl_->ForwardEncoder(std::move(features));
}

std::tuple<Ort::Value, Ort::Value, Ort::Value, Ort::Value, Ort::Value,
           Ort::Value>
OfflineWhisperModel::ForwardDecoder(Ort::Value tokens,
                                    Ort::Value n_layer_self_k_cache,
                                    Ort::Value n_layer_self_v_cache,
                                    Ort::Value n_layer_cross_k,
                                    Ort::Value n_layer_cross_v,
                                    Ort::Value offset) {
  return impl_->ForwardDecoder(
      std::move(tokens), std::move(n_layer_self_k_cache),
      std::move(n_layer_self_v_cache), std::move(n_layer_cross_k),
      std::move(n_layer_cross_v), std::move(offset));
}

std::pair<Ort::Value, Ort::Value> OfflineWhisperModel::GetInitialSelfKVCache() {
  return impl_->GetInitialSelfKVCache();
}

OrtAllocator *OfflineWhisperModel::Allocator() const {
  return impl_->Allocator();
}

const std::vector<int64_t> &OfflineWhisperModel::GetInitialTokens() const {
  return impl_->GetInitialTokens();
}

int32_t OfflineWhisperModel::EOT() const { return impl_->EOT(); }

int32_t OfflineWhisperModel::TextCtx() const { return impl_->TextCtx(); }

}  // namespace sherpa_onnx
