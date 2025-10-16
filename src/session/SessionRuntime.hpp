#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>
#include "SessionSpec.hpp"
#include "../core/Graph.hpp"
#include "../core/GraphConfig.hpp"
#include "../core/NodeFactory.hpp"
#include "../offline/OfflineGraphRenderer.hpp"
#include "../offline/OfflineTimelineRenderer.hpp" // for renderGraphWithCommands
#include "../offline/TransportGenerator.hpp"
#include "../core/GraphUtils.hpp" // computeGraphPrerollSamples
#include "../core/SpectralDuckerNode.hpp"

struct SessionRuntime {
  struct Rack {
    std::string id;
    Graph graph;
    GraphSpec spec;
    std::vector<GraphSpec::CommandSpec> cmds;
    int64_t startOffsetFrames = 0;
    float gain = 1.0f;
  };

  struct RackStats {
    std::string id;
    double peakDb = 0.0;
    double rmsDb = 0.0;
    double cpuAvgMs = 0.0;
    double cpuMaxMs = 0.0;
    uint64_t blocks = 0;
  };

  uint32_t sampleRate = 48000;
  uint32_t channels = 2;
  std::vector<Rack> racks;
  bool enablePerRackMeters = false;
  bool enablePerRackCpu = false;
  struct Bus { std::string id; uint32_t channels = 2; std::vector<float> buffer; std::vector<SessionSpec::InsertRef> inserts; };
  std::vector<Bus> buses;
  struct Route { std::string from; std::string to; float gain = 1.0f; };
  std::vector<Route> routes;
  // Session-level commands (resolved to sample time)
  std::vector<GraphSpec::CommandSpec> sessionCommands;

  void setPerRackMeters(bool v) { enablePerRackMeters = v; }
  void setPerRackCpu(bool v) { enablePerRackCpu = v; }

  static inline void computePeakAndRmsSimple(const std::vector<float>& interleaved, uint32_t /*channels*/, double& outPeakDb, double& outRmsDb) {
    double peak = 0.0; long double sumSq = 0.0L; const size_t n = interleaved.size();
    for (size_t i = 0; i < n; ++i) {
      const double a = std::fabs(static_cast<double>(interleaved[i]));
      if (a > peak) peak = a;
      sumSq += static_cast<long double>(interleaved[i]) * static_cast<long double>(interleaved[i]);
    }
    const double rms = (n > 0) ? std::sqrt(static_cast<double>(sumSq / static_cast<long double>(n))) : 0.0;
    auto toDb = [](double x) -> double { return (x > 0.0) ? (20.0 * std::log10(x)) : -std::numeric_limits<double>::infinity(); };
    outPeakDb = toDb(peak);
    outRmsDb = toDb(rms);
  }

