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
#include <cassert>
#include "instruments/kick/KickSynth.hpp"
#include "offline/OfflineRenderer.hpp"
#include "offline/OfflineGraphRenderer.hpp"
#include "offline/OfflineParallelGraphRenderer.hpp"
#include "offline/OfflineTimelineRenderer.hpp"
#include "offline/TransportGenerator.hpp"
#include "io/AudioFileWriter.hpp"
#include "realtime/RealtimeRenderer.hpp"
#include "realtime/RealtimeGraphRenderer.hpp"
#include "core/Graph.hpp"
#include "core/GraphConfig.hpp"
#include "core/NodeFactory.hpp"
#include "core/MixerNode.hpp"
#include "instruments/kick/KickNode.hpp"
#include "core/ParamMap.hpp"

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

static const char* toStr(FileFormat f) {
  switch (f) {
    case FileFormat::Wav: return "wav";
    case FileFormat::Aiff: return "aiff";
    case FileFormat::Caf: return "caf";
  }
  return "wav";
}

static const char* toStr(BitDepth d) {
  switch (d) {
    case BitDepth::Pcm16: return "16";
    case BitDepth::Pcm24: return "24";
    case BitDepth::Float32: return "32f";
  }
  return "32f";
}

static void printUsage(const char* exe) {
  std::fprintf(stderr,
               "Usage: %s [--f0 Hz] [--fend Hz] [--pitch-decay ms] [--amp-decay ms]\n"
               "          [--gain 0..1] [--bpm N] [--duration sec] [--click 0..1]\n"
               "          [--wav path] [--sr Hz] [--pcm16] [--format wav|aiff|caf] [--bitdepth 16|24|32f]\n"
               "          [--offline-threads N]\n"
               "          [--graph path.json] [--quit-after sec]\n"
               "          [--validate path.json] [--list-nodes path.json] [--list-params kick|clap]\n"
               "\n"
               "Examples:\n"
               "  %s                       # one-shot, defaults (real-time)\n"
               "  %s --bpm 120            # 120 BPM continuous till Ctrl-C (real-time)\n"
                "  %s --wav out.wav --sr 44100 --duration 2.0  # offline render to WAV\n",
               exe, exe, exe, exe);
}

static const char* dupStr(const std::string& s) {
  char* p = static_cast<char*>(std::malloc(s.size() + 1));
  if (!p) return nullptr;
  std::memcpy(p, s.c_str(), s.size() + 1);
  return p;
}

static int listNodesGraphJson(const std::string& path) {
  try {
    GraphSpec spec = loadGraphSpecFromJsonFile(path);
    std::printf("Nodes (%zu):\n", spec.nodes.size());
    for (const auto& n : spec.nodes) {
      std::printf("- id=%s type=%s\n", n.id.c_str(), n.type.c_str());
    }
    if (spec.hasMixer) {
      std::printf("Mixer: master=%g%% softClip=%s inputs=%zu\n",
                  spec.mixer.masterPercent, spec.mixer.softClip ? "true" : "false", spec.mixer.inputs.size());
    }
    if (spec.hasTransport) {
      std::printf("Transport: bpm=%.2f bars=%u res=%u swing=%.1f%% patterns=%zu ramps=%zu\n",
                  spec.transport.bpm, spec.transport.lengthBars, spec.transport.resolution,
                  spec.transport.swingPercent, spec.transport.patterns.size(), spec.transport.tempoRamps.size());
    }
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Failed to load graph JSON: %s\n", e.what());
    return 1;
  }
}

