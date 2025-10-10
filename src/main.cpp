#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudioTypes.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <csignal>
#include <thread>
#include <chrono>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <memory>
#include <iostream>
#include <unistd.h>
#include <sys/select.h>
#include "instruments/kick/KickSynth.hpp"
#include "offline/OfflineRenderer.hpp"
#include "offline/OfflineGraphRenderer.hpp"
#include "io/AudioFileWriter.hpp"
#include "realtime/RealtimeRenderer.hpp"
#include "realtime/RealtimeGraphRenderer.hpp"
#include "core/Graph.hpp"
#include "core/GraphConfig.hpp"
#include "core/NodeFactory.hpp"
#include "core/MixerNode.hpp"
#include "instruments/kick/KickNode.hpp"

// Use KickSynth (from dsp/) for both realtime and offline paths

// Realtime callback now lives in RealtimeRenderer

static std::atomic<bool> gRunning{true};

static void onSigInt(int) {
  gRunning.store(false);
}

// non-blocking stdin check using select
static bool isStdinReady() {
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(STDIN_FILENO, &readfds);
  timeval tv{0, 0};
  int rv = select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &tv);
  return (rv > 0) && FD_ISSET(STDIN_FILENO, &readfds);
}

static void printUsage(const char* exe) {
  std::fprintf(stderr,
               "Usage: %s [--f0 Hz] [--fend Hz] [--pitch-decay ms] [--amp-decay ms]\n"
               "          [--gain 0..1] [--bpm N] [--duration sec] [--click 0..1]\n"
               "          [--wav path] [--sr Hz] [--pcm16] [--format wav|aiff|caf] [--bitdepth 16|24|32f]\n"
               "          [--graph path.json]\n"
               "\n"
               "Examples:\n"
               "  %s                       # one-shot, defaults (real-time)\n"
               "  %s --bpm 120            # 120 BPM continuous till Ctrl-C (real-time)\n"
                "  %s --wav out.wav --sr 44100 --duration 2.0  # offline render to WAV\n",
               exe, exe, exe, exe);
}

