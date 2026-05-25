#include "u273/reference/calibration/ActiveTopologyInventory.h"

#include <sstream>
#include <utility>

namespace u273::reference::calibration {

namespace ss = u273::reference::state_space;

namespace {

[[nodiscard]] ActiveTopologyNodeSpec node(std::string primary, std::string fallback = {})
{
    ActiveTopologyNodeSpec spec {};
    spec.primary = std::move(primary);
    spec.fallback = std::move(fallback);
    return spec;
}

[[nodiscard]] ActiveTopologyCandidateSpec npnSpec(std::string id,
                                                  std::string device,
                                                  ActiveTopologyNodeSpec collector,
                                                  ActiveTopologyNodeSpec base,
                                                  ActiveTopologyNodeSpec emitter,
                                                  double priorScore,
                                                  std::string status,
                                                  std::string evidence,
                                                  std::string blockingReason)
{
    ActiveTopologyCandidateSpec spec {};
    spec.id = std::move(id);
    spec.device = std::move(device);
    spec.kind = ActiveDeviceKind::npn;
    spec.collector = std::move(collector);
    spec.base = std::move(base);
    spec.emitter = std::move(emitter);
    spec.priorScore = priorScore;
    spec.status = std::move(status);
    spec.evidence = std::move(evidence);
    spec.blockingReason = std::move(blockingReason);
    return spec;
}

[[nodiscard]] ss::NodeId resolveNode(const ss::CircuitGraph& circuit, const ActiveTopologyNodeSpec& spec)
{
    if (!spec.primary.empty()) {
        const auto primary = circuit.findNode(spec.primary);
        if (primary.value > 0) {
            return primary;
        }
    }

    if (!spec.fallback.empty()) {
        return circuit.findNode(spec.fallback);
    }

    return ss::NodeId {-1};
}

void appendTerminal(std::ostringstream& message,
                    const char* label,
                    const ActiveTopologyNodeSpec& spec,
                    ss::NodeId node)
{
    message << ", " << label << '=';
    if (!spec.primary.empty()) {
        message << spec.primary;
    } else {
        message << "<missing>";
    }
    if (!spec.fallback.empty()) {
        message << " fallback=" << spec.fallback;
    }
    message << " resolved=" << node.value;
}

[[nodiscard]] ActiveTopologyCandidate instantiateCandidate(const ss::CircuitGraph& circuit,
                                                           const ActiveTopologyCandidateSpec& spec)
{
    ActiveTopologyCandidate candidate {};
    candidate.id = spec.id;
    candidate.device = spec.device;
    candidate.kind = spec.kind;
    candidate.collector = resolveNode(circuit, spec.collector);
    candidate.base = resolveNode(circuit, spec.base);
    candidate.emitter = resolveNode(circuit, spec.emitter);
    candidate.priorScore = spec.priorScore;

    if (!candidate.hasUsablePins()) {
        std::ostringstream message {};
        message << "topology inventory candidate is incomplete or unresolved for " << candidate.id;
        appendTerminal(message, "C", spec.collector, candidate.collector);
        appendTerminal(message, "B", spec.base, candidate.base);
        appendTerminal(message, "E", spec.emitter, candidate.emitter);
        if (!spec.blockingReason.empty()) {
            message << "; " << spec.blockingReason;
        }
        candidate.reject(message.str());
    }

    return candidate;
}

} // namespace

bool ActiveTopologyNodeSpec::hasAnyName() const noexcept
{
    return !primary.empty() || !fallback.empty();
}

bool ActiveTopologyCandidateSpec::hasCompleteTerminalNames() const noexcept
{
    return collector.hasAnyName() && base.hasAnyName() && emitter.hasAnyName();
}

std::vector<ActiveTopologyCandidateSpec> defaultB6ActiveTopologyCandidateSpecs()
{
    return {
        npnSpec("B6.Ts1",
                "Ts1",
                node("V64"),
                node(""),
                node("N08"),
                0.1,
                "symbolic_KCL_required_for_Ts1",
                "Printed nodes V64/N08 are present, but B/C/E routing is not closed.",
                "Ts1 terminal routing remains symbolic; keep guarded until route proof."),
        npnSpec("B6.Ts2",
                "Ts2",
                node("V64", "N64_T2"),
                node("N38"),
                node("N32"),
                0.5,
                "probable_from_VBE_and_local_KCL_not_pin_proof",
                "N38=3.8 V and N32=3.2 V imply a plausible 0.6 V VBE candidate.",
                "Collector route and interaction with Ts3 still require confirmation."),
        npnSpec("B6.Ts3",
                "Ts3",
                node(""),
                node("N24"),
                node("N074"),
                0.1,
                "symbolic_until_middle_stage_route_proof",
                "Printed nodes N24/N074 are present; complete collector route is not proven.",
                "Ts3 and compensation/feedback routes remain symbolic."),
        npnSpec("B6.Ts4",
                "Ts4",
                node("N48"),
                node("N24"),
                node("N18"),
                0.5,
                "probable_from_VBE_VCE_and_R32_current_not_pin_proof",
                "N24=2.4 V, N18=1.8 V, and N48=4.8 V give plausible VBE/VCE.",
                "Ts3/Ts4 coupling and route still require confirmation before nonlinear closure."),
        npnSpec("B6.Ts5",
                "Ts5",
                node("N215"),
                node("N105"),
                node(""),
                0.1,
                "bounded_only_no_stamp_until_output_route_proof",
                "Output-stage current bounds exist, but emitter/collector routing is not proven.",
                "Do not connect Ts5 into MNA until output-stage topology is proven."),
        npnSpec("B6.Ts6",
                "Ts6",
                node("N105"),
                node(""),
                node("TS6_EMITTER"),
                0.1,
                "bounded_only_no_stamp_until_output_route_proof",
                "Output-stage current bounds exist; TS6 emitter path is still not a pin proof.",
                "Do not connect Ts6 into MNA until output-stage topology is proven.")
    };
}

std::vector<ActiveTopologyCandidateSpec> defaultB11ActiveTopologyCandidateSpecs()
{
    return {
        npnSpec("B11.Ts1",
                "Ts1",
                node(""),
                node(""),
                node(""),
                0.1,
                "orientation_unconfirmed",
                "B11 hybrid-pi inventory only gives current estimates; B/C/E is not confirmed.",
                "B11 Ts1 remains guarded until schematic/photo route proof."),
        npnSpec("B11.Ts2",
                "Ts2",
                node(""),
                node(""),
                node(""),
                0.1,
                "orientation_unconfirmed",
                "B11 hybrid-pi inventory only gives current estimates; B/C/E is not confirmed.",
                "B11 Ts2 remains guarded until schematic/photo route proof."),
        npnSpec("B11.Ts3",
                "Ts3",
                node(""),
                node(""),
                node(""),
                0.1,
                "orientation_unconfirmed",
                "B11 middle-stage local checks are not terminal proof.",
                "B11 Ts3 remains guarded until schematic/photo route proof."),
        npnSpec("B11.Ts4",
                "Ts4",
                node(""),
                node(""),
                node(""),
                0.1,
                "orientation_unconfirmed",
                "B11 middle-stage local checks are not terminal proof.",
                "B11 Ts4 remains guarded until schematic/photo route proof."),
        npnSpec("B11.Ts5",
                "Ts5",
                node(""),
                node(""),
                node(""),
                0.1,
                "orientation_unconfirmed",
                "B11 output-stage current sweeps are not terminal proof.",
                "B11 Ts5 remains guarded until schematic/photo route proof."),
        npnSpec("B11.Ts6",
                "Ts6",
                node(""),
                node(""),
                node(""),
                0.1,
                "orientation_unconfirmed",
                "B11 output-stage current sweeps are not terminal proof.",
                "B11 Ts6 remains guarded until schematic/photo route proof.")
    };
}

std::vector<ActiveTopologyCandidateSpec> defaultB6B11ActiveTopologyCandidateSpecs()
{
    auto specs = defaultB6ActiveTopologyCandidateSpecs();
    auto b11 = defaultB11ActiveTopologyCandidateSpecs();
    specs.insert(specs.end(), b11.begin(), b11.end());
    return specs;
}

std::vector<ActiveTopologyCandidate> instantiateActiveTopologyCandidates(
    const ss::CircuitGraph& circuit,
    const std::vector<ActiveTopologyCandidateSpec>& specs)
{
    std::vector<ActiveTopologyCandidate> candidates {};
    candidates.reserve(specs.size());
    for (const auto& spec : specs) {
        candidates.push_back(instantiateCandidate(circuit, spec));
    }
    return candidates;
}

} // namespace u273::reference::calibration