static int validateGraphJson(const std::string& path) {
  try {
    GraphSpec spec = loadGraphSpecFromJsonFile(path);
    int errors = 0;
    std::vector<std::string> nodeIds;
    nodeIds.reserve(spec.nodes.size());
    for (const auto& n : spec.nodes) nodeIds.push_back(n.id);
    auto hasNode = [&](const std::string& id) {
      return std::find(nodeIds.begin(), nodeIds.end(), id) != nodeIds.end();
    };
    // Check unique node ids
    {
      auto tmp = nodeIds; std::sort(tmp.begin(), tmp.end());
      for (size_t i = 1; i < tmp.size(); ++i) {
        if (tmp[i] == tmp[i-1]) { std::fprintf(stderr, "Duplicate node id: %s\n", tmp[i].c_str()); errors++; }
      }
    }
    // Known node types
    for (const auto& n : spec.nodes) {
      if (!(n.type == "kick" || n.type == "clap")) {
        std::fprintf(stderr, "Unknown node type '%s' (id=%s)\n", n.type.c_str(), n.id.c_str());
      }
    }
    // Mixer inputs exist
    if (spec.hasMixer) {
      for (const auto& mi : spec.mixer.inputs) {
        if (!hasNode(mi.id)) { std::fprintf(stderr, "Mixer references unknown node '%s'\n", mi.id.c_str()); errors++; }
      }
    }
    // Commands
    for (const auto& c : spec.commands) {
      if (!hasNode(c.nodeId)) { std::fprintf(stderr, "Command references unknown node '%s'\n", c.nodeId.c_str()); errors++; continue; }
      if (c.type == "SetParam" || c.type == "SetParamRamp") {
        uint16_t pid = c.paramId;
        if (pid == 0 && !c.paramName.empty()) {
          // resolve by name
          std::string nodeType;
          for (const auto& n : spec.nodes) if (n.id == c.nodeId) { nodeType = n.type; break; }
          if (nodeType == "kick") pid = resolveParamIdByName(kKickParamMap, c.paramName);
          else if (nodeType == "clap") pid = resolveParamIdByName(kClapParamMap, c.paramName);
        }
        if (pid == 0) { std::fprintf(stderr, "Command missing/unknown param (node=%s)\n", c.nodeId.c_str()); errors++; }
      }
    }
    // Transport patterns
    if (spec.hasTransport) {
      for (const auto& p : spec.transport.patterns) {
        if (!hasNode(p.nodeId)) { std::fprintf(stderr, "Pattern references unknown node '%s'\n", p.nodeId.c_str()); errors++; }
      }
    }
    if (errors == 0) {
      std::printf("%s: OK\n", path.c_str());
      return 0;
    }
    return 2;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Failed to load graph JSON: %s\n", e.what());
    return 1;
  }
}