  void loadFromSpec(const SessionSpec& s) {
    sampleRate = s.sampleRate; channels = s.channels;
    racks.clear(); racks.reserve(s.racks.size());
    buses.clear(); routes.clear();
    for (const auto& rr : s.racks) {
      GraphSpec gs = loadGraphSpecFromJsonFile(rr.path);
      Graph g;
      for (const auto& ns : gs.nodes) {
        auto node = createNodeFromSpec(ns);
        if (node) g.addNode(ns.id, std::move(node));
      }
      if (gs.hasMixer) {
        std::vector<MixerChannel> chans;
        for (const auto& inp : gs.mixer.inputs) {
          MixerChannel mc; mc.id = inp.id; mc.gain = inp.gainPercent * (1.0f/100.0f);
          chans.push_back(mc);
        }
        const float master = gs.mixer.masterPercent * (1.0f/100.0f);
        g.setMixer(std::make_unique<MixerNode>(std::move(chans), master, gs.mixer.softClip));
      }
      if (!gs.connections.empty()) {
        g.setConnections(gs.connections);
      }
      // Build command list for this rack (transport + explicit commands), resolve param names
      std::vector<GraphSpec::CommandSpec> rackCmds = gs.commands;
      if (gs.hasTransport) {
        GraphSpec::Transport tgen = gs.transport;
        // Apply per-rack overrides (bars/loopCount or minutes/seconds)
        if (rr.bars > 0) tgen.lengthBars = rr.bars;
        if (rr.loopCount > 0 && tgen.lengthBars > 0) tgen.lengthBars = tgen.lengthBars * rr.loopCount;
        if ((rr.loopMinutes > 0.0 || rr.loopSeconds > 0.0) && tgen.lengthBars > 0) {
          const double targetSec = (rr.loopMinutes > 0.0 ? rr.loopMinutes * 60.0 : rr.loopSeconds);
          const double bpm = tgen.bpm > 0.0 ? tgen.bpm : 120.0;
          const double secPerBar = 4.0 * (60.0 / bpm);
          const double perLoopSec = secPerBar * static_cast<double>(tgen.lengthBars);
          uint32_t loops = perLoopSec > 0.0 ? static_cast<uint32_t>(std::ceil(targetSec / perLoopSec)) : 1u;
          if (loops == 0) loops = 1;
          tgen.lengthBars = tgen.lengthBars * loops;
        }
        auto gen = generateCommandsFromTransport(tgen, sampleRate);
        rackCmds.insert(rackCmds.end(), gen.begin(), gen.end());
      }
      {
        std::unordered_map<std::string, std::string> nodeIdToType;
        for (const auto& ns : gs.nodes) nodeIdToType.emplace(ns.id, ns.type);
        auto mapParam = [](const std::string& type, const std::string& name) -> uint16_t {
          if (type == std::string("kick")) return resolveParamIdByName(kKickParamMap, name);
          if (type == std::string("clap")) return resolveParamIdByName(kClapParamMap, name);
          if (type == std::string("tb303_ext")) return resolveParamIdByName(kTb303ParamMap, name);
          if (type == std::string("mam_chip")) return resolveParamIdByName(kMamChipParamMap, name);
          return 0;
        };
        for (auto& c : rackCmds) {
          if (c.paramId == 0 && !c.paramName.empty()) {
            auto it = nodeIdToType.find(c.nodeId);
            const std::string nodeType = (it != nodeIdToType.end()) ? it->second : std::string();
            c.paramId = mapParam(nodeType, c.paramName);
          }
        }
      }
      Rack r; r.id = rr.id; r.graph = std::move(g); r.spec = std::move(gs); r.cmds = std::move(rackCmds); r.startOffsetFrames = rr.startOffsetFrames; r.gain = rr.gain;
      racks.push_back(std::move(r));
    }
    // Initialize buses and routes
    for (const auto& b : s.buses) {
      Bus bb; bb.id = b.id; bb.channels = b.channels; bb.inserts = b.inserts; buses.push_back(std::move(bb));
    }
    for (const auto& r : s.routes) { Route rr; rr.from = r.from; rr.to = r.to; rr.gain = r.gain; routes.push_back(rr); }

    // Resolve session commands from musical time to sample time
    sessionCommands.clear();
    for (const auto& sc : s.commands) {
      double resolvedTimeSec = sc.timeSec;

      // Resolve musical time if absolute time not provided
      if (sc.timeSec == 0.0 && !sc.rack.empty() && sc.bar > 0) {
        // Find the referenced rack
        auto rackIt = std::find_if(racks.begin(), racks.end(),
          [&](const Rack& r) { return r.id == sc.rack; });
        if (rackIt == racks.end()) {
          // Warning already printed in main.cpp, just skip
          continue;
        }

        // Convert musical time to seconds using the rack's transport info
        const uint32_t bar = sc.bar - 1; // Convert to 0-based
        const uint32_t step = (sc.step > 0) ? sc.step - 1 : 0; // Convert to 0-based, default to bar start
        const uint32_t stepsPerBar = sc.res;

        // Calculate loop length for this rack
        uint64_t loopLen = 0;
        if (rackIt->spec.hasTransport) {
          const double bpm = (rackIt->spec.transport.bpm > 0.0) ? rackIt->spec.transport.bpm : 120.0;
          const double secPerBar = 4.0 * (60.0 / bpm);
          const uint64_t framesPerBar = static_cast<uint64_t>(secPerBar * sampleRate + 0.5);
          const uint32_t bars = (rackIt->spec.transport.lengthBars > 0) ? rackIt->spec.transport.lengthBars : 1u;
          loopLen = framesPerBar * static_cast<uint64_t>(bars);
        }

        if (loopLen > 0) {
          const double secPerBar = static_cast<double>(loopLen) / sampleRate;
          const double stepsPerSec = static_cast<double>(stepsPerBar) / secPerBar;
          const double stepSec = static_cast<double>(step) / stepsPerSec;

          resolvedTimeSec = static_cast<double>(bar) * secPerBar + stepSec;
        }
      }

      // Convert to CommandSpec format
      GraphSpec::CommandSpec cmd;
      cmd.sampleTime = static_cast<uint64_t>(std::llround(resolvedTimeSec * sampleRate));
      cmd.nodeId = sc.nodeId;
      cmd.type = sc.type;
      cmd.paramId = 0; // Session commands use nodeId targeting, not paramId
      cmd.value = sc.value;
      cmd.rampMs = sc.rampMs;
      sessionCommands.push_back(cmd);
    }
  }

