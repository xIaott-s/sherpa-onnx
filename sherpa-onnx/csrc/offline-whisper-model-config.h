// sherpa-onnx/csrc/offline-whisper-model-config.h
//
// Copyright (c)  2023  Xiaomi Corporation
#ifndef SHERPA_ONNX_CSRC_OFFLINE_WHISPER_MODEL_CONFIG_H_
#define SHERPA_ONNX_CSRC_OFFLINE_WHISPER_MODEL_CONFIG_H_

#include <string>

#include "sherpa-onnx/csrc/parse-options.h"

namespace sherpa_onnx {

struct OfflineWhisperModelConfig {
  std::string encoder;
  std::string decoder;

  OfflineWhisperModelConfig() = default;
  OfflineWhisperModelConfig(const std::string &encoder,
                            const std::string &decoder)
      : encoder(encoder), decoder(decoder) {}

  void Register(ParseOptions *po);
  bool Validate() const;

  std::string ToString() const;
};

}  // namespace sherpa_onnx

#endif  // SHERPA_ONNX_CSRC_OFFLINE_WHISPER_MODEL_CONFIG_H_