int main(int argc, char** argv) {
  KickParams params;
  std::string wavPath;
  FileFormat outFormat = FileFormat::Wav;
  BitDepth outDepth = BitDepth::Float32;
  std::string graphPath;
  std::string validatePath;
  std::string listNodesPath;
  std::string listParamsType;
  double offlineSr = 48000.0;
  bool pcm16 = false;
  double quitAfterSec = 0.0;
  uint32_t offlineThreads = 0; // 0=auto (fallback to single-thread renderer if 0)

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
      need(1); params.startFreqHz = static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(a, "--fend") == 0) {
      need(1); params.endFreqHz = static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(a, "--pitch-decay") == 0) {
      need(1); params.pitchDecayMs = static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(a, "--amp-decay") == 0) {
      need(1); params.ampDecayMs = static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(a, "--gain") == 0) {
      need(1); params.gain = static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(a, "--bpm") == 0) {
      need(1); params.bpm = static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(a, "--duration") == 0) {
      need(1); params.durationSec = static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(a, "--click") == 0) {
      need(1); params.click = static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(a, "--wav") == 0) {
      need(1); wavPath = argv[++i];
    } else if (std::strcmp(a, "--sr") == 0) {
      need(1); offlineSr = std::atof(argv[++i]);
      if (offlineSr <= 8000.0) offlineSr = 8000.0;
    } else if (std::strcmp(a, "--format") == 0) {
      need(1); {
        const char* f = argv[++i];
        if (std::strcmp(f, "wav") == 0) outFormat = FileFormat::Wav;
        else if (std::strcmp(f, "aiff") == 0) outFormat = FileFormat::Aiff;
        else if (std::strcmp(f, "caf") == 0) outFormat = FileFormat::Caf;
      }
    } else if (std::strcmp(a, "--bitdepth") == 0) {
      need(1); {
        const char* b = argv[++i];
        if (std::strcmp(b, "16") == 0) outDepth = BitDepth::Pcm16;
        else if (std::strcmp(b, "24") == 0) outDepth = BitDepth::Pcm24;
        else outDepth = BitDepth::Float32;
      }
    } else if (std::strcmp(a, "--pcm16") == 0) {
      pcm16 = true;
    } else if (std::strcmp(a, "--offline-threads") == 0) {
      need(1); offlineThreads = static_cast<uint32_t>(std::max(0, std::atoi(argv[++i])));
    } else if (std::strcmp(a, "--quit-after") == 0) {
      need(1); quitAfterSec = std::atof(argv[++i]);
    } else if (std::strcmp(a, "--graph") == 0) {
      need(1); graphPath = argv[++i];
    } else if (std::strcmp(a, "--validate") == 0) {
      need(1); validatePath = argv[++i];
    } else if (std::strcmp(a, "--list-nodes") == 0) {
      need(1); listNodesPath = argv[++i];
    } else if (std::strcmp(a, "--list-params") == 0) {
      need(1); listParamsType = argv[++i];
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

  // Utilities
  if (!validatePath.empty()) return validateGraphJson(validatePath);
  if (!listNodesPath.empty()) return listNodesGraphJson(listNodesPath);
  if (!listParamsType.empty()) {
    if (listParamsType == std::string("kick")) {
      std::printf("kick params:\n");
      for (size_t idx = 0; idx < kKickParamMap.count; ++idx) {
        const auto& d = kKickParamMap.defs[idx];
        std::printf("%u %s [%g..%g] def=%g %s\n", d.id, d.name, d.minValue, d.maxValue, d.defaultValue, d.smoothing);
      }
      return 0;
    } else if (listParamsType == std::string("clap")) {
      std::printf("clap params:\n");
      for (size_t idx = 0; idx < kClapParamMap.count; ++idx) {
        const auto& d = kClapParamMap.defs[idx];
        std::printf("%u %s [%g..%g] def=%g %s\n", d.id, d.name, d.minValue, d.maxValue, d.defaultValue, d.smoothing);
      }
      return 0;
    } else {
      std::fprintf(stderr, "Unknown node type for --list-params: %s\n", listParamsType.c_str());
      return 1;
    }
  }

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
    std::vector<float> interleaved;
    if (!graphPath.empty()) {
      try {
        GraphSpec spec2 = loadGraphSpecFromJsonFile(graphPath);
        // Synthesize commands from transport, if present
        std::vector<GraphSpec::CommandSpec> cmds = spec2.commands;
        if (spec2.hasTransport) {
          auto gen = generateCommandsFromTransport(spec2.transport, sr);
          cmds.insert(cmds.end(), gen.begin(), gen.end());
        }
        interleaved = renderGraphWithCommands(graph, cmds, sr, channels, totalFrames);
      } catch (...) {
        interleaved = (offlineThreads > 1) ? renderGraphInterleavedParallel(graph, sr, channels, totalFrames, offlineThreads)
                                           : renderGraphInterleaved(graph, sr, channels, totalFrames);
      }
    } else {
      interleaved = (offlineThreads > 1) ? renderGraphInterleavedParallel(graph, sr, channels, totalFrames, offlineThreads)
                                         : renderGraphInterleaved(graph, sr, channels, totalFrames);
    }

    AudioFileSpec spec;
    spec.format = outFormat;
    spec.bitDepth = pcm16 ? BitDepth::Pcm16 : outDepth;
    spec.sampleRate = sr;
    spec.channels = channels;

    try {
      writeWithExtAudioFile(wavPath, spec, interleaved);
      std::fprintf(stderr, "Wrote %llu frames at %u Hz to %s (%s/%s)\n",
                   static_cast<unsigned long long>(totalFrames), sr, wavPath.c_str(),
                   toStr(spec.format), toStr(spec.bitDepth));
    } catch (const std::exception& e) {
      std::fprintf(stderr, "Audio file write failed: %s\n", e.what());
      return 1;
    }
    return 0;
  }

  // Realtime renderer path via graph
  Graph graph;
  std::thread transportFeeder;
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
  SpscCommandQueue<2048> cmdQueue;
  rt.setCommandQueue(&cmdQueue);
  try {
    rt.start(graph, 48000.0, 2);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Realtime start failed: %s\n", e.what());
    return 1;
  }

  // Always allow Ctrl-C or Enter to stop in realtime
  // If a graph was provided and it has commands/transport, enqueue them (demo horizon)
  if (!graphPath.empty()) {
    try {
      GraphSpec spec = loadGraphSpecFromJsonFile(graphPath);
      // Build initial command set (explicit + transport)
      std::vector<GraphSpec::CommandSpec> baseCmds = spec.commands;
      if (spec.hasTransport) {
        auto gen = generateCommandsFromTransport(spec.transport, 48000);
        baseCmds.insert(baseCmds.end(), gen.begin(), gen.end());
      }
      // Repeat transport loop up to horizon (quit-after or ~60s)
      uint64_t horizonFrames = (quitAfterSec > 0.0) ? static_cast<uint64_t>(quitAfterSec * 48000.0) : static_cast<uint64_t>(60.0 * 48000.0);
      uint64_t loopLen = 0;
      for (const auto& c : baseCmds) if (c.sampleTime > loopLen) loopLen = c.sampleTime;
      if (loopLen == 0) loopLen = static_cast<uint64_t>(spec.transport.lengthBars * (4.0 * (60.0 / std::max(1.0f, spec.transport.bpm))) * 48000.0);
      const uint64_t safety = 128;
      loopLen += safety;
      std::vector<GraphSpec::CommandSpec> realtimeCmds;
      for (uint64_t offset = 0; offset < horizonFrames; offset += loopLen) {
        for (auto c : baseCmds) { c.sampleTime += offset; realtimeCmds.push_back(c); }
      }
      for (const auto& c : realtimeCmds) {
        Command cmd{};
        cmd.sampleTime = c.sampleTime;
        cmd.nodeId = dupStr(c.nodeId);
        if (c.type == std::string("Trigger")) cmd.type = CommandType::Trigger;
        else if (c.type == std::string("SetParam")) cmd.type = CommandType::SetParam;
        else if (c.type == std::string("SetParamRamp")) cmd.type = CommandType::SetParamRamp;
        cmd.paramId = c.paramId;
        cmd.value = c.value;
        cmd.rampMs = c.rampMs;
        (void)cmdQueue.push(cmd);
      }

      // Rolling feeder thread to extend horizon in realtime
      if (spec.hasTransport && loopLen > 0) {
        uint64_t nextOffset = ((horizonFrames / loopLen) + 1) * loopLen;
        transportFeeder = std::thread([&cmdQueue, baseCmds, loopLen, nextOffset]() mutable {
          uint64_t offset = nextOffset;
          while (gRunning.load()) {
            for (auto c : baseCmds) {
              Command cmd{};
              cmd.sampleTime = c.sampleTime + offset;
              cmd.nodeId = dupStr(c.nodeId);
              if (c.type == std::string("Trigger")) cmd.type = CommandType::Trigger;
              else if (c.type == std::string("SetParam")) cmd.type = CommandType::SetParam;
              else if (c.type == std::string("SetParamRamp")) cmd.type = CommandType::SetParamRamp;
              cmd.paramId = c.paramId;
              cmd.value = c.value;
              cmd.rampMs = c.rampMs;
              (void)cmdQueue.push(cmd);
            }
            offset += loopLen;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
          }
        });
      }
    } catch (...) {}
  }
  double elapsedSec = 0.0;
  while (gRunning.load()) {
    if (isStdinReady()) {
      char buf[4];
      (void)read(STDIN_FILENO, buf, sizeof(buf));
      gRunning.store(false);
      break;
    }
    if (quitAfterSec > 0.0) {
      elapsedSec += 0.05;
      if (elapsedSec >= quitAfterSec) {
        gRunning.store(false);
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  rt.stop();
  if (transportFeeder.joinable()) transportFeeder.join();

  return 0;
}


