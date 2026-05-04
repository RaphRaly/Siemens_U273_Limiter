#include "u273/reference/state_space/CircuitGraph.h"

#include <stdexcept>
#include <utility>

namespace u273::reference::state_space {

bool operator==(NodeId lhs, NodeId rhs) noexcept
{
    return lhs.value == rhs.value;
}

bool operator!=(NodeId lhs, NodeId rhs) noexcept
{
    return !(lhs == rhs);
}

CircuitGraph::CircuitGraph()
{
    nodeNames_.emplace_back("0");
}

NodeId CircuitGraph::addNode(std::string name)
{
    nodeNames_.push_back(std::move(name));
    return NodeId {static_cast<int>(nodeNames_.size() - 1)};
}

NodeId CircuitGraph::findNode(std::string_view name) const noexcept
{
    for (std::size_t index = 0; index < nodeNames_.size(); ++index) {
        if (nodeNames_[index] == name) {
            return NodeId {static_cast<int>(index)};
        }
    }
    return NodeId {-1};
}

const std::string& CircuitGraph::nodeName(NodeId node) const
{
    if (node.value < 0 || node.value >= static_cast<int>(nodeNames_.size())) {
        throw std::out_of_range("invalid node id");
    }
    return nodeNames_[static_cast<std::size_t>(node.value)];
}

void CircuitGraph::addResistor(std::string id, NodeId positive, NodeId negative, double resistanceOhm)
{
    resistors_.push_back(Resistor {std::move(id), positive, negative, resistanceOhm});
}

void CircuitGraph::addCapacitor(std::string id, NodeId positive, NodeId negative, double capacitanceFarad)
{
    capacitors_.push_back(Capacitor {std::move(id), positive, negative, capacitanceFarad});
}

void CircuitGraph::addCurrentSource(std::string id, NodeId positive, NodeId negative, double currentAmp)
{
    currentSources_.push_back(CurrentSource {std::move(id), positive, negative, currentAmp});
}

void CircuitGraph::addVoltageSource(std::string id, NodeId positive, NodeId negative, double voltage)
{
    voltageSources_.push_back(VoltageSource {std::move(id), positive, negative, voltage});
}

void CircuitGraph::addDiode(std::string id, NodeId anode, NodeId cathode, DiodeModel model)
{
    diodes_.push_back(Diode {std::move(id), anode, cathode, model});
}

void CircuitGraph::addNpnBjt(std::string id, NodeId collector, NodeId base, NodeId emitter, NpnBjtModel model)
{
    npnBjts_.push_back(NpnBjt {std::move(id), collector, base, emitter, model});
}

void CircuitGraph::addIdealTransformerPort(std::string id,
                                           NodeId primaryPositive,
                                           NodeId primaryNegative,
                                           NodeId secondaryPositive,
                                           NodeId secondaryNegative,
                                           double turnsRatio,
                                           double sourceResistanceOhm,
                                           double loadResistanceOhm)
{
    idealTransformerPorts_.push_back(IdealTransformerPort {
        std::move(id),
        primaryPositive,
        primaryNegative,
        secondaryPositive,
        secondaryNegative,
        turnsRatio,
        sourceResistanceOhm,
        loadResistanceOhm,
        true});
}

int CircuitGraph::nodeCount() const noexcept
{
    return static_cast<int>(nodeNames_.size());
}

int CircuitGraph::unknownCount() const noexcept
{
    return nodeCount() - 1 + static_cast<int>(voltageSources_.size());
}

} // namespace u273::reference::state_space
