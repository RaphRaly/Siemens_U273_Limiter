#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "u273/reference/state_space/ComponentModels.h"

namespace u273::reference::state_space {

// Stable node handle. Ground is always node 0; all other nodes map to solver
// unknowns by subtracting one.
struct NodeId {
    int value {};
};

inline constexpr NodeId kGroundNode {0};

[[nodiscard]] bool operator==(NodeId lhs, NodeId rhs) noexcept;
[[nodiscard]] bool operator!=(NodeId lhs, NodeId rhs) noexcept;

struct Resistor {
    std::string id {};
    NodeId positive {};
    NodeId negative {};
    double resistanceOhm {};
};

struct Capacitor {
    std::string id {};
    NodeId positive {};
    NodeId negative {};
    double capacitanceFarad {};
};

struct CurrentSource {
    std::string id {};
    NodeId positive {};
    NodeId negative {};
    double currentAmp {};
};

struct VoltageSource {
    std::string id {};
    NodeId positive {};
    NodeId negative {};
    double voltage {};
};

struct Diode {
    std::string id {};
    NodeId anode {};
    NodeId cathode {};
    DiodeModel model {};
};

struct NpnBjt {
    std::string id {};
    NodeId collector {};
    NodeId base {};
    NodeId emitter {};
    NpnBjtModel model {};
};

struct IdealTransformerPort {
    std::string id {};
    NodeId primaryPositive {};
    NodeId primaryNegative {};
    NodeId secondaryPositive {};
    NodeId secondaryNegative {};
    double turnsRatio {};
    double sourceResistanceOhm {};
    double loadResistanceOhm {};
    bool magneticModelDeferred {true};
};

// Circuit inventory used by the offline MNA/state-space solver. It stores
// components as typed lists so each stamping pass can stay simple.
class CircuitGraph {
public:
    CircuitGraph();

    [[nodiscard]] NodeId addNode(std::string name);
    [[nodiscard]] NodeId findNode(std::string_view name) const noexcept;
    [[nodiscard]] const std::string& nodeName(NodeId node) const;

    void addResistor(std::string id, NodeId positive, NodeId negative, double resistanceOhm);
    void addCapacitor(std::string id, NodeId positive, NodeId negative, double capacitanceFarad);
    void addCurrentSource(std::string id, NodeId positive, NodeId negative, double currentAmp);
    void addVoltageSource(std::string id, NodeId positive, NodeId negative, double voltage);
    void addDiode(std::string id, NodeId anode, NodeId cathode, DiodeModel model);
    void addNpnBjt(std::string id, NodeId collector, NodeId base, NodeId emitter, NpnBjtModel model);
    void addIdealTransformerPort(std::string id,
                                 NodeId primaryPositive,
                                 NodeId primaryNegative,
                                 NodeId secondaryPositive,
                                 NodeId secondaryNegative,
                                 double turnsRatio,
                                 double sourceResistanceOhm,
                                 double loadResistanceOhm);

    [[nodiscard]] int nodeCount() const noexcept;
    [[nodiscard]] int unknownCount() const noexcept;

    [[nodiscard]] const std::vector<Resistor>& resistors() const noexcept { return resistors_; }
    [[nodiscard]] const std::vector<Capacitor>& capacitors() const noexcept { return capacitors_; }
    [[nodiscard]] const std::vector<CurrentSource>& currentSources() const noexcept { return currentSources_; }
    [[nodiscard]] const std::vector<VoltageSource>& voltageSources() const noexcept { return voltageSources_; }
    [[nodiscard]] const std::vector<Diode>& diodes() const noexcept { return diodes_; }
    [[nodiscard]] const std::vector<NpnBjt>& npnBjts() const noexcept { return npnBjts_; }
    [[nodiscard]] const std::vector<IdealTransformerPort>& idealTransformerPorts() const noexcept
    {
        return idealTransformerPorts_;
    }

private:
    std::vector<std::string> nodeNames_ {};
    std::vector<Resistor> resistors_ {};
    std::vector<Capacitor> capacitors_ {};
    std::vector<CurrentSource> currentSources_ {};
    std::vector<VoltageSource> voltageSources_ {};
    std::vector<Diode> diodes_ {};
    std::vector<NpnBjt> npnBjts_ {};
    std::vector<IdealTransformerPort> idealTransformerPorts_ {};
};

} // namespace u273::reference::state_space