int main(int argc, char** argv) {
  KickParams params;
  std::string wavPath;
  std::string outFormat = "wav";
  std::string bitDepth = "32f";
  std::string graphPath;
  double offlineSr = 48000.0;
  bool pcm16 = false;

  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    auto need = [&](int remain) {
      if (i + remain >= argc) {
        printUsage(argv[0]);
        std::exit(1);
      }
    };
    if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
      printUsage(argv[0]);
      return 0;
    } else if (std::strcmp(a, "--f0") == 0) {
      need(1); params.startFreqHz = std::atof(argv[++i]);
    } else if (std::strcmp(a, "--fend") == 0) {
      need(1); params.endFreqHz = std::atof(argv[++i]);
    } else if (std::strcmp(a, "--pitch-decay") == 0) {
      need(1); params.pitchDecayMs = std::atof(argv[++i]);
    } else if (std::strcmp(a, "--amp-decay") == 0) {
      need(1); params.ampDecayMs = std::atof(argv[++i]);
    } else if (std::strcmp(a, "--gain") == 0) {
      need(1); params.gain = std::atof(argv[++i]);
    } else if (std::strcmp(a, "--bpm") == 0) {
      need(1); params.bpm = std::atof(argv[++i]);
    } else if (std::strcmp(a, "--duration") == 0) {
      need(1); params.durationSec = std::atof(argv[++i]);
    } else if (std::strcmp(a, "--click") == 0) {
      need(1); params.click = std::atof(argv[++i]);
    } else if (std::strcmp(a, "--wav") == 0) {
      need(1); wavPath = argv[++i];
    } else if (std::strcmp(a, "--sr") == 0) {
      need(1); offlineSr = std::atof(argv[++i]);
      if (offlineSr <= 8000.0) offlineSr = 8000.0;
    } else if (std::strcmp(a, "--format") == 0) {
      need(1); outFormat = argv[++i];
    } else if (std::strcmp(a, "--bitdepth") == 0) {
      need(1); bitDepth = argv[++i];
    } else if (std::strcmp(a, "--pcm16") == 0) {
      pcm16 = true;
    } else if (std::strcmp(a, "--graph") == 0) {
      need(1); graphPath = argv[++i];
    } else {
      std::fprintf(stderr, "Unknown option: %s\n", a);
      printUsage(argv[0]);
      return 1;
    }
  }

  params.loop = params.bpm > 0.0f;
  if (params.gain < 0.0f) params.gain = 0.0f;
  if (params.gain > 1.5f) params.gain = 1.5f;
  if (params.click < 0.0f) params.click = 0.0f;
  if (params.click > 1.0f) params.click = 1.0f;

  std::signal(SIGINT, onSigInt);

  // Offline render path to WAV if requested
  if (!wavPath.empty()) {
    const uint32_t channels = 2;
    const uint32_t sr = static_cast<uint32_t>(offlineSr + 0.5);
    const uint64_t totalFrames = static_cast<uint64_t>(std::max(0.0f, params.durationSec) * static_cast<float>(sr) + 0.5);

    Graph graph;
    if (!graphPath.empty()) {
      try {
        GraphSpec spec = loadGraphSpecFromJsonFile(graphPath);
        for (const auto& ns : spec.nodes) {
          auto node = createNodeFromSpec(ns);
          if (node) graph.addNode(ns.id, std::move(node));
        }
        if (spec.hasMixer) {
          std::vector<MixerChannel> chans;
          for (const auto& inp : spec.mixer.inputs) {
            MixerChannel mc; mc.id = inp.id; mc.gain = inp.gainPercent * (1.0f/100.0f);
            chans.push_back(mc);
          }
          const float master = spec.mixer.masterPercent * (1.0f/100.0f);
          graph.setMixer(std::make_unique<MixerNode>(std::move(chans), master, spec.mixer.softClip));
        }
      } catch (const std::exception& e) {
        std::fprintf(stderr, "Failed to load graph JSON: %s\n", e.what());
        return 1;
      }
    } else {
      KickParams p = params;
      p.loop = false;
      graph.addNode("kick_default", std::make_unique<KickNode>(p));
    }
    auto interleaved = renderGraphInterleaved(graph, sr, channels, totalFrames);

    AudioFileSpec spec;
    if (outFormat == "wav") spec.format = FileFormat::Wav;
    else if (outFormat == "aiff") spec.format = FileFormat::Aiff;
    else if (outFormat == "caf") spec.format = FileFormat::Caf;
    else spec.format = FileFormat::Wav;

    if (pcm16 || bitDepth == "16") spec.bitDepth = BitDepth::Pcm16;
    else if (bitDepth == "24") spec.bitDepth = BitDepth::Pcm24;
    else spec.bitDepth = BitDepth::Float32;
    spec.sampleRate = sr;
    spec.channels = channels;

    try {
      writeWithExtAudioFile(wavPath, spec, interleaved);
      std::fprintf(stderr, "Wrote %llu frames at %u Hz to %s (%s/%s)\n",
                   static_cast<unsigned long long>(totalFrames), sr, wavPath.c_str(),
                   (outFormat.c_str()), (pcm16?"16":"32f"));
    } catch (const std::exception& e) {
      std::fprintf(stderr, "Audio file write failed: %s\n", e.what());
      return 1;
    }
    return 0;
  }

  // Realtime renderer path via graph
  Graph graph;
  if (!graphPath.empty()) {
    try {
      GraphSpec spec = loadGraphSpecFromJsonFile(graphPath);
      for (const auto& ns : spec.nodes) {
        auto node = createNodeFromSpec(ns);
        if (node) graph.addNode(ns.id, std::move(node));
      }
      if (spec.hasMixer) {
        std::vector<MixerChannel> chans;
        for (const auto& inp : spec.mixer.inputs) {
          MixerChannel mc; mc.id = inp.id; mc.gain = inp.gainPercent * (1.0f/100.0f);
          chans.push_back(mc);
        }
        const float master = spec.mixer.masterPercent * (1.0f/100.0f);
        graph.setMixer(std::make_unique<MixerNode>(std::move(chans), master, spec.mixer.softClip));
      }
    } catch (const std::exception& e) {
      std::fprintf(stderr, "Failed to load graph JSON: %s\n", e.what());
      return 1;
    }
  } else {
    graph.addNode("kick_default", std::make_unique<KickNode>(params));
  }
  RealtimeGraphRenderer rt;
  try {
    rt.start(graph, 48000.0, 2);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Realtime start failed: %s\n", e.what());
    return 1;
  }

  // Always allow Ctrl-C or Enter to stop in realtime
  while (gRunning.load()) {
    if (isStdinReady()) {
      char buf[4];
      (void)read(STDIN_FILENO, buf, sizeof(buf));
      gRunning.store(false);
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  rt.stop();

  return 0;
}