  // Render offline: mix simple sum with per-rack gain and start offset (silence before start)
  std::vector<float> renderOffline(uint64_t frames, std::vector<RackStats>* outStats = nullptr) {
    std::vector<float> mix;
    mix.assign(static_cast<size_t>(frames * channels), 0.0f);
    if (outStats) outStats->clear();
    // Prepare bus buffers
    for (auto& b : buses) b.buffer.assign(static_cast<size_t>(frames * b.channels), 0.0f);

    // Session commands are resolved but not yet applied in offline rendering
    // This provides the foundation for musical time addressing in offline sessions

    struct RackOutput { std::string id; std::vector<float> audio; int64_t startOffsetFrames = 0; float gain = 1.0f; };
    std::vector<RackOutput> outputs;
    outputs.reserve(racks.size());

    // Render each rack with its commands
    for (auto& r : racks) {
      const uint64_t rackFrames = frames > static_cast<uint64_t>(std::max<int64_t>(0, -r.startOffsetFrames))
        ? (frames - static_cast<uint64_t>(std::max<int64_t>(0, -r.startOffsetFrames))) : 0ull;
      if (rackFrames == 0) continue;
      if (enablePerRackCpu) r.graph.enableCpuStats(true);
      auto audio = renderGraphWithCommands(r.graph, r.cmds, sampleRate, channels, rackFrames);
      outputs.push_back(RackOutput{r.id, audio, r.startOffsetFrames, r.gain});
    }
    // Route rack outputs to buses or final mix
    for (const auto& ro : outputs) {
      const auto& audio = ro.audio;
      if (outStats && enablePerRackMeters) {
        RackStats st; st.id = ro.id; computePeakAndRmsSimple(audio, channels, st.peakDb, st.rmsDb);
        outStats->push_back(st);
      }
      // Route to buses per routes; if no bus routes match, sum to mix directly
      bool routed = false;
      for (const auto& rt : routes) {
        if (rt.from != ro.id) continue; routed = true;
        auto it = std::find_if(buses.begin(), buses.end(), [&](const Bus& b){ return b.id == rt.to; });
        if (it == buses.end()) continue;
        const uint32_t bch = it->channels;
        uint64_t writeStart = static_cast<uint64_t>(std::max<int64_t>(0, ro.startOffsetFrames));
        const size_t n = audio.size();
        for (size_t i = 0; i < n; ++i) {
          const uint64_t frameIndex = writeStart + (i / channels);
          if (frameIndex >= frames) break;
          const uint32_t ch = static_cast<uint32_t>(i % channels);
          const uint32_t dstCh = (bch == channels) ? ch : std::min(ch, bch - 1);
          it->buffer[static_cast<size_t>(frameIndex * bch) + dstCh] += audio[i] * (ro.gain * rt.gain);
        }
      }
      if (!routed) {
        uint64_t writeStart = static_cast<uint64_t>(std::max<int64_t>(0, ro.startOffsetFrames));
        const size_t n = audio.size();
        for (size_t i = 0; i < n; ++i) {
          const uint64_t frameIndex = writeStart + (i / channels);
          if (frameIndex >= frames) break;
          mix[static_cast<size_t>(frameIndex * channels) + (i % channels)] += audio[i] * ro.gain;
        }
      }
    }
    // Apply inserts and mix buses to final
    for (auto& b : buses) {
      // Apply known inserts
      for (const auto& ins : b.inserts) {
        if (ins.type == std::string("spectral_ducker")) {
          SpectralDuckerNode duck;
          // Params
          try {
            if (ins.params.contains("mix")) duck.mix = ins.params["mix"].get<float>();
            if (ins.params.contains("detectorHpfHz")) duck.scHpfHz = ins.params["detectorHpfHz"].get<float>();
            if (ins.params.contains("applyMode")) {
              std::string m = ins.params["applyMode"].get<std::string>();
              duck.applyMode = (m == std::string("dynamicEq")) ? SpectralDuckerNode::ApplyMode::DynamicEq : SpectralDuckerNode::ApplyMode::Multiply;
            }
            if (ins.params.contains("stereoMode")) {
              std::string sm = ins.params["stereoMode"].get<std::string>();
              duck.stereoMode = (sm == std::string("MidSide")) ? SpectralDuckerNode::StereoMode::MidSide : SpectralDuckerNode::StereoMode::LR;
            }
            if (ins.params.contains("msSideScale")) duck.msSideScale = ins.params["msSideScale"].get<float>();
          } catch (...) {}
          duck.prepare(static_cast<double>(sampleRate), static_cast<uint32_t>(std::min<uint64_t>(frames, 4096)));
          // Build sidechain from referenced rack(s)
          std::vector<float> sc; sc.assign(static_cast<size_t>(frames * b.channels), 0.0f);
          for (const auto& scRef : ins.sidechains) {
            const std::string& fromRack = scRef.second;
            auto itOut = std::find_if(outputs.begin(), outputs.end(), [&](const RackOutput& ro){ return ro.id == fromRack; });
            if (itOut == outputs.end()) continue;
            const auto& audio = itOut->audio; const uint32_t bch = b.channels;
            const uint64_t writeStart = static_cast<uint64_t>(std::max<int64_t>(0, itOut->startOffsetFrames));
            const size_t n = audio.size();
            for (size_t i = 0; i < n; ++i) {
              const uint64_t frameIndex = writeStart + (i / channels);
              if (frameIndex >= frames) break;
              const uint32_t ch = static_cast<uint32_t>(i % channels);
              const uint32_t dstCh = (bch == channels) ? ch : std::min(ch, bch - 1);
              sc[static_cast<size_t>(frameIndex * bch) + dstCh] += audio[i];
            }
          }
          // Apply ducking on bus buffer in-place
          ProcessContext ctx{}; ctx.sampleRate = sampleRate; ctx.frames = static_cast<uint32_t>(frames); ctx.blockStart = 0;
          duck.applySidechain(ctx, b.buffer.data(), sc.data(), b.channels);
        }
      }
      // Sum bus to mix
      const size_t n = b.buffer.size();
      const uint32_t bch = b.channels;
      for (size_t i = 0; i < n; ++i) {
        const uint64_t frameIndex = static_cast<uint64_t>(i / bch);
        const uint32_t ch = static_cast<uint32_t>(i % bch);
        const uint32_t dstCh = (channels == bch) ? ch : std::min(ch, channels - 1);
        mix[static_cast<size_t>(frameIndex * channels) + dstCh] += b.buffer[i];
      }
    }
    return mix;
  }

  // Plan total frames considering content length, preroll, start offsets, and looping
  uint64_t planTotalFrames(double sessionTailMs, bool enableLoop = false, uint32_t maxLoops = 1) const {
    uint64_t maxEnd = 0;

    if (enableLoop && maxLoops > 0) {
      // For looping, calculate based on the loop duration (max of rack loop lengths)
      for (const auto& r : racks) {
        uint64_t loopLen = 0;
        if (r.spec.hasTransport) {
          const double bpm = (r.spec.transport.bpm > 0.0) ? r.spec.transport.bpm : 120.0;
          const double secPerBar = 4.0 * (60.0 / bpm);
          const uint64_t framesPerBar = static_cast<uint64_t>(secPerBar * sampleRate + 0.5);
          const uint32_t bars = (r.spec.transport.lengthBars > 0) ? r.spec.transport.lengthBars : 1u;
          loopLen = framesPerBar * static_cast<uint64_t>(bars);
        }
        if (loopLen > maxEnd) maxEnd = loopLen;
      }
      maxEnd = maxEnd * maxLoops;
    } else {
      // Standard calculation: max of rack content
      for (const auto& r : racks) {
        // Determine content duration from actual command tail (respects per-rack overrides)
        uint64_t content = 0;
        for (const auto& c : r.cmds) if (c.sampleTime > content) content = c.sampleTime;
        const uint64_t preroll = computeGraphPrerollSamples(r.spec, sampleRate);
        const uint64_t start = static_cast<uint64_t>(std::max<int64_t>(0, r.startOffsetFrames));
        const uint64_t end = start + preroll + content;
        if (end > maxEnd) maxEnd = end;
      }
    }

    const uint64_t tail = static_cast<uint64_t>((sessionTailMs / 1000.0) * static_cast<double>(sampleRate) + 0.5);
    return maxEnd + tail;
  }

  // Render with optional looping support
  std::vector<float> renderOfflineWithLoop(uint64_t frames, uint32_t maxLoops = 1, std::vector<RackStats>* outStats = nullptr) {
    std::vector<float> mix;
    mix.assign(static_cast<size_t>(frames * channels), 0.0f);
    if (outStats) outStats->clear();

    // Render multiple loops if requested
    uint64_t framesRendered = 0;
    for (uint32_t loop = 0; loop < maxLoops && framesRendered < frames; ++loop) {
      uint64_t loopFrames = std::min(frames - framesRendered, frames / maxLoops); // Distribute frames across loops
      if (loopFrames == 0) break;

      std::vector<float> loopMix = renderOffline(loopFrames, outStats);
      // Copy loop audio to the appropriate position in the final mix
      for (size_t i = 0; i < loopMix.size() && framesRendered + i < mix.size(); ++i) {
        mix[framesRendered + i] += loopMix[i];
      }
      framesRendered += loopFrames;
    }

    return mix;
  }
};


